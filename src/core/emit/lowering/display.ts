import { registerIndex } from "../../opcodes.ts";
import { machineSupports } from "../../machineProfile.ts";
import { bankMemberKey, findStateBankMember } from "../../state-banks.ts";
import type { ProgramAst, RegisterName, StatementAst } from "../../types.ts";
import type { LoweringCtx } from "../lowering-ctx.ts";
import {
  emitErrorStopOpcode,
} from "./proc-raw-setup.ts";
import type {
  DisplayField,
  DisplayLiteralProgram,
  FirstSpliceDisplayLiteralProgram,
} from "../lowering-helpers.ts";
import {
  COORD_LIST_DX,
  DISPLAY_EXPR_PREFIX,
  buildDiagnostic,
  dashedCoordReportDisplayTemplate,
  decimalDisplayLiteralNumber,
  displayLiteralProgram,
  displayLoopOpcode,
  exponentTailDisplayLiteralProgram,
  firstSpliceDisplayLiteralProgram,
  leadingZeroHexProductDisplayProgram,
  normalizeConstantLiteral,
  shouldUsePreloadedDisplayLiteral,
  signDigitLiteralDisplayProgram,
  signedFirstSpliceDisplayLiteralProgram,
  zeroDigitTailDisplayProgram,
} from "../lowering-helpers.ts";
import { compileExpression } from "./expr.ts";

export function compileShowSequenceRead(ctx: LoweringCtx, 
    firstShow: Extract<StatementAst, { kind: "show" }>,
    secondShow: Extract<StatementAst, { kind: "show" }>,
    input: Extract<StatementAst, { kind: "input" }>,
  ): boolean {
    const helper = ctx.sharedShowSequenceHelper(firstShow.display, secondShow.display, firstShow.line);
    if (helper === undefined) return false;
    ctx.emitJump(0x53, "ПП", helper.label, `show ${firstShow.display}; show ${secondShow.display}`, firstShow.line);
    ctx.emitStore(input.target, `read ${input.target}`, input.line);
    ctx.optimizations.push({
      name: "show-sequence-helper-call",
      detail: `Reused shared helper for show ${firstShow.display}; show ${secondShow.display}; read ${input.target}.`,
    });
    return true;
}

export function compileShow(ctx: LoweringCtx, displayName: string, line: number): void {
    const display = ctx.ast.displays.find((candidate) => candidate.name === displayName);
    if (!display) {
      ctx.diagnostics.push(buildDiagnostic("error", `Unknown display '${displayName}'.`, line));
      return;
    }

    const literalHelper = ctx.sharedLiteralDisplayHelper(display, line);
    if (literalHelper !== undefined) {
      ctx.emitJump(0x53, "ПП", literalHelper.label, `show ${display.name}`, line);
      ctx.optimizations.push({
        name: "screen-video-literal-helper-call",
        detail: `Reused shared literal video helper for screen ${display.name}.`,
      });
      return;
    }
    if (compileLiteralDisplay(ctx, display, line)) return;
    if (compileTextDisplay(ctx, display, line)) return;
    if (compileDashedCoordReportDisplay(ctx, display, line)) return;
    if (compileFloorPackedRowDisplay(ctx, display, line)) return;

    const strategy = ctx.selectDisplayStrategy(display);
    if (strategy === "packed-display-helper") {
      const helper = ctx.sharedDisplayHelper(display, line);
      if (helper !== undefined) {
        ctx.emitJump(0x53, "ПП", helper.label, `show ${display.name}`, line);
        ctx.optimizations.push({
          name: "packed-display-helper-call",
          detail: `Reused shared packed display helper for screen ${display.name}.`,
        });
        return;
      }
    }
    if (strategy === "display-byte-helper") {
      const helper = ctx.sharedDisplayByteHelper(display, line);
      if (helper !== undefined) {
        ctx.emitJump(0x53, "ПП", helper.label, `show ${display.name}`, line);
        ctx.optimizations.push({
          name: "display-byte-helper-call",
          detail: `Reused shared display-byte helper for screen ${display.name}.`,
        });
        return;
      }
    }

    if (strategy === "packed-storage-reuse" && compilePackedStorageReuseDisplay(ctx, display, line, true)) return;
    if (strategy === "display-byte-builder" && compileDisplayByteBuilder(ctx, display, line, true)) return;

    compilePackedDisplayBody(ctx, display, line, true);
    ctx.reportPackedDisplayLowering(display);
}

export function compileFloorPackedRowDisplay(ctx: LoweringCtx, display: ProgramAst["displays"][number], line: number): boolean {
    if (!machineSupports(ctx.machineProfile, "display-bytes")) return false;

    const [floor, separator, row] = display.items;
    if (
      display.items.length !== 3 ||
      floor?.kind !== "source" ||
      separator?.kind !== "literal" ||
      row?.kind !== "source" ||
      separator.text !== "."
    ) {
      return false;
    }

    const floorState = ctx.findStateField(floor.name);
    const rowState = row.expr === undefined ? ctx.findStateField(row.name) : undefined;
    const rowIsPacked = rowState?.type === "packed" || row.name.startsWith(DISPLAY_EXPR_PREFIX) || row.expr !== undefined;
    const floorMin = floorState?.min ?? 0;
    const floorMax = floorState?.max ?? floorMin;
    const floorWidth = floor.width ?? Math.max(1, String(Math.trunc(Math.max(Math.abs(floorMin), Math.abs(floorMax)))).length);
    if (
      floorState === undefined ||
      !rowIsPacked ||
      floorWidth !== 1 ||
      floorMin < 0 ||
      floorMax > 9 ||
      row.width !== undefined
    ) {
      return false;
    }

    if (row.expr !== undefined) {
      const indexedRowExpr = row.expr.kind === "indexed" ? row.expr : undefined;
      const indexedRow = indexedRowExpr === undefined ? undefined : findStateBankMember(ctx.ast, indexedRowExpr);
      compileExpression(ctx, row.expr);
      ctx.emitRecall(floor.name, `display ${display.name} floor`, line);
      ctx.emitOp(0x14, "<->", "display packed row expression merge", line);
      ctx.emitOp(0x0e, "В↑", "display packed row expression copy", line);
      ctx.emitOp(0x25, "F reverse", "display packed row expression rotate", line);
      ctx.emitOp(0x14, "<->", "display packed row floor restore", line);
      if (indexedRow !== undefined && indexedRowExpr !== undefined) {
        ctx.optimizations.push({
          name: "indexed-packed-row-table",
          detail: `Read ${bankMemberKey(indexedRowExpr.base, indexedRowExpr.field)}[${floor.name}] through indirect memory for screen ${display.name}.`,
        });
      }
      ctx.optimizations.push({
        name: "floor-packed-row-expression-display",
        detail: `Computed packed row expression inline for screen ${display.name} and spliced the one-digit floor through X2.`,
      });
    } else {
      ctx.emitRecall(floor.name, `display ${display.name} floor`, line);
      ctx.emitRecall(row.name, `display ${display.name} packed row`, line);
      ctx.emitOp(0x14, "<->", "display packed row floor merge", line);
    }
    ctx.emitOp(0x25, "F reverse", "display packed row preserve", line);
    ctx.emitOp(0x0c, "ВП", "display packed row restore", line);
    ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
    ctx.optimizations.push({
      name: "floor-packed-row-display",
      detail: `Displayed screen ${display.name} by splicing a one-digit floor into a packed video row.`,
    });
    return true;
}

export function compileDashedCoordReportDisplay(ctx: LoweringCtx, display: ProgramAst["displays"][number], line: number): boolean {
    const template = dashedCoordReportDisplayTemplate(display);
    if (template === undefined) return false;
    const maskRegister = ctx.allocation.registers[COORD_LIST_DX];
    if (maskRegister === undefined) return false;
    if (!ctx.displayFieldFitsUnsignedWidth(template.cell) || !ctx.displayFieldFitsUnsignedWidth(template.bearing)) {
      return false;
    }

    if (ctx.currentXDashedCoordReportBodyMatches(template)) {
      emitDashedCoordReportPackedBodyDisplay(ctx, display.name, maskRegister, line);
      ctx.optimizations.push({
        name: "dashed-coord-report-packed-body",
        detail: `Reused packed --CC-- N body already in X for screen ${display.name}.`,
      });
      ctx.optimizations.push({
        name: "dashed-coord-report-lowering",
        detail: `Lowered screen ${display.name} as --CC-- N calculator video output.`,
      });
      return true;
    }

    if (ctx.currentXVariable !== template.bearing.name) {
      ctx.emitRecall(template.bearing.name, `display ${display.name} bearing`, line);
    }
    ctx.emitRecall(template.cell.name, `display ${display.name} cell`, line);
    if (ctx.scaledCoordVariables.has(template.cell.name)) {
      ctx.emitNumberOrPreload("10");
      ctx.emitOp(0x12, "*", "display dashed scaled cell restore", line);
    }
    ctx.emitNumber("4");
    ctx.emitOp(0x15, "F 10^x", "display dashed cell scale", line);
    ctx.emitOp(0x12, "*", "display dashed cell shift", line);
    ctx.emitOp(0x10, "+", "display dashed bearing append", line);
    ctx.emitNumber("7");
    ctx.emitOp(0x15, "F 10^x", "display dashed video anchor", line);
    ctx.emitOp(0x10, "+", "display dashed video body", line);
    ctx.emitOp(0x60 + registerIndex(maskRegister), `П->X ${maskRegister}`, `display ${display.name} dashed mask`, line);
    ctx.emitOp(0x39, "К ⊕", "display dashed mask merge", line);
    ctx.emitOp(0x35, "К {x}", "display dashed video fraction", line);
    ctx.emitOp(0x0b, "/-/", "display dashed sign", line);
    ctx.emitOp(0x0c, "ВП", "display dashed exponent entry", line);
    ctx.emitOp(0x07, "7", "display dashed exponent", line);
    ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
    ctx.optimizations.push({
      name: "dashed-coord-report-lowering",
      detail: `Lowered screen ${display.name} as --CC-- N calculator video output.`,
    });
    return true;
}

export function emitDashedCoordReportPackedBodyDisplay(ctx: LoweringCtx, displayName: string, maskRegister: RegisterName, line: number): void {
    ctx.emitNumber("7");
    ctx.emitOp(0x15, "F 10^x", "display dashed video anchor", line);
    ctx.emitOp(0x10, "+", "display dashed video body", line);
    ctx.emitOp(0x60 + registerIndex(maskRegister), `П->X ${maskRegister}`, `display ${displayName} dashed mask`, line);
    ctx.emitOp(0x39, "К ⊕", "display dashed mask merge", line);
    ctx.emitOp(0x35, "К {x}", "display dashed video fraction", line);
    ctx.emitOp(0x0b, "/-/", "display dashed sign", line);
    ctx.emitOp(0x0c, "ВП", "display dashed exponent entry", line);
    ctx.emitOp(0x07, "7", "display dashed exponent", line);
    ctx.emitOp(0x50, "С/П", `show ${displayName}`, line);
}

export function compilePackedDisplayBody(ctx: LoweringCtx, 
    display: ProgramAst["displays"][number],
    line: number,
    reuseCurrentX: boolean,
  ): void {
    const fields = ctx.numericDisplayFields(display, line);
    if (fields === undefined) return;
    compilePackedDisplayFields(ctx, display, fields, line, reuseCurrentX);
    if (fields.some((field) => field.kind === "literal")) {
      ctx.optimizations.push({
        name: "display-decimal-literal-field",
        detail: `Packed decimal digit literals directly into screen ${display.name}.`,
      });
    }
    ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
}

export function compilePackedDisplayFields(ctx: LoweringCtx, 
    display: ProgramAst["displays"][number],
    fields: DisplayField[],
    line: number,
    reuseCurrentX: boolean,
  ): void {
    const currentIndex = reuseCurrentX && ctx.currentXVariable !== undefined
      ? fields.findIndex((field) => field.kind === "source" && field.name === ctx.currentXVariable)
      : -1;
    if (currentIndex > 0) {
      const current = fields[currentIndex]!;
      compilePackedDisplayFieldsInOrder(ctx, display, fields.slice(0, currentIndex), line, false);
      ctx.emitNumberOrPreload(String(10 ** current.width));
      ctx.emitOp(0x12, "*", "packed display field shift", line);
      ctx.emitOp(0x10, "+", "packed display current field append", line);
      for (const field of fields.slice(currentIndex + 1)) {
        ctx.emitNumberOrPreload(String(10 ** field.width));
        ctx.emitOp(0x12, "*", "packed display field shift", line);
        if (field.kind === "source" || field.value !== "0") {
          emitDisplayFieldValue(ctx, display, field, line);
          ctx.emitOp(0x10, "+", "packed display field append", line);
        }
      }
      ctx.optimizations.push({
        name: currentIndex === fields.length - 1 ? "display-current-x-suffix-reuse" : "display-current-x-middle-reuse",
        detail: `Reused ${current.name} already in X as field ${currentIndex + 1} of screen ${display.name}.`,
      });
      return;
    }

    const orderedFields = reuseCurrentX && ctx.canReorderNumericDisplay(display)
      ? orderDisplaySources(ctx, fields.map((field) => field.name))
        .map((source) => fields.find((field) => field.name === source)!)
      : fields;

    compilePackedDisplayFieldsInOrder(ctx, 
      display,
      orderedFields,
      line,
      reuseCurrentX,
    );
}

export function compilePackedDisplayFieldsInOrder(ctx: LoweringCtx, 
    display: ProgramAst["displays"][number],
    fields: DisplayField[],
    line: number,
    reuseCurrentX: boolean,
  ): void {
    if (fields.length === 0) {
      ctx.emitNumber("0");
    } else {
      for (let index = 0; index < fields.length; index += 1) {
        const field = fields[index]!;
        if (index === 0 && reuseCurrentX && field.kind === "source" && field.name === ctx.currentXVariable) {
          ctx.optimizations.push({
            name: "display-current-x-reuse",
            detail: `Reused ${field.name} already in X as the first field of screen ${display.name}.`,
          });
          continue;
        }
        if (index === 0) {
          emitDisplayFieldValue(ctx, display, field, line);
        } else {
          ctx.emitNumberOrPreload(String(10 ** field.width));
          ctx.emitOp(0x12, "*", "packed display field shift", line);
          if (field.kind === "source" || field.value !== "0") {
            emitDisplayFieldValue(ctx, display, field, line);
            ctx.emitOp(0x10, "+", "packed display field append", line);
          }
        }
      }
    }
}

export function emitDisplayFieldValue(ctx: LoweringCtx, 
    display: ProgramAst["displays"][number],
    field: DisplayField,
    line: number,
  ): void {
    if (field.kind === "literal") {
      ctx.emitNumberOrPreload(field.value ?? "0");
      const last = ctx.items.at(-1);
      if (last?.kind === "op") last.comment = `display ${display.name} digit literal`;
      return;
    }
    ctx.emitRecall(field.name, `display ${display.name} source`, line);
}

export function compilePackedStorageReuseDisplay(ctx: LoweringCtx, 
    display: ProgramAst["displays"][number],
    line: number,
    reuseCurrentX: boolean,
  ): boolean {
    const fields = ctx.packedStorageReuseFields(display);
    if (fields === undefined) return false;
    const ordered = ctx.orderStorageReuseFields(fields, reuseCurrentX);
    for (let index = 0; index < ordered.length; index += 1) {
      const field = ordered[index]!;
      if (!(index === 0 && reuseCurrentX && field.name === ctx.currentXVariable)) {
        ctx.emitRecall(field.name, `display ${display.name} packed field`, line);
      }
      if (index > 0) ctx.emitOp(0x10, "+", "packed display storage append", line);
    }
    ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
    ctx.optimizations.push({
      name: "packed-display-storage-reuse",
      detail: `Displayed screen ${display.name} by adding fields already stored in their decimal positions.`,
    });
    return true;
}

export function compileDisplayByteBuilder(ctx: LoweringCtx, 
    display: ProgramAst["displays"][number],
    line: number,
    _reuseCurrentX: boolean,
  ): boolean {
    const template = ctx.mantissaExponentDisplayTemplate(display);
    if (template === undefined) {
      return compileVariableLeadingMantissaMaskDisplay(ctx, display, line) ||
        compileMantissaMaskDisplay(ctx, display, line, _reuseCurrentX);
    }
    const scratch = ctx.displayTemplateScratchRegisters(display);
    if (scratch === undefined) return false;

    ctx.emitRecall(template.score.name, `display ${display.name} score`, line);
    ctx.emitNumberOrPreload("1000");
    ctx.emitOp(0x13, "/", "display template score shift", line);
    ctx.emitRecall(template.total.name, `display ${display.name} total`, line);
    ctx.emitNumberOrPreload("10000000");
    ctx.emitOp(0x13, "/", "display template total shift", line);
    ctx.emitOp(0x10, "+", "display template total append", line);
    ctx.emitOp(0x09, "9", "display template numeric anchor", line);
    ctx.emitOp(0x10, "+", "display template numeric body", line);
    ctx.emitRecall(scratch.mask, `display ${display.name} separator mask`, line);
    ctx.emitOp(0x38, "К ∨", "display template body merge", line);
    const exponentCanBeZero = ctx.displayFieldCanBeZero(template.exponent);
    ctx.emitStore(scratch.value, `display ${display.name} body`, line);

    const exponentZero = exponentCanBeZero ? ctx.freshLabel("display_exponent_zero") : undefined;
    ctx.emitRecall(template.exponent.name, `display ${display.name} exponent`, line);
    if (exponentZero !== undefined) {
      ctx.emitJump(0x57, "F x!=0", exponentZero, "display template zero exponent", line);
    }
    ctx.emitStore(scratch.loop, `display ${display.name} exponent counter`, line);
    ctx.emitRecall(scratch.value, `display ${display.name} body`, line);
    const loopStart = ctx.freshLabel("display_exponent_loop");
    ctx.emitLabel(loopStart);
    ctx.emitOp(0x0c, "ВП", "display template exponent entry", line);
    ctx.emitOp(0x01, "1", "display template exponent digit", line);
    ctx.emitOp(0x0b, "/-/", "display template exponent sign", line);
    ctx.emitJump(displayLoopOpcode(scratch.loopRegister), `F L${scratch.loopRegister}`, loopStart, "display template exponent loop", line);
    ctx.emitStore(scratch.value, `display ${display.name} exponent body`, line);
    if (exponentZero !== undefined) ctx.emitLabel(exponentZero);

    ctx.emitRecall(template.leader.name, `display ${display.name} leader`, line);
    ctx.emitRecall(scratch.value, `display ${display.name} body`, line);
    ctx.emitOp(0x14, "<->", "display template leader merge", line);
    ctx.emitOp(0x54, "К НОП", "display template leader preserve", line, true);
    ctx.emitOp(0x0c, "ВП", "display template leader restore", line);
    ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
    ctx.optimizations.push({
      name: "display-byte-x2-lowering",
      detail: `Built literal-separated screen ${display.name} through a mantissa/exponent video template.`,
    });
    return true;
}

export function compileMantissaMaskDisplay(ctx: LoweringCtx, 
    display: ProgramAst["displays"][number],
    line: number,
    _reuseCurrentX: boolean,
  ): boolean {
    const template = ctx.mantissaMaskDisplayTemplate(display);
    const scratch = ctx.displayMaskScratchRegister(display);
    const maskRegister = ctx.displayMaskRegister(display);
    if (template === undefined || scratch === undefined || maskRegister === undefined) return false;

    emitDisplayCellsTemplate(ctx, display, template, scratch, maskRegister, line);
    ctx.optimizations.push({
      name: "display-byte-mask-lowering",
      detail: `Built literal-separated screen ${display.name} through a calculator video mask.`,
    });
    return true;
}

function emitDisplayCellsTemplate(ctx: LoweringCtx,
    display: ProgramAst["displays"][number],
    template: NonNullable<ReturnType<LoweringCtx["mantissaMaskDisplayTemplate"]>>,
    scratch: RegisterName,
    maskRegister: RegisterName,
    line: number,
  ): void {
    compilePackedDisplayFields(ctx, display, template.bodyFields, line, false);
    ctx.emitOp(0x60 + registerIndex(maskRegister), `П->X ${maskRegister}`, `display ${display.name} literal mask`, line);
    ctx.emitOp(0x38, "К ∨", "display mask body merge", line);
    ctx.emitOp(0x40 + registerIndex(scratch), `X->П ${scratch}`, `display ${display.name} body`, line, true);
    if (template.leader.kind === "source") {
      ctx.emitRecall(template.leader.field.name, `display ${display.name} leader`, line);
    } else {
      emitDisplayFirstDigit(ctx, template.leader.cell, line, `display ${display.name} leader`);
    }
    emitMantissaMaskLeaderSplice(ctx, display.name, scratch, template.width, line, "display mask");
}

export function compileVariableLeadingMantissaMaskDisplay(ctx: LoweringCtx,
    display: ProgramAst["displays"][number],
    line: number,
  ): boolean {
    const template = ctx.variableLeadingMantissaMaskDisplayTemplate(display);
    const scratch = ctx.displayMaskScratchRegister(display);
    const masks = ctx.variableDisplayMaskRegisters(display);
    if (template === undefined || scratch === undefined || masks === undefined) return false;

    const lowLabel = ctx.freshLabel("display_mask_low");
    const endLabel = ctx.freshLabel("display_mask_end");
    ctx.emitRecall(template.source.name, `display ${display.name} leading field`, line);
    ctx.emitNumberOrPreload(String(template.split));
    ctx.emitOp(0x11, "-", "display mask leading width test", line);
    ctx.emitJump(0x59, "F x>=0", lowLabel, "display mask low branch", line);

    compileVariableLeadingHighBody(ctx, display, template.high.restFields, template.source.name, template.split, scratch, line);
    emitMantissaMaskBodyMerge(ctx, display.name, masks.high, scratch, line);
    emitVariableLeadingHighLeader(ctx, template.source.name, template.split, line);
    emitMantissaMaskLeaderSplice(ctx, display.name, scratch, template.high.width, line, "display mask high");
    ctx.emitJump(0x51, "БП", endLabel, "display mask end", line);

    ctx.emitLabel(lowLabel);
    compilePackedDisplayFields(ctx, display, template.low.bodyFields, line, false);
    emitMantissaMaskBodyMerge(ctx, display.name, masks.low, scratch, line);
    ctx.emitRecall(template.source.name, `display ${display.name} leader`, line);
    emitMantissaMaskLeaderSplice(ctx, display.name, scratch, template.low.width, line, "display mask");
    ctx.emitLabel(endLabel);
    ctx.optimizations.push({
      name: "display-byte-variable-mask-lowering",
      detail: `Built variable-width literal-separated screen ${display.name} through calculator video masks.`,
    });
    return true;
}

function compileVariableLeadingHighBody(ctx: LoweringCtx,
    display: ProgramAst["displays"][number],
    restFields: DisplayField[],
    source: string,
    split: number,
    scratch: RegisterName,
    line: number,
  ): void {
    emitVariableLeadingTailDigit(ctx, source, split, line);
    ctx.emitOp(0x40 + registerIndex(scratch), `X->П ${scratch}`, `display ${display.name} trailing digit`, line, true);

    ctx.emitNumberOrPreload("9");
    const last = ctx.items.at(-1);
    if (last?.kind === "op") last.comment = `display ${display.name} numeric anchor`;

    ctx.emitNumberOrPreload(String(split));
    ctx.emitOp(0x12, "*", "packed display field shift", line);
    ctx.emitOp(0x60 + registerIndex(scratch), `П->X ${scratch}`, `display ${display.name} trailing digit`, line);
    ctx.emitOp(0x10, "+", "packed display field append", line);

    for (const field of restFields) {
      ctx.emitNumberOrPreload(String(10 ** field.width));
      ctx.emitOp(0x12, "*", "packed display field shift", line);
      if (field.kind === "source" || field.value !== "0") {
        emitDisplayFieldValue(ctx, display, field, line);
        ctx.emitOp(0x10, "+", "packed display field append", line);
      }
    }
}

function emitVariableLeadingTailDigit(ctx: LoweringCtx, source: string, split: number, line: number): void {
    ctx.emitRecall(source, "display mask trailing digit", line);
    ctx.emitNumberOrPreload(String(split));
    ctx.emitOp(0x13, "/", "display mask trailing digit quotient", line);
    ctx.emitOp(0x35, "К {x}", "display mask trailing digit fraction", line);
    ctx.emitNumberOrPreload(String(split));
    ctx.emitOp(0x12, "*", "display mask trailing digit", line);
}

function emitVariableLeadingHighLeader(ctx: LoweringCtx, source: string, split: number, line: number): void {
    ctx.emitRecall(source, "display mask high leader", line);
    ctx.emitNumberOrPreload(String(split));
    ctx.emitOp(0x13, "/", "display mask high leader quotient", line);
    ctx.emitOp(0x34, "К [x]", "display mask high leader", line);
}

function emitMantissaMaskBodyMerge(ctx: LoweringCtx,
    displayName: string,
    maskRegister: RegisterName,
    scratch: RegisterName,
    line: number,
  ): void {
    ctx.emitOp(0x60 + registerIndex(maskRegister), `П->X ${maskRegister}`, `display ${displayName} literal mask`, line);
    ctx.emitOp(0x38, "К ∨", "display mask body merge", line);
    ctx.emitOp(0x40 + registerIndex(scratch), `X->П ${scratch}`, `display ${displayName} body`, line, true);
}

function emitMantissaMaskLeaderSplice(ctx: LoweringCtx,
    displayName: string,
    scratch: RegisterName,
    width: number,
    line: number,
    comment: string,
  ): void {
    ctx.emitOp(0x60 + registerIndex(scratch), `П->X ${scratch}`, `display ${displayName} body`, line);
    ctx.emitOp(0x14, "<->", `${comment} leader merge`, line);
    ctx.emitOp(0x54, "К НОП", `${comment} leader preserve`, line, true);
    ctx.emitOp(0x0c, "ВП", `${comment} leader restore`, line);
    emitDisplayExponent(ctx, width - 1, line, `${comment} exponent`);
    ctx.emitOp(0x50, "С/П", `show ${displayName}`, line);
}

export function emitDisplayLiteralProgram(ctx: LoweringCtx, 
    program: Exclude<DisplayLiteralProgram, { kind: "error" }>,
    line: number | undefined,
    comment: string,
  ): void {
    if (program.kind === "kinv") {
      ctx.emitNumberOrPreload(program.digits);
      ctx.emitOp(0x3a, "К ИНВ", comment, line);
      if (program.negative) ctx.emitOp(0x0b, "/-/", `${comment} sign`, line);
      return;
    }
    ctx.emitNumberOrPreload(program.left);
    ctx.emitOp(0x0e, "В↑", `${comment} split`, line);
    ctx.emitNumberOrPreload(program.right);
    ctx.emitOp(0x39, "К ⊕", comment, line);
    if (program.negative) ctx.emitOp(0x0b, "/-/", `${comment} sign`, line);
}

export function emitFirstSpliceDisplayLiteralProgram(ctx: LoweringCtx, 
    program: FirstSpliceDisplayLiteralProgram,
    tempRegister: RegisterName,
    line: number | undefined,
    comment: string,
  ): void {
    emitDisplayLiteralProgram(ctx, program.body, line, `${comment} body`);
    ctx.emitOp(0x40 + registerIndex(tempRegister), `X->П ${tempRegister}`, `${comment} body scratch`, line, true);
    if (program.first === 8) {
      ctx.emitOp(0x60 + registerIndex(tempRegister), `П->X ${tempRegister}`, `${comment} body scratch`, line);
      if (program.negative) ctx.emitOp(0x0b, "/-/", `${comment} sign`, line);
      ctx.emitOp(0x54, "К НОП", `${comment} first digit reuse`, line, true);
      ctx.emitOp(0x0c, "ВП", `${comment} first digit reuse`, line);
      emitDisplayExponent(ctx, program.exponent, line, `${comment} exponent`);
      ctx.optimizations.push({
        name: "display-literal-first-digit-reuse",
        detail: "Reused the literal body's leading 8 while restoring X2.",
      });
      return;
    }
    if (program.first === 10 && program.second === 10) {
      ctx.emitOp(0x35, "К {x}", `${comment} first digit from body`, line);
      ctx.emitOp(0x60 + registerIndex(tempRegister), `П->X ${tempRegister}`, `${comment} body scratch`, line);
      if (program.negative) ctx.emitOp(0x0b, "/-/", `${comment} sign`, line);
      emitFirstDigitSplice(ctx, line);
      emitDisplayExponent(ctx, program.exponent, line, `${comment} exponent`);
      ctx.optimizations.push({
        name: "display-literal-minus-source-reuse",
        detail: "Derived a leading '-' from the literal body's fractional tail.",
      });
      return;
    }
    emitDisplayFirstDigit(ctx, program.first, line, `${comment} first digit`);
    ctx.emitOp(0x60 + registerIndex(tempRegister), `П->X ${tempRegister}`, `${comment} body scratch`, line);
    if (program.negative) ctx.emitOp(0x0b, "/-/", `${comment} sign`, line);
    emitFirstDigitSplice(ctx, line);
    emitDisplayExponent(ctx, program.exponent, line, `${comment} exponent`);
}

export function emitDisplayFirstDigit(ctx: LoweringCtx, cell: number, line: number | undefined, comment: string): void {
    if (cell >= 0 && cell <= 9) {
      ctx.emitNumber(String(cell));
      const last = ctx.items.at(-1);
      if (last?.kind === "op") last.comment = comment;
      return;
    }
    if (cell >= 10 && cell <= 14) {
      ctx.emitNumber(`1${15 - cell}`);
      ctx.emitOp(0x3a, "К ИНВ", comment, line);
      ctx.emitOp(0x35, "К {x}", comment, line);
      return;
    }
    ctx.diagnostics.push(buildDiagnostic("error", `Unsupported display first digit ${cell}.`, line));
}

export function emitDisplayExponent(ctx: LoweringCtx, exponent: number, line: number | undefined, comment: string): void {
    if (!Number.isInteger(exponent) || exponent < 0 || exponent > 99) {
      ctx.diagnostics.push(buildDiagnostic("error", `Unsupported display exponent ${exponent}.`, line));
      return;
    }
    ctx.emitOp(0x0c, "ВП", comment, line);
    for (const char of String(exponent)) {
      ctx.emitOp(Number(char), char, comment, line);
    }
}

export function compileTextDisplay(ctx: LoweringCtx, display: ProgramAst["displays"][number], line: number): boolean {
    const normalized = ctx.collapseTextPrefixDisplay(display);
    if (normalized === undefined) return false;
    const { text, source } = normalized;
    if (
      text !== "BEEr " ||
      source.width !== undefined && source.width !== 2
    ) {
      return false;
    }

    const field = ctx.findStateField(source.name);
    if (field === undefined || (field.min ?? 0) < 0 || (field.max ?? 0) > 99) return false;
    if (ctx.allocation.registers[source.name] !== "0") return false;
    if (ctx.currentAddress() !== 0) return false;

    const scratchRegisters = new Set<RegisterName>(["1", "2", "7", "8", "a"]);
    const conflicting = Object.entries(ctx.allocation.registers)
      .filter(([name, register]) => name !== source.name && scratchRegisters.has(register));
    if (conflicting.length > 0) return false;

    emitTwoDigitTextDisplay(ctx, source.name, line);
    ctx.optimizations.push({
      name: "screen-text-lowering",
      detail: `Lowered screen ${display.name} as visible text ${JSON.stringify(text)} plus ${source.name}.`,
    });
    return true;
}

export function compileLiteralDisplay(ctx: LoweringCtx, display: ProgramAst["displays"][number], line: number): boolean {
    const literal = ctx.collapseLiteralOnlyDisplay(display);
    if (literal === undefined) return false;
    const compiled = compileLiteralDisplayBody(ctx, display, line, literal);
    if (!compiled) return false;
    ctx.optimizations.push({
      name: literal.length === 0 ? "screen-empty-literal-lowering" : "screen-video-literal-lowering",
      detail: literal.length === 0
        ? `Lowered empty screen ${display.name} as a plain pause.`
        : `Lowered screen ${display.name} as a literal calculator video string.`,
    });
    return true;
}

export function compileLiteralDisplayBody(ctx: LoweringCtx, 
    display: ProgramAst["displays"][number],
    line: number,
    literal = ctx.collapseLiteralOnlyDisplay(display),
  ): boolean {
    if (literal === undefined) return false;
    if (literal.length === 0) {
      ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
      return true;
    }
    if (compilePreloadedDisplayLiteral(ctx, display, literal, line)) return true;
    const program = displayLiteralProgram(literal);
    if (program !== undefined) {
      if (program.kind === "error") {
        emitErrorStopOpcode(ctx, `show ${display.name}`, line, true);
        ctx.emitOp(0x54, "К НОП", `show ${display.name} skipped after error pause`, line, true);
        ctx.optimizations.push({
          name: "screen-error-literal-lowering",
          detail: `Lowered screen ${display.name} as a resumable ЕГГ0Г pause with a skipped padding cell.`,
        });
      } else if (program.kind === "kinv") {
        ctx.emitNumberOrPreload(program.digits);
        ctx.emitOp(0x3a, "К ИНВ", "display literal video bytes", line);
      } else {
        ctx.emitNumberOrPreload(program.left);
        ctx.emitOp(0x0e, "В↑", "display literal x/y split", line);
        ctx.emitNumberOrPreload(program.right);
        ctx.emitOp(0x39, "К ⊕", "display literal video bytes", line);
      }
      if (program.kind !== "error" && program.negative) {
        ctx.emitOp(0x0b, "/-/", "display literal sign", line);
      }
      if (program.kind === "error") return true;
      ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
      return true;
    }
    if (compileDecimalLiteralDisplay(ctx, display, literal, line)) return true;
    if (compileLeadingZeroHexProductDisplay(ctx, display, literal, line)) return true;
    if (compileZeroDigitTailDisplay(ctx, display, literal, line)) return true;
    if (compileSignDigitLiteralDisplay(ctx, display, literal, line)) return true;
    const firstSplice =
      signedFirstSpliceDisplayLiteralProgram(literal) ??
      exponentTailDisplayLiteralProgram(literal) ??
      firstSpliceDisplayLiteralProgram(literal);
    if (firstSplice !== undefined) {
      const scratch = ctx.firstSpliceDisplayScratch(display);
      if (scratch !== undefined) {
        emitFirstSpliceDisplayLiteralProgram(ctx, firstSplice, scratch, line, "display literal video bytes");
        ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
        ctx.optimizations.push({
          name: "screen-text-literal-first-splice",
          detail: `Lowered screen ${display.name} by building a literal mantissa and splicing its first digit.`,
        });
        return true;
      }
    }
    return false;
}

export function compilePreloadedDisplayLiteral(ctx: LoweringCtx, 
    display: ProgramAst["displays"][number],
    literal: string,
    line: number,
  ): boolean {
    if (!shouldUsePreloadedDisplayLiteral(literal)) return false;
    const register = ctx.allocation.constants[normalizeConstantLiteral(literal)];
    if (register === undefined) return false;
    ctx.emitOp(0x60 + registerIndex(register), `П->X ${register}`, `display ${display.name} literal`, line);
    ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
    ctx.optimizations.push({
      name: "screen-text-literal-preload",
      detail: `Displayed screen ${display.name} from prebuilt literal R${register}.`,
    });
    return true;
}

export function compileDecimalLiteralDisplay(ctx: LoweringCtx, 
    display: ProgramAst["displays"][number],
    literal: string,
    line: number,
  ): boolean {
    const value = decimalDisplayLiteralNumber(literal);
    if (value === undefined) return false;
    if (Number(value) === 0) ctx.emitZero(`display ${display.name} literal`, line);
    else ctx.emitNumber(value);
    ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
    ctx.optimizations.push({
      name: "screen-decimal-literal-lowering",
      detail: `Lowered screen ${display.name} as an ordinary decimal display literal.`,
    });
    return true;
}

export function compileLeadingZeroHexProductDisplay(ctx: LoweringCtx,
    display: ProgramAst["displays"][number],
    literal: string,
    line: number,
  ): boolean {
    const program = leadingZeroHexProductDisplayProgram(literal);
    if (program === undefined || !machineSupports(ctx.machineProfile, "display-bytes")) return false;
    const register = ctx.allocation.constants[normalizeConstantLiteral(program.sourceLiteral)];
    if (register === undefined) return false;

    ctx.emitOp(0x60 + registerIndex(register), `П->X ${register}`, `display ${display.name} hex zero source`, line);
    ctx.emitOp(0x34, "К [x]", "display leading-zero hex source", line);
    ctx.emitNumberOrPreload(program.factor);
    ctx.emitOp(0x12, "*", "display leading-zero hex product", line);
    ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
    ctx.optimizations.push({
      name: "screen-leading-zero-hex-lowering",
      detail: `Lowered screen ${display.name} through hex mantissa multiplication to preserve the leading zero.`,
    });
    ctx.optimizations.push({
      name: "hex-mantissa-arithmetic",
      detail: `Used ${program.sourceLiteral} * ${program.factor} to render ${JSON.stringify(literal)} with a non-normal leading zero.`,
    });
    return true;
}

export function compileZeroDigitTailDisplay(ctx: LoweringCtx, 
    display: ProgramAst["displays"][number],
    literal: string,
    line: number,
  ): boolean {
    const program = zeroDigitTailDisplayProgram(literal);
    if (program === undefined) return false;
    if (!ctx.scratchRegistersAvailable(new Set<RegisterName>(["9", "c"]))) return false;

    ctx.emitNumber(String(program.input));
    ctx.emitOp(0x54, "К НОП", "display zero-digit tail seed", line, true);
    ctx.emitNumber("50");
    ctx.emitOp(0x15, "F 10^x", "display zero-digit tail monster", line);
    ctx.emitOp(0x22, "F x^2", "display zero-digit tail monster", line);
    ctx.emitOp(0x22, "F x^2", "display zero-digit tail monster", line);
    ctx.emitOp(0x22, "F x^2", "display zero-digit tail monster", line);
    ctx.emitOp(0x12, "*", "display zero-digit tail monster", line);
    ctx.emitOp(0x49, "X->П 9", "display zero-digit tail scratch", line, true);
    ctx.emitOp(0x69, "П->X 9", "display zero-digit tail scratch", line);
    ctx.emitOp(0x6c, "П->X c", "display zero-digit tail hidden tail", line);
    ctx.emitOp(0x0c, "ВП", "display zero-digit tail restore", line);
    ctx.emitOp(0x07, "7", "display zero-digit tail exponent", line);
    ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
    ctx.optimizations.push({
      name: "screen-zero-digit-tail-lowering",
      detail: `Lowered screen ${display.name} through the 0C tail sign-digit display trick.`,
    });
    return true;
}

export function compileSignDigitLiteralDisplay(ctx: LoweringCtx, 
    display: ProgramAst["displays"][number],
    literal: string,
    line: number,
  ): boolean {
    const program = signDigitLiteralDisplayProgram(literal);
    if (program === undefined) return false;
    const scratch = ctx.signDigitLiteralScratch();
    if (scratch === undefined) return false;

    ctx.emitNumber("11");
    ctx.emitOp(0x3a, "К ИНВ", "display sign-digit E source", line);
    ctx.emitOp(0x35, "К {x}", "display sign-digit E source", line);
    ctx.emitOp(0x40 + registerIndex(scratch.source), `X->П ${scratch.source}`, "display sign-digit E source", line, true);

    ctx.emitOp(0x60 + registerIndex(scratch.source), `П->X ${scratch.source}`, "display sign-digit source", line);
    ctx.emitNumber(program.start);
    emitFirstDigitSplice(ctx, line);

    for (let index = 0; index < program.indirectSteps; index += 1) {
      emitSignDigitIndirectStep(ctx, scratch.indirect, line);
      if (index < program.indirectSteps - 1) {
        ctx.emitOp(0x60 + registerIndex(scratch.source), `П->X ${scratch.source}`, "display sign-digit source", line);
        ctx.emitOp(0x60 + registerIndex(scratch.indirect), `П->X ${scratch.indirect}`, "display sign-digit body", line);
        emitFirstDigitSplice(ctx, line);
      }
    }

    if (program.first === "Е") {
      ctx.emitOp(0x60 + registerIndex(scratch.source), `П->X ${scratch.source}`, "display sign-digit final source", line);
    } else {
      ctx.emitNumber(program.first);
    }
    ctx.emitOp(0x60 + registerIndex(scratch.indirect), `П->X ${scratch.indirect}`, "display sign-digit final body", line);
    emitFirstDigitSplice(ctx, line);
    ctx.emitOp(0x50, "С/П", `show ${display.name}`, line);
    ctx.optimizations.push({
      name: "screen-sign-digit-literal-lowering",
      detail: `Lowered screen ${display.name} through indirect sign-digit display construction.`,
    });
    return true;
}

export function emitFirstDigitSplice(ctx: LoweringCtx, line: number | undefined): void {
    ctx.emitOp(0x14, "<->", "display sign-digit first-cell splice", line);
    ctx.emitOp(0x54, "К НОП", "display sign-digit first-cell splice", line, true);
    ctx.emitOp(0x0c, "ВП", "display sign-digit first-cell splice", line);
}

export function emitSignDigitIndirectStep(ctx: LoweringCtx, register: RegisterName, line: number): void {
    ctx.emitOp(0x40 + registerIndex(register), `X->П ${register}`, "display sign-digit indirect scratch", line);
    ctx.emitOp(0xd0 + registerIndex(register), `К П->X ${register}`, "display sign-digit indirect normalize", line);
    ctx.emitOp(0x60 + registerIndex(register), `П->X ${register}`, "display sign-digit indirect body", line);
}

export function emitTwoDigitTextDisplay(ctx: LoweringCtx, source: string, line: number): void {
    ctx.emitRecall(source, "text display verse", line);
    ctx.emitOp(0x01, "1", "text tens divisor", line);
    ctx.emitOp(0x00, "0", "text tens divisor", line);
    ctx.emitOp(0x13, "/", "text tens", line);
    ctx.emitOp(0x34, "К [x]", "text tens integer", line);
    ctx.emitOp(0x41, "X->П 1", "text tens scratch", line);
    ctx.emitOp(0x0f, "F Вx", "text ones from last X", line);
    ctx.emitOp(0x35, "К {x}", "text ones fraction", line);
    ctx.emitOp(0x01, "1", "text ones scale", line);
    ctx.emitOp(0x00, "0", "text ones scale", line);
    ctx.emitOp(0x12, "*", "text ones", line);
    ctx.emitOp(0x42, "X->П 2", "text ones scratch", line);
    ctx.emitOp(0x01, "1", "text display tens prefix", line);
    ctx.emitOp(0x01, "1", "text display tens prefix", line);
    ctx.emitOp(0x48, "X->П 8", "text display prefix scratch", line);
    ctx.emitOp(0x01, "1", "text display tens offset", line);
    ctx.emitOp(0x02, "2", "text display tens offset", line);
    ctx.emitOp(0x47, "X->П 7", "text display digit offset", line);
    ctx.emitOp(0x62, "П->X 2", "text display ones digit", line);
    ctx.emitJump(0x53, "ПП", 34, "text digit renderer", line);
    ctx.emitOp(0x4a, "X->П a", "text display rendered ones", line);
    ctx.emitOp(0x01, "1", "text display ones prefix", line);
    ctx.emitOp(0x04, "4", "text display ones prefix", line);
    ctx.emitOp(0x48, "X->П 8", "text display prefix scratch", line);
    ctx.emitOp(0x01, "1", "text display ones offset", line);
    ctx.emitOp(0x03, "3", "text display ones offset", line);
    ctx.emitOp(0x47, "X->П 7", "text display digit offset", line);
    ctx.emitOp(0x61, "П->X 1", "text display tens digit", line);
    ctx.emitJump(0x53, "ПП", 34, "text digit renderer", line);
    ctx.emitOp(0x6a, "П->X a", "text display rendered ones", line);
    ctx.emitOp(0x0e, "В↑", "show text", line);
    ctx.emitOp(0x50, "С/П", "show text", line);
    ctx.emitOp(0x06, "6", "text digit renderer", line);
    ctx.emitOp(0x11, "-", "text digit renderer", line);
    ctx.emitOp(0x0b, "/-/", "text digit renderer", line);
    ctx.emitJump(0x5c, "F x<0", 45, "text digit renderer", line);
    ctx.emitOp(0x09, "9", "text digit complement", line);
    ctx.emitOp(0x10, "+", "text digit complement", line);
    ctx.emitOp(0xd7, "К П->X 7", "text display byte", line);
    ctx.emitOp(0x10, "+", "text display byte", line);
    ctx.emitOp(0x3a, "К ИНВ", "text visible digit", line);
    ctx.emitOp(0x52, "В/О", "text digit return", line);
    ctx.emitOp(0x01, "1", "text digit complement", line);
    ctx.emitOp(0x10, "+", "text digit complement", line);
    ctx.emitOp(0xd7, "К П->X 7", "text display byte", line);
    ctx.emitOp(0x10, "+", "text display byte", line);
    ctx.emitOp(0x3a, "К ИНВ", "text visible digit", line);
    ctx.emitOp(0xd8, "К П->X 8", "text display prefix", line);
    ctx.emitOp(0x0e, "В↑", "text digit return value", line);
    ctx.emitOp(0x52, "В/О", "text digit return", line);
}

export function orderDisplaySources(ctx: LoweringCtx, sources: string[]): string[] {
    if (ctx.currentXVariable === undefined) return sources;
    const index = sources.indexOf(ctx.currentXVariable);
    if (index <= 0) return sources;
    ctx.optimizations.push({
      name: "display-stack-reuse",
      detail: `Reordered packed display inputs to reuse ${ctx.currentXVariable} already in X.`,
    });
    return [
      ctx.currentXVariable,
      ...sources.slice(0, index),
      ...sources.slice(index + 1),
    ];
}
