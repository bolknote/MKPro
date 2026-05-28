import { lowerIrToLayout, lowerIrToMachine, raiseLayoutToIr, raiseMachineToIr } from "../ir.ts";
import type {
  AppliedOptimization,
  CompileOptions,
  IrOp,
  LayoutIrCell,
  MachineItem,
  PreloadReport,
} from "../types.ts";
import { arithmeticIfPass } from "./arithmetic-if.ts";
import { constantFolding } from "./constant-folding.ts";
import { cseDisplayBlock } from "./cse-display-block.ts";
import { deadCodeAfterHalt } from "./dead-code-after-halt.ts";
import { deadStoreBeforeCommutative } from "./dead-store-before-commutative.ts";
import { deadStoreElimination } from "./dead-store-elimination.ts";
import { duplicateFailureTail } from "./duplicate-failure-tail.ts";
import type { IrPass } from "./helpers.ts";
import { indirectMemoryTable, stableIndirectFlow } from "./indirect-addressing.ts";
import { jumpThread } from "./jump-thread.ts";
import { jumpToNextThreading } from "./jump-to-next.ts";
import { lastXReuse } from "./last-x-reuse.ts";
import { preloadedIndirectFlow } from "./preloaded-indirect-flow.ts";
import { redundantPrologueElimination } from "./redundant-prologue.ts";
import { registerCoalesce } from "./register-coalesce.ts";
import { returnZeroJump } from "./return-zero-jump.ts";
import { r0FractionalSentinel } from "./r0-fractional-sentinel.ts";
import { storeRecallPeephole } from "./store-recall-peephole.ts";
import { vpX2Peephole } from "./vp-x2-peephole.ts";

const PASS_PIPELINE: ReadonlyArray<IrPass> = [
  redundantPrologueElimination,
  returnZeroJump,
  storeRecallPeephole,
  jumpToNextThreading,
  jumpThread,
  stableIndirectFlow,
  preloadedIndirectFlow,
  indirectMemoryTable,
  deadStoreBeforeCommutative,
  deadStoreElimination,
  lastXReuse,
  r0FractionalSentinel,
  vpX2Peephole,
  constantFolding,
  duplicateFailureTail,
  cseDisplayBlock,
  deadCodeAfterHalt,
  registerCoalesce,
  arithmeticIfPass,
];

const MAX_FIXPOINT_ITERATIONS = 8;

export interface RunPassesResult {
  items: MachineItem[];
  applied: number;
  optimizations: AppliedOptimization[];
  passCounts: Record<string, number>;
  preloads: PreloadReport[];
}

interface RunOnIrOptions {
  layoutOnly: boolean;
}

function runPassesOnIr(
  initial: IrOp[],
  options: CompileOptions,
  { layoutOnly }: RunOnIrOptions,
): {
  ops: IrOp[];
  applied: number;
  optimizations: AppliedOptimization[];
  passCounts: Record<string, number>;
  preloads: PreloadReport[];
} {
  let current = initial;
  let totalApplied = 0;
  const optimizations: AppliedOptimization[] = [];
  const preloads: PreloadReport[] = [];
  const passCounts: Record<string, number> = {};
  let changedInIteration = true;
  let iteration = 0;
  while (changedInIteration && iteration < MAX_FIXPOINT_ITERATIONS) {
    changedInIteration = false;
    iteration += 1;
    for (const pass of PASS_PIPELINE) {
      if (layoutOnly && !pass.layoutSafe) continue;
      const result = pass.run(current, { options });
      passCounts[pass.name] = (passCounts[pass.name] ?? 0) + result.applied;
      if (result.applied > 0) {
        changedInIteration = true;
        totalApplied += result.applied;
        for (const opt of result.optimizations) {
          const existing = optimizations.find((entry) => entry.name === opt.name);
          if (existing !== undefined) {
            existing.detail = `${existing.detail} (+${opt.detail})`;
          } else {
            optimizations.push({ ...opt });
          }
        }
        if (result.preloads !== undefined) preloads.push(...result.preloads);
      }
      current = result.ops;
    }
  }
  return { ops: current, applied: totalApplied, optimizations, passCounts, preloads };
}

export function runIrPasses(items: MachineItem[], options: CompileOptions): RunPassesResult {
  const ir = raiseMachineToIr(items);
  const result = runPassesOnIr(ir, options, { layoutOnly: false });
  return {
    items: lowerIrToMachine(result.ops),
    applied: result.applied,
    optimizations: result.optimizations,
    passCounts: result.passCounts,
    preloads: result.preloads,
  };
}

export interface RunLayoutPassesResult {
  cells: LayoutIrCell[];
  applied: number;
  optimizations: AppliedOptimization[];
  passCounts: Record<string, number>;
  preloads: PreloadReport[];
}

export function runIrPassesOnLayout(
  cells: LayoutIrCell[],
  options: CompileOptions,
): RunLayoutPassesResult {
  const ir = raiseLayoutToIr(cells);
  const result = runPassesOnIr(ir, options, { layoutOnly: true });
  const lowered = lowerIrToLayout(result.ops);
  return {
    cells: lowered.cells,
    applied: result.applied,
    optimizations: result.optimizations,
    passCounts: result.passCounts,
    preloads: result.preloads,
  };
}

export { PASS_PIPELINE };
export type { IrPass };
