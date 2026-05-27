import type { IrOp } from "../types.ts";
import {
  calculateLabelAddresses,
  cellsPerOp,
  hasUnsafe,
  targetAddress,
  type IrPass,
  type IrPassFn,
  type PassResult,
} from "./helpers.ts";

const run: IrPassFn = (ops, ctx) => {
  if (ctx.options.opt !== "max") return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
  const usesCall = ops.some((op) => op.kind === "call" || op.kind === "indirect-call");
  if (usesCall) return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
  const labels = calculateLabelAddresses(ops);
  const result: IrOp[] = [];
  let applied = 0;
  let currentAddress = 0;
  for (const op of ops) {
    if (op.kind === "label") {
      result.push(op);
      continue;
    }
    if (op.kind === "jump" && !hasUnsafe(op)) {
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
            unsafeReason: "empty return stack assumed",
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
              unsafe: true,
            },
          ]
        : [],
    unsafeUnverified:
      applied > 0 ? ["В/О as БП 01 assumes the return stack is empty."] : [],
  };
  return passResult;
};

export const returnZeroJump: IrPass = {
  name: "return-zero-jump",
  run,
  layoutSafe: false,
};
