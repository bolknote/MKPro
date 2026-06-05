import type {
  AppliedOptimization,
  CompileOptions,
  IrOp,
  IrTargetMeta,
  OpcodeInfo,
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
  | `expr:${number}`
  | `expr-key:${string}`
  | "same:unknown"
  | `decimal:${string}:normalized`
  | `decimal:${string}:unnormalized`;
export type X2ValueSet = ReadonlySet<X2ValueFact>;
export type X2ShapeFact =
  | `mantissa:${string}:decimal`
  | `exponent:${string}:${string}:decimal`
  | `hex:${string}:mantissa`
  | `hex-exponent:${string}:${string}`
  | `super:${string}`
  | `super-exponent:${string}:${string}`;
export type X2ShapeSet = ReadonlySet<X2ShapeFact>;
export type X2ShapeSafety = "dotSafeDecimal" | "structuralOnly" | "errorProne" | "unknown";
export type ParsedX2ShapeFact =
  | {
      readonly kind: "decimal-mantissa";
      readonly raw: string;
      readonly normalized: string | undefined;
      readonly safety: X2ShapeSafety;
    }
  | {
      readonly kind: "decimal-exponent";
      readonly mantissa: string;
      readonly exponent: string;
      readonly normalized: string | undefined;
      readonly safety: X2ShapeSafety;
    }
  | { readonly kind: "hex-mantissa"; readonly raw: string; readonly safety: X2ShapeSafety }
  | { readonly kind: "hex-exponent"; readonly mantissa: string; readonly exponent: string; readonly safety: X2ShapeSafety }
  | { readonly kind: "super-mantissa"; readonly raw: string; readonly safety: X2ShapeSafety }
  | { readonly kind: "super-exponent"; readonly mantissa: string; readonly exponent: string; readonly safety: X2ShapeSafety }
  | { readonly kind: "unknown"; readonly raw: string; readonly safety: "unknown" };
export type X2MantissaRadix = "decimal" | "hex" | "super" | "unknown";
export interface X2MantissaDataModel {
  readonly kind: "mantissa";
  readonly radix: X2MantissaRadix;
  readonly raw: string;
  readonly canonical: string;
  readonly sign: "" | "-";
  readonly hasDecimalPoint: boolean;
  readonly hasLeadingZero: boolean;
  readonly digits: readonly string[];
  readonly significantDigits: number;
  readonly normalizedDecimal?: string | undefined;
  readonly normalizedSameAsRaw: boolean;
  readonly safety: X2ShapeSafety;
}
export type X2ShapeDataModel =
  | X2MantissaDataModel
  | {
      readonly kind: "exponent-entry";
      readonly mantissa: X2MantissaDataModel;
      readonly exponentRaw: string;
      readonly exponentSign: "" | "-";
      readonly exponentDigits: readonly string[];
      readonly normalizedDecimal?: string | undefined;
      readonly closedStructuralMantissa?: X2MantissaDataModel | undefined;
      readonly safety: X2ShapeSafety;
    }
  | { readonly kind: "unknown"; readonly raw: string; readonly safety: "unknown" };
type X2ValueMemory = Partial<Record<RegisterName, X2ValueSet>>;
type X2ShapeMemory = Partial<Record<RegisterName, X2ShapeSet>>;

const NORMALIZED_DECIMAL_ZERO: X2ValueFact = "decimal:0:normalized";
const SAME_UNKNOWN_VALUE: X2ValueFact = "same:unknown";
const X2_SIGN_CHANGE_OPCODE = 0x0b;
const STACK_EXCHANGE_XY_OPCODE = 0x14;
const X2_EMPTY_OPCODE_START = 0x54;
const X2_EMPTY_OPCODE_END = 0x56;
const COMMUTATIVE_STABLE_EXPR_OPCODES = new Set<number>([
  0x10, // +
  0x12, // *
  0x36, // К max
  0x37, // К ∧
  0x38, // К ∨
  0x39, // К ⊕
]);
const STABLE_CONSTANT_EXPR_OPCODES = new Set<number>([
  0x20, // F pi
]);
const PURE_OPAQUE_EXPR_OPCODES = new Set<number>([
  0x10, // +
  0x11, // -
  0x12, // *
  0x13, // /
  0x15, // F 10^x
  0x16, // F e^x
  0x17, // F lg
  0x18, // F ln
  0x19, // F sin^-1
  0x1a, // F cos^-1
  0x1b, // F tg^-1
  0x1c, // F sin
  0x1d, // F cos
  0x1e, // F tg
  0x21, // F sqrt
  0x22, // F x^2
  0x23, // F 1/x
  0x24, // F x^y
  0x26, // К °->′
  0x2a, // К °->′"
  0x30, // К °<-′"
  0x31, // К |x|
  0x32, // К ЗН
  0x33, // К °<-′
  0x34, // К [x]
  0x35, // К {x}
  0x36, // К max
  0x37, // К ∧
  0x38, // К ∨
  0x39, // К ⊕
  0x3a, // К ИНВ
]);
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
  readonly y?: X2ValueSet | undefined;
  readonly x2: X2ValueSet;
  readonly xShape?: X2ShapeSet;
  readonly yShape?: X2ShapeSet | undefined;
  readonly x2Shape?: X2ShapeSet;
  readonly entry: X2EntryState;
  readonly vpContext?: X2VpContextState;
  readonly structuralEntry?: X2StructuralEntryState;
  readonly structuralVpContext?: X2StructuralEntryState;
  readonly vpEntryMantissa?: ReadonlySet<string> | undefined;
  readonly vpEntryShape?: X2ShapeSet | undefined;
  readonly memory?: X2ValueMemory | undefined;
  readonly shapeMemory?: X2ShapeMemory | undefined;
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

type X2StructuralEntryState =
  | { readonly kind: "none" }
  | {
      readonly kind: "exponent";
      readonly mantissa: X2ShapeSet;
      readonly exponent: ReadonlySet<string>;
    }
  | { readonly kind: "unknown" };

export interface X2RestoreExposureOptions {
  readonly redundantSyncRegister?: RegisterName | undefined;
  readonly redundantSyncValue?: boolean | undefined;
  readonly redundantSyncShape?: boolean | undefined;
}

export interface X2VpShapeContextAnalysis {
  readonly kind:
    | "none"
    | "active-mantissa"
    | "active-exponent"
    | "vp-exponent-context"
    | "active-structural-exponent"
    | "vp-structural-exponent-context"
    | "unknown";
  readonly phase: "none" | "active-entry" | "vp-context" | "unknown";
  readonly source: "none" | "decimal" | "structural" | "unknown";
  readonly mantissa?: ReadonlySet<string> | undefined;
  readonly shape?: X2ShapeSet | undefined;
  readonly exponent?: ReadonlySet<string> | undefined;
  readonly hasExponentDigit: boolean;
  readonly restoresX2: boolean;
  readonly canDiscardSeparatorBeforeNonDigit: boolean;
  readonly canDiscardSeparatorBeforeSignChange: boolean;
  readonly canDiscardRestoreBeforeFreshDigit: boolean;
  readonly canCancelExponentSignPair: boolean;
}

export interface X2StackEffectAnalysis {
  readonly x2Effect: OpcodeInfo["x2Effect"];
  readonly stackEffect: OpcodeInfo["stackEffect"];
  readonly stackShifts: boolean;
  readonly stackPreserves: boolean;
  readonly stackConsumes: boolean;
  readonly stackExposes: boolean;
  readonly stackBarrier: boolean;
  readonly x2Affects: boolean;
  readonly x2Preserves: boolean;
  readonly x2Restores: boolean;
  readonly hardX2OverwriteWithoutStackUse: boolean;
  readonly stackLiftAndX2Sync: boolean;
}

export interface RecallValueProof {
  readonly register: RegisterName;
  readonly inX: boolean;
  readonly x2SyncRegister?: RegisterName | undefined;
  readonly x2SyncValue: boolean;
  readonly x2SyncShape?: true | undefined;
}

export interface RecallRemovalAnalysis {
  readonly register: RegisterName;
  readonly valueProof?: RecallValueProof | undefined;
  readonly redundantSyncRegister?: RegisterName | undefined;
  readonly redundantSyncValue: boolean;
  readonly redundantSyncShape: boolean;
  readonly x2SyncRedundant: boolean;
  readonly exposesStackLift: boolean;
  readonly exposesX2Restore: boolean;
  readonly removable: boolean;
}

export interface DirectReturnAnalysisContext {
  readonly labelEntries: ReadonlySet<number>;
  readonly labels: ReadonlyMap<string, number>;
  readonly addresses: ReadonlyMap<number, number>;
}

export type KnownReturnCallOp =
  | Extract<IrOp, { kind: "call" }>
  | Extract<IrOp, { kind: "indirect-call" }>;

export function isKnownReturnCallOp(op: IrOp): op is KnownReturnCallOp {
  return op.kind === "call" || op.kind === "indirect-call";
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

function hasIrRoles(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return "meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0;
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

export function computeLabelEntryIndexes(
  ops: readonly IrOp[],
  options: { readonly procedureBoundary?: "any" | "start" } = {},
): Set<number> {
  const stringTargets = new Set<string>();
  const numericTargets = new Set<number>();
  let unknownIndirectFlow = false;
  for (const op of ops) {
    const target = directFlowTarget(op);
    if (typeof target === "string") stringTargets.add(target);
    if (typeof target === "number") numericTargets.add(target);
    if (isIndirectFlowOp(op)) {
      const indirectTarget = knownIndirectFlowTarget(op);
      if (indirectTarget === undefined) unknownIndirectFlow = true;
      else numericTargets.add(indirectTarget);
    }
  }

  const procedureBoundary = options.procedureBoundary ?? "any";
  const entries = new Set<number>();
  let address = 0;
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      const isProcedureEntry = procedureBoundary === "any"
        ? op.procedureBoundary !== undefined
        : op.procedureBoundary === "start";
      if (
        isProcedureEntry ||
        unknownIndirectFlow ||
        stringTargets.has(op.name) ||
        numericTargets.has(address)
      ) {
        entries.add(index);
      }
      continue;
    }
    address += cellsPerOp(op);
  }
  return entries;
}

export function directReturnAnalysisContext(ops: readonly IrOp[]): DirectReturnAnalysisContext {
  return {
    labelEntries: computeLabelEntryIndexes(ops),
    labels: labelIndexes(ops),
    addresses: addressIndexes(ops),
  };
}

export function directCallTargetIndex(
  call: Extract<IrOp, { kind: "call" }>,
  context: DirectReturnAnalysisContext,
): number | undefined {
  return typeof call.target === "string"
    ? context.labels.get(call.target)
    : context.addresses.get(call.target);
}

export function knownReturnCallTargetIndex(
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
): number | undefined {
  if (call.kind === "call") return directCallTargetIndex(call, context);
  const target = knownIndirectFlowTarget(call);
  return target === undefined ? undefined : context.addresses.get(target);
}

export function directCallReturnsThroughTransparentRange(
  ops: readonly IrOp[],
  call: Extract<IrOp, { kind: "call" }>,
  context: DirectReturnAnalysisContext,
  isTransparent: (op: IrOp) => boolean,
): boolean {
  const targetIndex = directCallTargetIndex(call, context);
  if (targetIndex === undefined) return false;
  return linearReturnRangeIsTransparent(ops, targetIndex, context.labelEntries, isTransparent);
}

export function knownReturnCallReturnsThroughTransparentRange(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
  isTransparent: (op: IrOp) => boolean,
): boolean {
  const targetIndex = knownReturnCallTargetIndex(call, context);
  if (targetIndex === undefined) return false;
  return linearReturnRangeIsTransparent(ops, targetIndex, context.labelEntries, isTransparent);
}

export function linearReturnRangeIsTransparent(
  ops: readonly IrOp[],
  targetIndex: number,
  labelEntries: ReadonlySet<number>,
  isTransparent: (op: IrOp) => boolean,
): boolean {
  const startIndex = ops[targetIndex]?.kind === "label" ? targetIndex + 1 : targetIndex;
  for (let index = startIndex; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      if (labelEntries.has(index)) return false;
      continue;
    }
    if (hasRewriteBarrier(op)) return false;
    if (op.kind === "return") return true;
    if (!isTransparent(op)) return false;
  }
  return false;
}

function directFlowTarget(op: IrOp): string | number | undefined {
  switch (op.kind) {
    case "jump":
    case "cjump":
    case "call":
    case "loop":
      return op.target;
    case "orphan-address":
      return op.target;
    default:
      return undefined;
  }
}

function isIndirectFlowOp(op: IrOp): op is Extract<IrOp, { kind: "indirect-jump" | "indirect-call" | "indirect-cjump" }> {
  return op.kind === "indirect-jump" || op.kind === "indirect-call" || op.kind === "indirect-cjump";
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

function plainProducesOpaqueExpressionValue(
  op: Extract<IrOp, { kind: "plain" }>,
  producerIndex: number | undefined,
): X2ValueFact | undefined {
  if (producerIndex === undefined) return undefined;
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op) || hasIrRoles(op)) return undefined;
  if (!PURE_OPAQUE_EXPR_OPCODES.has(op.opcode)) return undefined;
  const info = getOpcode(op.opcode);
  if (
    info.risk !== "documented" ||
    info.x2Effect !== "preserves" ||
    info.stackEffect === "barrier" ||
    info.stackEffect === "unknown" ||
    info.stackEffect === "exposes" ||
    info.stackEffect === "shifts"
  ) {
    return undefined;
  }
  return expressionValueFact(producerIndex);
}

function plainProducesStableExpressionValues(
  op: Extract<IrOp, { kind: "plain" }>,
  x: X2ValueSet | undefined,
  y: X2ValueSet | undefined,
  xShape: X2ShapeSet | undefined = undefined,
  yShape: X2ShapeSet | undefined = undefined,
): Set<X2ValueFact> {
  const constant = plainProducesStableConstantExpressionValue(op);
  if (constant !== undefined) return new Set([constant]);
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op) || hasIrRoles(op)) return new Set();
  if (!PURE_OPAQUE_EXPR_OPCODES.has(op.opcode)) return new Set();
  const info = getOpcode(op.opcode);
  if (
    info.risk !== "documented" ||
    info.x2Effect !== "preserves" ||
    (
      info.stackEffect !== "preserves" &&
      info.stackEffect !== "consume-y-drop" &&
      info.stackEffect !== "consume-y-keep"
    )
  ) {
    return new Set();
  }
  const output = new Set<X2ValueFact>();
  const opcode = op.opcode.toString(16).toUpperCase().padStart(2, "0");
  if (info.stackEffect === "preserves") {
    for (const key of stableExpressionSourceKeys(x, xShape)) {
      output.add(stableExpressionValueFact(opcode, key));
    }
  } else if (info.stackEffect === "consume-y-drop" || info.stackEffect === "consume-y-keep") {
    for (const yKey of stableExpressionSourceKeys(y, yShape)) {
      for (const xKey of stableExpressionSourceKeys(x, xShape)) {
        output.add(stableBinaryExpressionValueFact(op, opcode, yKey, xKey));
      }
    }
  }
  return output;
}

function plainProducesStableConstantExpressionValue(
  op: Extract<IrOp, { kind: "plain" }>,
): X2ValueFact | undefined {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op) || hasIrRoles(op)) return undefined;
  if (!STABLE_CONSTANT_EXPR_OPCODES.has(op.opcode)) return undefined;
  const info = getOpcode(op.opcode);
  if (info.risk !== "documented" || info.x2Effect !== "preserves" || info.stackEffect !== "shifts") {
    return undefined;
  }
  const opcode = op.opcode.toString(16).toUpperCase().padStart(2, "0");
  return stableExpressionValueFact(opcode, "");
}

function stableBinaryExpressionValueFact(
  op: Extract<IrOp, { kind: "plain" }>,
  opcode: string,
  yKey: string,
  xKey: string,
): X2ValueFact {
  const operands = COMMUTATIVE_STABLE_EXPR_OPCODES.has(op.opcode) ? [yKey, xKey].sort() : [yKey, xKey];
  return stableExpressionValueFact(opcode, operands.join(","));
}

function plainXValueAfterNonPreservingOp(
  op: Extract<IrOp, { kind: "plain" }>,
  producerIndex: number | undefined,
  x: X2ValueSet | undefined = undefined,
  y: X2ValueSet | undefined = undefined,
  xShape: X2ShapeSet | undefined = undefined,
  yShape: X2ShapeSet | undefined = undefined,
): Set<X2ValueFact> {
  const output = plainProducesStableExpressionValues(op, x, y, xShape, yShape);
  const opaque = plainProducesOpaqueExpressionValue(op, producerIndex);
  if (opaque !== undefined) output.add(opaque);
  return output;
}

export function analyzeX2StackEffect(op: IrOp | undefined): X2StackEffectAnalysis {
  const defaultEffect: Pick<X2StackEffectAnalysis, "x2Effect" | "stackEffect"> = {
    x2Effect: "preserves",
    stackEffect: "preserves",
  };
  const raw = op === undefined || hasRewriteBarrier(op)
    ? { x2Effect: "unknown" as const, stackEffect: "unknown" as const }
    : "opcode" in op
      ? getOpcode(op.opcode)
      : defaultEffect;
  const stackConsumes = raw.stackEffect === "consume-y-drop" || raw.stackEffect === "consume-y-keep";
  const stackBarrier = raw.stackEffect === "barrier" || raw.stackEffect === "unknown";
  const hardX2OverwriteWithoutStackUse = op?.kind === "plain" &&
    raw.x2Effect === "affects" &&
    raw.stackEffect === "preserves" &&
    !plainPreservesXValue(op);
  return {
    x2Effect: raw.x2Effect,
    stackEffect: raw.stackEffect,
    stackShifts: raw.stackEffect === "shifts",
    stackPreserves: raw.stackEffect === "preserves",
    stackConsumes,
    stackExposes: raw.stackEffect === "exposes",
    stackBarrier,
    x2Affects: raw.x2Effect === "affects",
    x2Preserves: raw.x2Effect === "preserves",
    x2Restores: raw.x2Effect === "restores",
    hardX2OverwriteWithoutStackUse,
    stackLiftAndX2Sync: op !== undefined &&
      raw.stackEffect === "shifts" &&
      raw.x2Effect === "affects",
  };
}

export function x2NextStackShiftingProducerIndex(
  ops: readonly IrOp[],
  start: number,
  context: DirectReturnAnalysisContext,
): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (analyzeX2StackEffect(op).stackShifts) return index;
    if (isKnownReturnCallOp(op) && x2SimpleDirectReturnPreservesStack(ops, op, context)) continue;
    if (!x2IsFallthroughStackPreservingGapOp(op)) return undefined;
  }
  return undefined;
}

export function x2NextHardX2OverwriteIndex(
  ops: readonly IrOp[],
  start: number,
  context: DirectReturnAnalysisContext,
): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (analyzeX2StackEffect(op).hardX2OverwriteWithoutStackUse) return index;
    if (isKnownReturnCallOp(op) && x2SimpleDirectReturnPreservesStack(ops, op, context)) continue;
    if (!x2IsFallthroughStackPreservingGapOp(op)) return undefined;
  }
  return undefined;
}

function x2IsFallthroughStackPreservingGapOp(op: IrOp): boolean {
  return x2IsStackPreservingGapOp(op) ||
    op.kind === "cjump" ||
    op.kind === "loop" ||
    x2IsKnownIndirectFallthroughStackPreservingConditional(op);
}

function x2IsKnownIndirectFallthroughStackPreservingConditional(op: IrOp): boolean {
  return op.kind === "indirect-cjump" &&
    knownIndirectFlowTarget(op) !== undefined &&
    !hasRewriteBarrier(op) &&
    analyzeX2StackEffect(op).stackPreserves;
}

function x2IsStackPreservingGapOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  switch (op.kind) {
    case "label":
    case "store":
    case "indirect-store":
    case "orphan-address":
      return true;
    case "plain": {
      const effect = analyzeX2StackEffect(op);
      return effect.stackPreserves && !effect.x2Restores;
    }
    default:
      return false;
  }
}

function x2SimpleDirectReturnPreservesStack(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
): boolean {
  return knownReturnCallReturnsThroughTransparentRange(ops, call, context, x2IsStrictStackPreservingLinearOp);
}

function x2IsStrictStackPreservingLinearOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  switch (op.kind) {
    case "label":
    case "store":
    case "indirect-store":
    case "orphan-address":
      return true;
    case "plain": {
      const effect = analyzeX2StackEffect(op);
      return effect.stackPreserves && !effect.x2Restores;
    }
    default:
      return false;
  }
}

export function labelIndexes(ops: readonly IrOp[]): Map<string, number> {
  const result = new Map<string, number>();
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label") result.set(op.name, i);
  }
  return result;
}

export function addressIndexes(ops: readonly IrOp[]): Map<number, number> {
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
        const transferred = transferX2ValueDataflowState(input, ops[index]!, edge.kind, trackRegisterMemory, index);
        const output = x2ValueEdgeDropsUnstableOpaqueExpressionFacts(ops[index]!, edge, index)
          ? dropUnstableOpaqueExpressionX2ValueFacts(transferred, trackRegisterMemory)
          : transferred;
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

export function x2ValueSetHasConcreteDecimal(input: X2ValueSet | undefined): boolean {
  if (input === undefined) return false;
  for (const fact of input) {
    if (isConcreteDecimalX2ValueFact(fact)) return true;
  }
  return false;
}

export function x2ValueFactIsNormalizedDecimal(fact: X2ValueFact): boolean {
  return normalizedDecimalValueFromFact(fact) !== undefined;
}

export function x2ValueFactRestoredVisibleDecimal(fact: X2ValueFact): string | undefined {
  const decimal = /^decimal:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):(normalized|unnormalized)$/u.exec(fact);
  return decimal === null ? undefined : normalizePlainDecimal(decimal[1]!);
}

export function x2ValueSetHasRestoredVisibleDecimal(
  input: X2ValueSet | undefined,
  fact: X2ValueFact,
): boolean {
  const visible = x2ValueFactRestoredVisibleDecimal(fact);
  if (visible === undefined) return false;
  for (const candidate of input ?? []) {
    if (x2ValueFactRestoredVisibleDecimal(candidate) === visible) return true;
  }
  return false;
}

export function x2ValueSetsHaveSameRestoredVisibleDecimal(
  left: X2ValueSet | undefined,
  right: X2ValueSet | undefined,
): boolean {
  if (left === undefined || right === undefined) return false;
  const leftValues = new Set<string>();
  for (const fact of left) {
    const visible = x2ValueFactRestoredVisibleDecimal(fact);
    if (visible !== undefined) leftValues.add(visible);
  }
  for (const fact of right) {
    const visible = x2ValueFactRestoredVisibleDecimal(fact);
    if (visible !== undefined && leftValues.has(visible)) return true;
  }
  return false;
}

export function x2ValueSetHasNormalizedDecimalFact(
  input: X2ValueSet | undefined,
  fact: X2ValueFact,
): boolean {
  return x2ValueFactIsNormalizedDecimal(fact) && input?.has(fact) === true;
}

export function x2StateHasSameNormalizedDecimalInXAndX2(
  state: X2ValueDataflowState | undefined,
): boolean {
  if (state === undefined) return false;
  for (const fact of state.x) {
    if (x2ValueSetHasNormalizedDecimalFact(state.x2, fact)) return true;
  }
  return false;
}

export function x2NormalizedDecimalRestoreGapIsFreeStanding(
  ops: readonly IrOp[],
  index: number,
  context?: DirectReturnAnalysisContext,
): boolean {
  for (let cursor = index - 1; cursor >= 0; cursor -= 1) {
    const op = ops[cursor]!;
    if (op.kind === "label") continue;
    if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;

    switch (op.kind) {
      case "plain": {
        const effect = plainX2Effect(op);
        if (effect === "affects" || effect === "restores") return true;
        if (effect === "preserves") continue;
        return false;
      }
      case "recall":
      case "indirect-recall":
        return true;
      case "store":
      case "indirect-store":
      case "orphan-address":
      case "cjump":
      case "loop":
        continue;
      case "indirect-cjump":
        if (knownIndirectFlowTarget(op) !== undefined) continue;
        return false;
      case "call":
      case "indirect-call":
        if (
          context !== undefined &&
          isKnownReturnCallOp(op) &&
          x2RestoreGapDirectReturnDoesNotObserveRestore(ops, op, context)
        ) continue;
        return false;
      default:
        return false;
    }
  }
  return false;
}

export function parseX2ShapeFact(fact: X2ShapeFact): ParsedX2ShapeFact {
  const mantissa = /^mantissa:(.*):decimal$/u.exec(fact);
  if (mantissa !== null) {
    const raw = mantissa[1]!;
    const normalized = normalizePlainDecimal(raw);
    return {
      kind: "decimal-mantissa",
      raw,
      normalized,
      safety: normalized === undefined ? "unknown" : "dotSafeDecimal",
    };
  }
  const exponent = /^exponent:([^:]*):([^:]*):decimal$/u.exec(fact);
  if (exponent !== null) {
    const mantissaRaw = exponent[1]!;
    const exponentRaw = exponent[2]!;
    return {
      kind: "decimal-exponent",
      mantissa: mantissaRaw,
      exponent: exponentRaw,
      normalized: normalizedExponentEntryValue(mantissaRaw, exponentRaw),
      safety: "errorProne",
    };
  }
  const hex = /^hex:(.*):mantissa$/u.exec(fact);
  if (hex !== null) {
    return { kind: "hex-mantissa", raw: hex[1]!, safety: "structuralOnly" };
  }
  const hexExponent = /^hex-exponent:([^:]*):([^:]*)$/u.exec(fact);
  if (hexExponent !== null) {
    return {
      kind: "hex-exponent",
      mantissa: hexExponent[1]!,
      exponent: hexExponent[2]!,
      safety: "structuralOnly",
    };
  }
  const superMantissa = /^super:(.*)$/u.exec(fact);
  if (superMantissa !== null) {
    return { kind: "super-mantissa", raw: superMantissa[1]!, safety: "structuralOnly" };
  }
  const superExponent = /^super-exponent:([^:]*):([^:]*)$/u.exec(fact);
  if (superExponent !== null) {
    return {
      kind: "super-exponent",
      mantissa: superExponent[1]!,
      exponent: superExponent[2]!,
      safety: "structuralOnly",
    };
  }
  return { kind: "unknown", raw: fact, safety: "unknown" };
}

export function x2ShapeDataModelForFact(fact: X2ShapeFact): X2ShapeDataModel {
  const mantissa = /^mantissa:(.*):decimal$/u.exec(fact);
  if (mantissa !== null) return decimalMantissaDataModel(mantissa[1]!);
  const exponent = /^exponent:([^:]*):([^:]*):decimal$/u.exec(fact);
  if (exponent !== null) {
    const mantissaModel = decimalMantissaDataModel(exponent[1]!);
    const exponentRaw = exponent[2]!;
    return {
      kind: "exponent-entry",
      mantissa: mantissaModel,
      exponentRaw,
      exponentSign: exponentRaw.startsWith("-") ? "-" : "",
      exponentDigits: shapeDigits(exponentRaw),
      normalizedDecimal: normalizedExponentEntryValue(exponent[1]!, exponentRaw),
      safety: "errorProne",
    };
  }
  const hex = /^hex:(.*):mantissa$/u.exec(fact);
  if (hex !== null) return structuralMantissaDataModel("hex", hex[1]!, "structuralOnly");
  const hexExponent = /^hex-exponent:([^:]*):([^:]*)$/u.exec(fact);
  if (hexExponent !== null) {
    const mantissa = structuralMantissaDataModel("hex", hexExponent[1]!, "structuralOnly");
    return {
      kind: "exponent-entry",
      mantissa,
      exponentRaw: hexExponent[2]!,
      exponentSign: hexExponent[2]!.startsWith("-") ? "-" : "",
      exponentDigits: shapeDigits(hexExponent[2]!),
      closedStructuralMantissa: closedStructuralExponentMantissaModel(mantissa, hexExponent[2]!),
      safety: "structuralOnly",
    };
  }
  const superMantissa = /^super:(.*)$/u.exec(fact);
  if (superMantissa !== null) return structuralMantissaDataModel("super", superMantissa[1]!, "structuralOnly");
  const superExponent = /^super-exponent:([^:]*):([^:]*)$/u.exec(fact);
  if (superExponent !== null) {
    const mantissa = structuralMantissaDataModel("super", superExponent[1]!, "structuralOnly");
    return {
      kind: "exponent-entry",
      mantissa,
      exponentRaw: superExponent[2]!,
      exponentSign: superExponent[2]!.startsWith("-") ? "-" : "",
      exponentDigits: shapeDigits(superExponent[2]!),
      closedStructuralMantissa: closedStructuralExponentMantissaModel(mantissa, superExponent[2]!),
      safety: "structuralOnly",
    };
  }
  return { kind: "unknown", raw: fact, safety: "unknown" };
}

export function x2ShapeDataModels(input: X2ShapeSet | undefined): X2ShapeDataModel[] {
  const models: X2ShapeDataModel[] = [];
  for (const fact of input ?? []) models.push(x2ShapeDataModelForFact(fact));
  return models;
}

export function x2MantissaShapeFactFromModel(model: X2MantissaDataModel): X2ShapeFact | undefined {
  return x2MantissaShapeFactFromParts(model.radix, model.canonical);
}

export function x2ShapeFactFromDataModel(model: X2ShapeDataModel): X2ShapeFact | undefined {
  if (model.kind === "mantissa") return x2MantissaShapeFactFromModel(model);
  if (model.kind !== "exponent-entry") return undefined;
  const mantissa = x2MantissaShapeFactFromModel(model.mantissa);
  return mantissa === undefined ? undefined : x2ExponentShapeFactFromMantissaFact(mantissa, model.exponentRaw);
}

export function x2CanonicalShapeFact(fact: X2ShapeFact): X2ShapeFact {
  return x2ShapeFactFromDataModel(x2ShapeDataModelForFact(fact)) ?? fact;
}

export function x2ExponentShapeFactFromMantissaFact(
  fact: X2ShapeFact,
  exponentRaw: string,
): X2ShapeFact | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "mantissa") return undefined;
  const exponent = canonicalExponentShapeRaw(exponentRaw);
  if (exponent === undefined) return undefined;
  if (model.radix === "decimal") return decimalExponentShapeFact(model.canonical, exponent);
  if (model.radix === "hex") return `hex-exponent:${model.canonical}:${exponent}`;
  if (model.radix === "super") return `super-exponent:${model.canonical}:${exponent}`;
  return undefined;
}

export function x2MantissaSignChangedShapeFact(fact: X2ShapeFact): X2ShapeFact | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "mantissa") return undefined;
  if (model.radix === "decimal") {
    const signed = signChangedMantissaShape(model.canonical);
    return signed === undefined ? undefined : decimalMantissaShapeFact(signed);
  }
  if (model.radix === "hex" || model.radix === "super") {
    return x2MantissaShapeFactFromParts(model.radix, toggleRawSign(model.canonical));
  }
  return undefined;
}

export function x2ExponentSignChangedShapeFact(fact: X2ShapeFact): X2ShapeFact | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "exponent-entry") return undefined;
  return x2ExponentShapeFactFromMantissaFact(
    x2MantissaShapeFactFromModel(model.mantissa) ?? fact,
    toggleExponentSign(model.exponentRaw),
  );
}

export function x2ExponentMantissaSignChangedShapeFact(fact: X2ShapeFact): X2ShapeFact | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "exponent-entry") return undefined;
  const mantissa = x2MantissaShapeFactFromModel(model.mantissa);
  if (mantissa === undefined) return undefined;
  const signedMantissa = x2MantissaSignChangedShapeFact(mantissa);
  return signedMantissa === undefined
    ? undefined
    : x2ExponentShapeFactFromMantissaFact(signedMantissa, model.exponentRaw);
}

export function x2ClosedStructuralExponentMantissaShapeFact(fact: X2ShapeFact): X2ShapeFact | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "exponent-entry") return undefined;
  const mantissa = x2MantissaShapeFactFromModel(model.mantissa);
  return mantissa === undefined ? undefined : x2StructuralMantissaShiftShapeFact(mantissa, model.exponentRaw);
}

export function x2StructuralMantissaShiftShapeFact(
  fact: X2ShapeFact,
  exponentRaw: string,
): X2ShapeFact | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "mantissa" || (model.radix !== "hex" && model.radix !== "super")) return undefined;
  const shifted = shiftedStructuralMantissaRaw(model.canonical, exponentRaw);
  if (shifted === undefined) return undefined;
  const radix = model.radix === "super" && shifted !== model.canonical ? "hex" : model.radix;
  return x2MantissaShapeFactFromModel(structuralMantissaDataModel(radix, shifted, "structuralOnly"));
}

export function x2StructuralMantissaAppendDigitsShapeFact(
  fact: X2ShapeFact,
  suffixRaw: string,
): X2ShapeFact | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "mantissa" || (model.radix !== "hex" && model.radix !== "super")) return undefined;
  const suffix = canonicalStructuralDigitRun(suffixRaw);
  if (suffix === undefined) return undefined;
  const sign = model.sign;
  const unsigned = sign === "" ? model.canonical : model.canonical.slice(1);
  const appended = `${sign}${unsigned}${suffix}`;
  if (shapeDigits(appended).length > 8) return undefined;
  const radix = model.radix === "super" && suffix.length > 0 ? "hex" : model.radix;
  return x2MantissaShapeFactFromModel(structuralMantissaDataModel(radix, appended, "structuralOnly"));
}

export function x2StructuralMantissaConcatShapeFacts(
  left: X2ShapeFact,
  right: X2ShapeFact,
): X2ShapeFact | undefined {
  const rightModel = x2ShapeDataModelForFact(right);
  if (
    rightModel.kind !== "mantissa" ||
    (rightModel.radix !== "hex" && rightModel.radix !== "super") ||
    rightModel.sign !== "" ||
    rightModel.hasDecimalPoint
  ) {
    return undefined;
  }
  return x2StructuralMantissaAppendDigitsShapeFact(left, rightModel.digits.join(""));
}

export function x2ShapeFactSafety(fact: X2ShapeFact): X2ShapeSafety {
  return x2ShapeDataModelForFact(fact).safety;
}

export function x2ShapeSetSafety(input: X2ShapeSet | undefined): X2ShapeSafety {
  if (input === undefined || input.size === 0) return "unknown";
  let sawDotSafe = false;
  let sawStructural = false;
  let sawUnknown = false;
  for (const fact of input) {
    const safety = x2ShapeFactSafety(fact);
    if (safety === "errorProne") return "errorProne";
    if (safety === "structuralOnly") sawStructural = true;
    else if (safety === "dotSafeDecimal") sawDotSafe = true;
    else sawUnknown = true;
  }
  if (sawStructural) return "structuralOnly";
  if (sawUnknown) return "unknown";
  return sawDotSafe ? "dotSafeDecimal" : "unknown";
}

export function x2RestoreSafety(state: X2ValueDataflowState | undefined): X2ShapeSafety {
  if (state === undefined) return "unknown";
  if (x2ValueSetHasConcreteDecimal(state.x2)) return "dotSafeDecimal";
  return x2ShapeSetSafety(state.x2Shape);
}

export function x2StateHasDotSafeDecimalX2(state: X2ValueDataflowState | undefined): boolean {
  return x2RestoreSafety(state) === "dotSafeDecimal";
}

export function x2StateHasStructuralShapeX2(state: X2ValueDataflowState | undefined): boolean {
  return x2RestoreSafety(state) === "structuralOnly";
}

export function x2StateHasUnsafeDotRestoreShapeX2(state: X2ValueDataflowState | undefined): boolean {
  const safety = x2RestoreSafety(state);
  return safety === "structuralOnly" || safety === "errorProne";
}

export function x2ShapeSetsHaveSameDotSafeDecimal(
  left: X2ShapeSet | undefined,
  right: X2ShapeSet | undefined,
): boolean {
  if (x2ShapeSetSafety(left) !== "dotSafeDecimal" || x2ShapeSetSafety(right) !== "dotSafeDecimal") return false;
  const leftValues = dotSafeDecimalShapeValues(left);
  const rightValues = dotSafeDecimalShapeValues(right);
  for (const value of leftValues) {
    if (rightValues.has(value)) return true;
  }
  return false;
}

export function x2ShapeSetsHaveSameStructuralShape(
  left: X2ShapeSet | undefined,
  right: X2ShapeSet | undefined,
): boolean {
  if (left === undefined || right === undefined) return false;
  const leftShapes = structuralRestoreShapeFacts(left);
  const rightShapes = structuralRestoreShapeFacts(right);
  for (const shape of leftShapes) {
    if (rightShapes.has(shape)) return true;
  }
  return false;
}

export function x2SignChangedSharedStructuralShapeFacts(
  xShapes: X2ShapeSet | undefined,
  x2Shapes: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  const visibleRestoreShapes = structuralRestoreShapeFacts(canonicalStructuralShapeFacts(xShapes));
  for (const fact of canonicalStructuralShapeFacts(x2Shapes)) {
    const model = x2ShapeDataModelForFact(fact);
    if (!structuralShapeSetHasIntersection(visibleRestoreShapes, structuralRestoreShapeFacts(new Set([fact])))) {
      continue;
    }
    if (model.kind === "mantissa" && (model.radix === "hex" || model.radix === "super")) {
      output.add(signChangedStructuralMantissaShapeFact(fact));
      continue;
    }
    if (
      model.kind === "exponent-entry" &&
      (model.mantissa.radix === "hex" || model.mantissa.radix === "super") &&
      model.safety === "structuralOnly"
    ) {
      const signed = x2ExponentMantissaSignChangedShapeFact(fact);
      if (signed !== undefined) output.add(signed);
    }
  }
  return output;
}

function structuralShapeSetHasIntersection(left: X2ShapeSet, right: X2ShapeSet): boolean {
  for (const shape of left) {
    if (right.has(shape)) return true;
  }
  return false;
}

export function x2StateHasSameDotSafeDecimalInXAndX2(state: X2ValueDataflowState | undefined): boolean {
  return state !== undefined && x2ShapeSetsHaveSameDotSafeDecimal(state.xShape, state.x2Shape);
}

export function x2StateHasSameStructuralShapeInXAndX2(state: X2ValueDataflowState | undefined): boolean {
  return state !== undefined && x2ShapeSetsHaveSameStructuralShape(state.xShape, state.x2Shape);
}

export function x2StateIsClosedPlainContext(state: X2ValueDataflowState | undefined): boolean {
  return state?.entry.kind === "closed" &&
    (state.vpContext === undefined || state.vpContext.kind === "none") &&
    (state.structuralVpContext === undefined || state.structuralVpContext.kind === "none");
}

export function x2StateHasSameDotRestoreValueInXAndX2(state: X2ValueDataflowState | undefined): boolean {
  return x2ValueSetHasIntersection(state?.x, state?.x2) || x2StateHasSameDotSafeDecimalInXAndX2(state);
}

export function x2StateHasSameRestoredVisibleDecimalInXAndX2(
  state: X2ValueDataflowState | undefined,
): boolean {
  return x2ValueSetsHaveSameRestoredVisibleDecimal(state?.x, state?.x2);
}

export function x2CanUseDotRestoreAt(
  ops: readonly IrOp[],
  index: number,
  state: X2ValueDataflowState | undefined,
  dotSafe: boolean,
  immediateSync: boolean,
  context?: DirectReturnAnalysisContext,
): boolean {
  return dotSafe || immediateSync || x2CanUseClosedSignChangeDotSourceAt(ops, index, state, context);
}

export function x2CanUseClosedSignChangeDotSourceAt(
  ops: readonly IrOp[],
  index: number,
  state: X2ValueDataflowState | undefined,
  context?: DirectReturnAnalysisContext,
): boolean {
  for (let cursor = index - 1; cursor >= 0; cursor -= 1) {
    const op = ops[cursor]!;
    if (op.kind === "label") continue;
    if (isFreeStandingX2EmptyOp(op)) continue;
    if (
      context !== undefined &&
      isKnownReturnCallOp(op) &&
      x2RestoreGapDirectReturnDoesNotObserveRestore(ops, op, context)
    ) continue;
    if (hasRewriteBarrier(op)) return false;
    return isFreeStandingX2SignChange(op) &&
      x2StateIsClosedPlainContext(state) &&
      x2StateHasSameDotRestoreValueInXAndX2(state);
  }
  return false;
}

function isFreeStandingX2SignChange(op: IrOp): op is Extract<IrOp, { kind: "plain" }> {
  return op.kind === "plain" &&
    op.opcode === X2_SIGN_CHANGE_OPCODE &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasIrRoles(op);
}

function isFreeStandingX2EmptyOp(op: IrOp): boolean {
  return op.kind === "plain" &&
    op.opcode >= X2_EMPTY_OPCODE_START &&
    op.opcode <= X2_EMPTY_OPCODE_END &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasIrRoles(op);
}

export function x2StateHasDecimalVpEntrySource(state: X2ValueDataflowState | undefined): boolean {
  return state?.vpEntryMantissa !== undefined && state.vpEntryMantissa.size > 0;
}

export function x2StateHasStructuralVpEntrySource(state: X2ValueDataflowState | undefined): boolean {
  return state?.vpEntryShape !== undefined && state.vpEntryShape.size > 0;
}

export function x2StateHasVpEntrySource(state: X2ValueDataflowState | undefined): boolean {
  return x2StateHasDecimalVpEntrySource(state) || x2StateHasStructuralVpEntrySource(state);
}

export function x2StatesHaveSameVpEntrySource(
  left: X2ValueDataflowState | undefined,
  right: X2ValueDataflowState | undefined,
): boolean {
  return sameNonEmptyStringSet(left?.vpEntryMantissa, right?.vpEntryMantissa) ||
    sameNonEmptyShapeSet(left?.vpEntryShape, right?.vpEntryShape) ||
    x2ShapeSetsHaveSameStructuralShape(left?.vpEntryShape, right?.vpEntryShape);
}

export function x2StateCanDiscardRestoreRunBeforeProvedVp(
  beforeRun: X2ValueDataflowState | undefined,
  beforeVp: X2ValueDataflowState | undefined,
): boolean {
  const context = analyzeX2VpShapeContext(beforeRun);
  if (context.kind === "active-mantissa") {
    return sameNonEmptyStringSet(context.mantissa, beforeVp?.vpEntryMantissa);
  }
  return x2StatesHaveSameVpEntrySource(beforeRun, beforeVp);
}

export function x2HasOnlyRestoreGapBeforeVp(
  ops: readonly IrOp[],
  start: number,
  context?: DirectReturnAnalysisContext,
): boolean {
  let sawRestoreGap = false;
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    if (isFreeStandingX2RestoreGapOp(op)) {
      sawRestoreGap = true;
      continue;
    }
    if (
      context !== undefined &&
      isKnownReturnCallOp(op) &&
      x2RestoreGapDirectReturnDoesNotObserveRestore(ops, op, context)
    ) continue;
    return sawRestoreGap && isFreeStandingX2VpOp(op);
  }
  return false;
}

function x2RestoreGapDirectReturnDoesNotObserveRestore(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
): boolean {
  return knownReturnCallReturnsThroughTransparentRange(ops, call, context, isLinearX2RestoreGapTransparentOp);
}

function isLinearX2RestoreGapTransparentOp(op: IrOp): boolean {
  return op.kind === "orphan-address" ||
    (
      op.kind === "plain" &&
      !hasRewriteBarrier(op) &&
      !isDisplayFocusSensitive(op) &&
      !hasIrRoles(op) &&
      op.opcode >= X2_EMPTY_OPCODE_START &&
      op.opcode <= X2_EMPTY_OPCODE_END
    );
}

function isFreeStandingX2RestoreGapOp(op: IrOp): boolean {
  return op.kind === "plain" &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasIrRoles(op) &&
    (op.opcode === X2_SIGN_CHANGE_OPCODE || (op.opcode >= X2_EMPTY_OPCODE_START && op.opcode <= X2_EMPTY_OPCODE_END));
}

function isFreeStandingX2VpOp(op: IrOp): boolean {
  return op.kind === "plain" &&
    op.opcode === 0x0c &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasIrRoles(op);
}

export function analyzeX2VpShapeContext(
  state: X2ValueDataflowState | undefined,
): X2VpShapeContextAnalysis {
  if (state === undefined) {
    return emptyX2VpShapeContextAnalysis("unknown");
  }
  if (state.entry.kind === "open") {
    return {
      kind: "active-mantissa",
      phase: "active-entry",
      source: "decimal",
      mantissa: state.entry.raw,
      hasExponentDigit: false,
      restoresX2: false,
      canDiscardSeparatorBeforeNonDigit: false,
      canDiscardSeparatorBeforeSignChange: false,
      canDiscardRestoreBeforeFreshDigit: false,
      canCancelExponentSignPair: false,
    };
  }
  if (state.entry.kind === "exponent") {
    const hasExponentDigit = x2ExponentSetHasDigit(state.entry.exponent);
    return {
      kind: "active-exponent",
      phase: "active-entry",
      source: "decimal",
      mantissa: state.entry.mantissa,
      exponent: state.entry.exponent,
      hasExponentDigit,
      restoresX2: true,
      canDiscardSeparatorBeforeNonDigit: hasExponentDigit,
      canDiscardSeparatorBeforeSignChange: false,
      canDiscardRestoreBeforeFreshDigit: false,
      canCancelExponentSignPair: true,
    };
  }
  if (state.structuralEntry?.kind === "exponent") {
    const hasExponentDigit = x2ExponentSetHasDigit(state.structuralEntry.exponent);
    return {
      kind: "active-structural-exponent",
      phase: "active-entry",
      source: "structural",
      shape: state.structuralEntry.mantissa,
      exponent: state.structuralEntry.exponent,
      hasExponentDigit,
      restoresX2: true,
      canDiscardSeparatorBeforeNonDigit: hasExponentDigit,
      canDiscardSeparatorBeforeSignChange: false,
      canDiscardRestoreBeforeFreshDigit: false,
      canCancelExponentSignPair: true,
    };
  }
  if (state.entry.kind === "closed" && state.vpContext?.kind === "exponent") {
    const hasExponentDigit = x2ExponentSetHasDigit(state.vpContext.exponent);
    return {
      kind: "vp-exponent-context",
      phase: "vp-context",
      source: "decimal",
      mantissa: state.vpContext.mantissa,
      exponent: state.vpContext.exponent,
      hasExponentDigit,
      restoresX2: true,
      canDiscardSeparatorBeforeNonDigit: false,
      canDiscardSeparatorBeforeSignChange: hasExponentDigit,
      canDiscardRestoreBeforeFreshDigit: true,
      canCancelExponentSignPair: false,
    };
  }
  if (state.entry.kind === "closed" && state.structuralVpContext?.kind === "exponent") {
    const hasExponentDigit = x2ExponentSetHasDigit(state.structuralVpContext.exponent);
    return {
      kind: "vp-structural-exponent-context",
      phase: "vp-context",
      source: "structural",
      shape: state.structuralVpContext.mantissa,
      exponent: state.structuralVpContext.exponent,
      hasExponentDigit,
      restoresX2: true,
      canDiscardSeparatorBeforeNonDigit: false,
      canDiscardSeparatorBeforeSignChange: hasExponentDigit,
      canDiscardRestoreBeforeFreshDigit: true,
      canCancelExponentSignPair: false,
    };
  }
  if (
    state.entry.kind === "closed" &&
    (state.vpContext === undefined || state.vpContext.kind === "none") &&
    (state.structuralVpContext === undefined || state.structuralVpContext.kind === "none")
  ) {
    return emptyX2VpShapeContextAnalysis("none");
  }
  return emptyX2VpShapeContextAnalysis("unknown");
}

function emptyX2VpShapeContextAnalysis(
  kind: "none" | "unknown",
): X2VpShapeContextAnalysis {
  return {
    kind,
    phase: kind,
    source: kind,
    hasExponentDigit: false,
    restoresX2: false,
    canDiscardSeparatorBeforeNonDigit: false,
    canDiscardSeparatorBeforeSignChange: false,
    canDiscardRestoreBeforeFreshDigit: false,
    canCancelExponentSignPair: false,
  };
}

export function x2ExponentSetHasDigit(exponents: ReadonlySet<string> | undefined): boolean {
  if (exponents === undefined) return false;
  for (const exponent of exponents) {
    const digits = exponent.startsWith("-") ? exponent.slice(1) : exponent;
    if (digits.length === 0) return false;
  }
  return exponents.size > 0;
}

export function x2StateHasX2RestoreContext(state: X2ValueDataflowState | undefined): boolean {
  return analyzeX2VpShapeContext(state).restoresX2;
}

export function sameX2ExponentShapeContext(
  left: X2VpShapeContextAnalysis,
  right: X2VpShapeContextAnalysis,
): boolean {
  return left.kind !== "none" &&
    left.kind !== "unknown" &&
    left.kind === right.kind &&
    (
      sameNonEmptyStringSet(left.mantissa, right.mantissa) ||
      sameNonEmptyShapeSet(left.shape, right.shape)
    ) &&
    sameNonEmptyStringSet(left.exponent, right.exponent);
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
  return recallAlreadySyncedInX2MemoryValue(op, state, isConcreteDecimalX2ValueFact);
}

export function recallAlreadySyncedInX2MemoryValue(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
  predicate: (fact: X2ValueFact) => boolean = isStorableX2MemoryValueFact,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  for (const fact of state.memory?.[register] ?? []) {
    if (predicate(fact) && state.x2.has(fact)) return register;
  }
  return undefined;
}

export function recallAlreadySyncedInX2PreloadedDecimal(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  for (const fact of preloadedConstantValueFacts(op)) {
    if (state.x2.has(fact)) return register;
  }
  return undefined;
}

export function recallAlreadySyncedInX2StructuralShape(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  const shapes = recallStructuralShapeFacts(op, state, register);
  return x2ShapeSetsHaveSameStructuralShape(state.x2Shape, shapes) ? register : undefined;
}

export function recallAlreadyInXDecimalMemory(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RegisterName | undefined {
  return recallAlreadyInXMemoryValue(op, state, isConcreteDecimalX2ValueFact);
}

export function recallAlreadyInXMemoryValue(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
  predicate: (fact: X2ValueFact) => boolean = isStorableX2MemoryValueFact,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  for (const fact of state.memory?.[register] ?? []) {
    if (predicate(fact) && state.x.has(fact)) return register;
  }
  return undefined;
}

export function recallAlreadyInXPreloadedDecimal(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  for (const fact of preloadedConstantValueFacts(op)) {
    if (state.x.has(fact)) return register;
  }
  return undefined;
}

export function recallAlreadyInXStructuralShape(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  const shapes = recallStructuralShapeFacts(op, state, register);
  return x2ShapeSetsHaveSameStructuralShape(state.xShape, shapes) ? register : undefined;
}

export function recallValueProof(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RecallValueProof | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  const inX =
    x2ValueSetHasRegister(state.x, register) ||
    recallAlreadyInXMemoryValue(op, state) === register ||
    recallAlreadyInXPreloadedDecimal(op, state) === register ||
    recallAlreadyInXStructuralShape(op, state) === register;
  const x2SyncRegister = recallAlreadySyncedInX2Value(op, state);
  const x2SyncValue =
    (recallAlreadySyncedInX2MemoryValue(op, state) ??
      recallAlreadySyncedInX2PreloadedDecimal(op, state)) !== undefined;
  const x2SyncShape = recallAlreadySyncedInX2StructuralShape(op, state) === register ? true : undefined;
  return {
    register,
    inX,
    x2SyncRegister,
    x2SyncValue,
    ...(x2SyncShape === true ? { x2SyncShape } : {}),
  };
}

export function analyzeRecallRemoval(
  ops: readonly IrOp[],
  recallIndex: number,
  x2RegisterState: RegisterValueSet | undefined,
  x2ValueState: X2ValueDataflowState | undefined,
): RecallRemovalAnalysis | undefined {
  const op = ops[recallIndex];
  if (op === undefined) return undefined;
  const register = removableRecallValueRegister(op);
  if (register === undefined) return undefined;
  const valueProof = recallValueProof(op, x2ValueState);
  const redundantSyncRegister = recallAlreadySyncedInX2(op, x2RegisterState) ?? valueProof?.x2SyncRegister;
  const redundantSyncValue = valueProof?.x2SyncValue === true;
  const redundantSyncShape = valueProof?.x2SyncShape === true;
  const exposesStackLift = removingRecallCanExposeStackLift(ops, recallIndex);
  const exposesX2Restore =
    removingRecallCanExposeX2Restore(ops, recallIndex, {
      redundantSyncRegister,
      redundantSyncValue,
      redundantSyncShape,
    }) &&
    !recallRemovalPreservesImmediateVpRestoreContext(ops, recallIndex, x2ValueState, valueProof);
  return {
    register,
    valueProof,
    redundantSyncRegister,
    redundantSyncValue,
    redundantSyncShape,
    x2SyncRedundant: redundantSyncRegister !== undefined || redundantSyncValue || redundantSyncShape,
    exposesStackLift,
    exposesX2Restore,
    removable: !exposesStackLift && !exposesX2Restore,
  };
}

function recallRemovalPreservesImmediateVpRestoreContext(
  ops: readonly IrOp[],
  recallIndex: number,
  state: X2ValueDataflowState | undefined,
  valueProof: RecallValueProof | undefined,
): boolean {
  if (
    state === undefined ||
    valueProof === undefined ||
    (valueProof.x2SyncShape !== true && valueProof.x2SyncValue !== true)
  ) return false;
  const op = ops[recallIndex];
  if (op === undefined) return false;
  const nextRestore = nextImmediateX2RestoreOp(ops, recallIndex + 1);
  if (nextRestore?.kind !== "plain" || nextRestore.opcode !== 0x0c) return false;
  const recalledValues = recallX2ValueFacts(state, valueProof.register, true, op);
  const recalledMantissas = vpEntryMantissasFromValueFacts(recalledValues);
  const vpContext = analyzeX2VpShapeContext(state);
  if (
    vpContext.kind === "active-mantissa" &&
    sameNonEmptyStringSet(vpContext.mantissa, recalledMantissas)
  ) return true;
  if (sameNonEmptyStringSet(state.vpEntryMantissa, recalledMantissas)) return true;
  const recalledShapes = recallStructuralShapeFacts(op, state, valueProof.register);
  return x2ShapeSetsHaveSameStructuralShape(state.vpEntryShape, recalledShapes);
}

function nextImmediateX2RestoreOp(ops: readonly IrOp[], start: number): IrOp | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    return op;
  }
  return undefined;
}

function emptyRegisterDataflowState(): RegisterDataflowState {
  return { x: new Set(), x2: new Set() };
}

function emptyX2ValueDataflowState(trackRegisterMemory = false): X2ValueDataflowState {
  return {
    x: new Set(),
    y: new Set(),
    x2: new Set(),
    xShape: new Set(),
    yShape: new Set(),
    x2Shape: new Set(),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: noneX2StructuralEntryState(),
    memory: trackRegisterMemory ? {} : undefined,
    shapeMemory: trackRegisterMemory ? {} : undefined,
  };
}

function cloneRegisterDataflowState(input: RegisterDataflowState): RegisterDataflowState {
  return { x: new Set(input.x), x2: new Set(input.x2) };
}

function cloneX2ValueDataflowState(input: X2ValueDataflowState): X2ValueDataflowState {
  return {
    x: new Set(input.x),
    y: cloneOptionalValueSet(input.y),
    x2: new Set(input.x2),
    xShape: cloneOptionalShapeSet(input.xShape),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: cloneX2EntryState(input.entry),
    vpContext: cloneX2VpContextState(input.vpContext),
    structuralEntry: cloneX2StructuralEntryState(input.structuralEntry),
    structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
    vpEntryMantissa: cloneOptionalStringSet(input.vpEntryMantissa),
    vpEntryShape: cloneOptionalShapeSet(input.vpEntryShape),
    memory: input.memory,
    shapeMemory: input.shapeMemory,
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
      const counter = loopCounterRegister(op.counter);
      const x = removeRegisterValue(input.x, counter);
      const x2 = removeRegisterValue(input.x2, counter);
      return {
        x,
        x2: effect === "preserves"
          ? x2
          : effect === "affects"
            ? new Set(x)
            : new Set(),
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
  producerIndex: number | undefined,
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
      const stable = registerWritePreservesStoredValue(closed, op.register)
        ? closed
        : invalidateRegisterDependentX2ValueState(closed, op.register, trackRegisterMemory);
      return {
        x: addX2Value(stable.x, registerValueFact(op.register)),
        y: cloneOptionalValueSet(stable.y),
        x2: addStoredX2ValueAlias(stable, registerValueFact(op.register)),
        xShape: cloneOptionalShapeSet(stable.xShape),
        yShape: cloneOptionalShapeSet(stable.yShape),
        x2Shape: cloneOptionalShapeSet(stable.x2Shape),
        entry: closedX2EntryState(),
        vpContext: cloneX2VpContextState(stable.vpContext),
        structuralEntry: noneX2StructuralEntryState(),
        structuralVpContext: cloneX2StructuralEntryState(stable.structuralVpContext),
        memory: trackRegisterMemory ? storeX2ValueMemory(stable.memory, op.register, stable.x) : undefined,
        shapeMemory: trackRegisterMemory ? storeX2ShapeMemory(stable.shapeMemory, op.register, stable.xShape) : undefined,
      };
    }
    case "indirect-store":
      return transferIndirectStoreX2ValueState(input, op, trackRegisterMemory);
    case "recall": {
      const closed = closeX2ValueEntry(input);
      const value = recallX2ValueFacts(input, op.register, trackRegisterMemory, op);
      const shape = recallX2ShapeFacts(value, op, trackRegisterMemory ? input.shapeMemory?.[op.register] : undefined);
      return {
        x: new Set(value),
        y: new Set(closed.x),
        x2: new Set(value),
        xShape: new Set(shape),
        yShape: cloneOptionalShapeSet(closed.xShape),
        x2Shape: new Set(shape),
        entry: closedX2EntryState(),
        vpContext: noneX2VpContextState(),
        structuralEntry: noneX2StructuralEntryState(),
        structuralVpContext: noneX2StructuralEntryState(),
        vpEntryMantissa: vpEntryMantissasFromValueFacts(value),
        vpEntryShape: vpEntryShapesFromShapeFacts(shape),
        memory: input.memory,
        shapeMemory: input.shapeMemory,
      };
    }
    case "indirect-recall": {
      const closed = closeX2ValueEntry(input);
      const target = knownIndirectMemoryTarget(op);
      const values = target === undefined
        ? new Set<X2ValueFact>([SAME_UNKNOWN_VALUE])
        : recallX2ValueFacts(input, target, trackRegisterMemory, op);
      const shape = recallX2ShapeFacts(
        values,
        op,
        target === undefined || !trackRegisterMemory ? undefined : input.shapeMemory?.[target],
      );
      return {
        x: values,
        y: new Set(closed.x),
        x2: new Set(values),
        xShape: new Set(shape),
        yShape: cloneOptionalShapeSet(closed.xShape),
        x2Shape: new Set(shape),
        entry: closedX2EntryState(),
        vpContext: noneX2VpContextState(),
        structuralEntry: noneX2StructuralEntryState(),
        structuralVpContext: noneX2StructuralEntryState(),
        vpEntryMantissa: vpEntryMantissasFromValueFacts(values),
        vpEntryShape: vpEntryShapesFromShapeFacts(shape),
        memory: input.memory,
        shapeMemory: input.shapeMemory,
      };
    }
    case "plain":
      return transferPlainX2ValueState(input, op, producerIndex);
    case "cjump": {
      const closed = closeX2ValueEntry(input);
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      const x = syncUnknownSameValue(new Set(closed.x), effect, producerIndex);
      const xShape = cloneOptionalShapeSet(closed.xShape);
      return {
        x,
        y: cloneOptionalValueSet(closed.y),
        x2: transferConditionalX2ValueSet(closed, x, effect),
        xShape,
        yShape: cloneOptionalShapeSet(closed.yShape),
        x2Shape: transferConditionalX2ShapeSet(closed, xShape, effect),
        entry: closedX2EntryState(),
        vpContext: transferConditionalX2VpContextState(closed, effect),
        structuralEntry: noneX2StructuralEntryState(),
        structuralVpContext: transferConditionalX2StructuralVpContextState(closed, effect),
        vpEntryMantissa: transferConditionalX2VpEntryMantissaState(x, effect),
        vpEntryShape: transferConditionalX2VpEntryShapeState(xShape, effect),
        memory: closed.memory,
        shapeMemory: closed.shapeMemory,
      };
    }
    case "loop": {
      const closed = closeX2ValueEntry(input);
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      const counter = loopCounterRegister(op.counter);
      const stable = invalidateRegisterDependentX2ValueState(closed, counter, trackRegisterMemory);
      const counterFact = registerValueFact(counter);
      const x = syncUnknownSameValue(removeX2Value(stable.x, counterFact), effect, producerIndex);
      const xShape = cloneOptionalShapeSet(stable.xShape);
      const x2 = effect === "preserves"
        ? removeX2Value(stable.x2, counterFact)
        : effect === "affects"
          ? new Set(x)
          : new Set<X2ValueFact>();
      return {
        x,
        y: cloneOptionalValueSet(stable.y),
        x2,
        xShape,
        yShape: cloneOptionalShapeSet(stable.yShape),
        x2Shape: transferConditionalX2ShapeSet(stable, xShape, effect),
        entry: closedX2EntryState(),
        vpContext: transferConditionalX2VpContextState(stable, effect),
        structuralEntry: noneX2StructuralEntryState(),
        structuralVpContext: transferConditionalX2StructuralVpContextState(stable, effect),
        vpEntryMantissa: transferConditionalX2VpEntryMantissaState(x, effect),
        vpEntryShape: transferConditionalX2VpEntryShapeState(xShape, effect),
        memory: trackRegisterMemory ? deleteX2ValueMemory(stable.memory, counter) : undefined,
        shapeMemory: trackRegisterMemory ? deleteX2ShapeMemory(stable.shapeMemory, counter) : undefined,
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
      const x = syncUnknownSameValue(new Set(closed.x), "affects", producerIndex);
      const xShape = cloneOptionalShapeSet(closed.xShape);
      return {
        x,
        y: cloneOptionalValueSet(closed.y),
        x2: new Set(x),
        xShape,
        yShape: cloneOptionalShapeSet(closed.yShape),
        x2Shape: cloneOptionalShapeSet(xShape),
        entry: closedX2EntryState(),
        vpContext: noneX2VpContextState(),
        structuralEntry: noneX2StructuralEntryState(),
        structuralVpContext: noneX2StructuralEntryState(),
        vpEntryMantissa: transferConditionalX2VpEntryMantissaState(x, "affects"),
        vpEntryShape: transferConditionalX2VpEntryShapeState(xShape, "affects"),
        memory: closed.memory,
        shapeMemory: closed.shapeMemory,
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

function loopCounterRegister(counter: Extract<IrOp, { kind: "loop" }>["counter"]): RegisterName {
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
  producerIndex: number | undefined,
): X2ValueDataflowState {
  if (op.opcode >= 0 && op.opcode <= 9) {
    return transferDecimalDigitX2ValueState(input, String(op.opcode));
  }
  if (op.opcode === 0x0a) {
    return transferDotRestoreX2ValueState(input);
  }
  if (op.opcode === 0x0b) {
    return transferSignChangeX2ValueState(input, producerIndex);
  }
  if (op.opcode === 0x0c) {
    return transferVpX2ValueState(input);
  }
  if (op.opcode === 0x0d) {
    return {
      x: new Set([NORMALIZED_DECIMAL_ZERO]),
      y: cloneOptionalValueSet(input.y),
      x2: new Set([NORMALIZED_DECIMAL_ZERO]),
      xShape: new Set([decimalMantissaShapeFact("0")]),
      yShape: cloneOptionalShapeSet(input.yShape),
      x2Shape: new Set([decimalMantissaShapeFact("0")]),
      entry: closedX2EntryState(),
      vpContext: noneX2VpContextState(),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: noneX2StructuralEntryState(),
      vpEntryMantissa: new Set(["0"]),
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  if (op.opcode === STACK_EXCHANGE_XY_OPCODE) {
    return transferExchangeXYX2ValueState(input, op);
  }
  const effect = plainX2Effect(op);
  const closedExponentValues = closedExponentEntryDecimalFacts(input.entry);
  if (closedExponentValues.size > 0) {
    const sourceX = new Set(closedExponentValues);
    const closedExponentShapes = closedExponentEntryShapeFacts(input.entry);
    const x = plainPreservesXValue(op)
      ? new Set(sourceX)
      : plainXValueAfterNonPreservingOp(op, producerIndex, sourceX, input.y, closedExponentShapes, input.yShape);
    const xShape = plainPreservesXValue(op) ? new Set(closedExponentShapes) : new Set<X2ShapeFact>();
    const x2 = effect === "preserves"
      ? new Set(closedExponentValues)
      : effect === "affects"
        ? new Set(x)
        : new Set<X2ValueFact>();
    const x2Shape = transferPlainX2ShapeSet(input, xShape, effect, closedExponentShapes);
    return {
      x,
      y: transferPlainYValueSet(input, sourceX, op),
      x2,
      xShape,
      yShape: transferPlainYShapeSet(input, closedExponentShapes, op),
      x2Shape,
      entry: nextX2EntryStateForPlainEffect(effect),
      vpContext: transferPlainX2VpContextState(input, effect),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: transferPlainX2StructuralVpContextState(input, effect),
      vpEntryMantissa: transferPlainX2VpEntryMantissaState(input, op, x, x2, effect),
      vpEntryShape: transferPlainX2VpEntryShapeState(input, op, xShape, x2Shape, effect),
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  const closedExponentShapes = closedExponentEntryShapeFacts(input.entry);
  if (closedExponentShapes.size > 0) {
    const sourceX = new Set<X2ValueFact>();
    const x = plainPreservesXValue(op)
      ? new Set<X2ValueFact>()
      : plainXValueAfterNonPreservingOp(op, producerIndex, sourceX, input.y, closedExponentShapes, input.yShape);
    const xShape = plainPreservesXValue(op) ? new Set(closedExponentShapes) : new Set<X2ShapeFact>();
    const x2Shape = transferPlainX2ShapeSet(input, xShape, effect, closedExponentShapes);
    return {
      x,
      y: transferPlainYValueSet(input, sourceX, op),
      x2: new Set(),
      xShape,
      yShape: transferPlainYShapeSet(input, closedExponentShapes, op),
      x2Shape,
      entry: nextX2EntryStateForPlainEffect(effect),
      vpContext: transferPlainX2VpContextState(input, effect),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: transferPlainX2StructuralVpContextState(input, effect),
      vpEntryMantissa: transferPlainX2VpEntryMantissaState(input, op, x, new Set(), effect),
      vpEntryShape: transferPlainX2VpEntryShapeState(input, op, xShape, x2Shape, effect),
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  const x = syncUnknownSameValue(
    plainPreservesXValue(op)
      ? new Set(input.x)
      : plainXValueAfterNonPreservingOp(op, producerIndex, input.x, input.y, input.xShape, input.yShape),
    effect,
    producerIndex,
  );
  const x2 = transferPlainX2ValueSet(input, x, effect);
  const xShape = plainPreservesXValue(op) ? cloneOptionalShapeSet(input.xShape) : new Set<X2ShapeFact>();
  const x2Shape = transferPlainX2ShapeSet(input, xShape, effect);
  const structuralEntry = input.structuralEntry ?? noneX2StructuralEntryState();
  if (structuralEntry.kind === "exponent") {
    const closedStructuralShapes = structuralExponentEntryShapeFacts(structuralEntry);
    const sourceX = new Set<X2ValueFact>();
    const structuralXValue = plainPreservesXValue(op)
      ? new Set<X2ValueFact>()
      : plainXValueAfterNonPreservingOp(op, producerIndex, sourceX, input.y, closedStructuralShapes, input.yShape);
    const structuralXShape = plainPreservesXValue(op) ? new Set(closedStructuralShapes) : new Set<X2ShapeFact>();
    const structuralX2Shape = transferPlainX2ShapeSet(input, structuralXShape, effect, closedStructuralShapes);
    return {
      x: structuralXValue,
      y: transferPlainYValueSet(input, sourceX, op),
      x2: new Set(),
      xShape: structuralXShape,
      yShape: transferPlainYShapeSet(input, closedStructuralShapes, op),
      x2Shape: structuralX2Shape,
      entry: nextX2EntryStateForPlainEffect(effect),
      vpContext: transferPlainX2VpContextState(input, effect),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: transferPlainX2StructuralVpContextState(input, effect),
      vpEntryShape: transferPlainX2VpEntryShapeState(input, op, structuralXShape, structuralX2Shape, effect),
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  return {
    x,
    y: transferPlainYValueSet(input, input.x, op),
    x2,
    xShape,
    yShape: transferPlainYShapeSet(input, input.xShape, op),
    x2Shape,
    entry: nextX2EntryStateForPlainEffect(effect),
    vpContext: transferPlainX2VpContextState(input, effect),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: transferPlainX2StructuralVpContextState(input, effect),
    vpEntryMantissa: transferPlainX2VpEntryMantissaState(input, op, x, x2, effect),
    vpEntryShape: transferPlainX2VpEntryShapeState(input, op, xShape, x2Shape, effect),
    memory: input.memory,
    shapeMemory: input.shapeMemory,
  };
}

function transferDecimalDigitX2ValueState(input: X2ValueDataflowState, digit: string): X2ValueDataflowState {
  if (input.structuralEntry?.kind === "exponent") {
    const entry = advanceStructuralExponentDigitEntry(input.structuralEntry, digit);
    return x2ValueStateFromStructuralExponentEntry(entry, input.memory, input.shapeMemory, input.y, input.yShape);
  }
  if (input.entry.kind === "exponent") {
    const entry = advanceExponentDigitEntry(input.entry, digit);
    return x2ValueStateFromExponentEntry(entry, input.memory, input.shapeMemory, input.y, input.yShape);
  }
  const entry = advanceDecimalDigitEntry(input.entry, digit);
  if (entry.kind !== "open") {
    return {
      x: new Set(),
      y: cloneOptionalValueSet(input.y),
      x2: new Set(),
      xShape: new Set(),
      yShape: cloneOptionalShapeSet(input.yShape),
      x2Shape: new Set(),
      entry,
      vpContext: noneX2VpContextState(),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: noneX2StructuralEntryState(),
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  return x2ValueStateFromOpenDecimalEntry(entry, input.memory, input.shapeMemory, input.y, input.yShape);
}

function x2ValueStateFromOpenDecimalEntry(
  entry: Extract<X2EntryState, { kind: "open" }>,
  memory: X2ValueMemory | undefined,
  shapeMemory: X2ShapeMemory | undefined,
  y: X2ValueSet | undefined = undefined,
  yShape: X2ShapeSet | undefined = undefined,
): X2ValueDataflowState {
  const x = new Set<X2ValueFact>();
  const x2 = new Set<X2ValueFact>();
  const xShape = new Set<X2ShapeFact>();
  const x2Shape = new Set<X2ShapeFact>();
  for (const raw of entry.raw) {
    const normalized = normalizeDecimalMantissaEntry(raw);
    if (normalized !== undefined) {
      x.add(decimalValueFact(normalized, "normalized"));
      xShape.add(decimalMantissaShapeFact(normalized));
    }
    const x2Fact = x2DecimalEntryFact(raw);
    if (x2Fact !== undefined) x2.add(x2Fact);
    x2Shape.add(decimalMantissaShapeFact(raw));
  }
  return {
    x,
    y: cloneOptionalValueSet(y),
    x2,
    xShape,
    yShape: cloneOptionalShapeSet(yShape),
    x2Shape,
    entry,
    vpContext: noneX2VpContextState(),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: noneX2StructuralEntryState(),
    memory,
    shapeMemory,
  };
}

function transferDotRestoreX2ValueState(input: X2ValueDataflowState): X2ValueDataflowState {
  if (input.entry.kind === "open") {
    const entry = advanceDecimalPointEntry(input.entry);
    if (entry.kind === "open") {
      return x2ValueStateFromOpenDecimalEntry(entry, input.memory, input.shapeMemory, input.y, input.yShape);
    }
    return {
      x: new Set(),
      y: cloneOptionalValueSet(input.y),
      x2: new Set(),
      xShape: new Set(),
      yShape: cloneOptionalShapeSet(input.yShape),
      x2Shape: new Set(),
      entry,
      vpContext: noneX2VpContextState(),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: noneX2StructuralEntryState(),
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  if (input.entry.kind !== "closed") {
    return {
      x: new Set(),
      y: cloneOptionalValueSet(input.y),
      x2: new Set(),
      yShape: cloneOptionalShapeSet(input.yShape),
      entry: { kind: "unknown" },
      vpContext: { kind: "unknown" },
      structuralEntry: { kind: "unknown" },
      structuralVpContext: { kind: "unknown" },
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  return {
    x: normalizeX2RestoreFactsForX(input.x2),
    y: cloneOptionalValueSet(input.y),
    x2: new Set(input.x2),
    xShape: normalizeX2RestoreShapesForX(input.x2Shape),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: noneX2StructuralEntryState(),
    vpEntryMantissa: vpEntryMantissasFromValueFacts(input.x2),
    vpEntryShape: vpEntryShapesFromShapeFacts(input.x2Shape),
    memory: input.memory,
    shapeMemory: input.shapeMemory,
  };
}

function transferSignChangeX2ValueState(
  input: X2ValueDataflowState,
  producerIndex: number | undefined,
): X2ValueDataflowState {
  if (input.structuralEntry?.kind === "exponent") {
    const entry = signChangeStructuralExponentEntry(input.structuralEntry);
    return x2ValueStateFromStructuralExponentEntry(entry, input.memory, input.shapeMemory, input.y, input.yShape);
  }
  if (input.entry.kind === "exponent") {
    const entry = signChangeExponentEntry(input.entry) as Extract<X2EntryState, { kind: "exponent" }>;
    return x2ValueStateFromExponentEntry(entry, input.memory, input.shapeMemory, input.y, input.yShape);
  }
  if (input.entry.kind === "closed" && input.structuralVpContext?.kind === "exponent") {
    return {
      x: new Set(),
      y: cloneOptionalValueSet(input.y),
      x2: new Set(),
      xShape: new Set(),
      yShape: cloneOptionalShapeSet(input.yShape),
      x2Shape: new Set(),
      entry: closedX2EntryState(),
      vpContext: noneX2VpContextState(),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: signChangeStructuralExponentEntry(input.structuralVpContext),
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  if (input.entry.kind === "closed" && input.vpContext?.kind === "exponent") {
    return {
      x: new Set(),
      y: cloneOptionalValueSet(input.y),
      x2: new Set(),
      xShape: new Set(),
      yShape: cloneOptionalShapeSet(input.yShape),
      x2Shape: new Set(),
      entry: closedX2EntryState(),
      vpContext: signChangeVpContext(input.vpContext),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  if (input.entry.kind === "closed" && (input.vpContext === undefined || input.vpContext.kind === "none")) {
    const state = signChangeClosedDecimalState(input, producerIndex);
    if (state !== undefined) return state;
  }
  if (input.entry.kind !== "open") {
    return {
      x: new Set(),
      y: cloneOptionalValueSet(input.y),
      x2: new Set(),
      xShape: new Set(),
      yShape: cloneOptionalShapeSet(input.yShape),
      x2Shape: new Set(),
      entry: { kind: "unknown" },
      vpContext: { kind: "unknown" },
      structuralEntry: { kind: "unknown" },
      structuralVpContext: { kind: "unknown" },
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  const x = new Set<X2ValueFact>();
  const x2 = new Set<X2ValueFact>();
  const xShape = new Set<X2ShapeFact>();
  const x2Shape = new Set<X2ShapeFact>();
  for (const raw of input.entry.raw) {
    const signed = signChangedDecimalEntry(raw);
    const normalized = normalizeDecimalMantissaEntry(signed);
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
    y: cloneOptionalValueSet(input.y),
    x2,
    xShape,
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape,
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: noneX2StructuralEntryState(),
    vpEntryMantissa: signChangedMantissaShapes(input.entry.raw),
    memory: input.memory,
    shapeMemory: input.shapeMemory,
  };
}

function transferVpX2ValueState(input: X2ValueDataflowState): X2ValueDataflowState {
  if (input.entry.kind === "open") {
    // Keep this structural only: exponent-entry hidden X2 shapes can make a
    // later `.` signal ЕГГ0Г, so they are not safe value-alias facts yet.
    return {
      x: new Set(),
      y: cloneOptionalValueSet(input.y),
      x2: new Set(),
      xShape: new Set(),
      yShape: cloneOptionalShapeSet(input.yShape),
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
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: noneX2StructuralEntryState(),
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  if (input.entry.kind === "exponent") {
    return x2ValueStateFromExponentEntry(input.entry, input.memory, input.shapeMemory, input.y, input.yShape);
  }
  if (input.structuralEntry?.kind === "exponent") {
    return x2ValueStateFromStructuralExponentEntry(
      input.structuralEntry,
      input.memory,
      input.shapeMemory,
      input.y,
      input.yShape,
    );
  }
  if (input.entry.kind === "closed" && input.vpEntryMantissa !== undefined) {
    // This is not inferred from plain `X == X2`: the MK-61 previous-command
    // context matters. The fact is set only by proved X2 syncs that can feed
    // ordinary exponent entry, and is cleared by stores and non-empty gaps.
    return {
      x: new Set(),
      y: cloneOptionalValueSet(input.y),
      x2: new Set(),
      xShape: new Set(),
      yShape: cloneOptionalShapeSet(input.yShape),
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
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: noneX2StructuralEntryState(),
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  if (input.entry.kind === "closed" && input.vpEntryShape !== undefined && input.vpEntryShape.size > 0) {
    const structuralEntry = structuralExponentEntryFromVpEntryShapes(input.vpEntryShape);
    return x2ValueStateFromStructuralExponentEntry(structuralEntry, input.memory, input.shapeMemory, input.y, input.yShape);
  }
  return {
    x: new Set(),
    y: cloneOptionalValueSet(input.y),
    x2: new Set(),
    xShape: new Set(),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: new Set(),
    entry: { kind: "unknown" },
    vpContext: { kind: "unknown" },
    structuralEntry: { kind: "unknown" },
    structuralVpContext: { kind: "unknown" },
    memory: input.memory,
    shapeMemory: input.shapeMemory,
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
  const x2Shape = transferConditionalX2ShapeSet(closed, xShape, effect);
  const output: X2ValueDataflowState = {
    x,
    y: cloneOptionalValueSet(closed.y),
    x2: transferConditionalX2ValueSet(closed, x, effect),
    xShape,
    yShape: cloneOptionalShapeSet(closed.yShape),
    x2Shape,
    entry: closedX2EntryState(),
    vpContext: transferConditionalX2VpContextState(closed, effect),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: transferConditionalX2StructuralVpContextState(closed, effect),
    vpEntryMantissa: transferConditionalX2VpEntryMantissaState(x, effect),
    vpEntryShape: transferConditionalX2VpEntryShapeState(xShape, effect),
    memory: closed.memory,
    shapeMemory: closed.shapeMemory,
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
  const stable = registerWritePreservesStoredValue(closed, target)
    ? closed
    : invalidateRegisterDependentX2ValueState(closed, target, trackRegisterMemory);
  const value = registerValueFact(target);
  return {
    x: addX2Value(stable.x, value),
    y: cloneOptionalValueSet(stable.y),
    x2: addStoredX2ValueAlias(stable, value),
    xShape: cloneOptionalShapeSet(stable.xShape),
    yShape: cloneOptionalShapeSet(stable.yShape),
    x2Shape: cloneOptionalShapeSet(stable.x2Shape),
    entry: closedX2EntryState(),
    vpContext: cloneX2VpContextState(stable.vpContext),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: cloneX2StructuralEntryState(stable.structuralVpContext),
    memory: trackRegisterMemory ? storeX2ValueMemory(stable.memory, target, stable.x) : undefined,
    shapeMemory: trackRegisterMemory ? storeX2ShapeMemory(stable.shapeMemory, target, stable.xShape) : undefined,
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

function transferExchangeXYX2ValueState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "plain" }>,
): X2ValueDataflowState {
  const effect = plainX2Effect(op);
  const sourceX = visibleX2ValueFactsForStack(input);
  const sourceXShape = visibleX2ShapeFactsForStack(input);
  const x = cloneOptionalValueSet(input.y);
  const xShape = cloneOptionalShapeSet(input.yShape);
  const x2 = transferPlainX2ValueSet(input, x, effect);
  const x2Shape = transferPlainX2ShapeSet(input, xShape, effect);
  return {
    x,
    y: new Set(sourceX),
    x2,
    xShape,
    yShape: new Set(sourceXShape),
    x2Shape,
    entry: nextX2EntryStateForPlainEffect(effect),
    vpContext: transferPlainX2VpContextState(input, effect),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: transferPlainX2StructuralVpContextState(input, effect),
    vpEntryMantissa: transferPlainX2VpEntryMantissaState(input, op, x, x2, effect),
    vpEntryShape: transferPlainX2VpEntryShapeState(input, op, xShape, x2Shape, effect),
    memory: input.memory,
    shapeMemory: input.shapeMemory,
  };
}

function visibleX2ValueFactsForStack(input: X2ValueDataflowState): X2ValueSet {
  const closedExponentValues = closedExponentEntryDecimalFacts(input.entry);
  return closedExponentValues.size > 0 ? closedExponentValues : input.x;
}

function visibleX2ShapeFactsForStack(input: X2ValueDataflowState): X2ShapeSet {
  const closedExponentShapes = closedExponentEntryShapeFacts(input.entry);
  if (closedExponentShapes.size > 0) return closedExponentShapes;
  const structuralEntry = input.structuralEntry ?? noneX2StructuralEntryState();
  if (structuralEntry.kind === "exponent") return structuralExponentEntryShapeFacts(structuralEntry);
  return cloneOptionalShapeSet(input.xShape);
}

function transferPlainYValueSet(
  input: X2ValueDataflowState,
  sourceX: X2ValueSet,
  op: Extract<IrOp, { kind: "plain" }>,
): Set<X2ValueFact> {
  const info = getOpcode(op.opcode);
  if (info.stackEffect === "shifts") return new Set(sourceX);
  if (info.stackEffect === "preserves") return new Set(input.y ?? []);
  if (info.stackEffect === "consume-y-keep" && info.risk === "documented") return new Set(input.y ?? []);
  return new Set();
}

function transferPlainYShapeSet(
  input: X2ValueDataflowState,
  sourceXShape: X2ShapeSet | undefined,
  op: Extract<IrOp, { kind: "plain" }>,
): Set<X2ShapeFact> {
  const info = getOpcode(op.opcode);
  if (info.stackEffect === "shifts") return cloneOptionalShapeSet(sourceXShape);
  if (info.stackEffect === "preserves") return cloneOptionalShapeSet(input.yShape);
  if (info.stackEffect === "consume-y-keep" && info.risk === "documented") {
    return cloneOptionalShapeSet(input.yShape);
  }
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
  producerIndex: number | undefined = undefined,
): Set<X2ValueFact> {
  if (effect === "affects" && x.size === 0) {
    x.add(producerIndex === undefined ? SAME_UNKNOWN_VALUE : expressionValueFact(producerIndex));
  }
  return x;
}

function transferPlainX2VpContextState(
  input: X2ValueDataflowState,
  effect: ReturnType<typeof plainX2Effect>,
): X2VpContextState {
  return effect === "preserves" ? cloneX2VpContextState(input.vpContext) : noneX2VpContextState();
}

function transferPlainX2StructuralVpContextState(
  input: X2ValueDataflowState,
  effect: ReturnType<typeof plainX2Effect>,
): X2StructuralEntryState {
  if (effect !== "preserves") return noneX2StructuralEntryState();
  if (input.structuralEntry?.kind === "exponent") return x2StructuralContextFromEntry(input.structuralEntry);
  return cloneX2StructuralEntryState(input.structuralVpContext);
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

function transferPlainX2VpEntryShapeState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "plain" }>,
  xShape: X2ShapeSet,
  x2Shape: X2ShapeSet,
  effect: ReturnType<typeof plainX2Effect>,
): X2ShapeSet | undefined {
  if (effect === "affects") return sharedStructuralShapeFacts({ xShape, x2Shape });
  if (effect === "preserves" && isEmptyPlainOp(op)) return cloneOptionalShapeSet(input.vpEntryShape);
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

function transferConditionalX2StructuralVpContextState(
  input: X2ValueDataflowState,
  effect: ReturnType<typeof conditionalX2Effect>,
): X2StructuralEntryState {
  if (effect !== "preserves") return noneX2StructuralEntryState();
  if (input.structuralEntry?.kind === "exponent") return x2StructuralContextFromEntry(input.structuralEntry);
  return cloneX2StructuralEntryState(input.structuralVpContext);
}

function transferConditionalX2VpEntryMantissaState(
  x: X2ValueSet,
  effect: ReturnType<typeof conditionalX2Effect>,
): ReadonlySet<string> | undefined {
  return effect === "affects" ? sharedNormalizedDecimalMantissas({ x, x2: x }) : undefined;
}

function transferConditionalX2VpEntryShapeState(
  xShape: X2ShapeSet,
  effect: ReturnType<typeof conditionalX2Effect>,
): X2ShapeSet | undefined {
  return effect === "affects" ? sharedStructuralShapeFacts({ xShape, x2Shape: xShape }) : undefined;
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
    y: cloneOptionalValueSet(incoming.y),
    x2: new Set(incoming.x2),
    xShape: cloneOptionalShapeSet(incoming.xShape),
    yShape: cloneOptionalShapeSet(incoming.yShape),
    x2Shape: cloneOptionalShapeSet(incoming.x2Shape),
    entry: cloneX2EntryState(incoming.entry),
    vpContext: cloneX2VpContextState(incoming.vpContext),
    structuralEntry: cloneX2StructuralEntryState(incoming.structuralEntry),
    structuralVpContext: cloneX2StructuralEntryState(incoming.structuralVpContext),
    vpEntryMantissa: cloneOptionalStringSet(incoming.vpEntryMantissa),
    vpEntryShape: cloneOptionalShapeSet(incoming.vpEntryShape),
    memory: trackRegisterMemory ? cloneX2ValueMemory(incoming.memory) : undefined,
    shapeMemory: trackRegisterMemory ? cloneX2ShapeMemory(incoming.shapeMemory) : undefined,
  };
  return {
    x: joinX2ValueSets(current.x, incoming.x),
    y: joinX2ValueSets(current.y ?? new Set(), incoming.y ?? new Set()),
    x2: joinX2ValueSets(current.x2, incoming.x2),
    xShape: joinOptionalShapeSets(current.xShape, incoming.xShape),
    yShape: joinOptionalShapeSets(current.yShape, incoming.yShape),
    x2Shape: joinOptionalShapeSets(current.x2Shape, incoming.x2Shape),
    entry: joinX2EntryStates(current.entry, incoming.entry),
    vpContext: joinX2VpContextStates(current.vpContext, incoming.vpContext),
    structuralEntry: joinX2StructuralEntryStates(current.structuralEntry, incoming.structuralEntry),
    structuralVpContext: joinX2StructuralEntryStates(current.structuralVpContext, incoming.structuralVpContext),
    vpEntryMantissa: joinOptionalStringSets(current.vpEntryMantissa, incoming.vpEntryMantissa),
    vpEntryShape: joinOptionalShapeSets(current.vpEntryShape, incoming.vpEntryShape),
    memory: trackRegisterMemory ? joinX2ValueMemories(current.memory, incoming.memory) : undefined,
    shapeMemory: trackRegisterMemory ? joinX2ShapeMemories(current.shapeMemory, incoming.shapeMemory) : undefined,
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
    sameX2ValueSet(left.y, right.y) &&
    sameX2ValueSet(left.x2, right.x2) &&
    sameOptionalShapeSet(left.xShape, right.xShape) &&
    sameOptionalShapeSet(left.yShape, right.yShape) &&
    sameOptionalShapeSet(left.x2Shape, right.x2Shape) &&
    sameX2EntryState(left.entry, right.entry) &&
    sameX2VpContextState(left.vpContext, right.vpContext) &&
    sameX2StructuralEntryState(left.structuralEntry, right.structuralEntry) &&
    sameX2StructuralEntryState(left.structuralVpContext, right.structuralVpContext) &&
    sameOptionalStringSet(left.vpEntryMantissa, right.vpEntryMantissa) &&
    sameOptionalShapeSet(left.vpEntryShape, right.vpEntryShape) &&
    sameX2ValueMemory(left.memory, right.memory) &&
    sameX2ShapeMemory(left.shapeMemory, right.shapeMemory);
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

function cloneX2ShapeMemory(input: X2ShapeMemory | undefined): X2ShapeMemory {
  const output: X2ShapeMemory = {};
  for (const register of x2ShapeMemoryRegisters(input)) {
    const shapes = input?.[register];
    if (shapes !== undefined && shapes.size > 0) output[register] = new Set(shapes);
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

function joinX2ShapeMemories(
  current: X2ShapeMemory | undefined,
  incoming: X2ShapeMemory | undefined,
): X2ShapeMemory {
  const output: X2ShapeMemory = {};
  if (current === undefined || incoming === undefined) return output;
  for (const register of x2ShapeMemoryRegisters(current)) {
    const shapes = intersectKnownX2ShapeSets(current?.[register], incoming?.[register]);
    if (shapes.size > 0) output[register] = shapes;
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

function intersectKnownX2ShapeSets(
  current: X2ShapeSet | undefined,
  incoming: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  return intersectX2ShapeSets(current, incoming);
}

function intersectX2ShapeSets(
  current: X2ShapeSet | undefined,
  incoming: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  if (current === undefined || incoming === undefined) return new Set();
  const currentSet = canonicalShapeSet(current);
  const incomingSet = canonicalShapeSet(incoming);
  const joined = new Set<X2ShapeFact>();
  for (const shape of currentSet) {
    if (incomingSet.has(shape)) joined.add(shape);
  }
  if (sameCanonicalShapeSet(currentSet, incomingSet)) return joined;
  for (const shape of commonStructuralRestoreShapeFacts(currentSet, incomingSet)) {
    joined.add(shape);
  }
  return joined;
}

function commonStructuralRestoreShapeFacts(
  left: X2ShapeSet,
  right: X2ShapeSet,
): Set<X2ShapeFact> {
  const leftRestoreShapes = structuralRestoreShapeFacts(left);
  const rightRestoreShapes = structuralRestoreShapeFacts(right);
  const output = new Set<X2ShapeFact>();
  for (const fact of leftRestoreShapes) {
    if (!rightRestoreShapes.has(fact)) continue;
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind === "mantissa" && (model.radix === "hex" || model.radix === "super")) {
      output.add(fact);
    }
  }
  return output;
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

function sameX2ShapeMemory(left: X2ShapeMemory | undefined, right: X2ShapeMemory | undefined): boolean {
  const leftRegisters = x2ShapeMemoryRegisters(left);
  const rightRegisters = x2ShapeMemoryRegisters(right);
  if (leftRegisters.length !== rightRegisters.length) return false;
  for (const register of leftRegisters) {
    if (!rightRegisters.includes(register)) return false;
    if (!sameOptionalShapeSet(left?.[register], right?.[register])) return false;
  }
  return true;
}

function sameCanonicalShapeSet(left: X2ShapeSet, right: X2ShapeSet): boolean {
  if (left.size !== right.size) return false;
  for (const shape of left) {
    if (!right.has(shape)) return false;
  }
  return true;
}

function x2ValueMemoryRegisters(input: X2ValueMemory | undefined): RegisterName[] {
  if (input === undefined) return [];
  return Object.keys(input).filter((key): key is RegisterName =>
    (REGISTER_NAMES as readonly string[]).includes(key) && input[key as RegisterName] !== undefined
  );
}

function x2ShapeMemoryRegisters(input: X2ShapeMemory | undefined): RegisterName[] {
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
  const stored = storableX2MemoryValueFacts(values);
  if (stored.size === 0) delete output[register];
  else output[register] = stored;
  return output;
}

function storeX2ShapeMemory(
  input: X2ShapeMemory | undefined,
  register: RegisterName,
  shapes: X2ShapeSet | undefined,
): X2ShapeMemory {
  const output = cloneX2ShapeMemory(input);
  const stored = storableX2ShapeMemoryFacts(shapes);
  if (stored.size === 0) delete output[register];
  else output[register] = stored;
  return output;
}

function invalidateRegisterDependentX2ValueState(
  input: X2ValueDataflowState,
  register: RegisterName,
  trackRegisterMemory: boolean,
): X2ValueDataflowState {
  return {
    x: removeRegisterDependentValueFacts(input.x, register),
    y: removeRegisterDependentValueFacts(input.y, register),
    x2: removeRegisterDependentValueFacts(input.x2, register),
    xShape: cloneOptionalShapeSet(input.xShape),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: cloneX2EntryState(input.entry),
    vpContext: cloneX2VpContextState(input.vpContext),
    structuralEntry: cloneX2StructuralEntryState(input.structuralEntry),
    structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
    vpEntryMantissa: cloneOptionalStringSet(input.vpEntryMantissa),
    vpEntryShape: cloneOptionalShapeSet(input.vpEntryShape),
    memory: trackRegisterMemory ? removeRegisterDependentX2ValueMemory(input.memory, register) : undefined,
    shapeMemory: trackRegisterMemory ? cloneX2ShapeMemory(input.shapeMemory) : undefined,
  };
}

function x2ValueEdgeDropsUnstableOpaqueExpressionFacts(op: IrOp, edge: Edge, sourceIndex: number): boolean {
  return edge.target <= sourceIndex || op.kind === "call" || op.kind === "indirect-call" || op.kind === "return";
}

function dropUnstableOpaqueExpressionX2ValueFacts(
  input: X2ValueDataflowState,
  trackRegisterMemory: boolean,
): X2ValueDataflowState {
  return {
    x: removeUnstableOpaqueExpressionValueFacts(input.x),
    y: removeUnstableOpaqueExpressionValueFacts(input.y),
    x2: removeUnstableOpaqueExpressionValueFacts(input.x2),
    xShape: cloneOptionalShapeSet(input.xShape),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: cloneX2EntryState(input.entry),
    vpContext: cloneX2VpContextState(input.vpContext),
    structuralEntry: cloneX2StructuralEntryState(input.structuralEntry),
    structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
    vpEntryMantissa: cloneOptionalStringSet(input.vpEntryMantissa),
    vpEntryShape: cloneOptionalShapeSet(input.vpEntryShape),
    memory: trackRegisterMemory ? removeUnstableOpaqueExpressionValueMemory(input.memory) : undefined,
    shapeMemory: trackRegisterMemory ? cloneX2ShapeMemory(input.shapeMemory) : undefined,
  };
}

function registerWritePreservesStoredValue(input: X2ValueDataflowState, register: RegisterName): boolean {
  return input.x.has(registerValueFact(register));
}

function removeUnstableOpaqueExpressionValueMemory(input: X2ValueMemory | undefined): X2ValueMemory {
  const output: X2ValueMemory = {};
  for (const key of x2ValueMemoryRegisters(input)) {
    const values = removeUnstableOpaqueExpressionValueFacts(input?.[key]);
    if (values.size > 0) output[key] = values;
  }
  return output;
}

function removeUnstableOpaqueExpressionValueFacts(input: X2ValueSet | undefined): Set<X2ValueFact> {
  const output = new Set<X2ValueFact>();
  for (const fact of input ?? []) {
    if (!isUnstableOpaqueExpressionValueFact(fact)) output.add(fact);
  }
  return output;
}

function isUnstableOpaqueExpressionValueFact(fact: X2ValueFact): boolean {
  return /^expr:\d+$/u.test(fact);
}

function removeRegisterDependentX2ValueMemory(
  input: X2ValueMemory | undefined,
  register: RegisterName,
): X2ValueMemory {
  const output: X2ValueMemory = {};
  for (const key of x2ValueMemoryRegisters(input)) {
    const values = removeRegisterDependentValueFacts(input?.[key], register);
    if (values.size > 0) output[key] = values;
  }
  return output;
}

function removeRegisterDependentValueFacts(
  input: X2ValueSet | undefined,
  register: RegisterName,
): Set<X2ValueFact> {
  const output = new Set<X2ValueFact>();
  for (const fact of input ?? []) {
    if (!x2ValueFactDependsOnRegister(fact, register)) output.add(fact);
  }
  return output;
}

function x2ValueFactDependsOnRegister(fact: X2ValueFact, register: RegisterName): boolean {
  if (fact === registerValueFact(register)) return true;
  if (!fact.startsWith("expr-key:")) return false;
  const re = /reg:([0-9a-e])/gu;
  for (const match of fact.matchAll(re)) {
    if (match[1] === register) return true;
  }
  return false;
}

function deleteX2ValueMemory(input: X2ValueMemory | undefined, register: RegisterName): X2ValueMemory {
  const output = cloneX2ValueMemory(input);
  delete output[register];
  return output;
}

function deleteX2ShapeMemory(input: X2ShapeMemory | undefined, register: RegisterName): X2ShapeMemory {
  const output = cloneX2ShapeMemory(input);
  delete output[register];
  return output;
}

function clearX2ValueMemory(input: X2ValueDataflowState): X2ValueDataflowState {
  return {
    x: new Set(input.x),
    y: cloneOptionalValueSet(input.y),
    x2: new Set(input.x2),
    xShape: cloneOptionalShapeSet(input.xShape),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: cloneX2EntryState(input.entry),
    vpContext: cloneX2VpContextState(input.vpContext),
    structuralEntry: cloneX2StructuralEntryState(input.structuralEntry),
    structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
    vpEntryMantissa: cloneOptionalStringSet(input.vpEntryMantissa),
    vpEntryShape: cloneOptionalShapeSet(input.vpEntryShape),
    memory: {},
    shapeMemory: {},
  };
}

function concreteStoredX2ValueFacts(values: X2ValueSet): Set<X2ValueFact> {
  const output = new Set<X2ValueFact>();
  for (const value of values) {
    if (isConcreteDecimalX2ValueFact(value)) output.add(value);
  }
  return output;
}

function storableX2ShapeMemoryFacts(shapes: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const shape of shapes ?? []) {
    const canonical = x2ShapeFactFromDataModel(x2ShapeDataModelForFact(shape));
    output.add(canonical ?? shape);
  }
  return output;
}

function storableX2MemoryValueFacts(values: X2ValueSet): Set<X2ValueFact> {
  const output = concreteStoredX2ValueFacts(values);
  for (const value of values) {
    if (isExpressionX2ValueFact(value)) output.add(value);
  }
  return output;
}

function isConcreteDecimalX2ValueFact(value: X2ValueFact): boolean {
  return /^decimal:-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+):(normalized|unnormalized)$/u.test(value);
}

function isExpressionX2ValueFact(value: X2ValueFact): boolean {
  return /^expr:\d+$/u.test(value) || value.startsWith("expr-key:");
}

function isStorableX2MemoryValueFact(value: X2ValueFact): boolean {
  return isConcreteDecimalX2ValueFact(value) || isExpressionX2ValueFact(value);
}

function isOpaqueSharedValueFact(value: X2ValueFact): boolean {
  return value === SAME_UNKNOWN_VALUE || value.startsWith("reg:") || value.startsWith("expr:") ||
    value.startsWith("expr-key:");
}

function recallX2ValueFacts(
  input: X2ValueDataflowState,
  register: RegisterName,
  trackRegisterMemory: boolean,
  op?: IrOp,
): Set<X2ValueFact> {
  const value = registerValueFact(register);
  const output = new Set<X2ValueFact>(trackRegisterMemory ? input.memory?.[register] ?? [] : []);
  for (const fact of preloadedConstantValueFacts(op)) output.add(fact);
  output.add(value);
  return output;
}

function recallX2ShapeFacts(
  values: X2ValueSet,
  op?: IrOp,
  memoryShapes?: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  const output = x2ShapesFromValueFacts(values);
  for (const fact of memoryShapes ?? []) output.add(fact);
  for (const fact of preloadedConstantShapeFacts(op)) output.add(fact);
  return output;
}

function recallStructuralShapeFacts(
  op: IrOp,
  state: X2ValueDataflowState,
  register: RegisterName,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const fact of state.shapeMemory?.[register] ?? []) {
    for (const structural of structuralRestoreShapeFacts(new Set([fact]))) output.add(structural);
  }
  for (const fact of preloadedConstantShapeFacts(op)) {
    for (const structural of structuralRestoreShapeFacts(new Set([fact]))) output.add(structural);
  }
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

function dotSafeDecimalShapeValues(input: X2ShapeSet | undefined): Set<string> {
  const output = new Set<string>();
  for (const model of x2ShapeDataModels(input)) {
    if (model.kind !== "mantissa" || model.radix !== "decimal" || model.safety !== "dotSafeDecimal") continue;
    if (model.normalizedDecimal !== undefined && model.normalizedSameAsRaw) output.add(model.normalizedDecimal);
  }
  return output;
}

function structuralMantissaShapeFacts(input: X2ShapeSet): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const fact of input) {
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind === "mantissa" && (model.radix === "hex" || model.radix === "super")) {
      const canonical = x2ShapeFactFromDataModel(model);
      if (canonical !== undefined) output.add(canonical);
    }
  }
  return output;
}

function structuralRestoreShapeFacts(input: X2ShapeSet): Set<X2ShapeFact> {
  const output = structuralMantissaShapeFacts(input);
  for (const fact of input) {
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind === "exponent-entry" && model.safety === "structuralOnly") {
      const canonical = x2ShapeFactFromDataModel(model);
      if (canonical !== undefined) output.add(canonical);
      const closed = x2ClosedStructuralExponentMantissaShapeFact(fact);
      if (closed !== undefined) output.add(closed);
    }
  }
  return output;
}

function canonicalStructuralShapeFacts(input: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const fact of input ?? []) {
    const canonical = x2ShapeFactFromDataModel(x2ShapeDataModelForFact(fact));
    if (canonical !== undefined && x2ShapeFactSafety(canonical) === "structuralOnly") output.add(canonical);
  }
  return output;
}

function decimalMantissaDataModel(raw: string): X2MantissaDataModel {
  const canonical = canonicalShapeRaw(raw);
  const normalized = normalizePlainDecimal(canonical);
  return {
    kind: "mantissa",
    radix: "decimal",
    raw,
    canonical,
    sign: canonical.startsWith("-") ? "-" : "",
    hasDecimalPoint: canonical.includes("."),
    hasLeadingZero: decimalHasLeadingZero(canonical, normalized),
    digits: shapeDigits(canonical),
    significantDigits: normalized === undefined ? 0 : significantDecimalDigits(normalized),
    normalizedDecimal: normalized,
    normalizedSameAsRaw: normalized !== undefined && canonical === normalized,
    safety: normalized === undefined ? "unknown" : "dotSafeDecimal",
  };
}

function structuralMantissaDataModel(
  radix: "hex" | "super",
  raw: string,
  safety: X2ShapeSafety,
): X2MantissaDataModel {
  const canonical = canonicalShapeRaw(raw);
  const digits = shapeDigits(canonical);
  return {
    kind: "mantissa",
    radix,
    raw,
    canonical,
    sign: canonical.startsWith("-") ? "-" : "",
    hasDecimalPoint: canonical.includes("."),
    hasLeadingZero: shapeHasLeadingZero(canonical),
    digits,
    significantDigits: significantShapeDigits(digits),
    normalizedSameAsRaw: true,
    safety,
  };
}

function closedStructuralExponentMantissaModel(
  mantissa: X2MantissaDataModel,
  exponentRaw: string,
): X2MantissaDataModel | undefined {
  if (mantissa.radix !== "hex" && mantissa.radix !== "super") return undefined;
  const shifted = shiftedStructuralMantissaRaw(mantissa.canonical, exponentRaw);
  if (shifted === undefined) return undefined;
  const radix = mantissa.radix === "super" && shifted !== mantissa.canonical ? "hex" : mantissa.radix;
  return structuralMantissaDataModel(radix, shifted, "structuralOnly");
}

function shiftedStructuralMantissaRaw(raw: string, exponentRaw: string): string | undefined {
  const exponent = canonicalExponentShapeRaw(exponentRaw);
  if (exponent === undefined) return undefined;
  if (exponent === "" || exponent === "0" || exponent === "00") return raw;
  const shift = Number(exponent);
  if (!Number.isInteger(shift) || Math.abs(shift) > 7) return undefined;
  const sign = raw.startsWith("-") ? "-" : "";
  const unsigned = sign === "" ? raw : raw.slice(1);
  const parts = unsigned.split(".");
  if (parts.length > 2) return undefined;
  const integer = parts[0] ?? "";
  const fraction = parts[1] ?? "";
  if (integer.length === 0 && fraction.length === 0) return undefined;
  const digits = `${integer}${fraction}`;
  if (!/^[0-9A-FА-Я]+$/u.test(digits)) return undefined;
  const point = integer.length + shift;
  const shifted = point >= digits.length
    ? `${digits}${"0".repeat(point - digits.length)}`
    : point > 0
      ? `${digits.slice(0, point)}.${digits.slice(point)}`
      : `0.${"0".repeat(-point)}${digits}`;
  if (shapeDigits(shifted).length > 8) return undefined;
  return `${sign}${shifted}`;
}

function canonicalStructuralDigitRun(raw: string): string | undefined {
  const canonical = canonicalShapeRaw(raw);
  if (canonical.length === 0) return "";
  return /^[0-9A-FА-Я]+$/u.test(canonical) ? canonical : undefined;
}

function x2MantissaShapeFactFromParts(radix: X2MantissaRadix, raw: string): X2ShapeFact | undefined {
  if (radix === "decimal") return decimalMantissaShapeFact(raw);
  if (radix === "hex") return `hex:${raw}:mantissa`;
  if (radix === "super") return `super:${raw}`;
  return undefined;
}

function canonicalShapeRaw(raw: string): string {
  return raw.trim().replace(/\s+/gu, "").replace(/,/gu, ".").toUpperCase();
}

function canonicalExponentShapeRaw(raw: string): string | undefined {
  const canonical = canonicalShapeRaw(raw);
  return /^-?[0-9]{0,2}$/u.test(canonical) ? canonical : undefined;
}

function shapeDigits(raw: string): string[] {
  const digits: string[] = [];
  for (const char of raw) {
    if (/^[0-9A-FА-Я]$/u.test(char)) digits.push(char);
  }
  return digits;
}

function decimalHasLeadingZero(raw: string, normalized: string | undefined): boolean {
  if (normalized === undefined) return false;
  return raw !== normalized && /^-?0[0-9]/u.test(raw);
}

function shapeHasLeadingZero(raw: string): boolean {
  return /^-?0[0-9A-FА-Я]/u.test(raw);
}

function significantShapeDigits(digits: readonly string[]): number {
  const first = digits.findIndex((digit) => digit !== "0");
  return first < 0 ? 1 : digits.length - first;
}

function preloadedConstantValueFacts(op: IrOp | undefined): Set<X2ValueFact> {
  const value = preloadedConstantLiteral(op);
  const decimal = value === undefined ? undefined : normalizePreloadedDecimalLiteral(value);
  return decimal === undefined ? new Set() : new Set([decimalValueFact(decimal, "normalized")]);
}

function preloadedConstantShapeFacts(op: IrOp | undefined): Set<X2ShapeFact> {
  const value = preloadedConstantLiteral(op);
  if (value === undefined) return new Set();
  const decimal = normalizePreloadedDecimalLiteral(value);
  if (decimal !== undefined) return new Set([decimalMantissaShapeFact(decimal)]);
  const structuralExponent = normalizePreloadedStructuralExponentShape(value);
  if (structuralExponent !== undefined) return new Set([structuralExponent]);
  const shape = normalizePreloadedShapeLiteral(value);
  if (shape === undefined) return new Set();
  if (/^F[A-F]$/u.test(shape)) return new Set<X2ShapeFact>([`super:${shape}`]);
  if (/[A-FА-Я]/u.test(shape)) return new Set<X2ShapeFact>([`hex:${shape}:mantissa`]);
  return new Set();
}

function preloadedConstantLiteral(op: IrOp | undefined): string | undefined {
  if (op === undefined || !("meta" in op)) return undefined;
  const match = /(?:^|;\s*)preload const\s+([^;]+)/iu.exec(op.meta.comment ?? "");
  if (match === null) return undefined;
  const literal = match[1]!
    .replace(/\s+\b(?:base|left|right|stack)\b.*$/iu, "")
    .trim();
  return literal.length === 0 ? undefined : literal;
}

function normalizePreloadedShapeLiteral(input: string): string | undefined {
  const normalized = input.trim().replace(/\s+/gu, "").replace(/,/gu, ".").toUpperCase();
  return normalized.length === 0 || normalized.length > 32 ? undefined : normalized;
}

function normalizePreloadedStructuralExponentShape(input: string): X2ShapeFact | undefined {
  const normalized = normalizePreloadedShapeLiteral(input);
  if (normalized === undefined) return undefined;
  const match = /^(.+)E([+-]?[0-9]{1,2})$/u.exec(normalized);
  if (match === null) return undefined;
  const mantissa = match[1]!;
  if (!/[A-FА-Я]/u.test(mantissa)) return undefined;
  const exponent = match[2]!.replace(/^\+/u, "");
  const mantissaFact = /^F[A-F]$/u.test(mantissa)
    ? `super:${mantissa}` as X2ShapeFact
    : `hex:${mantissa}:mantissa` as X2ShapeFact;
  return x2ExponentShapeFactFromMantissaFact(mantissaFact, exponent);
}

function normalizePreloadedDecimalLiteral(input: string): string | undefined {
  const normalized = input.trim().replace(/,/gu, ".").replace(/[Ее]/gu, "e");
  const match = /^(-?)(?:(\d+)(?:\.(\d*))?|\.(\d+))(?:e([+-]?\d+))?$/iu.exec(normalized);
  if (match === null) return undefined;
  const sign = match[1]!;
  const integer = match[2] ?? "0";
  const fraction = match[3] ?? match[4] ?? "";
  const exponent = match[5] === undefined ? 0 : Number(match[5]);
  if (!Number.isInteger(exponent) || Math.abs(exponent) > 64) return undefined;
  const digits = `${integer}${fraction}`.replace(/^0+/u, "") || "0";
  const scale = fraction.length - exponent;
  if (digits.length + Math.max(0, -scale) > 80) return undefined;
  const unsigned = scaledDecimalDigits(digits, scale);
  return unsigned === undefined ? undefined : normalizePlainDecimal(`${sign}${unsigned}`);
}

function scaledDecimalDigits(digits: string, scale: number): string | undefined {
  if (!/^\d+$/u.test(digits)) return undefined;
  if (scale <= 0) return `${digits}${"0".repeat(-scale)}`;
  const point = digits.length - scale;
  if (point > 0) return `${digits.slice(0, point)}.${digits.slice(point)}`;
  return `0.${"0".repeat(-point)}${digits}`;
}

function vpEntryMantissasFromValueFacts(values: X2ValueSet): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  for (const value of values) {
    const decimal = normalizedDecimalValueFromFact(value);
    if (decimal !== undefined) mantissas.add(decimal);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function vpEntryShapesFromShapeFacts(shapes: X2ShapeSet | undefined): X2ShapeSet | undefined {
  if (shapes === undefined) return undefined;
  const structural = structuralMantissaShapeFacts(shapes);
  return structural.size === 0 ? undefined : structural;
}

function cloneOptionalShapeSet(input: X2ShapeSet | undefined): Set<X2ShapeFact> {
  return canonicalShapeSet(input);
}

function joinOptionalShapeSets(
  current: X2ShapeSet | undefined,
  incoming: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  return intersectX2ShapeSets(current, incoming);
}

function sameOptionalShapeSet(
  left: X2ShapeSet | undefined,
  right: X2ShapeSet | undefined,
): boolean {
  const leftSet = canonicalShapeSet(left);
  const rightSet = canonicalShapeSet(right);
  if (leftSet.size !== rightSet.size) return false;
  for (const value of leftSet) {
    if (!rightSet.has(value)) return false;
  }
  return true;
}

function sameNonEmptyShapeSet(left: X2ShapeSet | undefined, right: X2ShapeSet | undefined): boolean {
  const leftSet = canonicalShapeSet(left);
  const rightSet = canonicalShapeSet(right);
  if (leftSet.size === 0 || leftSet.size !== rightSet.size) return false;
  for (const value of leftSet) {
    if (!rightSet.has(value)) return false;
  }
  return true;
}

function canonicalShapeSet(input: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const fact of input ?? []) output.add(x2CanonicalShapeFact(fact));
  return output;
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

function cloneOptionalValueSet(input: X2ValueSet | undefined): Set<X2ValueFact> {
  return input === undefined ? new Set() : new Set(input);
}

function sameNonEmptyStringSet(
  left: ReadonlySet<string> | undefined,
  right: ReadonlySet<string> | undefined,
): boolean {
  return left !== undefined &&
    right !== undefined &&
    left.size > 0 &&
    left.size === right.size &&
    sameStringSet(left, right);
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

function expressionValueFact(producerIndex: number): X2ValueFact {
  return `expr:${producerIndex}`;
}

function stableExpressionValueFact(opcode: string, source: string): X2ValueFact {
  return `expr-key:${opcode}(${source})`;
}

function stableExpressionSourceKey(fact: X2ValueFact): string | undefined {
  if (fact.startsWith("reg:")) return fact;
  if (fact.startsWith("expr-key:")) return fact;
  if (normalizedDecimalValueFromFact(fact) !== undefined) return fact;
  return undefined;
}

function stableExpressionSourceKeys(
  values: X2ValueSet | undefined,
  shapes: X2ShapeSet | undefined,
): Set<string> {
  const keys = new Set<string>();
  for (const fact of values ?? []) {
    const key = stableExpressionSourceKey(fact);
    if (key !== undefined) keys.add(key);
  }
  for (const fact of structuralRestoreShapeFacts(canonicalStructuralShapeFacts(shapes))) {
    keys.add(stableStructuralExpressionSourceKey(fact));
  }
  return keys;
}

function stableStructuralExpressionSourceKey(fact: X2ShapeFact): string {
  return `shape:${x2CanonicalShapeFact(fact)}`;
}

function decimalValueFact(value: string, flavor: "normalized" | "unnormalized"): X2ValueFact {
  return `decimal:${value}:${flavor}`;
}

function normalizedDecimalValueFromFact(fact: X2ValueFact): string | undefined {
  return /^decimal:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):normalized$/u.exec(fact)?.[1];
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
      const fact = x2ExponentShapeFactFromMantissaFact(decimalMantissaShapeFact(mantissa), exponent);
      if (fact !== undefined) shapes.add(fact);
    }
  }
  return shapes;
}

function structuralExponentEntryFromVpEntryShapes(shapes: X2ShapeSet): X2StructuralEntryState {
  const mantissa = structuralMantissaShapeFacts(shapes);
  return mantissa.size === 0
    ? { kind: "unknown" }
    : { kind: "exponent", mantissa, exponent: new Set([""]) };
}

function structuralExponentEntryShapeFacts(input: Extract<X2StructuralEntryState, { kind: "exponent" }>): Set<X2ShapeFact> {
  const shapes = new Set<X2ShapeFact>();
  for (const mantissa of input.mantissa) {
    for (const exponent of input.exponent) {
      const fact = x2ExponentShapeFactFromMantissaFact(mantissa, exponent);
      if (fact !== undefined && x2ShapeFactSafety(fact) === "structuralOnly") shapes.add(fact);
    }
  }
  return shapes;
}

function normalizedExponentEntryValue(mantissa: string, exponent: string): string | undefined {
  const exponentMatch = /^(-?)([0-9]{1,2})$/u.exec(exponent);
  const mantissaParts = exponentMantissaDecimalParts(mantissa);
  if (mantissaParts === undefined || exponentMatch === null) return undefined;
  const exponentSign = exponentMatch[1]!;
  const shift = Number(exponentMatch[2]!);
  if (!Number.isInteger(shift)) return undefined;
  const scale = exponentSign === "-"
    ? mantissaParts.scale + shift
    : mantissaParts.scale - shift;
  const unsigned = scaledDecimalDigits(mantissaParts.digits, scale);
  const normalized = unsigned === undefined ? undefined : normalizePlainDecimal(`${mantissaParts.sign}${unsigned}`);
  if (normalized === undefined || significantDecimalDigits(normalized) > 8) return undefined;
  return normalized;
}

function exponentMantissaDecimalParts(
  mantissa: string,
): { readonly sign: "" | "-"; readonly digits: string; readonly scale: number } | undefined {
  const integer = /^(-?)([0-9]{1,8})$/u.exec(mantissa);
  if (integer !== null) {
    const digits = effectiveExponentMantissaDigits(integer[2]!);
    return { sign: integer[1]! === "-" ? "-" : "", digits, scale: 0 };
  }
  const fractional = /^(-?)([0-9]{1,8})\.([0-9]+)$/u.exec(mantissa);
  if (fractional === null) return undefined;
  const integerDigits = fractional[2]!;
  const fractionDigits = fractional[3]!;
  if (integerDigits.length + fractionDigits.length > 8) return undefined;
  return {
    sign: fractional[1]! === "-" ? "-" : "",
    digits: `${integerDigits}${fractionDigits}`,
    scale: fractionDigits.length,
  };
}

function effectiveExponentMantissaDigits(rawDigits: string): string {
  const stripped = rawDigits.replace(/^0+/u, "");
  if (stripped.length > 0) return stripped;
  return `1${"0".repeat(Math.max(0, rawDigits.length - 1))}`;
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

function normalizeDecimalMantissaEntry(raw: string): string | undefined {
  const match = /^(-?)([0-9]{1,8})(?:\.([0-9]*))?$/u.exec(raw);
  if (match === null) return undefined;
  if (decimalMantissaDigitCount(raw) > 8) return undefined;
  const sign = match[1]!;
  const integer = match[2]!;
  const fraction = match[3];
  const normalized = fraction === undefined || fraction.length === 0
    ? normalizePlainDecimal(`${sign}${integer}`)
    : normalizePlainDecimal(`${sign}${integer}.${fraction}`);
  return normalized;
}

function decimalMantissaDigitCount(raw: string): number {
  return raw.replace(/^-/, "").replace(".", "").length;
}

function x2DecimalEntryFact(raw: string): X2ValueFact | undefined {
  const normalized = normalizeDecimalMantissaEntry(raw);
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
  const normalized = normalizeDecimalMantissaEntry(raw);
  if (normalized === undefined || normalized === "0") return "0";
  return raw.startsWith("-") ? raw.slice(1) : `-${raw}`;
}

function signChangeClosedDecimalState(
  input: X2ValueDataflowState,
  producerIndex: number | undefined,
): X2ValueDataflowState | undefined {
  const shaped = signChangedVpEntryMantissas(input);
  if (shaped !== undefined) {
    return x2ValueStateFromMantissaShapes(shaped, input.memory, input.shapeMemory, input.y, input.yShape);
  }
  const shapeBacked = signChangedClosedShapeMantissas(input);
  if (shapeBacked !== undefined) {
    return x2ValueStateFromMantissaShapes(shapeBacked, input.memory, input.shapeMemory, input.y, input.yShape);
  }
  const structuralShapes = signChangedClosedStructuralShapeFacts(input);
  if (structuralShapes !== undefined) return x2ValueStateFromStructuralShapes(
    structuralShapes,
    input.memory,
    input.shapeMemory,
    input.y,
    input.yShape,
  );

  const values = new Set<X2ValueFact>();
  const opaque = producerIndex === undefined ? SAME_UNKNOWN_VALUE : expressionValueFact(producerIndex);
  if (input.x.has(SAME_UNKNOWN_VALUE) && input.x2.has(SAME_UNKNOWN_VALUE)) {
    values.add(opaque);
  }
  for (const fact of input.x2) {
    if (!input.x.has(fact)) continue;
    const decimal = normalizedDecimalValueFromFact(fact);
    if (decimal === undefined && isOpaqueSharedValueFact(fact)) {
      values.add(opaque);
      const key = stableExpressionSourceKey(fact);
      if (key !== undefined) values.add(stableExpressionValueFact("0B", key));
      continue;
    }
    if (decimal === undefined) continue;
    values.add(decimalValueFact(signChangedNormalizedDecimalValue(decimal), "normalized"));
  }
  if (values.size === 0) return undefined;
  const shapes = new Set<X2ShapeFact>();
  for (const value of values) {
    const decimal = normalizedDecimalValueFromFact(value);
    if (decimal !== undefined) shapes.add(decimalMantissaShapeFact(decimal));
  }
  return {
    x: values,
    y: cloneOptionalValueSet(input.y),
    x2: new Set(values),
    xShape: shapes,
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: new Set(shapes),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    vpEntryMantissa: vpEntryMantissasFromValueFacts(values),
    memory: input.memory,
    shapeMemory: input.shapeMemory,
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
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind !== "mantissa" || model.radix !== "decimal" || model.safety !== "dotSafeDecimal") continue;
    const raw = model.canonical;
    const normalized = normalizeDecimalMantissaEntry(raw);
    if (normalized === undefined || model.normalizedDecimal === undefined) continue;
    if (input.xShape?.has(decimalMantissaShapeFact(normalized)) !== true) continue;
    const signed = signChangedMantissaShape(raw);
    if (signed === undefined) return undefined;
    mantissas.add(signed);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function signChangedClosedStructuralShapeFacts(input: X2ValueDataflowState): ReadonlySet<X2ShapeFact> | undefined {
  const shapes = x2SignChangedSharedStructuralShapeFacts(input.xShape, input.x2Shape);
  return shapes.size === 0 ? undefined : shapes;
}

function signChangedStructuralMantissaShapeFact(
  fact: X2ShapeFact,
): X2ShapeFact {
  return x2MantissaSignChangedShapeFact(fact) ?? fact;
}

function toggleRawSign(raw: string): string {
  return raw.startsWith("-") ? raw.slice(1) : `-${raw}`;
}

function toggleExponentSign(raw: string): string {
  return raw.startsWith("-") ? raw.slice(1) : `-${raw}`;
}

function sharedNormalizedDecimalMantissas(input: Pick<X2ValueDataflowState, "x" | "x2">): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  for (const fact of input.x2) {
    if (!input.x.has(fact)) continue;
    const decimal = normalizedDecimalValueFromFact(fact);
    if (decimal !== undefined) mantissas.add(decimal);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function sharedStructuralShapeFacts(
  input: Pick<X2ValueDataflowState, "xShape" | "x2Shape">,
): X2ShapeSet | undefined {
  const xRestoreShapes = structuralRestoreShapeFacts(canonicalStructuralShapeFacts(input.xShape));
  const x2RestoreShapes = structuralRestoreShapeFacts(canonicalStructuralShapeFacts(input.x2Shape));
  const shapes = new Set<X2ShapeFact>();
  for (const fact of x2RestoreShapes) {
    if (!xRestoreShapes.has(fact)) continue;
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind === "mantissa" && (model.radix === "hex" || model.radix === "super")) shapes.add(fact);
  }
  return shapes.size === 0 ? undefined : shapes;
}

function isEmptyPlainOp(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return op.opcode >= 0x54 && op.opcode <= 0x56;
}

function signChangedVpEntryMantissas(input: X2ValueDataflowState): ReadonlySet<string> | undefined {
  if (input.vpEntryMantissa !== undefined) return signChangedMantissaShapes(input.vpEntryMantissa);
  const mantissas = new Set<string>();
  for (const fact of input.x2) {
    if (!input.x.has(fact)) continue;
    const decimal = normalizedDecimalValueFromFact(fact);
    if (decimal === undefined) continue;
    const signed = signChangedMantissaShape(decimal);
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
  const normalized = normalizeDecimalMantissaEntry(raw);
  if (normalized === undefined) return undefined;
  if (normalized === "0") return "-0";
  return raw.startsWith("-") ? raw.slice(1) : `-${raw}`;
}

function x2ValueStateFromMantissaShapes(
  mantissas: ReadonlySet<string>,
  memory: X2ValueMemory | undefined = undefined,
  shapeMemory: X2ShapeMemory | undefined = undefined,
  y: X2ValueSet | undefined = undefined,
  yShape: X2ShapeSet | undefined = undefined,
): X2ValueDataflowState | undefined {
  const x = new Set<X2ValueFact>();
  const x2 = new Set<X2ValueFact>();
  const xShape = new Set<X2ShapeFact>();
  const x2Shape = new Set<X2ShapeFact>();
  for (const raw of mantissas) {
    const normalized = normalizeDecimalMantissaEntry(raw);
    const x2Fact = x2DecimalEntryFact(raw);
    if (normalized === undefined || x2Fact === undefined) return undefined;
    x.add(decimalValueFact(normalized, "normalized"));
    x2.add(x2Fact);
    xShape.add(decimalMantissaShapeFact(normalized));
    x2Shape.add(decimalMantissaShapeFact(raw));
  }
  return {
    x,
    y: cloneOptionalValueSet(y),
    x2,
    xShape,
    yShape: cloneOptionalShapeSet(yShape),
    x2Shape,
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: noneX2StructuralEntryState(),
    vpEntryMantissa: mantissas,
    memory,
    shapeMemory,
  };
}

function x2ValueStateFromStructuralShapes(
  shapes: ReadonlySet<X2ShapeFact>,
  memory: X2ValueMemory | undefined = undefined,
  shapeMemory: X2ShapeMemory | undefined = undefined,
  y: X2ValueSet | undefined = undefined,
  yShape: X2ShapeSet | undefined = undefined,
): X2ValueDataflowState {
  const shapeSet = canonicalStructuralShapeFacts(shapes);
  return {
    x: new Set(),
    y: cloneOptionalValueSet(y),
    x2: new Set(),
    xShape: shapeSet,
    yShape: cloneOptionalShapeSet(yShape),
    x2Shape: new Set(shapeSet),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: noneX2StructuralEntryState(),
    vpEntryShape: vpEntryShapesFromShapeFacts(shapeSet),
    memory,
    shapeMemory,
  };
}

function x2ValueStateFromStructuralExponentEntry(
  input: X2StructuralEntryState,
  memory: X2ValueMemory | undefined = undefined,
  shapeMemory: X2ShapeMemory | undefined = undefined,
  y: X2ValueSet | undefined = undefined,
  yShape: X2ShapeSet | undefined = undefined,
): X2ValueDataflowState {
  if (input.kind !== "exponent") {
    return {
      x: new Set(),
      y: cloneOptionalValueSet(y),
      x2: new Set(),
      xShape: new Set(),
      yShape: cloneOptionalShapeSet(yShape),
      x2Shape: new Set(),
      entry: closedX2EntryState(),
      vpContext: noneX2VpContextState(),
      structuralEntry: cloneX2StructuralEntryState(input),
      structuralVpContext: { kind: "unknown" },
      memory,
      shapeMemory,
    };
  }
  const shapes = structuralExponentEntryShapeFacts(input);
  return {
    x: new Set(),
    y: cloneOptionalValueSet(y),
    x2: new Set(),
    xShape: new Set(shapes),
    yShape: cloneOptionalShapeSet(yShape),
    x2Shape: new Set(shapes),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    structuralEntry: cloneX2StructuralEntryState(input),
    structuralVpContext: x2StructuralContextFromEntry(input),
    memory,
    shapeMemory,
  };
}

function x2ValueStateFromExponentEntry(
  input: X2EntryState,
  memory: X2ValueMemory | undefined = undefined,
  shapeMemory: X2ShapeMemory | undefined = undefined,
  y: X2ValueSet | undefined = undefined,
  yShape: X2ShapeSet | undefined = undefined,
): X2ValueDataflowState {
  if (input.kind !== "exponent") {
    return {
      x: new Set(),
      y: cloneOptionalValueSet(y),
      x2: new Set(),
      xShape: new Set(),
      yShape: cloneOptionalShapeSet(yShape),
      x2Shape: new Set(),
      entry: cloneX2EntryState(input),
      vpContext: { kind: "unknown" },
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: noneX2StructuralEntryState(),
      memory,
      shapeMemory,
    };
  }
  const values = closedExponentEntryDecimalFacts(input);
  const shapes = closedExponentEntryShapeFacts(input);
  return {
    x: new Set(values),
    y: cloneOptionalValueSet(y),
    // Active exponent entry already has a normalized visible X value, but
    // hidden X2 is still a ВП-entry shape. A following `.` may signal ЕГГ0Г,
    // so dot-safe X2 value facts appear only after a closing X2 sync.
    x2: new Set(),
    xShape: new Set(shapes),
    yShape: cloneOptionalShapeSet(yShape),
    x2Shape: new Set(shapes),
    entry: cloneX2EntryState(input),
    vpContext: x2VpContextFromExponentEntry(input),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: noneX2StructuralEntryState(),
    memory,
    shapeMemory,
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

function noneX2StructuralEntryState(): X2StructuralEntryState {
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

function cloneX2StructuralEntryState(input: X2StructuralEntryState | undefined): X2StructuralEntryState {
  if (input === undefined || input.kind === "none" || input.kind === "unknown") {
    return input ?? noneX2StructuralEntryState();
  }
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

function x2StructuralContextFromEntry(
  input: Extract<X2StructuralEntryState, { kind: "exponent" }>,
): X2StructuralEntryState {
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
      y: cloneOptionalValueSet(input.y),
      x2: new Set(closedExponentValues),
      xShape: new Set(closedExponentShapes),
      yShape: cloneOptionalShapeSet(input.yShape),
      x2Shape: new Set(closedExponentShapes),
      entry: closedX2EntryState(),
      vpContext: cloneX2VpContextState(input.vpContext),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
      vpEntryMantissa: vpEntryMantissasFromValueFacts(closedExponentValues),
      vpEntryShape: vpEntryShapesFromShapeFacts(closedExponentShapes),
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  const structuralEntry = input.structuralEntry ?? noneX2StructuralEntryState();
  if (structuralEntry.kind === "exponent") {
    const shapes = structuralExponentEntryShapeFacts(structuralEntry);
    return {
      x: new Set(),
      y: cloneOptionalValueSet(input.y),
      x2: new Set(),
      xShape: new Set(shapes),
      yShape: cloneOptionalShapeSet(input.yShape),
      x2Shape: new Set(shapes),
      entry: closedX2EntryState(),
      vpContext: cloneX2VpContextState(input.vpContext),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: x2StructuralContextFromEntry(structuralEntry),
      vpEntryShape: undefined,
      memory: input.memory,
      shapeMemory: input.shapeMemory,
    };
  }
  return {
    x: new Set(input.x),
    y: cloneOptionalValueSet(input.y),
    x2: new Set(input.x2),
    xShape: cloneOptionalShapeSet(input.xShape),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: closedX2EntryState(),
    vpContext: cloneX2VpContextState(input.vpContext),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
    vpEntryMantissa: cloneOptionalStringSet(input.vpEntryMantissa),
    vpEntryShape: cloneOptionalShapeSet(input.vpEntryShape),
    memory: input.memory,
    shapeMemory: input.shapeMemory,
  };
}

function advanceDecimalDigitEntry(input: X2EntryState, digit: string): X2EntryState {
  if (input.kind === "unknown") return { kind: "unknown" };
  if (input.kind === "exponent") return advanceExponentDigitEntry(input, digit);
  const source = input.kind === "closed" ? new Set([""]) : input.raw;
  const raw = new Set<string>();
  for (const prefix of source) {
    const next = `${prefix}${digit}`;
    if (decimalMantissaDigitCount(next) > 8) return { kind: "unknown" };
    raw.add(next);
  }
  return { kind: "open", raw };
}

function advanceDecimalPointEntry(input: Extract<X2EntryState, { kind: "open" }>): X2EntryState {
  const raw = new Set<string>();
  for (const prefix of input.raw) {
    if (prefix.includes(".")) return { kind: "unknown" };
    raw.add(`${prefix}.`);
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

function advanceStructuralExponentDigitEntry(
  input: Extract<X2StructuralEntryState, { kind: "exponent" }>,
  digit: string,
): X2StructuralEntryState {
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

function signChangeStructuralExponentEntry(
  input: Extract<X2StructuralEntryState, { kind: "exponent" }>,
): X2StructuralEntryState {
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

function joinX2StructuralEntryStates(
  currentInput: X2StructuralEntryState | undefined,
  incomingInput: X2StructuralEntryState | undefined,
): X2StructuralEntryState {
  const current = currentInput ?? noneX2StructuralEntryState();
  const incoming = incomingInput ?? noneX2StructuralEntryState();
  if (current.kind === "unknown" || incoming.kind === "unknown") return { kind: "unknown" };
  if (current.kind === "none" || incoming.kind === "none" || current.kind !== incoming.kind) {
    return current.kind === incoming.kind ? noneX2StructuralEntryState() : { kind: "unknown" };
  }
  const mantissa = joinOptionalShapeSets(current.mantissa, incoming.mantissa);
  const exponent = joinStringSets(current.exponent, incoming.exponent);
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

function sameX2StructuralEntryState(
  leftInput: X2StructuralEntryState | undefined,
  rightInput: X2StructuralEntryState | undefined,
): boolean {
  const left = leftInput ?? noneX2StructuralEntryState();
  const right = rightInput ?? noneX2StructuralEntryState();
  if (left.kind !== right.kind) return false;
  if (left.kind !== "exponent" || right.kind !== "exponent") return true;
  return sameOptionalShapeSet(left.mantissa, right.mantissa) && sameStringSet(left.exponent, right.exponent);
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
  const stable = invalidateRegisterDependentX2ValueState(input, register, trackRegisterMemory);
  return {
    ...stable,
    memory: trackRegisterMemory ? deleteX2ValueMemory(stable.memory, register) : undefined,
    shapeMemory: trackRegisterMemory ? deleteX2ShapeMemory(stable.shapeMemory, register) : undefined,
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
          const effect = analyzeX2StackEffect(op);
          if (effect.stackEffect === "unknown" || effect.stackExposes) return true;
          if (effect.stackEffect === "barrier") return false;
          if (effect.stackShifts) {
            depth = shiftDifference(depth);
            break;
          }
          if (effect.stackEffect === "consume-y-drop") {
            if (depth === 1) return true;
            depth = dropDifference(depth);
            break;
          }
          if (effect.stackEffect === "consume-y-keep") {
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

export function x2SyncCanExposeContextSensitiveRestore(
  ops: readonly IrOp[],
  syncIndex: number,
  options: X2RestoreExposureOptions = {},
): boolean {
  const labels = labelIndexes(ops);
  const addresses = addressIndexes(ops);
  const visited = new Set<string>();
  const visit = (
    start: number,
    returnStack: readonly number[] = [],
    sawExecutableAfterSync = false,
  ): boolean => {
    for (let i = start; i < ops.length; i += 1) {
      const key = `${i}:${sawExecutableAfterSync ? 1 : 0}:${returnStack.join(",")}`;
      if (visited.has(key)) return false;
      visited.add(key);

      const op = ops[i]!;
      if (hasRewriteBarrier(op)) return true;

      switch (op.kind) {
        case "plain": {
          const effect = plainX2Effect(op);
          if (effect === "unknown") return true;
          if (effect === "restores" && isContextSensitiveX2Restore(op)) {
            const redundantSync =
              options.redundantSyncRegister !== undefined ||
              options.redundantSyncValue === true ||
              options.redundantSyncShape === true;
            return redundantSync && sawExecutableAfterSync ? false : true;
          }
          if (effect === "restores") return false;
          if (effect === "affects") return false;
          sawExecutableAfterSync = true;
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
          sawExecutableAfterSync = true;
          break;
      }
    }
    return false;
  };
  return visit(syncIndex + 1);
}

export function removingRecallCanExposeX2Restore(
  ops: readonly IrOp[],
  recallIndex: number,
  options: X2RestoreExposureOptions = {},
): boolean {
  return x2SyncCanExposeContextSensitiveRestore(ops, recallIndex, options);
}
