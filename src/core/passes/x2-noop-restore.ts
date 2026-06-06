import type { IrOp } from "../types.ts";
import {
  computeX2DotRestoreGapStates,
  computeX2ImmediateSyncStates,
  computeX2ValueStates,
  directReturnAnalysisContext,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  x2CanUseDotRestoreAt,
  x2HasSignRestoreGapBeforeVp,
  x2NormalizedDecimalRestoreGapIsFreeStanding,
  x2SyncCanExposeContextSensitiveRestore,
  x2HasOnlyRestoreGapBeforeVp,
  x2StateHasSameDotRestoreValueInXAndX2,
  x2StateHasSameNormalizedDecimalInXAndX2,
  x2StateHasSameRestoredVisibleDecimalInXAndX2,
  x2StateCanDiscardRestoreRunBeforeProvedVp,
  x2StateHasOnlyDotSafeStructuralMantissaX2,
  x2StateHasUnsafeDotRestoreShapeX2,
  x2StateIsClosedPlainContext,
  x2StatesHaveSameVpEntrySignSource,
  type DirectReturnAnalysisContext,
  type IrPass,
  type IrPassFn,
  type X2ValueDataflowState,
} from "./helpers.ts";

const DOT = 0x0a;

const run: IrPassFn = (ops) => {
  const valueStates = computeX2ValueStates(ops);
  const dotSafeStates = computeX2DotRestoreGapStates(ops);
  const immediateSyncStates = computeX2ImmediateSyncStates(ops);
  const directReturnContext = directReturnAnalysisContext(ops);
  const removed = new Set<number>();

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (!isPlainDot(op)) continue;
    if (isDisplayFocusSensitive(op)) continue;
    const state = valueStates[index];
    if (
      !x2CanUseDotRestoreAt(
        ops,
        index,
        state,
        dotSafeStates[index] === true,
        immediateSyncStates[index] === true,
        directReturnContext,
      ) &&
      !(
        x2StateHasSameNormalizedDecimalInXAndX2(state) &&
        x2NormalizedDecimalRestoreGapIsFreeStanding(ops, index, directReturnContext)
      ) &&
      !(
        x2StateHasSameRestoredVisibleDecimalInXAndX2(state) &&
        x2NormalizedDecimalRestoreGapIsFreeStanding(ops, index, directReturnContext)
      )
    ) continue;
    if (!isDotSafeValueContext(state)) continue;
    if (
      x2StateHasUnsafeDotRestoreShapeX2(state) &&
      !x2StateHasOnlyDotSafeStructuralMantissaX2(state)
    ) continue;
    if (
      !x2StateHasSameDotRestoreValueInXAndX2(state) &&
      !x2StateHasSameRestoredVisibleDecimalInXAndX2(state)
    ) continue;
    if (dotCanExposeContextSensitiveRestore(ops, index, state, valueStates[index + 1], directReturnContext)) continue;
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

function dotCanExposeContextSensitiveRestore(
  ops: readonly IrOp[],
  index: number,
  state: X2ValueDataflowState | undefined,
  stateAfterDot: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext,
): boolean {
  if (!x2SyncCanExposeContextSensitiveRestore(ops, index)) return false;
  return !dotPreservesVpEntrySourceThroughRestoreGap(ops, index, state, stateAfterDot, context);
}

function dotPreservesVpEntrySourceThroughRestoreGap(
  ops: readonly IrOp[],
  index: number,
  state: X2ValueDataflowState | undefined,
  stateAfterDot: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext,
): boolean {
  if (!x2HasOnlyRestoreGapBeforeVp(ops, index + 1, context)) return false;
  return x2StateCanDiscardRestoreRunBeforeProvedVp(state, stateAfterDot) ||
    (
      x2HasSignRestoreGapBeforeVp(ops, index + 1, context) &&
      x2StatesHaveSameVpEntrySignSource(state, stateAfterDot)
    );
}

function isDotSafeValueContext(
  state: ReturnType<typeof computeX2ValueStates>[number],
): boolean {
  return x2StateIsClosedPlainContext(state);
}

export const x2NoopRestore: IrPass = {
  name: "x2-noop-restore",
  run,
  layoutSafe: false,
};
