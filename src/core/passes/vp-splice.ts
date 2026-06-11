import type { IrOp } from "../types.ts";
import {
  analyzeX2StackEffect,
  analyzeX2VpRestoreGapSource,
  analyzeX2VpShapeContext,
  analyzeX2VpShapeTransition,
  computeX2ValueStates,
  directReturnAnalysisContext,
  emptyResult,
  hasRewriteBarrier,
  isFreeStandingX2EmptyOp,
  isFreeStandingX2SignChangeOp,
  isFreeStandingX2VpOp,
  isDisplayFocusSensitive,
  removingRecallCanExposeX2Restore,
  x2PlanRestoreRunBeforeProvedVp,
  x2PreviousFreeStandingRestoreExecutableIndex,
  x2RestoreRunBeforeIndex,
  x2RestoreRunBeforeTerminal,
  x2StateHasSameClosedSignChangeSourceInXAndX2,
  x2StateIsClosedPlainContext,
  type DirectReturnAnalysisContext,
  type IrPass,
  type IrPassFn,
  type X2ValueDataflowState,
} from "./helpers.ts";

function isDecimalDigit(op: IrOp): boolean {
  return op.kind === "plain" &&
    op.opcode >= 0 &&
    op.opcode <= 9 &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op);
}

function isTransparentVpGapOp(op: IrOp): boolean {
  return op.kind === "label" || op.kind === "orphan-address";
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
  const source = analyzeX2VpRestoreGapSource(
    ops,
    secondSignIndex + 1,
    state,
    stateAfterPair,
    context,
    { includesLeadingSignRestore: true },
  );
  return source.replacementDotHasOnlyRestoreGapBeforeVp &&
    source.canDiscardShapeSignPairBeforeProvedVp;
}

function mantissaRestoreRunBeforeProvedVp(
  ops: readonly IrOp[],
  vpIndex: number,
  states: readonly (X2ValueDataflowState | undefined)[],
  context: DirectReturnAnalysisContext,
): readonly number[] {
  return x2PlanRestoreRunBeforeProvedVp(
    ops,
    vpIndex,
    states,
    context,
    { requireSignRestore: true },
  ).removableIndexes;
}

function x2ContextRestoreRunBeforeFreshDigit(
  ops: readonly IrOp[],
  startIndex: number,
  state: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext,
): readonly number[] {
  const transition = analyzeX2VpShapeTransition(state, "fresh-digit");
  if (!transition.canDiscardRestoreRun) {
    if (
      !x2StateIsClosedPlainContext(state) ||
      x2PreviousFreeStandingRestoreExecutableIndex(ops, startIndex) !== undefined
    ) return [];
  }
  return x2ContextRestoreRunBeforeFreshDigitEntry(ops, startIndex, context);
}

function x2ContextRestoreRunBeforeDeadOverwrite(
  ops: readonly IrOp[],
  startIndex: number,
  state: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext,
): readonly number[] {
  if (!analyzeX2VpShapeTransition(state, "hard-overwrite").canDiscardRestoreRun) return [];
  return x2ContextRestoreRunBeforeHardOverwrite(ops, startIndex, context);
}

function x2ContextRestoreRunBeforeFreshDigitEntry(
  ops: readonly IrOp[],
  startIndex: number,
  context: DirectReturnAnalysisContext,
): readonly number[] {
  return x2ContextRestoreRunBeforeTerminal(ops, startIndex, context, isDecimalDigit);
}

function isHardX2OverwriteWithoutStackUse(op: IrOp): boolean {
  return analyzeX2StackEffect(op).hardX2OverwriteWithoutStackUse;
}

function x2ContextRestoreRunBeforeHardOverwrite(
  ops: readonly IrOp[],
  startIndex: number,
  context: DirectReturnAnalysisContext,
): readonly number[] {
  return x2ContextRestoreRunBeforeTerminal(ops, startIndex, context, isHardX2OverwriteWithoutStackUse);
}

function x2ContextRestoreRunBeforeTerminal(
  ops: readonly IrOp[],
  startIndex: number,
  context: DirectReturnAnalysisContext,
  isTerminal: (op: IrOp) => boolean,
): readonly number[] {
  return x2RestoreRunBeforeTerminal(
    ops,
    startIndex,
    context,
    (op) => isTerminal(op),
  ).removableIndexes;
}

function canRemoveClosedContextSignPairBeforeProvedVp(
  ops: readonly IrOp[],
  secondSignIndex: number,
  state: X2ValueDataflowState,
  stateAfterPair: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext,
): boolean {
  const source = analyzeX2VpRestoreGapSource(
    ops,
    secondSignIndex + 1,
    state,
    stateAfterPair,
    context,
  );
  return source.replacementDotHasOnlyRestoreGapBeforeVp &&
    source.canDiscardRestoreRunBeforeProvedVp;
}

function freeStandingEmptyRunBeforeProvedVp(
  ops: readonly IrOp[],
  index: number,
  context: DirectReturnAnalysisContext,
): readonly number[] {
  return x2RestoreRunBeforeIndex(ops, index, context, { includeSignRestores: false }).removableIndexes;
}

function nextFreshDigitIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    if (isFreeStandingX2EmptyOp(op)) continue;
    return index;
  }
  return undefined;
}

function removableExponentSeparatorRun(
  ops: readonly IrOp[],
  startIndex: number,
  state: X2ValueDataflowState | undefined,
): readonly number[] {
  if (
    !analyzeX2VpShapeTransition(state, "empty-before-non-digit").canDiscardCurrentOp &&
    !analyzeX2VpShapeTransition(state, "empty-before-sign-change").canDiscardCurrentOp
  ) return [];

  const run: number[] = [];
  for (let index = startIndex; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (isTransparentVpGapOp(op)) continue;
    if (isFreeStandingX2EmptyOp(op)) {
      run.push(index);
      continue;
    }
    if (run.length === 0) return [];
    if (isDecimalDigit(op)) return [];
    if (isFreeStandingX2SignChangeOp(op)) {
      return analyzeX2VpShapeTransition(state, "empty-before-sign-change").canDiscardCurrentOp ? run : [];
    }
    return analyzeX2VpShapeTransition(state, "empty-before-non-digit").canDiscardCurrentOp ? run : [];
  }
  return [];
}

function canRemoveSecondVpAfterPreviousVp(
  stateBeforePreviousVp: X2ValueDataflowState | undefined,
): boolean {
  return analyzeX2VpShapeTransition(stateBeforePreviousVp, "vp").canDiscardCurrentOp;
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
      isFreeStandingX2VpOp(prev) &&
      isFreeStandingX2VpOp(cur) &&
      canRemoveSecondVpAfterPreviousVp(x2ValueStates[i - 1])
    ) {
      remove.add(i);
      continue;
    }
    // КНОП/К1/К2 ... ВП -> ВП : drop the inert empty run preceding exponent entry.
    if (isFreeStandingX2VpOp(cur)) {
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
          isFreeStandingX2VpOp(ops[firstEmpty - 1]!) &&
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
    if (isFreeStandingX2EmptyOp(cur)) {
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
      isFreeStandingX2EmptyOp(prev) &&
      isFreeStandingX2SignChangeOp(cur) &&
      analyzeX2VpShapeTransition(x2ValueStates[i - 1], "empty-before-sign-change").canDiscardCurrentOp
    ) {
      remove.add(i - 1);
      continue;
    }
    // A VP/X2-context restore/empty run is inert before the same kind of hard
    // overwrite: its only remaining role would be restored X/previous-command
    // context, and that context is destroyed together with X/X2.
    if (isFreeStandingX2EmptyOp(cur) || isFreeStandingX2SignChangeOp(cur)) {
      const restoreRun = x2ContextRestoreRunBeforeDeadOverwrite(ops, i, x2ValueStates[i], context);
      if (restoreRun.length > 0) {
        for (const runIndex of restoreRun) remove.add(runIndex);
        continue;
      }
    }
    // In exponent-entry mode /-/ only toggles the exponent sign. Adjacent toggles
    // cancel; outside exponent-entry this is not safe before following digits.
    if (
      isFreeStandingX2SignChangeOp(prev) &&
      isFreeStandingX2SignChangeOp(cur) &&
      analyzeX2VpShapeTransition(x2ValueStates[i - 1], "sign-pair").canDiscardSignPair
    ) {
      remove.add(i - 1);
      remove.add(i);
    }
    // A mantissa sign pair before ВП also cancels for non-zero digit-entry
    // shapes. Signed zero is deliberately excluded because `/-/` leaves a
    // sticky `-0` mantissa shape there.
    if (
      isFreeStandingX2SignChangeOp(prev) &&
      isFreeStandingX2SignChangeOp(cur) &&
      canRemoveOpenMantissaSignPairBeforeProvedVp(ops, i, x2ValueStates[i - 1], x2ValueStates[i + 1], context)
    ) {
      remove.add(i - 1);
      remove.add(i);
    }
    // A closed VP/X2-context restore/empty run is discarded by fresh digit
    // entry: every restored X/previous-command effect is overwritten by the
    // new number. Active exponent-entry is excluded because a following digit
    // is an exponent digit there, not fresh mantissa entry.
    if (isFreeStandingX2EmptyOp(cur) || isFreeStandingX2SignChangeOp(cur)) {
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
      isFreeStandingX2SignChangeOp(prev) &&
      isFreeStandingX2SignChangeOp(cur) &&
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
