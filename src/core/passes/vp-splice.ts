import type { IrOp } from "../types.ts";
import { computeX2ValueStates, emptyResult, hasRewriteBarrier, type IrPass, type IrPassFn } from "./helpers.ts";

const VP = 0x0c;
const SIGN_CHANGE = 0x0b;
const KNOP = 0x54;
const K1 = 0x55;
const K2 = 0x56;

function isPlainOpcode(op: IrOp, opcode: number): boolean {
  return op.kind === "plain" && op.opcode === opcode && !hasRewriteBarrier(op);
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

// Both rewrites are proven behaviorally equivalent on the MK-61 emulator:
//   ВП ВП  ≡ ВП   (a second exponent-entry while already in exponent mode is inert)
//   КНОП/К1/К2 ВП ≡ ВП  (an empty op immediately before exponent entry is removable)
//   ВП ... /-/ /-/ ... ≡ ВП ... ... while the dataflow proves exponent-entry
// Each collapse drops one or two cells without changing any observable result.
const run: IrPassFn = (ops) => {
  const remove = new Set<number>();
  const x2ValueStates = computeX2ValueStates(ops);
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
    // In exponent-entry mode /-/ only toggles the exponent sign. Adjacent toggles
    // cancel; outside exponent-entry this is not safe before following digits.
    if (
      isFreeStandingSignChange(prev) &&
      isFreeStandingSignChange(cur) &&
      x2ValueStates[i - 1]?.entry.kind === "exponent"
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
        detail: `Collapsed ${remove.size} redundant ВП/empty/sign cell(s) around an exponent-entry boundary (ВП ВП -> ВП, КНОП/К1/К2 ВП -> ВП, exponent /-/ /-/ -> empty).`,
      },
    ],
  };
};

export const vpSplice: IrPass = {
  name: "vp-splice",
  run,
  layoutSafe: false,
};
