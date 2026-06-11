import type { IrOp } from "../types.ts";
import { hasRewriteBarrier, type IrPass, type IrPassFn } from "./helpers.ts";
import { createLabelAllocator, targetKey } from "./outline.ts";

interface SharedCallTail {
  key: string;
  label: string;
  call: Extract<IrOp, { kind: "call" }>;
  continuation: Extract<IrOp, { kind: "jump" }>;
  count: number;
}

const run: IrPassFn = (ops) => {
  const candidates = collectSharedCallTails(ops);
  const normalizeContinuation = buildContinuationNormalizer(ops);
  if (candidates.size === 0) return { ops: [...ops], applied: 0, optimizations: [] };

  const result: IrOp[] = [];
  let applied = 0;
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    const next = ops[index + 1];
    if (op.kind === "call" && next?.kind === "jump") {
      const key = callTailKey(op, next, normalizeContinuation);
      const candidate = candidates.get(key);
      if (candidate !== undefined) {
        result.push({
          kind: "jump",
          target: candidate.label,
          opcode: 0x51,
          meta: {
            ...op.meta,
            mnemonic: "БП",
            comment: op.meta.comment?.replace(/^proc call/u, "shared call tail") ?? "shared call tail",
          },
          targetMeta: { comment: "shared call tail" },
        });
        index += 1;
        applied += 1;
        continue;
      }
    }
    result.push(op);
  }

  for (const candidate of candidates.values()) {
    result.push({ kind: "label", name: candidate.label });
    result.push({
      ...candidate.call,
      meta: {
        ...candidate.call.meta,
        comment: candidate.call.meta.comment ?? "shared call tail helper",
      },
    });
    result.push({
      ...candidate.continuation,
      meta: {
        ...candidate.continuation.meta,
        comment: candidate.continuation.meta.comment ?? "shared call tail continuation",
      },
    });
  }

  return {
    ops: result,
    applied,
    optimizations: [{
      name: "shared-call-tail",
      detail: `Shared ${applied} call+jump tail sequence${applied === 1 ? "" : "s"}.`,
    }],
  };
};

export const sharedCallTail: IrPass = {
  name: "shared-call-tail",
  run,
  layoutSafe: false,
};

function collectSharedCallTails(ops: readonly IrOp[]): Map<string, SharedCallTail> {
  const normalizeContinuation = buildContinuationNormalizer(ops);
  const counts = new Map<string, {
    call: Extract<IrOp, { kind: "call" }>;
    continuation: Extract<IrOp, { kind: "jump" }>;
    count: number;
  }>();
  for (let index = 0; index < ops.length - 1; index += 1) {
    const op = ops[index]!;
    const next = ops[index + 1]!;
    if (op.kind !== "call" || next.kind !== "jump") continue;
    if (hasRewriteBarrier(op) || hasRewriteBarrier(next)) continue;
    const normalizedTarget = normalizeContinuation(next.target);
    const continuation: Extract<IrOp, { kind: "jump" }> = {
      ...next,
      target: normalizedTarget,
    };
    const key = callTailKey(op, continuation, normalizeContinuation);
    const existing = counts.get(key);
    if (existing === undefined) {
      counts.set(key, { call: op, continuation, count: 1 });
    } else {
      existing.count += 1;
    }
  }

  const result = new Map<string, SharedCallTail>();
  const labels = createLabelAllocator(ops, "__shared_call_tail_");
  for (const [key, candidate] of counts) {
    if (candidate.count < 3) continue;
    result.set(key, {
      key,
      label: labels.next(),
      call: candidate.call,
      continuation: candidate.continuation,
      count: candidate.count,
    });
  }
  return result;
}

function callTailKey(
  call: Extract<IrOp, { kind: "call" }>,
  continuation: Extract<IrOp, { kind: "jump" }>,
  normalizeContinuation: (target: string | number) => string | number,
): string {
  return `${targetKey(call.target)}|${targetKey(normalizeContinuation(continuation.target))}`;
}

function buildContinuationNormalizer(ops: readonly IrOp[]): (target: string | number) => string | number {
  const labelIndexes = new Map<string, number>();
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") labelIndexes.set(op.name, index);
  }

  const normalize = (target: string | number, seen = new Set<string>()): string | number => {
    if (typeof target !== "string") return target;
    if (seen.has(target)) return target;
    seen.add(target);
    const labelIndex = labelIndexes.get(target);
    if (labelIndex === undefined) return target;
    const executableIndex = nextExecutableIndex(ops, labelIndex + 1);
    const executable = executableIndex === undefined ? undefined : ops[executableIndex];
    if (executable?.kind !== "jump" || hasRewriteBarrier(executable)) return target;
    return normalize(executable.target, seen);
  };

  return (target) => normalize(target);
}

function nextExecutableIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    if (ops[index]?.kind !== "label") return index;
  }
  return undefined;
}
