import type { ExpressionAst, StatementAst } from "../types.ts";
import { compileExpression } from "./lowering/expr.ts";
import {
  binaryOpcode,
  expressionReferencesIdentifier,
  expressionToIntentText,
  countIdentifierReads,
  expressionPureForSubstitution,
} from "./lowering-helpers.ts";
import {
  canLowerStackResidentExpression,
  stackResidentRestoreOps,
} from "./stack-residency-analysis.ts";
import type { LoweringCtx } from "./lowering-ctx.ts";

const STACK_TEMP_UNARY_CALL_OPCODES: Readonly<Record<string, readonly [number, string]>> = {
  abs: [0x31, "К |x|"],
  sign: [0x32, "К ЗН"],
  int: [0x34, "К [x]"],
  frac: [0x35, "К {x}"],
  sqr: [0x22, "F x^2"],
  inv: [0x23, "F 1/x"],
  sqrt: [0x21, "F sqrt"],
  lg: [0x17, "F lg"],
  ln: [0x18, "F ln"],
  sin: [0x1c, "F sin"],
  cos: [0x1d, "F cos"],
  tg: [0x1e, "F tg"],
  asin: [0x19, "F sin^-1"],
  acos: [0x1a, "F cos^-1"],
  atg: [0x1b, "F tg^-1"],
  exp: [0x16, "F e^x"],
  pow10: [0x15, "F 10^x"],
  bit_not: [0x3a, "К ИНВ"],
  to_min: [0x26, "К °->′"],
  to_sec: [0x2a, "К °->′\""],
  from_sec: [0x30, "К °<-′\""],
  from_min: [0x33, "К °<-′"],
};

const STACK_TEMP_BINARY_CALL_OPCODES: Readonly<Record<string, readonly [number, string]>> = {
  max: [0x36, "К max"],
  bit_and: [0x37, "К ∧"],
  bit_or: [0x38, "К ∨"],
  bit_xor: [0x39, "К ⊕"],
};

function emitRestoreStackTemp(ctx: LoweringCtx, tempIndex: number, tempCount: number, line: number): void {
  for (const op of stackResidentRestoreOps(tempIndex, tempCount)) {
    if (op === "reverse") ctx.emitOp(0x25, "F reverse", "stack-resident temp restore", line);
    else ctx.emitOp(0x14, "X↔Y", "stack-resident temp restore", line);
  }
}

function emitSwapIfNonCommutativeLeft(ctx: LoweringCtx, op: string, line: number): void {
  if (op !== "+" && op !== "*") ctx.emitOp(0x14, "X↔Y", "stack-resident operand order", line);
}

/** After N temps are stacked, align Y=left and X=right for a Y op X binary. */
function emitStackBinaryOnTemps(
  ctx: LoweringCtx,
  leftName: string,
  rightName: string,
  op: string,
  temps: readonly string[],
  line: number,
): void {
  const leftIndex = temps.indexOf(leftName);
  const rightIndex = temps.indexOf(rightName);
  if (leftIndex < 0 || rightIndex < 0) return;
  if (temps.length === 2) {
    if (leftIndex === 1 && rightIndex === 0) ctx.emitOp(0x14, "X↔Y", "stack-resident operand order", line);
    ctx.emitOp(binaryOpcode(op), op, `expr ${op}`, line);
    return;
  }
  emitRestoreStackTemp(ctx, rightIndex, temps.length, line);
  emitRestoreStackTemp(ctx, leftIndex, temps.length, line);
  ctx.emitOp(0x14, "X↔Y", "stack-resident operand order", line);
  ctx.emitOp(binaryOpcode(op), op, `expr ${op}`, line);
}

function referencesAnyTemp(expr: ExpressionAst, temps: readonly string[]): boolean {
  return temps.some((temp) => countIdentifierReads(expr, temp) > 0);
}

function compileStackResidentExpression(
  ctx: LoweringCtx,
  expr: ExpressionAst,
  temps: readonly string[],
  line: number,
): void {
  switch (expr.kind) {
    case "identifier": {
      const index = temps.indexOf(expr.name);
      if (index >= 0) emitRestoreStackTemp(ctx, index, temps.length, line);
      else compileExpression(ctx, expr);
      return;
    }
    case "number":
    case "string":
    case "indexed":
      compileExpression(ctx, expr);
      return;
    case "unary":
      compileStackResidentExpression(ctx, expr.expr, temps, line);
      ctx.emitOp(0x0b, "/-/", "stack-resident unary minus", line);
      return;
    case "binary": {
      const leftTemps = referencesAnyTemp(expr.left, temps);
      const rightTemps = referencesAnyTemp(expr.right, temps);
      if (
        leftTemps &&
        rightTemps &&
        expr.left.kind === "identifier" &&
        expr.right.kind === "identifier" &&
        temps.includes(expr.left.name) &&
        temps.includes(expr.right.name)
      ) {
        emitStackBinaryOnTemps(ctx, expr.left.name, expr.right.name, expr.op, temps, line);
        return;
      }
      if (leftTemps && rightTemps) {
        compileStackResidentExpression(ctx, expr.left, temps, line);
        compileStackResidentExpression(ctx, expr.right, temps, line);
        emitSwapIfNonCommutativeLeft(ctx, expr.op, line);
        ctx.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`, line);
        return;
      }
      if (leftTemps) {
        compileStackResidentExpression(ctx, expr.left, temps, line);
        compileExpression(ctx, expr.right);
        ctx.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`, line);
        return;
      }
      compileStackResidentExpression(ctx, expr.right, temps, line);
      compileExpression(ctx, expr.left);
      emitSwapIfNonCommutativeLeft(ctx, expr.op, line);
      ctx.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`, line);
      return;
    }
    case "call": {
      const name = expr.callee.toLowerCase();
      const unary = STACK_TEMP_UNARY_CALL_OPCODES[name];
      if (unary !== undefined && expr.args.length === 1) {
        compileStackResidentExpression(ctx, expr.args[0]!, temps, line);
        ctx.emitOp(unary[0], unary[1], `${expr.callee}()`, line);
        return;
      }
      const binary = STACK_TEMP_BINARY_CALL_OPCODES[name];
      if (binary !== undefined && expr.args.length === 2) {
        const left = expr.args[0]!;
        const right = expr.args[1]!;
        const leftTemps = temps.some((temp) => countIdentifierReads(left, temp) > 0);
        const rightTemps = temps.some((temp) => countIdentifierReads(right, temp) > 0);
        if (leftTemps && rightTemps) {
          compileStackResidentExpression(ctx, left, temps, line);
          compileStackResidentExpression(ctx, right, temps, line);
        } else if (leftTemps) {
          compileStackResidentExpression(ctx, left, temps, line);
          compileExpression(ctx, right);
        } else {
          compileStackResidentExpression(ctx, right, temps, line);
          compileExpression(ctx, left);
          ctx.emitOp(0x14, "X↔Y", "stack-resident call operand order", line);
        }
        ctx.emitOp(binary[0], binary[1], `${expr.callee}()`, line);
        return;
      }
      compileExpression(ctx, expr);
      return;
    }
  }
}

function emitStackResidentConsumer(
  ctx: LoweringCtx,
  consumer: Extract<StatementAst, { kind: "assign" | "halt" | "pause" | "return_value" }>,
): void {
  switch (consumer.kind) {
    case "assign":
      ctx.emitStore(consumer.target, `set ${consumer.target}`, consumer.line);
      return;
    case "halt":
      ctx.emitOp(0x50, "С/П", "halt", consumer.line);
      return;
    case "pause":
      ctx.emitOp(0x50, "С/П", "pause", consumer.line);
      return;
    case "return_value":
      ctx.emitOp(0x52, "В/О", "return value", consumer.line);
      return;
  }
}

function stackTempValueDeadAfterConsumer(
  temp: string,
  overwrittenByConsumer: string | undefined,
  tail: readonly StatementAst[],
): boolean {
  if (overwrittenByConsumer === temp) return true;
  return !tail.some((statement) => {
    switch (statement.kind) {
      case "pause":
      case "halt":
        return expressionReferencesIdentifier(statement.expr, temp);
      case "assign":
        return expressionReferencesIdentifier(statement.expr, temp);
      case "indexed_assign":
        return (
          expressionReferencesIdentifier(statement.target.index, temp) ||
          expressionReferencesIdentifier(statement.expr, temp)
        );
      case "return_value":
        return expressionReferencesIdentifier(statement.expr, temp);
      default:
        return false;
    }
  });
}

/** Fuse consecutive single-use assign temps + combining consumer without register spills. */
export function compileMultiStackResidentTemps(
  ctx: LoweringCtx,
  statements: readonly StatementAst[],
  start: number,
): number {
  if (ctx.loweringOptions.stackResidentTemps !== true) return 0;

  const temps: Extract<StatementAst, { kind: "assign" }>[] = [];
  const targets = new Set<string>();
  for (let index = start; index < statements.length && temps.length < 4; index += 1) {
    const statement = statements[index]!;
    if (statement.kind !== "assign") break;
    if (!stackTempSourceIsSafe(statement)) break;
    if (targets.has(statement.target)) break;
    if (expressionReferencesIdentifier(statement.expr, statement.target)) break;
    if ([...targets].some((target) => expressionReferencesIdentifier(statement.expr, target))) break;
    targets.add(statement.target);
    temps.push(statement);
  }

  if (temps.length < 2) return 0;

  const consumer = statements[start + temps.length];
  if (
    consumer?.kind !== "assign" &&
    consumer?.kind !== "halt" &&
    consumer?.kind !== "pause" &&
    consumer?.kind !== "return_value"
  ) {
    return 0;
  }

  const tail = statements.slice(start + temps.length + 1);
  const overwrite = consumer.kind === "assign" ? consumer.target : undefined;
  for (const temp of temps) {
    if (!stackTempValueDeadAfterConsumer(temp.target, overwrite, tail)) return 0;
  }

  const tempNames = temps.map((temp) => temp.target);
  if (!canLowerStackResidentExpression(consumer.expr, tempNames)) return 0;

  for (let index = 0; index < temps.length; index += 1) {
    if (index > 0) ctx.emitOp(0x0e, "В↑", `stack-resident lift ${temps[index - 1]!.target}`, temps[index]!.line);
    compileExpression(ctx, temps[index]!.expr);
    ctx.markCurrentX(temps[index]!.target);
  }

  compileStackResidentExpression(ctx, consumer.expr, tempNames, consumer.line);
  emitStackResidentConsumer(ctx, consumer);

  ctx.optimizations.push({
    name: "stack-resident-temps",
    detail: `Kept ${tempNames.join(", ")} on the X/Y/Z/T stack across ${expressionToIntentText(consumer.expr)} at line ${consumer.line}.`,
  });
  return temps.length + 1;
}

function stackTempSourceIsSafe(statement: Extract<StatementAst, { kind: "assign" }>): boolean {
  if (!expressionPureForSubstitution(statement.expr)) return false;
  return statement.expr.kind !== "call";
}
