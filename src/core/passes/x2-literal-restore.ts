import type { IrOp, RegisterName } from "../types.ts";
import { isStableIndirectSelector } from "../indirect-addressing.ts";
import {
  computeX2DotRestoreGapStates,
  computeX2ImmediateSyncStates,
  computeX2ValueStates,
  addressIndexes,
  directReturnAnalysisContext,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  knownIndirectMemoryTarget,
  knownIndirectFlowTarget,
  labelIndexes,
  analyzeX2StackEffect,
  planX2ReplacementStackLift,
  replacingNumberEntryCanExposeStackLift,
  plainPreservesXValue,
  transferX2ValueStateForEdge,
  x2CanUseSourceDotRestoreAt,
  x2PlanDotReplacementVpSource,
  x2SyncCanExposeContextSensitiveRestore,
  x2StateHasUnsafeDotRestoreShapeX2,
  x2StateHasSameDotRestoreValueInXAndX2,
  x2StateIsClosedPlainContext,
  x2BinaryExpressionValueFacts,
  x2StableConstantExpressionValueFacts,
  x2UnaryExpressionValueFacts,
  x2ValueSetHasFact,
  x2ValueSetHasRestoredVisibleDecimal,
  x2ValueShapeSetHasRestoredVisibleDecimal,
  x2ValueSetHasNormalizedDecimalFact,
  type DirectReturnAnalysisContext,
  type IrPass,
  type IrPassFn,
  type X2ValueDataflowState,
  type X2ValueFact,
} from "./helpers.ts";

const DOT = 0x0a;
const SIGN_CHANGE = 0x0b;
const VP = 0x0c;
const STACK_LIFT = 0x0e;

interface NumericLiteralRun {
  readonly end: number;
  readonly displayValue: string;
  readonly x2Fact: X2ValueFact;
  readonly dotPreservesVpEntrySource: boolean;
}

interface UnaryExpressionRun {
  readonly end: number;
  readonly displayValue: string;
  readonly x2Facts: readonly X2ValueFact[];
  readonly sourceStackEnd: number;
  readonly allowDuplicateYStackProof: boolean;
}

interface ExpressionSourceRun {
  readonly end: number;
  readonly displayValue: string;
  readonly x2Facts: readonly X2ValueFact[];
  readonly sourceStackEnd?: number | undefined;
  readonly allowDuplicateYStackProof?: boolean | undefined;
}

function isPlainDigit(op: IrOp): op is Extract<IrOp, { kind: "plain" }> {
  return op.kind === "plain" &&
    op.opcode >= 0 &&
    op.opcode <= 9 &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op);
}

function isPlainDot(op: IrOp | undefined): op is Extract<IrOp, { kind: "plain" }> {
  return op !== undefined &&
    op.kind === "plain" &&
    op.opcode === DOT &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op);
}

function isPlainSignChange(op: IrOp | undefined): op is Extract<IrOp, { kind: "plain" }> {
  return op !== undefined &&
    op.kind === "plain" &&
    op.opcode === SIGN_CHANGE &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasRoles(op);
}

function isPlainVp(op: IrOp | undefined): op is Extract<IrOp, { kind: "plain" }> {
  return op !== undefined &&
    op.kind === "plain" &&
    op.opcode === VP &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op);
}

function decimalLiteralRunAt(
  ops: readonly IrOp[],
  start: number,
  options: { readonly allowSinglePositiveInteger?: boolean } = {},
): NumericLiteralRun | undefined {
  const digits: string[] = [];
  let end = start;
  while (end < ops.length) {
    const op = ops[end]!;
    if (!isPlainDigit(op)) break;
    digits.push(String(op.opcode));
    end += 1;
  }
  if (digits.length === 0) return undefined;
  const fractionDigits: string[] = [];
  const hasPoint = isPlainDot(ops[end]);
  if (hasPoint) {
    end += 1;
    while (end < ops.length) {
      const op = ops[end]!;
      if (!isPlainDigit(op)) break;
      fractionDigits.push(String(op.opcode));
      end += 1;
    }
    if (fractionDigits.length === 0) return undefined;
  }
  if (digits.length + fractionDigits.length > 8) return undefined;
  const sign = isPlainSignChange(ops[end]) ? "-" : "";
  const unsigned = hasPoint ? `${digits.join("")}.${fractionDigits.join("")}` : digits.join("");
  const raw = `${sign}${unsigned}`;
  const normalized = normalizeDecimalMantissaEntry(raw);
  if (normalized === undefined) return undefined;
  const x2Fact = decimalEntryFact(raw);
  if (x2Fact === undefined) return undefined;
  const dotPreservesVpEntrySource = !hasPoint && raw === normalized && normalized !== "0";
  if (sign === "") {
    if (!options.allowSinglePositiveInteger && digits.length < 2 && !hasPoint) return undefined;
    return { end: end - 1, displayValue: raw, x2Fact, dotPreservesVpEntrySource };
  }
  return { end, displayValue: raw, x2Fact, dotPreservesVpEntrySource };
}

function exponentLiteralRunAt(ops: readonly IrOp[], start: number): NumericLiteralRun | undefined {
  const mantissaDigits: string[] = [];
  let cursor = start;
  while (cursor < ops.length) {
    const op = ops[cursor]!;
    if (!isPlainDigit(op)) break;
    mantissaDigits.push(String(op.opcode));
    cursor += 1;
  }
  const fractionDigits: string[] = [];
  const hasPoint = isPlainDot(ops[cursor]);
  if (hasPoint) {
    cursor += 1;
    while (cursor < ops.length) {
      const op = ops[cursor]!;
      if (!isPlainDigit(op)) break;
      fractionDigits.push(String(op.opcode));
      cursor += 1;
    }
    if (fractionDigits.length === 0) return undefined;
  }
  const mantissaSign = isPlainSignChange(ops[cursor]) ? "-" : "";
  if (mantissaSign === "-") cursor += 1;
  if (
    mantissaDigits.length === 0 ||
    mantissaDigits.length + fractionDigits.length > 8 ||
    !isPlainVp(ops[cursor])
  ) return undefined;
  cursor += 1;

  const exponentDigits: string[] = [];
  while (cursor < ops.length) {
    const op = ops[cursor]!;
    if (!isPlainDigit(op)) break;
    exponentDigits.push(String(op.opcode));
    cursor += 1;
  }
  if (exponentDigits.length === 0 || exponentDigits.length > 2) return undefined;
  const exponentSign = isPlainSignChange(ops[cursor]) ? "-" : "";
  if (exponentSign === "-") cursor += 1;

  const mantissa = hasPoint
    ? `${mantissaSign}${mantissaDigits.join("")}.${fractionDigits.join("")}`
    : `${mantissaSign}${mantissaDigits.join("")}`;
  const value = normalizedExponentEntryValue(mantissa, `${exponentSign}${exponentDigits.join("")}`);
  if (value === undefined) return undefined;
  const dotPreservesVpEntrySource = mantissaDigits[0] !== "0";
  return {
    end: cursor - 1,
    displayValue: value,
    x2Fact: decimalValueFact(value, "normalized"),
    dotPreservesVpEntrySource,
  };
}

function literalRunsAt(ops: readonly IrOp[], start: number): readonly NumericLiteralRun[] {
  const exponent = exponentLiteralRunAt(ops, start);
  const decimal = decimalLiteralRunAt(ops, start);
  if (exponent === undefined) return decimal === undefined ? [] : [decimal];
  if (decimal === undefined || decimal.end === exponent.end) return [exponent];
  return [exponent, decimal];
}

function unaryExpressionRunAt(
  ops: readonly IrOp[],
  start: number,
  terminalBoundaryLabels: ReadonlyMap<string, number>,
  terminalBoundaryAddresses: ReadonlyMap<number, number>,
): UnaryExpressionRun | undefined {
  return rpnExpressionRunAt(ops, start, terminalBoundaryLabels, terminalBoundaryAddresses) ??
    unaryExpressionRunFromSingleSourceAt(ops, start) ??
    binaryExpressionRunAt(ops, start);
}

function rpnExpressionRunAt(
  ops: readonly IrOp[],
  start: number,
  terminalBoundaryLabels: ReadonlyMap<string, number>,
  terminalBoundaryAddresses: ReadonlyMap<number, number>,
): UnaryExpressionRun | undefined {
  const stack: ExpressionSourceRun[] = [];
  let cursor = start;
  let sawOperator = false;

  while (cursor < ops.length) {
    if (stack.length === 1 && sawOperator && isPlainXPreservingX2Sync(ops[cursor])) {
      const [result] = stack;
      return {
        end: cursor,
        displayValue: result!.displayValue,
        x2Facts: result!.x2Facts,
        sourceStackEnd: result!.sourceStackEnd ?? result!.end,
        allowDuplicateYStackProof: result!.allowDuplicateYStackProof ?? true,
      };
    }

    if (
      stack.length === 1 &&
      sawOperator &&
      isTerminalExpressionBoundary(ops, cursor, terminalBoundaryLabels, terminalBoundaryAddresses, start)
    ) {
      const [result] = stack;
      return {
        end: result!.end,
        displayValue: result!.displayValue,
        x2Facts: result!.x2Facts,
        sourceStackEnd: result!.sourceStackEnd ?? result!.end,
        allowDuplicateYStackProof: result!.allowDuplicateYStackProof ?? true,
      };
    }

    if (stack.length > 0 && isPlainExpressionSyncGapOp(ops[cursor])) {
      const top = stack[stack.length - 1]!;
      stack[stack.length - 1] = { ...top, end: cursor };
      cursor += 1;
      continue;
    }

    if (stack.length > 0 && isRpnExpressionNonExecutableGap(ops[cursor])) {
      const top = stack[stack.length - 1]!;
      stack[stack.length - 1] = { ...top, end: cursor };
      cursor += 1;
      continue;
    }

    if (isPlainStackLiftSeparator(ops[cursor]) && stack.length > 0) {
      cursor += 1;
      continue;
    }

    const source = expressionSourceRunAt(ops, cursor);
    if (source !== undefined) {
      stack.push({
        ...source,
        sourceStackEnd: source.end,
        allowDuplicateYStackProof: true,
      });
      cursor = source.end + 1;
      continue;
    }

    const op = ops[cursor];
    if (op === undefined || op.kind !== "plain") return undefined;

    if (stack.length >= 1) {
      const source = stack[stack.length - 1]!;
      const nextFacts = stableUnaryExpressionValueFactsForSource(op, source.x2Facts);
      if (nextFacts.length > 0) {
        const mnemonic = "mnemonic" in op.meta ? op.meta.mnemonic : undefined;
        stack[stack.length - 1] = {
          end: cursor,
          displayValue: `${mnemonic ?? "expr"}(${source.displayValue})`,
          x2Facts: nextFacts,
          sourceStackEnd: source.sourceStackEnd ?? source.end,
          allowDuplicateYStackProof: source.allowDuplicateYStackProof ?? true,
        };
        sawOperator = true;
        cursor += 1;
        while (isPlainExpressionSyncGapOp(ops[cursor]) || isRpnExpressionNonExecutableGap(ops[cursor])) {
          const top = stack[stack.length - 1]!;
          stack[stack.length - 1] = { ...top, end: cursor };
          cursor += 1;
        }
        continue;
      }
    }

    if (stack.length >= 2) {
      const xSource = stack.pop()!;
      const ySource = stack.pop()!;
      const nextFacts = binaryExpressionValueFactsForSources(op, ySource, xSource);
      if (nextFacts.length > 0) {
        const mnemonic = "mnemonic" in op.meta ? op.meta.mnemonic : undefined;
        stack.push({
          end: cursor,
          displayValue: `${mnemonic ?? "expr"}(${ySource.displayValue},${xSource.displayValue})`,
          x2Facts: nextFacts,
          sourceStackEnd: cursor,
          allowDuplicateYStackProof: false,
        });
        sawOperator = true;
        cursor += 1;
        while (isPlainExpressionSyncGapOp(ops[cursor]) || isRpnExpressionNonExecutableGap(ops[cursor])) {
          const top = stack[stack.length - 1]!;
          stack[stack.length - 1] = { ...top, end: cursor };
          cursor += 1;
        }
        continue;
      }
      stack.push(ySource, xSource);
    }

    return undefined;
  }

  return undefined;
}

function isTerminalExpressionBoundary(
  ops: readonly IrOp[],
  cursor: number,
  labels: ReadonlyMap<string, number>,
  addresses: ReadonlyMap<number, number>,
  immutableBeforeIndex: number,
  visited: ReadonlySet<number> = new Set(),
): boolean {
  const op = ops[cursor];
  if (op?.kind === "label" || op?.kind === "orphan-address") {
    return isTerminalExpressionBoundary(ops, cursor + 1, labels, addresses, immutableBeforeIndex, visited);
  }
  if (op?.kind === "jump" && typeof op.target === "string") {
    const target = labels.get(op.target);
    if (target === undefined || visited.has(target)) return false;
    return isTerminalExpressionBoundary(
      ops,
      target + 1,
      labels,
      addresses,
      immutableBeforeIndex,
      new Set([...visited, target]),
    );
  }
  if (op?.kind === "jump" && typeof op.target === "number") {
    const targetIndex = addresses.get(op.target);
    if (
      targetIndex === undefined ||
      targetIndex >= immutableBeforeIndex ||
      visited.has(targetIndex)
    ) return false;
    return isTerminalExpressionBoundary(
      ops,
      targetIndex,
      labels,
      addresses,
      immutableBeforeIndex,
      new Set([...visited, targetIndex]),
    );
  }
  if (op?.kind === "cjump" || op?.kind === "loop") {
    const target = directBranchTargetEntryForTerminalBoundary(op, labels, addresses, immutableBeforeIndex);
    if (target === undefined || visited.has(target.visitKey)) return false;
    const nextVisited = new Set([...visited, cursor, target.visitKey]);
    return isTerminalExpressionBoundary(
      ops,
      target.entry,
      labels,
      addresses,
      immutableBeforeIndex,
      nextVisited,
    ) && isTerminalExpressionBoundary(
      ops,
      cursor + 1,
      labels,
      addresses,
      immutableBeforeIndex,
      nextVisited,
    );
  }
  if (op?.kind === "indirect-jump") {
    const target = knownIndirectFlowTarget(op);
    const targetIndex = target === undefined ? undefined : addresses.get(target);
    if (
      targetIndex === undefined ||
      targetIndex >= immutableBeforeIndex ||
      visited.has(targetIndex)
    ) return false;
    return isTerminalExpressionBoundary(
      ops,
      targetIndex,
      labels,
      addresses,
      immutableBeforeIndex,
      new Set([...visited, targetIndex]),
    );
  }
  if (op?.kind === "indirect-cjump") {
    const target = knownIndirectFlowTarget(op);
    const targetIndex = target === undefined ? undefined : addresses.get(target);
    if (
      targetIndex === undefined ||
      targetIndex >= immutableBeforeIndex ||
      visited.has(targetIndex)
    ) return false;
    const nextVisited = new Set([...visited, cursor, targetIndex]);
    return isTerminalExpressionBoundary(
      ops,
      targetIndex,
      labels,
      addresses,
      immutableBeforeIndex,
      nextVisited,
    ) && isTerminalExpressionBoundary(
      ops,
      cursor + 1,
      labels,
      addresses,
      immutableBeforeIndex,
      nextVisited,
    );
  }
  return op === undefined || op.kind === "stop" || op.kind === "return";
}

function directBranchTargetEntryForTerminalBoundary(
  op: Extract<IrOp, { kind: "cjump" | "loop" }>,
  labels: ReadonlyMap<string, number>,
  addresses: ReadonlyMap<number, number>,
  immutableBeforeIndex: number,
): { readonly entry: number; readonly visitKey: number } | undefined {
  if (typeof op.target === "string") {
    const target = labels.get(op.target);
    return target === undefined ? undefined : { entry: target + 1, visitKey: target };
  }
  const targetIndex = addresses.get(op.target);
  if (targetIndex === undefined || targetIndex >= immutableBeforeIndex) return undefined;
  return { entry: targetIndex, visitKey: targetIndex };
}

function isRpnExpressionNonExecutableGap(op: IrOp | undefined): boolean {
  return op !== undefined && op.kind === "orphan-address" && !hasRewriteBarrier(op);
}

function unaryExpressionRunFromSingleSourceAt(ops: readonly IrOp[], start: number): UnaryExpressionRun | undefined {
  const source = expressionSourceRunAt(ops, start);
  if (source === undefined) return undefined;
  let cursor = source.end + 1;
  let displayValue = source.displayValue;
  let x2Facts: readonly X2ValueFact[] = source.x2Facts;

  while (cursor < ops.length) {
    const unary = ops[cursor];
    if (unary === undefined || unary.kind !== "plain") return undefined;
    const nextFacts = stableUnaryExpressionValueFactsForSource(unary, x2Facts);
    if (nextFacts.length === 0) return undefined;
    const mnemonic = "mnemonic" in unary.meta ? unary.meta.mnemonic : undefined;
    displayValue = `${mnemonic ?? "expr"}(${displayValue})`;
    x2Facts = nextFacts;
    cursor += 1;

    while (isPlainExpressionSyncGapOp(ops[cursor])) cursor += 1;
    if (isPlainXPreservingX2Sync(ops[cursor])) {
      return {
        end: cursor,
        displayValue,
        x2Facts,
        sourceStackEnd: source.end,
        allowDuplicateYStackProof: true,
      };
    }
  }

  return undefined;
}

function binaryExpressionRunAt(ops: readonly IrOp[], start: number): UnaryExpressionRun | undefined {
  const ySource = expressionOperandRunAt(ops, start);
  if (ySource === undefined) return undefined;
  const xStart = binaryExpressionXSourceStart(ops, ySource.end + 1);
  const xSource = expressionOperandRunAt(ops, xStart);
  if (xSource === undefined) return undefined;
  const binaryIndex = xSource.end + 1;
  const binary = ops[binaryIndex];
  if (binary === undefined || binary.kind !== "plain") return undefined;
  const binaryFacts = binaryExpressionValueFactsForSources(binary, ySource, xSource);
  if (binaryFacts.length === 0) return undefined;
  const mnemonic = "mnemonic" in binary.meta ? binary.meta.mnemonic : undefined;
  let cursor = binaryIndex + 1;
  let displayValue = `${mnemonic ?? "expr"}(${ySource.displayValue},${xSource.displayValue})`;
  let x2Facts: readonly X2ValueFact[] = binaryFacts;

  while (cursor < ops.length) {
    while (isPlainExpressionSyncGapOp(ops[cursor])) cursor += 1;
    if (isPlainXPreservingX2Sync(ops[cursor])) {
      return {
        end: cursor,
        displayValue,
        x2Facts,
        sourceStackEnd: binaryIndex,
        allowDuplicateYStackProof: false,
      };
    }
    const unary = ops[cursor];
    if (unary === undefined || unary.kind !== "plain") return undefined;
    const nextFacts = stableUnaryExpressionValueFactsForSource(unary, x2Facts);
    if (nextFacts.length === 0) return undefined;
    const unaryMnemonic = "mnemonic" in unary.meta ? unary.meta.mnemonic : undefined;
    displayValue = `${unaryMnemonic ?? "expr"}(${displayValue})`;
    x2Facts = nextFacts;
    cursor += 1;
  }

  return undefined;
}

function expressionOperandRunAt(ops: readonly IrOp[], start: number): ExpressionSourceRun | undefined {
  const source = expressionSourceRunAt(ops, start);
  if (source === undefined) return undefined;

  let cursor = source.end + 1;
  let end = source.end;
  let displayValue = source.displayValue;
  let x2Facts: readonly X2ValueFact[] = source.x2Facts;

  while (cursor < ops.length) {
    const unary = ops[cursor];
    if (unary === undefined || unary.kind !== "plain") break;
    const nextFacts = stableUnaryExpressionValueFactsForSource(unary, x2Facts);
    if (nextFacts.length === 0) break;
    const mnemonic = "mnemonic" in unary.meta ? unary.meta.mnemonic : undefined;
    displayValue = `${mnemonic ?? "expr"}(${displayValue})`;
    x2Facts = nextFacts;
    end = cursor;
    cursor += 1;

    while (isPlainExpressionSyncGapOp(ops[cursor])) {
      end = cursor;
      cursor += 1;
    }
  }

  return { end, displayValue, x2Facts };
}

function binaryExpressionXSourceStart(ops: readonly IrOp[], start: number): number {
  return isPlainStackLiftSeparator(ops[start]) ? start + 1 : start;
}

function isPlainStackLiftSeparator(op: IrOp | undefined): op is Extract<IrOp, { kind: "plain" }> {
  return op !== undefined &&
    op.kind === "plain" &&
    op.opcode === STACK_LIFT &&
    !hasRewriteBarrier(op) &&
    !isDisplayFocusSensitive(op) &&
    !hasRoles(op);
}

function expressionSourceRunAt(ops: readonly IrOp[], start: number): ExpressionSourceRun | undefined {
  const exponent = exponentLiteralRunAt(ops, start);
  if (exponent !== undefined) {
    return {
      end: exponent.end,
      displayValue: exponent.displayValue,
      x2Facts: [exponent.x2Fact],
    };
  }
  const decimal = decimalLiteralRunAt(ops, start, { allowSinglePositiveInteger: true });
  if (decimal !== undefined) {
    return {
      end: decimal.end,
      displayValue: decimal.displayValue,
      x2Facts: [decimal.x2Fact],
    };
  }
  return registerExpressionSourceRunAt(ops, start) ?? stableConstantExpressionSourceRunAt(ops, start);
}

function registerExpressionSourceRunAt(
  ops: readonly IrOp[],
  start: number,
): ExpressionSourceRun | undefined {
  const op = ops[start];
  if (op === undefined || hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return undefined;
  const register = registerExpressionSourceRegister(op);
  if (register === undefined) return undefined;
  return {
    end: start,
    displayValue: `R${register}`,
    x2Facts: [registerValueFact(register)],
  };
}

function registerExpressionSourceRegister(op: IrOp): RegisterName | undefined {
  if (op.kind === "recall") return op.register;
  if (op.kind !== "indirect-recall" || !isStableIndirectSelector(op.register)) return undefined;
  return knownIndirectMemoryTarget(op);
}

function stableConstantExpressionSourceRunAt(
  ops: readonly IrOp[],
  start: number,
): ExpressionSourceRun | undefined {
  const op = ops[start];
  if (op === undefined || op.kind !== "plain") return undefined;
  const x2Facts = [...x2StableConstantExpressionValueFacts(op)];
  if (x2Facts.length === 0) return undefined;
  const mnemonic = "mnemonic" in op.meta ? op.meta.mnemonic : undefined;
  return {
    end: start,
    displayValue: mnemonic ?? "const",
    x2Facts,
  };
}

function registerValueFact(register: RegisterName): X2ValueFact {
  return `reg:${register}`;
}

function stableUnaryExpressionValueFactsForSource(
  unary: IrOp,
  sourceFacts: readonly X2ValueFact[],
): readonly X2ValueFact[] {
  const output = new Set<X2ValueFact>();
  for (const sourceFact of sourceFacts) {
    for (const x2Fact of x2UnaryExpressionValueFacts(unary, sourceFact)) output.add(x2Fact);
  }
  return [...output];
}

function binaryExpressionValueFactsForSources(
  binary: IrOp,
  ySource: ExpressionSourceRun,
  xSource: ExpressionSourceRun,
): readonly X2ValueFact[] {
  const output = new Set<X2ValueFact>();
  for (const yFact of ySource.x2Facts) {
    for (const xFact of xSource.x2Facts) {
      for (const x2Fact of x2BinaryExpressionValueFacts(binary, yFact, xFact)) output.add(x2Fact);
    }
  }
  return [...output];
}

function isPlainExpressionSyncGapOp(op: IrOp | undefined): op is Extract<IrOp, { kind: "plain" }> {
  if (op === undefined || op.kind !== "plain" || hasRewriteBarrier(op) || isDisplayFocusSensitive(op) || hasRoles(op)) {
    return false;
  }
  const effect = analyzeX2StackEffect(op);
  return plainPreservesXValue(op) && effect.stackPreserves && effect.x2Preserves;
}

function isPlainXPreservingX2Sync(op: IrOp | undefined): op is Extract<IrOp, { kind: "plain" }> {
  if (op === undefined || op.kind !== "plain" || hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) {
    return false;
  }
  const effect = analyzeX2StackEffect(op);
  return effect.stackPreserves && effect.x2Affects && plainPreservesXValue(op);
}

function decimalValueFact(value: string, flavor: "normalized" | "unnormalized"): X2ValueFact {
  return `decimal:${value}:${flavor}`;
}

function decimalEntryFact(raw: string): X2ValueFact | undefined {
  const normalized = normalizeDecimalMantissaEntry(raw);
  if (normalized === undefined) return undefined;
  return decimalValueFact(raw === normalized ? raw : raw, raw === normalized ? "normalized" : "unnormalized");
}

function normalizeDecimalEntry(raw: string): string | undefined {
  const match = /^(-?)([0-9]{1,8})$/u.exec(raw);
  if (match === null) return undefined;
  const sign = match[1]!;
  const digits = match[2]!.replace(/^0+(?=\d)/u, "");
  if (digits === "0") return "0";
  return `${sign}${digits}`;
}

function normalizeDecimalMantissaEntry(raw: string): string | undefined {
  const match = /^(-?)([0-9]{1,8})(?:\.([0-9]+))?$/u.exec(raw);
  if (match === null) return normalizeDecimalEntry(raw);
  const digitCount = match[2]!.length + (match[3] ?? "").length;
  if (digitCount > 8) return undefined;
  const sign = match[1]!;
  const integer = match[2]!;
  const fraction = match[3];
  if (fraction === undefined) return normalizeDecimalEntry(`${sign}${integer}`);
  return normalizeSignedPlainDecimal(`${sign}${integer}.${fraction}`);
}

function normalizedExponentEntryValue(mantissa: string, exponent: string): string | undefined {
  const exponentMatch = /^(-?)([0-9]{1,2})$/u.exec(exponent);
  const mantissaParts = exponentMantissaDecimalParts(mantissa);
  if (mantissaParts === undefined || exponentMatch === null) return undefined;
  const shift = Number(exponentMatch[2]!);
  const scale = exponentMatch[1] === "-"
    ? mantissaParts.scale + shift
    : mantissaParts.scale - shift;
  if (mantissaParts.digits.length + Math.max(0, -scale) > 80) return undefined;
  const unsigned = scaledDecimalDigits(mantissaParts.digits, scale);
  const normalized = unsigned === undefined
    ? undefined
    : normalizeSignedPlainDecimal(`${mantissaParts.sign}${unsigned}`);
  if (normalized === undefined || significantDecimalDigits(normalized) > 8) return undefined;
  return normalized;
}

function exponentMantissaDecimalParts(
  mantissa: string,
): { readonly sign: "" | "-"; readonly digits: string; readonly scale: number } | undefined {
  const integer = /^(-?)([0-9]{1,8})$/u.exec(mantissa);
  if (integer !== null) {
    return {
      sign: integer[1]! === "-" ? "-" : "",
      digits: effectiveExponentMantissaDigits(integer[2]!),
      scale: 0,
    };
  }
  const fractional = /^(-?)([0-9]{1,8})\.([0-9]+)$/u.exec(mantissa);
  if (fractional === null) return undefined;
  const integerDigits = fractional[2]!;
  const fractionDigits = fractional[3]!;
  if (integerDigits.length + fractionDigits.length > 8) return undefined;
  return {
    sign: fractional[1]! === "-" ? "-" : "",
    digits: `${integerDigits}${fractionDigits}`,
    scale: fractionDigits.length,
  };
}

function effectiveExponentMantissaDigits(rawDigits: string): string {
  const stripped = rawDigits.replace(/^0+/u, "");
  if (stripped.length > 0) return stripped;
  return `1${"0".repeat(Math.max(0, rawDigits.length - 1))}`;
}

function scaledDecimalDigits(digits: string, scale: number): string | undefined {
  if (!/^\d+$/u.test(digits)) return undefined;
  if (scale <= 0) return `${digits}${"0".repeat(-scale)}`;
  const point = digits.length - scale;
  if (point > 0) return `${digits.slice(0, point)}.${digits.slice(point)}`;
  return `0.${"0".repeat(-point)}${digits}`;
}

function normalizeSignedPlainDecimal(raw: string): string | undefined {
  const match = /^(-?)([0-9]+)(?:\.([0-9]+))?$/u.exec(raw);
  if (match === null) return undefined;
  const sign = match[1]!;
  const integer = match[2]!.replace(/^0+(?=\d)/u, "");
  const fraction = (match[3] ?? "").replace(/0+$/u, "");
  const unsigned = fraction.length === 0 ? integer : `${integer}.${fraction}`;
  if (unsigned === "0") return "0";
  return `${sign}${unsigned}`;
}

function significantDecimalDigits(input: string): number {
  const unsigned = input.startsWith("-") ? input.slice(1) : input;
  const [integer, fraction] = unsigned.split(".");
  const digits = `${integer ?? ""}${fraction ?? ""}`.replace(/^0+/u, "");
  const significant = fraction === undefined ? digits.replace(/0+$/u, "") : digits;
  return significant.length === 0 ? 1 : significant.length;
}

function hasRoles(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return "meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0;
}

function replacingLiteralCanExposeContextSensitiveRestore(
  ops: readonly IrOp[],
  run: NumericLiteralRun,
  state: X2ValueDataflowState | undefined,
  producerIndex: number,
  context: DirectReturnAnalysisContext,
  vpReachabilityCache: Map<string, boolean>,
  redundantSync: {
    readonly value: boolean;
    readonly displayValue: boolean;
    readonly shape: boolean;
  },
): boolean {
  let replacementCanReachVpRestore: boolean | undefined;
  const canReachVpRestore = (): boolean => {
    const start = run.end + 1;
    const cacheKey = `${start}:${producerIndex}`;
    replacementCanReachVpRestore ??= vpReachabilityCache.get(cacheKey);
    if (replacementCanReachVpRestore === undefined) {
      replacementCanReachVpRestore = literalReplacementCanReachVpRestore(ops, start, producerIndex);
      vpReachabilityCache.set(cacheKey, replacementCanReachVpRestore);
    }
    return replacementCanReachVpRestore;
  };
  const replacementDotState = transferX2ValueStateForEdge(
    state,
    { kind: "plain", opcode: DOT, meta: { mnemonic: "." } },
    "normal",
    { trackRegisterMemory: true },
    producerIndex,
  );
  const vpSourcePlan = x2PlanDotReplacementVpSource(
    ops,
    run.end,
    state,
    replacementDotState,
    context,
  );
  if (
    !run.dotPreservesVpEntrySource &&
    vpSourcePlan.source.replacementDotHasOnlyRestoreGapBeforeVp
  ) return true;
  if (
    !x2SyncCanExposeContextSensitiveRestore(ops, run.end, {
      numericTargetMustBeBeforeIndex: producerIndex,
    })
  ) return false;
  if (
    run.dotPreservesVpEntrySource &&
    (
      vpSourcePlan.source.hasOnlyRestoreGapBeforeVp ||
      vpSourcePlan.source.replacementDotHasOnlyRestoreGapBeforeVp
    )
  ) return false;
  if (
    !canReachVpRestore() &&
    !x2SyncCanExposeContextSensitiveRestore(ops, run.end, {
      redundantSyncValue: redundantSync.value,
      redundantSyncDisplayValue: redundantSync.displayValue,
      redundantSyncShape: redundantSync.shape,
      numericTargetMustBeBeforeIndex: producerIndex,
    })
  ) return false;
  return true;
}

function addressStableFlowTargetIndex(
  addresses: ReadonlyMap<number, number>,
  target: number | undefined,
  numericTargetMustBeBeforeIndex: number,
): number | undefined {
  if (target === undefined) return undefined;
  const targetIndex = addresses.get(target);
  if (targetIndex === undefined || targetIndex >= numericTargetMustBeBeforeIndex) return undefined;
  return targetIndex;
}

function literalReplacementCanReachVpRestore(
  ops: readonly IrOp[],
  start: number,
  numericTargetMustBeBeforeIndex: number,
): boolean {
  const labels = labelIndexes(ops);
  const addresses = addressIndexes(ops);
  const visited = new Set<string>();
  const visit = (cursor: number, returnStack: readonly number[] = []): boolean => {
    for (let index = cursor; index < ops.length; index += 1) {
      const key = `${index}:${returnStack.join(",")}`;
      if (visited.has(key)) return false;
      visited.add(key);
      const op = ops[index]!;
      if (hasRewriteBarrier(op)) return true;
      switch (op.kind) {
        case "label":
        case "store":
        case "indirect-store":
        case "orphan-address":
          continue;
        case "plain": {
          if (isPlainVp(op)) return true;
          const effect = analyzeX2StackEffect(op);
          if (effect.x2Preserves) continue;
          if (effect.x2Affects || effect.x2Restores) return false;
          return true;
        }
        case "jump": {
          if (typeof op.target !== "string") {
            const targetIndex = addresses.get(op.target);
            return targetIndex === undefined || targetIndex >= numericTargetMustBeBeforeIndex
              ? true
              : visit(targetIndex, returnStack);
          }
          const target = labels.get(op.target);
          return target === undefined ? true : visit(target + 1, returnStack);
        }
        case "cjump":
        case "loop": {
          const target = typeof op.target === "string" ? labels.get(op.target) : addresses.get(op.target);
          if (
            typeof op.target !== "string" &&
            (target === undefined || target >= numericTargetMustBeBeforeIndex)
          ) return true;
          return (target === undefined ? true : visit(typeof op.target === "string" ? target + 1 : target, returnStack)) ||
            visit(index + 1, returnStack);
        }
        case "call": {
          const target = typeof op.target === "string" ? labels.get(op.target) : addresses.get(op.target);
          if (
            typeof op.target !== "string" &&
            (target === undefined || target >= numericTargetMustBeBeforeIndex)
          ) return true;
          if (target === undefined || returnStack.length >= 5) return true;
          return visit(typeof op.target === "string" ? target + 1 : target, [index + 1, ...returnStack]);
        }
        case "indirect-jump": {
          const target = knownIndirectFlowTarget(op);
          const targetIndex = addressStableFlowTargetIndex(addresses, target, numericTargetMustBeBeforeIndex);
          return targetIndex === undefined ? true : visit(targetIndex, returnStack);
        }
        case "indirect-call": {
          const target = knownIndirectFlowTarget(op);
          const targetIndex = addressStableFlowTargetIndex(addresses, target, numericTargetMustBeBeforeIndex);
          if (targetIndex === undefined || returnStack.length >= 5) return true;
          return visit(targetIndex, [index + 1, ...returnStack]);
        }
        case "indirect-cjump": {
          const target = knownIndirectFlowTarget(op);
          const targetIndex = addressStableFlowTargetIndex(addresses, target, numericTargetMustBeBeforeIndex);
          return (targetIndex === undefined ? true : visit(targetIndex, returnStack)) ||
            visit(index + 1, returnStack);
        }
        case "return": {
          if (returnStack.length > 0) return visit(returnStack[0]!, returnStack.slice(1));
          return true;
        }
        case "recall":
        case "indirect-recall":
        case "stop":
          return false;
      }
    }
    return false;
  };
  return visit(start);
}

function dotRestoreOp(value: string, source: IrOp): IrOp {
  const sourceComment = "meta" in source ? source.meta.comment : undefined;
  return {
    kind: "plain",
    opcode: DOT,
    meta: {
      mnemonic: ".",
      comment: [sourceComment, `restore literal ${value} from hidden X2 temp`].filter(Boolean).join("; "),
    },
  };
}

function replacingLiteralStackLiftCanExpose(
  ops: readonly IrOp[],
  runStart: number,
  run: NumericLiteralRun,
  state: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext,
  invalidatedProducerIndexes: ReadonlySet<number>,
): boolean {
  return planX2ReplacementStackLift(
    ops,
    runStart,
    run.end,
    state,
    context,
    replacingNumberEntryCanExposeStackLift(ops, run.end, {
      numericTargetMustBeBeforeIndex: runStart,
    }),
    { invalidatedProducerIndexes },
  ).exposesStackLift;
}

function replacingExpressionStackLiftCanExpose(
  ops: readonly IrOp[],
  runStart: number,
  run: UnaryExpressionRun,
  state: X2ValueDataflowState | undefined,
  context: DirectReturnAnalysisContext,
  invalidatedProducerIndexes: ReadonlySet<number>,
): boolean {
  return planX2ReplacementStackLift(
    ops,
    runStart,
    run.sourceStackEnd,
    state,
    context,
    replacingNumberEntryCanExposeStackLift(ops, run.end, {
      numericTargetMustBeBeforeIndex: runStart,
    }),
    {
      allowDuplicateYStackProof: run.allowDuplicateYStackProof,
      invalidatedProducerIndexes,
    },
  ).exposesStackLift;
}

function markReplacedStackLiftProducers(
  ops: readonly IrOp[],
  start: number,
  end: number,
  output: Set<number>,
): void {
  for (let index = start; index < end; index += 1) {
    const op = ops[index];
    if (op !== undefined && analyzeX2StackEffect(op).stackLiftAndX2Sync) output.add(index);
  }
}

const run: IrPassFn = (ops) => {
  const x2ValueStates = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const dotSafeStates = computeX2DotRestoreGapStates(ops);
  const immediateSyncStates = computeX2ImmediateSyncStates(ops);
  const directReturnContext = directReturnAnalysisContext(ops);
  const vpReachabilityCache = new Map<string, boolean>();
  const replacedStackLiftProducerIndexes = new Set<number>();
  const terminalBoundaryLabels = labelIndexes(ops);
  const terminalBoundaryAddresses = addressIndexes(ops);
  const result: IrOp[] = [];
  let removed = 0;

  opLoop:
  for (let index = 0; index < ops.length; index += 1) {
    const state = x2ValueStates[index];
    for (const runAtIndex of literalRunsAt(ops, index)) {
      const exactX2Fact = x2ValueSetHasFact(state?.x2, runAtIndex.x2Fact);
      const visibleDecimalX2ValueFact = x2ValueSetHasRestoredVisibleDecimal(state?.x2, runAtIndex.x2Fact);
      const visibleDecimalX2ShapeFact = visibleDecimalX2ValueFact
        ? false
        : x2ValueShapeSetHasRestoredVisibleDecimal(state?.x2, state?.x2Shape, runAtIndex.x2Fact);
      const visibleDecimalX2DisplayValueFact = visibleDecimalX2ValueFact && !exactX2Fact;
      const visibleDecimalX2DotSafeShapeFact = visibleDecimalX2ShapeFact && !x2StateHasUnsafeDotRestoreShapeX2(state);
      const visibleDecimalX2Fact = visibleDecimalX2ValueFact ||
        visibleDecimalX2DotSafeShapeFact;
      const sourceProvesFreeStandingRestore = x2ValueSetHasNormalizedDecimalFact(state?.x2, runAtIndex.x2Fact) ||
        visibleDecimalX2Fact;
      if (
        x2StateIsClosedPlainContext(state) &&
        x2CanUseSourceDotRestoreAt(
          ops,
          index,
          state,
          dotSafeStates[index] === true,
          immediateSyncStates[index] === true,
          sourceProvesFreeStandingRestore,
          directReturnContext,
        ) &&
        (exactX2Fact || visibleDecimalX2Fact) &&
        !replacingLiteralStackLiftCanExpose(
          ops,
          index,
          runAtIndex,
          state,
          directReturnContext,
          replacedStackLiftProducerIndexes,
        ) &&
        !replacingLiteralCanExposeContextSensitiveRestore(
          ops,
          runAtIndex,
          state,
          index,
          directReturnContext,
          vpReachabilityCache,
          {
            value: exactX2Fact,
            displayValue: visibleDecimalX2DisplayValueFact,
            shape: visibleDecimalX2DotSafeShapeFact,
          },
        )
      ) {
        markReplacedStackLiftProducers(ops, index, runAtIndex.end, replacedStackLiftProducerIndexes);
        result.push(dotRestoreOp(runAtIndex.displayValue, ops[index]!));
        removed += runAtIndex.end - index;
        index = runAtIndex.end;
        continue opLoop;
      }
    }
    const expressionRun = unaryExpressionRunAt(ops, index, terminalBoundaryLabels, terminalBoundaryAddresses);
    const expressionSourceProvesFreeStandingRestore =
      x2StateHasSameDotRestoreValueInXAndX2(state) &&
      !x2StateHasUnsafeDotRestoreShapeX2(state);
    if (
      expressionRun !== undefined &&
      x2StateIsClosedPlainContext(state) &&
      x2ValueSetHasAnyFact(state?.x2, expressionRun.x2Facts) &&
      x2CanUseSourceDotRestoreAt(
        ops,
        index,
        state,
        dotSafeStates[index] === true,
        immediateSyncStates[index] === true,
        expressionSourceProvesFreeStandingRestore,
        directReturnContext,
      ) &&
      !replacingExpressionStackLiftCanExpose(
        ops,
        index,
        expressionRun,
        state,
        directReturnContext,
        replacedStackLiftProducerIndexes,
      ) &&
      !replacingLiteralCanExposeContextSensitiveRestore(
        ops,
        {
          end: expressionRun.end,
          displayValue: expressionRun.displayValue,
          x2Fact: expressionRun.x2Facts[0]!,
          dotPreservesVpEntrySource: false,
        },
        state,
        index,
        directReturnContext,
        vpReachabilityCache,
        {
          value: true,
          displayValue: false,
          shape: false,
        },
      )
    ) {
      markReplacedStackLiftProducers(ops, index, expressionRun.end, replacedStackLiftProducerIndexes);
      result.push(dotRestoreOp(expressionRun.displayValue, ops[index]!));
      removed += expressionRun.end - index;
      index = expressionRun.end;
      continue opLoop;
    }
    result.push(ops[index]!);
  }

  if (removed === 0) return emptyResult(ops);
  return {
    ops: result,
    applied: removed,
    optimizations: [
      {
        name: "x2-literal-restore",
        detail: `Replaced ${removed} repeated numeric literal cell${removed === 1 ? "" : "s"} with hidden X2 . restore(s).`,
      },
    ],
  };
};

function x2ValueSetHasAnyFact(
  input: ReadonlySet<X2ValueFact> | undefined,
  facts: readonly X2ValueFact[],
): boolean {
  for (const fact of facts) {
    if (x2ValueSetHasFact(input, fact)) return true;
  }
  return false;
}

export const x2LiteralRestore: IrPass = {
  name: "x2-literal-restore",
  run,
  layoutSafe: false,
};
