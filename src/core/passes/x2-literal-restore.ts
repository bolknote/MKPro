import type { IrOp } from "../types.ts";
import { getOpcode } from "../opcodes.ts";
import {
  computeX2DotRestoreGapStates,
  computeX2ValueStates,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  replacingNumberEntryCanExposeStackLift,
  x2ValueSetHasNormalizedDecimal,
  type IrPass,
  type IrPassFn,
  type X2ValueDataflowState,
} from "./helpers.ts";

const DOT = 0x0a;
const SIGN_CHANGE = 0x0b;
const VP = 0x0c;

interface NumericLiteralRun {
  readonly end: number;
  readonly value: string;
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
  const value = digits.join("");
  if (!/^[1-9][0-9]{0,7}$/u.test(value)) return undefined;
  if (isPlainSignChange(ops[end])) return { end, value: `-${value}` };
  if (digits.length < 2) return undefined;
  return { end: end - 1, value };
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
  return { end: cursor - 1, value };
}

function literalRunAt(ops: readonly IrOp[], start: number): NumericLiteralRun | undefined {
  return exponentLiteralRunAt(ops, start) ?? decimalLiteralRunAt(ops, start);
}

function normalizedExponentEntryValue(mantissa: string, exponent: string): string | undefined {
  const mantissaMatch = /^(-?)([1-9][0-9]{0,7})$/u.exec(mantissa);
  const exponentMatch = /^(-?)([0-9]{1,2})$/u.exec(exponent);
  if (mantissaMatch === null || exponentMatch === null) return undefined;
  const sign = mantissaMatch[1]!;
  const digits = mantissaMatch[2]!;
  const shift = Number(exponentMatch[2]!);
  const normalized = exponentMatch[1] === "-"
    ? decimalShiftRight(digits, shift)
    : `${digits}${"0".repeat(shift)}`;
  if (normalized === undefined || significantDecimalDigits(normalized) > 8) return undefined;
  return `${sign}${normalized}`;
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

function addingDotCanExposeX2RestoreContext(ops: readonly IrOp[], literalEnd: number): boolean {
  for (let index = literalEnd + 1; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (hasRewriteBarrier(op)) return true;
    switch (op.kind) {
      case "label":
      case "store":
      case "indirect-store":
      case "orphan-address":
        continue;
      case "plain":
        if (op.opcode === DOT || op.opcode === SIGN_CHANGE || op.opcode === 0x0c) return true;
        if (getOpcode(op.opcode).x2Effect === "preserves") continue;
        return false;
      case "jump":
      case "cjump":
      case "call":
      case "loop":
      case "indirect-jump":
      case "indirect-call":
      case "indirect-cjump":
        return true;
      case "recall":
      case "indirect-recall":
      case "return":
      case "stop":
        return false;
    }
  }
  return false;
}

function isImmediateAfterX2AffectingSync(ops: readonly IrOp[], index: number): boolean {
  for (let cursor = index - 1; cursor >= 0; cursor -= 1) {
    const op = ops[cursor]!;
    if (op.kind === "label") continue;
    if (hasRewriteBarrier(op)) return false;
    if (op.kind === "cjump" || op.kind === "loop" || op.kind === "indirect-cjump") {
      return getOpcode(op.opcode).conditionalX2Effect?.fallthrough === "affects";
    }
    return op.kind === "plain" && getOpcode(op.opcode).x2Effect === "affects";
  }
  return false;
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
  const x2ValueStates = computeX2ValueStates(ops);
  const dotSafeStates = computeX2DotRestoreGapStates(ops);
  const result: IrOp[] = [];
  let removed = 0;

  for (let index = 0; index < ops.length; index += 1) {
    const state = x2ValueStates[index];
    const runAtIndex = literalRunAt(ops, index);
    if (
      runAtIndex !== undefined &&
      isFreshClosedDecimalEntry(state) &&
      (dotSafeStates[index] === true || isImmediateAfterX2AffectingSync(ops, index)) &&
      x2ValueSetHasNormalizedDecimal(state?.x2, runAtIndex.value) &&
      !replacingNumberEntryCanExposeStackLift(ops, runAtIndex.end) &&
      !addingDotCanExposeX2RestoreContext(ops, runAtIndex.end)
    ) {
      result.push(dotRestoreOp(runAtIndex.value, ops[index]!));
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
