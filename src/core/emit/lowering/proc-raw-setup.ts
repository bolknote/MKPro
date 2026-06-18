import { registerIndex } from "../../opcodes.ts";
import type { ExpressionAst, ProgramAst, RegisterName, StatementAst } from "../../types.ts";
import type { LoweringCtx } from "../lowering-ctx.ts";
import {
  compileCondition,
  compileIf,
} from "./control-flow.ts";
import {
  compileRandomCoordListSetup,
} from "./coord-list.ts";
import {
  compileDisplayByteBuilder,
  compileLiteralDisplayBody,
  compilePackedDisplayBody,
  emitDisplayLiteralProgram,
  emitFirstSpliceDisplayLiteralProgram,
} from "./display.ts";
import {
  compileExpression,
  compileStackStopRiskTail,
} from "./expr.ts";
import {
  compileBitMaskWithQuotientScratch,
  emitBitMaskFromCurrentXWithQuotientScratch,
  emitCompactBitMaskFromCurrentXWithQuotientScratch,
  emitSpatialLineCountLoopBody,
  emitSpatialLineProgressionHelperBody,
  emitSpatialSumLoopHelperBody,
} from "./spatial.ts";
import type {
  ExecutableSetupPreload,
  StackUnaryDerivationCall,
  XParamProcLowering,
  XParamStackStopRiskRead,
  XParamValueFunction,
} from "../lowering-helpers.ts";
import {
  COORD_LIST_DX,
  formattedCoordReportFormatForProgram,
  NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE,
  PACKED_COUNTER_PREFIX,
  buildDiagnostic,
  conditionCompileCost,
  conditionToText,
  countExpressionCalls,
  countIdentifierReadsInCondition,
  addExpressions,
  binaryOpcode,
  displayLiteralProgram,
  divideExpressions,
  coordListItemInfo,
  estimateExpressionCost,
  estimateNumberCost,
  estimateOrdinaryIfCost,
  estimateNegativeZeroThresholdFlowCost,
  exponentTailDisplayLiteralProgram,
  expressionCanConsumeIdentifierFromX,
  expressionEquals,
  expressionPureForSubstitution,
  expressionReferencesIdentifier,
  expressionToIntentText,
  firstSpliceDisplayLiteralProgram,
  fracExpression,
  flOpcode,
  formatRawContractDetail,
  intExpression,
  isPredecrementIndirectRegister,
  isPreincrementIndirectRegister,
  isSimpleStackLoad,
  isZeroExpression,
  isUnitDecrementExpression,
  isUnitIncrementExpression,
  matchIntOrFracCall,
  matchNegativeZeroThresholdCondition,
  matchNumericSelfUpdate,
  matchStackUnaryDerivationCall,
  matchXParamValueFunction,
  matchXParamReturnDecay,
  matchXParamStackStopRiskRead,
  multiplyExpressions,
  normalizeConstantLiteral,
  numberExpression,
  numericLiteralValue,
  orderRawInputs,
  parseRawInstruction,
  positiveIntegerPowerOfTenExponent,
  randomCoordListItemPlacement,
  randomCoordListSetupFields,
  selectCheaperEquivalentCondition,
  signedFirstSpliceDisplayLiteralProgram,
  spatialCountMaskScratchName,
  substituteConditionIdentifier,
  X_TRANSFORM_UNARY_OPCODES,
  xParamValueScratchName,
} from "../lowering-helpers.ts";
import {
  getOpcode,
} from "../../opcodes.ts";
import {
  bankMemberKey,
  findStateBankMember,
} from "../../state-banks.ts";
import type {
  ConditionAst,
  ProcAst,
  StateFieldAst,
} from "../../types.ts";

type NumericSetupPreload = ExecutableSetupPreload & { kind: "number" };

interface RepeatedIndexedSetupGroup {
  fields: StateFieldAst[];
  initial: ExpressionAst;
  minRegisterIndex: number;
  maxRegisterIndex: number;
}

type SetupNumericPreloadAction = { extraTargetIndexes?: number[] } & (
    | { kind: "direct"; targetIndex: number; cost: number }
    | {
        kind: "pow10";
        targetIndex: number;
        cost: number;
        exponent: string;
        detail: string;
      }
    | {
        kind: "unary";
        targetIndex: number;
        cost: number;
        sourceIndex: number;
        opcode: number;
        mnemonic: string;
        detail: string;
      }
    | {
        kind: "unary-sequence";
        targetIndex: number;
        cost: number;
        sourceIndex: number;
        ops: Array<{ opcode: number; mnemonic: string; comment: string }>;
        detail: string;
      }
    | {
        kind: "binary";
        targetIndex: number;
        cost: number;
        leftIndex: number;
        rightIndex: number;
        op: "+" | "-" | "*" | "/" | "pow";
        detail: string;
      }
  );

const MAX_EXACT_SETUP_CONSTANT_SYNTHESIS_PRELOADS = 10;

export function compileSetupProgramWithPreloads(ctx: LoweringCtx, 
    preloads: readonly ExecutableSetupPreload[],
    fields: readonly StateFieldAst[],
  ): void {
    const dynamicRegisters = new Set(fields.map((field) => ctx.allocation.registers[field.name]).filter(Boolean));
    const r0StackInitialized = fields.some((field) =>
      field.initialStack !== undefined && ctx.allocation.registers[field.name] === "0"
    );
    const r0NeedsNonNumericRestore = preloads.some((preload) => preload.register === "0" && preload.kind !== "number");
    const repeatedIndexedGroups = r0StackInitialized || r0NeedsNonNumericRestore
      ? []
      : repeatedIndexedSetupGroups(ctx, fields);
    const groupedFieldNames = new Set(repeatedIndexedGroups.flatMap((group) => group.fields.map((field) => field.name)));
    const usesR0SetupPointer = repeatedIndexedGroups.length > 0;
    const activePreloads = preloads.filter((preload) =>
      !dynamicRegisters.has(preload.register) &&
      !(usesR0SetupPointer && preload.register === "0")
    );
    let numericSegment: NumericSetupPreload[] = [];
    const emitNumericSegment = (): void => {
      for (const action of setupNumericPreloadActions(numericSegment)) {
        emitSetupNumericPreloadAction(ctx, numericSegment, action);
        const targets = setupNumericActionTargetIndexes(action);
        const storedRegisters: RegisterName[] = [];
        const seenRegisters = new Set<RegisterName>();
        for (const targetIndex of targets) {
          const preload = numericSegment[targetIndex]!;
          if (seenRegisters.has(preload.register)) continue;
          seenRegisters.add(preload.register);
          storedRegisters.push(preload.register);
          ctx.emitOp(0x40 + registerIndex(preload.register), `X->П ${preload.register}`, `setup R${preload.register}`, undefined, true);
        }
        if (storedRegisters.length > 1) {
          const registers = storedRegisters.map((register) => `R${register}`).join(", ");
          ctx.optimizations.push({
            name: "duplicate-preload-store-reuse",
            detail: `Loaded setup constant ${normalizeConstantLiteral(numericSegment[action.targetIndex]!.value)} once and stored it into ${registers}.`,
          });
        }
        if (storedRegisters.length < targets.length) {
          const register = numericSegment[action.targetIndex]!.register;
          ctx.optimizations.push({
            name: "duplicate-preload-register-elision",
            detail: `Skipped ${targets.length - storedRegisters.length} duplicate setup store(s) to R${register} after the same constant was already stored there.`,
          });
        }
      }
      numericSegment = [];
    };
    for (const preload of activePreloads) {
      if (preload.kind === "number") {
        numericSegment.push(preload as NumericSetupPreload);
        continue;
      }
      emitNumericSegment();
      if (preload.kind === "display-literal") {
        const program = displayLiteralProgram(preload.value);
        const firstSplice =
          signedFirstSpliceDisplayLiteralProgram(preload.value) ??
          exponentTailDisplayLiteralProgram(preload.value) ??
          firstSpliceDisplayLiteralProgram(preload.value);
        if (firstSplice !== undefined) {
          emitFirstSpliceDisplayLiteralProgram(ctx, firstSplice, preload.register, undefined, `setup R${preload.register}`);
        } else if (program !== undefined && program.kind !== "error") {
          emitDisplayLiteralProgram(ctx, program, undefined, `setup R${preload.register}`);
        } else {
          continue;
        }
      } else {
        ctx.emitNumber(preload.value);
      }
      ctx.emitOp(0x40 + registerIndex(preload.register), `X->П ${preload.register}`, `setup R${preload.register}`, undefined, true);
    }
    emitNumericSegment();
    for (const field of fields.filter((candidate) => candidate.initialStack === "Y")) {
      ctx.emitOp(0x14, "X↔Y", `setup ${field.name} from stack.Y`, field.line);
      ctx.emitStore(field.name, `setup ${field.name}`, field.line, true);
      ctx.emitOp(0x14, "X↔Y", `restore stack.X after ${field.name}`, field.line);
    }
    for (const field of fields.filter((candidate) => candidate.initialStack === "X")) {
      ctx.emitStore(field.name, `setup ${field.name}`, field.line, true);
    }
    for (const group of repeatedIndexedGroups) compileRepeatedIndexedSetupGroup(ctx, group);
    if (usesR0SetupPointer) restoreSetupPointerR0(ctx, preloads);

    const initializedCoordLists = new Set<string>();
    for (const field of fields) {
      if (groupedFieldNames.has(field.name)) continue;
      if (field.initial === undefined) continue;
      const coordList = randomCoordListItemPlacement(field.name, field.initial);
      if (coordList !== undefined) {
        if (!initializedCoordLists.has(coordList.listName)) {
          const group = randomCoordListSetupFields(fields, coordList);
          compileRandomCoordListSetup(ctx, group, coordList);
          initializedCoordLists.add(coordList.listName);
        }
        continue;
      }
      compileExpression(ctx, field.initial);
      ctx.emitStore(field.name, `setup ${field.name}`, field.line, true);
    }
    const formattedReportFormat = formattedCoordReportFormatForProgram(ctx.ast);
    if (formattedReportFormat !== undefined) {
      const register = ctx.allocation.registers[COORD_LIST_DX];
      const program = displayLiteralProgram(formattedReportFormat.mask);
      if (register !== undefined && program !== undefined && program.kind !== "error") {
        emitDisplayLiteralProgram(ctx, program, undefined, "setup formatted report mask");
        ctx.emitOp(0x40 + registerIndex(register), `X->П ${register}`, "setup formatted report mask", undefined, true);
      }
    }
    if (fields.some((field) => field.initial !== undefined && randomCoordListItemPlacement(field.name, field.initial) !== undefined)) {
      ctx.emitNumber("7");
    }
    ctx.emitOp(0x50, "С/П", "setup complete");
    compileRuntimeHelpers(ctx);
}

function repeatedIndexedSetupGroups(ctx: LoweringCtx, fields: readonly StateFieldAst[]): RepeatedIndexedSetupGroup[] {
    const groups: RepeatedIndexedSetupGroup[] = [];
    let index = 0;
    while (index < fields.length) {
      const first = fields[index]!;
      if (first.initial === undefined || first.initialStack !== undefined || setupInitialIsLiteralExpression(first.initial)) {
        index += 1;
        continue;
      }
      if (coordListItemInfo(first.name) !== undefined || randomCoordListItemPlacement(first.name, first.initial) !== undefined) {
        index += 1;
        continue;
      }
      const firstRegister = ctx.allocation.registers[first.name];
      if (firstRegister === undefined) {
        index += 1;
        continue;
      }
      const run: StateFieldAst[] = [first];
      let previousRegisterIndex = registerIndex(firstRegister);
      let cursor = index + 1;
      while (cursor < fields.length) {
        const candidate = fields[cursor]!;
        if (candidate.initial === undefined || candidate.initialStack !== undefined) break;
        if (coordListItemInfo(candidate.name) !== undefined) break;
        if (!expressionEquals(candidate.initial, first.initial)) break;
        const candidateRegister = ctx.allocation.registers[candidate.name];
        if (candidateRegister === undefined) break;
        const candidateRegisterIndex = registerIndex(candidateRegister);
        if (candidateRegisterIndex !== previousRegisterIndex + 1) break;
        run.push(candidate);
        previousRegisterIndex = candidateRegisterIndex;
        cursor += 1;
      }
      const minRegisterIndex = registerIndex(firstRegister);
      const maxRegisterIndex = previousRegisterIndex;
      if (run.length >= 3 && minRegisterIndex >= 1 && maxRegisterIndex <= 14) {
        groups.push({
          fields: run,
          initial: first.initial,
          minRegisterIndex,
          maxRegisterIndex,
        });
        index = cursor;
      } else {
        index += 1;
      }
    }
    return groups;
}

function setupInitialIsLiteralExpression(expr: ExpressionAst): boolean {
    if (expr.kind === "number") return true;
    return expr.kind === "unary" && expr.op === "-" && expr.expr.kind === "number";
}

function compileRepeatedIndexedSetupGroup(ctx: LoweringCtx, group: RepeatedIndexedSetupGroup): void {
    const first = group.fields[0]!;
    const last = group.fields.at(-1)!;
    const label = ctx.freshLabel("setup_indexed_bank");
    ctx.emitNumber(String(group.maxRegisterIndex + 1));
    ctx.emitOp(0x40, "X->П 0", `setup indexed pointer ${first.name}..${last.name}`, first.line, true);
    ctx.emitLabel(label);
    compileExpression(ctx, group.initial);
    ctx.emitOp(0xb0, "К X->П 0", `setup indexed ${first.name}..${last.name}`, first.line, true);
    ctx.emitOp(0x60, "П->X 0", "setup indexed pointer", first.line, true);
    ctx.emitNumber(String(group.minRegisterIndex));
    ctx.emitOp(0x11, "-", "setup indexed remaining", first.line, true);
    ctx.emitJump(0x5e, "F x=0", label, `setup indexed loop ${first.name}..${last.name}`, first.line);
    ctx.currentXVariable = undefined;
    ctx.currentXAliases.clear();
    ctx.currentXKnownZero = false;
    ctx.optimizations.push({
      name: "indexed-bank-loop",
      detail: `Initialized ${group.fields.length} indexed bank fields (${first.name}..${last.name}) with one indirect setup loop.`,
    });
}

function restoreSetupPointerR0(ctx: LoweringCtx, preloads: readonly ExecutableSetupPreload[]): void {
    const preload = preloads.find((candidate) => candidate.register === "0" && candidate.kind === "number");
    if (preload === undefined) return;
    ctx.emitNumber(preload.value);
    ctx.emitOp(0x40, "X->П 0", "restore setup R0", undefined, true);
}

function setupNumericPreloadActions(preloads: readonly NumericSetupPreload[]): SetupNumericPreloadAction[] {
    const count = preloads.length;
    if (count === 0) return [];
    if (count > MAX_EXACT_SETUP_CONSTANT_SYNTHESIS_PRELOADS) {
      return directSetupNumericPreloadActions(preloads);
    }
    const normalized = preloads.map((preload) => normalizeConstantLiteral(preload.value));
    const numeric = normalized.map((value) => Number(value));
    const directCosts = preloads.map((preload) => estimateNumberCost(preload.value));
    const fullMask = (1 << count) - 1;
    const best = new Array<number>(fullMask + 1).fill(Number.POSITIVE_INFINITY);
    const previous = new Array<{ mask: number; action: SetupNumericPreloadAction } | undefined>(fullMask + 1);
    best[0] = 0;

    const apply = (mask: number, action: SetupNumericPreloadAction): void => {
      const targets = setupNumericActionTargetIndexes(action);
      if (targets.some((targetIndex) => (mask & (1 << targetIndex)) !== 0)) return;
      let nextMask = mask;
      for (const targetIndex of targets) nextMask |= 1 << targetIndex;
      const nextCost = best[mask]! + action.cost;
      if (nextCost >= best[nextMask]!) return;
      best[nextMask] = nextCost;
      previous[nextMask] = { mask, action };
    };

    for (let mask = 0; mask <= fullMask; mask += 1) {
      if (!Number.isFinite(best[mask]!)) continue;
      for (let targetIndex = 0; targetIndex < count; targetIndex += 1) {
        if ((mask & (1 << targetIndex)) !== 0) continue;
        apply(mask, withDuplicateSetupTargets(
          { kind: "direct", targetIndex, cost: directCosts[targetIndex]! },
          normalized,
          mask,
        ));
        for (const action of setupConstantSynthesisActions(preloads, normalized, numeric, directCosts, mask, targetIndex)) {
          apply(mask, withDuplicateSetupTargets(action, normalized, mask));
        }
      }
    }

    if (!Number.isFinite(best[fullMask]!)) {
      return directSetupNumericPreloadActions(preloads);
    }
    const actions: SetupNumericPreloadAction[] = [];
    for (let mask = fullMask; mask !== 0;) {
      const step = previous[mask];
      if (step === undefined) break;
      actions.push(step.action);
      mask = step.mask;
    }
    return actions.reverse();
}

function directSetupNumericPreloadActions(preloads: readonly NumericSetupPreload[]): SetupNumericPreloadAction[] {
    const normalized = preloads.map((preload) => normalizeConstantLiteral(preload.value));
    const covered = new Set<number>();
    const actions: SetupNumericPreloadAction[] = [];
    for (let targetIndex = 0; targetIndex < preloads.length; targetIndex += 1) {
      if (covered.has(targetIndex)) continue;
      const value = normalized[targetIndex]!;
      const extraTargetIndexes: number[] = [];
      for (let index = targetIndex + 1; index < preloads.length; index += 1) {
        if (!covered.has(index) && normalized[index] === value) extraTargetIndexes.push(index);
      }
      const action: SetupNumericPreloadAction = {
        kind: "direct",
        targetIndex,
        cost: estimateNumberCost(preloads[targetIndex]!.value),
        ...(extraTargetIndexes.length === 0 ? {} : { extraTargetIndexes }),
      };
      for (const index of setupNumericActionTargetIndexes(action)) covered.add(index);
      actions.push(action);
    }
    return actions;
}

function withDuplicateSetupTargets(
  action: SetupNumericPreloadAction,
  normalized: readonly string[],
  loadedMask: number,
): SetupNumericPreloadAction {
    const value = normalized[action.targetIndex]!;
    const extraTargetIndexes: number[] = [];
    for (let index = action.targetIndex + 1; index < normalized.length; index += 1) {
      if ((loadedMask & (1 << index)) !== 0) continue;
      if (normalized[index] === value) extraTargetIndexes.push(index);
    }
    return extraTargetIndexes.length === 0 ? action : { ...action, extraTargetIndexes };
}

function setupNumericActionTargetIndexes(action: SetupNumericPreloadAction): number[] {
    return [action.targetIndex, ...(action.extraTargetIndexes ?? [])];
}

function setupConstantSynthesisActions(
  preloads: readonly NumericSetupPreload[],
  normalized: readonly string[],
  numeric: readonly number[],
  directCosts: readonly number[],
  loadedMask: number,
  targetIndex: number,
): SetupNumericPreloadAction[] {
    const target = numeric[targetIndex]!;
    if (!Number.isFinite(target)) return [];
    const targetValue = normalized[targetIndex]!;
    const directCost = directCosts[targetIndex]!;
    const actions: SetupNumericPreloadAction[] = [];
    const accept = (action: SetupNumericPreloadAction): void => {
      if (action.cost < directCost) actions.push(action);
    };
    const loadedIndexes: number[] = [];
    for (let index = 0; index < preloads.length; index += 1) {
      if ((loadedMask & (1 << index)) !== 0) loadedIndexes.push(index);
    }

    const negated = normalizeConstantLiteral(String(-target));
    const negatedIndex = loadedIndexes.find((index) => normalized[index] === negated);
    if (negatedIndex !== undefined) {
      accept({
        kind: "unary",
        targetIndex,
        cost: 2,
        sourceIndex: negatedIndex,
        opcode: 0x0b,
        mnemonic: "/-/",
        detail: `changed the sign of setup R${preloads[negatedIndex]!.register} (${normalized[negatedIndex]})`,
      });
    }

    if (Number.isSafeInteger(target) && target >= 0) {
      for (const sourceIndex of loadedIndexes) {
        const source = numeric[sourceIndex]!;
        if (!Number.isSafeInteger(source)) continue;
        const squared = source * source;
        if (!Number.isSafeInteger(squared)) continue;
        if (normalizeConstantLiteral(String(squared)) !== targetValue) continue;
        accept({
          kind: "unary",
          targetIndex,
          cost: 2,
          sourceIndex,
          opcode: 0x22,
          mnemonic: "F x^2",
          detail: `squared setup R${preloads[sourceIndex]!.register} (${normalized[sourceIndex]})`,
        });
      }
    }

    for (const sourceIndex of loadedIndexes) {
      const source = numeric[sourceIndex]!;
      if (!Number.isSafeInteger(source)) continue;
      const doubled = source * 2;
      if (Number.isSafeInteger(doubled) && normalizeConstantLiteral(String(doubled)) === targetValue) {
        accept({
          kind: "unary-sequence",
          targetIndex,
          cost: 3,
          sourceIndex,
          ops: [
            { opcode: 0x0e, mnemonic: "В↑", comment: "stack" },
            { opcode: 0x10, mnemonic: "+", comment: "" },
          ],
          detail: `doubled setup R${preloads[sourceIndex]!.register} (${normalized[sourceIndex]})`,
        });
      }
      if (source % 2 === 0) {
        const half = source / 2;
        if (Number.isSafeInteger(half) && normalizeConstantLiteral(String(half)) === targetValue) {
          accept({
            kind: "unary-sequence",
            targetIndex,
            cost: 3,
            sourceIndex,
            ops: [
              { opcode: 0x02, mnemonic: "2", comment: "divisor" },
              { opcode: 0x13, mnemonic: "/", comment: "" },
            ],
            detail: `halved setup R${preloads[sourceIndex]!.register} (${normalized[sourceIndex]})`,
          });
        }
      }
    }

    for (const leftIndex of loadedIndexes) {
      const left = numeric[leftIndex]!;
      if (!Number.isSafeInteger(left)) continue;
      for (const rightIndex of loadedIndexes) {
        const right = numeric[rightIndex]!;
        if (!Number.isSafeInteger(right)) continue;
        const candidates: Array<{ op: "+" | "-" | "*" | "/" | "pow"; value: number }> = [
          { op: "+", value: left + right },
          { op: "-", value: left - right },
          { op: "*", value: left * right },
        ];
        if (right !== 0 && left % right === 0) candidates.push({ op: "/", value: left / right });
        if (left > 0 && right >= 0 && right <= 12) candidates.push({ op: "pow", value: left ** right });
        for (const candidate of candidates) {
          if (!Number.isSafeInteger(candidate.value)) continue;
          if (normalizeConstantLiteral(String(candidate.value)) !== targetValue) continue;
          accept({
            kind: "binary",
            targetIndex,
            cost: 4,
            leftIndex,
            rightIndex,
            op: candidate.op,
            detail: `combined setup R${preloads[leftIndex]!.register} (${normalized[leftIndex]}) and R${preloads[rightIndex]!.register} (${normalized[rightIndex]}) with ${candidate.op === "pow" ? "F x^y" : candidate.op}`,
          });
        }
      }
    }

    const powerOfTenExponent = positiveIntegerPowerOfTenExponent(targetValue);
    if (powerOfTenExponent !== undefined) {
      const exponent = String(powerOfTenExponent);
      accept({
        kind: "pow10",
        targetIndex,
        cost: estimateNumberCost(exponent) + 1,
        exponent,
        detail: `loaded exponent ${exponent} and applied F 10^x`,
      });
    }
    return actions;
}

function emitSetupNumericPreloadAction(
  ctx: LoweringCtx,
  preloads: readonly NumericSetupPreload[],
  action: SetupNumericPreloadAction,
): void {
    const target = preloads[action.targetIndex]!;
    const targetValue = normalizeConstantLiteral(target.value);
    if (action.kind === "direct") {
      ctx.emitNumber(target.value);
      return;
    }
    if (action.kind === "pow10") {
      ctx.emitNumber(action.exponent);
      ctx.emitOp(0x15, "F 10^x", `setup constant ${targetValue}`);
    } else if (action.kind === "unary") {
      const source = preloads[action.sourceIndex]!;
      ctx.emitOp(0x60 + registerIndex(source.register), `П->X ${source.register}`, `setup constant ${targetValue} base ${normalizeConstantLiteral(source.value)}`);
      ctx.emitOp(action.opcode, action.mnemonic, `setup constant ${targetValue}`);
    } else if (action.kind === "unary-sequence") {
      const source = preloads[action.sourceIndex]!;
      ctx.emitOp(0x60 + registerIndex(source.register), `П->X ${source.register}`, `setup constant ${targetValue} base ${normalizeConstantLiteral(source.value)}`);
      for (const op of action.ops) {
        const comment = op.comment === "" ? `setup constant ${targetValue}` : `setup constant ${targetValue} ${op.comment}`;
        ctx.emitOp(op.opcode, op.mnemonic, comment);
      }
    } else {
      const left = preloads[action.leftIndex]!;
      const right = preloads[action.rightIndex]!;
      ctx.emitOp(0x60 + registerIndex(left.register), `П->X ${left.register}`, `setup constant ${targetValue} left ${normalizeConstantLiteral(left.value)}`);
      ctx.emitOp(0x0e, "В↑", `setup constant ${targetValue} stack`);
      ctx.emitOp(0x60 + registerIndex(right.register), `П->X ${right.register}`, `setup constant ${targetValue} right ${normalizeConstantLiteral(right.value)}`);
      if (action.op === "pow") ctx.emitOp(0x24, "F x^y", `setup constant ${targetValue}`);
      else ctx.emitOp(binaryOpcode(action.op), action.op, `setup constant ${targetValue}`);
    }
    ctx.optimizations.push({
      name: "constant-synthesis",
      detail: `Built setup constant ${targetValue} by ${action.detail} (${action.cost} cells instead of direct ${estimateNumberCost(target.value)}).`,
    });
}

export function compileProcedures(ctx: LoweringCtx): void {
    const order = procEmissionOrder(ctx);
    const packedLineFamilyConsumedUpdateProcs = (
      ctx.loweringOptions.packedLineFamilyUpdateCheckTail === true ||
      ctx.loweringOptions.packedLineFamilyMutatingSelectorUpdateCheckTail === true ||
      ctx.loweringOptions.packedLineFamilyBorrowedMutatingSelectorUpdateCheckTail === true
    )
      ? new Set(
        order
          .map((proc) => {
            if (ctx.loweringOptions.packedLineFamilyBorrowedMutatingSelectorUpdateCheckTail === true) {
              return packedLineFamilyBorrowedMutatingSelectorUpdateCheckMarkerProc(ctx, proc)?.match.updateProc;
            }
            if (ctx.loweringOptions.packedLineFamilyMutatingSelectorUpdateCheckTail === true) {
              return packedLineFamilyMutatingSelectorUpdateCheckMarkerProc(ctx, proc)?.updateProc;
            }
            return packedLineFamilyUpdateCheckMarkerProc(ctx, proc)?.updateProc;
          })
          .filter((name): name is string => name !== undefined),
      )
      : new Set<string>();
    for (const proc of order) {
      if (ctx.inlineProcNames.has(proc.name)) continue;
      if (packedLineFamilyConsumedUpdateProcs.has(proc.name)) continue;
      const skipSingleUseXParamStackStopRisk =
        matchXParamStackStopRiskRead(ctx.ast, proc) !== undefined &&
        countExpressionCalls(ctx.ast, proc.name) === 1;
      if (skipSingleUseXParamStackStopRisk) continue;
      ctx.emitProcedureLabel(proc.name);
      ctx.compileWithinProcedure(proc, () => {
        let emittedExplicitReturn = false;
        const xParam = ctx.xParamProcs.get(proc.name);
        const xParamValue = ctx.loweringOptions.xParamValueFunctions === true
          ? matchXParamValueFunction(proc)
          : undefined;
        const xParamDecay = matchXParamReturnDecay(proc);
        const xParamStackStopRisk = matchXParamStackStopRiskRead(ctx.ast, proc);
        if (xParamValue !== undefined && xParamValueScratchName(ctx.ast) !== undefined) {
          compileXParamValueFunctionBody(ctx, proc, xParamValue, xParamValueScratchName(ctx.ast)!);
        } else if (xParamDecay !== undefined) {
          compileXParamReturnDecayBody(ctx, xParamDecay);
          ctx.emitOp(0x52, "В/О", "x-param decay return", xParamDecay.line);
          ctx.optimizations.push({
            name: "x-param-return-decay",
            detail: `Compiled ${proc.name} to consume ${xParamDecay.param} directly from X.`,
          });
        } else if (xParamStackStopRisk !== undefined) {
          compileXParamStackStopRiskReadBody(ctx, xParamStackStopRisk);
          ctx.emitOp(0x52, "В/О", "x-param stack-stop-risk return", xParamStackStopRisk.line);
          ctx.optimizations.push({
            name: "x-param-stack-stop-risk-read",
            detail: `Compiled ${proc.name} to consume ${xParamStackStopRisk.param} directly from X.`,
          });
          ctx.optimizations.push({
            name: "show-read-stack-stop-risk-lowering",
            detail: `Reused displayed ${xParamStackStopRisk.param} as the parked Y value for ${proc.name}.`,
          });
        } else if (
          ctx.loweringOptions.packedLineFamilyBorrowedMutatingSelectorUpdateCheckTail === true &&
          compilePackedLineFamilyBorrowedMutatingSelectorUpdateCheckMarkerProc(ctx, proc)
        ) {
          emittedExplicitReturn = true;
        } else if (
          ctx.loweringOptions.packedLineFamilyMutatingSelectorUpdateCheckTail === true &&
          compilePackedLineFamilyMutatingSelectorUpdateCheckMarkerProc(ctx, proc)
        ) {
          emittedExplicitReturn = true;
        } else if (
          ctx.loweringOptions.packedLineFamilyUpdateCheckTail === true &&
          compilePackedLineFamilyUpdateCheckMarkerProc(ctx, proc)
        ) {
          emittedExplicitReturn = true;
        } else if (compileXParamIndexedFractionalReportTailProc(ctx, proc)) {
          emittedExplicitReturn = true;
        } else if (compilePackedLineFamilyScoreProc(ctx, proc)) {
          // The custom body emits its own shared-tail В/О because one path calls
          // into the middle of the tail and another path falls through to it.
          emittedExplicitReturn = true;
        } else if (xParam !== undefined) {
          compileXParamProcBody(ctx, proc, xParam);
        } else {
          ctx.compileStatements(proc.body);
        }
        if (!ctx.statementsTerminate(proc.body) && !emittedExplicitReturn) {
          ctx.emitOp(0x52, "В/О", "implicit return from proc");
        }
      });
      ctx.emitProcedureEndLabel(proc.name);
    }
}

interface XParamIndexedFractionalReportTailProc {
  readonly param: string;
  readonly selector: string;
  readonly selectorRegister: RegisterName;
  readonly yName: string;
  readonly update: Extract<StatementAst, { kind: "indexed_assign" }>;
  readonly factor: ExpressionAst;
  readonly op: "+" | "-";
  readonly mask: ExpressionAst;
  readonly branch: Extract<StatementAst, { kind: "if" }>;
  readonly haltLine: number | undefined;
}

interface PackedLineFamilyUpdateCheckStep {
  readonly lineExpr: ExpressionAst;
  readonly slot: number;
  readonly sourceLine: number | undefined;
  readonly normalizer?: string;
}

interface PackedLineFamilyUpdateCheckMarkerProc {
  readonly updateProc: string;
  readonly selector: string;
  readonly selectorRegister: RegisterName;
  readonly yName: string;
  readonly deltaName: string;
  readonly update: Extract<StatementAst, { kind: "indexed_assign" }>;
  readonly factor: ExpressionAst;
  readonly op: "+" | "-";
  readonly mask: ExpressionAst;
  readonly branch: Extract<StatementAst, { kind: "if" }>;
  readonly haltLine: number | undefined;
  readonly first: Extract<StatementAst, { kind: "assign" }>;
  readonly steps: readonly PackedLineFamilyUpdateCheckStep[];
}

interface PackedLineFamilyUpdateCheckMatchOptions {
  readonly allowMutatingSelector?: boolean;
}

interface PackedLineFamilyBorrowedMutatingSelectorUpdateCheckMarkerProc {
  readonly match: PackedLineFamilyUpdateCheckMarkerProc;
  readonly selectorRegister: RegisterName;
  readonly borrowedOwner?: string;
}

function compilePackedLineFamilyBorrowedMutatingSelectorUpdateCheckMarkerProc(ctx: LoweringCtx, proc: ProcAst): boolean {
    const borrowed = packedLineFamilyBorrowedMutatingSelectorUpdateCheckMarkerProc(ctx, proc);
    if (borrowed === undefined) return false;
    const { match } = borrowed;

    const firstSlot = match.steps[0]?.slot;
    if (firstSlot === undefined) return false;
    ctx.emitStore(match.deltaName, `set ${match.deltaName} from X parameter`, match.first.line);
    ctx.emitNumberOrPreload(String(firstSlot + 1));
    emitBorrowedSelectorStore(ctx, borrowed, `packed-line borrowed mutating selector start ${firstSlot + 1}`, match.first.line);

    const tailLabel = ctx.freshLabel("packed_line_family_borrowed_mutating_update_check_tail");
    for (let index = 0; index < match.steps.length; index += 1) {
      const step = match.steps[index]!;
      compilePackedLineFamilyMutatingSelectorUpdateCheckStep(ctx, match, step);
      if (index === match.steps.length - 1) {
        ctx.emitLabel(tailLabel);
      } else {
        ctx.emitJump(0x53, "ПП", tailLabel, "packed-line borrowed mutating shared update/check tail", step.sourceLine);
        ctx.currentYVariable = undefined;
      }
    }

    emitPackedLineFamilyMutatingSelectorUpdateCheckTail(ctx, {
      ...match,
      selectorRegister: borrowed.selectorRegister,
    });
    ctx.optimizations.push({
      name: "packed-line-family-borrowed-mutating-selector-update-check-tail",
      detail: `Borrowed R${borrowed.selectorRegister}${borrowed.borrowedOwner === undefined ? "" : ` (${borrowed.borrowedOwner})`} as a descending mutating selector for ${proc.name} slots ${match.steps.map((step) => step.slot).join(",")}.`,
    });
    return true;
}

function packedLineFamilyBorrowedMutatingSelectorUpdateCheckMarkerProc(
  ctx: LoweringCtx,
  proc: ProcAst,
): PackedLineFamilyBorrowedMutatingSelectorUpdateCheckMarkerProc | undefined {
    const match = packedLineFamilyUpdateCheckMarkerProc(ctx, proc);
    if (match === undefined) return undefined;
    if (!packedLineFamilyUpdateCheckStepsDescendByOne(match.steps)) return undefined;
    if (!match.steps.every((step) => expressionPreservesPreviousXAsYForPackedLine(step.lineExpr))) return undefined;
    if (!match.steps.every((step) => step.normalizer === undefined || xParamProcPreservesCallerY(ctx, step.normalizer))) {
      return undefined;
    }
    const borrowed = borrowedPredecrementSelectorRegister(ctx, proc, match);
    if (borrowed === undefined) return undefined;
    return { match, ...borrowed };
}

function emitBorrowedSelectorStore(
  ctx: LoweringCtx,
  borrowed: PackedLineFamilyBorrowedMutatingSelectorUpdateCheckMarkerProc,
  comment: string,
  line: number | undefined,
): void {
    if (borrowed.borrowedOwner !== undefined) {
      ctx.emitStore(borrowed.borrowedOwner, comment, line);
      return;
    }
    ctx.emitOp(0x40 + registerIndex(borrowed.selectorRegister), `X->П ${borrowed.selectorRegister}`, comment, line);
    ctx.currentXVariable = undefined;
    ctx.currentXAliases.clear();
}

function compilePackedLineFamilyMutatingSelectorUpdateCheckMarkerProc(ctx: LoweringCtx, proc: ProcAst): boolean {
    const match = packedLineFamilyMutatingSelectorUpdateCheckMarkerProc(ctx, proc);
    if (match === undefined) return false;

    const firstSlot = match.steps[0]?.slot;
    if (firstSlot === undefined) return false;
    ctx.emitStore(match.deltaName, `set ${match.deltaName} from X parameter`, match.first.line);
    ctx.emitNumberOrPreload(String(firstSlot + 1));
    ctx.emitStore(match.selector, `packed-line mutating selector start ${firstSlot + 1}`, match.first.line);

    const tailLabel = ctx.freshLabel("packed_line_family_mutating_update_check_tail");
    for (let index = 0; index < match.steps.length; index += 1) {
      const step = match.steps[index]!;
      compilePackedLineFamilyMutatingSelectorUpdateCheckStep(ctx, match, step);
      if (index === match.steps.length - 1) {
        ctx.emitLabel(tailLabel);
      } else {
        ctx.emitJump(0x53, "ПП", tailLabel, "packed-line mutating shared update/check tail", step.sourceLine);
        ctx.currentYVariable = undefined;
      }
    }

    emitPackedLineFamilyMutatingSelectorUpdateCheckTail(ctx, match);
    ctx.optimizations.push({
      name: "packed-line-family-mutating-selector-update-check-tail",
      detail: `Lowered ${proc.name} and ${match.updateProc} as a descending R${match.selectorRegister} mutating-selector packed-line update/check tail for slots ${match.steps.map((step) => step.slot).join(",")}.`,
    });
    return true;
}

function packedLineFamilyMutatingSelectorUpdateCheckMarkerProc(
  ctx: LoweringCtx,
  proc: ProcAst,
): PackedLineFamilyUpdateCheckMarkerProc | undefined {
    const match = packedLineFamilyUpdateCheckMarkerProc(ctx, proc, { allowMutatingSelector: true });
    if (match === undefined) return undefined;
    if (!isPredecrementIndirectRegister(match.selectorRegister)) return undefined;
    if (!packedLineFamilyUpdateCheckStepsDescendByOne(match.steps)) return undefined;
    if (!match.steps.every((step) => expressionPreservesPreviousXAsYForPackedLine(step.lineExpr))) return undefined;
    if (!match.steps.every((step) => step.normalizer === undefined || xParamProcPreservesCallerY(ctx, step.normalizer))) {
      return undefined;
    }
    return match;
}

function compilePackedLineFamilyUpdateCheckMarkerProc(ctx: LoweringCtx, proc: ProcAst): boolean {
    const match = packedLineFamilyUpdateCheckMarkerProc(ctx, proc);
    if (match === undefined) return false;

    ctx.emitStore(match.deltaName, `set ${match.deltaName} from X parameter`, match.first.line);
    const tailLabel = ctx.freshLabel("packed_line_family_update_check_tail");
    for (let index = 0; index < match.steps.length; index += 1) {
      const step = match.steps[index]!;
      compilePackedLineFamilyUpdateCheckStep(ctx, match, step);
      if (index === match.steps.length - 1) {
        ctx.emitLabel(tailLabel);
      } else {
        ctx.emitJump(0x53, "ПП", tailLabel, "packed-line shared update/check tail", step.sourceLine);
        ctx.currentYVariable = undefined;
      }
    }

    emitPackedLineFamilyUpdateCheckTail(ctx, match);
    ctx.optimizations.push({
      name: "packed-line-family-update-check-tail",
      detail: `Lowered ${proc.name} and ${match.updateProc} as a local shared packed-line update/check tail for slots ${match.steps.map((step) => step.slot).join(",")}.`,
    });
    return true;
}

function compilePackedLineFamilyMutatingSelectorUpdateCheckStep(
  ctx: LoweringCtx,
  match: PackedLineFamilyUpdateCheckMarkerProc,
  step: PackedLineFamilyUpdateCheckStep,
): void {
    compileExpression(ctx, packedLineFamilyUpdateStepLineValue(match, step));
    compileExpressionPreservingPreviousXAsY(ctx, step.lineExpr, step.sourceLine);
    if (step.normalizer !== undefined) {
      compileBlockCall(ctx, step.normalizer, step.sourceLine ?? match.update.line);
    }
}

function compilePackedLineFamilyUpdateCheckStep(
  ctx: LoweringCtx,
  match: PackedLineFamilyUpdateCheckMarkerProc,
  step: PackedLineFamilyUpdateCheckStep,
): void {
    compileExpression(ctx, step.lineExpr);
    if (step.normalizer !== undefined) {
      compileBlockCall(ctx, step.normalizer, step.sourceLine ?? match.update.line);
    }
    ctx.markCurrentX(match.yName);
    ctx.emitNumberOrPreload(String(step.slot));
    ctx.currentYVariable = match.yName;
}

function emitPackedLineFamilyUpdateCheckTail(
  ctx: LoweringCtx,
  match: Pick<
    PackedLineFamilyUpdateCheckMarkerProc,
    "selector" | "selectorRegister" | "update" | "factor" | "op" | "mask" | "branch" | "haltLine"
  >,
): void {
    ctx.emitStore(match.selector, `set ${match.selector} from X parameter`, match.update.line);
    ctx.emitOp(0x14, "X↔Y", "stack-carried packed digit index", match.update.line);
    ctx.emitOp(0x15, "F 10ˣ", "stack-carried packed digit pow10", match.update.line);
    compileExpression(ctx, match.factor);
    ctx.emitOp(0x12, "×", "stack-carried packed digit delta", match.update.line);
    ctx.emitOp(
      0xd0 + registerIndex(match.selectorRegister),
      `К П->X ${match.selectorRegister}`,
      "indexed packed digit update base",
      match.update.line,
    );
    ctx.emitOp(binaryOpcode(match.op), match.op, "indexed packed digit update", match.update.line);
    ctx.emitPreparedIndexedStore(match.update.target, match.selectorRegister, match.update.line);

    compileExpression(ctx, match.mask);
    ctx.emitOp(0x37, "К ∧", "updated packed fractional report mask", match.branch.line);
    ctx.emitOp(0x35, "К {x}", "updated packed fractional report predicate", match.branch.line);

    const haltLabel = ctx.freshLabel("packed_frac_report_x2_halt");
    ctx.emitJump(0x5e, "F x=0", haltLabel, "true branch for packed fractional report !=", match.branch.line);
    ctx.emitOp(0x52, "В/О", "packed fractional report return", match.branch.line);
    ctx.emitLabel(haltLabel);
    ctx.emitOp(0x0a, ".", "updated packed fractional report X2 restore", match.branch.line);
    ctx.emitOp(0x50, "С/П", "halt", match.haltLine);
    ctx.currentYVariable = undefined;
}

function emitPackedLineFamilyMutatingSelectorUpdateCheckTail(
  ctx: LoweringCtx,
  match: Pick<
    PackedLineFamilyUpdateCheckMarkerProc,
    "selectorRegister" | "update" | "factor" | "op" | "mask" | "branch" | "haltLine"
  >,
): void {
    ctx.emitOp(0x15, "F 10ˣ", "stack-carried packed digit pow10", match.update.line);
    compileExpression(ctx, match.factor);
    ctx.emitOp(0x12, "×", "stack-carried packed digit delta", match.update.line);
    ctx.emitOp(binaryOpcode(match.op), match.op, "mutating-selector packed digit update", match.update.line);
    ctx.emitOp(
      0xb0 + registerIndex(match.selectorRegister),
      `К X->П ${match.selectorRegister}`,
      `predecrement indexed set ${bankMemberKey(match.update.target.base, match.update.target.field)}`,
      match.update.line,
    );

    compileExpression(ctx, match.mask);
    ctx.emitOp(0x37, "К ∧", "updated packed fractional report mask", match.branch.line);
    ctx.emitOp(0x35, "К {x}", "updated packed fractional report predicate", match.branch.line);

    const haltLabel = ctx.freshLabel("packed_frac_report_x2_halt");
    ctx.emitJump(0x5e, "F x=0", haltLabel, "true branch for packed fractional report !=", match.branch.line);
    ctx.emitOp(0x52, "В/О", "packed fractional report return", match.branch.line);
    ctx.emitLabel(haltLabel);
    ctx.emitOp(0x0a, ".", "updated packed fractional report X2 restore", match.branch.line);
    ctx.emitOp(0x50, "С/П", "halt", match.haltLine);
    ctx.currentYVariable = undefined;
}

function packedLineFamilyUpdateCheckMarkerProc(
  ctx: LoweringCtx,
  proc: ProcAst,
  options: PackedLineFamilyUpdateCheckMatchOptions = {},
): PackedLineFamilyUpdateCheckMarkerProc | undefined {
    const first = proc.body[0];
    if (first?.kind !== "assign" || first.expr.kind !== "identifier") return undefined;

    const candidates = ctx.ast.procs.flatMap((candidate) => {
      const update = xParamIndexedFractionalReportTailProc(ctx, candidate, {
        allowMutatingSelector: options.allowMutatingSelector === true,
      });
      if (update === undefined || update.factor.kind !== "identifier") return [];
      if (first.target !== update.factor.name) return [];
      return [{ proc: candidate, update }];
    });
    for (const candidate of candidates) {
      const steps = packedLineFamilyUpdateCheckMarkerSteps(ctx, proc.body.slice(1), candidate.proc.name, candidate.update);
      if (steps === undefined) continue;
      return {
        updateProc: candidate.proc.name,
        selector: candidate.update.selector,
        selectorRegister: candidate.update.selectorRegister,
        yName: candidate.update.yName,
        deltaName: first.target,
        update: candidate.update.update,
        factor: candidate.update.factor,
        op: candidate.update.op,
        mask: candidate.update.mask,
        branch: candidate.update.branch,
        haltLine: candidate.update.haltLine,
        first,
        steps,
      };
    }
    return undefined;
}

function packedLineFamilyUpdateCheckStepsDescendByOne(
  steps: readonly PackedLineFamilyUpdateCheckStep[],
): boolean {
    if (steps.length < 2) return false;
    for (let index = 1; index < steps.length; index += 1) {
      if (steps[index - 1]!.slot - steps[index]!.slot !== 1) return false;
    }
    return true;
}

function packedLineFamilyUpdateStepLineValue(
  match: PackedLineFamilyUpdateCheckMarkerProc,
  step: PackedLineFamilyUpdateCheckStep,
): ExpressionAst {
    return {
      ...match.update.target,
      index: numberExpression(step.slot),
    };
}

function xParamProcPreservesCallerY(ctx: LoweringCtx, procName: string): boolean {
    const lowering = ctx.xParamProcs.get(procName);
    if (lowering === undefined) return false;
    if (lowering.kind === "copy" || lowering.kind === "add") return true;
    if (lowering.kind !== "expr") return false;
    return expressionPreservesPreviousXAsYForPackedLine(lowering.first.expr);
}

function expressionPreservesPreviousXAsYForPackedLine(expr: ExpressionAst): boolean {
    if (isSimpleStackLoad(expr)) return true;
    switch (expr.kind) {
      case "unary":
        return expr.op === "-" && expressionPreservesPreviousXAsYForPackedLine(expr.expr);
      case "binary":
        return (expr.op === "+" || expr.op === "-" || expr.op === "*" || expr.op === "/") &&
          expressionPreservesPreviousXAsYForPackedLine(expr.left) &&
          expressionPreservesPreviousXAsYForPackedLine(expr.right);
      case "call":
        return expr.args.length === 1 &&
          X_TRANSFORM_UNARY_OPCODES[expr.callee.toLowerCase()] !== undefined &&
          expressionPreservesPreviousXAsYForPackedLine(expr.args[0]!);
      case "number":
      case "identifier":
      case "indexed":
      case "string":
        return false;
    }
}

function compileExpressionPreservingPreviousXAsY(
  ctx: LoweringCtx,
  expr: ExpressionAst,
  line?: number,
): void {
    if (isSimpleStackLoad(expr)) {
      compileExpression(ctx, expr);
      return;
    }
    switch (expr.kind) {
      case "unary":
        compileExpressionPreservingPreviousXAsY(ctx, expr.expr, line);
        ctx.emitOp(0x0b, "/-/", "stack-preserving unary minus", line);
        return;
      case "binary":
        compileExpressionPreservingPreviousXAsY(ctx, expr.left, line);
        compileExpressionPreservingPreviousXAsY(ctx, expr.right, line);
        ctx.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`, line);
        return;
      case "call": {
        compileExpressionPreservingPreviousXAsY(ctx, expr.args[0]!, line);
        const opcode = X_TRANSFORM_UNARY_OPCODES[expr.callee.toLowerCase()];
        if (opcode === undefined) {
          compileExpression(ctx, expr);
          return;
        }
        ctx.emitOp(opcode[0], opcode[1], `${expr.callee}()`, line);
        return;
      }
      case "number":
      case "identifier":
      case "indexed":
      case "string":
        compileExpression(ctx, expr);
        return;
    }
}

function borrowedPredecrementSelectorRegister(
  ctx: LoweringCtx,
  proc: ProcAst,
  match: PackedLineFamilyUpdateCheckMarkerProc,
): { selectorRegister: RegisterName; borrowedOwner?: string } | undefined {
    const ownerByRegister = new Map<RegisterName, string>();
    for (const [name, register] of Object.entries(ctx.allocation.registers)) {
      ownerByRegister.set(register, name);
    }
    const constantRegisters = new Set(Object.values(ctx.allocation.constants));
    const preferred: RegisterName[] = ["3", "0", "1", "2"];
    for (const selectorRegister of preferred) {
      if (!isPredecrementIndirectRegister(selectorRegister)) continue;
      if (constantRegisters.has(selectorRegister)) continue;
      if (ctx.allocation.negativeZeroDegree === selectorRegister) continue;
      const borrowedOwner = ownerByRegister.get(selectorRegister);
      if (borrowedOwner !== undefined) {
        if (packedLineFamilyClusterReadsIdentifier(ctx, proc, match, borrowedOwner)) continue;
        if (!allCallsToProcCanClobberIdentifier(ctx.ast, proc.name, borrowedOwner)) continue;
      }
      return borrowedOwner === undefined ? { selectorRegister } : { selectorRegister, borrowedOwner };
    }
    return undefined;
}

function packedLineFamilyClusterReadsIdentifier(
  ctx: LoweringCtx,
  proc: ProcAst,
  match: PackedLineFamilyUpdateCheckMarkerProc,
  name: string,
): boolean {
    if (statementsReadIdentifierForBorrow(ctx.ast, proc.body, name, new Set())) return true;
    const updateProc = ctx.ast.procs.find((candidate) => candidate.name === match.updateProc);
    if (updateProc !== undefined && statementsReadIdentifierForBorrow(ctx.ast, updateProc.body, name, new Set())) {
      return true;
    }
    for (const normalizer of new Set(match.steps.map((step) => step.normalizer).filter((item): item is string => item !== undefined))) {
      const normalizerProc = ctx.ast.procs.find((candidate) => candidate.name === normalizer);
      if (normalizerProc !== undefined && statementsReadIdentifierForBorrow(ctx.ast, normalizerProc.body, name, new Set())) {
        return true;
      }
    }
    return false;
}

function allCallsToProcCanClobberIdentifier(ast: ProgramAst, procName: string, name: string): boolean {
    let found = false;
    let safe = true;
    const visitStatements = (statements: readonly StatementAst[], continuation: readonly StatementAst[]): void => {
      if (!safe) return;
      for (let index = 0; index < statements.length; index += 1) {
        const statement = statements[index]!;
        const rest = [...statements.slice(index + 1), ...continuation];
        if (statement.kind === "call" && statement.block === procName) {
          found = true;
          if (firstIdentifierAccessForBorrow(ast, rest, name, new Set()) === "read") {
            safe = false;
            return;
          }
        }
        switch (statement.kind) {
          case "loop":
          case "while":
            visitStatements(statement.body, [...statement.body, ...rest]);
            break;
          case "if":
            visitStatements(statement.thenBody, rest);
            if (statement.elseBody !== undefined) visitStatements(statement.elseBody, rest);
            break;
          case "dispatch":
            for (const dispatchCase of statement.cases) visitStatements(dispatchCase.body, rest);
            if (statement.defaultBody !== undefined) visitStatements(statement.defaultBody, rest);
            break;
          case "pause":
          case "preview":
          case "halt":
          case "return_value":
          case "assign":
          case "indexed_assign":
          case "coord_list_remove":
          case "core":
          case "show":
          case "input":
          case "call":
          case "decimal_series":
            break;
        }
      }
    };
    for (const entry of ast.entries) visitStatements(entry.body, []);
    for (const candidate of ast.procs) {
      if (candidate.name !== procName) visitStatements(candidate.body, []);
    }
    return found && safe;
}

type BorrowIdentifierAccess = "read" | "write" | "none";

function firstIdentifierAccessForBorrow(
  ast: ProgramAst,
  statements: readonly StatementAst[],
  name: string,
  seenProcs: Set<string>,
): BorrowIdentifierAccess {
    for (const statement of statements) {
      const access = statementFirstIdentifierAccessForBorrow(ast, statement, name, seenProcs);
      if (access !== "none") return access;
    }
    return "none";
}

function statementFirstIdentifierAccessForBorrow(
  ast: ProgramAst,
  statement: StatementAst,
  name: string,
  seenProcs: Set<string>,
): BorrowIdentifierAccess {
    if (statement.kind === "call") {
      const proc = ast.procs.find((candidate) => candidate.name === statement.block);
      if (proc === undefined) return "none";
      const key = `${name}\0${proc.name}`;
      if (seenProcs.has(key)) return "none";
      seenProcs.add(key);
      const access = firstIdentifierAccessForBorrow(ast, proc.body, name, seenProcs);
      seenProcs.delete(key);
      return access;
    }
    if (statement.kind === "if") {
      if (
        expressionReferencesIdentifier(statement.condition.left, name) ||
        expressionReferencesIdentifier(statement.condition.right, name)
      ) return "read";
      const thenAccess = firstIdentifierAccessForBorrow(ast, statement.thenBody, name, seenProcs);
      const elseAccess = statement.elseBody === undefined
        ? "none"
        : firstIdentifierAccessForBorrow(ast, statement.elseBody, name, seenProcs);
      if (thenAccess === "read" || elseAccess === "read") return "read";
      if (thenAccess === "write" || elseAccess === "write") return "write";
      return "none";
    }
    if (statement.kind === "loop" || statement.kind === "while") {
      if (
        statement.kind === "while" &&
        (
          expressionReferencesIdentifier(statement.condition.left, name) ||
          expressionReferencesIdentifier(statement.condition.right, name)
        )
      ) return "read";
      const bodyAccess = firstIdentifierAccessForBorrow(ast, statement.body, name, seenProcs);
      return bodyAccess === "read" ? "read" : bodyAccess === "write" ? "write" : "none";
    }
    if (statement.kind === "dispatch") {
      if (expressionReferencesIdentifier(statement.expr, name)) return "read";
      let sawWrite = false;
      for (const dispatchCase of statement.cases) {
        if (expressionReferencesIdentifier(dispatchCase.value, name)) return "read";
        const access = firstIdentifierAccessForBorrow(ast, dispatchCase.body, name, seenProcs);
        if (access === "read") return "read";
        if (access === "write") sawWrite = true;
      }
      if (statement.defaultBody !== undefined) {
        const access = firstIdentifierAccessForBorrow(ast, statement.defaultBody, name, seenProcs);
        if (access === "read") return "read";
        if (access === "write") sawWrite = true;
      }
      return sawWrite ? "write" : "none";
    }
    if (statementReadsIdentifierForBorrow(ast, statement, name, seenProcs)) return "read";
    if (statementWritesIdentifierForBorrow(statement, name)) return "write";
    return "none";
}

function statementsReadIdentifierForBorrow(
  ast: ProgramAst,
  statements: readonly StatementAst[],
  name: string,
  seenProcs: Set<string>,
): boolean {
    return statements.some((statement) => statementReadsIdentifierForBorrow(ast, statement, name, seenProcs));
}

function statementReadsIdentifierForBorrow(
  ast: ProgramAst,
  statement: StatementAst,
  name: string,
  seenProcs: Set<string>,
): boolean {
    switch (statement.kind) {
      case "pause":
      case "preview":
      case "return_value":
        return expressionReferencesIdentifier(statement.expr, name);
      case "halt":
        return expressionReferencesIdentifier(statement.expr, name) ||
          statement.displaySources?.includes(name) === true;
      case "assign":
        return expressionReferencesIdentifier(statement.expr, name);
      case "indexed_assign":
        return expressionReferencesIdentifier(statement.target.index, name) ||
          expressionReferencesIdentifier(statement.expr, name);
      case "coord_list_remove":
        return expressionReferencesIdentifier(statement.item, name);
      case "if":
        return expressionReferencesIdentifier(statement.condition.left, name) ||
          expressionReferencesIdentifier(statement.condition.right, name) ||
          statementsReadIdentifierForBorrow(ast, statement.thenBody, name, seenProcs) ||
          (statement.elseBody !== undefined && statementsReadIdentifierForBorrow(ast, statement.elseBody, name, seenProcs));
      case "loop":
        return statementsReadIdentifierForBorrow(ast, statement.body, name, seenProcs);
      case "while":
        return expressionReferencesIdentifier(statement.condition.left, name) ||
          expressionReferencesIdentifier(statement.condition.right, name) ||
          statementsReadIdentifierForBorrow(ast, statement.body, name, seenProcs);
      case "dispatch":
        return expressionReferencesIdentifier(statement.expr, name) ||
          statement.cases.some((dispatchCase) =>
            expressionReferencesIdentifier(dispatchCase.value, name) ||
            statementsReadIdentifierForBorrow(ast, dispatchCase.body, name, seenProcs)
          ) ||
          (statement.defaultBody !== undefined &&
            statementsReadIdentifierForBorrow(ast, statement.defaultBody, name, seenProcs));
      case "core":
        return statement.inputs?.some((input) => expressionReferencesIdentifier(input.expr, name)) ?? false;
      case "call": {
        const proc = ast.procs.find((candidate) => candidate.name === statement.block);
        if (proc === undefined) return false;
        const key = `${name}\0${proc.name}`;
        if (seenProcs.has(key)) return false;
        seenProcs.add(key);
        const result = statementsReadIdentifierForBorrow(ast, proc.body, name, seenProcs);
        seenProcs.delete(key);
        return result;
      }
      case "show":
      case "input":
      case "decimal_series":
        return false;
    }
}

function statementWritesIdentifierForBorrow(statement: StatementAst, name: string): boolean {
    switch (statement.kind) {
      case "assign":
      case "input":
        return statement.target === name;
      case "indexed_assign":
        return statement.target.base === name;
      case "if":
        return statement.thenBody.some((child) => statementWritesIdentifierForBorrow(child, name)) &&
          (statement.elseBody?.some((child) => statementWritesIdentifierForBorrow(child, name)) ?? false);
      case "loop":
      case "while":
        return statement.body.some((child) => statementWritesIdentifierForBorrow(child, name));
      case "dispatch":
        return statement.cases.some((dispatchCase) =>
          dispatchCase.body.some((child) => statementWritesIdentifierForBorrow(child, name))
        ) &&
          (statement.defaultBody?.some((child) => statementWritesIdentifierForBorrow(child, name)) ?? false);
      default:
        return false;
    }
}

function packedLineFamilyUpdateCheckMarkerSteps(
  ctx: LoweringCtx,
  statements: readonly StatementAst[],
  updateProc: string,
  update: XParamIndexedFractionalReportTailProc,
): PackedLineFamilyUpdateCheckStep[] | undefined {
    const steps: PackedLineFamilyUpdateCheckStep[] = [];
    let cursor = 0;
    let normalizer: string | undefined;
    while (cursor < statements.length) {
      const direct = packedLineFamilyUpdateCheckDirectStep(statements, cursor, updateProc, update);
      if (direct !== undefined) {
        steps.push(direct.step);
        cursor += direct.consumed;
        continue;
      }

      const normalized = packedLineFamilyUpdateCheckNormalizedStep(ctx, statements, cursor, updateProc, update);
      if (normalized !== undefined) {
        if (normalizer !== undefined && normalizer !== normalized.normalizer) return undefined;
        normalizer = normalized.normalizer;
        steps.push(normalized.step);
        cursor += normalized.consumed;
        continue;
      }
      return undefined;
    }

    if (steps.length < 4 || new Set(steps.map((step) => step.slot)).size < 4) return undefined;
    if (!steps.every((step) => packedLineFamilyUpdateSlotMatchesBank(ctx, update.update.target, step.slot))) {
      return undefined;
    }
    return steps;
}

function packedLineFamilyUpdateCheckDirectStep(
  statements: readonly StatementAst[],
  cursor: number,
  updateProc: string,
  update: XParamIndexedFractionalReportTailProc,
): { step: PackedLineFamilyUpdateCheckStep; consumed: number } | undefined {
    const lineAssign = statements[cursor];
    const paramAssign = statements[cursor + 1];
    const call = statements[cursor + 2];
    if (
      lineAssign?.kind !== "assign" ||
      lineAssign.target !== update.yName ||
      paramAssign?.kind !== "assign" ||
      paramAssign.target !== update.param ||
      call?.kind !== "call" ||
      call.block !== updateProc
    ) {
      return undefined;
    }
    const slot = numericLiteralValue(paramAssign.expr);
    if (slot === undefined) return undefined;
    return {
      step: { lineExpr: lineAssign.expr, slot, sourceLine: lineAssign.line },
      consumed: 3,
    };
}

function packedLineFamilyUpdateCheckNormalizedStep(
  ctx: LoweringCtx,
  statements: readonly StatementAst[],
  cursor: number,
  updateProc: string,
  update: XParamIndexedFractionalReportTailProc,
): { step: PackedLineFamilyUpdateCheckStep; normalizer: string; consumed: number } | undefined {
    const rawAssign = statements[cursor];
    const normalizerCall = statements[cursor + 1];
    const paramAssign = statements[cursor + 2];
    const call = statements[cursor + 3];
    if (
      rawAssign?.kind !== "assign" ||
      normalizerCall?.kind !== "call" ||
      paramAssign?.kind !== "assign" ||
      paramAssign.target !== update.param ||
      call?.kind !== "call" ||
      call.block !== updateProc
    ) {
      return undefined;
    }
    const normalizer = ctx.ast.procs.find((candidate) => candidate.name === normalizerCall.block);
    if (!procAssignsTargetFromParam(normalizer, update.yName, rawAssign.target)) return undefined;
    const slot = numericLiteralValue(paramAssign.expr);
    if (slot === undefined) return undefined;
    return {
      step: {
        lineExpr: rawAssign.expr,
        slot,
        sourceLine: rawAssign.line,
        normalizer: normalizerCall.block,
      },
      normalizer: normalizerCall.block,
      consumed: 4,
    };
}

function procAssignsTargetFromParam(proc: ProcAst | undefined, target: string, param: string): boolean {
    if (proc === undefined) return false;
    return proc.body.some((statement) =>
      statement.kind === "assign" &&
      statement.target === target &&
      expressionReferencesIdentifier(statement.expr, param)
    );
}

function packedLineFamilyUpdateSlotMatchesBank(
  ctx: LoweringCtx,
  target: Extract<ExpressionAst, { kind: "indexed" }>,
  slot: number,
): boolean {
    const resolved = findStateBankMember(ctx.ast, target);
    if (resolved === undefined) return false;
    return resolved.member.elements.some((element) => element.index === slot);
}

function compileXParamIndexedFractionalReportTailProc(ctx: LoweringCtx, proc: ProcAst): boolean {
    const match = xParamIndexedFractionalReportTailProc(ctx, proc);
    if (match === undefined) return false;

    if (ctx.loweringOptions.xParamYStackStoredEntry === true) {
      ctx.emitStore(match.selector, `set ${match.selector} from X parameter`, proc.body[0]?.line);
      ctx.emitOp(0x14, "X↔Y", "stack-carried packed digit index", match.update.line);
      ctx.markCurrentX(match.yName);
      ctx.emitLabel(xParamYStackStoredEntryLabel(proc.name));
      ctx.emitOp(0x15, "F 10ˣ", "stack-carried packed digit pow10", match.update.line);
      compileExpression(ctx, match.factor);
      ctx.emitOp(0x12, "×", "stack-carried packed digit delta", match.update.line);
      ctx.emitOp(
        0xd0 + registerIndex(match.selectorRegister),
        `К П->X ${match.selectorRegister}`,
        "indexed packed digit update base",
        match.update.line,
      );
      ctx.emitOp(binaryOpcode(match.op), match.op, "indexed packed digit update", match.update.line);
      ctx.emitPreparedIndexedStore(match.update.target, match.selectorRegister, match.update.line);

      compileExpression(ctx, match.mask);
      ctx.emitOp(0x37, "К ∧", "updated packed fractional report mask", match.branch.line);
      ctx.emitOp(0x35, "К {x}", "updated packed fractional report predicate", match.branch.line);

      const haltLabel = ctx.freshLabel("packed_frac_report_x2_halt");
      ctx.emitJump(0x5e, "F x=0", haltLabel, "true branch for packed fractional report !=", match.branch.line);
      ctx.emitOp(0x52, "В/О", "packed fractional report return", match.branch.line);
      ctx.emitLabel(haltLabel);
      ctx.emitOp(0x0a, ".", "updated packed fractional report X2 restore", match.branch.line);
      ctx.emitOp(0x50, "С/П", "halt", match.haltLine);
    } else {
      emitPackedLineFamilyUpdateCheckTail(ctx, match);
    }

    ctx.currentYVariable = undefined;
    ctx.optimizations.push({
      name: ctx.loweringOptions.xParamYStackStoredEntry === true ? "x-param-y-stack-multi-entry" : "x-param-proc-entry",
      detail: ctx.loweringOptions.xParamYStackStoredEntry === true
        ? `Compiled rule ${proc.name} with a secondary entry after its X-parameter store and Y-stack swap.`
        : `Compiled rule ${proc.name} to copy ${ctx.xParamProcs.get(proc.name)?.param ?? "parameter"} directly from X.`,
    });
    ctx.optimizations.push({
      name: "indexed-packed-y-stack-pow10-delta",
      detail: `Used ${match.yName} carried in Y as the pow10 index for ${expressionToIntentText(match.update.target)} at line ${match.update.line}.`,
    });
    ctx.optimizations.push({
      name: "indexed-packed-fractional-report-branch",
      detail: `Reused ${expressionToIntentText(match.update.target)} left in X after a packed update for fractional ${conditionToText(match.branch.condition)} at line ${match.branch.line}.`,
    });
    ctx.optimizations.push({
      name: "indexed-packed-fractional-report-x2-tail",
      detail: `Restored the terminal fractional packed report through X2 after branching directly to the halt path at line ${match.branch.line}.`,
    });
    return true;
}

function xParamIndexedFractionalReportTailProc(
    ctx: LoweringCtx,
    proc: ProcAst,
    options: PackedLineFamilyUpdateCheckMatchOptions = {},
  ): XParamIndexedFractionalReportTailProc | undefined {
    const lowering = ctx.xParamProcs.get(proc.name);
    if (lowering?.kind !== "copy") return undefined;
    const yStack = ctx.xParamYStackProcs.get(proc.name);
    if (yStack === undefined) return undefined;

    const first = proc.body[0];
    const update = proc.body[1];
    if (first !== lowering.first || update?.kind !== "indexed_assign") return undefined;

    const selector = lowering.first.target;
    const selectorRegister = options.allowMutatingSelector === true
      ? physicalIndexedSelectorRegister(ctx, update.target, selector)
      : directPhysicalIndexedSelectorRegister(ctx, update.target, selector);
    if (selectorRegister === undefined) return undefined;

    const delta = indexedPackedPow10DeltaUpdate(update);
    if (delta === undefined) return undefined;
    const yTerm = yStackPow10DeltaTerm(delta.term, yStack.yName);
    if (yTerm === undefined) return undefined;

    const report = fractionalReportTail(update.target, proc.body.slice(2));
    if (report === undefined) return undefined;

    return {
      param: lowering.param,
      selector,
      selectorRegister,
      yName: yStack.yName,
      update,
      factor: yTerm.factor,
      op: delta.op,
      mask: report.mask,
      branch: report.branch,
      haltLine: report.haltLine,
    };
}

function directPhysicalIndexedSelectorRegister(
    ctx: LoweringCtx,
    target: Extract<ExpressionAst, { kind: "indexed" }>,
    selector: string,
  ): RegisterName | undefined {
    const selectorRegister = physicalIndexedSelectorRegister(ctx, target, selector);
    if (selectorRegister === undefined || registerIndex(selectorRegister) < 7) return undefined;
    return selectorRegister;
}

function physicalIndexedSelectorRegister(
    ctx: LoweringCtx,
    target: Extract<ExpressionAst, { kind: "indexed" }>,
    selector: string,
  ): RegisterName | undefined {
    if (target.index.kind !== "identifier" || target.index.name !== selector) return undefined;
    const selectorRegister = ctx.allocation.registers[selector];
    if (selectorRegister === undefined) return undefined;
    const resolved = findStateBankMember(ctx.ast, target);
    if (resolved === undefined) return undefined;
    for (const element of resolved.member.elements) {
      const register = ctx.allocation.registers[element.name];
      if (register === undefined || registerIndex(register) !== element.index) return undefined;
    }
    return selectorRegister;
}

function indexedPackedPow10DeltaUpdate(
    statement: Extract<StatementAst, { kind: "indexed_assign" }>,
  ): { op: "+" | "-"; term: ExpressionAst } | undefined {
    const expr = statement.expr;
    if (expr.kind === "call" && expr.callee.toLowerCase() === "packed_add" && expr.args.length === 3) {
      if (!expressionEquals(expr.args[0]!, statement.target)) return undefined;
      return {
        op: "+",
        term: multiplyExpressions(expr.args[2]!, {
          kind: "call",
          callee: "pow10",
          args: [expr.args[1]!],
        }),
      };
    }
    if (expr.kind !== "binary" || (expr.op !== "+" && expr.op !== "-")) return undefined;
    if (!expressionEquals(expr.left, statement.target)) return undefined;
    if (!isPow10Term(expr.right)) return undefined;
    return { op: expr.op, term: expr.right };
}

function yStackPow10DeltaTerm(
    expr: ExpressionAst,
    yName: string,
  ): { factor: ExpressionAst } | undefined {
    if (isPow10OfIdentifier(expr, yName)) return { factor: numberExpression(1) };
    if (expr.kind !== "binary" || expr.op !== "*") return undefined;
    if (isPow10OfIdentifier(expr.left, yName) && isSimpleStackLoad(expr.right)) {
      return { factor: expr.right };
    }
    if (isPow10OfIdentifier(expr.right, yName) && isSimpleStackLoad(expr.left)) {
      return { factor: expr.left };
    }
    return undefined;
}

function isPow10Term(expr: ExpressionAst): boolean {
    if (expr.kind !== "call") return false;
    const name = expr.callee.toLowerCase();
    if (name === "pow10" && expr.args.length === 1) return true;
    return name === "pow" && expr.args.length === 2 && numericLiteralValue(expr.args[0]!) === 10;
}

function isPow10OfIdentifier(expr: ExpressionAst, name: string): boolean {
    if (expr.kind !== "call") return false;
    const callee = expr.callee.toLowerCase();
    if (callee === "pow10" && expr.args.length === 1) {
      return expr.args[0]?.kind === "identifier" && expr.args[0].name === name;
    }
    return callee === "pow" &&
      expr.args.length === 2 &&
      numericLiteralValue(expr.args[0]!) === 10 &&
      expr.args[1]?.kind === "identifier" &&
      expr.args[1].name === name;
}

function fractionalReportTail(
    target: Extract<ExpressionAst, { kind: "indexed" }>,
    tail: readonly StatementAst[],
  ): { mask: ExpressionAst; branch: Extract<StatementAst, { kind: "if" }>; haltLine: number | undefined } | undefined {
    if (tail.length === 1 && tail[0]?.kind === "if") {
      return fractionalBitReportBranch(target, tail[0]);
    }
    if (tail.length !== 2) return undefined;
    const reportAssign = tail[0];
    const branch = tail[1];
    if (reportAssign?.kind !== "assign" || branch?.kind !== "if") return undefined;
    const report = bitAndReportForTarget(reportAssign.expr, target);
    if (report === undefined || !isSimpleStackLoad(report.mask)) return undefined;
    if (!branchIsIdentifierFractionalReportTrap(branch, reportAssign.target)) return undefined;
    return {
      mask: report.mask,
      branch,
      haltLine: branch.thenBody[0]?.line ?? branch.line,
    };
}

function fractionalBitReportBranch(
    target: Extract<ExpressionAst, { kind: "indexed" }>,
    branch: Extract<StatementAst, { kind: "if" }>,
  ): { mask: ExpressionAst; branch: Extract<StatementAst, { kind: "if" }>; haltLine: number | undefined } | undefined {
    if (branch.elseBody !== undefined || branch.thenBody.length !== 1) return undefined;
    const halt = branch.thenBody[0];
    if (halt?.kind !== "halt" || halt.literal !== undefined) return undefined;
    if (branch.condition.op !== "!=") return undefined;

    const left = fractionalBitAndReportForTarget(branch.condition.left, target);
    const right = fractionalBitAndReportForTarget(branch.condition.right, target);
    const report = left === undefined
      ? (right === undefined ? undefined : (isZeroExpression(branch.condition.left) ? right : undefined))
      : (isZeroExpression(branch.condition.right) ? left : undefined);
    if (report === undefined) return undefined;
    if (!expressionEquals(halt.expr, report.expr) || !isSimpleStackLoad(report.mask)) return undefined;
    return { mask: report.mask, branch, haltLine: halt.line };
}

function branchIsIdentifierFractionalReportTrap(
    branch: Extract<StatementAst, { kind: "if" }>,
    reportName: string,
  ): boolean {
    if (branch.elseBody !== undefined || branch.thenBody.length !== 1) return false;
    const halt = branch.thenBody[0];
    if (halt?.kind !== "halt" || halt.literal !== undefined) return false;
    if (halt.expr.kind !== "identifier" || halt.expr.name !== reportName) return false;
    if (branch.condition.op !== "!=") return false;

    const left = fractionalIdentifierReport(branch.condition.left, reportName);
    const right = fractionalIdentifierReport(branch.condition.right, reportName);
    return left ? isZeroExpression(branch.condition.right) : (right && isZeroExpression(branch.condition.left));
}

function fractionalIdentifierReport(expr: ExpressionAst, reportName: string): boolean {
    return expr.kind === "call" &&
      expr.callee.toLowerCase() === "frac" &&
      expr.args.length === 1 &&
      expr.args[0]?.kind === "identifier" &&
      expr.args[0].name === reportName;
}

function fractionalBitAndReportForTarget(
    expr: ExpressionAst,
    target: Extract<ExpressionAst, { kind: "indexed" }>,
  ): { expr: ExpressionAst; mask: ExpressionAst } | undefined {
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== "frac" || expr.args.length !== 1) return undefined;
    return bitAndReportForTarget(expr.args[0]!, target);
}

function bitAndReportForTarget(
    expr: ExpressionAst,
    target: Extract<ExpressionAst, { kind: "indexed" }>,
  ): { expr: ExpressionAst; mask: ExpressionAst } | undefined {
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== "bit_and" || expr.args.length !== 2) return undefined;
    const [left, right] = expr.args;
    if (left !== undefined && right !== undefined && expressionEquals(left, target)) {
      return { expr, mask: right };
    }
    if (left !== undefined && right !== undefined && expressionEquals(right, target)) {
      return { expr, mask: left };
    }
    return undefined;
}

interface PackedLineFamilyScoreProc {
  readonly score: string;
  readonly normalizer: string;
  readonly normalizerParam: string;
  readonly normalizerReturn: string;
  readonly first: PackedScoreTerm;
  readonly second: PackedScoreTerm;
  readonly diagonalAdd: PackedScoreDiagonalTerm;
  readonly diagonalSub: PackedScoreDiagonalTerm;
  readonly shared: ExpressionAst;
  readonly partial: ExpressionAst;
  readonly line: number;
}

interface PackedScoreTerm {
  readonly lineValue: ExpressionAst;
  readonly index: ExpressionAst;
  readonly line: number;
}

interface PackedScoreDiagonalTerm {
  readonly lineValue: ExpressionAst;
  readonly rawAssign: Extract<StatementAst, { kind: "assign" }>;
  readonly scoreAssign: Extract<StatementAst, { kind: "assign" }>;
}

function compilePackedLineFamilyScoreProc(ctx: LoweringCtx, proc: ProcAst): boolean {
    const match = packedLineFamilyScoreProc(ctx, proc);
    if (match === undefined) return false;

    const helper = ctx.freshLabel("packed_line_family_score_accumulator");
    const emitScore = (term: PackedScoreTerm): void => {
      compileExpression(ctx, term.lineValue);
      compileExpression(ctx, term.index);
      ctx.emitJump(0x53, "ПП", helper, "packed-line score accumulator helper", term.line);
    };
    const markScoreInX = (): void => {
      ctx.currentXVariable = match.score;
      ctx.currentXAliases = new Set([match.score]);
      ctx.currentXKnownZero = false;
    };

    ctx.currentXKnownZero = false;
    ctx.emitZero("packed-line score accumulator", match.line);
    ctx.emitLabel(packedLineFamilyScoreZeroEntryLabel(proc.name));
    emitScore(match.first);
    markScoreInX();
    emitScore(match.second);
    markScoreInX();

    const tail = ctx.freshLabel("packed_line_family_score_tail");
    compileExpression(ctx, match.diagonalAdd.lineValue);
    compileExpression(ctx, match.partial);
    ctx.emitJump(0x53, "ПП", tail, "packed-line shared diagonal score", match.diagonalAdd.scoreAssign.line);
    markScoreInX();

    compileExpression(ctx, match.diagonalSub.lineValue);
    compileExpression(ctx, match.partial);
    ctx.emitOp(0x0b, "/-/", "packed-line negative diagonal index", match.diagonalSub.rawAssign.line);
    ctx.emitLabel(tail);
    compileExpression(ctx, match.shared);
    ctx.emitOp(0x10, "+", "packed-line diagonal index", match.diagonalAdd.rawAssign.line);
    compileBlockCall(ctx, match.normalizer, match.diagonalAdd.rawAssign.line);
    ctx.emitJump(0x53, "ПП", helper, "packed-line score accumulator helper", match.diagonalAdd.scoreAssign.line);
    markScoreInX();
    ctx.emitOp(0x52, "В/О", "packed-line family score return", match.diagonalSub.scoreAssign.line);
    ctx.emitLabel(helper);
    ctx.emitOp(0x15, "F 10ˣ", "packed-line score helper pow10", match.line);
    ctx.emitOp(0x13, "/", "packed-line score helper divide", match.line);
    ctx.emitOp(0x35, "К {x}", "packed-line score helper frac", match.line);
    ctx.emitNumberOrPreload("0.41200076");
    ctx.emitOp(0x11, "-", "packed-line score helper center", match.line);
    ctx.emitOp(0x22, "F x²", "packed-line score helper square", match.line);
    ctx.emitOp(0x10, "+", "packed-line score helper accumulate", match.line);
    ctx.emitOp(0x52, "В/О", "packed-line score helper return", match.line);
    ctx.optimizations.push({
      name: "packed-line-family-score-accumulator",
      detail: `Accumulated four packed_score() terms through one helper while sharing diagonal scoring for ${expressionToIntentText(match.diagonalAdd.rawAssign.expr)} and ${expressionToIntentText(match.diagonalSub.rawAssign.expr)} in ${proc.name}.`,
    });
    return true;
}

function packedLineFamilyScoreZeroEntryLabel(procName: string): string {
    return `\0packed_line_family_score_zero_entry_${procName}`;
}

export function statementCanEnterPackedLineFamilyScoreZeroEntry(
    ctx: LoweringCtx,
    statement: StatementAst | undefined,
  ): boolean {
    if (statement?.kind !== "call") return false;
    const proc = ctx.ast.procs.find((candidate) => candidate.name === statement.block);
    return proc !== undefined && packedLineFamilyScoreProc(ctx, proc) !== undefined;
}

function packedLineFamilyScoreProc(ctx: LoweringCtx, proc: ProcAst): PackedLineFamilyScoreProc | undefined {
    if (proc.body.length !== 7) return undefined;
    const [firstAssign, addRaw, addCall, addScore, subRaw, subCall, subScore] = proc.body;
    if (
      firstAssign?.kind !== "assign" ||
      addRaw?.kind !== "assign" ||
      addCall?.kind !== "call" ||
      addScore?.kind !== "assign" ||
      subRaw?.kind !== "assign" ||
      subCall?.kind !== "call" ||
      subScore?.kind !== "assign"
    ) return undefined;
    if (addCall.block !== subCall.block) return undefined;
    if (addRaw.target !== subRaw.target) return undefined;
    const normalizer = ctx.xParamProcs.get(addCall.block);
    if (normalizer === undefined || normalizer.param !== addRaw.target) return undefined;
    const normalizerProc = ctx.ast.procs.find((candidate) => candidate.name === addCall.block);
    if (normalizerProc === undefined) return undefined;
    const normalizerReturn = ctx.procReturnXVariable(normalizerProc);
    if (normalizerReturn === undefined) return undefined;
    if (!ctx.stackOnlyStateFields.has(firstAssign.target)) return undefined;

    const firstPair = packedScorePair(firstAssign.expr);
    if (firstPair === undefined) return undefined;
    const addTerm = packedScoreAccumulatorTerm(addScore, firstAssign.target, normalizerReturn);
    const subTerm = packedScoreAccumulatorTerm(subScore, firstAssign.target, normalizerReturn);
    if (addTerm === undefined || subTerm === undefined) return undefined;
    const diagonal = sharedAddSubOperands(addRaw.expr, subRaw.expr);
    if (diagonal === undefined) return undefined;

    return {
      score: firstAssign.target,
      normalizer: addCall.block,
      normalizerParam: normalizer.param,
      normalizerReturn,
      first: { ...firstPair.first, line: firstAssign.line },
      second: { ...firstPair.second, line: firstAssign.line },
      diagonalAdd: { lineValue: addTerm.lineValue, rawAssign: addRaw, scoreAssign: addScore },
      diagonalSub: { lineValue: subTerm.lineValue, rawAssign: subRaw, scoreAssign: subScore },
      shared: diagonal.shared,
      partial: diagonal.partial,
      line: firstAssign.line,
    };
}

function packedScorePair(expr: ExpressionAst): { first: Omit<PackedScoreTerm, "line">; second: Omit<PackedScoreTerm, "line"> } | undefined {
    if (expr.kind !== "binary" || expr.op !== "+") return undefined;
    const first = packedScoreTerm(expr.left);
    const second = packedScoreTerm(expr.right);
    return first !== undefined && second !== undefined ? { first, second } : undefined;
}

function packedScoreAccumulatorTerm(
  statement: Extract<StatementAst, { kind: "assign" }>,
  score: string,
  indexName: string,
): { lineValue: ExpressionAst } | undefined {
    if (statement.target !== score) return undefined;
    const expr = statement.expr;
    if (expr.kind !== "binary" || expr.op !== "+") return undefined;
    const target = { kind: "identifier", name: score } satisfies ExpressionAst;
    if (expressionEquals(expr.left, target)) return packedScoreTermWithIndex(expr.right, indexName);
    if (expressionEquals(expr.right, target)) return packedScoreTermWithIndex(expr.left, indexName);
    return undefined;
}

function packedScoreTermWithIndex(expr: ExpressionAst, indexName: string): { lineValue: ExpressionAst } | undefined {
    const term = packedScoreTerm(expr);
    if (term === undefined) return undefined;
    return term.index.kind === "identifier" && term.index.name === indexName
      ? { lineValue: term.lineValue }
      : undefined;
}

function packedScoreTerm(expr: ExpressionAst): Omit<PackedScoreTerm, "line"> | undefined {
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== "packed_score" || expr.args.length !== 2) {
      return undefined;
    }
    const [lineValue, index] = expr.args;
    return lineValue !== undefined && index !== undefined ? { lineValue, index } : undefined;
}

function sharedAddSubOperands(
  add: ExpressionAst,
  sub: ExpressionAst,
): { shared: ExpressionAst; partial: ExpressionAst } | undefined {
    if (add.kind !== "binary" || add.op !== "+" || sub.kind !== "binary" || sub.op !== "-") return undefined;
    if (!expressionEquals(add.left, sub.left) || !expressionEquals(add.right, sub.right)) return undefined;
    if (!isSimpleStackLoad(add.left) || !isSimpleStackLoad(add.right)) return undefined;
    return { shared: add.left, partial: add.right };
}

function compileXParamValueFunctionBody(
  ctx: LoweringCtx,
  proc: ProcAst,
  match: XParamValueFunction,
  scratch: string,
): void {
    emitPositiveModuloOfCurrentX(ctx, match.width, match.line);
    ctx.emitStore(scratch, `set ${scratch}`, match.line);
    const scratchExpr: ExpressionAst = { kind: "identifier", name: scratch };
    ctx.compileStatements([
      {
        kind: "if",
        condition: {
          left: scratchExpr,
          op: "<=",
          right: numberExpression(0),
        },
        thenBody: [{
          kind: "assign",
          target: scratch,
          expr: {
            kind: "binary",
            op: "+",
            left: scratchExpr,
            right: numberExpression(match.width),
          },
          line: match.line,
        }],
        line: match.line,
      },
      { kind: "return_value", expr: scratchExpr, line: match.line },
    ]);
    ctx.optimizations.push({
      name: "x-param-value-function",
      detail: `Compiled ${proc.name} to consume ${match.param} directly from X through ${scratch}.`,
    });
}

function emitPositiveModuloOfCurrentX(ctx: LoweringCtx, width: number, line: number): void {
    ctx.emitOp(0x34, "К [x]", "x-param value integer part", line);
    ctx.emitNumberOrPreload(String(width));
    ctx.emitOp(0x13, "/", "x-param value modulo quotient", line);
    ctx.emitOp(0x35, "К {x}", "x-param value modulo fraction", line);
    ctx.emitNumberOrPreload(String(width));
    ctx.emitOp(0x12, "*", "x-param value modulo scale", line);
}

// Cheap static size proxy for layout ordering: number of statements in a proc
// body (nested bodies counted shallowly). Exact cell cost is only known after
// lowering, but statement count tracks it well enough to drive a placement search.
function procBodySizeProxy(proc: ProcAst): number {
    return proc.body.length;
}

// Order in which non-inline procedures are emitted. Default is source order; the
// layout-search variants reorder so a chosen class of procs occupies the lowest,
// cheapest addresses. Procs are reached only through label references, so every
// ordering is behavior-preserving; the whole-program selector keeps whichever
// ordering lowers to the fewest cells.
function procEmissionOrder(ctx: LoweringCtx): readonly ProcAst[] {
    const strategy = ctx.loweringOptions.orderProcsByCallCount === true
      ? "call-count"
      : ctx.loweringOptions.procLayoutStrategy;
    if (strategy === undefined) return ctx.ast.procs;
    const indexed = ctx.ast.procs.map((proc, index) => ({ proc, index }));
    const callCount = (proc: ProcAst): number => ctx.procCallCounts.get(proc.name) ?? 0;
    indexed.sort((a, b) => {
      let primary = 0;
      switch (strategy) {
        case "call-count":
          primary = callCount(b.proc) - callCount(a.proc);
          break;
        case "size-asc":
          primary = procBodySizeProxy(a.proc) - procBodySizeProxy(b.proc);
          break;
        case "size-desc":
          primary = procBodySizeProxy(b.proc) - procBodySizeProxy(a.proc);
          break;
        case "reverse":
          primary = b.index - a.index;
          break;
      }
      return primary !== 0 ? primary : a.index - b.index;
    });
    return indexed.map((entry) => entry.proc);
}

function compileXParamReturnDecayBody(
  ctx: LoweringCtx,
  decay: { factor: ExpressionAst; divisor: ExpressionAst; line: number },
): void {
    ctx.emitOp(0x0e, "В↑", "x-param decay keep base", decay.line);
    compileExpression(ctx, decay.factor);
    ctx.emitOp(0x12, "*", "x-param decay scale", decay.line);
    compileExpression(ctx, decay.divisor);
    ctx.emitOp(0x13, "/", "x-param decay divide", decay.line);
    ctx.emitOp(0x34, "К [x]", "x-param decay int", decay.line);
    ctx.emitOp(0x11, "-", "x-param decay subtract", decay.line);
}

function compileXParamStackStopRiskReadBody(
  ctx: LoweringCtx,
  risk: XParamStackStopRiskRead,
): void {
    ctx.emitOp(0x0e, "В↑", "x-param keep displayed value", risk.showLine);
    ctx.emitOp(0x50, "С/П", `show ${risk.display}`, risk.showLine);
    ctx.armValueInY(risk.param);
    compileStackStopRiskTail(ctx, risk.risk, {
      inputComment: "risk input read()",
      inputLine: risk.line,
      consumerLine: risk.line,
    });
    ctx.clearArmedValueInY();
}

export function compileRuntimeHelpers(ctx: LoweringCtx): void {
    for (let index = 0; index < ctx.terminalTailHelpers.length; index += 1) {
      const helper = ctx.terminalTailHelpers[index]!;
      ctx.emitLabel(helper.label);
      ctx.compileStatements(helper.body);
      ctx.optimizations.push({
        name: "local-terminal-tail",
        detail: `Emitted local terminal tail for branch at line ${helper.line}.`,
      });
    }
    for (const helper of ctx.displayHelpers.values()) {
      ctx.emitLabel(helper.label);
      compilePackedDisplayBody(ctx, helper.display, helper.line, false);
      ctx.emitOp(0x52, "В/О", `display ${helper.display.name} return`, helper.line);
      ctx.optimizations.push({
        name: "packed-display-helper",
        detail: `Emitted shared packed display helper for screen ${helper.display.name}.`,
      });
    }
    for (const helper of ctx.displayByteHelpers.values()) {
      ctx.emitLabel(helper.label);
      compileDisplayByteBuilder(ctx, helper.display, helper.line, false);
      ctx.emitOp(0x52, "В/О", `display ${helper.display.name} return`, helper.line);
      ctx.optimizations.push({
        name: "display-byte-helper",
        detail: `Emitted shared display-byte helper for screen ${helper.display.name}.`,
      });
    }
    for (const helper of ctx.literalDisplayHelpers.values()) {
      ctx.emitLabel(helper.label);
      compileLiteralDisplayBody(ctx, helper.display, helper.line);
      ctx.emitOp(0x52, "В/О", `display ${helper.display.name} return`, helper.line);
      ctx.optimizations.push({
        name: "screen-video-literal-helper",
        detail: `Emitted shared literal video helper for screen ${helper.display.name}.`,
      });
    }
    for (const helper of ctx.showSequenceHelpers.values()) {
      ctx.emitLabel(helper.label);
      compilePackedDisplayBody(ctx, helper.first, helper.line, false);
      compilePackedDisplayBody(ctx, helper.second, helper.line, false);
      ctx.emitOp(0x52, "В/О", `show sequence ${helper.first.name}/${helper.second.name} return`, helper.line);
      ctx.optimizations.push({
        name: "show-sequence-helper",
        detail: `Emitted shared helper for show ${helper.first.name}; show ${helper.second.name}; read.`,
      });
    }
    for (const helper of ctx.randomCellHelpers.values()) {
      ctx.emitLabel(helper.label);
      ctx.emittingRandomCellHelper = true;
      try {
        compileExpression(ctx, helper.expr);
      } finally {
        ctx.emittingRandomCellHelper = false;
      }
      ctx.emitOp(0x52, "В/О", "random coordinate helper return", helper.line);
      ctx.optimizations.push({
        name: "random-cell-helper",
        detail: `Emitted shared random cell helper for ${expressionToIntentText(helper.expr)}.`,
      });
    }
    for (const helper of ctx.expressionHelpers.values()) {
      ctx.emitLabel(helper.label);
      ctx.emittingExpressionHelper = true;
      try {
        compileExpression(ctx, helper.expr);
      } finally {
        ctx.emittingExpressionHelper = false;
      }
      ctx.emitOp(0x52, "В/О", "expression helper return", helper.line);
      ctx.optimizations.push({
        name: "expression-helper",
        detail: `Emitted shared helper for ${expressionToIntentText(helper.expr)}.`,
      });
    }
    if (ctx.packedScoreStackHelper !== undefined) {
      const helper = ctx.packedScoreStackHelper;
      ctx.emitLabel(helper.label);
      ctx.emitOp(0x15, "F 10ˣ", "packed_score helper pow10", helper.line);
      ctx.emitOp(0x13, "/", "packed_score helper divide", helper.line);
      ctx.emitOp(0x35, "К {x}", "packed_score helper frac", helper.line);
      ctx.emitNumberOrPreload("0.41200076");
      ctx.emitOp(0x11, "-", "packed_score helper center", helper.line);
      ctx.emitOp(0x22, "F x²", "packed_score helper square", helper.line);
      ctx.emitOp(0x52, "В/О", "packed_score helper return", helper.line);
      ctx.optimizations.push({
        name: "packed-score-stack-helper",
        detail: "Emitted shared stack helper for packed_score(value, index).",
      });
    }
    for (const helper of ctx.nearAnyHelpers.values()) {
      ctx.emitLabel(helper.label);
      compileExpression(ctx, helper.value);
      ctx.emitOp(0x11, "-", "near_any delta", helper.line);
      ctx.emitOp(0x31, "К |x|", "near_any distance", helper.line);
      compileExpression(ctx, helper.radius);
      ctx.emitOp(0x14, "<->", "near_any radius before distance", helper.line);
      ctx.emitOp(0x11, "-", "near_any margin", helper.line);
      ctx.emitOp(0x52, "В/О", "near_any return", helper.line);
      ctx.optimizations.push({
        name: "near-any-helper",
        detail: `Emitted shared near_any helper for ${expressionToIntentText(helper.value)} / ${expressionToIntentText(helper.radius)}.`,
      });
    }
    for (const helper of ctx.lineCountHelpers.values()) {
      ctx.emitLabel(helper.label);
      ctx.emitStore(spatialCountMaskScratchName(), "line count mask", helper.line);
      emitSpatialLineCountLoopBody(ctx, spatialCountMaskScratchName(), helper.cell, helper.board, helper.line);
      ctx.emitOp(0x52, "В/О", "line_count return", helper.line);
      ctx.optimizations.push({
        name: "spatial-line-count-helper",
        detail: `Emitted shared line_count helper for ${helper.board.name}/${expressionToIntentText(helper.cell)}.`,
      });
    }
    for (const helper of ctx.spatialLineProgressionHelpers.values()) {
      ctx.emitLabel(helper.label);
      emitSpatialLineProgressionHelperBody(ctx, helper.hitMask, helper.cell, helper.operation, helper.line);
      ctx.emitOp(0x52, "В/О", `${helper.operation} line progression return`, helper.line);
      ctx.optimizations.push({
        name: "spatial-line-progression-helper",
        detail: `Emitted shared ${helper.operation} line progression helper for ${helper.hitMask}.`,
      });
    }
    for (const helper of ctx.spatialSumLoopHelpers.values()) {
      ctx.emitLabel(helper.label);
      emitSpatialSumLoopHelperBody(ctx, helper.hitMask, helper.cell, helper.operation, helper.line);
      ctx.emitOp(0x52, "В/О", `${helper.operation} progression return`, helper.line);
      ctx.optimizations.push({
        name: "spatial-sum-loop-helper",
        detail: `Emitted shared ${helper.operation} progression helper for ${helper.hitMask}.`,
      });
    }
    for (const helper of ctx.spatialBitMaskHelpers.values()) {
      ctx.emitLabel(helper.label);
      if (ctx.loweringOptions.compactBitMaskHelperBody === true) {
        emitCompactBitMaskFromCurrentXWithQuotientScratch(ctx, helper.scratch, helper.line);
      } else {
        ctx.emitOp(0x0e, "В↑", "bit_mask helper argument lift", helper.line);
        emitBitMaskFromCurrentXWithQuotientScratch(ctx, helper.scratch, helper.line);
      }
      ctx.emitOp(0x52, "В/О", "bit_mask return", helper.line);
      ctx.optimizations.push({
        name: "bit-mask-helper",
        detail: `Emitted shared bit_mask helper using ${helper.scratch} with quotient reuse.`,
      });
    }
    for (const helper of ctx.spatialHitHelpers.values()) {
      ctx.emitLabel(helper.label);
      const bitMaskHelper = ctx.spatialBitMaskHelpers.get(helper.scratch) ??
        ctx.spatialBitMaskHelpers.values().next().value;
      if (bitMaskHelper !== undefined) {
        ctx.emitJump(0x53, "ПП", bitMaskHelper.label, "spatial hit bit_mask", helper.line);
        ctx.optimizations.push({
          name: "spatial-hit-bit-mask-helper-reuse",
          detail: `Reused shared bit_mask helper ${bitMaskHelper.label} inside spatial hit ${helper.label}.`,
        });
      } else {
        ctx.emitStore(helper.scratch, "spatial hit index", helper.line);
        // Build the cell mask before recalling the set: constructing the mask
        // churns the four-deep stack, so nothing else may be held while it runs.
        compileBitMaskWithQuotientScratch(ctx,
          { kind: "identifier", name: helper.scratch },
          helper.scratch,
          helper.line,
          { forceInline: true },
        );
      }
      ctx.emitRecall(helper.mask, "spatial hit mask", helper.line);
      ctx.emitOp(0x37, "К ∧", "spatial hit test", helper.line);
      ctx.emitOp(0x35, "К {x}", "spatial hit membership fraction", helper.line);
      ctx.emitOp(0x32, "К ЗН", "spatial hit to count", helper.line);
      ctx.emitOp(0x52, "В/О", "spatial hit return", helper.line);
    }
}

export function compileInitialState(ctx: LoweringCtx): void {
    if (ctx.ast.v2) {
      const fields = ctx.ast.states.flatMap((state) => state.fields);
      if (fields.some((field) => field.initial !== undefined || field.initialStack !== undefined)) {
        ctx.optimizations.push({
          name: "auto-preload-initial-state",
          detail: "Moved initial state into setup/preload values so official program cells stay focused on turn logic.",
        });
      }
      return;
    }
    for (const state of ctx.ast.states) {
      for (const field of state.fields.filter((candidate) => candidate.initialStack === "Y")) {
        ctx.emitOp(0x14, "X↔Y", `init ${state.name}.${field.name} from stack.Y`, field.line);
        ctx.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
        ctx.emitOp(0x14, "X↔Y", `restore stack.X after ${field.name}`, field.line);
      }
      for (const field of state.fields.filter((candidate) => candidate.initialStack === "X")) {
        ctx.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
      }
      for (const field of state.fields) {
        if (field.initialStack !== undefined) continue;
        if (field.initial === undefined) continue;
        compileExpression(ctx, field.initial);
        ctx.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
      }
      if (state.fields.length > 0) {
        ctx.optimizations.push({
          name: "intent-state-lowering",
          detail: `Lowered state ${state.name} with ${state.fields.length} fields to register-backed values.`,
        });
      }
    }
}

export function compileRepeatedAssignmentValue(ctx: LoweringCtx, statements: readonly StatementAst[], start: number): number {
    const first = statements[start];
    if (first?.kind !== "assign" || !expressionPureForSubstitution(first.expr)) return 0;
    let end = start + 1;
    while (end < statements.length) {
      const candidate = statements[end]!;
      if (candidate.kind !== "assign" || !expressionEquals(candidate.expr, first.expr)) break;
      if (assignmentFeedsLaterUnitDecrementLoop(ctx, statements, end)) break;
      end += 1;
    }
    const count = end - start;
    if (count <= 1) return 0;
    for (let index = start; index < end; index += 1) {
      const assignment = statements[index] as Extract<StatementAst, { kind: "assign" }>;
      if (ctx.allocation.registers[assignment.target] === undefined) return 0;
    }
    compileExpression(ctx, first.expr);
    for (let index = start; index < end; index += 1) {
      const assignment = statements[index] as Extract<StatementAst, { kind: "assign" }>;
      ctx.emitStore(assignment.target, `set ${assignment.target}`, assignment.line);
    }
    ctx.optimizations.push({
      name: "repeated-assignment-value-reuse",
      detail: `Stored one computed value into ${count} consecutive assignment targets at line ${first.line}.`,
    });
    return count;
}

function assignmentFeedsLaterUnitDecrementLoop(
  ctx: LoweringCtx,
  statements: readonly StatementAst[],
  index: number,
): boolean {
    const initializer = statements[index];
    if (initializer?.kind !== "assign") return false;
    const initialValue = initializer.expr.kind === "number" ? Number(initializer.expr.raw) : undefined;
    if (initialValue === undefined || !Number.isInteger(initialValue) || initialValue < 1) return false;
    const register = ctx.allocation.registers[initializer.target];
    if (register === undefined || flOpcode(register) === undefined) return false;

    for (let cursor = index + 1; cursor < statements.length; cursor += 1) {
      const statement = statements[cursor]!;
      if (statement.kind === "while") {
        return unitDecrementLoopTarget(statement) === initializer.target;
      }
      if (!statementSafeBetweenCounterInitializerAndLoop(statement, initializer.target)) return false;
    }
    return false;
}

function unitDecrementLoopTarget(loop: Extract<StatementAst, { kind: "while" }>): string | undefined {
    const condition = loop.condition;
    const target = condition.left.kind === "identifier"
      ? condition.left.name
      : condition.right.kind === "identifier"
        ? condition.right.name
        : undefined;
    if (target === undefined) return undefined;
    if (!unitPositiveLoopCondition(condition, target)) return undefined;
    const final = loop.body.at(-1);
    if (final?.kind !== "assign" || final.target !== target) return undefined;
    return isUnitDecrementExpression(target, final.expr) ? target : undefined;
}

function unitPositiveLoopCondition(condition: ConditionAst, target: string): boolean {
    const leftIdentifier = condition.left.kind === "identifier" && condition.left.name === target;
    const rightIdentifier = condition.right.kind === "identifier" && condition.right.name === target;
    const leftValue = condition.left.kind === "number" ? Number(condition.left.raw) : undefined;
    const rightValue = condition.right.kind === "number" ? Number(condition.right.raw) : undefined;

    if (leftIdentifier) {
      if (condition.op === ">=" && rightValue === 1) return true;
      if (condition.op === ">" && rightValue === 0) return true;
    }
    if (rightIdentifier) {
      if (condition.op === "<=" && leftValue === 1) return true;
      if (condition.op === "<" && leftValue === 0) return true;
    }
    return false;
}

function statementSafeBetweenCounterInitializerAndLoop(statement: StatementAst, target: string): boolean {
    return statement.kind === "assign" &&
      statement.target !== target &&
      !expressionReferencesIdentifier(statement.expr, target);
}

export function compileXParamProcCall(ctx: LoweringCtx, 
    assign: Extract<StatementAst, { kind: "assign" }>,
    call: Extract<StatementAst, { kind: "call" }>,
  ): boolean {
    const lowering = ctx.xParamProcs.get(call.block);
    if (lowering === undefined || assign.target !== lowering.param) return false;
    if (!expressionPureForSubstitution(assign.expr)) return false;

    const reusedCurrentX =
      assign.expr.kind === "identifier" &&
      ctx.xHolds(assign.expr.name);
    if (!reusedCurrentX) compileExpression(ctx, assign.expr);
    compileBlockCall(ctx, call.block, call.line);
    ctx.optimizations.push({
      name: "x-param-proc-call",
      detail: reusedCurrentX
        ? `Passed ${assign.target} to rule ${call.block} through the value already in X at line ${assign.line}.`
        : `Passed ${assign.target} to rule ${call.block} through X at line ${assign.line}.`,
    });
    return true;
}

export function compileXParamProcBody(ctx: LoweringCtx, proc: ProgramAst["procs"][number], lowering: XParamProcLowering): void {
    if (lowering.kind === "indexed") {
      ctx.currentXVariable = lowering.param;
      ctx.currentXAliases = new Set([lowering.param]);
      ctx.currentXKnownZero = false;
      ctx.compileStatement(lowering.first);
      ctx.compileStatements(proc.body.slice(1));
      ctx.optimizations.push({
        name: "x-param-indexed-entry",
        detail: `Compiled rule ${proc.name} to consume ${lowering.param} as an indexed selector directly from X.`,
      });
      return;
    }
    if (lowering.kind === "copy") {
      ctx.emitStore(lowering.first.target, `set ${lowering.first.target} from X parameter`, lowering.first.line);
      const yStack = ctx.xParamYStackProcs.get(proc.name);
      if (yStack !== undefined && ctx.loweringOptions.xParamYStackStoredEntry === true) {
        ctx.emitOp(0x14, "X↔Y", "x-param y-stack entry", lowering.first.line);
        ctx.markCurrentX(yStack.yName);
        ctx.emitLabel(xParamYStackStoredEntryLabel(proc.name));
      } else if (yStack !== undefined) {
        ctx.currentYVariable = yStack.yName;
      }
      ctx.compileStatements(proc.body.slice(1));
      if (yStack !== undefined) ctx.currentYVariable = undefined;
      ctx.optimizations.push({
        name: yStack !== undefined && ctx.loweringOptions.xParamYStackStoredEntry === true ? "x-param-y-stack-multi-entry" : "x-param-proc-entry",
        detail: yStack !== undefined && ctx.loweringOptions.xParamYStackStoredEntry === true
          ? `Compiled rule ${proc.name} with a secondary entry after its X-parameter store and Y-stack swap.`
          : `Compiled rule ${proc.name} to copy ${lowering.param} directly from X.`,
      });
      return;
    }
    if (lowering.kind === "expr") {
      ctx.currentXVariable = lowering.param;
      ctx.currentXAliases = new Set([lowering.param]);
      ctx.currentXKnownZero = false;
      if (!compileXParamFirstExpression(ctx, lowering.first.expr, lowering.param, lowering.first.line)) {
        ctx.diagnostics.push(buildDiagnostic(
          "error",
          `Cannot compile X-parameter expression for ${proc.name}.`,
          lowering.first.line,
        ));
        return;
      }
      if (ctx.stackOnlyStateFields.has(lowering.first.target)) {
        ctx.currentXVariable = lowering.first.target;
        ctx.currentXAliases = new Set([lowering.first.target]);
        ctx.currentXKnownZero = false;
      } else {
        ctx.emitStore(lowering.first.target, `set ${lowering.first.target} from X parameter expression`, lowering.first.line);
      }
      ctx.compileStatements(proc.body.slice(1));
      ctx.optimizations.push({
        name: "x-param-proc-entry",
        detail: ctx.stackOnlyStateFields.has(lowering.first.target)
          ? `Compiled rule ${proc.name} to return stack-only ${lowering.first.target} from ${lowering.param} already in X.`
          : `Compiled rule ${proc.name} to compute ${lowering.first.target} from ${lowering.param} already in X.`,
      });
      return;
    }
    ctx.emitRecall(lowering.other, `${proc.name} ${lowering.first.target} base`, lowering.first.line);
    ctx.emitOp(0x10, "+", `${proc.name} ${lowering.first.target} from X parameter`, lowering.first.line);
    ctx.emitStore(lowering.first.target, `set ${lowering.first.target}`, lowering.first.line);
    ctx.compileStatements(proc.body.slice(1));
    ctx.optimizations.push({
      name: "x-param-proc-entry",
      detail: `Compiled rule ${proc.name} to consume ${lowering.param} directly from X.`,
    });
}

export function xParamYStackStoredEntryLabel(procName: string): string {
    return `\0xparam_ystack_entry_${procName}`;
}

function compileXParamFirstExpression(ctx: LoweringCtx, expr: ExpressionAst, param: string, line: number): boolean {
    if (!expressionCanConsumeIdentifierFromX(expr, param)) return false;
    switch (expr.kind) {
      case "identifier":
        return expr.name === param;
      case "unary":
        if (expr.op !== "-" || !compileXParamFirstExpression(ctx, expr.expr, param, line)) return false;
        ctx.emitOp(0x0b, "/-/", "x-param unary minus", line);
        return true;
      case "binary": {
        const leftUses = expressionReferencesIdentifier(expr.left, param);
        const rightUses = expressionReferencesIdentifier(expr.right, param);
        if (leftUses === rightUses) return false;
        if (leftUses) {
          if (!compileXParamFirstExpression(ctx, expr.left, param, line)) return false;
          compileExpression(ctx, expr.right);
        } else {
          if (expr.op !== "+" && expr.op !== "*") return false;
          if (!compileXParamFirstExpression(ctx, expr.right, param, line)) return false;
          compileExpression(ctx, expr.left);
        }
        ctx.emitOp(binaryOpcode(expr.op), expr.op, `x-param expr ${expr.op}`, line);
        return true;
      }
      case "call": {
        if (expr.args.length !== 1 || !compileXParamFirstExpression(ctx, expr.args[0]!, param, line)) return false;
        const opcode = X_TRANSFORM_UNARY_OPCODES[expr.callee.toLowerCase()];
        if (opcode === undefined) return false;
        ctx.emitOp(opcode[0], opcode[1], `x-param ${expr.callee}()`, line);
        return true;
      }
      default:
        return false;
    }
}

export function compileStackUnaryDerivedAssignments(ctx: LoweringCtx, statements: readonly StatementAst[], start: number): number {
    const first = statements[start];
    if (first?.kind !== "assign") return 0;
    const firstMatch = matchStackUnaryDerivationCall(first.expr);
    if (firstMatch === undefined) return 0;
    if (!expressionPureForSubstitution(firstMatch.arg)) return 0;

    const derivations: Array<{
      statement: Extract<StatementAst, { kind: "assign" }>;
      call: StackUnaryDerivationCall;
    }> = [];
    const targets = new Set<string>();
    const end = Math.min(statements.length, start + 4);
    for (let index = start; index < end; index += 1) {
      const statement = statements[index]!;
      if (statement.kind !== "assign") break;
      const call = matchStackUnaryDerivationCall(statement.expr);
      if (call === undefined || !expressionEquals(call.arg, firstMatch.arg)) break;
      if (targets.has(statement.target)) break;
      targets.add(statement.target);
      derivations.push({ statement, call });
    }

    if (derivations.length < 3) return 0;
    if ([...targets].some((target) => expressionReferencesIdentifier(firstMatch.arg, target))) return 0;

    const argCost = estimateExpressionCost(firstMatch.arg);
    const normalCost = derivations.length * (argCost + 2);
    const duplicateCost = derivations.length - 1;
    const restoreCost = 1 + (derivations.length - 2) * 2;
    const sharedCost = argCost + duplicateCost + derivations.length * 2 + restoreCost;
    if (sharedCost >= normalCost) return 0;

    compileExpression(ctx, firstMatch.arg);
    for (let copy = 1; copy < derivations.length; copy += 1) {
      ctx.emitOp(0x0e, "В↑", "duplicate operand for Z-stack derived tail", first.line);
    }

    for (let index = 0; index < derivations.length; index += 1) {
      const { statement, call } = derivations[index]!;
      if (index === 1) {
        ctx.emitOp(0x14, "XY", "restore shared operand from Y", statement.line);
      } else if (index > 1) {
        ctx.emitOp(0x25, "F reverse", "rotate shared operand from Z", statement.line);
        ctx.emitOp(0x14, "XY", "restore shared operand from stack", statement.line);
      }
      ctx.emitOp(call.opcode, call.mnemonic, `${call.fn}()`, statement.line);
      ctx.emitStore(statement.target, `set ${statement.target}`, statement.line);
    }

    const functions = derivations.map(({ call }) => `${call.fn}()`).join("/");
    const stackRegisters = derivations.length === 4 ? "X/Y/Z/T" : "X/Y/Z";
    ctx.optimizations.push({
      name: "z-stack-derived-value-reuse",
      detail: `Computed ${expressionToIntentText(firstMatch.arg)} once and derived ${functions} through ${stackRegisters} stack copies.`,
    });
    return derivations.length;
}

export function compileGuardAssignmentSubstitution(ctx: LoweringCtx, 
    assign: Extract<StatementAst, { kind: "assign" }>,
    guarded: Extract<StatementAst, { kind: "if" }>,
  ): boolean {
    const readsInCondition = countIdentifierReadsInCondition(guarded.condition, assign.target);
    if (readsInCondition === 0) return false;
    if ((ctx.readCounts.get(assign.target) ?? 0) !== readsInCondition) return false;
    if (!expressionPureForSubstitution(assign.expr)) return false;
    const substitutedCondition = substituteConditionIdentifier(guarded.condition, assign.target, assign.expr);
    const ordinaryCost = estimateExpressionCost(assign.expr) + 1 + conditionCompileCost(guarded.condition);
    const substitutedCost = conditionCompileCost(substitutedCondition);
    if (substitutedCost + 4 >= ordinaryCost) return false;
    compileIf(ctx, {
      ...guarded,
      condition: substitutedCondition,
    }, guarded.line);
    ctx.optimizations.push({
      name: "single-use-guard-substitution",
      detail: `Substituted ${assign.target} directly into the following condition at lines ${assign.line}/${guarded.line}.`,
    });
    return true;
}

// A single instruction of a verified decimal-recurrence listing. The byte
// sequences below are hand-tuned and validated on real MK-61 hardware; they
// cannot be derived parametrically, so each verified (digits, counterStart)
// pair carries its own complete listing.
type DecimalSeriesOp =
  | { readonly kind: "op"; readonly opcode: number; readonly mnemonic: string; readonly comment: string }
  | { readonly kind: "jump"; readonly opcode: number; readonly mnemonic: string; readonly target: number; readonly comment: string }
  | { readonly kind: "formal"; readonly opcode: number; readonly comment: string };

interface VerifiedDecimalSeriesListing {
  readonly digits: number;
  readonly counterStart: number;
  readonly ops: readonly DecimalSeriesOp[];
}

function op(opcode: number, mnemonic: string, comment: string): DecimalSeriesOp {
  return { kind: "op", opcode, mnemonic, comment };
}

function jump(opcode: number, mnemonic: string, target: number, comment: string): DecimalSeriesOp {
  return { kind: "jump", opcode, mnemonic, target, comment };
}

function formal(opcode: number, comment: string): DecimalSeriesOp {
  return { kind: "formal", opcode, comment };
}

// Verified decimal-recurrence listings, keyed by (digits, counterStart). New
// precisions are data-only additions once their byte sequence has been
// validated on hardware. The (94, 65) listing is currently the only one.
const VERIFIED_DECIMAL_SERIES_LISTINGS: readonly VerifiedDecimalSeriesListing[] = [
  {
    digits: 94,
    counterStart: 65,
    ops: [
      op(0x52, "В/О", "decimal recurrence setup"),
      op(0x06, "6", "decimal recurrence setup"),
      op(0x05, "5", "decimal recurrence setup"),
      op(0x23, "F 1/x", "decimal recurrence setup"),
      op(0x40, "хП0", "decimal recurrence setup"),
      op(0x0d, "Cx", "decimal recurrence loop entry"),
      op(0xb0, "К хП0", "decimal recurrence loop entry"),
      op(0x60, "Пх0", "decimal recurrence loop entry"),
      jump(0x5e, "F x=0", 5, "decimal recurrence loop guard"),
      op(0x0f, "F Вx", "decimal recurrence scale"),
      op(0x07, "7", "decimal recurrence scale"),
      op(0x15, "F 10^x", "decimal recurrence scale"),
      op(0x20, "F π", "decimal recurrence scale"),
      op(0xde, "К Пхe", "decimal recurrence helper selector"),
      op(0x53, "ПП", "decimal recurrence helper call"),
      formal(0xe1, "decimal recurrence helper call"),
      op(0x01, "1", "decimal recurrence term"),
      op(0x10, "+", "decimal recurrence term"),
      op(0x4e, "хПe", "decimal recurrence accumulator"),
      op(0xde, "К Пхe", "decimal recurrence accumulator"),
      op(0x11, "-", "decimal recurrence accumulator"),
      jump(0x5e, "F x=0", 14, "decimal recurrence carry guard"),
      op(0x6e, "Пхe", "decimal recurrence carry"),
      op(0x0c, "ВП", "decimal recurrence carry"),
      op(0x0b, "/-/", "decimal recurrence carry"),
      op(0x02, "2", "decimal recurrence carry"),
      op(0x34, "К [x]", "decimal recurrence carry"),
      op(0x00, "0", "decimal recurrence reference gap"),
      op(0x25, "F ↻", "decimal recurrence carry"),
      op(0x10, "+", "decimal recurrence carry"),
      op(0x00, "0", "decimal recurrence reference gap"),
      op(0x0e, "В↑", "decimal recurrence carry"),
      op(0x0f, "F Вx", "decimal recurrence carry"),
      op(0x00, "0", "decimal recurrence reference gap"),
      op(0x13, "/", "decimal recurrence division"),
      op(0x0f, "F Вx", "decimal recurrence division"),
      op(0x25, "F ↻", "decimal recurrence division"),
      op(0x34, "К [x]", "decimal recurrence division"),
      op(0xbe, "К хПe", "decimal recurrence division"),
      op(0x12, "×", "decimal recurrence division"),
      op(0x11, "-", "decimal recurrence division"),
      op(0x06, "6", "decimal recurrence normalization"),
      op(0x15, "F 10^x", "decimal recurrence normalization"),
      op(0x12, "×", "decimal recurrence normalization"),
      op(0x6e, "Пхe", "decimal recurrence accumulator update"),
      op(0x0c, "ВП", "decimal recurrence accumulator update"),
      op(0x02, "2", "decimal recurrence accumulator update"),
      op(0x4e, "хПe", "decimal recurrence accumulator update"),
      op(0x10, "+", "decimal recurrence accumulator update"),
      op(0x32, "К ЗН", "decimal recurrence accumulator update"),
      op(0x11, "-", "decimal recurrence accumulator update"),
      jump(0x5e, "F x=0", 11, "decimal recurrence next term"),
      op(0x6e, "Пхe", "decimal recurrence final mantissa"),
      op(0x02, "2", "decimal recurrence final mantissa"),
      op(0x05, "5", "decimal recurrence final mantissa"),
      op(0x10, "+", "decimal recurrence final mantissa"),
      op(0x4e, "хПe", "decimal recurrence final mantissa"),
      op(0x01, "1", "decimal recurrence exponent"),
      op(0x16, "F e^x", "decimal recurrence exponent"),
      op(0x40, "хП0", "decimal recurrence result"),
      op(0x50, "С/П", "decimal recurrence stop"),
    ],
  },
];

function verifiedDecimalSeriesListing(digits: number, counterStart: number): VerifiedDecimalSeriesListing | undefined {
  return VERIFIED_DECIMAL_SERIES_LISTINGS.find(
    (listing) => listing.digits === digits && listing.counterStart === counterStart,
  );
}

export function compileDecimalSeries(ctx: LoweringCtx, statement: Extract<StatementAst, { kind: "decimal_series" }>): void {
    const line = statement.line;
    const listing = verifiedDecimalSeriesListing(statement.digits, statement.counterStart);
    if (listing === undefined) {
      ctx.diagnostics.push(buildDiagnostic(
        "error",
        `No verified decimal recurrence listing for ${statement.digits}-digit precision with counter ${statement.counterStart}. ` +
          `Verified pairs: ${VERIFIED_DECIMAL_SERIES_LISTINGS.map((l) => `(${l.digits}, ${l.counterStart})`).join(", ")}. ` +
          `The hand-tuned recurrence byte sequence must be validated on hardware before a new pair can be added.`,
        line,
      ));
      return;
    }

    for (const instruction of listing.ops) {
      if (instruction.kind === "op") {
        ctx.emitOp(instruction.opcode, instruction.mnemonic, instruction.comment, line);
      } else if (instruction.kind === "jump") {
        ctx.emitJump(instruction.opcode, instruction.mnemonic, instruction.target, instruction.comment, line);
      } else {
        ctx.emitFormalAddress(instruction.opcode, instruction.comment, line);
      }
    }

    ctx.optimizations.push({
      name: "decimal-series-lowering",
      detail: `Lowered decimal recurrence to ${statement.digits}-digit MK-61 program.`,
    });
}

export function compileLocalTerminalElseTail(ctx: LoweringCtx, 
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    if (statement.elseBody === undefined) return false;
    if (ctx.statementsTerminate(statement.thenBody)) return false;
    if (!ctx.statementsTerminate(statement.elseBody)) return false;

    const helper = ctx.ensureTerminalTailHelper(statement.elseBody, line);
    compileCondition(ctx, statement.condition, helper.label, line);
    ctx.compileStatements(statement.thenBody);
    ctx.optimizations.push({
      name: "local-terminal-tail-branch",
      detail: `Branched to a local terminal tail for else path at line ${line}.`,
    });
    return true;
}

export function compileEqualityWithCurrentX(ctx: LoweringCtx, 
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): boolean {
    if (condition.op !== "==" && condition.op !== "!=") return false;
    if (
      condition.right.kind === "identifier" &&
      ctx.xHolds(condition.right.name) &&
      isSimpleStackLoad(condition.left)
    ) {
      compileExpression(ctx, condition.left);
    } else if (
      condition.left.kind === "identifier" &&
      ctx.xHolds(condition.left.name) &&
      isSimpleStackLoad(condition.right)
    ) {
      compileExpression(ctx, condition.right);
    } else {
      return false;
    }
    ctx.emitOp(0x11, "-", "condition compare", line);
    const opcode = condition.op === "==" ? 0x5e : 0x57;
    ctx.emitJump(opcode, getOpcode(opcode).name, falseLabel, `false branch for ${condition.op}`, line);
    ctx.optimizations.push({
      name: "condition-current-x-reuse",
      detail: `Reused the value already in X for equality comparison at line ${line}.`,
    });
    return true;
}

export function compileNegativeZeroThresholdFlow(ctx: LoweringCtx, 
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): boolean {
    const register = ctx.allocation.negativeZeroDegree;
    const threshold = matchNegativeZeroThresholdCondition(condition, ctx.ast);
    if (threshold === undefined) return false;

    const preloadedConstants = new Set(Object.keys(ctx.allocation.constants));
    const selectedCost = estimateNegativeZeroThresholdFlowCost(threshold, preloadedConstants);
    const ordinaryCost = conditionCompileCost(
      selectCheaperEquivalentCondition(condition, ctx.ast, preloadedConstants).condition,
      preloadedConstants,
    );
    if (register === undefined || selectedCost >= ordinaryCost) {
      ctx.candidates.push({
        site: `if@${line}`,
        variant: "negative-zero-threshold-flow",
        steps: selectedCost,
        selected: false,
        reason: selectedCost >= ordinaryCost
          ? `Negative-zero threshold flow estimated at ${selectedCost} cells; ordinary condition was shorter (${ordinaryCost}).`
          : "Negative-zero threshold flow matched, but no compiler-owned negative-zero register was available.",
      });
      return false;
    }

    emitNegativeZeroThresholdRaw(ctx, threshold.value, numberExpression(threshold.bound), register, line);
    const opcode = threshold.truth === "ge" ? 0x57 : 0x5e;
    ctx.emitJump(opcode, getOpcode(opcode).name, falseLabel, `negative-zero false branch for ${condition.op}`, line);
    ctx.optimizations.push({
      name: "negative-zero-threshold-flow",
      detail: `Tested ${conditionToText(condition)} through preloaded ${NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE} in R${register}.`,
    });
    return true;
}

export function emitNegativeZeroThresholdRaw(ctx: LoweringCtx, 
    value: ExpressionAst,
    bound: ExpressionAst,
    register: RegisterName,
    line?: number,
  ): void {
    compileExpression(ctx, divideExpressions(value, bound));
    ctx.emitOp(0x60 + registerIndex(register), `П->X ${register}`, "negative-zero threshold sentinel", line);
    ctx.emitOp(0x14, "X↔Y", "place threshold value above negative-zero sentinel", line);
    ctx.emitOp(0x12, "*", "negative-zero threshold zero-through", line);
    ctx.emitOp(0x0e, "В↑", "normalize negative-zero threshold result", line);
}

export function compileIntFracSharedTail(ctx: LoweringCtx, 
    first: Extract<StatementAst, { kind: "assign" }>,
    second: Extract<StatementAst, { kind: "assign" }>,
  ): boolean {
    const a = matchIntOrFracCall(first.expr);
    const b = matchIntOrFracCall(second.expr);
    if (a === undefined || b === undefined) return false;
    if (a.fn === b.fn) return false;
    if (!expressionEquals(a.arg, b.arg)) return false;
    if (!expressionPureForSubstitution(a.arg)) return false;
    if (first.target === second.target) return false;
    // В↑ + X↔Y add two cells over a single part, so the shared tail only wins
    // when recomputing the operand would cost more than that.
    if (estimateExpressionCost(a.arg) <= 2) return false;

    const intStatement = a.fn === "int" ? first : second;
    const fracStatement = a.fn === "frac" ? first : second;

    compileExpression(ctx, a.arg);
    ctx.emitOp(0x0e, "В↑", "duplicate operand for shared int/frac tail");
    ctx.emitOp(0x34, "К [x]", "int()");
    ctx.emitStore(intStatement.target, `set ${intStatement.target}`, intStatement.line);
    ctx.emitOp(0x14, "XY", "restore saved operand for frac()");
    ctx.emitOp(0x35, "К {x}", "frac()");
    ctx.emitStore(fracStatement.target, `set ${fracStatement.target}`, fracStatement.line);
    ctx.optimizations.push({
      name: "int-frac-shared-tail",
      detail: `Computed ${expressionToIntentText(a.arg)} once and derived int()/frac() through a shared В↑/X↔Y tail.`,
    });
    return true;
}

export function compileOneBasedModuloNormalization(ctx: LoweringCtx,
    assign: Extract<StatementAst, { kind: "assign" }>,
    branch: Extract<StatementAst, { kind: "if" }>,
  ): boolean {
    const match = matchOneBasedModuloNormalization(assign, branch);
    if (match === undefined) return false;
    const field = ctx.findStateField(assign.target);
    if ((field?.min ?? Number.NEGATIVE_INFINITY) < 0) return false;
    const normalized = oneBasedModuloExpression(
      { kind: "identifier", name: assign.target },
      match.width,
    );
    const loweredCost = estimateExpressionCost(normalized) + 1;
    const ordinaryCost = estimateExpressionCost(assign.expr) + 1 + estimateOrdinaryIfCost(branch, ctx.ast);
    if (loweredCost >= ordinaryCost) return false;

    compileExpression(ctx, normalized);
    ctx.emitStore(assign.target, `one-based modulo normalize ${assign.target}`, assign.line);
    ctx.optimizations.push({
      name: "one-based-modulo-normalization",
      detail: `Folded ${assign.target} modulo-${match.width} zero-fix branch into one branchless normalization expression.`,
    });
    return true;
}

function matchOneBasedModuloNormalization(
    assign: Extract<StatementAst, { kind: "assign" }>,
    branch: Extract<StatementAst, { kind: "if" }>,
  ): { width: number } | undefined {
    if (branch.elseBody !== undefined || branch.thenBody.length !== 1) return undefined;
    if (
      branch.condition.op !== "<=" ||
      branch.condition.left.kind !== "identifier" ||
      branch.condition.left.name !== assign.target ||
      numericLiteralValue(branch.condition.right) !== 0
    ) return undefined;
    const thenAssign = branch.thenBody[0]!;
    if (thenAssign.kind !== "assign" || thenAssign.target !== assign.target) return undefined;
    const width = matchModuloRemainderAssignment(assign.target, assign.expr);
    if (width === undefined) return undefined;
    const delta = matchNumericSelfUpdate(assign.target, thenAssign.expr);
    if (delta !== width) return undefined;
    return { width };
}

function matchModuloRemainderAssignment(target: string, expr: ExpressionAst): number | undefined {
    const product = matchBinaryNumericOperand(expr, "*");
    if (product === undefined) return undefined;
    const { other, value: width } = product;
    if (!validModuloWidth(width)) return undefined;
    if (other.kind !== "call" || other.callee.toLowerCase() !== "frac" || other.args.length !== 1) return undefined;
    const divided = other.args[0]!;
    if (divided.kind !== "binary" || divided.op !== "/") return undefined;
    if (numericLiteralValue(divided.right) !== width) return undefined;
    const dividend = divided.left;
    if (dividend.kind !== "call" || dividend.callee.toLowerCase() !== "int" || dividend.args.length !== 1) return undefined;
    const source = dividend.args[0]!;
    return source.kind === "identifier" && source.name === target ? width : undefined;
}

function matchBinaryNumericOperand(expr: ExpressionAst, op: "*" | "/"): { other: ExpressionAst; value: number } | undefined {
    if (expr.kind !== "binary" || expr.op !== op) return undefined;
    const leftValue = numericLiteralValue(expr.left);
    if (leftValue !== undefined) return { other: expr.right, value: leftValue };
    const rightValue = numericLiteralValue(expr.right);
    if (rightValue !== undefined) return { other: expr.left, value: rightValue };
    return undefined;
}

function validModuloWidth(width: number): boolean {
    return Number.isSafeInteger(width) && width > 1;
}

function oneBasedModuloExpression(expr: ExpressionAst, width: number): ExpressionAst {
    const shifted = addExpressions(intExpression(expr), numberExpression(width - 1));
    return addExpressions(
      multiplyExpressions(
        fracExpression(divideExpressions(shifted, numberExpression(width))),
        numberExpression(width),
      ),
      numberExpression(1),
    );
}

export function compileUnitDecrement(ctx: LoweringCtx, statement: Extract<StatementAst, { kind: "assign" }>): boolean {
    if (!isUnitDecrementExpression(statement.target, statement.expr)) return false;
    if (statement.target.startsWith(PACKED_COUNTER_PREFIX)) return false;
    const register = ctx.allocation.registers[statement.target];
    if (register === undefined) return false;
    // A standalone `x--` writes a value that later statements observe directly
    // (e.g. `if x == 0`, `if x <= 0`, `while x != 0`, or `show(x)`). F Lx is not a
    // sound unit decrement for that: it clamps a positive counter at 1 instead of
    // reaching 0, so any such observation breaks. The compact form is the
    // indirect pre-decrement through R0..R3. The hardware zero-underflow edge
    // writes a negative sentinel; terminal underflow fusions handle that edge
    // separately by proving the negative path cannot return.
    if (
      isPredecrementIndirectRegister(register) &&
      (
        targetRangeFitsIndirectDecrement(ctx, statement.target) ||
        ctx.loweringOptions.indirectUnderflowDecrement === true &&
        targetRangeFitsTerminalUnderflowDecrement(ctx, statement.target) &&
          ctx.terminalUnderflowUnitDecrementProtected(statement)
      )
    ) {
      return emitIndirectUnitDecrement(ctx, statement.target, register, `decrement ${statement.target}`, statement.line);
    }
    // Otherwise fall back to the generic recall/-1/store decrement, which is always
    // correct (the fused `x--; if x==0|<0` patterns keep using F Lx separately).
    return false;
}

function targetRangeFitsIndirectDecrement(ctx: LoweringCtx, target: string): boolean {
    const field = ctx.findStateField(target);
    if (field?.min === undefined || field.max === undefined) return false;
    // Default lowering keeps the incidental recall inside the register file.
    // Wider counters can still use the pre-decrement through the speculative
    // terminal-underflow proof, where the full-program selector must approve the
    // surrounding layout.
    return field.type === "range" && field.min >= 1 && field.max <= 14;
}

function targetRangeFitsTerminalUnderflowDecrement(ctx: LoweringCtx, target: string): boolean {
    const field = ctx.findStateField(target);
    if (field?.min === undefined || field.max === undefined) return false;
    return field.type === "range" && field.min >= 0;
}

function emitIndirectUnitDecrement(ctx: LoweringCtx, target: string, register: RegisterName, comment: string, line: number): boolean {
    if (!isPredecrementIndirectRegister(register)) return false;
    ctx.emitOp(0xd0 + registerIndex(register), `К П->X ${register}`, comment, line);
    ctx.currentXVariable = undefined;
    ctx.currentXAliases.clear();
    ctx.currentXKnownZero = false;
    ctx.optimizations.push({
      name: "indirect-incdec-counter",
      detail: `Decremented ${target} by using ${getOpcode(0xd0 + registerIndex(register)).name}'s pre-decrement side effect at line ${line}.`,
    });
    return true;
}

export function compileUnitIncrement(ctx: LoweringCtx, statement: Extract<StatementAst, { kind: "assign" }>): boolean {
    if (!isUnitIncrementExpression(statement.target, statement.expr)) return false;
    if (!targetRangeFitsIndirectIncrement(ctx, statement.target)) return false;
    return emitIndirectUnitIncrement(ctx, statement.target, `increment ${statement.target}`, statement.line);
}

export function emitIndirectUnitIncrement(ctx: LoweringCtx, target: string, comment: string, line: number): boolean {
    const register = ctx.allocation.registers[target];
    if (register === undefined || !isPreincrementIndirectRegister(register)) return false;
    ctx.emitOp(0xd0 + registerIndex(register), `К П->X ${register}`, comment, line);
    ctx.currentXVariable = undefined;
    ctx.currentXAliases.clear();
    ctx.currentXKnownZero = false;
    ctx.optimizations.push({
      name: "indirect-incdec-counter",
      detail: `Incremented ${target} by using ${getOpcode(0xd0 + registerIndex(register)).name}'s pre-increment side effect at line ${line}.`,
    });
    return true;
}

function targetRangeFitsIndirectIncrement(ctx: LoweringCtx, target: string): boolean {
    const field = ctx.findStateField(target);
    if (field?.min === undefined || field.max === undefined) return false;
    return field.type === "range" && field.min >= 0;
}

export function emitErrorStopOpcode(ctx: LoweringCtx, comment: string, line: number, raw = false): void {
    // К ÷ is a one-cell ЕГГ0Г trap (X kept, X->X1, PC becomes addr+2) that,
    // unlike the equivalent service codes 2B..2E, can be entered straight from
    // the keyboard (К then ÷), so the manual key/patch export needs no
    // service-mode sequence. The mnemonic is spelled with the real ÷ keycap
    // because this cell is literally the keystroke to press, not a symbolic
    // divide.
    ctx.emitOp(0x29, "К ÷", comment, line, raw);
}

export function compileBlockCall(ctx: LoweringCtx, blockName: string, line: number): void {
    const proc = ctx.ast.procs.find((candidate) => candidate.name === blockName);
    if (proc) {
      if (ctx.inlineProcNames.has(proc.name)) {
        ctx.compileStatements(proc.body);
        const uses = ctx.procCallCounts.get(proc.name) ?? 0;
        ctx.optimizations.push({
          name: uses === 1 ? "single-use-rule-inline" : "size-model-rule-inline",
          detail: uses === 1
            ? `Inlined single-use rule ${proc.name} at line ${line}.`
            : `Inlined ${uses}-use rule ${proc.name} because it is smaller than a ПП/В/О subroutine.`,
        });
        return;
      }
      // A value-returning function ends in В/О (it returns to the caller), so it
      // must be reached with ПП even though its body "terminates"; only true
      // terminal rules (halt/jump-away) may use the direct-jump tail call.
      if (ctx.statementsTerminate(proc.body) && !ctx.functionProcs.has(proc.name)) {
        ctx.emitJump(0x51, "БП", proc.name, `terminal rule ${proc.name}`, line);
        ctx.optimizations.push({
          name: "terminal-rule-tail-call",
          detail: `Compiled terminal rule ${proc.name} as a direct jump instead of a subroutine call.`,
        });
        return;
      }
      if (ctx.currentXKnownZero && packedLineFamilyScoreProc(ctx, proc) !== undefined) {
        const bankSelectors = ctx.snapshotBankSelectorCache();
        ctx.emitJump(
          0x53,
          "ПП",
          packedLineFamilyScoreZeroEntryLabel(proc.name),
          `proc call ${proc.name} zero-accumulator entry`,
          line,
        );
        ctx.restoreBankSelectorCacheAfterCall(proc.name, bankSelectors);
        const returnX = ctx.procReturnXVariable(proc);
        if (returnX !== undefined) {
          ctx.currentXVariable = returnX;
          ctx.currentXAliases = new Set([returnX]);
          ctx.currentXKnownZero = false;
        }
        ctx.optimizations.push({
          name: "zero-accumulator-proc-entry",
          detail: `Entered ${proc.name} after its zero literal because X already held a proved zero at line ${line}.`,
        });
        return;
      }
      const bankSelectors = ctx.snapshotBankSelectorCache();
      ctx.emitJump(0x53, "ПП", proc.name, `proc call ${proc.name}`, line);
      ctx.restoreBankSelectorCacheAfterCall(proc.name, bankSelectors);
      const returnX = ctx.procReturnXVariable(proc);
      if (returnX !== undefined) {
        ctx.currentXVariable = returnX;
        ctx.currentXAliases = new Set([returnX]);
        ctx.currentXKnownZero = false;
      }
      ctx.optimizations.push({
        name: "proc-call-lowering",
        detail: `Compiled call to rule ${proc.name} as ПП/В/О subroutine.`,
      });
      if (returnX !== undefined) {
        ctx.optimizations.push({
          name: "proc-return-x-reuse",
          detail: `Tracked ${returnX} in X after returning from rule ${proc.name}.`,
        });
      }
      return;
    }
    ctx.diagnostics.push(buildDiagnostic("error", `Unknown block '${blockName}'.`, line));
}

export function compileRawStatement(ctx: LoweringCtx, statement: Extract<StatementAst, { kind: "core" }>): void {
    const inputs = statement.inputs ?? [];
    const outputs = statement.outputs ?? [];

    for (const input of orderRawInputs(inputs)) {
      compileExpression(ctx, input.expr);
    }

    compileRawLines(ctx, statement.lines, statement.strict ?? false);

    for (const output of outputs) {
      ctx.emitStore(output.target, `raw returns ${output.slot}`, output.line);
    }

    if (
      inputs.length > 0 ||
      outputs.length > 0 ||
      statement.clobbers !== undefined ||
      statement.preserves !== undefined
    ) {
      ctx.optimizations.push({
        name: "raw-block-contract",
        detail: formatRawContractDetail(statement),
      });
    }
}

export function compileRawLines(ctx: LoweringCtx, 
    lines: Array<{ text: string; line: number }>,
    strict = false,
  ): void {
    for (const line of lines) {
      if (line.text.endsWith(":")) {
        ctx.emitLabel(line.text.slice(0, -1));
        continue;
      }
      const parsed = parseRawInstruction(line.text);
      if (!parsed) {
        ctx.diagnostics.push({
          level: strict ? "error" : "warning",
          message: `Unknown raw instruction '${line.text}'`,
          line: line.line,
        });
        continue;
      }
      ctx.emitOp(parsed.opcode, parsed.mnemonic, parsed.comment, line.line, true);
      if (parsed.opcode === 0x5f) {
        ctx.optimizations.push({
          name: "raw-display-5f",
          detail: `Raw block uses opcode 5F as a display-state transform at line ${line.line}.`,
        });
      }
      if (parsed.formalTargetOpcode !== undefined) {
        ctx.emitFormalAddress(parsed.formalTargetOpcode, parsed.comment ?? parsed.mnemonic, line.line);
      } else if (parsed.target !== undefined) {
        ctx.emitAddress(parsed.target, parsed.comment ?? parsed.mnemonic, line.line);
      }
    }
}
