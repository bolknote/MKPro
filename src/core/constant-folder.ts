import type {
  ConditionAst,
  ExpressionAst,
  ProgramAst,
  StatementAst,
} from "./types.ts";

const MAX_FOLDED_SIGNIFICANT_DIGITS = 8;
const MAX_FOLDED_DECIMAL_SCALE = 12;
const MAX_LITERAL_EXPONENT = 18;

interface DecimalValue {
  num: bigint;
  scale: number;
}

interface LinearTerm {
  expr: ExpressionAst;
  coeff: DecimalValue;
}

interface SignedExpressionTerm {
  expr: ExpressionAst;
  negative: boolean;
}

interface LinearForm {
  constant: DecimalValue;
  terms: Map<string, LinearTerm>;
}

class ConstantFolder {
  applied = 0;

  foldProgram(ast: ProgramAst): number {
    for (const state of ast.states) {
      for (const field of state.fields) {
        if (field.initial !== undefined) field.initial = this.foldExpression(field.initial);
      }
    }

    for (const entry of ast.entries) this.foldStatements(entry.body);
    for (const proc of ast.procs) this.foldStatements(proc.body);

    return this.applied;
  }

  private foldStatements(statements: StatementAst[]): void {
    for (const statement of statements) {
      switch (statement.kind) {
        case "pause":
        case "halt":
        case "return_value":
          statement.expr = this.foldExpression(statement.expr);
          break;
        case "assign":
          statement.expr = this.foldExpression(statement.expr);
          break;
        case "loop":
          this.foldStatements(statement.body);
          break;
        case "if":
          statement.condition = this.foldCondition(statement.condition);
          this.foldStatements(statement.thenBody);
          if (statement.elseBody !== undefined) this.foldStatements(statement.elseBody);
          break;
        case "dispatch":
          statement.expr = this.foldExpression(statement.expr);
          for (const dispatchCase of statement.cases) {
            dispatchCase.value = this.foldExpression(dispatchCase.value);
            this.foldStatements(dispatchCase.body);
          }
          if (statement.defaultBody !== undefined) this.foldStatements(statement.defaultBody);
          break;
        case "core":
          for (const input of statement.inputs ?? []) {
            input.expr = this.foldExpression(input.expr);
          }
          break;
        case "input":
        case "show":
        case "call":
          break;
      }
    }
  }

  private foldCondition(condition: ConditionAst): ConditionAst {
    return {
      left: this.foldExpression(condition.left),
      op: condition.op,
      right: this.foldExpression(condition.right),
    };
  }

  private folded(expr: ExpressionAst): ExpressionAst {
    this.applied += 1;
    return expr;
  }

  private foldExpression(expr: ExpressionAst): ExpressionAst {
    switch (expr.kind) {
      case "number":
      case "string":
      case "identifier":
        return expr;
      case "unary": {
        const inner = this.foldExpression(expr.expr);
        if (inner.kind === "number") {
          const negated = negateNumberExpression(inner);
          if (negated !== undefined) return this.folded(negated);
        }
        if (inner.kind === "unary" && inner.op === "-") return this.folded(inner.expr);
        return inner === expr.expr ? expr : { ...expr, expr: inner };
      }
      case "call": {
        const args = expr.args.map((arg) => this.foldExpression(arg));
        const changed = args.some((arg, index) => arg !== expr.args[index]);
        const call = changed ? { ...expr, args } : expr;
        const folded = foldPureConstantCall(expr.callee, args);
        if (folded !== undefined) return this.folded(folded);
        return call;
      }
      case "binary": {
        const left = this.foldExpression(expr.left);
        const right = this.foldExpression(expr.right);
        const folded = this.foldBinary(expr.op, left, right);
        if (folded !== undefined) return this.folded(folded);
        return left === expr.left && right === expr.right ? expr : { ...expr, left, right };
      }
    }
  }

  private foldBinary(
    op: Extract<ExpressionAst, { kind: "binary" }>["op"],
    left: ExpressionAst,
    right: ExpressionAst,
  ): ExpressionAst | undefined {
    const numeric = foldNumericBinary(op, left, right);
    if (numeric !== undefined && estimateNumberCost(numeric.raw) <= estimateBinaryCost(left, right)) {
      return numeric;
    }

    switch (op) {
      case "+":
        if (isNumericValue(left, 0)) return right;
        if (isNumericValue(right, 0)) return left;
        {
          const signed = foldSignedAddSubExpression({ kind: "binary", op, left, right });
          if (signed !== undefined && estimateExpressionCost(signed) < estimateBinaryCost(left, right)) return signed;
        }
        {
          const linear = foldLinearExpression({ kind: "binary", op, left, right });
          if (linear !== undefined && estimateExpressionCost(linear) <= estimateBinaryCost(left, right)) return linear;
        }
        return undefined;
      case "-":
        if (isNumericValue(right, 0)) return left;
        if (isNumericValue(left, 0)) return { kind: "unary", op: "-", expr: right };
        {
          const signed = foldSignedAddSubExpression({ kind: "binary", op, left, right });
          if (signed !== undefined && estimateExpressionCost(signed) < estimateBinaryCost(left, right)) return signed;
        }
        {
          const linear = foldLinearExpression({ kind: "binary", op, left, right });
          if (linear !== undefined && estimateExpressionCost(linear) <= estimateBinaryCost(left, right)) return linear;
        }
        return undefined;
      case "*": {
        if (isNumericValue(left, 1)) return right;
        if (isNumericValue(right, 1)) return left;
        if (isNumericValue(left, 0) && expressionSafeToDrop(right)) return numberExpression(0);
        if (isNumericValue(right, 0) && expressionSafeToDrop(left)) return numberExpression(0);
        const linear = foldLinearExpression({ kind: "binary", op, left, right });
        if (linear !== undefined && estimateExpressionCost(linear) <= estimateBinaryCost(left, right)) return linear;
        return undefined;
      }
      case "/":
        if (isNumericValue(right, 1)) return left;
        {
          const linear = foldLinearExpression({ kind: "binary", op, left, right });
          if (linear !== undefined && estimateExpressionCost(linear) <= estimateBinaryCost(left, right)) return linear;
        }
        return undefined;
    }
  }
}

export function foldProgramConstants(ast: ProgramAst): number {
  return new ConstantFolder().foldProgram(ast);
}

function foldNumericBinary(
  op: Extract<ExpressionAst, { kind: "binary" }>["op"],
  left: ExpressionAst,
  right: ExpressionAst,
): Extract<ExpressionAst, { kind: "number" }> | undefined {
  if (left.kind !== "number" || right.kind !== "number") return undefined;
  const lhs = parseDecimalLiteral(left.raw);
  const rhs = parseDecimalLiteral(right.raw);
  if (lhs === undefined || rhs === undefined) return undefined;

  const result = (() => {
    switch (op) {
      case "+":
        return addDecimal(lhs, rhs);
      case "-":
        return subtractDecimal(lhs, rhs);
      case "*":
        return multiplyDecimal(lhs, rhs);
      case "/":
        return divideDecimal(lhs, rhs);
    }
  })();

  return result === undefined ? undefined : decimalNumberExpression(result);
}

function negateNumberExpression(
  expr: Extract<ExpressionAst, { kind: "number" }>,
): Extract<ExpressionAst, { kind: "number" }> | undefined {
  const value = decimalFromExpression(expr);
  return value === undefined ? undefined : decimalNumberExpression(negateDecimal(value));
}

function foldPureConstantCall(
  callee: string,
  args: ExpressionAst[],
): Extract<ExpressionAst, { kind: "number" }> | undefined {
  const name = callee.toLowerCase();
  const decimals = args.map(decimalFromExpression);
  if (decimals.some((value) => value === undefined)) return undefined;
  const values = decimals as DecimalValue[];

  const result = (() => {
    switch (name) {
      case "abs":
        return values.length === 1 ? absDecimal(values[0]!) : undefined;
      case "sign":
        return values.length === 1 ? decimal(decimalSign(values[0]!)) : undefined;
      case "int":
        return values.length === 1 ? truncateDecimal(values[0]!) : undefined;
      case "frac":
        return values.length === 1 ? fractionalDecimal(values[0]!) : undefined;
      case "sqr":
        return values.length === 1 ? multiplyDecimal(values[0]!, values[0]!) : undefined;
      case "inv":
        return values.length === 1 ? divideDecimal(decimal(1), values[0]!) : undefined;
      case "pow10":
        return values.length === 1 ? pow10Decimal(values[0]!) : undefined;
      case "pow":
        return values.length === 2 ? powDecimalInteger(values[0]!, values[1]!) : undefined;
      case "max":
        return values.length === 2 ? maxDecimal(values[0]!, values[1]!) : undefined;
      case "bit_and":
        return values.length === 2 ? foldBitwise(values[0]!, values[1]!, "and") : undefined;
      case "bit_or":
        return values.length === 2 ? foldBitwise(values[0]!, values[1]!, "or") : undefined;
      case "bit_xor":
        return values.length === 2 ? foldBitwise(values[0]!, values[1]!, "xor") : undefined;
      case "bit_not":
        return values.length === 1 ? foldBitwiseNot(values[0]!) : undefined;
      default:
        return undefined;
    }
  })();

  return result === undefined ? undefined : decimalNumberExpression(result);
}

function decimalFromExpression(expr: ExpressionAst): DecimalValue | undefined {
  if (expr.kind !== "number") return undefined;
  return parseDecimalLiteral(expr.raw);
}

function foldLinearExpression(expr: ExpressionAst): ExpressionAst | undefined {
  if (!expressionPureForFolding(expr)) return undefined;
  const form = linearizeExpression(expr);
  if (form === undefined) return undefined;
  const rebuilt = buildLinearExpression(form);
  if (rebuilt === undefined || expressionKey(rebuilt) === expressionKey(expr)) return undefined;
  return rebuilt;
}

function foldSignedAddSubExpression(expr: ExpressionAst): ExpressionAst | undefined {
  if (!expressionPureForFolding(expr)) return undefined;
  const terms: SignedExpressionTerm[] = [];
  collectSignedAddSubTerms(expr, false, terms);
  if (terms.length < 2) return undefined;
  const rebuilt = buildSignedAddSubExpression(terms);
  if (rebuilt === undefined || expressionKey(rebuilt) === expressionKey(expr)) return undefined;
  return rebuilt;
}

function collectSignedAddSubTerms(
  expr: ExpressionAst,
  negative: boolean,
  terms: SignedExpressionTerm[],
): void {
  if (expr.kind === "unary" && expr.op === "-") {
    collectSignedAddSubTerms(expr.expr, !negative, terms);
    return;
  }
  if (expr.kind === "binary" && (expr.op === "+" || expr.op === "-")) {
    collectSignedAddSubTerms(expr.left, negative, terms);
    collectSignedAddSubTerms(expr.right, expr.op === "-" ? !negative : negative, terms);
    return;
  }
  terms.push({ expr, negative });
}

function buildSignedAddSubExpression(terms: SignedExpressionTerm[]): ExpressionAst | undefined {
  let result: ExpressionAst | undefined;
  for (const term of terms) {
    if (result === undefined) {
      result = term.negative ? { kind: "unary", op: "-", expr: term.expr } : term.expr;
      continue;
    }
    result = term.negative ? subtractExpressions(result, term.expr) : addExpressions(result, term.expr);
  }
  return result;
}

function linearizeExpression(expr: ExpressionAst): LinearForm | undefined {
  if (!expressionPureForFolding(expr)) return undefined;
  switch (expr.kind) {
    case "string":
      return undefined;
    case "number": {
      const value = parseDecimalLiteral(expr.raw);
      return value === undefined ? singleTerm(expr) : { constant: value, terms: new Map() };
    }
    case "identifier":
    case "call":
      return singleTerm(expr);
    case "unary": {
      const inner = linearizeExpression(expr.expr);
      return inner === undefined ? undefined : scaleLinearForm(inner, decimal(-1));
    }
    case "binary": {
      switch (expr.op) {
        case "+": {
          const left = linearizeExpression(expr.left);
          const right = linearizeExpression(expr.right);
          return left === undefined || right === undefined ? undefined : addLinearForms(left, right);
        }
        case "-": {
          const left = linearizeExpression(expr.left);
          const right = linearizeExpression(expr.right);
          return left === undefined || right === undefined
            ? undefined
            : addLinearForms(left, scaleLinearForm(right, decimal(-1)));
        }
        case "*": {
          const leftFactor = decimalFromExpression(expr.left);
          if (leftFactor !== undefined) {
            const right = linearizeExpression(expr.right);
            return right === undefined ? undefined : scaleLinearForm(right, leftFactor);
          }
          const rightFactor = decimalFromExpression(expr.right);
          if (rightFactor !== undefined) {
            const left = linearizeExpression(expr.left);
            return left === undefined ? undefined : scaleLinearForm(left, rightFactor);
          }
          return singleTerm(expr);
        }
        case "/": {
          const divisor = decimalFromExpression(expr.right);
          if (divisor === undefined) return singleTerm(expr);
          const reciprocal = divideDecimal(decimal(1), divisor);
          if (reciprocal === undefined) return undefined;
          const left = linearizeExpression(expr.left);
          return left === undefined ? undefined : scaleLinearForm(left, reciprocal);
        }
      }
    }
  }
}

function buildLinearExpression(form: LinearForm): ExpressionAst | undefined {
  const signedTerms = [...form.terms.values()]
    .map(signedTermExpression)
    .filter((term): term is { expr: ExpressionAst; negative: boolean } => term !== undefined);
  const constant = normalizeDecimal(form.constant);
  const constantSign = decimalSign(constant);

  if (signedTerms.length === 0) return decimalNumberExpression(constant);

  const hasPositiveTerm = signedTerms.some((term) => !term.negative);
  // Leading with the constant would place its digits right before the first
  // term. If that term also begins with a number literal (e.g. a coefficient),
  // the two literals concatenate on the real MK-61 (1 then 3 -> 13). In that
  // case emit the terms first and fold the constant in at the end instead.
  const firstTerm = signedTerms[0];
  const firstTermLeadsWithLiteral = firstTerm !== undefined &&
    expressionBeginsWithNumberEntry(
      firstTerm.negative ? { kind: "unary", op: "-", expr: firstTerm.expr } : firstTerm.expr,
    );
  const startWithConstant =
    (constantSign > 0 || (constantSign < 0 && !hasPositiveTerm)) && !firstTermLeadsWithLiteral;
  let result: ExpressionAst | undefined;

  if (startWithConstant && constantSign !== 0) {
    result = decimalNumberExpression(constant);
    if (result === undefined) return undefined;
  }

  for (const term of signedTerms) {
    if (result === undefined) {
      result = term.negative ? { kind: "unary", op: "-", expr: term.expr } : term.expr;
    } else {
      result = term.negative ? subtractExpressions(result, term.expr) : addExpressions(result, term.expr);
    }
  }

  if (result === undefined) return decimalNumberExpression(constant);
  if (!startWithConstant && constantSign !== 0) {
    const constantExpr = decimalNumberExpression(absDecimal(constant));
    if (constantExpr === undefined) return undefined;
    result = constantSign < 0
      ? subtractExpressions(result, constantExpr)
      : addExpressions(result, constantExpr);
  }
  return result;
}

// Conservatively reports whether compiling expr begins with a digit/number
// entry (rather than a recall or operation). Used to avoid placing two number
// literals next to each other, which would concatenate on the MK-61.
function expressionBeginsWithNumberEntry(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "string":
      return false;
    case "number":
      return true;
    case "identifier":
      return false;
    case "unary":
      return expr.expr.kind === "number" || expressionBeginsWithNumberEntry(expr.expr);
    case "binary":
      return expressionBeginsWithNumberEntry(expr.left);
    case "call":
      return expr.args.length > 0 && expressionBeginsWithNumberEntry(expr.args[0]!);
  }
}

function signedTermExpression(term: LinearTerm): { expr: ExpressionAst; negative: boolean } | undefined {
  const coeff = normalizeDecimal(term.coeff);
  if (isDecimalZero(coeff)) return undefined;
  const negative = coeff.num < 0n;
  const magnitude = absDecimal(coeff);
  const coefficient = decimalNumberExpression(magnitude);
  if (coefficient === undefined) return undefined;
  return {
    expr: isDecimalOne(magnitude) ? term.expr : multiplyExpressions(coefficient, term.expr),
    negative,
  };
}

function singleTerm(expr: ExpressionAst): LinearForm {
  return {
    constant: decimal(0),
    terms: new Map([[expressionKey(expr), { expr, coeff: decimal(1) }]]),
  };
}

function addLinearForms(left: LinearForm, right: LinearForm): LinearForm {
  const result: LinearForm = {
    constant: addDecimal(left.constant, right.constant),
    terms: new Map(left.terms),
  };
  for (const [key, term] of right.terms) addLinearTerm(result, key, term.expr, term.coeff);
  return result;
}

function scaleLinearForm(form: LinearForm, factor: DecimalValue): LinearForm {
  const normalized = normalizeDecimal(factor);
  if (isDecimalZero(normalized)) return { constant: decimal(0), terms: new Map() };
  const terms = new Map<string, LinearTerm>();
  for (const [key, term] of form.terms) {
    terms.set(key, {
      expr: term.expr,
      coeff: multiplyDecimal(term.coeff, normalized),
    });
  }
  return {
    constant: multiplyDecimal(form.constant, normalized),
    terms,
  };
}

function addLinearTerm(form: LinearForm, key: string, expr: ExpressionAst, coeff: DecimalValue): void {
  const existing = form.terms.get(key);
  const next = existing === undefined ? normalizeDecimal(coeff) : addDecimal(existing.coeff, coeff);
  if (isDecimalZero(next)) {
    form.terms.delete(key);
    return;
  }
  form.terms.set(key, { expr: existing?.expr ?? expr, coeff: next });
}

function multiplyExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(left, 1)) return right;
  if (isNumericValue(right, 1)) return left;
  return { kind: "binary", op: "*", left, right };
}

function addExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(left, 0)) return right;
  if (isNumericValue(right, 0)) return left;
  const rightValue = numericLiteralValue(right);
  if (rightValue !== undefined && rightValue < 0 && right.kind === "number") {
    const positive = negateNumberExpression(right);
    if (positive !== undefined) return subtractExpressions(left, positive);
  }
  return { kind: "binary", op: "+", left, right };
}

function subtractExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(right, 0)) return left;
  if (isNumericValue(left, 0)) return { kind: "unary", op: "-", expr: right };
  const rightValue = numericLiteralValue(right);
  if (rightValue !== undefined && rightValue < 0 && right.kind === "number") {
    const positive = negateNumberExpression(right);
    if (positive !== undefined) return { kind: "binary", op: "+", left, right: positive };
  }
  return { kind: "binary", op: "-", left, right };
}

function expressionKey(expr: ExpressionAst): string {
  switch (expr.kind) {
    case "string":
      return `string:${JSON.stringify(expr.text)}`;
    case "number":
      return `number:${expr.raw.trim().toLowerCase()}`;
    case "identifier":
      return `identifier:${expr.name}`;
    case "unary":
      return `unary:${expr.op}:${expressionKey(expr.expr)}`;
    case "binary":
      return `binary:${expr.op}:${expressionKey(expr.left)}:${expressionKey(expr.right)}`;
    case "call":
      return `call:${expr.callee}:${expr.args.map(expressionKey).join(",")}`;
  }
}

function expressionPureForFolding(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
      return true;
    case "unary":
      return expressionPureForFolding(expr.expr);
    case "binary":
      return expressionPureForFolding(expr.left) && expressionPureForFolding(expr.right);
    case "call":
      return expr.callee.toLowerCase() !== "random" && expr.args.every(expressionPureForFolding);
  }
}

function expressionSafeToDrop(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
      return true;
    case "unary":
      return expressionSafeToDrop(expr.expr);
    case "binary":
      return expr.op !== "/" && expressionSafeToDrop(expr.left) && expressionSafeToDrop(expr.right);
    case "call":
      return false;
  }
}

function parseDecimalLiteral(raw: string): DecimalValue | undefined {
  const match = /^(-)?(\d+)(?:\.(\d+))?(?:e([+-]?\d+))?$/iu.exec(raw.trim());
  if (!match) return undefined;

  const exponent = match[4] === undefined ? 0 : Number(match[4]);
  if (!Number.isSafeInteger(exponent) || Math.abs(exponent) > MAX_LITERAL_EXPONENT) return undefined;

  const integerPart = match[2]!;
  const fractionalPart = match[3] ?? "";
  let digits = `${integerPart}${fractionalPart}`.replace(/^0+/u, "");
  let scale = fractionalPart.length - exponent;
  if (scale < 0) {
    digits = `${digits}${"0".repeat(-scale)}`;
    scale = 0;
  }
  const num = (match[1] === "-" ? -1n : 1n) * BigInt(digits || "0");
  return normalizeDecimal({ num, scale });
}

function addDecimal(left: DecimalValue, right: DecimalValue): DecimalValue {
  const scale = Math.max(left.scale, right.scale);
  return normalizeDecimal({
    num: left.num * pow10(scale - left.scale) + right.num * pow10(scale - right.scale),
    scale,
  });
}

function subtractDecimal(left: DecimalValue, right: DecimalValue): DecimalValue {
  return addDecimal(left, { num: -right.num, scale: right.scale });
}

function multiplyDecimal(left: DecimalValue, right: DecimalValue): DecimalValue {
  return normalizeDecimal({
    num: left.num * right.num,
    scale: left.scale + right.scale,
  });
}

function divideDecimal(left: DecimalValue, right: DecimalValue): DecimalValue | undefined {
  if (right.num === 0n) return undefined;
  let numerator = left.num * pow10(right.scale);
  let denominator = right.num * pow10(left.scale);
  if (denominator < 0n) {
    numerator = -numerator;
    denominator = -denominator;
  }

  const divisor = gcd(absBigInt(numerator), denominator);
  numerator /= divisor;
  denominator /= divisor;

  let twos = 0;
  while (denominator % 2n === 0n) {
    denominator /= 2n;
    twos += 1;
  }
  let fives = 0;
  while (denominator % 5n === 0n) {
    denominator /= 5n;
    fives += 1;
  }
  if (denominator !== 1n) return undefined;

  const scale = Math.max(twos, fives);
  if (scale > MAX_FOLDED_DECIMAL_SCALE) return undefined;
  return normalizeDecimal({
    num: numerator * powBigInt(2n, scale - twos) * powBigInt(5n, scale - fives),
    scale,
  });
}

function normalizeDecimal(value: DecimalValue): DecimalValue {
  let { num, scale } = value;
  if (num === 0n) return { num: 0n, scale: 0 };
  while (scale > 0 && num % 10n === 0n) {
    num /= 10n;
    scale -= 1;
  }
  return { num, scale };
}

function decimal(value: number): DecimalValue {
  return normalizeDecimal({ num: BigInt(value), scale: 0 });
}

function negateDecimal(value: DecimalValue): DecimalValue {
  return normalizeDecimal({ num: -value.num, scale: value.scale });
}

function absDecimal(value: DecimalValue): DecimalValue {
  return normalizeDecimal({ num: absBigInt(value.num), scale: value.scale });
}

function decimalSign(value: DecimalValue): -1 | 0 | 1 {
  const normalized = normalizeDecimal(value);
  if (normalized.num < 0n) return -1;
  if (normalized.num > 0n) return 1;
  return 0;
}

function compareDecimal(left: DecimalValue, right: DecimalValue): -1 | 0 | 1 {
  const scale = Math.max(left.scale, right.scale);
  const lhs = left.num * pow10(scale - left.scale);
  const rhs = right.num * pow10(scale - right.scale);
  if (lhs < rhs) return -1;
  if (lhs > rhs) return 1;
  return 0;
}

function truncateDecimal(value: DecimalValue): DecimalValue {
  const normalized = normalizeDecimal(value);
  if (normalized.scale === 0) return normalized;
  return normalizeDecimal({
    num: normalized.num / pow10(normalized.scale),
    scale: 0,
  });
}

function fractionalDecimal(value: DecimalValue): DecimalValue {
  return subtractDecimal(value, truncateDecimal(value));
}

function pow10Decimal(exponent: DecimalValue): DecimalValue | undefined {
  const integer = decimalToIntegerBigInt(exponent);
  if (integer === undefined || integer < BigInt(-MAX_LITERAL_EXPONENT) || integer > BigInt(MAX_LITERAL_EXPONENT)) {
    return undefined;
  }
  const power = Number(integer);
  return power >= 0
    ? normalizeDecimal({ num: pow10(power), scale: 0 })
    : normalizeDecimal({ num: 1n, scale: -power });
}

function powDecimalInteger(base: DecimalValue, exponent: DecimalValue): DecimalValue | undefined {
  if (decimalSign(base) <= 0) return undefined;
  const integerExponent = decimalToIntegerBigInt(exponent);
  if (
    integerExponent === undefined ||
    integerExponent < BigInt(-MAX_LITERAL_EXPONENT) ||
    integerExponent > BigInt(MAX_LITERAL_EXPONENT)
  ) {
    return undefined;
  }

  const magnitude = Number(absBigInt(integerExponent));
  let result = decimal(1);
  for (let i = 0; i < magnitude; i += 1) {
    result = multiplyDecimal(result, base);
    if (result.scale > MAX_FOLDED_DECIMAL_SCALE) return undefined;
  }

  return integerExponent >= 0n ? result : divideDecimal(decimal(1), result);
}

function maxDecimal(left: DecimalValue, right: DecimalValue): DecimalValue {
  if (isDecimalZero(left) || isDecimalZero(right)) return decimal(0);
  return compareDecimal(left, right) >= 0 ? left : right;
}

function foldBitwise(left: DecimalValue, right: DecimalValue, op: "and" | "or" | "xor"): DecimalValue | undefined {
  const lhs = decimalMantissaDigits(left);
  const rhs = decimalMantissaDigits(right);
  if (lhs === undefined || rhs === undefined) return undefined;
  const digits = [8];
  for (let index = 1; index < 8; index += 1) {
    const digit = bitwiseNibble(lhs[index]!, rhs[index]!, op);
    if (digit > 9) return undefined;
    digits.push(digit);
  }
  return decimalFromDigits(digits, 7);
}

function foldBitwiseNot(value: DecimalValue): DecimalValue | undefined {
  const digits = decimalMantissaDigits(value);
  if (digits === undefined) return undefined;
  const result = [8];
  for (let index = 1; index < 8; index += 1) {
    const digit = (~digits[index]!) & 0x0f;
    if (digit > 9) return undefined;
    result.push(digit);
  }
  return decimalFromDigits(result, 7);
}

function bitwiseNibble(left: number, right: number, op: "and" | "or" | "xor"): number {
  switch (op) {
    case "and":
      return left & right;
    case "or":
      return left | right;
    case "xor":
      return left ^ right;
  }
}

function decimalToIntegerBigInt(value: DecimalValue): bigint | undefined {
  const normalized = normalizeDecimal(value);
  return normalized.scale === 0 ? normalized.num : undefined;
}

function decimalMantissaDigits(value: DecimalValue): number[] | undefined {
  const normalized = normalizeDecimal(value);
  if (normalized.num === 0n) return new Array<number>(8).fill(0);
  const digits = absBigInt(normalized.num)
    .toString()
    .slice(0, 8)
    .padEnd(8, "0");
  return [...digits].map((digit) => Number(digit));
}

function decimalFromDigits(digits: number[], scale: number): DecimalValue {
  return normalizeDecimal({
    num: BigInt(digits.join("")),
    scale,
  });
}

function isDecimalZero(value: DecimalValue): boolean {
  return normalizeDecimal(value).num === 0n;
}

function isDecimalOne(value: DecimalValue): boolean {
  const normalized = normalizeDecimal(value);
  return normalized.num === 1n && normalized.scale === 0;
}

function decimalNumberExpression(value: DecimalValue): Extract<ExpressionAst, { kind: "number" }> | undefined {
  const normalized = normalizeDecimal(value);
  if (normalized.scale > MAX_FOLDED_DECIMAL_SCALE) return undefined;
  const raw = decimalToRaw(normalized);
  if (significantDigitCount(raw) > MAX_FOLDED_SIGNIFICANT_DIGITS) return undefined;
  return { kind: "number", raw };
}

function decimalToRaw(value: DecimalValue): string {
  const normalized = normalizeDecimal(value);
  if (normalized.num === 0n) return "0";
  const sign = normalized.num < 0n ? "-" : "";
  const digits = absBigInt(normalized.num).toString();
  if (normalized.scale === 0) return `${sign}${digits}`;
  if (digits.length <= normalized.scale) {
    return `${sign}0.${"0".repeat(normalized.scale - digits.length)}${digits}`;
  }
  const split = digits.length - normalized.scale;
  return `${sign}${digits.slice(0, split)}.${digits.slice(split)}`;
}

function significantDigitCount(raw: string): number {
  const digits = raw.replace(/^-|\.|e[+-]?\d+$/giu, "").replace(/^0+/u, "");
  return digits.length === 0 ? 1 : digits.length;
}

function numberExpression(value: number): Extract<ExpressionAst, { kind: "number" }> {
  return { kind: "number", raw: String(value) };
}

function isNumericValue(expr: ExpressionAst, value: number): boolean {
  const parsed = numericLiteralValue(expr);
  return parsed !== undefined && parsed === value;
}

function numericLiteralValue(expr: ExpressionAst): number | undefined {
  if (expr.kind !== "number") return undefined;
  const value = Number(expr.raw);
  return Number.isFinite(value) ? value : undefined;
}

function estimateBinaryCost(left: ExpressionAst, right: ExpressionAst): number {
  return estimateExpressionCost(left) + estimateExpressionCost(right) + 1;
}

function estimateExpressionCost(expr: ExpressionAst): number {
  switch (expr.kind) {
    case "string":
      return 0;
    case "number":
      return estimateNumberCost(expr.raw);
    case "identifier":
      return 1;
    case "unary":
      return estimateExpressionCost(expr.expr) + 1;
    case "binary":
      return estimateExpressionCost(expr.left) + estimateExpressionCost(expr.right) + 1;
    case "call":
      return expr.args.reduce((sum, arg) => sum + estimateExpressionCost(arg), 1);
  }
}

function estimateNumberCost(raw: string): number {
  const normalized = raw.trim().toLowerCase();
  const negative = normalized.startsWith("-");
  const unsigned = negative ? normalized.slice(1) : normalized;
  const [mantissa = "0", exponent] = unsigned.split("e");
  let cost = negative ? 1 : 0;
  for (const char of mantissa) {
    if (char === "." || /\d/u.test(char)) cost += 1;
  }
  if (exponent !== undefined) {
    cost += 1;
    if (exponent.startsWith("-")) cost += 1;
    cost += exponent.replace(/^[+-]/u, "").length;
  }
  return cost;
}

function pow10(exponent: number): bigint {
  return powBigInt(10n, exponent);
}

function powBigInt(base: bigint, exponent: number): bigint {
  let result = 1n;
  for (let i = 0; i < exponent; i += 1) result *= base;
  return result;
}

function gcd(left: bigint, right: bigint): bigint {
  let a = left;
  let b = right;
  while (b !== 0n) {
    const next = a % b;
    a = b;
    b = next;
  }
  return a;
}

function absBigInt(value: bigint): bigint {
  return value < 0n ? -value : value;
}
