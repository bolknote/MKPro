import type { IrMeta, IrOp } from "../types.ts";
import { type IrPass, type IrPassFn } from "./helpers.ts";

interface TailJumpTarget {
  continuation: string | number;
  start: number;
  end: number;
}

const run: IrPassFn = (ops) => {
  const tailJumpTargets = findTailJumpTargets(ops);
  const returnContinuations = collectReturnContinuations(ops, tailJumpTargets);
  const returnLabels = collectReturnLabels(ops);
  const result: IrOp[] = [];
  let applied = 0;
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      result.push(op);
      continue;
    }
    const next = ops[index + 1];
    if (op.kind === "return") {
      const continuation = returnContinuations.get(index);
      if (continuation !== undefined) {
        const meta: IrMeta = {
          mnemonic: "БП",
          comment: op.meta.comment?.replace(/^implicit return from proc/u, "tail continuation") ?? "tail continuation",
        };
        if (op.meta.sourceLine !== undefined) meta.sourceLine = op.meta.sourceLine;
        result.push({
          kind: "jump",
          target: continuation,
          opcode: 0x51,
          meta,
          targetMeta: { comment: "tail continuation" },
        });
        applied += 1;
        continue;
      }
    }
    if (op.kind === "call") {
      const continuationIndex = nextExecutableIndex(ops, index + 1);
      const continuation = continuationIndex === undefined ? undefined : ops[continuationIndex];
      const continuationIsImmediate = continuationIndex === index + 1;
      if (continuation?.kind === "jump" && isReturnLabel(continuation.target, returnLabels)) {
        result.push({
          kind: "jump",
          target: op.target,
          opcode: 0x51,
          meta: {
            ...op.meta,
            mnemonic: "БП",
            comment: op.meta.comment?.replace(/^proc call/u, "tail call") ?? "tail call",
          },
          targetMeta: { ...op.targetMeta },
        });
        if (continuationIsImmediate) index += 1;
        applied += 1;
        continue;
      }
      if (continuation?.kind === "return") {
        result.push({
          kind: "jump",
          target: op.target,
          opcode: 0x51,
          meta: {
            ...op.meta,
            mnemonic: "БП",
            comment: op.meta.comment?.replace(/^proc call/u, "tail call") ?? "tail call",
          },
          targetMeta: { ...op.targetMeta },
        });
        if (continuationIsImmediate) index += 1;
        applied += 1;
        continue;
      }
      const target = typeof op.target === "string" ? tailJumpTargets.get(op.target) : undefined;
      if (target !== undefined && next?.kind === "jump" && sameTarget(next.target, target.continuation)) {
        result.push({
          kind: "jump",
          target: op.target,
          opcode: 0x51,
          meta: {
            ...op.meta,
            mnemonic: "БП",
            comment: op.meta.comment?.replace(/^proc call/u, "tail jump") ?? "tail jump",
          },
          targetMeta: { ...op.targetMeta },
        });
        index += 1;
        applied += 1;
        continue;
      }
      if (target !== undefined && next?.kind === "label" && sameTarget(next.name, target.continuation)) {
        result.push({
          kind: "jump",
          target: op.target,
          opcode: 0x51,
          meta: {
            ...op.meta,
            mnemonic: "БП",
            comment: op.meta.comment?.replace(/^proc call/u, "tail jump") ?? "tail jump",
          },
          targetMeta: { ...op.targetMeta },
        });
        applied += 1;
        continue;
      }
    }
    result.push(op);
  }
  if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  const tailJumpCount = tailJumpTargets.size;
  return {
    ops: result,
    applied,
    optimizations: [{
      name: "tail-call-lowering",
      detail: tailJumpCount === 0
        ? `Replaced ${applied} subroutine tail call${applied === 1 ? "" : "s"} with direct jump(s).`
        : `Replaced ${applied} subroutine tail operation${applied === 1 ? "" : "s"} with direct jump continuation${tailJumpCount === 1 ? "" : "s"}.`,
    }],
  };
};

export const tailCallLowering: IrPass = {
  name: "tail-call-lowering",
  run,
  layoutSafe: false,
};

function findTailJumpTargets(ops: readonly IrOp[]): Map<string, TailJumpTarget> {
  const calls = new Map<string, Array<string | number | undefined>>();
  const nonCallFlowTargets = new Set<string>();

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "call" && typeof op.target === "string") {
      const continuation = callContinuation(ops, index);
      const existing = calls.get(op.target) ?? [];
      existing.push(continuation);
      calls.set(op.target, existing);
      continue;
    }
    if ((op.kind === "jump" || op.kind === "cjump" || op.kind === "loop") && typeof op.target === "string") {
      nonCallFlowTargets.add(op.target);
    }
  }

  const regions = collectCallableRegions(ops, new Set(calls.keys()));
  const result = new Map<string, TailJumpTarget>();
  for (const [target, continuations] of calls) {
    if (nonCallFlowTargets.has(target)) continue;
    const region = regions.get(target);
    if (region === undefined || !blockHasReturn(ops, region.start, region.end)) continue;
    const first = continuations[0];
    if (first === undefined) continue;
    if (continuations.every((continuation) => continuation !== undefined && sameTarget(continuation, first))) {
      result.set(target, { continuation: first, start: region.start, end: region.end });
    }
  }
  return result;
}

function collectCallableRegions(
  ops: readonly IrOp[],
  callTargets: ReadonlySet<string>,
): Map<string, { start: number; end: number }> {
  const result = new Map<string, { start: number; end: number }>();
  let current: { name: string; start: number } | undefined;
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind !== "label") continue;
    if (!callTargets.has(op.name)) continue;
    if (current !== undefined) result.set(current.name, { start: current.start, end: index });
    current = { name: op.name, start: index + 1 };
  }
  if (current !== undefined) result.set(current.name, { start: current.start, end: ops.length });
  return result;
}

function collectReturnContinuations(
  ops: readonly IrOp[],
  targets: ReadonlyMap<string, TailJumpTarget>,
): Map<number, string | number> {
  const result = new Map<number, string | number>();
  for (const target of targets.values()) {
    for (let index = target.start; index < target.end; index += 1) {
      if (ops[index]?.kind === "return") result.set(index, target.continuation);
    }
  }
  return result;
}

function collectReturnLabels(ops: readonly IrOp[]): Set<string> {
  const result = new Set<string>();
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind !== "label") continue;
    const next = nextExecutableIndex(ops, index + 1);
    if (next !== undefined && ops[next]?.kind === "return") result.add(op.name);
  }
  return result;
}

function nextExecutableIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    if (ops[index]?.kind !== "label") return index;
  }
  return undefined;
}

function isReturnLabel(target: string | number, returnLabels: ReadonlySet<string>): boolean {
  return typeof target === "string" && returnLabels.has(target);
}

function blockHasReturn(ops: readonly IrOp[], start: number, end: number): boolean {
  for (let index = start; index < end; index += 1) {
    if (ops[index]?.kind === "return") return true;
  }
  return false;
}

function callContinuation(ops: readonly IrOp[], index: number): string | number | undefined {
  const next = ops[index + 1];
  if (next?.kind === "jump") return next.target;
  if (next?.kind === "label") return next.name;
  return undefined;
}

function sameTarget(left: string | number, right: string | number): boolean {
  return left === right;
}
