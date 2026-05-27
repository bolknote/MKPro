import type { IrOp } from "../types.ts";
import {
  calculateLabelAddresses,
  cellsPerOp,
  hasRewriteBarrier,
  targetAddress,
  type IrPass,
  type IrPassFn,
  type PassResult,
} from "./helpers.ts";

const run: IrPassFn = (ops) => {
  const usesCall = ops.some((op) => op.kind === "call" || op.kind === "indirect-call");
  if (usesCall) return { ops: [...ops], applied: 0, optimizations: [] };
  const labels = calculateLabelAddresses(ops);
  const result: IrOp[] = [];
  let applied = 0;
  let currentAddress = 0;
  for (const op of ops) {
    if (op.kind === "label") {
      result.push(op);
      continue;
    }
    if (op.kind === "jump" && !hasRewriteBarrier(op)) {
      const resolved = targetAddress(op.target, labels);
      const targetsBackward =
        typeof op.target === "number"
          ? true
          : resolved !== undefined && resolved < currentAddress;
      if (resolved === 1 && targetsBackward) {
        result.push({
          kind: "return",
          opcode: 0x52,
          meta: {
            mnemonic: "В/О",
            comment: "optimized БП 01",
          },
        });
        applied += 1;
        currentAddress += 1;
        continue;
      }
    }
    result.push(op);
    currentAddress += cellsPerOp(op);
  }
  const passResult: PassResult = {
    ops: result,
    applied,
    optimizations:
      applied > 0
        ? [
            {
              name: "return-zero-jump",
              detail: `Replaced ${applied} БП 01 sequence with В/О under empty-return-stack assumption.`,
            },
          ]
        : [],
  };
  return passResult;
};

export const returnZeroJump: IrPass = {
  name: "return-zero-jump",
  run,
  layoutSafe: false,
};
