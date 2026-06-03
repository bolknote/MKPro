import type {
  AppliedOptimization,
  CompileOptions,
  IrOp,
  IrTargetMeta,
  PreloadReport,
  RegisterName,
} from "../types.ts";
import { getOpcode } from "../opcodes.ts";

export interface PassContext {
  readonly options: CompileOptions;
}

export interface PassResult {
  readonly ops: IrOp[];
  readonly applied: number;
  readonly optimizations: AppliedOptimization[];
  readonly preloads?: readonly PreloadReport[];
}

export type IrPassFn = (ops: readonly IrOp[], context: PassContext) => PassResult;

export interface IrPass {
  readonly name: string;
  readonly run: IrPassFn;
  readonly layoutSafe: boolean;
}

export function emptyResult(ops: readonly IrOp[]): PassResult {
  return { ops: [...ops], applied: 0, optimizations: [] };
}

export function cellsPerOp(op: IrOp): number {
  switch (op.kind) {
    case "label":
      return 0;
    case "jump":
    case "cjump":
    case "call":
    case "loop":
      return 2;
    case "orphan-address":
      return 1;
    default:
      return 1;
  }
}

export function calculateLabelAddresses(ops: readonly IrOp[]): Map<string, number> {
  const map = new Map<string, number>();
  let address = 0;
  for (const op of ops) {
    if (op.kind === "label") {
      map.set(op.name, address);
      continue;
    }
    address += cellsPerOp(op);
  }
  return map;
}

export function targetAddress(
  target: string | number,
  labels: ReadonlyMap<string, number>,
): number | undefined {
  if (typeof target === "number") return target;
  return labels.get(target);
}

export function withTargetMeta(meta: IrTargetMeta): IrTargetMeta {
  const out: IrTargetMeta = {};
  if (meta.comment !== undefined) out.comment = meta.comment;
  if (meta.sourceLine !== undefined) out.sourceLine = meta.sourceLine;
  if (meta.roles !== undefined) out.roles = [...meta.roles];
  if (meta.formalOpcode !== undefined) out.formalOpcode = meta.formalOpcode;
  return out;
}

export function findOpForward<T extends IrOp>(
  ops: readonly IrOp[],
  start: number,
  predicate: (op: IrOp) => op is T,
): T | undefined {
  for (let i = start; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (predicate(op)) return op;
    if (op.kind === "label") continue;
    return undefined;
  }
  return undefined;
}

export function hasRewriteBarrier(op: IrOp): boolean {
  return "meta" in op && "raw" in op.meta && op.meta.raw === true;
}

export function isDisplayFocusSensitive(op: IrOp): boolean {
  return "meta" in op && (
    op.meta.roles?.includes("display-byte") === true ||
    /\b(display|screen|show|x2|вп)\b/iu.test(op.meta.comment ?? "")
  );
}

export function knownIndirectMemoryTarget(op: IrOp): RegisterName | undefined {
  if (op.kind !== "indirect-recall" && op.kind !== "indirect-store") return undefined;
  const match = /\bindirect-memory-target=([0-9a-e])\b/iu.exec(op.meta.comment ?? "");
  if (!match) return undefined;
  return match[1]!.toLowerCase() as RegisterName;
}

function labelIndexes(ops: readonly IrOp[]): Map<string, number> {
  const result = new Map<string, number>();
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label") result.set(op.name, i);
  }
  return result;
}

function plainX2Effect(op: IrOp): "affects" | "restores" | "preserves" | "unknown" {
  if (op.kind !== "plain") return "unknown";
  if (hasRewriteBarrier(op)) return "unknown";
  return getOpcode(op.opcode).x2Effect;
}

function isContextSensitiveX2Restore(op: IrOp): boolean {
  return op.kind === "plain" && (op.opcode === 0x0a || op.opcode === 0x0c);
}

type StackDifferenceDepth = 1 | 2 | 3;

function shiftDifference(depth: StackDifferenceDepth): StackDifferenceDepth | undefined {
  if (depth === 1) return 2;
  if (depth === 2) return 3;
  return undefined;
}

function dropDifference(depth: StackDifferenceDepth): StackDifferenceDepth | undefined {
  if (depth === 1) return undefined;
  if (depth === 2) return 1;
  return 2;
}

export function removingRecallCanExposeStackLift(ops: readonly IrOp[], recallIndex: number): boolean {
  const labels = labelIndexes(ops);
  const visited = new Set<string>();
  const visit = (
    start: number,
    initialDepth: StackDifferenceDepth,
    returnStack: readonly number[] = [],
  ): boolean => {
    let depth: StackDifferenceDepth | undefined = initialDepth;

    for (let i = start; i < ops.length; i += 1) {
      if (depth === undefined) return false;
      const key = `${i}:${depth}:${returnStack.join(",")}`;
      if (visited.has(key)) return false;
      visited.add(key);

      const op = ops[i]!;
      if (hasRewriteBarrier(op)) return true;

      switch (op.kind) {
        case "label":
        case "store":
        case "indirect-store":
        case "orphan-address":
          break;
        case "recall":
        case "indirect-recall":
          depth = shiftDifference(depth);
          break;
        case "plain": {
          const effect = getOpcode(op.opcode).stackEffect;
          if (effect === "unknown" || effect === "exposes") return true;
          if (effect === "barrier") return false;
          if (effect === "shifts") {
            depth = shiftDifference(depth);
            break;
          }
          if (effect === "consume-y-drop") {
            if (depth === 1) return true;
            depth = dropDifference(depth);
            break;
          }
          if (effect === "consume-y-keep") {
            if (depth === 1) return true;
            break;
          }
          break;
        }
        case "jump": {
          if (typeof op.target !== "string") return true;
          const target = labels.get(op.target);
          return target === undefined ? true : visit(target + 1, depth, returnStack);
        }
        case "cjump":
        case "loop": {
          if (typeof op.target !== "string") return true;
          const target = labels.get(op.target);
          return (
            (target === undefined ? true : visit(target + 1, depth, returnStack)) ||
            visit(i + 1, depth, returnStack)
          );
        }
        case "call": {
          if (typeof op.target !== "string") return true;
          const target = labels.get(op.target);
          if (target === undefined || returnStack.length >= 5) return true;
          return visit(target + 1, depth, [i + 1, ...returnStack]);
        }
        case "indirect-jump":
        case "indirect-call":
        case "indirect-cjump":
          return true;
        case "return":
          if (returnStack.length === 0) return false;
          return visit(returnStack[0]!, depth, returnStack.slice(1));
        case "stop":
          return false;
      }
    }
    return false;
  };

  return visit(recallIndex + 1, 1);
}

export function removingRecallCanExposeX2Restore(ops: readonly IrOp[], recallIndex: number): boolean {
  const labels = labelIndexes(ops);
  const visited = new Set<string>();
  const visit = (start: number, returnStack: readonly number[] = []): boolean => {
    for (let i = start; i < ops.length; i += 1) {
      const key = `${i}:${returnStack.join(",")}`;
      if (visited.has(key)) return false;
      visited.add(key);

      const op = ops[i]!;
      if (hasRewriteBarrier(op)) return true;

      switch (op.kind) {
        case "plain": {
          const effect = plainX2Effect(op);
          if (effect === "unknown") return true;
          if (effect === "restores" && isContextSensitiveX2Restore(op)) return true;
          if (effect === "restores") return false;
          if (effect === "affects") return false;
          break;
        }
        case "recall":
        case "indirect-recall":
        case "stop":
          return false;
        case "return":
          // В/О itself is X2-affecting when returning from a subroutine, so a
          // later `.`/`ВП` in the caller observes the return-time X2 sync rather
          // than the recall we are considering removing. Stack-lift exposure is
          // checked by a separate analysis that still follows direct returns.
          return false;
        case "jump": {
          if (typeof op.target !== "string") return true;
          const target = labels.get(op.target);
          return target === undefined ? true : visit(target + 1, returnStack);
        }
        case "cjump":
        case "loop": {
          if (typeof op.target !== "string") return true;
          const target = labels.get(op.target);
          // Direct conditionals synchronize X2 on the fallthrough path; the
          // jumped path is the one that can still observe the removed recall.
          return target === undefined ? true : visit(target + 1, returnStack);
        }
        case "call": {
          if (typeof op.target !== "string") return true;
          const target = labels.get(op.target);
          if (target === undefined || returnStack.length >= 5) return true;
          return visit(target + 1, [i + 1, ...returnStack]);
        }
        case "indirect-jump":
        case "indirect-call":
        case "indirect-cjump":
          return true;
        case "label":
        case "store":
        case "indirect-store":
        case "orphan-address":
          break;
      }
    }
    return false;
  };
  return visit(recallIndex + 1);
}
