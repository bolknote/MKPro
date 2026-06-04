import type { IrOp } from "../types.ts";
import {
  analyzeX2StackEffect,
  analyzeX2VpShapeContext,
  computeX2ValueStates,
  emptyResult,
  hasRewriteBarrier,
  removingRecallCanExposeX2Restore,
  sameX2ExponentShapeContext,
  x2StateHasSameDotSafeDecimalInXAndX2,
  x2StateHasSameStructuralShapeInXAndX2,
  x2StateHasX2RestoreContext,
  x2StateIsClosedPlainContext,
  x2StatesHaveSameVpEntrySource,
  x2ValueSetHasIntersection,
  type IrPass,
  type IrPassFn,
  type X2ValueDataflowState,
} from "./helpers.ts";

const VP = 0x0c;
const SIGN_CHANGE = 0x0b;
const KNOP = 0x54;
const K1 = 0x55;
const K2 = 0x56;

function isPlainOpcode(op: IrOp, opcode: number): boolean {
  return op.kind === "plain" && op.opcode === opcode && !hasRewriteBarrier(op);
}

function isDecimalDigit(op: IrOp): boolean {
  return op.kind === "plain" && op.opcode >= 0 && op.opcode <= 9 && !hasRewriteBarrier(op);
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
): boolean {
  if (!x2StateIsClosedPlainContext(state)) return false;
  if (state === undefined) return false;
  if (
    !x2ValueSetHasIntersection(state.x, state.x2) &&
    !x2StateHasSameDotSafeDecimalInXAndX2(state) &&
    !x2StateHasSameStructuralShapeInXAndX2(state)
  ) return false;
  if (!removingRecallCanExposeX2Restore(ops, secondSignIndex)) return true;
  return canRemoveClosedContextSignPairBeforeProvedVp(ops, secondSignIndex, state, stateAfterPair);
}

function canRemoveOpenMantissaSignPairBeforeProvedVp(
  ops: readonly IrOp[],
  secondSignIndex: number,
  state: X2ValueDataflowState | undefined,
  stateAfterPair: X2ValueDataflowState | undefined,
): boolean {
  if (state?.entry.kind !== "open") return false;
  const nextIndex = nextNonLabelIndex(ops, secondSignIndex + 1);
  if (nextIndex === undefined || !isFreeStandingVp(ops[nextIndex]!)) return false;
  return sameNonEmptyStringSet(state.entry.raw, stateAfterPair?.vpEntryMantissa);
}

function canRemoveVpContextSignPairBeforeFreshDigit(
  ops: readonly IrOp[],
  secondSignIndex: number,
  state: X2ValueDataflowState | undefined,
  stateAfterPair: X2ValueDataflowState | undefined,
): boolean {
  const before = analyzeX2VpShapeContext(state);
  const after = analyzeX2VpShapeContext(stateAfterPair);
  if (!isVpExponentContext(before.kind) || !isVpExponentContext(after.kind)) return false;
  const nextIndex = nextFreshDigitIndex(ops, secondSignIndex + 1);
  if (nextIndex === undefined || !isDecimalDigit(ops[nextIndex]!)) return false;
  return sameX2ExponentShapeContext(before, after);
}

function canRemoveVpContextSignBeforeFreshDigit(
  ops: readonly IrOp[],
  signIndex: number,
  state: X2ValueDataflowState | undefined,
): boolean {
  if (!isVpExponentContext(analyzeX2VpShapeContext(state).kind)) return false;
  const nextIndex = nextFreshDigitIndex(ops, signIndex + 1);
  return nextIndex !== undefined && isDecimalDigit(ops[nextIndex]!);
}

function isActiveExponentContext(kind: ReturnType<typeof analyzeX2VpShapeContext>["kind"]): boolean {
  return kind === "active-exponent" || kind === "active-structural-exponent";
}

function isVpExponentContext(kind: ReturnType<typeof analyzeX2VpShapeContext>["kind"]): boolean {
  return kind === "vp-exponent-context" || kind === "vp-structural-exponent-context";
}

function canRemoveX2RestoreSignBeforeDeadOverwrite(
  ops: readonly IrOp[],
  signIndex: number,
  state: X2ValueDataflowState | undefined,
): boolean {
  return x2StateHasX2RestoreContext(state) && isFollowedByHardX2OverwriteWithoutStackUse(ops, signIndex + 1);
}

function canRemoveX2ContextEmptyBeforeDeadOverwrite(
  ops: readonly IrOp[],
  emptyIndex: number,
  state: X2ValueDataflowState | undefined,
): boolean {
  return x2StateHasX2RestoreContext(state) && isFollowedByHardX2OverwriteWithoutStackUse(ops, emptyIndex + 1);
}

function isFollowedByHardX2OverwriteWithoutStackUse(ops: readonly IrOp[], start: number): boolean {
  const nextIndex = nextFreshDigitIndex(ops, start);
  return nextIndex !== undefined && isHardX2OverwriteWithoutStackUse(ops[nextIndex]!);
}

function isHardX2OverwriteWithoutStackUse(op: IrOp): boolean {
  return analyzeX2StackEffect(op).hardX2OverwriteWithoutStackUse;
}

function canRemoveClosedContextSignPairBeforeProvedVp(
  ops: readonly IrOp[],
  secondSignIndex: number,
  state: X2ValueDataflowState,
  stateAfterPair: X2ValueDataflowState | undefined,
): boolean {
  const nextIndex = nextNonLabelIndex(ops, secondSignIndex + 1);
  if (nextIndex === undefined || !isFreeStandingVp(ops[nextIndex]!)) return false;
  return x2StatesHaveSameVpEntrySource(state, stateAfterPair);
}

function nextNonLabelIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    if (ops[index]!.kind !== "label") return index;
  }
  return undefined;
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

function sameNonEmptyStringSet(left: ReadonlySet<string> | undefined, right: ReadonlySet<string> | undefined): boolean {
  if (left === undefined || right === undefined || left.size === 0 || left.size !== right.size) return false;
  for (const value of left) {
    if (!right.has(value)) return false;
  }
  return true;
}

// These rewrites are proven behaviorally equivalent on the MK-61 emulator:
//   ВП ВП  ≡ ВП   (a second exponent-entry while already in exponent mode is inert)
//   КНОП/К1/К2 ВП ≡ ВП  (an empty op immediately before exponent entry is removable)
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
  for (let i = 1; i < ops.length; i += 1) {
    const prev = ops[i - 1]!;
    const cur = ops[i]!;
    if (remove.has(i - 1)) continue;
    // ВП ВП -> ВП : drop the redundant second exponent entry.
    if (isFreeStandingVp(prev) && isFreeStandingVp(cur)) {
      remove.add(i);
      continue;
    }
    // КНОП/К1/К2 ВП -> ВП : drop the inert empty op preceding exponent entry.
    if (isFreeStandingEmptyOp(prev) && isFreeStandingVp(cur)) {
      remove.add(i - 1);
      continue;
    }
    // After at least one exponent digit, an empty op only separates the number
    // from the following non-digit command. Removing it leaves that following
    // command to close exponent entry in the same place.
    if (
      isFreeStandingEmptyOp(prev) &&
      isActiveExponentContext(analyzeX2VpShapeContext(x2ValueStates[i - 1]).kind) &&
      analyzeX2VpShapeContext(x2ValueStates[i - 1]).hasExponentDigit &&
      !isDecimalDigit(cur)
    ) {
      remove.add(i - 1);
      continue;
    }
    // The same separator is also inert before /-/ after X2-preserving commands:
    // the digit entry is closed for ordinary digits, but the VP/X2 exponent
    // context is still visible to /-/.
    if (
      isFreeStandingEmptyOp(prev) &&
      isFreeStandingSignChange(cur) &&
      isVpExponentContext(analyzeX2VpShapeContext(x2ValueStates[i - 1]).kind) &&
      analyzeX2VpShapeContext(x2ValueStates[i - 1]).hasExponentDigit
    ) {
      remove.add(i - 1);
      continue;
    }
    // A VP/X2-context empty separator is inert before the same kind of hard
    // overwrite: its only remaining role would be previous-command context, and
    // that context is destroyed together with X/X2.
    if (
      isFreeStandingEmptyOp(cur) &&
      canRemoveX2ContextEmptyBeforeDeadOverwrite(ops, i, x2ValueStates[i])
    ) {
      remove.add(i);
      continue;
    }
    // In exponent-entry mode /-/ only toggles the exponent sign. Adjacent toggles
    // cancel; outside exponent-entry this is not safe before following digits.
    if (
      isFreeStandingSignChange(prev) &&
      isFreeStandingSignChange(cur) &&
      isActiveExponentContext(analyzeX2VpShapeContext(x2ValueStates[i - 1]).kind)
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
      canRemoveOpenMantissaSignPairBeforeProvedVp(ops, i, x2ValueStates[i - 1], x2ValueStates[i + 1])
    ) {
      remove.add(i - 1);
      remove.add(i);
    }
    // After an X2-preserving gap, a VP-context /-/ /-/ pair is observable
    // because it restores X2 into X even though the exponent sign cancels. A
    // following digit, possibly after empty ops, starts fresh number entry, so
    // that restored X is lost.
    if (
      isFreeStandingSignChange(prev) &&
      isFreeStandingSignChange(cur) &&
      canRemoveVpContextSignPairBeforeFreshDigit(ops, i, x2ValueStates[i - 1], x2ValueStates[i + 1])
    ) {
      remove.add(i - 1);
      remove.add(i);
    }
    // A single VP-context /-/ also only restores/toggles X2. If fresh number
    // entry starts next, possibly through empty ops, that restored X is lost.
    // Active exponent-entry is excluded by requiring a closed entry state.
    if (
      !remove.has(i) &&
      isFreeStandingSignChange(cur) &&
      canRemoveVpContextSignBeforeFreshDigit(ops, i, x2ValueStates[i])
    ) {
      remove.add(i);
    }
    // If a VP/X2-context sign restore is followed only by inert separators and
    // then by a command that unconditionally replaces both X and X2 without
    // stack use, the restored value cannot be observed.
    if (
      !remove.has(i) &&
      isFreeStandingSignChange(cur) &&
      canRemoveX2RestoreSignBeforeDeadOverwrite(ops, i, x2ValueStates[i])
    ) {
      remove.add(i);
    }
    // In closed context, two sign changes are only removable when value
    // dataflow proves ordinary decimal X and X2 equality and the pair is not
    // acting as a previous-command shield for a later `.`/`/-/`/`ВП`.
    if (
      isFreeStandingSignChange(prev) &&
      isFreeStandingSignChange(cur) &&
      canRemoveClosedContextSignPair(ops, i, x2ValueStates[i - 1], x2ValueStates[i + 1])
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
        detail: `Collapsed ${remove.size} redundant ВП/empty/sign cell(s) around an X2 boundary (ВП ВП -> ВП, КНОП/К1/К2 ВП -> ВП, exponent-digit empty separators, VP-context /-/ separators/signs before fresh digits/dead overwrites, exponent /-/ /-/ -> empty, mantissa /-/ /-/ before proved ВП/fresh digit -> empty, closed value /-/ /-/ -> empty).`,
      },
    ],
  };
};

export const vpSplice: IrPass = {
  name: "vp-splice",
  run,
  layoutSafe: false,
};
