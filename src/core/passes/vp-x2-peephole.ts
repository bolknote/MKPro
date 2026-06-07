import type { IrOp } from "../types.ts";
import {
  computeX2ValueStates,
  computeLabelEntryIndexes,
  directReturnAnalysisContext,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  isKnownReturnCallOp,
  knownReturnCallReturnsThroughNestedTransparentRange,
  plainPreservesXValue,
  type DirectReturnAnalysisContext,
  type IrPass,
  type IrPassFn,
  type KnownReturnCallOp,
  type X2ValueDataflowState,
  x2StateIsClosedPlainContext,
  x2SyncCanExposeContextSensitiveRestore,
  x2ShapeSetHasExactIntegerDisplay,
  x2ShapeSetHasExactNonNegativeIntegerDisplay,
  x2ShapeSetRestoredVisibleDecimals,
  x2ValueFactRestoredVisibleDecimal,
} from "./helpers.ts";

const ABS = 0x31;
const INTEGER = 0x34;
const FRACTION = 0x35;

function x2BoundaryText(op: IrOp): string {
  if (!("meta" in op)) return "";
  return [
    op.meta.comment,
    "tactic" in op.meta ? op.meta.tactic : undefined,
  ].filter(Boolean).join(" ").toLowerCase();
}

function isVpX2Boundary(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  return op.kind === "plain" &&
    op.opcode === 0x0c &&
    /display|x2|вп/u.test(x2BoundaryText(op));
}

function isFractionAfterX2Boundary(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  return op.kind === "plain" && op.opcode === FRACTION;
}

function isFreeStandingFractionOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  return op.kind === "plain" && op.opcode === FRACTION && !hasRoles(op);
}

function isFreeStandingIntegerOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  return op.kind === "plain" && op.opcode === INTEGER && !hasRoles(op);
}

function isFreeStandingAbsOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  return op.kind === "plain" && op.opcode === ABS && !hasRoles(op);
}

function isFreeStandingNoopUnaryOp(op: IrOp): boolean {
  return isFreeStandingFractionOp(op) || isFreeStandingIntegerOp(op) || isFreeStandingAbsOp(op);
}

function isFractionalNoopValue(value: string): boolean {
  return value === "0" || /^-?0\.[0-9]+$/u.test(value);
}

function stateHasFractionalNoopX(state: X2ValueDataflowState | undefined): boolean {
  if (state === undefined || !x2StateIsClosedPlainContext(state)) return false;
  for (const fact of state.x) {
    const visible = x2ValueFactRestoredVisibleDecimal(fact);
    if (visible !== undefined && isFractionalNoopValue(visible)) return true;
  }
  for (const visible of x2ShapeSetRestoredVisibleDecimals(state.xShape)) {
    if (isFractionalNoopValue(visible)) return true;
  }
  return false;
}

function stateHasIntegerNoopX(state: X2ValueDataflowState | undefined): boolean {
  return state !== undefined &&
    x2StateIsClosedPlainContext(state) &&
    x2ShapeSetHasExactIntegerDisplay(state.xShape);
}

function stateHasAbsNoopX(state: X2ValueDataflowState | undefined): boolean {
  return state !== undefined &&
    x2StateIsClosedPlainContext(state) &&
    x2ShapeSetHasExactNonNegativeIntegerDisplay(state.xShape);
}

function isKnownNoopUnaryOp(
  ops: readonly IrOp[],
  index: number,
  states: readonly (X2ValueDataflowState | undefined)[],
): boolean {
  const op = ops[index]!;
  const state = states[index];
  if (!isFreeStandingNoopUnaryOp(op)) return false;
  const knownNoop = op.kind === "plain" && op.opcode === FRACTION
    ? stateHasFractionalNoopX(state)
    : op.kind === "plain" && op.opcode === INTEGER
      ? stateHasIntegerNoopX(state)
      : stateHasAbsNoopX(state);
  return knownNoop &&
    // К {x} preserves hidden X2. Once dataflow proves it is also a visible-X
    // no-op, and К [x] is treated the same only when the display shape is
    // already the exact integer display. К |x| follows the same rule for
    // exact non-negative integer displays. Removing any of them cannot change
    // a later restore value. The exposure guard still keeps immediate restore
    // boundaries where the opcode itself could be the observable
    // previous-command context.
    !x2SyncCanExposeContextSensitiveRestore(
      ops,
      index,
      { redundantSyncValue: true, redundantSyncShape: true },
    );
}

function isFreeStandingEmptyOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  return op.kind === "plain" &&
    op.opcode >= 0x54 &&
    op.opcode <= 0x56 &&
    !hasRoles(op);
}

function hasRoles(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return "meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0;
}

function fractionAfterBoundaryIndex(
  ops: readonly IrOp[],
  boundaryIndex: number,
  labelEntries: ReadonlySet<number>,
  context: DirectReturnAnalysisContext,
): number | undefined {
  for (let index = boundaryIndex + 1; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      if (labelEntries.has(index)) return undefined;
      continue;
    }
    if (isXPreservingBoundaryGap(op)) continue;
    if (isKnownReturnCallOp(op) && simpleDirectReturnPreservesX(ops, op, context)) continue;
    return isFractionAfterX2Boundary(op) ? index : undefined;
  }
  return undefined;
}

function isXPreservingBoundaryGap(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  switch (op.kind) {
    case "store":
    case "indirect-store":
    case "orphan-address":
      return true;
    case "plain":
      return !hasRoles(op) && (isFreeStandingEmptyOp(op) || plainPreservesXValue(op));
    default:
      return false;
  }
}

function simpleDirectReturnPreservesX(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
): boolean {
  return knownReturnCallReturnsThroughNestedTransparentRange(ops, call, context, isXPreservingBoundaryGap);
}

const run: IrPassFn = (ops) => {
  if (!ops.some(isFractionAfterX2Boundary) && !ops.some(isFreeStandingNoopUnaryOp)) return emptyResult(ops);
  const remove = new Set<number>();
  const labelEntries = computeLabelEntryIndexes(ops);
  const context = directReturnAnalysisContext(ops);
  const states = computeX2ValueStates(ops, { trackRegisterMemory: true });
  for (let i = 0; i < ops.length; i += 1) {
    if (!isVpX2Boundary(ops[i]!)) continue;
    const fractionIndex = fractionAfterBoundaryIndex(ops, i, labelEntries, context);
    if (fractionIndex !== undefined) remove.add(fractionIndex);
  }
  for (let i = 0; i < ops.length; i += 1) {
    if (!remove.has(i) && isKnownNoopUnaryOp(ops, i, states)) remove.add(i);
  }
  if (remove.size === 0) return emptyResult(ops);
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [
      {
        name: "vp-fraction-restore",
        detail: `Removed ${remove.size} redundant К {x}/К [x]/К |x| op(s) already supplied by a ВП/X2 boundary or proved no-op X value.`,
      },
    ],
  };
};

export const vpX2Peephole: IrPass = {
  name: "vp-x2-peephole",
  run,
  layoutSafe: false,
};
