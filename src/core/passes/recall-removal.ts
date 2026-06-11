// Shared engine for the recall-removal pass family.
//
// store-recall-peephole, last-x-reuse, flow-x-reuse, and branch-target-x-reuse
// all answer the same question — "is this П->X recall redundant because X
// already carries the value?" — with different scopes of proof (adjacent pair,
// linear scan, CFG intersection, branch-edge projection). This module owns the
// shared machinery: lazy X2/value/return analyses, the removal-plan call into
// the stack scheduler, and the rewrite/report boilerplate. Each pass keeps only
// its candidate-discovery logic and its established report identity.

import type { IrOp, RegisterName } from "../types.ts";
import {
  computeX2RegisterStates,
  computeX2ValueStates,
  directReturnAnalysisContext,
  emptyResult,
  planRecallRemovalWithStackScheduler,
  type DirectReturnAnalysisContext,
  type PassResult,
  type RecallRemovalStackSchedulerPlan,
  type X2ValueDataflowState,
} from "./helpers.ts";

export interface RecallRemovalPlanOverrides {
  /** Replace the indexed X2-register state, e.g. with a branch-edge projection. */
  readonly x2RegisterState?: ReadonlySet<RegisterName> | undefined;
  /** Replace the indexed X2 value state, e.g. with a branch-edge projection. */
  readonly x2ValueState?: X2ValueDataflowState | undefined;
  readonly stackSchedulerStart?: number;
  readonly stackExposureEnd?: number;
  readonly stackSchedulerState?: X2ValueDataflowState | undefined;
}

export interface RecallRemovalEngine {
  readonly ops: readonly IrOp[];
  /** Indexes already scheduled for removal by the current pass run. */
  readonly removed: Set<number>;
  x2RegisterState(index: number): ReadonlySet<RegisterName> | undefined;
  x2ValueState(index: number): X2ValueDataflowState | undefined;
  directReturnContext(): DirectReturnAnalysisContext;
  /**
   * Safety plan for removing the recall at `recallIndex`: X2 sync, stack lift,
   * and previous-command-context proofs via the shared stack scheduler. The
   * caller still owns the mode-specific "value already in X" proof.
   */
  plan(
    recallIndex: number,
    overrides?: RecallRemovalPlanOverrides,
  ): RecallRemovalStackSchedulerPlan | undefined;
}

export interface RecallRemovalReport {
  readonly name: string;
  readonly detail: (removedCount: number) => string;
}

/**
 * Run one recall-removal pass: `collect` marks redundant recall indexes in
 * `engine.removed`; the driver rewrites the op list and emits the report.
 * Analyses are computed lazily so passes that find no candidate pay nothing.
 */
export function runRecallRemovalPass(
  ops: readonly IrOp[],
  report: RecallRemovalReport,
  collect: (engine: RecallRemovalEngine) => void,
): PassResult {
  const engine = createEngine(ops);
  collect(engine);
  if (engine.removed.size === 0) return emptyResult(ops);
  return {
    ops: ops.filter((_, index) => !engine.removed.has(index)),
    applied: engine.removed.size,
    optimizations: [{ name: report.name, detail: report.detail(engine.removed.size) }],
  };
}

function createEngine(ops: readonly IrOp[]): RecallRemovalEngine {
  let x2States: Array<ReadonlySet<RegisterName> | undefined> | undefined;
  let x2ValueStates: Array<X2ValueDataflowState | undefined> | undefined;
  let returnContext: DirectReturnAnalysisContext | undefined;
  const removed = new Set<number>();

  const x2RegisterState = (index: number): ReadonlySet<RegisterName> | undefined => {
    x2States ??= computeX2RegisterStates(ops);
    return x2States[index];
  };
  const x2ValueState = (index: number): X2ValueDataflowState | undefined => {
    x2ValueStates ??= computeX2ValueStates(ops, { trackRegisterMemory: true });
    return x2ValueStates[index];
  };
  const directReturnContext = (): DirectReturnAnalysisContext => {
    returnContext ??= directReturnAnalysisContext(ops);
    return returnContext;
  };

  return {
    ops,
    removed,
    x2RegisterState,
    x2ValueState,
    directReturnContext,
    plan: (recallIndex, overrides = {}) =>
      planRecallRemovalWithStackScheduler(
        ops,
        recallIndex,
        "x2RegisterState" in overrides ? overrides.x2RegisterState : x2RegisterState(recallIndex),
        "x2ValueState" in overrides ? overrides.x2ValueState : x2ValueState(recallIndex),
        directReturnContext(),
        {
          removedIndexes: removed,
          ...(overrides.stackSchedulerStart !== undefined
            ? { stackSchedulerStart: overrides.stackSchedulerStart }
            : {}),
          ...(overrides.stackExposureEnd !== undefined
            ? { stackExposureEnd: overrides.stackExposureEnd }
            : {}),
          ...(overrides.stackSchedulerState !== undefined
            ? { stackSchedulerState: overrides.stackSchedulerState }
            : {}),
        },
      ),
  };
}
