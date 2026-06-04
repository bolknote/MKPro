import type { IrOp } from "../types.ts";
import { getOpcode } from "../opcodes.ts";
import {
  emptyResult,
  hasRewriteBarrier,
  removingPreShiftLiftCanExposeStack,
  type IrPass,
  type IrPassFn,
} from "./helpers.ts";

function isStackLift(op: IrOp): boolean {
  return op.kind === "plain" && op.opcode === 0x0e && !hasRewriteBarrier(op);
}

function isStackShiftingProducer(op: IrOp | undefined): boolean {
  if (op === undefined || hasRewriteBarrier(op) || !("opcode" in op)) return false;
  return getOpcode(op.opcode).stackEffect === "shifts";
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
      return getOpcode(op.opcode).stackEffect === "preserves";
    default:
      return false;
  }
}

function nextStackShiftingProducerIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isStackShiftingProducer(op)) return index;
    if (!isStackPreservingGapOp(op)) return undefined;
  }
  return undefined;
}

const run: IrPassFn = (ops) => {
  const remove = new Set<number>();
  for (let index = 0; index < ops.length - 1; index += 1) {
    const op = ops[index]!;
    if (!isStackLift(op)) continue;
    const producerIndex = nextStackShiftingProducerIndex(ops, index + 1);
    if (producerIndex === undefined) continue;
    if (removingPreShiftLiftCanExposeStack(ops, producerIndex)) continue;
    remove.add(index);
  }

  if (remove.size === 0) return emptyResult(ops);
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [{
      name: "pre-shift-stack-lift",
      detail: `Removed ${remove.size} В↑ lift${remove.size === 1 ? "" : "s"} already supplied by the following stack-shifting command.`,
    }],
  };
};

export const preShiftStackLift: IrPass = {
  name: "pre-shift-stack-lift",
  run,
  layoutSafe: false,
};
