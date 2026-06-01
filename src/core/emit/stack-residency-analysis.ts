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

export interface StackResidentTempSegment {
  assign: Extract<StatementAst, { kind: "assign" }>;
  /** Statements after this assign and before the next assign or consumer. */
  preserveAfter: readonly StatementAst[];
}

export interface StackResidentFusionSite {
  temps: readonly StackResidentTempSegment[];
  consumer: Extract<
    StatementAst,
    { kind: "assign" | "halt" | "pause" | "return_value" | "indexed_assign" }
  >;
  consumerIndex: number;
  /** True when any preserveAfter segment is non-empty. */
  crossesControlFlow: boolean;
}

export function stackTempSourceIsSafe(expr: ExpressionAst): boolean {
  if (!expressionIsDeterministic(expr)) return false;
  if (expr.kind === "call") return false;
  return expressionPureForSubstitution(expr);
}

export function statementsReadIdentifier(statements: readonly StatementAst[], name: string): boolean {
  return statements.some((statement) => statementReadsIdentifier(statement, name));
}

export function statementReadsIdentifier(statement: StatementAst, name: string): boolean {
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

function conditionReferencesProtected(
  condition: Extract<StatementAst, { kind: "if" }>["condition"],
  protectedTemps: ReadonlySet<string>,
): boolean {
  for (const temp of protectedTemps) {
    if (
      expressionReferencesIdentifier(condition.left, temp) ||
      expressionReferencesIdentifier(condition.right, temp)
    ) {
      return true;
    }
  }
  return false;
}

function statementsPreserveStackResidencyBlock(
  statements: readonly StatementAst[],
  protectedTemps: ReadonlySet<string>,
): boolean {
  return statements.every((statement) => statementPreservesStackResidency(statement, protectedTemps));
}

/** True when lowering `statement` leaves the calculator stack layout intact for `protectedTemps`. */
export function statementPreservesStackResidency(
  statement: StatementAst,
  protectedTemps: ReadonlySet<string>,
): boolean {
  for (const temp of protectedTemps) {
    if (statementReadsIdentifier(statement, temp)) return false;
  }
  switch (statement.kind) {
    case "assign":
    case "indexed_assign":
    case "show":
    case "halt":
    case "pause":
    case "input":
    case "return_value":
    case "call":
    case "core":
    case "decimal_series":
    case "coord_list_remove":
      return false;
    case "if":
      if (conditionReferencesProtected(statement.condition, protectedTemps)) return false;
      return (
        statementsPreserveStackResidencyBlock(statement.thenBody, protectedTemps) &&
        (statement.elseBody === undefined ||
          statementsPreserveStackResidencyBlock(statement.elseBody, protectedTemps))
      );
    case "loop":
      return statementsPreserveStackResidencyBlock(statement.body, protectedTemps);
    case "while":
      if (conditionReferencesProtected(statement.condition, protectedTemps)) return false;
      return statementsPreserveStackResidencyBlock(statement.body, protectedTemps);
    case "dispatch":
      for (const temp of protectedTemps) {
        if (expressionReferencesIdentifier(statement.expr, temp)) return false;
      }
      return (
        statement.cases.every((dispatchCase) => {
          for (const temp of protectedTemps) {
            if (expressionReferencesIdentifier(dispatchCase.value, temp)) return false;
          }
          return statementsPreserveStackResidencyBlock(dispatchCase.body, protectedTemps);
        }) &&
        (statement.defaultBody === undefined ||
          statementsPreserveStackResidencyBlock(statement.defaultBody, protectedTemps))
      );
  }
}

function isStackResidentConsumer(statement: StatementAst | undefined): statement is StackResidentFusionSite["consumer"] {
  if (statement === undefined) return false;
  return (
    statement.kind === "assign" ||
    statement.kind === "halt" ||
    statement.kind === "pause" ||
    statement.kind === "return_value" ||
    statement.kind === "indexed_assign"
  );
}

function consumerOverwriteTarget(
  consumer: StackResidentFusionSite["consumer"],
): string | undefined {
  return consumer.kind === "assign" ? consumer.target : undefined;
}

function stackTempValueDeadAfterConsumer(
  temp: string,
  overwrittenByConsumer: string | undefined,
  tail: readonly StatementAst[],
): boolean {
  return overwrittenByConsumer === temp || !statementsReadIdentifier(tail, temp);
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

function assignTempIsSafe(statement: Extract<StatementAst, { kind: "assign" }>, targets: ReadonlySet<string>): boolean {
  if (!stackTempSourceIsSafe(statement.expr)) return false;
  if (targets.has(statement.target)) return false;
  if (expressionReferencesIdentifier(statement.expr, statement.target)) return false;
  if ([...targets].some((target) => expressionReferencesIdentifier(statement.expr, target))) return false;
  return true;
}

/** Find a stack-resident fusion site starting at `start`, including control-flow gaps. */
export function findStackResidentFusionSite(
  statements: readonly StatementAst[],
  start: number,
): StackResidentFusionSite | undefined {
  const segments: StackResidentTempSegment[] = [];
  const targets = new Set<string>();
  let index = start;

  while (index < statements.length && segments.length < 4) {
    const statement = statements[index];
    if (statement?.kind !== "assign" || !assignTempIsSafe(statement, targets)) break;

    targets.add(statement.target);
    index += 1;
    const protectedTemps = new Set(targets);
    const preserveStart = index;
    while (index < statements.length && statementPreservesStackResidency(statements[index]!, protectedTemps)) {
      index += 1;
    }
    segments.push({
      assign: statement,
      preserveAfter: statements.slice(preserveStart, index),
    });

    if (index >= statements.length) return undefined;
    if (statements[index]!.kind === "assign" && segments.length < 4) continue;
    break;
  }

  if (segments.length === 0) return undefined;

  const consumer = statements[index];
  if (!isStackResidentConsumer(consumer)) return undefined;

  const tempNames = segments.map((segment) => segment.assign.target);
  const tail = statements.slice(index + 1);
  const overwrite = consumerOverwriteTarget(consumer);
  for (const name of tempNames) {
    if (!stackTempValueDeadAfterConsumer(name, overwrite, tail)) return undefined;
  }
  if (!canLowerStackResidentExpression(consumer.expr, tempNames)) return undefined;

  const crossesControlFlow = segments.some((segment) => segment.preserveAfter.length > 0);
  if (segments.length === 1 && !crossesControlFlow) return undefined;

  return {
    temps: segments,
    consumer,
    consumerIndex: index,
    crossesControlFlow,
  };
}

function countIndexedConsumer(statements: readonly StatementAst[], start: number): number {
  const temp = statements[start];
  const consumer = statements[start + 1];
  if (temp?.kind !== "assign" || consumer?.kind !== "indexed_assign") return 0;
  if (findStackResidentFusionSite(statements, start) !== undefined) return 0;
  if (!stackTempSourceIsSafe(temp.expr)) return 0;
  if (expressionReferencesIdentifier(temp.expr, temp.target)) return 0;
  const tail = statements.slice(start + 2);
  if (!stackTempValueDeadAfterConsumer(temp.target, undefined, tail)) return 0;
  if (countIdentifierReads(consumer.expr, temp.target) !== 1) return 0;
  if (!canLowerStackResidentExpression(consumer.expr, [temp.target])) return 0;
  return 1;
}

function countStraightSingleUsePair(statements: readonly StatementAst[], start: number): number {
  const temp = statements[start];
  const consumer = statements[start + 1];
  if (temp?.kind !== "assign" || !isStackResidentConsumer(consumer)) return 0;
  if (findStackResidentFusionSite(statements, start) !== undefined) return 0;
  if (!stackTempSourceIsSafe(temp.expr)) return 0;
  if (expressionReferencesIdentifier(temp.expr, temp.target)) return 0;
  const tail = statements.slice(start + 2);
  if (!stackTempValueDeadAfterConsumer(temp.target, consumerOverwriteTarget(consumer), tail)) return 0;
  if (countIdentifierReads(consumer.expr, temp.target) !== 1) return 0;
  if (!canLowerStackResidentExpression(consumer.expr, [temp.target])) return 0;
  return 1;
}

function peakLiveAssignTempsInBlock(statements: readonly StatementAst[]): number {
  const defs = new Map<string, number>();
  const lastUse = new Map<string, number>();
  const indexStatement = (statement: StatementAst, index: number): void => {
    if (statement.kind === "assign") defs.set(statement.target, index);
    for (const target of defs.keys()) {
      if (statementReadsIdentifier(statement, target)) lastUse.set(target, index);
    }
    switch (statement.kind) {
      case "if":
        statement.thenBody.forEach((child, offset) => indexStatement(child, index * 1000 + offset));
        statement.elseBody?.forEach((child, offset) => indexStatement(child, index * 1000 + 100 + offset));
        break;
      case "loop":
        statement.body.forEach((child, offset) => indexStatement(child, index * 1000 + offset));
        break;
      case "while":
        statement.body.forEach((child, offset) => indexStatement(child, index * 1000 + offset));
        break;
      case "dispatch":
        for (const dispatchCase of statement.cases) {
          dispatchCase.body.forEach((child, offset) => indexStatement(child, index * 1000 + offset));
        }
        statement.defaultBody?.forEach((child, offset) => indexStatement(child, index * 1000 + 200 + offset));
        break;
      default:
        break;
    }
  };
  statements.forEach((statement, index) => indexStatement(statement, index));
  return defs.size === 0 ? 0 : lastUse.size;
}

export interface StackResidencySummary {
  /** Peak count of assign targets live at once. */
  maxLiveTemps: number;
  /** `t0=e0; …; consumer` fusion sites (straight-line or control-flow). */
  fusionSites: number;
  /** Subset of `fusionSites` whose temps are separated by stack-preserving control flow. */
  controlFlowFusions: number;
  /** Straight-line `temp = e; consumer` pairs not already covered by a fusion site. */
  singleUsePairs: number;
  /** `temp = e; cells[i] op= temp` indexed compound consumers. */
  indexedConsumers: number;
}

/** Summarize stack-residency candidates across a whole statement tree (control-flow aware). */
export function summarizeStackResidencyCandidatesInBlock(statements: readonly StatementAst[]): StackResidencySummary {
  let fusionSites = 0;
  let controlFlowFusions = 0;
  let indexedConsumers = 0;
  let singleUsePairs = 0;

  const visit = (body: readonly StatementAst[]): void => {
    for (let index = 0; index < body.length; index += 1) {
      const site = findStackResidentFusionSite(body, index);
      if (site !== undefined) {
        fusionSites += 1;
        if (site.crossesControlFlow) controlFlowFusions += 1;
        index = site.consumerIndex;
        continue;
      }
      indexedConsumers += countIndexedConsumer(body, index);
      singleUsePairs += countStraightSingleUsePair(body, index);
      const statement = body[index]!;
      switch (statement.kind) {
        case "if":
          if (statement.elseBody) visit(statement.elseBody);
          visit(statement.thenBody);
          break;
        case "loop":
          visit(statement.body);
          break;
        case "while":
          visit(statement.body);
          break;
        case "dispatch":
          for (const dispatchCase of statement.cases) visit(dispatchCase.body);
          if (statement.defaultBody) visit(statement.defaultBody);
          break;
        default:
          break;
      }
    }
  };

  visit(statements);
  return {
    maxLiveTemps: peakLiveAssignTempsInBlock(statements),
    fusionSites,
    controlFlowFusions,
    singleUsePairs,
    indexedConsumers,
  };
}

function mergeSummaries(left: StackResidencySummary, right: StackResidencySummary): StackResidencySummary {
  return {
    maxLiveTemps: Math.max(left.maxLiveTemps, right.maxLiveTemps),
    fusionSites: left.fusionSites + right.fusionSites,
    controlFlowFusions: left.controlFlowFusions + right.controlFlowFusions,
    singleUsePairs: left.singleUsePairs + right.singleUsePairs,
    indexedConsumers: left.indexedConsumers + right.indexedConsumers,
  };
}

/** Analyze every entry/proc body in a parsed program. */
export function analyzeProgramStackResidency(ast: ProgramAst): StackResidencySummary {
  let summary: StackResidencySummary = {
    maxLiveTemps: 0,
    fusionSites: 0,
    controlFlowFusions: 0,
    singleUsePairs: 0,
    indexedConsumers: 0,
  };
  for (const entry of ast.entries) {
    summary = mergeSummaries(summary, summarizeStackResidencyCandidatesInBlock(entry.body));
  }
  for (const proc of ast.procs) {
    summary = mergeSummaries(summary, summarizeStackResidencyCandidatesInBlock(proc.body));
  }
  return summary;
}

/** Parse source and return stack-residency candidate counts (measurement harness). */
export function analyzeSourceStackResidency(source: string): StackResidencySummary {
  return analyzeProgramStackResidency(parseProgram(source));
}
