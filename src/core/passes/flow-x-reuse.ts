import type { IrOp, RegisterName } from "../types.ts";
import {
  cellsPerOp,
  emptyResult,
  hasRewriteBarrier,
  knownIndirectMemoryTarget,
  removingRecallCanExposeStackLift,
  removingRecallCanExposeX2Restore,
  type IrPass,
  type IrPassFn,
} from "./helpers.ts";

type XRegisterSet = ReadonlySet<RegisterName>;

interface Graph {
  successors: number[][];
}

const run: IrPassFn = (ops) => {
  if (ops.length === 0) return emptyResult(ops);
  if (hasNumericFlowTarget(ops) || hasIndirectFlow(ops)) return emptyResult(ops);

  const graph = buildGraph(ops);
  const inStates = computeXRegisterStates(ops, graph);
  const remove = new Set<number>();

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind !== "recall" || hasRewriteBarrier(op)) continue;
    if (removingRecallCanExposeStackLift(ops, index)) continue;
    if (removingRecallCanExposeX2Restore(ops, index)) continue;
    if (inStates[index]?.has(op.register) === true) remove.add(index);
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

      const output = transferXSet(input, ops[index]!);
      if (!sameSet(output, outStates[index])) {
        outStates[index] = output;
        changed = true;
      }

      for (const successor of graph.successors[index] ?? []) {
        const joined = joinXSets(inStates[successor], output);
        if (!sameSet(joined, inStates[successor])) {
          inStates[successor] = joined;
          changed = true;
        }
      }
    }
  }

  return inStates;
}

function transferXSet(input: XRegisterSet, op: IrOp): Set<RegisterName> {
  if (hasRewriteBarrier(op)) return new Set();

  switch (op.kind) {
    case "label":
    case "jump":
    case "cjump":
    case "orphan-address":
      return new Set(input);
    case "store":
      return addRegister(input, op.register);
    case "recall":
      return new Set([op.register]);
    case "indirect-store": {
      const target = knownIndirectMemoryTarget(op);
      return target === undefined ? new Set() : addRegister(input, target);
    }
    case "indirect-recall": {
      const target = knownIndirectMemoryTarget(op);
      return target === undefined ? new Set() : new Set([target]);
    }
    case "plain":
    case "stop":
    case "call":
    case "loop":
    case "indirect-jump":
    case "indirect-call":
    case "indirect-cjump":
    case "return":
      return new Set();
  }
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
  const successors: number[][] = Array.from({ length: ops.length }, () => []);
  const callReturns: number[] = [];
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    const next = index + 1;
    if (op.kind === "call" && next < ops.length) callReturns.push(next);
  }

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    const next = index + 1;
    const fallthrough = (): void => {
      if (next < ops.length) successors[index]!.push(next);
    };
    const jumpTo = (target: string | number): void => {
      const targetIndex = typeof target === "string" ? labelIndex.get(target) : addressIndex.get(target);
      if (targetIndex !== undefined) successors[index]!.push(targetIndex);
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
      case "return":
        successors[index]!.push(...callReturns);
        break;
      case "indirect-jump":
      case "indirect-call":
      case "indirect-cjump":
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

function hasIndirectFlow(ops: readonly IrOp[]): boolean {
  return ops.some((op) =>
    op.kind === "indirect-jump" ||
    op.kind === "indirect-call" ||
    op.kind === "indirect-cjump"
  );
}
