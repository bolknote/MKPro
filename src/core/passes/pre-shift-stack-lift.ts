import type { IrOp } from "../types.ts";
import { getOpcode } from "../opcodes.ts";
import {
  analyzeX2StackEffect,
  directReturnAnalysisContext,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  isKnownReturnCallOp,
  knownIndirectFlowTarget,
  knownReturnCallReturnsThroughTransparentRange,
  plainPreservesXValue,
  removingRecallCanExposeX2Restore,
  removingPreShiftLiftCanExposeStack,
  removingStackLiftCanExposeStack,
  type DirectReturnAnalysisContext,
  type IrPass,
  type IrPassFn,
  type KnownReturnCallOp,
  x2NextHardX2OverwriteIndex,
  x2NextStackShiftingProducerIndex,
} from "./helpers.ts";

function isStackLift(op: IrOp): boolean {
  return op.kind === "plain" && op.opcode === 0x0e && !hasRewriteBarrier(op);
}

const run: IrPassFn = (ops) => {
  const remove = new Set<number>();
  const context = directReturnAnalysisContext(ops);
  for (let index = 0; index < ops.length - 1; index += 1) {
    const op = ops[index]!;
    if (!isStackLift(op)) continue;
    if (previousProducerAlreadySuppliesLiftX2Sync(ops, index, context)) {
      if (removingStackLiftCanExposeStack(ops, index)) continue;
      remove.add(index);
      continue;
    }
    const producerIndex = x2NextStackShiftingProducerIndex(ops, index + 1, context);
    if (producerIndex !== undefined) {
      if (
        producerIndex === index + 1 &&
        isStackLift(ops[producerIndex]!) &&
        previousProducerAlreadySuppliesLiftX2Sync(ops, producerIndex, context) &&
        !removingStackLiftCanExposeStack(ops, producerIndex)
      ) continue;
      if (removingPreShiftLiftCanExposeStack(ops, producerIndex)) continue;
      if (removingStackLiftCanExposeStack(ops, index)) continue;
      if (removingRecallCanExposeX2Restore(ops, index)) continue;
      remove.add(index);
      continue;
    }
    if (x2NextFallthroughSyncConditionalIndex(ops, index + 1, context) !== undefined) {
      if (removingStackLiftCanExposeStack(ops, index)) continue;
      if (removingRecallCanExposeX2Restore(ops, index)) continue;
      remove.add(index);
      continue;
    }
    if (x2NextDirectReturnSyncIndex(ops, index + 1, context) !== undefined) {
      if (removingStackLiftCanExposeStack(ops, index)) continue;
      if (removingRecallCanExposeX2Restore(ops, index)) continue;
      remove.add(index);
      continue;
    }
    if (x2NextHardX2OverwriteIndex(ops, index + 1, context) === undefined) continue;
    if (removingStackLiftCanExposeStack(ops, index)) continue;
    if (removingRecallCanExposeX2Restore(ops, index)) continue;
    remove.add(index);
  }

  if (remove.size === 0) return emptyResult(ops);
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [{
      name: "pre-shift-stack-lift",
      detail: `Removed ${remove.size} В↑ lift${remove.size === 1 ? "" : "s"} already supplied by a following stack-shifting command or made dead before a hard X2 overwrite.`,
    }],
  };
};

function previousProducerAlreadySuppliesLiftX2Sync(
  ops: readonly IrOp[],
  liftIndex: number,
  context: DirectReturnAnalysisContext,
): boolean {
  for (let index = liftIndex - 1; index >= 0; index -= 1) {
    const previous = ops[index]!;
    if (producerSuppliesLiftX2Sync(previous)) return true;
    if (!isBackwardStackLiftX2SyncGap(ops, previous, index, context)) return false;
  }
  return false;
}

function producerSuppliesLiftX2Sync(op: IrOp): boolean {
  return !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    analyzeX2StackEffect(op).stackLiftAndX2Sync;
}

function x2NextFallthroughSyncConditionalIndex(
  ops: readonly IrOp[],
  start: number,
  context: DirectReturnAnalysisContext,
): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isFallthroughX2SyncConditional(op)) return index;
    if (!isBackwardStackLiftX2SyncGap(ops, op, index, context)) return undefined;
  }
  return undefined;
}

function x2NextDirectReturnSyncIndex(
  ops: readonly IrOp[],
  start: number,
  context: DirectReturnAnalysisContext,
): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (
      isKnownReturnCallOp(op) &&
      !hasRewriteBarrier(op) &&
      !isDisplayFocusSensitive(op) &&
      directReturnPreservesStackXAndX2(ops, op, context)
    ) {
      return index;
    }
    if (!isBackwardStackLiftX2SyncGap(ops, op, index, context)) return undefined;
  }
  return undefined;
}

function isFallthroughX2SyncConditional(op: IrOp): boolean {
  if (op.kind !== "cjump" && op.kind !== "loop") return false;
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  const effect = analyzeX2StackEffect(op);
  if (!effect.stackPreserves) return false;
  const conditional = getOpcode(op.opcode).conditionalX2Effect;
  return conditional?.fallthrough === "affects" && conditional.jump === "preserves";
}

function isBackwardStackLiftX2SyncGap(
  ops: readonly IrOp[],
  op: IrOp,
  index: number,
  context: DirectReturnAnalysisContext,
): boolean {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  if (op.kind === "label") return !context.labelEntries.has(index);
  if (isKnownReturnCallOp(op)) return directReturnPreservesStackXAndX2(ops, op, context);
  if (isKnownFallthroughStackX2Gap(op)) return true;
  switch (op.kind) {
    case "store":
    case "indirect-store":
    case "orphan-address":
      return true;
    case "plain": {
      const effect = analyzeX2StackEffect(op);
      return effect.stackPreserves && effect.x2Preserves && plainPreservesXValue(op);
    }
    default:
      return false;
  }
}

function isKnownFallthroughStackX2Gap(op: IrOp): boolean {
  if (op.kind !== "cjump" && op.kind !== "loop" && op.kind !== "indirect-cjump") return false;
  if (op.kind === "indirect-cjump" && knownIndirectFlowTarget(op) === undefined) return false;
  const effect = analyzeX2StackEffect(op);
  if (!effect.stackPreserves) return false;
  const fallthroughX2 = getOpcode(op.opcode).conditionalX2Effect?.fallthrough;
  return fallthroughX2 === "affects" || fallthroughX2 === "preserves";
}

function directReturnPreservesStackXAndX2(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
): boolean {
  return knownReturnCallReturnsThroughTransparentRange(
    ops,
    call,
    context,
    (op) => isLinearStackXAndX2PreservingOp(op),
  );
}

function isLinearStackXAndX2PreservingOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  switch (op.kind) {
    case "label":
    case "store":
    case "indirect-store":
    case "orphan-address":
      return true;
    case "plain": {
      const effect = analyzeX2StackEffect(op);
      return effect.stackPreserves && effect.x2Preserves && plainPreservesXValue(op);
    }
    default:
      return false;
  }
}

export const preShiftStackLift: IrPass = {
  name: "pre-shift-stack-lift",
  run,
  layoutSafe: false,
};
