import type { IrOp } from "../types.ts";
import {
  computeX2DotRestoreGapStates,
  computeX2ImmediateSyncStates,
  computeX2ValueStates,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  x2SyncCanExposeContextSensitiveRestore,
  x2StateHasSameDotSafeDecimalInXAndX2,
  x2StateIsClosedPlainContext,
  x2ValueSetHasIntersection,
  type IrPass,
  type IrPassFn,
} from "./helpers.ts";

const DOT = 0x0a;
const SIGN_CHANGE = 0x0b;

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
    if (!isDotSafeValueContext(state)) continue;
    if (!x2ValueSetHasIntersection(state?.x, state?.x2) && !x2StateHasSameDotSafeDecimalInXAndX2(state)) continue;
    if (x2SyncCanExposeContextSensitiveRestore(ops, index)) continue;
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

function isDotSafeValueContext(
  state: ReturnType<typeof computeX2ValueStates>[number],
): boolean {
  return x2StateIsClosedPlainContext(state);
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
      x2StateIsClosedPlainContext(state) &&
      (x2ValueSetHasIntersection(state?.x, state?.x2) || x2StateHasSameDotSafeDecimalInXAndX2(state));
  }
  return false;
}

export const x2NoopRestore: IrPass = {
  name: "x2-noop-restore",
  run,
  layoutSafe: false,
};
