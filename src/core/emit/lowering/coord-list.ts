import { registerIndex } from "../../opcodes.ts";
import type { ExpressionAst, ProgramAst, RegisterName, StatementAst } from "../../types.ts";
import type { LoweringCtx } from "../lowering-ctx.ts";
import type {
  CoordListIndirectContext,
  DashedCoordReportTemplate,
  RandomCoordListPlacement,
} from "../../compiler.ts";
import {
  COORD_LIST_COUNTER,
  COORD_LIST_CURRENT,
  COORD_LIST_DX,
  COORD_LIST_POINTER,
  addExpressions,
  buildDiagnostic,
  coordListHasConditionCall,
  coordListLineCountCall,
  divideExpressions,
  flOpcode,
  intExpression,
  isZeroOriginTenByTenPlacement,
  multiplyExpressions,
  numberExpression,
  sameCoordListCall,
  subtractExpressions,
} from "../../compiler.ts";
import {
  getOpcode,
} from "../../opcodes.ts";
import type {
  ConditionAst,
  StateFieldAst,
} from "../../types.ts";

export function compileRandomCoordListSetup(ctx: LoweringCtx, fields: readonly StateFieldAst[], placement: RandomCoordListPlacement): void {
    const context = ctx.randomCoordListSetupContext(fields);
    if (context === undefined) {
      ctx.diagnostics.push(buildDiagnostic(
        "error",
        "random_unique() coord_list setup needs contiguous list registers plus coord-list scratch registers.",
        fields[0]?.line,
      ));
      return;
    }
    const line = fields[0]?.line;
    const draw = ctx.freshLabel("random_coord_draw");
    const check = ctx.freshLabel("random_coord_check");
    const store = ctx.freshLabel("random_coord_store");
    const seed = fields.at(-1)!;

    ctx.emitOp(0x3b, "К СЧ", "random coord seed", line);
    ctx.emitStore(seed.name, "random coord seed", seed.line, true);
    ctx.emitNumberOrPreload(String(fields.length));
    ctx.emitStore(COORD_LIST_COUNTER, "random coord remaining", line, true);

    ctx.emitLabel(draw);
    ctx.emitRandomCoordListCandidate(placement, seed.name, line);

    ctx.emitNumberOrPreload(String(fields.length));
    ctx.emitRecall(COORD_LIST_COUNTER, "random coord remaining", line);
    ctx.emitOp(0x11, "-", "random coord previous count", line);
    ctx.emitStore(COORD_LIST_DX, "random coord previous count", line, true);
    ctx.emitNumberOrPreload(String(context.pointerStart));
    ctx.emitStore(COORD_LIST_POINTER, "random coord pointer", line, true);
    ctx.emitRecall(COORD_LIST_DX, "random coord previous count", line);
    ctx.emitJump(0x57, "F x!=0", store, "random coord first item", line);

    ctx.emitLabel(check);
    ctx.emitRecall(COORD_LIST_CURRENT, "random coord candidate", line);
    ctx.emitCoordListIndirectRecall(context.pointerRegister, line, "random coord previous");
    ctx.emitOp(0x11, "-", "random coord uniqueness", line);
    ctx.emitJump(0x57, "F x!=0", draw, "random coord collision", line);
    ctx.emitJump(context.previousCounterOpcode, getOpcode(context.previousCounterOpcode).name, check, "random coord previous loop", line);

    ctx.emitLabel(store);
    ctx.emitRecall(COORD_LIST_CURRENT, "random coord candidate", line);
    ctx.emitOp(0xb0 + registerIndex(context.pointerRegister), `К X->П ${context.pointerRegister}`, "random coord store", line, true);
    ctx.emitJump(context.outerCounterOpcode, getOpcode(context.outerCounterOpcode).name, draw, "random coord outer loop", line);
    ctx.optimizations.push({
      name: "setup-coord-list-indirect-random-unique",
      detail: `Generated compact indirect setup for ${fields.length} unique coord_list item(s).`,
    });
}

export function emitRandomCoordListCandidate(ctx: LoweringCtx, 
    placement: RandomCoordListPlacement,
    seedField: string,
    line: number | undefined,
  ): void {
    const cellCount = placement.width * placement.height;
    ctx.emitRecall(seedField, "random coord seed", line);
    ctx.emitNumberOrPreload("37");
    ctx.emitOp(0x12, "*", "random coord next seed", line);
    ctx.emitOp(0x35, "К {x}", "random coord seed fraction", line);
    ctx.emitStore(seedField, "random coord seed", line, true);
    ctx.emitNumberOrPreload(String(cellCount));
    ctx.emitOp(0x12, "*", "random coord scaled seed", line);
    ctx.emitOp(0x34, "К [x]", "random coord flat index", line);
    if (ctx.coordListUsesScaledDecimalStorage(placement.listName) && isZeroOriginTenByTenPlacement(placement)) {
      ctx.emitNumberOrPreload("10");
      ctx.emitOp(0x13, "/", "random coord scaled decimal cell", line);
      ctx.emitStore(COORD_LIST_CURRENT, "random coord scaled decimal cell", line, true);
      return;
    }
    ctx.emitStore(COORD_LIST_CURRENT, "random coord flat index", line, true);
    if (placement.xMin === 0 && placement.yMin === 0 && placement.width === 10 && placement.height === 10) {
      return;
    }

    const flat = { kind: "identifier", name: COORD_LIST_CURRENT } satisfies ExpressionAst;
    const row = intExpression(divideExpressions(flat, numberExpression(placement.width)));
    ctx.compileExpression(row);
    ctx.emitStore(COORD_LIST_DX, "random coord row", line, true);

    const rowId = { kind: "identifier", name: COORD_LIST_DX } satisfies ExpressionAst;
    const x = addExpressions(
      numberExpression(placement.xMin),
      subtractExpressions(flat, multiplyExpressions(numberExpression(placement.width), rowId)),
    );
    const y = addExpressions(numberExpression(placement.yMin), rowId);
    ctx.compileExpression(addExpressions(x, multiplyExpressions(numberExpression(10), y)));
    ctx.emitStore(COORD_LIST_CURRENT, "random coord candidate", line, true);
}

export function compileCoordListLineCountDashedReport(ctx: LoweringCtx, 
    assignment: Extract<StatementAst, { kind: "assign" }>,
    show: Extract<StatementAst, { kind: "show" }>,
  ): boolean {
    const template = ctx.dashedCoordReportTemplateAfterLineCount(assignment, show);
    if (template === undefined) return false;
    if (!ctx.compileCoordListLineCountAssignment(assignment, template)) return false;
    ctx.compileShow(show.display, show.line);
    ctx.optimizations.push({
      name: "coord-list-line-count-dashed-report-fusion",
      detail: `Packed coord_list line_count() directly for dashed report ${show.display} at line ${assignment.line}.`,
    });
    return true;
}

export function compileCoordListLineCountAssignment(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "assign" }>,
    dashedReport?: DashedCoordReportTemplate,
  ): boolean {
    const call = coordListLineCountCall(statement.expr);
    if (call === undefined) return false;
    const context = ctx.coordListIndirectContext(call);
    if (context === undefined) return false;
    const current = ctx.allocation.registers[COORD_LIST_CURRENT];
    if (current === undefined) return false;
    const scaled = ctx.coordListUsesScaledDecimalStorage(call);
    if (scaled && !ctx.scaleCoordListCellInPlace(context.cell, statement.line)) return false;

    ctx.emitCoordListLineCountInitialTotal(statement.target, statement.line, dashedReport);
    ctx.emitCoordListLoopSetup(context, statement.line);

    const start = ctx.freshLabel("coord_list_line_loop");
    const visible = ctx.freshLabel("coord_list_visible");
    const countNext = ctx.freshLabel("coord_list_count_next");
    ctx.emitLabel(start);
    ctx.emitCoordListIndirectRecall(context.pointerRegister, statement.line, "coord_list candidate");
    ctx.emitStore(COORD_LIST_CURRENT, "coord_list current", statement.line);

    if (scaled) {
      ctx.compileScaledCoordListVisibilityTest(context.cell, visible, countNext, statement.line, "coord_list");
    } else {
      ctx.compileCoordOnesDigit({ kind: "identifier", name: COORD_LIST_CURRENT }, statement.line);
      ctx.compileCoordOnesDigit(context.cell, statement.line);
      ctx.emitOp(0x11, "-", "coord_list dx", statement.line);
      ctx.emitJump(0x57, "F x!=0", visible, "coord_list same column", statement.line);

      ctx.compileCoordTensDigit({ kind: "identifier", name: COORD_LIST_CURRENT }, statement.line);
      ctx.compileCoordTensDigit(context.cell, statement.line);
      ctx.emitOp(0x11, "-", "coord_list dy", statement.line);
      ctx.emitJump(0x57, "F x!=0", visible, "coord_list same row", statement.line);
      ctx.emitOp(0x31, "К |x|", "coord_list |dy|", statement.line);
      ctx.emitOp(0x14, "<->", "coord_list dx", statement.line);
      ctx.emitOp(0x31, "К |x|", "coord_list |dx|", statement.line);
      ctx.emitOp(0x11, "-", "coord_list diagonal compare", statement.line);
      ctx.emitJump(0x57, "F x!=0", visible, "coord_list same diagonal", statement.line);
      ctx.emitJump(0x51, "БП", countNext, "coord_list not visible", statement.line);
    }

    ctx.emitLabel(visible);
    if (!ctx.emitIndirectUnitIncrement(statement.target, "coord_list line_count total", statement.line)) {
      ctx.emitRecall(statement.target, "coord_list line_count total", statement.line);
      ctx.emitNumberOrPreload("1");
      ctx.emitOp(0x10, "+", "coord_list add visible", statement.line);
      ctx.emitStore(statement.target, "coord_list line_count total", statement.line);
    }

    ctx.emitLabel(countNext);
    ctx.emitCoordListCounterLoop(context.counterRegister, start, statement.line, "coord_list line_count loop");
    ctx.emitCoordListLineCountResult(statement.target, statement.line, dashedReport);
    ctx.optimizations.push({
      name: scaled ? "coord-list-scaled-line-count" : "coord-list-line-count-indirect-loop",
      detail: scaled
        ? `Lowered coord_list_line_count() through scaled decimal coordinates at line ${statement.line}.`
        : `Lowered coord_list_line_count() through a compact indirect register loop at line ${statement.line}.`,
    });
    if (dashedReport !== undefined) {
      ctx.optimizations.push({
        name: "coord-list-line-count-dashed-report-body",
        detail: `Accumulated ${statement.target} as a packed dashed report body at line ${statement.line}.`,
      });
    }
    return true;
}

export function emitCoordListLineCountInitialTotal(ctx: LoweringCtx, 
    target: string,
    line: number,
    dashedReport?: DashedCoordReportTemplate,
    commentPrefix = "coord_list line_count",
  ): void {
    if (dashedReport === undefined) {
      ctx.emitZero(`${commentPrefix} total`, line);
      ctx.emitStore(target, `${commentPrefix} total`, line);
      return;
    }
    ctx.emitDashedCoordReportCellBody(dashedReport, line, `${commentPrefix} dashed report`);
    ctx.emitStore(target, `${commentPrefix} dashed report body`, line);
}

export function emitCoordListLineCountResult(ctx: LoweringCtx, 
    target: string,
    line: number,
    dashedReport?: DashedCoordReportTemplate,
    commentPrefix = "coord_list line_count",
  ): void {
    ctx.emitRecall(
      target,
      dashedReport === undefined ? `${commentPrefix} result` : `${commentPrefix} dashed report body`,
      line,
    );
    if (dashedReport !== undefined) ctx.currentXDashedCoordReportBody = dashedReport;
}

export function emitDashedCoordReportCellBody(ctx: LoweringCtx, 
    template: DashedCoordReportTemplate,
    line: number,
    commentPrefix: string,
  ): void {
    if (!ctx.xHolds(template.cell.name)) ctx.emitRecall(template.cell.name, `${commentPrefix} cell`, line);
    if (ctx.scaledCoordVariables.has(template.cell.name)) {
      ctx.emitNumber("5");
    } else {
      ctx.emitNumber("4");
    }
    ctx.emitOp(0x15, "F 10^x", `${commentPrefix} cell scale`, line);
    ctx.emitOp(0x12, "*", `${commentPrefix} cell shift`, line);
}

export function compileScaledCoordListVisibilityTest(ctx: LoweringCtx, 
    cell: ExpressionAst,
    visible: string,
    countNext: string,
    line: number,
    commentPrefix: string,
  ): void {
    ctx.compileScaledCoordFraction({ kind: "identifier", name: COORD_LIST_CURRENT }, line, `${commentPrefix} current x`);
    ctx.compileScaledCoordFraction(cell, line, `${commentPrefix} cell x`);
    ctx.emitOp(0x11, "-", `${commentPrefix} dx`, line);
    ctx.emitJump(0x57, "F x!=0", visible, `${commentPrefix} same column`, line);
    ctx.emitNumberOrPreload("10");
    ctx.emitOp(0x12, "*", `${commentPrefix} dx digit`, line);

    ctx.compileScaledCoordInteger({ kind: "identifier", name: COORD_LIST_CURRENT }, line, `${commentPrefix} current y`);
    ctx.compileScaledCoordInteger(cell, line, `${commentPrefix} cell y`);
    ctx.emitOp(0x11, "-", `${commentPrefix} dy`, line);
    ctx.emitJump(0x57, "F x!=0", visible, `${commentPrefix} same row`, line);
    ctx.emitOp(0x31, "К |x|", `${commentPrefix} |dy|`, line);
    ctx.emitOp(0x14, "<->", `${commentPrefix} dx`, line);
    ctx.emitOp(0x31, "К |x|", `${commentPrefix} |dx|`, line);
    ctx.emitOp(0x11, "-", `${commentPrefix} diagonal compare`, line);
    ctx.emitJump(0x57, "F x!=0", visible, `${commentPrefix} same diagonal`, line);
    ctx.emitJump(0x51, "БП", countNext, `${commentPrefix} not visible`, line);
}

export function compileScaledCoordFraction(ctx: LoweringCtx, expr: ExpressionAst, line: number, comment: string): void {
    ctx.compileExpression(expr);
    ctx.emitOp(0x35, "К {x}", comment, line);
}

export function compileScaledCoordInteger(ctx: LoweringCtx, expr: ExpressionAst, line: number, comment: string): void {
    ctx.compileExpression(expr);
    ctx.emitOp(0x34, "К [x]", comment, line);
}

export function compileFusedCoordListScan(ctx: LoweringCtx, statements: StatementAst[], index: number): number {
    const branch = statements[index];
    const next = statements[index + 1];
    if (branch?.kind !== "if" || next === undefined || branch.elseBody !== undefined) return 0;
    if (!ctx.coordListFusedHitBodyAllowed(branch.thenBody)) return 0;

    const hasCall = coordListHasConditionCall(branch.condition);
    const lineCount = ctx.coordListLineCountAssignmentFromStatement(next);
    if (hasCall === undefined || lineCount === undefined) return 0;
    const lineCountCall = coordListLineCountCall(lineCount.expr);
    if (lineCountCall === undefined || !sameCoordListCall(hasCall, lineCountCall)) return 0;

    const context = ctx.coordListIndirectContext(lineCountCall);
    const current = ctx.allocation.registers[COORD_LIST_CURRENT];
    if (context === undefined || current === undefined) return 0;
    const scaled = ctx.coordListUsesScaledDecimalStorage(lineCountCall);

    const target = lineCount.target;
    const line = branch.line;
    const dashedReport = index + 3 === statements.length
      ? ctx.dashedCoordReportTemplateAfterLineCount(lineCount, statements[index + 2])
      : undefined;
    if (scaled && !ctx.scaleCoordListCellInPlace(context.cell, line)) return 0;
    ctx.emitCoordListLineCountInitialTotal(target, line, dashedReport, "coord_list fused");
    ctx.emitCoordListLoopSetup(context, line);

    const start = ctx.freshLabel("coord_list_fused_loop");
    const hit = ctx.freshLabel("coord_list_fused_hit");
    const visible = ctx.freshLabel("coord_list_fused_visible");
    const countNext = ctx.freshLabel("coord_list_fused_next");
    ctx.emitLabel(start);
    ctx.emitCoordListIndirectRecall(context.pointerRegister, line, "coord_list fused candidate");
    ctx.emitStore(COORD_LIST_CURRENT, "coord_list fused current", line);

    ctx.compileExpression(context.cell);
    ctx.emitRecall(COORD_LIST_CURRENT, "coord_list fused current", line);
    ctx.emitOp(0x11, "-", "coord_list fused hit compare", line);
    ctx.emitJump(0x57, "F x!=0", hit, "coord_list fused hit", line);

    if (scaled) {
      ctx.compileScaledCoordListVisibilityTest(context.cell, visible, countNext, line, "coord_list fused");
    } else {
      ctx.compileCoordOnesDigit({ kind: "identifier", name: COORD_LIST_CURRENT }, line);
      ctx.compileCoordOnesDigit(context.cell, line);
      ctx.emitOp(0x11, "-", "coord_list fused dx", line);
      ctx.emitJump(0x57, "F x!=0", visible, "coord_list fused same column", line);

      ctx.compileCoordTensDigit({ kind: "identifier", name: COORD_LIST_CURRENT }, line);
      ctx.compileCoordTensDigit(context.cell, line);
      ctx.emitOp(0x11, "-", "coord_list fused dy", line);
      ctx.emitJump(0x57, "F x!=0", visible, "coord_list fused same row", line);
      ctx.emitOp(0x31, "К |x|", "coord_list fused |dy|", line);
      ctx.emitOp(0x14, "<->", "coord_list fused dx", line);
      ctx.emitOp(0x31, "К |x|", "coord_list fused |dx|", line);
      ctx.emitOp(0x11, "-", "coord_list fused diagonal compare", line);
      ctx.emitJump(0x57, "F x!=0", visible, "coord_list fused same diagonal", line);
      ctx.emitJump(0x51, "БП", countNext, "coord_list fused not visible", line);
    }

    ctx.emitLabel(hit);
    ctx.compileStatements(branch.thenBody);
    ctx.emitLabel(visible);
    if (!ctx.emitIndirectUnitIncrement(target, "coord_list fused total", line)) {
      ctx.emitRecall(target, "coord_list fused total", line);
      ctx.emitNumberOrPreload("1");
      ctx.emitOp(0x10, "+", "coord_list fused add visible", line);
      ctx.emitStore(target, "coord_list fused total", line);
    }

    ctx.emitLabel(countNext);
    ctx.emitCoordListCounterLoop(context.counterRegister, start, line, "coord_list fused loop");
    ctx.emitCoordListLineCountResult(target, line, dashedReport, "coord_list fused");
    ctx.optimizations.push({
      name: scaled ? "coord-list-scaled-fused-hit-line-count" : "coord-list-fused-hit-line-count",
      detail: scaled
        ? `Fused coord_list membership and line_count through scaled decimal coordinates at line ${branch.line}.`
        : `Fused coord_list membership and line_count into one indirect scan at line ${branch.line}.`,
    });
    if (dashedReport !== undefined) {
      ctx.optimizations.push({
        name: "coord-list-fused-dashed-report-body",
        detail: `Accumulated ${target} as a packed dashed report body during the fused scan at line ${branch.line}.`,
      });
    }
    return 2;
}

export function compileCoordListHasCondition(ctx: LoweringCtx, 
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): boolean {
    const call = coordListHasConditionCall(condition);
    if (call === undefined) return false;
    const context = ctx.coordListIndirectContext(call);
    if (context === undefined) return false;
    const scaled = ctx.coordListUsesScaledDecimalStorage(call);
    if (scaled && !ctx.scaleCoordListCellInPlace(context.cell, line)) return false;

    const trueLabel = ctx.freshLabel("coord_list_hit");
    ctx.emitCoordListLoopSetup(context, line);
    const start = ctx.freshLabel("coord_list_has_loop");
    ctx.emitLabel(start);
    ctx.compileExpression(context.cell);
    ctx.emitCoordListIndirectRecall(context.pointerRegister, line, "coord_list candidate");
    ctx.emitOp(0x11, "-", "coord_list hit compare", line);
    ctx.emitJump(0x57, "F x!=0", trueLabel, "coord_list hit", line);
    ctx.emitCoordListCounterLoop(context.counterRegister, start, line, "coord_list has loop");
    ctx.emitJump(0x51, "БП", falseLabel, "coord_list miss", line);
    ctx.emitLabel(trueLabel);
    ctx.optimizations.push({
      name: scaled ? "coord-list-scaled-membership" : "coord-list-indirect-membership",
      detail: scaled
        ? `Lowered coord_list membership through scaled decimal coordinates at line ${line}.`
        : `Lowered coord_list membership through an indirect register walk at line ${line}.`,
    });
    return true;
}

export function emitCoordListLoopSetup(ctx: LoweringCtx, context: CoordListIndirectContext, line: number): void {
    ctx.emitNumberOrPreload(String(context.pointerStart));
    ctx.emitStore(COORD_LIST_POINTER, "coord_list pointer", line);
    ctx.emitNumberOrPreload(String(context.count));
    ctx.emitStore(COORD_LIST_COUNTER, "coord_list counter", line);
}

export function emitCoordListIndirectRecall(ctx: LoweringCtx, 
    pointerRegister: RegisterName,
    line: number | undefined,
    comment: string,
  ): void {
    ctx.emitOp(0xd0 + registerIndex(pointerRegister), `К П->X ${pointerRegister}`, comment, line);
}

export function emitCoordListCounterLoop(ctx: LoweringCtx, 
    counterRegister: RegisterName,
    target: string,
    line: number,
    comment: string,
  ): void {
    const opcode = flOpcode(counterRegister);
    if (opcode !== undefined) {
      ctx.emitJump(opcode, getOpcode(opcode).name, target, comment, line);
      ctx.coordListCounterKnownOne = true;
      return;
    }
    ctx.emitRecall(COORD_LIST_COUNTER, "coord_list counter", line);
    ctx.emitNumberOrPreload("1");
    ctx.emitOp(0x11, "-", "coord_list decrement", line);
    ctx.emitStore(COORD_LIST_COUNTER, "coord_list counter", line);
    ctx.emitRecall(COORD_LIST_COUNTER, "coord_list counter", line);
    ctx.emitJump(0x5e, "F x=0", target, comment, line);
    ctx.coordListCounterKnownOne = false;
}

export function compileCoordOnesDigit(ctx: LoweringCtx, expr: ExpressionAst, line: number): void {
    ctx.compileExpression(expr);
    ctx.emitNumberOrPreload("10");
    ctx.emitOp(0x13, "/", "coord quotient", line);
    ctx.emitOp(0x35, "К {x}", "coord fractional part", line);
    ctx.emitNumberOrPreload("10");
    ctx.emitOp(0x12, "*", "coord ones digit", line);
}

export function compileCoordTensDigit(ctx: LoweringCtx, expr: ExpressionAst, line: number): void {
    ctx.compileExpression(expr);
    ctx.emitNumberOrPreload("10");
    ctx.emitOp(0x13, "/", "coord quotient", line);
    ctx.emitOp(0x34, "К [x]", "coord tens digit", line);
}
