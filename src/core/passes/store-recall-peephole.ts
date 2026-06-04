import type { IrOp } from "../types.ts";
import {
  computeX2RegisterStates,
  computeX2ValueStates,
  recallAlreadySyncedInX2,
  recallAlreadySyncedInX2MemoryValue,
  recallAlreadySyncedInX2PreloadedDecimal,
  recallAlreadySyncedInX2Value,
  removableRecallValueRegister,
  removingRecallCanExposeStackLift,
  removingRecallCanExposeX2Restore,
  storedCurrentXValueRegister,
  type IrPass,
  type IrPassFn,
  type PassResult,
} from "./helpers.ts";

const run: IrPassFn = (ops) => {
  const result: IrOp[] = [];
  const x2States = computeX2RegisterStates(ops);
  const x2ValueStates = computeX2ValueStates(ops, { trackRegisterMemory: true });
  let applied = 0;
  for (let i = 0; i < ops.length; i += 1) {
    const current = ops[i]!;
    const next = ops[i + 1];
    const storedRegister = storedCurrentXValueRegister(current);
    const recalledRegister = next === undefined ? undefined : removableRecallValueRegister(next);
    const redundantSyncRegister = next === undefined
      ? undefined
      : recallAlreadySyncedInX2(next, x2States[i + 1]) ??
        recallAlreadySyncedInX2Value(next, x2ValueStates[i + 1]);
    const redundantSyncValue = next === undefined
      ? false
      : (recallAlreadySyncedInX2MemoryValue(next, x2ValueStates[i + 1]) ??
        recallAlreadySyncedInX2PreloadedDecimal(next, x2ValueStates[i + 1])) !== undefined;
    if (
      storedRegister !== undefined &&
      recalledRegister !== undefined &&
      storedRegister === recalledRegister &&
      !removingRecallCanExposeStackLift(ops, i + 1) &&
      !removingRecallCanExposeX2Restore(ops, i + 1, { redundantSyncRegister, redundantSyncValue })
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
