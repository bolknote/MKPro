import type { IrOp } from "../types.ts";
import {
  analyzeX2StackEffect,
  computeX2ValueStates,
  directReturnAnalysisContext,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  x2PlanVpSpliceCandidatesAt,
  type IrPass,
  type IrPassFn,
  type X2VpSignRestoreSourceProofReason,
  type X2VpSourceMatchReason,
  type X2VpSpliceCandidateStage,
} from "./helpers.ts";

function isDecimalDigit(op: IrOp): boolean {
  return op.kind === "plain" &&
    op.opcode >= 0 &&
    op.opcode <= 9 &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op);
}

function isHardX2OverwriteWithoutStackUse(op: IrOp): boolean {
  return analyzeX2StackEffect(op).hardX2OverwriteWithoutStackUse;
}

interface VpSpliceCandidateSelection {
  readonly startIndex: number;
  readonly stage: X2VpSpliceCandidateStage;
  readonly sourceMatchReason: X2VpSourceMatchReason | undefined;
  readonly signRestoreSourceProofReason: X2VpSignRestoreSourceProofReason | undefined;
  readonly removableIndexes: readonly number[];
}

const stageOrder: readonly X2VpSpliceCandidateStage[] = [
  "duplicate-vp",
  "proved-vp",
  "exponent-boundary",
  "hard-overwrite-terminal",
  "sign-pair-before-fresh-digit",
  "fresh-digit-terminal",
  "closed-sign-pair",
];

const sourceMatchReasonOrder: readonly X2VpSourceMatchReason[] = [
  "same-exponent-context",
  "active-mantissa-source",
  "entry-source",
  "explicit-sign-source",
  "nonzero-sign-source",
  "source-mismatch",
];

const signRestoreSourceProofReasonOrder: readonly X2VpSignRestoreSourceProofReason[] = [
  "shape-transition",
  "source-match-explicit-sign",
  "source-match-nonzero-sign",
  "shared-sign-source",
  "no-sign-restore-source",
];

function stageRank(stage: X2VpSpliceCandidateStage): number {
  const rank = stageOrder.indexOf(stage);
  return rank < 0 ? stageOrder.length : rank;
}

function rankedIndex<T>(order: readonly T[], value: T | undefined): number | undefined {
  if (value === undefined) return undefined;
  const rank = order.indexOf(value);
  return rank < 0 ? order.length : rank;
}

function sourceMatchReasonRank(reason: X2VpSourceMatchReason | undefined): number | undefined {
  return rankedIndex(sourceMatchReasonOrder, reason);
}

function signRestoreSourceProofReasonRank(
  reason: X2VpSignRestoreSourceProofReason | undefined,
): number | undefined {
  return rankedIndex(signRestoreSourceProofReasonOrder, reason);
}

function compareOptionalRanks(leftRank: number | undefined, rightRank: number | undefined): number {
  if (leftRank === undefined || rightRank === undefined) return 0;
  return leftRank - rightRank;
}

function compareSourceProofReason(
  left: VpSpliceCandidateSelection,
  right: VpSpliceCandidateSelection,
): number {
  const sourceDiff = compareOptionalRanks(
    sourceMatchReasonRank(left.sourceMatchReason),
    sourceMatchReasonRank(right.sourceMatchReason),
  );
  if (sourceDiff !== 0) return sourceDiff;
  return compareOptionalRanks(
    signRestoreSourceProofReasonRank(left.signRestoreSourceProofReason),
    signRestoreSourceProofReasonRank(right.signRestoreSourceProofReason),
  );
}

function firstRemovableIndex(removableIndexes: readonly number[]): number {
  return Math.min(...removableIndexes);
}

function lastRemovableIndex(removableIndexes: readonly number[]): number {
  return Math.max(...removableIndexes);
}

function compareCandidates(left: VpSpliceCandidateSelection, right: VpSpliceCandidateSelection): number {
  const firstDiff = firstRemovableIndex(left.removableIndexes) - firstRemovableIndex(right.removableIndexes);
  if (firstDiff !== 0) return firstDiff;

  const lastDiff = lastRemovableIndex(right.removableIndexes) - lastRemovableIndex(left.removableIndexes);
  if (lastDiff !== 0) return lastDiff;

  const lengthDiff = right.removableIndexes.length - left.removableIndexes.length;
  if (lengthDiff !== 0) return lengthDiff;

  const sourceDiff = compareSourceProofReason(left, right);
  if (sourceDiff !== 0) return sourceDiff;

  const stageDiff = stageRank(left.stage) - stageRank(right.stage);
  if (stageDiff !== 0) return stageDiff;

  return left.startIndex - right.startIndex;
}

function canApplyCandidate(removableIndexes: readonly number[], remove: ReadonlySet<number>): boolean {
  return removableIndexes.length > 0 && removableIndexes.every((index) => !remove.has(index));
}

function stageCountsDetail(stageCounts: ReadonlyMap<X2VpSpliceCandidateStage, number>): string {
  const parts = stageOrder
    .map((stage) => {
      const count = stageCounts.get(stage);
      return count === undefined ? undefined : `${stage}=${count}`;
    })
    .filter((part): part is string => part !== undefined);
  return parts.length === 0 ? "" : ` Stages: ${parts.join(", ")}.`;
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
// Each collapse drops one or more cells without changing any observable result.
const run: IrPassFn = (ops) => {
  const remove = new Set<number>();
  const stageCounts = new Map<X2VpSpliceCandidateStage, number>();
  const x2ValueStates = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const context = directReturnAnalysisContext(ops);
  const candidates: VpSpliceCandidateSelection[] = [];
  for (let i = 1; i < ops.length; i += 1) {
    const planned = x2PlanVpSpliceCandidatesAt(ops, i, x2ValueStates, context, {
      isDecimalDigit,
      isHardX2OverwriteWithoutStackUse,
    });
    for (const candidate of planned) {
      if (candidate.splice.removableIndexes.length === 0) continue;
      candidates.push({
        startIndex: i,
        stage: candidate.stage,
        sourceMatchReason: candidate.sourceMatchReason,
        signRestoreSourceProofReason: candidate.signRestoreSourceProofReason,
        removableIndexes: candidate.splice.removableIndexes,
      });
    }
  }

  candidates.sort(compareCandidates);
  for (const candidate of candidates) {
    if (canApplyCandidate(candidate.removableIndexes, remove)) {
      for (const index of candidate.removableIndexes) remove.add(index);
      stageCounts.set(
        candidate.stage,
        (stageCounts.get(candidate.stage) ?? 0) + candidate.removableIndexes.length,
      );
    }
  }
  if (remove.size === 0) return emptyResult(ops);
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [
      {
        name: "vp-exponent-splice",
        detail: `Collapsed ${remove.size} redundant ВП/empty/sign cell(s) around an X2 boundary (active-entry ВП ВП -> ВП, КНОП/К1/К2 ВП -> ВП, exponent-digit empty separators, VP-context /-/ separators/signs before fresh digits/dead overwrites, exponent /-/ /-/ -> empty, mantissa /-/ and empty restore runs before proved ВП/fresh digit -> empty, closed value /-/ /-/ -> empty).${stageCountsDetail(stageCounts)}`,
      },
    ],
  };
};

export const vpSplice: IrPass = {
  name: "vp-splice",
  run,
  layoutSafe: false,
};
