import { registerIndex } from "../../opcodes.ts";
import type { ExpressionAst, RegisterName } from "../../types.ts";
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
  estimateExpressionCost,
  expressionEquals,
  expressionToIntentText,
  isNumericValue,
  isPureExpression,
  isSimpleStackLoad,
  isSmallSetMacroName,
  isTicTacToeMacroName,
  matchStackUnaryDerivationCall,
  matchXParamReturnDecay,
  matchRemainderByConstant,
  negatedNumberLiteral,
  randomRangeExpression,
  smallSetExpressionMacro,
  smallSetMacroArityOk,
  smallSetMacroArityText,
  ticTacToeExpressionMacro,
  ticTacToeMacroArity,
} from "../lowering-helpers.ts";

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
        if (compileRemainderByConstant(ctx, expr)) {
          return;
        }
        if ((expr.op === "+" || expr.op === "*") && compileCommutativeWithCurrentX(ctx, expr)) {
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

export function compileCall(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): void {
    if (compileFunctionCall(ctx, expr)) return;
    const name = expr.callee.toLowerCase();
    if (name === "direction") {
      compileDirectionCall(ctx, expr);
      return;
    }
    if (name === "__direction_cardinal") {
      compileCardinalDirectionCall(ctx, expr);
      return;
    }
    if (name === "neighbor_count" || name === "line_count") {
      if (compileSpatialCountCall(ctx, name, expr)) return;
    }
    if (name === "__spatial_hit") {
      if (compileSpatialHitCall(ctx, expr)) return;
    }
    if (name === NEGATIVE_ZERO_DEGREE_SELECTOR_GE) {
      if (compileNegativeZeroDegreeSelectorCall(ctx, expr)) return;
    }
    if (isTicTacToeMacroName(name) && ticTacToeMacroArity(name) !== expr.args.length) {
      ctx.diagnostics.push({
        level: "error",
        message: `${expr.callee}() expects ${ticTacToeMacroArity(name)} arguments, got ${expr.args.length}.`,
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
    const macro = ticTacToeExpressionMacro(name, expr.args);
    if (macro !== undefined) {
      compileExpression(ctx, macro);
      ctx.optimizations.push({
        name: "tic-tac-toe-primitive-lowering",
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

    if (name === "pow") {
      if (expr.args.length !== 2) {
        ctx.diagnostics.push({
          level: "error",
          message: "Function pow expects two arguments.",
        });
        return;
      }
      compileExpression(ctx, expr.args[1]!);
      compileExpression(ctx, expr.args[0]!);
      ctx.emitOp(0x24, "F x^y", `${expr.callee}()`);
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
    compileExpression(ctx, expr.args[0]!);
    const opcodes: Record<string, [number, string]> = {
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
    const opcode = opcodes[name];
    if (!opcode) {
      ctx.diagnostics.push({
        level: "error",
        message: `Unknown function ${expr.callee}.`,
      });
      return;
    }
    ctx.emitOp(opcode[0], opcode[1], `${expr.callee}()`);
  }

function compileRandomCall(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): void {
  if (expr.args.length === 0) {
    ctx.emitOp(0x3b, "К СЧ", `${expr.callee}()`);
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
    message: `${expr.callee}() expects zero or two arguments, got ${expr.args.length}.`,
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
      return (expr.callee.toLowerCase() === "random" && (expr.args.length === 0 || expr.args.length === 2)) ||
        expr.args.some(expressionContainsValidRandom);
  }
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

function compileCurrentXDerivation(ctx: LoweringCtx, expr: ExpressionAst): boolean {
  if (expr.kind === "identifier") return ctx.xHolds(expr.name);
  if (expr.kind === "unary" && expr.op === "-") {
    if (!compileCurrentXDerivation(ctx, expr.expr)) return false;
    ctx.emitOp(0x0b, "/-/", "current-X unary minus");
    return true;
  }
  const derived = matchStackUnaryDerivationCall(expr);
  if (derived === undefined) return false;
  if (!compileCurrentXDerivation(ctx, derived.arg)) return false;
  ctx.emitOp(derived.opcode, derived.mnemonic, `current-X ${derived.fn}`);
  return true;
}

export function compileDirectionCall(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): void {
    if (expr.args.length !== 1) {
      ctx.diagnostics.push({
        level: "error",
        message: `direction() expects one keypad argument, got ${expr.args.length}.`,
      });
      return;
    }
    const arg = expr.args[0]!;
    if (arg.kind !== "identifier") {
      ctx.diagnostics.push({
        level: "error",
        message: "direction() currently requires an identifier argument so the optimizer can reuse its register.",
      });
      return;
    }
    const keyRegister = ctx.allocation.registers[arg.name];
    if (!keyRegister) {
      ctx.diagnostics.push({
        level: "error",
        message: `Unknown direction key '${arg.name}'.`,
      });
      return;
    }

    const notFloor = ctx.freshLabel("direction_not_floor");
    const yAxis = ctx.freshLabel("direction_y_axis");
    const done = ctx.freshLabel("direction_done");

    ctx.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "direction key");
    ctx.emitOp(0x31, "К |x|", "direction floor test");
    ctx.emitNumberOrPreload("5");
    ctx.emitOp(0x11, "-", "direction abs(key)-5");
    ctx.emitJump(0x5e, "F x=0", notFloor, "direction not floor");

    ctx.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "direction floor key");
    ctx.emitNumberOrPreload("20");
    ctx.emitOp(0x12, "*", "direction floor delta");
    ctx.emitJump(0x51, "БП", done, "direction done");

    ctx.emitLabel(notFloor);
    ctx.emitOp(0x31, "К |x|", "direction x-axis test");
    ctx.emitNumberOrPreload("1");
    ctx.emitOp(0x11, "-", "direction axis discriminator");
    ctx.emitJump(0x5e, "F x=0", yAxis, "direction y-axis");

    ctx.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "direction x key");
    ctx.emitNumberOrPreload("5");
    ctx.emitOp(0x11, "-", "direction key-5");
    ctx.emitNumberOrPreload("10");
    ctx.emitOp(0x12, "*", "direction x delta");
    ctx.emitJump(0x51, "БП", done, "direction done");

    ctx.emitLabel(yAxis);
    ctx.emitNumberOrPreload("5");
    ctx.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "direction y key");
    ctx.emitOp(0x11, "-", "direction 5-key");
    ctx.emitNumberOrPreload("3");
    ctx.emitOp(0x13, "/", "direction y delta");

    ctx.emitLabel(done);
    ctx.optimizations.push({
      name: "direction-keypad-lowering",
      detail: `Lowered direction(${arg.name}) through a shared keypad geometry formula.`,
    });
  }

export function compileCardinalDirectionCall(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): void {
    const keyRegister = directionKeyRegister(ctx, expr);
    if (keyRegister === undefined) return;

    const yAxis = ctx.freshLabel("direction_cardinal_y_axis");
    const done = ctx.freshLabel("direction_cardinal_done");

    ctx.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "cardinal direction key");
    ctx.emitNumberOrPreload("5");
    ctx.emitOp(0x11, "-", "cardinal direction key-5");
    ctx.emitOp(0x31, "К |x|", "cardinal direction axis test");
    ctx.emitNumberOrPreload("1");
    ctx.emitOp(0x11, "-", "cardinal direction axis discriminator");
    ctx.emitJump(0x5e, "F x=0", yAxis, "cardinal direction y-axis");

    ctx.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "cardinal direction x key");
    ctx.emitNumberOrPreload("5");
    ctx.emitOp(0x11, "-", "cardinal direction key-5");
    ctx.emitNumberOrPreload("10");
    ctx.emitOp(0x12, "*", "cardinal direction x delta");
    ctx.emitJump(0x51, "БП", done, "cardinal direction done");

    ctx.emitLabel(yAxis);
    ctx.emitNumberOrPreload("5");
    ctx.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "cardinal direction y key");
    ctx.emitOp(0x11, "-", "cardinal direction 5-key");
    ctx.emitNumberOrPreload("3");
    ctx.emitOp(0x13, "/", "cardinal direction y delta");

    ctx.emitLabel(done);
    ctx.optimizations.push({
      name: "direction-cardinal-lowering",
      detail: `Lowered guarded cardinal direction(${directionKeyName(ctx, expr)}) without floor-key cases.`,
    });
  }

export function directionKeyRegister(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): RegisterName | undefined {
    if (expr.args.length !== 1) {
      ctx.diagnostics.push({
        level: "error",
        message: `direction() expects one keypad argument, got ${expr.args.length}.`,
      });
      return undefined;
    }
    const arg = expr.args[0]!;
    if (arg.kind !== "identifier") {
      ctx.diagnostics.push({
        level: "error",
        message: "direction() currently requires an identifier argument so the optimizer can reuse its register.",
      });
      return undefined;
    }
    const keyRegister = ctx.allocation.registers[arg.name];
    if (!keyRegister) {
      ctx.diagnostics.push({
        level: "error",
        message: `Unknown direction key '${arg.name}'.`,
      });
      return undefined;
    }
    return keyRegister;
  }

export function directionKeyName(ctx: LoweringCtx, expr: Extract<ExpressionAst, { kind: "call" }>): string {
    const arg = expr.args[0];
    return arg?.kind === "identifier" ? arg.name : "?";
  }
