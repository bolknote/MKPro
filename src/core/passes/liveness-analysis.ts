import type { IrOp, RegisterName } from "../types.ts";
import { buildCfgSuccessors, loopCounterRegister } from "./cfg.ts";
import { knownIndirectMemoryTarget } from "./helpers.ts";

export interface LivenessInfo {
  readonly liveIn: ReadonlyArray<ReadonlySet<RegisterName>>;
  readonly liveOut: ReadonlyArray<ReadonlySet<RegisterName>>;
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

export function computeLiveness(ops: readonly IrOp[]): LivenessInfo {
  const successors = buildCfgSuccessors(ops, { indirectCallFallthrough: true });
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
