import { registerIndex } from "../../opcodes.ts";
import type { ExpressionAst, RegisterName, StatementAst, StateBankMemberAst } from "../../types.ts";
import { contiguousRegisterOffset } from "../../state-banks.ts";
import type { LoweringCtx } from "../lowering-ctx.ts";
import {
  buildDiagnostic,
  expressionToIntentText,
  numericLiteralValue,
  parseRawInstruction,
} from "../lowering-helpers.ts";
import { compileExpression } from "./expr.ts";
import { emitBitMaskFromCurrentXWithQuotientScratch } from "./spatial.ts";

export const LINE4_MOVE_SCRATCH_PREFIX = "__mkpro_line4_";

interface Line4MoveCall {
  bank: string;
  occupied: string;
  cell: string;
  delta: ExpressionAst;
}

interface Line4MoveScratch {
  delta: string;
  x: string;
  y: string;
  index: string;
  mask: string;
  pow: string;
  selector: string;
  threshold: string;
}

interface ResolvedLine4Bank {
  member: StateBankMemberAst;
  offset: number;
}

export function compileLine4MoveAssignment(
  ctx: LoweringCtx,
  statement: Extract<StatementAst, { kind: "assign" }>,
): boolean {
  const call = line4MoveCall(statement.expr);
  if (call === undefined) return false;
  const delta = numericLiteralValue(call.delta);
  if (delta !== 1 && delta !== -1) {
    ctx.diagnostics.push(buildDiagnostic(
      "error",
      `line4_move delta must be +1 or -1, got ${expressionToIntentText(call.delta)}.`,
      statement.line,
    ));
    return true;
  }
  if (ctx.findStateField(call.occupied) === undefined) {
    ctx.diagnostics.push(buildDiagnostic("error", `Unknown line4_move occupancy state '${call.occupied}'.`, statement.line));
    return true;
  }
  if (ctx.findStateField(call.cell) === undefined) {
    ctx.diagnostics.push(buildDiagnostic("error", `Unknown line4_move cell state '${call.cell}'.`, statement.line));
    return true;
  }
  if (ctx.findStateField(statement.target) === undefined) {
    ctx.diagnostics.push(buildDiagnostic("error", `Unknown line4_move result state '${statement.target}'.`, statement.line));
    return true;
  }
  const bank = resolveLine4Bank(ctx, call.bank, statement.line);
  if (bank === undefined) return true;
  const scratch = line4MoveScratchNames(call.bank, statement.target);
  if (!line4ScratchAllocated(ctx, scratch, statement.line)) return true;

  const helper = ctx.ensureLine4MoveHelper(call.bank, call.occupied, call.cell, statement.target, statement.line);
  compileExpression(ctx, call.delta);
  ctx.emitStore(scratch.delta, "line4_move delta", statement.line);
  ctx.emitJump(0x53, "ПП", helper.label, "line4_move", statement.line);
  ctx.optimizations.push({
    name: "line4-move-helper-call",
    detail: `Lowered line4_move(${call.bank}, ${call.occupied}, ${call.cell}, ${expressionToIntentText(call.delta)}) through ${helper.label}.`,
  });
  return true;
}

export function compileLine4RandomReplyHalt(
  ctx: LoweringCtx,
  statement: Extract<StatementAst, { kind: "halt" }>,
): boolean {
  const call = line4RandomReplyCall(statement.expr);
  if (call === undefined) return false;
  emitLine4RandomReplyProgram(ctx, statement.line);
  ctx.optimizations.push({
    name: "line4-random-reply-program",
    detail: "Lowered line4_random_reply() as a compact 4x4 random-reply line-game kernel.",
  });
  return true;
}

export function line4MoveScratchNames(bank: string, target: string): Line4MoveScratch {
  const base = `${LINE4_MOVE_SCRATCH_PREFIX}${bank}_${target}`;
  return {
    delta: `${base}_delta`,
    x: `${base}_x`,
    y: `${base}_y`,
    index: `${base}_index`,
    mask: `${base}_mask`,
    pow: `${base}_mask`,
    selector: `${base}_selector`,
    threshold: `${base}_threshold`,
  };
}

export function line4MoveCall(expr: ExpressionAst): Line4MoveCall | undefined {
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "line4_move" || expr.args.length !== 4) {
    return undefined;
  }
  const [bank, occupied, cell, delta] = expr.args;
  if (bank?.kind !== "identifier" || occupied?.kind !== "identifier" || cell?.kind !== "identifier" || delta === undefined) {
    return undefined;
  }
  return {
    bank: bank.name,
    occupied: occupied.name,
    cell: cell.name,
    delta,
  };
}

export function line4RandomReplyCall(expr: ExpressionAst): boolean | undefined {
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "line4_random_reply") return undefined;
  return true;
}

const LINE4_RANDOM_REPLY_PROGRAM = [
  "В/О",
  "Пх2",
  "Пх3",
  "С/П",
  "хП3",
  "КППb",
  "Кx=0 3",
  "4",
  "хП1",
  "4",
  "хП2",
  "ПП EB",
  "KΛ",
  "К{x}",
  "Кx=0 c",
  "9",
  "8",
  "ПП 58",
  "Пх0",
  "Кmax",
  "хП0",
  "-",
  "Кx=0 c",
  "Пх1",
  "хП3",
  "Пх2",
  "хП8",
  "F L2 11",
  "F L1 09",
  "Пх8",
  "КППd",
  "хП2",
  "Пх3",
  "КППd",
  "хП1",
  "Пх9",
  "Пх2",
  "Пхc",
  "*",
  "F 10^x",
  "К[x]",
  "Пх1",
  "F 10^x",
  "+",
  "К∨",
  "хП9",
  "хП0",
  "-",
  "F x!=0 97",
  "Пхa",
  "/-/",
  "хПa",
  "F tg",
  "хПe",
  "Пх7",
  "Пх1",
  "КППe",
  "Пх6",
  "Пх2",
  "КППe",
  "Пх5",
  "Пх2",
  "ПП 72",
  "Пх4",
  "Пх2",
  "/-/",
  "Пх1",
  "+",
  "КППd",
  "КБПe",
  "К[x]",
  "4",
  "/",
  "К{x}",
  "4",
  "*",
  "F x>=0 85",
  "Кx=0 a",
  "4",
  "+",
  "В/О",
  "F 10^x",
  "Пхa",
  "*",
  "+",
  "КхП0",
  "Пхb",
  "KΛ",
  "К{x}",
  "Кx!=0 a",
  "хП3",
  "F 10^x",
  "/",
  "К{x}",
  "Пхd",
  "-",
  "F x^2",
  "+",
] as const;

function emitLine4RandomReplyProgram(ctx: LoweringCtx, line: number | undefined): void {
  for (const text of LINE4_RANDOM_REPLY_PROGRAM) {
    const parsed = parseRawInstruction(text);
    if (parsed === undefined) {
      ctx.diagnostics.push(buildDiagnostic("error", `Cannot lower line4_random_reply instruction '${text}'.`, line));
      return;
    }
    ctx.emitOp(parsed.opcode, parsed.mnemonic, "line4_random_reply", line, true);
    if (parsed.formalTargetOpcode !== undefined) {
      ctx.emitFormalAddress(parsed.formalTargetOpcode, "line4_random_reply", line);
    } else if (parsed.target !== undefined) {
      ctx.emitAddress(parsed.target, "line4_random_reply", line);
    }
  }
}

export function emitLine4MoveHelpers(ctx: LoweringCtx): void {
  for (const helper of ctx.line4MoveHelpers.values()) {
    const bank = resolveLine4Bank(ctx, helper.bank, helper.line);
    if (bank === undefined) continue;
    const scratch = line4MoveScratchNames(helper.bank, helper.target);
    const selectorRegister = selectorRegisterFor(ctx, scratch.selector, helper.line);
    if (selectorRegister === undefined) continue;
    const selectorIndex = registerIndex(selectorRegister);

    ctx.emitLabel(helper.label);
    emitLine4MoveBody(ctx, helper, scratch, bank.offset, selectorIndex);
    ctx.optimizations.push({
      name: "line4-move-helper",
      detail: `Emitted packed 4x4 line-state move helper for ${helper.bank}/${helper.occupied}.`,
    });

    ctx.emitLabel(helper.updateLabel);
    emitLine4DigitUpdateBody(ctx, helper, scratch, selectorIndex);

    ctx.emitLabel(helper.normLabel);
    emitLine4PositiveNormBody(ctx, helper.line);
  }
}

function emitLine4MoveBody(
  ctx: LoweringCtx,
  helper: { bank: string; occupied: string; cell: string; target: string; updateLabel: string; normLabel: string; line?: number },
  scratch: Line4MoveScratch,
  bankOffset: number,
  selectorIndex: number,
): void {
  const line = helper.line;
  const legalLabel = ctx.freshLabel("line4_move_legal");

  ctx.emitZero(`line4_move clear ${helper.target}`, line);
  ctx.emitStore(helper.target, `line4_move clear ${helper.target}`, line);

  emitLine4SplitCell(ctx, helper.cell, scratch, line);
  emitLine4DenseIndex(ctx, scratch, line);
  emitBitMaskFromCurrentXWithQuotientScratch(ctx, scratch.pow, line);
  ctx.emitStore(scratch.mask, "line4_move cell mask", line);

  ctx.emitRecall(helper.occupied, "line4_move occupied", line);
  ctx.emitRecall(scratch.mask, "line4_move cell mask", line);
  ctx.emitOp(0x37, "К ∧", "line4_move occupied test", line);
  ctx.emitOp(0x35, "К {x}", "line4_move occupied fraction", line);
  ctx.emitJump(0x5e, "F x=0", legalLabel, "line4_move legal cell", line);
  ctx.emitOp(0x52, "В/О", "line4_move occupied return", line);

  ctx.emitLabel(legalLabel);
  ctx.emitRecall(helper.occupied, "line4_move occupied", line);
  ctx.emitRecall(scratch.mask, "line4_move cell mask", line);
  ctx.emitOp(0x38, "К ∨", "line4_move set occupied", line);
  ctx.emitStore(helper.occupied, "line4_move set occupied", line);
  ctx.emitNumber("1");
  ctx.emitStore(helper.target, "line4_move legal", line);

  ctx.emitNumber("3");
  ctx.emitRecall(scratch.delta, "line4_move delta", line);
  ctx.emitOp(0x12, "*", "line4_move target delta", line);
  ctx.emitNumber("4");
  ctx.emitOp(0x10, "+", "line4_move win threshold", line);
  ctx.emitStore(scratch.threshold, "line4_move win threshold", line);

  emitLine4UpdateCall(ctx, scratch, helper.updateLabel, 1 + bankOffset, scratch.y, line);
  emitLine4UpdateCall(ctx, scratch, helper.updateLabel, 2 + bankOffset, scratch.x, line);

  ctx.emitRecall(scratch.x, "line4_move diag-left x", line);
  ctx.emitRecall(scratch.y, "line4_move diag-left y", line);
  ctx.emitOp(0x10, "+", "line4_move diag-left index", line);
  ctx.emitJump(0x53, "ПП", helper.normLabel, "line4_move norm4", line);
  ctx.emitStore(scratch.index, "line4_move diag-left index", line);
  emitLine4PreparedUpdateCall(ctx, scratch, helper.updateLabel, 3 + bankOffset, line);

  ctx.emitRecall(scratch.x, "line4_move diag-right x", line);
  ctx.emitRecall(scratch.y, "line4_move diag-right y", line);
  ctx.emitOp(0x11, "-", "line4_move diag-right index", line);
  ctx.emitNumber("4");
  ctx.emitOp(0x10, "+", "line4_move diag-right index", line);
  ctx.emitJump(0x53, "ПП", helper.normLabel, "line4_move norm4", line);
  ctx.emitStore(scratch.index, "line4_move diag-right index", line);
  emitLine4PreparedUpdateCall(ctx, scratch, helper.updateLabel, 4 + bankOffset, line);

  ctx.emitRecall(helper.target, "line4_move result", line);
  ctx.emitOp(0x52, "В/О", "line4_move return", line);

  // Keep selectorIndex visibly used here so the move body and update body are
  // forced to agree on the same indirect-memory register at compile time.
  void selectorIndex;
}

function emitLine4SplitCell(
  ctx: LoweringCtx,
  cell: string,
  scratch: Line4MoveScratch,
  line: number | undefined,
): void {
  ctx.emitRecall(cell, "line4_move cell", line);
  ctx.emitNumber("10");
  ctx.emitOp(0x13, "/", "line4_move y quotient", line);
  ctx.emitOp(0x34, "К [x]", "line4_move y", line);
  ctx.emitStore(scratch.y, "line4_move y", line);

  ctx.emitRecall(cell, "line4_move cell", line);
  ctx.emitNumber("10");
  ctx.emitOp(0x13, "/", "line4_move x quotient", line);
  ctx.emitOp(0x35, "К {x}", "line4_move x fraction", line);
  ctx.emitNumber("10");
  ctx.emitOp(0x12, "*", "line4_move x", line);
  ctx.emitStore(scratch.x, "line4_move x", line);
}

function emitLine4DenseIndex(ctx: LoweringCtx, scratch: Line4MoveScratch, line: number | undefined): void {
  ctx.emitRecall(scratch.y, "line4_move dense y", line);
  ctx.emitNumber("1");
  ctx.emitOp(0x11, "-", "line4_move dense row", line);
  ctx.emitNumber("4");
  ctx.emitOp(0x12, "*", "line4_move dense row", line);
  ctx.emitRecall(scratch.x, "line4_move dense x", line);
  ctx.emitOp(0x10, "+", "line4_move dense index", line);
}

function emitLine4UpdateCall(
  ctx: LoweringCtx,
  scratch: Line4MoveScratch,
  updateLabel: string,
  selector: number,
  indexVariable: string,
  line: number | undefined,
): void {
  ctx.emitRecall(indexVariable, "line4_move line digit index", line);
  ctx.emitStore(scratch.index, "line4_move line digit index", line);
  emitLine4PreparedUpdateCall(ctx, scratch, updateLabel, selector, line);
}

function emitLine4PreparedUpdateCall(
  ctx: LoweringCtx,
  scratch: Line4MoveScratch,
  updateLabel: string,
  selector: number,
  line: number | undefined,
): void {
  ctx.emitNumberOrPreload(String(selector));
  ctx.emitStore(scratch.selector, "line4_move line selector", line);
  ctx.emitJump(0x53, "ПП", updateLabel, "line4_move update line", line);
}

function emitLine4DigitUpdateBody(
  ctx: LoweringCtx,
  helper: { target: string; line?: number },
  scratch: Line4MoveScratch,
  selectorIndex: number,
): void {
  const line = helper.line;
  const noWinLabel = ctx.freshLabel("line4_no_win");
  ctx.emitRecall(scratch.index, "line4_update digit index", line);
  ctx.emitOp(0x15, "F 10^x", "line4_update digit divisor", line);
  ctx.emitStore(scratch.pow, "line4_update digit divisor", line);

  ctx.emitOp(0xd0 + selectorIndex, `К П->X ${selectorIndex.toString(16)}`, "line4_update recall line", line);
  ctx.emitRecall(scratch.pow, "line4_update digit divisor", line);
  ctx.emitOp(0x13, "/", "line4_update shifted line", line);
  ctx.emitOp(0x35, "К {x}", "line4_update digit fraction", line);
  ctx.emitNumber("10");
  ctx.emitOp(0x12, "*", "line4_update digit", line);
  ctx.emitOp(0x34, "К [x]", "line4_update digit", line);
  ctx.emitRecall(scratch.threshold, "line4_update win threshold", line);
  ctx.emitOp(0x11, "-", "line4_update win compare", line);
  ctx.emitJump(0x57, "F x!=0", noWinLabel, "line4_update no win", line);
  ctx.emitNumber("2");
  ctx.emitStore(helper.target, "line4_update win", line);

  ctx.emitLabel(noWinLabel);
  ctx.emitRecall(scratch.pow, "line4_update digit divisor", line);
  ctx.emitNumber("10");
  ctx.emitOp(0x13, "/", "line4_update digit place", line);
  ctx.emitRecall(scratch.delta, "line4_update delta", line);
  ctx.emitOp(0x12, "*", "line4_update signed place", line);
  ctx.emitOp(0xd0 + selectorIndex, `К П->X ${selectorIndex.toString(16)}`, "line4_update recall line", line);
  ctx.emitOp(0x10, "+", "line4_update add mark", line);
  ctx.emitOp(0xb0 + selectorIndex, `К X->П ${selectorIndex.toString(16)}`, "line4_update store line", line);
  ctx.emitOp(0x52, "В/О", "line4_update return", line);
}

function emitLine4PositiveNormBody(ctx: LoweringCtx, line: number | undefined): void {
  const nonZeroLabel = ctx.freshLabel("line4_norm_nonzero");
  ctx.emitNumber("4");
  ctx.emitOp(0x13, "/", "line4_norm quotient", line);
  ctx.emitOp(0x35, "К {x}", "line4_norm remainder", line);
  ctx.emitNumber("4");
  ctx.emitOp(0x12, "*", "line4_norm scale", line);
  ctx.emitJump(0x57, "F x!=0", nonZeroLabel, "line4_norm nonzero", line);
  ctx.emitNumber("4");
  ctx.emitOp(0x52, "В/О", "line4_norm zero return", line);
  ctx.emitLabel(nonZeroLabel);
  ctx.emitOp(0x52, "В/О", "line4_norm return", line);
}

function resolveLine4Bank(
  ctx: LoweringCtx,
  name: string,
  line: number | undefined,
): ResolvedLine4Bank | undefined {
  const bank = ctx.ast.banks?.find((candidate) => candidate.name === name);
  const member = bank?.members.find((candidate) => candidate.name === undefined);
  if (bank === undefined || member === undefined) {
    ctx.diagnostics.push(buildDiagnostic("error", `line4_move expects '${name}' to be a packed[1..4] state bank.`, line));
    return undefined;
  }
  const indexes = member.elements.map((element) => element.index).join(",");
  if (member.type !== "packed" || indexes !== "1,2,3,4") {
    ctx.diagnostics.push(buildDiagnostic("error", `line4_move expects '${name}' to be packed[1..4], got ${member.type}[${indexes}].`, line));
    return undefined;
  }
  const offset = contiguousRegisterOffset(member, ctx.allocation.registers);
  if (offset === undefined) {
    ctx.diagnostics.push(buildDiagnostic("error", `line4_move state bank '${name}' must be allocated in contiguous registers.`, line));
    return undefined;
  }
  return { member, offset };
}

function selectorRegisterFor(ctx: LoweringCtx, name: string, line: number | undefined): RegisterName | undefined {
  const register = ctx.allocation.registers[name];
  if (register === undefined) {
    ctx.diagnostics.push(buildDiagnostic("error", `No register allocated for ${name}.`, line));
    return undefined;
  }
  if (registerIndex(register) < 7) {
    ctx.diagnostics.push(buildDiagnostic("error", `line4_move selector '${name}' needs R7..Re, got R${register}.`, line));
    return undefined;
  }
  return register;
}

function line4ScratchAllocated(ctx: LoweringCtx, scratch: Line4MoveScratch, line: number | undefined): boolean {
  for (const name of Object.values(scratch)) {
    if (ctx.allocation.registers[name] !== undefined) continue;
    ctx.diagnostics.push(buildDiagnostic("error", `No register allocated for ${name}.`, line));
    return false;
  }
  return true;
}
