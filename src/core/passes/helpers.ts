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
export type X2ValueFact =
  | `reg:${RegisterName}`
  | "same:unknown"
  | `decimal:${string}:normalized`
  | `decimal:${string}:unnormalized`;
export type X2ValueSet = ReadonlySet<X2ValueFact>;
export type X2ShapeFact =
  | `mantissa:${string}:decimal`
  | `exponent:${string}:${string}:decimal`
  | `hex:${string}:mantissa`
  | `super:${string}`;
export type X2ShapeSet = ReadonlySet<X2ShapeFact>;
type X2ValueMemory = Partial<Record<RegisterName, X2ValueSet>>;

const NORMALIZED_DECIMAL_ZERO: X2ValueFact = "decimal:0:normalized";
const SAME_UNKNOWN_VALUE: X2ValueFact = "same:unknown";
const REGISTER_NAMES: readonly RegisterName[] = [
  "0",
  "1",
  "2",
  "3",
  "4",
  "5",
  "6",
  "7",
  "8",
  "9",
  "a",
  "b",
  "c",
  "d",
  "e",
];

export interface X2ValueDataflowState {
  readonly x: X2ValueSet;
  readonly x2: X2ValueSet;
  readonly xShape?: X2ShapeSet;
  readonly x2Shape?: X2ShapeSet;
  readonly entry: X2EntryState;
  readonly vpContext?: X2VpContextState;
  readonly vpEntryMantissa?: ReadonlySet<string> | undefined;
  readonly memory?: X2ValueMemory | undefined;
}

type X2EntryState =
  | { readonly kind: "closed" }
  | { readonly kind: "open"; readonly raw: ReadonlySet<string> }
  | {
      readonly kind: "exponent";
      readonly mantissa: ReadonlySet<string>;
      readonly exponent: ReadonlySet<string>;
    }
  | { readonly kind: "unknown" };

type X2VpContextState =
  | { readonly kind: "none" }
  | {
      readonly kind: "exponent";
      readonly mantissa: ReadonlySet<string>;
      readonly exponent: ReadonlySet<string>;
    }
  | { readonly kind: "unknown" };

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

export function knownIndirectFlowTarget(op: IrOp): number | undefined {
  if (op.kind !== "indirect-jump" && op.kind !== "indirect-call" && op.kind !== "indirect-cjump") {
    return undefined;
  }
  const match = /\bindirect-target=(\d+)\b/u.exec(op.meta.comment ?? "");
  if (!match) return undefined;
  const target = Number(match[1]);
  return Number.isInteger(target) && target >= 0 && target <= 104 ? target : undefined;
}

export function removableRecallValueRegister(op: IrOp): RegisterName | undefined {
  if (hasRewriteBarrier(op)) return undefined;
  if (op.kind === "recall") return op.register;
  if (op.kind !== "indirect-recall") return undefined;
  if (!isStableIndirectSelector(op.register)) return undefined;
  return knownIndirectMemoryTarget(op);
}

export function storedCurrentXValueRegister(op: IrOp): RegisterName | undefined {
  if (hasRewriteBarrier(op)) return undefined;
  if (op.kind === "store") return op.register;
  if (op.kind !== "indirect-store") return undefined;
  return knownIndirectMemoryTarget(op);
}

export function plainPreservesXValue(op: Extract<IrOp, { kind: "plain" }>): boolean {
  if (hasRewriteBarrier(op)) return false;
  if (op.opcode === 0x0e) return true;
  if (op.opcode >= 0xf0 && op.opcode <= 0xff) return true;
  return op.opcode >= 0x54 && op.opcode <= 0x56;
}

function labelIndexes(ops: readonly IrOp[]): Map<string, number> {
  const result = new Map<string, number>();
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label") result.set(op.name, i);
  }
  return result;
}

function addressIndexes(ops: readonly IrOp[]): Map<number, number> {
  const result = new Map<number, number>();
  let address = 0;
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label") continue;
    result.set(address, i);
    address += cellsPerOp(op);
  }
  return result;
}

function plainX2Effect(op: IrOp): "affects" | "restores" | "preserves" | "unknown" {
  if (op.kind !== "plain") return "unknown";
  if (hasRewriteBarrier(op)) return "unknown";
  return getOpcode(op.opcode).x2Effect;
}

function isContextSensitiveX2Restore(op: IrOp): boolean {
  return op.kind === "plain" && (op.opcode === 0x0a || op.opcode === 0x0b || op.opcode === 0x0c);
}

type ConditionalX2Op = Extract<IrOp, { kind: "cjump" | "loop" | "indirect-cjump" }>;

function conditionalX2Effect(
  op: ConditionalX2Op,
  edge: "fallthrough" | "jump",
): "affects" | "preserves" | "unknown" {
  const effect = getOpcode(op.opcode).conditionalX2Effect;
  if (effect === undefined) return "unknown";
  return effect[edge];
}

function conditionalX2EffectForGraphEdge(
  op: ConditionalX2Op,
  edge: Edge["kind"],
): "affects" | "preserves" | "unknown" {
  if (edge === "fallthrough" || edge === "jump") return conditionalX2Effect(op, edge);
  return "unknown";
}

function indirectConditionalX2EffectForGraphEdge(
  op: Extract<IrOp, { kind: "indirect-cjump" }>,
  edge: Edge["kind"],
): "affects" | "preserves" | "unknown" {
  return conditionalX2EffectForGraphEdge(op, edge);
}

interface Edge {
  readonly target: number;
  readonly kind: "normal" | "fallthrough" | "jump";
}

interface RegisterDataflowState {
  readonly x: RegisterValueSet;
  readonly x2: RegisterValueSet;
}

interface X2ValueDataflowOptions {
  readonly trackRegisterMemory?: boolean;
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

export function computeX2ValueStates(
  ops: readonly IrOp[],
  options: X2ValueDataflowOptions = {},
): Array<X2ValueDataflowState | undefined> {
  if (ops.length === 0) return [];
  const trackRegisterMemory = options.trackRegisterMemory === true;
  const edges = buildRegisterValueGraph(ops);
  const inStates: Array<X2ValueDataflowState | undefined> = Array.from({ length: ops.length }, () => undefined);
  inStates[0] = emptyX2ValueDataflowState(trackRegisterMemory);

  let changed = true;
  let iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    iterations += 1;

    for (let index = 0; index < ops.length; index += 1) {
      const input = inStates[index];
      if (input === undefined) continue;
      for (const edge of edges[index] ?? []) {
        const output = transferX2ValueDataflowState(input, ops[index]!, edge.kind, trackRegisterMemory);
        const joined = joinX2ValueDataflowStates(inStates[edge.target], output, trackRegisterMemory);
        if (!sameX2ValueDataflowState(joined, inStates[edge.target])) {
          inStates[edge.target] = joined;
          changed = true;
        }
      }
    }
  }

  return inStates;
}

export function x2ValueSetHasIntersection(left: X2ValueSet | undefined, right: X2ValueSet | undefined): boolean {
  if (left === undefined || right === undefined) return false;
  for (const value of left) {
    if (right.has(value)) return true;
  }
  return false;
}

export function x2ValueSetHasRegister(input: X2ValueSet | undefined, register: RegisterName): boolean {
  return input?.has(registerValueFact(register)) === true;
}

export function x2ValueSetHasNormalizedDecimal(input: X2ValueSet | undefined, value: string): boolean {
  return input?.has(decimalValueFact(value, "normalized")) === true;
}

type X2RestoreBoundaryState = "none" | "synced" | "boundary";
type X2DotRestoreGapState = "none" | "synced" | "one-gap" | "safe";

export function computeX2RestoreBoundaryStates(ops: readonly IrOp[]): boolean[] {
  if (ops.length === 0) return [];
  const edges = buildRegisterValueGraph(ops);
  const inStates: Array<X2RestoreBoundaryState | undefined> = Array.from({ length: ops.length }, () => undefined);
  inStates[0] = "none";

  let changed = true;
  let iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    iterations += 1;

    for (let index = 0; index < ops.length; index += 1) {
      const input = inStates[index];
      if (input === undefined) continue;
      for (const edge of edges[index] ?? []) {
        const output = transferX2RestoreBoundaryState(input, ops[index]!, edge.kind);
        const joined = joinX2RestoreBoundaryStates(inStates[edge.target], output);
        if (joined !== inStates[edge.target]) {
          inStates[edge.target] = joined;
          changed = true;
        }
      }
    }
  }

  return inStates.map((state) => state === "boundary");
}

export function computeX2DotRestoreGapStates(ops: readonly IrOp[]): boolean[] {
  if (ops.length === 0) return [];
  const edges = buildRegisterValueGraph(ops);
  const inStates: Array<X2DotRestoreGapState | undefined> = Array.from({ length: ops.length }, () => undefined);
  inStates[0] = "none";

  let changed = true;
  let iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    iterations += 1;

    for (let index = 0; index < ops.length; index += 1) {
      const input = inStates[index];
      if (input === undefined) continue;
      for (const edge of edges[index] ?? []) {
        const output = transferX2DotRestoreGapState(input, ops[index]!, edge.kind);
        const joined = joinX2DotRestoreGapStates(inStates[edge.target], output);
        if (joined !== inStates[edge.target]) {
          inStates[edge.target] = joined;
          changed = true;
        }
      }
    }
  }

  return inStates.map((state) => state === "safe");
}

export function computeX2ImmediateSyncStates(ops: readonly IrOp[]): boolean[] {
  if (ops.length === 0) return [];
  const edges = buildRegisterValueGraph(ops);
  const inStates: Array<boolean | undefined> = Array.from({ length: ops.length }, () => undefined);
  inStates[0] = false;

  let changed = true;
  let iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    iterations += 1;

    for (let index = 0; index < ops.length; index += 1) {
      const input = inStates[index];
      if (input === undefined) continue;
      for (const edge of edges[index] ?? []) {
        const output = transferX2ImmediateSyncState(input, ops[index]!, edge.kind);
        const joined = joinX2ImmediateSyncStates(inStates[edge.target], output);
        if (joined !== inStates[edge.target]) {
          inStates[edge.target] = joined;
          changed = true;
        }
      }
    }
  }

  return inStates.map((state) => state === true);
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

export function recallAlreadySyncedInX2Value(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  return register !== undefined && x2ValueSetHasRegister(state?.x2, register) ? register : undefined;
}

export function recallAlreadySyncedInX2DecimalMemory(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  for (const fact of state.memory?.[register] ?? []) {
    if (isConcreteDecimalX2ValueFact(fact) && state.x2.has(fact)) return register;
  }
  return undefined;
}

function emptyRegisterDataflowState(): RegisterDataflowState {
  return { x: new Set(), x2: new Set() };
}

function emptyX2ValueDataflowState(trackRegisterMemory = false): X2ValueDataflowState {
  return {
    x: new Set(),
    x2: new Set(),
    xShape: new Set(),
    x2Shape: new Set(),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    memory: trackRegisterMemory ? {} : undefined,
  };
}

function cloneRegisterDataflowState(input: RegisterDataflowState): RegisterDataflowState {
  return { x: new Set(input.x), x2: new Set(input.x2) };
}

function cloneX2ValueDataflowState(input: X2ValueDataflowState): X2ValueDataflowState {
  return {
    x: new Set(input.x),
    x2: new Set(input.x2),
    xShape: cloneOptionalShapeSet(input.xShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: cloneX2EntryState(input.entry),
    vpContext: cloneX2VpContextState(input.vpContext),
    vpEntryMantissa: cloneOptionalStringSet(input.vpEntryMantissa),
    memory: input.memory,
  };
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
    case "plain": {
      const effect = plainX2Effect(op);
      const x = plainPreservesXValue(op) ? new Set(input.x) : new Set<RegisterName>();
      return { x, x2: transferPlainX2RegisterSet(input, x, effect) };
    }
    case "cjump": {
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      return {
        x: new Set(input.x),
        x2: transferConditionalX2RegisterSet(input, effect),
      };
    }
    case "loop": {
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      return {
        x: new Set(),
        x2: effect === "preserves" ? new Set(input.x2) : new Set(),
      };
    }
    case "indirect-jump":
    case "indirect-call":
      return transferIndirectFlowRegisterState(input, op);
    case "indirect-cjump":
      return transferIndirectConditionalRegisterState(input, op, edge);
    case "stop":
      return emptyRegisterDataflowState();
    case "return":
      return { x: new Set(input.x), x2: new Set(input.x) };
  }
}

function transferX2ValueDataflowState(
  input: X2ValueDataflowState,
  op: IrOp,
  edge: Edge["kind"],
  trackRegisterMemory: boolean,
): X2ValueDataflowState {
  if (hasRewriteBarrier(op)) return emptyX2ValueDataflowState(trackRegisterMemory);

  switch (op.kind) {
    case "label":
      return cloneX2ValueDataflowState(input);
    case "jump":
    case "call":
    case "orphan-address":
      return closeX2ValueEntry(input);
    case "store": {
      const closed = closeX2ValueEntry(input);
      return {
        x: addX2Value(closed.x, registerValueFact(op.register)),
        x2: addStoredX2ValueAlias(closed, registerValueFact(op.register)),
        xShape: cloneOptionalShapeSet(closed.xShape),
        x2Shape: cloneOptionalShapeSet(closed.x2Shape),
        entry: closedX2EntryState(),
        vpContext: cloneX2VpContextState(closed.vpContext),
        memory: trackRegisterMemory ? storeX2ValueMemory(closed.memory, op.register, closed.x) : undefined,
      };
    }
    case "indirect-store":
      return transferIndirectStoreX2ValueState(input, op, trackRegisterMemory);
    case "recall": {
      const value = recallX2ValueFacts(input, op.register, trackRegisterMemory);
      return {
        x: new Set(value),
        x2: new Set(value),
        xShape: x2ShapesFromValueFacts(value),
        x2Shape: x2ShapesFromValueFacts(value),
        entry: closedX2EntryState(),
        vpContext: noneX2VpContextState(),
        vpEntryMantissa: vpEntryMantissasFromValueFacts(value),
        memory: input.memory,
      };
    }
    case "indirect-recall": {
      const target = knownIndirectMemoryTarget(op);
      const values = target === undefined
        ? new Set<X2ValueFact>([SAME_UNKNOWN_VALUE])
        : recallX2ValueFacts(input, target, trackRegisterMemory);
      return {
        x: values,
        x2: new Set(values),
        xShape: x2ShapesFromValueFacts(values),
        x2Shape: x2ShapesFromValueFacts(values),
        entry: closedX2EntryState(),
        vpContext: noneX2VpContextState(),
        vpEntryMantissa: vpEntryMantissasFromValueFacts(values),
        memory: input.memory,
      };
    }
    case "plain":
      return transferPlainX2ValueState(input, op);
    case "cjump": {
      const closed = closeX2ValueEntry(input);
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      const x = syncUnknownSameValue(new Set(closed.x), effect);
      const xShape = cloneOptionalShapeSet(closed.xShape);
      return {
        x,
        x2: transferConditionalX2ValueSet(closed, x, effect),
        xShape,
        x2Shape: transferConditionalX2ShapeSet(closed, xShape, effect),
        entry: closedX2EntryState(),
        vpContext: transferConditionalX2VpContextState(closed, effect),
        vpEntryMantissa: transferConditionalX2VpEntryMantissaState(x, effect),
        memory: closed.memory,
      };
    }
    case "loop": {
      const closed = closeX2ValueEntry(input);
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      return {
        x: new Set(),
        x2: effect === "preserves" ? new Set(closed.x2) : new Set(),
        xShape: new Set(),
        x2Shape: effect === "preserves" ? cloneOptionalShapeSet(closed.x2Shape) : new Set(),
        entry: closedX2EntryState(),
        vpContext: transferConditionalX2VpContextState(closed, effect),
        memory: closed.memory,
      };
    }
    case "indirect-jump":
    case "indirect-call":
      return transferIndirectFlowX2ValueState(input, op, trackRegisterMemory);
    case "indirect-cjump":
      return transferIndirectConditionalX2ValueState(input, op, edge, trackRegisterMemory);
    case "stop":
      return emptyX2ValueDataflowState(trackRegisterMemory);
    case "return": {
      const closed = closeX2ValueEntry(input);
      const x = syncUnknownSameValue(new Set(closed.x), "affects");
      const xShape = cloneOptionalShapeSet(closed.xShape);
      return {
        x,
        x2: new Set(x),
        xShape,
        x2Shape: cloneOptionalShapeSet(xShape),
        entry: closedX2EntryState(),
        vpContext: noneX2VpContextState(),
        vpEntryMantissa: transferConditionalX2VpEntryMantissaState(x, "affects"),
        memory: closed.memory,
      };
    }
  }
}

function transferX2RestoreBoundaryState(
  input: X2RestoreBoundaryState,
  op: IrOp,
  edge: Edge["kind"],
): X2RestoreBoundaryState {
  if (hasRewriteBarrier(op)) return "none";

  switch (op.kind) {
    case "label":
      return input;
    case "jump":
    case "call":
    case "orphan-address":
    case "store":
    case "indirect-store":
      return x2PreservingExecutableBoundary(input);
    case "recall":
    case "indirect-recall":
    case "return":
    case "stop":
      return "synced";
    case "plain":
      return transferPlainX2RestoreBoundaryState(input, plainX2Effect(op));
    case "cjump":
    case "loop": {
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      return transferConditionalX2RestoreBoundaryState(input, effect);
    }
    case "indirect-cjump": {
      const effect = indirectConditionalX2EffectForGraphEdge(op, edge);
      return transferConditionalX2RestoreBoundaryState(input, effect);
    }
    case "indirect-jump":
    case "indirect-call":
      return x2PreservingExecutableBoundary(input);
  }
}

function transferX2DotRestoreGapState(
  input: X2DotRestoreGapState,
  op: IrOp,
  edge: Edge["kind"],
): X2DotRestoreGapState {
  if (hasRewriteBarrier(op)) return "none";

  switch (op.kind) {
    case "label":
      return input;
    case "jump":
    case "call":
      return input;
    case "orphan-address":
    case "store":
    case "indirect-store":
      return advanceX2DotRestoreGap(input, true);
    case "recall":
    case "indirect-recall":
    case "return":
    case "stop":
      return "synced";
    case "plain":
      return transferPlainX2DotRestoreGapState(input, op);
    case "cjump":
    case "loop": {
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      return transferConditionalX2DotRestoreGapState(input, effect);
    }
    case "indirect-cjump": {
      const effect = indirectConditionalX2EffectForGraphEdge(op, edge);
      return transferConditionalX2DotRestoreGapState(input, effect);
    }
    case "indirect-jump":
    case "indirect-call":
      return advanceX2DotRestoreGap(input, true);
  }
}

function transferX2ImmediateSyncState(input: boolean, op: IrOp, edge: Edge["kind"]): boolean {
  if (hasRewriteBarrier(op)) return false;

  switch (op.kind) {
    case "label":
      return input;
    case "recall":
    case "indirect-recall":
    case "return":
    case "stop":
      return true;
    case "plain": {
      const effect = plainX2Effect(op);
      return effect === "affects" || effect === "restores";
    }
    case "cjump":
    case "loop":
    case "indirect-cjump": {
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      return effect === "affects";
    }
    case "jump":
    case "call":
    case "orphan-address":
    case "store":
    case "indirect-store":
    case "indirect-jump":
    case "indirect-call":
      return false;
  }
}

function x2PreservingExecutableBoundary(input: X2RestoreBoundaryState): X2RestoreBoundaryState {
  return input === "none" ? "none" : "boundary";
}

function advanceX2DotRestoreGap(input: X2DotRestoreGapState, countsFromFreshSync: boolean): X2DotRestoreGapState {
  if (input === "none") return "none";
  if (input === "safe") return "safe";
  if (input === "one-gap") return "safe";
  return countsFromFreshSync ? "one-gap" : "synced";
}

function transferPlainX2RestoreBoundaryState(
  input: X2RestoreBoundaryState,
  effect: ReturnType<typeof plainX2Effect>,
): X2RestoreBoundaryState {
  if (effect === "affects" || effect === "restores") return "synced";
  if (effect === "preserves") return x2PreservingExecutableBoundary(input);
  return "none";
}

function transferPlainX2DotRestoreGapState(input: X2DotRestoreGapState, op: Extract<IrOp, { kind: "plain" }>): X2DotRestoreGapState {
  const effect = plainX2Effect(op);
  if (effect === "affects" || effect === "restores") return "synced";
  if (effect !== "preserves") return "none";
  return advanceX2DotRestoreGap(input, !isInitialIgnoredDotGapOp(op));
}

function transferConditionalX2RestoreBoundaryState(
  input: X2RestoreBoundaryState,
  effect: ReturnType<typeof conditionalX2Effect>,
): X2RestoreBoundaryState {
  if (effect === "affects") return "synced";
  if (effect === "preserves") return x2PreservingExecutableBoundary(input);
  return "none";
}

function transferConditionalX2DotRestoreGapState(
  input: X2DotRestoreGapState,
  effect: ReturnType<typeof conditionalX2Effect>,
): X2DotRestoreGapState {
  if (effect === "affects") return "synced";
  if (effect === "preserves") return input;
  return "none";
}

function joinX2RestoreBoundaryStates(
  current: X2RestoreBoundaryState | undefined,
  incoming: X2RestoreBoundaryState,
): X2RestoreBoundaryState {
  if (current === undefined) return incoming;
  return x2RestoreBoundaryRank(current) < x2RestoreBoundaryRank(incoming) ? current : incoming;
}

function x2RestoreBoundaryRank(state: X2RestoreBoundaryState): number {
  if (state === "none") return 0;
  if (state === "synced") return 1;
  return 2;
}

function joinX2DotRestoreGapStates(
  current: X2DotRestoreGapState | undefined,
  incoming: X2DotRestoreGapState,
): X2DotRestoreGapState {
  if (current === undefined) return incoming;
  return x2DotRestoreGapRank(current) < x2DotRestoreGapRank(incoming) ? current : incoming;
}

function joinX2ImmediateSyncStates(current: boolean | undefined, incoming: boolean): boolean {
  return current === undefined ? incoming : current && incoming;
}

function x2DotRestoreGapRank(state: X2DotRestoreGapState): number {
  if (state === "none") return 0;
  if (state === "synced") return 1;
  if (state === "one-gap") return 2;
  return 3;
}

function isInitialIgnoredDotGapOp(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return op.opcode === 0x54 || op.opcode === 0x55 || op.opcode === 0x56;
}

function addRegisterValue(input: RegisterValueSet, register: RegisterName): Set<RegisterName> {
  const output = new Set(input);
  output.add(register);
  return output;
}

function addStoredX2Alias(input: RegisterDataflowState, register: RegisterName): Set<RegisterName> {
  const output = new Set(input.x2);
  output.delete(register);
  if (setsIntersect(input.x, input.x2)) output.add(register);
  return output;
}

function removeRegisterValue(input: RegisterValueSet, register: RegisterName): Set<RegisterName> {
  const output = new Set(input);
  output.delete(register);
  return output;
}

function dropMutatedSelectorFact(input: RegisterDataflowState, register: RegisterName): RegisterDataflowState {
  return {
    x: removeRegisterValue(input.x, register),
    x2: removeRegisterValue(input.x2, register),
  };
}

function transferIndirectFlowRegisterState(
  input: RegisterDataflowState,
  op: Extract<IrOp, { kind: "indirect-jump" | "indirect-call" }>,
): RegisterDataflowState {
  if (isStableIndirectSelector(op.register)) return cloneRegisterDataflowState(input);
  return dropMutatedSelectorFact(input, op.register);
}

function transferIndirectConditionalRegisterState(
  input: RegisterDataflowState,
  op: Extract<IrOp, { kind: "indirect-cjump" }>,
  edge: Edge["kind"],
): RegisterDataflowState {
  const effect = indirectConditionalX2EffectForGraphEdge(op, edge);
  if (effect === "unknown") return emptyRegisterDataflowState();
  const output: RegisterDataflowState = {
    x: new Set(input.x),
    x2: transferConditionalX2RegisterSet(input, effect),
  };
  return edge === "jump" && !isStableIndirectSelector(op.register)
    ? dropMutatedSelectorFact(output, op.register)
    : output;
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

function transferPlainX2ValueState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "plain" }>,
): X2ValueDataflowState {
  if (op.opcode >= 0 && op.opcode <= 9) {
    return transferDecimalDigitX2ValueState(input, String(op.opcode));
  }
  if (op.opcode === 0x0a) {
    return transferDotRestoreX2ValueState(input);
  }
  if (op.opcode === 0x0b) {
    return transferSignChangeX2ValueState(input);
  }
  if (op.opcode === 0x0c) {
    return transferVpX2ValueState(input);
  }
  if (op.opcode === 0x0d) {
    return {
      x: new Set([NORMALIZED_DECIMAL_ZERO]),
      x2: new Set([NORMALIZED_DECIMAL_ZERO]),
      xShape: new Set([decimalMantissaShapeFact("0")]),
      x2Shape: new Set([decimalMantissaShapeFact("0")]),
      entry: closedX2EntryState(),
      vpContext: noneX2VpContextState(),
      vpEntryMantissa: new Set(["0"]),
      memory: input.memory,
    };
  }
  const effect = plainX2Effect(op);
  const closedExponentValues = closedExponentEntryDecimalFacts(input.entry);
  if (closedExponentValues.size > 0) {
    const x = plainPreservesXValue(op) ? new Set(closedExponentValues) : new Set<X2ValueFact>();
    const closedExponentShapes = closedExponentEntryShapeFacts(input.entry);
    const xShape = plainPreservesXValue(op) ? new Set(closedExponentShapes) : new Set<X2ShapeFact>();
    const x2 = effect === "preserves"
      ? new Set(closedExponentValues)
      : effect === "affects"
        ? new Set(x)
        : new Set<X2ValueFact>();
    return {
      x,
      x2,
      xShape,
      x2Shape: transferPlainX2ShapeSet(input, xShape, effect, closedExponentShapes),
      entry: nextX2EntryStateForPlainEffect(effect),
      vpContext: transferPlainX2VpContextState(input, effect),
      vpEntryMantissa: transferPlainX2VpEntryMantissaState(input, op, x, x2, effect),
      memory: input.memory,
    };
  }
  const closedExponentShapes = closedExponentEntryShapeFacts(input.entry);
  if (closedExponentShapes.size > 0) {
    const x = new Set<X2ValueFact>();
    const xShape = plainPreservesXValue(op) ? new Set(closedExponentShapes) : new Set<X2ShapeFact>();
    return {
      x,
      x2: new Set(),
      xShape,
      x2Shape: transferPlainX2ShapeSet(input, xShape, effect, closedExponentShapes),
      entry: nextX2EntryStateForPlainEffect(effect),
      vpContext: transferPlainX2VpContextState(input, effect),
      vpEntryMantissa: transferPlainX2VpEntryMantissaState(input, op, x, new Set(), effect),
      memory: input.memory,
    };
  }
  const x = syncUnknownSameValue(
    plainPreservesXValue(op) ? new Set(input.x) : new Set<X2ValueFact>(),
    effect,
  );
  const x2 = transferPlainX2ValueSet(input, x, effect);
  const xShape = plainPreservesXValue(op) ? cloneOptionalShapeSet(input.xShape) : new Set<X2ShapeFact>();
  return {
    x,
    x2,
    xShape,
    x2Shape: transferPlainX2ShapeSet(input, xShape, effect),
    entry: nextX2EntryStateForPlainEffect(effect),
    vpContext: transferPlainX2VpContextState(input, effect),
    vpEntryMantissa: transferPlainX2VpEntryMantissaState(input, op, x, x2, effect),
    memory: input.memory,
  };
}

function transferDecimalDigitX2ValueState(input: X2ValueDataflowState, digit: string): X2ValueDataflowState {
  if (input.entry.kind === "exponent") {
    const entry = advanceExponentDigitEntry(input.entry, digit);
    return x2ValueStateFromExponentEntry(entry, input.memory);
  }
  const entry = advanceDecimalDigitEntry(input.entry, digit);
  if (entry.kind !== "open") {
    return {
      x: new Set(),
      x2: new Set(),
      xShape: new Set(),
      x2Shape: new Set(),
      entry,
      vpContext: noneX2VpContextState(),
      memory: input.memory,
    };
  }
  const x = new Set<X2ValueFact>();
  const x2 = new Set<X2ValueFact>();
  const xShape = new Set<X2ShapeFact>();
  const x2Shape = new Set<X2ShapeFact>();
  for (const raw of entry.raw) {
    const normalized = normalizeDecimalEntry(raw);
    if (normalized !== undefined) {
      x.add(decimalValueFact(normalized, "normalized"));
      xShape.add(decimalMantissaShapeFact(normalized));
    }
    const x2Fact = x2DecimalEntryFact(raw);
    if (x2Fact !== undefined) x2.add(x2Fact);
    x2Shape.add(decimalMantissaShapeFact(raw));
  }
  return { x, x2, xShape, x2Shape, entry, vpContext: noneX2VpContextState(), memory: input.memory };
}

function transferDotRestoreX2ValueState(input: X2ValueDataflowState): X2ValueDataflowState {
  if (input.entry.kind !== "closed") {
    return {
      x: new Set(),
      x2: new Set(),
      entry: { kind: "unknown" },
      vpContext: { kind: "unknown" },
      memory: input.memory,
    };
  }
  return {
    x: normalizeX2RestoreFactsForX(input.x2),
    x2: new Set(input.x2),
    xShape: normalizeX2RestoreShapesForX(input.x2Shape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    vpEntryMantissa: vpEntryMantissasFromValueFacts(input.x2),
    memory: input.memory,
  };
}

function transferSignChangeX2ValueState(input: X2ValueDataflowState): X2ValueDataflowState {
  if (input.entry.kind === "exponent") {
    const entry = signChangeExponentEntry(input.entry) as Extract<X2EntryState, { kind: "exponent" }>;
    return x2ValueStateFromExponentEntry(entry, input.memory);
  }
  if (input.entry.kind === "closed" && input.vpContext?.kind === "exponent") {
    return {
      x: new Set(),
      x2: new Set(),
      xShape: new Set(),
      x2Shape: new Set(),
      entry: closedX2EntryState(),
      vpContext: signChangeVpContext(input.vpContext),
      memory: input.memory,
    };
  }
  if (input.entry.kind === "closed" && (input.vpContext === undefined || input.vpContext.kind === "none")) {
    const state = signChangeClosedDecimalState(input);
    if (state !== undefined) return state;
  }
  if (input.entry.kind !== "open") {
    return {
      x: new Set(),
      x2: new Set(),
      xShape: new Set(),
      x2Shape: new Set(),
      entry: { kind: "unknown" },
      vpContext: { kind: "unknown" },
      memory: input.memory,
    };
  }
  const x = new Set<X2ValueFact>();
  const x2 = new Set<X2ValueFact>();
  const xShape = new Set<X2ShapeFact>();
  const x2Shape = new Set<X2ShapeFact>();
  for (const raw of input.entry.raw) {
    const signed = signChangedDecimalEntry(raw);
    const normalized = normalizeDecimalEntry(signed);
    if (normalized !== undefined) {
      x.add(decimalValueFact(normalized, "normalized"));
      xShape.add(decimalMantissaShapeFact(normalized));
    }
    const x2Fact = x2DecimalEntryFact(signed);
    if (x2Fact !== undefined) x2.add(x2Fact);
    x2Shape.add(decimalMantissaShapeFact(signed));
  }
  return {
    x,
    x2,
    xShape,
    x2Shape,
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    vpEntryMantissa: signChangedMantissaShapes(input.entry.raw),
    memory: input.memory,
  };
}

function transferVpX2ValueState(input: X2ValueDataflowState): X2ValueDataflowState {
  if (input.entry.kind === "open") {
    // Keep this structural only: exponent-entry hidden X2 shapes can make a
    // later `.` signal ЕГГ0Г, so they are not safe value-alias facts yet.
    return {
      x: new Set(),
      x2: new Set(),
      xShape: new Set(),
      x2Shape: exponentEntryShapeFacts({
        kind: "exponent",
        mantissa: input.entry.raw,
        exponent: new Set([""]),
      }),
      entry: {
        kind: "exponent",
        mantissa: new Set(input.entry.raw),
        exponent: new Set([""]),
      },
      vpContext: {
        kind: "exponent",
        mantissa: new Set(input.entry.raw),
        exponent: new Set([""]),
      },
      memory: input.memory,
    };
  }
  if (input.entry.kind === "exponent") {
    return x2ValueStateFromExponentEntry(input.entry, input.memory);
  }
  if (input.entry.kind === "closed" && input.vpEntryMantissa !== undefined) {
    // This is not inferred from plain `X == X2`: the MK-61 previous-command
    // context matters. The fact is set only by proved X2 syncs that can feed
    // ordinary exponent entry, and is cleared by stores and non-empty gaps.
    return {
      x: new Set(),
      x2: new Set(),
      xShape: new Set(),
      x2Shape: exponentEntryShapeFacts({
        kind: "exponent",
        mantissa: input.vpEntryMantissa,
        exponent: new Set([""]),
      }),
      entry: {
        kind: "exponent",
        mantissa: new Set(input.vpEntryMantissa),
        exponent: new Set([""]),
      },
      vpContext: {
        kind: "exponent",
        mantissa: new Set(input.vpEntryMantissa),
        exponent: new Set([""]),
      },
      memory: input.memory,
    };
  }
  return {
    x: new Set(),
    x2: new Set(),
    xShape: new Set(),
    x2Shape: new Set(),
    entry: { kind: "unknown" },
    vpContext: { kind: "unknown" },
    memory: input.memory,
  };
}

function transferIndirectFlowX2ValueState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "indirect-jump" | "indirect-call" }>,
  trackRegisterMemory: boolean,
): X2ValueDataflowState {
  const closed = closeX2ValueEntry(input);
  if (isStableIndirectSelector(op.register)) return closed;
  return dropMutatedSelectorX2ValueFact(closed, op.register, trackRegisterMemory);
}

function transferIndirectConditionalX2ValueState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "indirect-cjump" }>,
  edge: Edge["kind"],
  trackRegisterMemory: boolean,
): X2ValueDataflowState {
  const effect = indirectConditionalX2EffectForGraphEdge(op, edge);
  if (effect === "unknown") return emptyX2ValueDataflowState(trackRegisterMemory);
  const closed = closeX2ValueEntry(input);
  const x = syncUnknownSameValue(new Set(closed.x), effect);
  const xShape = cloneOptionalShapeSet(closed.xShape);
  const output: X2ValueDataflowState = {
    x,
    x2: transferConditionalX2ValueSet(closed, x, effect),
    xShape,
    x2Shape: transferConditionalX2ShapeSet(closed, xShape, effect),
    entry: closedX2EntryState(),
    vpContext: transferConditionalX2VpContextState(closed, effect),
    vpEntryMantissa: transferConditionalX2VpEntryMantissaState(x, effect),
    memory: closed.memory,
  };
  return edge === "jump" && !isStableIndirectSelector(op.register)
    ? dropMutatedSelectorX2ValueFact(output, op.register, trackRegisterMemory)
    : output;
}

function transferIndirectStoreX2ValueState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "indirect-store" }>,
  trackRegisterMemory: boolean,
): X2ValueDataflowState {
  const target = knownIndirectMemoryTarget(op);
  if (target === undefined) {
    const closed = closeX2ValueEntry(input);
    return trackRegisterMemory ? clearX2ValueMemory(closed) : closed;
  }
  const closed = closeX2ValueEntry(input);
  const value = registerValueFact(target);
  return {
    x: addX2Value(closed.x, value),
    x2: addStoredX2ValueAlias(closed, value),
    xShape: cloneOptionalShapeSet(closed.xShape),
    x2Shape: cloneOptionalShapeSet(closed.x2Shape),
    entry: closedX2EntryState(),
    vpContext: cloneX2VpContextState(closed.vpContext),
    memory: trackRegisterMemory ? storeX2ValueMemory(closed.memory, target, closed.x) : undefined,
  };
}

function setsIntersect(left: RegisterValueSet, right: RegisterValueSet): boolean {
  for (const value of left) {
    if (right.has(value)) return true;
  }
  return false;
}

function transferPlainX2RegisterSet(
  input: RegisterDataflowState,
  x: RegisterValueSet,
  effect: ReturnType<typeof plainX2Effect>,
): Set<RegisterName> {
  if (effect === "preserves") return new Set(input.x2);
  if (effect === "affects") return new Set(x);
  return new Set();
}

function transferPlainX2ValueSet(
  input: X2ValueDataflowState,
  x: X2ValueSet,
  effect: ReturnType<typeof plainX2Effect>,
): Set<X2ValueFact> {
  if (effect === "preserves") return new Set(input.x2);
  if (effect === "affects") return new Set(x);
  return new Set();
}

function transferPlainX2ShapeSet(
  input: X2ValueDataflowState,
  xShape: X2ShapeSet,
  effect: ReturnType<typeof plainX2Effect>,
  closedEntryShape: X2ShapeSet | undefined = undefined,
): Set<X2ShapeFact> {
  if (effect === "preserves") return cloneOptionalShapeSet(closedEntryShape ?? input.x2Shape);
  if (effect === "affects") return new Set(xShape);
  return new Set();
}

function syncUnknownSameValue(
  x: Set<X2ValueFact>,
  effect: "affects" | "preserves" | "restores" | "unknown",
): Set<X2ValueFact> {
  if (effect === "affects" && x.size === 0) x.add(SAME_UNKNOWN_VALUE);
  return x;
}

function transferPlainX2VpContextState(
  input: X2ValueDataflowState,
  effect: ReturnType<typeof plainX2Effect>,
): X2VpContextState {
  return effect === "preserves" ? cloneX2VpContextState(input.vpContext) : noneX2VpContextState();
}

function transferPlainX2VpEntryMantissaState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "plain" }>,
  x: X2ValueSet,
  x2: X2ValueSet,
  effect: ReturnType<typeof plainX2Effect>,
): ReadonlySet<string> | undefined {
  if (effect === "affects") return sharedNormalizedDecimalMantissas({ x, x2 });
  if (effect === "preserves" && isEmptyPlainOp(op)) return cloneOptionalStringSet(input.vpEntryMantissa);
  return undefined;
}

function nextX2EntryStateForPlainEffect(effect: ReturnType<typeof plainX2Effect>): X2EntryState {
  if (effect === "restores") return { kind: "unknown" };
  return closedX2EntryState();
}

function transferConditionalX2RegisterSet(
  input: RegisterDataflowState,
  effect: ReturnType<typeof conditionalX2Effect>,
): Set<RegisterName> {
  if (effect === "preserves") return new Set(input.x2);
  if (effect === "affects") return new Set(input.x);
  return new Set();
}

function transferConditionalX2ValueSet(
  input: X2ValueDataflowState,
  x: X2ValueSet,
  effect: ReturnType<typeof conditionalX2Effect>,
): Set<X2ValueFact> {
  if (effect === "preserves") return new Set(input.x2);
  if (effect === "affects") return new Set(x);
  return new Set();
}

function transferConditionalX2ShapeSet(
  input: X2ValueDataflowState,
  xShape: X2ShapeSet,
  effect: ReturnType<typeof conditionalX2Effect>,
): Set<X2ShapeFact> {
  if (effect === "preserves") return cloneOptionalShapeSet(input.x2Shape);
  if (effect === "affects") return new Set(xShape);
  return new Set();
}

function transferConditionalX2VpContextState(
  input: X2ValueDataflowState,
  effect: ReturnType<typeof conditionalX2Effect>,
): X2VpContextState {
  return effect === "preserves" ? cloneX2VpContextState(input.vpContext) : noneX2VpContextState();
}

function transferConditionalX2VpEntryMantissaState(
  x: X2ValueSet,
  effect: ReturnType<typeof conditionalX2Effect>,
): ReadonlySet<string> | undefined {
  return effect === "affects" ? sharedNormalizedDecimalMantissas({ x, x2: x }) : undefined;
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

function joinX2ValueDataflowStates(
  current: X2ValueDataflowState | undefined,
  incoming: X2ValueDataflowState,
  trackRegisterMemory = false,
): X2ValueDataflowState {
  if (current === undefined) return {
    x: new Set(incoming.x),
    x2: new Set(incoming.x2),
    xShape: cloneOptionalShapeSet(incoming.xShape),
    x2Shape: cloneOptionalShapeSet(incoming.x2Shape),
    entry: cloneX2EntryState(incoming.entry),
    vpContext: cloneX2VpContextState(incoming.vpContext),
    vpEntryMantissa: cloneOptionalStringSet(incoming.vpEntryMantissa),
    memory: trackRegisterMemory ? cloneX2ValueMemory(incoming.memory) : undefined,
  };
  return {
    x: joinX2ValueSets(current.x, incoming.x),
    x2: joinX2ValueSets(current.x2, incoming.x2),
    xShape: joinOptionalShapeSets(current.xShape, incoming.xShape),
    x2Shape: joinOptionalShapeSets(current.x2Shape, incoming.x2Shape),
    entry: joinX2EntryStates(current.entry, incoming.entry),
    vpContext: joinX2VpContextStates(current.vpContext, incoming.vpContext),
    vpEntryMantissa: joinOptionalStringSets(current.vpEntryMantissa, incoming.vpEntryMantissa),
    memory: trackRegisterMemory ? joinX2ValueMemories(current.memory, incoming.memory) : undefined,
  };
}

function sameRegisterDataflowState(
  left: RegisterDataflowState | undefined,
  right: RegisterDataflowState | undefined,
): boolean {
  if (left === undefined || right === undefined) return left === right;
  return sameRegisterValueSet(left.x, right.x) && sameRegisterValueSet(left.x2, right.x2);
}

function sameX2ValueDataflowState(
  left: X2ValueDataflowState | undefined,
  right: X2ValueDataflowState | undefined,
): boolean {
  if (left === undefined || right === undefined) return left === right;
  return sameX2ValueSet(left.x, right.x) &&
    sameX2ValueSet(left.x2, right.x2) &&
    sameOptionalShapeSet(left.xShape, right.xShape) &&
    sameOptionalShapeSet(left.x2Shape, right.x2Shape) &&
    sameX2EntryState(left.entry, right.entry) &&
    sameX2VpContextState(left.vpContext, right.vpContext) &&
    sameOptionalStringSet(left.vpEntryMantissa, right.vpEntryMantissa) &&
    sameX2ValueMemory(left.memory, right.memory);
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

function joinX2ValueSets(
  current: X2ValueSet | undefined,
  incoming: X2ValueSet,
): Set<X2ValueFact> {
  if (current === undefined) return new Set(incoming);
  const joined = new Set<X2ValueFact>();
  for (const value of current) {
    if (incoming.has(value)) joined.add(value);
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

function sameX2ValueSet(
  left: X2ValueSet | undefined,
  right: X2ValueSet | undefined,
): boolean {
  if (left === undefined || right === undefined) return left === right;
  if (left.size !== right.size) return false;
  for (const value of left) {
    if (!right.has(value)) return false;
  }
  return true;
}

function cloneX2ValueMemory(input: X2ValueMemory | undefined): X2ValueMemory {
  const output: X2ValueMemory = {};
  for (const register of x2ValueMemoryRegisters(input)) {
    const values = input?.[register];
    if (values !== undefined && values.size > 0) output[register] = new Set(values);
  }
  return output;
}

function joinX2ValueMemories(
  current: X2ValueMemory | undefined,
  incoming: X2ValueMemory | undefined,
): X2ValueMemory {
  const output: X2ValueMemory = {};
  if (current === undefined || incoming === undefined) return output;
  for (const register of x2ValueMemoryRegisters(current)) {
    const values = intersectKnownX2ValueSets(current?.[register], incoming?.[register]);
    if (values.size > 0) output[register] = values;
  }
  return output;
}

function intersectKnownX2ValueSets(
  current: X2ValueSet | undefined,
  incoming: X2ValueSet | undefined,
): Set<X2ValueFact> {
  if (current === undefined || incoming === undefined) return new Set();
  const joined = new Set<X2ValueFact>();
  for (const value of current) {
    if (incoming.has(value)) joined.add(value);
  }
  return joined;
}

function sameX2ValueMemory(left: X2ValueMemory | undefined, right: X2ValueMemory | undefined): boolean {
  const leftRegisters = x2ValueMemoryRegisters(left);
  const rightRegisters = x2ValueMemoryRegisters(right);
  if (leftRegisters.length !== rightRegisters.length) return false;
  for (const register of leftRegisters) {
    if (!rightRegisters.includes(register)) return false;
    if (!sameX2ValueSet(left?.[register], right?.[register])) return false;
  }
  return true;
}

function x2ValueMemoryRegisters(input: X2ValueMemory | undefined): RegisterName[] {
  if (input === undefined) return [];
  return Object.keys(input).filter((key): key is RegisterName =>
    (REGISTER_NAMES as readonly string[]).includes(key) && input[key as RegisterName] !== undefined
  );
}

function storeX2ValueMemory(
  input: X2ValueMemory | undefined,
  register: RegisterName,
  values: X2ValueSet,
): X2ValueMemory {
  const output = cloneX2ValueMemory(input);
  const stored = concreteStoredX2ValueFacts(values);
  if (stored.size === 0) delete output[register];
  else output[register] = stored;
  return output;
}

function deleteX2ValueMemory(input: X2ValueMemory | undefined, register: RegisterName): X2ValueMemory {
  const output = cloneX2ValueMemory(input);
  delete output[register];
  return output;
}

function clearX2ValueMemory(input: X2ValueDataflowState): X2ValueDataflowState {
  return {
    x: new Set(input.x),
    x2: new Set(input.x2),
    xShape: cloneOptionalShapeSet(input.xShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: cloneX2EntryState(input.entry),
    vpContext: cloneX2VpContextState(input.vpContext),
    vpEntryMantissa: cloneOptionalStringSet(input.vpEntryMantissa),
    memory: {},
  };
}

function concreteStoredX2ValueFacts(values: X2ValueSet): Set<X2ValueFact> {
  const output = new Set<X2ValueFact>();
  for (const value of values) {
    if (isConcreteDecimalX2ValueFact(value)) output.add(value);
  }
  return output;
}

function isConcreteDecimalX2ValueFact(value: X2ValueFact): boolean {
  return /^decimal:-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+):(normalized|unnormalized)$/u.test(value);
}

function recallX2ValueFacts(
  input: X2ValueDataflowState,
  register: RegisterName,
  trackRegisterMemory: boolean,
): Set<X2ValueFact> {
  const value = registerValueFact(register);
  const output = new Set<X2ValueFact>(trackRegisterMemory ? input.memory?.[register] ?? [] : []);
  output.add(value);
  return output;
}

function x2ShapesFromValueFacts(values: X2ValueSet): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const value of values) {
    const decimal = /^decimal:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):(normalized|unnormalized)$/u.exec(value);
    if (decimal !== null) output.add(decimalMantissaShapeFact(decimal[1]!));
  }
  return output;
}

function vpEntryMantissasFromValueFacts(values: X2ValueSet): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  for (const value of values) {
    const decimal = /^decimal:(-?[0-9]+):normalized$/u.exec(value);
    if (decimal !== null) mantissas.add(decimal[1]!);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function cloneOptionalShapeSet(input: X2ShapeSet | undefined): Set<X2ShapeFact> {
  return input === undefined ? new Set() : new Set(input);
}

function joinOptionalShapeSets(
  current: X2ShapeSet | undefined,
  incoming: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  if (current === undefined || incoming === undefined) return new Set();
  const joined = new Set<X2ShapeFact>();
  for (const value of current) {
    if (incoming.has(value)) joined.add(value);
  }
  return joined;
}

function sameOptionalShapeSet(
  left: X2ShapeSet | undefined,
  right: X2ShapeSet | undefined,
): boolean {
  const leftSet = left ?? new Set<X2ShapeFact>();
  const rightSet = right ?? new Set<X2ShapeFact>();
  if (leftSet.size !== rightSet.size) return false;
  for (const value of leftSet) {
    if (!rightSet.has(value)) return false;
  }
  return true;
}

function joinStringSets(current: ReadonlySet<string>, incoming: ReadonlySet<string>): Set<string> {
  const joined = new Set<string>();
  for (const value of current) {
    if (incoming.has(value)) joined.add(value);
  }
  return joined;
}

function sameStringSet(left: ReadonlySet<string>, right: ReadonlySet<string>): boolean {
  if (left.size !== right.size) return false;
  for (const value of left) {
    if (!right.has(value)) return false;
  }
  return true;
}

function cloneOptionalStringSet(input: ReadonlySet<string> | undefined): ReadonlySet<string> | undefined {
  return input === undefined ? undefined : new Set(input);
}

function joinOptionalStringSets(
  current: ReadonlySet<string> | undefined,
  incoming: ReadonlySet<string> | undefined,
): ReadonlySet<string> | undefined {
  if (current === undefined || incoming === undefined) return undefined;
  const joined = joinStringSets(current, incoming);
  return joined.size === 0 ? undefined : joined;
}

function sameOptionalStringSet(
  left: ReadonlySet<string> | undefined,
  right: ReadonlySet<string> | undefined,
): boolean {
  if (left === undefined || right === undefined) return left === right;
  return sameStringSet(left, right);
}

function registerValueFact(register: RegisterName): X2ValueFact {
  return `reg:${register}`;
}

function decimalValueFact(value: string, flavor: "normalized" | "unnormalized"): X2ValueFact {
  return `decimal:${value}:${flavor}`;
}

function decimalMantissaShapeFact(value: string): X2ShapeFact {
  return `mantissa:${value}:decimal`;
}

function decimalExponentShapeFact(mantissa: string, exponent: string): X2ShapeFact {
  return `exponent:${mantissa}:${exponent}:decimal`;
}

function closedExponentEntryDecimalFacts(input: X2EntryState): Set<X2ValueFact> {
  const values = new Set<X2ValueFact>();
  if (input.kind !== "exponent") return values;
  for (const mantissa of input.mantissa) {
    for (const exponent of input.exponent) {
      const value = normalizedExponentEntryValue(mantissa, exponent);
      if (value !== undefined) values.add(decimalValueFact(value, "normalized"));
    }
  }
  return values;
}

function closedExponentEntryShapeFacts(input: X2EntryState): Set<X2ShapeFact> {
  if (input.kind !== "exponent") return new Set();
  const shapes = exponentEntryShapeFacts(input);
  for (const mantissa of input.mantissa) {
    for (const exponent of input.exponent) {
      const value = normalizedExponentEntryValue(mantissa, exponent);
      if (value !== undefined) shapes.add(decimalMantissaShapeFact(value));
    }
  }
  return shapes;
}

function exponentEntryShapeFacts(input: Extract<X2EntryState, { kind: "exponent" }>): Set<X2ShapeFact> {
  const shapes = new Set<X2ShapeFact>();
  for (const mantissa of input.mantissa) {
    for (const exponent of input.exponent) {
      shapes.add(decimalExponentShapeFact(mantissa, exponent));
    }
  }
  return shapes;
}

function normalizedExponentEntryValue(mantissa: string, exponent: string): string | undefined {
  const mantissaMatch = /^(-?)([0-9]{1,8})$/u.exec(mantissa);
  const exponentMatch = /^(-?)([0-9]{1,2})$/u.exec(exponent);
  if (mantissaMatch === null || exponentMatch === null) return undefined;
  const sign = mantissaMatch[1]!;
  const digits = effectiveExponentMantissaDigits(mantissaMatch[2]!);
  const exponentSign = exponentMatch[1]!;
  const shift = Number(exponentMatch[2]!);
  if (!Number.isInteger(shift)) return undefined;
  const unsigned = exponentSign === "-"
    ? decimalShiftRight(digits, shift)
    : `${digits}${"0".repeat(shift)}`;
  if (unsigned === undefined || significantDecimalDigits(unsigned) > 8) return undefined;
  return unsigned === "0" ? "0" : `${sign}${unsigned}`;
}

function effectiveExponentMantissaDigits(rawDigits: string): string {
  const stripped = rawDigits.replace(/^0+/u, "");
  if (stripped.length > 0) return stripped;
  return `1${"0".repeat(Math.max(0, rawDigits.length - 1))}`;
}

function decimalShiftRight(digits: string, places: number): string | undefined {
  if (places <= 0) return digits;
  const point = digits.length - places;
  const raw = point > 0
    ? `${digits.slice(0, point)}.${digits.slice(point)}`
    : `0.${"0".repeat(-point)}${digits}`;
  return normalizePlainDecimal(raw);
}

function normalizePlainDecimal(raw: string): string | undefined {
  const match = /^(-?)(?:([0-9]+)(?:\.([0-9]+))?|\.(\d+))$/u.exec(raw);
  if (match === null) return undefined;
  const sign = match[1]!;
  const integer = (match[2] ?? "0").replace(/^0+(?=\d)/u, "");
  const fraction = (match[3] ?? match[4] ?? "").replace(/0+$/u, "");
  const normalized = fraction.length === 0 ? integer : `${integer}.${fraction}`;
  if (normalized === "0") return "0";
  return `${sign}${normalized}`;
}

function significantDecimalDigits(input: string): number {
  const unsigned = input.startsWith("-") ? input.slice(1) : input;
  const digits = unsigned.replace(".", "").replace(/^0+/u, "");
  return digits.length === 0 ? 1 : digits.length;
}

function normalizeDecimalEntry(raw: string): string | undefined {
  const match = /^(-?)([0-9]{1,8})$/u.exec(raw);
  if (match === null) return undefined;
  const sign = match[1]!;
  const digits = match[2]!.replace(/^0+(?=\d)/u, "");
  if (digits === "0") return "0";
  return `${sign}${digits}`;
}

function x2DecimalEntryFact(raw: string): X2ValueFact | undefined {
  const normalized = normalizeDecimalEntry(raw);
  if (normalized === undefined) return undefined;
  if (raw === normalized) return decimalValueFact(raw, "normalized");
  return decimalValueFact(raw, "unnormalized");
}

function normalizeX2RestoreFactsForX(input: X2ValueSet): Set<X2ValueFact> {
  const output = new Set<X2ValueFact>();
  for (const fact of input) {
    const decimal = /^decimal:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):(normalized|unnormalized)$/u.exec(fact);
    if (decimal) {
      const normalized = normalizePlainDecimal(decimal[1]!);
      if (normalized !== undefined) output.add(decimalValueFact(normalized, "normalized"));
      continue;
    }
    output.add(fact);
  }
  return output;
}

function normalizeX2RestoreShapesForX(input: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const fact of input ?? []) {
    const mantissa = /^mantissa:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):decimal$/u.exec(fact);
    if (mantissa !== null) {
      const normalized = normalizePlainDecimal(mantissa[1]!);
      if (normalized !== undefined) output.add(decimalMantissaShapeFact(normalized));
      continue;
    }
    output.add(fact);
  }
  return output;
}

function signChangedDecimalEntry(raw: string): string {
  const normalized = normalizeDecimalEntry(raw);
  if (normalized === undefined || normalized === "0") return "0";
  return raw.startsWith("-") ? raw.slice(1) : `-${raw}`;
}

function signChangeClosedDecimalState(input: X2ValueDataflowState): X2ValueDataflowState | undefined {
  const shaped = signChangedVpEntryMantissas(input);
  if (shaped !== undefined) return x2ValueStateFromMantissaShapes(shaped, input.memory);
  const shapeBacked = signChangedClosedShapeMantissas(input);
  if (shapeBacked !== undefined) return x2ValueStateFromMantissaShapes(shapeBacked, input.memory);

  const values = new Set<X2ValueFact>();
  if (input.x.has(SAME_UNKNOWN_VALUE) && input.x2.has(SAME_UNKNOWN_VALUE)) {
    values.add(SAME_UNKNOWN_VALUE);
  }
  for (const fact of input.x2) {
    if (!input.x.has(fact)) continue;
    const decimal = /^decimal:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):normalized$/u.exec(fact);
    if (decimal === null) continue;
    values.add(decimalValueFact(signChangedNormalizedDecimalValue(decimal[1]!), "normalized"));
  }
  if (values.size === 0) return undefined;
  const shapes = new Set<X2ShapeFact>();
  for (const value of values) {
    const decimal = /^decimal:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):normalized$/u.exec(value);
    if (decimal !== null) shapes.add(decimalMantissaShapeFact(decimal[1]!));
  }
  return {
    x: values,
    x2: new Set(values),
    xShape: shapes,
    x2Shape: new Set(shapes),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    vpEntryMantissa: vpEntryMantissasFromValueFacts(values),
    memory: input.memory,
  };
}

function signChangedNormalizedDecimalValue(raw: string): string {
  const normalized = normalizePlainDecimal(raw);
  if (normalized === undefined || normalized === "0") return "0";
  return normalized.startsWith("-") ? normalized.slice(1) : `-${normalized}`;
}

function signChangedClosedShapeMantissas(input: X2ValueDataflowState): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  for (const fact of input.x2Shape ?? []) {
    const match = /^mantissa:(-?[0-9]+):decimal$/u.exec(fact);
    if (match === null) continue;
    const raw = match[1]!;
    const normalized = normalizeDecimalEntry(raw);
    if (normalized === undefined) continue;
    if (input.xShape?.has(decimalMantissaShapeFact(normalized)) !== true) continue;
    const signed = signChangedMantissaShape(raw);
    if (signed === undefined) return undefined;
    mantissas.add(signed);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function sharedNormalizedDecimalMantissas(input: Pick<X2ValueDataflowState, "x" | "x2">): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  for (const fact of input.x2) {
    if (!input.x.has(fact)) continue;
    const decimal = /^decimal:(-?[0-9]+):normalized$/u.exec(fact);
    if (decimal !== null) mantissas.add(decimal[1]!);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function isEmptyPlainOp(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return op.opcode >= 0x54 && op.opcode <= 0x56;
}

function signChangedVpEntryMantissas(input: X2ValueDataflowState): ReadonlySet<string> | undefined {
  if (input.vpEntryMantissa !== undefined) return signChangedMantissaShapes(input.vpEntryMantissa);
  const mantissas = new Set<string>();
  for (const fact of input.x2) {
    if (!input.x.has(fact)) continue;
    const decimal = /^decimal:(-?[0-9]+):normalized$/u.exec(fact);
    if (decimal === null) continue;
    const signed = signChangedMantissaShape(decimal[1]!);
    if (signed === undefined) return undefined;
    mantissas.add(signed);
  }
  return mantissas.size > 0 ? mantissas : undefined;
}

function signChangedMantissaShapes(input: ReadonlySet<string>): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  for (const raw of input) {
    const signed = signChangedMantissaShape(raw);
    if (signed === undefined) return undefined;
    mantissas.add(signed);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function signChangedMantissaShape(raw: string): string | undefined {
  const normalized = normalizeDecimalEntry(raw);
  if (normalized === undefined) return undefined;
  if (normalized === "0") return "-0";
  return raw.startsWith("-") ? raw.slice(1) : `-${raw}`;
}

function x2ValueStateFromMantissaShapes(
  mantissas: ReadonlySet<string>,
  memory: X2ValueMemory | undefined = undefined,
): X2ValueDataflowState | undefined {
  const x = new Set<X2ValueFact>();
  const x2 = new Set<X2ValueFact>();
  const xShape = new Set<X2ShapeFact>();
  const x2Shape = new Set<X2ShapeFact>();
  for (const raw of mantissas) {
    const normalized = normalizeDecimalEntry(raw);
    const x2Fact = x2DecimalEntryFact(raw);
    if (normalized === undefined || x2Fact === undefined) return undefined;
    x.add(decimalValueFact(normalized, "normalized"));
    x2.add(x2Fact);
    xShape.add(decimalMantissaShapeFact(normalized));
    x2Shape.add(decimalMantissaShapeFact(raw));
  }
  return {
    x,
    x2,
    xShape,
    x2Shape,
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    vpEntryMantissa: mantissas,
    memory,
  };
}

function x2ValueStateFromExponentEntry(
  input: X2EntryState,
  memory: X2ValueMemory | undefined = undefined,
): X2ValueDataflowState {
  if (input.kind !== "exponent") {
    return {
      x: new Set(),
      x2: new Set(),
      xShape: new Set(),
      x2Shape: new Set(),
      entry: cloneX2EntryState(input),
      vpContext: { kind: "unknown" },
      memory,
    };
  }
  const values = closedExponentEntryDecimalFacts(input);
  const shapes = closedExponentEntryShapeFacts(input);
  return {
    x: new Set(values),
    // Active exponent entry already has a normalized visible X value, but
    // hidden X2 is still a ВП-entry shape. A following `.` may signal ЕГГ0Г,
    // so dot-safe X2 value facts appear only after a closing X2 sync.
    x2: new Set(),
    xShape: new Set(shapes),
    x2Shape: new Set(shapes),
    entry: cloneX2EntryState(input),
    vpContext: x2VpContextFromExponentEntry(input),
    memory,
  };
}

function closedX2EntryState(): X2EntryState {
  return { kind: "closed" };
}

function cloneX2EntryState(input: X2EntryState): X2EntryState {
  if (input.kind === "open") return { kind: "open", raw: new Set(input.raw) };
  if (input.kind === "exponent") {
    return {
      kind: "exponent",
      mantissa: new Set(input.mantissa),
      exponent: new Set(input.exponent),
    };
  }
  return input;
}

function noneX2VpContextState(): X2VpContextState {
  return { kind: "none" };
}

function cloneX2VpContextState(input: X2VpContextState | undefined): X2VpContextState {
  if (input === undefined || input.kind === "none" || input.kind === "unknown") return input ?? noneX2VpContextState();
  return {
    kind: "exponent",
    mantissa: new Set(input.mantissa),
    exponent: new Set(input.exponent),
  };
}

function x2VpContextFromExponentEntry(input: Extract<X2EntryState, { kind: "exponent" }>): X2VpContextState {
  return {
    kind: "exponent",
    mantissa: new Set(input.mantissa),
    exponent: new Set(input.exponent),
  };
}

function closeX2ValueEntry(input: X2ValueDataflowState): X2ValueDataflowState {
  const closedExponentValues = closedExponentEntryDecimalFacts(input.entry);
  const closedExponentShapes = closedExponentEntryShapeFacts(input.entry);
  if (closedExponentValues.size > 0 || closedExponentShapes.size > 0) {
    return {
      x: new Set(closedExponentValues),
      x2: new Set(closedExponentValues),
      xShape: new Set(closedExponentShapes),
      x2Shape: new Set(closedExponentShapes),
      entry: closedX2EntryState(),
      vpContext: cloneX2VpContextState(input.vpContext),
      vpEntryMantissa: vpEntryMantissasFromValueFacts(closedExponentValues),
      memory: input.memory,
    };
  }
  return {
    x: new Set(input.x),
    x2: new Set(input.x2),
    xShape: cloneOptionalShapeSet(input.xShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: closedX2EntryState(),
    vpContext: cloneX2VpContextState(input.vpContext),
    vpEntryMantissa: cloneOptionalStringSet(input.vpEntryMantissa),
    memory: input.memory,
  };
}

function advanceDecimalDigitEntry(input: X2EntryState, digit: string): X2EntryState {
  if (input.kind === "unknown") return { kind: "unknown" };
  if (input.kind === "exponent") return advanceExponentDigitEntry(input, digit);
  const source = input.kind === "closed" ? new Set([""]) : input.raw;
  const raw = new Set<string>();
  for (const prefix of source) {
    const next = `${prefix}${digit}`;
    if (next.length > 8) return { kind: "unknown" };
    raw.add(next);
  }
  return { kind: "open", raw };
}

function advanceExponentDigitEntry(input: Extract<X2EntryState, { kind: "exponent" }>, digit: string): X2EntryState {
  const exponent = new Set<string>();
  for (const prefix of input.exponent) {
    const sign = prefix.startsWith("-") ? "-" : "";
    const digits = prefix.slice(sign.length);
    if (digits.length >= 2) return { kind: "unknown" };
    exponent.add(`${prefix}${digit}`);
  }
  return {
    kind: "exponent",
    mantissa: new Set(input.mantissa),
    exponent,
  };
}

function signChangeExponentEntry(input: Extract<X2EntryState, { kind: "exponent" }>): X2EntryState {
  const exponent = new Set<string>();
  for (const raw of input.exponent) {
    exponent.add(raw.startsWith("-") ? raw.slice(1) : `-${raw}`);
  }
  return {
    kind: "exponent",
    mantissa: new Set(input.mantissa),
    exponent,
  };
}

function signChangeVpContext(input: Extract<X2VpContextState, { kind: "exponent" }>): X2VpContextState {
  const exponent = new Set<string>();
  for (const raw of input.exponent) {
    exponent.add(raw.startsWith("-") ? raw.slice(1) : `-${raw}`);
  }
  return {
    kind: "exponent",
    mantissa: new Set(input.mantissa),
    exponent,
  };
}

function joinX2EntryStates(current: X2EntryState, incoming: X2EntryState): X2EntryState {
  if (current.kind === "unknown" || incoming.kind === "unknown") return { kind: "unknown" };
  if (current.kind === "closed" || incoming.kind === "closed" || current.kind !== incoming.kind) {
    return current.kind === incoming.kind ? closedX2EntryState() : { kind: "unknown" };
  }
  if (current.kind === "exponent" && incoming.kind === "exponent") {
    const mantissa = joinStringSets(current.mantissa, incoming.mantissa);
    const exponent = joinStringSets(current.exponent, incoming.exponent);
    return mantissa.size === 0 || exponent.size === 0
      ? { kind: "unknown" }
      : { kind: "exponent", mantissa, exponent };
  }
  if (current.kind !== "open" || incoming.kind !== "open") return { kind: "unknown" };
  const joined = joinStringSets(current.raw, incoming.raw);
  return joined.size === 0 ? { kind: "unknown" } : { kind: "open", raw: joined };
}

function joinX2VpContextStates(
  current: X2VpContextState | undefined,
  incoming: X2VpContextState | undefined,
): X2VpContextState {
  const left = current ?? noneX2VpContextState();
  const right = incoming ?? noneX2VpContextState();
  if (left.kind === "unknown" || right.kind === "unknown") return { kind: "unknown" };
  if (left.kind === "none" || right.kind === "none" || left.kind !== right.kind) {
    return left.kind === right.kind ? noneX2VpContextState() : { kind: "unknown" };
  }
  const mantissa = joinStringSets(left.mantissa, right.mantissa);
  const exponent = joinStringSets(left.exponent, right.exponent);
  return mantissa.size === 0 || exponent.size === 0
    ? { kind: "unknown" }
    : { kind: "exponent", mantissa, exponent };
}

function sameX2EntryState(left: X2EntryState, right: X2EntryState): boolean {
  if (left.kind !== right.kind) return false;
  if (left.kind === "exponent" && right.kind === "exponent") {
    return sameStringSet(left.mantissa, right.mantissa) && sameStringSet(left.exponent, right.exponent);
  }
  if (left.kind !== "open" || right.kind !== "open") return true;
  return sameStringSet(left.raw, right.raw);
}

function sameX2VpContextState(
  leftInput: X2VpContextState | undefined,
  rightInput: X2VpContextState | undefined,
): boolean {
  const left = leftInput ?? noneX2VpContextState();
  const right = rightInput ?? noneX2VpContextState();
  if (left.kind !== right.kind) return false;
  if (left.kind !== "exponent" || right.kind !== "exponent") return true;
  return sameStringSet(left.mantissa, right.mantissa) && sameStringSet(left.exponent, right.exponent);
}

function addX2Value(input: X2ValueSet, value: X2ValueFact): Set<X2ValueFact> {
  const output = new Set(input);
  output.add(value);
  return output;
}

function addStoredX2ValueAlias(input: X2ValueDataflowState, value: X2ValueFact): Set<X2ValueFact> {
  const output = new Set(input.x2);
  output.delete(value);
  if (x2ValueSetHasIntersection(input.x, input.x2)) output.add(value);
  return output;
}

function removeX2Value(input: X2ValueSet, value: X2ValueFact): Set<X2ValueFact> {
  const output = new Set(input);
  output.delete(value);
  return output;
}

function dropMutatedSelectorX2ValueFact(
  input: X2ValueDataflowState,
  register: RegisterName,
  trackRegisterMemory: boolean,
): X2ValueDataflowState {
  const value = registerValueFact(register);
  return {
    x: removeX2Value(input.x, value),
    x2: removeX2Value(input.x2, value),
    xShape: cloneOptionalShapeSet(input.xShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: cloneX2EntryState(input.entry),
    vpContext: cloneX2VpContextState(input.vpContext),
    vpEntryMantissa: cloneOptionalStringSet(input.vpEntryMantissa),
    memory: trackRegisterMemory ? deleteX2ValueMemory(input.memory, register) : undefined,
  };
}

function buildRegisterValueGraph(ops: readonly IrOp[]): Edge[][] {
  const labels = labelIndexes(ops);
  const addresses = addressIndexes(ops);
  const successors: Edge[][] = Array.from({ length: ops.length }, () => []);
  const callReturns: number[] = [];
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    const next = index + 1;
    if ((op.kind === "call" || (op.kind === "indirect-call" && knownIndirectFlowTarget(op) !== undefined)) && next < ops.length) {
      callReturns.push(next);
    }
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
    const jumpToAddress = (target: number): void => {
      const targetIndex = addresses.get(target);
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
        if (target !== undefined) jumpToAddress(target);
        break;
      }
      case "indirect-call": {
        const target = knownIndirectFlowTarget(op);
        if (target !== undefined) jumpToAddress(target);
        break;
      }
      case "indirect-cjump": {
        const target = knownIndirectFlowTarget(op);
        if (target !== undefined) jumpToAddress(target);
        fallthrough();
        break;
      }
      case "return":
        for (const target of callReturns) normal(target);
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
  const addresses = addressIndexes(ops);
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
        case "indirect-jump": {
          const target = knownIndirectFlowTarget(op);
          const targetIndex = target === undefined ? undefined : addresses.get(target);
          return targetIndex === undefined ? true : visit(targetIndex, depth, returnStack);
        }
        case "indirect-call": {
          const target = knownIndirectFlowTarget(op);
          const targetIndex = target === undefined ? undefined : addresses.get(target);
          if (targetIndex === undefined || returnStack.length >= 5) return true;
          return visit(targetIndex, depth, [i + 1, ...returnStack]);
        }
        case "indirect-cjump": {
          const target = knownIndirectFlowTarget(op);
          const targetIndex = target === undefined ? undefined : addresses.get(target);
          return (
            (targetIndex === undefined ? true : visit(targetIndex, depth, returnStack)) ||
            visit(i + 1, depth, returnStack)
          );
        }
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

export function removingStackLiftCanExposeStack(ops: readonly IrOp[], liftIndex: number): boolean {
  return stackDifferenceCanReachConsumer(ops, liftIndex + 1, 1);
}

export function removingPreShiftLiftCanExposeStack(ops: readonly IrOp[], producerIndex: number): boolean {
  return stackDifferenceCanReachConsumer(ops, producerIndex + 1, 2);
}

export function replacingNumberEntryCanExposeStackLift(ops: readonly IrOp[], numberEntryEndIndex: number): boolean {
  return stackDifferenceCanReachConsumer(ops, numberEntryEndIndex + 1, 1);
}

export function removingRecallCanExposeX2Restore(
  ops: readonly IrOp[],
  recallIndex: number,
  options: X2RestoreExposureOptions = {},
): boolean {
  const labels = labelIndexes(ops);
  const addresses = addressIndexes(ops);
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
        case "indirect-jump": {
          const target = knownIndirectFlowTarget(op);
          const targetIndex = target === undefined ? undefined : addresses.get(target);
          return targetIndex === undefined ? true : visit(targetIndex, returnStack, true);
        }
        case "indirect-call": {
          const target = knownIndirectFlowTarget(op);
          const targetIndex = target === undefined ? undefined : addresses.get(target);
          if (targetIndex === undefined || returnStack.length >= 5) return true;
          return visit(targetIndex, [i + 1, ...returnStack], true);
        }
        case "indirect-cjump": {
          const target = knownIndirectFlowTarget(op);
          const targetIndex = target === undefined ? undefined : addresses.get(target);
          const fallthrough = conditionalX2Effect(op, "fallthrough");
          const jump = conditionalX2Effect(op, "jump");
          if (fallthrough === "unknown" || jump === "unknown") return true;
          return (
            (jump === "preserves" && (targetIndex === undefined ? true : visit(targetIndex, returnStack, true))) ||
            (fallthrough === "preserves" && visit(i + 1, returnStack, true))
          );
        }
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
