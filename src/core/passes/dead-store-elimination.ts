import type { IrOp } from "../types.ts";
import { computeLiveness } from "./liveness-analysis.ts";
import { hasUnsafe, type IrPass, type IrPassFn, type PassResult } from "./helpers.ts";

const run: IrPassFn = (ops) => {
  if (ops.length === 0) return { ops: [], applied: 0, optimizations: [], unsafeUnverified: [] };
  const liveness = computeLiveness(ops);
  const removed = new Set<number>();
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind !== "store") continue;
    if (hasUnsafe(op)) continue;
    if (liveness.liveOut[i]!.has(op.register)) continue;
    removed.add(i);
  }
  if (removed.size === 0) {
    return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
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
        name: "dead-store-elimination",
        detail: `Removed ${removed.size} store(s) to register(s) never read before the next assignment.`,
        unsafe: false,
      },
    ],
    unsafeUnverified: [],
  };
  return passResult;
};

export const deadStoreElimination: IrPass = {
  name: "dead-store-elimination",
  run,
  layoutSafe: false,
};
