import { getOpcode } from "../opcodes.ts";
import type { IrCondition, IrOp } from "../types.ts";
import { hasRewriteBarrier, type IrPass, type IrPassFn } from "./helpers.ts";

const INVERTED_CONDITIONS: Record<IrCondition, { condition: IrCondition; opcode: number }> = {
  "==0": { condition: "!=0", opcode: 0x57 },
  "!=0": { condition: "==0", opcode: 0x5e },
  "<0": { condition: ">=0", opcode: 0x59 },
  ">=0": { condition: "<0", opcode: 0x5c },
};

const run: IrPassFn = (ops, context) => {
  if (context.options.tailBranchInversion !== true) {
    return { ops: [...ops], applied: 0, optimizations: [] };
  }

  const refs = countLabelRefs(ops);
  const result: IrOp[] = [];
  let applied = 0;

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind !== "cjump" || typeof op.target !== "string" || hasRewriteBarrier(op)) {
      result.push(op);
      continue;
    }

    const jumpIndex = nextExecutableIndex(ops, index + 1);
    if (jumpIndex === undefined) {
      result.push(op);
      continue;
    }
    const jump = ops[jumpIndex]!;
    if (jump.kind !== "jump" || hasRewriteBarrier(jump)) {
      result.push(op);
      continue;
    }

    const labelIndex = jumpIndex + 1;
    const label = ops[labelIndex];
    if (label?.kind !== "label" || label.name !== op.target || (refs.get(label.name) ?? 0) !== 1) {
      result.push(op);
      continue;
    }

    const inverted = INVERTED_CONDITIONS[op.condition];
    result.push({
      ...op,
      condition: inverted.condition,
      target: jump.target,
      opcode: inverted.opcode,
      meta: {
        ...op.meta,
        mnemonic: getOpcode(inverted.opcode).name,
        comment: op.meta.comment?.replace(/^false branch/u, "direct tail branch") ?? "direct tail branch",
      },
      targetMeta: { ...jump.targetMeta },
    });
    index = labelIndex;
    applied += 1;
  }

  if (applied === 0) return { ops: result, applied: 0, optimizations: [] };
  return {
    ops: result,
    applied,
    optimizations: [{
      name: "tail-branch-inversion",
      detail: `Inverted ${applied} branch${applied === 1 ? "" : "es"} whose then-path was only a tail jump.`,
    }],
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

function nextExecutableIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    if (ops[index]!.kind !== "label") return index;
  }
  return undefined;
}

export const tailBranchInversion: IrPass = {
  name: "tail-branch-inversion",
  run,
  layoutSafe: false,
};
