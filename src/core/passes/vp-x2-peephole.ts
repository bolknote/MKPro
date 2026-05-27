import type { IrOp } from "../types.ts";
import { type IrPass, type IrPassFn } from "./helpers.ts";

function displayBoundaryText(op: IrOp): string {
  if (!("meta" in op)) return "";
  return [
    op.meta.comment,
    "tactic" in op.meta ? op.meta.tactic : undefined,
  ].filter(Boolean).join(" ").toLowerCase();
}

function isDisplayVp(op: IrOp): boolean {
  return op.kind === "plain" &&
    op.opcode === 0x0c &&
    /display|x2|вп/u.test(displayBoundaryText(op));
}

function isFractionAfterDisplayBoundary(op: IrOp): boolean {
  return op.kind === "plain" &&
    op.opcode === 0x35 &&
    /display|x2|frac/u.test(displayBoundaryText(op));
}

const run: IrPassFn = (ops) => {
  const remove = new Set<number>();
  for (let i = 1; i < ops.length; i += 1) {
    if (isDisplayVp(ops[i - 1]!) && isFractionAfterDisplayBoundary(ops[i]!)) {
      remove.add(i);
    }
  }
  if (remove.size === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [
      {
        name: "vp-fraction-restore",
        detail: `Removed ${remove.size} К {x} op(s) already supplied by a ВП/X2 display boundary.`,
      },
    ],
  };
};

export const vpX2Peephole: IrPass = {
  name: "vp-x2-peephole",
  run,
  layoutSafe: false,
};
