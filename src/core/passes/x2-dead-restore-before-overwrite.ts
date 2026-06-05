import type { IrOp } from "../types.ts";
import {
  analyzeX2StackEffect,
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

const run: IrPassFn = (ops) => {
  const states = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const remove = new Set<number>();

  for (let index = 0; index < ops.length; index += 1) {
    if (remove.has(index)) continue;
    const op = ops[index]!;
    if (!isDeadRestoreCandidate(op, states[index])) continue;
    const overwrite = followingHardOverwrite(ops, index + 1);
    if (overwrite === undefined) continue;

    remove.add(index);
    for (const gap of overwrite.gaps) remove.add(gap);
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
    return x2StateIsClosedPlainContext(state) &&
      (x2StateHasDotSafeDecimalX2(state) || x2StateHasStructuralShapeX2(state)) ||
      state.entry.kind === "open" ||
      x2StateHasX2RestoreContext(state);
  }
  if (op.opcode === VP) {
    return state.entry.kind === "open" ||
      state.entry.kind === "exponent" ||
      (state.entry.kind === "closed" && x2StateHasVpEntrySource(state)) ||
      x2StateHasX2RestoreContext(state);
  }
  return false;
}

function followingHardOverwrite(
  ops: readonly IrOp[],
  start: number,
): { readonly index: number; readonly gaps: readonly number[] } | undefined {
  const gaps: number[] = [];
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    if (isFreeStandingEmptyOp(op)) {
      gaps.push(index);
      continue;
    }
    return isHardX2OverwriteWithoutStackUse(op) ? { index, gaps } : undefined;
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

export const x2DeadRestoreBeforeOverwrite: IrPass = {
  name: "x2-dead-restore-before-overwrite",
  run,
  layoutSafe: false,
};
