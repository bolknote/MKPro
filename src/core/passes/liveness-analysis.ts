import type { IrLoopCounter, IrOp, RegisterName } from "../types.ts";
import { cellsPerOp, knownIndirectMemoryTarget } from "./helpers.ts";

export interface LivenessInfo {
  readonly liveIn: ReadonlyArray<ReadonlySet<RegisterName>>;
  readonly liveOut: ReadonlyArray<ReadonlySet<RegisterName>>;
}

interface SuccessorMap {
  readonly successors: number[][];
}

function buildTargetIndexes(ops: readonly IrOp[]): {
  labelIndex: Map<string, number>;
  addressIndex: Map<number, number>;
} {
  const labelIndex = new Map<string, number>();
  const addressIndex = new Map<number, number>();
  let address = 0;
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label") {
      labelIndex.set(op.name, i);
      continue;
    }
    addressIndex.set(address, i);
    address += cellsPerOp(op);
  }
  return { labelIndex, addressIndex };
}

function buildSuccessors(ops: readonly IrOp[]): SuccessorMap {
  const { labelIndex, addressIndex } = buildTargetIndexes(ops);
  const successors: number[][] = Array.from({ length: ops.length }, () => []);
  const callReturns: number[] = [];
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    const next = i + 1;
    if ((op.kind === "call" || op.kind === "indirect-call") && next < ops.length) {
      callReturns.push(next);
    }
  }
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    const next = i + 1;
    const fallthrough = (): void => {
      if (next < ops.length) successors[i]!.push(next);
    };
    const jumpTo = (target: string | number): void => {
      if (typeof target === "string") {
        const idx = labelIndex.get(target);
        if (idx !== undefined) successors[i]!.push(idx);
      } else {
        const idx = addressIndex.get(target);
        if (idx !== undefined) successors[i]!.push(idx);
      }
    };
    switch (op.kind) {
      case "label":
      case "store":
      case "recall":
      case "indirect-store":
      case "indirect-recall":
      case "plain":
      case "orphan-address":
        fallthrough();
        break;
      case "stop":
        // On MK-61, С/П pauses execution; pressing С/П again resumes from the next
        // cell. Treat every stop as falling through for control-flow analysis.
        fallthrough();
        break;
      case "return":
        successors[i]!.push(...callReturns);
        break;
      case "jump":
        jumpTo(op.target);
        break;
      case "cjump":
      case "loop":
        jumpTo(op.target);
        fallthrough();
        break;
      case "call":
        jumpTo(op.target);
        fallthrough();
        break;
      case "indirect-jump":
        break;
      case "indirect-call":
        fallthrough();
        break;
      case "indirect-cjump":
        fallthrough();
        break;
    }
  }
  return { successors };
}

function defsAndUses(op: IrOp): { defs: ReadonlyArray<RegisterName>; uses: ReadonlyArray<RegisterName> } {
  switch (op.kind) {
    case "store":
      return { defs: [op.register], uses: [] };
    case "recall":
      return { defs: [], uses: [op.register] };
    case "indirect-recall": {
      const target = knownIndirectMemoryTarget(op);
      return {
        defs: [],
        uses: target === undefined ? [op.register] : [op.register, target],
      };
    }
    case "indirect-store": {
      const target = knownIndirectMemoryTarget(op);
      return {
        defs: target === undefined ? [] : [target],
        uses: [op.register],
      };
    }
    case "indirect-jump":
    case "indirect-call":
    case "indirect-cjump":
      return { defs: [], uses: [op.register] };
    case "loop": {
      const register = loopCounterRegister(op.counter);
      return { defs: [register], uses: [register] };
    }
    default:
      return { defs: [], uses: [] };
  }
}

function loopCounterRegister(counter: IrLoopCounter): RegisterName {
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

export function computeLiveness(ops: readonly IrOp[]): LivenessInfo {
  const { successors } = buildSuccessors(ops);
  const n = ops.length;
  const liveIn: Set<RegisterName>[] = Array.from({ length: n }, () => new Set<RegisterName>());
  const liveOut: Set<RegisterName>[] = Array.from({ length: n }, () => new Set<RegisterName>());
  let changed = true;
  let iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    iterations += 1;
    for (let i = n - 1; i >= 0; i -= 1) {
      const op = ops[i]!;
      const succ = successors[i]!;
      const newOut = new Set<RegisterName>();
      for (const s of succ) {
        for (const reg of liveIn[s]!) newOut.add(reg);
      }
      const { defs, uses } = defsAndUses(op);
      const newIn = new Set<RegisterName>(uses);
      for (const reg of newOut) {
        if (!defs.includes(reg)) newIn.add(reg);
      }
      if (!setsEqual(newIn, liveIn[i]!) || !setsEqual(newOut, liveOut[i]!)) {
        liveIn[i] = newIn;
        liveOut[i] = newOut;
        changed = true;
      }
    }
  }
  return { liveIn, liveOut };
}

function setsEqual<T>(a: ReadonlySet<T>, b: ReadonlySet<T>): boolean {
  if (a.size !== b.size) return false;
  for (const value of a) {
    if (!b.has(value)) return false;
  }
  return true;
}
