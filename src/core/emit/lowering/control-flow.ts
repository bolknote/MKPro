import { registerIndex } from "../../opcodes.ts";
import type { ExpressionAst, StatementAst } from "../../types.ts";
import type { LoweringCtx } from "../lowering-ctx.ts";
import {
  compileCoordListHasCondition,
} from "./coord-list.ts";
import {
  compileLiteralDisplayBody,
} from "./display.ts";
import {
  compileExpression,
} from "./expr.ts";
import {
  compileEqualityWithCurrentX,
  compileLocalTerminalElseTail,
  compileNegativeZeroThresholdFlow,
  emitErrorStopOpcode,
} from "./proc-raw-setup.ts";
import {
  compileBitMaskWithQuotientScratch,
  emitBitSetCollectionWithScratch,
  emitBitSetWithScratch,
} from "./spatial.ts";
import type {
  BitMembershipCondition,
  NearAnyHelperConditionMatch,
} from "../lowering-helpers.ts";
import {
  COORD_LIST_COUNTER,
  DISPATCH_SCRATCH_PREFIX,
  bitMaskScratchName,
  buildBranchRemovalCandidate,
  buildDiagnostic,
  buildDoubleClampCandidate,
  buildGuardedUpdateSelectorCandidate,
  canTestAgainstZeroDirectly,
  conditionCompileCost,
  conditionToText,
  decrementBranchTestsZero,
  directTestOpcode,
  dispatchExpressionRegister,
  dispatchUsesNumericResidualChain,
  displayLiteralProgram,
  estimateConditionCost,
  estimateExpressionCost,
  estimateGuardedUpdateSelectorCost,
  estimateNumberCost,
  estimateOrdinaryGuardedUpdateCost,
  estimateOrdinaryIfCost,
  estimateSmallSetConditionCost,
  expressionEquals,
  expressionIsDeterministic,
  expressionToIntentText,
  flOpcode,
  ifSelectorScratchName,
  invertCondition,
  isBitClearAssignment,
  isUnitDecrementExpression,
  isZeroExpression,
  maskedGuardedUpdateExpression,
  matchBitAbsenceCondition,
  matchBitMembershipCondition,
  matchEqualityConstantCondition,
  matchNearAnyHelperCondition,
  matchRemainderByConstant,
  matchResidualGuardedUpdate,
  matchSmallSetCondition,
  nearAnyHelperKey,
  numericLiteralValue,
  optimizeDispatchDefaultCases,
  programHasLineCountForMask,
  residualAdjustmentCost,
  selectCheaperEquivalentCondition,
  selectDispatchCandidate,
  spatialHitScratchName,
  statementListsEqual,
} from "../lowering-helpers.ts";
import {
  getOpcode,
} from "../../opcodes.ts";
import type {
  ConditionAst,
  ProgramAst,
} from "../../types.ts";

export function compileLiteralShowHalt(ctx: LoweringCtx, 
    show: Extract<StatementAst, { kind: "show" }>,
    halt: Extract<StatementAst, { kind: "halt" }>,
  ): boolean {
    if (halt.literal !== undefined || !isZeroExpression(halt.expr)) return false;
    const display = ctx.ast.displays.find((candidate) => candidate.name === show.display);
    if (display === undefined) return false;
    const literal = ctx.collapseLiteralOnlyDisplay(display);
    if (literal === undefined || displayLiteralProgram(literal)?.kind !== "error") return false;
    compileLiteralHalt(ctx, literal, show.line);
    ctx.optimizations.push({
      name: "terminal-display-fusion",
      detail: `Folded literal screen ${show.display} followed by halt at line ${halt.line} into a terminal error stop.`,
    });
    return true;
}

export function compileDecrementZeroBranch(ctx: LoweringCtx, 
    decrement: Extract<StatementAst, { kind: "assign" }>,
    branch: Extract<StatementAst, { kind: "if" }>,
  ): boolean {
    if (!isUnitDecrementExpression(decrement.target, decrement.expr)) return false;
    if (!decrementBranchTestsZero(branch.condition, decrement.target)) return false;
    const field = ctx.findStateField(decrement.target);
    if ((field?.min ?? Number.NEGATIVE_INFINITY) < 1) return false;
    const register = ctx.allocation.registers[decrement.target];
    if (register === undefined) return false;
    const opcode = flOpcode(register);
    if (opcode === undefined) return false;

    const nonZeroLabel = ctx.freshLabel("decrement_nonzero");
    const thenTerminates = ctx.statementsTerminate(branch.thenBody);
    const endLabel = branch.elseBody !== undefined && !thenTerminates ? ctx.freshLabel("if_end") : undefined;

    ctx.emitJump(opcode, getOpcode(opcode).name, nonZeroLabel, `decrement/test ${decrement.target}`, decrement.line);
    ctx.compileStatements(branch.thenBody);
    if (branch.elseBody !== undefined) {
      if (endLabel !== undefined) ctx.emitJump(0x51, "БП", endLabel, "if end", branch.line);
      ctx.emitLabel(nonZeroLabel);
      ctx.compileStatements(branch.elseBody);
      if (endLabel !== undefined) ctx.emitLabel(endLabel);
    } else {
      ctx.emitLabel(nonZeroLabel);
    }
    ctx.optimizations.push({
      name: "fl-decrement-zero-branch",
      detail: `Fused ${decrement.target} decrement and zero branch at lines ${decrement.line}/${branch.line}.`,
    });
    return true;
}

export function compileDecrementUnderflowBranch(ctx: LoweringCtx, 
    decrement: Extract<StatementAst, { kind: "assign" }>,
    branch: Extract<StatementAst, { kind: "if" }>,
  ): boolean {
    if (!isUnitDecrementExpression(decrement.target, decrement.expr)) return false;
    if (branch.elseBody !== undefined) return false;
    if (!decrementBranchTestsUnderflow(branch.condition, decrement.target)) return false;
    if (!ctx.statementsTerminate(branch.thenBody)) return false;
    if (ctx.allocation.registers[decrement.target] === undefined) return false;

    const okLabel = ctx.freshLabel("decrement_ok");
    ctx.emitRecall(decrement.target, `decrement/test ${decrement.target}`, decrement.line);
    ctx.emitNumberOrPreload("1");
    ctx.emitOp(0x11, "-", `decrement/test ${decrement.target}`, decrement.line);
    ctx.emitJump(0x5c, "F x<0", okLabel, `decrement underflow ${decrement.target}`, branch.line);
    ctx.compileStatements(branch.thenBody);
    ctx.emitLabel(okLabel);
    ctx.currentXVariable = undefined;
    ctx.currentXAliases.clear();
    ctx.currentXKnownZero = false;
    ctx.emitStore(decrement.target, `set ${decrement.target}`, decrement.line);
    ctx.optimizations.push({
      name: "decrement-underflow-branch",
      detail: `Fused ${decrement.target} decrement and negative branch at lines ${decrement.line}/${branch.line}.`,
    });
    return true;
}

// True when `statements` is exactly a domain-error trap: a single `halt("ЕГГОГ")`
// (literal that lowers to the one-cell ЕГГ0Г stop), or a single call to a proc
// whose body is exactly that. This is the shape a `< 0` / `<= 0` guard branches
// to before it is replaced by a self-trapping domain opcode.
function statementsAreDomainErrorTrap(ctx: LoweringCtx, statements: readonly StatementAst[], seen = new Set<string>()): boolean {
    if (statements.length !== 1) return false;
    const statement = statements[0]!;
    if (statement.kind === "halt") {
      return statement.literal !== undefined && displayLiteralProgram(statement.literal)?.kind === "error";
    }
    if (statement.kind === "call") {
      if (seen.has(statement.block)) return false;
      seen.add(statement.block);
      const proc = ctx.ast.procs.find((candidate) => candidate.name === statement.block);
      if (proc === undefined) return false;
      return statementsAreDomainErrorTrap(ctx, proc.body, seen);
    }
    return false;
}

// Plan how to lower a comparison guard whose taken branch is a pure domain-error
// trap into a single self-trapping opcode. Each opcode raises ЕГГОГ exactly on
// its mathematical domain (proved on hardware in tests/emulator/trap-opcodes.test.ts):
//   `F √`   traps iff X < 0
//   `F lg`  traps iff X <= 0
//   `F 1/x` traps iff X == 0   (division by zero)
// The plan computes the comparison difference D = first - second into X so the
// trap fires iff the guard condition is true; otherwise execution falls through
// into the false path. A single operand means no subtraction is needed:
//   a <  b  <=>  a - b <  0   (√)
//   a <= b  <=>  a - b <= 0   (lg)
//   a >  b  <=>  b - a <  0   (√)
//   a >= b  <=>  b - a <= 0   (lg)
//   a == b  <=>  a - b == 0   (1/x; symmetric, so either zero side collapses)
interface DomainErrorGuardPlan {
  first: ExpressionAst;
  second?: ExpressionAst;
  trapOpcode: number;
  trapMnemonic: string;
}

function planDomainErrorGuard(condition: ConditionAst): DomainErrorGuardPlan | undefined {
    const SQRT_OPCODE = 0x21;
    const SQRT_MNEMONIC = "F sqrt";
    const LG_OPCODE = 0x17;
    const LG_MNEMONIC = "F lg";
    const RECIP_OPCODE = 0x23;
    const RECIP_MNEMONIC = "F 1/x";
    let trapOpcode: number;
    let trapMnemonic: string;
    let a: ExpressionAst;
    let b: ExpressionAst;
    // `symmetric` traps test |difference| against zero (==), so the minuend and
    // subtrahend are interchangeable; sign-sensitive traps must keep their order.
    let symmetric = false;
    switch (condition.op) {
      case "<":
        trapOpcode = SQRT_OPCODE; trapMnemonic = SQRT_MNEMONIC; a = condition.left; b = condition.right; break;
      case "<=":
        trapOpcode = LG_OPCODE; trapMnemonic = LG_MNEMONIC; a = condition.left; b = condition.right; break;
      case ">":
        trapOpcode = SQRT_OPCODE; trapMnemonic = SQRT_MNEMONIC; a = condition.right; b = condition.left; break;
      case ">=":
        trapOpcode = LG_OPCODE; trapMnemonic = LG_MNEMONIC; a = condition.right; b = condition.left; break;
      case "==":
        trapOpcode = RECIP_OPCODE; trapMnemonic = RECIP_MNEMONIC; a = condition.left; b = condition.right; symmetric = true; break;
      default:
        return undefined;
    }
    // D = a - b. When the subtrahend b is zero, D = a (no subtraction). For an
    // equality trap the difference is symmetric, so a zero minuend collapses to
    // the other operand too; for sign-sensitive traps a zero minuend (D = -b)
    // would need an extra negate, so it is left to the ordinary path.
    if (isZeroExpression(b)) return { first: a, trapOpcode, trapMnemonic };
    if (isZeroExpression(a)) return symmetric ? { first: b, trapOpcode, trapMnemonic } : undefined;
    return { first: a, second: b, trapOpcode, trapMnemonic };
}

// Replace a terminal-trap guard with a self-trapping domain opcode. Speculative
// (gated by loweringOptions.domainErrorGuards): emitting `F √` / `F lg` / `F 1/x`
// collapses the compare + conditional branch + shared trap into one cell, and
// when every caller of a shared trap proc is converted, that proc becomes dead
// and is dropped. The smallest variant is selected, so it never regresses.
export function compileDomainErrorGuard(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    if (ctx.loweringOptions.domainErrorGuards !== true) return false;
    if (!statementsAreDomainErrorTrap(ctx, statement.thenBody)) return false;
    const plan = planDomainErrorGuard(statement.condition);
    if (plan === undefined) return false;

    // Mirror compileCondition's X reuse: a bare identifier already sitting in X
    // (e.g. straight after a read) has no register to recall, so only recompute
    // operands that X does not already hold.
    const emitOperand = (expr: ExpressionAst): void => {
      if (expr.kind === "identifier" && ctx.xHolds(expr.name)) return;
      compileExpression(ctx, expr);
    };
    if (plan.second === undefined) {
      emitOperand(plan.first);
    } else {
      compileExpression(ctx, plan.first);
      compileExpression(ctx, plan.second);
      ctx.emitOp(0x11, "-", "domain guard difference", line);
    }
    ctx.emitOp(plan.trapOpcode, plan.trapMnemonic, "domain-error guard trap", line);
    // The trap consumed the taken (true) branch; X now holds garbage on the
    // fall-through (false) path, so reset the tracked X state before the else.
    ctx.currentXVariable = undefined;
    ctx.currentXAliases.clear();
    ctx.currentXKnownZero = false;
    ctx.scaledCoordVariables.clear();
    if (statement.elseBody !== undefined) ctx.compileStatements(statement.elseBody);
    ctx.optimizations.push({
      name: "domain-error-guard",
      detail: `Replaced a "${statement.condition.op}" terminal-error guard with a self-trapping ${plan.trapMnemonic} at line ${line}.`,
    });
    return true;
}

export function compileIf(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): void {
    if (compileDomainErrorGuard(ctx, statement, line)) return;
    if (compileArithmeticIfSelect(ctx, statement)) return;
    if (compileGuardedUpdateSelector(ctx, statement)) return;
    if (compileNestedGuardSharedFailure(ctx, statement, line)) return;
    if (compileResidualGuardedUpdate(ctx, statement, line)) return;
    if (compileDirectTerminalIfBranch(ctx, statement, line)) return;
    if (compileMembershipClearReuse(ctx, statement, line)) return;
    if (compileMembershipSetReuse(ctx, statement, line)) return;
    if (compileLocalTerminalElseTail(ctx, statement, line)) return;
    if (compileResidualEqualityElseIf(ctx, statement, line)) return;

    const selected = ctx.branchOrderStatement(statement, line);
    const falseLabel = ctx.freshLabel("if_false");
    const thenTerminates = ctx.statementsTerminate(selected.thenBody);
    const endLabel = thenTerminates ? undefined : ctx.freshLabel("if_end");
    const fallthroughIdentifier = ctx.nearAnyFallthroughCandidate(selected.condition, selected.thenBody);
    const falseBranchIdentifier = selected.elseBody === undefined
      ? undefined
      : ctx.falseBranchCurrentXCandidate(selected.condition, selected.elseBody);
    compileCondition(ctx, selected.condition, falseLabel, line);
    if (fallthroughIdentifier !== undefined) {
      ctx.currentXVariable = fallthroughIdentifier;
      ctx.currentXAliases = new Set([fallthroughIdentifier]);
      ctx.currentXKnownZero = false;
    }
    ctx.compileStatements(selected.thenBody);
    if (selected.elseBody) {
      if (endLabel !== undefined) ctx.emitJump(0x51, "БП", endLabel, "if end", line);
      ctx.emitLabel(falseLabel);
      if (falseBranchIdentifier !== undefined) {
        ctx.currentXVariable = falseBranchIdentifier;
        ctx.currentXAliases = new Set([falseBranchIdentifier]);
        ctx.currentXKnownZero = false;
        ctx.optimizations.push({
          name: "x-preserving-false-branch",
          detail: `Preserved ${falseBranchIdentifier} in X across the false branch of the zero-test at line ${line}.`,
        });
      }
      ctx.compileStatements(selected.elseBody);
      if (endLabel !== undefined) ctx.emitLabel(endLabel);
      if (thenTerminates) {
        ctx.optimizations.push({
          name: "terminal-branch-end-elision",
          detail: `Omitted unreachable if-end jump after terminal then branch at line ${line}.`,
        });
      }
    } else {
      ctx.emitLabel(falseLabel);
    }
}

export function compileResidualEqualityElseIf(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    if (statement.elseBody?.length !== 1) return false;
    const nested = statement.elseBody[0];
    if (nested?.kind !== "if") return false;

    const first = matchEqualityConstantCondition(statement.condition);
    const second = matchEqualityConstantCondition(nested.condition);
    if (first === undefined || second === undefined) return false;
    if (!expressionEquals(first.expr, second.expr) || !expressionIsDeterministic(first.expr)) return false;
    if (first.value === second.value) return false;

    const residualCost = residualAdjustmentCost(first.value, second.value);
    const ordinarySecondCompareCost = estimateExpressionCost(second.expr) + estimateNumberCost(String(second.value)) + 1;
    if (residualCost >= ordinarySecondCompareCost) return false;

    const firstFalseLabel = ctx.freshLabel("if_residual_next");
    const secondFalseLabel = ctx.freshLabel("if_residual_false");
    const thenTerminates = ctx.statementsTerminate(statement.thenBody);
    const nestedThenTerminates = ctx.statementsTerminate(nested.thenBody);
    const endLabel =
      !thenTerminates || (nested.elseBody !== undefined && !nestedThenTerminates)
        ? ctx.freshLabel("if_residual_end")
        : undefined;

    compileExpression(ctx, first.expr);
    ctx.emitNumberOrPreload(String(first.value));
    ctx.emitOp(0x11, "-", "condition compare", line);
    ctx.emitJump(0x5e, "F x=0", firstFalseLabel, "false branch for ==", line);
    ctx.compileStatements(statement.thenBody);
    if (!thenTerminates && endLabel !== undefined) {
      ctx.emitJump(0x51, "БП", endLabel, "if end", line);
    }

    ctx.emitLabel(firstFalseLabel);
    emitResidualAdjustment(ctx, first.value, second.value, nested.line);
    ctx.emitJump(0x5e, "F x=0", secondFalseLabel, "false branch for ==", nested.line);
    ctx.compileStatements(nested.thenBody);
    if (nested.elseBody !== undefined && !nestedThenTerminates && endLabel !== undefined) {
      ctx.emitJump(0x51, "БП", endLabel, "if end", nested.line);
    }

    ctx.emitLabel(secondFalseLabel);
    if (nested.elseBody !== undefined) ctx.compileStatements(nested.elseBody);
    if (endLabel !== undefined) ctx.emitLabel(endLabel);
    ctx.optimizations.push({
      name: "residual-elseif-compare",
      detail: `Reused ${expressionToIntentText(first.expr)} - ${first.value} for else-if comparison to ${second.value} at line ${nested.line}.`,
    });
    return true;
}

export function emitResidualAdjustment(ctx: LoweringCtx, 
    previousValue: number,
    nextValue: number,
    line: number | undefined,
  ): void {
    const delta = previousValue - nextValue;
    if (delta === 0) return;
    if (delta > 0) {
      ctx.emitNumberOrPreload(String(delta));
      ctx.emitOp(0x10, "+", "residual else-if compare", line);
      return;
    }
    ctx.emitNumberOrPreload(String(-delta));
    ctx.emitOp(0x11, "-", "residual else-if compare", line);
}

export function compileNestedGuardSharedFailure(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    if (statement.elseBody === undefined || statement.thenBody.length !== 1) return false;
    const inner = statement.thenBody[0];
    if (inner?.kind !== "if" || inner.elseBody === undefined) return false;
    if (!statementListsEqual(statement.elseBody, inner.elseBody)) return false;

    const failureLabel = ctx.freshLabel("guard_failure");
    const thenTerminates = ctx.statementsTerminate(inner.thenBody);
    const endLabel = thenTerminates ? undefined : ctx.freshLabel("guard_end");
    compileCondition(ctx, statement.condition, failureLabel, line);
    const residualUpdate = matchResidualGuardedUpdate(inner);
    const useResidualUpdate = residualUpdate !== undefined && residualGuardedUpdateSaves(ctx, residualUpdate);
    if (useResidualUpdate) {
      compileResidualGuardedCondition(ctx, residualUpdate, failureLabel, inner.line);
      emitResidualGuardedUpdate(ctx, residualUpdate);
    } else {
      compileCondition(ctx, inner.condition, failureLabel, inner.line);
    }
    const successBody = useResidualUpdate ? residualUpdate.tail : inner.thenBody;
    ctx.compileStatements(successBody);
    if (endLabel !== undefined) ctx.emitJump(0x51, "БП", endLabel, "guard success end", inner.line);
    ctx.emitLabel(failureLabel);
    ctx.compileStatements(statement.elseBody);
    if (endLabel !== undefined) ctx.emitLabel(endLabel);
    ctx.optimizations.push({
      name: "nested-guard-shared-failure",
      detail: `Shared identical nested failure branch at lines ${line}/${inner.line}.`,
    });
    return true;
}

export function compileDirectTerminalIfBranch(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    const thenTarget = ctx.directTerminalCallTarget(statement.thenBody);
    const elseTarget = statement.elseBody === undefined ? undefined : ctx.directTerminalCallTarget(statement.elseBody);
    if (thenTarget === undefined && elseTarget === undefined) return false;

    const preloadedConstants = new Set(Object.keys(ctx.allocation.constants));
    const originalCost = estimateConditionCost(statement.condition, ctx.ast, preloadedConstants);
    const candidates: Array<{
      branchWhen: "true" | "false";
      target: string;
      condition: ConditionAst;
      estimatedCost: number;
      ordinaryCost: number;
    }> = [];

    if (
      thenTarget !== undefined &&
      (statement.elseBody !== undefined || ctx.loweringOptions.aggressiveTerminalDirect === true)
    ) {
      const condition = invertCondition(statement.condition);
      candidates.push({
        branchWhen: "true",
        target: thenTarget,
        condition,
        estimatedCost: estimateConditionCost(condition, ctx.ast, preloadedConstants),
        ordinaryCost: originalCost + 2,
      });
    }
    if (elseTarget !== undefined) {
      const thenTerminates = ctx.statementsTerminate(statement.thenBody);
      candidates.push({
        branchWhen: "false",
        target: elseTarget,
        condition: statement.condition,
        estimatedCost: originalCost,
        ordinaryCost: originalCost + (thenTerminates ? 1 : 2),
      });
    }

    const selected = candidates
      .filter((candidate) => candidate.estimatedCost < candidate.ordinaryCost)
      .sort((left, right) => left.estimatedCost - right.estimatedCost)[0];
    if (selected === undefined) return false;

    compileCondition(ctx, selected.condition, selected.target, line);
    if (selected.branchWhen === "true") {
      if (statement.elseBody) ctx.compileStatements(statement.elseBody);
    } else {
      ctx.compileStatements(statement.thenBody);
    }
    ctx.optimizations.push({
      name: "terminal-if-direct-branch",
      detail: `Branched directly to terminal ${selected.target} for ${selected.branchWhen} path at line ${line} (${selected.estimatedCost} vs ${selected.ordinaryCost} estimated branch cells).`,
    });
    return true;
}

export function compileResidualGuardedUpdate(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    const update = matchResidualGuardedUpdate(statement);
    if (update === undefined) return false;
    if (!residualGuardedUpdateSaves(ctx, update)) return false;

    const falseLabel = ctx.freshLabel("if_false");
    const thenTerminates = ctx.statementsTerminate(update.tail);
    const endLabel = statement.elseBody !== undefined && !thenTerminates ? ctx.freshLabel("if_end") : undefined;

    compileResidualGuardedCondition(ctx, update, falseLabel, line);
    emitResidualGuardedUpdate(ctx, update);
    ctx.compileStatements(update.tail);

    if (statement.elseBody !== undefined) {
      if (endLabel !== undefined) ctx.emitJump(0x51, "БП", endLabel, "if end", line);
      ctx.emitLabel(falseLabel);
      ctx.compileStatements(statement.elseBody);
      if (endLabel !== undefined) ctx.emitLabel(endLabel);
    } else {
      ctx.emitLabel(falseLabel);
    }

    return true;
}

type ResidualGuardedUpdate = NonNullable<ReturnType<typeof matchResidualGuardedUpdate>>;

function residualGuardedUpdateSaves(ctx: LoweringCtx, update: ResidualGuardedUpdate): boolean {
    const correction = update.bound + update.delta;
    const correctionRaw = String(correction);
    const ordinaryUpdateCost = estimateExpressionCost(update.assignment.expr) + 1;
    const residualUpdateCost = (correction === 0 ? 0 : ctx.estimateNumberOrPreloadCost(correctionRaw) + 1) + 1;
    return residualUpdateCost < ordinaryUpdateCost;
}

function compileResidualGuardedCondition(
    ctx: LoweringCtx,
    update: ResidualGuardedUpdate,
    falseLabel: string,
    line: number,
  ): void {
    compileExpression(ctx, update.condition.left);
    compileExpression(ctx, update.condition.right);
    ctx.emitOp(0x11, "-", "condition compare", line);
    const falseOpcode = update.condition.op === "<" ? 0x5c : 0x59;
    ctx.emitJump(falseOpcode, getOpcode(falseOpcode).name, falseLabel, `false branch for ${update.condition.op}`, line);
}

function emitResidualGuardedUpdate(ctx: LoweringCtx, update: ResidualGuardedUpdate): void {
    const correction = update.bound + update.delta;
    if (correction !== 0) {
      ctx.emitNumberOrPreload(String(correction));
      ctx.emitOp(0x10, "+", `residual guarded update ${update.target}`, update.assignment.line);
    }
    ctx.emitStore(update.target, `set ${update.target}`, update.assignment.line);
    ctx.optimizations.push({
      name: "residual-guarded-update",
      detail: `Reused ${update.target} - ${update.bound} while updating ${update.target} at line ${update.assignment.line}.`,
    });
}

export function compileMembershipClearReuse(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    const clearPrefix = ctx.membershipClearPrefix(statement.thenBody);
    if (clearPrefix === undefined) return false;
    const { clear, tail } = clearPrefix;
    const membership = matchBitMembershipCondition(statement.condition);
    if (membership === undefined) return false;
    if (!isBitClearAssignment(clear, membership)) return false;

    const falseLabel = ctx.freshLabel("if_false");
    const endLabel = ctx.freshLabel("if_end");

    if (!compileBitMembershipMaskValue(ctx, membership, line)) compileExpression(ctx, membership.test);
    ctx.emitJump(0x57, "F x!=0", falseLabel, "false branch for !=", line);
    ctx.emitOp(0x3a, "К ИНВ", "reuse membership mask for clear", clear.line);
    compileExpression(ctx, membership.collection);
    ctx.emitOp(0x37, "К ∧", "clear matched cell with reused mask", clear.line);
    ctx.emitStore(clear.target, `set ${clear.target}`, clear.line);
    ctx.compileStatements(tail);
    if (statement.elseBody) {
      ctx.emitJump(0x51, "БП", endLabel, "if end", line);
      ctx.emitLabel(falseLabel);
      ctx.currentXKnownZero = true;
      ctx.compileStatements(statement.elseBody);
      ctx.emitLabel(endLabel);
    } else {
      ctx.emitLabel(falseLabel);
    }
    ctx.optimizations.push({
      name: "cell-membership-clear-reuse",
      detail: `Reused the successful membership mask when clearing ${clear.target} at line ${clear.line}.`,
    });
    return true;
}

export function compileBitMembershipMaskValue(ctx: LoweringCtx, membership: BitMembershipCondition, line: number): boolean {
    if (membership.mode === "mask") {
      compileExpression(ctx, membership.mask);
      compileExpression(ctx, membership.collection);
      ctx.emitOp(0x37, "К ∧", "bit membership test", line);
      ctx.emitOp(0x35, "К {x}", "bit membership fraction", line);
      return true;
    }
    if (membership.collection.kind !== "identifier") return false;
    const scratch = spatialHitScratchName(membership.collection.name);
    if (ctx.allocation.registers[scratch] === undefined) return false;
    const helper = ctx.ensureSpatialBitMaskHelper(scratch, line);
    compileExpression(ctx, membership.item);
    ctx.emitJump(0x53, "ПП", helper.label, "bit_mask helper", line);
    compileExpression(ctx, membership.collection);
    ctx.emitOp(0x37, "К ∧", "bit membership test", line);
    ctx.emitOp(0x35, "К {x}", "bit membership fraction", line);
    ctx.optimizations.push({
      name: "bit-mask-helper-call",
      detail: `Reused shared bit_mask helper for ${membership.collection.name} at line ${line}.`,
    });
    return true;
}

export function compileMembershipSetReuse(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    const present = matchBitMembershipCondition(statement.condition);
    if (present !== undefined && present.collection.kind === "identifier" && statement.elseBody !== undefined) {
      const setRun = ctx.membershipSetRunPrefix(statement.elseBody, present);
      if (setRun !== undefined) {
        return compileMembershipSetRunReuseForPresentCondition(ctx, statement, present, setRun, line);
      }
      const setPrefix = ctx.membershipSetPrefix(statement.elseBody, present);
      if (setPrefix !== undefined) {
        return compileMembershipSetReuseForPresentCondition(ctx, statement, present, setPrefix, line);
      }
    }

    const absent = matchBitAbsenceCondition(statement.condition);
    if (absent === undefined || absent.collection.kind !== "identifier") return false;
    const setRun = ctx.membershipSetRunPrefix(statement.thenBody, absent);
    if (setRun !== undefined) {
      return compileMembershipSetRunReuseForAbsentCondition(ctx, statement, absent, setRun, line);
    }
    const setPrefix = ctx.membershipSetPrefix(statement.thenBody, absent);
    if (setPrefix === undefined) return false;
    return compileMembershipSetReuseForAbsentCondition(ctx, statement, absent, setPrefix, line);
}

export function compileMembershipSetReuseForPresentCondition(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    membership: BitMembershipCondition,
    setPrefix: {
      set: Extract<StatementAst, { kind: "assign" }>;
      tail: StatementAst[];
    },
    line: number,
  ): boolean {
    const scratch = bitMaskScratchName(statement);
    if (ctx.allocation.registers[scratch] === undefined) return false;

    const { set, tail } = setPrefix;
    const falseLabel = ctx.freshLabel("if_false");
    const thenTerminates = ctx.statementsTerminate(statement.thenBody);
    const endLabel = thenTerminates ? undefined : ctx.freshLabel("if_end");

    emitMembershipMaskTest(ctx, membership, scratch, line);
    ctx.emitJump(0x5e, "F x=0", falseLabel, "false branch for !=", line);
    ctx.compileStatements(statement.thenBody);
    if (endLabel !== undefined) ctx.emitJump(0x51, "БП", endLabel, "if end", line);
    ctx.emitLabel(falseLabel);
    emitBitSetWithScratch(ctx, membership, set, scratch);
    ctx.compileStatements(tail);
    if (endLabel !== undefined) ctx.emitLabel(endLabel);

    ctx.optimizations.push({
      name: "cell-membership-set-reuse",
      detail: `Reused the failed membership mask when setting ${set.target} at line ${set.line}.`,
    });
    return true;
}

export function compileMembershipSetReuseForAbsentCondition(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    membership: BitMembershipCondition,
    setPrefix: {
      set: Extract<StatementAst, { kind: "assign" }>;
      tail: StatementAst[];
    },
    line: number,
  ): boolean {
    const scratch = bitMaskScratchName(statement);
    if (ctx.allocation.registers[scratch] === undefined) return false;

    const { set, tail } = setPrefix;
    const falseLabel = ctx.freshLabel("if_false");
    const thenTerminates = ctx.statementsTerminate(statement.thenBody);
    const endLabel = statement.elseBody !== undefined && !thenTerminates ? ctx.freshLabel("if_end") : undefined;

    emitMembershipMaskTest(ctx, membership, scratch, line);
    ctx.emitJump(0x57, "F x!=0", falseLabel, "false branch for ==", line);
    emitBitSetWithScratch(ctx, membership, set, scratch);
    ctx.compileStatements(tail);
    if (statement.elseBody !== undefined) {
      if (endLabel !== undefined) ctx.emitJump(0x51, "БП", endLabel, "if end", line);
      ctx.emitLabel(falseLabel);
      ctx.compileStatements(statement.elseBody);
      if (endLabel !== undefined) ctx.emitLabel(endLabel);
    } else {
      ctx.emitLabel(falseLabel);
    }

    ctx.optimizations.push({
      name: "cell-membership-set-reuse",
      detail: `Reused the failed membership mask when setting ${set.target} at line ${set.line}.`,
    });
    return true;
}

export function compileMembershipSetRunReuseForPresentCondition(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    membership: BitMembershipCondition,
    setRun: {
      sets: Array<{
        set: Extract<StatementAst, { kind: "assign" }>;
        collection: ExpressionAst;
      }>;
      tail: StatementAst[];
    },
    line: number,
  ): boolean {
    const scratch = bitMaskScratchName(setRun.sets[0]!.set);
    if (ctx.allocation.registers[scratch] === undefined) return false;

    const falseLabel = ctx.freshLabel("if_false");
    const thenTerminates = ctx.statementsTerminate(statement.thenBody);
    const endLabel = thenTerminates ? undefined : ctx.freshLabel("if_end");

    emitMembershipMaskTest(ctx, membership, scratch, line);
    ctx.emitJump(0x5e, "F x=0", falseLabel, "false branch for !=", line);
    ctx.compileStatements(statement.thenBody);
    if (endLabel !== undefined) ctx.emitJump(0x51, "БП", endLabel, "if end", line);
    ctx.emitLabel(falseLabel);
    for (const { set, collection } of setRun.sets) {
      emitBitSetCollectionWithScratch(ctx, collection, set, scratch);
    }
    ctx.compileStatements(setRun.tail);
    if (endLabel !== undefined) ctx.emitLabel(endLabel);

    const targets = setRun.sets.map(({ set }) => set.target).join(", ");
    ctx.optimizations.push({
      name: "cell-membership-mask-run-reuse",
      detail: `Reused the failed membership mask when setting ${targets} after line ${line}.`,
    });
    return true;
}

export function compileMembershipSetRunReuseForAbsentCondition(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    membership: BitMembershipCondition,
    setRun: {
      sets: Array<{
        set: Extract<StatementAst, { kind: "assign" }>;
        collection: ExpressionAst;
      }>;
      tail: StatementAst[];
    },
    line: number,
  ): boolean {
    const scratch = bitMaskScratchName(setRun.sets[0]!.set);
    if (ctx.allocation.registers[scratch] === undefined) return false;

    const falseLabel = ctx.freshLabel("if_false");
    const thenTerminates = ctx.statementsTerminate(statement.thenBody);
    const endLabel = statement.elseBody !== undefined && !thenTerminates ? ctx.freshLabel("if_end") : undefined;

    emitMembershipMaskTest(ctx, membership, scratch, line);
    ctx.emitJump(0x57, "F x!=0", falseLabel, "false branch for ==", line);
    for (const { set, collection } of setRun.sets) {
      emitBitSetCollectionWithScratch(ctx, collection, set, scratch);
    }
    ctx.compileStatements(setRun.tail);
    if (statement.elseBody !== undefined) {
      if (endLabel !== undefined) ctx.emitJump(0x51, "БП", endLabel, "if end", line);
      ctx.emitLabel(falseLabel);
      ctx.compileStatements(statement.elseBody);
      if (endLabel !== undefined) ctx.emitLabel(endLabel);
    } else {
      ctx.emitLabel(falseLabel);
    }

    const targets = setRun.sets.map(({ set }) => set.target).join(", ");
    ctx.optimizations.push({
      name: "cell-membership-mask-run-reuse",
      detail: `Reused the failed membership mask when setting ${targets} after line ${line}.`,
    });
    return true;
}

export function emitMembershipMaskTest(ctx: LoweringCtx, 
    membership: BitMembershipCondition,
    scratch: string,
    line: number,
  ): void {
    if (membership.mode === "index") {
      compileBitMaskWithQuotientScratch(ctx, membership.item, scratch, line);
    } else {
      compileExpression(ctx, membership.mask);
    }
    ctx.emitStore(scratch, "cell bit mask scratch", line);
    compileExpression(ctx, membership.collection);
    ctx.emitRecall(scratch, "reuse cell bit mask", line);
    ctx.emitOp(0x37, "К ∧", "membership test with reused mask", line);
    ctx.emitOp(0x35, "К {x}", "membership fraction", line);
}

export function compileArithmeticIfSelect(ctx: LoweringCtx, statement: Extract<StatementAst, { kind: "if" }>): boolean {
    const canUseNegativeZero = ctx.allocation.negativeZeroDegree !== undefined;
    const selected = buildBranchRemovalCandidate(
      statement,
      ctx.ast,
      { negativeZeroDegree: canUseNegativeZero },
    );
    if (!selected) {
      if (!canUseNegativeZero) ctx.recordRejectedNegativeZeroBranchCandidate(statement);
      return false;
    }

    const ordinaryCost = estimateOrdinaryIfCost(statement, ctx.ast);
    const selectedCost = estimateExpressionCost(selected.expr) + 1;
    if (selectedCost >= ordinaryCost) {
      ctx.candidates.push({
        site: `if@${statement.line}`,
        variant: selected.name,
        steps: selectedCost,
        selected: false,
        reason: `Branchless ${selected.name} estimated at ${selectedCost} cells; ordinary branched form was shorter (${ordinaryCost}).`,
      });
      if (!selected.name.startsWith("negative-zero-threshold-")) {
        ctx.recordRejectedNegativeZeroBranchCandidate(statement);
      }
      return false;
    }

    compileExpression(ctx, selected.expr);
    if (selected.kind === "assign") {
      ctx.emitStore(selected.target, `${selected.name} ${selected.target}`, statement.line);
    } else {
      ctx.emitOp(0x50, "С/П", `${selected.kind} ${selected.name}`, statement.line);
    }
    ctx.optimizations.push({
      name: "branch-removal",
      detail: `${selected.detail} at line ${statement.line}; emitted branchless ${selected.name}.`,
    });
    ctx.optimizations.push({
      name: selected.name,
      detail: `${selected.detail} at line ${statement.line} (${selectedCost} vs ${ordinaryCost} estimated steps).`,
    });
    return true;
}

export function compileGuardedUpdateSelector(ctx: LoweringCtx, statement: Extract<StatementAst, { kind: "if" }>): boolean {
    const scratch = ifSelectorScratchName(statement);
    if (ctx.allocation.registers[scratch] === undefined) return false;
    const candidate = buildGuardedUpdateSelectorCandidate(statement, ctx.ast, {
      negativeZeroDegree: ctx.allocation.negativeZeroDegree !== undefined,
    });
    if (candidate === undefined) return false;

    const ordinaryCost = estimateOrdinaryGuardedUpdateCost(statement, ctx.ast);
    const selectedCost = estimateGuardedUpdateSelectorCost(candidate, scratch);
    if (selectedCost >= ordinaryCost) {
      ctx.candidates.push({
        site: `if@${statement.line}`,
        variant: candidate.name,
        steps: selectedCost,
        selected: false,
        reason: `Guarded update selector estimated at ${selectedCost} cells; ordinary branched form was shorter (${ordinaryCost}).`,
      });
      return false;
    }

    compileExpression(ctx, candidate.selector);
    ctx.emitStore(scratch, `${candidate.name} selector`, statement.line);
    const selector: ExpressionAst = { kind: "identifier", name: scratch };
    for (const update of candidate.updates) {
      compileExpression(ctx, maskedGuardedUpdateExpression(update, selector));
      ctx.emitStore(update.target, `${candidate.name} ${update.target}`, statement.line);
    }
    ctx.optimizations.push({
      name: "branch-removal",
      detail: `${candidate.detail} at line ${statement.line}; emitted masked guarded updates.`,
    });
    ctx.optimizations.push({
      name: candidate.name,
      detail: `${candidate.detail} at line ${statement.line} (${selectedCost} vs ${ordinaryCost} estimated steps).`,
    });
    return true;
}

export function compileDoubleBranchRemoval(ctx: LoweringCtx, 
    first: Extract<StatementAst, { kind: "if" }>,
    second: Extract<StatementAst, { kind: "if" }>,
  ): boolean {
    const selected = buildDoubleClampCandidate(first, second);
    if (!selected) return false;

    const ordinaryCost = estimateOrdinaryIfCost(first, ctx.ast) + estimateOrdinaryIfCost(second, ctx.ast);
    const selectedCost = estimateExpressionCost(selected.expr) + 1;
    if (selectedCost >= ordinaryCost) {
      ctx.candidates.push({
        site: `if@${first.line}+${second.line}`,
        variant: selected.name,
        steps: selectedCost,
        selected: false,
        reason: `Branchless ${selected.name} estimated at ${selectedCost} cells; paired branched form was shorter (${ordinaryCost}).`,
      });
      return false;
    }

    compileExpression(ctx, selected.expr);
    ctx.emitStore(selected.target, `${selected.name} ${selected.target}`, first.line);
    ctx.optimizations.push({
      name: "branch-removal",
      detail: `${selected.detail} at lines ${first.line}/${second.line}; emitted branchless ${selected.name}.`,
    });
    ctx.optimizations.push({
      name: selected.name,
      detail: `${selected.detail} at lines ${first.line}/${second.line} (${selectedCost} vs ${ordinaryCost} estimated steps).`,
    });
    return true;
}

export function compileDispatch(ctx: LoweringCtx, statement: Extract<StatementAst, { kind: "dispatch" }>): void {
    const optimized = optimizeDispatchDefaultCases(statement);
    if (optimized.removed > 0) {
      ctx.optimizations.push({
        name: "dispatch-default-merge",
        detail: `Removed ${optimized.removed} dispatch case${optimized.removed === 1 ? "" : "s"} whose body matched the default branch.`,
      });
    }
    if (optimized.reordered > 0) {
      ctx.optimizations.push({
        name: "dispatch-case-ordering",
        detail: `Reordered ${optimized.reordered} numeric dispatch case${optimized.reordered === 1 ? "" : "s"} to shorten residual comparisons.`,
      });
    }

    const site = statement.name ?? `dispatch@${statement.line}`;
    const selected = selectDispatchCandidate(optimized.statement, ctx.machineProfile);
    for (const candidate of selected.candidates) ctx.candidates.push(candidate);

    ctx.optimizations.push({
      name: "dispatch-lowering",
      detail: `Selected ${selected.selected.variant} for ${site}.`,
    });

    compileDispatchCompareChain(ctx, optimized.statement, selected.selected.variant === "fallthrough-compare-chain");
}

export function compileDispatchCompareChain(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "dispatch" }>,
    useFallthrough: boolean,
  ): void {
    if (compileNumericResidualDispatchCompareChain(ctx, statement, useFallthrough)) return;

    const scratch = `${DISPATCH_SCRATCH_PREFIX}${statement.scratchId}`;
    const sourceRegister = dispatchExpressionRegister(statement, ctx.allocation);
    const register = sourceRegister ?? ctx.allocation.registers[scratch];
    if (!register) {
      if (compileNumericResidualDispatchCompareChain(ctx, statement, useFallthrough, { allowSingleCase: true })) return;
      ctx.diagnostics.push({
        level: "error",
        message: `Internal: no scratch register reserved for dispatch at line ${statement.line}.`,
        line: statement.line,
      });
      return;
    }

    compileDispatchSelectorExpression(ctx, statement.expr);
    if (sourceRegister === undefined) {
      ctx.emitOp(
        0x40 + registerIndex(register),
        `X->П ${register}`,
        `dispatch scratch`,
        statement.line,
      );
    } else {
      ctx.optimizations.push({
        name: "dispatch-source-register",
        detail: `Reused R${register} as dispatch scratch for identifier expression.`,
      });
    }

    const endLabel = ctx.freshLabel("dispatch_end");
    let xContainsDispatchExpr = sourceRegister !== undefined;
    for (let index = 0; index < statement.cases.length; index += 1) {
      const dispatchCase = statement.cases[index]!;
      const nextLabel = ctx.freshLabel("dispatch_next");
      const lastCase = index === statement.cases.length - 1;
      if (index > 0 && !xContainsDispatchExpr) {
        ctx.emitOp(
          0x60 + registerIndex(register),
          `П->X ${register}`,
          "dispatch scratch recall",
          dispatchCase.line,
        );
        xContainsDispatchExpr = true;
      }
      if (xContainsDispatchExpr && isZeroExpression(dispatchCase.value)) {
        ctx.emitJump(0x5e, "F x=0", nextLabel, "zero-case mismatch", dispatchCase.line);
      } else {
        compileExpression(ctx, dispatchCase.value);
        ctx.emitOp(0x11, "-", "dispatch compare", dispatchCase.line);
        ctx.emitJump(0x5e, "F x=0", nextLabel, "case mismatch", dispatchCase.line);
        xContainsDispatchExpr = false;
      }
      markDispatchCaseMatchZero(ctx);
      ctx.compileStatements(dispatchCase.body);
      if (
        !ctx.statementsTerminate(dispatchCase.body) &&
        (!useFallthrough || !lastCase || statement.defaultBody !== undefined)
      ) {
        ctx.emitJump(0x51, "БП", endLabel, "dispatch end", dispatchCase.line);
      }
      ctx.emitLabel(nextLabel);
      xContainsDispatchExpr = xContainsDispatchExpr && isZeroExpression(dispatchCase.value);
    }
    if (statement.defaultBody) ctx.compileStatements(statement.defaultBody);
    ctx.emitLabel(endLabel);
}

export function compileNumericResidualDispatchCompareChain(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "dispatch" }>,
    useFallthrough: boolean,
    options: { allowSingleCase?: boolean } = {},
  ): boolean {
    if (!dispatchUsesNumericResidualChain(statement)) {
      if (options.allowSingleCase !== true || statement.cases.length !== 1) return false;
    }
    const values = statement.cases.map((dispatchCase) => numericLiteralValue(dispatchCase.value));
    if (values.some((value) => value === undefined)) return false;
    const numericValues = values as number[];

    compileDispatchSelectorExpression(ctx, statement.expr);
    const endLabel = ctx.freshLabel("dispatch_end");
    let comparedValue = 0;
    let hasComparedValue = false;
    for (let index = 0; index < statement.cases.length; index += 1) {
      const dispatchCase = statement.cases[index]!;
      const value = numericValues[index]!;
      const nextLabel = ctx.freshLabel("dispatch_next");
      const lastCase = index === statement.cases.length - 1;
      if (!hasComparedValue) {
        if (value !== 0) {
          emitPositiveResidualCompare(ctx, value, "dispatch compare", dispatchCase.line);
        }
        hasComparedValue = true;
      } else {
        emitResidualCompareDelta(ctx, value - comparedValue, "dispatch residual compare", dispatchCase.line);
      }
      comparedValue = value;
      ctx.emitJump(0x5e, "F x=0", nextLabel, "case mismatch", dispatchCase.line);
      markDispatchCaseMatchZero(ctx);
      ctx.compileStatements(dispatchCase.body);
      if (
        !ctx.statementsTerminate(dispatchCase.body) &&
        (!useFallthrough || !lastCase || statement.defaultBody !== undefined)
      ) {
        ctx.emitJump(0x51, "БП", endLabel, "dispatch end", dispatchCase.line);
      }
      ctx.emitLabel(nextLabel);
    }
    if (statement.defaultBody) ctx.compileStatements(statement.defaultBody);
    ctx.emitLabel(endLabel);
    ctx.optimizations.push({
      name: "numeric-dispatch-residual-chain",
      detail: `Reused residual comparisons for numeric dispatch at line ${statement.line}.`,
    });
    return true;
}

function compileDispatchSelectorExpression(ctx: LoweringCtx, expr: ExpressionAst): void {
    if (expr.kind === "identifier" && ctx.xHolds(expr.name)) return;
    compileExpression(ctx, expr);
}

function markDispatchCaseMatchZero(ctx: LoweringCtx): void {
    ctx.currentXVariable = undefined;
    ctx.currentXAliases.clear();
    ctx.currentXKnownZero = true;
}

function decrementBranchTestsUnderflow(condition: ConditionAst, target: string): boolean {
    return condition.left.kind === "identifier" &&
      condition.left.name === target &&
      condition.op === "<" &&
      isZeroExpression(condition.right);
}

export function emitPositiveResidualCompare(ctx: LoweringCtx, value: number, comment: string, line?: number): void {
    const magnitude = Math.abs(value);
    if (magnitude === 0) return;
    ctx.emitNumberOrPreload(String(magnitude));
    ctx.emitOp(value < 0 ? 0x10 : 0x11, value < 0 ? "+" : "-", comment, line);
}

export function emitResidualCompareDelta(ctx: LoweringCtx, delta: number, comment: string, line?: number): void {
    const magnitude = Math.abs(delta);
    if (magnitude === 0) return;
    ctx.emitNumberOrPreload(String(magnitude));
    ctx.emitOp(delta < 0 ? 0x10 : 0x11, delta < 0 ? "+" : "-", comment, line);
}

export function compileLiteralHalt(ctx: LoweringCtx, literal: string, line: number): void {
    const program = displayLiteralProgram(literal);
    if (program?.kind === "error") {
      emitErrorStopOpcode(ctx, "halt literal ЕГГ0Г", line);
      ctx.optimizations.push({
        name: "error-stop",
        detail: `Used one-cell error opcode for literal ЕГГ0Г stop at line ${line}.`,
      });
      return;
    }

    const display: ProgramAst["displays"][number] = {
      kind: "display",
      name: `__halt_literal_${line}`,
      format: "packed",
      sources: [],
      items: [{ kind: "literal", text: literal, line }],
      line,
    };
    if (!compileLiteralDisplayBody(ctx, display, line, literal)) {
      ctx.diagnostics.push(buildDiagnostic(
        "error",
        `Literal halt ${JSON.stringify(literal)} is not lowerable yet.`,
        line,
      ));
      return;
    }
    ctx.optimizations.push({
      name: "terminal-literal-stop",
      detail: `Lowered literal terminal stop ${JSON.stringify(literal)} at line ${line}.`,
    });
}

export function emitKnownOneIndirectLoopBack(ctx: LoweringCtx, target: string, line: number): boolean {
    if (!ctx.coordListCounterKnownOne || !ctx.zeroAddressLabels.has(target)) return false;
    const register = ctx.allocation.registers[COORD_LIST_COUNTER];
    if (register === undefined || flOpcode(register) === undefined) return false;
    ctx.emitOp(0x80 + registerIndex(register), `К БП ${register}`, "loop back via known-one counter", line);
    ctx.optimizations.push({
      name: "indirect-incdec-counter",
      detail: `Reused ${COORD_LIST_COUNTER} = 1 as a one-cell indirect loop-back to 00 at line ${line}.`,
    });
    return true;
}

export function compileCondition(ctx: LoweringCtx, 
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): void {
    if (compileCoordListHasCondition(ctx, condition, falseLabel, line)) return;
    if (compileNegativeZeroThresholdFlow(ctx, condition, falseLabel, line)) return;

    const preloadedConstants = new Set(Object.keys(ctx.allocation.constants));
    const selected = selectCheaperEquivalentCondition(
      condition,
      ctx.ast,
      preloadedConstants,
    );
    if (selected.changed) {
      ctx.optimizations.push({
        name: "comparison-boundary-normalization",
        detail: `Normalized ${conditionToText(condition)} to ${conditionToText(selected.condition)} at line ${line}.`,
      });
    }
    const compiledCondition = selected.condition;
    if (compileNearAnyHelperCondition(ctx, compiledCondition, falseLabel, line, preloadedConstants)) return;
    if (compileSmallSetCondition(ctx, compiledCondition, falseLabel, line, preloadedConstants)) return;
    if (compileRemainderZeroCondition(ctx, compiledCondition, falseLabel, line)) return;
    if (isZeroExpression(compiledCondition.right) && canTestAgainstZeroDirectly(compiledCondition.op)) {
      const bitHasLowering = compileBitHasConditionWithBitMaskHelper(ctx, compiledCondition.left, line)
        ?? compileBitHasConditionWithSpatialHelper(ctx, compiledCondition.left, line);
      if (bitHasLowering === undefined) {
        if (!(compiledCondition.left.kind === "identifier" && ctx.xHolds(compiledCondition.left.name))) {
          compileExpression(ctx, compiledCondition.left);
        }
      }
      const opcode = directTestOpcode(compiledCondition.op);
      ctx.emitJump(opcode, getOpcode(opcode).name, falseLabel, `false branch for ${condition.op}`, line);
      ctx.optimizations.push({
        name: bitHasLowering?.name ?? "zero-condition-test",
        detail: bitHasLowering?.detail
          ?? `Tested ${compiledCondition.op} 0 without materializing a zero literal at line ${line}.`,
      });
      return;
    }
    if (compileEqualityWithCurrentX(ctx, compiledCondition, falseLabel, line)) return;
    if (compiledCondition.op === ">" || compiledCondition.op === "<=") {
      compileExpression(ctx, compiledCondition.right);
      compileExpression(ctx, compiledCondition.left);
    } else {
      compileExpression(ctx, compiledCondition.left);
      compileExpression(ctx, compiledCondition.right);
    }
    ctx.emitOp(0x11, "-", "condition compare", line);

    const opcode =
      compiledCondition.op === "<" || compiledCondition.op === ">"
        ? 0x5c
        : compiledCondition.op === ">=" || compiledCondition.op === "<="
          ? 0x59
          : compiledCondition.op === "=="
            ? 0x5e
            : 0x57;
    const mnemonic = getOpcode(opcode).name;
    ctx.emitJump(opcode, mnemonic, falseLabel, `false branch for ${compiledCondition.op}`, line);
}

export function compileRemainderZeroCondition(
    ctx: LoweringCtx,
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): boolean {
    if ((condition.op !== "==" && condition.op !== "!=") || !isZeroExpression(condition.right)) return false;
    if (condition.left.kind !== "binary") return false;
    const matched = matchRemainderByConstant(condition.left);
    if (matched === undefined || numericLiteralValue(matched.divisor) === 0) return false;

    compileExpression(ctx, matched.value);
    compileExpression(ctx, matched.divisor);
    ctx.emitOp(0x13, "/", "remainder quotient", line);
    ctx.emitOp(0x35, "К {x}", "remainder zero fractional part", line);
    const opcode = directTestOpcode(condition.op);
    ctx.emitJump(opcode, getOpcode(opcode).name, falseLabel, `false branch for ${condition.op}`, line);
    ctx.optimizations.push({
      name: "remainder-zero-test-lowering",
      detail: `Tested ${expressionToIntentText(condition.left)} ${condition.op} 0 without rescaling the fractional remainder at line ${line}.`,
    });
    return true;
}

export function compileBitHasConditionWithBitMaskHelper(ctx: LoweringCtx, 
    expr: ExpressionAst,
    line: number,
  ): { name: string; detail: string } | undefined {
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== "bit_has" || expr.args.length !== 2) return undefined;
    const [mask, index] = expr.args;
    if (mask?.kind !== "identifier" || index === undefined) return undefined;
    if (
      !programHasLineCountForMask(ctx.ast, mask.name) &&
      ctx.loweringOptions.sharedBitMaskHelperCalls !== true
    ) return undefined;
    const scratch = ctx.sharedBitMaskHelperScratch() ?? spatialHitScratchName(mask.name);
    if (!ctx.allocation.registers[scratch]) return undefined;
    const helper = ctx.ensureSpatialBitMaskHelper(scratch, line);
    compileExpression(ctx, index);
    ctx.emitJump(0x53, "ПП", helper.label, "bit_mask helper", line);
    compileExpression(ctx, mask);
    ctx.emitOp(0x37, "К ∧", "bit membership test", line);
    ctx.emitOp(0x35, "К {x}", "bit membership fraction", line);
    return {
      name: "bit-mask-condition-helper",
      detail: `Tested bit_has() through the shared bit_mask helper at line ${line}.`,
    };
}

export function compileBitHasConditionWithSpatialHelper(ctx: LoweringCtx, 
    expr: ExpressionAst,
    line: number,
  ): { name: string; detail: string } | undefined {
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== "bit_has" || expr.args.length !== 2) return undefined;
    const [mask, index] = expr.args;
    if (mask?.kind !== "identifier" || index === undefined) return undefined;
    const scratch = spatialHitScratchName(mask.name);
    if (!ctx.allocation.registers[scratch]) return undefined;
    const helper = ctx.ensureSpatialHitHelper(mask.name, scratch);
    compileExpression(ctx, index);
    ctx.emitJump(0x53, "ПП", helper.label, `spatial hit ${mask.name}`, line);
    return {
      name: "spatial-hit-condition-helper",
      detail: `Tested bit_has() through the shared spatial hit helper at line ${line}.`,
    };
}

export function compileNearAnyHelperCondition(ctx: LoweringCtx, 
    condition: ConditionAst,
    falseLabel: string,
    line: number,
    _preloadedConstants: ReadonlySet<string>,
  ): boolean {
    const match = matchNearAnyHelperCondition(condition);
    if (match === undefined) return false;
    const key = nearAnyHelperKey(match.value, match.radius);
    const stats = ctx.nearAnyHelperStats.get(key);
    if (stats === undefined || stats.helperCost >= stats.ordinaryCost) return false;

    const helper = ctx.nearAnyHelper(match.value, match.radius, line);
    compileNearAnyMarginWithHelper(ctx, match, helper.label, line);
    const opcode = directTestOpcode(match.op);
    ctx.emitJump(opcode, getOpcode(opcode).name, falseLabel, `false branch for ${match.op}`, line);
    ctx.optimizations.push({
      name: "near-any-helper-lowering",
      detail: `Lowered ${conditionToText(condition)} through shared near_any helper at line ${line} (${stats.helperCost} vs ${stats.ordinaryCost} estimated group steps).`,
    });
    return true;
}

export function compileNearAnyMarginWithHelper(ctx: LoweringCtx, 
    match: NearAnyHelperConditionMatch,
    label: string,
    line: number,
  ): void {
    for (let index = 0; index < match.candidates.length; index += 1) {
      const candidate = match.candidates[index]!;
      compileNearAnyCandidate(ctx, candidate, line);
      ctx.emitJump(0x53, "ПП", label, "near_any candidate", line);
      if (index > 0) ctx.emitOp(0x36, "К max", "near_any max margin", line);
    }
}

export function compileNearAnyCandidate(ctx: LoweringCtx, candidate: ExpressionAst, line: number): void {
    if (candidate.kind === "identifier" && ctx.xHolds(candidate.name)) {
      ctx.optimizations.push({
        name: "stack-current-x-scheduling",
        detail: `Reused ${candidate.name} already in X for near_any candidate at line ${line}.`,
      });
      return;
    }
    compileExpression(ctx, candidate);
}

export function compileSmallSetCondition(ctx: LoweringCtx, 
    condition: ConditionAst,
    falseLabel: string,
    line: number,
    preloadedConstants: ReadonlySet<string>,
  ): boolean {
    const match = matchSmallSetCondition(condition);
    if (match === undefined) return false;
    const selectedCost = estimateSmallSetConditionCost(match, preloadedConstants);
    const ordinaryCost = conditionCompileCost(condition, preloadedConstants);
    if (selectedCost >= ordinaryCost) return false;
    if (match.mode === "any") {
      const trueLabel = ctx.freshLabel(`${match.kind}_any_true`);
      for (const item of match.tests.slice(0, -1)) {
        compileExpression(ctx, item.expr);
        ctx.emitJump(item.trueOpcode, getOpcode(item.trueOpcode).name, trueLabel, `${match.kind} any hit`, line);
      }
      const last = match.tests.at(-1)!;
      compileExpression(ctx, last.expr);
      ctx.emitJump(last.falseOpcode, getOpcode(last.falseOpcode).name, falseLabel, `${match.kind} any miss`, line);
      ctx.emitLabel(trueLabel);
    } else {
      for (const item of match.tests) {
        compileExpression(ctx, item.expr);
        ctx.emitJump(item.falseOpcode, getOpcode(item.falseOpcode).name, falseLabel, `${match.kind} all miss`, line);
      }
    }
    ctx.optimizations.push({
      name: "small-set-condition-lowering",
      detail: `Lowered ${conditionToText(condition)} as ${match.tests.length} direct small-set test${match.tests.length === 1 ? "" : "s"} at line ${line} (${selectedCost} vs ${ordinaryCost} estimated steps).`,
    });
    return true;
}
