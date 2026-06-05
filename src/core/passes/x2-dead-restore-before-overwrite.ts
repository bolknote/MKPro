import type { IrOp } from "../types.ts";
import {
  analyzeX2StackEffect,
  cellsPerOp,
  computeLabelEntryIndexes,
  computeX2ValueStates,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  x2StateHasDotSafeDecimalX2,
  x2StateHasStructuralShapeX2,
  x2StateHasX2RestoreContext,
  x2StateIsClosedPlainContext,
  x2StateHasVpEntrySource,
  type IrPass,
  type IrPassFn,
  type X2ValueDataflowState,
} from "./helpers.ts";

const DOT = 0x0a;
const SIGN_CHANGE = 0x0b;
const VP = 0x0c;

interface DirectReturnGapContext {
  readonly labelEntries: ReadonlySet<number>;
  readonly labels: ReadonlyMap<string, number>;
  readonly addresses: ReadonlyMap<number, number>;
}

const run: IrPassFn = (ops) => {
  const states = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const context: DirectReturnGapContext = {
    labelEntries: computeLabelEntryIndexes(ops),
    labels: labelIndexes(ops),
    addresses: addressIndexes(ops),
  };
  const remove = new Set<number>();

  for (let index = 0; index < ops.length; index += 1) {
    if (remove.has(index)) continue;
    const op = ops[index]!;
    if (!isDeadRestoreCandidate(op, states[index])) continue;
    const deadRun = deadRestoreRunBeforeHardOverwrite(ops, states, index, context);
    if (deadRun === undefined) continue;

    for (const removeIndex of deadRun) remove.add(removeIndex);
  }

  if (remove.size === 0) return emptyResult(ops);
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [
      {
        name: "x2-dead-restore-before-overwrite",
        detail: `Removed ${remove.size} X2 restore/separator cell(s) whose restored X value is overwritten before it can be observed.`,
      },
    ],
  };
};

function isDeadRestoreCandidate(
  op: IrOp,
  state: X2ValueDataflowState | undefined,
): boolean {
  if (state === undefined || !isFreeStandingPlain(op)) return false;
  if (isDisplayFocusSensitive(op)) return false;
  if (op.opcode === DOT) {
    return x2StateIsClosedPlainContext(state) && x2StateHasDotSafeDecimalX2(state);
  }
  if (op.opcode === SIGN_CHANGE) {
    return isDeadSignRestoreCandidate(state);
  }
  if (op.opcode === VP) {
    return state.entry.kind === "open" ||
      state.entry.kind === "exponent" ||
      (state.entry.kind === "closed" && x2StateHasVpEntrySource(state)) ||
      x2StateHasX2RestoreContext(state);
  }
  return false;
}

function isDeadSignRestoreCandidate(state: X2ValueDataflowState): boolean {
  return (
    x2StateIsClosedPlainContext(state) &&
    (x2StateHasDotSafeDecimalX2(state) || x2StateHasStructuralShapeX2(state))
  ) ||
    state.entry.kind === "open" ||
    x2StateHasX2RestoreContext(state);
}

function deadRestoreRunBeforeHardOverwrite(
  ops: readonly IrOp[],
  states: readonly (X2ValueDataflowState | undefined)[],
  start: number,
  context: DirectReturnGapContext,
): readonly number[] | undefined {
  const remove: number[] = [start];
  let sameSegment = true;
  for (let index = start + 1; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      sameSegment = false;
      continue;
    }
    if (isFreeStandingEmptyOp(op)) {
      if (sameSegment) remove.push(index);
      continue;
    }
    if (sameSegment && isDeadRestoreCandidate(op, states[index])) {
      remove.push(index);
      continue;
    }
    if (op.kind === "call" && simpleDirectReturnDoesNotObserveRestore(ops, op, context)) continue;
    return isHardX2OverwriteWithoutStackUse(op) ? remove : undefined;
  }
  return undefined;
}

function isFreeStandingPlain(op: IrOp): op is Extract<IrOp, { kind: "plain" }> {
  return op.kind === "plain" && !hasRewriteBarrier(op) && !hasRoles(op);
}

function isFreeStandingEmptyOp(op: IrOp): boolean {
  return isFreeStandingPlain(op) && op.opcode >= 0x54 && op.opcode <= 0x56;
}

function hasRoles(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return "meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0;
}

function isHardX2OverwriteWithoutStackUse(op: IrOp): boolean {
  return analyzeX2StackEffect(op).hardX2OverwriteWithoutStackUse;
}

function simpleDirectReturnDoesNotObserveRestore(
  ops: readonly IrOp[],
  call: Extract<IrOp, { kind: "call" }>,
  context: DirectReturnGapContext,
): boolean {
  const targetIndex = targetIndexForCall(call, context);
  if (targetIndex === undefined) return false;
  return linearReturnRangeDoesNotObserveRestore(ops, targetIndex, context.labelEntries);
}

function targetIndexForCall(
  call: Extract<IrOp, { kind: "call" }>,
  context: DirectReturnGapContext,
): number | undefined {
  return typeof call.target === "string"
    ? context.labels.get(call.target)
    : context.addresses.get(call.target);
}

function linearReturnRangeDoesNotObserveRestore(
  ops: readonly IrOp[],
  targetIndex: number,
  labelEntries: ReadonlySet<number>,
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
    if (!isRestoreTransparentLinearOp(op)) return false;
  }
  return false;
}

function isRestoreTransparentLinearOp(op: IrOp): boolean {
  return op.kind === "orphan-address" || isFreeStandingEmptyOp(op);
}

function labelIndexes(ops: readonly IrOp[]): Map<string, number> {
  const result = new Map<string, number>();
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") result.set(op.name, index);
  }
  return result;
}

function addressIndexes(ops: readonly IrOp[]): Map<number, number> {
  const result = new Map<number, number>();
  let address = 0;
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    result.set(address, index);
    address += cellsPerOp(op);
  }
  return result;
}

export const x2DeadRestoreBeforeOverwrite: IrPass = {
  name: "x2-dead-restore-before-overwrite",
  run,
  layoutSafe: false,
};
