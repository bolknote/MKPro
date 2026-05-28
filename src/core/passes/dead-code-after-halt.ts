import type { IrOp } from "../types.ts";
import { computeLiveness } from "./liveness-analysis.ts";
import { cellsPerOp, type IrPass, type IrPassFn, type PassResult } from "./helpers.ts";

function reachableFromEntry(ops: readonly IrOp[]): Set<number> {
  const labelIndex = new Map<string, number>();
  const addressIndex = new Map<number, number>();
  let address = 0;
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label") labelIndex.set(op.name, i);
    else {
      addressIndex.set(address, i);
      address += cellsPerOp(op);
    }
  }
  const visited = new Set<number>();
  const stack: number[] = [];
  if (ops.length > 0) stack.push(0);
  while (stack.length > 0) {
    const i = stack.pop()!;
    if (visited.has(i)) continue;
    visited.add(i);
    const op = ops[i]!;
    const fallthrough = (): void => {
      if (i + 1 < ops.length) stack.push(i + 1);
    };
    const target = (label: string | number): void => {
      if (typeof label === "string") {
        const idx = labelIndex.get(label);
        if (idx !== undefined) stack.push(idx);
      } else {
        const idx = addressIndex.get(label);
        if (idx !== undefined) stack.push(idx);
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
        // С/П always allows resume to the next cell on MK-61.
        fallthrough();
        break;
      case "return":
        break;
      case "jump":
        target(op.target);
        break;
      case "cjump":
      case "loop":
        target(op.target);
        fallthrough();
        break;
      case "call":
        target(op.target);
        fallthrough();
        break;
      case "indirect-jump": {
        const knownTarget = knownIndirectTarget(op);
        if (knownTarget !== undefined) target(knownTarget);
        break;
      }
      case "indirect-call": {
        const knownTarget = knownIndirectTarget(op);
        if (knownTarget !== undefined) target(knownTarget);
        fallthrough();
        break;
      }
      case "indirect-cjump": {
        const knownTarget = knownIndirectTarget(op);
        if (knownTarget !== undefined) target(knownTarget);
        fallthrough();
        break;
      }
    }
  }
  return visited;
}

function knownIndirectTarget(op: IrOp): number | undefined {
  if (op.kind !== "indirect-jump" && op.kind !== "indirect-call" && op.kind !== "indirect-cjump") {
    return undefined;
  }
  const match = /\bindirect-target=(\d+)\b/u.exec(op.meta.comment ?? "");
  if (!match) return undefined;
  const target = Number(match[1]);
  if (!Number.isInteger(target) || target < 0 || target > 104) return undefined;
  return target;
}

const run: IrPassFn = (ops) => {
  if (ops.length === 0) return { ops: [], applied: 0, optimizations: [] };
  const reachable = reachableFromEntry(ops);
  if (reachable.size === ops.length) {
    return { ops: [...ops], applied: 0, optimizations: [] };
  }
  computeLiveness(ops);
  const result: IrOp[] = [];
  let applied = 0;
  for (let i = 0; i < ops.length; i += 1) {
    if (reachable.has(i)) {
      result.push(ops[i]!);
      continue;
    }
    if (ops[i]!.kind === "label") {
      result.push(ops[i]!);
      continue;
    }
    applied += 1;
  }
  if (applied === 0) {
    return { ops: result, applied: 0, optimizations: [] };
  }
  const passResult: PassResult = {
    ops: result,
    applied,
    optimizations: [
      {
        name: "dead-code-after-halt",
        detail: `Removed ${applied} unreachable op(s) from the entry CFG.`,
      },
    ],
  };
  return passResult;
};

export const deadCodeAfterHalt: IrPass = {
  name: "dead-code-after-halt",
  run,
  layoutSafe: false,
};
