import type { IrOp, IrTargetMeta } from "../types.ts";
import { hasUnsafe, type IrPass, type IrPassFn, type PassResult } from "./helpers.ts";

function isZeroDigit(op: IrOp): boolean {
  return op.kind === "plain" && op.opcode === 0x00;
}

function isPauseLike(op: IrOp): boolean {
  return op.kind === "stop";
}

function isUnconditionalJump(op: IrOp): op is Extract<IrOp, { kind: "jump" }> {
  return op.kind === "jump";
}

const run: IrPassFn = (ops) => {
  const rewrite = new Map<string, string>();
  const remove = new Set<number>();
  let applied = 0;

  for (let i = 0; i + 8 < ops.length; i += 1) {
    const firstLabel = ops[i];
    const firstZero = ops[i + 1];
    const firstPause = ops[i + 2];
    const trampolineLabel = ops[i + 3];
    const trampolineJump = ops[i + 4];
    const secondLabel = ops[i + 5];
    const secondZero = ops[i + 6];
    const secondPause = ops[i + 7];
    const endLabel = ops[i + 8];

    if (
      firstLabel?.kind === "label" &&
      firstZero !== undefined &&
      isZeroDigit(firstZero) &&
      firstPause !== undefined &&
      isPauseLike(firstPause) &&
      trampolineLabel?.kind === "label" &&
      trampolineJump !== undefined &&
      isUnconditionalJump(trampolineJump) &&
      typeof trampolineJump.target === "string" &&
      secondLabel?.kind === "label" &&
      secondZero !== undefined &&
      isZeroDigit(secondZero) &&
      secondPause !== undefined &&
      isPauseLike(secondPause) &&
      endLabel?.kind === "label" &&
      trampolineJump.target === endLabel.name &&
      !hasUnsafe(firstZero) &&
      !hasUnsafe(firstPause) &&
      !hasUnsafe(trampolineJump) &&
      !hasUnsafe(secondZero) &&
      !hasUnsafe(secondPause)
    ) {
      rewrite.set(firstLabel.name, secondLabel.name);
      rewrite.set(trampolineLabel.name, endLabel.name);
      for (let index = i; index <= i + 4; index += 1) remove.add(index);
      applied += 1;
      i += 4;
    }
  }

  if (applied === 0) {
    return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
  }

  const result: IrOp[] = [];
  for (let index = 0; index < ops.length; index += 1) {
    if (remove.has(index)) continue;
    const op = ops[index]!;
    if (
      (op.kind === "jump" || op.kind === "cjump" || op.kind === "call" || op.kind === "loop") &&
      typeof op.target === "string"
    ) {
      const replacement = rewrite.get(op.target);
      if (replacement !== undefined) {
        const targetMeta: IrTargetMeta = {};
        if (op.targetMeta.comment !== undefined) targetMeta.comment = op.targetMeta.comment;
        if (op.targetMeta.sourceLine !== undefined) targetMeta.sourceLine = op.targetMeta.sourceLine;
        if (op.targetMeta.unsafeReason !== undefined) targetMeta.unsafeReason = op.targetMeta.unsafeReason;
        result.push({ ...op, target: replacement, targetMeta });
        continue;
      }
    }
    result.push(op);
  }

  const passResult: PassResult = {
    ops: result,
    applied,
    optimizations: [
      {
        name: "duplicate-failure-tail-merge",
        detail: `Merged ${applied} duplicate pause-0 failure tail(s).`,
        unsafe: false,
      },
    ],
    unsafeUnverified: [],
  };
  return passResult;
};

export const duplicateFailureTail: IrPass = {
  name: "duplicate-failure-tail-merge",
  run,
  layoutSafe: false,
};
