import type { IrOp, IrTargetMeta } from "../types.ts";
import { hasRewriteBarrier, type IrPass, type IrPassFn, type PassResult } from "./helpers.ts";

function isZeroDigit(op: IrOp): boolean {
  return op.kind === "plain" && op.opcode === 0x00;
}

function isPauseLike(op: IrOp): boolean {
  return op.kind === "stop";
}

function isUnconditionalJump(op: IrOp): op is Extract<IrOp, { kind: "jump" }> {
  return op.kind === "jump";
}

function isTerminalFlow(op: IrOp): boolean {
  return op.kind === "jump" || op.kind === "indirect-jump" || op.kind === "return";
}

function cannotFallThrough(op: IrOp | undefined): boolean {
  return op === undefined || isTerminalFlow(op);
}

function previousExecutableCannotFallThrough(ops: readonly IrOp[], index: number): boolean {
  for (let cursor = index - 1; cursor >= 0; cursor -= 1) {
    const op = ops[cursor];
    if (op?.kind === "label") return false;
    return cannotFallThrough(op);
  }
  return true;
}

function removableLabelRun(ops: readonly IrOp[], index: number): { labels: string[]; next: number } | undefined {
  const labels: string[] = [];
  let cursor = index;
  while (cursor < ops.length) {
    const op = ops[cursor];
    if (op === undefined || !canRemoveLabel(op)) break;
    labels.push(op.name);
    cursor += 1;
  }
  return labels.length === 0 ? undefined : { labels, next: cursor };
}

function terminalFlowKey(op: IrOp): string | undefined {
  if (op.kind === "jump") return `jump:${String(op.target)}:${op.opcode}`;
  if (op.kind === "indirect-jump") return `indirect-jump:${op.register}:${op.opcode}`;
  if (op.kind === "return") return `return:${op.opcode}`;
  return undefined;
}

function canRemoveLabel(op: IrOp): op is Extract<IrOp, { kind: "label" }> {
  return op.kind === "label" && op.procedureBoundary === undefined;
}

const run: IrPassFn = (ops) => {
  const rewrite = new Map<string, string>();
  const remove = new Set<number>();
  let applied = 0;
  let pauseOnlyApplied = 0;
  const separatedPauseTails = new Map<string, string>();

  for (let i = 0; i + 2 < ops.length; i += 1) {
    if (remove.has(i)) continue;
    const run = removableLabelRun(ops, i);
    if (run === undefined) continue;
    const pause = ops[run.next];
    const flow = ops[run.next + 1];
    if (
      pause !== undefined &&
      isPauseLike(pause) &&
      flow !== undefined &&
      isTerminalFlow(flow) &&
      previousExecutableCannotFallThrough(ops, i) &&
      !hasRewriteBarrier(pause) &&
      !hasRewriteBarrier(flow)
    ) {
      const key = terminalFlowKey(flow);
      if (key === undefined) continue;
      const keptLabel = separatedPauseTails.get(key);
      if (keptLabel === undefined) {
        separatedPauseTails.set(key, run.labels[0]!);
        i = run.next + 1;
        continue;
      }
      for (const label of run.labels) rewrite.set(label, keptLabel);
      for (let index = i; index <= run.next + 1; index += 1) remove.add(index);
      applied += 1;
      pauseOnlyApplied += 1;
      i = run.next + 1;
    }
  }

  for (let i = 0; i + 7 < ops.length; i += 1) {
    if (remove.has(i)) continue;
    const firstLabel = ops[i];
    const firstPause = ops[i + 1];
    const firstFlowLabel = ops[i + 2];
    const firstFlow = ops[i + 3];
    const secondLabel = ops[i + 4];
    const secondPause = ops[i + 5];
    const secondFlowLabel = ops[i + 6];
    const secondFlow = ops[i + 7];

    if (
      firstLabel !== undefined &&
      canRemoveLabel(firstLabel) &&
      firstPause !== undefined &&
      isPauseLike(firstPause) &&
      firstFlowLabel !== undefined &&
      canRemoveLabel(firstFlowLabel) &&
      firstFlow !== undefined &&
      isTerminalFlow(firstFlow) &&
      secondLabel !== undefined &&
      canRemoveLabel(secondLabel) &&
      secondPause !== undefined &&
      isPauseLike(secondPause) &&
      secondFlowLabel !== undefined &&
      canRemoveLabel(secondFlowLabel) &&
      secondFlow !== undefined &&
      isTerminalFlow(secondFlow) &&
      terminalFlowKey(firstFlow) === terminalFlowKey(secondFlow) &&
      !hasRewriteBarrier(firstPause) &&
      !hasRewriteBarrier(firstFlow) &&
      !hasRewriteBarrier(secondPause) &&
      !hasRewriteBarrier(secondFlow)
    ) {
      rewrite.set(firstLabel.name, secondLabel.name);
      rewrite.set(firstFlowLabel.name, secondFlowLabel.name);
      for (let index = i; index <= i + 3; index += 1) remove.add(index);
      applied += 1;
      pauseOnlyApplied += 1;
      i += 3;
    }
  }

  for (let i = 0; i + 5 < ops.length; i += 1) {
    if (remove.has(i)) continue;
    const firstLabel = ops[i];
    const firstPause = ops[i + 1];
    const firstFlow = ops[i + 2];
    const secondLabel = ops[i + 3];
    const secondPause = ops[i + 4];
    const secondFlow = ops[i + 5];

    if (
      firstLabel !== undefined &&
      canRemoveLabel(firstLabel) &&
      firstPause !== undefined &&
      isPauseLike(firstPause) &&
      firstFlow !== undefined &&
      isTerminalFlow(firstFlow) &&
      secondLabel !== undefined &&
      canRemoveLabel(secondLabel) &&
      secondPause !== undefined &&
      isPauseLike(secondPause) &&
      secondFlow !== undefined &&
      isTerminalFlow(secondFlow) &&
      terminalFlowKey(firstFlow) === terminalFlowKey(secondFlow) &&
      !hasRewriteBarrier(firstPause) &&
      !hasRewriteBarrier(firstFlow) &&
      !hasRewriteBarrier(secondPause) &&
      !hasRewriteBarrier(secondFlow)
    ) {
      rewrite.set(firstLabel.name, secondLabel.name);
      for (let index = i; index <= i + 2; index += 1) remove.add(index);
      applied += 1;
      pauseOnlyApplied += 1;
      i += 2;
    }
  }

  for (let i = 0; i + 8 < ops.length; i += 1) {
    if (remove.has(i)) continue;
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
      !hasRewriteBarrier(firstZero) &&
      !hasRewriteBarrier(firstPause) &&
      !hasRewriteBarrier(trampolineJump) &&
      !hasRewriteBarrier(secondZero) &&
      !hasRewriteBarrier(secondPause)
    ) {
      rewrite.set(firstLabel.name, secondLabel.name);
      rewrite.set(trampolineLabel.name, endLabel.name);
      for (let index = i; index <= i + 4; index += 1) remove.add(index);
      applied += 1;
      i += 4;
    }
  }

  if (applied === 0) {
    return { ops: [...ops], applied: 0, optimizations: [] };
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
        if (op.targetMeta.roles !== undefined) targetMeta.roles = [...op.targetMeta.roles];
        if (op.targetMeta.formalOpcode !== undefined) targetMeta.formalOpcode = op.targetMeta.formalOpcode;
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
        detail: `Merged ${applied} duplicate failure tail(s), including ${pauseOnlyApplied} pause-only tail(s).`,
      },
    ],
  };
  return passResult;
};

export const duplicateFailureTail: IrPass = {
  name: "duplicate-failure-tail-merge",
  run,
  layoutSafe: false,
};
