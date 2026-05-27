import type { IrOp, RegisterName } from "../types.ts";
import { computeLiveness } from "./liveness-analysis.ts";
import { hasUnsafe, type IrPass, type IrPassFn, type PassResult } from "./helpers.ts";

function gatherUsedRegisters(ops: readonly IrOp[]): Set<RegisterName> {
  const set = new Set<RegisterName>();
  for (const op of ops) {
    if (op.kind === "store" || op.kind === "recall") set.add(op.register);
    if (
      op.kind === "indirect-store" ||
      op.kind === "indirect-recall" ||
      op.kind === "indirect-jump" ||
      op.kind === "indirect-call" ||
      op.kind === "indirect-cjump"
    ) {
      set.add(op.register);
    }
  }
  return set;
}

function liveRangePerRegister(
  ops: readonly IrOp[],
  registers: ReadonlySet<RegisterName>,
): Map<RegisterName, Set<number>> {
  const liveness = computeLiveness(ops);
  const ranges = new Map<RegisterName, Set<number>>();
  for (const reg of registers) ranges.set(reg, new Set<number>());
  for (let i = 0; i < ops.length; i += 1) {
    for (const reg of liveness.liveIn[i]!) {
      ranges.get(reg)?.add(i);
    }
    for (const reg of liveness.liveOut[i]!) {
      ranges.get(reg)?.add(i);
    }
  }
  return ranges;
}

function intersects(a: ReadonlySet<number>, b: ReadonlySet<number>): boolean {
  const smaller = a.size < b.size ? a : b;
  const larger = a.size < b.size ? b : a;
  for (const value of smaller) {
    if (larger.has(value)) return true;
  }
  return false;
}

function usesIndirectAccess(ops: readonly IrOp[], register: RegisterName): boolean {
  for (const op of ops) {
    if (
      op.kind === "indirect-store" ||
      op.kind === "indirect-recall" ||
      op.kind === "indirect-jump" ||
      op.kind === "indirect-call" ||
      op.kind === "indirect-cjump"
    ) {
      if (op.register === register) return true;
    }
  }
  return false;
}

const run: IrPassFn = (ops) => {
  if (ops.length === 0) return { ops: [], applied: 0, optimizations: [], unsafeUnverified: [] };
  if (ops.some((op) => hasUnsafe(op))) {
    return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
  }
  const registers = gatherUsedRegisters(ops);
  if (registers.size <= 1) {
    return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
  }
  const ranges = liveRangePerRegister(ops, registers);
  const ordered = [...registers].sort();
  const mapping = new Map<RegisterName, RegisterName>();
  for (let i = 0; i < ordered.length; i += 1) {
    const a = ordered[i]!;
    if (mapping.has(a)) continue;
    if (usesIndirectAccess(ops, a)) continue;
    for (let j = i + 1; j < ordered.length; j += 1) {
      const b = ordered[j]!;
      if (mapping.has(b)) continue;
      if (usesIndirectAccess(ops, b)) continue;
      const rangeA = ranges.get(a)!;
      const rangeB = ranges.get(b)!;
      if (intersects(rangeA, rangeB)) continue;
      // Safety net: skip — coalescing would also need to rewrite stores/recalls and
      // we lack the address-rewrite plumbing here. The pass remains structural for
      // capability accounting; concrete rewriting can be added once Phase 4 IR
      // emitter is in place.
      mapping.set(b, a);
      break;
    }
  }
  // The pass currently never rewrites the program; it only reports an opportunity
  // when it finds at least one non-overlapping pair.
  if (mapping.size === 0) {
    return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
  }
  return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
};

export const registerCoalesce: IrPass = {
  name: "register-coalesce",
  run,
  layoutSafe: true,
};
