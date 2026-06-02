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

function nextEffectiveOp(ops: readonly IrOp[], index: number): IrOp | undefined {
  for (let i = index; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind !== "label") return op;
  }
  return undefined;
}

function isImmediateStackConsumer(op: IrOp): boolean {
  if (op.kind !== "plain") return false;
  return (
    (op.opcode >= 0x10 && op.opcode <= 0x14) ||
    op.opcode === 0x24 ||
    op.opcode === 0x25 ||
    (op.opcode >= 0x36 && op.opcode <= 0x39)
  );
}

export function removingRecallCanExposeStackLift(ops: readonly IrOp[], recallIndex: number): boolean {
  const next = nextEffectiveOp(ops, recallIndex + 1);
  return next !== undefined && isImmediateStackConsumer(next);
}

export function removingRecallCanExposeX2Restore(ops: readonly IrOp[], recallIndex: number): boolean {
  const labels = labelIndexes(ops);
  const visited = new Set<number>();
  const visit = (start: number): boolean => {
    for (let i = start; i < ops.length; i += 1) {
      if (visited.has(i)) return false;
      visited.add(i);

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
        case "return":
          return false;
        case "jump": {
          if (typeof op.target !== "string") return true;
          const target = labels.get(op.target);
          return target === undefined ? true : visit(target + 1);
        }
        case "cjump":
        case "loop": {
          if (typeof op.target !== "string") return true;
          const target = labels.get(op.target);
          // Direct conditionals synchronize X2 on the fallthrough path; the
          // jumped path is the one that can still observe the removed recall.
          return target === undefined ? true : visit(target + 1);
        }
        case "call": {
          if (typeof op.target !== "string") return true;
          const target = labels.get(op.target);
          return target === undefined ? true : visit(target + 1);
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
