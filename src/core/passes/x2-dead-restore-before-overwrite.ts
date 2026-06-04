import { getOpcode } from "../opcodes.ts";
import type { IrOp } from "../types.ts";
import {
  computeX2ValueStates,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  plainPreservesXValue,
  type IrPass,
  type IrPassFn,
  type X2ValueDataflowState,
  type X2ValueFact,
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
  if (op.opcode === DOT || op.opcode === SIGN_CHANGE) {
    return state.entry.kind === "closed" &&
      (state.vpContext === undefined || state.vpContext.kind === "none") &&
      hasDotSafeDecimalX2Value(state);
  }
  if (op.opcode === VP) {
    return state.entry.kind === "open" ||
      state.entry.kind === "exponent" ||
      (state.entry.kind === "closed" && state.vpEntryMantissa !== undefined);
  }
  return false;
}

function hasDotSafeDecimalX2Value(state: X2ValueDataflowState): boolean {
  for (const fact of state.x2) {
    if (isDecimalX2ValueFact(fact)) return true;
  }
  return false;
}

function isDecimalX2ValueFact(fact: X2ValueFact): boolean {
  return /^decimal:-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+):(normalized|unnormalized)$/u.test(fact);
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
  if (op.kind !== "plain" || hasRewriteBarrier(op)) return false;
  const opcode = getOpcode(op.opcode);
  return opcode.x2Effect === "affects" &&
    opcode.stackEffect === "preserves" &&
    !plainPreservesXValue(op);
}

export const x2DeadRestoreBeforeOverwrite: IrPass = {
  name: "x2-dead-restore-before-overwrite",
  run,
  layoutSafe: false,
};
