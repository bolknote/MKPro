import type { ExpressionAst, RegisterName, StateFieldAst, StatementAst } from "../../types.ts";
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
  SEGMENTED_BITPLANE_COUNTER,
  SEGMENTED_BITPLANE_INDEX,
  SEGMENTED_BITPLANE_SEED,
  SEGMENTED_BITPLANE_SELECTOR,
  addExpressions,
  bitMaskScratchName,
  boardForCellMask,
  cellMaskExpression,
  countExpressionCalls,
  expressionEquals,
  expressionIsDeterministic,
  expressionReferencesIdentifier,
  expressionToIntentText,
  flOpcode,
  isNumericValue,
  isSimpleStackLoad,
  matchBitSetAssignment,
  matchCellHelperCall,
  matchSingleBitMaskOpAssignment,
  numericLiteralValue,
  offsetExpressionAst,
  segmentedBitplaneCollectionInfo,
  type SegmentedBitplaneRandomUniquePlacement,
  spatialCountExpression,
  spatialCountMaskScratchName,
  spatialCountScratchNames,
  spatialCountStepScratchName,
  spatialHitScratchName,
  spatialBitIndexExpressionForBoard,
  spatialLineProgressions,
  spatialNeighborProgressions,
  grid4MaskScratchName,
} from "../lowering-helpers.ts";
import {
  getOpcode,
  registerIndex,
} from "../../opcodes.ts";
import type {
  V2BoardAst,
} from "../../types.ts";

type SegmentedBitplaneUpdateStatement = Extract<StatementAst, { kind: "segmented_bitplane_update" }>;

const SEGMENTED_BITPLANE_SELECTOR_VALUES = ["0", "1", "11", "14"] as const;

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
    const firstMaskSet = matchBitOrMaskAssignment(first);
    const secondMaskSet = matchBitOrMaskAssignment(second);
    if (firstMaskSet !== undefined && secondMaskSet !== undefined) {
      if (!expressionEquals(firstMaskSet.mask, secondMaskSet.mask)) return false;
      if (expressionReferencesIdentifier(firstMaskSet.mask, first.target)) return false;
      if (expressionReferencesIdentifier(secondMaskSet.mask, second.target)) return false;

      const scratch = bitMaskScratchName(first);
      if (!ctx.allocation.registers[scratch]) return false;

      compileExpression(ctx, firstMaskSet.mask);
      ctx.emitStore(scratch, "cell bit mask scratch", first.line);
      emitCommutativeMaskOpWithScratch(ctx, firstMaskSet.collection, scratch, {
        opcode: 0x38,
        mnemonic: "К ∨",
        opComment: "bit_set with reused mask",
        recallComment: "reuse cell bit mask",
        optimization: "mask-stack-op-reuse",
        detail: `Kept ${scratch} on the stack for the first bit_set at line ${first.line}.`,
        line: first.line,
      });
      ctx.emitStore(first.target, `set ${first.target}`, first.line);
      compileExpression(ctx, secondMaskSet.collection);
      ctx.emitRecall(scratch, "reuse cell bit mask", second.line);
      ctx.emitOp(0x38, "К ∨", "bit_set with reused mask", second.line);
      ctx.emitStore(second.target, `set ${second.target}`, second.line);
      ctx.optimizations.push({
        name: "bit-set-mask-cse",
        detail: `Computed a cell mask once for adjacent set updates at lines ${first.line}/${second.line}.`,
      });
      return true;
    }

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

export function compileBitOrMaskSetReuse(ctx: LoweringCtx,
    first: Extract<StatementAst, { kind: "assign" }>,
    second: Extract<StatementAst, { kind: "assign" }>,
  ): boolean {
    const firstSet = matchBitOrMaskAssignment(first);
    const secondSet = matchBitOrMaskAssignment(second);
    if (firstSet === undefined || secondSet === undefined) return false;
    if (!expressionEquals(firstSet.mask, secondSet.mask)) return false;
    if (expressionReferencesIdentifier(firstSet.mask, first.target)) return false;
    if (expressionReferencesIdentifier(secondSet.mask, second.target)) return false;

    const scratch = bitMaskScratchName(first);
    if (!ctx.allocation.registers[scratch]) return false;

    compileExpression(ctx, firstSet.mask);
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
      detail: `Computed a set-mask expression once for adjacent set updates at lines ${first.line}/${second.line}.`,
    });
    return true;
}

function matchBitOrMaskAssignment(
    statement: Extract<StatementAst, { kind: "assign" }>,
  ): { collection: ExpressionAst; mask: ExpressionAst } | undefined {
    const expr = statement.expr;
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== "bit_or" || expr.args.length !== 2) return undefined;
    const left = expr.args[0]!;
    const right = expr.args[1]!;
    if (left.kind === "identifier" && left.name === statement.target) {
      return { collection: left, mask: right };
    }
    if (right.kind === "identifier" && right.name === statement.target) {
      return { collection: right, mask: left };
    }
    return undefined;
}

export function compileSingleBitMaskOpCopyReuse(ctx: LoweringCtx,
    first: Extract<StatementAst, { kind: "assign" }>,
    second: Extract<StatementAst, { kind: "assign" }>,
  ): boolean {
    const lowering = matchFractionalMaskOpThenCopy(first, second);
    if (lowering === undefined) return false;

    compileExpression(ctx, lowering.base);
    ctx.emitStore(second.target, `set ${second.target}`, second.line);
    if (lowering.fractional) ctx.emitOp(0x35, "К {x}", "single bit op copy mask fraction", first.line);
    if (lowering.negate) ctx.emitOp(0x3a, "К ИНВ", "single bit op copy mask complement", first.line);
    compileExpression(ctx, lowering.collection);
    ctx.emitOp(lowering.opcode, lowering.mnemonic, `${first.target} bit op with copied mask`, first.line);
    ctx.emitStore(first.target, `set ${first.target}`, first.line);
    ctx.optimizations.push({
      name: "single-bit-mask-op-copy-reuse",
      detail: `Copied ${expressionToIntentText(lowering.base)} to ${second.target} and reused it for ${first.target} ${lowering.mnemonic} at lines ${first.line}/${second.line}.`,
    });
    return true;
}

interface FractionalMaskOpThenCopy {
    opcode: number;
    mnemonic: string;
    collection: ExpressionAst;
    base: ExpressionAst;
    fractional: boolean;
    negate: boolean;
}

function matchFractionalMaskOpThenCopy(
    first: Extract<StatementAst, { kind: "assign" }>,
    second: Extract<StatementAst, { kind: "assign" }>,
  ): FractionalMaskOpThenCopy | undefined {
    if (first.target === second.target) return undefined;
    const expr = first.expr;
    if (expr.kind !== "call" || expr.args.length !== 2) return undefined;
    const op = bitwiseOpcode(expr.callee);
    if (op === undefined) return undefined;
    const collection = expr.args[0]!;
    if (!expressionEquals(collection, { kind: "identifier", name: first.target })) return undefined;
    const mask = matchFractionalMaskOperand(expr.args[1]!);
    if (mask === undefined) return undefined;
    if (!expressionEquals(mask.base, second.expr)) return undefined;
    if (!expressionIsDeterministic(mask.base) || !expressionIsDeterministic(collection)) return undefined;
    if (expressionReferencesIdentifier(mask.base, first.target)) return undefined;
    if (expressionReferencesIdentifier(collection, second.target)) return undefined;
    return {
      opcode: op[0],
      mnemonic: op[1],
      collection,
      base: mask.base,
      fractional: mask.fractional,
      negate: mask.negate,
    };
}

function bitwiseOpcode(name: string): [number, string] | undefined {
    switch (name.toLowerCase()) {
      case "bit_and": return [0x37, "К ∧"];
      case "bit_or": return [0x38, "К ∨"];
      case "bit_xor": return [0x39, "К ⊕"];
      default: return undefined;
    }
}

function matchFractionalMaskOperand(expr: ExpressionAst): { base: ExpressionAst; fractional: boolean; negate: boolean } | undefined {
    let mask = expr;
    let negate = false;
    if (mask.kind === "call" && mask.callee.toLowerCase() === "bit_not" && mask.args.length === 1) {
      negate = true;
      mask = mask.args[0]!;
    }
    if (mask.kind === "call" && mask.callee.toLowerCase() === "frac" && mask.args.length === 1) {
      return { base: mask.args[0]!, fractional: true, negate };
    }
    return { base: mask, fractional: false, negate };
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
    emitBitMaskIndexLiftIfNeeded(ctx, index, line);
    emitBitMaskFromCurrentXWithQuotientScratch(ctx, scratch, line);
    ctx.optimizations.push({
      name: "bit-mask-quotient-reuse",
      detail: `Reused ${expressionToIntentText(index)} / 4 through ${scratch} while building bit_mask().`,
    });
}

export function emitBitMaskIndexLiftIfNeeded(ctx: LoweringCtx, index: ExpressionAst, line: number | undefined): void {
    if (!bitMaskIndexNeedsDigitEntryLift(index)) return;
    ctx.emitOp(0x0e, "В↑", "bit mask index lift", line);
}

function bitMaskIndexNeedsDigitEntryLift(index: ExpressionAst): boolean {
    return index.kind !== "identifier" && index.kind !== "number";
}

export function emitBitMaskFromCurrentXWithQuotientScratch(
    ctx: LoweringCtx,
    scratch: string,
    line: number | undefined,
  ): void {
    ctx.emitStackNumberOrPreload("4");
    ctx.emitOp(0x13, "/", "bit mask quotient", line);
    ctx.emitStore(scratch, "bit mask quotient", line);
    ctx.emitOp(0x35, "К {x}", "bit mask remainder fraction", line);
    ctx.emitNumber("4");
    ctx.emitOp(0x12, "*", "bit mask remainder scale", line);
    ctx.emitOp(0x0e, "В↑", "bit mask power base lift", line);
    ctx.emitStackNumberOrPreload("2");
    ctx.emitOp(0x24, "F x^y", "bit mask power", line);
    ctx.emitOp(0x0e, "В↑", "bit mask round bias lift", line);
    ctx.emitNumberOrPreload("0.5");
    ctx.emitOp(0x10, "+", "bit mask round bias", line);
    ctx.emitOp(0x34, "К [x]", "bit mask round", line);
    ctx.emitRecall(scratch, "bit mask quotient", line);
    ctx.emitOp(0x34, "К [x]", "bit mask digit index", line);
    if (ctx.allocation.constants["1"] !== undefined && ctx.allocation.constants["10"] === undefined) {
      ctx.emitStackNumberOrPreload("1");
      ctx.emitOp(0x10, "+", "bit mask decade index", line);
      ctx.emitOp(0x15, "F 10^x", "bit mask decade", line);
      ctx.optimizations.push({
        name: "bit-mask-decade-index-preload",
        detail: "Placed fractional-nibble bit masks with a preloaded stack 1 for int(q) + 1 before 10^x.",
      });
    } else {
      ctx.emitOp(0x15, "F 10^x", "bit mask decade", line);
      ctx.emitNumberOrPreload("10");
      ctx.emitOp(0x12, "*", "bit mask next decade", line);
      ctx.optimizations.push({
        name: "bit-mask-decade-scale",
        detail: "Placed fractional-nibble bit masks with 10^int(q) * 10 instead of entering int(q) + 1.",
      });
    }
    ctx.emitOp(0x13, "/", "bit mask fractional place", line);
    ctx.emitOp(0x0e, "В↑", "bit mask anchor lift", line);
    ctx.emitStackNumberOrPreload("8");
    ctx.emitOp(0x10, "+", "bit mask anchor", line);
}

export function emitCompactBitMaskFromCurrentXWithQuotientScratch(
    ctx: LoweringCtx,
    scratch: string,
    line: number | undefined,
  ): void {
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
    ctx.optimizations.push({
      name: "compact-bit-mask-helper-body",
      detail: "Used number-entry stack lift in the shared bit_mask helper instead of explicit В↑ separators.",
    });
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
    if (ctx.emitLoopCarriedPromptValue(set.target, set.line)) return;
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
      compileExpression(ctx, spatialBitIndexExpressionForBoard(board, offsetExpressionAst(cell, offset)));
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
    const segmented = segmentedBitplaneCollectionInfo(ctx.ast, mask.name);
    if (segmented !== undefined) {
      if (
        ctx.loweringOptions.segmentedLineCountScan === true &&
        compileSegmentedBitplaneLineCountScan(ctx, mask.name, cell, undefined)
      ) {
        return true;
      }
      const scratch = spatialCountScratchNames();
      if (scratch.some((name) => ctx.allocation.registers[name] === undefined)) return false;
      if (ctx.allocation.registers[SEGMENTED_BITPLANE_INDEX] === undefined) return false;
      if (segmented.planes.some((plane) => ctx.allocation.registers[plane] === undefined)) return false;
      emitSpatialProgressionCountLoopBody(
        ctx,
        mask.name,
        cell,
        spatialLineProgressions(board, cell),
        false,
        undefined,
        "line_count",
      );
      ctx.optimizations.push({
        name: "segmented-bitplane-line-count-helper",
        detail: `Lowered line_count(${mask.name}, ...) through a shared segmented bitplane progression helper.`,
      });
      return true;
    }
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

function compileSegmentedBitplaneLineCountScan(
    ctx: LoweringCtx,
    collection: string,
    cell: ExpressionAst,
    sourceLine: number | undefined,
  ): boolean {
    const info = segmentedBitplaneCollectionInfo(ctx.ast, collection);
    if (info === undefined) return false;
    if (cell.kind !== "identifier") return false;

    const scratch = spatialCountScratchNames();
    const total = scratch[0]!;
    const counter = scratch[3]!;
    const counterRegister = ctx.allocation.registers[counter];
    const flCounterOpcode = counterRegister === undefined ? undefined : flOpcode(counterRegister);
    if (flCounterOpcode === undefined) return false;
    if (scratch.some((name) => ctx.allocation.registers[name] === undefined)) return false;
    if (ctx.allocation.registers[SEGMENTED_BITPLANE_INDEX] === undefined) return false;
    if (info.planes.some((plane) => ctx.allocation.registers[plane] === undefined)) return false;

    const candidate = { kind: "identifier", name: SEGMENTED_BITPLANE_INDEX } satisfies ExpressionAst;
    const start = ctx.freshLabel("segmented_line_count_scan");
    const visible = ctx.freshLabel("segmented_line_count_visible");
    const next = ctx.freshLabel("segmented_line_count_next");

    ctx.emitZero("line_count scan total", sourceLine);
    ctx.emitStore(total, "line_count scan total", sourceLine);
    ctx.emitNumberOrPreload("100");
    ctx.emitStore(counter, "line_count scan counter", sourceLine);

    ctx.emitLabel(start);
    ctx.emitRecall(counter, "line_count scan counter", sourceLine);
    ctx.emitNumberOrPreload("1");
    ctx.emitOp(0x11, "-", "line_count scan candidate index", sourceLine);
    ctx.emitStore(SEGMENTED_BITPLANE_INDEX, "line_count scan candidate", sourceLine);

    emitSegmentedLineCountOnesDigit(ctx, candidate, sourceLine, "line_count scan candidate x");
    emitSegmentedLineCountOnesDigit(ctx, cell, sourceLine, "line_count scan cell x");
    ctx.emitOp(0x11, "-", "line_count scan dx", sourceLine);
    ctx.emitJump(0x57, "F x!=0", visible, "line_count scan same column", sourceLine);
    ctx.emitNumberOrPreload("10");
    ctx.emitOp(0x12, "*", "line_count scan dx digit", sourceLine);

    emitSegmentedLineCountTensDigit(ctx, candidate, sourceLine, "line_count scan candidate y");
    emitSegmentedLineCountTensDigit(ctx, cell, sourceLine, "line_count scan cell y");
    ctx.emitOp(0x11, "-", "line_count scan dy", sourceLine);
    ctx.emitJump(0x57, "F x!=0", visible, "line_count scan same row", sourceLine);
    ctx.emitOp(0x31, "К |x|", "line_count scan |dy|", sourceLine);
    ctx.emitOp(0x14, "<->", "line_count scan dx", sourceLine);
    ctx.emitOp(0x31, "К |x|", "line_count scan |dx|", sourceLine);
    ctx.emitOp(0x11, "-", "line_count scan diagonal compare", sourceLine);
    ctx.emitJump(0x57, "F x!=0", visible, "line_count scan same diagonal", sourceLine);
    ctx.emitJump(0x51, "БП", next, "line_count scan not visible", sourceLine);

    ctx.emitLabel(visible);
    ctx.emitRecall(SEGMENTED_BITPLANE_INDEX, "line_count scan probe candidate", sourceLine);
    emitSegmentedBitplaneHitHelperBody(ctx, collection, sourceLine);
    ctx.emitRecall(total, "line_count scan total", sourceLine);
    ctx.emitOp(0x10, "+", "line_count scan add hit", sourceLine);
    ctx.emitStore(total, "line_count scan total", sourceLine);

    ctx.emitLabel(next);
    ctx.emitJump(flCounterOpcode, getOpcode(flCounterOpcode).name, start, "line_count scan loop", sourceLine);
    ctx.emitRecall(total, "line_count scan result", sourceLine);
    ctx.optimizations.push({
      name: "segmented-bitplane-line-count-scan",
      detail: `Lowered line_count(${collection}, ...) as one 10x10 segmented bitplane scan.`,
    });
    return true;
}

function emitSegmentedLineCountTensDigit(
    ctx: LoweringCtx,
    expr: ExpressionAst,
    sourceLine: number | undefined,
    comment: string,
  ): void {
    compileExpression(ctx, expr);
    ctx.emitNumberOrPreload("10");
    ctx.emitOp(0x13, "/", `${comment} /10`, sourceLine);
    ctx.emitOp(0x34, "К [x]", comment, sourceLine);
}

function emitSegmentedLineCountOnesDigit(
    ctx: LoweringCtx,
    expr: ExpressionAst,
    sourceLine: number | undefined,
    comment: string,
  ): void {
    compileExpression(ctx, expr);
    ctx.emitNumberOrPreload("10");
    ctx.emitOp(0x13, "/", `${comment} /10`, sourceLine);
    ctx.emitOp(0x35, "К {x}", `${comment} fraction`, sourceLine);
    ctx.emitNumberOrPreload("10");
    ctx.emitOp(0x12, "*", comment, sourceLine);
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
    if (segmentedBitplaneCollectionInfo(ctx.ast, hitMask) !== undefined) {
      emitSegmentedBitplaneHitHelperBody(ctx, hitMask, sourceLine);
      ctx.optimizations.push({
        name: "segmented-bitplane-sum-helper-inline-hit",
        detail: `Inlined segmented bitplane hit inside the ${operation} sum-loop helper for ${hitMask}.`,
      });
    } else {
      emitInlineSpatialHitFromCurrentX(ctx, hitMask, sourceLine);
    }

    ctx.emitRecall(offset, `${operation} offset`);
    ctx.emitRecall(step, `${operation} step`);
    ctx.emitOp(0x10, "+", `${operation} next offset`);
    ctx.emitStore(offset, `${operation} offset`);
    ctx.emitOp(0x14, "X↔Y", `${operation} restore hit count`, sourceLine);
    ctx.emitRecall(total, `${operation} accumulator`);
    ctx.emitOp(0x10, "+", `${operation} add hit`);
    ctx.emitStore(total, `${operation} accumulator`);
    ctx.optimizations.push({
      name: "spatial-sum-hit-stack-restore",
      detail: `Preserved the ${operation} hit count on the stack while advancing the spatial offset.`,
    });

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
    ctx.emitRecall(scratch, "spatial hit index", sourceLine);
    emitInlineSpatialHitFromCurrentX(ctx, hitMask, sourceLine);
}

export function emitInlineSpatialHitFromCurrentX(ctx: LoweringCtx, 
    hitMask: string,
    sourceLine: number | undefined,
  ): void {
    const scratch = ctx.sharedBitMaskHelperScratch() ?? spatialHitScratchName(hitMask);
    const helper = ctx.ensureSpatialBitMaskHelper(scratch, sourceLine);
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

export function compileSegmentedBitplaneUpdateStatement(
    ctx: LoweringCtx,
    statement: SegmentedBitplaneUpdateStatement,
  ): boolean {
    const info = segmentedBitplaneCollectionInfo(ctx.ast, statement.collection);
    if (info === undefined) return false;
    if (ctx.allocation.registers[SEGMENTED_BITPLANE_INDEX] === undefined) return false;
    if (info.planes.some((plane) => ctx.allocation.registers[plane] === undefined)) return false;

    const selector = emitSegmentedBitplaneIndirectSelect(ctx, statement.item, statement.line);
    if (selector !== undefined) {
      ctx.emitRecall(SEGMENTED_BITPLANE_INDEX, "segmented bitplane local index", statement.line);
      emitBitMaskFromCurrentXWithQuotientScratch(ctx, SEGMENTED_BITPLANE_INDEX, statement.line);
      if (statement.op === "-=") ctx.emitOp(0x3a, "К ИНВ", "segmented bitplane clear mask", statement.line);
      ctx.emitOp(0xd0 + registerIndex(selector), `К П->X ${selector}`, "segmented bitplane selected plane", statement.line);
      ctx.emitOp(
        statement.op === "+=" ? 0x38 : 0x37,
        statement.op === "+=" ? "К ∨" : "К ∧",
        statement.op === "+=" ? "segmented bitplane set" : "segmented bitplane clear",
        statement.line,
      );
      ctx.emitOp(0xb0 + registerIndex(selector), `К X->П ${selector}`, "segmented bitplane store selected plane", statement.line);
      ctx.optimizations.push({
        name: "segmented-bitplane-update-indirect",
        detail: `${statement.op === "+=" ? "Set" : "Cleared"} ${statement.collection} with one indirect selected plane at line ${statement.line}.`,
      });
      return true;
    }

    emitSegmentedBitplaneGroupDispatch(ctx, statement.item, statement.line, (group) => {
      emitSegmentedBitplaneLocalMask(ctx, group, statement.line);
      if (statement.op === "-=") ctx.emitOp(0x3a, "К ИНВ", "segmented bitplane clear mask", statement.line);
      ctx.emitStore(SEGMENTED_BITPLANE_INDEX, "segmented bitplane update mask", statement.line);
      ctx.emitRecall(info.planes[group]!, "segmented bitplane update plane", statement.line);
      ctx.emitRecall(SEGMENTED_BITPLANE_INDEX, "segmented bitplane update mask", statement.line);
      ctx.emitOp(
        statement.op === "+=" ? 0x38 : 0x37,
        statement.op === "+=" ? "К ∨" : "К ∧",
        statement.op === "+=" ? "segmented bitplane set" : "segmented bitplane clear",
        statement.line,
      );
      ctx.emitStore(info.planes[group]!, "segmented bitplane update plane", statement.line);
    });
    ctx.optimizations.push({
      name: "segmented-bitplane-update",
      detail: `${statement.op === "+=" ? "Set" : "Cleared"} ${statement.collection} through four 25-cell bitplanes at line ${statement.line}.`,
    });
    return true;
}

export function compileSegmentedBitplaneRandomUniqueSetup(
    ctx: LoweringCtx,
    fields: readonly StateFieldAst[],
    placement: SegmentedBitplaneRandomUniquePlacement,
  ): void {
    const line = fields[0]?.line;
    const info = segmentedBitplaneCollectionInfo(ctx.ast, placement.collection);
    const selector = segmentedBitplaneSelectorRegister(ctx);
    if (
      info === undefined ||
      selector === undefined ||
      ctx.allocation.registers[SEGMENTED_BITPLANE_INDEX] === undefined ||
      ctx.allocation.registers[SEGMENTED_BITPLANE_COUNTER] === undefined ||
      ctx.allocation.registers[SEGMENTED_BITPLANE_SEED] === undefined ||
      ctx.allocation.registers[placement.countSource] === undefined ||
      info.planes.some((plane) => ctx.allocation.registers[plane] === undefined) ||
      fields.length !== info.planes.length
    ) {
      ctx.diagnostics.push({
        level: "error",
        message: `segmented cells random() setup for ${placement.collection} needs four planes, a count source, selector, index, counter, and seed registers.`,
        ...(line === undefined ? {} : { line }),
      });
      return;
    }

    ctx.emitZero("segmented bitplane setup zero", line);
    for (const plane of info.planes) {
      ctx.emitStore(plane, `setup ${plane}`, line, true);
    }

    ctx.emitOp(0x3b, "К СЧ", "segmented bitplane random seed", line);
    ctx.emitStore(SEGMENTED_BITPLANE_SEED, "segmented bitplane random seed", line, true);
    ctx.emitRecall(placement.countSource, "segmented bitplane random count", line);
    ctx.emitStore(SEGMENTED_BITPLANE_COUNTER, "segmented bitplane remaining placements", line, true);

    const draw = ctx.freshLabel("seg_bitplane_random_draw");
    ctx.emitLabel(draw);
    emitSegmentedBitplaneRandomCandidate(ctx, line);

    const selected = emitSegmentedBitplaneIndirectSelect(ctx, { kind: "identifier", name: SEGMENTED_BITPLANE_INDEX }, line);
    if (selected === undefined) {
      ctx.diagnostics.push({
        level: "error",
        message: `segmented cells random() setup for ${placement.collection} could not select a plane indirectly.`,
        ...(line === undefined ? {} : { line }),
      });
      return;
    }

    ctx.emitRecall(SEGMENTED_BITPLANE_INDEX, "segmented bitplane random local index", line);
    emitBitMaskFromCurrentXWithQuotientScratch(ctx, SEGMENTED_BITPLANE_INDEX, line);
    ctx.emitStore(SEGMENTED_BITPLANE_INDEX, "segmented bitplane random mask", line, true);
    ctx.emitOp(0xd0 + registerIndex(selected), `К П->X ${selected}`, "segmented bitplane random selected plane", line);
    ctx.emitRecall(SEGMENTED_BITPLANE_INDEX, "segmented bitplane random mask", line);
    ctx.emitOp(0x37, "К ∧", "segmented bitplane random collision probe", line);
    ctx.emitOp(0x35, "К {x}", "segmented bitplane random collision fraction", line);
    ctx.emitJump(0x5e, "F x=0", draw, "segmented bitplane random collision", line);

    ctx.emitOp(0xd0 + registerIndex(selected), `К П->X ${selected}`, "segmented bitplane random selected plane", line);
    ctx.emitRecall(SEGMENTED_BITPLANE_INDEX, "segmented bitplane random mask", line);
    ctx.emitOp(0x38, "К ∨", "segmented bitplane random set", line);
    ctx.emitOp(0xb0 + registerIndex(selected), `К X->П ${selected}`, "segmented bitplane random store selected plane", line);

    ctx.emitRecall(SEGMENTED_BITPLANE_COUNTER, "segmented bitplane remaining placements", line);
    ctx.emitNumberOrPreload("1");
    ctx.emitOp(0x11, "-", "segmented bitplane decrement remaining placements", line);
    ctx.emitStore(SEGMENTED_BITPLANE_COUNTER, "segmented bitplane remaining placements", line, true);
    ctx.emitJump(0x5e, "F x=0", draw, "segmented bitplane random next placement", line);

    ctx.emitRecall(placement.countSource, "segmented bitplane setup complete count", line);
    ctx.emitOp(0x0b, "/-/", "segmented bitplane setup complete display", line);
    ctx.optimizations.push({
      name: "segmented-bitplane-random-unique",
      detail: `Generated unique random setup for ${placement.collection} through four 25-cell bitplanes.`,
    });
}

function emitSegmentedBitplaneRandomCandidate(ctx: LoweringCtx, line: number | undefined): void {
    ctx.emitRecall(SEGMENTED_BITPLANE_SEED, "segmented bitplane random seed", line);
    ctx.emitNumberOrPreload("37");
    ctx.emitOp(0x12, "*", "segmented bitplane next random seed", line);
    ctx.emitOp(0x35, "К {x}", "segmented bitplane random seed fraction", line);
    ctx.emitStore(SEGMENTED_BITPLANE_SEED, "segmented bitplane random seed", line, true);
    ctx.emitNumberOrPreload("100");
    ctx.emitOp(0x12, "*", "segmented bitplane random scaled seed", line);
    ctx.emitOp(0x34, "К [x]", "segmented bitplane random flat index", line);
    ctx.emitStore(SEGMENTED_BITPLANE_INDEX, "segmented bitplane random candidate", line, true);
}

export function emitSegmentedBitplaneHitUpdateDispatch(
    ctx: LoweringCtx,
    collection: string,
    item: ExpressionAst,
    update: SegmentedBitplaneUpdateStatement,
    falseLabel: string,
    thenLabel: string,
    line: number,
  ): boolean {
    const info = segmentedBitplaneCollectionInfo(ctx.ast, collection);
    if (info === undefined) return false;
    if (update.collection !== collection || !expressionEquals(update.item, item)) return false;
    if (ctx.allocation.registers[SEGMENTED_BITPLANE_INDEX] === undefined) return false;
    if (info.planes.some((plane) => ctx.allocation.registers[plane] === undefined)) return false;

    const selector = emitSegmentedBitplaneIndirectSelect(ctx, item, line);
    if (selector !== undefined) {
      ctx.emitRecall(SEGMENTED_BITPLANE_INDEX, "segmented bitplane local index", line);
      emitBitMaskFromCurrentXWithQuotientScratch(ctx, SEGMENTED_BITPLANE_INDEX, line);
      ctx.emitOp(0xd0 + registerIndex(selector), `К П->X ${selector}`, "segmented bitplane selected plane", line);
      ctx.emitOp(0x37, "К ∧", "segmented bitplane probe", line);
      ctx.emitOp(0x35, "К {x}", "segmented bitplane hit fraction", line);
      ctx.emitJump(0x57, "F x≠0", falseLabel, "false branch for segmented hit", line);
      if (update.op === "-=") {
        ctx.emitOp(0x3a, "К ИНВ", "segmented bitplane clear matched mask", update.line);
        ctx.emitOp(0xd0 + registerIndex(selector), `К П->X ${selector}`, "segmented bitplane clear selected plane", update.line);
        ctx.emitOp(0x37, "К ∧", "segmented bitplane clear matched bit", update.line);
        ctx.emitOp(0xb0 + registerIndex(selector), `К X->П ${selector}`, "segmented bitplane store selected plane", update.line);
      }
      ctx.emitJump(0x51, "БП", thenLabel, "segmented bitplane hit tail", line);
      ctx.optimizations.push({
        name: "segmented-bitplane-hit-update-indirect",
        detail: `Fused ${collection} probe/update through one indirect selected plane at line ${line}.`,
      });
      return true;
    }

    emitSegmentedBitplaneGroupDispatch(ctx, item, line, (group) => {
      const plane = info.planes[group]!;
      emitSegmentedBitplaneLocalMask(ctx, group, line);
      ctx.emitRecall(plane, "segmented bitplane probe plane", line);
      ctx.emitOp(0x37, "К ∧", "segmented bitplane probe", line);
      ctx.emitOp(0x35, "К {x}", "segmented bitplane hit fraction", line);
      ctx.emitJump(0x57, "F x≠0", falseLabel, "false branch for segmented hit", line);
      if (update.op === "-=") {
        ctx.emitOp(0x3a, "К ИНВ", "segmented bitplane clear matched mask", update.line);
        ctx.emitRecall(plane, "segmented bitplane clear plane", update.line);
        ctx.emitOp(0x37, "К ∧", "segmented bitplane clear matched bit", update.line);
        ctx.emitStore(plane, "segmented bitplane clear plane", update.line);
      }
      ctx.emitJump(0x51, "БП", thenLabel, "segmented bitplane hit tail", line);
      return true;
    });
    ctx.optimizations.push({
      name: "segmented-bitplane-hit-update",
      detail: `Fused ${collection} membership probe with ${update.op} update at line ${line}.`,
    });
    return true;
}

function emitSegmentedBitplaneGroupDispatch(
    ctx: LoweringCtx,
    item: ExpressionAst,
    line: number | undefined,
    emitGroup: (group: number) => boolean | void,
  ): void {
    const labels = [0, 1, 2, 3].map((group) => ctx.freshLabel(`seg_bitplane_group_${group}`));
    const end = ctx.freshLabel("seg_bitplane_group_end");
    if (!(item.kind === "identifier" && ctx.xHolds(item.name)) && !ctx.xHoldsExpression(item)) {
      compileExpression(ctx, item);
    }
    ctx.emitStore(SEGMENTED_BITPLANE_INDEX, "segmented bitplane index", line);
    for (let group = 0; group < 3; group += 1) {
      ctx.emitRecall(SEGMENTED_BITPLANE_INDEX, "segmented bitplane index", line);
      ctx.emitNumberOrPreload(String((group + 1) * 25));
      ctx.emitOp(0x11, "-", "segmented bitplane threshold", line);
      ctx.emitJump(0x5c, "F x<0", labels[group]!, "segmented bitplane select", line);
    }
    ctx.emitJump(0x51, "БП", labels[3]!, "segmented bitplane select", line);

    for (let group = 0; group < 4; group += 1) {
      ctx.emitLabel(labels[group]!);
      const exitsGroup = emitGroup(group) === true;
      if (group < 3 && !exitsGroup) ctx.emitJump(0x51, "БП", end, "segmented bitplane group end", line);
    }
    ctx.emitLabel(end);
}

function emitSegmentedBitplaneLocalMask(ctx: LoweringCtx, group: number, line: number | undefined): void {
    ctx.emitRecall(SEGMENTED_BITPLANE_INDEX, "segmented bitplane index", line);
    if (group > 0) {
      ctx.emitNumberOrPreload(String(group * 25));
      ctx.emitOp(0x11, "-", "segmented bitplane local index", line);
    }
    emitBitMaskFromCurrentXWithQuotientScratch(ctx, SEGMENTED_BITPLANE_INDEX, line);
}

function segmentedBitplaneSelectorRegister(ctx: LoweringCtx): RegisterName | undefined {
    const register = ctx.allocation.registers[SEGMENTED_BITPLANE_SELECTOR];
    return register !== undefined && registerIndex(register) >= 7 ? register : undefined;
}

function emitSegmentedBitplaneIndirectSelect(
    ctx: LoweringCtx,
    item: ExpressionAst,
    line: number | undefined,
  ): RegisterName | undefined {
    const selector = segmentedBitplaneSelectorRegister(ctx);
    if (selector === undefined) return undefined;
    if (ctx.allocation.registers[SEGMENTED_BITPLANE_INDEX] === undefined) return undefined;

    const labels = [0, 1, 2, 3].map((group) => ctx.freshLabel(`seg_bitplane_select_${group}`));
    const selected = ctx.freshLabel("seg_bitplane_selected");
    const itemIsStoredIndex = item.kind === "identifier" && item.name === SEGMENTED_BITPLANE_INDEX;
    if (!(item.kind === "identifier" && ctx.xHolds(item.name)) && !ctx.xHoldsExpression(item)) {
      compileExpression(ctx, item);
    }
    if (!itemIsStoredIndex) {
      ctx.emitStore(SEGMENTED_BITPLANE_INDEX, "segmented bitplane index", line);
    }
    for (let group = 0; group < 3; group += 1) {
      ctx.emitRecall(SEGMENTED_BITPLANE_INDEX, "segmented bitplane index", line);
      ctx.emitNumberOrPreload(String((group + 1) * 25));
      ctx.emitOp(0x11, "-", "segmented bitplane threshold", line);
      ctx.emitJump(0x5c, "F x<0", labels[group]!, "segmented bitplane indirect select", line);
    }
    ctx.emitJump(0x51, "БП", labels[3]!, "segmented bitplane indirect select", line);

    for (let group = 0; group < 4; group += 1) {
      ctx.emitLabel(labels[group]!);
      ctx.emitRecall(SEGMENTED_BITPLANE_INDEX, "segmented bitplane index", line);
      if (group > 0) {
        ctx.emitNumberOrPreload(String(group * 25));
        ctx.emitOp(0x11, "-", "segmented bitplane local index", line);
      }
      ctx.emitStore(SEGMENTED_BITPLANE_INDEX, "segmented bitplane local index", line);
      ctx.emitNumberOrPreload(SEGMENTED_BITPLANE_SELECTOR_VALUES[group]!);
      ctx.emitStore(SEGMENTED_BITPLANE_SELECTOR, "segmented bitplane selector", line);
      if (group < 3) ctx.emitJump(0x51, "БП", selected, "segmented bitplane selected", line);
    }
    ctx.emitLabel(selected);
    return selector;
}

export function compileSegmentedBitplaneHitCall(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
    if (expr.args.length !== 2) {
      ctx.diagnostics.push({
        level: "error",
        message: "__seg_bit_has() expects two arguments.",
      });
      return true;
    }
    const [collection, index] = expr.args;
    if (collection?.kind !== "identifier" || index === undefined) return false;
    if (segmentedBitplaneCollectionInfo(ctx.ast, collection.name) === undefined) {
      ctx.diagnostics.push({
        level: "error",
        message: `Collection ${collection.name} is not lowered as segmented bitplanes.`,
      });
      return true;
    }
    if (!(index.kind === "identifier" && ctx.xHolds(index.name)) && !ctx.xHoldsExpression(index)) {
      compileExpression(ctx, index);
    }
    if (emitSegmentedBitplaneHitFromCurrentX(ctx, collection.name, undefined)) return true;
    ctx.diagnostics.push({
      level: "error",
      message: `segmented bitplane hit for ${collection.name} needs four plane registers and ${SEGMENTED_BITPLANE_INDEX}.`,
    });
    return true;
}

export function emitSegmentedBitplaneHitFromCurrentX(
    ctx: LoweringCtx,
    collection: string,
    sourceLine: number | undefined,
  ): boolean {
    const info = segmentedBitplaneCollectionInfo(ctx.ast, collection);
    if (info === undefined) return false;
    if (ctx.allocation.registers[SEGMENTED_BITPLANE_INDEX] === undefined) return false;
    if (info.planes.some((plane) => ctx.allocation.registers[plane] === undefined)) return false;

    const shouldShare =
      countExpressionCalls(ctx.ast, "__seg_bit_has") > 1 ||
      countExpressionCalls(ctx.ast, "line_count") > 0;
    if (!shouldShare) {
      return emitSegmentedBitplaneHitHelperBody(ctx, collection, sourceLine);
    }

    const helper = ctx.ensureSegmentedBitplaneHitHelper(collection, sourceLine);
    ctx.emitJump(0x53, "ПП", helper.label, `segmented bitplane hit ${collection}`, sourceLine);
    ctx.optimizations.push({
      name: "segmented-bitplane-hit-helper-call",
      detail: `Tested ${collection} through a shared 25-cell bitplane selector.`,
    });
    return true;
}

export function emitSegmentedBitplaneHitHelperBody(
    ctx: LoweringCtx,
    collection: string,
    sourceLine: number | undefined,
  ): boolean {
    const info = segmentedBitplaneCollectionInfo(ctx.ast, collection);
    if (info === undefined) return false;
    if (ctx.allocation.registers[SEGMENTED_BITPLANE_INDEX] === undefined) return false;
    if (info.planes.some((plane) => ctx.allocation.registers[plane] === undefined)) return false;

    ctx.emitStore(SEGMENTED_BITPLANE_INDEX, "segmented bitplane index", sourceLine);
    const selector = emitSegmentedBitplaneIndirectSelect(ctx, { kind: "identifier", name: SEGMENTED_BITPLANE_INDEX }, sourceLine);
    if (selector !== undefined) {
      ctx.emitRecall(SEGMENTED_BITPLANE_INDEX, "segmented bitplane local index", sourceLine);
      emitBitMaskFromCurrentXWithQuotientScratch(ctx, SEGMENTED_BITPLANE_INDEX, sourceLine);
      ctx.emitOp(0xd0 + registerIndex(selector), `К П->X ${selector}`, "segmented bitplane selected plane", sourceLine);
      ctx.emitOp(0x37, "К ∧", "segmented bitplane hit test", sourceLine);
      ctx.emitOp(0x35, "К {x}", "segmented bitplane hit fraction", sourceLine);
      ctx.emitOp(0x32, "К ЗН", "segmented bitplane hit to count", sourceLine);
      ctx.optimizations.push({
        name: "segmented-bitplane-hit-indirect-helper",
        detail: `Emitted shared hit helper for ${collection} with one indirect selected plane.`,
      });
      return true;
    }

    const scratch = SEGMENTED_BITPLANE_INDEX;
    const labels = info.planes.map((_plane, index) => ctx.freshLabel(`seg_bitplane_${index}`));
    const end = ctx.freshLabel("seg_bitplane_end");
    ctx.emitStore(scratch, "segmented bitplane index", sourceLine);
    for (let group = 0; group < 3; group += 1) {
      ctx.emitRecall(scratch, "segmented bitplane index", sourceLine);
      ctx.emitNumberOrPreload(String((group + 1) * 25));
      ctx.emitOp(0x11, "-", "segmented bitplane threshold", sourceLine);
      ctx.emitJump(0x5c, "F x<0", labels[group]!, "segmented bitplane select", sourceLine);
    }
    ctx.emitJump(0x51, "БП", labels[3]!, "segmented bitplane select", sourceLine);

    for (let group = 0; group < 4; group += 1) {
      ctx.emitLabel(labels[group]!);
      ctx.emitRecall(scratch, "segmented bitplane index", sourceLine);
      if (group > 0) {
        ctx.emitNumberOrPreload(String(group * 25));
        ctx.emitOp(0x11, "-", "segmented bitplane local index", sourceLine);
      }
      emitBitMaskFromCurrentXWithQuotientScratch(ctx, scratch, sourceLine);
      ctx.emitRecall(info.planes[group]!, "segmented bitplane plane", sourceLine);
      ctx.emitOp(0x37, "К ∧", "segmented bitplane hit test", sourceLine);
      ctx.emitOp(0x35, "К {x}", "segmented bitplane hit fraction", sourceLine);
      ctx.emitOp(0x32, "К ЗН", "segmented bitplane hit to count", sourceLine);
      if (group < 3) ctx.emitJump(0x51, "БП", end, "segmented bitplane hit end", sourceLine);
    }
    ctx.emitLabel(end);
    ctx.optimizations.push({
      name: "segmented-bitplane-hit-helper",
      detail: `Emitted shared hit helper for ${collection} through four 25-cell bitplanes.`,
    });
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
