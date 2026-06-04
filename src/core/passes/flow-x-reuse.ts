import type { IrOp, RegisterName } from "../types.ts";
import { isStableIndirectSelector } from "../indirect-addressing.ts";
import {
  cellsPerOp,
  computeX2RegisterStates,
  computeX2ValueStates,
  emptyResult,
  hasRewriteBarrier,
  knownIndirectFlowTarget,
  knownIndirectMemoryTarget,
  plainPreservesXValue,
  recallAlreadyInXDecimalMemory,
  recallAlreadyInXPreloadedDecimal,
  recallAlreadySyncedInX2,
  recallAlreadySyncedInX2DecimalMemory,
  recallAlreadySyncedInX2PreloadedDecimal,
  recallAlreadySyncedInX2Value,
  removableRecallValueRegister,
  removingRecallCanExposeStackLift,
  removingRecallCanExposeX2Restore,
  storedCurrentXValueRegister,
  type IrPass,
  type IrPassFn,
} from "./helpers.ts";

type XRegisterSet = ReadonlySet<RegisterName>;

interface Graph {
  successors: Edge[][];
}

interface Edge {
  readonly target: number;
  readonly kind: "normal" | "fallthrough" | "jump";
}

const run: IrPassFn = (ops) => {
  if (ops.length === 0) return emptyResult(ops);
  if (hasNumericFlowTarget(ops) || hasUnknownIndirectFlow(ops)) return emptyResult(ops);

  const graph = buildGraph(ops);
  const inStates = computeXRegisterStates(ops, graph);
  const x2States = computeX2RegisterStates(ops);
  const x2ValueStates = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const remove = new Set<number>();

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    const recallRegister = removableRecallValueRegister(op);
    if (recallRegister === undefined) continue;
    if (removingRecallCanExposeStackLift(ops, index)) continue;
    const redundantSyncRegister =
      recallAlreadySyncedInX2(op, x2States[index]) ??
      recallAlreadySyncedInX2Value(op, x2ValueStates[index]);
    const redundantSyncValue =
      (recallAlreadySyncedInX2DecimalMemory(op, x2ValueStates[index]) ??
        recallAlreadySyncedInX2PreloadedDecimal(op, x2ValueStates[index])) !== undefined;
    if (removingRecallCanExposeX2Restore(ops, index, { redundantSyncRegister, redundantSyncValue })) continue;
    const alreadyInX =
      inStates[index]?.has(recallRegister) === true ||
      recallAlreadyInXDecimalMemory(op, x2ValueStates[index]) === recallRegister ||
      recallAlreadyInXPreloadedDecimal(op, x2ValueStates[index]) === recallRegister;
    if (alreadyInX) remove.add(index);
  }

  if (remove.size === 0) return emptyResult(ops);

  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [{
      name: "flow-x-reuse",
      detail: `Dropped ${remove.size} recall${remove.size === 1 ? "" : "s"} whose register value already reaches the point in X on every CFG predecessor.`,
    }],
  };
};

export const flowXReuse: IrPass = {
  name: "flow-x-reuse",
  run,
  layoutSafe: false,
};

function computeXRegisterStates(ops: readonly IrOp[], graph: Graph): Array<XRegisterSet | undefined> {
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

      for (const edge of graph.successors[index] ?? []) {
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

function transferXSet(input: XRegisterSet, op: IrOp, edge: Edge["kind"]): Set<RegisterName> {
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
    case "loop":
      return new Set();
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

function buildGraph(ops: readonly IrOp[]): Graph {
  const { labelIndex, addressIndex } = buildTargetIndexes(ops);
  const successors: Edge[][] = Array.from({ length: ops.length }, () => []);
  const callReturns: number[] = [];
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    const next = index + 1;
    if ((op.kind === "call" || op.kind === "indirect-call") && next < ops.length) callReturns.push(next);
  }

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    const next = index + 1;
    const fallthrough = (): void => {
      if (next < ops.length) successors[index]!.push({ target: next, kind: "fallthrough" });
    };
    const jumpTo = (target: string | number): void => {
      const targetIndex = typeof target === "string" ? labelIndex.get(target) : addressIndex.get(target);
      if (targetIndex !== undefined) successors[index]!.push({ target: targetIndex, kind: "jump" });
    };
    const normal = (target: number): void => {
      successors[index]!.push({ target, kind: "normal" });
    };

    switch (op.kind) {
      case "label":
      case "store":
      case "recall":
      case "indirect-store":
      case "indirect-recall":
      case "plain":
      case "orphan-address":
      case "stop":
        fallthrough();
        break;
      case "jump":
        jumpTo(op.target);
        break;
      case "cjump":
        jumpTo(op.target);
        fallthrough();
        break;
      case "loop":
        jumpTo(op.target);
        fallthrough();
        break;
      case "call":
        jumpTo(op.target);
        break;
      case "indirect-jump": {
        const target = knownIndirectFlowTarget(op);
        if (target !== undefined) jumpTo(target);
        break;
      }
      case "indirect-call": {
        const target = knownIndirectFlowTarget(op);
        if (target !== undefined) jumpTo(target);
        break;
      }
      case "indirect-cjump": {
        const target = knownIndirectFlowTarget(op);
        if (target !== undefined) jumpTo(target);
        fallthrough();
        break;
      }
      case "return":
        for (const target of callReturns) normal(target);
        break;
    }
  }

  return { successors };
}

function buildTargetIndexes(ops: readonly IrOp[]): {
  labelIndex: Map<string, number>;
  addressIndex: Map<number, number>;
} {
  const labelIndex = new Map<string, number>();
  const addressIndex = new Map<number, number>();
  let address = 0;
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      labelIndex.set(op.name, index);
      continue;
    }
    addressIndex.set(address, index);
    address += cellsPerOp(op);
  }
  return { labelIndex, addressIndex };
}

function hasNumericFlowTarget(ops: readonly IrOp[]): boolean {
  return ops.some((op) => {
    switch (op.kind) {
      case "jump":
      case "cjump":
      case "call":
      case "loop":
        return typeof op.target === "number";
      case "orphan-address":
        return typeof op.target === "number";
      default:
        return false;
    }
  });
}

function hasUnknownIndirectFlow(ops: readonly IrOp[]): boolean {
  return ops.some((op) =>
    (op.kind === "indirect-jump" ||
      op.kind === "indirect-call" ||
      op.kind === "indirect-cjump") &&
    knownIndirectFlowTarget(op) === undefined
  );
}
