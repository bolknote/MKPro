import type { IrOp, RegisterName } from "../types.ts";
import {
  analyzeRecallRemoval,
  computeX2RegisterStates,
  computeX2ValueStates,
  directReturnAnalysisContext,
  emptyResult,
  removableRecallValueRegister,
  storedCurrentXValueRegister,
  type IrPass,
  type IrPassFn,
  type X2ValueDataflowState,
  x2PreviousStackLiftDuplicateYProducerIndex,
} from "./helpers.ts";

const run: IrPassFn = (ops) => {
  let x2States: Array<ReadonlySet<RegisterName> | undefined> | undefined;
  let x2ValueStates: Array<X2ValueDataflowState | undefined> | undefined;
  let directReturnContext: ReturnType<typeof directReturnAnalysisContext> | undefined;
  const remove = new Set<number>();

  for (let i = 0; i < ops.length; i += 1) {
    const current = ops[i]!;
    const next = ops[i + 1];
    const storedRegister = storedCurrentXValueRegister(current);
    const recalledRegister = next === undefined ? undefined : removableRecallValueRegister(next);
    if (storedRegister === undefined || recalledRegister === undefined) continue;
    x2States ??= computeX2RegisterStates(ops);
    x2ValueStates ??= computeX2ValueStates(ops, { trackRegisterMemory: true });
    directReturnContext ??= directReturnAnalysisContext(ops);
    const removal = analyzeRecallRemoval(ops, i + 1, x2States[i + 1], x2ValueStates[i + 1], directReturnContext);
    const duplicateYProducerIndex = removal?.exposesStackLift === true && removal.exposesX2Restore !== true
      ? x2PreviousStackLiftDuplicateYProducerIndex(ops, i + 1, i + 1, x2ValueStates[i + 1], directReturnContext)
      : undefined;
    const removable = removal?.removable === true ||
      (duplicateYProducerIndex !== undefined && !remove.has(duplicateYProducerIndex));
    if (
      (storedRegister === recalledRegister || removal?.valueProof?.inX === true) &&
      removable
    ) {
      remove.add(i + 1);
    }
  }
  if (remove.size === 0) return emptyResult(ops);
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [
      {
        name: "store-recall-peephole",
        detail: `Dropped ${remove.size} redundant П->X immediately after X->П to the same register.`,
      },
    ],
  };
};

export const storeRecallPeephole: IrPass = {
  name: "store-recall-peephole",
  run,
  layoutSafe: false,
};
