import type { IrOp } from "../types.ts";
import {
  computeX2ValueStates,
  computeLabelEntryIndexes,
  directReturnAnalysisContext,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  isKnownReturnCallOp,
  knownReturnCallReturnsThroughTransparentRange,
  plainPreservesXValue,
  type DirectReturnAnalysisContext,
  type IrPass,
  type IrPassFn,
  type KnownReturnCallOp,
  type X2ValueDataflowState,
  x2StateIsClosedPlainContext,
  x2SyncCanExposeContextSensitiveRestore,
  x2ShapeSetRestoredVisibleDecimals,
  x2ValueFactRestoredVisibleDecimal,
} from "./helpers.ts";

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
  return op.kind === "plain" && op.opcode === 0x35;
}

function isFreeStandingFractionOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  return op.kind === "plain" && op.opcode === 0x35 && !hasRoles(op);
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

function isKnownFractionalNoopFraction(
  ops: readonly IrOp[],
  index: number,
  states: readonly (X2ValueDataflowState | undefined)[],
): boolean {
  const op = ops[index]!;
  const state = states[index];
  return isFreeStandingFractionOp(op) &&
    stateHasFractionalNoopX(state) &&
    // К {x} preserves hidden X2. Once dataflow proves it is also a visible-X
    // no-op, removing it cannot change a later restore value. The exposure
    // guard still keeps immediate restore boundaries where the opcode itself
    // could be the observable previous-command context.
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
  return knownReturnCallReturnsThroughTransparentRange(ops, call, context, isXPreservingBoundaryGap);
}

const run: IrPassFn = (ops) => {
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
    if (!remove.has(i) && isKnownFractionalNoopFraction(ops, i, states)) remove.add(i);
  }
  if (remove.size === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [
      {
        name: "vp-fraction-restore",
        detail: `Removed ${remove.size} redundant К {x} op(s) already supplied by a ВП/X2 boundary or proved fractional X value.`,
      },
    ],
  };
};

export const vpX2Peephole: IrPass = {
  name: "vp-x2-peephole",
  run,
  layoutSafe: false,
};
