import type { IrOp, RegisterName } from "../types.ts";
import { getOpcode, registerIndex } from "../opcodes.ts";
import { computeLiveness } from "./liveness-analysis.ts";
import {
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  knownIndirectMemoryTarget,
  type IrPass,
  type IrPassFn,
} from "./helpers.ts";

const DIRECT_STORE_BASE = 0x40;
const DIRECT_RECALL_BASE = 0x60;

const LOOP_COUNTER_REGISTER: Record<string, RegisterName> = {
  L0: "0",
  L1: "1",
  L2: "2",
  L3: "3",
};

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
      const memoryTarget = knownIndirectMemoryTarget(op);
      if (memoryTarget !== undefined) set.add(memoryTarget);
    }
    if (op.kind === "loop") set.add(LOOP_COUNTER_REGISTER[op.counter]!);
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
      if (knownIndirectMemoryTarget(op) === register) return true;
    }
  }
  return false;
}

function usesDisplayFocusSensitiveAccess(ops: readonly IrOp[], register: RegisterName): boolean {
  return ops.some((op) =>
    (op.kind === "store" || op.kind === "recall") &&
    op.register === register &&
    isDisplayFocusSensitive(op)
  );
}

function usesLoopCounter(ops: readonly IrOp[], register: RegisterName): boolean {
  return ops.some((op) => op.kind === "loop" && LOOP_COUNTER_REGISTER[op.counter] === register);
}

function unionInto(target: Set<number>, source: ReadonlySet<number>): void {
  for (const value of source) target.add(value);
}

function coalescedOpcode(kind: "store" | "recall", register: RegisterName): number {
  const base = kind === "store" ? DIRECT_STORE_BASE : DIRECT_RECALL_BASE;
  return base + registerIndex(register);
}

function rewriteRegisterOp(op: IrOp, register: RegisterName): IrOp {
  if (op.kind !== "store" && op.kind !== "recall") return op;
  const opcode = coalescedOpcode(op.kind, register);
  return {
    ...op,
    register,
    opcode,
    meta: {
      ...op.meta,
      mnemonic: getOpcode(opcode).name,
    },
  };
}

function excludedRegisters(ops: readonly IrOp[], registers: ReadonlySet<RegisterName>): Set<RegisterName> {
  const excluded = new Set<RegisterName>();
  for (const reg of registers) {
    if (
      usesIndirectAccess(ops, reg) ||
      usesDisplayFocusSensitiveAccess(ops, reg) ||
      usesLoopCounter(ops, reg)
    ) {
      excluded.add(reg);
    }
  }
  return excluded;
}

function coalesceNonOverlapping(ops: readonly IrOp[]): { ops: IrOp[]; applied: number } {
  const registers = gatherUsedRegisters(ops);
  if (registers.size <= 1) return { ops: [...ops], applied: 0 };
  const liveness = computeLiveness(ops);
  const liveAtEntry = liveness.liveIn[0] ?? new Set<RegisterName>();
  const ranges = liveRangePerRegister(ops, registers);
  const ordered = [...registers].sort();
  const mapping = new Map<RegisterName, RegisterName>();
  for (let i = 0; i < ordered.length; i += 1) {
    const a = ordered[i]!;
    if (mapping.has(a)) continue;
    if (liveAtEntry.has(a)) continue;
    if (usesIndirectAccess(ops, a)) continue;
    if (usesDisplayFocusSensitiveAccess(ops, a)) continue;
    if (usesLoopCounter(ops, a)) continue;
    for (let j = i + 1; j < ordered.length; j += 1) {
      const b = ordered[j]!;
      if (mapping.has(b)) continue;
      if (liveAtEntry.has(b)) continue;
      if (usesIndirectAccess(ops, b)) continue;
      if (usesDisplayFocusSensitiveAccess(ops, b)) continue;
      if (usesLoopCounter(ops, b)) continue;
      const rangeA = ranges.get(a)!;
      const rangeB = ranges.get(b)!;
      if (intersects(rangeA, rangeB)) continue;
      mapping.set(b, a);
      unionInto(rangeA, rangeB);
      break;
    }
  }
  if (mapping.size === 0) return { ops: [...ops], applied: 0 };
  const result = ops.map((op) => {
    if (op.kind !== "store" && op.kind !== "recall") return op;
    const replacement = mapping.get(op.register);
    return replacement === undefined ? op : rewriteRegisterOp(op, replacement);
  });
  return { ops: result, applied: mapping.size };
}

// Form 2 — copy coalescing. A copy lowers to `recall S` immediately followed by
// `store D`. When D is assigned ONLY by that copy (so it never diverges from S),
// D's prior value is dead at the copy, and S is never stored while D is live
// (so they never hold different values), D and S can share one register: every
// read of D becomes a read of S and the now self-referential store is dropped,
// which removes a cell and frees D's register. The liveness-based interference
// check makes this sound even across the turn loop.
function coalesceCopies(ops: readonly IrOp[]): { ops: IrOp[]; applied: number } {
  const registers = gatherUsedRegisters(ops);
  if (registers.size <= 1) return { ops: [...ops], applied: 0 };
  const liveness = computeLiveness(ops);
  const excluded = excludedRegisters(ops, registers);

  const storeIndices = new Map<RegisterName, number[]>();
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "store") {
      if (!storeIndices.has(op.register)) storeIndices.set(op.register, []);
      storeIndices.get(op.register)!.push(i);
    }
  }

  const mapping = new Map<RegisterName, RegisterName>();
  const dropIndices = new Set<number>();
  for (let i = 0; i + 1 < ops.length; i += 1) {
    const rec = ops[i]!;
    const st = ops[i + 1]!;
    if (rec.kind !== "recall" || st.kind !== "store") continue;
    const source = rec.register;
    const dest = st.register;
    if (source === dest || mapping.has(dest) || mapping.has(source)) continue;
    if (excluded.has(source) || excluded.has(dest)) continue;
    // (1) dest assigned only by this copy.
    const destStores = storeIndices.get(dest) ?? [];
    if (destStores.length !== 1 || destStores[0] !== i + 1) continue;
    // (2) dest's prior value is dead at the copy (this copy is its only live def).
    if (liveness.liveIn[i]!.has(dest)) continue;
    // (3) source is never stored while dest is live -> they never diverge.
    let diverges = false;
    for (const j of storeIndices.get(source) ?? []) {
      if (liveness.liveOut[j]!.has(dest)) {
        diverges = true;
        break;
      }
    }
    if (diverges) continue;
    mapping.set(dest, source);
    dropIndices.add(i + 1);
  }

  if (mapping.size === 0) return { ops: [...ops], applied: 0 };
  const result: IrOp[] = [];
  for (let i = 0; i < ops.length; i += 1) {
    if (dropIndices.has(i)) continue;
    const op = ops[i]!;
    if (op.kind === "store" || op.kind === "recall") {
      const replacement = mapping.get(op.register);
      result.push(replacement === undefined ? op : rewriteRegisterOp(op, replacement));
    } else {
      result.push(op);
    }
  }
  return { ops: result, applied: mapping.size };
}

const run: IrPassFn = (ops, context) => {
  if (ops.length === 0) return { ops: [], applied: 0, optimizations: [] };
  if (ops.some((op) => hasRewriteBarrier(op))) {
    return { ops: [...ops], applied: 0, optimizations: [] };
  }

  const optimizations: { name: string; detail: string }[] = [];
  let current: IrOp[] = [...ops];
  let applied = 0;

  const nonOverlap = coalesceNonOverlapping(current);
  current = nonOverlap.ops;
  applied += nonOverlap.applied;
  if (nonOverlap.applied > 0) {
    optimizations.push({
      name: "register-coalesce",
      detail: `Coalesced ${nonOverlap.applied} non-overlapping register live range(s).`,
    });
  }

  if (context.options.coalesceCopies === true) {
    const copies = coalesceCopies(current);
    current = copies.ops;
    applied += copies.applied;
    if (copies.applied > 0) {
      optimizations.push({
        name: "copy-coalesce",
        detail: `Coalesced ${copies.applied} non-diverging copy assignment(s), dropping the copy and freeing a register.`,
      });
    }
  }

  if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  return { ops: current, applied, optimizations };
};

export const registerCoalesce: IrPass = {
  name: "register-coalesce",
  run,
  layoutSafe: true,
};
