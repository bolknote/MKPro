import type { IrOp } from "../types.ts";
import { hasRewriteBarrier, type IrPass, type IrPassFn } from "./helpers.ts";

const run: IrPassFn = (ops) => {
  const labelRefs = countLabelRefs(ops);
  const result: IrOp[] = [];
  let applied = 0;

  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind !== "cjump" || typeof op.target !== "string" || hasRewriteBarrier(op)) {
      result.push(op);
      continue;
    }

    const thenJumpIndex = findNextFlowOp(ops, i + 1);
    if (thenJumpIndex === undefined) {
      result.push(op);
      continue;
    }
    const thenJump = ops[thenJumpIndex]!;
    if (thenJump.kind !== "jump" || typeof thenJump.target !== "string") {
      result.push(op);
      continue;
    }

    const falseLabelIndex = thenJumpIndex + 1;
    const falseLabel = ops[falseLabelIndex];
    if (falseLabel?.kind !== "label" || falseLabel.name !== op.target) {
      result.push(op);
      continue;
    }

    const endLabelIndex = findLabel(ops, thenJump.target, falseLabelIndex + 1);
    if (endLabelIndex === undefined || (labelRefs.get(op.target) ?? 0) !== 1) {
      result.push(op);
      continue;
    }

    const thenOps = ops.slice(i + 1, thenJumpIndex);
    const elseOps = ops.slice(falseLabelIndex + 1, endLabelIndex);
    if (!isPureLinearBlock(thenOps) || !isPureLinearBlock(elseOps) || !opsEquivalent(thenOps, elseOps)) {
      result.push(op);
      continue;
    }

    result.push(...thenOps);
    i = endLabelIndex - 1;
    applied += 1;
  }

  if (applied === 0) return { ops: result, applied: 0, optimizations: [] };
  return {
    ops: result,
    applied,
    optimizations: [
      {
        name: "arithmetic-if-pass",
        detail: `Collapsed ${applied} conditional block(s) whose simplified branches were byte-identical.`,
      },
    ],
  };
};

function countLabelRefs(ops: readonly IrOp[]): Map<string, number> {
  const refs = new Map<string, number>();
  for (const op of ops) {
    if (
      (op.kind === "jump" || op.kind === "cjump" || op.kind === "call" || op.kind === "loop") &&
      typeof op.target === "string"
    ) {
      refs.set(op.target, (refs.get(op.target) ?? 0) + 1);
    }
  }
  return refs;
}

function findNextFlowOp(ops: readonly IrOp[], start: number): number | undefined {
  for (let i = start; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label") return undefined;
    if (op.kind === "jump" || op.kind === "cjump" || op.kind === "call" || op.kind === "loop") return i;
  }
  return undefined;
}

function findLabel(ops: readonly IrOp[], name: string, start: number): number | undefined {
  for (let i = start; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label" && op.name === name) return i;
  }
  return undefined;
}

function isPureLinearBlock(ops: readonly IrOp[]): boolean {
  return ops.length > 0 && ops.every((op) =>
    !hasRewriteBarrier(op) &&
    (op.kind === "plain" ||
      op.kind === "store" ||
      op.kind === "recall" ||
      op.kind === "stop")
  );
}

function opsEquivalent(a: readonly IrOp[], b: readonly IrOp[]): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i += 1) {
    const left = a[i]!;
    const right = b[i]!;
    if (left.kind !== right.kind) return false;
    if ("opcode" in left && "opcode" in right && left.opcode !== right.opcode) return false;
    if (left.kind === "store" && right.kind === "store" && left.register !== right.register) return false;
    if (left.kind === "recall" && right.kind === "recall" && left.register !== right.register) return false;
    if (left.kind === "stop" && right.kind === "stop" && left.semantic !== right.semantic) return false;
  }
  return true;
}

export const arithmeticIfPass: IrPass = {
  name: "arithmetic-if-pass",
  run,
  layoutSafe: false,
};

export type { IrOp };
