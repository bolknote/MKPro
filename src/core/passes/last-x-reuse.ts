import type { IrOp, RegisterName } from "../types.ts";
import { isStableIndirectSelector } from "../indirect-addressing.ts";
import { loopCounterRegister } from "./cfg.ts";
import {
  computeLabelEntryIndexes,
  computeX2RegisterStates,
  computeX2ValueStates,
  directReturnAnalysisContext,
  isKnownReturnCallOp,
  planRecallRemovalWithStackScheduler,
  plainPreservesXValue,
  removableRecallValueRegister,
  storedCurrentXValueRegister,
  type IrPass,
  type IrPassFn,
  type PassResult,
  x2KnownReturnCallPreservesStackXAndX2,
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
  const directReturnContext = directReturnAnalysisContext(ops);
  const labelEntries = computeLabelEntryIndexes(ops, { procedureBoundary: "start" });
  let xHolds: RegisterName | undefined;
  let canTrustValueX = true;
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label") {
      if (labelEntries.has(i)) {
        xHolds = undefined;
        canTrustValueX = false;
      }
      continue;
    }
    const storedRegister = storedCurrentXValueRegister(op);
    if (storedRegister !== undefined) {
      xHolds = storedRegister;
      canTrustValueX = true;
      continue;
    }
    const recallRegister = removableRecallValueRegister(op);
    const removalPlan = planRecallRemovalWithStackScheduler(
      ops,
      i,
      x2States[i],
      x2ValueStates[i],
      directReturnContext,
      { removedIndexes: removed },
    );
    if (
      recallRegister !== undefined &&
      (xHolds === recallRegister || (canTrustValueX && removalPlan?.analysis.valueProof?.inX === true)) &&
      removalPlan?.removable === true
    ) {
      removed.add(i);
      continue;
    }
    if (recallRegister !== undefined) {
      xHolds = recallRegister;
      canTrustValueX = true;
      continue;
    }
    if (isKnownReturnCallOp(op) && x2KnownReturnCallPreservesStackXAndX2(ops, op, directReturnContext)) {
      if (op.kind === "indirect-call" && !isStableIndirectSelector(op.register) && xHolds === op.register) {
        xHolds = undefined;
        canTrustValueX = false;
      }
      continue;
    }
    if (
      op.kind === "jump" ||
      op.kind === "call" ||
      op.kind === "indirect-jump" ||
      op.kind === "indirect-call" ||
      op.kind === "indirect-cjump" ||
      op.kind === "return" ||
      op.kind === "stop"
    ) {
      // Stops let the user resume; X (and possibly Y/Z/T) may have been
      // touched interactively. Drop the X-reuse assumption across stops.
      xHolds = undefined;
      canTrustValueX = false;
      continue;
    }
    if (op.kind === "cjump") {
      continue;
    }
    if (op.kind === "loop") {
      if (xHolds === loopCounterRegister(op.counter)) xHolds = undefined;
      canTrustValueX = false;
      continue;
    }
    if (clobbersX(op)) {
      xHolds = undefined;
      canTrustValueX = true;
    }
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

export const lastXReuse: IrPass = {
  name: "last-x-reuse",
  run,
  layoutSafe: false,
};
