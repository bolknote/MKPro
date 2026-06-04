import { getOpcode } from "../opcodes.ts";
import type { IrOp } from "../types.ts";
import {
  computeX2DotRestoreGapStates,
  computeX2ImmediateSyncStates,
  computeX2ValueStates,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  x2ValueSetHasIntersection,
  type IrPass,
  type IrPassFn,
} from "./helpers.ts";

const DOT = 0x0a;
const SIGN_CHANGE = 0x0b;
const VP = 0x0c;

const run: IrPassFn = (ops) => {
  const valueStates = computeX2ValueStates(ops);
  const dotSafeStates = computeX2DotRestoreGapStates(ops);
  const immediateSyncStates = computeX2ImmediateSyncStates(ops);
  const removed = new Set<number>();

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (!isPlainDot(op)) continue;
    if (isDisplayFocusSensitive(op)) continue;
    const state = valueStates[index];
    if (
      dotSafeStates[index] !== true &&
      immediateSyncStates[index] !== true &&
      !isImmediateAfterModeledSignChange(ops, index, state)
    ) continue;
    if (!x2ValueSetHasIntersection(state?.x, state?.x2)) continue;
    if (removingDotCanExposeX2RestoreContext(ops, index)) continue;
    removed.add(index);
  }

  if (removed.size === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  return {
    ops: ops.filter((_, index) => !removed.has(index)),
    applied: removed.size,
    optimizations: [
      {
        name: "x2-noop-restore",
        detail: `Removed ${removed.size} . restore${removed.size === 1 ? "" : "s"} whose X2 value was already in X.`,
      },
    ],
  };
};

function isPlainDot(op: IrOp): op is Extract<IrOp, { kind: "plain" }> {
  return op.kind === "plain" && op.opcode === DOT && !hasRewriteBarrier(op);
}

function isImmediateAfterModeledSignChange(
  ops: readonly IrOp[],
  dotIndex: number,
  state: ReturnType<typeof computeX2ValueStates>[number],
): boolean {
  for (let index = dotIndex - 1; index >= 0; index -= 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    if (hasRewriteBarrier(op)) return false;
    return op.kind === "plain" &&
      op.opcode === SIGN_CHANGE &&
      state?.entry.kind === "closed" &&
      (state.vpContext === undefined || state.vpContext.kind === "none") &&
      x2ValueSetHasIntersection(state.x, state.x2);
  }
  return false;
}

function removingDotCanExposeX2RestoreContext(ops: readonly IrOp[], dotIndex: number): boolean {
  for (let index = dotIndex + 1; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (hasRewriteBarrier(op)) return true;
    switch (op.kind) {
      case "label":
        continue;
      case "plain": {
        if (op.opcode === DOT || op.opcode === SIGN_CHANGE || op.opcode === VP) return true;
        const effect = getOpcode(op.opcode).x2Effect;
        if (effect === "preserves") continue;
        return false;
      }
      case "store":
      case "indirect-store":
      case "orphan-address":
        continue;
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

export const x2NoopRestore: IrPass = {
  name: "x2-noop-restore",
  run,
  layoutSafe: false,
};
