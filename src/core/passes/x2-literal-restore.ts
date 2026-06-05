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
  x2NormalizedDecimalRestoreGapIsFreeStanding,
  x2SyncCanExposeContextSensitiveRestore,
  x2ValueSetHasRestoredVisibleDecimal,
  x2ValueSetHasNormalizedDecimalFact,
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

function isPlainDot(op: IrOp | undefined): op is Extract<IrOp, { kind: "plain" }> {
  return op !== undefined &&
    op.kind === "plain" &&
    op.opcode === DOT &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op);
}

function isPlainSignChange(op: IrOp | undefined): op is Extract<IrOp, { kind: "plain" }> {
  return op !== undefined &&
    op.kind === "plain" &&
    op.opcode === SIGN_CHANGE &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasRoles(op);
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
  const fractionDigits: string[] = [];
  const hasPoint = isPlainDot(ops[end]);
  if (hasPoint) {
    end += 1;
    while (end < ops.length) {
      const op = ops[end]!;
      if (!isPlainDigit(op)) break;
      fractionDigits.push(String(op.opcode));
      end += 1;
    }
    if (fractionDigits.length === 0) return undefined;
  }
  if (digits.length + fractionDigits.length > 8) return undefined;
  const sign = isPlainSignChange(ops[end]) ? "-" : "";
  const unsigned = hasPoint ? `${digits.join("")}.${fractionDigits.join("")}` : digits.join("");
  const raw = `${sign}${unsigned}`;
  const normalized = normalizeDecimalMantissaEntry(raw);
  if (normalized === undefined) return undefined;
  const x2Fact = decimalEntryFact(raw);
  if (x2Fact === undefined) return undefined;
  const dotPreservesVpEntrySource = !hasPoint && raw === normalized && normalized !== "0";
  if (sign === "") {
    if (digits.length < 2 && !hasPoint) return undefined;
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
  const fractionDigits: string[] = [];
  const hasPoint = isPlainDot(ops[cursor]);
  if (hasPoint) {
    cursor += 1;
    while (cursor < ops.length) {
      const op = ops[cursor]!;
      if (!isPlainDigit(op)) break;
      fractionDigits.push(String(op.opcode));
      cursor += 1;
    }
    if (fractionDigits.length === 0) return undefined;
  }
  const mantissaSign = isPlainSignChange(ops[cursor]) ? "-" : "";
  if (mantissaSign === "-") cursor += 1;
  if (
    mantissaDigits.length === 0 ||
    mantissaDigits.length + fractionDigits.length > 8 ||
    !isPlainVp(ops[cursor])
  ) return undefined;
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

  const mantissa = hasPoint
    ? `${mantissaSign}${mantissaDigits.join("")}.${fractionDigits.join("")}`
    : `${mantissaSign}${mantissaDigits.join("")}`;
  const value = normalizedExponentEntryValue(mantissa, `${exponentSign}${exponentDigits.join("")}`);
  if (value === undefined) return undefined;
  const dotPreservesVpEntrySource = mantissaDigits[0] !== "0";
  return {
    end: cursor - 1,
    displayValue: value,
    x2Fact: decimalValueFact(value, "normalized"),
    dotPreservesVpEntrySource,
  };
}

function literalRunAt(ops: readonly IrOp[], start: number): NumericLiteralRun | undefined {
  return exponentLiteralRunAt(ops, start) ?? decimalLiteralRunAt(ops, start);
}

function decimalValueFact(value: string, flavor: "normalized" | "unnormalized"): X2ValueFact {
  return `decimal:${value}:${flavor}`;
}

function decimalEntryFact(raw: string): X2ValueFact | undefined {
  const normalized = normalizeDecimalMantissaEntry(raw);
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

function normalizeDecimalMantissaEntry(raw: string): string | undefined {
  const match = /^(-?)([0-9]{1,8})(?:\.([0-9]+))?$/u.exec(raw);
  if (match === null) return normalizeDecimalEntry(raw);
  const digitCount = match[2]!.length + (match[3] ?? "").length;
  if (digitCount > 8) return undefined;
  const sign = match[1]!;
  const integer = match[2]!;
  const fraction = match[3];
  if (fraction === undefined) return normalizeDecimalEntry(`${sign}${integer}`);
  return normalizeSignedPlainDecimal(`${sign}${integer}.${fraction}`);
}

function normalizedExponentEntryValue(mantissa: string, exponent: string): string | undefined {
  const exponentMatch = /^(-?)([0-9]{1,2})$/u.exec(exponent);
  const mantissaParts = exponentMantissaDecimalParts(mantissa);
  if (mantissaParts === undefined || exponentMatch === null) return undefined;
  const shift = Number(exponentMatch[2]!);
  const scale = exponentMatch[1] === "-"
    ? mantissaParts.scale + shift
    : mantissaParts.scale - shift;
  const unsigned = scaledDecimalDigits(mantissaParts.digits, scale);
  const normalized = unsigned === undefined
    ? undefined
    : normalizeSignedPlainDecimal(`${mantissaParts.sign}${unsigned}`);
  if (normalized === undefined || significantDecimalDigits(normalized) > 8) return undefined;
  return normalized;
}

function exponentMantissaDecimalParts(
  mantissa: string,
): { readonly sign: "" | "-"; readonly digits: string; readonly scale: number } | undefined {
  const integer = /^(-?)([0-9]{1,8})$/u.exec(mantissa);
  if (integer !== null) {
    return {
      sign: integer[1]! === "-" ? "-" : "",
      digits: effectiveExponentMantissaDigits(integer[2]!),
      scale: 0,
    };
  }
  const fractional = /^(-?)([0-9]{1,8})\.([0-9]+)$/u.exec(mantissa);
  if (fractional === null) return undefined;
  const integerDigits = fractional[2]!;
  const fractionDigits = fractional[3]!;
  if (integerDigits.length + fractionDigits.length > 8) return undefined;
  return {
    sign: fractional[1]! === "-" ? "-" : "",
    digits: `${integerDigits}${fractionDigits}`,
    scale: fractionDigits.length,
  };
}

function effectiveExponentMantissaDigits(rawDigits: string): string {
  const stripped = rawDigits.replace(/^0+/u, "");
  if (stripped.length > 0) return stripped;
  return `1${"0".repeat(Math.max(0, rawDigits.length - 1))}`;
}

function scaledDecimalDigits(digits: string, scale: number): string | undefined {
  if (!/^\d+$/u.test(digits)) return undefined;
  if (scale <= 0) return `${digits}${"0".repeat(-scale)}`;
  const point = digits.length - scale;
  if (point > 0) return `${digits.slice(0, point)}.${digits.slice(point)}`;
  return `0.${"0".repeat(-point)}${digits}`;
}

function normalizeSignedPlainDecimal(raw: string): string | undefined {
  const match = /^(-?)([0-9]+)(?:\.([0-9]+))?$/u.exec(raw);
  if (match === null) return undefined;
  const sign = match[1]!;
  const integer = match[2]!.replace(/^0+(?=\d)/u, "");
  const fraction = (match[3] ?? "").replace(/0+$/u, "");
  const unsigned = fraction.length === 0 ? integer : `${integer}.${fraction}`;
  if (unsigned === "0") return "0";
  return `${sign}${unsigned}`;
}

function significantDecimalDigits(input: string): number {
  const digits = input.replace(/^-/, "").replace(".", "").replace(/^0+/u, "");
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

function isFreeStandingSignChange(op: IrOp): boolean {
  return op.kind === "plain" &&
    op.opcode === SIGN_CHANGE &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasRoles(op);
}

function isFreeStandingVp(op: IrOp): boolean {
  return isPlainVp(op) && !hasRoles(op);
}

function hasRoles(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return "meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0;
}

function hasOnlyRestoreGapBeforeVp(ops: readonly IrOp[], start: number): boolean {
  let sawRestoreGap = false;
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    if (isFreeStandingEmptyOp(op) || isFreeStandingSignChange(op)) {
      sawRestoreGap = true;
      continue;
    }
    return sawRestoreGap && isFreeStandingVp(op);
  }
  return false;
}

function replacingLiteralCanExposeContextSensitiveRestore(
  ops: readonly IrOp[],
  run: NumericLiteralRun,
): boolean {
  if (!x2SyncCanExposeContextSensitiveRestore(ops, run.end)) return false;
  return !run.dotPreservesVpEntrySource || !hasOnlyRestoreGapBeforeVp(ops, run.end + 1);
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
    const exactX2Fact = runAtIndex === undefined ? false : x2ValueSetHasFact(state?.x2, runAtIndex.x2Fact);
    const visibleDecimalX2Fact = runAtIndex === undefined
      ? false
      : x2ValueSetHasRestoredVisibleDecimal(state?.x2, runAtIndex.x2Fact);
    if (
      runAtIndex !== undefined &&
      isFreshClosedDecimalEntry(state) &&
      (
        x2CanUseDotRestoreAt(ops, index, state, dotSafeStates[index] === true, immediateSyncStates[index] === true) ||
        (
          x2ValueSetHasNormalizedDecimalFact(state?.x2, runAtIndex.x2Fact) &&
          x2NormalizedDecimalRestoreGapIsFreeStanding(ops, index)
        ) ||
        (
          visibleDecimalX2Fact &&
          x2NormalizedDecimalRestoreGapIsFreeStanding(ops, index)
        )
      ) &&
      (exactX2Fact || visibleDecimalX2Fact) &&
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
