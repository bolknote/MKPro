import { registerIndex } from "../../opcodes.ts";
import type { ExpressionAst, ProgramAst, RegisterName, StatementAst } from "../../types.ts";
import type { LoweringCtx } from "../lowering-ctx.ts";
import {
  compileCondition,
  compileIf,
} from "./control-flow.ts";
import {
  compileRandomCoordListSetup,
} from "./coord-list.ts";
import {
  compileDisplayByteBuilder,
  compileLiteralDisplayBody,
  compilePackedDisplayBody,
  emitDisplayLiteralProgram,
  emitFirstSpliceDisplayLiteralProgram,
} from "./display.ts";
import {
  compileExpression,
} from "./expr.ts";
import {
  compileBitMaskWithQuotientScratch,
  emitBitMaskFromCurrentXWithQuotientScratch,
  emitSpatialLineCountLoopBody,
  emitSpatialLineProgressionHelperBody,
  emitSpatialSumLoopHelperBody,
} from "./spatial.ts";
import type {
  ExecutableSetupPreload,
  StackUnaryDerivationCall,
  XParamProcLowering,
} from "../lowering-helpers.ts";
import {
  COORD_LIST_DX,
  DASHED_COORD_REPORT_MASK,
  NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE,
  buildDiagnostic,
  conditionCompileCost,
  conditionToText,
  countIdentifierReadsInCondition,
  displayLiteralProgram,
  divideExpressions,
  estimateExpressionCost,
  estimateNegativeZeroThresholdFlowCost,
  exponentTailDisplayLiteralProgram,
  expressionEquals,
  expressionPureForSubstitution,
  expressionReferencesIdentifier,
  expressionToIntentText,
  firstSpliceDisplayLiteralProgram,
  flOpcode,
  formatRawContractDetail,
  isPreincrementIndirectRegister,
  isSimpleStackLoad,
  isUnitDecrementExpression,
  isUnitIncrementExpression,
  matchIntOrFracCall,
  matchNegativeZeroThresholdCondition,
  matchStackUnaryDerivationCall,
  matchXParamReturnDecay,
  numberExpression,
  orderRawInputs,
  parseRawInstruction,
  programUsesDashedCoordReport,
  randomCoordListItemPlacement,
  randomCoordListSetupFields,
  selectCheaperEquivalentCondition,
  signedFirstSpliceDisplayLiteralProgram,
  spatialCountMaskScratchName,
  substituteConditionIdentifier,
} from "../lowering-helpers.ts";
import {
  getOpcode,
} from "../../opcodes.ts";
import type {
  ConditionAst,
  StateFieldAst,
} from "../../types.ts";

export function compileSetupProgramWithPreloads(ctx: LoweringCtx, 
    preloads: readonly ExecutableSetupPreload[],
    fields: readonly StateFieldAst[],
  ): void {
    const dynamicRegisters = new Set(fields.map((field) => ctx.allocation.registers[field.name]).filter(Boolean));
    for (const preload of preloads) {
      if (dynamicRegisters.has(preload.register)) continue;
      if (preload.kind === "display-literal") {
        const program = displayLiteralProgram(preload.value);
        const firstSplice =
          signedFirstSpliceDisplayLiteralProgram(preload.value) ??
          exponentTailDisplayLiteralProgram(preload.value) ??
          firstSpliceDisplayLiteralProgram(preload.value);
        if (firstSplice !== undefined) {
          emitFirstSpliceDisplayLiteralProgram(ctx, firstSplice, preload.register, undefined, `setup R${preload.register}`);
        } else if (program !== undefined && program.kind !== "error") {
          emitDisplayLiteralProgram(ctx, program, undefined, `setup R${preload.register}`);
        } else {
          continue;
        }
      } else {
        ctx.emitNumber(preload.value);
      }
      ctx.emitOp(0x40 + registerIndex(preload.register), `X->П ${preload.register}`, `setup R${preload.register}`, undefined, true);
    }
    for (const field of fields.filter((candidate) => candidate.initialStack === "Y")) {
      ctx.emitOp(0x14, "X↔Y", `setup ${field.name} from stack.Y`, field.line);
      ctx.emitStore(field.name, `setup ${field.name}`, field.line, true);
      ctx.emitOp(0x14, "X↔Y", `restore stack.X after ${field.name}`, field.line);
    }
    for (const field of fields.filter((candidate) => candidate.initialStack === "X")) {
      ctx.emitStore(field.name, `setup ${field.name}`, field.line, true);
    }
    const initializedCoordLists = new Set<string>();
    for (const field of fields) {
      if (field.initial === undefined) continue;
      const coordList = randomCoordListItemPlacement(field.name, field.initial);
      if (coordList !== undefined) {
        if (!initializedCoordLists.has(coordList.listName)) {
          const group = randomCoordListSetupFields(fields, coordList);
          compileRandomCoordListSetup(ctx, group, coordList);
          initializedCoordLists.add(coordList.listName);
        }
        continue;
      }
      compileExpression(ctx, field.initial);
      ctx.emitStore(field.name, `setup ${field.name}`, field.line, true);
    }
    if (programUsesDashedCoordReport(ctx.ast)) {
      const register = ctx.allocation.registers[COORD_LIST_DX];
      const program = displayLiteralProgram(DASHED_COORD_REPORT_MASK);
      if (register !== undefined && program !== undefined && program.kind !== "error") {
        emitDisplayLiteralProgram(ctx, program, undefined, "setup dashed report mask");
        ctx.emitOp(0x40 + registerIndex(register), `X->П ${register}`, "setup dashed report mask", undefined, true);
      }
    }
    if (fields.some((field) => field.initial !== undefined && randomCoordListItemPlacement(field.name, field.initial) !== undefined)) {
      ctx.emitNumber("7");
    }
    ctx.emitOp(0x50, "С/П", "setup complete");
    compileRuntimeHelpers(ctx);
}

export function compileProcedures(ctx: LoweringCtx): void {
    for (const proc of ctx.ast.procs) {
      if (ctx.inlineProcNames.has(proc.name)) continue;
      ctx.emitLabel(proc.name);
      const xParam = ctx.xParamProcs.get(proc.name);
      const xParamDecay = matchXParamReturnDecay(proc);
      if (xParamDecay !== undefined) {
        compileXParamReturnDecayBody(ctx, xParamDecay);
        ctx.emitOp(0x52, "В/О", "x-param decay return", xParamDecay.line);
        ctx.optimizations.push({
          name: "x-param-return-decay",
          detail: `Compiled ${proc.name} to consume ${xParamDecay.param} directly from X.`,
        });
      } else if (xParam !== undefined) {
        compileXParamProcBody(ctx, proc, xParam);
      } else {
        ctx.compileStatements(proc.body);
      }
      if (!ctx.statementsTerminate(proc.body)) {
        ctx.emitOp(0x52, "В/О", "implicit return from proc");
      }
    }
}

function compileXParamReturnDecayBody(
  ctx: LoweringCtx,
  decay: { factor: ExpressionAst; divisor: ExpressionAst; line: number },
): void {
    ctx.emitOp(0x0e, "В↑", "x-param decay keep base", decay.line);
    compileExpression(ctx, decay.factor);
    ctx.emitOp(0x12, "*", "x-param decay scale", decay.line);
    compileExpression(ctx, decay.divisor);
    ctx.emitOp(0x13, "/", "x-param decay divide", decay.line);
    ctx.emitOp(0x34, "К [x]", "x-param decay int", decay.line);
    ctx.emitOp(0x11, "-", "x-param decay subtract", decay.line);
}

export function compileRuntimeHelpers(ctx: LoweringCtx): void {
    for (let index = 0; index < ctx.terminalTailHelpers.length; index += 1) {
      const helper = ctx.terminalTailHelpers[index]!;
      ctx.emitLabel(helper.label);
      ctx.compileStatements(helper.body);
      ctx.optimizations.push({
        name: "local-terminal-tail",
        detail: `Emitted local terminal tail for branch at line ${helper.line}.`,
      });
    }
    for (const helper of ctx.displayHelpers.values()) {
      ctx.emitLabel(helper.label);
      compilePackedDisplayBody(ctx, helper.display, helper.line, false);
      ctx.emitOp(0x52, "В/О", `display ${helper.display.name} return`, helper.line);
      ctx.optimizations.push({
        name: "packed-display-helper",
        detail: `Emitted shared packed display helper for screen ${helper.display.name}.`,
      });
    }
    for (const helper of ctx.displayByteHelpers.values()) {
      ctx.emitLabel(helper.label);
      compileDisplayByteBuilder(ctx, helper.display, helper.line, false);
      ctx.emitOp(0x52, "В/О", `display ${helper.display.name} return`, helper.line);
      ctx.optimizations.push({
        name: "display-byte-helper",
        detail: `Emitted shared display-byte helper for screen ${helper.display.name}.`,
      });
    }
    for (const helper of ctx.literalDisplayHelpers.values()) {
      ctx.emitLabel(helper.label);
      compileLiteralDisplayBody(ctx, helper.display, helper.line);
      ctx.emitOp(0x52, "В/О", `display ${helper.display.name} return`, helper.line);
      ctx.optimizations.push({
        name: "screen-video-literal-helper",
        detail: `Emitted shared literal video helper for screen ${helper.display.name}.`,
      });
    }
    for (const helper of ctx.showSequenceHelpers.values()) {
      ctx.emitLabel(helper.label);
      compilePackedDisplayBody(ctx, helper.first, helper.line, false);
      compilePackedDisplayBody(ctx, helper.second, helper.line, false);
      ctx.emitOp(0x52, "В/О", `show sequence ${helper.first.name}/${helper.second.name} return`, helper.line);
      ctx.optimizations.push({
        name: "show-sequence-helper",
        detail: `Emitted shared helper for show ${helper.first.name}; show ${helper.second.name}; read.`,
      });
    }
    for (const helper of ctx.randomCellHelpers.values()) {
      ctx.emitLabel(helper.label);
      ctx.emittingRandomCellHelper = true;
      try {
        compileExpression(ctx, helper.expr);
      } finally {
        ctx.emittingRandomCellHelper = false;
      }
      ctx.emitOp(0x52, "В/О", "random coordinate helper return", helper.line);
      ctx.optimizations.push({
        name: "random-cell-helper",
        detail: `Emitted shared random cell helper for ${expressionToIntentText(helper.expr)}.`,
      });
    }
    for (const helper of ctx.expressionHelpers.values()) {
      ctx.emitLabel(helper.label);
      ctx.emittingExpressionHelper = true;
      try {
        compileExpression(ctx, helper.expr);
      } finally {
        ctx.emittingExpressionHelper = false;
      }
      ctx.emitOp(0x52, "В/О", "expression helper return", helper.line);
      ctx.optimizations.push({
        name: "expression-helper",
        detail: `Emitted shared helper for ${expressionToIntentText(helper.expr)}.`,
      });
    }
    for (const helper of ctx.nearAnyHelpers.values()) {
      ctx.emitLabel(helper.label);
      compileExpression(ctx, helper.value);
      ctx.emitOp(0x11, "-", "near_any delta", helper.line);
      ctx.emitOp(0x31, "К |x|", "near_any distance", helper.line);
      compileExpression(ctx, helper.radius);
      ctx.emitOp(0x14, "<->", "near_any radius before distance", helper.line);
      ctx.emitOp(0x11, "-", "near_any margin", helper.line);
      ctx.emitOp(0x52, "В/О", "near_any return", helper.line);
      ctx.optimizations.push({
        name: "near-any-helper",
        detail: `Emitted shared near_any helper for ${expressionToIntentText(helper.value)} / ${expressionToIntentText(helper.radius)}.`,
      });
    }
    for (const helper of ctx.lineCountHelpers.values()) {
      ctx.emitLabel(helper.label);
      ctx.emitStore(spatialCountMaskScratchName(), "line count mask", helper.line);
      emitSpatialLineCountLoopBody(ctx, spatialCountMaskScratchName(), helper.cell, helper.board, helper.line);
      ctx.emitOp(0x52, "В/О", "line_count return", helper.line);
      ctx.optimizations.push({
        name: "spatial-line-count-helper",
        detail: `Emitted shared line_count helper for ${helper.board.name}/${expressionToIntentText(helper.cell)}.`,
      });
    }
    for (const helper of ctx.spatialLineProgressionHelpers.values()) {
      ctx.emitLabel(helper.label);
      emitSpatialLineProgressionHelperBody(ctx, helper.hitMask, helper.cell, helper.operation, helper.line);
      ctx.emitOp(0x52, "В/О", `${helper.operation} line progression return`, helper.line);
      ctx.optimizations.push({
        name: "spatial-line-progression-helper",
        detail: `Emitted shared ${helper.operation} line progression helper for ${helper.hitMask}.`,
      });
    }
    for (const helper of ctx.spatialSumLoopHelpers.values()) {
      ctx.emitLabel(helper.label);
      emitSpatialSumLoopHelperBody(ctx, helper.hitMask, helper.cell, helper.operation, helper.line);
      ctx.emitOp(0x52, "В/О", `${helper.operation} progression return`, helper.line);
      ctx.optimizations.push({
        name: "spatial-sum-loop-helper",
        detail: `Emitted shared ${helper.operation} progression helper for ${helper.hitMask}.`,
      });
    }
    for (const helper of ctx.spatialBitMaskHelpers.values()) {
      ctx.emitLabel(helper.label);
      emitBitMaskFromCurrentXWithQuotientScratch(ctx, helper.scratch, helper.line);
      ctx.emitOp(0x52, "В/О", "bit_mask return", helper.line);
      ctx.optimizations.push({
        name: "bit-mask-helper",
        detail: `Emitted shared bit_mask helper using ${helper.scratch} with quotient reuse.`,
      });
    }
    for (const helper of ctx.spatialHitHelpers.values()) {
      ctx.emitLabel(helper.label);
      ctx.emitStore(helper.scratch, "spatial hit index", helper.line);
      // Build the cell mask before recalling the set: constructing the mask
      // churns the four-deep stack, so nothing else may be held while it runs.
      compileBitMaskWithQuotientScratch(ctx, 
        { kind: "identifier", name: helper.scratch },
        helper.scratch,
        helper.line,
        { forceInline: true },
      );
      ctx.emitRecall(helper.mask, "spatial hit mask", helper.line);
      ctx.emitOp(0x37, "К ∧", "spatial hit test", helper.line);
      ctx.emitOp(0x35, "К {x}", "spatial hit membership fraction", helper.line);
      ctx.emitOp(0x32, "К ЗН", "spatial hit to count", helper.line);
      ctx.emitOp(0x52, "В/О", "spatial hit return", helper.line);
    }
}

export function compileInitialState(ctx: LoweringCtx): void {
    if (ctx.ast.v2) {
      const fields = ctx.ast.states.flatMap((state) => state.fields);
      if (fields.some((field) => field.initial !== undefined || field.initialStack !== undefined)) {
        ctx.optimizations.push({
          name: "auto-preload-initial-state",
          detail: "Moved initial state into setup/preload values so official program cells stay focused on turn logic.",
        });
      }
      return;
    }
    for (const state of ctx.ast.states) {
      for (const field of state.fields.filter((candidate) => candidate.initialStack === "Y")) {
        ctx.emitOp(0x14, "X↔Y", `init ${state.name}.${field.name} from stack.Y`, field.line);
        ctx.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
        ctx.emitOp(0x14, "X↔Y", `restore stack.X after ${field.name}`, field.line);
      }
      for (const field of state.fields.filter((candidate) => candidate.initialStack === "X")) {
        ctx.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
      }
      for (const field of state.fields) {
        if (field.initialStack !== undefined) continue;
        if (field.initial === undefined) continue;
        compileExpression(ctx, field.initial);
        ctx.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
      }
      if (state.fields.length > 0) {
        ctx.optimizations.push({
          name: "intent-state-lowering",
          detail: `Lowered state ${state.name} with ${state.fields.length} fields to register-backed values.`,
        });
      }
    }
}

export function compileRepeatedAssignmentValue(ctx: LoweringCtx, statements: StatementAst[], start: number): number {
    const first = statements[start];
    if (first?.kind !== "assign" || !expressionPureForSubstitution(first.expr)) return 0;
    let end = start + 1;
    while (end < statements.length) {
      const candidate = statements[end]!;
      if (candidate.kind !== "assign" || !expressionEquals(candidate.expr, first.expr)) break;
      end += 1;
    }
    const count = end - start;
    if (count <= 1) return 0;
    compileExpression(ctx, first.expr);
    for (let index = start; index < end; index += 1) {
      const assignment = statements[index] as Extract<StatementAst, { kind: "assign" }>;
      ctx.emitStore(assignment.target, `set ${assignment.target}`, assignment.line);
    }
    ctx.optimizations.push({
      name: "repeated-assignment-value-reuse",
      detail: `Stored one computed value into ${count} consecutive assignment targets at line ${first.line}.`,
    });
    return count;
}

export function compileXParamProcCall(ctx: LoweringCtx, 
    assign: Extract<StatementAst, { kind: "assign" }>,
    call: Extract<StatementAst, { kind: "call" }>,
  ): boolean {
    const lowering = ctx.xParamProcs.get(call.block);
    if (lowering === undefined || assign.target !== lowering.param) return false;
    if (!expressionPureForSubstitution(assign.expr)) return false;

    compileExpression(ctx, assign.expr);
    compileBlockCall(ctx, call.block, call.line);
    ctx.optimizations.push({
      name: "x-param-proc-call",
      detail: `Passed ${assign.target} to rule ${call.block} through X at line ${assign.line}.`,
    });
    return true;
}

export function compileXParamProcBody(ctx: LoweringCtx, proc: ProgramAst["procs"][number], lowering: XParamProcLowering): void {
    ctx.emitRecall(lowering.other, `${proc.name} ${lowering.first.target} base`, lowering.first.line);
    ctx.emitOp(0x10, "+", `${proc.name} ${lowering.first.target} from X parameter`, lowering.first.line);
    ctx.emitStore(lowering.first.target, `set ${lowering.first.target}`, lowering.first.line);
    ctx.compileStatements(proc.body.slice(1));
    ctx.optimizations.push({
      name: "x-param-proc-entry",
      detail: `Compiled rule ${proc.name} to consume ${lowering.param} directly from X.`,
    });
}

export function compileStackUnaryDerivedAssignments(ctx: LoweringCtx, statements: StatementAst[], start: number): number {
    const first = statements[start];
    if (first?.kind !== "assign") return 0;
    const firstMatch = matchStackUnaryDerivationCall(first.expr);
    if (firstMatch === undefined) return 0;
    if (!expressionPureForSubstitution(firstMatch.arg)) return 0;

    const derivations: Array<{
      statement: Extract<StatementAst, { kind: "assign" }>;
      call: StackUnaryDerivationCall;
    }> = [];
    const targets = new Set<string>();
    const end = Math.min(statements.length, start + 4);
    for (let index = start; index < end; index += 1) {
      const statement = statements[index]!;
      if (statement.kind !== "assign") break;
      const call = matchStackUnaryDerivationCall(statement.expr);
      if (call === undefined || !expressionEquals(call.arg, firstMatch.arg)) break;
      if (targets.has(statement.target)) break;
      targets.add(statement.target);
      derivations.push({ statement, call });
    }

    if (derivations.length < 3) return 0;
    if ([...targets].some((target) => expressionReferencesIdentifier(firstMatch.arg, target))) return 0;

    const argCost = estimateExpressionCost(firstMatch.arg);
    const normalCost = derivations.length * (argCost + 2);
    const duplicateCost = derivations.length - 1;
    const restoreCost = 1 + (derivations.length - 2) * 2;
    const sharedCost = argCost + duplicateCost + derivations.length * 2 + restoreCost;
    if (sharedCost >= normalCost) return 0;

    compileExpression(ctx, firstMatch.arg);
    for (let copy = 1; copy < derivations.length; copy += 1) {
      ctx.emitOp(0x0e, "В↑", "duplicate operand for Z-stack derived tail", first.line);
    }

    for (let index = 0; index < derivations.length; index += 1) {
      const { statement, call } = derivations[index]!;
      if (index === 1) {
        ctx.emitOp(0x14, "XY", "restore shared operand from Y", statement.line);
      } else if (index > 1) {
        ctx.emitOp(0x25, "F reverse", "rotate shared operand from Z", statement.line);
        ctx.emitOp(0x14, "XY", "restore shared operand from stack", statement.line);
      }
      ctx.emitOp(call.opcode, call.mnemonic, `${call.fn}()`, statement.line);
      ctx.emitStore(statement.target, `set ${statement.target}`, statement.line);
    }

    const functions = derivations.map(({ call }) => `${call.fn}()`).join("/");
    const stackRegisters = derivations.length === 4 ? "X/Y/Z/T" : "X/Y/Z";
    ctx.optimizations.push({
      name: "z-stack-derived-value-reuse",
      detail: `Computed ${expressionToIntentText(firstMatch.arg)} once and derived ${functions} through ${stackRegisters} stack copies.`,
    });
    return derivations.length;
}

export function compileGuardAssignmentSubstitution(ctx: LoweringCtx, 
    assign: Extract<StatementAst, { kind: "assign" }>,
    guarded: Extract<StatementAst, { kind: "if" }>,
  ): boolean {
    const readsInCondition = countIdentifierReadsInCondition(guarded.condition, assign.target);
    if (readsInCondition === 0) return false;
    if ((ctx.readCounts.get(assign.target) ?? 0) !== readsInCondition) return false;
    if (!expressionPureForSubstitution(assign.expr)) return false;
    const substitutedCondition = substituteConditionIdentifier(guarded.condition, assign.target, assign.expr);
    const ordinaryCost = estimateExpressionCost(assign.expr) + 1 + conditionCompileCost(guarded.condition);
    const substitutedCost = conditionCompileCost(substitutedCondition);
    if (substitutedCost + 4 >= ordinaryCost) return false;
    compileIf(ctx, {
      ...guarded,
      condition: substitutedCondition,
    }, guarded.line);
    ctx.optimizations.push({
      name: "single-use-guard-substitution",
      detail: `Substituted ${assign.target} directly into the following condition at lines ${assign.line}/${guarded.line}.`,
    });
    return true;
}

export function compileDecimalFactorialSeries(ctx: LoweringCtx, statement: Extract<StatementAst, { kind: "decimal_series" }>): void {
    const line = statement.line;
    if (statement.digits !== 94 || statement.counterStart !== 65) {
      ctx.diagnostics.push(buildDiagnostic(
        "error",
        `Unsupported ${statement.digits}-digit recurrence with counter ${statement.counterStart}.`,
        line,
      ));
      return;
    }

    ctx.emitOp(0x52, "В/О", "decimal recurrence setup", line);
    ctx.emitOp(0x06, "6", "decimal recurrence setup", line);
    ctx.emitOp(0x05, "5", "decimal recurrence setup", line);
    ctx.emitOp(0x23, "F 1/x", "decimal recurrence setup", line);
    ctx.emitOp(0x40, "хП0", "decimal recurrence setup", line);
    ctx.emitOp(0x0d, "Cx", "decimal recurrence loop entry", line);
    ctx.emitOp(0xb0, "К хП0", "decimal recurrence loop entry", line);
    ctx.emitOp(0x60, "Пх0", "decimal recurrence loop entry", line);
    ctx.emitJump(0x5e, "F x=0", 5, "decimal recurrence loop guard", line);
    ctx.emitOp(0x0f, "F Вx", "decimal recurrence scale", line);
    ctx.emitOp(0x07, "7", "decimal recurrence scale", line);
    ctx.emitOp(0x15, "F 10^x", "decimal recurrence scale", line);
    ctx.emitOp(0x20, "F π", "decimal recurrence scale", line);
    ctx.emitOp(0xde, "К Пхe", "decimal recurrence helper selector", line);
    ctx.emitOp(0x53, "ПП", "decimal recurrence helper call", line);
    ctx.emitFormalAddress(0xe1, "decimal recurrence helper call", line);
    ctx.emitOp(0x01, "1", "decimal recurrence term", line);
    ctx.emitOp(0x10, "+", "decimal recurrence term", line);
    ctx.emitOp(0x4e, "хПe", "decimal recurrence accumulator", line);
    ctx.emitOp(0xde, "К Пхe", "decimal recurrence accumulator", line);
    ctx.emitOp(0x11, "-", "decimal recurrence accumulator", line);
    ctx.emitJump(0x5e, "F x=0", 14, "decimal recurrence carry guard", line);
    ctx.emitOp(0x6e, "Пхe", "decimal recurrence carry", line);
    ctx.emitOp(0x0c, "ВП", "decimal recurrence carry", line);
    ctx.emitOp(0x0b, "/-/", "decimal recurrence carry", line);
    ctx.emitOp(0x02, "2", "decimal recurrence carry", line);
    ctx.emitOp(0x34, "К [x]", "decimal recurrence carry", line);
    ctx.emitOp(0x00, "0", "decimal recurrence reference gap", line);
    ctx.emitOp(0x25, "F ↻", "decimal recurrence carry", line);
    ctx.emitOp(0x10, "+", "decimal recurrence carry", line);
    ctx.emitOp(0x00, "0", "decimal recurrence reference gap", line);
    ctx.emitOp(0x0e, "В↑", "decimal recurrence carry", line);
    ctx.emitOp(0x0f, "F Вx", "decimal recurrence carry", line);
    ctx.emitOp(0x00, "0", "decimal recurrence reference gap", line);
    ctx.emitOp(0x13, "/", "decimal recurrence division", line);
    ctx.emitOp(0x0f, "F Вx", "decimal recurrence division", line);
    ctx.emitOp(0x25, "F ↻", "decimal recurrence division", line);
    ctx.emitOp(0x34, "К [x]", "decimal recurrence division", line);
    ctx.emitOp(0xbe, "К хПe", "decimal recurrence division", line);
    ctx.emitOp(0x12, "×", "decimal recurrence division", line);
    ctx.emitOp(0x11, "-", "decimal recurrence division", line);
    ctx.emitOp(0x06, "6", "decimal recurrence normalization", line);
    ctx.emitOp(0x15, "F 10^x", "decimal recurrence normalization", line);
    ctx.emitOp(0x12, "×", "decimal recurrence normalization", line);
    ctx.emitOp(0x6e, "Пхe", "decimal recurrence accumulator update", line);
    ctx.emitOp(0x0c, "ВП", "decimal recurrence accumulator update", line);
    ctx.emitOp(0x02, "2", "decimal recurrence accumulator update", line);
    ctx.emitOp(0x4e, "хПe", "decimal recurrence accumulator update", line);
    ctx.emitOp(0x10, "+", "decimal recurrence accumulator update", line);
    ctx.emitOp(0x32, "К ЗН", "decimal recurrence accumulator update", line);
    ctx.emitOp(0x11, "-", "decimal recurrence accumulator update", line);
    ctx.emitJump(0x5e, "F x=0", 11, "decimal recurrence next term", line);
    ctx.emitOp(0x6e, "Пхe", "decimal recurrence final mantissa", line);
    ctx.emitOp(0x02, "2", "decimal recurrence final mantissa", line);
    ctx.emitOp(0x05, "5", "decimal recurrence final mantissa", line);
    ctx.emitOp(0x10, "+", "decimal recurrence final mantissa", line);
    ctx.emitOp(0x4e, "хПe", "decimal recurrence final mantissa", line);
    ctx.emitOp(0x01, "1", "decimal recurrence exponent", line);
    ctx.emitOp(0x16, "F e^x", "decimal recurrence exponent", line);
    ctx.emitOp(0x40, "хП0", "decimal recurrence result", line);
    ctx.emitOp(0x50, "С/П", "decimal recurrence stop", line);
    ctx.optimizations.push({
      name: "decimal-factorial-series-lowering",
      detail: `Lowered decimal recurrence to ${statement.digits}-digit MK-61 program.`,
    });
}

export function compileLocalTerminalElseTail(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    if (statement.elseBody === undefined) return false;
    if (ctx.statementsTerminate(statement.thenBody)) return false;
    if (!ctx.statementsTerminate(statement.elseBody)) return false;

    const helper = ctx.ensureTerminalTailHelper(statement.elseBody, line);
    compileCondition(ctx, statement.condition, helper.label, line);
    ctx.compileStatements(statement.thenBody);
    ctx.optimizations.push({
      name: "local-terminal-tail-branch",
      detail: `Branched to a local terminal tail for else path at line ${line}.`,
    });
    return true;
}

export function compileEqualityWithCurrentX(ctx: LoweringCtx, 
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): boolean {
    if (condition.op !== "==" && condition.op !== "!=") return false;
    if (
      condition.right.kind === "identifier" &&
      ctx.xHolds(condition.right.name) &&
      isSimpleStackLoad(condition.left)
    ) {
      compileExpression(ctx, condition.left);
    } else if (
      condition.left.kind === "identifier" &&
      ctx.xHolds(condition.left.name) &&
      isSimpleStackLoad(condition.right)
    ) {
      compileExpression(ctx, condition.right);
    } else {
      return false;
    }
    ctx.emitOp(0x11, "-", "condition compare", line);
    const opcode = condition.op === "==" ? 0x5e : 0x57;
    ctx.emitJump(opcode, getOpcode(opcode).name, falseLabel, `false branch for ${condition.op}`, line);
    ctx.optimizations.push({
      name: "condition-current-x-reuse",
      detail: `Reused the value already in X for equality comparison at line ${line}.`,
    });
    return true;
}

export function compileNegativeZeroThresholdFlow(ctx: LoweringCtx, 
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): boolean {
    const register = ctx.allocation.negativeZeroDegree;
    const threshold = matchNegativeZeroThresholdCondition(condition, ctx.ast);
    if (threshold === undefined) return false;

    const preloadedConstants = new Set(Object.keys(ctx.allocation.constants));
    const selectedCost = estimateNegativeZeroThresholdFlowCost(threshold, preloadedConstants);
    const ordinaryCost = conditionCompileCost(
      selectCheaperEquivalentCondition(condition, ctx.ast, preloadedConstants).condition,
      preloadedConstants,
    );
    if (register === undefined || selectedCost >= ordinaryCost) {
      ctx.candidates.push({
        site: `if@${line}`,
        variant: "negative-zero-threshold-flow",
        steps: selectedCost,
        selected: false,
        reason: selectedCost >= ordinaryCost
          ? `Negative-zero threshold flow estimated at ${selectedCost} cells; ordinary condition was shorter (${ordinaryCost}).`
          : "Negative-zero threshold flow matched, but no compiler-owned negative-zero register was available.",
      });
      return false;
    }

    emitNegativeZeroThresholdRaw(ctx, threshold.value, numberExpression(threshold.bound), register, line);
    const opcode = threshold.truth === "ge" ? 0x57 : 0x5e;
    ctx.emitJump(opcode, getOpcode(opcode).name, falseLabel, `negative-zero false branch for ${condition.op}`, line);
    ctx.optimizations.push({
      name: "negative-zero-threshold-flow",
      detail: `Tested ${conditionToText(condition)} through preloaded ${NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE} in R${register}.`,
    });
    return true;
}

export function emitNegativeZeroThresholdRaw(ctx: LoweringCtx, 
    value: ExpressionAst,
    bound: ExpressionAst,
    register: RegisterName,
    line?: number,
  ): void {
    compileExpression(ctx, divideExpressions(value, bound));
    ctx.emitOp(0x60 + registerIndex(register), `П->X ${register}`, "negative-zero threshold sentinel", line);
    ctx.emitOp(0x14, "X↔Y", "place threshold value above negative-zero sentinel", line);
    ctx.emitOp(0x12, "*", "negative-zero threshold zero-through", line);
    ctx.emitOp(0x0e, "В↑", "normalize negative-zero threshold result", line);
}

export function compileIntFracSharedTail(ctx: LoweringCtx, 
    first: Extract<StatementAst, { kind: "assign" }>,
    second: Extract<StatementAst, { kind: "assign" }>,
  ): boolean {
    const a = matchIntOrFracCall(first.expr);
    const b = matchIntOrFracCall(second.expr);
    if (a === undefined || b === undefined) return false;
    if (a.fn === b.fn) return false;
    if (!expressionEquals(a.arg, b.arg)) return false;
    if (!expressionPureForSubstitution(a.arg)) return false;
    if (first.target === second.target) return false;
    // В↑ + X↔Y add two cells over a single part, so the shared tail only wins
    // when recomputing the operand would cost more than that.
    if (estimateExpressionCost(a.arg) <= 2) return false;

    const intStatement = a.fn === "int" ? first : second;
    const fracStatement = a.fn === "frac" ? first : second;

    compileExpression(ctx, a.arg);
    ctx.emitOp(0x0e, "В↑", "duplicate operand for shared int/frac tail");
    ctx.emitOp(0x34, "К [x]", "int()");
    ctx.emitStore(intStatement.target, `set ${intStatement.target}`, intStatement.line);
    ctx.emitOp(0x14, "XY", "restore saved operand for frac()");
    ctx.emitOp(0x35, "К {x}", "frac()");
    ctx.emitStore(fracStatement.target, `set ${fracStatement.target}`, fracStatement.line);
    ctx.optimizations.push({
      name: "int-frac-shared-tail",
      detail: `Computed ${expressionToIntentText(a.arg)} once and derived int()/frac() through a shared В↑/X↔Y tail.`,
    });
    return true;
}

export function compileUnitDecrement(ctx: LoweringCtx, statement: Extract<StatementAst, { kind: "assign" }>): boolean {
    if (!isUnitDecrementExpression(statement.target, statement.expr)) return false;
    const register = ctx.allocation.registers[statement.target];
    if (register === undefined) return false;
    const opcode = flOpcode(register);
    if (opcode === undefined) return false;
    const after = ctx.freshLabel("fl_decrement_done");
    const preservedXVariable = ctx.currentXVariable === statement.target ? undefined : ctx.currentXVariable;
    const preservedXKnownZero = ctx.currentXKnownZero;
    ctx.emitJump(opcode, getOpcode(opcode).name, after, `decrement ${statement.target}`, statement.line);
    ctx.currentXVariable = preservedXVariable;
    ctx.currentXAliases = preservedXVariable !== undefined ? new Set([preservedXVariable]) : new Set();
    ctx.currentXKnownZero = preservedXKnownZero;
    ctx.machineEntryOpen = false;
    ctx.emitLabel(after);
    ctx.optimizations.push({
      name: "fl-unit-decrement",
      detail: `Lowered ${statement.target} -= 1 through ${getOpcode(opcode).name}.`,
    });
    return true;
}

export function compileUnitIncrement(ctx: LoweringCtx, statement: Extract<StatementAst, { kind: "assign" }>): boolean {
    if (!isUnitIncrementExpression(statement.target, statement.expr)) return false;
    if (!targetRangeFitsIndirectIncrement(ctx, statement.target)) return false;
    return emitIndirectUnitIncrement(ctx, statement.target, `increment ${statement.target}`, statement.line);
}

export function emitIndirectUnitIncrement(ctx: LoweringCtx, target: string, comment: string, line: number): boolean {
    const register = ctx.allocation.registers[target];
    if (register === undefined || !isPreincrementIndirectRegister(register)) return false;
    ctx.emitOp(0xd0 + registerIndex(register), `К П->X ${register}`, comment, line);
    ctx.currentXVariable = undefined;
    ctx.currentXAliases.clear();
    ctx.currentXKnownZero = false;
    ctx.optimizations.push({
      name: "indirect-incdec-counter",
      detail: `Incremented ${target} by using ${getOpcode(0xd0 + registerIndex(register)).name}'s pre-increment side effect at line ${line}.`,
    });
    return true;
}

function targetRangeFitsIndirectIncrement(ctx: LoweringCtx, target: string): boolean {
    const field = ctx.findStateField(target);
    if (field?.min === undefined || field.max === undefined) return false;
    return field.type === "range" && field.min >= 0 && field.max + 1 <= 14;
}

export function emitErrorStopOpcode(ctx: LoweringCtx, comment: string, line: number, raw = false): void {
    ctx.emitOp(0x2b, "error 2B", comment, line, raw);
}

export function compileBlockCall(ctx: LoweringCtx, blockName: string, line: number): void {
    const proc = ctx.ast.procs.find((candidate) => candidate.name === blockName);
    if (proc) {
      if (ctx.inlineProcNames.has(proc.name)) {
        ctx.compileStatements(proc.body);
        const uses = ctx.procCallCounts.get(proc.name) ?? 0;
        ctx.optimizations.push({
          name: uses === 1 ? "single-use-rule-inline" : "size-model-rule-inline",
          detail: uses === 1
            ? `Inlined single-use rule ${proc.name} at line ${line}.`
            : `Inlined ${uses}-use rule ${proc.name} because it is smaller than a ПП/В/О subroutine.`,
        });
        return;
      }
      // A value-returning function ends in В/О (it returns to the caller), so it
      // must be reached with ПП even though its body "terminates"; only true
      // terminal rules (halt/jump-away) may use the direct-jump tail call.
      if (ctx.statementsTerminate(proc.body) && !ctx.functionProcs.has(proc.name)) {
        ctx.emitJump(0x51, "БП", proc.name, `terminal rule ${proc.name}`, line);
        ctx.optimizations.push({
          name: "terminal-rule-tail-call",
          detail: `Compiled terminal rule ${proc.name} as a direct jump instead of a subroutine call.`,
        });
        return;
      }
      const bankSelectors = ctx.snapshotBankSelectorCache();
      ctx.emitJump(0x53, "ПП", proc.name, `proc call ${proc.name}`, line);
      ctx.restoreBankSelectorCacheAfterCall(proc.name, bankSelectors);
      const returnX = ctx.procReturnXVariable(proc);
      if (returnX !== undefined) {
        ctx.currentXVariable = returnX;
        ctx.currentXAliases = new Set([returnX]);
        ctx.currentXKnownZero = false;
      }
      ctx.optimizations.push({
        name: "proc-call-lowering",
        detail: `Compiled call to rule ${proc.name} as ПП/В/О subroutine.`,
      });
      if (returnX !== undefined) {
        ctx.optimizations.push({
          name: "proc-return-x-reuse",
          detail: `Tracked ${returnX} in X after returning from rule ${proc.name}.`,
        });
      }
      return;
    }
    ctx.diagnostics.push(buildDiagnostic("error", `Unknown block '${blockName}'.`, line));
}

export function compileRawStatement(ctx: LoweringCtx, statement: Extract<StatementAst, { kind: "core" }>): void {
    const inputs = statement.inputs ?? [];
    const outputs = statement.outputs ?? [];

    for (const input of orderRawInputs(inputs)) {
      compileExpression(ctx, input.expr);
    }

    compileRawLines(ctx, statement.lines, statement.strict ?? false);

    for (const output of outputs) {
      ctx.emitStore(output.target, `raw returns ${output.slot}`, output.line);
    }

    if (
      inputs.length > 0 ||
      outputs.length > 0 ||
      statement.clobbers !== undefined ||
      statement.preserves !== undefined
    ) {
      ctx.optimizations.push({
        name: "raw-block-contract",
        detail: formatRawContractDetail(statement),
      });
    }
}

export function compileRawLines(ctx: LoweringCtx, 
    lines: Array<{ text: string; line: number }>,
    strict = false,
  ): void {
    for (const line of lines) {
      if (line.text.endsWith(":")) {
        ctx.emitLabel(line.text.slice(0, -1));
        continue;
      }
      const parsed = parseRawInstruction(line.text);
      if (!parsed) {
        ctx.diagnostics.push({
          level: strict ? "error" : "warning",
          message: `Unknown raw instruction '${line.text}'`,
          line: line.line,
        });
        continue;
      }
      ctx.emitOp(parsed.opcode, parsed.mnemonic, parsed.comment, line.line, true);
      if (parsed.formalTargetOpcode !== undefined) {
        ctx.emitFormalAddress(parsed.formalTargetOpcode, parsed.comment ?? parsed.mnemonic, line.line);
      } else if (parsed.target !== undefined) {
        ctx.emitAddress(parsed.target, parsed.comment ?? parsed.mnemonic, line.line);
      }
    }
}
