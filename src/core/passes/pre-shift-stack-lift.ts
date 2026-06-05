import type { IrOp } from "../types.ts";
import {
  analyzeX2StackEffect,
  directReturnAnalysisContext,
  emptyResult,
  hasRewriteBarrier,
  isKnownReturnCallOp,
  knownIndirectFlowTarget,
  knownReturnCallReturnsThroughTransparentRange,
  removingRecallCanExposeX2Restore,
  removingPreShiftLiftCanExposeStack,
  removingStackLiftCanExposeStack,
  type DirectReturnAnalysisContext,
  type IrPass,
  type IrPassFn,
  type KnownReturnCallOp,
} from "./helpers.ts";

function isStackLift(op: IrOp): boolean {
  return op.kind === "plain" && op.opcode === 0x0e && !hasRewriteBarrier(op);
}

function isStackShiftingProducer(op: IrOp | undefined): boolean {
  return analyzeX2StackEffect(op).stackShifts;
}

function isStackPreservingGapOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  switch (op.kind) {
    case "label":
    case "store":
    case "indirect-store":
    case "orphan-address":
      return true;
    case "plain":
      return analyzeX2StackEffect(op).stackPreserves;
    default:
      return false;
  }
}

function isFallthroughStackPreservingGapOp(op: IrOp): boolean {
  return isStackPreservingGapOp(op) ||
    op.kind === "cjump" ||
    op.kind === "loop" ||
    isKnownIndirectFallthroughStackPreservingConditional(op);
}

function isKnownIndirectFallthroughStackPreservingConditional(op: IrOp): boolean {
  return op.kind === "indirect-cjump" &&
    knownIndirectFlowTarget(op) !== undefined &&
    !hasRewriteBarrier(op) &&
    analyzeX2StackEffect(op).stackPreserves;
}

function nextStackShiftingProducerIndex(
  ops: readonly IrOp[],
  start: number,
  context: DirectReturnAnalysisContext,
): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isStackShiftingProducer(op)) return index;
    if (isKnownReturnCallOp(op) && simpleDirectReturnPreservesStack(ops, op, context)) continue;
    if (!isFallthroughStackPreservingGapOp(op)) return undefined;
  }
  return undefined;
}

function isHardX2Overwrite(op: IrOp | undefined): boolean {
  return analyzeX2StackEffect(op).hardX2OverwriteWithoutStackUse;
}

function nextHardX2OverwriteIndex(
  ops: readonly IrOp[],
  start: number,
  context: DirectReturnAnalysisContext,
): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isHardX2Overwrite(op)) return index;
    if (isKnownReturnCallOp(op) && simpleDirectReturnPreservesStack(ops, op, context)) continue;
    if (!isFallthroughStackPreservingGapOp(op)) return undefined;
  }
  return undefined;
}

const run: IrPassFn = (ops) => {
  const remove = new Set<number>();
  const context = directReturnAnalysisContext(ops);
  for (let index = 0; index < ops.length - 1; index += 1) {
    const op = ops[index]!;
    if (!isStackLift(op)) continue;
    const producerIndex = nextStackShiftingProducerIndex(ops, index + 1, context);
    if (producerIndex !== undefined) {
      if (removingPreShiftLiftCanExposeStack(ops, producerIndex)) continue;
      if (removingStackLiftCanExposeStack(ops, index)) continue;
      if (removingRecallCanExposeX2Restore(ops, index)) continue;
      remove.add(index);
      continue;
    }
    if (nextHardX2OverwriteIndex(ops, index + 1, context) === undefined) continue;
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

export const preShiftStackLift: IrPass = {
  name: "pre-shift-stack-lift",
  run,
  layoutSafe: false,
};

function simpleDirectReturnPreservesStack(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
): boolean {
  return knownReturnCallReturnsThroughTransparentRange(ops, call, context, isStrictStackPreservingLinearOp);
}

function isStrictStackPreservingLinearOp(op: IrOp): boolean {
  switch (op.kind) {
    case "label":
    case "store":
    case "indirect-store":
    case "orphan-address":
      return true;
    case "plain":
      return analyzeX2StackEffect(op).stackPreserves;
    default:
      return false;
  }
}
