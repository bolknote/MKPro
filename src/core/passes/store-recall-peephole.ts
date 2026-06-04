import type { IrOp } from "../types.ts";
import {
  computeX2RegisterStates,
  hasRewriteBarrier,
  recallAlreadySyncedInX2,
  removingRecallCanExposeStackLift,
  removingRecallCanExposeX2Restore,
  type IrPass,
  type IrPassFn,
  type PassResult,
} from "./helpers.ts";

const run: IrPassFn = (ops) => {
  const result: IrOp[] = [];
  const x2States = computeX2RegisterStates(ops);
  let applied = 0;
  for (let i = 0; i < ops.length; i += 1) {
    const current = ops[i]!;
    const next = ops[i + 1];
    const redundantSyncRegister = next === undefined
      ? undefined
      : recallAlreadySyncedInX2(next, x2States[i + 1]);
    if (
      current.kind === "store" &&
      next?.kind === "recall" &&
      current.register === next.register &&
      !hasRewriteBarrier(current) &&
      !hasRewriteBarrier(next) &&
      !removingRecallCanExposeStackLift(ops, i + 1) &&
      !removingRecallCanExposeX2Restore(ops, i + 1, { redundantSyncRegister })
    ) {
      result.push(current);
      applied += 1;
      i += 1;
      continue;
    }
    result.push(current);
  }
  const passResult: PassResult = {
    ops: result,
    applied,
    optimizations:
      applied > 0
        ? [
            {
              name: "store-recall-peephole",
              detail: `Dropped ${applied} redundant П->X immediately after X->П to the same register.`,
            },
          ]
        : [],
  };
  return passResult;
};

export const storeRecallPeephole: IrPass = {
  name: "store-recall-peephole",
  run,
  layoutSafe: false,
};
