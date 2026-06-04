import type { IrOp, RegisterName } from "../types.ts";
import { isStableIndirectSelector } from "../indirect-addressing.ts";
import {
  computeX2RegisterStates,
  hasRewriteBarrier,
  knownIndirectMemoryTarget,
  recallAlreadySyncedInX2,
  removableRecallValueRegister,
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
    const storedRegister = storedValueRegister(current);
    const recalledRegister = next === undefined ? undefined : removableRecallValueRegister(next);
    const redundantSyncRegister = next === undefined
      ? undefined
      : recallAlreadySyncedInX2(next, x2States[i + 1]);
    if (
      storedRegister !== undefined &&
      recalledRegister !== undefined &&
      storedRegister === recalledRegister &&
      !hasRewriteBarrier(current) &&
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

function storedValueRegister(op: IrOp): RegisterName | undefined {
  if (hasRewriteBarrier(op)) return undefined;
  if (op.kind === "store") return op.register;
  if (op.kind !== "indirect-store") return undefined;
  if (!isStableIndirectSelector(op.register)) return undefined;
  return knownIndirectMemoryTarget(op);
}

export const storeRecallPeephole: IrPass = {
  name: "store-recall-peephole",
  run,
  layoutSafe: false,
};
