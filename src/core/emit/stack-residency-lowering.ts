import type { StatementAst } from "../types.ts";
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
  findStackResidentFusionSite,
  stackResidentRestoreOps,
  type StackResidentFusionSite,
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
  op: "+" | "-" | "*" | "/",
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

function referencesAnyTemp(expr: import("../types.ts").ExpressionAst, temps: readonly string[]): boolean {
  return temps.some((temp) => countIdentifierReads(expr, temp) > 0);
}

function compileStackResidentExpression(
  ctx: LoweringCtx,
  expr: import("../types.ts").ExpressionAst,
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

function compilePreserveRegion(ctx: LoweringCtx, statements: readonly StatementAst[]): void {
  for (const statement of statements) ctx.compileStatement(statement);
}

// Two distinct notions of "preserve" meet here:
//   - the ANALYSIS (`statementPreservesStackResidency`) guarantees the gap never
//     destroys the *value* of a temp (no assign to its source can appear inside),
//   - the LOWERING (this predicate) asks whether the gap destroys the *stack
//     layout* (X/Y/Z/T) so the temps must be recomputed afterwards.
// A gap can preserve residency yet still require a rebuild: any condition test
// (`if`/`while`/`dispatch`) lifts onto the stack while comparing. Only nested
// empty `loop`s leave the layout untouched.
function preserveRegionRequiresStackRebuild(statements: readonly StatementAst[]): boolean {
  return statements.some((statement) => statementRequiresStackRebuild(statement));
}

function statementRequiresStackRebuild(statement: StatementAst): boolean {
  switch (statement.kind) {
    case "if":
    case "while":
    case "dispatch":
      return true;
    case "loop":
      return statement.body.some(statementRequiresStackRebuild);
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
      return true;
  }
}

function emitStackResidentFusion(ctx: LoweringCtx, site: StackResidentFusionSite): void {
  const tempNames = site.temps.map((segment) => segment.assign.target);

  for (let index = 0; index < site.temps.length; index += 1) {
    const segment = site.temps[index]!;
    if (index > 0) {
      ctx.emitOp(0x0e, "В↑", `stack-resident lift ${site.temps[index - 1]!.assign.target}`, segment.assign.line);
    }
    compileExpression(ctx, segment.assign.expr);
    ctx.markCurrentX(segment.assign.target);
    if (segment.preserveAfter.length > 0) {
      compilePreserveRegion(ctx, segment.preserveAfter);
      if (preserveRegionRequiresStackRebuild(segment.preserveAfter)) {
        for (let rebuild = 0; rebuild <= index; rebuild += 1) {
          if (rebuild > 0) {
            ctx.emitOp(0x0e, "В↑", `stack-resident rebuild ${site.temps[rebuild - 1]!.assign.target}`, segment.assign.line);
          }
          compileExpression(ctx, site.temps[rebuild]!.assign.expr);
          ctx.markCurrentX(site.temps[rebuild]!.assign.target);
        }
      }
    }
  }

  if (site.consumer.kind === "indexed_assign") {
    compileStackResidentExpression(ctx, site.consumer.expr, tempNames, site.consumer.line);
    ctx.emitIndexedStore(site.consumer.target, site.consumer.line);
  } else {
    compileStackResidentExpression(ctx, site.consumer.expr, tempNames, site.consumer.line);
    emitStackResidentConsumer(ctx, site.consumer);
  }

  const optimizationName = site.crossesControlFlow ? "stack-resident-control-flow" : "stack-resident-temps";
  const gapText = site.crossesControlFlow ? " across stack-preserving control flow" : "";
  ctx.optimizations.push({
    name: optimizationName,
    detail: `Kept ${tempNames.join(", ")} on the X/Y/Z/T stack${gapText} for ${expressionToIntentText(site.consumer.expr)} at line ${site.consumer.line}.`,
  });
}

/** Fuse assign temps + stack-preserving control-flow gaps + combining consumer. */
export function compileMultiStackResidentTemps(
  ctx: LoweringCtx,
  statements: readonly StatementAst[],
  start: number,
): number {
  if (ctx.loweringOptions.stackResidentTemps !== true) return 0;

  const site = findStackResidentFusionSite(statements, start);
  if (site === undefined) return 0;

  const consumer = site.consumer;
  if (
    consumer.kind === "indexed_assign" &&
    site.temps.some((segment) => expressionReferencesIdentifier(consumer.target.index, segment.assign.target))
  ) {
    return 0;
  }

  emitStackResidentFusion(ctx, site);
  return site.consumerIndex - start + 1;
}
