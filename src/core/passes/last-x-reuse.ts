import type { IrOp, RegisterName } from "../types.ts";
import {
  cellsPerOp,
  computeX2RegisterStates,
  computeX2ValueStates,
  knownIndirectFlowTarget,
  plainPreservesXValue,
  recallAlreadyInXDecimalMemory,
  recallAlreadySyncedInX2,
  recallAlreadySyncedInX2DecimalMemory,
  recallAlreadySyncedInX2Value,
  removableRecallValueRegister,
  removingRecallCanExposeStackLift,
  removingRecallCanExposeX2Restore,
  storedCurrentXValueRegister,
  type IrPass,
  type IrPassFn,
  type PassResult,
} from "./helpers.ts";

function clobbersX(op: IrOp): boolean {
  switch (op.kind) {
    case "label":
    case "orphan-address":
      return false;
    case "store":
      return false;
    case "recall":
    case "indirect-recall":
      return true;
    case "indirect-store":
      return false;
    case "plain":
      // Most ALU, swap, push, digit, and K-function commands touch X; documented
      // empty operators are the exception and keep the current value.
      return !plainPreservesXValue(op);
    case "stop":
      return false;
    case "jump":
    case "cjump":
    case "call":
    case "loop":
    case "indirect-jump":
    case "indirect-call":
    case "indirect-cjump":
    case "return":
      return false;
  }
}

const run: IrPassFn = (ops) => {
  const removed = new Set<number>();
  const x2States = computeX2RegisterStates(ops);
  const x2ValueStates = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const labelEntries = labelEntryIndexes(ops);
  let xHolds: RegisterName | undefined;
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label") {
      if (labelEntries.has(i)) xHolds = undefined;
      continue;
    }
    const storedRegister = storedCurrentXValueRegister(op);
    if (storedRegister !== undefined) {
      xHolds = storedRegister;
      continue;
    }
    const recallRegister = removableRecallValueRegister(op);
    const xAlreadyHasRecallValue = recallAlreadyInXDecimalMemory(op, x2ValueStates[i]) === recallRegister;
    const x2AlreadyHasRecallValue =
      recallAlreadySyncedInX2(op, x2States[i]) ??
      recallAlreadySyncedInX2Value(op, x2ValueStates[i]);
    const x2AlreadyHasRecallDecimal = recallAlreadySyncedInX2DecimalMemory(op, x2ValueStates[i]);
    if (
      recallRegister !== undefined &&
      (xHolds === recallRegister || xAlreadyHasRecallValue) &&
      !removingRecallCanExposeStackLift(ops, i) &&
      !removingRecallCanExposeX2Restore(ops, i, {
        redundantSyncRegister: x2AlreadyHasRecallValue,
        redundantSyncValue: x2AlreadyHasRecallDecimal !== undefined,
      })
    ) {
      removed.add(i);
      continue;
    }
    if (recallRegister !== undefined) {
      xHolds = recallRegister;
      continue;
    }
    if (
      op.kind === "jump" ||
      op.kind === "cjump" ||
      op.kind === "call" ||
      op.kind === "loop" ||
      op.kind === "indirect-jump" ||
      op.kind === "indirect-call" ||
      op.kind === "indirect-cjump" ||
      op.kind === "return" ||
      op.kind === "stop"
    ) {
      // Stops let the user resume; X (and possibly Y/Z/T) may have been
      // touched interactively. Drop the X-reuse assumption across stops.
      xHolds = undefined;
      continue;
    }
    if (clobbersX(op)) xHolds = undefined;
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
        name: "last-x-reuse",
        detail: `Dropped ${removed.size} recall(s) whose register value was already in X.`,
      },
    ],
  };
  return passResult;
};

function labelEntryIndexes(ops: readonly IrOp[]): Set<number> {
  const stringTargets = new Set<string>();
  const numericTargets = new Set<number>();
  let unknownIndirectFlow = false;
  for (const op of ops) {
    const target = flowTarget(op);
    if (typeof target === "string") stringTargets.add(target);
    if (typeof target === "number") numericTargets.add(target);
    const indirectTarget = indirectFlowTarget(op);
    if (indirectTarget === undefined && isIndirectFlow(op)) unknownIndirectFlow = true;
    if (indirectTarget !== undefined) numericTargets.add(indirectTarget);
  }

  const result = new Set<number>();
  let address = 0;
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      if (
        op.procedureBoundary === "start" ||
        unknownIndirectFlow ||
        stringTargets.has(op.name) ||
        numericTargets.has(address)
      ) {
        result.add(index);
      }
      continue;
    }
    address += cellsPerOp(op);
  }
  return result;
}

function flowTarget(op: IrOp): string | number | undefined {
  switch (op.kind) {
    case "jump":
    case "cjump":
    case "call":
    case "loop":
      return op.target;
    case "orphan-address":
      return op.target;
    default:
      return undefined;
  }
}

function isIndirectFlow(op: IrOp): op is Extract<IrOp, { kind: "indirect-jump" | "indirect-call" | "indirect-cjump" }> {
  return op.kind === "indirect-jump" || op.kind === "indirect-call" || op.kind === "indirect-cjump";
}

function indirectFlowTarget(op: IrOp): number | undefined {
  return isIndirectFlow(op) ? knownIndirectFlowTarget(op) : undefined;
}

export const lastXReuse: IrPass = {
  name: "last-x-reuse",
  run,
  layoutSafe: false,
};
