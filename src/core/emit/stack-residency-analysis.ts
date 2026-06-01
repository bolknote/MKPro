import type { ExpressionAst, ProgramAst, StatementAst } from "../types.ts";
import { parseProgram } from "../parser.ts";
import {
  countIdentifierReads,
  expressionIsDeterministic,
  expressionReferencesIdentifier,
  expressionPureForSubstitution,
  isSimpleStackLoad,
} from "./lowering-helpers.ts";

/** Stack slot names after N consecutive temps are lifted with В↑ between each. */
export type StackResidentSlot = "X" | "Y" | "Z" | "T";

export interface StackResidencyWindow {
  /** Inclusive start index in the statement array. */
  start: number;
  /** Exclusive end index. */
  end: number;
  /** Peak count of assign targets live at once inside the window. */
  maxLiveTemps: number;
  /** `temp = e; consumer` pairs eligible for single-use stack residency. */
  singleUsePairs: number;
  /** `t0=e0; t1=e1; consumer` triples eligible for dual stack residency. */
  dualTempTriples: number;
  /** `t0..t(n-1); consumer` runs with 3 or 4 temps and a qualifying consumer. */
  multiTempRuns: number;
  /** `temp = e; cells[i] op= temp` indexed compound consumers. */
  indexedConsumers: number;
}

/** Statements that end a straight-line stack-scheduling window. */
export function breaksStackResidencyWindow(statement: StatementAst): boolean {
  switch (statement.kind) {
    case "assign":
    case "indexed_assign":
    case "pause":
    case "halt":
    case "return_value":
    case "show":
    case "input":
      return false;
    case "call":
    case "if":
    case "loop":
    case "while":
    case "dispatch":
    case "coord_list_remove":
    case "decimal_series":
    case "core":
      return true;
  }
}

function stackTempSourceIsSafe(expr: ExpressionAst): boolean {
  if (!expressionIsDeterministic(expr)) return false;
  if (expr.kind === "call") return false;
  return expressionPureForSubstitution(expr);
}

function statementsReadIdentifier(statements: readonly StatementAst[], name: string): boolean {
  return statements.some((statement) => statementReadsIdentifier(statement, name));
}

function statementReadsIdentifier(statement: StatementAst, name: string): boolean {
  switch (statement.kind) {
    case "pause":
    case "halt":
      return expressionReferencesIdentifier(statement.expr, name);
    case "assign":
      return expressionReferencesIdentifier(statement.expr, name);
    case "indexed_assign":
      return (
        expressionReferencesIdentifier(statement.target.index, name) ||
        expressionReferencesIdentifier(statement.expr, name)
      );
    case "coord_list_remove":
      return expressionReferencesIdentifier(statement.item, name);
    case "if":
      return (
        expressionReferencesIdentifier(statement.condition.left, name) ||
        expressionReferencesIdentifier(statement.condition.right, name) ||
        statementsReadIdentifier(statement.thenBody, name) ||
        (statement.elseBody !== undefined && statementsReadIdentifier(statement.elseBody, name))
      );
    case "loop":
      return statementsReadIdentifier(statement.body, name);
    case "while":
      return (
        expressionReferencesIdentifier(statement.condition.left, name) ||
        expressionReferencesIdentifier(statement.condition.right, name) ||
        statementsReadIdentifier(statement.body, name)
      );
    case "dispatch":
      return (
        expressionReferencesIdentifier(statement.expr, name) ||
        statement.cases.some(
          (dispatchCase) =>
            expressionReferencesIdentifier(dispatchCase.value, name) ||
            statementsReadIdentifier(dispatchCase.body, name),
        ) ||
        (statement.defaultBody !== undefined && statementsReadIdentifier(statement.defaultBody, name))
      );
    case "show":
    case "input":
    case "call":
    case "decimal_series":
      return false;
    case "core":
      return statement.inputs?.some((input) => expressionReferencesIdentifier(input.expr, name)) ?? false;
    case "return_value":
      return expressionReferencesIdentifier(statement.expr, name);
  }
}

function stackTempValueDeadAfterConsumer(
  temp: string,
  overwrittenByConsumer: string | undefined,
  tail: readonly StatementAst[],
): boolean {
  return overwrittenByConsumer === temp || !statementsReadIdentifier(tail, temp);
}

function isStackResidentConsumer(statement: StatementAst | undefined): statement is
  | Extract<StatementAst, { kind: "assign" }>
  | Extract<StatementAst, { kind: "halt" }>
  | Extract<StatementAst, { kind: "pause" }>
  | Extract<StatementAst, { kind: "return_value" }>
  | Extract<StatementAst, { kind: "indexed_assign" }> {
  if (statement === undefined) return false;
  return (
    statement.kind === "assign" ||
    statement.kind === "halt" ||
    statement.kind === "pause" ||
    statement.kind === "return_value" ||
    statement.kind === "indexed_assign"
  );
}

function consumerExpr(statement: ReturnType<typeof isStackResidentConsumer> extends true ? StatementAst : never): ExpressionAst {
  return statement.expr;
}

function consumerOverwriteTarget(statement: ReturnType<typeof isStackResidentConsumer> extends true ? StatementAst : never): string | undefined {
  return statement.kind === "assign" ? statement.target : undefined;
}

/** Slot depth from X: temp[0] is deepest, temp[n-1] sits in X after emission. */
export function stackResidentSlotForTemp(tempIndex: number, tempCount: number): StackResidentSlot {
  const depthFromX = tempCount - 1 - tempIndex;
  if (depthFromX === 0) return "X";
  if (depthFromX === 1) return "Y";
  if (depthFromX === 2) return "Z";
  return "T";
}

/** Minimal reorder sequence to bring temp[tempIndex] into X (see z-stack-derived tail). */
export function stackResidentRestoreOps(tempIndex: number, tempCount: number): ReadonlyArray<"swap" | "reverse"> {
  const slot = stackResidentSlotForTemp(tempIndex, tempCount);
  if (slot === "X") return [];
  if (slot === "Y") return ["swap"];
  if (slot === "Z") return ["reverse", "swap"];
  return ["reverse", "reverse", "swap"];
}

function stackTempOtherOperandIsSafe(expr: ExpressionAst): boolean {
  if (expr.kind === "call") return false;
  return expressionPureForSubstitution(expr);
}

/** True when `expr` can be lowered with temps[] already resident on the stack. */
export function canLowerStackResidentExpression(expr: ExpressionAst, temps: readonly string[]): boolean {
  for (const temp of temps) {
    if (countIdentifierReads(expr, temp) !== 1) return false;
  }
  return validateStackResidentExpression(expr, temps);
}

function validateStackResidentExpression(expr: ExpressionAst, temps: readonly string[]): boolean {
  switch (expr.kind) {
    case "number":
    case "string":
      return true;
    case "identifier":
      return temps.includes(expr.name) || isSimpleStackLoad(expr);
    case "indexed":
      return isSimpleStackLoad(expr);
    case "unary":
      return validateStackResidentExpression(expr.expr, temps);
    case "binary": {
      const leftRefsTemp = temps.some((temp) => countIdentifierReads(expr.left, temp) > 0);
      const rightRefsTemp = temps.some((temp) => countIdentifierReads(expr.right, temp) > 0);
      if (leftRefsTemp && rightRefsTemp) {
        return (
          validateStackResidentExpression(expr.left, temps) &&
          validateStackResidentExpression(expr.right, temps)
        );
      }
      if (leftRefsTemp) {
        return validateStackResidentExpression(expr.left, temps) && stackTempOtherOperandIsSafe(expr.right);
      }
      if (rightRefsTemp) {
        return (
          validateStackResidentExpression(expr.right, temps) &&
          stackTempOtherOperandIsSafe(expr.left) &&
          (expr.op === "+" || expr.op === "*" || isSimpleStackLoad(expr.left))
        );
      }
      return stackTempOtherOperandIsSafe(expr.left) && stackTempOtherOperandIsSafe(expr.right);
    }
    case "call": {
      if (expr.args.length === 0) return false;
      return expr.args.every((arg) => validateStackResidentExpression(arg, temps));
    }
  }
}

function collectConsecutiveAssignTemps(
  statements: readonly StatementAst[],
  start: number,
  maxTemps: number,
): Extract<StatementAst, { kind: "assign" }>[] {
  const temps: Extract<StatementAst, { kind: "assign" }>[] = [];
  const targets = new Set<string>();
  for (let index = start; index < statements.length && temps.length < maxTemps; index += 1) {
    const statement = statements[index]!;
    if (statement.kind !== "assign") break;
    if (!stackTempSourceIsSafe(statement.expr)) break;
    if (targets.has(statement.target)) break;
    if (expressionReferencesIdentifier(statement.expr, statement.target)) break;
    if ([...targets].some((target) => expressionReferencesIdentifier(statement.expr, target))) break;
    targets.add(statement.target);
    temps.push(statement);
  }
  return temps;
}

function countMultiStackResidentRun(
  statements: readonly StatementAst[],
  start: number,
  tempCount: number,
): number {
  const temps = collectConsecutiveAssignTemps(statements, start, tempCount);
  if (temps.length !== tempCount) return 0;
  const consumer = statements[start + tempCount];
  if (!isStackResidentConsumer(consumer)) return 0;
  const tail = statements.slice(start + tempCount + 1);
  for (const temp of temps) {
    if (!stackTempValueDeadAfterConsumer(temp.target, consumerOverwriteTarget(consumer), tail)) return 0;
  }
  const expr = consumerExpr(consumer);
  const names = temps.map((temp) => temp.target);
  if (!canLowerStackResidentExpression(expr, names)) return 0;
  return 1;
}

function countSingleUsePair(statements: readonly StatementAst[], start: number): number {
  const temp = statements[start];
  const consumer = statements[start + 1];
  if (temp?.kind !== "assign" || !isStackResidentConsumer(consumer)) return 0;
  if (!stackTempSourceIsSafe(temp.expr)) return 0;
  if (expressionReferencesIdentifier(temp.expr, temp.target)) return 0;
  const tail = statements.slice(start + 2);
  if (!stackTempValueDeadAfterConsumer(temp.target, consumerOverwriteTarget(consumer), tail)) return 0;
  const expr = consumerExpr(consumer);
  if (countIdentifierReads(expr, temp.target) !== 1) return 0;
  if (!canLowerStackResidentExpression(expr, [temp.target])) return 0;
  return 1;
}

function countIndexedConsumer(statements: readonly StatementAst[], start: number): number {
  const temp = statements[start];
  const consumer = statements[start + 1];
  if (temp?.kind !== "assign" || consumer?.kind !== "indexed_assign") return 0;
  return countSingleUsePair(statements, start) > 0 ? 1 : 0;
}

function peakLiveAssignTemps(statements: readonly StatementAst[], start: number, end: number): number {
  const slice = statements.slice(start, end);
  const defs = new Map<string, number>();
  const lastUse = new Map<string, number>();
  for (let index = 0; index < slice.length; index += 1) {
    const statement = slice[index]!;
    if (statement.kind === "assign") defs.set(statement.target, index);
    for (const target of defs.keys()) {
      if (statementReadsIdentifier(statement, target)) lastUse.set(target, index);
    }
  }
  let peak = 0;
  for (let index = 0; index < slice.length; index += 1) {
    let live = 0;
    for (const [target, def] of defs) {
      const last = lastUse.get(target) ?? def;
      if (def <= index && index <= last) live += 1;
    }
    peak = Math.max(peak, live);
  }
  return peak;
}

/** Split `statements` into maximal straight-line stack-scheduling windows. */
export function analyzeStackResidencyWindows(statements: readonly StatementAst[]): StackResidencyWindow[] {
  const windows: StackResidencyWindow[] = [];
  let start = 0;
  while (start < statements.length) {
    let end = start;
    while (end < statements.length && !breaksStackResidencyWindow(statements[end]!)) end += 1;
    if (end === start) {
      start += 1;
      continue;
    }
    let singleUsePairs = 0;
    let dualTempTriples = 0;
    let multiTempRuns = 0;
    let indexedConsumers = 0;
    for (let index = start; index < end; index += 1) {
      singleUsePairs += countSingleUsePair(statements, index);
      dualTempTriples += countMultiStackResidentRun(statements, index, 2);
      indexedConsumers += countIndexedConsumer(statements, index);
      for (const n of [3, 4] as const) {
        multiTempRuns += countMultiStackResidentRun(statements, index, n);
      }
    }
    windows.push({
      start,
      end,
      maxLiveTemps: peakLiveAssignTemps(statements, start, end),
      singleUsePairs,
      dualTempTriples,
      multiTempRuns,
      indexedConsumers,
    });
    start = end === start ? start + 1 : end;
  }
  return windows;
}

/** Summarize stack-residency candidate counts for a whole program body. */
export function summarizeStackResidencyCandidates(statements: readonly StatementAst[]): {
  windows: number;
  maxLiveTemps: number;
  singleUsePairs: number;
  dualTempTriples: number;
  multiTempRuns: number;
  indexedConsumers: number;
} {
  const analyzed = analyzeStackResidencyWindows(statements);
  return {
    windows: analyzed.length,
    maxLiveTemps: analyzed.reduce((max, window) => Math.max(max, window.maxLiveTemps), 0),
    singleUsePairs: analyzed.reduce((sum, window) => sum + window.singleUsePairs, 0),
    dualTempTriples: analyzed.reduce((sum, window) => sum + window.dualTempTriples, 0),
    multiTempRuns: analyzed.reduce((sum, window) => sum + window.multiTempRuns, 0),
    indexedConsumers: analyzed.reduce((sum, window) => sum + window.indexedConsumers, 0),
  };
}

export interface ProgramStackResidencySummary {
  windows: number;
  maxLiveTemps: number;
  singleUsePairs: number;
  dualTempTriples: number;
  multiTempRuns: number;
  indexedConsumers: number;
}

function mergeSummaries(
  left: ProgramStackResidencySummary,
  right: ReturnType<typeof summarizeStackResidencyCandidates>,
): ProgramStackResidencySummary {
  return {
    windows: left.windows + right.windows,
    maxLiveTemps: Math.max(left.maxLiveTemps, right.maxLiveTemps),
    singleUsePairs: left.singleUsePairs + right.singleUsePairs,
    dualTempTriples: left.dualTempTriples + right.dualTempTriples,
    multiTempRuns: left.multiTempRuns + right.multiTempRuns,
    indexedConsumers: left.indexedConsumers + right.indexedConsumers,
  };
}

/** Analyze every entry/proc body in a parsed program. */
export function analyzeProgramStackResidency(ast: ProgramAst): ProgramStackResidencySummary {
  let summary: ProgramStackResidencySummary = {
    windows: 0,
    maxLiveTemps: 0,
    singleUsePairs: 0,
    dualTempTriples: 0,
    multiTempRuns: 0,
    indexedConsumers: 0,
  };
  for (const entry of ast.entries) {
    summary = mergeSummaries(summary, summarizeStackResidencyCandidates(entry.body));
  }
  for (const proc of ast.procs) {
    summary = mergeSummaries(summary, summarizeStackResidencyCandidates(proc.body));
  }
  return summary;
}

/** Parse source and return stack-residency candidate counts (measurement harness). */
export function analyzeSourceStackResidency(source: string): ProgramStackResidencySummary {
  return analyzeProgramStackResidency(parseProgram(source));
}
