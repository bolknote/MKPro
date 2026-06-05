import type { IrOp } from "../types.ts";
import {
  analyzeX2StackEffect,
  cellsPerOp,
  computeLabelEntryIndexes,
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

interface DirectReturnGapContext {
  readonly labelEntries: ReadonlySet<number>;
  readonly labels: ReadonlyMap<string, number>;
  readonly addresses: ReadonlyMap<number, number>;
}

function nextStackShiftingProducerIndex(
  ops: readonly IrOp[],
  start: number,
  context: DirectReturnGapContext,
): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isStackShiftingProducer(op)) return index;
    if (op.kind === "call" && simpleDirectReturnPreservesStack(ops, op, context)) continue;
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
  context: DirectReturnGapContext,
): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isHardX2Overwrite(op)) return index;
    if (op.kind === "call" && simpleDirectReturnPreservesStack(ops, op, context)) continue;
    if (!isFallthroughStackPreservingGapOp(op)) return undefined;
  }
  return undefined;
}

const run: IrPassFn = (ops) => {
  const remove = new Set<number>();
  const context: DirectReturnGapContext = {
    labelEntries: computeLabelEntryIndexes(ops),
    labels: labelIndexes(ops),
    addresses: addressIndexes(ops),
  };
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
  call: Extract<IrOp, { kind: "call" }>,
  context: DirectReturnGapContext,
): boolean {
  const targetIndex = targetIndexForCall(call, context);
  if (targetIndex === undefined) return false;
  return linearReturnRangePreservesStack(ops, targetIndex, context.labelEntries);
}

function targetIndexForCall(
  call: Extract<IrOp, { kind: "call" }>,
  context: DirectReturnGapContext,
): number | undefined {
  return typeof call.target === "string"
    ? context.labels.get(call.target)
    : context.addresses.get(call.target);
}

function linearReturnRangePreservesStack(
  ops: readonly IrOp[],
  targetIndex: number,
  labelEntries: ReadonlySet<number>,
): boolean {
  const startIndex = ops[targetIndex]?.kind === "label" ? targetIndex + 1 : targetIndex;
  for (let index = startIndex; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      if (labelEntries.has(index)) return false;
      continue;
    }
    if (hasRewriteBarrier(op)) return false;
    if (op.kind === "return") return true;
    if (!isStrictStackPreservingLinearOp(op)) return false;
  }
  return false;
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

function labelIndexes(ops: readonly IrOp[]): Map<string, number> {
  const result = new Map<string, number>();
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") result.set(op.name, index);
  }
  return result;
}

function addressIndexes(ops: readonly IrOp[]): Map<number, number> {
  const result = new Map<number, number>();
  let address = 0;
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    result.set(address, index);
    address += cellsPerOp(op);
  }
  return result;
}
