import type { IrOp } from "../types.ts";
import {
  computeX2DotRestoreGapStates,
  computeX2ImmediateSyncStates,
  computeX2ValueStates,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  x2CanUseDotRestoreAt,
  x2SyncCanExposeContextSensitiveRestore,
  x2StateHasSameDotRestoreValueInXAndX2,
  x2StateIsClosedPlainContext,
  x2StatesHaveSameVpEntrySource,
  type IrPass,
  type IrPassFn,
  type X2ValueDataflowState,
} from "./helpers.ts";

const DOT = 0x0a;

const run: IrPassFn = (ops) => {
  const valueStates = computeX2ValueStates(ops);
  const dotSafeStates = computeX2DotRestoreGapStates(ops);
  const immediateSyncStates = computeX2ImmediateSyncStates(ops);
  const removed = new Set<number>();

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (!isPlainDot(op)) continue;
    if (isDisplayFocusSensitive(op)) continue;
    const state = valueStates[index];
    if (
      !x2CanUseDotRestoreAt(ops, index, state, dotSafeStates[index] === true, immediateSyncStates[index] === true)
    ) continue;
    if (!isDotSafeValueContext(state)) continue;
    if (!x2StateHasSameDotRestoreValueInXAndX2(state)) continue;
    if (dotCanExposeContextSensitiveRestore(ops, index, state, valueStates[index + 1])) continue;
    removed.add(index);
  }

  if (removed.size === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  return {
    ops: ops.filter((_, index) => !removed.has(index)),
    applied: removed.size,
    optimizations: [
      {
        name: "x2-noop-restore",
        detail: `Removed ${removed.size} . restore${removed.size === 1 ? "" : "s"} whose X2 value was already in X.`,
      },
    ],
  };
};

function isPlainDot(op: IrOp): op is Extract<IrOp, { kind: "plain" }> {
  return op.kind === "plain" && op.opcode === DOT && !hasRewriteBarrier(op);
}

function dotCanExposeContextSensitiveRestore(
  ops: readonly IrOp[],
  index: number,
  state: X2ValueDataflowState | undefined,
  stateAfterDot: X2ValueDataflowState | undefined,
): boolean {
  if (!x2SyncCanExposeContextSensitiveRestore(ops, index)) return false;
  return !dotPreservesEmptyVpEntrySource(ops, index, state, stateAfterDot);
}

function dotPreservesEmptyVpEntrySource(
  ops: readonly IrOp[],
  index: number,
  state: X2ValueDataflowState | undefined,
  stateAfterDot: X2ValueDataflowState | undefined,
): boolean {
  return x2StatesHaveSameVpEntrySource(state, stateAfterDot) && hasOnlyEmptyGapBeforeVp(ops, index + 1);
}

function hasOnlyEmptyGapBeforeVp(ops: readonly IrOp[], start: number): boolean {
  let sawEmpty = false;
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isFreeStandingEmptyOp(op)) {
      sawEmpty = true;
      continue;
    }
    return sawEmpty && isFreeStandingVp(op);
  }
  return false;
}

function isFreeStandingVp(op: IrOp): boolean {
  return op.kind === "plain" &&
    op.opcode === 0x0c &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasRoles(op);
}

function isFreeStandingEmptyOp(op: IrOp): boolean {
  return op.kind === "plain" &&
    op.opcode >= 0x54 &&
    op.opcode <= 0x56 &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasRoles(op);
}

function hasRoles(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return "meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0;
}

function isDotSafeValueContext(
  state: ReturnType<typeof computeX2ValueStates>[number],
): boolean {
  return x2StateIsClosedPlainContext(state);
}

export const x2NoopRestore: IrPass = {
  name: "x2-noop-restore",
  run,
  layoutSafe: false,
};
