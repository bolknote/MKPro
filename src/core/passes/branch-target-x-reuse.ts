import type { IrOp, RegisterName } from "../types.ts";
import {
  analyzeRecallRemoval,
  computeX2RegisterStates,
  computeX2ValueStates,
  emptyResult,
  hasRewriteBarrier,
  removableRecallValueRegister,
  type IrPass,
  type IrPassFn,
} from "./helpers.ts";

const run: IrPassFn = (ops) => {
  const labels = labelIndexes(ops);
  const references = targetReferenceCounts(ops);
  const x2States = computeX2RegisterStates(ops);
  const x2ValueStates = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const remove = new Set<number>();

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if ((op.kind !== "cjump" && op.kind !== "loop") || typeof op.target !== "string" || hasRewriteBarrier(op)) {
      continue;
    }

    if ((references.get(op.target) ?? 0) !== 1) continue;

    const labelIndex = labels.get(op.target);
    if (labelIndex === undefined || hasFallthroughIntoLabel(ops, labelIndex)) continue;

    const targetIndex = nextExecutableIndex(ops, labelIndex + 1);
    if (targetIndex === undefined || remove.has(targetIndex)) continue;
    const target = ops[targetIndex]!;
    const targetRegister = removableRecallValueRegister(target);
    if (targetRegister === undefined) continue;
    if (op.kind === "loop" && loopCounterRegister(op.counter) === targetRegister) continue;
    const preservedRegister = branchPreservedRegister(ops, index, op);
    const removal = analyzeRecallRemoval(ops, targetIndex, x2States[targetIndex], x2ValueStates[targetIndex]);
    if (preservedRegister !== targetRegister && removal?.valueProof?.inX !== true) continue;
    if (removal?.removable !== true) {
      continue;
    }

    remove.add(targetIndex);
  }

  if (remove.size === 0) return emptyResult(ops);

  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [{
      name: "branch-target-x-reuse",
      detail: `Dropped ${remove.size} branch-target recall${remove.size === 1 ? "" : "s"} already preserved in X by the branch path.`,
    }],
  };
};

export const branchTargetXReuse: IrPass = {
  name: "branch-target-x-reuse",
  run,
  layoutSafe: false,
};

function branchPreservedRegister(
  ops: readonly IrOp[],
  branchIndex: number,
  branch: Extract<IrOp, { kind: "cjump" | "loop" }>,
): RegisterName | undefined {
  const register = immediatelyHeldRegister(ops, branchIndex);
  if (register === undefined) return undefined;
  if (branch.kind === "loop" && loopCounterRegister(branch.counter) === register) return undefined;
  return register;
}

function immediatelyHeldRegister(ops: readonly IrOp[], branchIndex: number): RegisterName | undefined {
  for (let index = branchIndex - 1; index >= 0; index -= 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    return removableRecallValueRegister(op);
  }
  return undefined;
}

function loopCounterRegister(counter: Extract<IrOp, { kind: "loop" }>["counter"]): RegisterName {
  switch (counter) {
    case "L0":
      return "0";
    case "L1":
      return "1";
    case "L2":
      return "2";
    case "L3":
      return "3";
  }
}

function labelIndexes(ops: readonly IrOp[]): Map<string, number> {
  const result = new Map<string, number>();
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") result.set(op.name, index);
  }
  return result;
}

function targetReferenceCounts(ops: readonly IrOp[]): Map<string, number> {
  const result = new Map<string, number>();
  for (const op of ops) {
    const target = stringTarget(op);
    if (target !== undefined) result.set(target, (result.get(target) ?? 0) + 1);
  }
  return result;
}

function stringTarget(op: IrOp): string | undefined {
  switch (op.kind) {
    case "jump":
    case "cjump":
    case "call":
    case "loop":
      return typeof op.target === "string" ? op.target : undefined;
    default:
      return undefined;
  }
}

function hasFallthroughIntoLabel(ops: readonly IrOp[], labelIndex: number): boolean {
  const previous = previousExecutableIndex(ops, labelIndex - 1);
  if (previous === undefined) return false;
  return !isNoFallthrough(ops[previous]!);
}

function previousExecutableIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index >= 0; index -= 1) {
    if (ops[index]?.kind !== "label") return index;
  }
  return undefined;
}

function nextExecutableIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    if (ops[index]?.kind !== "label") return index;
  }
  return undefined;
}

function isNoFallthrough(op: IrOp): boolean {
  return op.kind === "jump" || op.kind === "indirect-jump" || op.kind === "return" || op.kind === "stop";
}
