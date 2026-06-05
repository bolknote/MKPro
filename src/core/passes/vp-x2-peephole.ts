import type { IrOp } from "../types.ts";
import { getOpcode } from "../opcodes.ts";
import {
  computeX2RestoreBoundaryStates,
  hasRewriteBarrier,
  type IrPass,
  type IrPassFn,
} from "./helpers.ts";

function x2BoundaryText(op: IrOp): string {
  if (!("meta" in op)) return "";
  return [
    op.meta.comment,
    "tactic" in op.meta ? op.meta.tactic : undefined,
  ].filter(Boolean).join(" ").toLowerCase();
}

function isVpX2Boundary(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  return op.kind === "plain" &&
    op.opcode === 0x0c &&
    /display|x2|вп/u.test(x2BoundaryText(op));
}

function isX2Sync(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  if (op.kind === "recall" || op.kind === "indirect-recall") return true;
  if (op.kind !== "plain") return false;
  return getOpcode(op.opcode).x2Effect === "affects";
}

function isLinearX2PreservingExecutable(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  switch (op.kind) {
    case "store":
    case "indirect-store":
    case "orphan-address":
      return true;
    case "plain":
      return getOpcode(op.opcode).x2Effect === "preserves";
    default:
      return false;
  }
}

function isProvedVpX2Boundary(ops: readonly IrOp[], index: number, boundaryStates: readonly boolean[]): boolean {
  const op = ops[index];
  if (op === undefined || op.kind !== "plain" || op.opcode !== 0x0c || hasRewriteBarrier(op)) return false;
  if (boundaryStates[index] === true) return true;
  let sawPreservingGap = false;
  for (let cursor = index - 1; cursor >= 0; cursor -= 1) {
    const previous = ops[cursor]!;
    if (previous.kind === "label") continue;
    if (isX2Sync(previous)) return sawPreservingGap;
    if (!isLinearX2PreservingExecutable(previous)) return false;
    sawPreservingGap = true;
  }
  return false;
}

function isFractionAfterX2Boundary(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  return op.kind === "plain" && op.opcode === 0x35;
}

function isFreeStandingEmptyOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  return op.kind === "plain" &&
    op.opcode >= 0x54 &&
    op.opcode <= 0x56 &&
    !hasRoles(op);
}

function hasRoles(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return "meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0;
}

function fractionAfterBoundaryIndex(ops: readonly IrOp[], boundaryIndex: number): number | undefined {
  for (let index = boundaryIndex + 1; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isFreeStandingEmptyOp(op)) continue;
    return isFractionAfterX2Boundary(op) ? index : undefined;
  }
  return undefined;
}

const run: IrPassFn = (ops) => {
  const remove = new Set<number>();
  const boundaryStates = computeX2RestoreBoundaryStates(ops);
  for (let i = 0; i < ops.length; i += 1) {
    if (!isVpX2Boundary(ops[i]!) && !isProvedVpX2Boundary(ops, i, boundaryStates)) continue;
    const fractionIndex = fractionAfterBoundaryIndex(ops, i);
    if (fractionIndex !== undefined) remove.add(fractionIndex);
  }
  if (remove.size === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [
      {
        name: "vp-fraction-restore",
        detail: `Removed ${remove.size} К {x} op(s) already supplied by a ВП/X2 boundary.`,
      },
    ],
  };
};

export const vpX2Peephole: IrPass = {
  name: "vp-x2-peephole",
  run,
  layoutSafe: false,
};
