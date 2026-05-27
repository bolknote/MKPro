import type { IrOp, RegisterName } from "../types.ts";
import { hasRewriteBarrier, type IrPass, type IrPassFn, type PassResult } from "./helpers.ts";

function registerReadBeforeNextWrite(
  ops: readonly IrOp[],
  start: number,
  register: RegisterName,
): boolean {
  for (let i = start; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "recall" && op.register === register) return true;
    if (op.kind === "store" && op.register === register) return false;
  }
  return false;
}

function isCommutativeAlu(op: IrOp): boolean {
  if (op.kind !== "plain") return false;
  return op.opcode === 0x10 || op.opcode === 0x12;
}

const run: IrPassFn = (ops) => {
  const result: IrOp[] = [];
  let applied = 0;
  for (let i = 0; i < ops.length; i += 1) {
    const current = ops[i]!;
    const next = ops[i + 1];
    const after = ops[i + 2];
    if (
      current.kind === "store" &&
      next?.kind === "recall" &&
      after !== undefined &&
      isCommutativeAlu(after) &&
      !hasRewriteBarrier(current) &&
      !hasRewriteBarrier(next) &&
      !hasRewriteBarrier(after) &&
      !registerReadBeforeNextWrite(ops, i + 3, current.register)
    ) {
      applied += 1;
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
              name: "dead-temp-store",
              detail: `Removed ${applied} temp store(s) whose X value was consumed directly by stack scheduling.`,
            },
          ]
        : [],
  };
  return passResult;
};

export const deadStoreBeforeCommutative: IrPass = {
  name: "dead-temp-store",
  run,
  layoutSafe: false,
};
