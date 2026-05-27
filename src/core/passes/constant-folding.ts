import type { IrOp, IrMeta } from "../types.ts";
import { type IrPass, type IrPassFn, type PassResult } from "./helpers.ts";

function digitOf(op: IrOp): number | undefined {
  if (op.kind === "plain" && op.opcode <= 0x09) return op.opcode;
  return undefined;
}

function aluOpcode(op: IrOp): "+" | "-" | "*" | "/" | undefined {
  if (op.kind !== "plain") return undefined;
  if (op.opcode === 0x10) return "+";
  if (op.opcode === 0x11) return "-";
  if (op.opcode === 0x12) return "*";
  if (op.opcode === 0x13) return "/";
  return undefined;
}

function isIdentityPlus(op: IrOp, prev: IrOp | undefined): boolean {
  if (prev === undefined) return false;
  const digit = digitOf(prev);
  if (digit !== 0) return false;
  return aluOpcode(op) === "+";
}

function isIdentityMul(op: IrOp, prev: IrOp | undefined): boolean {
  if (prev === undefined) return false;
  const digit = digitOf(prev);
  if (digit !== 1) return false;
  return aluOpcode(op) === "*";
}

const run: IrPassFn = (ops) => {
  const result: IrOp[] = [];
  let applied = 0;
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    const prev = result[result.length - 1];
    if (
      (isIdentityPlus(op, prev) || isIdentityMul(op, prev)) &&
      prev !== undefined &&
      prev.kind === "plain" &&
      prev.meta.raw !== true
    ) {
      result.pop();
      applied += 1;
      continue;
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
        name: "constant-folding",
        detail: `Dropped ${applied} identity arithmetic operation(s) (0+ or 1*).`,
      },
    ],
  };
  return passResult;
};

export const constantFolding: IrPass = {
  name: "constant-folding",
  run,
  layoutSafe: false,
};

export type { IrMeta };
