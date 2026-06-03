import type { ExpressionAst, StatementAst } from "../../types.ts";
import type { LoweringCtx } from "../lowering-ctx.ts";
import {
  compileExpression,
} from "./expr.ts";
import {
  emitNegativeZeroThresholdRaw,
} from "./proc-raw-setup.ts";
import type {
  BitMembershipCondition,
  SpatialLineProgression,
} from "../lowering-helpers.ts";
import {
  NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE,
  NEGATIVE_ZERO_DEGREE_SELECTOR_GE,
  addExpressions,
  bitMaskScratchName,
  boardForCellMask,
  cellMaskExpression,
  expressionEquals,
  expressionToIntentText,
  flOpcode,
  isNumericValue,
  isSimpleStackLoad,
  matchBitSetAssignment,
  matchCellHelperCall,
  matchSingleBitMaskOpAssignment,
  numericLiteralValue,
  offsetExpressionAst,
  spatialCountExpression,
  spatialCountMaskScratchName,
  spatialCountScratchNames,
  spatialCountStepScratchName,
  spatialHitScratchName,
  spatialLineProgressions,
  spatialNeighborProgressions,
  grid4MaskScratchName,
} from "../lowering-helpers.ts";
import {
  getOpcode,
} from "../../opcodes.ts";
import type {
  V2BoardAst,
} from "../../types.ts";

export function compileGridCellMaskReuse(ctx: LoweringCtx,
    first: Extract<StatementAst, { kind: "assign" }>,
    second: Extract<StatementAst, { kind: "assign" }>,
  ): boolean {
    const used = matchCellHelperCall(first.expr, ["cell_used", "cell_has"]);
    const mark = matchCellHelperCall(second.expr, ["cell_mark", "cell_set"]);
    if (!used || !mark) return false;
    if (!expressionEquals(used.mask, mark.mask) || !expressionEquals(used.x, mark.x) || !expressionEquals(used.y, mark.y)) {
      return false;
    }
    if (used.mask.kind !== "identifier" || second.target !== used.mask.name) return false;

    const scratch = grid4MaskScratchName(first);
    if (!ctx.allocation.registers[scratch]) return false;

    compileExpression(ctx, cellMaskExpression(used.x, used.y));
    ctx.emitStore(scratch, "grid cell mask scratch", first.line);
    emitCommutativeMaskOpWithScratch(ctx, used.mask, scratch, {
      opcode: 0x37,
      mnemonic: "К ∧",
      opComment: "cell_has with reused mask",
      recallComment: "reuse grid cell mask",
      optimization: "mask-stack-op-reuse",
      detail: `Kept ${scratch} on the stack for cell_has at line ${first.line}.`,
      line: first.line,
    });
    ctx.emitOp(0x35, "К {x}", "cell_has membership fraction", first.line);
    ctx.emitOp(0x32, "К ЗН", "cell_has to 0/1", first.line);
    ctx.emitStore(first.target, `set ${first.target}`, first.line);
    compileExpression(ctx, mark.mask);
    ctx.emitRecall(scratch, "reuse grid cell mask", second.line);
    ctx.emitOp(0x38, "К ∨", "cell_set with reused mask", second.line);
    ctx.emitStore(second.target, `set ${second.target}`, second.line);
    ctx.optimizations.push({
      name: "grid-cell-mask-cse",
      detail: `Computed cell_mask once for adjacent cell_has/cell_set at lines ${first.line}/${second.line}.`,
    });
    return true;
}

export function compileBitSetMaskReuse(ctx: LoweringCtx, 
    first: Extract<StatementAst, { kind: "assign" }>,
    second: Extract<StatementAst, { kind: "assign" }>,
  ): boolean {
    const firstSet = matchBitSetAssignment(first);
    const secondSet = matchBitSetAssignment(second);
    if (firstSet === undefined || secondSet === undefined) return false;
    if (!expressionEquals(firstSet.item, secondSet.item)) return false;

    const scratch = bitMaskScratchName(first);
    if (!ctx.allocation.registers[scratch]) return false;

    compileBitMaskWithQuotientScratch(ctx, firstSet.item, scratch, first.line);
    ctx.emitStore(scratch, "cell bit mask scratch", first.line);
    emitCommutativeMaskOpWithScratch(ctx, firstSet.collection, scratch, {
      opcode: 0x38,
      mnemonic: "К ∨",
      opComment: "bit_set with reused mask",
      recallComment: "reuse cell bit mask",
      optimization: "mask-stack-op-reuse",
      detail: `Kept ${scratch} on the stack for the first bit_set at line ${first.line}.`,
      line: first.line,
    });
    ctx.emitStore(first.target, `set ${first.target}`, first.line);
    compileExpression(ctx, secondSet.collection);
    ctx.emitRecall(scratch, "reuse cell bit mask", second.line);
    ctx.emitOp(0x38, "К ∨", "bit_set with reused mask", second.line);
    ctx.emitStore(second.target, `set ${second.target}`, second.line);
    ctx.optimizations.push({
      name: "bit-set-mask-cse",
      detail: `Computed bit_mask() once for adjacent set updates at lines ${first.line}/${second.line}.`,
    });
    return true;
}

export function compileSingleBitMaskOpAssignment(ctx: LoweringCtx, statement: Extract<StatementAst, { kind: "assign" }>): boolean {
    const match = matchSingleBitMaskOpAssignment(statement);
    if (match === undefined) return false;
    const scratch = bitMaskScratchName(statement);
    if (ctx.allocation.registers[scratch] === undefined) return false;

    compileBitMaskWithQuotientScratch(ctx, match.index, scratch, statement.line, { forceInline: true });
    if (match.negate) ctx.emitOp(0x3a, "К ИНВ", "bit_clear mask complement", statement.line);
    ctx.emitStore(scratch, "single bit op mask scratch", statement.line);
    emitCommutativeMaskOpWithScratch(ctx, match.collection, scratch, {
      opcode: match.opcode,
      mnemonic: match.mnemonic,
      opComment: `${statement.target} bit op`,
      recallComment: "single bit op mask",
      optimization: "mask-stack-op-reuse",
      detail: `Kept ${scratch} on the stack for a single-bit op at line ${statement.line}.`,
      line: statement.line,
    });
    ctx.emitStore(statement.target, `set ${statement.target}`, statement.line);
    ctx.optimizations.push({
      name: "single-bit-mask-op",
      detail: `Built the cell mask in ${scratch} before ${statement.target} ${match.mnemonic} at line ${statement.line}.`,
    });
    return true;
}

export function compileBitMaskWithQuotientScratch(ctx: LoweringCtx, 
    index: ExpressionAst,
    scratch: string,
    line: number | undefined,
    options: { forceInline?: boolean } = {},
  ): void {
    const helperScratch = ctx.sharedBitMaskHelperScratch() ?? scratch;
    if (options.forceInline !== true && ctx.loweringOptions.sharedBitMaskHelperCalls === true) {
      const helper = ctx.ensureSpatialBitMaskHelper(helperScratch, line);
      compileExpression(ctx, index);
      ctx.emitJump(0x53, "ПП", helper.label, "bit_mask helper", line);
      ctx.optimizations.push({
        name: "bit-mask-helper-call",
        detail: `Shared bit_mask(${expressionToIntentText(index)}) through ${helper.label}.`,
      });
      return;
    }
    compileExpression(ctx, index);
    emitBitMaskFromCurrentXWithQuotientScratch(ctx, scratch, line);
    ctx.optimizations.push({
      name: "bit-mask-quotient-reuse",
      detail: `Reused ${expressionToIntentText(index)} / 4 through ${scratch} while building bit_mask().`,
    });
}

export function emitBitMaskFromCurrentXWithQuotientScratch(ctx: LoweringCtx, scratch: string, line: number | undefined): void {
    ctx.emitNumber("4");
    ctx.emitOp(0x13, "/", "bit mask quotient", line);
    ctx.emitStore(scratch, "bit mask quotient", line);
    ctx.emitOp(0x35, "К {x}", "bit mask remainder fraction", line);
    ctx.emitNumber("4");
    ctx.emitOp(0x12, "*", "bit mask remainder scale", line);
    ctx.emitNumber("2");
    ctx.emitOp(0x24, "F x^y", "bit mask power", line);
    ctx.emitNumber("0.5");
    ctx.emitOp(0x10, "+", "bit mask round bias", line);
    ctx.emitOp(0x34, "К [x]", "bit mask round", line);
    ctx.emitRecall(scratch, "bit mask quotient", line);
    ctx.emitOp(0x34, "К [x]", "bit mask digit index", line);
    ctx.emitNumber("1");
    ctx.emitOp(0x10, "+", "bit mask decade index", line);
    ctx.emitOp(0x15, "F 10^x", "bit mask decade", line);
    ctx.emitOp(0x13, "/", "bit mask fractional place", line);
    ctx.emitNumber("8");
    ctx.emitOp(0x10, "+", "bit mask anchor", line);
}

export function emitBitSetWithScratch(ctx: LoweringCtx, 
    membership: BitMembershipCondition,
    set: Extract<StatementAst, { kind: "assign" }>,
    scratch: string,
  ): void {
    emitBitSetCollectionWithScratch(ctx, membership.collection, set, scratch);
}

export function emitBitSetCollectionWithScratch(ctx: LoweringCtx, 
    collection: ExpressionAst,
    set: Extract<StatementAst, { kind: "assign" }>,
    scratch: string,
  ): void {
    compileExpression(ctx, collection);
    ctx.emitRecall(scratch, "reuse cell bit mask", set.line);
    ctx.emitOp(0x38, "К ∨", "bit_set with reused mask", set.line);
    ctx.emitStore(set.target, `set ${set.target}`, set.line);
}

export function emitCommutativeMaskOpWithScratch(ctx: LoweringCtx,
    collection: ExpressionAst,
    scratch: string,
    options: {
      opcode: number;
      mnemonic: string;
      opComment: string;
      recallComment: string;
      optimization: string;
      detail: string;
      line: number | undefined;
    },
  ): void {
    if (ctx.xHolds(scratch) && isSimpleStackLoad(collection)) {
      compileExpression(ctx, collection);
      ctx.optimizations.push({
        name: options.optimization,
        detail: options.detail,
      });
    } else {
      compileExpression(ctx, collection);
      ctx.emitRecall(scratch, options.recallComment, options.line);
    }
    ctx.emitOp(options.opcode, options.mnemonic, options.opComment, options.line);
}

export function compileSpatialCountCall(ctx: LoweringCtx, name: "neighbor_count" | "line_count", expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
    if (expr.args.length !== 2) {
      ctx.diagnostics.push({
        level: "error",
        message: `${name}() expects two arguments, got ${expr.args.length}.`,
      });
      return true;
    }
    if (name === "line_count" && compileSpatialLineCountLoop(ctx, expr)) return true;
    // neighbor_count sums several spatial-hit probes. Each hit-helper call churns
    // the four-deep MK-61 stack, so a stack-held running sum is corrupted; the
    // loop body keeps the partial total in a register instead.
    if (name === "neighbor_count" && compileSpatialNeighborCountLoop(ctx, expr)) return true;
    const expanded = spatialCountExpression(name, expr.args, ctx.ast);
    if (expanded === undefined) return false;
    compileExpression(ctx, expanded);
    ctx.optimizations.push({
      name: "spatial-count-hit-helper",
      detail: `Lowered ${name}() through shared spatial hit helper calls.`,
    });
    return true;
}

export function compileSpatialNeighborCountLoop(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
    const [mask, cell] = expr.args;
    if (mask?.kind !== "identifier" || cell === undefined) return false;
    const board = boardForCellMask(mask, ctx.ast);
    const total = spatialCountScratchNames()[0]!;
    if (ctx.allocation.registers[total] === undefined) return false;

    // Enumerate the concrete neighbour offsets and accumulate each spatial hit
    // in a register. An FL-counter loop is avoided on purpose: the spatial-hit
    // helper churns the four-deep stack, and the post-layout indirect-memory
    // pass relocates the loop counter, so a register-accumulated unroll is the
    // only shape that survives both. The neighbour set is tiny (2 on a line,
    // 8 on a grid), so the unroll stays cheap.
    const offsets: number[] = [];
    for (const progression of spatialNeighborProgressions(board)) {
      const start = numericLiteralValue(progression.startOffset);
      const step = numericLiteralValue(progression.step);
      if (start === undefined || step === undefined) return false;
      for (let i = 0; i < progression.count; i += 1) offsets.push(start + i * step);
    }
    if (offsets.length === 0) return false;

    const helper = ctx.ensureSpatialHitHelper(mask.name, spatialHitScratchName(mask.name));
    offsets.forEach((offset, position) => {
      compileExpression(ctx, offsetExpressionAst(cell, offset));
      ctx.emitJump(0x53, "ПП", helper.label, `spatial hit ${mask.name}`, undefined);
      if (position === 0) {
        ctx.emitStore(total, "neighbor_count total", undefined);
      } else {
        ctx.emitRecall(total, "neighbor_count total", undefined);
        ctx.emitOp(0x10, "+", "neighbor_count add hit", undefined);
        ctx.emitStore(total, "neighbor_count total", undefined);
      }
    });
    ctx.emitRecall(total, "neighbor_count result", undefined);
    ctx.optimizations.push({
      name: "spatial-neighbor-count-unroll",
      detail: `Lowered neighbor_count(${mask.name}, ...) as ${offsets.length} register-accumulated spatial hits.`,
    });
    return true;
}

export function compileSpatialLineCountLoop(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
    const [mask, cell] = expr.args;
    if (mask?.kind !== "identifier" || cell === undefined) return false;
    const board = boardForCellMask(mask, ctx.ast);
    if (board === undefined) return false;
    const scratch = spatialCountScratchNames();
    if (scratch.some((name) => ctx.allocation.registers[name] === undefined)) return false;

    const maskScratch = spatialCountMaskScratchName();
    const useSharedMask = ctx.lineCountCallCount > 1 && ctx.allocation.registers[maskScratch] !== undefined;
    const hitMask = useSharedMask ? maskScratch : mask.name;
    const helper = ctx.sharedLineCountHelper(mask, cell, board, undefined);
    if (helper !== undefined) {
      compileExpression(ctx, mask);
      ctx.emitJump(0x53, "ПП", helper.label, `line_count ${mask.name}`, undefined);
      ctx.optimizations.push({
        name: "spatial-line-count-helper-call",
        detail: `Reused shared line_count helper for ${mask.name}.`,
      });
      return true;
    }

    if (useSharedMask) {
      compileExpression(ctx, mask);
      ctx.emitStore(maskScratch, "line count mask", undefined);
    }
    emitSpatialLineCountLoopBody(ctx, hitMask, cell, board, undefined);
    return true;
}

export function emitSpatialLineCountLoopBody(ctx: LoweringCtx, 
    hitMask: string,
    cell: ExpressionAst,
    board: V2BoardAst,
    sourceLine: number | undefined,
  ): void {
    emitSpatialProgressionCountLoopBody(ctx, 
      hitMask,
      cell,
      spatialLineProgressions(board, cell),
      board.width <= 4 && board.height <= 4,
      sourceLine,
      "line_count",
    );
}

export function emitSpatialProgressionCountLoopBody(ctx: LoweringCtx, 
    hitMask: string,
    cell: ExpressionAst,
    progressions: SpatialLineProgression[],
    useMax: boolean,
    sourceLine: number | undefined,
    operation: "line_count" | "neighbor_count",
  ): void {
    const scratch = spatialCountScratchNames();
    const total = scratch[0]!;
    const line = scratch[1]!;
    const offset = scratch[2]!;
    const counter = scratch[3]!;
    const counterRegister = ctx.allocation.registers[counter];
    const helperTakesCounterInX = counterRegister !== undefined && flOpcode(counterRegister) !== undefined;

    ctx.emitZero(`${operation} total`, sourceLine);
    ctx.emitStore(total, `${operation} total`, sourceLine);
    if (useMax && progressions.length >= 3 && ctx.allocation.registers[spatialCountStepScratchName()] !== undefined) {
      const helper = ctx.ensureSpatialLineProgressionHelper(hitMask, cell, operation, sourceLine);
      for (const progression of progressions) {
        ctx.emitZero(`${operation} current line`, sourceLine);
        ctx.emitStore(line, `${operation} current line`, sourceLine);
        compileExpression(ctx, progression.startOffset);
        ctx.emitStore(offset, `${operation} offset`, sourceLine);
        compileExpression(ctx, progression.step);
        ctx.emitStore(spatialCountStepScratchName(), `${operation} step`, sourceLine);
        ctx.emitNumberOrPreload(String(progression.count));
        ctx.emitStore(counter, `${operation} counter`, sourceLine);
        ctx.emitJump(0x53, "ПП", helper.label, `${operation} line progression`, sourceLine);

        ctx.emitRecall(total, `${operation} total`);
        ctx.emitRecall(line, `${operation} current line`);
        ctx.emitOp(0x36, "К max", `${operation} best line`);
        ctx.emitStore(total, `${operation} total`);
      }
      ctx.emitRecall(total, `${operation} result`);
      ctx.optimizations.push({
        name: "spatial-line-progression-helper-call",
        detail: `Reused shared ${operation} line progression helper for ${hitMask}.`,
      });
      return;
    }
    if (!useMax && progressions.length >= 3) {
      const helper = ctx.ensureSpatialSumLoopHelper(hitMask, cell, operation, sourceLine);
      for (const progression of progressions) {
        compileExpression(ctx, progression.startOffset);
        ctx.emitStore(offset, `${operation} offset`, sourceLine);
        compileExpression(ctx, progression.step);
        ctx.emitStore(line, `${operation} step`, sourceLine);
        if (!isNumericValue(progression.step, progression.count)) {
          ctx.emitNumberOrPreload(String(progression.count));
        }
        if (!helperTakesCounterInX) ctx.emitStore(counter, `${operation} counter`, sourceLine);
        ctx.emitJump(0x53, "ПП", helper.label, `${operation} progression`, sourceLine);
      }
      ctx.optimizations.push({
        name: "spatial-sum-loop-helper-call",
        detail: `Reused shared ${operation} progression helper for ${hitMask}.`,
      });
      return;
    }
    for (const progression of progressions) {
      if (useMax) {
        ctx.emitNumber("0");
        ctx.emitStore(line, `${operation} current line`, sourceLine);
      }
      compileExpression(ctx, progression.startOffset);
      ctx.emitStore(offset, `${operation} offset`, sourceLine);
      ctx.emitNumberOrPreload(String(progression.count));
      ctx.emitStore(counter, `${operation} counter`, sourceLine);

      const start = ctx.freshLabel(`${operation}_loop`);
      ctx.emitLabel(start);
      compileExpression(ctx, addExpressions(cell, { kind: "identifier", name: offset }));
      const helper = ctx.ensureSpatialHitHelper(hitMask, spatialHitScratchName(hitMask));
      ctx.emitJump(0x53, "ПП", helper.label, `spatial hit ${hitMask}`, sourceLine);
      ctx.emitRecall(useMax ? line : total, `${operation} accumulator`);
      ctx.emitOp(0x10, "+", `${operation} add hit`);
      ctx.emitStore(useMax ? line : total, `${operation} accumulator`);

      ctx.emitRecall(offset, `${operation} offset`);
      compileExpression(ctx, progression.step);
      ctx.emitOp(0x10, "+", `${operation} next offset`);
      ctx.emitStore(offset, `${operation} offset`);

      const counterRegister = ctx.allocation.registers[counter];
      const flCounterOpcode = counterRegister === undefined ? undefined : flOpcode(counterRegister);
      if (flCounterOpcode !== undefined) {
        ctx.emitJump(flCounterOpcode, getOpcode(flCounterOpcode).name, start, `${operation} loop`, sourceLine);
        ctx.optimizations.push({
          name: "spatial-count-fl-loop",
          detail: `Used ${getOpcode(flCounterOpcode).name} for ${operation} loop counter.`,
        });
      } else {
        ctx.emitRecall(counter, `${operation} counter`);
        ctx.emitNumber("1");
        ctx.emitOp(0x11, "-", `${operation} decrement`);
        ctx.emitStore(counter, `${operation} counter`);
        ctx.emitRecall(counter, `${operation} counter`);
        ctx.emitJump(0x5e, "F x=0", start, `${operation} loop`, sourceLine);
      }

      if (useMax) {
        ctx.emitRecall(total, `${operation} total`);
        ctx.emitRecall(line, `${operation} current line`);
        ctx.emitOp(0x36, "К max", `${operation} best line`);
        ctx.emitStore(total, `${operation} total`);
      }
    }
    ctx.emitRecall(total, `${operation} result`);
    ctx.optimizations.push({
      name: `spatial-${operation.replace("_", "-")}-loop`,
      detail: `Lowered ${operation}(...) as shared spatial hit loops.`,
    });
}

export function emitSpatialLineProgressionHelperBody(ctx: LoweringCtx, 
    hitMask: string,
    cell: ExpressionAst,
    operation: "line_count" | "neighbor_count",
    sourceLine: number | undefined,
  ): void {
    const scratch = spatialCountScratchNames();
    const line = scratch[1]!;
    const offset = scratch[2]!;
    const counter = scratch[3]!;
    const counterRegister = ctx.allocation.registers[counter];
    const flCounterOpcode = counterRegister === undefined ? undefined : flOpcode(counterRegister);

    const start = ctx.freshLabel(`${operation}_line_loop`);
    ctx.emitLabel(start);
    compileExpression(ctx, addExpressions(cell, { kind: "identifier", name: offset }));
    const helper = ctx.ensureSpatialHitHelper(hitMask, spatialHitScratchName(hitMask));
    ctx.emitJump(0x53, "ПП", helper.label, `spatial hit ${hitMask}`, sourceLine);
    ctx.emitRecall(line, `${operation} line accumulator`);
    ctx.emitOp(0x10, "+", `${operation} add hit`);
    ctx.emitStore(line, `${operation} line accumulator`);

    ctx.emitRecall(offset, `${operation} offset`);
    ctx.emitRecall(spatialCountStepScratchName(), `${operation} step`);
    ctx.emitOp(0x10, "+", `${operation} next offset`);
    ctx.emitStore(offset, `${operation} offset`);

    if (flCounterOpcode !== undefined) {
      ctx.emitJump(flCounterOpcode, getOpcode(flCounterOpcode).name, start, `${operation} line loop`, sourceLine);
      ctx.optimizations.push({
        name: "spatial-count-fl-loop",
        detail: `Used ${getOpcode(flCounterOpcode).name} for ${operation} line progression loop counter.`,
      });
    } else {
      ctx.emitRecall(counter, `${operation} counter`);
      ctx.emitNumber("1");
      ctx.emitOp(0x11, "-", `${operation} decrement`);
      ctx.emitStore(counter, `${operation} counter`);
      ctx.emitRecall(counter, `${operation} counter`);
      ctx.emitJump(0x5e, "F x=0", start, `${operation} line loop`, sourceLine);
    }
    ctx.emitRecall(line, `${operation} current line`);
}

export function emitSpatialSumLoopHelperBody(ctx: LoweringCtx, 
    hitMask: string,
    cell: ExpressionAst,
    operation: "line_count" | "neighbor_count",
    sourceLine: number | undefined,
  ): void {
    const scratch = spatialCountScratchNames();
    const total = scratch[0]!;
    const step = scratch[1]!;
    const offset = scratch[2]!;
    const counter = scratch[3]!;
    const counterRegister = ctx.allocation.registers[counter];
    const flCounterOpcode = counterRegister === undefined ? undefined : flOpcode(counterRegister);

    if (flCounterOpcode !== undefined) {
      ctx.emitStore(counter, `${operation} counter`, sourceLine);
    }

    const start = ctx.freshLabel(`${operation}_loop`);
    ctx.emitLabel(start);
    compileExpression(ctx, addExpressions(cell, { kind: "identifier", name: offset }));
    const hitScratch = spatialHitScratchName(hitMask);
    ctx.emitStore(hitScratch, "spatial hit index", sourceLine);

    ctx.emitRecall(offset, `${operation} offset`);
    ctx.emitRecall(step, `${operation} step`);
    ctx.emitOp(0x10, "+", `${operation} next offset`);
    ctx.emitStore(offset, `${operation} offset`);

    emitInlineSpatialHitFromScratch(ctx, hitMask, hitScratch, sourceLine);
    ctx.emitRecall(total, `${operation} accumulator`);
    ctx.emitOp(0x10, "+", `${operation} add hit`);
    ctx.emitStore(total, `${operation} accumulator`);

    if (flCounterOpcode !== undefined) {
      ctx.emitJump(flCounterOpcode, getOpcode(flCounterOpcode).name, start, `${operation} loop`, sourceLine);
      ctx.optimizations.push({
        name: "spatial-count-fl-loop",
        detail: `Used ${getOpcode(flCounterOpcode).name} for ${operation} loop counter.`,
      });
    } else {
      ctx.emitRecall(counter, `${operation} counter`);
      ctx.emitNumber("1");
      ctx.emitOp(0x11, "-", `${operation} decrement`);
      ctx.emitStore(counter, `${operation} counter`);
      ctx.emitRecall(counter, `${operation} counter`);
      ctx.emitJump(0x5e, "F x=0", start, `${operation} loop`, sourceLine);
    }
}

export function emitInlineSpatialHit(ctx: LoweringCtx, hitMask: string, sourceLine: number | undefined): void {
    const scratch = spatialHitScratchName(hitMask);
    ctx.emitStore(scratch, "spatial hit index", sourceLine);
    emitInlineSpatialHitFromScratch(ctx, hitMask, scratch, sourceLine);
}

export function emitInlineSpatialHitFromScratch(ctx: LoweringCtx, 
    hitMask: string,
    scratch: string,
    sourceLine: number | undefined,
  ): void {
    const helper = ctx.ensureSpatialBitMaskHelper(scratch, sourceLine);
    ctx.emitRecall(scratch, "spatial hit index", sourceLine);
    ctx.emitJump(0x53, "ПП", helper.label, "bit_mask helper", sourceLine);
    ctx.emitRecall(hitMask, "spatial hit mask", sourceLine);
    ctx.emitOp(0x37, "К ∧", "spatial hit test", sourceLine);
    ctx.emitOp(0x35, "К {x}", "spatial hit membership fraction", sourceLine);
    ctx.emitOp(0x32, "К ЗН", "spatial hit to count", sourceLine);
    ctx.optimizations.push({
      name: "spatial-hit-inline",
      detail: `Inlined spatial hit test for ${hitMask} into ${sourceLine === undefined ? "generated loop" : `line ${sourceLine}`}.`,
    });
}

export function compileSpatialHitCall(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
    if (expr.args.length !== 2) {
      ctx.diagnostics.push({
        level: "error",
        message: "__spatial_hit() expects two arguments.",
      });
      return true;
    }
    const [mask, index] = expr.args;
    if (mask?.kind !== "identifier" || index === undefined) return false;
    const scratch = spatialHitScratchName(mask.name);
    if (!ctx.allocation.registers[scratch]) return false;
    const helper = ctx.ensureSpatialHitHelper(mask.name, scratch);
    compileExpression(ctx, index);
    ctx.emitJump(0x53, "ПП", helper.label, `spatial hit ${mask.name}`, helper.line);
    return true;
}

export function compileNegativeZeroDegreeSelectorCall(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
    if (expr.args.length !== 2) {
      ctx.diagnostics.push({
        level: "error",
        message: `${NEGATIVE_ZERO_DEGREE_SELECTOR_GE}() expects two arguments.`,
      });
      return true;
    }
    const register = ctx.allocation.negativeZeroDegree;
    if (register === undefined) {
      ctx.diagnostics.push({
        level: "error",
        message: "Internal: negative-zero threshold selector was emitted without a reserved register.",
      });
      return true;
    }
    const [value, bound] = expr.args;
    if (value === undefined || bound === undefined) return true;

    emitNegativeZeroThresholdRaw(ctx, value, bound, register);
    ctx.emitOp(0x32, "К ЗН", "negative-zero threshold selector");
    ctx.optimizations.push({
      name: "negative-zero-threshold-selector",
      detail: `Used preloaded ${NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE} in R${register} for ${expressionToIntentText(value)} >= ${expressionToIntentText(bound)}.`,
    });
    return true;
}
