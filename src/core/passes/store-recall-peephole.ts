import type { IrOp, RegisterName } from "../types.ts";
import {
  computeX2RegisterStates,
  computeX2ValueStates,
  directReturnAnalysisContext,
  emptyResult,
  planRecallRemovalWithStackScheduler,
  removableRecallValueRegister,
  storedCurrentXValueRegister,
  type IrPass,
  type IrPassFn,
  type X2ValueDataflowState,
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
    const removalPlan = planRecallRemovalWithStackScheduler(
      ops,
      i + 1,
      x2States[i + 1],
      x2ValueStates[i + 1],
      directReturnContext,
      { removedIndexes: remove },
    );
    if (
      (storedRegister === recalledRegister || removalPlan?.analysis.valueProof?.inX === true) &&
      removalPlan?.removable === true
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
