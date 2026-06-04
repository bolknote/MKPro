import type {
  AppliedOptimization,
  CompileOptions,
  IrOp,
  IrTargetMeta,
  PreloadReport,
  RegisterName,
} from "../types.ts";
import { isStableIndirectSelector } from "../indirect-addressing.ts";
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

export type RegisterValueSet = ReadonlySet<RegisterName>;

export interface X2RestoreExposureOptions {
  readonly redundantSyncRegister?: RegisterName | undefined;
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

export function removableRecallValueRegister(op: IrOp): RegisterName | undefined {
  if (hasRewriteBarrier(op)) return undefined;
  if (op.kind === "recall") return op.register;
  if (op.kind !== "indirect-recall") return undefined;
  if (!isStableIndirectSelector(op.register)) return undefined;
  return knownIndirectMemoryTarget(op);
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

function conditionalX2Effect(
  op: Extract<IrOp, { kind: "cjump" | "loop" }>,
  edge: "fallthrough" | "jump",
): "affects" | "preserves" | "unknown" {
  const effect = getOpcode(op.opcode).conditionalX2Effect;
  if (effect === undefined) return "unknown";
  return effect[edge];
}

interface Edge {
  readonly target: number;
  readonly kind: "normal" | "fallthrough" | "jump";
}

interface RegisterDataflowState {
  readonly x: RegisterValueSet;
  readonly x2: RegisterValueSet;
}

export function computeX2RegisterStates(ops: readonly IrOp[]): Array<RegisterValueSet | undefined> {
  if (ops.length === 0) return [];
  const edges = buildRegisterValueGraph(ops);
  const inStates: Array<RegisterDataflowState | undefined> = Array.from({ length: ops.length }, () => undefined);
  inStates[0] = emptyRegisterDataflowState();

  let changed = true;
  let iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    iterations += 1;

    for (let index = 0; index < ops.length; index += 1) {
      const input = inStates[index];
      if (input === undefined) continue;
      for (const edge of edges[index] ?? []) {
        const output = transferRegisterDataflowState(input, ops[index]!, edge.kind);
        const joined = joinRegisterDataflowStates(inStates[edge.target], output);
        if (!sameRegisterDataflowState(joined, inStates[edge.target])) {
          inStates[edge.target] = joined;
          changed = true;
        }
      }
    }
  }

  return inStates.map((state) => state?.x2);
}

export function recallAlreadySyncedInX2(
  op: IrOp,
  state: RegisterValueSet | undefined,
): RegisterName | undefined {
  if (state === undefined) return undefined;
  if (op.kind === "recall") return state.has(op.register) ? op.register : undefined;
  if (op.kind !== "indirect-recall") return undefined;
  const target = knownIndirectMemoryTarget(op);
  return target !== undefined && state.has(target) ? target : undefined;
}

function emptyRegisterDataflowState(): RegisterDataflowState {
  return { x: new Set(), x2: new Set() };
}

function cloneRegisterDataflowState(input: RegisterDataflowState): RegisterDataflowState {
  return { x: new Set(input.x), x2: new Set(input.x2) };
}

function transferRegisterDataflowState(
  input: RegisterDataflowState,
  op: IrOp,
  edge: Edge["kind"],
): RegisterDataflowState {
  if (hasRewriteBarrier(op)) return emptyRegisterDataflowState();

  switch (op.kind) {
    case "label":
    case "jump":
    case "call":
    case "orphan-address":
      return cloneRegisterDataflowState(input);
    case "store":
      return {
        x: addRegisterValue(input.x, op.register),
        x2: addStoredX2Alias(input, op.register),
      };
    case "indirect-store":
      return transferIndirectStoreRegisterState(input, op);
    case "recall":
      return { x: new Set([op.register]), x2: new Set([op.register]) };
    case "indirect-recall": {
      const target = knownIndirectMemoryTarget(op);
      const registers = target === undefined ? new Set<RegisterName>() : new Set([target]);
      return { x: registers, x2: new Set(registers) };
    }
    case "plain":
      return { x: new Set(), x2: transferPlainX2RegisterSet(input.x2, plainX2Effect(op)) };
    case "cjump": {
      const effect = edge === "fallthrough"
        ? conditionalX2Effect(op, "fallthrough")
        : edge === "jump"
          ? conditionalX2Effect(op, "jump")
          : "unknown";
      return {
        x: new Set(input.x),
        x2: transferConditionalX2RegisterSet(input, effect),
      };
    }
    case "loop": {
      const effect = edge === "fallthrough"
        ? conditionalX2Effect(op, "fallthrough")
        : edge === "jump"
          ? conditionalX2Effect(op, "jump")
          : "unknown";
      return {
        x: new Set(),
        x2: effect === "preserves" ? new Set(input.x2) : new Set(),
      };
    }
    case "return":
    case "stop":
    case "indirect-jump":
    case "indirect-call":
    case "indirect-cjump":
      return emptyRegisterDataflowState();
  }
}

function addRegisterValue(input: RegisterValueSet, register: RegisterName): Set<RegisterName> {
  const output = new Set(input);
  output.add(register);
  return output;
}

function addStoredX2Alias(input: RegisterDataflowState, register: RegisterName): Set<RegisterName> {
  const output = new Set(input.x2);
  if (setsIntersect(input.x, input.x2)) output.add(register);
  return output;
}

function transferIndirectStoreRegisterState(
  input: RegisterDataflowState,
  op: Extract<IrOp, { kind: "indirect-store" }>,
): RegisterDataflowState {
  const target = knownIndirectMemoryTarget(op);
  if (target === undefined) return { x: new Set(input.x), x2: new Set(input.x2) };
  return {
    x: addRegisterValue(input.x, target),
    x2: addStoredX2Alias(input, target),
  };
}

function setsIntersect(left: RegisterValueSet, right: RegisterValueSet): boolean {
  for (const value of left) {
    if (right.has(value)) return true;
  }
  return false;
}

function transferPlainX2RegisterSet(
  input: RegisterValueSet,
  effect: ReturnType<typeof plainX2Effect>,
): Set<RegisterName> {
  return effect === "preserves" ? new Set(input) : new Set();
}

function transferConditionalX2RegisterSet(
  input: RegisterDataflowState,
  effect: ReturnType<typeof conditionalX2Effect>,
): Set<RegisterName> {
  if (effect === "preserves") return new Set(input.x2);
  if (effect === "affects") return new Set(input.x);
  return new Set();
}

function joinRegisterDataflowStates(
  current: RegisterDataflowState | undefined,
  incoming: RegisterDataflowState,
): RegisterDataflowState {
  if (current === undefined) return {
    x: new Set(incoming.x),
    x2: new Set(incoming.x2),
  };
  return {
    x: joinRegisterValueSets(current.x, incoming.x),
    x2: joinRegisterValueSets(current.x2, incoming.x2),
  };
}

function sameRegisterDataflowState(
  left: RegisterDataflowState | undefined,
  right: RegisterDataflowState | undefined,
): boolean {
  if (left === undefined || right === undefined) return left === right;
  return sameRegisterValueSet(left.x, right.x) && sameRegisterValueSet(left.x2, right.x2);
}

function joinRegisterValueSets(
  current: RegisterValueSet | undefined,
  incoming: RegisterValueSet,
): Set<RegisterName> {
  if (current === undefined) return new Set(incoming);
  const joined = new Set<RegisterName>();
  for (const register of current) {
    if (incoming.has(register)) joined.add(register);
  }
  return joined;
}

function sameRegisterValueSet(
  left: RegisterValueSet | undefined,
  right: RegisterValueSet | undefined,
): boolean {
  if (left === undefined || right === undefined) return left === right;
  if (left.size !== right.size) return false;
  for (const register of left) {
    if (!right.has(register)) return false;
  }
  return true;
}

function buildRegisterValueGraph(ops: readonly IrOp[]): Edge[][] {
  const labels = labelIndexes(ops);
  const successors: Edge[][] = Array.from({ length: ops.length }, () => []);
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
      if (next < ops.length) successors[index]!.push({ target: next, kind: "fallthrough" });
    };
    const normal = (target: number): void => {
      successors[index]!.push({ target, kind: "normal" });
    };
    const jumpTo = (target: string | number): void => {
      if (typeof target !== "string") return;
      const targetIndex = labels.get(target);
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
      case "return":
        for (const target of callReturns) normal(target);
        break;
      case "indirect-jump":
      case "indirect-call":
      case "indirect-cjump":
        break;
    }
  }

  return successors;
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

function stackDifferenceCanReachConsumer(
  ops: readonly IrOp[],
  start: number,
  initialDepth: StackDifferenceDepth,
): boolean {
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

  return visit(start, initialDepth);
}

export function removingRecallCanExposeStackLift(ops: readonly IrOp[], recallIndex: number): boolean {
  return stackDifferenceCanReachConsumer(ops, recallIndex + 1, 1);
}

export function removingPreShiftLiftCanExposeStack(ops: readonly IrOp[], producerIndex: number): boolean {
  return stackDifferenceCanReachConsumer(ops, producerIndex + 1, 2);
}

export function removingRecallCanExposeX2Restore(
  ops: readonly IrOp[],
  recallIndex: number,
  options: X2RestoreExposureOptions = {},
): boolean {
  const labels = labelIndexes(ops);
  const visited = new Set<string>();
  const visit = (
    start: number,
    returnStack: readonly number[] = [],
    sawExecutableAfterRecall = false,
  ): boolean => {
    for (let i = start; i < ops.length; i += 1) {
      const key = `${i}:${sawExecutableAfterRecall ? 1 : 0}:${returnStack.join(",")}`;
      if (visited.has(key)) return false;
      visited.add(key);

      const op = ops[i]!;
      if (hasRewriteBarrier(op)) return true;

      switch (op.kind) {
        case "plain": {
          const effect = plainX2Effect(op);
          if (effect === "unknown") return true;
          if (effect === "restores" && isContextSensitiveX2Restore(op)) {
            return options.redundantSyncRegister !== undefined && sawExecutableAfterRecall ? false : true;
          }
          if (effect === "restores") return false;
          if (effect === "affects") return false;
          sawExecutableAfterRecall = true;
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
          return target === undefined ? true : visit(target + 1, returnStack, true);
        }
        case "cjump":
        case "loop": {
          if (typeof op.target !== "string") return true;
          const target = labels.get(op.target);
          const fallthrough = conditionalX2Effect(op, "fallthrough");
          const jump = conditionalX2Effect(op, "jump");
          if (fallthrough === "unknown" || jump === "unknown") return true;
          return (
            (jump === "preserves" && (target === undefined ? true : visit(target + 1, returnStack, true))) ||
            (fallthrough === "preserves" && visit(i + 1, returnStack, true))
          );
        }
        case "call": {
          if (typeof op.target !== "string") return true;
          const target = labels.get(op.target);
          if (target === undefined || returnStack.length >= 5) return true;
          return visit(target + 1, [i + 1, ...returnStack], true);
        }
        case "indirect-jump":
        case "indirect-call":
        case "indirect-cjump":
          return true;
        case "label":
          break;
        case "store":
        case "indirect-store":
        case "orphan-address":
          sawExecutableAfterRecall = true;
          break;
      }
    }
    return false;
  };
  return visit(recallIndex + 1);
}
