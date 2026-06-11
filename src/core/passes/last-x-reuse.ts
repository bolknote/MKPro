import type { IrOp, RegisterName } from "../types.ts";
import { isStableIndirectSelector } from "../indirect-addressing.ts";
import { loopCounterRegister } from "./cfg.ts";
import {
  computeLabelEntryIndexes,
  isKnownReturnCallOp,
  plainPreservesXValue,
  removableRecallValueRegister,
  storedCurrentXValueRegister,
  type IrPass,
  type IrPassFn,
  x2KnownReturnCallPreservesStackXAndX2,
} from "./helpers.ts";
import { runRecallRemovalPass } from "./recall-removal.ts";

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

const run: IrPassFn = (ops) =>
  runRecallRemovalPass(
    ops,
    {
      name: "last-x-reuse",
      detail: (count) => `Dropped ${count} recall(s) whose register value was already in X.`,
    },
    (engine) => {
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
        const removalPlan = engine.plan(i);
        if (
          recallRegister !== undefined &&
          (xHolds === recallRegister || (canTrustValueX && removalPlan?.analysis.valueProof?.inX === true)) &&
          removalPlan?.removable === true
        ) {
          engine.removed.add(i);
          continue;
        }
        if (recallRegister !== undefined) {
          xHolds = recallRegister;
          canTrustValueX = true;
          continue;
        }
        if (isKnownReturnCallOp(op) && x2KnownReturnCallPreservesStackXAndX2(ops, op, engine.directReturnContext())) {
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
    },
  );

export const lastXReuse: IrPass = {
  name: "last-x-reuse",
  run,
  layoutSafe: false,
};
