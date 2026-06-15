import type { IrOp, RegisterName } from "../types.ts";
import { isStableIndirectSelector } from "../indirect-addressing.ts";
import { buildCfgEdges, buildTargetIndexes, loopCounterRegister, type CfgEdge } from "./cfg.ts";
import {
  hasRewriteBarrier,
  knownIndirectFlowTarget,
  knownIndirectMemoryTarget,
  plainPreservesXValue,
  removableRecallValueRegister,
  storedCurrentXValueRegister,
  type IrPass,
  type IrPassFn,
} from "./helpers.ts";
import { runRecallRemovalPass } from "./recall-removal.ts";

type XRegisterSet = ReadonlySet<RegisterName>;

const run: IrPassFn = (ops) =>
  runRecallRemovalPass(
    ops,
    {
      name: "flow-x-reuse",
      detail: (count) =>
        `Dropped ${count} recall${count === 1 ? "" : "s"} whose register value already reaches the point in X on every CFG predecessor.`,
    },
    (engine) => {
      if (ops.length === 0) return;
      if (hasUnknownIndirectFlow(ops)) return;
      const numericTargets = numericFlowTargetLayoutGuard(ops);
      if (numericTargets === undefined) return;

      const graph = buildCfgEdges(ops);
      const inStates = computeXRegisterStates(ops, graph);

      for (let index = 0; index < ops.length; index += 1) {
        const op = ops[index]!;
        const recallRegister = removableRecallValueRegister(op);
        if (recallRegister === undefined) continue;
        if (!numericTargets.canRemoveAt(index)) continue;
        const removalPlan = engine.plan(index);
        if (removalPlan?.removable !== true) continue;
        const alreadyInX =
          inStates[index]?.has(recallRegister) === true ||
          removalPlan.analysis.valueProof?.inX === true;
        if (alreadyInX) engine.removed.add(index);
      }
    },
  );

export const flowXReuse: IrPass = {
  name: "flow-x-reuse",
  run,
  layoutSafe: false,
};

function computeXRegisterStates(ops: readonly IrOp[], graph: CfgEdge[][]): Array<XRegisterSet | undefined> {
  const inStates: Array<Set<RegisterName> | undefined> = Array.from({ length: ops.length }, () => undefined);
  const outStates: Array<Set<RegisterName> | undefined> = Array.from({ length: ops.length }, () => undefined);
  inStates[0] = new Set();

  let changed = true;
  let iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    iterations += 1;

    for (let index = 0; index < ops.length; index += 1) {
      const input = inStates[index];
      if (input === undefined) continue;

      const output = transferXSet(input, ops[index]!, "normal");
      if (!sameSet(output, outStates[index])) {
        outStates[index] = output;
        changed = true;
      }

      for (const edge of graph[index] ?? []) {
        const edgeOutput = edge.kind === "normal" ? output : transferXSet(input, ops[index]!, edge.kind);
        const joined = joinXSets(inStates[edge.target], edgeOutput);
        if (!sameSet(joined, inStates[edge.target])) {
          inStates[edge.target] = joined;
          changed = true;
        }
      }
    }
  }

  return inStates;
}

function transferXSet(input: XRegisterSet, op: IrOp, edge: CfgEdge["kind"]): Set<RegisterName> {
  if (hasRewriteBarrier(op)) return new Set();

  switch (op.kind) {
    case "label":
    case "jump":
    case "cjump":
    case "orphan-address":
      return new Set(input);
    case "store":
    case "indirect-store": {
      const target = storedCurrentXValueRegister(op);
      return target === undefined ? new Set() : addRegister(input, target);
    }
    case "recall":
      return new Set([op.register]);
    case "indirect-recall": {
      const target = knownIndirectMemoryTarget(op);
      return target === undefined ? new Set() : new Set([target]);
    }
    case "plain":
      return plainPreservesXValue(op) ? new Set(input) : new Set();
    case "stop":
      return new Set();
    case "loop":
      return removeRegister(input, loopCounterRegister(op.counter));
    case "call":
    case "return":
      return new Set(input);
    case "indirect-jump":
    case "indirect-call":
      return transferIndirectFlowXSet(input, op.register);
    case "indirect-cjump":
      return edge === "jump" ? transferIndirectFlowXSet(input, op.register) : new Set(input);
  }
}

function transferIndirectFlowXSet(input: XRegisterSet, register: RegisterName): Set<RegisterName> {
  const output = new Set(input);
  if (!isStableIndirectSelector(register)) output.delete(register);
  return output;
}

function addRegister(input: XRegisterSet, register: RegisterName): Set<RegisterName> {
  const output = new Set(input);
  output.add(register);
  return output;
}

function removeRegister(input: XRegisterSet, register: RegisterName): Set<RegisterName> {
  const output = new Set(input);
  output.delete(register);
  return output;
}

function joinXSets(current: Set<RegisterName> | undefined, incoming: XRegisterSet): Set<RegisterName> {
  if (current === undefined) return new Set(incoming);
  const joined = new Set<RegisterName>();
  for (const register of current) {
    if (incoming.has(register)) joined.add(register);
  }
  return joined;
}

function sameSet(left: XRegisterSet | undefined, right: XRegisterSet | undefined): boolean {
  if (left === undefined || right === undefined) return left === right;
  if (left.size !== right.size) return false;
  for (const value of left) {
    if (!right.has(value)) return false;
  }
  return true;
}

interface NumericFlowTargetLayoutGuard {
  canRemoveAt(index: number): boolean;
}

function numericFlowTargetLayoutGuard(ops: readonly IrOp[]): NumericFlowTargetLayoutGuard | undefined {
  const { addressIndex } = buildTargetIndexes(ops);
  let latestTargetIndex = -1;
  for (const op of ops) {
    const target = numericFlowTarget(op);
    if (target === undefined) continue;
    const targetIndex = addressIndex.get(target);
    if (targetIndex === undefined) return undefined;
    latestTargetIndex = Math.max(latestTargetIndex, targetIndex);
  }
  return {
    canRemoveAt: (index) => index >= latestTargetIndex,
  };
}

function numericFlowTarget(op: IrOp): number | undefined {
  switch (op.kind) {
    case "jump":
    case "cjump":
    case "call":
    case "loop":
      return typeof op.target === "number" ? op.target : undefined;
    case "orphan-address":
      return typeof op.target === "number" ? op.target : undefined;
    default:
      return undefined;
  }
}

function hasUnknownIndirectFlow(ops: readonly IrOp[]): boolean {
  return ops.some((op) =>
    (op.kind === "indirect-jump" ||
      op.kind === "indirect-call" ||
      op.kind === "indirect-cjump") &&
    knownIndirectFlowTarget(op) === undefined
  );
}
