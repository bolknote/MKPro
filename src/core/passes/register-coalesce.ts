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

const run: IrPassFn = (ops) => {
  if (ops.length === 0) return { ops: [], applied: 0, optimizations: [] };
  if (ops.some((op) => hasRewriteBarrier(op))) {
    return { ops: [...ops], applied: 0, optimizations: [] };
  }
  const registers = gatherUsedRegisters(ops);
  if (registers.size <= 1) {
    return { ops: [...ops], applied: 0, optimizations: [] };
  }
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
  if (mapping.size === 0) {
    return { ops: [...ops], applied: 0, optimizations: [] };
  }
  const result = ops.map((op) => {
    if (op.kind !== "store" && op.kind !== "recall") return op;
    const replacement = mapping.get(op.register);
    return replacement === undefined ? op : rewriteRegisterOp(op, replacement);
  });
  return {
    ops: result,
    applied: mapping.size,
    optimizations: [
      {
        name: "register-coalesce",
        detail: `Coalesced ${mapping.size} non-overlapping register live range(s).`,
      },
    ],
  };
};

export const registerCoalesce: IrPass = {
  name: "register-coalesce",
  run,
  layoutSafe: true,
};
