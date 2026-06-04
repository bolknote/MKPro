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

interface DecimalLiteralRun {
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

function isFreshClosedDecimalEntry(state: X2ValueDataflowState | undefined): boolean {
  return state?.entry.kind === "closed" && (state.vpContext === undefined || state.vpContext.kind === "none");
}

function decimalLiteralRunAt(ops: readonly IrOp[], start: number): DecimalLiteralRun | undefined {
  const digits: string[] = [];
  let end = start;
  while (end < ops.length) {
    const op = ops[end]!;
    if (!isPlainDigit(op)) break;
    digits.push(String(op.opcode));
    end += 1;
  }
  if (digits.length < 2) return undefined;
  const value = digits.join("");
  if (!/^[1-9][0-9]{1,7}$/u.test(value)) return undefined;
  return { end: end - 1, value };
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
        if (op.opcode === DOT || op.opcode === 0x0b || op.opcode === 0x0c) return true;
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
    const runAtIndex = decimalLiteralRunAt(ops, index);
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
        detail: `Replaced ${removed} repeated decimal literal cell${removed === 1 ? "" : "s"} with hidden X2 . restore(s).`,
      },
    ],
  };
};

export const x2LiteralRestore: IrPass = {
  name: "x2-literal-restore",
  run,
  layoutSafe: false,
};
