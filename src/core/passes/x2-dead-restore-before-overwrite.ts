import type { IrOp } from "../types.ts";
import {
  analyzeX2StackEffect,
  computeX2DotRestoreGapStates,
  computeX2ImmediateSyncStates,
  computeX2ValueStates,
  directReturnAnalysisContext,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  isKnownReturnCallOp,
  knownReturnCallReturnsThroughNestedTransparentRange,
  plainPreservesXValue,
  removableRecallValueRegister,
  removingRecallCanExposeStackLift,
  removingStackLiftCanExposeStack,
  x2CanUseDotRestoreAt,
  x2StateHasDotSafeDecimalX2,
  x2StateHasOnlyDotSafeStructuralMantissaX2,
  x2StateHasStructuralShapeX2,
  x2StateHasUnsafeDotRestoreShapeX2,
  x2StateHasX2RestoreContext,
  x2StateIsClosedPlainContext,
  x2StateHasVpEntrySource,
  type DirectReturnAnalysisContext,
  type IrPass,
  type IrPassFn,
  type KnownReturnCallOp,
  type X2ValueDataflowState,
} from "./helpers.ts";

const DOT = 0x0a;
const SIGN_CHANGE = 0x0b;
const VP = 0x0c;

const run: IrPassFn = (ops) => {
  if (!ops.some(isPotentialDeadRestoreOrProducerOpcode)) return emptyResult(ops);
  const states = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const dotSafeStates = computeX2DotRestoreGapStates(ops);
  const immediateSyncStates = computeX2ImmediateSyncStates(ops);
  const context = directReturnAnalysisContext(ops);
  const remove = new Set<number>();
  const recallStackExposure = new Map<number, boolean>();
  const stackProducerExposure = new Map<number, boolean>();

  for (let index = 0; index < ops.length; index += 1) {
    if (remove.has(index)) continue;
    const op = ops[index]!;
    if (!isDeadRestoreRecallOrProducerCandidate(
      ops,
      op,
      states[index],
      index,
      dotSafeStates[index] === true,
      immediateSyncStates[index] === true,
      context,
    )) continue;
    const deadRun = deadRestoreRunBeforeHardOverwrite(
      ops,
      states,
      dotSafeStates,
      immediateSyncStates,
      index,
      context,
      recallStackExposure,
      stackProducerExposure,
    );
    if (deadRun === undefined) continue;
    if (isDeadRecallCandidate(op) && removingRecallCanExposeStackLift(ops, index)) continue;
    if (isDeadStackShiftingProducerCandidate(op) && stackShiftCanExposeStack(ops, index, stackProducerExposure)) {
      continue;
    }

    for (const removeIndex of deadRun) remove.add(removeIndex);
  }

  if (remove.size === 0) return emptyResult(ops);
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [
      {
        name: "x2-dead-restore-before-overwrite",
        detail: `Removed ${remove.size} dead X/X2 producer/restore/separator cell(s) overwritten before it can be observed.`,
      },
    ],
  };
};

function isPotentialDeadRestoreOrProducerOpcode(op: IrOp): boolean {
  if (removableRecallValueRegister(op) !== undefined && !isDisplayFocusSensitive(op)) return true;
  if (!isFreeStandingPlain(op)) return false;
  return op.opcode === DOT ||
    op.opcode === SIGN_CHANGE ||
    op.opcode === VP ||
    analyzeX2StackEffect(op).stackShifts;
}

function isDeadRestoreCandidate(
  ops: readonly IrOp[],
  op: IrOp,
  state: X2ValueDataflowState | undefined,
  index: number,
  dotSafe: boolean,
  immediateSync: boolean,
  context: DirectReturnAnalysisContext,
): boolean {
  if (state === undefined || !isFreeStandingPlain(op)) return false;
  if (isDisplayFocusSensitive(op)) return false;
  if (op.opcode === DOT) {
    const hasDotSafeStructuralX2 = x2StateHasOnlyDotSafeStructuralMantissaX2(state);
    if (!hasDotSafeStructuralX2 && x2StateHasUnsafeDotRestoreShapeX2(state)) return false;
    return x2StateIsClosedPlainContext(state) &&
      (
        x2StateHasDotSafeDecimalX2(state) ||
        hasDotSafeStructuralX2 ||
        x2CanUseDotRestoreAt(ops, index, state, dotSafe, immediateSync, context)
      );
  }
  if (op.opcode === SIGN_CHANGE) {
    return isDeadSignRestoreCandidate(state);
  }
  if (op.opcode === VP) {
    return state.entry.kind === "open" ||
      state.entry.kind === "exponent" ||
      (state.entry.kind === "closed" && x2StateHasVpEntrySource(state)) ||
      x2StateHasX2RestoreContext(state);
  }
  return false;
}

function isDeadRestoreRecallOrProducerCandidate(
  ops: readonly IrOp[],
  op: IrOp,
  state: X2ValueDataflowState | undefined,
  index: number,
  dotSafe: boolean,
  immediateSync: boolean,
  context: DirectReturnAnalysisContext,
): boolean {
  return isDeadRestoreCandidate(ops, op, state, index, dotSafe, immediateSync, context) ||
    isDeadRecallCandidate(op) ||
    isDeadStackShiftingProducerCandidate(op);
}

function isDeadRecallCandidate(op: IrOp): boolean {
  if (removableRecallValueRegister(op) === undefined) return false;
  return !isDisplayFocusSensitive(op);
}

function isDeadStackShiftingProducerCandidate(op: IrOp): boolean {
  if (!isFreeStandingPlain(op)) return false;
  if (isDisplayFocusSensitive(op)) return false;
  const effect = analyzeX2StackEffect(op);
  return effect.stackShifts && (effect.x2Affects || effect.x2Preserves);
}

function isDeadSignRestoreCandidate(state: X2ValueDataflowState): boolean {
  return (
    x2StateIsClosedPlainContext(state) &&
    (x2StateHasDotSafeDecimalX2(state) || x2StateHasStructuralShapeX2(state))
  ) ||
    state.entry.kind === "open" ||
    x2StateHasX2RestoreContext(state);
}

function deadRestoreRunBeforeHardOverwrite(
  ops: readonly IrOp[],
  states: readonly (X2ValueDataflowState | undefined)[],
  dotSafeStates: readonly boolean[],
  immediateSyncStates: readonly boolean[],
  start: number,
  context: DirectReturnAnalysisContext,
  recallStackExposure: Map<number, boolean>,
  stackProducerExposure: Map<number, boolean>,
): readonly number[] | undefined {
  const remove: number[] = [start];
  let sameSegment = true;
  for (let index = start + 1; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      sameSegment = false;
      continue;
    }
    if (isFreeStandingEmptyOp(op)) {
      if (sameSegment) remove.push(index);
      continue;
    }
    if (
      sameSegment &&
      isDeadRestoreCandidate(
        ops,
        op,
        states[index],
        index,
        dotSafeStates[index] === true,
        immediateSyncStates[index] === true,
        context,
      )
    ) {
      remove.push(index);
      continue;
    }
    if (isKnownReturnCallOp(op) && simpleDirectReturnDoesNotObserveRestore(ops, op, context)) continue;
    return isOverwriteEndpointThatCannotObserveRestoredX(
      ops,
      index,
      op,
      recallStackExposure,
      stackProducerExposure,
    )
      ? remove
      : undefined;
  }
  return undefined;
}

function isFreeStandingPlain(op: IrOp): op is Extract<IrOp, { kind: "plain" }> {
  return op.kind === "plain" &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasRoles(op);
}

function isFreeStandingEmptyOp(op: IrOp): boolean {
  return isFreeStandingPlain(op) && op.opcode >= 0x54 && op.opcode <= 0x56;
}

function hasRoles(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return "meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0;
}

function isHardX2OverwriteWithoutStackUse(op: IrOp): boolean {
  return analyzeX2StackEffect(op).hardX2OverwriteWithoutStackUse;
}

function isOverwriteEndpointThatCannotObserveRestoredX(
  ops: readonly IrOp[],
  index: number,
  op: IrOp,
  recallStackExposure: Map<number, boolean>,
  stackProducerExposure: Map<number, boolean>,
): boolean {
  if (isHardX2OverwriteWithoutStackUse(op)) return true;
  if (isStackShiftingOverwriteEndpoint(op)) {
    return !stackShiftCanExposeStack(ops, index, stackProducerExposure);
  }
  if (removableRecallValueRegister(op) === undefined || isDisplayFocusSensitive(op)) return false;
  let exposes = recallStackExposure.get(index);
  if (exposes === undefined) {
    exposes = removingRecallCanExposeStackLift(ops, index);
    recallStackExposure.set(index, exposes);
  }
  return !exposes;
}

function isStackShiftingOverwriteEndpoint(op: IrOp): boolean {
  if (!isFreeStandingPlain(op)) return false;
  const effect = analyzeX2StackEffect(op);
  return effect.stackShifts && effect.x2Affects && !plainPreservesXValue(op);
}

function stackShiftCanExposeStack(
  ops: readonly IrOp[],
  index: number,
  stackProducerExposure: Map<number, boolean>,
): boolean {
  let exposes = stackProducerExposure.get(index);
  if (exposes === undefined) {
    exposes = removingStackLiftCanExposeStack(ops, index);
    stackProducerExposure.set(index, exposes);
  }
  return exposes;
}

function simpleDirectReturnDoesNotObserveRestore(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
): boolean {
  return knownReturnCallReturnsThroughNestedTransparentRange(ops, call, context, isRestoreTransparentLinearOp);
}

function isRestoreTransparentLinearOp(op: IrOp): boolean {
  return op.kind === "orphan-address" || isFreeStandingEmptyOp(op);
}

export const x2DeadRestoreBeforeOverwrite: IrPass = {
  name: "x2-dead-restore-before-overwrite",
  run,
  layoutSafe: false,
};
