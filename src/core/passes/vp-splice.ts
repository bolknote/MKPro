import type { IrOp } from "../types.ts";
import {
  analyzeX2StackEffect,
  analyzeX2VpShapeContext,
  computeX2ValueStates,
  directReturnAnalysisContext,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  isKnownReturnCallOp,
  knownReturnCallReturnsThroughNestedTransparentRange,
  removingRecallCanExposeX2Restore,
  x2StateHasSameClosedSignChangeSourceInXAndX2,
  x2StateHasX2RestoreContext,
  x2StateCanDiscardRestoreRunBeforeProvedVp,
  x2StateIsClosedPlainContext,
  x2StatesHaveSameVpEntrySource,
  type DirectReturnAnalysisContext,
  type IrPass,
  type IrPassFn,
  type KnownReturnCallOp,
  type X2ValueDataflowState,
} from "./helpers.ts";

const VP = 0x0c;
const SIGN_CHANGE = 0x0b;
const KNOP = 0x54;
const K1 = 0x55;
const K2 = 0x56;

function isPlainOpcode(op: IrOp, opcode: number): boolean {
  return op.kind === "plain" &&
    op.opcode === opcode &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op);
}

function isDecimalDigit(op: IrOp): boolean {
  return op.kind === "plain" &&
    op.opcode >= 0 &&
    op.opcode <= 9 &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op);
}

// A ВП (exponent-entry) op that carries no layout/role contract, so removing the
// cell only shifts later addresses (which the layout fixpoint reconciles).
function isFreeStandingVp(op: IrOp): boolean {
  if (!isPlainOpcode(op, VP)) return false;
  return !("meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0);
}

function isFreeStandingEmptyOp(op: IrOp): boolean {
  if (!isPlainOpcode(op, KNOP) && !isPlainOpcode(op, K1) && !isPlainOpcode(op, K2)) return false;
  return !("meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0);
}

function isFreeStandingSignChange(op: IrOp): boolean {
  if (!isPlainOpcode(op, SIGN_CHANGE)) return false;
  return !("meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0);
}

function canRemoveClosedContextSignPair(
  ops: readonly IrOp[],
  secondSignIndex: number,
  state: X2ValueDataflowState | undefined,
  stateAfterPair: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext,
): boolean {
  if (!x2StateIsClosedPlainContext(state)) return false;
  if (state === undefined) return false;
  if (!x2StateHasSameClosedSignChangeSourceInXAndX2(state)) return false;
  if (!removingRecallCanExposeX2Restore(ops, secondSignIndex)) return true;
  return canRemoveClosedContextSignPairBeforeProvedVp(ops, secondSignIndex, state, stateAfterPair, context);
}

function canRemoveOpenMantissaSignPairBeforeProvedVp(
  ops: readonly IrOp[],
  secondSignIndex: number,
  state: X2ValueDataflowState | undefined,
  stateAfterPair: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext,
): boolean {
  if (analyzeX2VpShapeContext(state).kind !== "active-mantissa") return false;
  const nextIndex = nextVpAfterTransparentRestoreGap(ops, secondSignIndex + 1, context);
  if (nextIndex === undefined || !isFreeStandingVp(ops[nextIndex]!)) return false;
  return x2StateCanDiscardRestoreRunBeforeProvedVp(state, stateAfterPair);
}

function canRemoveMantissaRestoreRunBeforeProvedVp(
  state: X2ValueDataflowState | undefined,
  stateAfterRun: X2ValueDataflowState | undefined,
): boolean {
  return x2StateCanDiscardRestoreRunBeforeProvedVp(state, stateAfterRun);
}

function mantissaRestoreRunBeforeProvedVp(
  ops: readonly IrOp[],
  vpIndex: number,
  states: readonly (X2ValueDataflowState | undefined)[],
  context: DirectReturnAnalysisContext,
): readonly number[] {
  const run: number[] = [];
  let sawSign = false;
  for (let cursor = vpIndex - 1; cursor >= 0; cursor -= 1) {
    const op = ops[cursor]!;
    if (op.kind === "label") continue;
    if (isFreeStandingEmptyOp(op)) {
      run.push(cursor);
      continue;
    }
    if (isFreeStandingSignChange(op)) {
      run.push(cursor);
      sawSign = true;
      continue;
    }
    if (isKnownReturnCallOp(op) && simpleDirectReturnDoesNotObserveRestore(ops, op, context)) continue;
    break;
  }
  if (!sawSign) return [];
  run.reverse();
  const first = run[0];
  if (first === undefined) return [];
  return canRemoveMantissaRestoreRunBeforeProvedVp(states[first], states[vpIndex]) ? run : [];
}

function x2ContextRestoreRunBeforeFreshDigit(
  ops: readonly IrOp[],
  startIndex: number,
  state: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext,
): readonly number[] {
  if (!analyzeX2VpShapeContext(state).canDiscardRestoreBeforeFreshDigit) return [];
  return x2ContextRestoreRunBeforeFreshDigitEntry(ops, startIndex, context);
}

function x2ContextRestoreRunBeforeDeadOverwrite(
  ops: readonly IrOp[],
  startIndex: number,
  state: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext,
): readonly number[] {
  if (!x2StateHasX2RestoreContext(state)) return [];
  return x2ContextRestoreRunBeforeHardOverwrite(ops, startIndex, context);
}

function x2ContextRestoreRunBeforeFreshDigitEntry(
  ops: readonly IrOp[],
  startIndex: number,
  context: DirectReturnAnalysisContext,
): readonly number[] {
  const removableIndexes: number[] = [];
  for (let index = startIndex; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isFreeStandingEmptyOp(op) || isFreeStandingSignChange(op)) {
      removableIndexes.push(index);
      continue;
    }
    if (op.kind === "label") continue;
    if (isKnownReturnCallOp(op) && simpleDirectReturnDoesNotObserveRestore(ops, op, context)) continue;
    return removableIndexes.length > 0 && isDecimalDigit(op) ? removableIndexes : [];
  }
  return [];
}

function isHardX2OverwriteWithoutStackUse(op: IrOp): boolean {
  return analyzeX2StackEffect(op).hardX2OverwriteWithoutStackUse;
}

function x2ContextRestoreRunBeforeHardOverwrite(
  ops: readonly IrOp[],
  startIndex: number,
  context: DirectReturnAnalysisContext,
): readonly number[] {
  const removableIndexes: number[] = [];
  for (let index = startIndex; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isFreeStandingEmptyOp(op) || isFreeStandingSignChange(op)) {
      removableIndexes.push(index);
      continue;
    }
    if (op.kind === "label") continue;
    if (isKnownReturnCallOp(op) && simpleDirectReturnDoesNotObserveRestore(ops, op, context)) continue;
    return removableIndexes.length > 0 && isHardX2OverwriteWithoutStackUse(op) ? removableIndexes : [];
  }
  return [];
}

function simpleDirectReturnDoesNotObserveRestore(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  context: DirectReturnAnalysisContext,
): boolean {
  return knownReturnCallReturnsThroughNestedTransparentRange(ops, call, context, isRestoreTransparentLinearOp);
}

function isRestoreTransparentLinearOp(op: IrOp): boolean {
  return op.kind === "orphan-address" || isFreeStandingEmptyOp(op);
}

function canRemoveClosedContextSignPairBeforeProvedVp(
  ops: readonly IrOp[],
  secondSignIndex: number,
  state: X2ValueDataflowState,
  stateAfterPair: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext,
): boolean {
  const nextIndex = nextVpAfterTransparentRestoreGap(ops, secondSignIndex + 1, context);
  if (nextIndex === undefined || !isFreeStandingVp(ops[nextIndex]!)) return false;
  return x2StatesHaveSameVpEntrySource(state, stateAfterPair);
}

function nextVpAfterTransparentRestoreGap(
  ops: readonly IrOp[],
  start: number,
  context: DirectReturnAnalysisContext,
): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    if (isKnownReturnCallOp(op) && simpleDirectReturnDoesNotObserveRestore(ops, op, context)) continue;
    return isFreeStandingVp(op) ? index : undefined;
  }
  return undefined;
}

function freeStandingEmptyRunBeforeProvedVp(
  ops: readonly IrOp[],
  index: number,
  context: DirectReturnAnalysisContext,
): readonly number[] {
  const indexes: number[] = [];
  for (let cursor = index - 1; cursor >= 0; cursor -= 1) {
    const op = ops[cursor]!;
    if (op.kind === "label") continue;
    if (isFreeStandingEmptyOp(op)) {
      indexes.push(cursor);
      continue;
    }
    if (isKnownReturnCallOp(op) && simpleDirectReturnDoesNotObserveRestore(ops, op, context)) continue;
    break;
  }
  indexes.reverse();
  return indexes;
}

function nextFreshDigitIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    if (isFreeStandingEmptyOp(op)) continue;
    return index;
  }
  return undefined;
}

function removableExponentSeparatorRun(
  ops: readonly IrOp[],
  startIndex: number,
  state: X2ValueDataflowState | undefined,
): readonly number[] {
  const context = analyzeX2VpShapeContext(state);
  if (!context.canDiscardSeparatorBeforeNonDigit && !context.canDiscardSeparatorBeforeSignChange) return [];

  const run: number[] = [];
  for (let index = startIndex; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    if (isFreeStandingEmptyOp(op)) {
      run.push(index);
      continue;
    }
    if (run.length === 0) return [];
    if (isDecimalDigit(op)) return [];
    if (isFreeStandingSignChange(op)) {
      return context.canDiscardSeparatorBeforeSignChange || context.canDiscardSeparatorBeforeNonDigit ? run : [];
    }
    return context.canDiscardSeparatorBeforeNonDigit ? run : [];
  }
  return [];
}

function canRemoveSecondVpAfterPreviousVp(
  stateBeforePreviousVp: X2ValueDataflowState | undefined,
): boolean {
  const context = analyzeX2VpShapeContext(stateBeforePreviousVp);
  return context.kind === "active-mantissa" ||
    context.kind === "active-exponent" ||
    context.kind === "active-structural-exponent";
}

// These rewrites are proven behaviorally equivalent on the MK-61 emulator:
//   ВП ВП  ≡ ВП   only when the first ВП is entered from an active number-entry
//       context; a closed-context X2 ВП restore can make the second ВП observable.
//   КНОП/К1/К2 ... ВП ≡ ВП  (empty ops immediately before exponent entry are removable)
//   ВП ... /-/ /-/ ... ≡ ВП ... ... while the dataflow proves exponent-entry
//   value-safe /-/ /-/ ≡ empty in closed context when X and X2 are proven equal,
//       unless the pair shields a downstream context-sensitive X2 restore
//   ВП digit КНОП/К1/К2 op ≡ ВП digit op while op is not another digit
//      (after an exponent digit the empty op is only a separator before a
//       non-digit command; before the first digit, or before another digit, it
//       changes number-entry shape and must stay)
// Each collapse drops one or two cells without changing any observable result.
const run: IrPassFn = (ops) => {
  const remove = new Set<number>();
  const x2ValueStates = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const context = directReturnAnalysisContext(ops);
  for (let i = 1; i < ops.length; i += 1) {
    const prev = ops[i - 1]!;
    const cur = ops[i]!;
    if (remove.has(i - 1)) continue;
    // ВП ВП -> ВП : drop the redundant second exponent entry.
    if (
      isFreeStandingVp(prev) &&
      isFreeStandingVp(cur) &&
      canRemoveSecondVpAfterPreviousVp(x2ValueStates[i - 1])
    ) {
      remove.add(i);
      continue;
    }
    // КНОП/К1/К2 ... ВП -> ВП : drop the inert empty run preceding exponent entry.
    if (isFreeStandingVp(cur)) {
      const restoreRun = mantissaRestoreRunBeforeProvedVp(ops, i, x2ValueStates, context);
      if (restoreRun.length > 0) {
        for (const runIndex of restoreRun) remove.add(runIndex);
        continue;
      }
      const emptyRun = freeStandingEmptyRunBeforeProvedVp(ops, i, context);
      if (emptyRun.length > 0) {
        for (const emptyIndex of emptyRun) remove.add(emptyIndex);
        const firstEmpty = emptyRun[0]!;
        if (
          firstEmpty === i - emptyRun.length &&
          firstEmpty > 0 &&
          isFreeStandingVp(ops[firstEmpty - 1]!) &&
          canRemoveSecondVpAfterPreviousVp(x2ValueStates[firstEmpty - 1])
        ) {
          remove.add(i);
        }
        continue;
      }
    }
    // After at least one exponent digit, an empty op only separates the number
    // from the following non-digit command. Removing it leaves that following
    // command to close exponent entry in the same place. Labels are not
    // commands here: the scanner decides from the next executable opcode.
    const previousContext = analyzeX2VpShapeContext(x2ValueStates[i - 1]);
    if (isFreeStandingEmptyOp(cur)) {
      const separatorRun = removableExponentSeparatorRun(ops, i, x2ValueStates[i]);
      if (separatorRun.length > 0) {
        for (const separatorIndex of separatorRun) remove.add(separatorIndex);
        continue;
      }
    }
    // The same separator scanner also handles /-/ after X2-preserving commands:
    // the digit entry is closed for ordinary digits, but the VP/X2 exponent
    // context is still visible to /-/.
    if (
      isFreeStandingEmptyOp(prev) &&
      isFreeStandingSignChange(cur) &&
      previousContext.canDiscardSeparatorBeforeSignChange
    ) {
      remove.add(i - 1);
      continue;
    }
    // A VP/X2-context restore/empty run is inert before the same kind of hard
    // overwrite: its only remaining role would be restored X/previous-command
    // context, and that context is destroyed together with X/X2.
    if (isFreeStandingEmptyOp(cur) || isFreeStandingSignChange(cur)) {
      const restoreRun = x2ContextRestoreRunBeforeDeadOverwrite(ops, i, x2ValueStates[i], context);
      if (restoreRun.length > 0) {
        for (const runIndex of restoreRun) remove.add(runIndex);
        continue;
      }
    }
    // In exponent-entry mode /-/ only toggles the exponent sign. Adjacent toggles
    // cancel; outside exponent-entry this is not safe before following digits.
    if (
      isFreeStandingSignChange(prev) &&
      isFreeStandingSignChange(cur) &&
      previousContext.canCancelExponentSignPair
    ) {
      remove.add(i - 1);
      remove.add(i);
    }
    // A mantissa sign pair before ВП also cancels for non-zero digit-entry
    // shapes. Signed zero is deliberately excluded because `/-/` leaves a
    // sticky `-0` mantissa shape there.
    if (
      isFreeStandingSignChange(prev) &&
      isFreeStandingSignChange(cur) &&
      canRemoveOpenMantissaSignPairBeforeProvedVp(ops, i, x2ValueStates[i - 1], x2ValueStates[i + 1], context)
    ) {
      remove.add(i - 1);
      remove.add(i);
    }
    // A closed VP/X2-context restore/empty run is discarded by fresh digit
    // entry: every restored X/previous-command effect is overwritten by the
    // new number. Active exponent-entry is excluded because a following digit
    // is an exponent digit there, not fresh mantissa entry.
    if (isFreeStandingEmptyOp(cur) || isFreeStandingSignChange(cur)) {
      const restoreRun = x2ContextRestoreRunBeforeFreshDigit(ops, i, x2ValueStates[i], context);
      if (restoreRun.length > 0) {
        for (const runIndex of restoreRun) remove.add(runIndex);
        continue;
      }
    }
    // In closed context, two sign changes are only removable when value
    // dataflow proves ordinary decimal X and X2 equality and the pair is not
    // acting as a previous-command shield for a later `.`/`/-/`/`ВП`.
    if (
      isFreeStandingSignChange(prev) &&
      isFreeStandingSignChange(cur) &&
      canRemoveClosedContextSignPair(ops, i, x2ValueStates[i - 1], x2ValueStates[i + 1], context)
    ) {
      remove.add(i - 1);
      remove.add(i);
    }
  }
  if (remove.size === 0) return emptyResult(ops);
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [
      {
        name: "vp-exponent-splice",
        detail: `Collapsed ${remove.size} redundant ВП/empty/sign cell(s) around an X2 boundary (active-entry ВП ВП -> ВП, КНОП/К1/К2 ВП -> ВП, exponent-digit empty separators, VP-context /-/ separators/signs before fresh digits/dead overwrites, exponent /-/ /-/ -> empty, mantissa /-/ and empty restore runs before proved ВП/fresh digit -> empty, closed value /-/ /-/ -> empty).`,
      },
    ],
  };
};

export const vpSplice: IrPass = {
  name: "vp-splice",
  run,
  layoutSafe: false,
};
