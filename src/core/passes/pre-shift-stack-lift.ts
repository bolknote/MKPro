import type { IrOp } from "../types.ts";
import {
  analyzeX2StackEffect,
  directReturnAnalysisContext,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  removingRecallCanExposeX2Restore,
  removingPreShiftLiftCanExposeStack,
  removingStackLiftCanExposeStack,
  type IrPass,
  type IrPassFn,
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
    if (previousProducerAlreadySuppliesLiftX2Sync(ops, index)) {
      if (removingStackLiftCanExposeStack(ops, index)) continue;
      remove.add(index);
      continue;
    }
    const producerIndex = x2NextStackShiftingProducerIndex(ops, index + 1, context);
    if (producerIndex !== undefined) {
      if (
        producerIndex === index + 1 &&
        isStackLift(ops[producerIndex]!) &&
        previousProducerAlreadySuppliesLiftX2Sync(ops, producerIndex) &&
        !removingStackLiftCanExposeStack(ops, producerIndex)
      ) continue;
      if (removingPreShiftLiftCanExposeStack(ops, producerIndex)) continue;
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

function previousProducerAlreadySuppliesLiftX2Sync(ops: readonly IrOp[], liftIndex: number): boolean {
  const previous = ops[liftIndex - 1];
  return previous !== undefined &&
    analyzeX2StackEffect(previous).stackLiftAndX2Sync &&
    !isDisplayFocusSensitive(previous);
}

export const preShiftStackLift: IrPass = {
  name: "pre-shift-stack-lift",
  run,
  layoutSafe: false,
};
