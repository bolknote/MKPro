import type { IrOp, RegisterName } from "../types.ts";
import {
  computeX2RegisterStates,
  hasRewriteBarrier,
  recallAlreadySyncedInX2,
  removableRecallValueRegister,
  removingRecallCanExposeStackLift,
  removingRecallCanExposeX2Restore,
  type IrPass,
  type IrPassFn,
  type PassResult,
} from "./helpers.ts";

function clobbersX(op: IrOp): boolean {
  switch (op.kind) {
    case "label":
    case "orphan-address":
      return false;
    case "store":
      return false;
    case "recall":
    case "indirect-recall":
      return true;
    case "indirect-store":
      return false;
    case "plain":
      // ALU, swap, push, digits, K-functions all touch X. Conservatively assume X changes.
      return true;
    case "stop":
      return false;
    case "jump":
    case "cjump":
    case "call":
    case "loop":
    case "indirect-jump":
    case "indirect-call":
    case "indirect-cjump":
    case "return":
      return false;
  }
}

const run: IrPassFn = (ops) => {
  const removed = new Set<number>();
  const x2States = computeX2RegisterStates(ops);
  let xHolds: RegisterName | undefined;
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label") {
      xHolds = undefined;
      continue;
    }
    if (op.kind === "store") {
      if (!hasRewriteBarrier(op)) xHolds = op.register;
      else xHolds = undefined;
      continue;
    }
    const recallRegister = removableRecallValueRegister(op);
    if (
      recallRegister !== undefined &&
      xHolds === recallRegister &&
      !removingRecallCanExposeStackLift(ops, i) &&
      !removingRecallCanExposeX2Restore(ops, i, {
        redundantSyncRegister: recallAlreadySyncedInX2(op, x2States[i]),
      })
    ) {
      removed.add(i);
      continue;
    }
    if (
      op.kind === "jump" ||
      op.kind === "cjump" ||
      op.kind === "call" ||
      op.kind === "loop" ||
      op.kind === "indirect-jump" ||
      op.kind === "indirect-call" ||
      op.kind === "indirect-cjump" ||
      op.kind === "return" ||
      op.kind === "stop"
    ) {
      // Stops let the user resume; X (and possibly Y/Z/T) may have been
      // touched interactively. Drop the X-reuse assumption across stops.
      xHolds = undefined;
      continue;
    }
    if (clobbersX(op)) xHolds = undefined;
  }
  if (removed.size === 0) {
    return { ops: [...ops], applied: 0, optimizations: [] };
  }
  const result: IrOp[] = [];
  for (let i = 0; i < ops.length; i += 1) {
    if (!removed.has(i)) result.push(ops[i]!);
  }
  const passResult: PassResult = {
    ops: result,
    applied: removed.size,
    optimizations: [
      {
        name: "last-x-reuse",
        detail: `Dropped ${removed.size} recall(s) whose register value was already in X.`,
      },
    ],
  };
  return passResult;
};

export const lastXReuse: IrPass = {
  name: "last-x-reuse",
  run,
  layoutSafe: false,
};
