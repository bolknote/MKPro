// Shared control-flow-graph construction over linear IrOp lists.
//
// Several passes (liveness, flow-x-reuse, …) used to carry their own copies of
// the successor-graph builder with subtly diverging behavior. This module is
// the single canonical implementation; behavioral differences between callers
// are expressed through explicit CfgOptions instead of parallel code.

import type { IrLoopCounter, IrOp, RegisterName } from "../types.ts";
import { cellsPerOp, knownIndirectFlowTarget } from "./helpers.ts";

export interface CfgTargetIndexes {
  /** Label name → op index of the label op itself. */
  readonly labelIndex: Map<string, number>;
  /** Machine cell address → op index of the executable op starting there. */
  readonly addressIndex: Map<number, number>;
}

export function buildTargetIndexes(ops: readonly IrOp[]): CfgTargetIndexes {
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

export type CfgEdgeKind = "normal" | "fallthrough" | "jump";

export interface CfgEdge {
  readonly target: number;
  readonly kind: CfgEdgeKind;
}

export interface CfgOptions {
  /**
   * Add a direct fallthrough edge after `indirect-call` ops in addition to the
   * shared `return` → call-continuation wiring. Conservative consumers such as
   * liveness want the extra edge; intersection-based value proofs rely on the
   * return wiring alone so callee effects are not bypassed.
   */
  readonly indirectCallFallthrough?: boolean;
}

export interface NumericFlowTargetLayoutGuard {
  canDeleteAt(index: number): boolean;
}

export function numericFlowTargetLayoutGuard(ops: readonly IrOp[]): NumericFlowTargetLayoutGuard | undefined {
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
    canDeleteAt: (index) => index >= latestTargetIndex,
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

function returnActsAsAddressOneJump(op: IrOp): boolean {
  return op.kind === "return" && op.meta.comment === "optimized БП 01";
}

/**
 * Build the successor edge lists for a linear op sequence.
 *
 * Semantics shared by all consumers:
 * - `stop` (С/П) falls through: pressing С/П resumes at the next cell.
 * - `return` edges go to every call continuation (the op after each direct or
 *   indirect call), because the IR does not pair calls with returns.
 * - Indirect flow contributes an edge only when lowering proved its target
 *   (`indirect-target=NN`); unknown indirect flow contributes no edge here and
 *   must be handled conservatively by the caller if it matters.
 */
export function buildCfgEdges(ops: readonly IrOp[], options: CfgOptions = {}): CfgEdge[][] {
  const { labelIndex, addressIndex } = buildTargetIndexes(ops);
  const successors: CfgEdge[][] = Array.from({ length: ops.length }, () => []);

  const callReturns: number[] = [];
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    const next = index + 1;
    if ((op.kind === "call" || op.kind === "indirect-call") && next < ops.length) {
      callReturns.push(next);
    }
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
        if (options.indirectCallFallthrough === true) fallthrough();
        break;
      }
      case "indirect-cjump": {
        const target = knownIndirectFlowTarget(op);
        if (target !== undefined) jumpTo(target);
        fallthrough();
        break;
      }
      case "return":
        if (returnActsAsAddressOneJump(op)) {
          jumpTo(1);
        } else {
          for (const target of callReturns) successors[index]!.push({ target, kind: "normal" });
        }
        break;
    }
  }

  return successors;
}

/** Edge targets only, for consumers that do not care about edge kinds. */
export function buildCfgSuccessors(ops: readonly IrOp[], options: CfgOptions = {}): number[][] {
  return buildCfgEdges(ops, options).map((edges) => edges.map((edge) => edge.target));
}

/** The memory register decremented and tested by `F L0`..`F L3`. */
export function loopCounterRegister(counter: IrLoopCounter): RegisterName {
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
