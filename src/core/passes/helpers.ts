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
      readonly closedDecimalDisplay?: X2ShapeFact | undefined;
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
const STACK_COPY_Y_TO_X_OPCODE = 0x3e;
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
const EXACT_DECIMAL_DISPLAY_SHAPE_OPCODES = new Set<number>([
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
  0x31, // К |x|
  0x32, // К ЗН
  0x34, // К [x]
]);
const EXACT_DECIMAL_BINARY_MANTISSA_SHAPE_OPCODES = new Set<number>([
  0x10, // +
  0x11, // -
  0x12, // *
  0x13, // /
  0x24, // F x^y
  0x36, // К max
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
  readonly vpEntryMantissaTransient?: true | undefined;
  readonly vpEntrySignMantissa?: ReadonlySet<string> | undefined;
  readonly vpEntryShape?: X2ShapeSet | undefined;
  readonly vpEntrySignShape?: X2ShapeSet | undefined;
  readonly vpEntryShapeTransient?: true | undefined;
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
  readonly redundantSyncDisplayValue?: boolean | undefined;
  readonly redundantSyncShape?: boolean | undefined;
  readonly redundantSyncVpShape?: boolean | undefined;
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
  readonly x2SyncDisplayValue?: true | undefined;
  readonly x2SyncShape?: true | undefined;
  readonly x2SyncVpShape?: true | undefined;
}

export interface RecallRemovalAnalysis {
  readonly register: RegisterName;
  readonly valueProof?: RecallValueProof | undefined;
  readonly redundantSyncRegister?: RegisterName | undefined;
  readonly redundantSyncValue: boolean;
  readonly redundantSyncDisplayValue: boolean;
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

const directReturnAnalysisContextCache = new WeakMap<readonly IrOp[], DirectReturnAnalysisContext>();
const x2StackXAndX2ReturnMemo = new WeakMap<readonly IrOp[], Map<number, boolean>>();
const x2StrictStackReturnMemo = new WeakMap<readonly IrOp[], Map<number, boolean>>();
const x2RestoreGapReturnMemo = new WeakMap<readonly IrOp[], Map<number, boolean>>();

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
  const cached = directReturnAnalysisContextCache.get(ops);
  if (cached !== undefined) return cached;
  const context = {
    labelEntries: computeLabelEntryIndexes(ops),
    labels: labelIndexes(ops),
    addresses: addressIndexes(ops),
  };
  directReturnAnalysisContextCache.set(ops, context);
  return context;
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

export function knownReturnCallReturnsThroughNestedTransparentRange(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
  isTransparent: (op: IrOp) => boolean,
): boolean {
  const memo = new Map<number, boolean>();
  const active = new Set<number>();
  return nestedReturnCallRangeIsTransparent(ops, call, context, isTransparent, memo, active);
}

function nestedReturnCallRangeIsTransparent(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
  isTransparent: (op: IrOp) => boolean,
  memo: Map<number, boolean>,
  active: Set<number>,
): boolean {
  const targetIndex = knownReturnCallTargetIndex(call, context);
  if (targetIndex === undefined) return false;
  const cached = memo.get(targetIndex);
  if (cached !== undefined) return cached;
  if (active.has(targetIndex)) return false;

  active.add(targetIndex);
  const result = nestedLinearReturnRangeIsTransparent(
    ops,
    targetIndex,
    context,
    isTransparent,
    memo,
    active,
  );
  active.delete(targetIndex);
  memo.set(targetIndex, result);
  return result;
}

function nestedLinearReturnRangeIsTransparent(
  ops: readonly IrOp[],
  targetIndex: number,
  context: DirectReturnAnalysisContext,
  isTransparent: (op: IrOp) => boolean,
  memo: Map<number, boolean>,
  active: Set<number>,
): boolean {
  const startIndex = ops[targetIndex]?.kind === "label" ? targetIndex + 1 : targetIndex;
  for (let index = startIndex; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      if (context.labelEntries.has(index)) return false;
      continue;
    }
    if (hasRewriteBarrier(op)) return false;
    if (op.kind === "return") return true;
    if (isKnownReturnCallOp(op)) {
      if (isDisplayFocusSensitive(op)) return false;
      if (!nestedReturnCallRangeIsTransparent(ops, op, context, isTransparent, memo, active)) {
        return false;
      }
      continue;
    }
    if (!isTransparent(op)) return false;
  }
  return false;
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
  const constants = plainProducesStableConstantExpressionValues(op);
  if (constants.size > 0) return constants;
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
  const output = plainProducesConcreteDecimalValues(op, x, xShape);
  const opcode = op.opcode.toString(16).toUpperCase().padStart(2, "0");
  if (info.stackEffect === "preserves") {
    for (const key of stableExpressionSourceKeys(x, xShape)) {
      if (stableExpressionKeyHasConcreteDecimalResult(op, key)) continue;
      output.add(stableExpressionValueFact(opcode, key));
    }
  } else if (info.stackEffect === "consume-y-drop" || info.stackEffect === "consume-y-keep") {
    for (const fact of plainProducesConcreteBinaryDecimalValues(op, y, x, yShape, xShape)) output.add(fact);
    for (const yKey of stableExpressionSourceKeys(y, yShape)) {
      for (const xKey of stableExpressionSourceKeys(x, xShape)) {
        if (stableBinaryExpressionKeyHasConcreteDecimalResult(op, yKey, xKey)) continue;
        output.add(stableBinaryExpressionValueFact(op, opcode, yKey, xKey));
      }
    }
  }
  return output;
}

function stableExpressionKeyHasConcreteDecimalResult(
  op: Extract<IrOp, { kind: "plain" }>,
  key: string,
): boolean {
  return plainProducesConcreteDecimalValues(
    op,
    stableExpressionKeyValueSet(key),
    stableExpressionKeyShapeSet(key),
  ).size > 0;
}

function stableBinaryExpressionKeyHasConcreteDecimalResult(
  op: Extract<IrOp, { kind: "plain" }>,
  yKey: string,
  xKey: string,
): boolean {
  return plainProducesConcreteBinaryDecimalValues(
    op,
    stableExpressionKeyValueSet(yKey),
    stableExpressionKeyValueSet(xKey),
    stableExpressionKeyShapeSet(yKey),
    stableExpressionKeyShapeSet(xKey),
  ).size > 0;
}

function plainProducesConcreteDecimalValues(
  op: Extract<IrOp, { kind: "plain" }>,
  x: X2ValueSet | undefined,
  xShape: X2ShapeSet | undefined = undefined,
): Set<X2ValueFact> {
  const output = new Set<X2ValueFact>();
  const effectiveXShape = shapeSetWithStableExpressionValueShapes(xShape, x);
  if (
    op.opcode !== 0x15 &&
    op.opcode !== 0x16 &&
    op.opcode !== 0x17 &&
    op.opcode !== 0x18 &&
    op.opcode !== 0x19 &&
    op.opcode !== 0x1a &&
    op.opcode !== 0x1b &&
    op.opcode !== 0x1c &&
    op.opcode !== 0x1d &&
    op.opcode !== 0x1e &&
    op.opcode !== 0x21 &&
    op.opcode !== 0x22 &&
    op.opcode !== 0x23 &&
    op.opcode !== 0x26 &&
    op.opcode !== 0x2a &&
    op.opcode !== 0x30 &&
    op.opcode !== 0x31 &&
    op.opcode !== 0x32 &&
    op.opcode !== 0x33 &&
    op.opcode !== 0x34 &&
    op.opcode !== 0x35 &&
    op.opcode !== 0x3a
  ) return output;
  for (const fact of x ?? []) {
    const value = computationDecimalValueFromFact(fact);
    const concrete = value === undefined
      ? undefined
      : concreteDecimalUnaryValue(op.opcode, value);
    if (concrete !== undefined) output.add(decimalValueFact(concrete, "normalized"));
  }
  for (const value of x2ShapeSetRestoredVisibleDecimals(effectiveXShape)) {
    const concrete = concreteDecimalUnaryValue(op.opcode, value);
    if (concrete !== undefined) output.add(decimalValueFact(concrete, "normalized"));
  }
  for (const value of plainProducesConcreteStructuralUnaryDecimalValues(op, effectiveXShape)) {
    output.add(decimalValueFact(value, "normalized"));
  }
  return output;
}

function plainProducesConcreteDecimalShapeFacts(
  op: Extract<IrOp, { kind: "plain" }>,
  x: X2ValueSet | undefined,
  xShape: X2ShapeSet | undefined = undefined,
): Set<X2ShapeFact> {
  const output = plainProducesStableConstantShapeFacts(op);
  const effectiveXShape = shapeSetWithStableExpressionValueShapes(xShape, x);
  for (const fact of x ?? []) {
    const value = computationDecimalValueFromFact(fact);
    const concrete = value === undefined ? undefined : concreteDecimalUnaryDisplayShapeFact(op.opcode, value);
    if (concrete !== undefined) output.add(concrete);
  }
  for (const value of x2ShapeSetRestoredVisibleDecimals(effectiveXShape)) {
    const concrete = concreteDecimalUnaryDisplayShapeFact(op.opcode, value);
    if (concrete !== undefined) output.add(concrete);
  }
  for (const fact of plainProducesConcreteStructuralUnaryDecimalShapeFacts(op, effectiveXShape)) {
    if (fact !== undefined) output.add(fact);
  }
  return output;
}

function concreteDecimalUnaryDisplayShapeFact(opcode: number, value: string): X2ShapeFact | undefined {
  if (opcode === 0x35) return decimalFractionPartShapeFact(value);
  if (!EXACT_DECIMAL_DISPLAY_SHAPE_OPCODES.has(opcode)) return undefined;
  const concrete = concreteDecimalUnaryValue(opcode, value);
  return concrete === undefined ? undefined : exactDecimalDisplayShapeFact(concrete);
}

function exactPlainIntegerDecimalMantissaShapeFact(value: string): X2ShapeFact | undefined {
  const normalized = normalizePlainDecimal(value);
  if (normalized === undefined || !/^-?[0-9]+$/u.test(normalized)) return undefined;
  if (decimalMantissaDigitCount(normalized) > 8) return undefined;
  return decimalMantissaShapeFact(normalized);
}

function plainProducesConcreteBinaryDecimalValues(
  op: Extract<IrOp, { kind: "plain" }>,
  y: X2ValueSet | undefined,
  x: X2ValueSet | undefined,
  yShape: X2ShapeSet | undefined = undefined,
  xShape: X2ShapeSet | undefined = undefined,
): Set<X2ValueFact> {
  const output = new Set<X2ValueFact>();
  const effectiveYShape = shapeSetWithStableExpressionValueShapes(yShape, y);
  const effectiveXShape = shapeSetWithStableExpressionValueShapes(xShape, x);
  if (
    (op.opcode < 0x10 || op.opcode > 0x13) &&
    op.opcode !== 0x24 &&
    op.opcode !== 0x36 &&
    op.opcode !== 0x37 &&
    op.opcode !== 0x38 &&
    op.opcode !== 0x39
  ) return output;
  for (const yValue of normalizedDecimalValues(y, effectiveYShape)) {
    for (const xValue of normalizedDecimalValues(x, effectiveXShape)) {
      const concrete = concreteDecimalBinaryValue(op.opcode, yValue, xValue);
      if (concrete !== undefined) output.add(decimalValueFact(concrete, "normalized"));
    }
  }
  for (const fact of plainProducesStructuralBinaryDecimalValues(op, y, x, effectiveYShape, effectiveXShape)) {
    output.add(fact);
  }
  return output;
}

interface StructuralBitwiseOperand {
  readonly nibbles: readonly number[];
  readonly structural: boolean;
}

function plainProducesConcreteUnaryShapeFacts(
  op: Extract<IrOp, { kind: "plain" }>,
  x: X2ValueSet | undefined,
  xShape: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  const effectiveXShape = shapeSetWithStableExpressionValueShapes(xShape, x);
  if (op.opcode === 0x31) {
    for (const fact of structuralAbsMantissaShapeFacts(effectiveXShape)) output.add(fact);
    return output;
  }
  if (op.opcode !== 0x3a) return output;
  for (const operand of bitwiseOperandsFromValuesAndShapes(x, effectiveXShape)) {
    const result = structuralBitwiseNotMantissaShapeFact(operand);
    if (result !== undefined) output.add(result);
  }
  return output;
}

function structuralAbsMantissaShapeFacts(shapes: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const fact of structuralRestoreShapeFacts(canonicalStructuralShapeFacts(shapes))) {
    const result = structuralAbsMantissaShapeFact(fact);
    if (result !== undefined) output.add(result);
  }
  return output;
}

function structuralAbsMantissaShapeFact(fact: X2ShapeFact): X2ShapeFact | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "mantissa" || (model.radix !== "hex" && model.radix !== "super")) return undefined;
  const unsigned = model.sign === "-" ? model.canonical.slice(1) : model.canonical;
  return x2MantissaShapeFactFromModel(structuralMantissaDataModel(model.radix, unsigned, "structuralOnly"));
}

function plainProducesConcreteStructuralUnaryDecimalValues(
  op: Extract<IrOp, { kind: "plain" }>,
  xShape: X2ShapeSet | undefined,
): Set<string> {
  const output = new Set<string>();
  if (op.opcode !== 0x22 && op.opcode !== 0x32 && op.opcode !== 0x3a) return output;
  for (const fact of structuralRestoreShapeFacts(canonicalStructuralShapeFacts(xShape))) {
    const value = op.opcode === 0x22
      ? structuralHexSquareDecimalValue(fact)
      : op.opcode === 0x32
        ? structuralHexSignDecimalValue(fact)
        : structuralBitwiseNotDecimalValueFromFact(fact);
    if (value !== undefined) output.add(value);
  }
  return output;
}

function plainProducesConcreteStructuralUnaryDecimalShapeFacts(
  op: Extract<IrOp, { kind: "plain" }>,
  xShape: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  if (op.opcode !== 0x22 && op.opcode !== 0x32 && op.opcode !== 0x3a) return output;
  for (const fact of structuralRestoreShapeFacts(canonicalStructuralShapeFacts(xShape))) {
    const result = op.opcode === 0x22
      ? structuralHexSquareDecimalDisplayShapeFact(fact)
      : op.opcode === 0x32
        ? exactPlainIntegerDecimalMantissaShapeFact(structuralHexSignDecimalValue(fact) ?? "")
        : structuralBitwiseNotDecimalDisplayShapeFact(fact);
    if (result !== undefined) output.add(result);
  }
  return output;
}

function structuralHexSignDecimalValue(fact: X2ShapeFact): string | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "mantissa" || model.radix !== "hex") return undefined;
  for (const digit of model.digits) {
    const value = structuralHexNibbleValue(digit);
    if (value === undefined) return undefined;
    if (value === 0) continue;
    if (value === 15) return undefined;
    return model.sign === "-" ? "-1" : "1";
  }
  return "0";
}

function structuralHexSquareDecimalValue(fact: X2ShapeFact): string | undefined {
  return structuralHexSquareDecimalProduct(fact)?.value;
}

function structuralHexSquareDecimalDisplayShapeFact(fact: X2ShapeFact): X2ShapeFact | undefined {
  const product = structuralHexSquareDecimalProduct(fact);
  if (product === undefined || product.display === undefined) return undefined;
  return decimalMantissaShapeFact(product.display);
}

function structuralHexSquareDecimalProduct(
  fact: X2ShapeFact,
): StructuralHexDecimalProduct | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "mantissa" || model.radix !== "hex" || model.hasDecimalPoint) return undefined;
  const raw = model.sign === "" ? model.canonical : model.canonical.slice(1);
  const significant = /^0*([A-FСГЕ])$/u.exec(raw)?.[1];
  if (significant === undefined) return undefined;
  const digit = structuralHexNibbleValue(significant);
  switch (digit) {
    case 10:
      return { value: "0", display: "00" };
    case 11:
      return { value: "10", display: "10" };
    case 12:
      return { value: "20", display: "20" };
    case 13:
      return { value: "30", display: "30" };
    case 14:
    case 15:
      return { value: "0", display: "0" };
    default:
      return undefined;
  }
}

function plainProducesConcreteBinaryShapeFacts(
  op: Extract<IrOp, { kind: "plain" }>,
  y: X2ValueSet | undefined,
  x: X2ValueSet | undefined,
  yShape: X2ShapeSet | undefined,
  xShape: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  const effectiveYShape = shapeSetWithStableExpressionValueShapes(yShape, y);
  const effectiveXShape = shapeSetWithStableExpressionValueShapes(xShape, x);
  for (const fact of plainProducesConcreteDecimalBinaryShapeFacts(op, y, x, effectiveYShape, effectiveXShape)) {
    output.add(fact);
  }
  for (const fact of plainProducesStructuralBinaryDecimalShapes(op, y, x, effectiveYShape, effectiveXShape)) {
    output.add(fact);
  }
  if (op.opcode < 0x37 || op.opcode > 0x39) return output;
  const left = bitwiseOperandsFromValuesAndShapes(y, effectiveYShape);
  const right = bitwiseOperandsFromValuesAndShapes(x, effectiveXShape);
  for (const leftOperand of left) {
    for (const rightOperand of right) {
      const result = structuralBitwiseMantissaShapeFact(op.opcode, leftOperand, rightOperand);
      if (result !== undefined) output.add(result);
    }
  }
  return output;
}

function plainProducesConcreteDecimalBinaryShapeFacts(
  op: Extract<IrOp, { kind: "plain" }>,
  y: X2ValueSet | undefined,
  x: X2ValueSet | undefined,
  yShape: X2ShapeSet | undefined,
  xShape: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  if (!EXACT_DECIMAL_BINARY_MANTISSA_SHAPE_OPCODES.has(op.opcode)) return output;
  for (const yValue of normalizedDecimalValues(y, yShape)) {
    for (const xValue of normalizedDecimalValues(x, xShape)) {
      const concrete = concreteDecimalBinaryValue(op.opcode, yValue, xValue);
      const shape = concrete === undefined ? undefined : exactDecimalDisplayShapeFact(concrete);
      if (shape !== undefined) output.add(shape);
    }
  }
  return output;
}

function plainProducesStructuralBinaryDecimalShapes(
  op: Extract<IrOp, { kind: "plain" }>,
  y: X2ValueSet | undefined,
  x: X2ValueSet | undefined,
  yShape: X2ShapeSet | undefined,
  xShape: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const fact of structuralHexBinaryDecimalDisplayShapes(op, y, x, yShape, xShape)) output.add(fact);
  for (const fact of structuralBitwiseDecimalDisplayShapes(op, y, x, yShape, xShape)) output.add(fact);
  return output;
}

function plainProducesStructuralBinaryDecimalValues(
  op: Extract<IrOp, { kind: "plain" }>,
  y: X2ValueSet | undefined,
  x: X2ValueSet | undefined,
  yShape: X2ShapeSet | undefined,
  xShape: X2ShapeSet | undefined,
): Set<X2ValueFact> {
  const output = new Set<X2ValueFact>();
  for (const value of structuralHexBinaryDecimalValues(op, y, x, yShape, xShape)) {
    output.add(decimalValueFact(value, "normalized"));
  }
  return output;
}

function structuralHexBinaryDecimalValues(
  op: Extract<IrOp, { kind: "plain" }>,
  y: X2ValueSet | undefined,
  x: X2ValueSet | undefined,
  yShape: X2ShapeSet | undefined,
  xShape: X2ShapeSet | undefined,
): Set<string> {
  const output = new Set<string>();
  if (op.opcode >= 0x37 && op.opcode <= 0x39) {
    for (const left of bitwiseOperandsFromValuesAndShapes(y, yShape)) {
      for (const right of bitwiseOperandsFromValuesAndShapes(x, xShape)) {
        const result = structuralBitwiseDecimalValue(op.opcode, left, right);
        if (result !== undefined) output.add(result);
      }
    }
    return output;
  }
  if (op.opcode === 0x10) {
    for (const leftDigit of structuralSingleHexDigitValues(yShape)) {
      for (const right of normalizedDecimalValues(x, xShape)) {
        const result = structuralHexDigitPlusDecimalValue(leftDigit, right);
        if (result !== undefined) output.add(result);
      }
    }
    for (const left of normalizedDecimalValues(y, yShape)) {
      for (const rightDigit of structuralSingleHexDigitValues(xShape)) {
        const result = decimalPlusStructuralHexDigitValue(left, rightDigit);
        if (result !== undefined) output.add(result);
      }
    }
    return output;
  }
  if (op.opcode === 0x11) {
    for (const leftDigit of structuralSingleHexDigitValues(yShape)) {
      for (const right of normalizedDecimalValues(x, xShape)) {
        const result = structuralHexDigitMinusDecimalValue(leftDigit, right);
        if (result !== undefined) output.add(result);
      }
    }
    for (const left of normalizedDecimalValues(y, yShape)) {
      for (const rightDigit of structuralSingleHexDigitValues(xShape)) {
        const result = decimalMinusStructuralHexDigitValue(left, rightDigit);
        if (result !== undefined) output.add(result);
      }
    }
    return output;
  }
  if (op.opcode === 0x12) {
    for (const leftDigit of structuralSingleHexDigitValues(yShape)) {
      for (const right of normalizedDecimalValues(x, xShape)) {
        const result = structuralHexDigitTimesDecimalValue(leftDigit, right);
        if (result !== undefined) output.add(result);
      }
    }
    for (const left of normalizedDecimalValues(y, yShape)) {
      for (const rightDigit of structuralSingleHexDigitValues(xShape)) {
        const result = decimalTimesStructuralHexDigitValue(left, rightDigit);
        if (result !== undefined) output.add(result);
      }
    }
    for (const leftExponent of structuralSingleHexExponentOperands(yShape)) {
      for (const right of normalizedDecimalValues(x, xShape)) {
        const result = structuralHexExponentTimesDecimalValue(leftExponent, right);
        if (result !== undefined) output.add(result);
      }
    }
    for (const left of normalizedDecimalValues(y, yShape)) {
      for (const rightExponent of structuralSingleHexExponentOperands(xShape)) {
        const result = decimalTimesStructuralHexExponentValue(left, rightExponent);
        if (result !== undefined) output.add(result);
      }
    }
    return output;
  }
  return output;
}

function structuralHexBinaryDecimalDisplayShapes(
  op: Extract<IrOp, { kind: "plain" }>,
  y: X2ValueSet | undefined,
  x: X2ValueSet | undefined,
  yShape: X2ShapeSet | undefined,
  xShape: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  if (op.opcode === 0x10) {
    for (const leftDigit of structuralSingleHexDigitValues(yShape)) {
      for (const right of normalizedDecimalValues(x, xShape)) {
        const result = structuralHexDigitPlusDecimalValue(leftDigit, right);
        if (result !== undefined) output.add(decimalMantissaShapeFact(result));
      }
    }
    for (const left of normalizedDecimalValues(y, yShape)) {
      for (const rightDigit of structuralSingleHexDigitValues(xShape)) {
        const result = decimalPlusStructuralHexDigitValue(left, rightDigit);
        if (result !== undefined) output.add(decimalMantissaShapeFact(result));
      }
    }
    return output;
  }
  if (op.opcode === 0x11) {
    for (const leftDigit of structuralSingleHexDigitValues(yShape)) {
      for (const right of normalizedDecimalValues(x, xShape)) {
        const result = structuralHexDigitMinusDecimalValue(leftDigit, right);
        if (result !== undefined) output.add(decimalMantissaShapeFact(result));
      }
    }
    for (const left of normalizedDecimalValues(y, yShape)) {
      for (const rightDigit of structuralSingleHexDigitValues(xShape)) {
        const result = decimalMinusStructuralHexDigitValue(left, rightDigit);
        if (result !== undefined) output.add(decimalMantissaShapeFact(result));
      }
    }
    return output;
  }
  if (op.opcode !== 0x12) return output;
  for (const leftDigit of structuralSingleHexDigitValues(yShape)) {
    for (const right of normalizedDecimalValues(x, xShape)) {
      const result = structuralHexDigitTimesDecimalDisplayShape(leftDigit, right);
      if (result !== undefined) output.add(result);
    }
  }
  for (const left of normalizedDecimalValues(y, yShape)) {
    for (const rightDigit of structuralSingleHexDigitValues(xShape)) {
      const result = decimalTimesStructuralHexDigitDisplayShape(left, rightDigit);
      if (result !== undefined) output.add(result);
    }
  }
  for (const leftExponent of structuralSingleHexExponentOperands(yShape)) {
    for (const right of normalizedDecimalValues(x, xShape)) {
      const result = structuralHexExponentTimesDecimalDisplayShape(leftExponent, right);
      if (result !== undefined) output.add(result);
    }
  }
  for (const left of normalizedDecimalValues(y, yShape)) {
    for (const rightExponent of structuralSingleHexExponentOperands(xShape)) {
      const result = decimalTimesStructuralHexExponentDisplayShape(left, rightExponent);
      if (result !== undefined) output.add(result);
    }
  }
  return output;
}

function normalizedDecimalValues(values: X2ValueSet | undefined, shapes: X2ShapeSet | undefined = undefined): Set<string> {
  const output = new Set<string>();
  for (const fact of values ?? []) {
    const value = computationDecimalValueFromFact(fact);
    if (value !== undefined) output.add(value);
  }
  for (const value of x2ShapeSetRestoredVisibleDecimals(shapes)) output.add(value);
  return output;
}

function shapeSetWithStableExpressionValueShapes(
  shapes: X2ShapeSet | undefined,
  values: X2ValueSet | undefined,
): X2ShapeSet | undefined {
  const output = cloneOptionalShapeSet(shapes);
  for (const value of values ?? []) {
    if (!value.startsWith("expr-key:")) continue;
    for (const shape of stableExpressionKeyShapeSet(value) ?? []) output.add(shape);
  }
  return output.size === 0 ? undefined : output;
}

function structuralSingleHexDigitValues(shapes: X2ShapeSet | undefined): Set<number> {
  const output = new Set<number>();
  for (const fact of structuralRestoreShapeFacts(canonicalStructuralShapeFacts(shapes))) {
    const model = x2ShapeDataModelForFact(fact);
    if (
      model.kind !== "mantissa" ||
      model.radix !== "hex" ||
      model.sign !== "" ||
      model.hasDecimalPoint ||
      model.digits.length !== 1
    ) {
      continue;
    }
    const value = structuralHexNibbleValue(model.digits[0]!);
    if (value !== undefined) output.add(value);
  }
  return output;
}

interface StructuralHexExponentOperand {
  readonly digit: number;
  readonly exponent: string;
}

function structuralSingleHexExponentOperands(shapes: X2ShapeSet | undefined): StructuralHexExponentOperand[] {
  const output = new Map<string, StructuralHexExponentOperand>();
  for (const fact of canonicalStructuralShapeFacts(shapes)) {
    const model = x2ShapeDataModelForFact(fact);
    const operand = structuralSingleHexExponentOperandFromShapeModel(model);
    if (operand === undefined) continue;
    const { digit, exponent } = operand;
    output.set(`${digit}:${exponent}`, { digit, exponent });
  }
  return [...output.values()];
}

function structuralSingleHexExponentOperandFromShapeModel(
  model: X2ShapeDataModel,
): StructuralHexExponentOperand | undefined {
  if (
    model.kind === "exponent-entry" &&
    model.mantissa.radix === "hex" &&
    model.mantissa.sign === "" &&
    !model.mantissa.hasDecimalPoint &&
    model.mantissa.digits.length === 1
  ) {
    const digit = structuralHexNibbleValue(model.mantissa.digits[0]!);
    const exponent = canonicalExponentShapeRaw(model.exponentRaw);
    if (digit === undefined || exponent === undefined) return undefined;
    return { digit, exponent };
  }
  if (model.kind !== "mantissa" || model.radix !== "hex" || model.sign !== "") return undefined;
  const integer = /^([A-FСГЕ])(0*)$/u.exec(model.canonical);
  if (integer !== null) {
    const digit = structuralHexNibbleValue(integer[1]!);
    if (digit === undefined) return undefined;
    return { digit, exponent: String(integer[2]!.length) };
  }
  const fraction = /^0\.(0*)([A-FСГЕ])$/u.exec(model.canonical);
  if (fraction !== null) {
    const digit = structuralHexNibbleValue(fraction[2]!);
    if (digit === undefined) return undefined;
    return { digit, exponent: `-${fraction[1]!.length + 1}` };
  }
  return undefined;
}

function structuralHexDigitPlusDecimalValue(leftDigit: number, right: string): string | undefined {
  if (!isVerifiedArithmeticHexDigit(leftDigit)) return undefined;
  const rightDigit = singleDecimalDigitValue(right);
  return rightDigit === undefined ? undefined : String(leftDigit + rightDigit);
}

function decimalPlusStructuralHexDigitValue(left: string, rightDigit: number): string | undefined {
  if (!isVerifiedArithmeticHexDigit(rightDigit)) return undefined;
  const leftDigit = singleDecimalDigitValue(left);
  if (leftDigit === undefined) return undefined;
  const value = (leftDigit + rightDigit) % 16;
  return String(value >= 10 ? value - 10 : value);
}

function structuralHexDigitMinusDecimalValue(leftDigit: number, right: string): string | undefined {
  if (!isVerifiedArithmeticHexDigit(leftDigit)) return undefined;
  const rightDigit = singleDecimalDigitValue(right);
  if (rightDigit === undefined) return undefined;
  const value = leftDigit - rightDigit;
  return String(rightDigit === 0 || value < 10 ? value : value - 10);
}

function decimalMinusStructuralHexDigitValue(left: string, rightDigit: number): string | undefined {
  if (!isVerifiedArithmeticHexDigit(rightDigit)) return undefined;
  const leftDigit = singleDecimalDigitValue(left);
  if (leftDigit === undefined) return undefined;
  const value = leftDigit - rightDigit;
  return String(value <= -11 ? value + 10 : value);
}

function isVerifiedArithmeticHexDigit(digit: number): boolean {
  return digit === 10 || digit === 11 || digit === 12 || digit === 13 || digit === 14;
}

function singleDecimalDigitValue(value: string): number | undefined {
  return /^[0-9]$/u.test(value) ? Number(value) : undefined;
}

function structuralHexDigitTimesDecimalValue(leftDigit: number, right: string): string | undefined {
  return structuralHexDigitTimesDecimalProduct(leftDigit, right)?.value;
}

function decimalTimesStructuralHexDigitValue(left: string, rightDigit: number): string | undefined {
  return decimalTimesStructuralHexDigitProduct(left, rightDigit)?.value;
}

function structuralHexDigitTimesDecimalDisplayShape(leftDigit: number, right: string): X2ShapeFact | undefined {
  const product = structuralHexDigitTimesDecimalProduct(leftDigit, right);
  return structuralHexDecimalProductDisplayShape(product);
}

function decimalTimesStructuralHexDigitDisplayShape(left: string, rightDigit: number): X2ShapeFact | undefined {
  const product = decimalTimesStructuralHexDigitProduct(left, rightDigit);
  return structuralHexDecimalProductDisplayShape(product);
}

interface StructuralHexDecimalProduct {
  readonly value: string;
  readonly display?: string;
  readonly displayShape?: X2ShapeFact;
}

function structuralHexDecimalProductDisplayShape(
  product: StructuralHexDecimalProduct | undefined,
): X2ShapeFact | undefined {
  if (product === undefined) return undefined;
  return product.displayShape ?? (product.display === undefined ? undefined : decimalMantissaShapeFact(product.display));
}

const STRUCTURAL_HEX_DIGIT_TIMES_DECIMAL_TABLE: ReadonlyMap<string, StructuralHexDecimalProduct> = new Map([
  ["10:0", { value: "0", display: "0" }],
  ["10:1", { value: "0", display: "0" }],
  ["10:2", { value: "4", display: "4" }],
  ["10:3", { value: "4", display: "4" }],
  ["10:4", { value: "8", display: "8" }],
  ["10:5", { value: "50", display: "50" }],
  ["10:6", { value: "0", display: "0" }],
  ["10:7", { value: "10", display: "10" }],
  ["10:8", { value: "20", display: "20" }],
  ["10:9", { value: "30", display: "30" }],
  ["10:18", { value: "20", display: "020" }],
  ["11:0", { value: "0", display: "0" }],
  ["11:1", { value: "1", display: "1" }],
  ["11:2", { value: "6", display: "6" }],
  ["11:3", { value: "1", display: "1" }],
  ["11:4", { value: "2", display: "2" }],
  ["11:5", { value: "11", display: "11" }],
  ["11:6", { value: "22", display: "22" }],
  ["11:7", { value: "33", display: "33" }],
  ["11:8", { value: "44", display: "44" }],
  ["11:9", { value: "55", display: "55" }],
  ["11:18", { value: "54", display: "054" }],
  ["12:0", { value: "0", display: "0" }],
  ["12:1", { value: "2", display: "2" }],
  ["12:2", { value: "8", display: "8" }],
  ["12:3", { value: "4", display: "4" }],
  ["12:4", { value: "0", display: "0" }],
  ["12:5", { value: "32", display: "32" }],
  ["12:6", { value: "44", display: "44" }],
  ["12:7", { value: "40", display: "40" }],
  ["12:8", { value: "52", display: "52" }],
  ["12:9", { value: "64", display: "64" }],
  ["12:18", { value: "912", display: "912" }],
  ["13:0", { value: "0", display: "0" }],
  ["13:1", { value: "3", display: "3" }],
  ["13:2", { value: "10", display: "10" }],
  ["13:3", { value: "23", display: "23" }],
  ["13:4", { value: "20", display: "20" }],
  ["13:5", { value: "53", display: "53" }],
  ["13:6", { value: "50", display: "50" }],
  ["13:7", { value: "63", display: "63" }],
  ["13:8", { value: "60", display: "60" }],
  ["13:9", { value: "73", display: "73" }],
  ["13:18", { value: "930", display: "930" }],
  ["14:0", { value: "0", display: "0" }],
  ["14:1", { value: "4", display: "4" }],
  ["14:2", { value: "12", display: "12" }],
  ["14:3", { value: "10", display: "10" }],
  ["14:4", { value: "24", display: "24" }],
  ["14:5", { value: "42", display: "42" }],
  ["14:6", { value: "40", display: "40" }],
  ["14:7", { value: "54", display: "54" }],
  ["14:8", { value: "68", display: "68" }],
  ["14:9", { value: "82", display: "82" }],
  ["14:18", { value: "948", display: "948" }],
]);

function structuralHexDigitTimesDecimalProduct(
  leftDigit: number,
  right: string,
): StructuralHexDecimalProduct | undefined {
  return STRUCTURAL_HEX_DIGIT_TIMES_DECIMAL_TABLE.get(`${leftDigit}:${right}`);
}

function decimalTimesStructuralHexDigitProduct(
  left: string,
  rightDigit: number,
): StructuralHexDecimalProduct | undefined {
  if (!DECIMAL_TIMES_STRUCTURAL_HEX_INPUTS.has(left)) return undefined;
  if (rightDigit === 10 || rightDigit === 11 || rightDigit === 12 || rightDigit === 13) {
    const value = exactDecimalToNormalized(BigInt(left) * 10n, 0);
    return value === undefined ? undefined : { value, display: value };
  }
  if (rightDigit === 14) return { value: "0", display: "0" };
  return undefined;
}

const DECIMAL_TIMES_STRUCTURAL_HEX_INPUTS = new Set(["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "18"]);

function structuralHexExponentTimesDecimalValue(
  left: StructuralHexExponentOperand,
  right: string,
): string | undefined {
  return structuralHexExponentTimesDecimalProduct(left, right)?.value;
}

function decimalTimesStructuralHexExponentValue(
  left: string,
  right: StructuralHexExponentOperand,
): string | undefined {
  return decimalTimesStructuralHexExponentProduct(left, right)?.value;
}

function structuralHexExponentTimesDecimalDisplayShape(
  left: StructuralHexExponentOperand,
  right: string,
): X2ShapeFact | undefined {
  return structuralHexDecimalProductDisplayShape(structuralHexExponentTimesDecimalProduct(left, right));
}

function decimalTimesStructuralHexExponentDisplayShape(
  left: string,
  right: StructuralHexExponentOperand,
): X2ShapeFact | undefined {
  return structuralHexDecimalProductDisplayShape(decimalTimesStructuralHexExponentProduct(left, right));
}

const STRUCTURAL_HEX_EXPONENT_TIMES_DECIMAL_TABLE: ReadonlyMap<string, StructuralHexDecimalProduct> = new Map([
  ["1", { value: "0.03", displayShape: decimalExponentShapeFact("3", "-2") }],
  ["2", { value: "0.1", displayShape: decimalExponentShapeFact("1", "-1") }],
  ["4", { value: "0.2", displayShape: decimalExponentShapeFact("2", "-1") }],
  ["5", { value: "0.53", displayShape: decimalExponentShapeFact("5.3", "-1") }],
  ["8", { value: "0.6", displayShape: decimalExponentShapeFact("6", "-1") }],
  ["16", { value: "9.2", display: "9.2" }],
]);

const DECIMAL_TIMES_STRUCTURAL_HEX_EXPONENT_TABLE: ReadonlyMap<string, StructuralHexDecimalProduct> = new Map([
  ["1", { value: "0.1", displayShape: decimalExponentShapeFact("1", "-1") }],
  ["2", { value: "0.2", displayShape: decimalExponentShapeFact("2", "-1") }],
  ["4", { value: "0.4", displayShape: decimalExponentShapeFact("4", "-1") }],
  ["5", { value: "0.5", displayShape: decimalExponentShapeFact("5", "-1") }],
  ["8", { value: "0.8", displayShape: decimalExponentShapeFact("8", "-1") }],
  ["16", { value: "1.6", display: "1.6" }],
]);

function structuralHexExponentTimesDecimalProduct(
  left: StructuralHexExponentOperand,
  right: string,
): StructuralHexDecimalProduct | undefined {
  return left.digit === 13 && left.exponent === "-2"
    ? STRUCTURAL_HEX_EXPONENT_TIMES_DECIMAL_TABLE.get(right)
    : undefined;
}

function decimalTimesStructuralHexExponentProduct(
  left: string,
  right: StructuralHexExponentOperand,
): StructuralHexDecimalProduct | undefined {
  return right.digit === 13 && right.exponent === "-2"
    ? DECIMAL_TIMES_STRUCTURAL_HEX_EXPONENT_TABLE.get(left)
    : undefined;
}

function structuralHexSubtractOneDecimalValue(digit: number): string | undefined {
  if (digit === 12) return "1";
  if (digit === 13) return "2";
  if (digit === 14) return "3";
  return undefined;
}

function structuralBitwiseNotMantissaShapeFact(
  operand: StructuralBitwiseOperand,
): X2ShapeFact | undefined {
  const result = structuralBitwiseNotNibbles(operand);
  if (result === undefined) return undefined;
  const hasHexCell = result.some((digit) => digit > 9);
  if (!hasHexCell && !operand.structural) return undefined;
  return x2MantissaShapeFactFromParts("hex", bitwiseMantissaRaw(result));
}

function structuralBitwiseNotDecimalValueFromFact(fact: X2ShapeFact): string | undefined {
  const nibbles = structuralMantissaNibbles(fact);
  if (nibbles === undefined) return undefined;
  const result = structuralBitwiseNotNibbles({ nibbles, structural: true });
  return result === undefined ? undefined : decimalValueFromBitwiseMantissaNibbles(result);
}

function structuralBitwiseNotDecimalDisplayShapeFact(fact: X2ShapeFact): X2ShapeFact | undefined {
  const nibbles = structuralMantissaNibbles(fact);
  if (nibbles === undefined) return undefined;
  const result = structuralBitwiseNotNibbles({ nibbles, structural: true });
  return result === undefined ? undefined : decimalDisplayShapeFromBitwiseMantissaNibbles(result);
}

function structuralBitwiseNotNibbles(operand: StructuralBitwiseOperand): number[] | undefined {
  if (operand.nibbles.length !== 8) return undefined;
  const result = [8];
  for (let index = 1; index < 8; index += 1) {
    result.push((~operand.nibbles[index]!) & 0x0f);
  }
  return result;
}

function bitwiseOperandsFromValuesAndShapes(
  values: X2ValueSet | undefined,
  shapes: X2ShapeSet | undefined,
): StructuralBitwiseOperand[] {
  const output: StructuralBitwiseOperand[] = [];
  for (const fact of values ?? []) {
    const value = normalizedDecimalValueFromFact(fact);
    const nibbles = value === undefined ? undefined : decimalMantissaDigits(value);
    if (nibbles !== undefined) output.push({ nibbles, structural: false });
  }
  for (const fact of structuralRestoreShapeFacts(canonicalStructuralShapeFacts(shapes))) {
    const nibbles = structuralMantissaNibbles(fact);
    if (nibbles !== undefined) output.push({ nibbles, structural: true });
  }
  return output;
}

function structuralBitwiseMantissaShapeFact(
  opcode: number,
  left: StructuralBitwiseOperand,
  right: StructuralBitwiseOperand,
): X2ShapeFact | undefined {
  const result = structuralBitwiseNibbles(opcode, left, right);
  if (result === undefined) return undefined;
  const hasHexCell = result.some((digit) => digit > 9);
  if (!hasHexCell && !left.structural && !right.structural) return undefined;
  return x2MantissaShapeFactFromParts("hex", bitwiseMantissaRaw(result));
}

function structuralBitwiseDecimalValue(
  opcode: number,
  left: StructuralBitwiseOperand,
  right: StructuralBitwiseOperand,
): string | undefined {
  if (!left.structural && !right.structural) return undefined;
  const result = structuralBitwiseNibbles(opcode, left, right);
  return result === undefined ? undefined : decimalValueFromBitwiseMantissaNibbles(result);
}

function structuralBitwiseDecimalDisplayShapes(
  op: Extract<IrOp, { kind: "plain" }>,
  y: X2ValueSet | undefined,
  x: X2ValueSet | undefined,
  yShape: X2ShapeSet | undefined,
  xShape: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  if (op.opcode < 0x37 || op.opcode > 0x39) return output;
  const left = bitwiseOperandsFromValuesAndShapes(y, yShape);
  const right = bitwiseOperandsFromValuesAndShapes(x, xShape);
  for (const leftOperand of left) {
    for (const rightOperand of right) {
      if (!leftOperand.structural && !rightOperand.structural) continue;
      const result = structuralBitwiseNibbles(op.opcode, leftOperand, rightOperand);
      const shape = result === undefined ? undefined : decimalDisplayShapeFromBitwiseMantissaNibbles(result);
      if (shape !== undefined) output.add(shape);
    }
  }
  return output;
}

function structuralBitwiseNibbles(
  opcode: number,
  left: StructuralBitwiseOperand,
  right: StructuralBitwiseOperand,
): number[] | undefined {
  if (left.nibbles.length !== 8 || right.nibbles.length !== 8) return undefined;
  const result = [8];
  for (let index = 1; index < 8; index += 1) {
    const digit = bitwiseMantissaDigit(opcode, left.nibbles[index]!, right.nibbles[index]!);
    if (digit === undefined || digit < 0 || digit > 15) return undefined;
    result.push(digit);
  }
  return result;
}

function decimalValueFromBitwiseMantissaNibbles(nibbles: readonly number[]): string | undefined {
  if (nibbles.length !== 8 || nibbles.some((digit) => digit < 0 || digit > 9)) return undefined;
  return decimalFromMantissaDigits(nibbles);
}

function decimalDisplayShapeFromBitwiseMantissaNibbles(nibbles: readonly number[]): X2ShapeFact | undefined {
  if (decimalValueFromBitwiseMantissaNibbles(nibbles) === undefined) return undefined;
  return decimalMantissaShapeFact(bitwiseMantissaRaw(nibbles));
}

function structuralMantissaNibbles(fact: X2ShapeFact): number[] | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "mantissa" || (model.radix !== "hex" && model.radix !== "super")) return undefined;
  const nibbles: number[] = [];
  for (const digit of model.digits.slice(0, 8)) {
    const value = structuralHexNibbleValue(digit);
    if (value === undefined) return undefined;
    nibbles.push(value);
  }
  while (nibbles.length < 8) nibbles.push(0);
  return nibbles;
}

function structuralHexNibbleValue(digit: string): number | undefined {
  const index = "0123456789ABCDEF".indexOf(digit);
  if (index >= 0) return index;
  switch (digit) {
    case "С":
      return 12;
    case "Г":
      return 13;
    case "Е":
      return 14;
    default:
      return undefined;
  }
}

function isStructuralHexDigit(char: string): boolean {
  return structuralHexNibbleValue(char) !== undefined;
}

function isStructuralHexShapeChar(char: string): boolean {
  return char === "." || char === "-" || isStructuralHexDigit(char);
}

function hasStructuralNonDecimalDigit(raw: string): boolean {
  for (const char of raw) {
    const value = structuralHexNibbleValue(char);
    if (value !== undefined && value >= 10) return true;
  }
  return false;
}

function structuralShapeRawIsValid(raw: string): boolean {
  let sawDigit = false;
  let sawDecimalPoint = false;
  for (const char of canonicalShapeRaw(raw)) {
    if (isStructuralHexDigit(char)) {
      sawDigit = true;
      continue;
    }
    if (char === ".") {
      if (sawDecimalPoint) return false;
      sawDecimalPoint = true;
      continue;
    }
    if (!isStructuralHexShapeChar(char)) return false;
  }
  return sawDigit;
}

function superShapeRawIsValid(raw: string): boolean {
  return /^-?F[A-F]$/u.test(canonicalShapeRaw(raw));
}

function bitwiseMantissaRaw(digits: readonly number[]): string {
  const rendered = digits.map((digit) => "0123456789ABCDEF"[digit] ?? "0");
  return `${rendered[0]}.${rendered.slice(1).join("")}`;
}

function concreteDecimalUnaryValue(opcode: number, value: string): string | undefined {
  switch (opcode) {
    case 0x15:
      return decimalPowerOfTen(value);
    case 0x16:
      return decimalExp(value);
    case 0x17:
      return decimalCommonLog(value);
    case 0x18:
      return decimalNaturalLog(value);
    case 0x19:
      return decimalArcSin(value);
    case 0x1a:
      return decimalArcCos(value);
    case 0x1b:
      return decimalArcTan(value);
    case 0x1c:
      return decimalSin(value);
    case 0x1d:
      return decimalCos(value);
    case 0x1e:
      return decimalTan(value);
    case 0x21:
      return decimalSquareRoot(value);
    case 0x22:
      return decimalSquare(value);
    case 0x23:
      return decimalReciprocal(value);
    case 0x26:
      return decimalToMinutes(value);
    case 0x2a:
      return decimalToMinutesSeconds(value);
    case 0x30:
      return decimalFromMinutesSeconds(value);
    case 0x33:
      return decimalFromMinutes(value);
    case 0x3a:
      return decimalBitwiseNot(value);
    case 0x31:
      return decimalAbs(value);
    case 0x32:
      return decimalSign(value);
    case 0x34:
      return decimalIntegerPart(value);
    case 0x35:
      return decimalFractionPart(value);
    default:
      return undefined;
  }
}

function decimalFromFactKey(key: string): string | undefined {
  return /^decimal:([^:]+):normalized$/u.exec(key)?.[1];
}

function decimalAbs(value: string): string | undefined {
  if (!/^-?[0-9]+(?:\.[0-9]+)?$/u.test(value)) return undefined;
  return value.startsWith("-") ? value.slice(1) : value;
}

function decimalSign(value: string): string | undefined {
  if (!/^-?[0-9]+(?:\.[0-9]+)?$/u.test(value)) return undefined;
  if (value === "0") return "0";
  return value.startsWith("-") ? "-1" : "1";
}

function decimalPowerOfTen(value: string): string | undefined {
  const exponent = parseExactDecimal(value);
  if (exponent === undefined || exponent.scale !== 0) return undefined;
  const power = Number(exponent.num);
  if (!Number.isSafeInteger(power) || power < -99 || power > 99) return undefined;
  return power >= 0
    ? exactDecimalToNormalized(pow10BigInt(power), 0)
    : exactDecimalToNormalized(1n, -power);
}

function decimalExp(value: string): string | undefined {
  return decimalIsZero(value) ? "1" : undefined;
}

function decimalCommonLog(value: string): string | undefined {
  return decimalIsOne(value) ? "0" : undefined;
}

function decimalNaturalLog(value: string): string | undefined {
  return decimalIsOne(value) ? "0" : undefined;
}

function decimalArcSin(value: string): string | undefined {
  return decimalIsZero(value) ? "0" : undefined;
}

function decimalArcCos(value: string): string | undefined {
  return decimalIsOne(value) ? "0" : undefined;
}

function decimalArcTan(value: string): string | undefined {
  return decimalIsZero(value) ? "0" : undefined;
}

function decimalSin(value: string): string | undefined {
  return decimalIsZero(value) ? "0" : undefined;
}

function decimalCos(value: string): string | undefined {
  return decimalIsZero(value) ? "1" : undefined;
}

function decimalTan(value: string): string | undefined {
  return decimalIsZero(value) ? "0" : undefined;
}

function decimalSquareRoot(value: string): string | undefined {
  const input = parseExactDecimal(value);
  if (input === undefined || input.num < 0n || input.scale % 2 !== 0) return undefined;
  const root = exactBigIntSquareRoot(input.num);
  return root === undefined ? undefined : exactDecimalToNormalized(root, input.scale / 2);
}

function decimalSquare(value: string): string | undefined {
  const input = parseExactDecimal(value);
  return input === undefined
    ? undefined
    : exactDecimalToNormalized(input.num * input.num, input.scale * 2);
}

function decimalReciprocal(value: string): string | undefined {
  const input = parseExactDecimal(value);
  return input === undefined
    ? undefined
    : exactDecimalDivisionToNormalized({ num: 1n, scale: 0 }, input);
}

function decimalToMinutes(value: string): string | undefined {
  const input = decimalWholeFractionParts(value);
  if (input === undefined) return undefined;
  const minutes = centesimalFieldValue(input.fraction, 2);
  if (minutes === undefined || minutes.num >= 60n * pow10BigInt(minutes.scale)) return undefined;
  const denominator = 60n * pow10BigInt(minutes.scale);
  const numerator = input.integer * denominator + minutes.num;
  return exactDecimalDivisionToNormalized(
    { num: input.sign === "-" ? -numerator : numerator, scale: 0 },
    { num: denominator, scale: 0 },
  );
}

function decimalToMinutesSeconds(value: string): string | undefined {
  const input = decimalWholeFractionParts(value);
  if (input === undefined) return undefined;
  const fields = centesimalMinuteSecondFields(input.fraction);
  if (fields === undefined) return undefined;
  const { minutes, seconds } = fields;
  if (minutes >= 60n || seconds.num >= 60n * pow10BigInt(seconds.scale)) return undefined;
  const scale = seconds.scale;
  const denominator = 3600n * pow10BigInt(scale);
  const numerator = input.integer * denominator + minutes * 60n * pow10BigInt(scale) + seconds.num;
  return exactDecimalDivisionToNormalized(
    { num: input.sign === "-" ? -numerator : numerator, scale: 0 },
    { num: denominator, scale: 0 },
  );
}

function decimalFromMinutes(value: string): string | undefined {
  const input = decimalWholeFractionParts(value);
  if (input === undefined) return undefined;
  const scale = input.fraction.length;
  const fraction = input.fraction.length === 0 ? 0n : BigInt(input.fraction);
  const denominatorScale = scale + 2;
  const numerator = input.integer * pow10BigInt(denominatorScale) + fraction * 60n;
  return exactDecimalToNormalized(input.sign === "-" ? -numerator : numerator, denominatorScale);
}

function decimalFromMinutesSeconds(value: string): string | undefined {
  const input = decimalWholeFractionParts(value);
  if (input === undefined) return undefined;
  const scale = input.fraction.length;
  const fraction = input.fraction.length === 0 ? 0n : BigInt(input.fraction);
  const totalMinutesNumerator = fraction * 60n;
  const scaleFactor = pow10BigInt(scale);
  const minutes = totalMinutesNumerator / scaleFactor;
  if (minutes >= 60n) return undefined;
  const minuteRemainder = totalMinutesNumerator - minutes * scaleFactor;
  const denominatorScale = scale + 4;
  const numerator =
    input.integer * pow10BigInt(denominatorScale) +
    minutes * 100n * scaleFactor +
    minuteRemainder * 60n;
  return exactDecimalToNormalized(input.sign === "-" ? -numerator : numerator, denominatorScale);
}

function decimalBitwiseNot(value: string): string | undefined {
  const digits = decimalMantissaDigits(value);
  if (digits === undefined) return undefined;
  const result = [8];
  for (let index = 1; index < 8; index += 1) {
    const digit = (~digits[index]!) & 0x0f;
    if (digit > 9) return undefined;
    result.push(digit);
  }
  return decimalFromMantissaDigits(result);
}

interface ExactDecimalParts {
  readonly num: bigint;
  readonly scale: number;
}

interface DecimalWholeFractionParts {
  readonly sign: "" | "-";
  readonly integer: bigint;
  readonly fraction: string;
}

interface ScaledCentesimalValue {
  readonly num: bigint;
  readonly scale: number;
}

function decimalWholeFractionParts(value: string): DecimalWholeFractionParts | undefined {
  const match = /^(-?)([0-9]+)(?:\.([0-9]+))?$/u.exec(value);
  if (match === null) return undefined;
  return {
    sign: match[1]! === "-" ? "-" : "",
    integer: BigInt(match[2]!),
    fraction: match[3] ?? "",
  };
}

function centesimalFieldValue(raw: string, width: number): ScaledCentesimalValue | undefined {
  if (!/^[0-9]*$/u.test(raw)) return undefined;
  if (raw.length === 0) return { num: 0n, scale: 0 };
  if (raw.length <= width) {
    return {
      num: BigInt(raw) * pow10BigInt(width - raw.length),
      scale: 0,
    };
  }
  return {
    num: BigInt(raw),
    scale: raw.length - width,
  };
}

function centesimalMinuteSecondFields(
  fraction: string,
): { readonly minutes: bigint; readonly seconds: ScaledCentesimalValue } | undefined {
  if (!/^[0-9]*$/u.test(fraction)) return undefined;
  const minuteRaw = fraction.length <= 2 ? fraction : fraction.slice(0, 2);
  const minuteValue = centesimalFieldValue(minuteRaw, 2);
  if (minuteValue === undefined || minuteValue.scale !== 0) return undefined;
  const secondRaw = fraction.length <= 2 ? "" : fraction.slice(2);
  const seconds = centesimalFieldValue(secondRaw, 2);
  return seconds === undefined ? undefined : { minutes: minuteValue.num, seconds };
}

function concreteDecimalBinaryValue(opcode: number, y: string, x: string): string | undefined {
  const left = parseExactDecimal(y);
  const right = parseExactDecimal(x);
  if (left === undefined || right === undefined) return undefined;
  if (opcode === 0x12) return exactDecimalToNormalized(left.num * right.num, left.scale + right.scale);
  if (opcode === 0x13) return exactDecimalDivisionToNormalized(left, right);
  if (opcode === 0x24) return exactDecimalPowerIdentityToNormalized(left, right);
  if (opcode === 0x36) return exactDecimalMaxToNormalized(left, right);
  if (opcode >= 0x37 && opcode <= 0x39) return exactDecimalBitwiseToNormalized(opcode, y, x);
  if (opcode !== 0x10 && opcode !== 0x11) return undefined;
  const scale = Math.max(left.scale, right.scale);
  const yNum = left.num * pow10BigInt(scale - left.scale);
  const xNum = right.num * pow10BigInt(scale - right.scale);
  return exactDecimalToNormalized(opcode === 0x10 ? yNum + xNum : yNum - xNum, scale);
}

function parseExactDecimal(value: string): ExactDecimalParts | undefined {
  const match = /^(-?)([0-9]+)(?:\.([0-9]+))?$/u.exec(value);
  if (match === null) return undefined;
  const sign = match[1]!;
  const digits = `${match[2]!}${match[3] ?? ""}`.replace(/^0+/u, "") || "0";
  const unsigned = BigInt(digits);
  return {
    num: sign === "-" ? -unsigned : unsigned,
    scale: (match[3] ?? "").length,
  };
}

function decimalIsZero(value: string): boolean {
  const parsed = parseExactDecimal(value);
  return parsed !== undefined && parsed.num === 0n;
}

function decimalIsOne(value: string): boolean {
  const parsed = parseExactDecimal(value);
  return parsed !== undefined && parsed.num === 1n && parsed.scale === 0;
}

function exactDecimalToNormalized(num: bigint, scale: number): string | undefined {
  const sign = num < 0n ? "-" : "";
  const unsigned = num < 0n ? -num : num;
  const rawDigits = unsigned.toString().padStart(scale + 1, "0");
  const point = rawDigits.length - scale;
  const raw = scale === 0
    ? `${sign}${rawDigits}`
    : `${sign}${rawDigits.slice(0, point)}.${rawDigits.slice(point)}`;
  const normalized = normalizePlainDecimal(raw);
  if (normalized === undefined || significantDecimalDigits(normalized) > 8) return undefined;
  return normalized;
}

function exactDecimalDivisionToNormalized(left: ExactDecimalParts, right: ExactDecimalParts): string | undefined {
  if (right.num === 0n) return undefined;
  let numerator = left.num * pow10BigInt(right.scale);
  let denominator = right.num * pow10BigInt(left.scale);
  if (denominator < 0n) {
    numerator = -numerator;
    denominator = -denominator;
  }
  const divisor = gcdBigInt(absBigInt(numerator), denominator);
  numerator /= divisor;
  denominator /= divisor;
  const factors = decimalDenominatorFactors(denominator);
  if (factors === undefined) return undefined;
  const scale = Math.max(factors.twos, factors.fives);
  const scaledNumerator =
    numerator *
    powBigInt(2n, scale - factors.twos) *
    powBigInt(5n, scale - factors.fives);
  return exactDecimalToNormalized(scaledNumerator, scale);
}

function exactDecimalMaxToNormalized(left: ExactDecimalParts, right: ExactDecimalParts): string | undefined {
  if (left.num === 0n || right.num === 0n) return "0";
  const scale = Math.max(left.scale, right.scale);
  const leftNum = left.num * pow10BigInt(scale - left.scale);
  const rightNum = right.num * pow10BigInt(scale - right.scale);
  return exactDecimalToNormalized(leftNum >= rightNum ? leftNum : rightNum, scale);
}

function exactDecimalPowerIdentityToNormalized(exponent: ExactDecimalParts, base: ExactDecimalParts): string | undefined {
  if (base.num === 0n) return "0";
  if (exponent.num === 1n && exponent.scale === 0) return exactDecimalToNormalized(base.num, base.scale);
  if (base.num === 1n && base.scale === 0) return "1";
  if (exponent.num === 0n && base.num > 0n) return "1";
  return undefined;
}

function exactDecimalBitwiseToNormalized(opcode: number, y: string, x: string): string | undefined {
  const left = decimalMantissaDigits(y);
  const right = decimalMantissaDigits(x);
  if (left === undefined || right === undefined) return undefined;
  const result = [8];
  for (let index = 1; index < 8; index += 1) {
    const digit = bitwiseMantissaDigit(opcode, left[index]!, right[index]!);
    if (digit === undefined || digit > 9) return undefined;
    result.push(digit);
  }
  return decimalFromMantissaDigits(result);
}

function decimalMantissaDigits(value: string): number[] | undefined {
  const normalized = parseExactDecimal(value);
  if (normalized === undefined) return undefined;
  if (normalized.num === 0n) return new Array<number>(8).fill(0);
  const digits = absBigInt(normalized.num).toString().slice(0, 8).padEnd(8, "0");
  return [...digits].map((digit) => Number(digit));
}

function decimalFromMantissaDigits(digits: readonly number[]): string | undefined {
  if (digits.length !== 8 || digits.some((digit) => digit < 0 || digit > 9)) return undefined;
  return exactDecimalToNormalized(BigInt(digits.join("")), 7);
}

function bitwiseMantissaDigit(opcode: number, left: number, right: number): number | undefined {
  switch (opcode) {
    case 0x37:
      return left & right;
    case 0x38:
      return left | right;
    case 0x39:
      return left ^ right;
    default:
      return undefined;
  }
}

function exactBigIntSquareRoot(input: bigint): bigint | undefined {
  if (input < 0n) return undefined;
  if (input < 2n) return input;
  let low = 1n;
  let high = input;
  while (low <= high) {
    const mid = (low + high) / 2n;
    const square = mid * mid;
    if (square === input) return mid;
    if (square < input) {
      low = mid + 1n;
    } else {
      high = mid - 1n;
    }
  }
  return undefined;
}

function decimalDenominatorFactors(input: bigint): { readonly twos: number; readonly fives: number } | undefined {
  let value = input;
  let twos = 0;
  let fives = 0;
  while (value % 2n === 0n) {
    value /= 2n;
    twos += 1;
  }
  while (value % 5n === 0n) {
    value /= 5n;
    fives += 1;
  }
  return value === 1n ? { twos, fives } : undefined;
}

function gcdBigInt(left: bigint, right: bigint): bigint {
  let a = left;
  let b = right;
  while (b !== 0n) {
    const next = a % b;
    a = b;
    b = next;
  }
  return a;
}

function absBigInt(value: bigint): bigint {
  return value < 0n ? -value : value;
}

function powBigInt(base: bigint, power: number): bigint {
  return base ** BigInt(power);
}

function pow10BigInt(power: number): bigint {
  return 10n ** BigInt(power);
}

function decimalIntegerPart(value: string): string | undefined {
  const match = /^(-?)([0-9]+)(?:\.[0-9]+)?$/u.exec(value);
  if (match === null) return undefined;
  const sign = match[1]!;
  const integer = match[2]!.replace(/^0+(?=\d)/u, "");
  if (integer === "0") return "0";
  return `${sign}${integer}`;
}

function decimalFractionPart(value: string): string | undefined {
  const match = /^(-?)([0-9]+)(?:\.([0-9]+))?$/u.exec(value);
  if (match === null) return undefined;
  const fraction = (match[3] ?? "").replace(/0+$/u, "");
  if (fraction.length === 0) return "0";
  return `${match[1]!}0.${fraction}`;
}

function decimalFractionPartShapeFact(value: string): X2ShapeFact | undefined {
  const match = /^(-?)([0-9]+)(?:\.([0-9]+))?$/u.exec(value);
  if (match === null) return undefined;
  const sign = match[1]!;
  const fraction = (match[3] ?? "").replace(/0+$/u, "");
  if (fraction.length === 0) return decimalMantissaShapeFact(sign === "-" ? "-0" : "0");
  const leadingZeroes = /^0*/u.exec(fraction)?.[0].length ?? 0;
  const significant = fraction.slice(leadingZeroes);
  if (significant.length === 0) return decimalMantissaShapeFact(sign === "-" ? "-0" : "0");
  const mantissa = significant.length === 1
    ? `${sign}${significant}`
    : `${sign}${significant[0]!}.${significant.slice(1)}`;
  return decimalExponentShapeFact(mantissa, `-${leadingZeroes + 1}`);
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

function plainProducesStableConstantExpressionValues(
  op: Extract<IrOp, { kind: "plain" }>,
): Set<X2ValueFact> {
  const output = new Set<X2ValueFact>();
  const constant = plainProducesStableConstantExpressionValue(op);
  if (constant !== undefined) output.add(constant);
  const decimal = plainProducesStableConstantDecimalValue(op);
  if (decimal !== undefined) output.add(decimal);
  return output;
}

function plainProducesStableConstantDecimalValue(
  op: Extract<IrOp, { kind: "plain" }>,
): X2ValueFact | undefined {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op) || hasIrRoles(op)) return undefined;
  if (op.opcode !== 0x20) return undefined;
  const info = getOpcode(op.opcode);
  if (info.risk !== "documented" || info.x2Effect !== "preserves" || info.stackEffect !== "shifts") {
    return undefined;
  }
  return decimalValueFact("3.1415926", "normalized");
}

function plainProducesStableConstantShapeFacts(
  op: Extract<IrOp, { kind: "plain" }>,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  const decimal = plainProducesStableConstantDecimalValue(op);
  const value = decimal === undefined ? undefined : normalizedDecimalValueFromFact(decimal);
  const shape = value === undefined ? undefined : exactDecimalDisplayShapeFact(value);
  if (shape !== undefined) output.add(shape);
  return output;
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

function plainXShapeAfterNonPreservingOp(
  op: Extract<IrOp, { kind: "plain" }>,
  x: X2ValueSet | undefined = undefined,
  y: X2ValueSet | undefined = undefined,
  xShape: X2ShapeSet | undefined = undefined,
  yShape: X2ShapeSet | undefined = undefined,
): Set<X2ShapeFact> {
  const output = plainProducesConcreteDecimalShapeFacts(op, x, xShape);
  for (const fact of plainProducesConcreteUnaryShapeFacts(op, x, xShape)) output.add(fact);
  for (const fact of plainProducesConcreteBinaryShapeFacts(op, y, x, yShape, xShape)) output.add(fact);
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

export function x2NextXPreservingX2SyncIndex(
  ops: readonly IrOp[],
  start: number,
  context: DirectReturnAnalysisContext,
): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (x2IsXPreservingSyncOp(ops, op, context)) return index;
    if (!x2IsStackXAndX2PreservingGapOp(ops, op, index, context)) return undefined;
  }
  return undefined;
}

export function x2PreviousXPreservingX2SyncIndex(
  ops: readonly IrOp[],
  end: number,
  context: DirectReturnAnalysisContext,
): number | undefined {
  for (let index = end - 1; index >= 0; index -= 1) {
    const op = ops[index]!;
    if (
      x2IsPreviousXPreservingSyncOp(ops, op, context) &&
      !x2ConditionalJumpCanEnterScannedRange(op, index, end, context)
    ) {
      return index;
    }
    if (!x2IsStackXAndX2PreservingGapOp(ops, op, index, context)) return undefined;
  }
  return undefined;
}

function x2IsXPreservingSyncOp(
  ops: readonly IrOp[],
  op: IrOp,
  context: DirectReturnAnalysisContext,
): boolean {
  if (x2IsFallthroughSyncConditionalOp(op)) return true;
  if (isKnownReturnCallOp(op) && x2KnownReturnCallPreservesStackXAndX2(ops, op, context)) return true;
  if (op.kind === "return" && !hasRewriteBarrier(op) && !isDisplayFocusSensitive(op)) return true;
  return x2IsPlainXPreservingX2Sync(op);
}

function x2IsPreviousXPreservingSyncOp(
  ops: readonly IrOp[],
  op: IrOp,
  context: DirectReturnAnalysisContext,
): boolean {
  if (x2IsFallthroughSyncConditionalOp(op)) return true;
  if (isKnownReturnCallOp(op) && x2KnownReturnCallPreservesStackXAndX2(ops, op, context)) return true;
  return x2IsPlainXPreservingX2Sync(op);
}

function x2ConditionalJumpCanEnterScannedRange(
  op: IrOp,
  index: number,
  end: number,
  context: DirectReturnAnalysisContext,
): boolean {
  if (op.kind !== "cjump" && op.kind !== "loop") return false;
  const target = typeof op.target === "string"
    ? context.labels.get(op.target)
    : context.addresses.get(op.target);
  return target !== undefined && target > index && target <= end;
}

function x2IsFallthroughSyncConditionalOp(op: IrOp): boolean {
  if (op.kind !== "cjump" && op.kind !== "loop" && op.kind !== "indirect-cjump") return false;
  if (op.kind === "indirect-cjump" && knownIndirectFlowTarget(op) === undefined) return false;
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  const effect = analyzeX2StackEffect(op);
  if (!effect.stackPreserves) return false;
  const conditional = getOpcode(op.opcode).conditionalX2Effect;
  return conditional?.fallthrough === "affects" && conditional.jump === "preserves";
}

function x2IsPlainXPreservingX2Sync(op: IrOp): boolean {
  if (op.kind !== "plain" || hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  const effect = analyzeX2StackEffect(op);
  return effect.stackPreserves && effect.x2Affects && plainPreservesXValue(op);
}

function x2IsStackXAndX2PreservingGapOp(
  ops: readonly IrOp[],
  op: IrOp,
  index: number,
  context: DirectReturnAnalysisContext,
): boolean {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  if (op.kind === "label") return !context.labelEntries.has(index);
  if (isKnownReturnCallOp(op)) return x2KnownReturnCallPreservesStackXAndX2(ops, op, context);
  if (x2IsFallthroughX2PreservingGapOp(op)) return true;
  switch (op.kind) {
    case "store":
    case "indirect-store":
    case "orphan-address":
      return true;
    case "plain": {
      const effect = analyzeX2StackEffect(op);
      return effect.stackPreserves && effect.x2Preserves && plainPreservesXValue(op);
    }
    default:
      return false;
  }
}

export function x2KnownReturnCallPreservesStackXAndX2(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
): boolean {
  return nestedReturnCallRangeIsTransparent(
    ops,
    call,
    context,
    x2IsStackXAndX2PreservingLinearOp,
    x2ReturnMemo(ops, x2StackXAndX2ReturnMemo),
    new Set(),
  );
}

function x2IsFallthroughX2PreservingGapOp(op: IrOp): boolean {
  if (op.kind !== "cjump" && op.kind !== "loop" && op.kind !== "indirect-cjump") return false;
  if (op.kind === "indirect-cjump" && knownIndirectFlowTarget(op) === undefined) return false;
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  const effect = analyzeX2StackEffect(op);
  if (!effect.stackPreserves) return false;
  const conditional = getOpcode(op.opcode).conditionalX2Effect;
  return conditional?.fallthrough === "preserves";
}

function x2IsStackXAndX2PreservingLinearOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  switch (op.kind) {
    case "label":
    case "store":
    case "indirect-store":
    case "orphan-address":
      return true;
    case "plain": {
      const effect = analyzeX2StackEffect(op);
      return effect.stackPreserves && effect.x2Preserves && plainPreservesXValue(op);
    }
    default:
      return false;
  }
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
  return nestedReturnCallRangeIsTransparent(
    ops,
    call,
    context,
    x2IsStrictStackPreservingLinearOp,
    x2ReturnMemo(ops, x2StrictStackReturnMemo),
    new Set(),
  );
}

function x2ReturnMemo(
  ops: readonly IrOp[],
  cache: WeakMap<readonly IrOp[], Map<number, boolean>>,
): Map<number, boolean> {
  let memo = cache.get(ops);
  if (memo === undefined) {
    memo = new Map();
    cache.set(ops, memo);
  }
  return memo;
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
  readonly y: RegisterValueSet;
  readonly x2: RegisterValueSet;
}

export interface X2RegisterEdgeState {
  readonly x?: RegisterValueSet | undefined;
  readonly y?: RegisterValueSet | undefined;
  readonly x2?: RegisterValueSet | undefined;
}

export interface X2ValueDataflowOptions {
  readonly trackRegisterMemory?: boolean;
}

export type X2DataflowEdgeKind = Edge["kind"];

const registerValueGraphCache = new WeakMap<readonly IrOp[], Edge[][]>();
const x2RegisterStatesCache = new WeakMap<readonly IrOp[], Array<RegisterValueSet | undefined>>();
const x2ValueStatesCache = new WeakMap<
  readonly IrOp[],
  {
    plain?: Array<X2ValueDataflowState | undefined> | undefined;
    registerMemory?: Array<X2ValueDataflowState | undefined> | undefined;
  }
>();
const x2RestoreBoundaryStatesCache = new WeakMap<readonly IrOp[], boolean[]>();
const x2DotRestoreGapStatesCache = new WeakMap<readonly IrOp[], boolean[]>();
const x2ImmediateSyncStatesCache = new WeakMap<readonly IrOp[], boolean[]>();
const recallRemovalAnalysisCache = new WeakMap<readonly IrOp[], Map<number, RecallRemovalCacheEntry[]>>();

interface RecallRemovalCacheEntry {
  readonly x2RegisterState: RegisterValueSet | undefined;
  readonly x2ValueState: X2ValueDataflowState | undefined;
  readonly context: DirectReturnAnalysisContext | undefined;
  readonly result: RecallRemovalAnalysis | undefined;
}

export function computeX2RegisterStates(ops: readonly IrOp[]): Array<RegisterValueSet | undefined> {
  if (ops.length === 0) return [];
  const cached = x2RegisterStatesCache.get(ops);
  if (cached !== undefined) return cached;
  const edges = registerValueGraphForOps(ops);
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

  const result = inStates.map((state) => state?.x2);
  x2RegisterStatesCache.set(ops, result);
  return result;
}

export function computeX2ValueStates(
  ops: readonly IrOp[],
  options: X2ValueDataflowOptions = {},
): Array<X2ValueDataflowState | undefined> {
  if (ops.length === 0) return [];
  const trackRegisterMemory = options.trackRegisterMemory === true;
  const cachedByMode = x2ValueStatesCache.get(ops);
  const cached = trackRegisterMemory ? cachedByMode?.registerMemory : cachedByMode?.plain;
  if (cached !== undefined) return cached;
  const edges = registerValueGraphForOps(ops);
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
        const transferred = transferX2ValueDataflowState(
          input,
          ops[index]!,
          edge.kind,
          trackRegisterMemory,
          index,
          edgeTargetStartsWithVp(ops, edge.target),
        );
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

  const nextCachedByMode = cachedByMode ?? {};
  if (trackRegisterMemory) {
    nextCachedByMode.registerMemory = inStates;
  } else {
    nextCachedByMode.plain = inStates;
  }
  x2ValueStatesCache.set(ops, nextCachedByMode);
  return inStates;
}

function edgeTargetStartsWithVp(ops: readonly IrOp[], target: number): boolean {
  for (let index = target; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    return op.kind === "plain" && op.opcode === 0x0c && !hasRewriteBarrier(op);
  }
  return false;
}

export function x2ValueSetHasIntersection(left: X2ValueSet | undefined, right: X2ValueSet | undefined): boolean {
  if (left === undefined || right === undefined) return false;
  const rightSet = canonicalX2ValueSet(right);
  for (const value of canonicalX2ValueSet(left)) {
    if (rightSet.has(value)) return true;
  }
  return false;
}

export function x2ValueSetHasFact(input: X2ValueSet | undefined, fact: X2ValueFact): boolean {
  const canonical = canonicalX2ValueFactIfValid(fact);
  return canonical !== undefined && canonicalX2ValueSet(input).has(canonical);
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
  if (decimal !== null) return normalizePlainDecimal(decimal[1]!);
  if (!fact.startsWith("expr-key:")) return undefined;
  const values = new Set<string>();
  for (const value of stableExpressionKeyValueSet(fact) ?? []) {
    const normalized = normalizedDecimalValueFromFact(value);
    if (normalized !== undefined) values.add(normalized);
  }
  return values.size === 1 ? [...values][0] : undefined;
}

export function x2ShapeFactRestoredVisibleDecimal(fact: X2ShapeFact): string | undefined {
  for (const shape of decimalDisplayShapeFacts(new Set([fact]))) {
    const model = x2ShapeDataModelForFact(shape);
    if (model.kind === "mantissa" && model.radix === "decimal") return model.normalizedDecimal;
    if (model.kind === "exponent-entry" && model.mantissa.radix === "decimal") return model.normalizedDecimal;
  }
  return undefined;
}

export function x2ShapeSetRestoredVisibleDecimals(input: X2ShapeSet | undefined): Set<string> {
  const output = new Set<string>();
  for (const fact of input ?? []) {
    const decimal = x2ShapeFactRestoredVisibleDecimal(fact);
    if (decimal !== undefined) output.add(decimal);
  }
  return output;
}

function x2ValueSetRestoredVisibleDecimals(input: X2ValueSet | undefined): Set<string> {
  const output = new Set<string>();
  for (const fact of input ?? []) {
    const visible = x2ValueFactRestoredVisibleDecimal(fact);
    if (visible !== undefined) output.add(visible);
  }
  return output;
}

export function x2ValueShapeSetRestoredVisibleDecimals(
  values: X2ValueSet | undefined,
  shapes: X2ShapeSet | undefined,
): Set<string> {
  const output = x2ValueSetRestoredVisibleDecimals(values);
  for (const decimal of x2ShapeSetRestoredVisibleDecimals(shapes)) output.add(decimal);
  return output;
}

export function x2ValueShapeSetHasRestoredVisibleDecimal(
  values: X2ValueSet | undefined,
  shapes: X2ShapeSet | undefined,
  fact: X2ValueFact,
): boolean {
  const visible = x2ValueFactRestoredVisibleDecimal(fact);
  if (visible === undefined) return false;
  return x2ValueShapeSetRestoredVisibleDecimals(values, shapes).has(visible);
}

export function x2ValueShapeSetsHaveSameRestoredVisibleDecimal(
  leftValues: X2ValueSet | undefined,
  leftShapes: X2ShapeSet | undefined,
  rightValues: X2ValueSet | undefined,
  rightShapes: X2ShapeSet | undefined,
): boolean {
  const left = x2ValueShapeSetRestoredVisibleDecimals(leftValues, leftShapes);
  if (left.size === 0) return false;

  for (const decimal of x2ValueShapeSetRestoredVisibleDecimals(rightValues, rightShapes)) {
    if (left.has(decimal)) return true;
  }
  return false;
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
  return x2ValueFactIsNormalizedDecimal(fact) && x2ValueSetHasFact(input, fact);
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
    if (!decimalMantissaShapeRawIsValid(raw)) return { kind: "unknown", raw: fact, safety: "unknown" };
    const normalized = normalizePlainDecimal(raw);
    return {
      kind: "decimal-mantissa",
      raw,
      normalized,
      safety: decimalMantissaShapeSafety(raw, normalized),
    };
  }
  const exponent = /^exponent:([^:]*):([^:]*):decimal$/u.exec(fact);
  if (exponent !== null) {
    const mantissaRaw = exponent[1]!;
    const exponentRaw = exponent[2]!;
    if (
      !decimalExponentMantissaRawIsValid(mantissaRaw) ||
      canonicalExponentShapeRaw(exponentRaw) === undefined
    ) {
      return { kind: "unknown", raw: fact, safety: "unknown" };
    }
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
    const raw = hex[1]!;
    if (!structuralShapeRawIsValid(raw)) return { kind: "unknown", raw: fact, safety: "unknown" };
    return { kind: "hex-mantissa", raw, safety: "structuralOnly" };
  }
  const hexExponent = /^hex-exponent:([^:]*):([^:]*)$/u.exec(fact);
  if (hexExponent !== null) {
    if (
      !structuralShapeRawIsValid(hexExponent[1]!) ||
      canonicalExponentShapeRaw(hexExponent[2]!) === undefined
    ) {
      return { kind: "unknown", raw: fact, safety: "unknown" };
    }
    return {
      kind: "hex-exponent",
      mantissa: hexExponent[1]!,
      exponent: hexExponent[2]!,
      safety: "structuralOnly",
    };
  }
  const superMantissa = /^super:(.*)$/u.exec(fact);
  if (superMantissa !== null) {
    const raw = superMantissa[1]!;
    if (!superShapeRawIsValid(raw)) return { kind: "unknown", raw: fact, safety: "unknown" };
    return { kind: "super-mantissa", raw, safety: "structuralOnly" };
  }
  const superExponent = /^super-exponent:([^:]*):([^:]*)$/u.exec(fact);
  if (superExponent !== null) {
    if (
      !superShapeRawIsValid(superExponent[1]!) ||
      canonicalExponentShapeRaw(superExponent[2]!) === undefined
    ) {
      return { kind: "unknown", raw: fact, safety: "unknown" };
    }
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
  if (mantissa !== null) {
    return decimalMantissaShapeRawIsValid(mantissa[1]!)
      ? decimalMantissaDataModel(mantissa[1]!)
      : { kind: "unknown", raw: fact, safety: "unknown" };
  }
  const exponent = /^exponent:([^:]*):([^:]*):decimal$/u.exec(fact);
  if (exponent !== null) {
    const exponentRaw = canonicalExponentShapeRaw(exponent[2]!);
    if (!decimalExponentMantissaRawIsValid(exponent[1]!) || exponentRaw === undefined) {
      return { kind: "unknown", raw: fact, safety: "unknown" };
    }
    const normalizedDecimal = normalizedExponentEntryValue(exponent[1]!, exponentRaw);
    const mantissaModel = decimalMantissaDataModel(exponent[1]!);
    return {
      kind: "exponent-entry",
      mantissa: mantissaModel,
      exponentRaw,
      exponentSign: exponentRaw.startsWith("-") ? "-" : "",
      exponentDigits: shapeDigits(exponentRaw),
      normalizedDecimal,
      closedDecimalDisplay: normalizedDecimal === undefined ? undefined : exactDecimalDisplayShapeFact(normalizedDecimal),
      safety: "errorProne",
    };
  }
  const hex = /^hex:(.*):mantissa$/u.exec(fact);
  if (hex !== null) {
    const raw = hex[1]!;
    return structuralShapeRawIsValid(raw)
      ? structuralMantissaDataModel("hex", raw, "structuralOnly")
      : { kind: "unknown", raw: fact, safety: "unknown" };
  }
  const hexExponent = /^hex-exponent:([^:]*):([^:]*)$/u.exec(fact);
  if (hexExponent !== null) {
    const exponentRaw = canonicalExponentShapeRaw(hexExponent[2]!);
    if (!structuralShapeRawIsValid(hexExponent[1]!) || exponentRaw === undefined) {
      return { kind: "unknown", raw: fact, safety: "unknown" };
    }
    const mantissa = structuralMantissaDataModel("hex", hexExponent[1]!, "structuralOnly");
    return {
      kind: "exponent-entry",
      mantissa,
      exponentRaw,
      exponentSign: exponentRaw.startsWith("-") ? "-" : "",
      exponentDigits: shapeDigits(exponentRaw),
      closedStructuralMantissa: closedStructuralExponentMantissaModel(mantissa, exponentRaw),
      safety: "structuralOnly",
    };
  }
  const superMantissa = /^super:(.*)$/u.exec(fact);
  if (superMantissa !== null) {
    const raw = superMantissa[1]!;
    return superShapeRawIsValid(raw)
      ? structuralMantissaDataModel("super", raw, "structuralOnly")
      : { kind: "unknown", raw: fact, safety: "unknown" };
  }
  const superExponent = /^super-exponent:([^:]*):([^:]*)$/u.exec(fact);
  if (superExponent !== null) {
    const exponentRaw = canonicalExponentShapeRaw(superExponent[2]!);
    if (!superShapeRawIsValid(superExponent[1]!) || exponentRaw === undefined) {
      return { kind: "unknown", raw: fact, safety: "unknown" };
    }
    const mantissa = structuralMantissaDataModel("super", superExponent[1]!, "structuralOnly");
    return {
      kind: "exponent-entry",
      mantissa,
      exponentRaw,
      exponentSign: exponentRaw.startsWith("-") ? "-" : "",
      exponentDigits: shapeDigits(exponentRaw),
      closedStructuralMantissa: closedStructuralExponentMantissaModel(mantissa, exponentRaw),
      safety: "structuralOnly",
    };
  }
  return { kind: "unknown", raw: fact, safety: "unknown" };
}

export function x2ShapeDataModels(input: X2ShapeSet | undefined): X2ShapeDataModel[] {
  const models: X2ShapeDataModel[] = [];
  for (const fact of canonicalShapeSet(input)) models.push(x2ShapeDataModelForFact(fact));
  return models;
}

export function x2MantissaShapeFactFromModel(model: X2MantissaDataModel): X2ShapeFact | undefined {
  return x2MantissaShapeFactFromParts(model.radix, model.canonical);
}

export function x2ShapeFactFromDataModel(model: X2ShapeDataModel): X2ShapeFact | undefined {
  if (model.kind === "mantissa") return x2MantissaShapeFactFromModel(model);
  if (model.kind !== "exponent-entry") return undefined;
  if (model.mantissa.radix === "decimal") {
    const exponent = canonicalExponentShapeRaw(model.exponentRaw);
    return exponent === undefined || !decimalExponentMantissaRawIsValid(model.mantissa.canonical)
      ? undefined
      : decimalExponentShapeFact(model.mantissa.canonical, exponent);
  }
  const mantissa = x2MantissaShapeFactFromModel(model.mantissa);
  return mantissa === undefined ? undefined : x2ExponentShapeFactFromMantissaFact(mantissa, model.exponentRaw);
}

export function x2CanonicalShapeFact(fact: X2ShapeFact): X2ShapeFact {
  return x2CanonicalShapeFactIfValid(fact) ?? fact;
}

function x2CanonicalShapeFactIfValid(fact: X2ShapeFact): X2ShapeFact | undefined {
  return x2ShapeFactFromDataModel(x2ShapeDataModelForFact(fact));
}

export function x2ExponentShapeFactFromMantissaFact(
  fact: X2ShapeFact,
  exponentRaw: string,
): X2ShapeFact | undefined {
  const exponent = canonicalExponentShapeRaw(exponentRaw);
  if (exponent === undefined) return undefined;
  const decimalMantissa = /^mantissa:(.*):decimal$/u.exec(fact);
  if (decimalMantissa !== null) {
    const mantissa = canonicalShapeRaw(decimalMantissa[1]!);
    return decimalExponentMantissaRawIsValid(mantissa) ? decimalExponentShapeFact(mantissa, exponent) : undefined;
  }
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "mantissa") return undefined;
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

export function x2ClosedDecimalExponentDisplayShapeFact(fact: X2ShapeFact): X2ShapeFact | undefined {
  const model = x2ShapeDataModelForFact(fact);
  return model.kind === "exponent-entry" && model.mantissa.radix === "decimal"
    ? model.closedDecimalDisplay
    : undefined;
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
  const rightDigits = structuralConcatRightDigitRun(right);
  return rightDigits === undefined ? undefined : x2StructuralMantissaAppendDigitsShapeFact(left, rightDigits);
}

function structuralConcatRightDigitRun(right: X2ShapeFact): string | undefined {
  const rawDigits = pureMantissaDigitRunFromShapeFact(right);
  if (rawDigits !== undefined) return rawDigits;

  const restoredDigits = new Set<string>();
  for (const fact of structuralMantissaShapeFacts(x2StructuralRestoreShapeFacts(new Set([right])))) {
    const digits = pureMantissaDigitRunFromShapeFact(fact);
    if (digits !== undefined) restoredDigits.add(digits);
  }
  return restoredDigits.size === 1 ? [...restoredDigits][0] : undefined;
}

function pureMantissaDigitRunFromShapeFact(fact: X2ShapeFact): string | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (
    model.kind !== "mantissa" ||
    (model.radix !== "decimal" && model.radix !== "hex" && model.radix !== "super") ||
    model.sign !== "" ||
    model.hasDecimalPoint
  ) {
    return undefined;
  }
  return model.digits.join("");
}

export function x2StructuralMantissaFirstDigitSpliceShapeFact(
  source: X2ShapeFact,
  target: X2ShapeFact,
): X2ShapeFact | undefined {
  const sourceDigit = x2FirstMantissaDigitFromShapeFact(source);
  if (sourceDigit === undefined) return undefined;
  const targetModel = x2ShapeDataModelForFact(target);
  if (
    targetModel.kind !== "mantissa" ||
    (targetModel.radix !== "decimal" && targetModel.radix !== "hex" && targetModel.radix !== "super") ||
    targetModel.sign !== "" ||
    targetModel.digits.length === 0
  ) {
    return undefined;
  }
  const spliced = replaceFirstShapeDigit(targetModel.canonical, sourceDigit);
  if (spliced === undefined) return undefined;
  const radix = structuralFirstDigitSpliceRadix(targetModel, sourceDigit, spliced);
  if (radix === undefined) return undefined;
  return x2MantissaShapeFactFromModel(structuralMantissaDataModel(radix, spliced, "structuralOnly"));
}

export function x2ShapeFactSafety(fact: X2ShapeFact): X2ShapeSafety {
  return x2ShapeDataModelForFact(fact).safety;
}

export function x2ShapeSetSafety(input: X2ShapeSet | undefined): X2ShapeSafety {
  if (input === undefined || input.size === 0) return "unknown";
  let sawDotSafe = false;
  let sawStructural = false;
  let sawUnknown = false;
  for (const model of x2ShapeDataModels(input)) {
    const safety = model.safety;
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
  return x2ShapeSetSafety(effectiveX2StateShape(state));
}

export function x2StateHasDotSafeDecimalX2(state: X2ValueDataflowState | undefined): boolean {
  return x2RestoreSafety(state) === "dotSafeDecimal";
}

export function x2StateHasStructuralShapeX2(state: X2ValueDataflowState | undefined): boolean {
  return x2RestoreSafety(state) === "structuralOnly";
}

export function x2StateHasOnlyDotSafeStructuralMantissaX2(state: X2ValueDataflowState | undefined): boolean {
  return state !== undefined &&
    !x2ValueSetHasConcreteDecimal(state.x2) &&
    x2ShapeSetHasOnlyDotSafeStructuralMantissas(effectiveX2StateShape(state));
}

export function x2CanUseVpDotRestoreAt(
  ops: readonly IrOp[],
  index: number,
  state: X2ValueDataflowState | undefined,
): boolean {
  return x2StateHasVpDotSafeStructuralContextX2(state) && x2VpDotRestoreGapIsSafe(ops, index);
}

export function x2StateHasVpDotSafeStructuralContextX2(state: X2ValueDataflowState | undefined): boolean {
  if (state === undefined || state.structuralVpContext?.kind !== "exponent") return false;
  const x2Shape = effectiveX2StateShape(state);
  if (x2Shape === undefined || x2Shape.size === 0) return false;
  for (const fact of x2Shape) {
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind !== "exponent-entry" || model.safety !== "structuralOnly") return false;
    const mantissa = model.closedStructuralMantissa ?? model.mantissa;
    if (!structuralMantissaHasVpDotSafeLead(mantissa)) return false;
  }
  return true;
}

function structuralMantissaHasVpDotSafeLead(model: X2MantissaDataModel): boolean {
  if ((model.radix !== "hex" && model.radix !== "super") || model.sign !== "") return false;
  for (const digit of model.digits) {
    const value = structuralHexNibbleValue(digit);
    if (value === undefined) return false;
    if (value === 0) continue;
    return value === 13 || value === 14;
  }
  return false;
}

function x2VpDotRestoreGapIsSafe(ops: readonly IrOp[], index: number): boolean {
  let preservingNonEmpty = 0;
  for (let cursor = index - 1; cursor >= 0; cursor -= 1) {
    const op = ops[cursor]!;
    if (op.kind === "label") continue;
    if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
    if (op.kind !== "plain") return false;
    if (op.opcode === 0x0c) return preservingNonEmpty <= 1;
    if (isFreeStandingX2EmptyOp(op)) continue;
    if (plainX2Effect(op) !== "preserves" || hasIrRoles(op)) return false;
    preservingNonEmpty += 1;
    if (preservingNonEmpty > 1) return false;
  }
  return false;
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

export function x2ShapeSetsHaveSameDecimalDisplayShape(
  left: X2ShapeSet | undefined,
  right: X2ShapeSet | undefined,
): boolean {
  const leftShapes = decimalDisplayShapeFacts(left);
  if (leftShapes.size === 0) return false;
  for (const shape of decimalDisplayShapeFacts(right)) {
    if (leftShapes.has(shape)) return true;
  }
  return false;
}

export function x2ShapeSetsHaveSameRestoredDisplayShape(
  left: X2ShapeSet | undefined,
  right: X2ShapeSet | undefined,
): boolean {
  const leftShapes = x2RestoredDisplayShapeFacts(left);
  if (leftShapes.size === 0) return false;
  for (const shape of x2RestoredDisplayShapeFacts(right)) {
    if (leftShapes.has(shape)) return true;
  }
  return false;
}

export function x2RestoredDisplayShapeFacts(input: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const output = decimalDisplayShapeFacts(input);
  for (const fact of x2StructuralRestoreShapeFacts(input)) output.add(fact);
  return output;
}

export function x2ShapeSetsHaveSameStructuralShape(
  left: X2ShapeSet | undefined,
  right: X2ShapeSet | undefined,
): boolean {
  if (left === undefined || right === undefined) return false;
  const leftShapes = x2StructuralRestoreShapeFacts(left);
  const rightShapes = x2StructuralRestoreShapeFacts(right);
  for (const shape of leftShapes) {
    if (rightShapes.has(shape)) return true;
  }
  return false;
}

export function x2StructuralRestoreShapeFacts(input: X2ShapeSet | undefined): Set<X2ShapeFact> {
  return structuralRestoreShapeFacts(canonicalStructuralShapeFacts(input));
}

export function x2ShapeSetsHaveSameDotSafeStructuralMantissa(
  left: X2ShapeSet | undefined,
  right: X2ShapeSet | undefined,
): boolean {
  const leftKeys = dotSafeStructuralMantissaRestoreKeys(left);
  if (leftKeys.size === 0) return false;
  for (const key of dotSafeStructuralMantissaRestoreKeys(right)) {
    if (leftKeys.has(key)) return true;
  }
  return false;
}

export function x2ShapeSetHasOnlyDotSafeStructuralMantissas(input: X2ShapeSet | undefined): boolean {
  const shapes = canonicalStructuralShapeFacts(input);
  if (shapes.size === 0) return false;
  for (const fact of shapes) {
    if (dotSafeStructuralMantissaRestoreKeys(new Set([fact])).size === 0) return false;
  }
  return true;
}

function dotSafeStructuralMantissaRestoreKeys(input: X2ShapeSet | undefined): Set<string> {
  const keys = new Set<string>();
  for (const fact of structuralMantissaShapeFacts(canonicalStructuralShapeFacts(input))) {
    const model = x2ShapeDataModelForFact(fact);
    if (
      model.kind !== "mantissa" ||
      model.radix !== "hex" ||
      model.sign !== "" ||
      model.hasDecimalPoint ||
      model.digits.length !== 1
    ) {
      continue;
    }
    const digit = structuralHexNibbleValue(model.digits[0]!);
    // Emulator probes show that single A/B/C hidden-X2 dot restores are
    // display-equivalent to recalls; D/E error and F normalizes to zero.
    if (digit === 10 || digit === 11 || digit === 12) keys.add(`hex-digit:${digit}`);
  }
  return keys;
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
  return state !== undefined && x2ShapeSetsHaveSameDotSafeDecimal(
    effectiveVisibleXStateShape(state),
    effectiveX2StateShape(state),
  );
}

export function x2StateHasSameStructuralShapeInXAndX2(state: X2ValueDataflowState | undefined): boolean {
  return state !== undefined && x2ShapeSetsHaveSameStructuralShape(
    effectiveVisibleXStateShape(state),
    effectiveX2StateShape(state),
  );
}

export function x2StateHasSameClosedSignChangeSourceInXAndX2(
  state: X2ValueDataflowState | undefined,
): boolean {
  return state !== undefined &&
    (
      x2ValueSetHasIntersection(state.x, state.x2) ||
      x2StateHasSameDotSafeDecimalInXAndX2(state) ||
      x2ShapeSetsHaveSameRestoredDisplayShape(effectiveVisibleXStateShape(state), effectiveX2StateShape(state)) ||
      x2StateHasSameRestoredVisibleDecimalInXAndX2(state)
    );
}

export function x2StateHasSameDotSafeStructuralMantissaInXAndX2(
  state: X2ValueDataflowState | undefined,
): boolean {
  return state !== undefined && x2ShapeSetsHaveSameDotSafeStructuralMantissa(
    effectiveVisibleXStateShape(state),
    effectiveX2StateShape(state),
  );
}

export function x2StateIsClosedPlainContext(state: X2ValueDataflowState | undefined): boolean {
  return state?.entry.kind === "closed" &&
    (state.vpContext === undefined || state.vpContext.kind === "none") &&
    (state.structuralVpContext === undefined || state.structuralVpContext.kind === "none");
}

export function x2StateHasSameDotRestoreValueInXAndX2(state: X2ValueDataflowState | undefined): boolean {
  return x2ValueSetHasIntersection(state?.x, state?.x2) ||
    x2StateHasSameDotSafeDecimalInXAndX2(state) ||
    x2StateHasSameDotSafeStructuralMantissaInXAndX2(state);
}

export function x2StateHasSameRestoredVisibleDecimalInXAndX2(
  state: X2ValueDataflowState | undefined,
): boolean {
  return x2ValueShapeSetsHaveSameRestoredVisibleDecimal(state?.x, state?.xShape, state?.x2, state?.x2Shape);
}

function effectiveVisibleXStateShape(state: X2ValueDataflowState | undefined): X2ShapeSet | undefined {
  return state === undefined ? undefined : shapeSetWithStableExpressionValueShapes(state.xShape, state.x);
}

function effectiveX2StateShape(state: X2ValueDataflowState | undefined): X2ShapeSet | undefined {
  return state === undefined ? undefined : shapeSetWithStableExpressionValueShapes(state.x2Shape, state.x2);
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

export function x2CanUseSourceDotRestoreAt(
  ops: readonly IrOp[],
  index: number,
  state: X2ValueDataflowState | undefined,
  dotSafe: boolean,
  immediateSync: boolean,
  sourceProvesFreeStandingRestore: boolean,
  context?: DirectReturnAnalysisContext,
): boolean {
  return x2CanUseDotRestoreAt(ops, index, state, dotSafe, immediateSync, context) ||
    (
      sourceProvesFreeStandingRestore &&
      x2NormalizedDecimalRestoreGapIsFreeStanding(ops, index, context)
    );
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
    return isFreeStandingX2SignChangeOp(op) &&
      x2StateIsClosedPlainContext(state) &&
      (
        !x2StateHasUnsafeDotRestoreShapeX2(state) ||
        x2StateHasOnlyDotSafeStructuralMantissaX2(state)
      ) &&
      x2StateHasSameClosedSignChangeSourceInXAndX2(state);
  }
  return false;
}

export function isFreeStandingX2SignChangeOp(op: IrOp): op is Extract<IrOp, { kind: "plain" }> {
  return op.kind === "plain" &&
    op.opcode === X2_SIGN_CHANGE_OPCODE &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasIrRoles(op);
}

export function isFreeStandingX2EmptyOp(op: IrOp): boolean {
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
  return stringSetsHaveIntersection(vpEntrySourceKeys(left), vpEntrySourceKeys(right));
}

export function x2StatesHaveSameVpEntrySignSource(
  left: X2ValueDataflowState | undefined,
  right: X2ValueDataflowState | undefined,
): boolean {
  if (left === undefined || right === undefined) return false;
  return stringSetsHaveIntersection(
    vpEntrySignSourceKeys(left),
    mergeStringSets(vpEntrySourceKeys(right), vpEntrySignSourceKeys(right)),
  );
}

export function x2StatesHaveSameExplicitVpEntrySignSource(
  left: X2ValueDataflowState | undefined,
  right: X2ValueDataflowState | undefined,
): boolean {
  if (left === undefined || right === undefined) return false;
  return stringSetsHaveIntersection(
    explicitVpEntrySignSourceKeys(left),
    explicitVpEntrySignSourceKeys(right),
  );
}

export function x2StateCanDiscardRestoreRunBeforeProvedVp(
  beforeRun: X2ValueDataflowState | undefined,
  beforeVp: X2ValueDataflowState | undefined,
): boolean {
  const context = analyzeX2VpShapeContext(beforeRun);
  if (context.kind === "active-mantissa") {
    return stringSetsHaveIntersection(activeMantissaVpSourceKeys(context), vpEntrySourceKeys(beforeVp));
  }
  return x2StatesHaveSameVpEntrySource(beforeRun, beforeVp);
}

export interface X2RestoreGapBeforeVpScan {
  readonly vpIndex: number | undefined;
  readonly blockedIndex: number | undefined;
  readonly sawRestoreGap: boolean;
  readonly sawSignRestore: boolean;
}

export function x2HasOnlyRestoreGapBeforeVp(
  ops: readonly IrOp[],
  start: number,
  context?: DirectReturnAnalysisContext,
): boolean {
  const scan = x2RestoreGapBeforeVp(ops, start, context);
  return scan.sawRestoreGap && scan.vpIndex !== undefined;
}

export function x2ReplacementDotHasOnlyRestoreGapBeforeVp(
  ops: readonly IrOp[],
  start: number,
  context?: DirectReturnAnalysisContext,
): boolean {
  return x2RestoreGapBeforeVp(ops, start, context).vpIndex !== undefined;
}

export function x2HasSignRestoreGapBeforeVp(
  ops: readonly IrOp[],
  start: number,
  context?: DirectReturnAnalysisContext,
): boolean {
  const scan = x2RestoreGapBeforeVp(ops, start, context);
  return scan.sawSignRestore && scan.vpIndex !== undefined;
}

export function x2RestoreGapBeforeVp(
  ops: readonly IrOp[],
  start: number,
  context?: DirectReturnAnalysisContext,
): X2RestoreGapBeforeVpScan {
  let sawRestoreGap = false;
  let sawSign = false;
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    if (isFreeStandingX2RestoreGapOp(op)) {
      sawRestoreGap = true;
      if (isFreeStandingX2SignChangeOp(op)) sawSign = true;
      continue;
    }
    if (
      context !== undefined &&
      isKnownReturnCallOp(op) &&
      x2RestoreGapDirectReturnDoesNotObserveRestore(ops, op, context)
    ) continue;
    const isVp = isFreeStandingX2VpOp(op);
    return {
      vpIndex: isVp ? index : undefined,
      blockedIndex: isVp ? undefined : index,
      sawRestoreGap,
      sawSignRestore: sawSign,
    };
  }
  return {
    vpIndex: undefined,
    blockedIndex: undefined,
    sawRestoreGap,
    sawSignRestore: sawSign,
  };
}

export function x2RestoreGapDirectReturnDoesNotObserveRestore(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
): boolean {
  return nestedReturnCallRangeIsTransparent(
    ops,
    call,
    context,
    isLinearX2RestoreGapTransparentOp,
    x2ReturnMemo(ops, x2RestoreGapReturnMemo),
    new Set(),
  );
}

function isLinearX2RestoreGapTransparentOp(op: IrOp): boolean {
  return op.kind === "orphan-address" ||
    isFreeStandingX2EmptyOp(op);
}

export function isFreeStandingX2RestoreGapOp(op: IrOp): boolean {
  return op.kind === "plain" &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasIrRoles(op) &&
    (op.opcode === X2_SIGN_CHANGE_OPCODE || (op.opcode >= X2_EMPTY_OPCODE_START && op.opcode <= X2_EMPTY_OPCODE_END));
}

export function isFreeStandingX2VpOp(op: IrOp): boolean {
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
    (state.structuralEntry === undefined || state.structuralEntry.kind === "none") &&
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
  const cached = x2RestoreBoundaryStatesCache.get(ops);
  if (cached !== undefined) return cached;
  const edges = registerValueGraphForOps(ops);
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

  const result = inStates.map((state) => state === "boundary");
  x2RestoreBoundaryStatesCache.set(ops, result);
  return result;
}

export function computeX2DotRestoreGapStates(ops: readonly IrOp[]): boolean[] {
  if (ops.length === 0) return [];
  const cached = x2DotRestoreGapStatesCache.get(ops);
  if (cached !== undefined) return cached;
  const edges = registerValueGraphForOps(ops);
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

  const result = inStates.map((state) => state === "safe");
  x2DotRestoreGapStatesCache.set(ops, result);
  return result;
}

export function computeX2ImmediateSyncStates(ops: readonly IrOp[]): boolean[] {
  if (ops.length === 0) return [];
  const cached = x2ImmediateSyncStatesCache.get(ops);
  if (cached !== undefined) return cached;
  const edges = registerValueGraphForOps(ops);
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

  const result = inStates.map((state) => state === true);
  x2ImmediateSyncStatesCache.set(ops, result);
  return result;
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
    if (predicate(fact) && x2ValueSetHasFact(state.x2, fact)) return register;
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
    if (x2ValueSetHasFact(state.x2, fact)) return register;
  }
  return undefined;
}

export function recallAlreadySyncedInX2RestoredVisibleDecimal(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  const hiddenDecimals = x2ValueSetRestoredVisibleDecimals(state.x2);
  for (const decimal of dotSafeDecimalShapeValues(state.x2Shape)) hiddenDecimals.add(decimal);
  if (hiddenDecimals.size === 0) return undefined;

  for (const visible of x2ShapeSetRestoredVisibleDecimals(recallDecimalDisplayShapeFacts(op, state, register))) {
    if (hiddenDecimals.has(visible)) return register;
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

export function recallAlreadySyncedInX2VpShape(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  const shapes = recallVpEntryShapeSourceFacts(op, state, register);
  return x2ShapeSetsHaveSameRestoredDisplayShape(state.x2Shape, shapes) ? register : undefined;
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
    if (predicate(fact) && x2ValueSetHasFact(state.x, fact)) return register;
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
    if (x2ValueSetHasFact(state.x, fact)) return register;
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

export function recallAlreadyInXRestoredDisplayShape(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  const shapes = recallVpEntryShapeSourceFacts(op, state, register);
  return x2ShapeSetsHaveSameRestoredDisplayShape(state.xShape, shapes) ? register : undefined;
}

export function recallAlreadyInXRestoredVisibleDecimal(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  const values = recallX2ValueFacts(state, register, true, op);
  const shapes = recallDecimalDisplayShapeFacts(op, state, register);
  return x2ValueShapeSetsHaveSameRestoredVisibleDecimal(state.x, state.xShape, values, shapes)
    ? register
    : undefined;
}

export function recallAlreadyInXDecimalDisplayShape(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): RegisterName | undefined {
  const register = removableRecallValueRegister(op);
  if (register === undefined || state === undefined) return undefined;
  const shapes = recallDecimalDisplayShapeFacts(op, state, register);
  return x2ShapeSetsHaveSameDecimalDisplayShape(state.xShape, shapes) ? register : undefined;
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
    recallAlreadyInXRestoredVisibleDecimal(op, state) === register ||
    recallAlreadyInXRestoredDisplayShape(op, state) === register;
  const x2SyncRegister = recallAlreadySyncedInX2Value(op, state);
  const x2SyncValue =
    (recallAlreadySyncedInX2MemoryValue(op, state) ??
      recallAlreadySyncedInX2PreloadedDecimal(op, state)) !== undefined;
  const x2SyncDisplayValue =
    x2SyncValue !== true &&
      recallAlreadySyncedInX2RestoredVisibleDecimal(op, state) === register
      ? true
      : undefined;
  const x2SyncShape = recallAlreadySyncedInX2StructuralShape(op, state) === register ? true : undefined;
  const x2SyncVpShape =
    x2SyncShape !== true &&
      recallAlreadySyncedInX2VpShape(op, state) === register
      ? true
      : undefined;
  return {
    register,
    inX,
    x2SyncRegister,
    x2SyncValue,
    ...(x2SyncDisplayValue === true ? { x2SyncDisplayValue } : {}),
    ...(x2SyncShape === true ? { x2SyncShape } : {}),
    ...(x2SyncVpShape === true ? { x2SyncVpShape } : {}),
  };
}

export function analyzeRecallRemoval(
  ops: readonly IrOp[],
  recallIndex: number,
  x2RegisterState: RegisterValueSet | undefined,
  x2ValueState: X2ValueDataflowState | undefined,
  context?: DirectReturnAnalysisContext,
): RecallRemovalAnalysis | undefined {
  const op = ops[recallIndex];
  if (op === undefined) return undefined;
  const register = removableRecallValueRegister(op);
  if (register === undefined) return undefined;
  const cached = cachedRecallRemovalAnalysis(ops, recallIndex, x2RegisterState, x2ValueState, context);
  if (cached !== undefined) return cached.result;
  const valueProof = recallValueProof(op, x2ValueState);
  const redundantSyncRegister = recallAlreadySyncedInX2(op, x2RegisterState) ?? valueProof?.x2SyncRegister;
  const redundantSyncValue = valueProof?.x2SyncValue === true;
  const redundantSyncDisplayValue = valueProof?.x2SyncDisplayValue === true;
  const redundantSyncDisplayValueForContext =
    redundantSyncDisplayValue &&
    !removingRecallCanExposeX2Restore(ops, recallIndex, { redundantSyncDisplayValue: true });
  const redundantSyncShape = valueProof?.x2SyncShape === true;
  const redundantSyncVpShape = valueProof?.x2SyncVpShape === true;
  const exposesStackLift = removingRecallCanExposeStackLift(ops, recallIndex);
  const exposesX2Restore =
    removingRecallCanExposeX2Restore(ops, recallIndex, {
      redundantSyncRegister,
      redundantSyncValue,
      redundantSyncDisplayValue,
      redundantSyncShape,
      redundantSyncVpShape,
    }) &&
    !recallRemovalPreservesImmediateVpRestoreContext(ops, recallIndex, x2ValueState, valueProof, context);
  const result: RecallRemovalAnalysis = {
    register,
    valueProof,
    redundantSyncRegister,
    redundantSyncValue,
    redundantSyncDisplayValue,
    redundantSyncShape,
    x2SyncRedundant: redundantSyncRegister !== undefined ||
      redundantSyncValue ||
      redundantSyncDisplayValueForContext ||
      redundantSyncShape,
    exposesStackLift,
    exposesX2Restore,
    removable: !exposesStackLift && !exposesX2Restore,
  };
  cacheRecallRemovalAnalysis(ops, recallIndex, x2RegisterState, x2ValueState, context, result);
  return result;
}

function cachedRecallRemovalAnalysis(
  ops: readonly IrOp[],
  recallIndex: number,
  x2RegisterState: RegisterValueSet | undefined,
  x2ValueState: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext | undefined,
): RecallRemovalCacheEntry | undefined {
  for (const entry of recallRemovalAnalysisCache.get(ops)?.get(recallIndex) ?? []) {
    if (
      entry.x2RegisterState === x2RegisterState &&
      entry.x2ValueState === x2ValueState &&
      entry.context === context
    ) return entry;
  }
  return undefined;
}

function cacheRecallRemovalAnalysis(
  ops: readonly IrOp[],
  recallIndex: number,
  x2RegisterState: RegisterValueSet | undefined,
  x2ValueState: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext | undefined,
  result: RecallRemovalAnalysis | undefined,
): void {
  let byIndex = recallRemovalAnalysisCache.get(ops);
  if (byIndex === undefined) {
    byIndex = new Map();
    recallRemovalAnalysisCache.set(ops, byIndex);
  }
  const entries = byIndex.get(recallIndex);
  const entry: RecallRemovalCacheEntry = { x2RegisterState, x2ValueState, context, result };
  if (entries === undefined) {
    byIndex.set(recallIndex, [entry]);
  } else {
    entries.push(entry);
  }
}

function recallRemovalPreservesImmediateVpRestoreContext(
  ops: readonly IrOp[],
  recallIndex: number,
  state: X2ValueDataflowState | undefined,
  valueProof: RecallValueProof | undefined,
  context?: DirectReturnAnalysisContext,
): boolean {
  if (
    state === undefined ||
    valueProof === undefined ||
    (
      valueProof.x2SyncShape !== true &&
      valueProof.x2SyncValue !== true &&
      valueProof.x2SyncVpShape !== true
    )
  ) return false;
  const op = ops[recallIndex];
  if (op === undefined) return false;
  const recalledValues = recallX2ValueFacts(state, valueProof.register, true, op);
  const recalledMantissas = vpEntryMantissasFromValueFacts(recalledValues);
  const recalledShapes = recallVpEntryShapeSourceFacts(op, state, valueProof.register);
  const recalledSourceKeys = vpSourceKeys(recalledMantissas, recalledShapes);
  if (
    x2HasSignRestoreGapBeforeVp(ops, recallIndex + 1, context) &&
    x2HasOnlyRestoreGapBeforeVp(ops, recallIndex + 1, context) &&
    stringSetsHaveIntersection(vpEntrySignSourceKeys(state), recalledSourceKeys)
  ) return true;
  const nextRestore = nextImmediateX2RestoreOp(ops, recallIndex + 1);
  if (nextRestore?.kind !== "plain" || nextRestore.opcode !== 0x0c) return false;
  const vpContext = analyzeX2VpShapeContext(state);
  if (
    vpContext.kind === "active-mantissa" &&
    stringSetsHaveIntersection(activeMantissaVpSourceKeys(vpContext), recalledSourceKeys)
  ) return true;
  return stringSetsHaveIntersection(vpEntrySourceKeys(state), recalledSourceKeys);
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
  return { x: new Set(), y: new Set(), x2: new Set() };
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
  return { x: new Set(input.x), y: new Set(input.y), x2: new Set(input.x2) };
}

function cloneX2ValueDataflowState(input: X2ValueDataflowState): X2ValueDataflowState {
  return {
    x: canonicalX2ValueSet(input.x),
    y: cloneOptionalValueSet(input.y),
    x2: canonicalX2ValueSet(input.x2),
    xShape: cloneOptionalShapeSet(input.xShape),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: cloneX2EntryState(input.entry),
    vpContext: cloneX2VpContextState(input.vpContext),
    structuralEntry: cloneX2StructuralEntryState(input.structuralEntry),
    structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
    vpEntryMantissa: cloneOptionalStringSet(input.vpEntryMantissa),
    ...(input.vpEntryMantissaTransient === true ? { vpEntryMantissaTransient: true } : {}),
    vpEntrySignMantissa: cloneOptionalStringSet(input.vpEntrySignMantissa),
    vpEntryShape: cloneOptionalShapeSet(input.vpEntryShape),
    vpEntrySignShape: cloneOptionalShapeSet(input.vpEntrySignShape),
    ...(input.vpEntryShapeTransient === true ? { vpEntryShapeTransient: true } : {}),
    ...cloneX2MemoryFields(input),
  };
}

function cloneX2MemoryFields(
  input: Pick<X2ValueDataflowState, "memory" | "shapeMemory">,
): Pick<X2ValueDataflowState, "memory" | "shapeMemory"> {
  return {
    memory: input.memory === undefined ? undefined : cloneX2ValueMemory(input.memory),
    shapeMemory: input.shapeMemory === undefined ? undefined : cloneX2ShapeMemory(input.shapeMemory),
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
        y: transferStoreYRegisterSet(input, op.register),
        x2: addStoredX2Alias(input, op.register),
      };
    case "indirect-store":
      return transferIndirectStoreRegisterState(input, op);
    case "recall":
      return { x: new Set([op.register]), y: new Set(input.x), x2: new Set([op.register]) };
    case "indirect-recall": {
      const target = knownIndirectMemoryTarget(op);
      const registers = target === undefined ? new Set<RegisterName>() : new Set([target]);
      return { x: registers, y: new Set(input.x), x2: new Set(registers) };
    }
    case "plain": {
      return transferPlainRegisterDataflowState(input, op);
    }
    case "cjump": {
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      return {
        x: new Set(input.x),
        y: new Set(input.y),
        x2: transferConditionalX2RegisterSet(input, effect),
      };
    }
    case "loop": {
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      const counter = loopCounterRegister(op.counter);
      const x = removeRegisterValue(input.x, counter);
      const y = removeRegisterValue(input.y, counter);
      const x2 = removeRegisterValue(input.x2, counter);
      return {
        x,
        y,
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
      return { x: new Set(input.x), y: new Set(input.y), x2: new Set(input.x) };
  }
}

function transferX2ValueDataflowState(
  input: X2ValueDataflowState,
  op: IrOp,
  edge: Edge["kind"],
  trackRegisterMemory: boolean,
  producerIndex: number | undefined,
  targetStartsWithVp: boolean = false,
): X2ValueDataflowState {
  if (hasRewriteBarrier(op)) return emptyX2ValueDataflowState(trackRegisterMemory);

  switch (op.kind) {
    case "label":
      return cloneX2ValueDataflowState(input);
    case "jump":
    case "call":
      return targetStartsWithVp
        ? withDirectFlowVpSpliceSource(closeX2ValueEntry(input))
        : closeX2ValueEntry(input);
    case "orphan-address":
      return closeX2ValueEntry(input);
    case "store": {
      const closed = closeX2ValueEntry(input);
      const stable = registerWritePreservesStoredValue(closed, op.register)
        ? closed
        : invalidateRegisterDependentX2ValueState(closed, op.register, trackRegisterMemory);
      const vpEntryShape = vpEntryShapesFromStoreSplice(stable.x2Shape);
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
        vpEntryMantissa: vpEntryMantissasFromStoreSplice(stable.x2Shape),
        vpEntrySignMantissa: vpEntrySignMantissasFromStoreSplice(stable.x2Shape),
        vpEntryShape,
        vpEntrySignShape: vpEntrySignShapesFromStoreSplice(stable.x2Shape),
        ...(vpEntryShape === undefined ? {} : { vpEntryShapeTransient: true as const }),
        memory: trackRegisterMemory ? storeX2ValueMemory(stable.memory, op.register, stable.x) : undefined,
        shapeMemory: trackRegisterMemory
          ? storeX2ShapeMemory(stable.shapeMemory, op.register, stable.x, stable.xShape)
          : undefined,
      };
    }
    case "indirect-store":
      return transferIndirectStoreX2ValueState(input, op, trackRegisterMemory);
    case "recall": {
      const closed = closeX2ValueEntry(input);
      const value = recallX2ValueFacts(input, op.register, trackRegisterMemory, op);
      const shape = recallX2ShapeFacts(value, op, trackRegisterMemory ? input.shapeMemory?.[op.register] : undefined);
      return {
        x: canonicalX2ValueSet(value),
        y: cloneOptionalValueSet(closed.x),
        x2: canonicalX2ValueSet(value),
        xShape: new Set(shape),
        yShape: cloneOptionalShapeSet(closed.xShape),
        x2Shape: new Set(shape),
        entry: closedX2EntryState(),
        vpContext: noneX2VpContextState(),
        structuralEntry: noneX2StructuralEntryState(),
        structuralVpContext: noneX2StructuralEntryState(),
        vpEntryMantissa: vpEntryMantissasFromValueFacts(value),
        vpEntryShape: vpEntryShapesFromShapeFacts(shape),
        vpEntrySignShape: vpEntrySignShapesFromShapeFacts(shape),
        ...cloneX2MemoryFields(input),
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
        x: canonicalX2ValueSet(values),
        y: cloneOptionalValueSet(closed.x),
        x2: canonicalX2ValueSet(values),
        xShape: new Set(shape),
        yShape: cloneOptionalShapeSet(closed.xShape),
        x2Shape: new Set(shape),
        entry: closedX2EntryState(),
        vpContext: noneX2VpContextState(),
        structuralEntry: noneX2StructuralEntryState(),
        structuralVpContext: noneX2StructuralEntryState(),
        vpEntryMantissa: vpEntryMantissasFromValueFacts(values),
        vpEntryShape: vpEntryShapesFromShapeFacts(shape),
        vpEntrySignShape: vpEntrySignShapesFromShapeFacts(shape),
        ...cloneX2MemoryFields(input),
      };
    }
    case "plain":
      return transferPlainX2ValueState(input, op, producerIndex);
    case "cjump": {
      const closed = closeX2ValueEntry(input);
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      const x = syncUnknownSameValue(canonicalX2ValueSet(closed.x), effect, producerIndex);
      const xShape = cloneOptionalShapeSet(closed.xShape);
      const x2Shape = transferConditionalX2ShapeSet(closed, x, xShape, effect);
      const output: X2ValueDataflowState = {
        x,
        y: cloneOptionalValueSet(closed.y),
        x2: transferConditionalX2ValueSet(closed, x, xShape, effect),
        xShape,
        yShape: cloneOptionalShapeSet(closed.yShape),
        x2Shape,
        entry: closedX2EntryState(),
        vpContext: transferConditionalX2VpContextState(closed, effect),
        structuralEntry: noneX2StructuralEntryState(),
        structuralVpContext: transferConditionalX2StructuralVpContextState(closed, effect),
        vpEntryMantissa: transferConditionalX2VpEntryMantissaState(x, xShape, x2Shape, effect),
        vpEntrySignMantissa: transferConditionalX2VpEntrySignMantissaState(closed, x, xShape, x2Shape, effect),
        vpEntryShape: transferConditionalX2VpEntryShapeState(xShape, x2Shape, effect),
        vpEntrySignShape: transferConditionalX2VpEntrySignShapeState(closed, x, xShape, x2Shape, effect),
        ...cloneX2MemoryFields(closed),
      };
      return edge === "jump" && effect === "preserves" && targetStartsWithVp
        ? withDirectFlowVpSpliceSource(output)
        : output;
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
          ? x2SyncValueSetFromVisibleX(x, xShape)
          : new Set<X2ValueFact>();
      const x2Shape = transferConditionalX2ShapeSet(stable, x, xShape, effect);
      const output: X2ValueDataflowState = {
        x,
        y: cloneOptionalValueSet(stable.y),
        x2,
        xShape,
        yShape: cloneOptionalShapeSet(stable.yShape),
        x2Shape,
        entry: closedX2EntryState(),
        vpContext: transferConditionalX2VpContextState(stable, effect),
        structuralEntry: noneX2StructuralEntryState(),
        structuralVpContext: transferConditionalX2StructuralVpContextState(stable, effect),
        vpEntryMantissa: transferConditionalX2VpEntryMantissaState(x, xShape, x2Shape, effect),
        vpEntrySignMantissa: transferConditionalX2VpEntrySignMantissaState(stable, x, xShape, x2Shape, effect),
        vpEntryShape: transferConditionalX2VpEntryShapeState(xShape, x2Shape, effect),
        vpEntrySignShape: transferConditionalX2VpEntrySignShapeState(stable, x, xShape, x2Shape, effect),
        memory: trackRegisterMemory ? deleteX2ValueMemory(stable.memory, counter) : undefined,
        shapeMemory: trackRegisterMemory ? deleteX2ShapeMemory(stable.shapeMemory, counter) : undefined,
      };
      return edge === "jump" && effect === "preserves" && targetStartsWithVp
        ? withIndirectFlowVpSpliceSource(output)
        : output;
    }
    case "indirect-jump":
    case "indirect-call":
      return transferIndirectFlowX2ValueState(input, op, trackRegisterMemory, targetStartsWithVp);
    case "indirect-cjump":
      return transferIndirectConditionalX2ValueState(input, op, edge, trackRegisterMemory, targetStartsWithVp);
    case "stop":
      return emptyX2ValueDataflowState(trackRegisterMemory);
    case "return": {
      const closed = closeX2ValueEntry(input);
      const x = syncUnknownSameValue(canonicalX2ValueSet(closed.x), "affects", producerIndex);
      const xShape = cloneOptionalShapeSet(closed.xShape);
      const x2Shape = x2SyncShapeSetFromVisibleX(xShape, x);
      return {
        x,
        y: cloneOptionalValueSet(closed.y),
        x2: x2SyncValueSetFromVisibleX(x, xShape),
        xShape,
        yShape: cloneOptionalShapeSet(closed.yShape),
        x2Shape,
        entry: closedX2EntryState(),
        vpContext: noneX2VpContextState(),
        structuralEntry: noneX2StructuralEntryState(),
        structuralVpContext: noneX2StructuralEntryState(),
        vpEntryMantissa: transferConditionalX2VpEntryMantissaState(x, xShape, x2Shape, "affects"),
        vpEntrySignMantissa: transferConditionalX2VpEntrySignMantissaState(closed, x, xShape, x2Shape, "affects"),
        vpEntryShape: transferConditionalX2VpEntryShapeState(xShape, x2Shape, "affects"),
        vpEntrySignShape: transferConditionalX2VpEntrySignShapeState(closed, x, xShape, x2Shape, "affects"),
        ...cloneX2MemoryFields(closed),
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

function transferStoreYRegisterSet(input: RegisterDataflowState, register: RegisterName): Set<RegisterName> {
  return input.x.has(register) ? new Set(input.y) : removeRegisterValue(input.y, register);
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
    y: removeRegisterValue(input.y, register),
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
    y: new Set(input.y),
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
  if (target === undefined) {
    return isStableIndirectSelector(op.register)
      ? cloneRegisterDataflowState(input)
      : dropMutatedSelectorFact(input, op.register);
  }
  const output: RegisterDataflowState = {
    x: addRegisterValue(input.x, target),
    y: transferStoreYRegisterSet(input, target),
    x2: addStoredX2Alias(input, target),
  };
  return isStableIndirectSelector(op.register) ? output : dropMutatedSelectorFact(output, op.register);
}

function transferPlainRegisterDataflowState(
  input: RegisterDataflowState,
  op: Extract<IrOp, { kind: "plain" }>,
): RegisterDataflowState {
  if (op.opcode === STACK_EXCHANGE_XY_OPCODE) {
    const effect = plainX2Effect(op);
    const x = new Set(input.y);
    return {
      x,
      y: new Set(input.x),
      x2: transferPlainX2RegisterSet(input, x, effect),
    };
  }
  if (op.opcode === STACK_COPY_Y_TO_X_OPCODE) {
    const effect = plainX2Effect(op);
    const x = new Set(input.y);
    return {
      x,
      y: new Set(input.y),
      x2: transferPlainX2RegisterSet(input, x, effect),
    };
  }
  const effect = plainX2Effect(op);
  const x = plainPreservesXValue(op) ? new Set(input.x) : new Set<RegisterName>();
  return {
    x,
    y: transferPlainYRegisterSet(input, input.x, op),
    x2: transferPlainX2RegisterSet(input, x, effect),
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
      ...cloneX2MemoryFields(input),
    };
  }
  if (op.opcode === STACK_EXCHANGE_XY_OPCODE) {
    return transferExchangeXYX2ValueState(input, op);
  }
  if (op.opcode === STACK_COPY_Y_TO_X_OPCODE) {
    return transferCopyYToXX2ValueState(input, op);
  }
  const effect = plainX2Effect(op);
  const closedExponentValues = closedExponentEntryDecimalFacts(input.entry);
  if (closedExponentValues.size > 0) {
    const sourceX = new Set(closedExponentValues);
    const closedExponentShapes = closedExponentEntryShapeFacts(input.entry);
    const x = plainPreservesXValue(op)
      ? new Set(sourceX)
      : plainXValueAfterNonPreservingOp(op, producerIndex, sourceX, input.y, closedExponentShapes, input.yShape);
    const xShape = plainPreservesXValue(op)
      ? new Set(closedExponentShapes)
      : plainXShapeAfterNonPreservingOp(op, sourceX, input.y, closedExponentShapes, input.yShape);
    const x2 = effect === "preserves"
      ? new Set(closedExponentValues)
      : effect === "affects"
        ? new Set(x)
        : new Set<X2ValueFact>();
    const x2Shape = transferPlainX2ShapeSet(input, x, xShape, effect, closedExponentShapes);
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
      vpEntryMantissa: transferPlainX2VpEntryMantissaState(
        input,
        op,
        x,
        x2,
        xShape,
        x2Shape,
        effect,
        closedExponentShapes,
        closedExponentShapes,
      ),
      vpEntryMantissaTransient: transferPlainX2VpEntryMantissaTransientState(
        op,
        effect,
        closedExponentShapes,
        closedExponentShapes,
      ),
      vpEntrySignMantissa: transferPlainX2VpEntrySignMantissaState(input, op, effect),
      vpEntrySignShape: transferPlainX2VpEntrySignShapeState(input, op, x, x2, xShape, x2Shape, effect),
      vpEntryShape: transferPlainX2VpEntryShapeState(
        input,
        op,
        xShape,
        x2Shape,
        effect,
        closedExponentShapes,
        closedExponentShapes,
      ),
      vpEntryShapeTransient: transferPlainX2VpEntryShapeTransientState(
        op,
        effect,
        closedExponentShapes,
        closedExponentShapes,
      ),
      ...cloneX2MemoryFields(input),
    };
  }
  const closedExponentShapes = closedExponentEntryShapeFacts(input.entry);
  if (closedExponentShapes.size > 0) {
    const sourceX = new Set<X2ValueFact>();
    const x = plainPreservesXValue(op)
      ? new Set<X2ValueFact>()
      : plainXValueAfterNonPreservingOp(op, producerIndex, sourceX, input.y, closedExponentShapes, input.yShape);
    const xShape = plainPreservesXValue(op)
      ? new Set(closedExponentShapes)
      : plainXShapeAfterNonPreservingOp(op, sourceX, input.y, closedExponentShapes, input.yShape);
    const x2Shape = transferPlainX2ShapeSet(input, x, xShape, effect, closedExponentShapes);
    const x2 = transferPlainX2ValueSet(input, x, xShape, effect);
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
      vpEntryMantissa: transferPlainX2VpEntryMantissaState(
        input,
        op,
        x,
        x2,
        xShape,
        x2Shape,
        effect,
        closedExponentShapes,
        closedExponentShapes,
      ),
      vpEntryMantissaTransient: transferPlainX2VpEntryMantissaTransientState(
        op,
        effect,
        closedExponentShapes,
        closedExponentShapes,
      ),
      vpEntrySignMantissa: transferPlainX2VpEntrySignMantissaState(input, op, effect),
      vpEntrySignShape: transferPlainX2VpEntrySignShapeState(input, op, x, x2, xShape, x2Shape, effect),
      vpEntryShape: transferPlainX2VpEntryShapeState(
        input,
        op,
        xShape,
        x2Shape,
        effect,
        closedExponentShapes,
        closedExponentShapes,
      ),
      vpEntryShapeTransient: transferPlainX2VpEntryShapeTransientState(
        op,
        effect,
        closedExponentShapes,
        closedExponentShapes,
      ),
      ...cloneX2MemoryFields(input),
    };
  }
  const x = syncUnknownSameValue(
    plainPreservesXValue(op)
      ? new Set(input.x)
      : plainXValueAfterNonPreservingOp(op, producerIndex, input.x, input.y, input.xShape, input.yShape),
    effect,
    producerIndex,
  );
  const xShape = plainPreservesXValue(op)
    ? cloneOptionalShapeSet(input.xShape)
    : plainXShapeAfterNonPreservingOp(op, input.x, input.y, input.xShape, input.yShape);
  const x2 = transferPlainX2ValueSet(input, x, xShape, effect);
  const x2Shape = transferPlainX2ShapeSet(input, x, xShape, effect);
  const structuralEntry = input.structuralEntry ?? noneX2StructuralEntryState();
  if (structuralEntry.kind === "exponent") {
    const closedStructuralShapes = structuralExponentEntryShapeFacts(structuralEntry);
    const sourceX = new Set<X2ValueFact>();
    const structuralXValue = plainPreservesXValue(op)
      ? new Set<X2ValueFact>()
      : plainXValueAfterNonPreservingOp(op, producerIndex, sourceX, input.y, closedStructuralShapes, input.yShape);
    const structuralXShape = plainPreservesXValue(op)
      ? new Set(closedStructuralShapes)
      : plainXShapeAfterNonPreservingOp(op, sourceX, input.y, closedStructuralShapes, input.yShape);
    const structuralX2Shape = transferPlainX2ShapeSet(
      input,
      structuralXValue,
      structuralXShape,
      effect,
      closedStructuralShapes,
    );
    return {
      x: structuralXValue,
      y: transferPlainYValueSet(input, sourceX, op),
      x2: transferPlainX2ValueSet(input, structuralXValue, structuralXShape, effect),
      xShape: structuralXShape,
      yShape: transferPlainYShapeSet(input, closedStructuralShapes, op),
      x2Shape: structuralX2Shape,
      entry: nextX2EntryStateForPlainEffect(effect),
      vpContext: transferPlainX2VpContextState(input, effect),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: transferPlainX2StructuralVpContextState(input, effect),
      vpEntryMantissa: transferPlainX2VpEntryMantissaState(
        input,
        op,
        structuralXValue,
        new Set(),
        structuralXShape,
        structuralX2Shape,
        effect,
        closedStructuralShapes,
        closedStructuralShapes,
      ),
      vpEntryMantissaTransient: transferPlainX2VpEntryMantissaTransientState(
        op,
        effect,
        closedStructuralShapes,
        closedStructuralShapes,
      ),
      vpEntrySignMantissa: transferPlainX2VpEntrySignMantissaState(input, op, effect),
      vpEntrySignShape: transferPlainX2VpEntrySignShapeState(
        input,
        op,
        structuralXValue,
        new Set(),
        structuralXShape,
        structuralX2Shape,
        effect,
      ),
      vpEntryShape: transferPlainX2VpEntryShapeState(
        input,
        op,
        structuralXShape,
        structuralX2Shape,
        effect,
        closedStructuralShapes,
        closedStructuralShapes,
      ),
      vpEntryShapeTransient: transferPlainX2VpEntryShapeTransientState(
        op,
        effect,
        closedStructuralShapes,
        closedStructuralShapes,
      ),
      ...cloneX2MemoryFields(input),
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
    vpEntryMantissa: transferPlainX2VpEntryMantissaState(
      input,
      op,
      x,
      x2,
      xShape,
      x2Shape,
      effect,
      input.xShape,
      input.x2Shape,
      input.vpContext?.kind === "exponent",
    ),
    vpEntryMantissaTransient: transferPlainX2VpEntryMantissaTransientState(
      op,
      effect,
      input.xShape,
      input.x2Shape,
      input.vpContext?.kind === "exponent",
    ),
    vpEntrySignMantissa: transferPlainX2VpEntrySignMantissaState(input, op, effect),
    vpEntrySignShape: transferPlainX2VpEntrySignShapeState(input, op, x, x2, xShape, x2Shape, effect),
    vpEntryShape: transferPlainX2VpEntryShapeState(
      input,
      op,
      xShape,
      x2Shape,
      effect,
      input.xShape,
      input.x2Shape,
    ),
    vpEntryShapeTransient: transferPlainX2VpEntryShapeTransientState(
      op,
      effect,
      input.xShape,
      input.x2Shape,
    ),
    ...cloneX2MemoryFields(input),
  };
}

export function transferX2ValueStateForEdge(
  input: X2ValueDataflowState | undefined,
  op: IrOp,
  edge: X2DataflowEdgeKind,
  options: X2ValueDataflowOptions = {},
  producerIndex?: number,
): X2ValueDataflowState | undefined {
  if (input === undefined) return undefined;
  return transferX2ValueDataflowState(
    input,
    op,
    edge,
    options.trackRegisterMemory === true,
    producerIndex,
  );
}

export function transferX2RegisterStateForEdge(
  input: X2RegisterEdgeState | undefined,
  op: IrOp,
  edge: X2DataflowEdgeKind,
): RegisterValueSet | undefined {
  if (input === undefined || input.x2 === undefined) return undefined;
  if (hasRewriteBarrier(op)) return new Set();

  switch (op.kind) {
    case "label":
    case "jump":
    case "call":
    case "orphan-address":
      return new Set(input.x2);
    case "store":
      return input.x === undefined ? undefined : addProjectedStoredX2Alias(input.x2, input.x, op.register);
    case "indirect-store": {
      const target = knownIndirectMemoryTarget(op);
      if (target === undefined) return new Set(input.x2);
      return input.x === undefined ? undefined : addProjectedStoredX2Alias(input.x2, input.x, target);
    }
    case "recall":
      return new Set([op.register]);
    case "indirect-recall": {
      const target = knownIndirectMemoryTarget(op);
      return target === undefined ? new Set() : new Set([target]);
    }
    case "plain":
      return transferPlainX2RegisterSetForKnownEdge(input, op);
    case "cjump": {
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      return transferConditionalX2RegisterSetForKnownEdge(input, effect);
    }
    case "loop": {
      const effect = conditionalX2EffectForGraphEdge(op, edge);
      const counter = loopCounterRegister(op.counter);
      const x2 = removeRegisterValue(input.x2, counter);
      if (effect === "preserves") return x2;
      if (effect !== "affects") return new Set();
      return input.x === undefined ? undefined : removeRegisterValue(input.x, counter);
    }
    case "indirect-jump":
    case "indirect-call":
      return isStableIndirectSelector(op.register) ? new Set(input.x2) : removeRegisterValue(input.x2, op.register);
    case "indirect-cjump": {
      const effect = indirectConditionalX2EffectForGraphEdge(op, edge);
      const projected = transferConditionalX2RegisterSetForKnownEdge(input, effect);
      if (projected === undefined) return undefined;
      return edge === "jump" && !isStableIndirectSelector(op.register)
        ? removeRegisterValue(projected, op.register)
        : projected;
    }
    case "return":
      return input.x === undefined ? undefined : new Set(input.x);
    case "stop":
      return new Set();
  }
}

function addProjectedStoredX2Alias(
  x2: RegisterValueSet,
  x: RegisterValueSet,
  register: RegisterName,
): Set<RegisterName> {
  const output = removeRegisterValue(x2, register);
  if (setsIntersect(x, x2)) output.add(register);
  return output;
}

function transferPlainX2RegisterSetForKnownEdge(
  input: X2RegisterEdgeState,
  op: Extract<IrOp, { kind: "plain" }>,
): RegisterValueSet | undefined {
  const effect = plainX2Effect(op);
  if (effect === "preserves") return new Set(input.x2);
  if (effect !== "affects") return new Set();
  const x = projectedPlainVisibleXRegisterSet(input, op);
  return x === undefined ? undefined : new Set(x);
}

function projectedPlainVisibleXRegisterSet(
  input: X2RegisterEdgeState,
  op: Extract<IrOp, { kind: "plain" }>,
): RegisterValueSet | undefined {
  if (op.opcode === STACK_EXCHANGE_XY_OPCODE || op.opcode === STACK_COPY_Y_TO_X_OPCODE) {
    return input.y === undefined ? undefined : new Set(input.y);
  }
  if (plainPreservesXValue(op)) return input.x === undefined ? undefined : new Set(input.x);
  return new Set();
}

function transferConditionalX2RegisterSetForKnownEdge(
  input: X2RegisterEdgeState,
  effect: ReturnType<typeof conditionalX2Effect>,
): RegisterValueSet | undefined {
  if (effect === "preserves") return new Set(input.x2);
  if (effect === "affects") return input.x === undefined ? undefined : new Set(input.x);
  return new Set();
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
      ...cloneX2MemoryFields(input),
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
    ...cloneX2MemoryFields({ memory, shapeMemory }),
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
      ...cloneX2MemoryFields(input),
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
      ...cloneX2MemoryFields(input),
    };
  }
  return {
    x: normalizeX2RestoreFactsForX(input.x2),
    y: cloneOptionalValueSet(input.y),
    x2: cloneOptionalValueSet(input.x2),
    xShape: normalizeX2RestoreShapesForX(input.x2Shape),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: noneX2StructuralEntryState(),
    vpEntryMantissa: vpEntryMantissasFromX2Restore(input.x2, input.x2Shape),
    vpEntrySignMantissa: vpEntrySignMantissasFromX2Restore(input.x2, input.x2Shape),
    vpEntryShape: vpEntryShapesFromShapeFacts(input.x2Shape),
    vpEntrySignShape: vpEntrySignShapesFromShapeFacts(input.x2Shape),
    ...cloneX2MemoryFields(input),
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
    const context = signChangeStructuralExponentEntry(input.structuralVpContext);
    return x2ValueStateFromSignedStructuralVpContext(input, context);
  }
  if (input.entry.kind === "closed" && input.vpContext?.kind === "exponent") {
    const context = signChangeVpContext(input.vpContext);
    return x2ValueStateFromSignedDecimalVpContext(input, context);
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
      ...cloneX2MemoryFields(input),
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
    ...cloneX2MemoryFields(input),
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
      ...cloneX2MemoryFields(input),
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
  if (
    input.entry.kind === "closed" &&
    input.vpContext?.kind === "exponent" &&
    input.vpEntryMantissa !== undefined
  ) {
    const entry = x2EntryStateFromExponentParts(input.vpEntryMantissa, input.vpContext.exponent);
    if (entry.kind === "exponent") return x2ValueStateFromExponentEntry(
      entry,
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
      ...cloneX2MemoryFields(input),
    };
  }
  if (
    input.entry.kind === "closed" &&
    input.structuralVpContext?.kind === "exponent" &&
    input.vpEntryShape !== undefined &&
    input.vpEntryShape.size > 0
  ) {
    const structuralEntry = x2StructuralEntryStateFromParts(input.vpEntryShape, input.structuralVpContext.exponent);
    return x2ValueStateFromStructuralExponentEntry(structuralEntry, input.memory, input.shapeMemory, input.y, input.yShape);
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
    ...cloneX2MemoryFields(input),
  };
}

function transferIndirectFlowX2ValueState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "indirect-jump" | "indirect-call" }>,
  trackRegisterMemory: boolean,
  targetStartsWithVp: boolean,
): X2ValueDataflowState {
  const closed = closeX2ValueEntry(input);
  const stable = isStableIndirectSelector(op.register)
    ? closed
    : dropMutatedSelectorX2ValueFact(closed, op.register, trackRegisterMemory);
  return targetStartsWithVp ? withIndirectFlowVpSpliceSource(stable) : stable;
}

function transferIndirectConditionalX2ValueState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "indirect-cjump" }>,
  edge: Edge["kind"],
  trackRegisterMemory: boolean,
  targetStartsWithVp: boolean,
): X2ValueDataflowState {
  const effect = indirectConditionalX2EffectForGraphEdge(op, edge);
  if (effect === "unknown") return emptyX2ValueDataflowState(trackRegisterMemory);
  const closed = closeX2ValueEntry(input);
  const x = syncUnknownSameValue(canonicalX2ValueSet(closed.x), effect);
  const xShape = cloneOptionalShapeSet(closed.xShape);
  const x2Shape = transferConditionalX2ShapeSet(closed, x, xShape, effect);
  const output: X2ValueDataflowState = {
    x,
    y: cloneOptionalValueSet(closed.y),
    x2: transferConditionalX2ValueSet(closed, x, xShape, effect),
    xShape,
    yShape: cloneOptionalShapeSet(closed.yShape),
    x2Shape,
    entry: closedX2EntryState(),
    vpContext: transferConditionalX2VpContextState(closed, effect),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: transferConditionalX2StructuralVpContextState(closed, effect),
    vpEntryMantissa: transferConditionalX2VpEntryMantissaState(x, xShape, x2Shape, effect),
    vpEntrySignMantissa: transferConditionalX2VpEntrySignMantissaState(closed, x, xShape, x2Shape, effect),
    vpEntryShape: transferConditionalX2VpEntryShapeState(xShape, x2Shape, effect),
    vpEntrySignShape: transferConditionalX2VpEntrySignShapeState(closed, x, xShape, x2Shape, effect),
    ...cloneX2MemoryFields(closed),
  };
  if (edge !== "jump") return output;
  const stable = isStableIndirectSelector(op.register)
    ? output
    : dropMutatedSelectorX2ValueFact(output, op.register, trackRegisterMemory);
  return targetStartsWithVp ? withIndirectFlowVpSpliceSource(stable) : stable;
}

function transferIndirectStoreX2ValueState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "indirect-store" }>,
  trackRegisterMemory: boolean,
): X2ValueDataflowState {
  const target = knownIndirectMemoryTarget(op);
  if (target === undefined) {
    const closed = withStoreVpSpliceSource(closeX2ValueEntry(input));
    const cleared = trackRegisterMemory ? clearX2ValueMemory(closed) : closed;
    return isStableIndirectSelector(op.register)
      ? cleared
      : dropMutatedSelectorX2ValueFact(cleared, op.register, trackRegisterMemory);
  }
  const closed = closeX2ValueEntry(input);
  const stable = registerWritePreservesStoredValue(closed, target)
    ? closed
    : invalidateRegisterDependentX2ValueState(closed, target, trackRegisterMemory);
  const value = registerValueFact(target);
  const vpEntryShape = vpEntryShapesFromStoreSplice(stable.x2Shape);
  const output: X2ValueDataflowState = {
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
    vpEntryMantissa: vpEntryMantissasFromStoreSplice(stable.x2Shape),
    vpEntrySignMantissa: vpEntrySignMantissasFromStoreSplice(stable.x2Shape),
    vpEntryShape,
    vpEntrySignShape: vpEntrySignShapesFromStoreSplice(stable.x2Shape),
    ...(vpEntryShape === undefined ? {} : { vpEntryShapeTransient: true as const }),
    memory: trackRegisterMemory ? storeX2ValueMemory(stable.memory, target, stable.x) : undefined,
    shapeMemory: trackRegisterMemory ? storeX2ShapeMemory(stable.shapeMemory, target, stable.x, stable.xShape) : undefined,
  };
  return isStableIndirectSelector(op.register)
    ? output
    : dropMutatedSelectorX2ValueFact(output, op.register, trackRegisterMemory);
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

function transferPlainYRegisterSet(
  input: RegisterDataflowState,
  sourceX: RegisterValueSet,
  op: Extract<IrOp, { kind: "plain" }>,
): Set<RegisterName> {
  const info = getOpcode(op.opcode);
  if (info.stackEffect === "shifts") return new Set(sourceX);
  if (info.stackEffect === "preserves") return new Set(input.y);
  if (info.stackEffect === "consume-y-keep" && info.risk === "documented") return new Set(input.y);
  return new Set();
}

function transferPlainX2ValueSet(
  input: X2ValueDataflowState,
  x: X2ValueSet,
  xShape: X2ShapeSet | undefined,
  effect: ReturnType<typeof plainX2Effect>,
): Set<X2ValueFact> {
  if (effect === "preserves") return canonicalX2ValueSet(input.x2);
  if (effect === "affects") return x2SyncValueSetFromVisibleX(x, xShape);
  return new Set();
}

function x2SyncValueSetFromVisibleX(
  values: X2ValueSet,
  shapes: X2ShapeSet | undefined,
): Set<X2ValueFact> {
  const output = canonicalX2ValueSet(values);
  for (const decimal of x2ValueShapeSetRestoredVisibleDecimals(values, shapes)) {
    output.add(decimalValueFact(decimal, "normalized"));
  }
  return canonicalX2ValueSet(output);
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
  const x2 = transferPlainX2ValueSet(input, x, xShape, effect);
  const x2Shape = transferPlainX2ShapeSet(input, x, xShape, effect);
  return {
    x,
    y: cloneOptionalValueSet(sourceX),
    x2,
    xShape,
    yShape: new Set(sourceXShape),
    x2Shape,
    entry: nextX2EntryStateForPlainEffect(effect),
    vpContext: transferPlainX2VpContextState(input, effect),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: transferPlainX2StructuralVpContextState(input, effect),
    vpEntryMantissa: transferPlainX2VpEntryMantissaState(
      input,
      op,
      x,
      x2,
      xShape,
      x2Shape,
      effect,
      sourceXShape,
      input.x2Shape,
    ),
    vpEntryMantissaTransient: transferPlainX2VpEntryMantissaTransientState(
      op,
      effect,
      sourceXShape,
      input.x2Shape,
    ),
    vpEntrySignMantissa: transferPlainX2VpEntrySignMantissaState(input, op, effect),
    vpEntrySignShape: transferPlainX2VpEntrySignShapeState(input, op, x, x2, xShape, x2Shape, effect),
    vpEntryShape: transferPlainX2VpEntryShapeState(
      input,
      op,
      xShape,
      x2Shape,
      effect,
      sourceXShape,
      input.x2Shape,
    ),
    vpEntryShapeTransient: transferPlainX2VpEntryShapeTransientState(
      op,
      effect,
      sourceXShape,
      input.x2Shape,
    ),
    ...cloneX2MemoryFields(input),
  };
}

function transferCopyYToXX2ValueState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "plain" }>,
): X2ValueDataflowState {
  const closed = closeX2ValueEntry(input);
  const effect = plainX2Effect(op);
  const x = cloneOptionalValueSet(closed.y);
  const xShape = cloneOptionalShapeSet(closed.yShape);
  const x2 = transferPlainX2ValueSet(closed, x, xShape, effect);
  const x2Shape = transferPlainX2ShapeSet(closed, x, xShape, effect);
  return {
    x,
    y: cloneOptionalValueSet(closed.y),
    x2,
    xShape,
    yShape: cloneOptionalShapeSet(closed.yShape),
    x2Shape,
    entry: nextX2EntryStateForPlainEffect(effect),
    vpContext: transferPlainX2VpContextState(closed, effect),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: transferPlainX2StructuralVpContextState(closed, effect),
    vpEntryMantissa: transferPlainX2VpEntryMantissaState(
      closed,
      op,
      x,
      x2,
      xShape,
      x2Shape,
      effect,
      closed.xShape,
      closed.x2Shape,
    ),
    vpEntryMantissaTransient: transferPlainX2VpEntryMantissaTransientState(
      op,
      effect,
      closed.xShape,
      closed.x2Shape,
    ),
    vpEntrySignMantissa: transferPlainX2VpEntrySignMantissaState(closed, op, effect),
    vpEntrySignShape: transferPlainX2VpEntrySignShapeState(closed, op, x, x2, xShape, x2Shape, effect),
    vpEntryShape: transferPlainX2VpEntryShapeState(
      closed,
      op,
      xShape,
      x2Shape,
      effect,
      closed.xShape,
      closed.x2Shape,
    ),
    vpEntryShapeTransient: transferPlainX2VpEntryShapeTransientState(
      op,
      effect,
      closed.xShape,
      closed.x2Shape,
    ),
    ...cloneX2MemoryFields(closed),
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
  if (info.stackEffect === "shifts") return cloneOptionalValueSet(sourceX);
  if (info.stackEffect === "preserves") return cloneOptionalValueSet(input.y);
  if (info.stackEffect === "consume-y-keep" && info.risk === "documented") return cloneOptionalValueSet(input.y);
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
  x: X2ValueSet | undefined,
  xShape: X2ShapeSet,
  effect: ReturnType<typeof plainX2Effect>,
  closedEntryShape: X2ShapeSet | undefined = undefined,
): Set<X2ShapeFact> {
  if (effect === "preserves") return cloneOptionalShapeSet(closedEntryShape ?? input.x2Shape);
  if (effect === "affects") return x2SyncShapeSetFromVisibleX(xShape, x);
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
  return canonicalX2ValueSet(x);
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
  xShape: X2ShapeSet,
  x2Shape: X2ShapeSet,
  effect: ReturnType<typeof plainX2Effect>,
  firstDigitSourceShape: X2ShapeSet | undefined = xShape,
  firstDigitTargetShape: X2ShapeSet | undefined = x2Shape,
  includeExponentTargets = false,
): ReadonlySet<string> | undefined {
  if (effect === "affects") return sharedDecimalVpEntryMantissas({ x, x2, xShape, x2Shape });
  if (effect === "preserves") {
    const inherited = isEmptyPlainOp(op) && input.vpEntryMantissaTransient !== true
      ? input.vpEntryMantissa
      : undefined;
    return mergeOptionalStringSources(
      inherited,
      decimalFirstDigitVpSpliceMantissas(firstDigitSourceShape, firstDigitTargetShape, includeExponentTargets),
    );
  }
  return undefined;
}

function transferPlainX2VpEntryMantissaTransientState(
  op: Extract<IrOp, { kind: "plain" }>,
  effect: ReturnType<typeof plainX2Effect>,
  firstDigitSourceShape: X2ShapeSet | undefined,
  firstDigitTargetShape: X2ShapeSet | undefined,
  includeExponentTargets = false,
): true | undefined {
  if (effect !== "preserves" || isEmptyPlainOp(op)) return undefined;
  const spliced = decimalFirstDigitVpSpliceMantissas(
    firstDigitSourceShape,
    firstDigitTargetShape,
    includeExponentTargets,
  );
  return spliced === undefined ? undefined : true;
}

function transferPlainX2VpEntrySignMantissaState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "plain" }>,
  effect: ReturnType<typeof plainX2Effect>,
): ReadonlySet<string> | undefined {
  if (effect === "preserves" && isEmptyPlainOp(op)) return cloneOptionalStringSet(input.vpEntrySignMantissa);
  return undefined;
}

function transferPlainX2VpEntrySignShapeState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "plain" }>,
  x: X2ValueSet,
  x2: X2ValueSet,
  xShape: X2ShapeSet,
  x2Shape: X2ShapeSet,
  effect: ReturnType<typeof plainX2Effect>,
): X2ShapeSet | undefined {
  if (effect === "affects") return sharedVpEntrySignShapeFacts({ x, x2, xShape, x2Shape });
  if (effect === "preserves" && isEmptyPlainOp(op)) return cloneOptionalShapeSet(input.vpEntrySignShape);
  return undefined;
}

function transferPlainX2VpEntryShapeState(
  input: X2ValueDataflowState,
  op: Extract<IrOp, { kind: "plain" }>,
  xShape: X2ShapeSet,
  x2Shape: X2ShapeSet,
  effect: ReturnType<typeof plainX2Effect>,
  firstDigitSourceShape: X2ShapeSet | undefined = xShape,
  firstDigitTargetShape: X2ShapeSet | undefined = x2Shape,
): X2ShapeSet | undefined {
  if (effect === "affects") return sharedStructuralShapeFacts({ xShape, x2Shape });
  if (effect === "preserves") {
    const inherited = isEmptyPlainOp(op) && input.vpEntryShapeTransient !== true
      ? input.vpEntryShape
      : undefined;
    return mergeOptionalShapeSources(
      inherited,
      structuralFirstDigitVpSpliceShapeFacts(firstDigitSourceShape, firstDigitTargetShape),
    );
  }
  return undefined;
}

function transferPlainX2VpEntryShapeTransientState(
  op: Extract<IrOp, { kind: "plain" }>,
  effect: ReturnType<typeof plainX2Effect>,
  firstDigitSourceShape: X2ShapeSet | undefined,
  firstDigitTargetShape: X2ShapeSet | undefined,
): true | undefined {
  if (effect !== "preserves" || isEmptyPlainOp(op)) return undefined;
  return structuralFirstDigitVpSpliceShapeFacts(firstDigitSourceShape, firstDigitTargetShape) === undefined
    ? undefined
    : true;
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
  xShape: X2ShapeSet | undefined,
  effect: ReturnType<typeof conditionalX2Effect>,
): Set<X2ValueFact> {
  if (effect === "preserves") return canonicalX2ValueSet(input.x2);
  if (effect === "affects") return x2SyncValueSetFromVisibleX(x, xShape);
  return new Set();
}

function transferConditionalX2ShapeSet(
  input: X2ValueDataflowState,
  x: X2ValueSet | undefined,
  xShape: X2ShapeSet,
  effect: ReturnType<typeof conditionalX2Effect>,
): Set<X2ShapeFact> {
  if (effect === "preserves") return cloneOptionalShapeSet(input.x2Shape);
  if (effect === "affects") return x2SyncShapeSetFromVisibleX(xShape, x);
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
  xShape: X2ShapeSet,
  x2Shape: X2ShapeSet,
  effect: ReturnType<typeof conditionalX2Effect>,
): ReadonlySet<string> | undefined {
  return effect === "affects"
    ? sharedDecimalVpEntryMantissas({ x, x2: x, xShape, x2Shape })
    : undefined;
}

function transferConditionalX2VpEntrySignMantissaState(
  input: X2ValueDataflowState,
  x: X2ValueSet,
  xShape: X2ShapeSet,
  x2Shape: X2ShapeSet,
  effect: ReturnType<typeof conditionalX2Effect>,
): ReadonlySet<string> | undefined {
  if (effect === "affects") return sharedDecimalVpEntryMantissas({ x, x2: x, xShape, x2Shape });
  if (effect === "preserves") return cloneOptionalStringSet(input.vpEntrySignMantissa);
  return undefined;
}

function transferConditionalX2VpEntrySignShapeState(
  input: X2ValueDataflowState,
  x: X2ValueSet,
  xShape: X2ShapeSet,
  x2Shape: X2ShapeSet,
  effect: ReturnType<typeof conditionalX2Effect>,
): X2ShapeSet | undefined {
  if (effect === "affects") return sharedVpEntrySignShapeFacts({ x, x2: x, xShape, x2Shape });
  if (effect === "preserves") return cloneOptionalShapeSet(input.vpEntrySignShape);
  return undefined;
}

function transferConditionalX2VpEntryShapeState(
  xShape: X2ShapeSet,
  x2Shape: X2ShapeSet,
  effect: ReturnType<typeof conditionalX2Effect>,
): X2ShapeSet | undefined {
  return effect === "affects" ? sharedStructuralShapeFacts({ xShape, x2Shape }) : undefined;
}

function joinRegisterDataflowStates(
  current: RegisterDataflowState | undefined,
  incoming: RegisterDataflowState,
): RegisterDataflowState {
  if (current === undefined) return {
    x: new Set(incoming.x),
    y: new Set(incoming.y),
    x2: new Set(incoming.x2),
  };
  return {
    x: joinRegisterValueSets(current.x, incoming.x),
    y: joinRegisterValueSets(current.y, incoming.y),
    x2: joinRegisterValueSets(current.x2, incoming.x2),
  };
}

function joinX2ValueDataflowStates(
  current: X2ValueDataflowState | undefined,
  incoming: X2ValueDataflowState,
  trackRegisterMemory = false,
): X2ValueDataflowState {
  if (current === undefined) return {
    x: canonicalX2ValueSet(incoming.x),
    y: cloneOptionalValueSet(incoming.y),
    x2: canonicalX2ValueSet(incoming.x2),
    xShape: cloneOptionalShapeSet(incoming.xShape),
    yShape: cloneOptionalShapeSet(incoming.yShape),
    x2Shape: cloneOptionalShapeSet(incoming.x2Shape),
    entry: cloneX2EntryState(incoming.entry),
    vpContext: cloneX2VpContextState(incoming.vpContext),
    structuralEntry: cloneX2StructuralEntryState(incoming.structuralEntry),
    structuralVpContext: cloneX2StructuralEntryState(incoming.structuralVpContext),
    vpEntryMantissa: cloneOptionalStringSet(incoming.vpEntryMantissa),
    ...(incoming.vpEntryMantissaTransient === true ? { vpEntryMantissaTransient: true } : {}),
    vpEntrySignMantissa: cloneOptionalStringSet(incoming.vpEntrySignMantissa),
    vpEntryShape: cloneOptionalShapeSet(incoming.vpEntryShape),
    vpEntrySignShape: cloneOptionalShapeSet(incoming.vpEntrySignShape),
    ...(incoming.vpEntryShapeTransient === true ? { vpEntryShapeTransient: true } : {}),
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
    vpEntryMantissa: joinVpSourceMantissas(
      current.vpEntryMantissa,
      current.vpEntryShape,
      incoming.vpEntryMantissa,
      incoming.vpEntryShape,
    ),
    ...(
      current.vpEntryMantissaTransient === true || incoming.vpEntryMantissaTransient === true
        ? { vpEntryMantissaTransient: true as const }
        : {}
    ),
    vpEntrySignMantissa: joinVpSourceMantissas(
      current.vpEntrySignMantissa,
      current.vpEntrySignShape,
      incoming.vpEntrySignMantissa,
      incoming.vpEntrySignShape,
    ),
    vpEntryShape: joinVpSourceShapeFacts(
      current.vpEntryMantissa,
      current.vpEntryShape,
      incoming.vpEntryMantissa,
      incoming.vpEntryShape,
      false,
    ),
    vpEntrySignShape: joinVpSourceShapeFacts(
      current.vpEntrySignMantissa,
      current.vpEntrySignShape,
      incoming.vpEntrySignMantissa,
      incoming.vpEntrySignShape,
      true,
    ),
    ...(
      current.vpEntryShapeTransient === true || incoming.vpEntryShapeTransient === true
        ? { vpEntryShapeTransient: true as const }
        : {}
    ),
    memory: trackRegisterMemory ? joinX2ValueMemories(current.memory, incoming.memory) : undefined,
    shapeMemory: trackRegisterMemory ? joinX2ShapeMemories(current.shapeMemory, incoming.shapeMemory) : undefined,
  };
}

function sameRegisterDataflowState(
  left: RegisterDataflowState | undefined,
  right: RegisterDataflowState | undefined,
): boolean {
  if (left === undefined || right === undefined) return left === right;
  return sameRegisterValueSet(left.x, right.x) &&
    sameRegisterValueSet(left.y, right.y) &&
    sameRegisterValueSet(left.x2, right.x2);
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
    left.vpEntryMantissaTransient === right.vpEntryMantissaTransient &&
    sameOptionalStringSet(left.vpEntrySignMantissa, right.vpEntrySignMantissa) &&
    sameOptionalShapeSet(left.vpEntryShape, right.vpEntryShape) &&
    sameOptionalShapeSet(left.vpEntrySignShape, right.vpEntrySignShape) &&
    left.vpEntryShapeTransient === right.vpEntryShapeTransient &&
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
  if (current === undefined) return canonicalX2ValueSet(incoming);
  const currentSet = canonicalX2ValueSet(current);
  const incomingSet = canonicalX2ValueSet(incoming);
  const joined = new Set<X2ValueFact>();
  for (const value of currentSet) {
    if (incomingSet.has(value)) joined.add(value);
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
  const leftSet = canonicalX2ValueSet(left);
  const rightSet = canonicalX2ValueSet(right);
  if (leftSet.size !== rightSet.size) return false;
  for (const value of leftSet) {
    if (!rightSet.has(value)) return false;
  }
  return true;
}

function cloneX2ValueMemory(input: X2ValueMemory | undefined): X2ValueMemory {
  const output: X2ValueMemory = {};
  for (const register of x2ValueMemoryRegisters(input)) {
    const values = input?.[register];
    if (values !== undefined && values.size > 0) output[register] = canonicalX2ValueSet(values);
  }
  return output;
}

function cloneX2ShapeMemory(input: X2ShapeMemory | undefined): X2ShapeMemory {
  const output: X2ShapeMemory = {};
  for (const register of x2ShapeMemoryRegisters(input)) {
    const shapes = canonicalShapeSet(input?.[register]);
    if (shapes.size > 0) output[register] = new Set(shapes);
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
  const currentSet = canonicalX2ValueSet(current);
  const incomingSet = canonicalX2ValueSet(incoming);
  const joined = new Set<X2ValueFact>();
  for (const value of currentSet) {
    if (incomingSet.has(value)) joined.add(value);
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
  values: X2ValueSet,
  shapes: X2ShapeSet | undefined,
): X2ShapeMemory {
  const output = cloneX2ShapeMemory(input);
  const merged = shapeSetWithStableExpressionValueShapes(shapes, values);
  const stored = storableX2ShapeMemoryFacts(merged);
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
    ...(input.vpEntryMantissaTransient === true ? { vpEntryMantissaTransient: true } : {}),
    vpEntrySignMantissa: cloneOptionalStringSet(input.vpEntrySignMantissa),
    vpEntryShape: cloneOptionalShapeSet(input.vpEntryShape),
    vpEntrySignShape: cloneOptionalShapeSet(input.vpEntrySignShape),
    ...(input.vpEntryShapeTransient === true ? { vpEntryShapeTransient: true } : {}),
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
    ...(input.vpEntryMantissaTransient === true ? { vpEntryMantissaTransient: true } : {}),
    vpEntrySignMantissa: cloneOptionalStringSet(input.vpEntrySignMantissa),
    vpEntryShape: cloneOptionalShapeSet(input.vpEntryShape),
    vpEntrySignShape: cloneOptionalShapeSet(input.vpEntrySignShape),
    ...(input.vpEntryShapeTransient === true ? { vpEntryShapeTransient: true } : {}),
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
    if (!isUnstableOpaqueExpressionValueFact(fact)) output.add(canonicalX2ValueFact(fact));
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
    if (!x2ValueFactDependsOnRegister(fact, register)) output.add(canonicalX2ValueFact(fact));
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
    x: canonicalX2ValueSet(input.x),
    y: cloneOptionalValueSet(input.y),
    x2: canonicalX2ValueSet(input.x2),
    xShape: cloneOptionalShapeSet(input.xShape),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: cloneX2EntryState(input.entry),
    vpContext: cloneX2VpContextState(input.vpContext),
    structuralEntry: cloneX2StructuralEntryState(input.structuralEntry),
    structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
    vpEntryMantissa: cloneOptionalStringSet(input.vpEntryMantissa),
    ...(input.vpEntryMantissaTransient === true ? { vpEntryMantissaTransient: true } : {}),
    vpEntrySignMantissa: cloneOptionalStringSet(input.vpEntrySignMantissa),
    vpEntryShape: cloneOptionalShapeSet(input.vpEntryShape),
    vpEntrySignShape: cloneOptionalShapeSet(input.vpEntrySignShape),
    ...(input.vpEntryShapeTransient === true ? { vpEntryShapeTransient: true } : {}),
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
    const canonical = x2CanonicalShapeFactIfValid(shape);
    if (canonical !== undefined) output.add(canonical);
  }
  return output;
}

function storableX2MemoryValueFacts(values: X2ValueSet): Set<X2ValueFact> {
  const canonicalValues = canonicalX2ValueSet(values);
  const output = concreteStoredX2ValueFacts(canonicalValues);
  for (const value of canonicalValues) {
    const visible = value.startsWith("expr-key:") ? x2ValueFactRestoredVisibleDecimal(value) : undefined;
    if (visible !== undefined) output.add(decimalValueFact(visible, "normalized"));
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
  for (const fact of [...output]) {
    if (!fact.startsWith("expr-key:")) continue;
    const visible = x2ValueFactRestoredVisibleDecimal(fact);
    if (visible !== undefined) output.add(decimalValueFact(visible, "normalized"));
  }
  return canonicalX2ValueSet(output);
}

function recallX2ShapeFacts(
  values: X2ValueSet,
  op?: IrOp,
  memoryShapes?: X2ShapeSet | undefined,
): Set<X2ShapeFact> {
  const output = x2ShapesFromValueFacts(values);
  for (const fact of memoryShapes ?? []) output.add(fact);
  for (const fact of preloadedConstantShapeFacts(op)) output.add(fact);
  return canonicalShapeSet(output);
}

function recallStructuralShapeFacts(
  op: IrOp,
  state: X2ValueDataflowState,
  register: RegisterName,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const fact of stableExpressionShapeFactsFromValueFacts(recallX2ValueFacts(state, register, true, op))) {
    for (const structural of structuralRestoreShapeFacts(new Set([fact]))) output.add(structural);
  }
  for (const fact of state.shapeMemory?.[register] ?? []) {
    for (const structural of structuralRestoreShapeFacts(new Set([fact]))) output.add(structural);
  }
  for (const fact of preloadedConstantShapeFacts(op)) {
    for (const structural of structuralRestoreShapeFacts(new Set([fact]))) output.add(structural);
  }
  return output;
}

function recallVpEntryShapeSourceFacts(
  op: IrOp,
  state: X2ValueDataflowState,
  register: RegisterName,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const fact of recallDecimalDisplayShapeFacts(op, state, register)) output.add(fact);
  for (const fact of recallStructuralShapeFacts(op, state, register)) output.add(fact);
  return output;
}

function recallDecimalDisplayShapeFacts(
  op: IrOp,
  state: X2ValueDataflowState,
  register: RegisterName,
): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const fact of decimalDisplayShapeFacts(
    stableExpressionShapeFactsFromValueFacts(recallX2ValueFacts(state, register, true, op)),
  )) output.add(fact);
  for (const fact of decimalDisplayShapeFacts(state.shapeMemory?.[register])) output.add(fact);
  for (const fact of decimalDisplayShapeFacts(preloadedConstantShapeFacts(op))) output.add(fact);
  return output;
}

function stableExpressionShapeFactsFromValueFacts(values: X2ValueSet | undefined): Set<X2ShapeFact> {
  return shapeSetWithStableExpressionValueShapes(undefined, values) ?? new Set();
}

function x2ShapesFromValueFacts(values: X2ValueSet): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const value of values) {
    const decimal = /^decimal:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):(normalized|unnormalized)$/u.exec(value);
    if (decimal === null) continue;
    if (decimal[2] === "unnormalized") {
      output.add(decimalMantissaShapeFact(decimal[1]!));
      continue;
    }
    const shape = exactDecimalDisplayShapeFact(decimal[1]!);
    if (shape !== undefined) output.add(shape);
  }
  return shapeSetWithStableExpressionValueShapes(output, values) ?? new Set();
}

function dotSafeDecimalShapeValues(input: X2ShapeSet | undefined): Set<string> {
  const output = new Set<string>();
  for (const model of x2ShapeDataModels(input)) {
    if (model.kind !== "mantissa" || model.radix !== "decimal" || model.safety !== "dotSafeDecimal") continue;
    if (model.normalizedDecimal !== undefined && model.normalizedSameAsRaw) output.add(model.normalizedDecimal);
  }
  return output;
}

function decimalDisplayShapeFacts(input: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const model of x2ShapeDataModels(input)) {
    if (model.kind === "mantissa" && model.radix === "decimal") {
      if (model.normalizedDecimal === undefined || !model.normalizedSameAsRaw) continue;
      const shape = exactDecimalDisplayShapeFact(model.normalizedDecimal);
      if (shape !== undefined && shape === x2ShapeFactFromDataModel(model)) output.add(shape);
      continue;
    }
    if (model.kind === "exponent-entry" && model.mantissa.radix === "decimal") {
      const shape = model.closedDecimalDisplay;
      if (shape !== undefined) output.add(shape);
    }
  }
  return output;
}

function decimalExponentDisplayShapeFacts(input: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const model of x2ShapeDataModels(input)) {
    if (model.kind !== "exponent-entry" || model.mantissa.radix !== "decimal") continue;
    const shape = model.closedDecimalDisplay;
    if (shape !== undefined) output.add(shape);
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
  const safety = decimalMantissaShapeSafety(canonical, normalized);
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
    safety,
  };
}

function decimalMantissaShapeSafety(raw: string, normalized: string | undefined): X2ShapeSafety {
  if (normalized === undefined) return "unknown";
  return isSignedZeroDecimalMantissaRaw(raw, normalized) ? "errorProne" : "dotSafeDecimal";
}

function isSignedZeroDecimalMantissaRaw(raw: string, normalized: string): boolean {
  return normalized === "0" && canonicalShapeRaw(raw).startsWith("-");
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
  if (![...digits].every(isStructuralHexDigit)) return undefined;
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
  return [...canonical].every(isStructuralHexDigit) ? canonical : undefined;
}

function x2FirstMantissaDigitFromShapeFact(fact: X2ShapeFact): string | undefined {
  const model = x2ShapeDataModelForFact(fact);
  const mantissa = model.kind === "mantissa"
    ? model
    : model.kind === "exponent-entry"
      ? model.mantissa
      : undefined;
  if (mantissa === undefined || mantissa.sign !== "" || mantissa.digits.length === 0) return undefined;
  return mantissa.digits[0];
}

function replaceFirstShapeDigit(raw: string, digit: string): string | undefined {
  let replaced = false;
  let output = "";
  for (const char of raw) {
    if (!replaced && isStructuralHexDigit(char)) {
      output += digit;
      replaced = true;
      continue;
    }
    output += char;
  }
  return replaced ? output : undefined;
}

function structuralFirstDigitSpliceRadix(
  target: X2MantissaDataModel,
  sourceDigit: string,
  spliced: string,
): "hex" | "super" | undefined {
  const sourceIsDecimal = /^[0-9]$/u.test(sourceDigit);
  if (target.radix === "decimal") return sourceIsDecimal ? undefined : "hex";
  if (target.radix === "hex") return "hex";
  if (target.radix === "super") return spliced === target.canonical ? "super" : "hex";
  return undefined;
}

function x2MantissaShapeFactFromParts(radix: X2MantissaRadix, raw: string): X2ShapeFact | undefined {
  if (radix === "decimal") return decimalMantissaShapeRawIsValid(raw) ? decimalMantissaShapeFact(raw) : undefined;
  if (radix === "hex") return structuralShapeRawIsValid(raw) ? `hex:${canonicalShapeRaw(raw)}:mantissa` : undefined;
  if (radix === "super") return superShapeRawIsValid(raw) ? `super:${canonicalShapeRaw(raw)}` : undefined;
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
    if (isStructuralHexDigit(char)) digits.push(char);
  }
  return digits;
}

function decimalHasLeadingZero(raw: string, normalized: string | undefined): boolean {
  if (normalized === undefined) return false;
  return raw !== normalized && /^-?0[0-9]/u.test(raw);
}

function shapeHasLeadingZero(raw: string): boolean {
  const unsigned = raw.startsWith("-") ? raw.slice(1) : raw;
  return unsigned.startsWith("0") && (unsigned[1] !== undefined && isStructuralHexDigit(unsigned[1]));
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
  if (decimal !== undefined) {
    const shape = exactDecimalDisplayShapeFact(decimal);
    return shape === undefined ? new Set() : new Set([shape]);
  }
  const structuralExponent = normalizePreloadedStructuralExponentShape(value);
  if (structuralExponent !== undefined) return new Set([structuralExponent]);
  const shape = normalizePreloadedShapeLiteral(value);
  if (shape === undefined) return new Set();
  if (superShapeRawIsValid(shape)) return new Set<X2ShapeFact>([`super:${canonicalShapeRaw(shape)}`]);
  if (structuralShapeRawIsValid(shape) && hasStructuralNonDecimalDigit(shape)) {
    return new Set<X2ShapeFact>([`hex:${shape}:mantissa`]);
  }
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
  if (!structuralShapeRawIsValid(mantissa) || !hasStructuralNonDecimalDigit(mantissa)) return undefined;
  const exponent = match[2]!.replace(/^\+/u, "");
  const mantissaFact = superShapeRawIsValid(mantissa)
    ? `super:${canonicalShapeRaw(mantissa)}` as X2ShapeFact
    : `hex:${mantissa}:mantissa` as X2ShapeFact;
  return x2ExponentShapeFactFromMantissaFact(mantissaFact, exponent);
}

function normalizePreloadedDecimalLiteral(input: string): string | undefined {
  const normalized = input.trim().replace(/,/gu, ".").replace(/[Ее]/gu, "e");
  const match = /^(-?)(?:(\d+)(?:\.(\d*))?|\.(\d+))(?:e([+-]?\d{1,2}))?$/iu.exec(normalized);
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

function vpEntryMantissasFromRestoredValueFacts(values: X2ValueSet): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  for (const value of values) {
    const decimal = computationDecimalValueFromFact(value);
    if (decimal !== undefined) mantissas.add(decimal);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function vpEntryMantissasFromX2Restore(
  values: X2ValueSet,
  shapes: X2ShapeSet | undefined,
): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  const signedZero = signedZeroDecimalMantissaShapes(shapes);
  for (const raw of vpEntryMantissasFromRestoredValueFacts(values) ?? []) {
    if (raw !== "0" || signedZero.size === 0) mantissas.add(raw);
  }
  addStringSet(mantissas, signedZero);
  return mantissas.size === 0 ? undefined : mantissas;
}

function vpEntrySignMantissasFromX2Restore(
  values: X2ValueSet,
  shapes: X2ShapeSet | undefined,
): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  for (const value of values) {
    const decimal = /^decimal:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):(normalized|unnormalized)$/u.exec(value);
    if (decimal !== null && normalizeDecimalMantissaEntry(decimal[1]!) !== undefined) {
      mantissas.add(decimal[1]!);
    }
  }
  addStringSet(mantissas, signedZeroDecimalMantissaShapes(shapes));
  return mantissas.size === 0 ? undefined : mantissas;
}

function withStoreVpSpliceSource(input: X2ValueDataflowState): X2ValueDataflowState {
  const vpEntryShape = vpEntryShapesFromStoreSplice(input.x2Shape);
  return {
    ...input,
    vpEntryMantissa: vpEntryMantissasFromStoreSplice(input.x2Shape),
    vpEntrySignMantissa: vpEntrySignMantissasFromStoreSplice(input.x2Shape),
    vpEntryShape,
    vpEntrySignShape: vpEntrySignShapesFromStoreSplice(input.x2Shape),
    ...(vpEntryShape === undefined ? {} : { vpEntryShapeTransient: true as const }),
  };
}

function withDirectFlowVpSpliceSource(input: X2ValueDataflowState): X2ValueDataflowState {
  const vpEntryMantissa = decimalFirstDigitVpSpliceMantissas(input.xShape, input.x2Shape);
  const vpEntryShape = structuralFirstDigitVpSpliceShapeFacts(input.xShape, input.x2Shape);
  return {
    ...input,
    vpEntryMantissa,
    vpEntryShape,
    ...(vpEntryMantissa === undefined ? {} : { vpEntryMantissaTransient: true as const }),
    ...(vpEntryShape === undefined ? {} : { vpEntryShapeTransient: true as const }),
  };
}

function withIndirectFlowVpSpliceSource(input: X2ValueDataflowState): X2ValueDataflowState {
  const vpEntryMantissa = vpEntryMantissasFromIndirectFlowSplice(input.x2Shape);
  const vpEntryShape = vpEntryShapesFromIndirectFlowSplice(input.x2Shape);
  return {
    ...input,
    vpEntryMantissa,
    vpEntryShape,
    ...(vpEntryMantissa === undefined ? {} : { vpEntryMantissaTransient: true as const }),
    ...(vpEntryShape === undefined ? {} : { vpEntryShapeTransient: true as const }),
  };
}

function vpEntryMantissasFromStoreSplice(shapes: X2ShapeSet | undefined): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  for (const fact of shapes ?? []) {
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind !== "mantissa" || model.radix !== "decimal") continue;
    const spliced = decimalStoreVpSpliceMantissa(model.canonical);
    if (spliced !== undefined) mantissas.add(spliced);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function vpEntrySignMantissasFromStoreSplice(shapes: X2ShapeSet | undefined): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  for (const fact of shapes ?? []) {
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind !== "mantissa" || model.radix !== "decimal") continue;
    if (decimalMantissaDigitCount(model.canonical) > 8) continue;
    if (normalizeDecimalMantissaEntry(model.canonical) === undefined) continue;
    mantissas.add(model.canonical);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function vpEntrySignShapesFromStoreSplice(shapes: X2ShapeSet | undefined): X2ShapeSet | undefined {
  return vpEntrySignShapesFromShapeFacts(shapes);
}

function vpEntryShapesFromStoreSplice(shapes: X2ShapeSet | undefined): X2ShapeSet | undefined {
  const output = new Set<X2ShapeFact>();
  for (const fact of structuralRestoreShapeFacts(canonicalStructuralShapeFacts(shapes))) {
    const spliced = structuralStoreVpSpliceShapeFact(fact);
    if (spliced !== undefined) output.add(spliced);
  }
  return output.size === 0 ? undefined : output;
}

function vpEntryMantissasFromIndirectFlowSplice(shapes: X2ShapeSet | undefined): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  for (const fact of shapes ?? []) {
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind !== "mantissa" || model.radix !== "decimal") continue;
    const spliced = indirectFlowVpSpliceMantissa(model);
    if (spliced !== undefined) mantissas.add(spliced);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function vpEntryShapesFromIndirectFlowSplice(shapes: X2ShapeSet | undefined): X2ShapeSet | undefined {
  const output = new Set<X2ShapeFact>();
  for (const fact of structuralRestoreShapeFacts(canonicalStructuralShapeFacts(shapes))) {
    const spliced = indirectFlowStructuralVpSpliceShapeFact(fact);
    if (spliced !== undefined) output.add(spliced);
  }
  return output.size === 0 ? undefined : output;
}

function decimalStoreVpSpliceMantissa(raw: string): string | undefined {
  const canonical = canonicalShapeRaw(raw);
  if (decimalMantissaDigitCount(canonical) > 8) return undefined;
  const match = /^(-?)([0-9]{1,8})(?:\.([0-9]*))?$/u.exec(canonical);
  if (match === null) return undefined;
  const sign = match[1]!;
  const integer = match[2]!;
  const hasPoint = match[3] !== undefined;
  const fraction = match[3] ?? "";
  if (sign === "-") return negativeDecimalStoreVpSpliceMantissa(integer, fraction);
  if (/^0+$/u.test(integer)) {
    if (fraction.length === 0 || /^0*$/u.test(fraction)) return integer;
    return normalizeDecimalMantissaEntry(`0.${fraction}`);
  }
  const tail = integer.length === 1 ? "" : integer.slice(1);
  const zeroTail = tail.length === 0 || /^0+$/u.test(tail);
  const zeroFraction = fraction.length === 0 || /^0*$/u.test(fraction);
  if (zeroTail && zeroFraction) return "0.";
  if (tail.length === 0) {
    return normalizeDecimalMantissaEntry(hasPoint ? `0.${fraction}` : "0.");
  }
  return normalizeDecimalMantissaEntry(hasPoint ? `${tail}.${fraction}` : tail);
}

function indirectFlowVpSpliceMantissa(model: X2MantissaDataModel): string | undefined {
  if (model.radix !== "decimal" || model.digits.length === 0) return undefined;
  const sourceDigit = indirectFlowVpFirstDigit(model);
  const spliced = replaceFirstShapeDigit(model.canonical, sourceDigit);
  if (spliced === undefined || decimalMantissaDigitCount(spliced) > 8) return undefined;
  return normalizeDecimalMantissaEntry(spliced) === undefined ? undefined : spliced;
}

function indirectFlowStructuralVpSpliceShapeFact(fact: X2ShapeFact): X2ShapeFact | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "mantissa" || (model.radix !== "hex" && model.radix !== "super") || model.digits.length === 0) {
    return undefined;
  }
  const spliced = replaceFirstShapeDigit(model.canonical, indirectFlowVpFirstDigit(model));
  if (spliced === undefined || shapeDigits(spliced).length > 8) return undefined;
  return x2MantissaShapeFactFromModel(structuralMantissaDataModel("hex", spliced, "structuralOnly"));
}

function indirectFlowVpFirstDigit(model: X2MantissaDataModel): "7" | "8" {
  return model.digits.every((digit) => digit === "0") ? "8" : "7";
}

function negativeDecimalStoreVpSpliceMantissa(integer: string, fraction: string): string | undefined {
  const raw = fraction.length === 0 ? integer : `${integer}.${fraction}`;
  let replaced = false;
  let spliced = "";
  for (const char of raw) {
    if (char !== "." && char !== "0" && !replaced) {
      spliced += "9";
      replaced = true;
    } else {
      spliced += char;
    }
  }
  if (!replaced) return "-1";
  return normalizeDecimalMantissaEntry(`-${spliced}`);
}

function structuralStoreVpSpliceShapeFact(fact: X2ShapeFact): X2ShapeFact | undefined {
  const model = x2ShapeDataModelForFact(fact);
  if (model.kind !== "mantissa" || (model.radix !== "hex" && model.radix !== "super")) return undefined;
  const spliced = removeFirstStructuralMantissaDigit(model.canonical);
  if (spliced === undefined) return undefined;
  return x2MantissaShapeFactFromModel(structuralMantissaDataModel("hex", spliced, "structuralOnly"));
}

function removeFirstStructuralMantissaDigit(raw: string): string | undefined {
  const sign = raw.startsWith("-") ? "-" : "";
  const unsigned = sign === "" ? raw : raw.slice(1);
  let removed = false;
  let output = sign;
  for (const char of unsigned) {
    if (!removed && isStructuralHexDigit(char)) {
      removed = true;
      continue;
    }
    output += char;
  }
  if (!removed || shapeDigits(output).length === 0 || shapeDigits(output).length > 8) return undefined;
  return output;
}

function vpEntryShapesFromShapeFacts(shapes: X2ShapeSet | undefined): X2ShapeSet | undefined {
  if (shapes === undefined) return undefined;
  const structural = new Set<X2ShapeFact>();
  for (const fact of structuralRestoreShapeFacts(canonicalStructuralShapeFacts(shapes))) {
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind === "mantissa" && (model.radix === "hex" || model.radix === "super")) structural.add(fact);
  }
  return structural.size === 0 ? undefined : structural;
}

function vpEntrySignShapesFromShapeFacts(shapes: X2ShapeSet | undefined): X2ShapeSet | undefined {
  if (shapes === undefined) return undefined;
  return mergeOptionalShapeSources(
    decimalDisplayShapeFacts(shapes),
    vpEntryShapesFromShapeFacts(shapes),
  );
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
  for (const fact of input ?? []) {
    const canonical = x2CanonicalShapeFactIfValid(fact);
    if (canonical !== undefined) output.add(canonical);
  }
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

function stringSetsHaveIntersection(left: ReadonlySet<string>, right: ReadonlySet<string>): boolean {
  if (left.size === 0 || right.size === 0) return false;
  for (const value of left) {
    if (right.has(value)) return true;
  }
  return false;
}

function activeMantissaVpSourceKeys(context: X2VpShapeContextAnalysis): Set<string> {
  return vpSourceKeys(context.mantissa, undefined);
}

function vpEntrySourceKeys(state: X2ValueDataflowState | undefined): Set<string> {
  return vpSourceKeys(state?.vpEntryMantissa, state?.vpEntryShape);
}

function vpEntrySignSourceKeys(state: X2ValueDataflowState | undefined): Set<string> {
  const mantissas = state === undefined ? undefined : vpEntrySignSourceMantissas(state);
  const shapes = state === undefined ? undefined : vpEntrySignSourceShapes(state);
  return vpSourceKeys(mantissas, shapes);
}

function explicitVpEntrySignSourceKeys(state: X2ValueDataflowState | undefined): Set<string> {
  return vpSourceKeys(state?.vpEntrySignMantissa, state?.vpEntrySignShape);
}

function vpSourceKeys(
  mantissas: ReadonlySet<string> | undefined,
  shapes: X2ShapeSet | undefined,
): Set<string> {
  const keys = new Set<string>();
  addVpEntryRawMantissaSourceKeys(keys, mantissas);
  addVpEntryDisplaySourceKeys(keys, mantissas, shapes);
  return keys;
}

export function x2JoinedVpEntryMantissaSources(
  left: Pick<X2ValueDataflowState, "vpEntryMantissa" | "vpEntryShape"> | undefined,
  right: Pick<X2ValueDataflowState, "vpEntryMantissa" | "vpEntryShape"> | undefined,
): ReadonlySet<string> | undefined {
  return joinVpSourceMantissas(
    left?.vpEntryMantissa,
    left?.vpEntryShape,
    right?.vpEntryMantissa,
    right?.vpEntryShape,
  );
}

export function x2JoinedVpEntrySignShapeSources(
  left: Pick<X2ValueDataflowState, "vpEntrySignMantissa" | "vpEntrySignShape"> | undefined,
  right: Pick<X2ValueDataflowState, "vpEntrySignMantissa" | "vpEntrySignShape"> | undefined,
): X2ShapeSet | undefined {
  return joinVpSourceShapeFacts(
    left?.vpEntrySignMantissa,
    left?.vpEntrySignShape,
    right?.vpEntrySignMantissa,
    right?.vpEntrySignShape,
    true,
  );
}

function joinVpSourceMantissas(
  leftMantissas: ReadonlySet<string> | undefined,
  leftShapes: X2ShapeSet | undefined,
  rightMantissas: ReadonlySet<string> | undefined,
  rightShapes: X2ShapeSet | undefined,
): ReadonlySet<string> | undefined {
  const direct = joinOptionalStringSets(leftMantissas, rightMantissas);
  if (
    direct !== undefined &&
    (leftShapes?.size ?? 0) === 0 &&
    (rightShapes?.size ?? 0) === 0
  ) return direct;
  const mantissas = new Set<string>(direct);
  const sharedKeys = joinStringSets(
    vpSourceKeys(leftMantissas, leftShapes),
    vpSourceKeys(rightMantissas, rightShapes),
  );
  for (const key of sharedKeys) {
    const mantissa = vpMantissaFromSourceKey(key);
    if (mantissa !== undefined) mantissas.add(mantissa);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function vpMantissaFromSourceKey(key: string): string | undefined {
  const raw = /^decimal:(.*)$/u.exec(key)?.[1];
  if (raw !== undefined) {
    const canonical = canonicalShapeRaw(raw);
    return decimalExponentMantissaRawIsValid(canonical) ? canonical : undefined;
  }
  for (const fact of x2RestoredDisplayShapeFactsFromSourceKey(key) ?? []) {
    const decimal = x2ShapeFactRestoredVisibleDecimal(fact);
    if (decimal !== undefined && decimalExponentMantissaRawIsValid(decimal)) return decimal;
  }
  return undefined;
}

function joinVpSourceShapeFacts(
  leftMantissas: ReadonlySet<string> | undefined,
  leftShapes: X2ShapeSet | undefined,
  rightMantissas: ReadonlySet<string> | undefined,
  rightShapes: X2ShapeSet | undefined,
  includeDecimal: boolean,
): X2ShapeSet | undefined {
  const direct = joinOptionalShapeSets(leftShapes, rightShapes);
  if (direct.size > 0 && !includeDecimal) return direct;
  if (
    direct.size > 0 &&
    (leftMantissas?.size ?? 0) === 0 &&
    (rightMantissas?.size ?? 0) === 0 &&
    !shapeSetHasDecimalDisplaySource(leftShapes) &&
    !shapeSetHasDecimalDisplaySource(rightShapes)
  ) return direct;
  const shapes = new Set<X2ShapeFact>(direct);
  const sharedKeys = joinStringSets(
    vpSourceKeys(leftMantissas, leftShapes),
    vpSourceKeys(rightMantissas, rightShapes),
  );
  for (const key of sharedKeys) {
    for (const fact of x2RestoredDisplayShapeFactsFromSourceKey(key) ?? []) {
      const safety = x2ShapeFactSafety(fact);
      if (safety === "structuralOnly" || (includeDecimal && safety !== "unknown")) shapes.add(fact);
    }
  }
  if (shapes.size > 0 || ((leftShapes?.size ?? 0) === 0 && (rightShapes?.size ?? 0) === 0)) return shapes;
  return shapes.size === 0 ? undefined : shapes;
}

function shapeSetHasDecimalDisplaySource(shapes: X2ShapeSet | undefined): boolean {
  return decimalDisplayShapeFacts(shapes).size > 0;
}

function vpEntryDisplaySourceKeys(
  mantissas: ReadonlySet<string> | undefined,
  shapes: X2ShapeSet | undefined,
): Set<string> {
  const keys = new Set<string>();
  addVpEntryDisplaySourceKeys(keys, mantissas, shapes);
  return keys;
}

function addVpEntryRawMantissaSourceKeys(keys: Set<string>, mantissas: ReadonlySet<string> | undefined): void {
  for (const mantissa of mantissas ?? []) keys.add(`decimal:${mantissa}`);
}

function mergeStringSets(...sources: readonly ReadonlySet<string>[]): Set<string> {
  const output = new Set<string>();
  for (const source of sources) {
    for (const value of source) output.add(value);
  }
  return output;
}

function addVpEntryDisplaySourceKeys(
  keys: Set<string>,
  mantissas: ReadonlySet<string> | undefined,
  shapes: X2ShapeSet | undefined,
): void {
  for (const mantissa of mantissas ?? []) {
    const key = exactDecimalMantissaDisplaySourceKey(mantissa);
    if (key !== undefined) keys.add(key);
  }
  for (const shape of x2RestoredDisplaySourceKeyShapeFacts(shapes)) {
    keys.add(stableStructuralExpressionSourceKey(shape));
  }
}

function exactDecimalMantissaDisplaySourceKey(raw: string): string | undefined {
  const shape = decimalMantissaShapeFact(raw);
  return exactDecimalDisplayShapeFact(raw) === shape ? stableStructuralExpressionSourceKey(shape) : undefined;
}

function cloneOptionalValueSet(input: X2ValueSet | undefined): Set<X2ValueFact> {
  return input === undefined ? new Set() : canonicalX2ValueSet(input);
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
  return canonicalStableExpressionValueFact(`expr-key:${opcode}(${source})` as X2ValueFact);
}

function stableExpressionSourceKey(fact: X2ValueFact): string | undefined {
  if (fact.startsWith("reg:")) return fact;
  if (fact.startsWith("expr-key:")) return canonicalStableExpressionValueFactIfValid(fact);
  const decimal = computationDecimalValueFromFact(fact);
  if (decimal !== undefined) return decimalValueFact(decimal, "normalized");
  return undefined;
}

function canonicalStableExpressionValueFact(fact: X2ValueFact): X2ValueFact {
  return canonicalStableExpressionValueFactIfValid(fact) ?? fact;
}

function canonicalStableExpressionValueFactIfValid(fact: X2ValueFact): X2ValueFact | undefined {
  if (!fact.startsWith("expr-key:")) return fact;
  if (stableExpressionValueFactHasInvalidShapeSource(fact)) return undefined;
  return fact.replace(/shape:([^,()]+)/gu, (source, raw: string) => {
    const canonical = canonicalStableShapeSourceKey(raw as X2ShapeFact);
    return canonical ?? source;
  }) as X2ValueFact;
}

function stableExpressionValueFactHasInvalidShapeSource(fact: X2ValueFact): boolean {
  for (const match of fact.matchAll(/shape:([^,()]+)/gu)) {
    if (canonicalStableShapeSourceKey(match[1] as X2ShapeFact) === undefined) return true;
  }
  return false;
}

function canonicalX2ValueSet(input: X2ValueSet | undefined): Set<X2ValueFact> {
  const output = new Set<X2ValueFact>();
  for (const fact of input ?? []) {
    const canonical = canonicalX2ValueFactIfValid(fact);
    if (canonical !== undefined) output.add(canonical);
  }
  return output;
}

function canonicalX2ValueFact(fact: X2ValueFact): X2ValueFact {
  return canonicalX2ValueFactIfValid(fact) ?? fact;
}

function canonicalX2ValueFactIfValid(fact: X2ValueFact): X2ValueFact | undefined {
  return fact.startsWith("expr-key:") ? canonicalStableExpressionValueFactIfValid(fact) : fact;
}

function canonicalStableShapeSourceKey(fact: X2ShapeFact): string | undefined {
  const facts = x2RestoredDisplaySourceKeyShapeFacts(new Set([fact]));
  return facts.size === 1 ? stableStructuralExpressionSourceKey([...facts][0]!) : undefined;
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
  for (const key of stableExpressionDisplayShapeSourceKeys(values, shapes)) keys.add(key);
  return keys;
}

function stableExpressionDisplayShapeSourceKeys(
  values: X2ValueSet | undefined,
  shapes: X2ShapeSet | undefined,
): Set<string> {
  const keys = new Set<string>();
  const valueDecimals = normalizedDecimalValueSet(values);
  for (const fact of x2RestoredDisplaySourceKeyShapeFacts(shapes)) {
    const decimal = x2ShapeFactRestoredVisibleDecimal(fact);
    if (decimal !== undefined && valueDecimals.has(decimal)) continue;
    keys.add(stableStructuralExpressionSourceKey(fact));
  }
  return keys;
}

export function x2RestoredDisplaySourceKeyShapeFacts(input: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const output = decimalDisplayShapeFacts(input);
  for (const fact of canonicalStructuralRestoreSourceKeyFacts(input)) output.add(fact);
  return output;
}

export function x2RestoredDisplayShapeFactsFromSourceKey(key: string): X2ShapeSet | undefined {
  const raw = /^shape:(.*)$/u.exec(key)?.[1];
  if (raw === undefined) return undefined;
  const facts = x2RestoredDisplaySourceKeyShapeFacts(new Set<X2ShapeFact>([raw as X2ShapeFact]));
  return facts.size === 0 ? undefined : facts;
}

function normalizedDecimalValueSet(values: X2ValueSet | undefined): Set<string> {
  const output = new Set<string>();
  for (const fact of values ?? []) {
    const value = computationDecimalValueFromFact(fact);
    if (value !== undefined) output.add(value);
  }
  return output;
}

function stableStructuralExpressionSourceKey(fact: X2ShapeFact): string {
  return `shape:${x2CanonicalShapeFact(fact)}`;
}

function stableExpressionKeyValueSet(key: string): X2ValueSet | undefined {
  const values = stableExpressionKeyValueSetForEvaluation(key, new Set());
  return values.size === 0 ? undefined : values;
}

function stableExpressionKeyShapeSet(key: string): X2ShapeSet | undefined {
  const shapes = stableExpressionKeyShapeSetForEvaluation(key, new Set());
  return shapes.size === 0 ? undefined : shapes;
}

function stableExpressionKeyValueSetForEvaluation(
  key: string,
  seen: Set<string>,
): Set<X2ValueFact> {
  const values = new Set<X2ValueFact>();
  const decimal = decimalFromFactKey(key);
  if (decimal !== undefined) {
    values.add(decimalValueFact(decimal, "normalized"));
    return values;
  }

  for (const visible of x2ShapeSetRestoredVisibleDecimals(x2RestoredDisplayShapeFactsFromSourceKey(key))) {
    values.add(decimalValueFact(visible, "normalized"));
  }
  for (const result of stableExpressionKeyConcreteDecimalValues(key, seen)) {
    values.add(decimalValueFact(result, "normalized"));
  }
  return values;
}

function stableExpressionKeyShapeSetForEvaluation(
  key: string,
  seen: Set<string>,
): Set<X2ShapeFact> {
  const shapes = new Set<X2ShapeFact>();
  for (const fact of x2RestoredDisplayShapeFactsFromSourceKey(key) ?? []) shapes.add(fact);
  for (const fact of stableExpressionKeyConcreteShapeFacts(key, seen)) shapes.add(fact);
  return shapes;
}

function stableExpressionKeyConcreteDecimalValues(key: string, seen: Set<string>): Set<string> {
  const output = new Set<string>();
  const parsed = parseStableExpressionKey(key);
  if (parsed === undefined || seen.has(key)) return output;
  if (stableExpressionOpcodeArity(parsed.opcode) !== parsed.operands.length) return output;
  seen.add(key);
  const op = stableExpressionPlainOp(parsed.opcode);
  if (parsed.operands.length === 0) {
    const constant = plainProducesStableConstantDecimalValue(op);
    const value = constant === undefined ? undefined : normalizedDecimalValueFromFact(constant);
    if (value !== undefined) output.add(value);
  } else if (parsed.operands.length === 1) {
    const xKey = parsed.operands[0]!;
    for (const fact of plainProducesConcreteDecimalValues(
      op,
      stableExpressionKeyValueSetForEvaluation(xKey, seen),
      stableExpressionKeyShapeSetForEvaluation(xKey, seen),
    )) {
      const value = normalizedDecimalValueFromFact(fact);
      if (value !== undefined) output.add(value);
    }
  } else if (parsed.operands.length === 2) {
    const [yKey, xKey] = parsed.operands;
    for (const fact of plainProducesConcreteBinaryDecimalValues(
      op,
      stableExpressionKeyValueSetForEvaluation(yKey!, seen),
      stableExpressionKeyValueSetForEvaluation(xKey!, seen),
      stableExpressionKeyShapeSetForEvaluation(yKey!, seen),
      stableExpressionKeyShapeSetForEvaluation(xKey!, seen),
    )) {
      const value = normalizedDecimalValueFromFact(fact);
      if (value !== undefined) output.add(value);
    }
  }
  seen.delete(key);
  return output;
}

function stableExpressionKeyConcreteShapeFacts(key: string, seen: Set<string>): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  const parsed = parseStableExpressionKey(key);
  if (parsed === undefined || seen.has(key)) return output;
  if (stableExpressionOpcodeArity(parsed.opcode) !== parsed.operands.length) return output;
  seen.add(key);
  const op = stableExpressionPlainOp(parsed.opcode);
  if (parsed.operands.length === 0) {
    for (const fact of plainProducesStableConstantShapeFacts(op)) output.add(fact);
  } else if (parsed.operands.length === 1) {
    const xKey = parsed.operands[0]!;
    for (const fact of plainXShapeAfterNonPreservingOp(
      op,
      stableExpressionKeyValueSetForEvaluation(xKey, seen),
      undefined,
      stableExpressionKeyShapeSetForEvaluation(xKey, seen),
      undefined,
    )) output.add(fact);
  } else if (parsed.operands.length === 2) {
    const [yKey, xKey] = parsed.operands;
    for (const fact of plainXShapeAfterNonPreservingOp(
      op,
      stableExpressionKeyValueSetForEvaluation(xKey!, seen),
      stableExpressionKeyValueSetForEvaluation(yKey!, seen),
      stableExpressionKeyShapeSetForEvaluation(xKey!, seen),
      stableExpressionKeyShapeSetForEvaluation(yKey!, seen),
    )) output.add(fact);
  }
  seen.delete(key);
  return output;
}

function parseStableExpressionKey(
  key: string,
): { readonly opcode: number; readonly operands: readonly string[] } | undefined {
  const match = /^expr-key:([0-9A-F]{2})\((.*)\)$/u.exec(key);
  if (match === null) return undefined;
  const operands = splitStableExpressionOperands(match[2]!);
  if (operands === undefined) return undefined;
  return { opcode: Number.parseInt(match[1]!, 16), operands };
}

function splitStableExpressionOperands(source: string): readonly string[] | undefined {
  if (source.length === 0) return [];
  const operands: string[] = [];
  let depth = 0;
  let start = 0;
  for (let index = 0; index < source.length; index += 1) {
    const char = source[index]!;
    if (char === "(") {
      depth += 1;
      continue;
    }
    if (char === ")") {
      depth -= 1;
      if (depth < 0) return undefined;
      continue;
    }
    if (char !== "," || depth !== 0) continue;
    operands.push(source.slice(start, index));
    start = index + 1;
  }
  if (depth !== 0) return undefined;
  operands.push(source.slice(start));
  return operands.every((operand) => operand.length > 0) ? operands : undefined;
}

function stableExpressionPlainOp(opcode: number): Extract<IrOp, { kind: "plain" }> {
  return { kind: "plain", opcode, meta: { mnemonic: `expr-key ${opcode.toString(16).toUpperCase()}` } };
}

function stableExpressionOpcodeArity(opcode: number): number | undefined {
  if (STABLE_CONSTANT_EXPR_OPCODES.has(opcode)) return 0;
  if (!PURE_OPAQUE_EXPR_OPCODES.has(opcode)) return undefined;
  const info = getOpcode(opcode);
  if (info.risk !== "documented" || info.x2Effect !== "preserves") return undefined;
  if (info.stackEffect === "preserves") return 1;
  if (info.stackEffect === "consume-y-drop" || info.stackEffect === "consume-y-keep") return 2;
  return undefined;
}

function decimalValueFact(value: string, flavor: "normalized" | "unnormalized"): X2ValueFact {
  return `decimal:${value}:${flavor}`;
}

function normalizedDecimalValueFromFact(fact: X2ValueFact): string | undefined {
  return /^decimal:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):normalized$/u.exec(fact)?.[1];
}

function computationDecimalValueFromFact(fact: X2ValueFact): string | undefined {
  return x2ValueFactRestoredVisibleDecimal(fact);
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
  const entry = x2EntryStateFromExponentParts(input.mantissa, input.exponent);
  if (entry.kind !== "exponent") return values;
  for (const mantissa of entry.mantissa) {
    for (const exponent of entry.exponent) {
      const value = normalizedExponentEntryValue(mantissa, exponent);
      if (value !== undefined) values.add(decimalValueFact(value, "normalized"));
    }
  }
  return values;
}

function closedExponentEntryShapeFacts(input: X2EntryState): Set<X2ShapeFact> {
  if (input.kind !== "exponent") return new Set();
  const entry = x2EntryStateFromExponentParts(input.mantissa, input.exponent);
  if (entry.kind !== "exponent") return new Set();
  const shapes = exponentEntryShapeFacts(entry);
  for (const mantissa of entry.mantissa) {
    for (const exponent of entry.exponent) {
      const value = normalizedExponentEntryValue(mantissa, exponent);
      const displayShape = value === undefined ? undefined : exactDecimalDisplayShapeFact(value);
      if (displayShape !== undefined) shapes.add(displayShape);
    }
  }
  return shapes;
}

function exponentEntryShapeFacts(input: Extract<X2EntryState, { kind: "exponent" }>): Set<X2ShapeFact> {
  const entry = x2EntryStateFromExponentParts(input.mantissa, input.exponent);
  const shapes = new Set<X2ShapeFact>();
  if (entry.kind !== "exponent") return shapes;
  for (const mantissa of entry.mantissa) {
    for (const exponent of entry.exponent) {
      const fact = x2ExponentShapeFactFromMantissaFact(decimalMantissaShapeFact(mantissa), exponent);
      if (fact !== undefined) shapes.add(fact);
    }
  }
  return shapes;
}

function structuralExponentEntryFromVpEntryShapes(shapes: X2ShapeSet): X2StructuralEntryState {
  return x2StructuralEntryStateFromParts(shapes, new Set([""]));
}

function structuralExponentEntryShapeFacts(input: Extract<X2StructuralEntryState, { kind: "exponent" }>): Set<X2ShapeFact> {
  const entry = x2StructuralEntryStateFromParts(input.mantissa, input.exponent);
  const shapes = new Set<X2ShapeFact>();
  if (entry.kind !== "exponent") return shapes;
  for (const mantissa of entry.mantissa) {
    for (const exponent of entry.exponent) {
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
  if (mantissaParts.digits.length + Math.max(0, -scale) > 80) return undefined;
  const unsigned = scaledDecimalDigits(mantissaParts.digits, scale);
  const normalized = unsigned === undefined ? undefined : normalizePlainDecimal(`${mantissaParts.sign}${unsigned}`);
  if (normalized === undefined || significantDecimalDigits(normalized) > 8) return undefined;
  return normalized;
}

function exponentMantissaDecimalParts(
  mantissa: string,
): { readonly sign: "" | "-"; readonly digits: string; readonly scale: number } | undefined {
  const entryParts = exponentEntryMantissaDecimalParts(mantissa);
  if (entryParts !== undefined) return entryParts;
  return exactNormalizedExponentMantissaDecimalParts(mantissa);
}

function exponentEntryMantissaDecimalParts(
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

function exactNormalizedExponentMantissaDecimalParts(
  mantissa: string,
): { readonly sign: "" | "-"; readonly digits: string; readonly scale: number } | undefined {
  const canonical = canonicalShapeRaw(mantissa);
  const normalized = normalizePlainDecimal(canonical);
  if (normalized === undefined || normalized !== canonical || significantDecimalDigits(normalized) > 8) return undefined;
  const parts = parseExactDecimal(normalized);
  if (parts === undefined) return undefined;
  const digits = parts.num.toString().replace(/^-/, "");
  if (digits.length > 80) return undefined;
  return {
    sign: parts.num < 0n ? "-" : "",
    digits,
    scale: parts.scale,
  };
}

function effectiveExponentMantissaDigits(rawDigits: string): string {
  const stripped = rawDigits.replace(/^0+/u, "");
  if (stripped.length > 0) return stripped;
  return `1${"0".repeat(Math.max(0, rawDigits.length - 1))}`;
}

function exactDecimalDisplayShapeFact(value: string): X2ShapeFact | undefined {
  const normalized = normalizePlainDecimal(value);
  if (normalized === undefined || significantDecimalDigits(normalized) > 8) return undefined;
  if (normalized === "0") return decimalMantissaShapeFact("0");
  const ordinary = exactOrdinaryDecimalMantissaDisplayShapeFact(normalized);
  if (ordinary !== undefined) return ordinary;
  return exactScientificDecimalDisplayShapeFact(normalized);
}

function exactOrdinaryDecimalMantissaDisplayShapeFact(value: string): X2ShapeFact | undefined {
  const normalized = normalizePlainDecimal(value);
  if (normalized === undefined) return undefined;
  const unsigned = normalized.startsWith("-") ? normalized.slice(1) : normalized;
  const [integer, fraction] = unsigned.split(".");
  if ((integer ?? "0") === "0") return undefined;
  if (`${integer ?? ""}${fraction ?? ""}`.length > 8) return undefined;
  return decimalMantissaShapeFact(normalized);
}

function exactScientificDecimalDisplayShapeFact(value: string): X2ShapeFact | undefined {
  const parts = parseExactDecimal(value);
  if (parts === undefined || parts.num === 0n) return undefined;
  const sign = parts.num < 0n ? "-" : "";
  let digits = absBigInt(parts.num).toString();
  let scale = parts.scale;
  while (digits.endsWith("0") && digits.length > 1) {
    digits = digits.slice(0, -1);
    scale -= 1;
  }
  const exponent = digits.length - scale - 1;
  if (Math.abs(exponent) > 99) return undefined;
  const mantissa = digits.length === 1 ? `${sign}${digits}` : `${sign}${digits[0]!}.${digits.slice(1)}`;
  return decimalExponentShapeFact(mantissa, String(exponent));
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
  const [integer, fraction] = unsigned.split(".");
  const digits = `${integer ?? ""}${fraction ?? ""}`.replace(/^0+/u, "");
  const significant = fraction === undefined ? digits.replace(/0+$/u, "") : digits;
  return significant.length === 0 ? 1 : significant.length;
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
  for (const fact of canonicalShapeSet(input)) {
    const mantissa = /^mantissa:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):decimal$/u.exec(fact);
    if (mantissa !== null) {
      const normalized = normalizePlainDecimal(mantissa[1]!);
      const shape = normalized === undefined ? undefined : exactDecimalDisplayShapeFact(normalized);
      if (shape !== undefined) output.add(shape);
      continue;
    }
    output.add(fact);
  }
  return output;
}

function x2SyncShapeSetFromVisibleX(
  input: X2ShapeSet | undefined,
  values: X2ValueSet | undefined = undefined,
): Set<X2ShapeFact> {
  return normalizeX2SyncShapesFromX(shapeSetWithStableExpressionValueShapes(input, values));
}

function normalizeX2SyncShapesFromX(input: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const fact of canonicalShapeSet(input)) {
    const mantissa = /^mantissa:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):decimal$/u.exec(fact);
    if (mantissa !== null) {
      const raw = mantissa[1]!;
      const normalized = normalizePlainDecimal(raw);
      if (normalized === "0" && canonicalShapeRaw(raw).startsWith("-")) {
        output.add(decimalMantissaShapeFact(raw));
        continue;
      }
      const shape = normalized === undefined ? undefined : exactDecimalDisplayShapeFact(normalized);
      if (shape !== undefined) output.add(shape);
      continue;
    }
    output.add(fact);
  }
  return output;
}

function signChangedDecimalEntry(raw: string): string {
  const normalized = normalizeDecimalMantissaEntry(raw);
  if (normalized === undefined) return "0";
  if (normalized === "0") return raw.startsWith("-") ? raw.slice(1) : `-${raw}`;
  return raw.startsWith("-") ? raw.slice(1) : `-${raw}`;
}

function signChangeClosedDecimalState(
  input: X2ValueDataflowState,
  producerIndex: number | undefined,
): X2ValueDataflowState | undefined {
  const exponentShapeBacked = signChangedClosedDecimalExponentShapeState(input);
  if (exponentShapeBacked !== undefined) return exponentShapeBacked;
  const shaped = signChangedVpEntryMantissas(input);
  if (shaped !== undefined) {
    return x2ValueStateFromMantissaShapes(shaped, input.memory, input.shapeMemory, input.y, input.yShape);
  }
  const shapeBacked = signChangedClosedShapeMantissas(input);
  if (shapeBacked !== undefined) {
    return x2ValueStateFromMantissaShapes(shapeBacked, input.memory, input.shapeMemory, input.y, input.yShape);
  }
  const decimalShapeSource = signChangedVpEntryDecimalShapeState(input);
  if (decimalShapeSource !== undefined) return decimalShapeSource;
  const structuralState = signChangedClosedStructuralState(input, producerIndex);
  if (structuralState !== undefined) return structuralState;
  const structuralSource = signChangedVpEntryStructuralShapeFacts(input);
  if (structuralSource !== undefined) {
    return x2ValueStateFromStructuralShapes(
      structuralSource,
      input.memory,
      input.shapeMemory,
      input.y,
      input.yShape,
    );
  }

  const values = new Set<X2ValueFact>();
  const opaque = producerIndex === undefined ? SAME_UNKNOWN_VALUE : expressionValueFact(producerIndex);
  const xValues = canonicalX2ValueSet(input.x);
  const x2Values = canonicalX2ValueSet(input.x2);
  if (xValues.has(SAME_UNKNOWN_VALUE) && x2Values.has(SAME_UNKNOWN_VALUE)) {
    values.add(opaque);
  }
  for (const fact of x2Values) {
    const sameVisibleValue =
      xValues.has(fact) ||
      x2ValueShapeSetHasRestoredVisibleDecimal(xValues, input.xShape, fact);
    if (!sameVisibleValue) continue;
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
    const shape = decimal === undefined ? undefined : exactDecimalDisplayShapeFact(decimal);
    if (shape !== undefined) shapes.add(shape);
  }
  return {
    x: canonicalX2ValueSet(values),
    y: cloneOptionalValueSet(input.y),
    x2: canonicalX2ValueSet(values),
    xShape: shapes,
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: new Set(shapes),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    vpEntryMantissa: vpEntryMantissasFromValueFacts(values),
    vpEntrySignMantissa: vpEntryMantissasFromValueFacts(values),
    vpEntrySignShape: vpEntrySignShapesFromShapeFacts(shapes),
    ...cloneX2MemoryFields(input),
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
    if (!xValueOrShapeCanFeedClosedDecimalSignChange(input.x, input.xShape, fact, normalized)) continue;
    const signed = signChangedMantissaShape(raw);
    if (signed === undefined) return undefined;
    mantissas.add(signed);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function xValueOrShapeCanFeedClosedDecimalSignChange(
  x: X2ValueSet | undefined,
  xShape: X2ShapeSet | undefined,
  x2Shape: X2ShapeFact,
  normalized: string,
): boolean {
  if (xShape?.has(decimalMantissaShapeFact(normalized)) === true) return true;
  if (
    x2ValueShapeSetHasRestoredVisibleDecimal(
      x,
      xShape,
      decimalValueFact(normalized, "normalized"),
    )
  ) return true;
  return x2ShapeSetsHaveSameDecimalDisplayShape(xShape, new Set([x2Shape]));
}

function signChangedClosedDecimalExponentShapeState(
  input: X2ValueDataflowState,
): X2ValueDataflowState | undefined {
  const values = new Set<X2ValueFact>();
  const shapes = new Set<X2ShapeFact>();
  const mantissas = new Set<string>();
  const xValues = canonicalX2ValueSet(input.x);
  const x2Values = canonicalX2ValueSet(input.x2);
  for (const fact of input.x2Shape ?? []) {
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind !== "exponent-entry" || model.mantissa.radix !== "decimal") continue;
    const normalized = model.normalizedDecimal;
    if (normalized === undefined) continue;
    const sourceValue = decimalValueFact(normalized, "normalized");
    const hasSharedValue = xValues.has(sourceValue) && x2Values.has(sourceValue);
    const hasSharedDisplayShape = x2ShapeSetsHaveSameDecimalDisplayShape(input.xShape, new Set([fact]));
    const hasSharedRestoredVisibleDecimal = x2ValueShapeSetHasRestoredVisibleDecimal(
      input.x,
      input.xShape,
      sourceValue,
    );
    if (!hasSharedValue && !hasSharedDisplayShape && !hasSharedRestoredVisibleDecimal) continue;
    const signedShape = x2ExponentMantissaSignChangedShapeFact(fact);
    if (signedShape === undefined) continue;
    const signedDecimal = signChangedNormalizedDecimalValue(normalized);
    shapes.add(signedShape);
    const signedDisplayShape = exactDecimalDisplayShapeFact(signedDecimal);
    if (signedDisplayShape !== undefined) shapes.add(signedDisplayShape);
    if (hasSharedValue) {
      const signedValue = decimalValueFact(signedDecimal, "normalized");
      values.add(signedValue);
      mantissas.add(signedDecimal);
    }
    if (!hasSharedValue) {
      const keys = sharedRestoredDisplaySourceKeys(input.xShape, new Set([fact]));
      if (keys.size === 0 && hasSharedRestoredVisibleDecimal) {
        keys.add(stableStructuralExpressionSourceKey(fact));
      }
      for (const key of keys) {
        values.add(stableExpressionValueFact("0B", key));
      }
    }
  }
  if (values.size === 0 && shapes.size === 0) return undefined;
  return {
    x: canonicalX2ValueSet(values),
    y: cloneOptionalValueSet(input.y),
    x2: canonicalX2ValueSet(values),
    xShape: shapes,
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: new Set(shapes),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: noneX2StructuralEntryState(),
    vpEntryMantissa: mantissas,
    vpEntrySignMantissa: mantissas.size === 0 ? undefined : mantissas,
    vpEntryShape: vpEntryShapesFromShapeFacts(shapes),
    vpEntrySignShape: vpEntrySignShapesFromShapeFacts(shapes),
    ...cloneX2MemoryFields(input),
  };
}

function signChangedClosedStructuralShapeFacts(input: X2ValueDataflowState): ReadonlySet<X2ShapeFact> | undefined {
  const shapes = x2SignChangedSharedStructuralShapeFacts(input.xShape, input.x2Shape);
  return shapes.size === 0 ? undefined : shapes;
}

function signChangedVpEntryDecimalShapeState(input: X2ValueDataflowState): X2ValueDataflowState | undefined {
  const source = input.vpEntrySignShape;
  if (source === undefined) return undefined;
  const values = new Set<X2ValueFact>();
  const shapes = new Set<X2ShapeFact>();
  for (const fact of decimalDisplayShapeFacts(source)) {
    const signedShape = x2ExponentMantissaSignChangedShapeFact(fact) ?? x2MantissaSignChangedShapeFact(fact);
    if (signedShape === undefined) continue;
    shapes.add(signedShape);
    const visible = x2ShapeFactRestoredVisibleDecimal(fact);
    if (visible !== undefined) {
      const signedDisplay = exactDecimalDisplayShapeFact(signChangedNormalizedDecimalValue(visible));
      if (signedDisplay !== undefined) shapes.add(signedDisplay);
    }
    values.add(stableExpressionValueFact("0B", stableStructuralExpressionSourceKey(fact)));
  }
  if (values.size === 0 || shapes.size === 0) return undefined;
  return {
    x: canonicalX2ValueSet(values),
    y: cloneOptionalValueSet(input.y),
    x2: canonicalX2ValueSet(values),
    xShape: shapes,
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: new Set(shapes),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: noneX2StructuralEntryState(),
    vpEntrySignShape: vpEntrySignShapesFromShapeFacts(shapes),
    ...cloneX2MemoryFields(input),
  };
}

function signChangedVpEntryStructuralShapeFacts(input: X2ValueDataflowState): ReadonlySet<X2ShapeFact> | undefined {
  const source = input.vpEntrySignShape;
  if (source === undefined) return undefined;
  const shapes = new Set<X2ShapeFact>();
  for (const fact of canonicalStructuralShapeFacts(source)) {
    const signed = x2ExponentMantissaSignChangedShapeFact(fact) ?? x2MantissaSignChangedShapeFact(fact);
    if (signed === undefined) continue;
    const canonical = x2CanonicalShapeFact(signed);
    if (canonical !== undefined && x2ShapeFactSafety(canonical) === "structuralOnly") shapes.add(canonical);
  }
  return shapes.size === 0 ? undefined : shapes;
}

function signChangedClosedStructuralState(
  input: X2ValueDataflowState,
  producerIndex: number | undefined,
): X2ValueDataflowState | undefined {
  const structuralShapes = signChangedClosedStructuralShapeFacts(input);
  if (structuralShapes === undefined) return undefined;
  const state = x2ValueStateFromStructuralShapes(
    structuralShapes,
    input.memory,
    input.shapeMemory,
    input.y,
    input.yShape,
  );
  const values = new Set<X2ValueFact>();
  if (producerIndex !== undefined) values.add(expressionValueFact(producerIndex));
  for (const key of sharedStructuralRestoreSourceKeys(input.xShape, input.x2Shape)) {
    values.add(stableExpressionValueFact("0B", key));
  }
  if (values.size === 0) return state;
  return {
    ...state,
    x: canonicalX2ValueSet(values),
    x2: canonicalX2ValueSet(values),
  };
}

function sharedStructuralRestoreSourceKeys(
  xShapes: X2ShapeSet | undefined,
  x2Shapes: X2ShapeSet | undefined,
): Set<string> {
  const xRestoreShapes = canonicalStructuralRestoreSourceKeyFacts(xShapes);
  const keys = new Set<string>();
  for (const fact of canonicalStructuralRestoreSourceKeyFacts(x2Shapes)) {
    if (!xRestoreShapes.has(fact)) continue;
    keys.add(stableStructuralExpressionSourceKey(fact));
  }
  return keys;
}

export function canonicalStructuralRestoreSourceKeyFacts(shapes: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const restored = x2StructuralRestoreShapeFacts(shapes);
  const output = new Set<X2ShapeFact>();
  for (const fact of restored) {
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind === "exponent-entry" && model.closedStructuralMantissa !== undefined) {
      const closed = x2MantissaShapeFactFromModel(model.closedStructuralMantissa);
      if (closed !== undefined && restored.has(closed)) continue;
    }
    output.add(fact);
  }
  return output;
}

function sharedRestoredDisplaySourceKeys(
  xShapes: X2ShapeSet | undefined,
  x2Shapes: X2ShapeSet | undefined,
): Set<string> {
  const xSourceShapes = x2RestoredDisplaySourceKeyShapeFacts(xShapes);
  const keys = new Set<string>();
  for (const fact of x2RestoredDisplaySourceKeyShapeFacts(x2Shapes)) {
    if (xSourceShapes.has(fact)) keys.add(stableStructuralExpressionSourceKey(fact));
  }
  return keys;
}

function sharedExactDecimalDisplayShapeFacts(
  input: Pick<X2ValueDataflowState, "x" | "x2" | "xShape" | "x2Shape">,
): X2ShapeSet | undefined {
  const shapes = new Set<X2ShapeFact>();
  for (const fact of decimalDisplayShapeFacts(input.x2Shape)) {
    const visible = x2ShapeFactRestoredVisibleDecimal(fact);
    if (visible === undefined) continue;
    const sourceValue = decimalValueFact(visible, "normalized");
    if (x2ValueShapeSetHasRestoredVisibleDecimal(input.x, input.xShape, sourceValue)) {
      shapes.add(fact);
    }
  }
  for (const fact of decimalDisplayShapeFacts(input.xShape)) {
    const visible = x2ShapeFactRestoredVisibleDecimal(fact);
    if (visible === undefined) continue;
    const sourceValue = decimalValueFact(visible, "normalized");
    if (x2ValueShapeSetHasRestoredVisibleDecimal(input.x2, input.x2Shape, sourceValue)) {
      shapes.add(fact);
    }
  }
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

function sharedDecimalVpEntryMantissas(
  input: Pick<X2ValueDataflowState, "x" | "x2" | "xShape" | "x2Shape">,
): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  const signedZero = sharedSignedZeroDecimalMantissas(input.xShape, input.x2Shape);
  for (const raw of sharedNormalizedDecimalMantissas(input) ?? []) {
    if (raw !== "0" || signedZero === undefined) mantissas.add(raw);
  }
  addStringSet(mantissas, sharedExactDecimalDisplayMantissas(input));
  addStringSet(mantissas, signedZero);
  return mantissas.size === 0 ? undefined : mantissas;
}

function sharedExactDecimalDisplayMantissas(
  input: Pick<X2ValueDataflowState, "x" | "x2" | "xShape" | "x2Shape">,
): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  for (const fact of sharedExactDecimalDisplayShapeFacts(input) ?? []) {
    const visible = x2ShapeFactRestoredVisibleDecimal(fact);
    if (visible !== undefined) mantissas.add(visible);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function sharedNormalizedDecimalMantissas(input: Pick<X2ValueDataflowState, "x" | "x2">): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  const xValues = canonicalX2ValueSet(input.x);
  for (const fact of canonicalX2ValueSet(input.x2)) {
    if (!xValues.has(fact)) continue;
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

function sharedVpEntrySignShapeFacts(
  input: Pick<X2ValueDataflowState, "x" | "x2" | "xShape" | "x2Shape">,
): X2ShapeSet | undefined {
  return mergeOptionalShapeSources(
    sharedStructuralShapeFacts(input),
    sharedExactDecimalDisplayShapeFacts(input),
  );
}

function structuralFirstDigitVpSpliceShapeFacts(
  xShape: X2ShapeSet | undefined,
  x2Shape: X2ShapeSet | undefined,
): X2ShapeSet | undefined {
  const shapes = new Set<X2ShapeFact>();
  const sources = restoredVpFirstDigitSourceShapeFacts(xShape);
  const targets = vpFirstDigitSpliceTargetShapeFacts(x2Shape);
  for (const source of sources) {
    for (const target of targets) {
      const spliced = x2StructuralMantissaFirstDigitSpliceShapeFact(source, target);
      if (spliced !== undefined) shapes.add(spliced);
    }
  }
  return shapes.size === 0 ? undefined : shapes;
}

function decimalFirstDigitVpSpliceMantissas(
  xShape: X2ShapeSet | undefined,
  x2Shape: X2ShapeSet | undefined,
  includeExponentTargets = false,
): ReadonlySet<string> | undefined {
  const mantissas = new Set<string>();
  const sources = restoredVpFirstDigitSourceShapeFacts(xShape);
  const targets = decimalVpFirstDigitSpliceTargetShapeFacts(x2Shape, includeExponentTargets);
  for (const source of sources) {
    const sourceDigit = x2FirstMantissaDigitFromShapeFact(source);
    if (sourceDigit === undefined || !/^[0-9]$/u.test(sourceDigit)) continue;
    for (const target of targets) {
      const spliced = decimalFirstDigitVpSpliceMantissa(sourceDigit, target);
      if (spliced !== undefined) mantissas.add(spliced);
    }
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function decimalVpFirstDigitSpliceTargetShapeFacts(
  shapes: X2ShapeSet | undefined,
  includeExponentTargets: boolean,
): Set<X2ShapeFact> {
  const output = decimalMantissaShapeFacts(shapes);
  if (!includeExponentTargets) return output;
  for (const fact of shapes ?? []) {
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind !== "exponent-entry" || model.mantissa.radix !== "decimal") continue;
    const mantissa = x2MantissaShapeFactFromModel(model.mantissa);
    if (mantissa !== undefined) output.add(mantissa);
  }
  return output;
}

function decimalFirstDigitVpSpliceMantissa(
  sourceDigit: string,
  target: X2ShapeFact,
): string | undefined {
  const targetModel = x2ShapeDataModelForFact(target);
  if (
    targetModel.kind !== "mantissa" ||
    targetModel.radix !== "decimal" ||
    targetModel.sign !== "" ||
    targetModel.digits.length === 0
  ) {
    return undefined;
  }
  const spliced = replaceFirstShapeDigit(targetModel.canonical, sourceDigit);
  if (spliced === undefined || decimalMantissaDigitCount(spliced) > 8) return undefined;
  return normalizeDecimalMantissaEntry(spliced) === undefined ? undefined : spliced;
}

function vpFirstDigitSpliceTargetShapeFacts(shapes: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const output = structuralMantissaShapeFacts(x2StructuralRestoreShapeFacts(shapes));
  for (const fact of decimalMantissaShapeFacts(shapes)) output.add(fact);
  return output;
}

function decimalMantissaShapeFacts(shapes: X2ShapeSet | undefined): Set<X2ShapeFact> {
  const output = new Set<X2ShapeFact>();
  for (const fact of shapes ?? []) {
    const model = x2ShapeDataModelForFact(fact);
    if (model.kind === "mantissa" && model.radix === "decimal") {
      const canonical = x2ShapeFactFromDataModel(model);
      if (canonical !== undefined) output.add(canonical);
    }
  }
  return output;
}

function restoredVpFirstDigitSourceShapeFacts(shapes: X2ShapeSet | undefined): Set<X2ShapeFact> {
  return x2RestoredDisplayShapeFacts(shapes);
}

function mergeOptionalShapeSources(
  ...sources: readonly (X2ShapeSet | undefined)[]
): X2ShapeSet | undefined {
  const output = new Set<X2ShapeFact>();
  for (const source of sources) {
    for (const fact of source ?? []) {
      const canonical = x2CanonicalShapeFactIfValid(fact);
      if (canonical !== undefined) output.add(canonical);
    }
  }
  return output.size === 0 ? undefined : output;
}

function mergeOptionalStringSources(
  ...sources: readonly (ReadonlySet<string> | undefined)[]
): ReadonlySet<string> | undefined {
  const output = new Set<string>();
  for (const source of sources) {
    for (const value of source ?? []) output.add(value);
  }
  return output.size === 0 ? undefined : output;
}

function isEmptyPlainOp(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return op.opcode >= 0x54 && op.opcode <= 0x56;
}

function vpEntrySignSourceMantissas(input: X2ValueDataflowState): ReadonlySet<string> | undefined {
  const explicitSource = input.vpEntrySignMantissa ?? input.vpEntryMantissa;
  return explicitSource ?? sharedNormalizedDecimalMantissas(input);
}

function vpEntrySignSourceShapes(input: X2ValueDataflowState): X2ShapeSet | undefined {
  if (input.vpEntrySignShape !== undefined && input.vpEntrySignShape.size > 0) {
    return input.vpEntrySignShape;
  }
  const explicitSource = input.vpEntryShapeTransient === true ? undefined : input.vpEntryShape;
  if (explicitSource !== undefined && explicitSource.size > 0) return explicitSource;
  return mergeOptionalShapeSources(
    sharedStructuralShapeFacts(input),
    sharedExactDecimalDisplayShapeFacts(input),
  );
}

function signChangedVpEntryMantissas(input: X2ValueDataflowState): ReadonlySet<string> | undefined {
  const signSource = vpEntrySignSourceMantissas(input);
  if (signSource !== undefined) return signChangedMantissaShapes(signSource);
  return undefined;
}

function sharedSignedZeroDecimalMantissas(
  xShapes: X2ShapeSet | undefined,
  x2Shapes: X2ShapeSet | undefined,
): ReadonlySet<string> | undefined {
  const xSignedZero = signedZeroDecimalMantissaShapes(xShapes);
  const x2SignedZero = signedZeroDecimalMantissaShapes(x2Shapes);
  const mantissas = new Set<string>();
  for (const raw of x2SignedZero) {
    if (xSignedZero.has(raw)) mantissas.add(raw);
  }
  return mantissas.size === 0 ? undefined : mantissas;
}

function signedZeroDecimalMantissaShapes(input: X2ShapeSet | undefined): Set<string> {
  const output = new Set<string>();
  for (const fact of input ?? []) {
    const model = x2ShapeDataModelForFact(fact);
    if (
      model.kind === "mantissa" &&
      model.radix === "decimal" &&
      model.safety === "errorProne" &&
      model.normalizedDecimal === "0" &&
      model.sign === "-"
    ) output.add(model.canonical);
  }
  return output;
}

function addStringSet(target: Set<string>, source: ReadonlySet<string> | undefined): void {
  for (const value of source ?? []) target.add(value);
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
    vpEntrySignMantissa: mantissas,
    vpEntrySignShape: vpEntrySignShapesFromShapeFacts(x2Shape),
    ...cloneX2MemoryFields({ memory, shapeMemory }),
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
    vpEntrySignShape: vpEntrySignShapesFromShapeFacts(shapeSet),
    ...cloneX2MemoryFields({ memory, shapeMemory }),
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
      ...cloneX2MemoryFields({ memory, shapeMemory }),
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
    vpEntrySignShape: vpEntrySignShapesFromShapeFacts(shapes),
    ...cloneX2MemoryFields({ memory, shapeMemory }),
  };
}

function x2ValueStateFromSignedStructuralVpContext(
  input: X2ValueDataflowState,
  context: X2StructuralEntryState,
): X2ValueDataflowState {
  if (context.kind !== "exponent") {
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
      structuralVpContext: cloneX2StructuralEntryState(context),
      ...cloneX2MemoryFields(input),
    };
  }
  const shapes = structuralExponentEntryShapeFacts(context);
  return {
    x: new Set(),
    y: cloneOptionalValueSet(input.y),
    x2: new Set(),
    xShape: new Set(shapes),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: new Set(shapes),
    entry: closedX2EntryState(),
    vpContext: noneX2VpContextState(),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: x2StructuralContextFromEntry(context),
    vpEntryShape: vpEntryShapesFromShapeFacts(shapes),
    vpEntrySignShape: vpEntrySignShapesFromShapeFacts(shapes),
    ...cloneX2MemoryFields(input),
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
      ...cloneX2MemoryFields({ memory, shapeMemory }),
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
    vpEntrySignShape: vpEntrySignShapesFromShapeFacts(shapes),
    ...cloneX2MemoryFields({ memory, shapeMemory }),
  };
}

function x2ValueStateFromSignedDecimalVpContext(
  input: X2ValueDataflowState,
  context: X2VpContextState,
): X2ValueDataflowState {
  if (context.kind !== "exponent") {
    return {
      x: new Set(),
      y: cloneOptionalValueSet(input.y),
      x2: new Set(),
      xShape: new Set(),
      yShape: cloneOptionalShapeSet(input.yShape),
      x2Shape: new Set(),
      entry: closedX2EntryState(),
      vpContext: cloneX2VpContextState(context),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
      ...cloneX2MemoryFields(input),
    };
  }
  const entry = x2EntryStateFromExponentParts(context.mantissa, context.exponent);
  if (entry.kind !== "exponent") {
    return {
      x: new Set(),
      y: cloneOptionalValueSet(input.y),
      x2: new Set(),
      xShape: new Set(),
      yShape: cloneOptionalShapeSet(input.yShape),
      x2Shape: new Set(),
      entry: closedX2EntryState(),
      vpContext: cloneX2VpContextState(context),
      structuralEntry: noneX2StructuralEntryState(),
      structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
      ...cloneX2MemoryFields(input),
    };
  }
  const values = closedExponentEntryDecimalFacts(entry);
  const shapes = closedExponentEntryShapeFacts(entry);
  return {
    x: new Set(values),
    y: cloneOptionalValueSet(input.y),
    x2: new Set(values),
    xShape: new Set(shapes),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: new Set(shapes),
    entry: closedX2EntryState(),
    vpContext: x2VpContextFromExponentEntry(entry),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
    vpEntryMantissa: vpEntryMantissasFromValueFacts(values),
    vpEntryShape: vpEntryShapesFromShapeFacts(shapes),
    vpEntrySignShape: vpEntrySignShapesFromShapeFacts(shapes),
    ...cloneX2MemoryFields(input),
  };
}

function closedX2EntryState(): X2EntryState {
  return { kind: "closed" };
}

function cloneX2EntryState(input: X2EntryState): X2EntryState {
  if (input.kind === "open") return x2EntryStateFromOpenRaw(input.raw);
  if (input.kind === "exponent") return x2EntryStateFromExponentParts(input.mantissa, input.exponent);
  return input;
}

function noneX2VpContextState(): X2VpContextState {
  return { kind: "none" };
}

function noneX2StructuralEntryState(): X2StructuralEntryState {
  return { kind: "none" };
}

function x2StructuralEntryStateFromParts(
  mantissaInput: X2ShapeSet | undefined,
  exponentInput: ReadonlySet<string> | undefined,
): X2StructuralEntryState {
  const mantissa = structuralMantissaShapeFacts(canonicalShapeSet(mantissaInput));
  const exponent = canonicalExponentSet(exponentInput);
  return mantissa.size === 0 || exponent.size === 0
    ? { kind: "unknown" }
    : { kind: "exponent", mantissa, exponent };
}

function x2EntryStateFromOpenRaw(input: ReadonlySet<string> | undefined): X2EntryState {
  const raw = canonicalDecimalMantissaEntrySet(input);
  return raw.size === 0 ? { kind: "unknown" } : { kind: "open", raw };
}

function x2EntryStateFromExponentParts(
  mantissaInput: ReadonlySet<string> | undefined,
  exponentInput: ReadonlySet<string> | undefined,
): X2EntryState {
  const mantissa = canonicalDecimalExponentMantissaSet(mantissaInput);
  const exponent = canonicalExponentSet(exponentInput);
  return mantissa.size === 0 || exponent.size === 0
    ? { kind: "unknown" }
    : { kind: "exponent", mantissa, exponent };
}

function x2VpContextStateFromExponentParts(
  mantissaInput: ReadonlySet<string> | undefined,
  exponentInput: ReadonlySet<string> | undefined,
): X2VpContextState {
  const entry = x2EntryStateFromExponentParts(mantissaInput, exponentInput);
  return entry.kind === "exponent"
    ? { kind: "exponent", mantissa: entry.mantissa, exponent: entry.exponent }
    : { kind: "unknown" };
}

function canonicalDecimalMantissaEntrySet(input: ReadonlySet<string> | undefined): Set<string> {
  const output = new Set<string>();
  for (const raw of input ?? []) {
    const canonical = canonicalShapeRaw(raw);
    if (decimalMantissaShapeRawIsValid(canonical)) output.add(canonical);
  }
  return output;
}

function canonicalDecimalExponentMantissaSet(input: ReadonlySet<string> | undefined): Set<string> {
  const output = new Set<string>();
  for (const raw of input ?? []) {
    const canonical = canonicalShapeRaw(raw);
    if (decimalExponentMantissaRawIsValid(canonical)) output.add(canonical);
  }
  return output;
}

function decimalExponentMantissaRawIsValid(raw: string): boolean {
  const canonical = canonicalShapeRaw(raw);
  return decimalMantissaShapeRawIsValid(canonical) || normalizePlainDecimal(canonical) !== undefined;
}

function decimalMantissaShapeRawIsValid(raw: string): boolean {
  return normalizeDecimalMantissaEntry(canonicalShapeRaw(raw)) !== undefined;
}

function canonicalExponentSet(input: ReadonlySet<string> | undefined): Set<string> {
  const output = new Set<string>();
  for (const raw of input ?? []) {
    const canonical = canonicalExponentShapeRaw(raw);
    if (canonical !== undefined) output.add(canonical);
  }
  return output;
}

function cloneX2VpContextState(input: X2VpContextState | undefined): X2VpContextState {
  if (input === undefined || input.kind === "none" || input.kind === "unknown") return input ?? noneX2VpContextState();
  return x2VpContextStateFromExponentParts(input.mantissa, input.exponent);
}

function cloneX2StructuralEntryState(input: X2StructuralEntryState | undefined): X2StructuralEntryState {
  if (input === undefined || input.kind === "none" || input.kind === "unknown") {
    return input ?? noneX2StructuralEntryState();
  }
  return x2StructuralEntryStateFromParts(input.mantissa, input.exponent);
}

function x2VpContextFromExponentEntry(input: Extract<X2EntryState, { kind: "exponent" }>): X2VpContextState {
  return x2VpContextStateFromExponentParts(input.mantissa, input.exponent);
}

function x2StructuralContextFromEntry(
  input: Extract<X2StructuralEntryState, { kind: "exponent" }>,
): X2StructuralEntryState {
  return x2StructuralEntryStateFromParts(input.mantissa, input.exponent);
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
      vpEntrySignShape: vpEntrySignShapesFromShapeFacts(closedExponentShapes),
      ...cloneX2MemoryFields(input),
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
      vpEntryShape: vpEntryShapesFromShapeFacts(shapes),
      vpEntrySignShape: vpEntrySignShapesFromShapeFacts(shapes),
      ...cloneX2MemoryFields(input),
    };
  }
  return {
    x: canonicalX2ValueSet(input.x),
    y: cloneOptionalValueSet(input.y),
    x2: canonicalX2ValueSet(input.x2),
    xShape: cloneOptionalShapeSet(input.xShape),
    yShape: cloneOptionalShapeSet(input.yShape),
    x2Shape: cloneOptionalShapeSet(input.x2Shape),
    entry: closedX2EntryState(),
    vpContext: cloneX2VpContextState(input.vpContext),
    structuralEntry: noneX2StructuralEntryState(),
    structuralVpContext: cloneX2StructuralEntryState(input.structuralVpContext),
    vpEntryMantissa: input.vpEntryMantissaTransient === true ? undefined : cloneOptionalStringSet(input.vpEntryMantissa),
    vpEntrySignMantissa: cloneOptionalStringSet(input.vpEntrySignMantissa),
    vpEntryShape: input.vpEntryShapeTransient === true ? undefined : cloneOptionalShapeSet(input.vpEntryShape),
    vpEntrySignShape: cloneOptionalShapeSet(input.vpEntrySignShape),
    ...cloneX2MemoryFields(input),
  };
}

function advanceDecimalDigitEntry(input: X2EntryState, digit: string): X2EntryState {
  if (input.kind === "unknown") return { kind: "unknown" };
  if (input.kind === "exponent") return advanceExponentDigitEntry(input, digit);
  const source = input.kind === "closed" ? new Set([""]) : canonicalDecimalMantissaEntrySet(input.raw);
  if (source.size === 0) return { kind: "unknown" };
  const raw = new Set<string>();
  for (const prefix of source) {
    const next = `${prefix}${digit}`;
    if (decimalMantissaDigitCount(next) > 8) return { kind: "unknown" };
    raw.add(next);
  }
  return x2EntryStateFromOpenRaw(raw);
}

function advanceDecimalPointEntry(input: Extract<X2EntryState, { kind: "open" }>): X2EntryState {
  const raw = new Set<string>();
  const source = canonicalDecimalMantissaEntrySet(input.raw);
  if (source.size === 0) return { kind: "unknown" };
  for (const prefix of source) {
    raw.add(prefix.includes(".") ? prefix : `${prefix}.`);
  }
  return x2EntryStateFromOpenRaw(raw);
}

function advanceExponentDigitEntry(input: Extract<X2EntryState, { kind: "exponent" }>, digit: string): X2EntryState {
  const entry = x2EntryStateFromExponentParts(input.mantissa, input.exponent);
  if (entry.kind !== "exponent") return { kind: "unknown" };
  const exponent = new Set<string>();
  for (const prefix of entry.exponent) {
    const sign = prefix.startsWith("-") ? "-" : "";
    const digits = prefix.slice(sign.length);
    if (digits.length >= 2) return { kind: "unknown" };
    exponent.add(`${prefix}${digit}`);
  }
  return x2EntryStateFromExponentParts(entry.mantissa, exponent);
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
  return x2StructuralEntryStateFromParts(input.mantissa, exponent);
}

function signChangeExponentEntry(input: Extract<X2EntryState, { kind: "exponent" }>): X2EntryState {
  const entry = x2EntryStateFromExponentParts(input.mantissa, input.exponent);
  if (entry.kind !== "exponent") return { kind: "unknown" };
  const exponent = new Set<string>();
  for (const raw of entry.exponent) {
    exponent.add(raw.startsWith("-") ? raw.slice(1) : `-${raw}`);
  }
  return x2EntryStateFromExponentParts(entry.mantissa, exponent);
}

function signChangeStructuralExponentEntry(
  input: Extract<X2StructuralEntryState, { kind: "exponent" }>,
): X2StructuralEntryState {
  const exponent = new Set<string>();
  for (const raw of input.exponent) {
    exponent.add(raw.startsWith("-") ? raw.slice(1) : `-${raw}`);
  }
  return x2StructuralEntryStateFromParts(input.mantissa, exponent);
}

function signChangeVpContext(input: Extract<X2VpContextState, { kind: "exponent" }>): X2VpContextState {
  const context = x2VpContextStateFromExponentParts(input.mantissa, input.exponent);
  if (context.kind !== "exponent") return { kind: "unknown" };
  const exponent = new Set<string>();
  for (const raw of context.exponent) {
    exponent.add(raw.startsWith("-") ? raw.slice(1) : `-${raw}`);
  }
  return x2VpContextStateFromExponentParts(context.mantissa, exponent);
}

function joinX2EntryStates(current: X2EntryState, incoming: X2EntryState): X2EntryState {
  if (current.kind === "unknown" || incoming.kind === "unknown") return { kind: "unknown" };
  if (current.kind === "closed" || incoming.kind === "closed" || current.kind !== incoming.kind) {
    return current.kind === incoming.kind ? closedX2EntryState() : { kind: "unknown" };
  }
  if (current.kind === "exponent" && incoming.kind === "exponent") {
    const left = x2EntryStateFromExponentParts(current.mantissa, current.exponent);
    const right = x2EntryStateFromExponentParts(incoming.mantissa, incoming.exponent);
    if (left.kind !== "exponent" || right.kind !== "exponent") return { kind: "unknown" };
    return x2EntryStateFromExponentParts(
      joinStringSets(left.mantissa, right.mantissa),
      joinStringSets(left.exponent, right.exponent),
    );
  }
  if (current.kind !== "open" || incoming.kind !== "open") return { kind: "unknown" };
  const left = x2EntryStateFromOpenRaw(current.raw);
  const right = x2EntryStateFromOpenRaw(incoming.raw);
  if (left.kind !== "open" || right.kind !== "open") return { kind: "unknown" };
  return x2EntryStateFromOpenRaw(joinStringSets(left.raw, right.raw));
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
  const leftContext = x2VpContextStateFromExponentParts(left.mantissa, left.exponent);
  const rightContext = x2VpContextStateFromExponentParts(right.mantissa, right.exponent);
  if (leftContext.kind !== "exponent" || rightContext.kind !== "exponent") return { kind: "unknown" };
  return x2VpContextStateFromExponentParts(
    joinStringSets(leftContext.mantissa, rightContext.mantissa),
    joinStringSets(leftContext.exponent, rightContext.exponent),
  );
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
  return x2StructuralEntryStateFromParts(mantissa, exponent);
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
  const output = canonicalX2ValueSet(input);
  const canonical = canonicalX2ValueFactIfValid(value);
  if (canonical !== undefined) output.add(canonical);
  return output;
}

function addStoredX2ValueAlias(input: X2ValueDataflowState, value: X2ValueFact): Set<X2ValueFact> {
  const canonicalValue = canonicalX2ValueFactIfValid(value);
  const output = canonicalX2ValueSet(input.x2);
  if (canonicalValue === undefined) return output;
  output.delete(canonicalValue);
  if (x2ValueSetHasIntersection(input.x, input.x2)) output.add(canonicalValue);
  return output;
}

function removeX2Value(input: X2ValueSet, value: X2ValueFact): Set<X2ValueFact> {
  const output = canonicalX2ValueSet(input);
  const canonical = canonicalX2ValueFactIfValid(value);
  if (canonical !== undefined) output.delete(canonical);
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

function registerValueGraphForOps(ops: readonly IrOp[]): Edge[][] {
  const cached = registerValueGraphCache.get(ops);
  if (cached !== undefined) return cached;
  const graph = buildRegisterValueGraph(ops);
  registerValueGraphCache.set(ops, graph);
  return graph;
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
  const callReturnIndexes = stackDifferenceCallReturnIndexes(ops);
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
          if (returnStack.length === 0) {
            for (const target of callReturnIndexes) {
              if (visit(target, depth, [])) return true;
            }
            return false;
          }
          return visit(returnStack[0]!, depth, returnStack.slice(1));
        case "stop":
          return false;
      }
    }
    return false;
  };

  return visit(start, initialDepth);
}

function stackDifferenceCallReturnIndexes(ops: readonly IrOp[]): number[] {
  const returns: number[] = [];
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    const next = index + 1;
    if (next >= ops.length) continue;
    if (op.kind === "call" || op.kind === "indirect-call") returns.push(next);
  }
  return returns;
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
              options.redundantSyncShape === true ||
              (options.redundantSyncDisplayValue === true && op.opcode === 0x0a) ||
              (options.redundantSyncVpShape === true && op.opcode === 0x0c);
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
