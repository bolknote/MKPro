import type { ExpressionAst } from "../../types.ts";
import { affineIndexIdentifierOffset } from "../../state-banks.ts";
import type { LoweringCtx } from "../lowering-ctx.ts";
import {
  compileNegativeZeroDegreeSelectorCall,
  compileSpatialCountCall,
  compileSpatialHitCall,
} from "./spatial.ts";
import {
  NEGATIVE_ZERO_DEGREE_SELECTOR_GE,
  binaryOpcode,
  buildDiagnostic,
  countExpressionCalls,
  estimateExpressionCost,
  expressionEquals,
  expressionIsDeterministic,
  expressionReferencesIdentifier,
  expressionToIntentText,
  isNumericValue,
  isPureExpression,
  isSimpleStackLoad,
  isSmallSetMacroName,
  isPackedGridMacroName,
  minExpression,
  safeMaxExpression,
  safeMinExpression,
  matchXParamValueFunction,
  matchXParamReturnDecay,
  matchXParamStackStopRiskRead,
  matchRemainderByConstant,
  X_TRANSFORM_UNARY_OPCODES,
  X_TRANSFORM_UNARY_FUNCTIONS,
  negatedNumberLiteral,
  numberExpression,
  randomRangeExpression,
  smallSetExpressionMacro,
  smallSetMacroArityOk,
  smallSetMacroArityText,
  packedGridExpressionMacro,
  packedGridMacroArity,
  xParamValueScratchName,
} from "../lowering-helpers.ts";
import type { StackStopRiskMatch, XParamStackStopRiskRead } from "../lowering-helpers.ts";

export function compileExpression(ctx: LoweringCtx, expr: ExpressionAst): void {
    const randomCellHelper = ctx.sharedRandomCellHelper(expr);
    if (randomCellHelper !== undefined) {
      ctx.emitJump(0x53, "ПП", randomCellHelper.label, `random cell ${expressionToIntentText(expr)}`);
      ctx.optimizations.push({
        name: "random-cell-helper-call",
        detail: `Reused shared random cell helper for ${expressionToIntentText(expr)}.`,
      });
      return;
    }

    const helper = ctx.sharedExpressionHelper(expr);
    if (helper !== undefined) {
      ctx.emitJump(0x53, "ПП", helper.label, `expr ${expressionToIntentText(expr)}`);
      ctx.optimizations.push({
        name: "expression-helper-call",
        detail: `Reused shared helper for ${expressionToIntentText(expr)}.`,
      });
      return;
    }

    switch (expr.kind) {
      case "number":
        ctx.emitNumberOrPreload(expr.raw);
        return;
      case "string":
        ctx.diagnostics.push(buildDiagnostic(
          "error",
          `Display string ${JSON.stringify(expr.text)} can only be used through show(...).`,
        ));
        return;
      case "identifier": {
        const constant = ctx.constants.get(expr.name);
        if (constant) {
          if (ctx.constantStack.has(expr.name)) {
            ctx.diagnostics.push({
              level: "error",
              message: `Cyclic constant reference '${expr.name}'.`,
            });
            return;
          }
          ctx.constantStack.add(expr.name);
          try {
            compileExpression(ctx, constant);
          } finally {
            ctx.constantStack.delete(expr.name);
          }
          return;
        }
        ctx.emitRecall(expr.name, `recall ${expr.name}`);
        return;
      }
      case "indexed":
        if (ctx.xHoldsExpression(expr)) {
          ctx.optimizations.push({
            name: "current-x-indexed-reuse",
            detail: `Reused ${expressionToIntentText(expr)} already in X.`,
          });
          return;
        }
        ctx.emitIndexedRecall(expr);
        return;
      case "unary":
        if (expr.op === "-" && expr.expr.kind === "number") {
          ctx.emitNumberOrPreload(negatedNumberLiteral(expr.expr.raw));
          return;
        }
        compileExpression(ctx, expr.expr);
        ctx.emitOp(0x0b, "/-/", "unary minus");
        return;
      case "binary":
        if (expr.op === "-" && isNumericValue(expr.left, 0)) {
          compileExpression(ctx, expr.right);
          ctx.emitOp(0x0b, "/-/", "unary minus");
          return;
        }
        if (expr.op === "/" && isNumericValue(expr.left, 1)) {
          compileExpression(ctx, expr.right);
          ctx.emitOp(0x23, "F 1/x", "reciprocal division");
          ctx.optimizations.push({
            name: "reciprocal-division-lowering",
            detail: `Lowered ${expressionToIntentText(expr)} through F 1/x.`,
          });
          return;
        }
        if (compileRemainderByConstant(ctx, expr)) {
          return;
        }
        if ((expr.op === "+" || expr.op === "*") && compileCommutativeWithCurrentX(ctx, expr)) {
          return;
        }
        if (compileSquareBinary(ctx, expr)) {
          return;
        }
        if (compileStackDuplicatedBinary(ctx, expr)) {
          return;
        }
        compileExpression(ctx, expr.left);
        compileExpression(ctx, expr.right);
        ctx.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
        return;
      case "call":
        compileCall(ctx, expr);
        return;
    }
  }

export function compileCommutativeWithCurrentX(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "binary" }>): boolean {
    if (ctx.currentXVariable === undefined) return false;
    if (expr.left.kind === "identifier" && ctx.xHolds(expr.left.name) && isSimpleStackLoad(expr.right)) {
      compileExpression(ctx, expr.right);
      ctx.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
      ctx.optimizations.push({
        name: "stack-current-x-scheduling",
        detail: `Reused ${expr.left.name} already in X for commutative ${expr.op}.`,
      });
      return true;
    }
    if (expr.right.kind === "identifier" && ctx.xHolds(expr.right.name) && isSimpleStackLoad(expr.left)) {
      compileExpression(ctx, expr.left);
      ctx.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
      ctx.optimizations.push({
        name: "stack-current-x-scheduling",
        detail: `Reused ${expr.right.name} already in X for commutative ${expr.op}.`,
      });
      return true;
    }
    return false;
  }

export function compileSquareBinary(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "binary" }>): boolean {
    if (expr.op !== "*" || !expressionEquals(expr.left, expr.right) || !isPureExpression(expr.left)) return false;
    compileExpression(ctx, expr.left);
    ctx.emitOp(0x22, "F x^2", "square repeated operand");
    ctx.optimizations.push({
      name: "square-expression-lowering",
      detail: `Lowered ${expressionToIntentText(expr)} through F x^2.`,
    });
    return true;
  }

export function compileStackDuplicatedBinary(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "binary" }>): boolean {
    if (!expressionEquals(expr.left, expr.right)) return false;
    if (!isPureExpression(expr.left)) return false;
    // В↑ costs one cell, so duplication only pays off when recomputing the
    // operand would cost more than one cell.
    if (estimateExpressionCost(expr.left) <= 1) return false;
    compileExpression(ctx, expr.left);
    ctx.emitOp(0x0e, "В↑", "duplicate repeated operand through stack");
    ctx.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
    ctx.optimizations.push({
      name: "stack-current-x-scheduling",
      detail: `Duplicated ${expressionToIntentText(expr.left)} through the stack (В↑) for ${expr.op} instead of recomputing it.`,
    });
    return true;
  }

export function compileRemainderByConstant(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "binary" }>): boolean {
    const matched = matchRemainderByConstant(expr);
    if (matched === undefined) return false;
    compileExpression(ctx, matched.value);
    compileExpression(ctx, matched.divisor);
    ctx.emitOp(0x13, "/", "remainder quotient");
    ctx.emitOp(0x35, "К {x}", "remainder fractional part");
    compileExpression(ctx, matched.divisor);
    ctx.emitOp(0x12, "*", "remainder scale");
    ctx.optimizations.push({
      name: "remainder-fraction-lowering",
      detail: `Lowered ${expressionToIntentText(expr)} without recomputing the dividend.`,
    });
    return true;
  }

export function compileFunctionCall(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
    const proc = ctx.functionProcs.get(expr.callee);
    if (proc === undefined) return false;
    const params = proc.params ?? [];
    if (expr.args.length !== params.length) {
      ctx.diagnostics.push(buildDiagnostic(
        "error",
        `Function ${expr.callee} expects ${params.length} argument(s), got ${expr.args.length}.`,
        proc.line,
      ));
      return true;
    }
    const xParamValue = ctx.loweringOptions.xParamValueFunctions === true
      ? matchXParamValueFunction(proc)
      : undefined;
    if (xParamValue !== undefined && expr.args.length === 1 && xParamValueScratchName(ctx.ast) !== undefined) {
      compileExpression(ctx, expr.args[0]!);
      const bankSelectors = ctx.snapshotBankSelectorCache();
      ctx.emitJump(0x53, "ПП", proc.name, `call function ${proc.name}`, proc.line);
      ctx.restoreBankSelectorCacheAfterCall(proc.name, bankSelectors);
      ctx.optimizations.push({
        name: "x-param-value-function-call",
        detail: `Passed ${xParamValue.param} to ${proc.name} through X.`,
      });
      return true;
    }
    const xParamDecay = matchXParamReturnDecay(proc);
    if (xParamDecay !== undefined && expr.args.length === 1) {
      compileExpression(ctx, expr.args[0]!);
      const bankSelectors = ctx.snapshotBankSelectorCache();
      ctx.emitJump(0x53, "ПП", proc.name, `call function ${proc.name}`, proc.line);
      ctx.restoreBankSelectorCacheAfterCall(proc.name, bankSelectors);
      ctx.optimizations.push({
        name: "x-param-return-decay-call",
        detail: `Passed ${xParamDecay.param} to ${proc.name} through X.`,
      });
      return true;
    }
    const xParamStackStopRisk = matchXParamStackStopRiskRead(ctx.ast, proc);
    if (xParamStackStopRisk !== undefined && expr.args.length === 1) {
      compileExpression(ctx, expr.args[0]!);
      if (countExpressionCalls(ctx.ast, proc.name) === 1) {
        compileInlineXParamStackStopRiskRead(ctx, xParamStackStopRisk);
        ctx.optimizations.push({
          name: "x-param-stack-stop-risk-inline",
          detail: `Inlined single-use ${proc.name} while passing ${xParamStackStopRisk.param} through X.`,
        });
        ctx.optimizations.push({
          name: "show-read-stack-stop-risk-lowering",
          detail: `Reused displayed ${xParamStackStopRisk.param} as the parked Y value for ${proc.name}.`,
        });
        return true;
      }
      const bankSelectors = ctx.snapshotBankSelectorCache();
      ctx.emitJump(0x53, "ПП", proc.name, `call function ${proc.name}`, proc.line);
      ctx.restoreBankSelectorCacheAfterCall(proc.name, bankSelectors);
      ctx.optimizations.push({
        name: "x-param-stack-stop-risk-call",
        detail: `Passed ${xParamStackStopRisk.param} to ${proc.name} through X.`,
      });
      return true;
    }
    for (let index = 0; index < params.length; index += 1) {
      compileExpression(ctx, expr.args[index]!);
      ctx.emitStore(params[index]!, `arg ${params[index]} for ${expr.callee}`, proc.line);
    }
    const bankSelectors = ctx.snapshotBankSelectorCache();
    ctx.emitJump(0x53, "ПП", proc.name, `call function ${proc.name}`, proc.line);
    ctx.restoreBankSelectorCacheAfterCall(proc.name, bankSelectors);
    ctx.optimizations.push({
      name: "function-call",
      detail: `Called function ${proc.name} and consumed its result from X.`,
    });
    return true;
  }

function compileInlineXParamStackStopRiskRead(
  ctx: LoweringCtx,
  risk: XParamStackStopRiskRead,
): void {
    ctx.emitOp(0x0e, "В↑", "x-param inline keep displayed value", risk.showLine);
    ctx.emitOp(0x50, "С/П", `show ${risk.display}`, risk.showLine);
    ctx.armValueInY(risk.param);
    try {
      compileStackStopRiskTail(ctx, risk.risk, {
        inputComment: "risk input read()",
        inputLine: risk.line,
        consumerLine: risk.line,
      });
    } finally {
      ctx.clearArmedValueInY();
    }
  }

// True when compiling `expr` emits a bare read() stop (С/П) as its very first
// operation, so a preceding show can supply that stop. Holds for read() itself
// and for any chain of single-argument X-transforming intrinsics wrapping it
// (e.g. int(read()), frac(int(read()))).
export function expressionLeadsWithRead(expr: ExpressionAst): boolean {
  if (expr.kind !== "call") return false;
  const name = expr.callee.toLowerCase();
  if (name === "read") return expr.args.length === 0;
  if (expr.args.length === 1 && X_TRANSFORM_UNARY_FUNCTIONS.has(name)) {
    return expressionLeadsWithRead(expr.args[0]!);
  }
  return false;
}

// Emit the post-stop tail of a stack-stop risk fusion (see matchStackStopRisk).
// Precondition (established by the caller's head): the input value is in X and
// the parked `match.yName` value is in Y. The tail transforms the input on the
// stack, combines it with the parked Y value, and applies the outer wraps,
// leaving the result in X. The emitted opcodes are identical to the canonical
// hand-written form; the `labels` only supply neutral listing annotations.
export function compileStackStopRiskTail(
  ctx: LoweringCtx,
  match: StackStopRiskMatch,
  labels: { inputComment: string; inputLine: number; consumerLine: number },
): void {
  if (ctx.currentYVariable !== match.yName) {
    throw new Error(
      `stack-stop risk tail expected ${match.yName} parked in Y, found ${String(ctx.currentYVariable)}`,
    );
  }
  for (const [code, mnemonic] of match.inputUnary) {
    ctx.emitOp(code, mnemonic, labels.inputComment, labels.inputLine);
  }
  if (match.additive !== undefined) {
    ctx.emitOp(match.additive.digit, String(match.additive.digit), "risk multiplier", labels.consumerLine);
    ctx.emitOp(binaryOpcode(match.additive.op), match.additive.op, "risk multiplier", labels.consumerLine);
  }
  ctx.emitOp(binaryOpcode(match.yOp), match.yOp, "risk combine", labels.consumerLine);
  for (let index = match.wraps.length - 1; index >= 0; index -= 1) {
    const [code, mnemonic] = match.wraps[index]!;
    ctx.emitOp(code, mnemonic, "risk integer result", labels.consumerLine);
  }
}

export function compileCall(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): void {
    if (compileFunctionCall(ctx, expr)) return;
    const name = expr.callee.toLowerCase();
    if (name === "neighbor_count" || name === "line_count") {
      if (compileSpatialCountCall(ctx, name, expr)) return;
    }
    if (name === "__spatial_hit") {
      if (compileSpatialHitCall(ctx, expr)) return;
    }
    if (name === NEGATIVE_ZERO_DEGREE_SELECTOR_GE) {
      if (compileNegativeZeroDegreeSelectorCall(ctx, expr)) return;
    }
    if (isPackedGridMacroName(name) && packedGridMacroArity(name) !== expr.args.length) {
      ctx.diagnostics.push({
        level: "error",
        message: `${expr.callee}() expects ${packedGridMacroArity(name)} arguments, got ${expr.args.length}.`,
      });
      return;
    }
    if (isSmallSetMacroName(name) && !smallSetMacroArityOk(name, expr.args.length)) {
      ctx.diagnostics.push({
        level: "error",
        message: `${expr.callee}() expects ${smallSetMacroArityText(name)}, got ${expr.args.length}.`,
      });
      return;
    }
    const smallSetMacro = smallSetExpressionMacro(name, expr.args);
    if (smallSetMacro !== undefined) {
      compileExpression(ctx, smallSetMacro);
      ctx.optimizations.push({
        name: "small-set-primitive-lowering",
        detail: `Lowered ${expr.callee}() to coordinate-set arithmetic.`,
      });
      return;
    }
    if (name === "packed_score" && compilePackedScoreStackHelperCall(ctx, expr)) {
      return;
    }
    const macro = packedGridExpressionMacro(name, expr.args);
    if (macro !== undefined) {
      compileExpression(ctx, macro);
      ctx.optimizations.push({
        name: "packed-grid-primitive-lowering",
        detail: `Lowered ${expr.callee}() to reusable 4x4 grid/packed-line arithmetic.`,
      });
      return;
    }

    if (name === "random") {
      compileRandomCall(ctx, expr);
      return;
    }
    if (compileRandomIntegerCall(ctx, expr)) {
      return;
    }
    if (name === "read") {
      if (expr.args.length !== 0) {
        ctx.diagnostics.push({
          level: "error",
          message: `${expr.callee}() takes no arguments, got ${expr.args.length}.`,
        });
        return;
      }
      // When a preceding show was fused into this read, the calculator already
      // stopped for the entry and the value is sitting in X; reuse that stop
      // instead of emitting a second С/П.
      if (ctx.consumeArmedInputInX()) return;
      ctx.emitOp(0x50, "С/П", `${expr.callee}()`);
      return;
    }
    const zeroArgOpcodes: Record<string, [number, string]> = {
      pi: [0x20, "F pi"],
    };
    const zeroArgOpcode = zeroArgOpcodes[name];
    if (zeroArgOpcode !== undefined) {
      if (expr.args.length !== 0) {
        ctx.diagnostics.push({
          level: "error",
          message: `${expr.callee}() takes no arguments, got ${expr.args.length}.`,
        });
        return;
      }
      ctx.emitOp(zeroArgOpcode[0], zeroArgOpcode[1], `${expr.callee}()`);
      return;
    }
    if (name === "e") {
      if (expr.args.length !== 0) {
        ctx.diagnostics.push({
          level: "error",
          message: `${expr.callee}() takes no arguments, got ${expr.args.length}.`,
        });
        return;
      }
      ctx.emitNumber("1");
      ctx.emitOp(0x16, "F e^x", `${expr.callee}()`);
      return;
    }
    if (name === "entered") {
      if (expr.args.length !== 0) {
        ctx.diagnostics.push({
          level: "error",
          message: `${expr.callee}() takes no arguments, got ${expr.args.length}.`,
        });
        return;
      }
      ctx.currentXVariable = undefined;
      ctx.currentXAliases.clear();
      ctx.currentXKnownZero = false;
      ctx.optimizations.push({
        name: "entered-current-x",
        detail: "Consumed the current keyboard-entered X value without emitting another stop.",
      });
      return;
    }

    if (name === "pow") {
      if (expr.args.length !== 2) {
        ctx.diagnostics.push({
          level: "error",
          message: "Function pow expects two arguments.",
        });
        return;
      }
      if (isNumericValue(expr.args[1]!, 2)) {
        compileExpression(ctx, expr.args[0]!);
        ctx.emitOp(0x22, "F x^2", `${expr.callee}()`);
        ctx.optimizations.push({
          name: "pow-square-lowering",
          detail: `Lowered ${expressionToIntentText(expr)} through F x^2.`,
        });
        return;
      }
      if (isNumericValue(expr.args[0]!, 10)) {
        compileExpression(ctx, expr.args[1]!);
        ctx.emitOp(0x15, "F 10^x", `${expr.callee}()`);
        ctx.optimizations.push({
          name: "pow10-opcode-lowering",
          detail: `Lowered ${expressionToIntentText(expr)} through F 10^x.`,
        });
        return;
      }
      compileExpression(ctx, expr.args[1]!);
      compileExpression(ctx, expr.args[0]!);
      ctx.emitOp(0x24, "F x^y", `${expr.callee}()`);
      return;
    }

    if (name === "min") {
      if (expr.args.length !== 2) {
        ctx.diagnostics.push({
          level: "error",
          message: `Function ${expr.callee} expects two arguments.`,
        });
        return;
      }
      compileExpression(ctx, minExpression(expr.args[0]!, expr.args[1]!));
      ctx.optimizations.push({
        name: "min-via-max-lowering",
        detail: `Lowered ${expressionToIntentText(expr)} through min-via-max().`,
      });
      return;
    }

    if (name === "safe_max" || name === "safe_min") {
      if (expr.args.length !== 2) {
        ctx.diagnostics.push({
          level: "error",
          message: `Function ${expr.callee} expects two arguments.`,
        });
        return;
      }
      const left = expr.args[0]!;
      const right = expr.args[1]!;
      // The arithmetic identity references both operands twice, so impure
      // operands (calls such as random()/read()) cannot be duplicated safely.
      if (!isPureExpression(left) || !isPureExpression(right)) {
        ctx.diagnostics.push({
          level: "error",
          message: `${expr.callee}() requires duplicable operands; bind ${
            isPureExpression(left) ? "the second" : "the first"
          } argument to a variable first.`,
        });
        return;
      }
      const lowered =
        name === "safe_max"
          ? safeMaxExpression(left, right)
          : safeMinExpression(left, right);
      compileExpression(ctx, lowered);
      ctx.optimizations.push({
        name: "quirk-free-minmax-lowering",
        detail: `Lowered ${expressionToIntentText(expr)} through quirk-free arithmetic (avoids the К max zero-is-greatest behaviour).`,
      });
      return;
    }

    const binaryOpcodes: Record<string, [number, string]> = {
      max: [0x36, "К max"],
      bit_and: [0x37, "К ∧"],
      bit_or: [0x38, "К ∨"],
      bit_xor: [0x39, "К ⊕"],
    };
    const binaryCall = binaryOpcodes[name];
    if (binaryCall !== undefined) {
      if (expr.args.length !== 2) {
        ctx.diagnostics.push({
          level: "error",
          message: `Function ${expr.callee} expects two arguments.`,
        });
        return;
      }
      if (compileCommutativeCallWithDestructiveSelectorLast(ctx, expr, binaryCall)) return;
      if (compileCommutativeCallWithCurrentX(ctx, expr, binaryCall)) return;
      compileExpression(ctx, expr.args[0]!);
      compileExpression(ctx, expr.args[1]!);
      ctx.emitOp(binaryCall[0], binaryCall[1], `${expr.callee}()`);
      return;
    }

    if (expr.args.length !== 1) {
      ctx.diagnostics.push({
        level: "error",
        message: `Function ${expr.callee} expects one argument.`,
      });
      return;
    }
    const opcode = X_TRANSFORM_UNARY_OPCODES[name];
    if (!opcode) {
      ctx.diagnostics.push({
        level: "error",
        message: `Unknown function ${expr.callee}.`,
      });
      return;
    }
    if (compileCurrentXDerivation(ctx, expr)) {
      ctx.optimizations.push({
        name: "current-x-unary-derivation",
        detail: `Reused ${expressionToIntentText(expr.args[0]!)} already in X for ${expr.callee}().`,
      });
      return;
    }
    compileExpression(ctx, expr.args[0]!);
    ctx.emitOp(opcode[0], opcode[1], `${expr.callee}()`);
  }

function compileRandomCall(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): void {
  if (expr.args.length === 0) {
    ctx.emitOp(0x3b, "К СЧ", `${expr.callee}()`);
    return;
  }
  if (expr.args.length === 1) {
    compileExpression(ctx, randomRangeExpression(numberExpression(0), expr.args[0]!));
    ctx.optimizations.push({
      name: "random-range-lowering",
      detail: `Lowered ${expressionToIntentText(expr)} as random() * max.`,
    });
    return;
  }
  if (expr.args.length === 2) {
    compileExpression(ctx, randomRangeExpression(expr.args[0]!, expr.args[1]!));
    ctx.optimizations.push({
      name: "random-range-lowering",
      detail: `Lowered ${expressionToIntentText(expr)} as min + random() * (max - min).`,
    });
    return;
  }
  ctx.diagnostics.push({
    level: "error",
    message: `${expr.callee}() expects zero, one, or two arguments, got ${expr.args.length}.`,
  });
}

function compileRandomIntegerCall(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
  if (expr.callee.toLowerCase() !== "int" || expr.args.length !== 1) return false;
  const arg = expr.args[0]!;
  if (!expressionContainsValidRandom(arg)) return false;
  compileExpression(ctx, arg);
  ctx.emitOp(0x0e, "В↑", "random int keep scaled draw");
  ctx.emitOp(0x35, "К {x}", "random int fractional part");
  ctx.emitOp(0x11, "-", "random int floor");
  ctx.optimizations.push({
    name: "int-random-range-lowering",
    detail: `Lowered ${expressionToIntentText(expr)} without К [x] so the MK-61 random sequence keeps moving.`,
  });
  return true;
}

function expressionContainsValidRandom(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
      return false;
    case "indexed":
      return expressionContainsValidRandom(expr.index);
    case "unary":
      return expressionContainsValidRandom(expr.expr);
    case "binary":
      return expressionContainsValidRandom(expr.left) || expressionContainsValidRandom(expr.right);
    case "call":
      return (expr.callee.toLowerCase() === "random" && expr.args.length <= 2) ||
        expr.args.some(expressionContainsValidRandom);
    }
  }

export function compilePackedScoreStackHelperCall(
    ctx: LoweringCtx,
    expr: Extract<ExpressionAst, { kind: "call" }>,
  ): boolean {
    const helper = ctx.sharedPackedScoreStackHelper(undefined);
    if (helper === undefined) return false;
    compileExpression(ctx, expr.args[0]!);
    compileExpression(ctx, expr.args[1]!);
    ctx.emitJump(0x53, "ПП", helper.label, "packed_score helper");
    ctx.optimizations.push({
      name: "packed-score-stack-helper-call",
      detail: "Reused shared packed_score stack helper for packed-line scoring.",
    });
    return true;
  }

function compileCommutativeCallWithCurrentX(
  ctx: LoweringCtx,
  expr: Extract<ExpressionAst, { kind: "call" }>,
  opcode: [number, string],
): boolean {
  const left = expr.args[0]!;
  const right = expr.args[1]!;
  if (isSimpleStackLoad(right) && compileCurrentXDerivation(ctx, left)) {
    compileExpression(ctx, right);
    ctx.emitOp(opcode[0], opcode[1], `${expr.callee}()`);
    ctx.optimizations.push({
      name: "stack-current-x-scheduling",
      detail: `Reused ${expressionToIntentText(left)} already derivable from X for ${expr.callee}().`,
    });
    return true;
  }
  if (isSimpleStackLoad(left) && compileCurrentXDerivation(ctx, right)) {
    compileExpression(ctx, left);
    ctx.emitOp(opcode[0], opcode[1], `${expr.callee}()`);
    ctx.optimizations.push({
      name: "stack-current-x-scheduling",
      detail: `Reused ${expressionToIntentText(right)} already derivable from X for ${expr.callee}().`,
    });
    return true;
  }
  return false;
}

function compileCommutativeCallWithDestructiveSelectorLast(
  ctx: LoweringCtx,
  expr: Extract<ExpressionAst, { kind: "call" }>,
  opcode: [number, string],
): boolean {
  const left = expr.args[0]!;
  const right = expr.args[1]!;
  if (!expressionIsDeterministic(left) || !expressionIsDeterministic(right)) return false;

  const leftSource = directIntegerIndexedSource(left);
  if (leftSource !== undefined && expressionReferencesIdentifier(right, leftSource)) {
    compileExpression(ctx, right);
    compileExpression(ctx, left);
    ctx.emitOp(opcode[0], opcode[1], `${expr.callee}()`);
    ctx.optimizations.push({
      name: "destructive-selector-operand-order",
      detail: `Scheduled ${expressionToIntentText(left)} after ${expressionToIntentText(right)} so integer-part indirect addressing cannot destroy ${leftSource} before the other operand uses it.`,
    });
    return true;
  }
  return false;
}

function directIntegerIndexedSource(expr: ExpressionAst): string | undefined {
  if (expr.kind !== "indexed") return undefined;
  const affineIndex = affineIndexIdentifierOffset(expr.index);
  return affineIndex?.integerPart === true ? affineIndex.name : undefined;
}

function compileCurrentXDerivation(ctx: LoweringCtx, expr: ExpressionAst): boolean {
  if (expr.kind === "identifier") return ctx.xHolds(expr.name);
  if (expr.kind === "unary" && expr.op === "-") {
    if (!compileCurrentXDerivation(ctx, expr.expr)) return false;
    ctx.emitOp(0x0b, "/-/", "current-X unary minus");
    return true;
  }
  if (expr.kind !== "call" || expr.args.length !== 1) return false;
  const fn = expr.callee.toLowerCase();
  const opcode = X_TRANSFORM_UNARY_OPCODES[fn];
  if (opcode === undefined) return false;
  if (!compileCurrentXDerivation(ctx, expr.args[0]!)) return false;
  ctx.emitOp(opcode[0], opcode[1], `current-X ${fn}`);
  return true;
}
