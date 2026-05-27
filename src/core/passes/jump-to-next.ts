import type { IrOp } from "../types.ts";
import { hasRewriteBarrier, type IrPass, type IrPassFn, type PassResult } from "./helpers.ts";

const run: IrPassFn = (ops) => {
  const result: IrOp[] = [];
  let applied = 0;
  for (let i = 0; i < ops.length; i += 1) {
    const current = ops[i]!;
    if (
      current.kind === "jump" &&
      typeof current.target === "string" &&
      !hasRewriteBarrier(current)
    ) {
      let cursor = i + 1;
      let threaded = false;
      while (cursor < ops.length && ops[cursor]!.kind === "label") {
        const label = ops[cursor]!;
        if (label.kind === "label" && label.name === current.target) {
          threaded = true;
          break;
        }
        cursor += 1;
      }
      if (threaded) {
        applied += 1;
        continue;
      }
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
              name: "jump-to-next-threading",
              detail: `Removed ${applied} unconditional branch to the immediately following label.`,
            },
          ]
        : [],
  };
  return passResult;
};

export const jumpToNextThreading: IrPass = {
  name: "jump-to-next-threading",
  run,
  layoutSafe: false,
};
