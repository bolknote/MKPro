import type { IrOp } from "../types.ts";
import {
  computeX2DotRestoreGapStates,
  computeX2ImmediateSyncStates,
  computeX2ValueStates,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  replacingNumberEntryCanExposeStackLift,
  x2CanUseDotRestoreAt,
  x2SyncCanExposeContextSensitiveRestore,
  type IrPass,
  type IrPassFn,
  type X2ValueDataflowState,
  type X2ValueFact,
  type X2ValueSet,
} from "./helpers.ts";

const DOT = 0x0a;
const SIGN_CHANGE = 0x0b;
const VP = 0x0c;

interface NumericLiteralRun {
  readonly end: number;
  readonly displayValue: string;
  readonly x2Fact: X2ValueFact;
  readonly dotPreservesVpEntrySource: boolean;
}

function isPlainDigit(op: IrOp): op is Extract<IrOp, { kind: "plain" }> {
  return op.kind === "plain" &&
    op.opcode >= 0 &&
    op.opcode <= 9 &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op);
}

function isPlainSignChange(op: IrOp | undefined): op is Extract<IrOp, { kind: "plain" }> {
  return op !== undefined &&
    op.kind === "plain" &&
    op.opcode === SIGN_CHANGE &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op);
}

function isPlainVp(op: IrOp | undefined): op is Extract<IrOp, { kind: "plain" }> {
  return op !== undefined &&
    op.kind === "plain" &&
    op.opcode === VP &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op);
}

function isFreshClosedDecimalEntry(state: X2ValueDataflowState | undefined): boolean {
  return state?.entry.kind === "closed" && (state.vpContext === undefined || state.vpContext.kind === "none");
}

function decimalLiteralRunAt(ops: readonly IrOp[], start: number): NumericLiteralRun | undefined {
  const digits: string[] = [];
  let end = start;
  while (end < ops.length) {
    const op = ops[end]!;
    if (!isPlainDigit(op)) break;
    digits.push(String(op.opcode));
    end += 1;
  }
  if (digits.length === 0) return undefined;
  if (digits.length > 8) return undefined;
  const sign = isPlainSignChange(ops[end]) ? "-" : "";
  const raw = `${sign}${digits.join("")}`;
  const normalized = normalizeDecimalEntry(raw);
  if (normalized === undefined) return undefined;
  const x2Fact = decimalEntryFact(raw);
  if (x2Fact === undefined) return undefined;
  const dotPreservesVpEntrySource = raw === normalized && normalized !== "0";
  if (sign === "") {
    if (digits.length < 2) return undefined;
    return { end: end - 1, displayValue: raw, x2Fact, dotPreservesVpEntrySource };
  }
  return { end, displayValue: raw, x2Fact, dotPreservesVpEntrySource };
}

function exponentLiteralRunAt(ops: readonly IrOp[], start: number): NumericLiteralRun | undefined {
  const mantissaDigits: string[] = [];
  let cursor = start;
  while (cursor < ops.length) {
    const op = ops[cursor]!;
    if (!isPlainDigit(op)) break;
    mantissaDigits.push(String(op.opcode));
    cursor += 1;
  }
  const mantissaSign = isPlainSignChange(ops[cursor]) ? "-" : "";
  if (mantissaSign === "-") cursor += 1;
  if (mantissaDigits.length === 0 || !isPlainVp(ops[cursor])) return undefined;
  cursor += 1;

  const exponentDigits: string[] = [];
  while (cursor < ops.length) {
    const op = ops[cursor]!;
    if (!isPlainDigit(op)) break;
    exponentDigits.push(String(op.opcode));
    cursor += 1;
  }
  if (exponentDigits.length === 0 || exponentDigits.length > 2) return undefined;
  const exponentSign = isPlainSignChange(ops[cursor]) ? "-" : "";
  if (exponentSign === "-") cursor += 1;

  const value = normalizedExponentEntryValue(`${mantissaSign}${mantissaDigits.join("")}`, `${exponentSign}${exponentDigits.join("")}`);
  if (value === undefined) return undefined;
  return {
    end: cursor - 1,
    displayValue: value,
    x2Fact: decimalValueFact(value, "normalized"),
    dotPreservesVpEntrySource: false,
  };
}

function literalRunAt(ops: readonly IrOp[], start: number): NumericLiteralRun | undefined {
  return exponentLiteralRunAt(ops, start) ?? decimalLiteralRunAt(ops, start);
}

function decimalValueFact(value: string, flavor: "normalized" | "unnormalized"): X2ValueFact {
  return `decimal:${value}:${flavor}`;
}

function decimalEntryFact(raw: string): X2ValueFact | undefined {
  const normalized = normalizeDecimalEntry(raw);
  if (normalized === undefined) return undefined;
  return decimalValueFact(raw === normalized ? raw : raw, raw === normalized ? "normalized" : "unnormalized");
}

function normalizeDecimalEntry(raw: string): string | undefined {
  const match = /^(-?)([0-9]{1,8})$/u.exec(raw);
  if (match === null) return undefined;
  const sign = match[1]!;
  const digits = match[2]!.replace(/^0+(?=\d)/u, "");
  if (digits === "0") return "0";
  return `${sign}${digits}`;
}

function normalizedExponentEntryValue(mantissa: string, exponent: string): string | undefined {
  const mantissaMatch = /^(-?)([0-9]{1,8})$/u.exec(mantissa);
  const exponentMatch = /^(-?)([0-9]{1,2})$/u.exec(exponent);
  if (mantissaMatch === null || exponentMatch === null) return undefined;
  const sign = mantissaMatch[1]!;
  const digits = effectiveExponentMantissaDigits(mantissaMatch[2]!);
  const shift = Number(exponentMatch[2]!);
  const normalized = exponentMatch[1] === "-"
    ? decimalShiftRight(digits, shift)
    : `${digits}${"0".repeat(shift)}`;
  if (normalized === undefined || significantDecimalDigits(normalized) > 8) return undefined;
  return `${sign}${normalized}`;
}

function effectiveExponentMantissaDigits(rawDigits: string): string {
  const stripped = rawDigits.replace(/^0+/u, "");
  if (stripped.length > 0) return stripped;
  return `1${"0".repeat(Math.max(0, rawDigits.length - 1))}`;
}

function decimalShiftRight(digits: string, places: number): string | undefined {
  const point = digits.length - places;
  const raw = point > 0
    ? `${digits.slice(0, point)}.${digits.slice(point)}`
    : `0.${"0".repeat(-point)}${digits}`;
  return normalizePlainDecimal(raw);
}

function normalizePlainDecimal(raw: string): string | undefined {
  const match = /^(0|[1-9][0-9]*)(?:\.([0-9]+))?$/u.exec(raw);
  if (match === null) return undefined;
  const integer = match[1]!.replace(/^0+(?=\d)/u, "");
  const fraction = (match[2] ?? "").replace(/0+$/u, "");
  return fraction.length === 0 ? integer : `${integer}.${fraction}`;
}

function significantDecimalDigits(input: string): number {
  const digits = input.replace(".", "").replace(/^0+/u, "");
  return digits.length === 0 ? 1 : digits.length;
}

function x2ValueSetHasFact(input: X2ValueSet | undefined, fact: X2ValueFact): boolean {
  return input?.has(fact) === true;
}

function isFreeStandingEmptyOp(op: IrOp): boolean {
  return op.kind === "plain" &&
    op.opcode >= 0x54 &&
    op.opcode <= 0x56 &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasRoles(op);
}

function hasRoles(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return "meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0;
}

function hasOnlyEmptyGapBeforeVp(ops: readonly IrOp[], start: number): boolean {
  let sawEmpty = false;
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    if (isFreeStandingEmptyOp(op)) {
      sawEmpty = true;
      continue;
    }
    return sawEmpty && isPlainVp(op);
  }
  return false;
}

function replacingLiteralCanExposeContextSensitiveRestore(
  ops: readonly IrOp[],
  run: NumericLiteralRun,
): boolean {
  if (!x2SyncCanExposeContextSensitiveRestore(ops, run.end)) return false;
  return !run.dotPreservesVpEntrySource || !hasOnlyEmptyGapBeforeVp(ops, run.end + 1);
}

function dotRestoreOp(value: string, source: IrOp): IrOp {
  const sourceComment = "meta" in source ? source.meta.comment : undefined;
  return {
    kind: "plain",
    opcode: DOT,
    meta: {
      mnemonic: ".",
      comment: [sourceComment, `restore literal ${value} from hidden X2 temp`].filter(Boolean).join("; "),
    },
  };
}

const run: IrPassFn = (ops) => {
  const x2ValueStates = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const dotSafeStates = computeX2DotRestoreGapStates(ops);
  const immediateSyncStates = computeX2ImmediateSyncStates(ops);
  const result: IrOp[] = [];
  let removed = 0;

  for (let index = 0; index < ops.length; index += 1) {
    const state = x2ValueStates[index];
    const runAtIndex = literalRunAt(ops, index);
    if (
      runAtIndex !== undefined &&
      isFreshClosedDecimalEntry(state) &&
      x2CanUseDotRestoreAt(ops, index, state, dotSafeStates[index] === true, immediateSyncStates[index] === true) &&
      x2ValueSetHasFact(state?.x2, runAtIndex.x2Fact) &&
      !replacingNumberEntryCanExposeStackLift(ops, runAtIndex.end) &&
      !replacingLiteralCanExposeContextSensitiveRestore(ops, runAtIndex)
    ) {
      result.push(dotRestoreOp(runAtIndex.displayValue, ops[index]!));
      removed += runAtIndex.end - index;
      index = runAtIndex.end;
      continue;
    }
    result.push(ops[index]!);
  }

  if (removed === 0) return emptyResult(ops);
  return {
    ops: result,
    applied: removed,
    optimizations: [
      {
        name: "x2-literal-restore",
        detail: `Replaced ${removed} repeated numeric literal cell${removed === 1 ? "" : "s"} with hidden X2 . restore(s).`,
      },
    ],
  };
};

export const x2LiteralRestore: IrPass = {
  name: "x2-literal-restore",
  run,
  layoutSafe: false,
};
