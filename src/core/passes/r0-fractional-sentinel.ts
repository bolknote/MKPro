import type { IrOp } from "../types.ts";
import { type IrPass, type IrPassFn } from "./helpers.ts";
import { computeLiveness } from "./liveness-analysis.ts";

function isFractionalR0LiteralBeforeStore(ops: readonly IrOp[], storeIndex: number): boolean {
  let index = storeIndex - 1;
  let hasNonZeroFractionDigit = false;
  while (index >= 0) {
    const digit = ops[index];
    if (digit?.kind !== "plain" || digit.opcode < 0x00 || digit.opcode > 0x09) break;
    if (digit.opcode > 0) hasNonZeroFractionDigit = true;
    index -= 1;
  }
  const dot = ops[index];
  const zero = ops[index - 1];
  if (!hasNonZeroFractionDigit || dot?.kind !== "plain" || dot.opcode !== 0x0a) return false;
  if (zero === undefined) return true;
  return zero.kind === "plain" && zero.opcode === 0x00;
}

const run: IrPassFn = (ops) => {
  if (ops.length === 0) return { ops: [], applied: 0, optimizations: [] };
  const liveness = computeLiveness(ops);
  const remove = new Set<number>();
  let r0Fractional = false;

  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "store" && op.register === "0") {
      r0Fractional = isFractionalR0LiteralBeforeStore(ops, i);
      continue;
    }
    if (op.kind === "plain" || op.kind === "recall" || op.kind === "label") {
      continue;
    }
    if (!r0Fractional) continue;

    if (op.kind === "indirect-recall" && op.register === "0") {
      const next = ops[i + 1];
      if (next?.kind === "recall" && next.register === "3" && !liveness.liveOut[i]!.has("0")) {
        remove.add(i + 1);
      }
      r0Fractional = false;
      continue;
    }

    if (op.kind === "indirect-store" && op.register === "0") {
      const next = ops[i + 1];
      if (next?.kind === "store" && next.register === "3" && !liveness.liveOut[i]!.has("0")) {
        remove.add(i + 1);
      }
      r0Fractional = false;
      continue;
    }

    if (op.kind !== "store" || op.register !== "0") r0Fractional = false;
  }

  if (remove.size === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [
      {
        name: "r0-fractional-sentinel",
        detail: `Removed ${remove.size} redundant direct R3 access(es) after fractional-R0 indirect access.`,
      },
    ],
  };
};

export const r0FractionalSentinel: IrPass = {
  name: "r0-fractional-sentinel",
  run,
  layoutSafe: false,
};
