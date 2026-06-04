import type { IrOp } from "../types.ts";
import {
  analyzeX2StackEffect,
  emptyResult,
  hasRewriteBarrier,
  removingRecallCanExposeX2Restore,
  removingPreShiftLiftCanExposeStack,
  removingStackLiftCanExposeStack,
  type IrPass,
  type IrPassFn,
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
  return isStackPreservingGapOp(op) || op.kind === "cjump" || op.kind === "loop";
}

function nextStackShiftingProducerIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isStackShiftingProducer(op)) return index;
    if (!isFallthroughStackPreservingGapOp(op)) return undefined;
  }
  return undefined;
}

function isHardX2Overwrite(op: IrOp | undefined): boolean {
  return analyzeX2StackEffect(op).hardX2OverwriteWithoutStackUse;
}

function nextHardX2OverwriteIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isHardX2Overwrite(op)) return index;
    if (!isFallthroughStackPreservingGapOp(op)) return undefined;
  }
  return undefined;
}

const run: IrPassFn = (ops) => {
  const remove = new Set<number>();
  for (let index = 0; index < ops.length - 1; index += 1) {
    const op = ops[index]!;
    if (!isStackLift(op)) continue;
    const producerIndex = nextStackShiftingProducerIndex(ops, index + 1);
    if (producerIndex !== undefined) {
      if (removingPreShiftLiftCanExposeStack(ops, producerIndex)) continue;
      if (removingStackLiftCanExposeStack(ops, index)) continue;
      if (removingRecallCanExposeX2Restore(ops, index)) continue;
      remove.add(index);
      continue;
    }
    if (nextHardX2OverwriteIndex(ops, index + 1) === undefined) continue;
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
