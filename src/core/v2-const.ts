import { foldConstExpression } from "./constant-folder.ts";
import { normalizeConstantLiteral } from "./emit/lowering-helpers.ts";
import { ParseError, parseExpression } from "./parser.ts";
import type { ExpressionAst, V2ConstAst, V2ProgramAst, V2StatementAst } from "./types.ts";

export interface V2ConstBinding {
  readonly name: string;
  readonly value: ExpressionAst;
  readonly normalized: string;
  readonly line: number;
}

export interface V2ConstContext {
  readonly bindings: ReadonlyMap<string, V2ConstBinding>;
}

export function buildV2ConstContext(v2: V2ProgramAst): V2ConstContext {
  const bindings = new Map<string, V2ConstBinding>();
  const reserved = collectV2StateNames(v2);

  for (const decl of v2.consts) {
    if (reserved.has(decl.name)) {
      throw new ParseError(`Const '${decl.name}' shadows state field '${decl.name}'`, decl.line);
    }
    if (bindings.has(decl.name)) {
      throw new ParseError(`Duplicate const '${decl.name}'`, decl.line);
    }
    const partial = new Map<string, V2ConstBinding>();
    for (const [name, binding] of bindings) partial.set(name, binding);
    const value = evaluateV2ConstExpression(decl.expr, decl.line, partial);
    const normalized = normalizeConstantLiteral(value.raw);
    const binding: V2ConstBinding = { name: decl.name, value, normalized, line: decl.line };
    bindings.set(decl.name, binding);
  }

  assertNoConstAssignment(v2, bindings);
  return { bindings };
}

function evaluateV2ConstExpression(
  text: string,
  line: number,
  earlier: ReadonlyMap<string, V2ConstBinding>,
): ExpressionAst {
  let expr = parseExpression(text.trim(), line);
  expr = inlineConstIdentifiers(expr, earlier);
  expr = foldConstExpression(expr);
  if (expr.kind !== "number") {
    throw new ParseError(
      `Const value must be a compile-time number expression, got '${text.trim()}'`,
      line,
    );
  }
  if (!expressionAllowedInConst(expr)) {
    throw new ParseError(
      `Const value must use only numeric literals and + - * / operators`,
      line,
    );
  }
  return expr;
}

function expressionAllowedInConst(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "number":
      return true;
    case "unary":
      return (expr.op === "-" || expr.op === "+") && expressionAllowedInConst(expr.expr);
    case "binary":
      return (
        (expr.op === "+" || expr.op === "-" || expr.op === "*" || expr.op === "/") &&
        expressionAllowedInConst(expr.left) &&
        expressionAllowedInConst(expr.right)
      );
    default:
      return false;
  }
}

export function inlineConstIdentifiers(
  expr: ExpressionAst,
  consts: ReadonlyMap<string, V2ConstBinding>,
): ExpressionAst {
  switch (expr.kind) {
    case "number":
    case "string":
      return expr;
    case "identifier": {
      const binding = consts.get(expr.name);
      return binding === undefined ? expr : { ...binding.value };
    }
    case "indexed":
      return { ...expr, index: inlineConstIdentifiers(expr.index, consts) };
    case "unary":
      return { ...expr, expr: inlineConstIdentifiers(expr.expr, consts) };
    case "binary":
      return {
        ...expr,
        left: inlineConstIdentifiers(expr.left, consts),
        right: inlineConstIdentifiers(expr.right, consts),
      };
    case "call":
      return {
        ...expr,
        args: expr.args.map((arg) => inlineConstIdentifiers(arg, consts)),
      };
  }
}

function collectV2StateNames(v2: V2ProgramAst): Set<string> {
  return new Set(v2.state.map((field) => field.name));
}

function assertNoConstAssignment(v2: V2ProgramAst, consts: ReadonlyMap<string, V2ConstBinding>): void {
  const visit = (statements: readonly V2StatementAst[]): void => {
    for (const statement of statements) {
      checkV2StatementConstAssignment(statement, consts);
      visitV2StatementChildren(statement, visit);
    }
  };
  visit(v2.body);
  for (const rule of v2.rules) visit(rule.body);
}

function checkV2StatementConstAssignment(
  statement: V2StatementAst,
  consts: ReadonlyMap<string, V2ConstBinding>,
): void {
  if (statement.kind === "v2_assign" || statement.kind === "v2_update") {
    const root = assignmentRootIdentifier(statement.target);
    if (root !== undefined && consts.has(root)) {
      throw new ParseError(`Cannot assign to const '${root}'`, statement.line);
    }
  }
}

function assignmentRootIdentifier(target: string): string | undefined {
  const trimmed = target.trim();
  if (/^[A-Za-z_][\w]*$/u.test(trimmed)) return trimmed;
  const bracket = /^([A-Za-z_][\w]*)\s*\[/u.exec(trimmed);
  return bracket?.[1];
}

function visitV2StatementChildren(
  statement: V2StatementAst,
  visit: (statements: readonly V2StatementAst[]) => void,
): void {
  switch (statement.kind) {
    case "v2_if":
      visit(statement.thenBody);
      if (statement.elseBody !== undefined) visit(statement.elseBody);
      return;
    case "v2_while":
    case "v2_loop":
      visit(statement.body);
      return;
    case "v2_match":
      for (const matchCase of statement.cases) visit([matchCase.action]);
      if (statement.otherwise !== undefined) visit([statement.otherwise]);
      return;
    default:
      return;
  }
}
