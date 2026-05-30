import type { NearAnyHelperStats, XParamProcLowering } from "../compiler.ts";
import type { ExpressionAst, ProcAst } from "../types.ts";

/**
 * Read-only program analysis computed once per lowering attempt and injected
 * into the emitter. These maps are derived purely from the AST (and the
 * register allocation, for near_any cost stats) and are never mutated during
 * lowering, so isolating them keeps the mutable emission state separate from
 * the immutable facts the lowering passes consult.
 */
export interface ProgramAnalysis {
  readonly procCallCounts: Map<string, number>;
  readonly inlineProcNames: Set<string>;
  readonly functionProcs: Map<string, ProcAst>;
  readonly xParamProcs: Map<string, XParamProcLowering>;
  readonly readCounts: Map<string, number>;
  readonly displayUseCounts: Map<string, number>;
  readonly showSequenceUseCounts: Map<string, number>;
  readonly expressionUseCounts: Map<string, { count: number; expr: ExpressionAst }>;
  readonly nearAnyHelperStats: Map<string, NearAnyHelperStats>;
  readonly lineCountCallCount: number;
  readonly lineCountGroupCounts: Map<string, number>;
  readonly scaledCoordLists: Set<string>;
  readonly scaledCoordCellNames: Set<string>;
}
