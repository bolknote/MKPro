import type { IrOp, IrMeta, IrTargetMeta } from "../types.ts";
import { hasRewriteBarrier, type IrPass, type IrPassFn, type PassResult } from "./helpers.ts";

interface Block {
  startIndex: number;
  ops: IrOp[];
}

function isPureDataOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  if (op.kind === "recall") return true;
  if (op.kind === "plain") {
    if (op.opcode === 0x10 || op.opcode === 0x12) return true;
    if (op.opcode <= 0x09 || op.opcode === 0x0a) return true;
  }
  return false;
}

function isBlockTerminator(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  return op.kind === "stop" || op.kind === "store" || op.kind === "indirect-store";
}

function collectCseCandidates(ops: readonly IrOp[]): Block[] {
  const blocks: Block[] = [];
  let start = -1;
  let buffer: IrOp[] = [];
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label") {
      start = -1;
      buffer = [];
      continue;
    }
    if (isPureDataOp(op)) {
      if (start === -1) start = i;
      buffer.push(op);
      continue;
    }
    if (op.kind === "return" && buffer.length >= 3) {
      blocks.push({ startIndex: start, ops: [...buffer, op] });
    }
    start = -1;
    buffer = [];
    if (isBlockTerminator(op)) continue;
  }
  return blocks;
}

function blockSignature(block: Block): string {
  return block.ops
    .map((op) => {
      if (op.kind === "recall") return `r:${op.register}`;
      if (op.kind === "plain") return `p:${op.opcode.toString(16)}`;
      if (op.kind === "stop") return `stop:${op.semantic}`;
      return `o:${op.kind}`;
    })
    .join("|");
}

let cseLabelCounter = 0;
function freshLabel(): string {
  cseLabelCounter += 1;
  return `__cse_block_${cseLabelCounter}`;
}

const run: IrPassFn = (ops) => {
  if (ops.length === 0) return { ops: [], applied: 0, optimizations: [] };
  const blocks = collectCseCandidates(ops);
  if (blocks.length < 2) return { ops: [...ops], applied: 0, optimizations: [] };
  const bySignature = new Map<string, Block[]>();
  for (const block of blocks) {
    const sig = blockSignature(block);
    if (!bySignature.has(sig)) bySignature.set(sig, []);
    bySignature.get(sig)!.push(block);
  }
  let applied = 0;
  const replaceWith = new Map<number, { count: number; label: string; targetMeta: IrTargetMeta; jumpMeta: IrMeta }>();
  const labelsToInsert = new Map<number, string>();
  for (const [, group] of bySignature) {
    if (group.length < 2) continue;
    const blockSize = group[0]!.ops.length;
    const savedPerSite = blockSize - 2;
    if (savedPerSite < 1) continue;
    const totalSavings = savedPerSite * (group.length - 1);
    if (totalSavings <= 0) continue;
    const canonical = group[0]!;
    const label = freshLabel();
    labelsToInsert.set(canonical.startIndex, label);
    for (let i = 1; i < group.length; i += 1) {
      const dup = group[i]!;
      replaceWith.set(dup.startIndex, {
        count: dup.ops.length,
        label,
        targetMeta: { comment: "cse jump" },
        jumpMeta: { mnemonic: "БП", comment: "cse" },
      });
      applied += 1;
    }
  }
  if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  const result: IrOp[] = [];
  let i = 0;
  while (i < ops.length) {
    const labelHere = labelsToInsert.get(i);
    if (labelHere !== undefined) {
      result.push({ kind: "label", name: labelHere });
    }
    const replacement = replaceWith.get(i);
    if (replacement !== undefined) {
      result.push({
        kind: "jump",
        target: replacement.label,
        opcode: 0x51,
        meta: replacement.jumpMeta,
        targetMeta: replacement.targetMeta,
      });
      i += replacement.count;
      continue;
    }
    result.push(ops[i]!);
    i += 1;
  }
  const passResult: PassResult = {
    ops: result,
    applied,
    optimizations: [
      {
        name: "cse-display-block",
        detail: `Deduplicated ${applied} display block(s) by redirecting to a shared exit.`,
      },
    ],
  };
  return passResult;
};

export const cseDisplayBlock: IrPass = {
  name: "cse-display-block",
  run,
  layoutSafe: false,
};
