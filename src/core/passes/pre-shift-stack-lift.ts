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

const run: IrPassFn = (ops) => {
  const remove = new Set<number>();
  for (let index = 0; index < ops.length - 1; index += 1) {
    const op = ops[index]!;
    const next = ops[index + 1];
    if (!isStackLift(op) || !isStackShiftingProducer(next)) continue;
    if (removingPreShiftLiftCanExposeStack(ops, index + 1)) continue;
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
