// Pure lowering helpers extracted from compiler.ts: cost models, AST
// matchers/builders, opcode tables, macro expanders, scratch-name generators.
// No dependency on EmitContext; safe to import from compiler.ts and lowering/*.
import { findOpcodeName, getOpcode, registerFromText, registerIndex } from "../opcodes.ts";
import { formalAddressInfo, parseFormalAddressOpcode } from "../formal-address.ts";
import { machineSupports, type MachineProfile } from "../machineProfile.ts";
import type {
  CandidateReport,
  ConditionAst,
  Diagnostic,
  DispatchCaseAst,
  ExpressionAst,
  ProgramAst,
  ProcAst,
  RegisterName,
  StateFieldAst,
  StatementAst,
  V2BoardAst,
} from "../types.ts";

export const DISPATCH_SCRATCH_PREFIX = "__dispatch_";

export const GRID4_MASK_SCRATCH_PREFIX = "__grid4_mask_";

export const BIT_MASK_SCRATCH_PREFIX = "__bit_mask_";

export const REPEATED_UNARY_UPDATE_ARG_PREFIX = "__mkpro_unary_arg_";

export interface XParamValueFunction {
  readonly param: string;
  readonly width: number;
  readonly line: number;
}

export const IF_SELECTOR_SCRATCH_PREFIX = "__if_selector_";

export const DISPLAY_EXPR_PREFIX = "__display_expr_";

export const SPATIAL_HIT_SCRATCH_PREFIX = "__spatial_hit_";

export const SPATIAL_COUNT_SCRATCH_PREFIX = "__spatial_count_";

export const PACKED_COUNTER_PREFIX = "__packed_counter_";

export const COORD_LIST_ITEM_PREFIX = "__coord_list_";

export const COORD_LIST_POINTER = "__coord_list_pointer";

export const COORD_LIST_COUNTER = "__coord_list_counter";

export const COORD_LIST_CURRENT = "__coord_list_current";

export const COORD_LIST_DX = "__coord_list_dx";

// A formatted coordinate report renders a `<prefix> CC <separator> N` screen as one
// MK-61 video-format word. The video mask (the literal separator/anchor segments)
// together with the cell scale and video-anchor exponents are jointly
// hardware-fitted to the exact screen layout — field widths and separators — so
// each supported layout is one verified descriptor rather than a free constant.
// Only on-hardware-verified layouts appear here; any other 4-item report shape
// falls back to the generic per-item display lowering.
export interface FormattedCoordReportFormat {
  prefix: string;
  cellWidth: number;
  separator: string;
  bearingWidth: number;
  mask: string;
  cellScaleExp: number;
  videoAnchorExp: number;
}

export const VERIFIED_COORD_REPORT_FORMATS: readonly FormattedCoordReportFormat[] = [
  { prefix: "--", cellWidth: 2, separator: "--", bearingWidth: 1, mask: "8,-00--_", cellScaleExp: 4, videoAnchorExp: 7 },
];

export const NEGATIVE_ZERO_DEGREE_SELECTOR_GE = "__mkpro_negative_zero_ge";

export const NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE = "1|-00";

export const STACK_UNARY_DERIVATION_OPCODES = {
  abs: [0x31, "К |x|"],
  sign: [0x32, "К ЗН"],
  int: [0x34, "К [x]"],
  frac: [0x35, "К {x}"],
  sqr: [0x22, "F x^2"],
} as const satisfies Record<string, readonly [number, string]>;

export type StackUnaryDerivationFn = keyof typeof STACK_UNARY_DERIVATION_OPCODES;

export interface StackUnaryDerivationCall {
  fn: StackUnaryDerivationFn;
  arg: ExpressionAst;
  opcode: number;
  mnemonic: string;
}

export type XParamProcLowering =
  | {
      param: string;
      first: Extract<StatementAst, { kind: "assign" }>;
      kind: "add";
      other: string;
    }
  | {
      param: string;
      first: Extract<StatementAst, { kind: "assign" }>;
      kind: "copy";
    }
  | {
      param: string;
      first: Extract<StatementAst, { kind: "assign" }>;
      kind: "expr";
    }
  | {
      param: string;
      first: Extract<StatementAst, { kind: "indexed_assign" }>;
      kind: "indexed";
    };

export interface XParamReturnDecay {
  param: string;
  factor: ExpressionAst;
  divisor: ExpressionAst;
  line: number;
}

export interface XParamStackStopRiskRead {
  param: string;
  display: string;
  showLine: number;
  line: number;
  risk: StackStopRiskMatch;
}

export type DisplaySourceItem = Extract<ProgramAst["displays"][number]["items"][number], { kind: "source" }>;

export interface DisplayField {
  kind: "source" | "literal";
  item?: DisplaySourceItem;
  name: string;
  width: number;
  value?: string;
}

export interface FormattedCoordReportTemplate {
  cell: DisplayField;
  bearing: DisplayField;
  format: FormattedCoordReportFormat;
}

export function matchEqualityConstantCondition(
  condition: ConditionAst,
): { expr: ExpressionAst; value: number } | undefined {
  if (condition.op !== "==") return undefined;
  const rightValue = numericLiteralValue(condition.right);
  if (rightValue !== undefined && Number.isInteger(rightValue) && numericLiteralValue(condition.left) === undefined) {
    return { expr: condition.left, value: rightValue };
  }
  const leftValue = numericLiteralValue(condition.left);
  if (leftValue !== undefined && Number.isInteger(leftValue) && numericLiteralValue(condition.right) === undefined) {
    return { expr: condition.right, value: leftValue };
  }
  return undefined;
}

export function expressionIsDeterministic(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
      return true;
    case "indexed":
      return expressionIsDeterministic(expr.index);
    case "unary":
      return expressionIsDeterministic(expr.expr);
    case "binary":
      return expressionIsDeterministic(expr.left) && expressionIsDeterministic(expr.right);
    case "call": {
      const name = expr.callee.toLowerCase();
      if (name === "random" || name === "entered") return false;
      return expr.args.every(expressionIsDeterministic);
    }
  }
}

export function invertCondition(condition: ConditionAst): ConditionAst {
  return {
    ...condition,
    op: invertComparisonOp(condition.op),
  };
}

export function invertComparisonOp(op: ConditionAst["op"]): ConditionAst["op"] {
  switch (op) {
    case "==":
      return "!=";
    case "!=":
      return "==";
    case "<":
      return ">=";
    case "<=":
      return ">";
    case ">":
      return "<=";
    case ">=":
      return "<";
  }
}

export interface RegisterAllocation {
  registers: Record<string, RegisterName>;
  constants: Record<string, RegisterName>;
  negativeZeroDegree?: RegisterName;
}

export interface CoordListCall {
  cell: ExpressionAst;
  items: Array<{ name: string }>;
}

export interface CoordListIndirectContext {
  cell: ExpressionAst;
  count: number;
  pointerStart: number;
  pointerRegister: RegisterName;
  counterRegister: RegisterName;
}

export function coordListHasConditionCall(condition: ConditionAst): CoordListCall | undefined {
  if (!isZeroExpression(condition.right) || condition.op !== "!=") return undefined;
  return coordListHasCall(condition.left);
}

export function coordListHasCall(expr: ExpressionAst): CoordListCall | undefined {
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "coord_list_has") return undefined;
  if (expr.args.length < 2) return undefined;
  const [cell, ...items] = expr.args;
  if (cell === undefined) return undefined;
  const identifiers = items.every((item): item is Extract<ExpressionAst, { kind: "identifier" }> => item.kind === "identifier");
  if (!identifiers) return undefined;
  return { cell, items: items.map((item) => ({ name: item.name })) };
}

export function coordListLineCountCall(expr: ExpressionAst): CoordListCall | undefined {
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "coord_list_line_count") return undefined;
  if (expr.args.length < 2) return undefined;
  const [cell, ...items] = expr.args;
  if (cell === undefined) return undefined;
  const identifiers = items.every((item): item is Extract<ExpressionAst, { kind: "identifier" }> => item.kind === "identifier");
  if (!identifiers) return undefined;
  return { cell, items: items.map((item) => ({ name: item.name })) };
}

export function sameCoordListCall(left: CoordListCall, right: CoordListCall): boolean {
  return expressionEquals(left.cell, right.cell) &&
    left.items.length === right.items.length &&
    left.items.every((item, index) => item.name === right.items[index]?.name);
}

export function coordListItemInfo(name: string): { listName: string; index: number } | undefined {
  if (!name.startsWith(COORD_LIST_ITEM_PREFIX)) return undefined;
  const match = /^__coord_list_(.+)_(\d+)$/u.exec(name);
  if (!match) return undefined;
  return { listName: match[1]!, index: Number(match[2]) };
}

export function isPreincrementIndirectRegister(register: RegisterName): boolean {
  return register === "4" || register === "5" || register === "6";
}

// Indirect addressing through R0..R3 pre-decrements the register by one and
// reaches 0 from 1, unlike F Lx which clamps a positive counter at 1. From 0
// the hardware writes the negative sentinel, so general standalone decrements
// must prove the zero edge is not observable; terminal underflow fusions may use
// the sentinel only when the negative path cannot return.
export function isPredecrementIndirectRegister(register: RegisterName): boolean {
  return register === "0" || register === "1" || register === "2" || register === "3";
}

export function programHasLineCountForMask(ast: ProgramAst, maskName: string): boolean {
  let found = false;
  const visitExpr = (expr: ExpressionAst): void => {
    if (found) return;
    if (
      expr.kind === "call" &&
      expr.callee.toLowerCase() === "line_count" &&
      expr.args[0]?.kind === "identifier" &&
      expr.args[0].name === maskName
    ) {
      found = true;
      return;
    }
    if (expr.kind === "unary") visitExpr(expr.expr);
    if (expr.kind === "binary") {
      visitExpr(expr.left);
      visitExpr(expr.right);
    }
    if (expr.kind === "call") {
      for (const arg of expr.args) visitExpr(arg);
    }
  };
  const visitStatements = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (found) return;
      if (statement.kind === "pause" || statement.kind === "preview" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) {
          visitExpr(dispatchCase.value);
          visitStatements(dispatchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitStatements(entry.body);
  for (const proc of ast.procs) visitStatements(proc.body);
  return found;
}

export function programUsesFormattedCoordReport(ast: ProgramAst): boolean {
  return formattedCoordReportFormatForProgram(ast) !== undefined;
}

// The mask preload register is shared across the whole program, so the setup
// emitter needs the report format actually in use; return the format of the
// first formatted-report screen, if any.
export function formattedCoordReportFormatForProgram(ast: ProgramAst): FormattedCoordReportFormat | undefined {
  for (const display of ast.displays) {
    const template = formattedCoordReportDisplayTemplate(display);
    if (template !== undefined) return template.format;
  }
  return undefined;
}

export function formattedCoordReportDisplayTemplate(
  display: ProgramAst["displays"][number],
): FormattedCoordReportTemplate | undefined {
  const [prefix, cell, separator, bearing] = display.items;
  if (
    display.items.length !== 4 ||
    prefix?.kind !== "literal" ||
    cell?.kind !== "source" ||
    separator?.kind !== "literal" ||
    bearing?.kind !== "source"
  ) {
    return undefined;
  }
  // Recognize an arbitrary `<literal> cell:width <literal> bearing:width` report,
  // then gate it on a verified layout descriptor: the video mask and exponents
  // are hardware-fitted to the exact separators and field widths, so only
  // verified layouts lower (others fall through to the generic display path).
  const prefixText = normalizeDisplayTemplateLiteral(prefix.text);
  const separatorText = normalizeDisplayTemplateLiteral(separator.text);
  const cellWidth = cell.width ?? 2;
  const bearingWidth = bearing.width ?? 1;
  const format = VERIFIED_COORD_REPORT_FORMATS.find((candidate) =>
    candidate.prefix === prefixText &&
    candidate.separator === separatorText &&
    candidate.cellWidth === cellWidth &&
    candidate.bearingWidth === bearingWidth,
  );
  if (format === undefined) return undefined;
  return {
    cell: { kind: "source", item: cell, name: cell.name, width: cellWidth },
    bearing: { kind: "source", item: bearing, name: bearing.name, width: bearingWidth },
    format,
  };
}

export function normalizeDisplayTemplateLiteral(text: string): string {
  return text.replace(/\s/gu, "");
}

export function displayLoopOpcode(register: 0 | 1 | 2 | 3): number {
  switch (register) {
    case 0:
      return 0x5d;
    case 1:
      return 0x5b;
    case 2:
      return 0x58;
    case 3:
      return 0x5a;
  }
}

export type DisplayLiteralProgram =
  | { kind: "error" }
  | { kind: "kinv"; digits: string; negative: boolean }
  | { kind: "xor"; left: string; right: string; negative: boolean };

export interface FirstSpliceDisplayLiteralProgram {
  first: number;
  second?: number;
  body: Exclude<DisplayLiteralProgram, { kind: "error" }>;
  exponent: number;
  negative?: boolean;
}

export interface SignDigitLiteralDisplayProgram {
  signDigit: number;
  first: string;
  start: string;
  indirectSteps: number;
}

interface LeadingZeroHexProductPlan {
  sourceLiteral: string;
  factor: string;
}

const LEADING_ZERO_HEX_PRODUCT_ROWS: ReadonlyArray<readonly [string, number, string]> = [
  ["-", 10, "00"],
  ["-", 12, "04"],
  ["-", 14, "08"],
  ["-", 16, "000"],
  ["-", 17, "010"],
  ["-", 18, "020"],
  ["-", 19, "030"],
  ["-", 35, "030"],
  ["-", 36, "040"],
  ["-", 37, "050"],
  ["L", 15, "021"],
  ["L", 16, "032"],
  ["L", 17, "043"],
  ["L", 18, "054"],
  ["L", 29, "015"],
  ["С", 15, "052"],
  ["С", 26, "024"],
  ["С", 27, "020"],
  ["С", 28, "032"],
  ["С", 29, "044"],
  ["Г", 25, "053"],
  ["Г", 26, "050"],
  ["Г", 37, "033"],
  ["Г", 38, "030"],
  ["Г", 39, "043"],
  ["Е", 35, "042"],
  ["Е", 36, "040"],
  ["Е", 37, "054"],
];

export function displayLiteralProgram(text: string): DisplayLiteralProgram | undefined {
  const normalized = normalizeDisplayLiteralText(text);
  const errorCells = displayLiteralCells(normalized);
  if (errorCells !== undefined && isErrorLiteralCells(errorCells)) return { kind: "error" };

  const negative = normalized.startsWith("-") && normalized.length > 1;
  const body = negative ? normalized.slice(1) : normalized;
  const cells = displayLiteralCells(body);
  return displayLiteralProgramFromCells(cells, negative);
}

export function displayLiteralProgramFromCells(
  cells: readonly number[] | undefined,
  negative: boolean,
): DisplayLiteralProgram | undefined {
  if (cells === undefined || cells.length === 0 || cells.length > 8) return undefined;
  if (cells[0] !== 8) return undefined;

  const inverted = displayLiteralInversionDigits(cells);
  if (inverted !== undefined) return { kind: "kinv", digits: inverted, negative };

  const left: string[] = [];
  const right: string[] = [];
  for (let index = 0; index < cells.length; index += 1) {
    const pair = index === 0 ? [1, 9] as const : decimalXorPair(cells[index]!);
    if (pair === undefined) return undefined;
    left.push(String(pair[0]));
    right.push(String(pair[1]));
  }
  return { kind: "xor", left: left.join(""), right: right.join(""), negative };
}

export function firstSpliceDisplayLiteralProgram(text: string): FirstSpliceDisplayLiteralProgram | undefined {
  const cells = displayLiteralCells(text);
  if (cells === undefined || cells.length === 0 || cells.length > 8) return undefined;
  return firstSpliceDisplayLiteralProgramFromCells(
    cells,
    displayLiteralPointExponent(text) ?? cells.length - 1,
    false,
  );
}

export function firstSpliceDisplayLiteralProgramFromCells(
  cells: readonly number[],
  exponent: number,
  negative: boolean,
): FirstSpliceDisplayLiteralProgram | undefined {
  const first = cells[0]!;
  if (first === 15) return undefined;
  const body = displayLiteralProgramFromCells([8, ...cells.slice(1)], false);
  if (body === undefined || body.kind === "error") return undefined;
  const program: FirstSpliceDisplayLiteralProgram = { first, body, exponent };
  if (cells[1] !== undefined) program.second = cells[1];
  if (negative) program.negative = true;
  return program;
}

export function shouldUseFirstSpliceDisplayLiteral(text: string): boolean {
  if (firstSpliceDisplayLiteralProgram(text) === undefined) return false;
  if (decimalDisplayLiteralNumber(text) !== undefined) return false;
  if (zeroDigitTailDisplayProgram(text) !== undefined) return false;
  if (signDigitLiteralDisplayProgram(text) !== undefined) return false;
  const direct = displayLiteralProgram(text);
  return direct === undefined || direct.kind !== "error" && displayLiteralTrailingZeroExponent(text) !== undefined;
}

export function signedFirstSpliceDisplayLiteralProgram(text: string): FirstSpliceDisplayLiteralProgram | undefined {
  const normalized = normalizeDisplayLiteralText(text);
  if (!/^-[0-9]/u.test(normalized)) return undefined;
  const body = normalized.slice(1);
  const cells = displayLiteralCells(body);
  if (cells === undefined || cells.length === 0 || cells.length > 8) return undefined;
  return firstSpliceDisplayLiteralProgramFromCells(cells, displayLiteralPointExponent(body) ?? cells.length - 1, true);
}

export function exponentTailDisplayLiteralProgram(text: string): FirstSpliceDisplayLiteralProgram | undefined {
  const cells = displayLiteralCells(text);
  if (cells === undefined || cells.length !== 9) return undefined;
  const exponent = cells.at(-1);
  if (exponent === undefined || exponent < 0 || exponent > 9) return undefined;
  return firstSpliceDisplayLiteralProgramFromCells(cells.slice(0, 8), exponent, false);
}

export function shouldUsePreloadedDisplayLiteral(text: string): boolean {
  if (decimalDisplayLiteralNumber(text) !== undefined) return false;
  if (zeroDigitTailDisplayProgram(text) !== undefined) return false;
  if (signDigitLiteralDisplayProgram(text) !== undefined) return false;
  const firstSplice =
    signedFirstSpliceDisplayLiteralProgram(text) ??
    exponentTailDisplayLiteralProgram(text) ??
    firstSpliceDisplayLiteralProgram(text);
  if (firstSplice?.first === 0) return false;
  const direct = displayLiteralProgram(text);
  if (direct !== undefined && direct.kind !== "error") return shouldUseFirstSpliceDisplayLiteral(text);
  return shouldUseFirstSpliceDisplayLiteral(text) ||
    signedFirstSpliceDisplayLiteralProgram(text) !== undefined ||
    exponentTailDisplayLiteralProgram(text) !== undefined;
}

export function displayLiteralPointExponent(text: string): number | undefined {
  const normalized = normalizeDisplayLiteralText(text);
  const point = /[.,]/u.exec(normalized);
  if (point === null) return undefined;
  const prefix = normalized.slice(0, point.index);
  const cells = displayLiteralCells(prefix);
  if (cells === undefined || cells.length === 0) return undefined;
  return cells.length - 1;
}

export function displayLiteralTrailingZeroExponent(text: string): number | undefined {
  const cells = displayLiteralCells(text);
  if (cells === undefined || cells.length === 0 || cells.length > 8) return undefined;
  return cells.at(-1) === 0 ? cells.length - 1 : undefined;
}

export function decimalDisplayLiteralNumber(text: string): string | undefined {
  const normalized = normalizeDisplayLiteralText(text);
  if (!/^-?(?:0|[1-9][0-9]{0,7})$/u.test(normalized)) return undefined;
  return normalized;
}

export function leadingZeroHexProductDisplayProgram(text: string): LeadingZeroHexProductPlan | undefined {
  const normalized = normalizeDisplayLiteralText(text);
  const match = LEADING_ZERO_HEX_PRODUCT_ROWS.find(([, , output]) => output === normalized);
  if (match === undefined) return undefined;
  return { sourceLiteral: match[0], factor: String(match[1]) };
}

export function zeroDigitTailDisplayProgram(text: string): { input: number } | undefined {
  const cells = displayLiteralCells(text);
  if (cells === undefined || cells.length !== 2) return undefined;
  const [signDigit, tail] = cells;
  if (signDigit === undefined || signDigit < 2 || signDigit > 9 || tail !== 14) return undefined;
  return { input: signDigit - 1 };
}

export function signDigitLiteralDisplayProgram(text: string): SignDigitLiteralDisplayProgram | undefined {
  const cells = displayLiteralCells(text);
  if (cells === undefined || cells.length !== 9) return undefined;
  const signDigit = cells[0];
  if (signDigit === undefined || signDigit < 2 || signDigit > 9) return undefined;

  const body = cells.slice(1);
  const first = body[0];
  if (first === undefined || !((first >= 0 && first <= 9) || first === 14)) return undefined;
  const lower = body.slice(1);
  if (lower.length !== 7 || !lower.every((cell) => cell >= 0 && cell <= 9)) return undefined;

  const indirectSteps = signDigit - 1;
  const targetLower = Number.parseInt(lower.join(""), 10);
  const startLower = targetLower - indirectSteps;
  if (!Number.isSafeInteger(startLower) || startLower < 0 || startLower > 9999999) return undefined;
  return {
    signDigit,
    first: first === 14 ? "Е" : String(first),
    start: `1${String(startLower).padStart(7, "0")}`,
    indirectSteps,
  };
}

export function isErrorLiteralCells(cells: readonly number[]): boolean {
  return cells.length === 5 &&
    cells[0] === 14 &&
    cells[1] === 13 &&
    cells[2] === 13 &&
    cells[3] === 0 &&
    cells[4] === 13;
}

export function displayLiteralInversionDigits(cells: readonly number[]): string | undefined {
  if (cells.length === 0 || cells[0] !== 8) return undefined;
  const digits = ["1"];
  for (const cell of cells.slice(1)) {
    if (cell < 6 || cell > 15) return undefined;
    digits.push(String(15 - cell));
  }
  return digits.join("");
}

export function displayLiteralCells(text: string): number[] | undefined {
  const cells: number[] = [];
  const normalized = normalizeDisplayLiteralText(text);
  for (const char of normalized) {
    if (char === "." || char === ",") continue;
    if (/\s/u.test(char) || char === "_") {
      cells.push(15);
      continue;
    }
    if (/[0-9]/u.test(char)) {
      cells.push(Number(char));
      continue;
    }
    const symbol = DISPLAY_LITERAL_SYMBOLS[char];
    if (symbol === undefined) return undefined;
    cells.push(symbol);
  }
  return cells;
}

export function normalizeDisplayLiteralText(text: string): string {
  return text
    .replace(/[–—]/gu, "-")
    .replace(/[ОO]/gu, "0")
    .replace(/[Л]/gu, "L")
    .replace(/[ВB]/gu, "L")
    .replace(/[C]/gu, "С")
    .replace(/[D]/gu, "Г")
    .replace(/[G]/gu, "Г")
    .replace(/[E]/gu, "Е");
}

export const DISPLAY_LITERAL_SYMBOLS: Record<string, number> = {
  "-": 10,
  L: 11,
  "С": 12,
  "Г": 13,
  "Е": 14,
};

export function decimalXorPair(value: number): readonly [number, number] | undefined {
  for (let left = 0; left <= 9; left += 1) {
    for (let right = 0; right <= 9; right += 1) {
      if ((left ^ right) === value) return [left, right];
    }
  }
  return undefined;
}

export function normalizeConstantLiteral(raw: string): string {
  const value = Number(raw);
  return Number.isFinite(value) ? String(value) : raw.trim();
}

export function positiveIntegerPowerOfTenExponent(normalized: string): number | undefined {
  const trimmed = normalized.trim();
  if (!/^10+$/u.test(trimmed)) return undefined;
  return trimmed.length - 1;
}

export function negatedNumberLiteral(raw: string): string {
  const normalized = raw.trim();
  return normalized.startsWith("-") ? normalized.slice(1) : `-${normalized}`;
}

export function grid4MaskScratchName(statement: StatementAst): string {
  return `${GRID4_MASK_SCRATCH_PREFIX}${statement.line}`;
}

export function dispatchExpressionRegister(
  statement: Extract<StatementAst, { kind: "dispatch" }>,
  allocation: RegisterAllocation,
): RegisterName | undefined {
  if (statement.expr.kind !== "identifier") return undefined;
  return allocation.registers[statement.expr.name];
}

// True when the dispatch will be lowered through the numeric residual compare
// chain, which evaluates the selector once and keeps the running residual in X
// across cases (mismatched cases skip their body, so X survives). That lowering
// needs NO scratch register even for a non-identifier selector, so the allocator
// must not reserve one for it. Mirrors compileNumericResidualDispatchCompareChain.
export function dispatchUsesNumericResidualChain(statement: Extract<StatementAst, { kind: "dispatch" }>): boolean {
  if (statement.cases.length < 2) return false;
  const values = statement.cases.map((dispatchCase) => numericLiteralValue(dispatchCase.value));
  if (values.some((value) => value === undefined)) return false;
  return numericResidualDispatchIsCheaper(statement, values as number[]);
}

export function numericResidualDispatchIsCheaper(
  statement: Extract<StatementAst, { kind: "dispatch" }>,
  values: readonly number[],
): boolean {
  const sourceRegister = statement.expr.kind === "identifier";
  let ordinary = estimateExpressionCost(statement.expr) + (sourceRegister ? 0 : 1);
  for (let index = 0; index < values.length; index += 1) {
    const value = values[index]!;
    ordinary += (index > 0 ? 1 : 0) + (value === 0 ? 0 : estimateNumberCost(String(value)) + 1) + 2;
  }

  let residual = estimateExpressionCost(statement.expr);
  let comparedValue = 0;
  let hasComparedValue = false;
  for (const value of values) {
    if (!hasComparedValue) {
      residual += value === 0 ? 0 : estimateNumberCost(String(value)) + 1;
      hasComparedValue = true;
    } else {
      const delta = comparedValue - value;
      residual += delta === 0 ? 0 : estimateNumberCost(String(delta)) + 1;
    }
    residual += 2;
    comparedValue = value;
  }
  return residual <= ordinary;
}

export function isZeroExpression(expr: ExpressionAst): boolean {
  return expr.kind === "number" && Number(expr.raw) === 0;
}

export function isUnitDecrementExpression(target: string, expr: ExpressionAst): boolean {
  return expr.kind === "binary" &&
    expr.op === "-" &&
    expr.left.kind === "identifier" &&
    expr.left.name === target &&
    expr.right.kind === "number" &&
    Number(expr.right.raw) === 1;
}

export function isUnitIncrementExpression(target: string, expr: ExpressionAst): boolean {
  const delta = matchTargetPlusDelta(expr, target);
  return delta !== undefined && isNumericValue(delta, 1);
}

export function matchResidualGuardedUpdate(
  statement: Extract<StatementAst, { kind: "if" }>,
): {
  condition: ConditionAst;
  assignment: Extract<StatementAst, { kind: "assign" }>;
  tail: StatementAst[];
  target: string;
  bound: number;
  delta: number;
} | undefined {
  const condition = statement.condition;
  if (condition.op !== "<" && condition.op !== ">=") return undefined;
  if (condition.left.kind !== "identifier") return undefined;
  const bound = numericLiteralValue(condition.right);
  if (bound === undefined) return undefined;
  const target = condition.left.name;

  for (let index = 0; index < statement.thenBody.length; index += 1) {
    const candidate = statement.thenBody[index]!;
    if (candidate.kind !== "assign") return undefined;
    if (candidate.target !== target) {
      if (expressionReferencesIdentifier(candidate.expr, target)) return undefined;
      continue;
    }

    const delta = matchNumericSelfUpdate(target, candidate.expr);
    if (delta === undefined || delta === 0) return undefined;

    return {
      condition,
      assignment: candidate,
      tail: [
        ...statement.thenBody.slice(0, index),
        ...statement.thenBody.slice(index + 1),
      ],
      target,
      bound,
      delta,
    };
  }
  return undefined;
}

export function matchNumericSelfUpdate(target: string, expr: ExpressionAst): number | undefined {
  if (expr.kind !== "binary") return undefined;
  if (expr.op === "+") {
    if (expr.left.kind === "identifier" && expr.left.name === target) return numericLiteralValue(expr.right);
    if (expr.right.kind === "identifier" && expr.right.name === target) return numericLiteralValue(expr.left);
    return undefined;
  }
  if (
    expr.op === "-" &&
    expr.left.kind === "identifier" &&
    expr.left.name === target
  ) {
    const value = numericLiteralValue(expr.right);
    return value === undefined ? undefined : -value;
  }
  return undefined;
}

export function decrementBranchTestsZero(condition: ConditionAst, target: string): boolean {
  return condition.left.kind === "identifier" &&
    condition.left.name === target &&
    (condition.op === "<=" || condition.op === "==") &&
    isZeroExpression(condition.right);
}

export function flOpcode(register: RegisterName): number | undefined {
  switch (register) {
    case "0":
      return 0x5d;
    case "1":
      return 0x5b;
    case "2":
      return 0x58;
    case "3":
      return 0x5a;
    default:
      return undefined;
  }
}

export function isSimpleStackLoad(expr: ExpressionAst): boolean {
  return expr.kind === "identifier" || expr.kind === "number";
}

export function canTestAgainstZeroDirectly(op: ConditionAst["op"]): boolean {
  return op === "==" || op === "!=" || op === ">=" || op === "<";
}

export function directTestOpcode(op: ConditionAst["op"]): number {
  switch (op) {
    case "==":
      return 0x5e;
    case "!=":
      return 0x57;
    case ">=":
      return 0x59;
    case "<":
      return 0x5c;
    default:
      throw new Error(`No direct zero-test opcode for ${op}`);
  }
}

export function selectCheaperEquivalentCondition(
  condition: ConditionAst,
  ast: ProgramAst,
  preloadedConstants?: ReadonlySet<string>,
): { condition: ConditionAst; changed: boolean } {
  let best = condition;
  let bestCost = conditionCompileCost(condition, preloadedConstants);
  for (const candidate of equivalentConditionCandidates(condition, ast)) {
    const cost = conditionCompileCost(candidate, preloadedConstants);
    if (cost < bestCost) {
      best = candidate;
      bestCost = cost;
    }
  }
  return { condition: best, changed: !conditionEquals(best, condition) };
}

export function equivalentConditionCandidates(condition: ConditionAst, ast: ProgramAst): ConditionAst[] {
  const candidates: ConditionAst[] = [];
  const add = (candidate: ConditionAst): void => {
    if (!candidates.some((existing) => conditionEquals(existing, candidate))) candidates.push(candidate);
  };
  add(condition);
  const flipped = flipNumericLeftCondition(condition);
  if (flipped !== undefined) add(flipped);
  for (const candidate of [...candidates]) {
    const negated = negateZeroDifferenceCondition(candidate);
    if (negated !== undefined) add(negated);
  }
  for (const candidate of [...candidates]) {
    for (const boundary of integerBoundaryCandidates(candidate, ast)) add(boundary);
  }
  return candidates;
}

export function flipNumericLeftCondition(condition: ConditionAst): ConditionAst | undefined {
  if (condition.left.kind !== "number") return undefined;
  return {
    left: condition.right,
    op: flipComparisonOp(condition.op),
    right: condition.left,
  };
}

export function flipComparisonOp(op: ConditionAst["op"]): ConditionAst["op"] {
  switch (op) {
    case "<":
      return ">";
    case "<=":
      return ">=";
    case ">":
      return "<";
    case ">=":
      return "<=";
    case "==":
    case "!=":
      return op;
  }
}

export function negateZeroDifferenceCondition(condition: ConditionAst): ConditionAst | undefined {
  if (!isZeroExpression(condition.right)) return undefined;
  if (condition.left.kind !== "binary" || condition.left.op !== "-") return undefined;
  if (!expressionIsDeterministic(condition.left.left) || !expressionIsDeterministic(condition.left.right)) {
    return undefined;
  }
  return {
    left: {
      kind: "binary",
      op: "-",
      left: condition.left.right,
      right: condition.left.left,
    },
    op: flipComparisonOp(condition.op),
    right: condition.right,
  };
}

export function integerBoundaryCandidates(condition: ConditionAst, ast: ProgramAst): ConditionAst[] {
  if (!isKnownIntegerExpression(condition.left, ast)) return [];
  const value = numericLiteralValue(condition.right);
  if (value === undefined || !Number.isSafeInteger(value)) return [];
  const shifted = shiftedIntegerBoundary(condition.op, value);
  if (shifted === undefined) return [];
  return [{
    left: condition.left,
    op: shifted.op,
    right: numberExpression(shifted.value),
  }];
}

export function shiftedIntegerBoundary(
  op: ConditionAst["op"],
  value: number,
): { op: ConditionAst["op"]; value: number } | undefined {
  switch (op) {
    case "<":
      return Number.isSafeInteger(value - 1) ? { op: "<=", value: value - 1 } : undefined;
    case "<=":
      return Number.isSafeInteger(value + 1) ? { op: "<", value: value + 1 } : undefined;
    case ">":
      return Number.isSafeInteger(value + 1) ? { op: ">=", value: value + 1 } : undefined;
    case ">=":
      return Number.isSafeInteger(value - 1) ? { op: ">", value: value - 1 } : undefined;
    case "==":
    case "!=":
      return undefined;
  }
}

export function isKnownIntegerExpression(expr: ExpressionAst, ast: ProgramAst): boolean {
  return expr.kind === "identifier" && integerRangeFor(expr.name, ast) !== undefined;
}

export function isKnownIntegerValuedExpression(expr: ExpressionAst, ast: ProgramAst): boolean {
  if (expr.kind === "number") return Number.isSafeInteger(Number(expr.raw));
  if (expr.kind === "identifier") return integerRangeFor(expr.name, ast) !== undefined;
  if (expr.kind === "unary" && expr.op === "-") return isKnownIntegerValuedExpression(expr.expr, ast);
  if (expr.kind === "call" && expr.args.length === 1) {
    const name = expr.callee.toLowerCase();
    if (name === "int") return true;
    if (name === "abs") return isKnownIntegerValuedExpression(expr.args[0]!, ast);
  }
  if (expr.kind === "binary" && (expr.op === "+" || expr.op === "-" || expr.op === "*")) {
    return isKnownIntegerValuedExpression(expr.left, ast) && isKnownIntegerValuedExpression(expr.right, ast);
  }
  return false;
}

export function numericRangeForExpression(expr: ExpressionAst, ast: ProgramAst): { min?: number; max?: number } | undefined {
  const value = numericLiteralValue(expr);
  if (value !== undefined) return { min: value, max: value };
  if (expr.kind === "identifier") return numericRangeFor(expr.name, ast);
  if (expr.kind === "unary" && expr.op === "-") {
    const range = numericRangeForExpression(expr.expr, ast);
    if (range === undefined) return undefined;
    return {
      ...(range.max === undefined ? {} : { min: -range.max }),
      ...(range.min === undefined ? {} : { max: -range.min }),
    };
  }
  if (expr.kind === "call" && expr.callee.toLowerCase() === "abs" && expr.args.length === 1) {
    const range = numericRangeForExpression(expr.args[0]!, ast);
    if (range === undefined || range.min === undefined || range.max === undefined) return undefined;
    return {
      min: range.min <= 0 && range.max >= 0 ? 0 : Math.min(Math.abs(range.min), Math.abs(range.max)),
      max: Math.max(Math.abs(range.min), Math.abs(range.max)),
    };
  }
  return undefined;
}

export function conditionCompileCost(condition: ConditionAst, preloadedConstants?: ReadonlySet<string>): number {
  if (
    isZeroExpression(condition.right) &&
    (condition.op === "==" || condition.op === "!=") &&
    condition.left.kind === "binary"
  ) {
    const remainder = matchRemainderByConstant(condition.left);
    if (remainder !== undefined && numericLiteralValue(remainder.divisor) !== 0) {
      return estimateExpressionCostForCondition(remainder.value, preloadedConstants) +
        estimateExpressionCostForCondition(remainder.divisor, preloadedConstants) +
        4;
    }
  }
  if (isZeroExpression(condition.right) && canTestAgainstZeroDirectly(condition.op)) {
    return estimateExpressionCostForCondition(condition.left, preloadedConstants) + 2;
  }
  return estimateExpressionCostForCondition(condition.left, preloadedConstants) +
    estimateExpressionCostForCondition(condition.right, preloadedConstants) +
    3;
}

export function estimateSmallSetConditionCost(
  match: SmallSetConditionMatch,
  preloadedConstants: ReadonlySet<string>,
): number {
  return match.tests.reduce(
    (sum, test) => sum + estimateExpressionCostForCondition(test.expr, preloadedConstants) + 2,
    0,
  );
}

export function conditionEquals(left: ConditionAst, right: ConditionAst): boolean {
  return left.op === right.op && expressionEquals(left.left, right.left) && expressionEquals(left.right, right.right);
}

export function conditionToText(condition: ConditionAst): string {
  return `${expressionToIntentText(condition.left)} ${condition.op} ${expressionToIntentText(condition.right)}`;
}

export function expressionToIntentText(expr: ExpressionAst): string {
  switch (expr.kind) {
    case "number":
      return expr.raw;
    case "string":
      return JSON.stringify(expr.text);
    case "identifier":
      return expr.name;
    case "indexed": {
      const member = expr.field === undefined ? "" : `.${expr.field}`;
      return `${expr.base}[${expressionToIntentText(expr.index)}]${member}`;
    }
    case "unary":
      return `-${wrapExpressionText(expr.expr, 3)}`;
    case "binary":
      return `${wrapExpressionText(expr.left, binaryPrecedence(expr.op))} ${expr.op} ${wrapExpressionText(expr.right, binaryPrecedence(expr.op) + (expr.op === "-" || expr.op === "/" ? 1 : 0))}`;
    case "call":
      return `${expr.callee}(${expr.args.map(expressionToIntentText).join(", ")})`;
  }
}

export function wrapExpressionText(expr: ExpressionAst, parentPrecedence: number): string {
  const text = expressionToIntentText(expr);
  const precedence = expressionPrecedence(expr);
  return precedence < parentPrecedence ? `(${text})` : text;
}

export function expressionPrecedence(expr: ExpressionAst): number {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
    case "call":
    case "indexed":
      return 4;
    case "unary":
      return 3;
    case "binary":
      return binaryPrecedence(expr.op);
  }
}

export function binaryPrecedence(op: Extract<ExpressionAst, { kind: "binary" }>["op"]): number {
  return op === "*" || op === "/" ? 2 : 1;
}

export function buildDiagnostic(
  level: "warning" | "error",
  message: string,
  line?: number,
  code?: string,
): Diagnostic {
  const diagnostic: Diagnostic = { level, message };
  if (line !== undefined) diagnostic.line = line;
  if (code !== undefined) diagnostic.code = code;
  return diagnostic;
}

export type BranchRemovalCandidate = BranchAssignCandidate | BranchTerminalCandidate;

export interface GuardedUpdate {
  target: string;
  op: "+" | "-";
  delta: ExpressionAst;
}

export interface GuardedUpdateSelectorCandidate {
  selector: ExpressionAst;
  updates: GuardedUpdate[];
  name: string;
  detail: string;
  usesNegativeZero: boolean;
  usesComparison: boolean;
}

export interface BranchAssignCandidate {
  kind: "assign";
  target: string;
  expr: ExpressionAst;
  name: string;
  detail: string;
}

export interface BranchTerminalCandidate {
  kind: "pause" | "halt";
  expr: ExpressionAst;
  name: string;
  detail: string;
}

export function buildBranchRemovalCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
  options: { negativeZeroDegree?: boolean; comparisonSelectors?: boolean } = {},
): BranchRemovalCandidate | undefined {
  return buildTerminalSelectCandidate(statement, ast, options) ??
    buildComparisonBooleanCandidate(statement) ??
    buildBooleanAlgebraCandidate(statement, ast) ??
    buildAbsCandidate(statement) ??
    buildMaxMinCandidate(statement) ??
    buildClampCandidate(statement) ??
    buildSaturatingUpdateCandidate(statement, ast) ??
    buildBooleanSignToggleCandidate(statement, ast) ??
    buildBooleanUpdateCandidate(statement, ast, options) ??
    buildArithmeticIfSelect(statement, ast, options);
}

export function buildGuardedUpdateSelectorCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
  options: { negativeZeroDegree?: boolean } = {},
): GuardedUpdateSelectorCandidate | undefined {
  const updates = guardedUpdates(statement);
  if (updates === undefined) return undefined;

  const booleanSelector = booleanSelectorExpression(statement.condition, ast);
  const negativeZeroSelector = options.negativeZeroDegree
    ? negativeZeroThresholdSelectorExpression(statement.condition, ast)
    : undefined;
  const selector = booleanSelector ?? negativeZeroSelector ?? comparisonSelectorExpression(statement.condition);
  if (selector === undefined) return undefined;

  const usesNegativeZero = booleanSelector === undefined && negativeZeroSelector !== undefined;
  const usesComparison = booleanSelector === undefined && negativeZeroSelector === undefined;
  if (!usesNegativeZero && updates.length < 2) return undefined;
  return {
    selector,
    updates,
    name: usesNegativeZero
      ? "negative-zero-threshold-update"
      : usesComparison
        ? "comparison-guarded-update-selector"
        : "multi-guarded-update",
    detail: usesNegativeZero
      ? "Replaced threshold guarded update with a negative-zero selector"
      : usesComparison
        ? "Replaced comparison guarded updates with one stored arithmetic selector"
      : "Replaced guarded updates with one stored arithmetic selector",
    usesNegativeZero,
    usesComparison,
  };
}

export function guardedUpdates(statement: Extract<StatementAst, { kind: "if" }>): GuardedUpdate[] | undefined {
  if (statement.elseBody !== undefined || statement.thenBody.length === 0) return undefined;
  const updates: GuardedUpdate[] = [];
  for (const inner of statement.thenBody) {
    if (inner.kind !== "assign") return undefined;
    const plus = matchTargetPlusDelta(inner.expr, inner.target);
    if (plus !== undefined) {
      if (!expressionPureForSubstitution(plus)) return undefined;
      updates.push({ target: inner.target, op: "+", delta: plus });
      continue;
    }
    const minus = matchTargetMinusDelta(inner.expr, inner.target);
    if (minus !== undefined) {
      if (!expressionPureForSubstitution(minus)) return undefined;
      updates.push({ target: inner.target, op: "-", delta: minus });
      continue;
    }
    return undefined;
  }
  return updates;
}

export function maskedGuardedUpdateExpression(update: GuardedUpdate, selector: ExpressionAst): ExpressionAst {
  const current: ExpressionAst = { kind: "identifier", name: update.target };
  const delta = multiplyExpressions(update.delta, selector);
  return update.op === "+"
    ? addExpressions(current, delta)
    : subtractExpressions(current, delta);
}

export function buildDoubleClampCandidate(
  first: Extract<StatementAst, { kind: "if" }>,
  second: Extract<StatementAst, { kind: "if" }>,
): BranchAssignCandidate | undefined {
  const lower = clampBound(first, "lower");
  const upper = clampBound(second, "upper");
  if (!lower || !upper || lower.target !== upper.target) return undefined;
  const targetExpr: ExpressionAst = { kind: "identifier", name: lower.target };
  return {
    kind: "assign",
    target: lower.target,
    expr: minExpression(maxExpression(targetExpr, lower.bound), upper.bound),
    name: "arithmetic-if-double-clamp",
    detail: "Replaced adjacent lower/upper clamp branches with min(max())",
  };
}

export function clampBound(
  statement: Extract<StatementAst, { kind: "if" }>,
  direction: "lower" | "upper",
): { target: string; bound: ExpressionAst } | undefined {
  const assign = singleEffectiveAssign(statement);
  if (!assign) return undefined;
  const targetExpr: ExpressionAst = { kind: "identifier", name: assign.target };
  const { left, right, op } = statement.condition;
  if (!expressionEquals(left, targetExpr) || !expressionEquals(assign.expr, right)) return undefined;
  if (direction === "lower" && (op === "<" || op === "<=")) return { target: assign.target, bound: right };
  if (direction === "upper" && (op === ">" || op === ">=")) return { target: assign.target, bound: right };
  return undefined;
}

export function buildTerminalSelectCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
  options: { negativeZeroDegree?: boolean } = {},
): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return undefined;
  const thenStatement = effectiveTerminalStatement(statement.thenBody[0], ast);
  const elseStatement = effectiveTerminalStatement(statement.elseBody[0], ast);
  if (!thenStatement || !elseStatement) return undefined;
  if (elseStatement.kind !== thenStatement.kind) return undefined;

  const booleanSelector = booleanSelectorExpression(statement.condition, ast);
  const negativeZeroSelector = options.negativeZeroDegree
    ? negativeZeroThresholdSelectorExpression(statement.condition, ast)
    : undefined;
  const selector = booleanSelector ?? negativeZeroSelector ?? comparisonSelectorExpression(statement.condition);
  if (!selector) return undefined;
  const usesNegativeZero = booleanSelector === undefined && negativeZeroSelector !== undefined;
  return {
    kind: thenStatement.kind,
    expr: terminalSelectExpression(thenStatement.expr, elseStatement.expr, selector),
    name: usesNegativeZero ? "negative-zero-threshold-terminal-select" : "arithmetic-if-terminal-select",
    detail: usesNegativeZero
      ? `Replaced threshold ${thenStatement.kind} if/else with negative-zero selection`
      : `Replaced boolean ${thenStatement.kind} if/else with arithmetic selection`,
  };
}

export function effectiveTerminalStatement(
  statement: StatementAst | undefined,
  ast: ProgramAst,
): Extract<StatementAst, { kind: "pause" | "halt" }> | undefined {
  if (statement === undefined) return undefined;
  if (statement.kind === "pause") return statement;
  if (statement.kind === "halt") {
    return statement.literal === undefined && statement.display === undefined ? statement : undefined;
  }
  if (statement.kind !== "call") return undefined;
  const proc = ast.procs.find((candidate) => candidate.name === statement.block);
  if (proc === undefined || proc.body.length !== 1) return undefined;
  const terminal = proc.body[0];
  if (terminal?.kind === "pause") return terminal;
  if (terminal?.kind === "halt") {
    return terminal.literal === undefined && terminal.display === undefined ? terminal : undefined;
  }
  return undefined;
}

export function terminalSelectExpression(
  thenExpr: ExpressionAst,
  elseExpr: ExpressionAst,
  selector: ExpressionAst,
): ExpressionAst {
  const thenValue = numericLiteralValue(thenExpr);
  const elseValue = numericLiteralValue(elseExpr);
  if (thenValue !== undefined && elseValue !== undefined) {
    const delta = thenValue - elseValue;
    if (delta === 0) return numberExpression(thenValue);
    return addExpressions(
      numberExpression(elseValue),
      multiplyExpressions(numberExpression(delta), selector),
    );
  }
  return addExpressions(
    multiplyExpressions(thenExpr, selector),
    multiplyExpressions(elseExpr, oneMinus(selector)),
  );
}

export function comparisonSelectorExpression(condition: ConditionAst): ExpressionAst | undefined {
  const { left, right, op } = condition;
  switch (op) {
    case "==":
      return oneMinus(signExpression(absExpression(subtractExpressions(left, right))));
    case "!=":
      return signExpression(absExpression(subtractExpressions(left, right)));
    case ">":
      return maxExpression(numberExpression(0), signExpression(subtractExpressions(left, right)));
    case "<":
      return maxExpression(numberExpression(0), signExpression(subtractExpressions(right, left)));
    case ">=":
      return oneMinus(
        maxExpression(numberExpression(0), signExpression(subtractExpressions(right, left))),
      );
    case "<=":
      return oneMinus(
        maxExpression(numberExpression(0), signExpression(subtractExpressions(left, right))),
      );
    default:
      return undefined;
  }
}

export function negativeZeroThresholdSelectorExpression(condition: ConditionAst, ast: ProgramAst): ExpressionAst | undefined {
  const threshold = matchNegativeZeroThresholdCondition(condition, ast);
  if (threshold === undefined) return undefined;
  const selector: ExpressionAst = {
    kind: "call",
    callee: NEGATIVE_ZERO_DEGREE_SELECTOR_GE,
    args: [threshold.value, numberExpression(threshold.bound)],
  };
  return threshold.truth === "ge" ? selector : oneMinus(selector);
}

export function matchNegativeZeroThresholdCondition(
  condition: ConditionAst,
  ast: ProgramAst,
): { value: ExpressionAst; bound: number; truth: "ge" | "lt" } | undefined {
  if (condition.left.kind === "number") {
    const flipped = flipNumericLeftCondition(condition);
    return flipped === undefined ? undefined : matchNegativeZeroThresholdCondition(flipped, ast);
  }

  const value = condition.left;
  const bound = numericLiteralValue(condition.right);
  if (bound === undefined || !Number.isFinite(bound) || bound <= 0 || bound > 1e12) return undefined;
  if (!isKnownIntegerValuedExpression(value, ast)) return undefined;
  const range = numericRangeForExpression(value, ast);
  if (range === undefined || range.min === undefined || range.min < 0) return undefined;
  if (range.max !== undefined && range.max / bound >= 1e60) return undefined;

  switch (condition.op) {
    case ">=":
      return { value, bound, truth: "ge" };
    case "<":
      return { value, bound, truth: "lt" };
    case ">":
      return Number.isSafeInteger(bound + 1) ? { value, bound: bound + 1, truth: "ge" } : undefined;
    case "<=":
      return Number.isSafeInteger(bound + 1) ? { value, bound: bound + 1, truth: "lt" } : undefined;
    case "==":
    case "!=":
      return undefined;
  }
}

export function buildArithmeticIfSelect(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
  options: { negativeZeroDegree?: boolean } = {},
): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) {
    return undefined;
  }
  const thenAssign = statement.thenBody[0];
  const elseAssign = statement.elseBody[0];
  if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target) return undefined;

  const booleanSelector = booleanSelectorExpression(statement.condition, ast);
  const negativeZeroSelector = options.negativeZeroDegree
    ? negativeZeroThresholdSelectorExpression(statement.condition, ast)
    : undefined;
  const selector = booleanSelector ?? negativeZeroSelector;
  if (!selector) return undefined;
  const usesNegativeZero = booleanSelector === undefined && negativeZeroSelector !== undefined;

  const expr = addExpressions(
    multiplyExpressions(thenAssign.expr, selector),
    multiplyExpressions(elseAssign.expr, oneMinus(selector)),
  );
  return {
    kind: "assign",
    target: thenAssign.target,
    expr,
    name: usesNegativeZero ? "negative-zero-threshold-select" : "arithmetic-if-select",
    detail: usesNegativeZero
      ? "Replaced threshold if/else assignment with negative-zero selection"
      : "Replaced boolean if/else with arithmetic selection",
  };
}

export function buildComparisonBooleanCandidate(statement: Extract<StatementAst, { kind: "if" }>): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return undefined;
  const thenAssign = statement.thenBody[0];
  const elseAssign = statement.elseBody[0];
  if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target) return undefined;

  const thenValue = numericLiteralValue(thenAssign.expr);
  const elseValue = numericLiteralValue(elseAssign.expr);
  if (!((thenValue === 1 && elseValue === 0) || (thenValue === 0 && elseValue === 1))) return undefined;

  const truth = comparisonMask(statement.condition);
  if (!truth) return undefined;
  return {
    kind: "assign",
    target: thenAssign.target,
    expr: thenValue === 1 ? truth : oneMinus(truth),
    name: "arithmetic-if-comparison-mask",
    detail: "Replaced comparison-to-boolean branch with arithmetic mask",
  };
}

export function buildBooleanAlgebraCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return undefined;
  const thenAssign = statement.thenBody[0];
  const elseAssign = statement.elseBody[0];
  if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target) return undefined;

  const selector = booleanSelectorExpression(statement.condition, ast);
  const selectorName = booleanSelectorVariableName(statement.condition, ast);
  if (!selector || !selectorName) return undefined;
  const otherThen = booleanIdentifier(thenAssign.expr, ast);
  const otherElse = booleanIdentifier(elseAssign.expr, ast);
  const thenValue = numericLiteralValue(thenAssign.expr);
  const elseValue = numericLiteralValue(elseAssign.expr);
  if (otherThen && elseValue === 0) {
    return booleanAlgebraCandidate(thenAssign.target, multiplyExpressions(selector, otherThen), "and");
  }
  if (thenValue === 1 && otherElse) {
    return booleanAlgebraCandidate(thenAssign.target, maxExpression(selector, otherElse), "or");
  }
  if (otherThen && otherElse && expressionEquals(otherThen, otherElse)) return undefined;
  if (otherThen && otherElse && expressionEquals(thenAssign.expr, oneMinus(otherElse))) {
    return booleanAlgebraCandidate(thenAssign.target, absExpression(subtractExpressions(selector, otherElse)), "xor");
  }
  return undefined;
}

export function booleanAlgebraCandidate(target: string, expr: ExpressionAst, operation: string): BranchRemovalCandidate {
  return {
    kind: "assign",
    target,
    expr,
    name: "arithmetic-if-boolean-algebra",
    detail: `Replaced boolean ${operation.toUpperCase()} branch with arithmetic expression`,
  };
}

export function buildBooleanUpdateCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
  options: { comparisonSelectors?: boolean } = {},
): BranchRemovalCandidate | undefined {
  if (statement.elseBody || statement.thenBody.length !== 1) return undefined;
  const assign = statement.thenBody[0];
  if (assign?.kind !== "assign") return undefined;

  const booleanSelector = booleanSelectorExpression(statement.condition, ast);
  const comparisonSelector = booleanSelector === undefined && options.comparisonSelectors === true
    ? comparisonSelectorExpression(statement.condition)
    : undefined;
  const selector = booleanSelector ?? comparisonSelector;
  if (!selector) return undefined;
  const usesComparison = booleanSelector === undefined && comparisonSelector !== undefined;

  const current: ExpressionAst = { kind: "identifier", name: assign.target };
  const plus = matchTargetPlusDelta(assign.expr, assign.target);
  if (plus) {
    return {
      kind: "assign",
      target: assign.target,
      expr: addExpressions(current, multiplyExpressions(plus, selector)),
      name: usesComparison ? "arithmetic-if-comparison-update" : "arithmetic-if-update",
      detail: usesComparison
        ? "Replaced conditional addition with comparison-mask arithmetic"
        : "Replaced conditional addition with boolean-masked arithmetic",
    };
  }

  const minus = matchTargetMinusDelta(assign.expr, assign.target);
  if (minus) {
    return {
      kind: "assign",
      target: assign.target,
      expr: subtractExpressions(current, multiplyExpressions(minus, selector)),
      name: usesComparison ? "arithmetic-if-comparison-update" : "arithmetic-if-update",
      detail: usesComparison
        ? "Replaced conditional subtraction with comparison-mask arithmetic"
        : "Replaced conditional subtraction with boolean-masked arithmetic",
    };
  }

  if (usesComparison) return undefined;
  if (expressionEquals(assign.expr, negateExpression(current))) {
    return {
      kind: "assign",
      target: assign.target,
      expr: signToggleExpression(current, selector),
      name: "arithmetic-if-sign-toggle",
      detail: "Replaced conditional sign toggle with boolean-masked multiplier",
    };
  }

  return buildConditionalMoveCandidate(assign, selector);
}

export function buildBooleanSignToggleCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return undefined;
  const thenAssign = statement.thenBody[0];
  const elseAssign = statement.elseBody[0];
  if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target || !isIdentityAssignment(elseAssign)) return undefined;
  const selector = booleanSelectorExpression(statement.condition, ast);
  if (!selector) return undefined;
  const current: ExpressionAst = { kind: "identifier", name: thenAssign.target };
  if (!expressionEquals(thenAssign.expr, negateExpression(current))) return undefined;
  return {
    kind: "assign",
    target: thenAssign.target,
    expr: signToggleExpression(current, selector),
    name: "arithmetic-if-sign-toggle",
    detail: "Replaced conditional sign toggle with boolean-masked multiplier",
  };
}

export function buildConditionalMoveCandidate(
  assign: Extract<StatementAst, { kind: "assign" }>,
  selector: ExpressionAst,
): BranchRemovalCandidate | undefined {
  const current: ExpressionAst = { kind: "identifier", name: assign.target };
  if (expressionEquals(assign.expr, current)) return undefined;
  return {
    kind: "assign",
    target: assign.target,
    expr: addExpressions(
      multiplyExpressions(current, oneMinus(selector)),
      multiplyExpressions(assign.expr, selector),
    ),
    name: "arithmetic-if-conditional-move",
    detail: "Replaced conditional assignment with boolean-masked conditional move",
  };
}

export function buildSaturatingUpdateCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
): BranchRemovalCandidate | undefined {
  if (statement.elseBody || statement.thenBody.length !== 1) return undefined;
  const assign = statement.thenBody[0];
  if (assign?.kind !== "assign") return undefined;
  const range = integerRangeFor(assign.target, ast);
  if (!range) return undefined;

  const targetExpr: ExpressionAst = { kind: "identifier", name: assign.target };
  const { left, right, op } = statement.condition;
  if (!expressionEquals(left, targetExpr)) return undefined;

  const decrement = matchTargetMinusDelta(assign.expr, assign.target);
  if (decrement && op === ">" && isNumericValue(decrement, 1) && range.min !== undefined && isNumericValue(right, range.min)) {
    return maxCandidate(assign.target, assign.expr, right, "Replaced saturating decrement branch with max()");
  }

  const increment = matchTargetPlusDelta(assign.expr, assign.target);
  if (increment && op === "<" && isNumericValue(increment, 1) && range.max !== undefined && isNumericValue(right, range.max)) {
    return minCandidate(assign.target, assign.expr, right, "Replaced saturating increment branch with min-via-max()");
  }

  return undefined;
}

export function buildMaxMinCandidate(statement: Extract<StatementAst, { kind: "if" }>): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return undefined;
  const thenAssign = statement.thenBody[0];
  const elseAssign = statement.elseBody[0];
  if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target) return undefined;

  const { left, right, op } = statement.condition;
  if (["<", "<=", ">", ">="].includes(op)) {
    if ((op === ">" || op === ">=") && expressionEquals(thenAssign.expr, left) && expressionEquals(elseAssign.expr, right)) {
      return maxCandidate(thenAssign.target, left, right);
    }
    if ((op === "<" || op === "<=") && expressionEquals(thenAssign.expr, right) && expressionEquals(elseAssign.expr, left)) {
      return maxCandidate(thenAssign.target, left, right);
    }
    if ((op === "<" || op === "<=") && expressionEquals(thenAssign.expr, left) && expressionEquals(elseAssign.expr, right)) {
      return minCandidate(thenAssign.target, left, right);
    }
    if ((op === ">" || op === ">=") && expressionEquals(thenAssign.expr, right) && expressionEquals(elseAssign.expr, left)) {
      return minCandidate(thenAssign.target, left, right);
    }
  }

  return undefined;
}

export function buildClampCandidate(statement: Extract<StatementAst, { kind: "if" }>): BranchRemovalCandidate | undefined {
  const assign = singleEffectiveAssign(statement);
  if (!assign) return undefined;
  const targetExpr: ExpressionAst = { kind: "identifier", name: assign.target };
  const { left, right, op } = statement.condition;
  if (!expressionEquals(left, targetExpr)) return undefined;

  if ((op === "<" || op === "<=") && expressionEquals(assign.expr, right)) {
    return maxCandidate(assign.target, targetExpr, right, "Replaced lower clamp branch with max()");
  }
  if ((op === ">" || op === ">=") && expressionEquals(assign.expr, right)) {
    return minCandidate(assign.target, targetExpr, right, "Replaced upper clamp branch with min-via-max()");
  }
  return undefined;
}

export function buildAbsCandidate(statement: Extract<StatementAst, { kind: "if" }>): BranchRemovalCandidate | undefined {
  if (statement.thenBody.length !== 1) return undefined;
  const thenAssign = statement.thenBody[0];
  if (thenAssign?.kind !== "assign") return undefined;
  const { left, right, op } = statement.condition;
  if (!isNumericValue(right, 0)) return undefined;
  const negativeLeft = negateExpression(left);

  if (!statement.elseBody) {
    const targetExpr: ExpressionAst = { kind: "identifier", name: thenAssign.target };
    if (!expressionEquals(targetExpr, left)) return undefined;
    if ((op === "<" || op === "<=") && expressionEquals(thenAssign.expr, negativeLeft)) {
      return absCandidate(thenAssign.target, left);
    }
    return undefined;
  }

  if (statement.elseBody.length !== 1) return undefined;
  const elseAssign = statement.elseBody[0];
  if (elseAssign?.kind !== "assign" || thenAssign.target !== elseAssign.target) return undefined;

  if ((op === "<" || op === "<=") &&
    expressionEquals(thenAssign.expr, negativeLeft) &&
    expressionEquals(elseAssign.expr, left)) {
    return absCandidate(thenAssign.target, left);
  }
  if ((op === ">" || op === ">=") &&
    expressionEquals(thenAssign.expr, left) &&
    expressionEquals(elseAssign.expr, negativeLeft)) {
    return absCandidate(thenAssign.target, left);
  }
  return undefined;
}

export function absCandidate(target: string, expr: ExpressionAst): BranchRemovalCandidate {
  return {
    kind: "assign",
    target,
    expr: { kind: "call", callee: "abs", args: [expr] },
    name: "arithmetic-if-abs",
    detail: "Replaced sign branch with abs()",
  };
}

export function maxCandidate(
  target: string,
  left: ExpressionAst,
  right: ExpressionAst,
  detail = "Replaced max branch with К max",
): BranchRemovalCandidate {
  return {
    kind: "assign",
    target,
    expr: { kind: "call", callee: "max", args: [left, right] },
    name: "arithmetic-if-max",
    detail,
  };
}

export function minCandidate(
  target: string,
  left: ExpressionAst,
  right: ExpressionAst,
  detail = "Replaced min branch with min-via-max()",
): BranchRemovalCandidate {
  return {
    kind: "assign",
    target,
    expr: negateExpression({
      kind: "call",
      callee: "max",
      args: [negateExpression(left), negateExpression(right)],
    }),
    name: "arithmetic-if-min",
    detail,
  };
}

export function booleanSelectorExpression(condition: ConditionAst, ast: ProgramAst): ExpressionAst | undefined {
  const leftIdentifier = condition.left.kind === "identifier" ? condition.left.name : undefined;
  const rightIdentifier = condition.right.kind === "identifier" ? condition.right.name : undefined;
  const leftNumber = numericLiteralValue(condition.left);
  const rightNumber = numericLiteralValue(condition.right);

  let variable: string | undefined;
  let value: number | undefined;
  if (leftIdentifier !== undefined && rightNumber !== undefined) {
    variable = leftIdentifier;
    value = rightNumber;
  } else if (rightIdentifier !== undefined && leftNumber !== undefined) {
    variable = rightIdentifier;
    value = leftNumber;
  }
  if (variable === undefined || value === undefined) return undefined;
  if (!isBooleanVariable(variable, ast)) return undefined;

  const variableExpr: ExpressionAst = { kind: "identifier", name: variable };
  if ((condition.op === "==" && value === 1) || (condition.op === "!=" && value === 0)) {
    return variableExpr;
  }
  if ((condition.op === "==" && value === 0) || (condition.op === "!=" && value === 1)) {
    return oneMinus(variableExpr);
  }
  return undefined;
}

export function isBooleanVariable(name: string, ast: ProgramAst): boolean {
  for (const state of ast.states) {
    const field = state.fields.find((candidate) => candidate.name === name);
    if (!field) continue;
    if (field.type === "flag") return true;
    if (field.min === 0 && field.max === 1) return true;
  }
  return false;
}

export function integerRangeFor(name: string, ast: ProgramAst): { min?: number; max?: number } | undefined {
  const range = numericRangeFor(name, ast);
  if (range === undefined) return undefined;
  if (!Number.isInteger(range.min) || !Number.isInteger(range.max)) return undefined;
  return range;
}

export function numericRangeFor(name: string, ast: ProgramAst): { min?: number; max?: number } | undefined {
  for (const state of ast.states) {
    const field = state.fields.find((candidate) => candidate.name === name);
    if (!field) continue;
    if (field.type === "flag") return { min: 0, max: 1 };
    if (field.type === "range") {
      const range: { min?: number; max?: number } = {};
      if (field.min !== undefined) range.min = field.min;
      if (field.max !== undefined) range.max = field.max;
      return range;
    }
  }
  return undefined;
}

export function booleanSelectorVariableName(condition: ConditionAst, ast: ProgramAst): string | undefined {
  const leftIdentifier = condition.left.kind === "identifier" ? condition.left.name : undefined;
  const rightIdentifier = condition.right.kind === "identifier" ? condition.right.name : undefined;
  const leftNumber = numericLiteralValue(condition.left);
  const rightNumber = numericLiteralValue(condition.right);
  const name = leftIdentifier !== undefined && rightNumber !== undefined
    ? leftIdentifier
    : rightIdentifier !== undefined && leftNumber !== undefined
      ? rightIdentifier
      : undefined;
  return name !== undefined && isBooleanVariable(name, ast) ? name : undefined;
}

export function booleanIdentifier(expr: ExpressionAst, ast: ProgramAst): ExpressionAst | undefined {
  return expr.kind === "identifier" && isBooleanVariable(expr.name, ast) ? expr : undefined;
}

export function comparisonMask(condition: ConditionAst): ExpressionAst | undefined {
  if (condition.op !== "==" && condition.op !== "!=") return undefined;
  const notEqual = signExpression(absExpression(subtractExpressions(condition.left, condition.right)));
  return condition.op === "==" ? oneMinus(notEqual) : notEqual;
}

export function isPackedGridMacroName(name: string): boolean {
  return packedGridMacroArity(name) !== undefined;
}

export function packedGridMacroArity(name: string): number | undefined {
  const arities: Record<string, number> = {
    grid_norm: 1,
    grid_wrap: 1,
    bit_mask: 1,
    bit_has: 2,
    bit_set: 2,
    bit_clear: 2,
    bit_toggle: 2,
    diag_left_index: 2,
    diag_right_index: 2,
    cell_mask: 2,
    cell_has: 3,
    cell_set: 3,
    cell_clear: 3,
    cell_toggle: 3,
    cell_used: 3,
    cell_mark: 3,
    digit_at: 2,
    digit_add: 3,
    digit_set: 3,
    packed_add: 3,
    packed_digit: 2,
    packed_score: 2,
  };
  return arities[name];
}

export function packedGridExpressionMacro(name: string, args: ExpressionAst[]): ExpressionAst | undefined {
  switch (name) {
    case "grid_norm":
    case "grid_wrap":
      return gridNormExpression(args[0]!);
    case "bit_mask":
      return bitMaskExpression(args[0]!);
    case "bit_has":
      return bitMembershipExpression(args[0]!, args[1]!);
    case "bit_set":
      return bitOrExpression(args[0]!, bitMaskExpression(args[1]!));
    case "bit_clear":
      return bitAndExpression(args[0]!, bitNotExpression(bitMaskExpression(args[1]!)));
    case "bit_toggle":
      return bitXorExpression(args[0]!, bitMaskExpression(args[1]!));
    case "diag_left_index":
      return positiveGridNormExpression(addExpressions(args[0]!, args[1]!));
    case "diag_right_index":
      return positiveGridNormExpression(addExpressions(subtractExpressions(args[0]!, args[1]!), numberExpression(DEFAULT_BOARD_WIDTH)));
    case "cell_mask":
      return cellMaskExpression(args[0]!, args[1]!);
    case "cell_has":
    case "cell_used":
      return {
        kind: "call",
        callee: "sign",
        args: [{
          kind: "call",
          callee: "frac",
          args: [bitAndExpression(args[0]!, cellMaskExpression(args[1]!, args[2]!))],
        }],
      };
    case "cell_set":
    case "cell_mark":
      return bitOrExpression(args[0]!, cellMaskExpression(args[1]!, args[2]!));
    case "cell_clear":
      return bitAndExpression(args[0]!, bitNotExpression(cellMaskExpression(args[1]!, args[2]!)));
    case "cell_toggle":
      return bitXorExpression(args[0]!, cellMaskExpression(args[1]!, args[2]!));
    case "digit_at":
      return packedDigitExpression(args[0]!, args[1]!);
    case "digit_add":
      return addExpressions(
        args[0]!,
        multiplyExpressions(args[2]!, digitPlaceExpression(args[1]!)),
      );
    case "packed_add":
      return addExpressions(
        multiplyExpressions(args[2]!, pow10Expression(args[1]!)),
        args[0]!,
      );
    case "digit_set":
      return digitSetExpression(args[0]!, args[1]!, args[2]!);
    case "packed_digit":
      return packedDigitExpression(args[0]!, args[1]!);
    case "packed_score":
      return {
        kind: "call",
        callee: "sqr",
        args: [subtractExpressions(
          fracExpression(divideExpressions(args[0]!, pow10Expression(args[1]!))),
          numberExpression(0.41200076),
        )],
      };
    default:
      return undefined;
  }
}

export type SmallSetMacroName = "near_any" | "eq_any";

export interface SmallSetTest {
  expr: ExpressionAst;
  trueOpcode: number;
  falseOpcode: number;
}

export interface SmallSetConditionMatch {
  kind: SmallSetMacroName;
  mode: "any" | "all";
  tests: SmallSetTest[];
}

export interface NearAnyHelperConditionMatch {
  value: ExpressionAst;
  radius: ExpressionAst;
  candidates: ExpressionAst[];
  op: ">=" | "<";
}

export function isSmallSetMacroName(name: string): name is SmallSetMacroName {
  return name === "near_any" || name === "eq_any";
}

export function smallSetMacroArityOk(name: SmallSetMacroName, argCount: number): boolean {
  return name === "near_any" ? argCount >= 3 : argCount >= 2;
}

export function smallSetMacroArityText(name: SmallSetMacroName): string {
  return name === "near_any" ? "at least three arguments" : "at least two arguments";
}

export function smallSetExpressionMacro(name: string, args: ExpressionAst[]): ExpressionAst | undefined {
  if (!isSmallSetMacroName(name) || !smallSetMacroArityOk(name, args.length)) return undefined;
  if (name === "near_any") {
    const value = args[0]!;
    const radius = args[1]!;
    const distances = args.slice(2).map((candidate) => absExpression(subtractExpressions(value, candidate)));
    if (distances.length === 1) return subtractExpressions(radius, distances[0]!);
    return addExpressions(radius, maxExpressions(distances.map(negateExpression)));
  }
  const value = args[0]!;
  const differences = args.slice(1).map((candidate) => subtractExpressions(value, candidate));
  return productExpressions(differences);
}

export function matchSmallSetCondition(condition: ConditionAst): SmallSetConditionMatch | undefined {
  const normalized = normalizeZeroComparison(condition);
  if (normalized === undefined) return undefined;

  const near = matchNearAnyExpression(normalized.expr);
  if (near !== undefined && (normalized.op === ">=" || normalized.op === "<")) {
    return {
      kind: "near_any",
      mode: normalized.op === ">=" ? "any" : "all",
      tests: near.map((expr) => ({
        expr,
        trueOpcode: directTestOpcode("<"),
        falseOpcode: directTestOpcode(">="),
      })),
    };
  }

  const equal = matchEqAnyExpression(normalized.expr);
  if (equal !== undefined && (normalized.op === "==" || normalized.op === "!=")) {
    return {
      kind: "eq_any",
      mode: normalized.op === "==" ? "any" : "all",
      tests: equal.map((expr) => ({
        expr,
        trueOpcode: directTestOpcode("!="),
        falseOpcode: directTestOpcode("=="),
      })),
    };
  }

  return undefined;
}

export function matchNearAnyHelperCondition(condition: ConditionAst): NearAnyHelperConditionMatch | undefined {
  const normalized = normalizeZeroComparison(condition);
  if (normalized === undefined || (normalized.op !== ">=" && normalized.op !== "<")) return undefined;
  const match = matchNearAnySetExpression(normalized.expr);
  if (match === undefined) return undefined;
  if (!isSimpleStackLoad(match.value) || !isSimpleStackLoad(match.radius)) return undefined;
  if (match.candidates.length === 0 || match.candidates.some((candidate) => !isSimpleStackLoad(candidate))) {
    return undefined;
  }
  return { ...match, op: normalized.op };
}

export function matchNearAnySetExpression(
  expr: ExpressionAst,
): { value: ExpressionAst; radius: ExpressionAst; candidates: ExpressionAst[] } | undefined {
  if (expr.kind === "call" && expr.callee.toLowerCase() === "near_any" && smallSetMacroArityOk("near_any", expr.args.length)) {
    return {
      value: expr.args[0]!,
      radius: expr.args[1]!,
      candidates: expr.args.slice(2),
    };
  }
  return matchNearAnyMarginSetExpression(expr);
}

export function matchNearAnyMarginSetExpression(
  expr: ExpressionAst,
): { value: ExpressionAst; radius: ExpressionAst; candidates: ExpressionAst[] } | undefined {
  const terms = flattenMaxTerms(expr);
  const margins = terms.map(matchNearMarginTerm);
  if (margins.some((margin) => margin === undefined)) return undefined;
  const typedMargins = margins as NearMarginTerm[];
  if (typedMargins.length === 0) return undefined;
  const commonValue = commonDifferenceEndpoint(typedMargins.map((margin) => margin.difference));
  if (commonValue === undefined) return undefined;
  const radius = typedMargins[0]!.radius;
  if (!typedMargins.every((margin) => expressionEquals(margin.radius, radius))) return undefined;
  const candidates = typedMargins.map((margin) =>
    expressionEquals(margin.difference.left, commonValue) ? margin.difference.right : margin.difference.left
  );
  return { value: commonValue, radius, candidates };
}

export function nearAnyHelperKey(value: ExpressionAst, radius: ExpressionAst): string {
  return `${expressionToIntentText(value)}|${expressionToIntentText(radius)}`;
}

export function normalizeZeroComparison(condition: ConditionAst): { expr: ExpressionAst; op: ConditionAst["op"] } | undefined {
  if (isZeroExpression(condition.right)) return { expr: condition.left, op: condition.op };
  if (isZeroExpression(condition.left)) return { expr: condition.right, op: flipComparisonOp(condition.op) };
  return undefined;
}

export function matchNearAnyExpression(expr: ExpressionAst): ExpressionAst[] | undefined {
  if (expr.kind === "call" && expr.callee.toLowerCase() === "near_any") {
    const macro = smallSetExpressionMacro("near_any", expr.args);
    return macro === undefined ? undefined : matchNearAnyExpression(macro);
  }

  const terms = flattenMaxTerms(expr);
  const margins = terms.map(matchNearMarginTerm);
  if (margins.some((margin) => margin === undefined)) return undefined;
  const typedMargins = margins as NearMarginTerm[];
  const commonValue = commonDifferenceEndpoint(typedMargins.map((margin) => margin.difference));
  if (commonValue === undefined) return undefined;
  const radius = typedMargins[0]!.radius;
  if (!typedMargins.every((margin) => expressionEquals(margin.radius, radius))) return undefined;
  return typedMargins.map((margin) => margin.expr);
}

export interface NearMarginTerm {
  expr: ExpressionAst;
  radius: ExpressionAst;
  difference: Extract<ExpressionAst, { kind: "binary" }>;
}

export function matchNearMarginTerm(expr: ExpressionAst): NearMarginTerm | undefined {
  if (expr.kind !== "binary" || expr.op !== "-") return undefined;
  const absCall = expr.right;
  if (absCall.kind !== "call" || absCall.callee.toLowerCase() !== "abs" || absCall.args.length !== 1) {
    return undefined;
  }
  const difference = absCall.args[0]!;
  if (difference.kind !== "binary" || difference.op !== "-") return undefined;
  return { expr, radius: expr.left, difference };
}

export function matchEqAnyExpression(expr: ExpressionAst): ExpressionAst[] | undefined {
  if (expr.kind === "call" && expr.callee.toLowerCase() === "eq_any") {
    const macro = smallSetExpressionMacro("eq_any", expr.args);
    return macro === undefined ? undefined : matchEqAnyExpression(macro);
  }

  const factors = flattenProductTerms(expr);
  if (factors.length === 0) return undefined;
  const differences = factors.map((factor) =>
    factor.kind === "binary" && factor.op === "-" ? factor : undefined
  );
  if (differences.some((difference) => difference === undefined)) return undefined;
  const typedDifferences = differences as Array<Extract<ExpressionAst, { kind: "binary" }>>;
  if (commonDifferenceEndpoint(typedDifferences) === undefined) return undefined;
  return factors;
}

export function commonDifferenceEndpoint(
  differences: Array<Extract<ExpressionAst, { kind: "binary" }>>,
): ExpressionAst | undefined {
  if (differences.length === 0) return undefined;
  const candidates = [differences[0]!.left, differences[0]!.right];
  return candidates.find((candidate) =>
    differences.every((difference) =>
      expressionEquals(difference.left, candidate) || expressionEquals(difference.right, candidate)
    )
  );
}

export function flattenMaxTerms(expr: ExpressionAst): ExpressionAst[] {
  if (expr.kind === "call" && expr.callee.toLowerCase() === "max" && expr.args.length === 2) {
    return [...flattenMaxTerms(expr.args[0]!), ...flattenMaxTerms(expr.args[1]!)];
  }
  return [expr];
}

export function flattenProductTerms(expr: ExpressionAst): ExpressionAst[] {
  if (expr.kind === "binary" && expr.op === "*") {
    return [...flattenProductTerms(expr.left), ...flattenProductTerms(expr.right)];
  }
  return [expr];
}

export interface CellHelperCall {
  mask: ExpressionAst;
  x: ExpressionAst;
  y: ExpressionAst;
}

export type CellHelperName = "cell_used" | "cell_has" | "cell_mark" | "cell_set";

export interface BitMembershipCondition {
  collection: ExpressionAst;
  item: ExpressionAst;
  mask: ExpressionAst;
  mode: "index" | "mask";
  test: ExpressionAst;
}

export interface BitSetAssignment {
  collection: ExpressionAst;
  item: ExpressionAst;
}

export function matchCellHelperCall(expr: ExpressionAst, names: readonly CellHelperName[]): CellHelperCall | undefined {
  if (expr.kind !== "call" || !names.includes(expr.callee.toLowerCase() as CellHelperName) || expr.args.length !== 3) return undefined;
  return {
    mask: expr.args[0]!,
    x: expr.args[1]!,
    y: expr.args[2]!,
  };
}

export function matchBitSetAssignment(statement: Extract<StatementAst, { kind: "assign" }>): BitSetAssignment | undefined {
  const expr = statement.expr;
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "bit_set" || expr.args.length !== 2) return undefined;
  const collection = expr.args[0]!;
  if (collection.kind !== "identifier" || statement.target !== collection.name) return undefined;
  return {
    collection,
    item: expr.args[1]!,
  };
}

export function bitMaskScratchName(statement: StatementAst): string {
  return `${BIT_MASK_SCRATCH_PREFIX}${statement.line}`;
}

export function ifSelectorScratchName(statement: StatementAst): string {
  return `${IF_SELECTOR_SCRATCH_PREFIX}${statement.line}`;
}

export function matchBitMembershipCondition(condition: ConditionAst): BitMembershipCondition | undefined {
  if (condition.op !== "!=" || !isZeroExpression(condition.right)) return undefined;
  const test = condition.left;
  if (test.kind !== "call" || test.args.length !== 2) return undefined;
  if (test.callee.toLowerCase() === "bit_and") {
    return {
      collection: test.args[0]!,
      item: test.args[1]!,
      mask: test.args[1]!,
      mode: "mask",
      test,
    };
  }
  if (test.callee.toLowerCase() !== "bit_has") return undefined;
  const item = test.args[1]!;
  return {
    collection: test.args[0]!,
    item,
    mask: bitMaskExpression(item),
    mode: "index",
    test,
  };
}

export function matchBitAbsenceCondition(condition: ConditionAst): BitMembershipCondition | undefined {
  if (condition.op !== "==" || !isZeroExpression(condition.right)) return undefined;
  const test = condition.left;
  if (test.kind !== "call" || test.args.length !== 2) return undefined;
  if (test.callee.toLowerCase() === "bit_and") {
    return {
      collection: test.args[0]!,
      item: test.args[1]!,
      mask: test.args[1]!,
      mode: "mask",
      test,
    };
  }
  if (test.callee.toLowerCase() !== "bit_has") return undefined;
  const item = test.args[1]!;
  return {
    collection: test.args[0]!,
    item,
    mask: bitMaskExpression(item),
    mode: "index",
    test,
  };
}

export function isBitClearAssignment(
  statement:
    | Extract<StatementAst, { kind: "assign" }>
    | Extract<StatementAst, { kind: "indexed_assign" }>,
  membership: BitMembershipCondition,
): boolean {
  if (!expressionEquals(assignableTargetExpression(statement.target), membership.collection)) return false;
  const expr = statement.expr;
  if (expr.kind !== "call" || expr.args.length !== 2 || !expressionEquals(expr.args[0]!, membership.collection)) {
    return false;
  }
  const name = expr.callee.toLowerCase();
  if (name === "bit_clear") return membership.mode === "index" && expressionEquals(expr.args[1]!, membership.item);
  return name === "bit_and" && isBitNotOf(expr.args[1]!, membership.mask);
}

export function assignableTargetExpression(target: string | Extract<ExpressionAst, { kind: "indexed" }>): ExpressionAst {
  return typeof target === "string" ? { kind: "identifier", name: target } : target;
}

export function isBitNotOf(expr: ExpressionAst, inner: ExpressionAst): boolean {
  return expr.kind === "call" &&
    expr.callee.toLowerCase() === "bit_not" &&
    expr.args.length === 1 &&
    expressionEquals(expr.args[0]!, inner);
}

export interface SingleBitMaskOpAssignment {
  opcode: number;
  mnemonic: string;
  collection: ExpressionAst;
  index: ExpressionAst;
  negate: boolean;
}

// Recognize a standalone `target = bit_or/bit_and/bit_xor(collection,
// [bit_not] bit_mask(index))` assignment. These come from `cells += item` /
// `cells -= item` on a single-row board. The generic expression compiler builds
// `bit_mask(index)` inline while `collection` sits on the stack; the cell-mask
// construction (frac/x^y/10^x) overflows the four-deep MK-61 stack and corrupts
// the held accumulator. The dedicated lowering builds the mask into a scratch
// register first (anchor added last, max depth three), so nothing is held while
// the mask is constructed.
export function matchSingleBitMaskOpAssignment(
  statement: Extract<StatementAst, { kind: "assign" }>,
): SingleBitMaskOpAssignment | undefined {
  const expr = statement.expr;
  if (expr.kind !== "call" || expr.args.length !== 2) return undefined;
  const name = expr.callee.toLowerCase();

  // `cells += item` / `cells -= item` on a single-row board lower directly to
  // bit_set/bit_clear/bit_toggle(collection, index).
  const indexOps: Record<string, [number, string, boolean]> = {
    bit_set: [0x38, "К ∨", false],
    bit_clear: [0x37, "К ∧", true],
    bit_toggle: [0x39, "К ⊕", false],
  };
  const indexOp = indexOps[name];
  if (indexOp !== undefined) {
    return {
      opcode: indexOp[0],
      mnemonic: indexOp[1],
      collection: expr.args[0]!,
      index: expr.args[1]!,
      negate: indexOp[2],
    };
  }

  // Two-dimensional boards (and explicit bit ops) arrive pre-expanded as
  // bit_or/bit_and/bit_xor(collection, [bit_not] bit_mask(index)).
  const maskOps: Record<string, [number, string]> = {
    bit_or: [0x38, "К ∨"],
    bit_and: [0x37, "К ∧"],
    bit_xor: [0x39, "К ⊕"],
  };
  const op = maskOps[name];
  if (op === undefined) return undefined;

  let maskArg = expr.args[1]!;
  let negate = false;
  if (maskArg.kind === "call" && maskArg.callee.toLowerCase() === "bit_not" && maskArg.args.length === 1) {
    negate = true;
    maskArg = maskArg.args[0]!;
  }
  if (maskArg.kind !== "call" || maskArg.callee.toLowerCase() !== "bit_mask" || maskArg.args.length !== 1) {
    return undefined;
  }
  return {
    opcode: op[0],
    mnemonic: op[1],
    collection: expr.args[0]!,
    index: maskArg.args[0]!,
    negate,
  };
}

// The square-board macros (`grid_norm`, `diag_*_index`, `cell_*`) historically baked
// in a fixed 4-wide grid. The coordinate-wrap (`% width`) and diagonal fold
// (`+ width`) are exactly derivable from the board width, so they take a `width`
// parameter; the default keeps the original 4-wide lowering byte-for-byte.
export const DEFAULT_BOARD_WIDTH = 4;

// The cell-mask packs board cell (x, y) into one MK-61 mantissa as
// `10^x + floor(10^y * K_width)`. The fractional constant K_width encodes each
// row's bit-nibble offsets (for width 4 its digits 2,2,6 give the per-row
// offsets 0, 2, 22, 226) chosen so masks stay collision-free under the
// calculator's nibble К∧/К∨ ops. K is hardware-fitted per width and cannot be
// synthesized for an arbitrary width without on-hardware verification, so only
// verified widths appear in this table (mirrors the decimal-series limit).
const CELL_MASK_ROW_CONSTANT: Readonly<Record<number, number>> = {
  4: 0.22600029,
};

export function cellMaskRowConstant(width: number): number {
  const constant = CELL_MASK_ROW_CONSTANT[width];
  if (constant === undefined) {
    const verified = Object.keys(CELL_MASK_ROW_CONSTANT).join(", ");
    throw new Error(
      `cell_mask is only hardware-verified for board width(s) ${verified}; width ${width} needs a verified fractional constant`,
    );
  }
  return constant;
}

export function gridNormExpression(expr: ExpressionAst, width: number = DEFAULT_BOARD_WIDTH): ExpressionAst {
  const rem = multiplyExpressions(
    { kind: "call", callee: "frac", args: [divideExpressions({ kind: "call", callee: "int", args: [expr] }, numberExpression(width))] },
    numberExpression(width),
  );
  return addExpressions(
    rem,
    multiplyExpressions(numberExpression(width), oneMinus(signExpression(maxExpression(rem, numberExpression(0))))),
  );
}

export function positiveGridNormExpression(expr: ExpressionAst, width: number = DEFAULT_BOARD_WIDTH): ExpressionAst {
  const rem = multiplyExpressions(
    fracExpression(divideExpressions(intExpression(expr), numberExpression(width))),
    numberExpression(width),
  );
  return addExpressions(
    rem,
    multiplyExpressions(numberExpression(width), oneMinus(signExpression(rem))),
  );
}

export function cellMaskExpression(x: ExpressionAst, y: ExpressionAst, width: number = DEFAULT_BOARD_WIDTH): ExpressionAst {
  return addExpressions(
    pow10Expression(x),
    { kind: "call", callee: "int", args: [pow10Expression(multiplyExpressions(y, numberExpression(cellMaskRowConstant(width))))] },
  );
}

export interface RandomCoordListPlacement {
  listName: string;
  xMin: number;
  width: number;
  yMin: number;
  height: number;
  count: number;
}

export function isZeroOriginTenByTenPlacement(placement: RandomCoordListPlacement): boolean {
  return placement.xMin === 0 && placement.yMin === 0 && placement.width === 10 && placement.height === 10;
}

export function randomCoordListItemPlacement(fieldName: string, expr: ExpressionAst): RandomCoordListPlacement | undefined {
  const item = coordListItemInfo(fieldName);
  if (item === undefined) return undefined;
  if (expr.kind !== "call" || expr.callee !== "__random_coord_list_item" || expr.args.length !== 6) return undefined;
  const xMin = numericLiteralValue(expr.args[0]!);
  const width = numericLiteralValue(expr.args[1]!);
  const yMin = numericLiteralValue(expr.args[2]!);
  const height = numericLiteralValue(expr.args[3]!);
  const count = numericLiteralValue(expr.args[4]!);
  const index = numericLiteralValue(expr.args[5]!);
  if (
    xMin === undefined ||
    width === undefined ||
    yMin === undefined ||
    height === undefined ||
    count === undefined ||
    index === undefined ||
    index !== item.index
  ) return undefined;
  return { listName: item.listName, xMin, width, yMin, height, count };
}

export function randomCoordListSetupFields(
  fields: readonly StateFieldAst[],
  placement: RandomCoordListPlacement,
): StateFieldAst[] {
  return fields
    .filter((field) => {
      const item = coordListItemInfo(field.name);
      if (item === undefined || item.listName !== placement.listName) return false;
      const current = field.initial === undefined ? undefined : randomCoordListItemPlacement(field.name, field.initial);
      return current !== undefined &&
        current.xMin === placement.xMin &&
        current.width === placement.width &&
        current.yMin === placement.yMin &&
        current.height === placement.height &&
        current.count === placement.count;
    })
    .sort((left, right) => coordListItemInfo(left.name)!.index - coordListItemInfo(right.name)!.index);
}

// A cell mask is stored as `8.HHHHHHH`: the MK-61 blue logical operations
// (К∨/К∧/К⊕) force the integer part to 8 and operate nibble-wise on the seven
// fractional hex digits, so each cell's bit lives in a fixed fractional nibble
// rather than in an integer position (which normalization would collapse).
// Bit `index` (0-based) occupies hex nibble `floor(index/4)+1` after the point,
// with value `2^(index mod 4)` inside that nibble. `2^offset` is computed with
// `F x^y`, which is slightly imprecise (e.g. 2^3 → 7.9999993), so it is rounded
// before being placed, keeping the nibble exact.
export function bitMaskExpression(index: ExpressionAst): ExpressionAst {
  const nibble = intExpression(divideExpressions(index, numberExpression(4)));
  const offset = subtractExpressions(index, multiplyExpressions(nibble, numberExpression(4)));
  const bitValue = intExpression(addExpressions(powExpression(numberExpression(2), offset), numberExpression(0.5)));
  return addExpressions(
    numberExpression(8),
    divideExpressions(bitValue, pow10Expression(addExpressions(nibble, numberExpression(1)))),
  );
}

// Membership of a bit reduces to: the fractional part of `mask К∧ bitMask` is
// non-zero exactly when the bit is set (an absent bit yields `8.0`). `sign` of
// that fraction collapses to the 0/1 the language expects from `bit_has`.
export function bitMembershipExpression(mask: ExpressionAst, index: ExpressionAst): ExpressionAst {
  return {
    kind: "call",
    callee: "sign",
    args: [{
      kind: "call",
      callee: "frac",
      args: [bitAndExpression(mask, bitMaskExpression(index))],
    }],
  };
}

export function packedDigitExpression(lines: ExpressionAst, index: ExpressionAst): ExpressionAst {
  return {
    kind: "call",
    callee: "int",
    args: [
      multiplyExpressions(
        { kind: "call", callee: "frac", args: [divideExpressions(lines, pow10Expression(index))] },
        numberExpression(10),
      ),
    ],
  };
}

export function digitSetExpression(value: ExpressionAst, index: ExpressionAst, digit: ExpressionAst): ExpressionAst {
  const place = digitPlaceExpression(index);
  return addExpressions(
    subtractExpressions(value, multiplyExpressions(packedDigitExpression(value, index), place)),
    multiplyExpressions(digit, place),
  );
}

export function digitPlaceExpression(index: ExpressionAst): ExpressionAst {
  return pow10Expression(subtractExpressions(index, numberExpression(1)));
}

export function oneMinus(expr: ExpressionAst): ExpressionAst {
  if (isNumericValue(expr, 0)) return numberExpression(1);
  if (isNumericValue(expr, 1)) return numberExpression(0);
  return {
    kind: "binary",
    op: "-",
    left: numberExpression(1),
    right: expr,
  };
}

export function randomUnitExpression(): ExpressionAst {
  return { kind: "call", callee: "random", args: [] };
}

export function matchRandomRangeCall(
  expr: ExpressionAst,
): { min: ExpressionAst; max: ExpressionAst } | undefined {
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "random") {
    return undefined;
  }
  if (expr.args.length === 1) return { min: numberExpression(0), max: expr.args[0]! };
  if (expr.args.length !== 2) return undefined;
  return { min: expr.args[0]!, max: expr.args[1]! };
}

export function randomRangeSpanExpression(min: ExpressionAst, max: ExpressionAst): ExpressionAst {
  const minValue = numericLiteralValue(min);
  const maxValue = numericLiteralValue(max);
  if (
    minValue !== undefined &&
    maxValue !== undefined &&
    Number.isSafeInteger(minValue) &&
    Number.isSafeInteger(maxValue)
  ) {
    return numberExpression(maxValue - minValue);
  }
  return subtractExpressions(max, min);
}

export function randomRangeExpression(min: ExpressionAst, max: ExpressionAst): ExpressionAst {
  return addExpressions(
    min,
    multiplyExpressions(randomUnitExpression(), randomRangeSpanExpression(min, max)),
  );
}

export function multiplyExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(left, 0) || isNumericValue(right, 0)) return numberExpression(0);
  if (isNumericValue(left, 1)) return right;
  if (isNumericValue(right, 1)) return left;
  return { kind: "binary", op: "*", left, right };
}

export function productExpressions(expressions: ExpressionAst[]): ExpressionAst {
  return expressions.reduce((product, expr) => multiplyExpressions(product, expr));
}

export function addExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(left, 0)) return right;
  if (isNumericValue(right, 0)) return left;
  return { kind: "binary", op: "+", left, right };
}

export function subtractExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(right, 0)) return left;
  if (isNumericValue(left, 0)) return { kind: "unary", op: "-", expr: right };
  return { kind: "binary", op: "-", left, right };
}

export function divideExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(right, 1)) return left;
  return { kind: "binary", op: "/", left, right };
}

export function pow10Expression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "pow10", args: [expr] };
}

export function powExpression(base: ExpressionAst, exponent: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "pow", args: [base, exponent] };
}

export function maxExpression(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "max", args: [left, right] };
}

export function minExpression(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  return negateExpression(maxExpression(negateExpression(left), negateExpression(right)));
}

// Quirk-free maximum that avoids the К max (0x36) "zero is the greatest value"
// hardware behaviour by computing (a + b + |a - b|) / 2 with ordinary arithmetic.
// Both operands are referenced twice, so callers must pass duplicable (pure)
// expressions.
export function safeMaxExpression(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  const span = absExpression(subtractExpressions(left, right));
  return divideExpressions(addExpressions(addExpressions(left, right), span), numberExpression(2));
}

// Quirk-free minimum: (a + b - |a - b|) / 2. Same duplicable-operand requirement.
export function safeMinExpression(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  const span = absExpression(subtractExpressions(left, right));
  return divideExpressions(subtractExpressions(addExpressions(left, right), span), numberExpression(2));
}

export function absExpression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "abs", args: [expr] };
}

export function intExpression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "int", args: [expr] };
}

export function signExpression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "sign", args: [expr] };
}

export function bitAndExpression(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "bit_and", args: [left, right] };
}

export function bitOrExpression(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "bit_or", args: [left, right] };
}

export function bitXorExpression(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "bit_xor", args: [left, right] };
}

export function bitNotExpression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "bit_not", args: [expr] };
}

export function fracExpression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "frac", args: [expr] };
}

export function spatialCountExpression(
  name: "neighbor_count" | "line_count",
  args: ExpressionAst[],
  ast: ProgramAst,
): ExpressionAst | undefined {
  const [mask, cell] = args;
  if (mask === undefined || cell === undefined) return undefined;
  if (name === "neighbor_count") {
    const board = boardForCellMask(mask, ast);
    const offsets = board?.height === 1
      ? [-1, 1]
      : board?.width === 1
        ? [-10, 10]
        : [-11, -10, -9, -1, 1, 9, 10, 11];
    return sumExpressions(offsets.map((offset) =>
      spatialHitExpression(mask, spatialBitIndexExpressionForBoard(board, offsetExpressionAst(cell, offset)))
    ));
  }

  const board = boardForCellMask(mask, ast);
  if (board !== undefined && board.width <= 4 && board.height <= 4) {
    return maxExpressions([
      sumExpressions(spatialLineCells(board, "row", cell).map((index) => spatialHitExpression(mask, index))),
      sumExpressions(spatialLineCells(board, "column", cell).map((index) => spatialHitExpression(mask, index))),
      sumExpressions(spatialLineCells(board, "diag-left", cell).map((index) => spatialHitExpression(mask, index))),
      sumExpressions(spatialLineCells(board, "diag-right", cell).map((index) => spatialHitExpression(mask, index))),
    ]);
  }

  const offsets = [-99, -90, -81, -72, -63, -54, -45, -36, -27, -18, -9, 0, 9, 18, 27, 36, 45, 54, 63, 72, 81, 90, 99];
  return sumExpressions([
    spatialHitExpression(mask, cell),
    ...offsets.filter((offset) => offset !== 0).map((offset) => spatialHitExpression(mask, offsetExpressionAst(cell, offset))),
  ]);
}

export function spatialHitExpression(mask: ExpressionAst, index: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "__spatial_hit", args: [mask, index] };
}

export function spatialBitIndexExpressionForBoard(board: V2BoardAst | undefined, cell: ExpressionAst): ExpressionAst {
  if (board?.height === 1) return subtractExpressions(cell, numberExpression(board.xMin));
  if (board?.width === 1) return subtractExpressions(cell, numberExpression(board.yMin));
  return cell;
}

export function boardForCellMask(mask: ExpressionAst, ast: ProgramAst): V2BoardAst | undefined {
  if (mask.kind !== "identifier" || ast.v2 === undefined) return undefined;
  const domain = ast.v2.state.find((field) => field.name === mask.name)?.domain;
  if (domain === undefined) return undefined;
  return ast.v2.boards.find((board) => board.name === domain);
}

export function spatialLineCells(
  board: V2BoardAst,
  kind: "row" | "column" | "diag-left" | "diag-right",
  cell: ExpressionAst,
): ExpressionAst[] {
  const x = decimalOnesExpressionAst(cell);
  const y = decimalTensExpressionAst(cell);
  switch (kind) {
    case "row":
      return range(board.xMin, board.xMax).map((candidateX) => boardCellExpressionAst(numberExpression(candidateX), y));
    case "column":
      return range(board.yMin, board.yMax).map((candidateY) => boardCellExpressionAst(x, numberExpression(candidateY)));
    case "diag-left":
      return range(-Math.max(board.width, board.height) + 1, Math.max(board.width, board.height) - 1)
        .map((delta) => offsetExpressionAst(cell, delta * 11));
    case "diag-right":
      return range(-Math.max(board.width, board.height) + 1, Math.max(board.width, board.height) - 1)
        .map((delta) => offsetExpressionAst(cell, delta * 9));
  }
}

export interface SpatialLineProgression {
  startOffset: ExpressionAst;
  step: ExpressionAst;
  count: number;
}

export function spatialLineProgressions(board: V2BoardAst, cell: ExpressionAst): SpatialLineProgression[] {
  const x = decimalOnesExpressionAst(cell);
  const y = decimalTensExpressionAst(cell);
  const span = Math.max(board.width, board.height);
  return [
    {
      startOffset: subtractExpressions(numberExpression(board.xMin), x),
      step: numberExpression(1),
      count: board.width,
    },
    {
      startOffset: multiplyExpressions(numberExpression(10), subtractExpressions(numberExpression(board.yMin), y)),
      step: numberExpression(10),
      count: board.height,
    },
    {
      startOffset: numberExpression(-(span - 1) * 11),
      step: numberExpression(11),
      count: span * 2 - 1,
    },
    {
      startOffset: numberExpression(-(span - 1) * 9),
      step: numberExpression(9),
      count: span * 2 - 1,
    },
  ];
}

export function spatialNeighborProgressions(board: V2BoardAst | undefined): SpatialLineProgression[] {
  if (board?.height === 1) {
    return [{ startOffset: numberExpression(-1), step: numberExpression(2), count: 2 }];
  }
  if (board?.width === 1) {
    return [{ startOffset: numberExpression(-10), step: numberExpression(20), count: 2 }];
  }
  return [
    { startOffset: numberExpression(-11), step: numberExpression(1), count: 3 },
    { startOffset: numberExpression(-1), step: numberExpression(2), count: 2 },
    { startOffset: numberExpression(9), step: numberExpression(1), count: 3 },
  ];
}

export function boardCellExpressionAst(x: ExpressionAst, y: ExpressionAst): ExpressionAst {
  return addExpressions(x, multiplyExpressions(numberExpression(10), y));
}

export function decimalTensExpressionAst(expr: ExpressionAst): ExpressionAst {
  return intExpression(divideExpressions(expr, numberExpression(10)));
}

export function decimalOnesExpressionAst(expr: ExpressionAst): ExpressionAst {
  return multiplyExpressions(fracExpression(divideExpressions(expr, numberExpression(10))), numberExpression(10));
}

export function offsetExpressionAst(expr: ExpressionAst, offset: number): ExpressionAst {
  if (offset === 0) return expr;
  return offset > 0
    ? addExpressions(expr, numberExpression(offset))
    : subtractExpressions(expr, numberExpression(Math.abs(offset)));
}

export function sumExpressions(expressions: ExpressionAst[]): ExpressionAst {
  return expressions.reduce((sum, expr) => addExpressions(sum, expr), numberExpression(0));
}

export function maxExpressions(expressions: ExpressionAst[]): ExpressionAst {
  return expressions.reduce((best, expr) => maxExpression(best, expr));
}

export function spatialHitScratchName(mask: string): string {
  return `${SPATIAL_HIT_SCRATCH_PREFIX}${mask}`;
}

export function spatialCountScratchNames(): string[] {
  return [
    `${SPATIAL_COUNT_SCRATCH_PREFIX}total`,
    `${SPATIAL_COUNT_SCRATCH_PREFIX}line`,
    `${SPATIAL_COUNT_SCRATCH_PREFIX}offset`,
    spatialCountCounterScratchName(),
  ];
}

export function spatialCountCounterScratchName(): string {
  return `${SPATIAL_COUNT_SCRATCH_PREFIX}counter`;
}

export function spatialCountMaskScratchName(): string {
  return `${SPATIAL_COUNT_SCRATCH_PREFIX}mask`;
}

export function spatialCountStepScratchName(): string {
  return `${SPATIAL_COUNT_SCRATCH_PREFIX}step`;
}

export function range(start: number, end: number): number[] {
  const values: number[] = [];
  for (let value = start; value <= end; value += 1) values.push(value);
  return values;
}

export function signToggleExpression(current: ExpressionAst, selector: ExpressionAst): ExpressionAst {
  return multiplyExpressions(
    current,
    subtractExpressions(numberExpression(1), multiplyExpressions(numberExpression(2), selector)),
  );
}

export function negateExpression(expr: ExpressionAst): ExpressionAst {
  if (expr.kind === "unary") return expr.expr;
  const value = numericLiteralValue(expr);
  if (value !== undefined) return numberExpression(-value);
  return { kind: "unary", op: "-", expr };
}

export function matchRemainderByConstant(
  expr: Extract<ExpressionAst, { kind: "binary" }>,
): { value: ExpressionAst; divisor: ExpressionAst } | undefined {
  if (expr.op !== "-") return undefined;
  if (!expressionPureForSubstitution(expr.left)) return undefined;
  const product = expr.right;
  if (product.kind !== "binary" || product.op !== "*") return undefined;
  const leftIntDivide = matchIntDivideByConstant(product.left);
  if (
    leftIntDivide !== undefined &&
    expressionEquals(leftIntDivide.value, expr.left) &&
    expressionEquals(leftIntDivide.divisor, product.right)
  ) {
    return leftIntDivide;
  }
  const rightIntDivide = matchIntDivideByConstant(product.right);
  if (
    rightIntDivide !== undefined &&
    expressionEquals(rightIntDivide.value, expr.left) &&
    expressionEquals(rightIntDivide.divisor, product.left)
  ) {
    return rightIntDivide;
  }
  return undefined;
}

export function matchIntDivideByConstant(expr: ExpressionAst): { value: ExpressionAst; divisor: ExpressionAst } | undefined {
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "int" || expr.args.length !== 1) return undefined;
  const divided = expr.args[0]!;
  if (divided.kind !== "binary" || divided.op !== "/") return undefined;
  if (numericLiteralValue(divided.right) === undefined) return undefined;
  return {
    value: divided.left,
    divisor: divided.right,
  };
}

export function numberExpression(value: number): ExpressionAst {
  return { kind: "number", raw: String(value) };
}

export function matchTargetPlusDelta(expr: ExpressionAst, target: string): ExpressionAst | undefined {
  if (expr.kind !== "binary" || expr.op !== "+") return undefined;
  const targetExpr: ExpressionAst = { kind: "identifier", name: target };
  if (expressionEquals(expr.left, targetExpr)) return expr.right;
  if (expressionEquals(expr.right, targetExpr)) return expr.left;
  return undefined;
}

export function matchTargetMinusDelta(expr: ExpressionAst, target: string): ExpressionAst | undefined {
  if (expr.kind !== "binary" || expr.op !== "-") return undefined;
  const targetExpr: ExpressionAst = { kind: "identifier", name: target };
  if (expressionEquals(expr.left, targetExpr)) return expr.right;
  return undefined;
}

export function singleEffectiveAssign(statement: Extract<StatementAst, { kind: "if" }>): Extract<StatementAst, { kind: "assign" }> | undefined {
  if (statement.thenBody.length !== 1) return undefined;
  const thenAssign = statement.thenBody[0];
  if (thenAssign?.kind !== "assign") return undefined;
  if (!statement.elseBody) return thenAssign;
  if (statement.elseBody.length !== 1) return undefined;
  const elseAssign = statement.elseBody[0];
  if (elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target) return undefined;
  if (isIdentityAssignment(elseAssign)) return thenAssign;
  return undefined;
}

export function isIdentityAssignment(statement: Extract<StatementAst, { kind: "assign" }>): boolean {
  return expressionEquals(statement.expr, { kind: "identifier", name: statement.target });
}

export function expressionEquals(left: ExpressionAst, right: ExpressionAst): boolean {
  if (left.kind !== right.kind) return false;
  switch (left.kind) {
    case "number":
      return right.kind === "number" && left.raw === right.raw;
    case "string":
      return right.kind === "string" && left.text === right.text;
    case "identifier":
      return right.kind === "identifier" && left.name === right.name;
    case "indexed":
      return right.kind === "indexed" &&
        left.base === right.base &&
        left.field === right.field &&
        expressionEquals(left.index, right.index);
    case "unary":
      return right.kind === "unary" && left.op === right.op && expressionEquals(left.expr, right.expr);
    case "binary":
      return right.kind === "binary" &&
        left.op === right.op &&
        expressionEquals(left.left, right.left) &&
        expressionEquals(left.right, right.right);
    case "call":
      return right.kind === "call" &&
        left.callee.toLowerCase() === right.callee.toLowerCase() &&
        left.args.length === right.args.length &&
        left.args.every((arg, index) => expressionEquals(arg, right.args[index]!));
  }
}

export function expressionReferencesIdentifier(expr: ExpressionAst, name: string): boolean {
  switch (expr.kind) {
    case "number":
    case "string":
      return false;
    case "identifier":
      return expr.name === name;
    case "indexed":
      return expressionReferencesIdentifier(expr.index, name);
    case "unary":
      return expressionReferencesIdentifier(expr.expr, name);
    case "binary":
      return expressionReferencesIdentifier(expr.left, name) || expressionReferencesIdentifier(expr.right, name);
    case "call":
      return expr.args.some((arg) => expressionReferencesIdentifier(arg, name));
  }
}

// True when `expr` reads an indexed state cell of the given bank/field (any
// index). Used by stack-resident temp scheduling to confirm an indexed compound
// consumer `cells[i] op= temp` actually recalls the cell it writes, so that
// recall is what lifts the held temp into Y.
export function expressionReferencesIndexedCell(expr: ExpressionAst, base: string, field: string | undefined): boolean {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
      return false;
    case "indexed":
      return (expr.base === base && expr.field === field) || expressionReferencesIndexedCell(expr.index, base, field);
    case "unary":
      return expressionReferencesIndexedCell(expr.expr, base, field);
    case "binary":
      return (
        expressionReferencesIndexedCell(expr.left, base, field) ||
        expressionReferencesIndexedCell(expr.right, base, field)
      );
    case "call":
      return expr.args.some((arg) => expressionReferencesIndexedCell(arg, base, field));
  }
}

// An expression is pure when evaluating it twice yields the same value with no
// observable effect. Calls (random, read, macros) may be non-idempotent or have
// side effects, so they are conservatively impure. Purity makes it sound to
// compute a repeated operand once and duplicate it through the stack.
export function isPureExpression(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
      return true;
    case "indexed":
      return isPureExpression(expr.index);
    case "unary":
      return isPureExpression(expr.expr);
    case "binary":
      return isPureExpression(expr.left) && isPureExpression(expr.right);
    case "call":
      return false;
  }
}

export function matchIntOrFracCall(expr: ExpressionAst): { fn: "int" | "frac"; arg: ExpressionAst } | undefined {
  if (expr.kind !== "call" || expr.args.length !== 1) return undefined;
  const name = expr.callee.toLowerCase();
  if (name !== "int" && name !== "frac") return undefined;
  return { fn: name, arg: expr.args[0]! };
}

export function matchStackUnaryDerivationCall(expr: ExpressionAst): StackUnaryDerivationCall | undefined {
  if (expr.kind !== "call" || expr.args.length !== 1) return undefined;
  const name = expr.callee.toLowerCase();
  if (!isStackUnaryDerivationFn(name)) return undefined;
  const [opcode, mnemonic] = STACK_UNARY_DERIVATION_OPCODES[name];
  return { fn: name, arg: expr.args[0]!, opcode, mnemonic };
}

export function isStackUnaryDerivationFn(name: string): name is StackUnaryDerivationFn {
  return Object.prototype.hasOwnProperty.call(STACK_UNARY_DERIVATION_OPCODES, name);
}

export function matchXParamReturnDecay(proc: ProcAst): XParamReturnDecay | undefined {
  const params = proc.params ?? [];
  const [param] = params;
  if (param === undefined || params.length !== 1 || proc.body.length !== 1) return undefined;
  const only = proc.body[0];
  if (only?.kind !== "return_value") return undefined;
  const expr = only.expr;
  if (expr.kind !== "binary" || expr.op !== "-") return undefined;
  if (expr.left.kind !== "identifier" || expr.left.name !== param) return undefined;
  const right = expr.right;
  if (right.kind !== "call" || right.callee.toLowerCase() !== "int" || right.args.length !== 1) return undefined;
  const scaled = right.args[0]!;
  if (scaled.kind !== "binary" || scaled.op !== "/") return undefined;
  const product = scaled.left;
  if (product.kind !== "binary" || product.op !== "*") return undefined;
  let factor: ExpressionAst | undefined;
  if (product.left.kind === "identifier" && product.left.name === param) {
    factor = product.right;
  } else if (product.right.kind === "identifier" && product.right.name === param) {
    factor = product.left;
  } else {
    return undefined;
  }
  if (expressionReferencesIdentifier(factor, param) || expressionReferencesIdentifier(scaled.right, param)) return undefined;
  if (!expressionPureForSubstitution(factor) || !expressionPureForSubstitution(scaled.right)) return undefined;
  return { param, factor, divisor: scaled.right, line: only.line };
}

export function matchXParamStackStopRiskRead(program: ProgramAst, proc: ProcAst): XParamStackStopRiskRead | undefined {
  const params = proc.params ?? [];
  const [param] = params;
  if (param === undefined || params.length !== 1 || proc.body.length !== 2) return undefined;
  const show = proc.body[0];
  const ret = proc.body[1];
  if (show?.kind !== "show" || ret?.kind !== "return_value") return undefined;
  const display = program.displays.find((candidate) => candidate.name === show.display);
  const item = display?.items.length === 1 ? display.items[0] : undefined;
  if (item?.kind !== "source" || item.name !== param || item.expr !== undefined || item.width !== undefined) {
    return undefined;
  }
  const risk = matchStackStopRisk(ret.expr, param);
  if (risk === undefined) return undefined;
  return { param, display: show.display, showLine: show.line, line: ret.line, risk };
}

export function countExpressionCalls(program: ProgramAst, name: string): number {
  const target = name.toLowerCase();
  let count = 0;

  const visitExpr = (expr: ExpressionAst): void => {
    switch (expr.kind) {
      case "indexed":
        visitExpr(expr.index);
        return;
      case "unary":
        visitExpr(expr.expr);
        return;
      case "binary":
        visitExpr(expr.left);
        visitExpr(expr.right);
        return;
      case "call":
        if (expr.callee.toLowerCase() === target) count += 1;
        for (const arg of expr.args) visitExpr(arg);
        return;
      default:
        return;
    }
  };

  const visitCondition = (condition: ConditionAst): void => {
    visitExpr(condition.left);
    visitExpr(condition.right);
  };

  const visitStatements = (statements: readonly StatementAst[]): void => {
    for (const statement of statements) {
      switch (statement.kind) {
        case "pause":
        case "preview":
        case "halt":
        case "return_value":
          visitExpr(statement.expr);
          break;
        case "assign":
          visitExpr(statement.expr);
          break;
        case "indexed_assign":
          visitExpr(statement.target.index);
          visitExpr(statement.expr);
          break;
        case "coord_list_remove":
          visitExpr(statement.item);
          break;
        case "if":
          visitCondition(statement.condition);
          visitStatements(statement.thenBody);
          if (statement.elseBody !== undefined) visitStatements(statement.elseBody);
          break;
        case "while":
          visitCondition(statement.condition);
          visitStatements(statement.body);
          break;
        case "loop":
          visitStatements(statement.body);
          break;
        case "dispatch":
          visitExpr(statement.expr);
          for (const dispatchCase of statement.cases) {
            visitExpr(dispatchCase.value);
            visitStatements(dispatchCase.body);
          }
          if (statement.defaultBody !== undefined) visitStatements(statement.defaultBody);
          break;
        case "core":
          for (const input of statement.inputs ?? []) visitExpr(input.expr);
          break;
        default:
          break;
      }
    }
  };

  for (const entry of program.entries) visitStatements(entry.body);
  for (const proc of program.procs) visitStatements(proc.body);
  return count;
}

export function matchXParamValueFunction(proc: ProcAst): XParamValueFunction | undefined {
  const params = proc.params ?? [];
  const [param] = params;
  if (param === undefined || params.length !== 1 || proc.body.length !== 3) return undefined;
  const [assign, branch, ret] = proc.body;
  if (assign?.kind !== "assign" || assign.target !== param) return undefined;
  const width = matchPositiveModuloExpression(assign.expr, param);
  if (width === undefined) return undefined;
  if (branch?.kind !== "if" || branch.elseBody !== undefined) return undefined;
  if (!identifierExpressionIs(branch.condition.left, param) || branch.condition.op !== "<=" || !numericExpressionIs(branch.condition.right, 0)) {
    return undefined;
  }
  const [thenReturn] = branch.thenBody;
  const positiveReturn = addExpressions({ kind: "identifier", name: param }, numberExpression(width));
  const foldedPositiveReturn = addExpressions(numberExpression(width), { kind: "identifier", name: param });
  if (
    thenReturn?.kind !== "return_value" ||
    (!expressionEquals(thenReturn.expr, positiveReturn) && !expressionEquals(thenReturn.expr, foldedPositiveReturn))
  ) {
    return undefined;
  }
  if (ret?.kind !== "return_value" || !identifierExpressionIs(ret.expr, param)) return undefined;
  return { param, width, line: assign.line };
}

export function xParamValueFunctionParamNames(ast: ProgramAst): ReadonlySet<string> {
  const names = new Set<string>();
  for (const proc of ast.procs) {
    const match = matchXParamValueFunction(proc);
    if (match !== undefined) names.add(match.param);
  }
  return names;
}

export function xParamValueScratchName(ast: ProgramAst): string | undefined {
  return ast.states
    .flatMap((state) => state.fields)
    .find((field) => field.name.startsWith(REPEATED_UNARY_UPDATE_ARG_PREFIX))
    ?.name;
}

function matchPositiveModuloExpression(expr: ExpressionAst, param: string): number | undefined {
  if (expr.kind !== "binary" || expr.op !== "*") return undefined;
  const leftWidth = numericLiteralValue(expr.left);
  const rightWidth = numericLiteralValue(expr.right);
  const width = leftWidth ?? rightWidth;
  const frac = leftWidth === undefined ? expr.left : expr.right;
  if (width === undefined || !Number.isInteger(width) || width <= 0) return undefined;
  if (frac.kind !== "call" || frac.callee.toLowerCase() !== "frac" || frac.args.length !== 1) return undefined;
  const divided = frac.args[0]!;
  if (divided.kind !== "binary" || divided.op !== "/") return undefined;
  if (!numericExpressionIs(divided.right, width)) return undefined;
  const intCall = divided.left;
  if (intCall.kind !== "call" || intCall.callee.toLowerCase() !== "int" || intCall.args.length !== 1) return undefined;
  return identifierExpressionIs(intCall.args[0]!, param) ? width : undefined;
}

function identifierExpressionIs(expr: ExpressionAst, name: string): boolean {
  return expr.kind === "identifier" && expr.name === name;
}

function numericExpressionIs(expr: ExpressionAst, value: number): boolean {
  const parsed = numericLiteralValue(expr);
  return parsed !== undefined && parsed === value;
}

// --- Stack-stop risk fusion (generalized "stack-stop-risk") -----------------------
//
// The classic idiom shows a value, stops for a player input, and returns
// `int(value * (1 + sin(read())))`. The compact machine form keeps the value
// in Y across the С/П stop and transforms the entered value in X, so the
// whole result computes on the stack with no input register.
//
// This recognizer generalizes that single hardcoded formula to any pure
// expression of the shape `wrap*( Y (yOp) g(input) )`, where:
//   - `Y` is the displayed value parked in Y across the stop;
//   - `input` is the entered value sitting in X (a bare `read()` or, for the
//     stored-input variant, a named input field);
//   - `g(input)` is a chain of single-argument X-transforming intrinsics over
//     the input, optionally fused with one single-digit additive/scaling
//     constant (e.g. `1 + sin(read())`, `cos(read())`, `2 * sin(read())`);
//   - `yOp` is any of `+`, `-`, `*`, `/` (non-commutative ops require Y on the
//     left so they map directly to the machine's `Y op X` order);
//   - `wrap*` is an outer chain of X-transforming intrinsics (e.g. `int(...)`).
//
// All of these lower to the same kept-in-Y / transform-in-X stack sequence, so
// the generalization never grows a program; the canonical `sin` form is emitted
// byte-for-byte identically.

// Single source of truth for the single-argument intrinsics that lower as
// "compile the argument, then apply one X-transforming opcode" -- the argument
// is emitted first and nothing precedes it, so the result stays in X with the
// rest of the stack untouched. Shared by compileCall (code generation),
// expressionLeadsWithRead, and the stack-stop scheduler (operand ordering).
export const X_TRANSFORM_UNARY_OPCODES: Record<string, [number, string]> = {
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

export const X_TRANSFORM_UNARY_FUNCTIONS: ReadonlySet<string> = new Set(Object.keys(X_TRANSFORM_UNARY_OPCODES));

export function expressionCanConsumeIdentifierFromX(expr: ExpressionAst, name: string): boolean {
  if (!expressionPureForSubstitution(expr)) return false;
  switch (expr.kind) {
    case "identifier":
      return expr.name === name;
    case "unary":
      return expr.op === "-" && expressionCanConsumeIdentifierFromX(expr.expr, name);
    case "binary": {
      const leftUses = expressionReferencesIdentifier(expr.left, name);
      const rightUses = expressionReferencesIdentifier(expr.right, name);
      if (leftUses === rightUses) return false;
      if (leftUses) {
        return expressionCanConsumeIdentifierFromX(expr.left, name) &&
          expressionPureForSubstitution(expr.right);
      }
      return (expr.op === "+" || expr.op === "*") &&
        expressionCanConsumeIdentifierFromX(expr.right, name) &&
        expressionPureForSubstitution(expr.left);
    }
    case "call":
      return expr.args.length === 1 &&
        X_TRANSFORM_UNARY_OPCODES[expr.callee.toLowerCase()] !== undefined &&
        expressionCanConsumeIdentifierFromX(expr.args[0]!, name);
    default:
      return false;
  }
}

export interface StackStopRiskMatch {
  // Name of the value parked in Y across the stop.
  yName: string;
  // Binary operator combining the Y-parked value with the X-computed input.
  yOp: "+" | "-" | "*" | "/";
  // True when `yName` is the left operand (`Y op X`); commutative ops also allow
  // the parked value on the right.
  yOnLeft: boolean;
  // Unary X-transform opcodes applied to the input, innermost-first (emit order).
  inputUnary: ReadonlyArray<readonly [number, string]>;
  // Optional single-digit constant fused with the input chain.
  additive?: { digit: number; op: "+" | "-" | "*" | "/" };
  // Outer X-transform wraps applied to `(Y op X)`, outermost-first (peel order;
  // emitted in reverse so the innermost wrap is applied first).
  wraps: ReadonlyArray<readonly [number, string]>;
}

function isReadCallExpression(expr: ExpressionAst): boolean {
  return expr.kind === "call" && expr.callee.toLowerCase() === "read" && expr.args.length === 0;
}

function isIdentifierExpression(expr: ExpressionAst, name: string): boolean {
  return expr.kind === "identifier" && expr.name === name;
}

function countMatchingNodes(expr: ExpressionAst, pred: (e: ExpressionAst) => boolean): number {
  const self = pred(expr) ? 1 : 0;
  switch (expr.kind) {
    case "call":
      return self + expr.args.reduce((sum, arg) => sum + countMatchingNodes(arg, pred), 0);
    case "binary":
      return self + countMatchingNodes(expr.left, pred) + countMatchingNodes(expr.right, pred);
    case "unary":
      return self + countMatchingNodes(expr.expr, pred);
    case "indexed":
      return self + countMatchingNodes(expr.index, pred);
    default:
      return self;
  }
}

function expressionReferencesName(expr: ExpressionAst, name: string): boolean {
  switch (expr.kind) {
    case "identifier":
      return expr.name === name;
    case "indexed":
      return expr.base === name || expressionReferencesName(expr.index, name);
    case "unary":
      return expressionReferencesName(expr.expr, name);
    case "binary":
      return expressionReferencesName(expr.left, name) || expressionReferencesName(expr.right, name);
    case "call":
      return expr.args.some((arg) => expressionReferencesName(arg, name));
    default:
      return false;
  }
}

// A non-negative single-digit integer literal (0..9), the only constant the
// fused tail can push as one opcode without breaking byte-for-byte sizing.
function singleDigitLiteral(expr: ExpressionAst): number | undefined {
  if (expr.kind !== "number") return undefined;
  const trimmed = expr.raw.trim();
  if (!/^[0-9]$/u.test(trimmed)) return undefined;
  return Number(trimmed);
}

// Collect the chain of X-transform unary intrinsics wrapping the input leaf,
// innermost-first (emit order). Returns undefined when `expr` is not a pure
// chain bottoming out at the leaf.
function collectInputUnaryChain(
  expr: ExpressionAst,
  isLeaf: (e: ExpressionAst) => boolean,
): Array<readonly [number, string]> | undefined {
  if (isLeaf(expr)) return [];
  if (expr.kind === "call" && expr.args.length === 1) {
    const opcode = X_TRANSFORM_UNARY_OPCODES[expr.callee.toLowerCase()];
    if (opcode === undefined) return undefined;
    const inner = collectInputUnaryChain(expr.args[0]!, isLeaf);
    if (inner === undefined) return undefined;
    return [...inner, opcode];
  }
  return undefined;
}

// Recognize `wrap*( yName (yOp) g(input) )` (see header comment). `inputName`
// selects the stored-input variant (input leaf is that identifier) vs. the
// direct variant (input leaf is a bare `read()`).
export function matchStackStopRisk(
  expr: ExpressionAst,
  yName: string,
  inputName?: string,
): StackStopRiskMatch | undefined {
  const isLeaf = (candidate: ExpressionAst): boolean =>
    inputName !== undefined
      ? isIdentifierExpression(candidate, inputName)
      : isReadCallExpression(candidate);
  if (countMatchingNodes(expr, isLeaf) !== 1) return undefined;

  const wraps: Array<readonly [number, string]> = [];
  let core = expr;
  while (
    core.kind === "call" &&
    core.args.length === 1 &&
    X_TRANSFORM_UNARY_OPCODES[core.callee.toLowerCase()] !== undefined &&
    expressionReferencesName(core.args[0]!, yName)
  ) {
    wraps.push(X_TRANSFORM_UNARY_OPCODES[core.callee.toLowerCase()]!);
    core = core.args[0]!;
  }

  if (core.kind !== "binary") return undefined;
  const yOp = core.op;
  let xExpr: ExpressionAst;
  let yOnLeft: boolean;
  if (isIdentifierExpression(core.left, yName)) {
    yOnLeft = true;
    xExpr = core.right;
  } else if (isIdentifierExpression(core.right, yName)) {
    yOnLeft = false;
    xExpr = core.left;
  } else {
    return undefined;
  }
  if ((yOp === "-" || yOp === "/") && !yOnLeft) return undefined;
  if (expressionReferencesName(xExpr, yName)) return undefined;

  let additive: { digit: number; op: "+" | "-" | "*" | "/" } | undefined;
  let chainExpr = xExpr;
  if (xExpr.kind === "binary") {
    const leftHasLeaf = countMatchingNodes(xExpr.left, isLeaf) > 0;
    const rightHasLeaf = countMatchingNodes(xExpr.right, isLeaf) > 0;
    if (leftHasLeaf && !rightHasLeaf) {
      const digit = singleDigitLiteral(xExpr.right);
      if (digit === undefined) return undefined;
      additive = { digit, op: xExpr.op };
      chainExpr = xExpr.left;
    } else if (rightHasLeaf && !leftHasLeaf) {
      if (xExpr.op === "-" || xExpr.op === "/") return undefined;
      const digit = singleDigitLiteral(xExpr.left);
      if (digit === undefined) return undefined;
      additive = { digit, op: xExpr.op };
      chainExpr = xExpr.right;
    } else {
      return undefined;
    }
  }

  const inputUnary = collectInputUnaryChain(chainExpr, isLeaf);
  if (inputUnary === undefined) return undefined;
  const match: StackStopRiskMatch = { yName, yOp, yOnLeft, inputUnary, wraps };
  if (additive !== undefined) match.additive = additive;
  return match;
}

export function optimizeDispatchDefaultCases(
  statement: Extract<StatementAst, { kind: "dispatch" }>,
  options: { preserveCaseOrder?: boolean } = {},
): { statement: Extract<StatementAst, { kind: "dispatch" }>; removed: number; reordered: number } {
  if (statement.cases.length === 0) return { statement, removed: 0, reordered: 0 };
  if (!statement.defaultBody) {
    if (options.preserveCaseOrder === true) return { statement, removed: 0, reordered: 0 };
    const ordered = orderNumericDispatchCasesForResidual(statement.cases);
    return {
      statement: ordered.reordered === 0 ? statement : { ...statement, cases: ordered.cases },
      removed: 0,
      reordered: ordered.reordered,
    };
  }

  const defaultBody = statement.defaultBody;
  const kept: typeof statement.cases = [];
  let removed = 0;
  for (let index = 0; index < statement.cases.length; index += 1) {
    const dispatchCase = statement.cases[index]!;
    const value = numericLiteralValue(dispatchCase.value);
    const laterSameValue = value !== undefined &&
      statement.cases.slice(index + 1).some((laterCase) => numericLiteralValue(laterCase.value) === value);
    if (
      value !== undefined &&
      !laterSameValue &&
      statementListsEqual(dispatchCase.body, defaultBody)
    ) {
      removed += 1;
      continue;
    }
    kept.push(dispatchCase);
  }

  const ordered = options.preserveCaseOrder === true
    ? { cases: kept, reordered: 0 }
    : orderNumericDispatchCasesForResidual(kept);
  const nextStatement = removed === 0 && ordered.reordered === 0
    ? statement
    : { ...statement, cases: ordered.cases };
  return { statement: nextStatement, removed, reordered: ordered.reordered };
}

export function orderNumericDispatchCasesForResidual(cases: DispatchCaseAst[]): { cases: DispatchCaseAst[]; reordered: number } {
  if (cases.length < 2) return { cases, reordered: 0 };
  const values = cases.map((dispatchCase) => numericLiteralValue(dispatchCase.value));
  if (values.some((value) => value === undefined)) return { cases, reordered: 0 };
  const seen = new Set<number>();
  for (const value of values as number[]) {
    if (seen.has(value)) return { cases, reordered: 0 };
    seen.add(value);
  }
  const residualOrder = bestResidualDispatchOrder(values as number[]);
  const order = orderMatchesIdentity(residualOrder) ? zeroFirstDispatchOrder(values as number[]) : residualOrder;
  const reordered = order.filter((originalIndex, index) => originalIndex !== index).length;
  if (reordered === 0) return { cases, reordered: 0 };
  const ordered = order.map((index) => cases[index]!);
  return { cases: ordered, reordered };
}

export function orderMatchesIdentity(order: readonly number[]): boolean {
  return order.every((originalIndex, index) => originalIndex === index);
}

export function zeroFirstDispatchOrder(values: readonly number[]): number[] {
  const zeroIndex = values.indexOf(0);
  if (zeroIndex <= 0) return values.map((_, index) => index);
  return [zeroIndex, ...values.map((_, index) => index).filter((index) => index !== zeroIndex)];
}

export function bestResidualDispatchOrder(values: readonly number[]): number[] {
  const current = values.map((_, index) => index);
  let best = current;
  const currentCost = residualDispatchValueCost(values, current);
  let bestCost = currentCost;
  if (values.length > 8) return current;

  const used = new Set<number>();
  const order: number[] = [];
  const visit = (previous: number | undefined, cost: number): void => {
    if (cost >= bestCost) return;
    if (order.length === values.length) {
      bestCost = cost;
      best = [...order];
      return;
    }
    for (let index = 0; index < values.length; index += 1) {
      if (used.has(index)) continue;
      used.add(index);
      order.push(index);
      const value = values[index]!;
      visit(value, cost + residualStepCost(previous, value));
      order.pop();
      used.delete(index);
    }
  };
  visit(undefined, 0);
  if (currentCost - bestCost < 3) return current;
  return best;
}

export function residualDispatchValueCost(values: readonly number[], order: readonly number[]): number {
  let previous: number | undefined;
  let cost = 0;
  for (const index of order) {
    const value = values[index]!;
    cost += residualStepCost(previous, value);
    previous = value;
  }
  return cost;
}

export function residualStepCost(previous: number | undefined, value: number): number {
  const delta = previous === undefined ? value : value - previous;
  return delta === 0 ? 0 : estimateNumberCost(String(Math.abs(delta))) + 1;
}

export function statementListsEqual(left: readonly StatementAst[], right: readonly StatementAst[]): boolean {
  return left.length === right.length && left.every((statement, index) => statementEquals(statement, right[index]!));
}

export function statementEquals(left: StatementAst, right: StatementAst): boolean {
  if (left.kind !== right.kind) return false;
  switch (left.kind) {
    case "pause":
    case "preview":
      return expressionEquals(left.expr, (right as typeof left).expr) &&
        left.kind === (right as typeof left).kind;
    case "halt":
      return expressionEquals(left.expr, (right as typeof left).expr) &&
        left.literal === (right as typeof left).literal &&
        left.display === (right as typeof left).display;
    case "input":
      return left.target === (right as typeof left).target;
    case "assign":
      return left.target === (right as typeof left).target && expressionEquals(left.expr, (right as typeof left).expr);
    case "indexed_assign":
      return expressionEquals(left.target, (right as typeof left).target) &&
        expressionEquals(left.expr, (right as typeof left).expr);
    case "coord_list_remove":
      return left.list === (right as typeof left).list &&
        expressionEquals(left.item, (right as typeof left).item) &&
        JSON.stringify(left.items) === JSON.stringify((right as typeof left).items);
    case "loop":
      return statementListsEqual(left.body, (right as typeof left).body);
    case "while":
      return conditionEquals(left.condition, (right as typeof left).condition) &&
        statementListsEqual(left.body, (right as typeof left).body);
    case "if":
      return conditionEquals(left.condition, (right as typeof left).condition) &&
        statementListsEqual(left.thenBody, (right as typeof left).thenBody) &&
        statementListOptionEquals(left.elseBody, (right as typeof left).elseBody);
    case "dispatch":
      return expressionEquals(left.expr, (right as typeof left).expr) &&
        left.cases.length === (right as typeof left).cases.length &&
        left.cases.every((dispatchCase, index) =>
          expressionEquals(dispatchCase.value, (right as typeof left).cases[index]!.value) &&
          statementListsEqual(dispatchCase.body, (right as typeof left).cases[index]!.body)
        ) &&
        statementListOptionEquals(left.defaultBody, (right as typeof left).defaultBody);
    case "show":
      return left.display === (right as typeof left).display;
    case "call":
      return left.block === (right as typeof left).block;
    case "core":
      return rawLinesEqual(left.lines, (right as typeof left).lines) &&
        JSON.stringify(left.inputs ?? []) === JSON.stringify((right as typeof left).inputs ?? []) &&
        JSON.stringify(left.outputs ?? []) === JSON.stringify((right as typeof left).outputs ?? []) &&
        JSON.stringify(left.clobbers ?? []) === JSON.stringify((right as typeof left).clobbers ?? []) &&
        JSON.stringify(left.preserves ?? []) === JSON.stringify((right as typeof left).preserves ?? []) &&
        left.strict === (right as typeof left).strict;
    case "return_value":
      return expressionEquals(left.expr, (right as typeof left).expr);
    case "decimal_series":
      return left.digits === (right as typeof left).digits &&
        left.counterStart === (right as typeof left).counterStart;
  }
}

export function statementListOptionEquals(left: StatementAst[] | undefined, right: StatementAst[] | undefined): boolean {
  if (left === undefined || right === undefined) return left === right;
  return statementListsEqual(left, right);
}

export function rawLinesEqual(left: readonly { text: string }[], right: readonly { text: string }[]): boolean {
  return left.length === right.length && left.every((line, index) => line.text === right[index]!.text);
}

export function countIdentifierReadsInCondition(condition: ConditionAst, name: string): number {
  return countIdentifierReads(condition.left, name) + countIdentifierReads(condition.right, name);
}

export function countIdentifierReads(expr: ExpressionAst, name: string): number {
  switch (expr.kind) {
    case "number":
    case "string":
      return 0;
    case "identifier":
      return expr.name === name ? 1 : 0;
    case "indexed":
      return countIdentifierReads(expr.index, name);
    case "unary":
      return countIdentifierReads(expr.expr, name);
    case "binary":
      return countIdentifierReads(expr.left, name) + countIdentifierReads(expr.right, name);
    case "call":
      return expr.args.reduce((sum, arg) => sum + countIdentifierReads(arg, name), 0);
  }
}

export function substituteConditionIdentifier(condition: ConditionAst, name: string, replacement: ExpressionAst): ConditionAst {
  return {
    left: substituteExpressionIdentifier(condition.left, name, replacement),
    op: condition.op,
    right: substituteExpressionIdentifier(condition.right, name, replacement),
  };
}

export function substituteExpressionIdentifier(expr: ExpressionAst, name: string, replacement: ExpressionAst): ExpressionAst {
  switch (expr.kind) {
    case "number":
    case "string":
      return expr;
    case "identifier":
      return expr.name === name ? replacement : expr;
    case "indexed":
      return { ...expr, index: substituteExpressionIdentifier(expr.index, name, replacement) };
    case "unary":
      return { ...expr, expr: substituteExpressionIdentifier(expr.expr, name, replacement) };
    case "binary":
      return {
        ...expr,
        left: substituteExpressionIdentifier(expr.left, name, replacement),
        right: substituteExpressionIdentifier(expr.right, name, replacement),
      };
    case "call":
      return { ...expr, args: expr.args.map((arg) => substituteExpressionIdentifier(arg, name, replacement)) };
  }
}

export function expressionPureForSubstitution(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
      return true;
    case "indexed":
      return expressionPureForSubstitution(expr.index);
    case "unary":
      return expressionPureForSubstitution(expr.expr);
    case "binary":
      return expressionPureForSubstitution(expr.left) && expressionPureForSubstitution(expr.right);
    case "call": {
      const name = expr.callee.toLowerCase();
      if (name === "random" || name === "entered") return false;
      return expr.args.every(expressionPureForSubstitution);
    }
  }
}

export function isNumericValue(expr: ExpressionAst, value: number): boolean {
  const parsed = numericLiteralValue(expr);
  return parsed !== undefined && parsed === value;
}

export function numericLiteralValue(expr: ExpressionAst): number | undefined {
  if (expr.kind === "unary" && expr.op === "-") {
    const value = numericLiteralValue(expr.expr);
    return value === undefined ? undefined : -value;
  }
  if (expr.kind !== "number") return undefined;
  const value = Number(expr.raw);
  return Number.isFinite(value) ? value : undefined;
}

export function estimateOrdinaryIfCost(statement: Extract<StatementAst, { kind: "if" }>, ast: ProgramAst): number {
  const thenStatement = statement.thenBody[0];
  if (statement.thenBody.length !== 1 || !thenStatement) return Number.POSITIVE_INFINITY;
  const thenCost = estimateSimpleStatementCost(thenStatement, ast);
  if (!Number.isFinite(thenCost)) return Number.POSITIVE_INFINITY;
  if (!statement.elseBody) return estimateConditionCost(statement.condition, ast) + thenCost;
  const elseStatement = statement.elseBody[0];
  if (statement.elseBody.length !== 1 || !elseStatement) return Number.POSITIVE_INFINITY;
  const elseCost = estimateSimpleStatementCost(elseStatement, ast);
  if (!Number.isFinite(elseCost)) return Number.POSITIVE_INFINITY;
  return estimateConditionCost(statement.condition, ast) + thenCost + 2 + elseCost;
}

export function estimateOrdinaryGuardedUpdateCost(statement: Extract<StatementAst, { kind: "if" }>, ast: ProgramAst): number {
  if (statement.elseBody !== undefined || statement.thenBody.length === 0) return Number.POSITIVE_INFINITY;
  let bodyCost = 0;
  for (const inner of statement.thenBody) {
    const cost = estimateSimpleStatementCost(inner, ast);
    if (!Number.isFinite(cost)) return Number.POSITIVE_INFINITY;
    bodyCost += cost;
  }
  return estimateConditionCost(statement.condition, ast) + bodyCost;
}

export function estimateGuardedUpdateSelectorCost(candidate: GuardedUpdateSelectorCandidate, scratch: string): number {
  const selector: ExpressionAst = { kind: "identifier", name: scratch };
  return estimateExpressionCost(candidate.selector) + 1 +
    candidate.updates.reduce(
      (sum, update) => sum + estimateExpressionCost(maskedGuardedUpdateExpression(update, selector)) + 1,
      0,
    );
}

export function estimateSimpleStatementCost(statement: StatementAst, ast: ProgramAst): number {
  switch (statement.kind) {
    case "assign":
      return estimateExpressionCost(statement.expr) + 1;
    case "preview":
      return estimateExpressionCost(statement.expr);
    case "pause":
    case "halt":
      return estimateExpressionCost(statement.expr) + 1;
    case "call": {
      const terminal = effectiveTerminalStatement(statement, ast);
      return terminal === undefined ? Number.POSITIVE_INFINITY : estimateSimpleStatementCost(terminal, ast);
    }
    case "decimal_series":
      return 64;
    default:
      return Number.POSITIVE_INFINITY;
  }
}

export function estimateConditionCost(
  condition: ConditionAst,
  ast: ProgramAst,
  preloadedConstants?: ReadonlySet<string>,
): number {
  return conditionCompileCost(
    selectCheaperEquivalentCondition(condition, ast, preloadedConstants).condition,
    preloadedConstants,
  );
}

export function estimateExpressionCostForCondition(
  expr: ExpressionAst,
  preloadedConstants: ReadonlySet<string> | undefined,
): number {
  if (preloadedConstants === undefined) return estimateExpressionCost(expr);
  if (expr.kind === "number" && preloadedConstants.has(normalizeConstantLiteral(expr.raw))) return 1;
  if (
    expr.kind === "unary" &&
    expr.op === "-" &&
    expr.expr.kind === "number" &&
    preloadedConstants.has(normalizeConstantLiteral(negatedNumberLiteral(expr.expr.raw)))
  ) {
    return 1;
  }
  switch (expr.kind) {
    case "string":
      return 0;
    case "number":
      return estimateNumberCost(expr.raw);
    case "identifier":
      return 1;
    case "indexed":
      return estimateExpressionCostForCondition(expr.index, preloadedConstants) + 2;
    case "unary":
      return estimateExpressionCostForCondition(expr.expr, preloadedConstants) + 1;
    case "binary": {
      if (expr.op === "*" && expressionEquals(expr.left, expr.right) && isPureExpression(expr.left)) {
        return estimateExpressionCostForCondition(expr.left, preloadedConstants) + 1;
      }
      if (expr.op === "/" && isNumericValue(expr.left, 1)) {
        return estimateExpressionCostForCondition(expr.right, preloadedConstants) + 1;
      }
      const remainder = matchRemainderByConstant(expr);
      if (remainder !== undefined) {
        return estimateExpressionCostForCondition(remainder.value, preloadedConstants) +
          estimateExpressionCostForCondition(remainder.divisor, preloadedConstants) * 2 +
          3;
      }
      return estimateExpressionCostForCondition(expr.left, preloadedConstants) +
        estimateExpressionCostForCondition(expr.right, preloadedConstants) +
        1;
    }
    case "call":
      return estimateCallCostForCondition(expr, preloadedConstants);
  }
}

export function estimateCallCostForCondition(
  expr: Extract<ExpressionAst, { kind: "call" }>,
  preloadedConstants: ReadonlySet<string>,
): number {
  const name = expr.callee.toLowerCase();
  if (name === NEGATIVE_ZERO_DEGREE_SELECTOR_GE) {
    return estimateNegativeZeroDegreeSelectorCost(expr, preloadedConstants);
  }
  const smallSetMacro = smallSetExpressionMacro(name, expr.args);
  if (smallSetMacro !== undefined) return estimateExpressionCostForCondition(smallSetMacro, preloadedConstants);
  const macro = packedGridExpressionMacro(name, expr.args);
  if (macro !== undefined) return estimateExpressionCostForCondition(macro, preloadedConstants);
  if (name === "random") {
    if (expr.args.length === 1) {
      return estimateExpressionCostForCondition(randomRangeExpression(numberExpression(0), expr.args[0]!), preloadedConstants);
    }
    return expr.args.length === 2
      ? estimateExpressionCostForCondition(randomRangeExpression(expr.args[0]!, expr.args[1]!), preloadedConstants)
      : 1;
  }
  if (name === "pi") return 1;
  if (name === "e") return 2;
  if (name === "min" && expr.args.length === 2) {
    return estimateExpressionCostForCondition(minExpression(expr.args[0]!, expr.args[1]!), preloadedConstants);
  }
  if (name === "pow" || ["max", "bit_and", "bit_or", "bit_xor"].includes(name)) {
    if (name === "pow" && expr.args[1] !== undefined && isNumericValue(expr.args[1], 2)) {
      return (expr.args[0] ? estimateExpressionCostForCondition(expr.args[0], preloadedConstants) : 0) + 1;
    }
    if (name === "pow" && expr.args[0] !== undefined && isNumericValue(expr.args[0], 10)) {
      return (expr.args[1] ? estimateExpressionCostForCondition(expr.args[1], preloadedConstants) : 0) + 1;
    }
    return (expr.args[0] ? estimateExpressionCostForCondition(expr.args[0], preloadedConstants) : 0) +
      (expr.args[1] ? estimateExpressionCostForCondition(expr.args[1], preloadedConstants) : 0) +
      1;
  }
  return (expr.args[0] ? estimateExpressionCostForCondition(expr.args[0], preloadedConstants) : 0) + 1;
}

export function estimateNegativeZeroDegreeSelectorCost(
  expr: Extract<ExpressionAst, { kind: "call" }>,
  preloadedConstants?: ReadonlySet<string>,
): number {
  if (expr.args.length !== 2 || expr.args[0] === undefined || expr.args[1] === undefined) {
    return Number.POSITIVE_INFINITY;
  }
  return estimateNegativeZeroThresholdRawCost(expr.args[0], expr.args[1], preloadedConstants) + 1;
}

export function estimateNegativeZeroThresholdFlowCost(
  threshold: { value: ExpressionAst; bound: number },
  preloadedConstants?: ReadonlySet<string>,
): number {
  return estimateNegativeZeroThresholdRawCost(threshold.value, numberExpression(threshold.bound), preloadedConstants) + 2;
}

export function estimateNegativeZeroThresholdRawCost(
  value: ExpressionAst,
  bound: ExpressionAst,
  preloadedConstants?: ReadonlySet<string>,
): number {
  const ratio = divideExpressions(value, bound);
  return estimateExpressionCostForCondition(ratio, preloadedConstants) + 4;
}

export function estimateExpressionCost(expr: ExpressionAst): number {
  switch (expr.kind) {
    case "string":
      return 0;
    case "number":
      return estimateNumberCost(expr.raw);
    case "identifier":
      return 1;
    case "indexed":
      return estimateExpressionCost(expr.index) + 2;
    case "unary":
      return estimateExpressionCost(expr.expr) + 1;
    case "binary": {
      if (expr.op === "*" && expressionEquals(expr.left, expr.right) && isPureExpression(expr.left)) {
        return estimateExpressionCost(expr.left) + 1;
      }
      if (expr.op === "/" && isNumericValue(expr.left, 1)) {
        return estimateExpressionCost(expr.right) + 1;
      }
      const remainder = matchRemainderByConstant(expr);
      if (remainder !== undefined) {
        return estimateExpressionCost(remainder.value) + estimateExpressionCost(remainder.divisor) * 2 + 3;
      }
      return estimateExpressionCost(expr.left) + estimateExpressionCost(expr.right) + 1;
    }
    case "call":
      return estimateCallCost(expr);
  }
}

export function estimateCallCost(expr: Extract<ExpressionAst, { kind: "call" }>): number {
  const name = expr.callee.toLowerCase();
  if (name === NEGATIVE_ZERO_DEGREE_SELECTOR_GE) {
    return estimateNegativeZeroDegreeSelectorCost(expr);
  }
  const smallSetMacro = smallSetExpressionMacro(name, expr.args);
  if (smallSetMacro !== undefined) return estimateExpressionCost(smallSetMacro);
  const macro = packedGridExpressionMacro(name, expr.args);
  if (macro !== undefined) return estimateExpressionCost(macro);
  if (name === "random") {
    if (expr.args.length === 1) return estimateExpressionCost(randomRangeExpression(numberExpression(0), expr.args[0]!));
    return expr.args.length === 2 ? estimateExpressionCost(randomRangeExpression(expr.args[0]!, expr.args[1]!)) : 1;
  }
  if (name === "pi") return 1;
  if (name === "e") return 2;
  if (name === "min" && expr.args.length === 2) return estimateExpressionCost(minExpression(expr.args[0]!, expr.args[1]!));
  if (name === "safe_max" && expr.args.length === 2) return estimateExpressionCost(safeMaxExpression(expr.args[0]!, expr.args[1]!));
  if (name === "safe_min" && expr.args.length === 2) return estimateExpressionCost(safeMinExpression(expr.args[0]!, expr.args[1]!));
  if (name === "pow") {
    if (expr.args[1] !== undefined && isNumericValue(expr.args[1], 2)) {
      return (expr.args[0] ? estimateExpressionCost(expr.args[0]) : 0) + 1;
    }
    if (expr.args[0] !== undefined && isNumericValue(expr.args[0], 10)) {
      return (expr.args[1] ? estimateExpressionCost(expr.args[1]) : 0) + 1;
    }
    return (expr.args[0] ? estimateExpressionCost(expr.args[0]) : 0) +
      (expr.args[1] ? estimateExpressionCost(expr.args[1]) : 0) +
      1;
  }
  if (["max", "bit_and", "bit_or", "bit_xor"].includes(name)) {
    return (expr.args[0] ? estimateExpressionCost(expr.args[0]) : 0) +
      (expr.args[1] ? estimateExpressionCost(expr.args[1]) : 0) +
      1;
  }
  return (expr.args[0] ? estimateExpressionCost(expr.args[0]) : 0) + 1;
}

export function estimateNumberCost(raw: string): number {
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

export function residualAdjustmentCost(previousValue: number, nextValue: number): number {
  const delta = previousValue - nextValue;
  if (delta === 0) return 0;
  return estimateNumberCost(String(Math.abs(delta))) + 1;
}

export interface ExecutableSetupPreload {
  register: RegisterName;
  value: string;
  kind: "number" | "display-literal";
}

export function countStatements(statements: StatementAst[]): number {
  let count = 0;
  for (const statement of statements) {
    count += 1;
    if (statement.kind === "loop") count += countStatements(statement.body);
    if (statement.kind === "while") count += countStatements(statement.body);
    if (statement.kind === "if") {
      count += countStatements(statement.thenBody);
      if (statement.elseBody) count += countStatements(statement.elseBody);
    }
    if (statement.kind === "dispatch") {
      count += statement.cases.reduce((sum, dispatchCase) => sum + countStatements(dispatchCase.body), 0);
      if (statement.defaultBody) count += countStatements(statement.defaultBody);
    }
  }
  return count;
}

export function selectDispatchCandidate(
  statement: Extract<StatementAst, { kind: "dispatch" }>,
  machineProfile: MachineProfile,
): { selected: CandidateReport; candidates: CandidateReport[] } {
  const site = statement.name ?? `dispatch@${statement.line}`;
  const fallthroughCost = estimateDispatchCost(statement, true);
  const candidates: CandidateReport[] = [
    {
      site,
      variant: "fallthrough-compare-chain",
      steps: fallthroughCost,
      selected: true,
      reason: "uses case ordering; key-based dispatch does not already provide an address-valued selector",
    },
  ];

  if (machineSupports(machineProfile, "indirect-flow")) {
    candidates.push({
      site,
      variant: "indirect-register-flow",
      steps: Math.max(fallthroughCost, statement.cases.length + 3),
      selected: false,
      reason: "rejected; selector is key-valued, not address-valued, and building an address register would not beat the compare-chain",
    });
  }

  if (
    machineSupports(machineProfile, "dark-entries") &&
    machineSupports(machineProfile, "address-constants") &&
    machineSupports(machineProfile, "code-data-overlay")
  ) {
    candidates.push({
      site,
      variant: "dark-indirect-table",
      steps: Math.max(4, statement.cases.length + 3),
      selected: false,
      reason: "considered; layout proof did not establish a conflict-free address/data table for this site",
    });
  }

  if (
    statement.cases.length <= 6 &&
    machineSupports(machineProfile, "super-dark-dispatch") &&
    machineSupports(machineProfile, "indirect-flow")
  ) {
    candidates.push({
      site,
      variant: "super-dark-dispatch",
      steps: Math.max(3, statement.cases.length + 2),
      selected: false,
      reason: "considered; selector is key-valued, and layout proof did not place one-command cases at 48..53 with tails at 01..06",
    });
  }

  const selected = candidates.find((candidate) => candidate.selected) ?? candidates[0]!;
  return { selected, candidates };
}

export function estimateDispatchCost(
  statement: Extract<StatementAst, { kind: "dispatch" }>,
  fallthrough: boolean,
): number {
  const bodyCost = statement.cases.reduce((sum, dispatchCase) => sum + countStatements(dispatchCase.body), 0);
  const defaultCost = statement.defaultBody ? countStatements(statement.defaultBody) : 0;
  const jumpsAfterCases = Math.max(0, statement.cases.length - (fallthrough && !statement.defaultBody ? 1 : 0));
  return 2 + statement.cases.length * 5 + jumpsAfterCases * 2 + bodyCost + defaultCost;
}

export function orderRawInputs(
  inputs: NonNullable<Extract<StatementAst, { kind: "core" }>["inputs"]>,
): NonNullable<Extract<StatementAst, { kind: "core" }>["inputs"]> {
  const order = new Map([
    ["T", 0],
    ["Z", 1],
    ["Y", 2],
    ["X", 3],
  ]);
  return [...inputs].sort((left, right) => order.get(left.slot)! - order.get(right.slot)!);
}

export function formatRawContractDetail(statement: Extract<StatementAst, { kind: "core" }>): string {
  const inputs = statement.inputs?.length
    ? `takes ${orderRawInputs(statement.inputs).map((input) => `${input.slot}=${expressionToIntentText(input.expr)}`).join(", ")}`
    : "takes none";
  const outputs = statement.outputs?.length
    ? `returns ${statement.outputs.map((output) => `${output.slot}->${output.target}`).join(", ")}`
    : "returns none";
  const clobbers = `clobbers ${(statement.clobbers ?? ["unknown"]).join(", ")}`;
  const preserves = `preserves ${(statement.preserves ?? ["unknown"]).join(", ")}`;
  return `Inserted raw MK-61 block at line ${statement.line}: ${inputs}; ${outputs}; ${clobbers}; ${preserves}.`;
}

export function parseRawInstruction(
  text: string,
): { opcode: number; mnemonic: string; target?: string | number; formalTargetOpcode?: number; comment?: string } | undefined {
  const hex = /^[0-9A-Fa-f]{2}$/u.exec(text);
  if (hex) {
    const opcode = Number.parseInt(text, 16);
    return { opcode, mnemonic: getOpcode(opcode).name, comment: "raw hex" };
  }

  const direct = /^(БП|ПП|F\s*x<0|F\s*x=0|F\s*x(?:!=|≠)0|F\s*x(?:>=|≥)0|F\s*L[0-3])\s+([A-Za-z_][\w]*|[0-9A-Fa-f]{2})$/u.exec(text);
  if (direct) {
    const opcode = directOpcode(direct[1]!);
    const target = parseTarget(direct[2]!);
    return {
      opcode,
      mnemonic: getOpcode(opcode).name,
      ...(typeof target === "object" ? target : { target }),
      comment: "raw branch",
    };
  }

  const compactStore = /^хП([0-9a-eавсде])$/iu.exec(text);
  if (compactStore) {
    const register = registerFromText(compactStore[1]!);
    return { opcode: 0x40 + registerIndex(register), mnemonic: `X->П ${register}` };
  }
  const compactRecall = /^Пх([0-9a-eавсде])$/iu.exec(text);
  if (compactRecall) {
    const register = registerFromText(compactRecall[1]!);
    return { opcode: 0x60 + registerIndex(register), mnemonic: `П->X ${register}` };
  }

  const directMemory = /^(X(?:->|→)П|П(?:->|→)X)\s+R?([0-9a-eавсде])$/iu.exec(text);
  if (directMemory) {
    const register = registerFromText(directMemory[2]!);
    const op = directMemory[1]!.replaceAll("→", "->");
    const base = op.startsWith("X") ? 0x40 : 0x60;
    return {
      opcode: base + registerIndex(register),
      mnemonic: `${op} ${register}`,
    };
  }

  const indirect = /^(К\s*)?(БП|ПП|X(?:->|→)П|П(?:->|→)X|x(?:!=|≠)0|x(?:>=|≥)0|x<0|x=0)\s*R?([0-9a-eавсде])$/iu.exec(text);
  if (indirect?.[1]) {
    const register = registerFromText(indirect[3]!);
    return {
      opcode: indirectBase(indirect[2]!) + registerIndex(register),
      mnemonic: `К ${indirect[2]} ${register}`,
    };
  }

  const compactIndirect = /^К(БП|ПП|Пх|хП)([0-9a-eавсде])$/iu.exec(text);
  if (compactIndirect) {
    const register = registerFromText(compactIndirect[2]!);
    const op =
      compactIndirect[1] === "Пх"
        ? "П->X"
        : compactIndirect[1] === "хП"
          ? "X->П"
          : compactIndirect[1]!;
    return {
      opcode: indirectBase(op) + registerIndex(register),
      mnemonic: `К ${op} ${register}`,
    };
  }

  const found = findOpcodeName(text);
  if (found) return { opcode: found.code, mnemonic: found.name };
  return undefined;
}

export function parseTarget(text: string): string | number | { target: number; formalTargetOpcode: number } {
  const formalOpcode = parseFormalAddressOpcode(text);
  if (formalOpcode === undefined) return text;
  const info = formalAddressInfo(formalOpcode);
  return { target: info.ordinal, formalTargetOpcode: formalOpcode };
}

export function directOpcode(text: string): number {
  const normalized = text.replace(/\s+/g, " ").replaceAll("≠", "!=").replaceAll("≥", ">=");
  if (normalized === "БП") return 0x51;
  if (normalized === "ПП") return 0x53;
  if (normalized === "F x<0") return 0x5c;
  if (normalized === "F x=0") return 0x5e;
  if (normalized === "F x!=0") return 0x57;
  if (normalized === "F x>=0") return 0x59;
  if (normalized === "F L0") return 0x5d;
  if (normalized === "F L1") return 0x5b;
  if (normalized === "F L2") return 0x58;
  if (normalized === "F L3") return 0x5a;
  throw new Error(`Unknown direct opcode ${text}`);
}

export function indirectBase(text: string): number {
  const normalized = text.toLowerCase().replaceAll("→", "->").replaceAll("≠", "!=").replaceAll("≥", ">=");
  if (normalized === "x!=0") return 0x70;
  if (normalized === "бп") return 0x80;
  if (normalized === "x>=0") return 0x90;
  if (normalized === "пп") return 0xa0;
  if (normalized === "x->п") return 0xb0;
  if (normalized === "x<0") return 0xc0;
  if (normalized === "п->x") return 0xd0;
  if (normalized === "x=0") return 0xe0;
  throw new Error(`Unknown indirect opcode ${text}`);
}

export function binaryOpcode(op: "+" | "-" | "*" | "/"): number {
  return op === "+" ? 0x10 : op === "-" ? 0x11 : op === "*" ? 0x12 : 0x13;
}
