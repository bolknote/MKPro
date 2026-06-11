import type { IrOp } from "../types.ts";
import { hasRewriteBarrier, labelIndexes, type IrPass, type IrPassFn, type PassResult } from "./helpers.ts";

function followLabel(
  ops: readonly IrOp[],
  labels: Map<string, number>,
  start: string,
  seen: Set<string>,
): string | undefined {
  let current = start;
  while (!seen.has(current)) {
    seen.add(current);
    const index = labels.get(current);
    if (index === undefined) return current;
    let cursor = index + 1;
    while (cursor < ops.length && ops[cursor]!.kind === "label") cursor += 1;
    const next = ops[cursor];
    if (next?.kind !== "jump") return current;
    if (typeof next.target !== "string") return current;
    if (hasRewriteBarrier(next)) return current;
    current = next.target;
  }
  return current;
}

const run: IrPassFn = (ops) => {
  const labels = labelIndexes(ops);
  const result: IrOp[] = [];
  let applied = 0;
  for (const op of ops) {
    if (
      (op.kind === "jump" || op.kind === "cjump") &&
      typeof op.target === "string" &&
      !hasRewriteBarrier(op)
    ) {
      const final = followLabel(ops, labels, op.target, new Set<string>());
      if (final !== undefined && final !== op.target) {
        applied += 1;
        result.push({ ...op, target: final });
        continue;
      }
    }
    result.push(op);
  }
  if (applied === 0) {
    return { ops: result, applied: 0, optimizations: [] };
  }
  const passResult: PassResult = {
    ops: result,
    applied,
    optimizations: [
      {
        name: "jump-thread",
        detail: `Threaded ${applied} jump(s) through trampoline labels to the final target.`,
      },
    ],
  };
  return passResult;
};

export const jumpThread: IrPass = {
  name: "jump-thread",
  run,
  layoutSafe: false,
};
