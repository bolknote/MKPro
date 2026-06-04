import type { IrOp, RegisterName } from "../types.ts";
import { isStableIndirectSelector } from "../indirect-addressing.ts";
import { computeLiveness } from "./liveness-analysis.ts";
import {
  hasRewriteBarrier,
  knownIndirectMemoryTarget,
  type IrPass,
  type IrPassFn,
  type PassResult,
} from "./helpers.ts";

// A number-entry op (digit / '.' / sign / ВП) keeps the machine in entry mode.
function isNumberEntry(op: IrOp): boolean {
  return op.kind === "plain" && op.opcode <= 0x0c;
}

// Ops after which the machine is still mid number-entry, so a following number
// literal would concatenate: a digit-entry op, or a С/П that read user input.
function leavesEntryOpen(op: IrOp): boolean {
  if (isNumberEntry(op)) return true;
  return op.kind === "stop" && op.semantic === "input";
}

function previousEffectiveOp(ops: readonly IrOp[], index: number): IrOp | undefined {
  for (let i = index - 1; i >= 0; i -= 1) {
    if (ops[i]!.kind !== "label") return ops[i];
  }
  return undefined;
}

function nextEffectiveOp(ops: readonly IrOp[], index: number): IrOp | undefined {
  for (let i = index + 1; i < ops.length; i += 1) {
    if (ops[i]!.kind !== "label") return ops[i];
  }
  return undefined;
}

// Removing a store also drops the entry-finalizing side effect of X->П. If the
// store sits between an open number entry (a read or a digit run) and a fresh
// number literal, deleting it would let the two values concatenate on the real
// MK-61 (e.g. read 10 then 4 -> 104). Keep such stores even when their register
// is otherwise dead.
function finalizesNumberEntry(ops: readonly IrOp[], index: number): boolean {
  const prev = previousEffectiveOp(ops, index);
  const next = nextEffectiveOp(ops, index);
  return prev !== undefined && next !== undefined && leavesEntryOpen(prev) && isNumberEntry(next);
}

function isVpOp(op: IrOp): boolean {
  return op.kind === "plain" && op.opcode === 0x0c;
}

// In program mode, `ВП` observes the previous command class while restoring X2.
// `X->П r; ВП` is a documented X2 splice form even when r is dead as a memory
// cell, so DSE must keep that store.
function providesVpRestoreContext(ops: readonly IrOp[], index: number): boolean {
  const next = nextEffectiveOp(ops, index);
  return next !== undefined && isVpOp(next);
}

const run: IrPassFn = (ops) => {
  if (ops.length === 0) return { ops: [], applied: 0, optimizations: [] };
  const liveness = computeLiveness(ops);
  const removed = new Set<number>();
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    const target = deadStoreTarget(op);
    if (target === undefined) continue;
    if (hasRewriteBarrier(op)) continue;
    if (liveness.liveOut[i]!.has(target)) continue;
    if (finalizesNumberEntry(ops, i)) continue;
    if (providesVpRestoreContext(ops, i)) continue;
    removed.add(i);
  }
  if (removed.size === 0) {
    return { ops: [...ops], applied: 0, optimizations: [] };
  }
  const result: IrOp[] = [];
  for (let i = 0; i < ops.length; i += 1) {
    if (!removed.has(i)) result.push(ops[i]!);
  }
  const passResult: PassResult = {
    ops: result,
    applied: removed.size,
    optimizations: [
      {
        name: "dead-store-elimination",
        detail: `Removed ${removed.size} store(s) to register(s) never read before the next assignment.`,
      },
    ],
  };
  return passResult;
};

function deadStoreTarget(op: IrOp): RegisterName | undefined {
  if (op.kind === "store") return op.register;
  if (op.kind !== "indirect-store") return undefined;
  if (!isStableIndirectSelector(op.register)) return undefined;
  return knownIndirectMemoryTarget(op);
}

export const deadStoreElimination: IrPass = {
  name: "dead-store-elimination",
  run,
  layoutSafe: false,
};
