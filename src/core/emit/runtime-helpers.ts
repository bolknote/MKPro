import type { ExpressionAst, ProgramAst, StatementAst, V2BoardAst } from "../types.ts";

type DisplayAst = ProgramAst["displays"][number];
type SpatialProgressionOperation = "line_count" | "neighbor_count";

interface DisplayHelper {
  display: DisplayAst;
  label: string;
  line: number;
}

interface ShowSequenceHelper {
  first: DisplayAst;
  second: DisplayAst;
  label: string;
  line: number;
}

interface ExpressionHelper {
  expr: ExpressionAst;
  label: string;
  line?: number;
}

interface NearAnyHelper {
  value: ExpressionAst;
  radius: ExpressionAst;
  label: string;
  line?: number;
}

interface LineCountHelper {
  cell: ExpressionAst;
  board: V2BoardAst;
  label: string;
  line?: number;
}

interface SpatialHitHelper {
  mask: string;
  scratch: string;
  label: string;
  line?: number;
}

interface SpatialBitMaskHelper {
  scratch: string;
  label: string;
  line?: number;
}

interface SpatialProgressionHelper {
  hitMask: string;
  cell: ExpressionAst;
  label: string;
  operation: SpatialProgressionOperation;
  line?: number;
}

interface TerminalTailHelper {
  body: StatementAst[];
  label: string;
  line: number;
}

interface Line4MoveHelper {
  bank: string;
  occupied: string;
  cell: string;
  target: string;
  label: string;
  updateLabel: string;
  normLabel: string;
  line?: number;
}

/**
 * Owns the lazily-registered shared runtime-helper tables for one lowering
 * attempt. Lowering registers a helper (returning a stable label) while
 * compiling the body, and the helper bodies are emitted in one trailing pass.
 *
 * This collaborator holds only the helper state (the maps plus the re-entrancy
 * guards used while a helper body is being emitted). The registration policy
 * (`shared*Helper` / cost models) and the emission pass still live in the
 * lowering code, which reads these tables directly.
 */
export class RuntimeHelperRegistry {
  readonly spatialHitHelpers = new Map<string, SpatialHitHelper>();
  readonly displayHelpers = new Map<string, DisplayHelper>();
  readonly displayByteHelpers = new Map<string, DisplayHelper>();
  readonly literalDisplayHelpers = new Map<string, DisplayHelper>();
  readonly showSequenceHelpers = new Map<string, ShowSequenceHelper>();
  readonly expressionHelpers = new Map<string, ExpressionHelper>();
  readonly randomCellHelpers = new Map<string, ExpressionHelper>();
  readonly nearAnyHelpers = new Map<string, NearAnyHelper>();
  readonly lineCountHelpers = new Map<string, LineCountHelper>();
  readonly spatialBitMaskHelpers = new Map<string, SpatialBitMaskHelper>();
  readonly spatialLineProgressionHelpers = new Map<string, SpatialProgressionHelper>();
  readonly spatialSumLoopHelpers = new Map<string, SpatialProgressionHelper>();
  readonly terminalTailHelpers: TerminalTailHelper[] = [];
  readonly line4MoveHelpers = new Map<string, Line4MoveHelper>();

  // True while the body of an expression / random-coordinate helper is being emitted,
  // so the lowering does not recursively route that same expression back
  // through its own helper.
  emittingExpressionHelper = false;
  emittingRandomCellHelper = false;
}
