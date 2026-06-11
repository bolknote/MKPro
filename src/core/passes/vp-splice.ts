import type { IrOp } from "../types.ts";
import {
  analyzeX2StackEffect,
  computeX2ValueStates,
  directReturnAnalysisContext,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  x2PlanVpSpliceAt,
  type IrPass,
  type IrPassFn,
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
    if (remove.has(i - 1)) continue;
    const plan = x2PlanVpSpliceAt(ops, i, x2ValueStates, context, {
      isDecimalDigit,
      isHardX2OverwriteWithoutStackUse,
    });
    if (plan.removableIndexes.length > 0) {
      for (const index of plan.removableIndexes) remove.add(index);
      continue;
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
