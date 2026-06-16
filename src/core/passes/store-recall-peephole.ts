import {
  removableRecallValueRegister,
  storedCurrentXValueRegister,
  type IrPass,
  type IrPassFn,
} from "./helpers.ts";
import { runRecallRemovalPass } from "./recall-removal.ts";

const run: IrPassFn = (ops) =>
  runRecallRemovalPass(
    ops,
    {
      name: "store-recall-peephole",
      detail: (count) => `Dropped ${count} redundant П->X immediately after X->П to the same register.`,
    },
    (engine) => {
      for (let i = 0; i < ops.length; i += 1) {
        const current = ops[i]!;
        const next = ops[i + 1];
        const storedRegister = storedCurrentXValueRegister(current);
        const recalledRegister = next === undefined ? undefined : removableRecallValueRegister(next);
        if (storedRegister === undefined || recalledRegister === undefined) continue;
        const removalPlan = engine.plan(i + 1, { requireValueProof: storedRegister !== recalledRegister });
        if (
          (storedRegister === recalledRegister || removalPlan?.analysis.valueProof?.inX === true) &&
          removalPlan?.removable === true
        ) {
          engine.removed.add(i + 1);
        }
      }
    },
  );

export const storeRecallPeephole: IrPass = {
  name: "store-recall-peephole",
  run,
  layoutSafe: false,
};
