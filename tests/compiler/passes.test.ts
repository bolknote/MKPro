import { describe, expect, it } from "vitest";
import { lowerIrToMachine } from "../../src/core/ir.ts";
import { arithmeticIfPass } from "../../src/core/passes/arithmetic-if.ts";
import { branchTargetXReuse } from "../../src/core/passes/branch-target-x-reuse.ts";
import { deadCodeAfterHalt } from "../../src/core/passes/dead-code-after-halt.ts";
import { deadProcElimination } from "../../src/core/passes/dead-proc-elimination.ts";
import { deadStoreElimination } from "../../src/core/passes/dead-store-elimination.ts";
import { duplicateFailureTail } from "../../src/core/passes/duplicate-failure-tail.ts";
import { flowXReuse } from "../../src/core/passes/flow-x-reuse.ts";
import { indirectSelectorIntegerPart } from "../../src/core/passes/indirect-selector-integer-part.ts";
import { indirectMemoryTable, stableIndirectFlow } from "../../src/core/passes/indirect-addressing.ts";
import { jumpThread } from "../../src/core/passes/jump-thread.ts";
import { jumpToNextThreading } from "../../src/core/passes/jump-to-next.ts";
import { lastXReuse } from "../../src/core/passes/last-x-reuse.ts";
import { computeLiveness } from "../../src/core/passes/liveness-analysis.ts";
import { preShiftStackLift } from "../../src/core/passes/pre-shift-stack-lift.ts";
import { preloadedIndirectFlow, runtimeIndirectCallFlow } from "../../src/core/passes/preloaded-indirect-flow.ts";
import { r0FractionalSentinel } from "../../src/core/passes/r0-fractional-sentinel.ts";
import { redundantPrologueElimination } from "../../src/core/passes/redundant-prologue.ts";
import { computeNonOverlappingRegisterMapping, registerCoalesce } from "../../src/core/passes/register-coalesce.ts";
import { returnSuffixGadget } from "../../src/core/passes/return-suffix-gadget.ts";
import { sharedTerminalTail } from "../../src/core/passes/shared-terminal-tail.ts";
import { sharedStraightLineHelper } from "../../src/core/passes/shared-straight-line-helper.ts";
import { storeRecallPeephole } from "../../src/core/passes/store-recall-peephole.ts";
import { tailBranchInversion } from "../../src/core/passes/tail-branch-inversion.ts";
import { tailCallLowering } from "../../src/core/passes/tail-call.ts";
import { vpSplice } from "../../src/core/passes/vp-splice.ts";
import { vpX2Peephole } from "../../src/core/passes/vp-x2-peephole.ts";
import { x2DeadRestoreBeforeOverwrite } from "../../src/core/passes/x2-dead-restore-before-overwrite.ts";
import { x2HiddenTempRestore } from "../../src/core/passes/x2-hidden-temp-restore.ts";
import { x2LiteralRestore } from "../../src/core/passes/x2-literal-restore.ts";
import { x2NoopRestore } from "../../src/core/passes/x2-noop-restore.ts";
import {
  analyzeRecallRemoval,
  analyzeX2StackEffect,
  analyzeX2VpShapeContext,
  canonicalStructuralRestoreSourceKeyFacts,
  computeX2ImmediateSyncStates,
  computeX2RegisterStates,
  computeX2ValueStates,
  directReturnAnalysisContext,
  effectiveVisibleXStateShape,
  joinX2ValueDataflowStates,
  parseX2ShapeFact,
  recallValueProof,
  transferX2RegisterStateForEdge,
  transferX2ValueStateForEdge,
  x2CanUseClosedSignChangeDotSourceAt,
  x2CanUseSourceDotRestoreAt,
  x2CanUseVpDotRestoreAt,
  x2CanonicalShapeFact,
  x2ClosedDecimalExponentDisplayShapeFact,
  x2ClosedExponentDisplayShapeFact,
  x2ClosedStructuralExponentMantissaShapeFact,
  x2ExponentMantissaSignChangedShapeFact,
  x2ExponentShapeFactFromMantissaFact,
  x2ExponentSignChangedShapeFact,
  x2JoinedVpEntryMantissaSources,
  x2JoinedVpEntrySignShapeSources,
  x2MantissaShapeFactFromModel,
  x2MantissaSignChangedShapeFact,
  x2HasOnlyRestoreGapBeforeVp,
  x2ReplacementDotHasOnlyRestoreGapBeforeVp,
  x2RestoreGapBeforeVp,
  x2NextHardX2OverwriteIndex,
  x2NextStackShiftingProducerIndex,
  x2NextStackPreservingReturnX2SyncIndex,
  x2NextXPreservingX2SyncIndex,
  x2PreviousHardX2OverwriteIndex,
  x2PreviousStackPreservingReturnX2SyncIndex,
  x2PreviousXPreservingX2SyncIndex,
  x2NormalizedDecimalRestoreGapIsFreeStanding,
  x2StateCanDiscardRestoreRunBeforeProvedVp,
  x2StateHasOnlyDotSafeStructuralMantissaX2,
  x2StateHasSameClosedSignChangeSourceInXAndX2,
  x2StateHasSameDotRestoreValueInXAndX2,
  x2StateHasSameDotSafeStructuralMantissaInXAndX2,
  x2StateHasSameStructuralShapeInXAndX2,
  x2StateHasStructuralShapeX2,
  x2StateHasVpDotSafeStructuralContextX2,
  x2StateIsClosedPlainContext,
  x2ShapeDataModelForFact,
  x2ShapeFactRestoredVisibleDecimal,
  x2ShapeFactSafety,
  x2RestoredDisplayShapeFacts,
  x2RestoredDisplayShapeFactsFromSourceKey,
  x2RestoredDisplaySourceKeyShapeFacts,
  x2ShapeSetHasExactIntegerDisplay,
  x2ShapeSetHasExactNonNegativeDisplay,
  x2ShapeSetHasExactNonNegativeIntegerDisplay,
  x2ShapeSetHasOnlyDotSafeStructuralMantissas,
  x2ShapeSetRestoredVisibleDecimals,
  x2ShapeSetsHaveSameDecimalDisplayShape,
  x2ShapeSetsHaveSameDotSafeStructuralMantissa,
  x2ShapeSetsHaveSameDotSafeDecimal,
  x2ShapeSetsHaveSameRestoredDisplayShape,
  x2ShapeSetsHaveSameStructuralShape,
  x2ShapeSetSafety,
  x2SignChangedSharedStructuralShapeFacts,
  x2StructuralRestoreShapeFacts,
  x2StructuralMantissaAppendDigitsShapeFact,
  x2StructuralMantissaConcatShapeFacts,
  x2StructuralMantissaFirstDigitSpliceShapeFact,
  x2StructuralMantissaShiftShapeFact,
  x2StatesHaveSameExplicitVpEntrySignSource,
  x2StatesHaveSameVpEntrySource,
  x2StatesHaveSameVpEntrySignSource,
  x2StateHasUnsafeDotRestoreShapeX2,
  x2ValueSetHasFact,
  x2ValueSetHasIntersection,
  x2ValueSetHasRestoredVisibleDecimal,
  x2ValueShapeSetsHaveSameDotSafeDecimal,
  x2ValueShapeSetsHaveSameDotSafeStructuralMantissa,
  x2ValueShapeSetsHaveSameRestoredDisplayShape,
  x2ValueShapeSetRestoredVisibleDecimals,
  x2ValueShapeSetHasRestoredVisibleDecimal,
  x2ValueShapeSetsHaveSameRestoredVisibleDecimal,
  x2ValueSetsHaveSameRestoredVisibleDecimal,
  type X2ShapeFact,
  type X2ShapeSet,
  type X2ValueDataflowState,
  type X2ValueFact,
  type X2ValueSet,
} from "../../src/core/passes/helpers.ts";
import type { IrOp, RegisterName } from "../../src/core/types.ts";

const noopOptions = { delivery: "manual" as const, budget: 105, analysis: false };
const ctx = { options: noopOptions };

const REGISTER_INDEX: Record<RegisterName, number> = {
  "0": 0,
  "1": 1,
  "2": 2,
  "3": 3,
  "4": 4,
  "5": 5,
  "6": 6,
  "7": 7,
  "8": 8,
  "9": 9,
  a: 10,
  b: 11,
  c: 12,
  d: 13,
  e: 14,
};

function store(register: RegisterName): IrOp {
  return {
    kind: "store",
    register,
    opcode: 0x40 + REGISTER_INDEX[register],
    meta: { mnemonic: `X->П ${register}` },
  };
}

function recall(register: RegisterName, comment?: string): Extract<IrOp, { kind: "recall" }> {
  return {
    kind: "recall",
    register,
    opcode: 0x60 + REGISTER_INDEX[register],
    meta: comment === undefined
      ? { mnemonic: `П->X ${register}` }
      : { mnemonic: `П->X ${register}`, comment },
  };
}

function plain(opcode: number, mnemonic: string): Extract<IrOp, { kind: "plain" }> {
  return { kind: "plain", opcode, meta: { mnemonic } };
}

function jump(target: string): IrOp {
  return {
    kind: "jump",
    target,
    opcode: 0x51,
    meta: { mnemonic: "БП" },
    targetMeta: {},
  };
}

function cjump(target: string): IrOp {
  return {
    kind: "cjump",
    condition: "==0",
    target,
    opcode: 0x5e,
    meta: { mnemonic: "F x=0" },
    targetMeta: {},
  };
}

function loop(target: string): IrOp {
  return {
    kind: "loop",
    counter: "L0",
    target,
    opcode: 0x5d,
    meta: { mnemonic: "F L0" },
    targetMeta: {},
  };
}

function numericCjump(target: number): IrOp {
  return {
    kind: "cjump",
    condition: "==0",
    target,
    opcode: 0x5e,
    meta: { mnemonic: "F x=0" },
    targetMeta: {},
  };
}

function numericJump(target: number): IrOp {
  return {
    kind: "jump",
    target,
    opcode: 0x51,
    meta: { mnemonic: "БП" },
    targetMeta: {},
  };
}

function numericCall(target: number): IrOp {
  return {
    kind: "call",
    target,
    opcode: 0x53,
    meta: { mnemonic: "ПП" },
    targetMeta: {},
  };
}

function orphanAddress(target = 0): IrOp {
  return { kind: "orphan-address", target, meta: { comment: "test address gap" } };
}

function call(target: string): IrOp {
  return {
    kind: "call",
    target,
    opcode: 0x53,
    meta: { mnemonic: "ПП", comment: `proc call ${target}` },
    targetMeta: {},
  };
}

function indirectRecall(register: RegisterName): IrOp {
  return {
    kind: "indirect-recall",
    register,
    opcode: 0xd0 + REGISTER_INDEX[register],
    meta: { mnemonic: `К П->X ${register}` },
  };
}

function knownTargetIndirectRecall(register: RegisterName, target: RegisterName): IrOp {
  return {
    kind: "indirect-recall",
    register,
    opcode: 0xd0 + REGISTER_INDEX[register],
    meta: { mnemonic: `К П->X ${register}`, comment: `indirect-memory-target=${target}` },
  };
}

function indirectStore(register: RegisterName): IrOp {
  return {
    kind: "indirect-store",
    register,
    opcode: 0xb0 + REGISTER_INDEX[register],
    meta: { mnemonic: `К X->П ${register}` },
  };
}

function knownTargetIndirectStore(register: RegisterName, target: RegisterName): IrOp {
  return {
    kind: "indirect-store",
    register,
    opcode: 0xb0 + REGISTER_INDEX[register],
    meta: { mnemonic: `К X->П ${register}`, comment: `indirect-memory-target=${target}` },
  };
}

function indirectJump(register: RegisterName): IrOp {
  return {
    kind: "indirect-jump",
    register,
    opcode: 0x80 + REGISTER_INDEX[register],
    meta: { mnemonic: `К БП ${register}` },
  };
}

function knownTargetIndirectJump(register: RegisterName, target: number): IrOp {
  return {
    kind: "indirect-jump",
    register,
    opcode: 0x80 + REGISTER_INDEX[register],
    meta: { mnemonic: `К БП ${register}`, comment: `indirect-target=${target}` },
  };
}

function knownTargetIndirectCall(register: RegisterName, target: number): IrOp {
  return {
    kind: "indirect-call",
    register,
    opcode: 0xa0 + REGISTER_INDEX[register],
    meta: { mnemonic: `К ПП ${register}`, comment: `indirect-target=${target}` },
  };
}

function indirectCjump(register: RegisterName): IrOp {
  return {
    kind: "indirect-cjump",
    condition: "==0",
    register,
    opcode: 0xe0 + REGISTER_INDEX[register],
    meta: { mnemonic: `К x=0 ${register}` },
  };
}

function knownTargetIndirectCjump(register: RegisterName, target: number): IrOp {
  return {
    kind: "indirect-cjump",
    condition: "==0",
    register,
    opcode: 0xe0 + REGISTER_INDEX[register],
    meta: { mnemonic: `К x=0 ${register}`, comment: `indirect-target=${target}` },
  };
}

function markedFractionalIndirectRecall(register: RegisterName, source = "pos"): IrOp {
  return {
    kind: "indirect-recall",
    register,
    opcode: 0xd0 + REGISTER_INDEX[register],
    meta: { mnemonic: `К П->X ${register}`, comment: `indexed recall cells; indirect-selector-integer-part=${source}` },
  };
}

function label(name: string): IrOp {
  return { kind: "label", name };
}

function procStart(name: string): IrOp {
  return { kind: "label", name, procedureBoundary: "start", procedureName: name };
}

function procEnd(name: string): IrOp {
  return {
    kind: "label",
    name: `\0proc_end_${name}`,
    procedureBoundary: "end",
    procedureName: name,
    hidden: true,
  };
}

function halt(): IrOp {
  return { kind: "stop", opcode: 0x50, semantic: "halt", meta: { mnemonic: "С/П" } };
}

function pause(): IrOp {
  return { kind: "stop", opcode: 0x50, semantic: "pause", meta: { mnemonic: "С/П", comment: "pause" } };
}

function ret(): IrOp {
  return { kind: "return", opcode: 0x52, meta: { mnemonic: "В/О" } };
}

function machineCellCount(ops: readonly IrOp[]): number {
  return lowerIrToMachine(ops).filter((item) => item.kind !== "label").length;
}

function registerStateText(state: ReadonlySet<RegisterName> | undefined): string[] | undefined {
  return state === undefined ? undefined : [...state].sort();
}

function x2ValueStateText(state: X2ValueSet | undefined): string[] | undefined {
  return state === undefined ? undefined : [...state].sort();
}

function x2ShapeStateText(state: X2ShapeSet | undefined): string[] | undefined {
  return state === undefined ? undefined : [...state].sort();
}

function x2EntryStateText(state: ReturnType<typeof computeX2ValueStates>[number]): string | undefined {
  if (state === undefined) return undefined;
  switch (state.entry.kind) {
    case "closed":
    case "unknown":
      return state.entry.kind;
    case "open":
      return `open:${[...state.entry.raw].sort().join("|")}`;
    case "exponent":
      return `exponent:${[...state.entry.mantissa].sort().join("|")}:${
        [...state.entry.exponent].sort().join("|")
      }`;
  }
}

function x2VpContextStateText(state: ReturnType<typeof computeX2ValueStates>[number]): string | undefined {
  if (state === undefined) return undefined;
  const context = state.vpContext;
  if (context === undefined || context.kind === "none") return "none";
  if (context.kind === "unknown") return "unknown";
  return `exponent:${[...context.mantissa].sort().join("|")}:${[...context.exponent].sort().join("|")}`;
}

function x2StructuralVpContextStateText(state: ReturnType<typeof computeX2ValueStates>[number]): string | undefined {
  if (state === undefined) return undefined;
  const context = state.structuralVpContext;
  if (context === undefined || context.kind === "none") return "none";
  if (context.kind === "unknown") return "unknown";
  return `exponent:${[...context.mantissa].sort().join("|")}:${[...context.exponent].sort().join("|")}`;
}

function x2VpEntryShapeText(state: ReturnType<typeof computeX2ValueStates>[number]): string[] | undefined {
  return state === undefined ? undefined : x2ShapeStateText(state.vpEntryShape);
}

function x2VpEntrySignShapeText(state: ReturnType<typeof computeX2ValueStates>[number]): string[] | undefined {
  return state === undefined ? undefined : x2ShapeStateText(state.vpEntrySignShape);
}

function x2VpEntryMantissaText(state: ReturnType<typeof computeX2ValueStates>[number]): string[] | undefined {
  return state === undefined || state.vpEntryMantissa === undefined
    ? undefined
    : [...state.vpEntryMantissa].sort();
}

function x2VpEntrySignMantissaText(state: ReturnType<typeof computeX2ValueStates>[number]): string[] | undefined {
  return state === undefined || state.vpEntrySignMantissa === undefined
    ? undefined
    : [...state.vpEntrySignMantissa].sort();
}

describe("ir passes on synthetic programs", () => {
  it("x2 register dataflow tracks register-valued X2 through preserving operations", () => {
    const program: IrOp[] = [
      recall("2"),
      plain(0x20, "F pi"),
      store("3"),
      knownTargetIndirectRecall("7", "5"),
      plain(0x0d, "Cx"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[1])).toEqual(["2"]);
    expect(registerStateText(states[2])).toEqual(["2"]);
    expect(registerStateText(states[3])).toEqual(["2"]);
    expect(registerStateText(states[4])).toEqual(["5"]);
    expect(registerStateText(states[5])).toEqual([]);
  });

  it("x2 register dataflow creates aliases when storing a value already shared by X and X2", () => {
    const program: IrOp[] = [
      recall("2"),
      store("3"),
      plain(0x20, "F pi"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[1])).toEqual(["2"]);
    expect(registerStateText(states[2])).toEqual(["2", "3"]);
    expect(registerStateText(states[3])).toEqual(["2", "3"]);
  });

  it("x2 register dataflow syncs known X through F* empty opcodes", () => {
    const program: IrOp[] = [
      recall("2"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[1])).toEqual(["2"]);
    expect(registerStateText(states[2])).toEqual(["2"]);
    expect(registerStateText(states[3])).toEqual(["2"]);
  });

  it("x2 register dataflow syncs known X through stack lift", () => {
    const program: IrOp[] = [
      recall("2"),
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[1])).toEqual(["2"]);
    expect(registerStateText(states[2])).toEqual(["2"]);
    expect(registerStateText(states[3])).toEqual(["2"]);
  });

  it("x2 register dataflow carries Y aliases through Y->X and later stack lift", () => {
    const program: IrOp[] = [
      recall("2"),
      plain(0x0e, "В↑"),
      plain(0x35, "К {x}"),
      plain(0x3e, "Y->X"),
      plain(0x0e, "В↑"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[3])).toEqual(["2"]);
    expect(registerStateText(states[4])).toEqual(["2"]);
    expect(registerStateText(states[5])).toEqual(["2"]);
  });

  it("x2 register dataflow drops stale Y aliases after a register overwrite", () => {
    const program: IrOp[] = [
      recall("2"),
      plain(0x20, "F pi"),
      store("2"),
      plain(0x3e, "Y->X"),
      plain(0x0e, "В↑"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[2])).toEqual(["2"]);
    expect(registerStateText(states[3])).toEqual([]);
    expect(registerStateText(states[5])).toEqual([]);
  });

  it("x2 register dataflow drops aliases when a register is overwritten from non-X2 X", () => {
    const program: IrOp[] = [
      recall("2"),
      store("3"),
      plain(0x35, "К {x}"),
      store("3"),
      plain(0x20, "F pi"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[2])).toEqual(["2", "3"]);
    expect(registerStateText(states[3])).toEqual(["2", "3"]);
    expect(registerStateText(states[4])).toEqual(["2"]);
    expect(registerStateText(states[5])).toEqual(["2"]);
  });

  it("x2 register dataflow is path-sensitive for direct conditionals", () => {
    const program: IrOp[] = [
      recall("2"),
      cjump("jumped"),
      plain(0x20, "F pi"),
      jump("done"),
      label("jumped"),
      plain(0x20, "F pi"),
      label("done"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[2])).toEqual(["2"]);
    expect(registerStateText(states[4])).toEqual(["2"]);
    expect(registerStateText(states[5])).toEqual(["2"]);
  });

  it("x2 register edge projection models direct conditional fallthrough sync", () => {
    const projectedFallthrough = transferX2RegisterStateForEdge(
      { x: new Set<RegisterName>(["4"]), x2: new Set<RegisterName>(["2"]) },
      cjump("skip"),
      "fallthrough",
    );
    const projectedJump = transferX2RegisterStateForEdge(
      { x: new Set<RegisterName>(["4"]), x2: new Set<RegisterName>(["2"]) },
      cjump("skip"),
      "jump",
    );

    expect(registerStateText(projectedFallthrough)).toEqual(["4"]);
    expect(registerStateText(projectedJump)).toEqual(["2"]);
  });

  it("x2 register edge projection removes loop counters and unstable indirect selectors", () => {
    const projectedLoopJump = transferX2RegisterStateForEdge(
      { x: new Set<RegisterName>(["0", "4"]), x2: new Set<RegisterName>(["0", "4"]) },
      loop("done"),
      "jump",
    );
    const projectedIndirectFallthrough = transferX2RegisterStateForEdge(
      { x: new Set<RegisterName>(["1", "4"]), x2: new Set<RegisterName>(["1", "4"]) },
      knownTargetIndirectCjump("1", 7),
      "fallthrough",
    );
    const projectedIndirectJump = transferX2RegisterStateForEdge(
      { x: new Set<RegisterName>(["1", "4"]), x2: new Set<RegisterName>(["1", "4"]) },
      knownTargetIndirectCjump("1", 7),
      "jump",
    );

    expect(registerStateText(projectedLoopJump)).toEqual(["4"]);
    expect(registerStateText(projectedIndirectFallthrough)).toEqual(["1", "4"]);
    expect(registerStateText(projectedIndirectJump)).toEqual(["4"]);
  });

  it("x2 register dataflow follows proved stable indirect flow targets", () => {
    const program: IrOp[] = [
      recall("4"),
      knownTargetIndirectJump("8", 3),
      halt(),
      label("tail"),
      plain(0x20, "F pi"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[4])).toEqual(["4"]);
    expect(registerStateText(states[5])).toEqual(["4"]);
  });

  it("x2 register dataflow preserves X2 across stable indirect conditionals", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x35, "К {x}"),
      store("2"),
      knownTargetIndirectCjump("8", 7),
      plain(0x20, "F pi"),
      jump("done"),
      label("target"),
      plain(0x20, "F pi"),
      label("done"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[4])).toEqual(["1"]);
    expect(registerStateText(states[7])).toEqual(["1"]);
  });

  it("x2 register dataflow preserves indirect conditional fallthrough while dropping a mutated jump selector", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x35, "К {x}"),
      store("2"),
      knownTargetIndirectCjump("1", 7),
      plain(0x20, "F pi"),
      jump("done"),
      label("target"),
      plain(0x20, "F pi"),
      label("done"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[4])).toEqual(["1"]);
    expect(registerStateText(states[7])).toEqual([]);
  });

  it("x2 register dataflow drops only the mutated selector fact on indirect flow", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      knownTargetIndirectJump("1", 4),
      halt(),
      label("tail"),
      plain(0x20, "F pi"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[5])).toEqual(["2"]);
  });

  it("x2 register dataflow clears the mutated selector fact across indirect flow", () => {
    const program: IrOp[] = [
      recall("1"),
      knownTargetIndirectJump("1", 3),
      halt(),
      label("tail"),
      plain(0x20, "F pi"),
      halt(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[4])).toEqual([]);
    expect(registerStateText(states[5])).toEqual([]);
  });

  it("x2 register dataflow syncs X2 through direct subroutine returns", () => {
    const program: IrOp[] = [
      label("main"),
      call("load"),
      plain(0x20, "F pi"),
      halt(),
      label("load"),
      recall("4"),
      ret(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[2])).toEqual(["4"]);
    expect(registerStateText(states[3])).toEqual(["4"]);
  });

  it("x2 register dataflow clears return-time X2 when returned X is unknown", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x35, "К {x}"),
      call("noop"),
      plain(0x20, "F pi"),
      halt(),
      label("noop"),
      ret(),
    ];

    const states = computeX2RegisterStates(program);

    expect(registerStateText(states[2])).toEqual(["1"]);
    expect(registerStateText(states[3])).toEqual([]);
    expect(registerStateText(states[4])).toEqual([]);
  });

  it("x2 value dataflow tracks const zero through X-preserving gaps", () => {
    const program: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x54, "К НОП"),
      plain(0x55, "К 1"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[1]?.x)).toEqual(["decimal:0:normalized"]);
    expect(x2ValueStateText(states[1]?.x2)).toEqual(["decimal:0:normalized"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:0:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:0:normalized"]);
  });

  it("x2 value dataflow tracks normalized decimal digit runs", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toEqual(["decimal:12:normalized"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["decimal:12:normalized"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:12:normalized", "reg:2"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:12:normalized", "reg:2"]);
  });

  it("x2 value dataflow reads decimal preload facts from recall metadata", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.1020088E14"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[1]?.x)).toEqual([
      "decimal:810200880000000:normalized",
      "reg:2",
    ]);
    expect(x2ValueStateText(states[1]?.x2)).toEqual([
      "decimal:810200880000000:normalized",
      "reg:2",
    ]);
    expect(x2ShapeStateText(states[1]?.xShape)).toEqual([
      "exponent:8.1020088:14:decimal",
    ]);
    expect(x2ShapeStateText(states[1]?.x2Shape)).toEqual([
      "exponent:8.1020088:14:decimal",
    ]);
  });

  it("x2 value dataflow keeps hex-like preload constants as shape-only facts", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[1]?.x)).toEqual(["reg:2"]);
    expect(x2ValueStateText(states[1]?.x2)).toEqual(["reg:2"]);
    expect(x2ShapeStateText(states[1]?.xShape)).toEqual([
      "hex:8.70Е2-6С:mantissa",
    ]);
    expect(x2ShapeStateText(states[1]?.x2Shape)).toEqual([
      "hex:8.70Е2-6С:mantissa",
    ]);
  });

  it("x2 value dataflow rejects unknown Cyrillic glyphs as structural hex facts", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8Ж000000"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[1]?.x)).toEqual(["reg:2"]);
    expect(x2ValueStateText(states[1]?.x2)).toEqual(["reg:2"]);
    expect(x2ShapeStateText(states[1]?.xShape)).toEqual([]);
    expect(x2ShapeStateText(states[1]?.x2Shape)).toEqual([]);
  });

  it("x2 value dataflow rejects malformed structural mantissa shapes", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8..A"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[1]?.x)).toEqual(["reg:2"]);
    expect(x2ValueStateText(states[1]?.x2)).toEqual(["reg:2"]);
    expect(x2ShapeStateText(states[1]?.xShape)).toEqual([]);
    expect(x2ShapeStateText(states[1]?.x2Shape)).toEqual([]);
  });

  it("x2 value dataflow gives closed super sign-change a structural expr key", () => {
    const program: IrOp[] = [
      recall("2", "preload const FA"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[2]?.x)).toEqual([
      "expr-key:0B(shape:super:FA)",
      "expr:1",
    ]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual([
      "expr-key:0B(shape:super:FA)",
      "expr:1",
    ]);
    expect(x2ShapeStateText(states[2]?.xShape)).toEqual(["super:-FA"]);
    expect(x2ShapeStateText(states[2]?.x2Shape)).toEqual(["super:-FA"]);
  });

  it("x2 value dataflow gives closed hex sign-change a structural expr key", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[2]?.x)).toEqual([
      "expr-key:0B(shape:hex:8.70Е2-6С:mantissa)",
      "expr:1",
    ]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual([
      "expr-key:0B(shape:hex:8.70Е2-6С:mantissa)",
      "expr:1",
    ]);
    expect(x2ShapeStateText(states[2]?.xShape)).toEqual(["hex:-8.70Е2-6С:mantissa"]);
    expect(x2ShapeStateText(states[2]?.x2Shape)).toEqual(["hex:-8.70Е2-6С:mantissa"]);
  });

  it("x2 value dataflow recalls stored super shape facts", () => {
    const program: IrOp[] = [
      recall("2", "preload const FA"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[4]?.x)).toEqual(["reg:1"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["reg:1"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["super:FA"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["super:FA"]);
  });

  it("x2 value dataflow canonicalizes stored structural shape facts", () => {
    const program: IrOp[] = [
      recall("2", "preload const fa.ce"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[1]?.xShape)).toEqual(["hex:FA.CE:mantissa"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex:FA.CE:mantissa"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["hex:FA.CE:mantissa"]);
  });

  it("x2 value dataflow tracks structural Y shapes through stack lift and X/Y exchange", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      plain(0x0e, "В↑"),
      recall("3", "preload const CAFE"),
      plain(0x14, "X↔Y"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[2]?.yShape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2ShapeStateText(states[3]?.xShape)).toEqual(["hex:CAFE:mantissa"]);
    expect(x2ShapeStateText(states[3]?.yShape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2ShapeStateText(states[4]?.yShape)).toEqual(["hex:CAFE:mantissa"]);
  });

  it("x2 value dataflow stores structural shapes restored from Y by X/Y exchange", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      plain(0x0e, "В↑"),
      recall("3", "preload const CAFE"),
      plain(0x14, "X↔Y"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2ShapeStateText(states[7]?.xShape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2ShapeStateText(states[7]?.x2Shape)).toEqual(["hex:FACE:mantissa"]);
  });

  it("x2 value dataflow recalls stored hex sign-change structural expr keys", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[5]?.x)).toEqual([
      "expr-key:0B(shape:hex:8.70Е2-6С:mantissa)",
      "expr:4",
    ]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual([
      "expr-key:0B(shape:hex:8.70Е2-6С:mantissa)",
      "expr:4",
    ]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex:-8.70Е2-6С:mantissa"]);
    expect(x2ShapeStateText(states[5]?.x2Shape)).toEqual(["hex:-8.70Е2-6С:mantissa"]);
  });

  it("x2 value dataflow keeps structural abs shape-only", () => {
    const program: IrOp[] = [
      recall("2", "preload const -FACE"),
      plain(0x31, "К |x|"),
      halt(),
    ];
    const exponentProgram: IrOp[] = [
      recall("2", "preload const -FACE"),
      plain(0x0c, "ВП"),
      plain(0x01, "1"),
      plain(0x31, "К |x|"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });
    const exponentStates = computeX2ValueStates(exponentProgram, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[2]?.x)).toContain("expr-key:31(shape:hex:-FACE:mantissa)");
    expect(x2ShapeStateText(states[2]?.xShape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2ShapeStateText(states[2]?.x2Shape)).toEqual(["hex:-FACE:mantissa"]);
    expect(x2ShapeStateText(exponentStates[4]?.xShape)).toEqual(["hex:FACE0:mantissa"]);
    expect(x2ShapeStateText(exponentStates[4]?.x2Shape)).toEqual(["hex-exponent:-FACE:1"]);
  });

  it("x2 value dataflow tracks structural VP-entry shape context", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x54, "К НОП"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2VpEntryShapeText(states[1])).toEqual(["hex:8.70Е2-6С:mantissa"]);
    expect(x2VpEntryShapeText(states[2])).toEqual(["hex:-8.70Е2-6С:mantissa"]);
    expect(x2VpEntryShapeText(states[3])).toEqual(["hex:-8.70Е2-6С:mantissa"]);
  });

  it("x2 value dataflow treats closed structural exponent shifts as structural display equality", () => {
    const exponentProgram: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      halt(),
    ];
    const mantissaProgram: IrOp[] = [
      recall("1", "preload const Г00"),
      halt(),
    ];
    const exponentStates = computeX2ValueStates(exponentProgram, { trackRegisterMemory: true });
    const mantissaStates = computeX2ValueStates(mantissaProgram, { trackRegisterMemory: true });

    expect(x2ShapeStateText(exponentStates[4]?.x2Shape)).toEqual(["hex-exponent:Г:2"]);
    expect(x2ShapeStateText(mantissaStates[1]?.x2Shape)).toEqual(["hex:Г00:mantissa"]);
    expect(x2ShapeSetsHaveSameStructuralShape(exponentStates[4]?.x2Shape, mantissaStates[1]?.x2Shape)).toBe(true);
  });

  it("x2 value dataflow exposes closed structural exponent sync as a VP mantissa source", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex-exponent:Г:2"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["hex-exponent:Г:2"]);
    expect(x2VpEntryShapeText(states[4])).toEqual(["hex:Г00:mantissa"]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex-exponent:Г00:"]);
    expect(x2ShapeStateText(states[6]?.xShape)).toEqual(["hex-exponent:Г00:3"]);
  });

  it("x2 value dataflow exposes exact decimal exponent display sync as a VP source", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });
    const activeExponent = analyzeX2VpShapeContext(states[5]);

    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["exponent:1:8:decimal"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["exponent:1:8:decimal"]);
    expect(x2ValueStateText(states[4]?.x2)).toContain("decimal:100000000:normalized");
    expect(x2VpEntryShapeText(states[4])).toEqual([]);
    expect(x2VpEntryMantissaText(states[4])).toEqual(["100000000"]);
    expect(activeExponent).toMatchObject({
      kind: "active-exponent",
      source: "decimal",
      hasExponentDigit: false,
      restoresX2: true,
    });
    expect([...(activeExponent.mantissa ?? [])]).toEqual(["100000000"]);
    expect([...(activeExponent.exponent ?? [])]).toEqual([""]);
    expect(x2ShapeStateText(states[6]?.x2Shape)).toEqual([
      "exponent:100000000:3:decimal",
      "exponent:1:11:decimal",
    ]);
    expect(x2ValueStateText(states[6]?.x2)).toEqual([]);
  });

  it("x2 value dataflow restores synced exact decimal display shapes through dot", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[3]?.x2)).toEqual([]);
    expect(x2ShapeStateText(states[3]?.x2Shape)).toEqual(["exponent:1:8:decimal"]);
    expect(x2ValueStateText(states[4]?.x2)).toContain("decimal:100000000:normalized");
    expect(x2ValueStateText(states[5]?.x)).toContain("decimal:100000000:normalized");
  });

  it("x2 value dataflow derives VP mantissa sources from conditional structural X2 syncs", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      cjump("done"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex-exponent:Г:2"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["hex-exponent:Г:2"]);
    expect(x2VpEntryShapeText(states[4])).toEqual(["hex:Г00:mantissa"]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex-exponent:Г00:"]);
    expect(x2ShapeStateText(states[6]?.xShape)).toEqual(["hex-exponent:Г00:3"]);
  });

  it("x2 value dataflow joins structural exponent and mantissa shapes by restored display shape", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      cjump("right"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      jump("join"),
      label("right"),
      recall("2", "preload const Г00"),
      label("join"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[9]?.xShape)).toEqual(["hex:Г00:mantissa"]);
    expect(x2ShapeStateText(states[9]?.x2Shape)).toEqual(["hex:Г00:mantissa"]);
    expect(x2VpEntryShapeText(states[9])).toEqual(["hex:Г00:mantissa"]);
    expect(x2ShapeStateText(states[10]?.xShape)).toEqual(["hex-exponent:Г00:"]);
    expect(x2ShapeStateText(states[11]?.xShape)).toEqual(["hex-exponent:Г00:3"]);
  });

  it("x2 value dataflow joins structural shape memory by restored display shape", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      cjump("right"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("3"),
      jump("join"),
      label("right"),
      recall("2", "preload const Г00"),
      store("3"),
      label("join"),
      plain(0x0d, "Cx"),
      recall("3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[13]?.xShape)).toEqual(["hex:Г00:mantissa"]);
    expect(x2ShapeStateText(states[13]?.x2Shape)).toEqual(["hex:Г00:mantissa"]);
  });

  it("x2 value dataflow parses preloaded structural exponent notation", () => {
    const program: IrOp[] = [
      recall("1", "preload const ГE-2"),
      recall("2", "preload const FAE2"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[1]?.xShape)).toEqual(["hex-exponent:Г:-2"]);
    expect(x2ShapeStateText(states[2]?.xShape)).toEqual(["super-exponent:FA:2"]);
  });

  it("x2 value dataflow gives preloaded decimal constants display-accurate shapes", () => {
    const program: IrOp[] = [
      recall("1", "preload const 12.3"),
      recall("2", "preload const 1E8"),
      recall("3", "preload const 1E-8"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[1]?.x)).toEqual(["decimal:12.3:normalized", "reg:1"]);
    expect(x2ShapeStateText(states[1]?.xShape)).toEqual(["mantissa:12.3:decimal"]);
    expect(x2ValueStateText(states[2]?.x)).toEqual(["decimal:100000000:normalized", "reg:2"]);
    expect(x2ShapeStateText(states[2]?.xShape)).toEqual(["exponent:1:8:decimal"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:0.00000001:normalized", "reg:3"]);
    expect(x2ShapeStateText(states[3]?.xShape)).toEqual(["exponent:1:-8:decimal"]);
  });

  it("x2 shape algebra classifies decimal, exponent, hex, and super facts", () => {
    expect(parseX2ShapeFact("mantissa:02:decimal")).toEqual({
      kind: "decimal-mantissa",
      raw: "02",
      normalized: "2",
      safety: "dotSafeDecimal",
    });
    expect(parseX2ShapeFact("mantissa:-0:decimal")).toEqual({
      kind: "decimal-mantissa",
      raw: "-0",
      normalized: "0",
      safety: "errorProne",
    });
    expect(parseX2ShapeFact("mantissa:BAD:decimal")).toEqual({
      kind: "unknown",
      raw: "mantissa:BAD:decimal",
      safety: "unknown",
    });
    expect(parseX2ShapeFact("exponent:5::decimal")).toEqual({
      kind: "decimal-exponent",
      mantissa: "5",
      exponent: "",
      normalized: undefined,
      safety: "errorProne",
    });
    expect(parseX2ShapeFact("exponent:5:BAD:decimal")).toEqual({
      kind: "unknown",
      raw: "exponent:5:BAD:decimal",
      safety: "unknown",
    });
    expect(parseX2ShapeFact("hex:FABC:mantissa")).toEqual({
      kind: "hex-mantissa",
      raw: "FABC",
      safety: "structuralOnly",
    });
    expect(parseX2ShapeFact("hex-exponent:FABC:-3")).toEqual({
      kind: "hex-exponent",
      mantissa: "FABC",
      exponent: "-3",
      safety: "structuralOnly",
    });
    expect(parseX2ShapeFact("hex-exponent:FABC:BAD")).toEqual({
      kind: "unknown",
      raw: "hex-exponent:FABC:BAD",
      safety: "unknown",
    });
    expect(parseX2ShapeFact("super:FA")).toEqual({
      kind: "super-mantissa",
      raw: "FA",
      safety: "structuralOnly",
    });
    expect(parseX2ShapeFact("super:-FA")).toEqual({
      kind: "super-mantissa",
      raw: "-FA",
      safety: "structuralOnly",
    });
    expect(parseX2ShapeFact("super-exponent:FA:3")).toEqual({
      kind: "super-exponent",
      mantissa: "FA",
      exponent: "3",
      safety: "structuralOnly",
    });
    expect(parseX2ShapeFact("super-exponent:FA:BAD")).toEqual({
      kind: "unknown",
      raw: "super-exponent:FA:BAD",
      safety: "unknown",
    });
    expect(parseX2ShapeFact("hex:8Ж:mantissa")).toEqual({
      kind: "unknown",
      raw: "hex:8Ж:mantissa",
      safety: "unknown",
    });
    expect(parseX2ShapeFact("hex:8Е:mantissa")).toEqual({
      kind: "hex-mantissa",
      raw: "8Е",
      safety: "structuralOnly",
    });
    expect(parseX2ShapeFact("hex:8.70Е2-6С:mantissa")).toEqual({
      kind: "hex-mantissa",
      raw: "8.70Е2-6С",
      safety: "structuralOnly",
    });
    expect(parseX2ShapeFact("hex:8..A:mantissa")).toEqual({
      kind: "unknown",
      raw: "hex:8..A:mantissa",
      safety: "unknown",
    });
    expect(parseX2ShapeFact("super:8A")).toEqual({
      kind: "unknown",
      raw: "super:8A",
      safety: "unknown",
    });
    expect(parseX2ShapeFact("super:FС")).toEqual({
      kind: "unknown",
      raw: "super:FС",
      safety: "unknown",
    });
    expect(parseX2ShapeFact("super:FA.")).toEqual({
      kind: "unknown",
      raw: "super:FA.",
      safety: "unknown",
    });
  });

  it("x2 shape algebra keeps leading-zero and structural shapes out of no-op equality", () => {
    expect(x2ShapeSetSafety(new Set(["mantissa:2:decimal"]))).toBe("dotSafeDecimal");
    expect(x2ShapeSetSafety(new Set(["mantissa:-0:decimal"]))).toBe("errorProne");
    expect(x2ShapeSetSafety(new Set(["hex:FABC:mantissa"]))).toBe("structuralOnly");
    expect(x2ShapeSetSafety(new Set(["hex:8Ж:mantissa"]))).toBe("unknown");
    expect(x2ShapeSetSafety(new Set(["super:8A"]))).toBe("unknown");
    expect(x2ShapeSetSafety(new Set(["exponent:5::decimal"]))).toBe("errorProne");
    expect(x2ShapeFactSafety("hex-exponent:Г:BAD")).toBe("unknown");
    expect(x2ShapeFactSafety("super-exponent:FA:BAD")).toBe("unknown");
    expect(x2ShapeFactSafety("mantissa:BAD:decimal")).toBe("unknown");
    expect(x2ShapeFactSafety("exponent:5:BAD:decimal")).toBe("unknown");
    expect(x2ShapeSetSafety(new Set(["hex-exponent:Г:BAD"]))).toBe("unknown");
    expect(x2ShapeSetSafety(new Set(["mantissa:2:decimal", "mantissa:BAD:decimal"]))).toBe("dotSafeDecimal");
    expect(x2ShapeSetSafety(new Set(["mantissa:2:decimal", "hex:8Ж:mantissa"]))).toBe("dotSafeDecimal");
    expect(x2ShapeSetSafety(new Set(["hex:FABC:mantissa", "super:8A"]))).toBe("structuralOnly");
    expect(
      x2ShapeSetsHaveSameDotSafeDecimal(
        new Set(["mantissa:2:decimal"]),
        new Set(["mantissa:2:decimal"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameDotSafeDecimal(
        new Set(["mantissa:2:decimal", "hex:8Ж:mantissa"]),
        new Set(["mantissa:2:decimal", "super:8A"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameDotSafeDecimal(
        new Set(["mantissa:2:decimal"]),
        new Set(["mantissa:02:decimal"]),
      ),
    ).toBe(false);
    expect(
      x2ShapeSetsHaveSameDotSafeDecimal(
        new Set(["mantissa:2:decimal"]),
        new Set(["hex:FABC:mantissa"]),
      ),
    ).toBe(false);
    expect(
      x2ShapeSetsHaveSameStructuralShape(
        new Set(["hex:FABC:mantissa"]),
        new Set(["hex:FABC:mantissa"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameStructuralShape(
        new Set(["hex:fa.bc:mantissa"]),
        new Set(["hex:FA.BC:mantissa"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameStructuralShape(
        new Set(["hex-exponent:FABC:3"]),
        new Set(["hex-exponent:FABC:3"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameStructuralShape(
        new Set(["hex-exponent:fa.bc:-3"]),
        new Set(["hex-exponent:FA.BC:-3"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameStructuralShape(
        new Set(["super:fa"]),
        new Set(["super:FA"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameStructuralShape(
        new Set(["super:FA"]),
        new Set(["hex:FA:mantissa"]),
      ),
    ).toBe(false);
    expect(
      x2ShapeSetsHaveSameStructuralShape(
        new Set(["super:8A"]),
        new Set(["super:8A"]),
      ),
    ).toBe(false);
    expect(
      x2ShapeSetsHaveSameStructuralShape(
        new Set(["hex-exponent:Г:2"]),
        new Set(["hex:Г00:mantissa"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameStructuralShape(
        new Set(["hex-exponent:Г:-2"]),
        new Set(["hex:0.0Г:mantissa"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameStructuralShape(
        new Set(["super-exponent:FA:2"]),
        new Set(["hex:FA00:mantissa"]),
      ),
    ).toBe(true);
    expect(x2ShapeStateText(x2StructuralRestoreShapeFacts(new Set(["hex-exponent:Г:2"])))).toEqual([
      "hex-exponent:Г:2",
      "hex:Г00:mantissa",
    ]);
    expect(x2ShapeStateText(x2StructuralRestoreShapeFacts(new Set(["hex-exponent:Г:-2"])))).toEqual([
      "hex-exponent:Г:-2",
      "hex:0.0Г:mantissa",
    ]);
    expect(x2ShapeStateText(x2StructuralRestoreShapeFacts(new Set(["super-exponent:FA:2"])))).toEqual([
      "hex:FA00:mantissa",
      "super-exponent:FA:2",
    ]);
    expect(x2ShapeStateText(canonicalStructuralRestoreSourceKeyFacts(new Set(["hex-exponent:Г:2"])))).toEqual([
      "hex:Г00:mantissa",
    ]);
    expect(x2ShapeStateText(canonicalStructuralRestoreSourceKeyFacts(new Set(["super-exponent:FA:2"])))).toEqual([
      "hex:FA00:mantissa",
    ]);
    expect(x2ShapeStateText(canonicalStructuralRestoreSourceKeyFacts(new Set(["hex-exponent:Г:99"])))).toEqual([
      "hex-exponent:Г:99",
    ]);
  });

  it("x2 shape algebra compares decimal display shapes without making them dot-safe", () => {
    expect(
      x2ShapeSetsHaveSameDecimalDisplayShape(
        new Set(["exponent:1:8:decimal"]),
        new Set(["exponent:1:8:decimal"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameDotSafeDecimal(
        new Set(["exponent:1:8:decimal"]),
        new Set(["exponent:1:8:decimal"]),
      ),
    ).toBe(false);
    expect(
      x2ShapeSetsHaveSameDecimalDisplayShape(
        new Set(["exponent:100:0:decimal"]),
        new Set(["mantissa:100:decimal"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameDecimalDisplayShape(
        new Set(["mantissa:02:decimal"]),
        new Set(["mantissa:2:decimal"]),
      ),
    ).toBe(false);
    expect(
      x2ShapeSetsHaveSameDecimalDisplayShape(
        new Set(["mantissa:100000000:decimal"]),
        new Set(["exponent:1:8:decimal"]),
      ),
    ).toBe(false);
  });

  it("x2 shape algebra compares restored display shapes across decimal and structural facts", () => {
    expect(x2ShapeStateText(x2RestoredDisplayShapeFacts(new Set(["exponent:100000000:2:decimal"])))).toEqual([
      "exponent:1:10:decimal",
    ]);
    expect(x2ShapeStateText(x2RestoredDisplayShapeFacts(new Set(["hex-exponent:Г:2"])))).toEqual([
      "hex-exponent:Г:2",
      "hex:Г00:mantissa",
    ]);
    expect(x2ShapeStateText(x2RestoredDisplayShapeFacts(new Set(["super-exponent:FA:2"])))).toEqual([
      "hex:FA00:mantissa",
      "super-exponent:FA:2",
    ]);
    expect(x2ShapeStateText(x2RestoredDisplayShapeFacts(new Set(["mantissa:0.5:decimal"])))).toEqual([]);
    expect(x2ShapeStateText(x2RestoredDisplaySourceKeyShapeFacts(new Set(["exponent:100000000:2:decimal"])))).toEqual([
      "exponent:1:10:decimal",
    ]);
    expect(x2ShapeStateText(x2RestoredDisplaySourceKeyShapeFacts(new Set(["hex-exponent:Г:2"])))).toEqual([
      "hex:Г00:mantissa",
    ]);
    expect(x2ShapeStateText(x2RestoredDisplaySourceKeyShapeFacts(new Set(["super-exponent:FA:2"])))).toEqual([
      "hex:FA00:mantissa",
    ]);
    expect(x2ShapeStateText(x2RestoredDisplayShapeFactsFromSourceKey("shape:exponent:100000000:2:decimal"))).toEqual([
      "exponent:1:10:decimal",
    ]);
    expect(x2ShapeStateText(x2RestoredDisplayShapeFactsFromSourceKey("shape:hex-exponent:Г:2"))).toEqual([
      "hex:Г00:mantissa",
    ]);
    expect(x2ShapeStateText(x2RestoredDisplayShapeFactsFromSourceKey("shape:super-exponent:FA:2"))).toEqual([
      "hex:FA00:mantissa",
    ]);
    expect(x2RestoredDisplayShapeFactsFromSourceKey("shape:hex:8Ж:mantissa")).toBeUndefined();
    expect(x2RestoredDisplayShapeFactsFromSourceKey("decimal:1:normalized")).toBeUndefined();
    expect(
      x2ShapeSetsHaveSameRestoredDisplayShape(
        new Set(["exponent:100:0:decimal"]),
        new Set(["mantissa:100:decimal"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameRestoredDisplayShape(
        new Set(["hex-exponent:Г:-2"]),
        new Set(["hex:0.0Г:mantissa"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameRestoredDisplayShape(
        new Set(["super-exponent:FA:2"]),
        new Set(["hex:FA00:mantissa"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameRestoredDisplayShape(
        new Set(["mantissa:02:decimal"]),
        new Set(["mantissa:2:decimal"]),
      ),
    ).toBe(false);
  });

  it("x2 closed sign-change source equality accepts shape-only display proofs", () => {
    const decimalDisplayState: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      entry: { kind: "closed" },
    };
    const structuralState: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["hex:Г00:mantissa"]),
      x2Shape: new Set<X2ShapeFact>(["hex-exponent:Г:2"]),
      entry: { kind: "closed" },
    };
    const leadingZeroState: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:2:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["mantissa:02:decimal"]),
      entry: { kind: "closed" },
    };
    const mixedValueDisplayState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["decimal:0.5:normalized"]),
      x2: new Set(),
      xShape: new Set(),
      x2Shape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
      entry: { kind: "closed" },
    };
    const mixedDisplayValueState: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set<X2ValueFact>(["decimal:0.5:normalized"]),
      xShape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
      x2Shape: new Set(),
      entry: { kind: "closed" },
    };
    const rawFractionDisplayState: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:0.5:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
      entry: { kind: "closed" },
    };

    expect(x2StateHasSameClosedSignChangeSourceInXAndX2(decimalDisplayState)).toBe(true);
    expect(x2StateHasSameClosedSignChangeSourceInXAndX2(structuralState)).toBe(true);
    expect(x2StateHasSameClosedSignChangeSourceInXAndX2(mixedValueDisplayState)).toBe(true);
    expect(x2StateHasSameClosedSignChangeSourceInXAndX2(mixedDisplayValueState)).toBe(true);
    expect(x2StateHasSameClosedSignChangeSourceInXAndX2(leadingZeroState)).toBe(false);
    expect(x2StateHasSameClosedSignChangeSourceInXAndX2(rawFractionDisplayState)).toBe(false);
  });

  it("x2 shape algebra recognizes emulator-pinned dot-safe structural mantissas", () => {
    expect(
      x2ShapeSetsHaveSameDotSafeStructuralMantissa(
        new Set(["hex:A:mantissa"]),
        new Set(["hex:A:mantissa"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameDotSafeStructuralMantissa(
        new Set(["hex:C:mantissa"]),
        new Set(["hex:С:mantissa"]),
      ),
    ).toBe(true);
    expect(
      x2ShapeSetsHaveSameDotSafeStructuralMantissa(
        new Set(["hex:Г:mantissa"]),
        new Set(["hex:Г:mantissa"]),
      ),
    ).toBe(false);
    expect(
      x2ShapeSetsHaveSameDotSafeStructuralMantissa(
        new Set(["hex:F:mantissa"]),
        new Set(["hex:F:mantissa"]),
      ),
    ).toBe(false);
    expect(
      x2ShapeSetsHaveSameDotSafeStructuralMantissa(
        new Set(["hex-exponent:C:0"]),
        new Set(["hex:C:mantissa"]),
      ),
    ).toBe(false);
    expect(x2ShapeSetHasOnlyDotSafeStructuralMantissas(new Set(["hex:A:mantissa"]))).toBe(true);
    expect(x2ShapeSetHasOnlyDotSafeStructuralMantissas(new Set(["hex:B:mantissa", "hex:С:mantissa"]))).toBe(true);
    expect(x2ShapeSetHasOnlyDotSafeStructuralMantissas(new Set(["hex:Г:mantissa"]))).toBe(false);
    expect(x2ShapeSetHasOnlyDotSafeStructuralMantissas(new Set(["hex:A:mantissa", "hex:F:mantissa"]))).toBe(false);
    expect(x2ShapeSetHasOnlyDotSafeStructuralMantissas(new Set(["hex-exponent:A:0"]))).toBe(false);
  });

  it("x2 shape algebra recognizes safe structural VP-dot contexts", () => {
    const dProgram: IrOp[] = [
      recall("1", "preload const D"),
      plain(0x0c, "ВП"),
      plain(0x0a, "."),
      halt(),
    ];
    const eGapProgram: IrOp[] = [
      recall("1", "preload const Е"),
      plain(0x0c, "ВП"),
      plain(0x20, "F pi"),
      plain(0x0a, "."),
      halt(),
    ];
    const addressGapProgram: IrOp[] = [
      recall("1", "preload const Е"),
      plain(0x0c, "ВП"),
      orphanAddress(54),
      plain(0x20, "F pi"),
      plain(0x0a, "."),
      halt(),
    ];
    const twoGapProgram: IrOp[] = [
      recall("1", "preload const Е"),
      plain(0x0c, "ВП"),
      plain(0x20, "F pi"),
      plain(0x22, "F x^2"),
      plain(0x0a, "."),
      halt(),
    ];
    const cProgram: IrOp[] = [
      recall("1", "preload const C"),
      plain(0x0c, "ВП"),
      plain(0x0a, "."),
      halt(),
    ];
    const dStates = computeX2ValueStates(dProgram);
    const eGapStates = computeX2ValueStates(eGapProgram);
    const addressGapStates = computeX2ValueStates(addressGapProgram);
    const twoGapStates = computeX2ValueStates(twoGapProgram);
    const cStates = computeX2ValueStates(cProgram);
    const closedMantissaState: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      x2Shape: new Set<X2ShapeFact>(["hex:Г00:mantissa"]),
      entry: { kind: "closed" },
      structuralVpContext: {
        kind: "exponent",
        mantissa: new Set<X2ShapeFact>(["hex:Г:mantissa"]),
        exponent: new Set(["2"]),
      },
    };
    const unsafeClosedMantissaState: X2ValueDataflowState = {
      ...closedMantissaState,
      x2Shape: new Set<X2ShapeFact>(["hex:C00:mantissa"]),
    };
    const decimalClosedMantissaState: X2ValueDataflowState = {
      ...closedMantissaState,
      x2Shape: new Set<X2ShapeFact>(["mantissa:1300:decimal"]),
    };

    expect(x2StateHasVpDotSafeStructuralContextX2(dStates[2])).toBe(true);
    expect(x2CanUseVpDotRestoreAt(dProgram, 2, dStates[2])).toBe(true);
    expect(x2StateHasVpDotSafeStructuralContextX2(eGapStates[3])).toBe(true);
    expect(x2CanUseVpDotRestoreAt(eGapProgram, 3, eGapStates[3])).toBe(true);
    expect(x2StateHasVpDotSafeStructuralContextX2(addressGapStates[4])).toBe(true);
    expect(x2CanUseVpDotRestoreAt(addressGapProgram, 4, addressGapStates[4])).toBe(true);
    expect(x2StateHasVpDotSafeStructuralContextX2(twoGapStates[4])).toBe(true);
    expect(x2CanUseVpDotRestoreAt(twoGapProgram, 4, twoGapStates[4])).toBe(false);
    expect(x2StateHasVpDotSafeStructuralContextX2(cStates[2])).toBe(false);
    expect(x2CanUseVpDotRestoreAt(cProgram, 2, cStates[2])).toBe(false);
    expect(x2StateHasVpDotSafeStructuralContextX2(closedMantissaState)).toBe(true);
    expect(x2StateHasVpDotSafeStructuralContextX2(unsafeClosedMantissaState)).toBe(false);
    expect(x2StateHasVpDotSafeStructuralContextX2(decimalClosedMantissaState)).toBe(false);
  });

  it("x2 value algebra compares decimal facts by restored visible value", () => {
    expect(
      x2ValueSetsHaveSameRestoredVisibleDecimal(
        new Set(["decimal:2:normalized"]),
        new Set(["decimal:02:unnormalized"]),
      ),
    ).toBe(true);
    expect(
      x2ValueSetsHaveSameRestoredVisibleDecimal(
        new Set(["decimal:-2:normalized"]),
        new Set(["decimal:-02:unnormalized"]),
      ),
    ).toBe(true);
    expect(
      x2ValueSetHasRestoredVisibleDecimal(
        new Set(["decimal:1.2:normalized"]),
        "decimal:01.20:unnormalized",
      ),
    ).toBe(true);
    expect(
      x2ValueSetsHaveSameRestoredVisibleDecimal(
        new Set(["reg:1"]),
        new Set(["decimal:1:normalized"]),
      ),
    ).toBe(false);
  });

  it("x2 value/shape algebra compares exact restored visible decimals across mixed facts", () => {
    expect([...x2ValueShapeSetRestoredVisibleDecimals(
      new Set(["decimal:02:unnormalized"]),
      new Set(["exponent:5:-1:decimal"]),
    )].sort()).toEqual(["0.5", "2"]);
    expect(
      x2ValueShapeSetHasRestoredVisibleDecimal(
        new Set(["reg:1"]),
        new Set(["exponent:5:-1:decimal"]),
        "decimal:0.5:normalized",
      ),
    ).toBe(true);
    expect(
      x2ValueShapeSetsHaveSameRestoredVisibleDecimal(
        new Set(["decimal:0.5:normalized"]),
        undefined,
        new Set(["reg:2"]),
        new Set(["exponent:5:-1:decimal"]),
      ),
    ).toBe(true);
    expect(
      x2ValueShapeSetsHaveSameRestoredVisibleDecimal(
        new Set(["decimal:0.5:normalized"]),
        undefined,
        undefined,
        new Set(["mantissa:0.5:decimal"]),
      ),
    ).toBe(false);
    expect(
      x2ValueShapeSetsHaveSameRestoredVisibleDecimal(
        new Set(["decimal:2:normalized"]),
        new Set(["mantissa:02:decimal"]),
        new Set(["reg:2"]),
        new Set(["mantissa:2:decimal"]),
      ),
    ).toBe(true);
  });

  it("x2 value/shape algebra compares stable expr-key shapes for hidden-temp proofs", () => {
    expect(
      x2ValueShapeSetsHaveSameRestoredDisplayShape(
        new Set(["expr-key:31(shape:hex:-A:mantissa)"]),
        undefined,
        undefined,
        new Set(["hex:A:mantissa"]),
      ),
    ).toBe(true);
    expect(
      x2ValueShapeSetsHaveSameDotSafeStructuralMantissa(
        new Set(["expr-key:31(shape:hex:-A:mantissa)"]),
        undefined,
        undefined,
        new Set(["hex:A:mantissa"]),
      ),
    ).toBe(true);
    expect(
      x2ValueShapeSetsHaveSameDotSafeDecimal(
        new Set(["expr-key:22(decimal:2:normalized)"]),
        undefined,
        undefined,
        new Set(["mantissa:4:decimal"]),
      ),
    ).toBe(true);
    expect(
      x2ValueShapeSetsHaveSameDotSafeStructuralMantissa(
        new Set(["expr-key:31(shape:hex:-D:mantissa)"]),
        undefined,
        undefined,
        new Set(["hex:D:mantissa"]),
      ),
    ).toBe(false);
  });

  it("x2 restore safety flags structural and error-prone shape-only contexts", () => {
    const leadingZeroExponent: IrOp[] = [
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const structuralRecall: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      halt(),
    ];

    expect(x2StateHasUnsafeDotRestoreShapeX2(computeX2ValueStates(leadingZeroExponent)[4])).toBe(true);
    expect(x2StateHasUnsafeDotRestoreShapeX2(computeX2ValueStates(structuralRecall)[1])).toBe(true);
  });

  it("x2 shape data model captures normalized decimal, hex, super, and exponent-entry forms", () => {
    expect(x2ShapeDataModelForFact("mantissa:02:decimal")).toMatchObject({
      kind: "mantissa",
      radix: "decimal",
      raw: "02",
      canonical: "02",
      hasLeadingZero: true,
      normalizedDecimal: "2",
      normalizedSameAsRaw: false,
      significantDigits: 1,
      safety: "dotSafeDecimal",
    });
    expect(x2ShapeDataModelForFact("mantissa:BAD:decimal")).toMatchObject({
      kind: "unknown",
      raw: "mantissa:BAD:decimal",
      safety: "unknown",
    });
    expect(x2ShapeDataModelForFact("hex:8.70Е2-6С:mantissa")).toMatchObject({
      kind: "mantissa",
      radix: "hex",
      raw: "8.70Е2-6С",
      canonical: "8.70Е2-6С",
      hasDecimalPoint: true,
      digits: ["8", "7", "0", "Е", "2", "6", "С"],
      significantDigits: 7,
      safety: "structuralOnly",
    });
    expect(x2ShapeDataModelForFact("hex:123:mantissa")).toMatchObject({
      kind: "mantissa",
      radix: "hex",
      raw: "123",
      normalizedDecimal: "123",
      normalizedSameAsRaw: true,
      safety: "structuralOnly",
    });
    expect(x2ShapeDataModelForFact("hex:0123:mantissa")).toMatchObject({
      kind: "mantissa",
      radix: "hex",
      raw: "0123",
      normalizedDecimal: "123",
      normalizedSameAsRaw: false,
      safety: "structuralOnly",
    });
    expect(x2ShapeDataModelForFact("super:FA")).toMatchObject({
      kind: "mantissa",
      radix: "super",
      raw: "FA",
      digits: ["F", "A"],
      safety: "structuralOnly",
    });
    expect(x2ShapeDataModelForFact("super:FС")).toMatchObject({
      kind: "unknown",
      raw: "super:FС",
      safety: "unknown",
    });
    expect(x2ShapeDataModelForFact("super-exponent:8A:2")).toMatchObject({
      kind: "unknown",
      raw: "super-exponent:8A:2",
      safety: "unknown",
    });
    expect(x2ShapeDataModelForFact("exponent:5:BAD:decimal")).toMatchObject({
      kind: "unknown",
      raw: "exponent:5:BAD:decimal",
      safety: "unknown",
    });
    expect(x2ShapeDataModelForFact("hex-exponent:Г:BAD")).toMatchObject({
      kind: "unknown",
      raw: "hex-exponent:Г:BAD",
      safety: "unknown",
    });
    expect(x2ShapeDataModelForFact("super-exponent:FA:BAD")).toMatchObject({
      kind: "unknown",
      raw: "super-exponent:FA:BAD",
      safety: "unknown",
    });
    expect(x2ShapeDataModelForFact("exponent:5:3:decimal")).toMatchObject({
      kind: "exponent-entry",
      exponentRaw: "3",
      exponentDigits: ["3"],
      normalizedDecimal: "5000",
      closedDecimalDisplay: "mantissa:5000:decimal",
      safety: "errorProne",
    });
    expect(x2ShapeDataModelForFact("exponent:1.2:3:decimal")).toMatchObject({
      kind: "exponent-entry",
      exponentRaw: "3",
      exponentDigits: ["3"],
      normalizedDecimal: "1200",
      closedDecimalDisplay: "mantissa:1200:decimal",
      safety: "errorProne",
    });
    expect(x2ShapeDataModelForFact("exponent:100000000:2:decimal")).toMatchObject({
      kind: "exponent-entry",
      exponentRaw: "2",
      exponentDigits: ["2"],
      normalizedDecimal: "10000000000",
      closedDecimalDisplay: "exponent:1:10:decimal",
      safety: "errorProne",
    });
    expect(x2ShapeDataModelForFact("exponent:0.00000001:2:decimal")).toMatchObject({
      kind: "exponent-entry",
      exponentRaw: "2",
      exponentDigits: ["2"],
      normalizedDecimal: "0.000001",
      closedDecimalDisplay: "exponent:1:-6:decimal",
      safety: "errorProne",
    });
  });

  it("x2 shape algebra derives exponent-entry facts without decimalizing structural forms", () => {
    expect(x2ExponentShapeFactFromMantissaFact("mantissa:05:decimal", "3")).toBe("exponent:05:3:decimal");
    expect(x2ExponentShapeFactFromMantissaFact("hex:8.70Е2-6С:mantissa", "-2")).toBe(
      "hex-exponent:8.70Е2-6С:-2",
    );
    expect(x2ExponentShapeFactFromMantissaFact("super:FA", "")).toBe("super-exponent:FA:");
    expect(x2ShapeSetSafety(new Set(["hex-exponent:8.70Е2-6С:-2"]))).toBe("structuralOnly");
  });

  it("x2 shape algebra extracts exact visible decimals without accepting raw entry spellings", () => {
    expect(x2ShapeFactRestoredVisibleDecimal("exponent:5:-1:decimal")).toBe("0.5");
    expect(x2ShapeFactRestoredVisibleDecimal("mantissa:0.5:decimal")).toBeUndefined();
    expect(x2ShapeFactRestoredVisibleDecimal("mantissa:05:decimal")).toBeUndefined();
    expect(x2ShapeFactRestoredVisibleDecimal("mantissa:-0:decimal")).toBeUndefined();
    expect(x2ShapeFactRestoredVisibleDecimal("hex:123:mantissa")).toBe("123");
    expect(x2ShapeFactRestoredVisibleDecimal("hex:0123:mantissa")).toBeUndefined();
    expect(x2ShapeFactRestoredVisibleDecimal("hex:0.5:mantissa")).toBeUndefined();
    expect(x2ShapeFactRestoredVisibleDecimal("hex-exponent:123:1")).toBe("1230");
    expect(x2ShapeFactRestoredVisibleDecimal("hex-exponent:1.23:2")).toBe("123");
    expect(x2ShapeFactRestoredVisibleDecimal("hex-exponent:1.23:-1")).toBeUndefined();
    expect([...x2ShapeSetRestoredVisibleDecimals(new Set([
      "exponent:5:-1:decimal",
      "mantissa:0.5:decimal",
      "mantissa:05:decimal",
      "hex:123:mantissa",
      "hex:0123:mantissa",
    ]))]).toEqual(["0.5", "123"]);
  });

  it("x2 shape algebra identifies exact integer displays for integer-part no-op rewrites", () => {
    expect(x2ShapeSetHasExactIntegerDisplay(new Set(["mantissa:123:decimal"]))).toBe(true);
    expect(x2ShapeSetHasExactIntegerDisplay(new Set(["mantissa:-123:decimal"]))).toBe(true);
    expect(x2ShapeSetHasExactIntegerDisplay(new Set(["hex:123:mantissa"]))).toBe(true);
    expect(x2ShapeSetHasExactIntegerDisplay(new Set(["hex:-123:mantissa"]))).toBe(true);
    expect(x2ShapeSetHasExactIntegerDisplay(new Set(["exponent:1:3:decimal"]))).toBe(true);
    expect(x2ShapeSetHasExactIntegerDisplay(new Set(["exponent:-1:8:decimal"]))).toBe(true);
    expect(x2ShapeSetHasExactIntegerDisplay(new Set(["mantissa:0123:decimal"]))).toBe(false);
    expect(x2ShapeSetHasExactIntegerDisplay(new Set(["hex:0123:mantissa"]))).toBe(false);
    expect(x2ShapeSetHasExactIntegerDisplay(new Set(["hex-exponent:123:1"]))).toBe(false);
    expect(x2ShapeSetHasExactIntegerDisplay(new Set(["exponent:1:-8:decimal"]))).toBe(false);
    expect(x2ShapeSetHasExactIntegerDisplay(new Set(["exponent:5:-1:decimal"]))).toBe(false);
    expect(x2ShapeSetHasExactIntegerDisplay(new Set(["mantissa:-0:decimal"]))).toBe(false);
    expect(x2ShapeSetHasExactNonNegativeIntegerDisplay(new Set(["mantissa:123:decimal"]))).toBe(true);
    expect(x2ShapeSetHasExactNonNegativeIntegerDisplay(new Set(["exponent:1:8:decimal"]))).toBe(true);
    expect(x2ShapeSetHasExactNonNegativeIntegerDisplay(new Set(["hex:123:mantissa"]))).toBe(true);
    expect(x2ShapeSetHasExactNonNegativeIntegerDisplay(new Set(["mantissa:0:decimal"]))).toBe(true);
    expect(x2ShapeSetHasExactNonNegativeIntegerDisplay(new Set(["mantissa:-123:decimal"]))).toBe(false);
    expect(x2ShapeSetHasExactNonNegativeIntegerDisplay(new Set(["exponent:-1:8:decimal"]))).toBe(false);
    expect(x2ShapeSetHasExactNonNegativeIntegerDisplay(new Set(["hex:-123:mantissa"]))).toBe(false);
    expect(x2ShapeSetHasExactNonNegativeDisplay(new Set(["exponent:5:-1:decimal"]))).toBe(true);
    expect(x2ShapeSetHasExactNonNegativeDisplay(new Set(["exponent:1:3:decimal"]))).toBe(true);
    expect(x2ShapeSetHasExactNonNegativeDisplay(new Set(["mantissa:0.5:decimal"]))).toBe(false);
    expect(x2ShapeSetHasExactNonNegativeDisplay(new Set(["exponent:-5:-1:decimal"]))).toBe(false);
  });

  it("x2 shape algebra closes non-negative structural exponent shifts as mantissa shapes", () => {
    expect(x2ClosedStructuralExponentMantissaShapeFact("hex-exponent:Г:2")).toBe("hex:Г00:mantissa");
    expect(x2ClosedStructuralExponentMantissaShapeFact("hex-exponent:8.70:2")).toBe("hex:870:mantissa");
    expect(x2ClosedStructuralExponentMantissaShapeFact("hex-exponent:8.70:1")).toBe("hex:87.0:mantissa");
    expect(x2ClosedStructuralExponentMantissaShapeFact("hex-exponent:Г:-2")).toBe("hex:0.0Г:mantissa");
    expect(x2ClosedStructuralExponentMantissaShapeFact("hex-exponent:8.70:-1")).toBe("hex:0.870:mantissa");
    expect(x2ClosedStructuralExponentMantissaShapeFact("super-exponent:FA:")).toBe("super:FA");
    expect(x2ClosedStructuralExponentMantissaShapeFact("super-exponent:FA:2")).toBe("hex:FA00:mantissa");
    expect(x2ClosedStructuralExponentMantissaShapeFact("super-exponent:FA:-2")).toBe("hex:0.FA:mantissa");
    expect(x2ShapeDataModelForFact("hex-exponent:Г:2")).toMatchObject({
      kind: "exponent-entry",
      closedStructuralMantissa: {
        kind: "mantissa",
        radix: "hex",
        canonical: "Г00",
        safety: "structuralOnly",
      },
    });
  });

  it("x2 shape algebra closes exponent display shapes through one helper", () => {
    expect(x2ClosedExponentDisplayShapeFact("exponent:5:3:decimal")).toBe("mantissa:5000:decimal");
    expect(x2ClosedExponentDisplayShapeFact("exponent:100000000:2:decimal")).toBe(
      "exponent:1:10:decimal",
    );
    expect(x2ClosedExponentDisplayShapeFact("hex-exponent:Г:2")).toBe("hex:Г00:mantissa");
    expect(x2ClosedExponentDisplayShapeFact("super-exponent:FA:-2")).toBe("hex:0.FA:mantissa");
    expect(x2ClosedExponentDisplayShapeFact("hex:Г00:mantissa")).toBeUndefined();
    expect(x2ClosedExponentDisplayShapeFact("hex-exponent:8Ж:1")).toBeUndefined();
  });

  it("x2 shape algebra exposes closed decimal exponent display shapes without dot-safety promotion", () => {
    expect(x2ClosedDecimalExponentDisplayShapeFact("exponent:5:3:decimal")).toBe("mantissa:5000:decimal");
    expect(x2ClosedDecimalExponentDisplayShapeFact("exponent:100000000:2:decimal")).toBe(
      "exponent:1:10:decimal",
    );
    expect(x2ClosedDecimalExponentDisplayShapeFact("exponent:0.00000001:2:decimal")).toBe(
      "exponent:1:-6:decimal",
    );
    expect(x2ClosedDecimalExponentDisplayShapeFact("mantissa:5:decimal")).toBeUndefined();
    expect(x2ClosedDecimalExponentDisplayShapeFact("hex-exponent:Г:2")).toBeUndefined();
    expect(x2ClosedDecimalExponentDisplayShapeFact("exponent:5:BAD:decimal")).toBeUndefined();
    expect(x2ShapeSetSafety(new Set(["exponent:100000000:2:decimal"]))).toBe("errorProne");
    expect(
      x2ShapeSetsHaveSameDecimalDisplayShape(
        new Set(["exponent:100000000:2:decimal"]),
        new Set(["exponent:1:10:decimal"]),
      ),
    ).toBe(true);
  });

  it("x2 shape algebra shifts and appends structural mantissa digits without decimalizing", () => {
    expect(x2StructuralMantissaShiftShapeFact("hex:8.70:mantissa", "2")).toBe("hex:870:mantissa");
    expect(x2StructuralMantissaShiftShapeFact("hex:Г:mantissa", "-2")).toBe("hex:0.0Г:mantissa");
    expect(x2StructuralMantissaShiftShapeFact("super:FA", "2")).toBe("hex:FA00:mantissa");
    expect(x2StructuralMantissaShiftShapeFact("mantissa:5:decimal", "2")).toBeUndefined();
    expect(x2StructuralMantissaShiftShapeFact("hex:12345678:mantissa", "1")).toBeUndefined();

    expect(x2StructuralMantissaAppendDigitsShapeFact("hex:8.70:mantissa", "Е2")).toBe(
      "hex:8.70Е2:mantissa",
    );
    expect(x2StructuralMantissaAppendDigitsShapeFact("super:FA", "1")).toBe("hex:FA1:mantissa");
    expect(x2StructuralMantissaAppendDigitsShapeFact("hex-exponent:Г:2", "5")).toBe("hex:Г005:mantissa");
    expect(x2StructuralMantissaAppendDigitsShapeFact("hex-exponent:Г:-2", "5")).toBe("hex:0.0Г5:mantissa");
    expect(x2StructuralMantissaAppendDigitsShapeFact("super-exponent:FA:1", "2")).toBe("hex:FA02:mantissa");
    expect(x2StructuralMantissaAppendDigitsShapeFact("hex:12345678:mantissa", "9")).toBeUndefined();
    expect(x2StructuralMantissaAppendDigitsShapeFact("hex-exponent:12345678:1", "9")).toBeUndefined();
    expect(x2StructuralMantissaAppendDigitsShapeFact("hex:8:mantissa", "E-2")).toBeUndefined();
  });

  it("x2 shape algebra concatenates only pure structural digit mantissas", () => {
    expect(x2StructuralMantissaConcatShapeFacts("hex:8.7:mantissa", "hex:0Е:mantissa")).toBe(
      "hex:8.70Е:mantissa",
    );
    expect(x2StructuralMantissaConcatShapeFacts("super:FA", "hex:12:mantissa")).toBe("hex:FA12:mantissa");
    expect(x2StructuralMantissaConcatShapeFacts("hex:8:mantissa", "mantissa:02:decimal")).toBe(
      "hex:802:mantissa",
    );
    expect(x2StructuralMantissaConcatShapeFacts("super:FA", "mantissa:02:decimal")).toBe("hex:FA02:mantissa");
    expect(x2StructuralMantissaConcatShapeFacts("hex:A:mantissa", "hex-exponent:B:2")).toBe(
      "hex:AB00:mantissa",
    );
    expect(x2StructuralMantissaConcatShapeFacts("hex:A:mantissa", "super-exponent:FA:1")).toBe(
      "hex:AFA0:mantissa",
    );
    expect(x2StructuralMantissaConcatShapeFacts("hex:A:mantissa", "exponent:1:2:decimal")).toBe(
      "hex:A100:mantissa",
    );
    expect(x2StructuralMantissaConcatShapeFacts("exponent:1:2:decimal", "hex:A:mantissa")).toBe(
      "hex:100A:mantissa",
    );
    expect(x2StructuralMantissaConcatShapeFacts("mantissa:8:decimal", "hex:1:mantissa")).toBe(
      "hex:81:mantissa",
    );
    expect(x2StructuralMantissaConcatShapeFacts("mantissa:8:decimal", "super:FA")).toBe(
      "hex:8FA:mantissa",
    );
    expect(x2StructuralMantissaConcatShapeFacts("mantissa:8:decimal", "hex-exponent:B:2")).toBe(
      "hex:8B00:mantissa",
    );
    expect(x2StructuralMantissaConcatShapeFacts("hex-exponent:Г:2", "mantissa:05:decimal")).toBe(
      "hex:Г0005:mantissa",
    );
    expect(x2StructuralMantissaConcatShapeFacts("hex-exponent:Г:-2", "hex:5:mantissa")).toBe(
      "hex:0.0Г5:mantissa",
    );
    expect(x2StructuralMantissaConcatShapeFacts("super-exponent:FA:1", "hex:2:mantissa")).toBe(
      "hex:FA02:mantissa",
    );
    expect(x2StructuralMantissaConcatShapeFacts("hex:1234567:mantissa", "mantissa:89:decimal")).toBeUndefined();
    expect(x2StructuralMantissaConcatShapeFacts("hex:8:mantissa", "hex:0.1:mantissa")).toBeUndefined();
    expect(x2StructuralMantissaConcatShapeFacts("hex:8:mantissa", "hex-exponent:A:-1")).toBeUndefined();
    expect(x2StructuralMantissaConcatShapeFacts("hex:8:mantissa", "exponent:1:-1:decimal")).toBeUndefined();
    expect(x2StructuralMantissaConcatShapeFacts("exponent:100000000:2:decimal", "hex:A:mantissa")).toBeUndefined();
    expect(x2StructuralMantissaConcatShapeFacts("hex:8:mantissa", "hex:-1:mantissa")).toBeUndefined();
    expect(x2StructuralMantissaConcatShapeFacts("mantissa:1234567:decimal", "hex:89:mantissa")).toBeUndefined();
    expect(x2StructuralMantissaConcatShapeFacts("mantissa:8.1:decimal", "hex:1:mantissa")).toBeUndefined();
    expect(x2StructuralMantissaConcatShapeFacts("mantissa:-8:decimal", "hex:1:mantissa")).toBeUndefined();
    expect(x2StructuralMantissaConcatShapeFacts("mantissa:8:decimal", "mantissa:1:decimal")).toBeUndefined();
  });

  it("x2 shape algebra splices a VP first digit into structural X2 mantissas", () => {
    expect(x2StructuralMantissaFirstDigitSpliceShapeFact(
      "hex:A:mantissa",
      "hex:8.70:mantissa",
    )).toBe("hex:A.70:mantissa");
    expect(x2StructuralMantissaFirstDigitSpliceShapeFact(
      "mantissa:3:decimal",
      "hex:8.70:mantissa",
    )).toBe("hex:3.70:mantissa");
    expect(x2StructuralMantissaFirstDigitSpliceShapeFact(
      "hex:1:mantissa",
      "super:FA",
    )).toBe("hex:1A:mantissa");
    expect(x2StructuralMantissaFirstDigitSpliceShapeFact(
      "hex:A:mantissa",
      "mantissa:800:decimal",
    )).toBe("hex:A00:mantissa");
    expect(x2StructuralMantissaFirstDigitSpliceShapeFact(
      "hex:A:mantissa",
      "mantissa:000:decimal",
    )).toBe("hex:A00:mantissa");
    expect(x2StructuralMantissaFirstDigitSpliceShapeFact(
      "mantissa:3:decimal",
      "mantissa:800:decimal",
    )).toBeUndefined();
    expect(x2StructuralMantissaFirstDigitSpliceShapeFact(
      "hex:-A:mantissa",
      "hex:8.70:mantissa",
    )).toBeUndefined();
    expect(x2StructuralMantissaFirstDigitSpliceShapeFact(
      "hex:A:mantissa",
      "hex:-8.70:mantissa",
    )).toBeUndefined();
  });

  it("x2 VP source proof uses structural shape algebra equality", () => {
    const base = computeX2ValueStates([halt()])[0]!;
    const shiftedSource = {
      ...base,
      vpEntryShape: new Set<X2ShapeFact>(["hex-exponent:Г:2"]),
    };
    const mantissaSource = {
      ...base,
      vpEntryShape: new Set<X2ShapeFact>(["hex:Г00:mantissa"]),
    };
    const differentSource = {
      ...base,
      vpEntryShape: new Set<X2ShapeFact>(["hex:Г0:mantissa"]),
    };

    expect(x2StatesHaveSameVpEntrySource(shiftedSource, mantissaSource)).toBe(true);
    expect(x2StatesHaveSameVpEntrySource(shiftedSource, differentSource)).toBe(false);
  });

  it("x2 VP source proof compares decimal and structural sources through source keys", () => {
    const base = computeX2ValueStates([halt()])[0]!;
    const decimalSource = {
      ...base,
      vpEntryMantissa: new Set(["-5000"]),
    };
    const sameDecimalSource = {
      ...base,
      vpEntryMantissa: new Set(["-5000"]),
    };
    const differentDecimalSource = {
      ...base,
      vpEntryMantissa: new Set(["5000"]),
    };
    const structuralSource = {
      ...base,
      vpEntryShape: new Set<X2ShapeFact>(["hex-exponent:Г:2"]),
    };
    const sameStructuralSource = {
      ...base,
      vpEntryShape: new Set<X2ShapeFact>(["hex:Г00:mantissa"]),
    };

    expect(x2StatesHaveSameVpEntrySource(decimalSource, sameDecimalSource)).toBe(true);
    expect(x2StatesHaveSameVpEntrySource(decimalSource, differentDecimalSource)).toBe(false);
    expect(x2StatesHaveSameVpEntrySource(structuralSource, sameStructuralSource)).toBe(true);
    expect(x2StatesHaveSameVpEntrySource(decimalSource, structuralSource)).toBe(false);
  });

  it("x2 VP sign source proof uses explicit structural sign shapes separately from ordinary VP sources", () => {
    const base = computeX2ValueStates([halt()])[0]!;
    const transientOrdinarySource = {
      ...base,
      vpEntryShape: new Set<X2ShapeFact>(["hex:ACE:mantissa"]),
      vpEntryShapeTransient: true as const,
      vpEntrySignShape: new Set<X2ShapeFact>(["hex:FACE:mantissa"]),
    };
    const sameStructuralSource = {
      ...base,
      vpEntryShape: new Set<X2ShapeFact>(["hex:FACE:mantissa"]),
    };

    expect(x2StatesHaveSameVpEntrySource(transientOrdinarySource, sameStructuralSource)).toBe(false);
    expect(x2StatesHaveSameVpEntrySignSource(transientOrdinarySource, sameStructuralSource)).toBe(true);
  });

  it("x2 VP sign source join uses display source keys without normalizing raw text", () => {
    expect([...(x2JoinedVpEntryMantissaSources(
      { vpEntryMantissa: new Set(["100"]) },
      { vpEntryShape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]) },
    ) ?? [])].sort()).toEqual(["100"]);
    expect([...(x2JoinedVpEntryMantissaSources(
      { vpEntryShape: new Set<X2ShapeFact>(["exponent:1:8:decimal"]) },
      { vpEntryShape: new Set<X2ShapeFact>(["exponent:100000000:0:decimal"]) },
    ) ?? [])].sort()).toEqual(["100000000"]);
    expect([...(x2JoinedVpEntryMantissaSources(
      {
        vpEntryMantissa: new Set(["7"]),
        vpEntryShape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      },
      {
        vpEntryMantissa: new Set(["7"]),
        vpEntryShape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      },
    ) ?? [])].sort()).toEqual(["100", "7"]);
    expect(x2JoinedVpEntryMantissaSources(
      { vpEntryMantissa: new Set(["02"]) },
      { vpEntryShape: new Set<X2ShapeFact>(["mantissa:2:decimal"]) },
    )).toBeUndefined();
    expect(x2ShapeStateText(x2JoinedVpEntrySignShapeSources(
      { vpEntrySignMantissa: new Set(["100"]) },
      { vpEntrySignShape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]) },
    ))).toEqual(["mantissa:100:decimal"]);
    expect(x2ShapeStateText(x2JoinedVpEntrySignShapeSources(
      { vpEntrySignShape: new Set<X2ShapeFact>(["hex-exponent:Г:2"]) },
      { vpEntrySignShape: new Set<X2ShapeFact>(["hex:Г00:mantissa"]) },
    ))).toEqual(["hex:Г00:mantissa"]);
    expect(x2ShapeStateText(x2JoinedVpEntrySignShapeSources(
      {
        vpEntrySignShape: new Set<X2ShapeFact>([
          "hex-exponent:Г:2",
          "exponent:100:0:decimal",
        ]),
      },
      {
        vpEntrySignShape: new Set<X2ShapeFact>([
          "hex:Г00:mantissa",
          "mantissa:100:decimal",
        ]),
      },
    ))).toEqual(["hex:Г00:mantissa", "mantissa:100:decimal"]);
    expect(x2JoinedVpEntrySignShapeSources(
      { vpEntrySignMantissa: new Set(["02"]) },
      { vpEntrySignShape: new Set<X2ShapeFact>(["mantissa:2:decimal"]) },
    )).toBeUndefined();
  });

  it("x2 VP source proof compares exact decimal display shapes without normalizing entry text", () => {
    const base = computeX2ValueStates([halt()])[0]!;
    const exactMantissaSource = {
      ...base,
      vpEntryMantissa: new Set(["100"]),
    };
    const exactExponentDisplaySource = {
      ...base,
      vpEntryShape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
    };
    const leadingZeroSource = {
      ...base,
      vpEntryMantissa: new Set(["02"]),
    };
    const normalizedDisplaySource = {
      ...base,
      vpEntryShape: new Set<X2ShapeFact>(["mantissa:2:decimal"]),
    };
    const signSource = {
      ...base,
      vpEntrySignMantissa: new Set(["100"]),
    };
    const mixedValueSignSource = {
      ...base,
      x: new Set<X2ValueFact>(["decimal:0.5:normalized"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
    };
    const mixedDisplaySignSource = {
      ...base,
      x2: new Set<X2ValueFact>(["decimal:0.5:normalized"]),
      xShape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
    };
    const exactFractionDisplaySource = {
      ...base,
      vpEntryShape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
    };
    const exactSignDisplaySource = {
      ...base,
      vpEntrySignShape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
    };
    const rawFractionSignSource = {
      ...base,
      xShape: new Set<X2ShapeFact>(["mantissa:0.5:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
    };
    const rawLeadingZeroSignSource = {
      ...base,
      vpEntrySignMantissa: new Set(["02"]),
    };
    const dotRestoredLeadingZeroSource = {
      ...base,
      vpEntryMantissa: new Set(["2"]),
      vpEntrySignMantissa: new Set(["02"]),
    };

    expect(x2StatesHaveSameVpEntrySource(exactMantissaSource, exactExponentDisplaySource)).toBe(true);
    expect(x2StatesHaveSameVpEntrySignSource(signSource, exactExponentDisplaySource)).toBe(true);
    expect(x2StatesHaveSameExplicitVpEntrySignSource(signSource, exactExponentDisplaySource)).toBe(false);
    expect(x2StatesHaveSameExplicitVpEntrySignSource(signSource, exactSignDisplaySource)).toBe(true);
    expect(x2StatesHaveSameVpEntrySignSource(mixedValueSignSource, exactFractionDisplaySource)).toBe(true);
    expect(x2StatesHaveSameVpEntrySignSource(mixedDisplaySignSource, exactFractionDisplaySource)).toBe(true);
    expect(x2StatesHaveSameVpEntrySignSource(rawFractionSignSource, exactFractionDisplaySource)).toBe(false);
    expect(x2StatesHaveSameVpEntrySource(leadingZeroSource, normalizedDisplaySource)).toBe(false);
    expect(x2StatesHaveSameVpEntrySource(rawLeadingZeroSignSource, dotRestoredLeadingZeroSource)).toBe(false);
    expect(x2StatesHaveSameVpEntrySignSource(rawLeadingZeroSignSource, dotRestoredLeadingZeroSource)).toBe(true);
    expect(x2StatesHaveSameVpEntrySignSource({
      ...base,
      vpEntrySignMantissa: new Set(["02"]),
    }, normalizedDisplaySource)).toBe(false);
  });

  it("x2 shape algebra toggles mantissa and exponent signs structurally", () => {
    expect(x2MantissaSignChangedShapeFact("mantissa:02:decimal")).toBe("mantissa:-02:decimal");
    expect(x2MantissaSignChangedShapeFact("mantissa:0:decimal")).toBe("mantissa:-0:decimal");
    expect(x2MantissaSignChangedShapeFact("hex:FACE:mantissa")).toBe("hex:-FACE:mantissa");
    expect(x2MantissaSignChangedShapeFact("super:-FA")).toBe("super:FA");
    expect(x2ExponentSignChangedShapeFact("exponent:05:3:decimal")).toBe("exponent:05:-3:decimal");
    expect(x2ExponentSignChangedShapeFact("hex-exponent:FACE:-2")).toBe("hex-exponent:FACE:2");
    expect(x2ExponentSignChangedShapeFact("super-exponent:FA:")).toBe("super-exponent:FA:-");
    expect(x2ExponentMantissaSignChangedShapeFact("exponent:05:3:decimal")).toBe(
      "exponent:-05:3:decimal",
    );
    expect(x2ExponentMantissaSignChangedShapeFact("hex-exponent:FACE:-2")).toBe("hex-exponent:-FACE:-2");
    expect(x2ExponentMantissaSignChangedShapeFact("super-exponent:-FA:3")).toBe("super-exponent:FA:3");
  });

  it("x2 shape algebra toggles shared structural X2 shapes through restore equality", () => {
    expect(x2ShapeStateText(x2SignChangedSharedStructuralShapeFacts(
      new Set(["hex:FACE:mantissa"]),
      new Set(["hex:FACE:mantissa"]),
    ))).toEqual(["hex:-FACE:mantissa"]);
    expect(x2ShapeStateText(x2SignChangedSharedStructuralShapeFacts(
      new Set(["hex:Г00:mantissa"]),
      new Set(["hex-exponent:Г:2"]),
    ))).toEqual(["hex-exponent:-Г:2"]);
    expect(x2ShapeStateText(x2SignChangedSharedStructuralShapeFacts(
      new Set(["hex:0.0Г:mantissa"]),
      new Set(["hex-exponent:Г:-2"]),
    ))).toEqual(["hex-exponent:-Г:-2"]);
    expect(x2ShapeStateText(x2SignChangedSharedStructuralShapeFacts(
      new Set(["hex:Г0:mantissa"]),
      new Set(["hex-exponent:Г:2"]),
    ))).toEqual([]);
  });

  it("x2 shape algebra rebuilds canonical mantissa facts from data models", () => {
    const decimalModel = x2ShapeDataModelForFact("mantissa: 02 :decimal");
    const hexModel = x2ShapeDataModelForFact("hex: fa.ce :mantissa");
    const superModel = x2ShapeDataModelForFact("super:fa");
    expect(decimalModel.kind === "mantissa" ? x2MantissaShapeFactFromModel(decimalModel) : undefined).toBe(
      "mantissa:02:decimal",
    );
    expect(hexModel.kind === "mantissa" ? x2MantissaShapeFactFromModel(hexModel) : undefined).toBe(
      "hex:FA.CE:mantissa",
    );
    expect(superModel.kind === "mantissa" ? x2MantissaShapeFactFromModel(superModel) : undefined).toBe("super:FA");
  });

  it("x2 shape algebra canonicalizes mantissa and exponent facts", () => {
    expect(x2CanonicalShapeFact("mantissa: 02 :decimal")).toBe("mantissa:02:decimal");
    expect(x2CanonicalShapeFact("hex: fa.ce :mantissa")).toBe("hex:FA.CE:mantissa");
    expect(x2CanonicalShapeFact("super: fa")).toBe("super:FA");
    expect(x2CanonicalShapeFact("super: -fa")).toBe("super:-FA");
    expect(x2CanonicalShapeFact("super:FС")).toBe("super:FС");
    expect(x2CanonicalShapeFact("hex-exponent: fa.ce : -3")).toBe("hex-exponent:FA.CE:-3");
    expect(x2CanonicalShapeFact("super-exponent: fa : 3")).toBe("super-exponent:FA:3");
  });

  it("x2 value dataflow keeps leading-zero X2 separate from normalized X", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["decimal:02:unnormalized"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:2:normalized", "reg:2"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:02:unnormalized"]);
  });

  it("x2 value dataflow tracks sign-change during decimal digit entry", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:-12:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:-12:normalized"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:-12:normalized", "reg:2"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:-12:normalized", "reg:2"]);
  });

  it("x2 value dataflow keeps signed leading-zero X2 separate from normalized X", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:-2:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:-02:unnormalized"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:-2:normalized", "reg:2"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:-02:unnormalized"]);
  });

  it("x2 value dataflow keeps open zero sign-change as signed-zero X2", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toEqual(["decimal:0:normalized"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["decimal:-0:unnormalized"]);
    expect(x2ShapeStateText(states[2]?.x2Shape)).toEqual(["mantissa:-0:decimal"]);
    expect(x2EntryStateText(states[3])).toBe("exponent:-0:");
    expect(x2EntryStateText(states[4])).toBe("exponent:-0:3");
  });

  it("x2 value dataflow restores X from hidden X2 through dot", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toEqual(["expr-key:35(reg:1)", "expr:1"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["reg:1"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["reg:1"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["reg:1"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["reg:1", "reg:2"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["reg:1", "reg:2"]);
  });

  it("x2 value dataflow normalizes visible X when dot restores leading-zero X2", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[3]?.x)).toEqual([
      "decimal:0:normalized",
      "expr:2",
    ]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:02:unnormalized"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:02:unnormalized"]);
  });

  it("x2 value dataflow uses dot-restored leading-zero decimals as VP-entry sources", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2VpEntryShapeText(states[4])).toEqual([]);
    expect(x2VpEntryMantissaText(states[4])).toEqual(["2"]);
    expect(x2EntryStateText(states[5])).toBe("exponent:2:");
    expect(x2EntryStateText(states[6])).toBe("exponent:2:3");
  });

  it("x2 value dataflow carries structural VP-entry shapes through dot restore", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[3]?.xShape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2ShapeStateText(states[3]?.x2Shape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2VpEntryShapeText(states[3])).toEqual(["hex:FACE:mantissa"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex-exponent:FACE:"]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex-exponent:FACE:3"]);
  });

  it("x2 value dataflow carries stable expr-key shapes through dot restore", () => {
    const structuralState: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set<X2ValueFact>(["expr-key:31(shape:hex:-FACE:mantissa)"]),
      entry: { kind: "closed" },
    };
    const decimalState: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)"]),
      entry: { kind: "closed" },
    };

    const structural = transferX2ValueStateForEdge(structuralState, plain(0x0a, "."), "normal", {}, 0);
    const decimal = transferX2ValueStateForEdge(decimalState, plain(0x0a, "."), "normal", {}, 0);

    expect(x2ShapeStateText(structural?.xShape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2VpEntryShapeText(structural)).toEqual(["hex:FACE:mantissa"]);
    expect(x2VpEntrySignShapeText(structural)).toEqual(["hex:FACE:mantissa"]);
    expect(x2ShapeStateText(decimal?.xShape)).toEqual(["mantissa:4:decimal"]);
    expect(x2VpEntryMantissaText(decimal)).toEqual(["4"]);
    expect(x2VpEntrySignShapeText(decimal)).toEqual(["mantissa:4:decimal"]);
  });

  it("x2 value dataflow derives VP sources from dot-restored structural exponent shapes", () => {
    const program: IrOp[] = [
      recall("2", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[3]?.xShape)).toEqual(["hex-exponent:Г:2"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex-exponent:Г:2"]);
    expect(x2VpEntryShapeText(states[4])).toEqual(["hex:Г00:mantissa"]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex-exponent:Г00:"]);
    expect(x2ShapeStateText(states[6]?.xShape)).toEqual(["hex-exponent:Г00:3"]);
  });

  it("x2 value dataflow syncs normalized X into X2 through F* empty opcodes", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["decimal:02:unnormalized"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:2:normalized"]);
  });

  it("x2 value dataflow syncs normalized X into X2 through stack lift", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["decimal:02:unnormalized"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:2:normalized"]);
  });

  it("x2 value dataflow tracks an opaque X/X2 equality through stack lift", () => {
    const program: IrOp[] = [
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      store("2"),
      plain(0x20, "Fπ"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[1]?.x)).toEqual(["expr:0"]);
    expect(x2ValueStateText(states[1]?.x2)).toEqual([]);
    expect(x2ValueStateText(states[2]?.x)).toEqual(["expr:0"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["expr:0"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["expr:0", "reg:2"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["expr:0", "reg:2"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["expr:0", "reg:2"]);
  });

  it("x2 value dataflow keeps opaque X/X2 equality through closed sign-change", () => {
    const program: IrOp[] = [
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      plain(0x0b, "/-/"),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toEqual(["expr:0"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["expr:0"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["expr:2"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["expr:2"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["expr:2", "reg:2"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["expr:2", "reg:2"]);
  });

  it("x2 value dataflow gives closed sign-change a stable expr key from stable sources", () => {
    const registerProgram: IrOp[] = [
      recall("1"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const nestedProgram: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const registerStates = computeX2ValueStates(registerProgram);
    const nestedStates = computeX2ValueStates(nestedProgram);

    expect(x2ValueStateText(registerStates[2]?.x)).toEqual(["expr-key:0B(reg:1)", "expr:1"]);
    expect(x2ValueStateText(registerStates[2]?.x2)).toEqual(["expr-key:0B(reg:1)", "expr:1"]);
    expect(x2ValueStateText(nestedStates[4]?.x)).toEqual([
      "expr-key:0B(expr-key:21(decimal:2:normalized))",
      "expr:3",
    ]);
    expect(x2ValueStateText(nestedStates[4]?.x2)).toEqual([
      "expr-key:0B(expr-key:21(decimal:2:normalized))",
      "expr:3",
    ]);
  });

  it("x2 value dataflow seeds opaque expr facts from pure unary X computations", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[2]?.x)).toEqual(["expr-key:21(decimal:2:normalized)", "expr:1"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["expr-key:21(decimal:2:normalized)", "expr:1"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["expr-key:21(decimal:2:normalized)", "expr:1"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["expr-key:21(decimal:2:normalized)", "expr:1", "reg:2"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["expr-key:21(decimal:2:normalized)", "expr:1", "reg:2"]);
  });

  it("x2 value dataflow gives repeated pure unary computations a stable expr key", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      store("1"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      store("2"),
      recall("1"),
      recall("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[2]?.x)).toContain("expr-key:21(decimal:2:normalized)");
    expect(x2ValueStateText(states[5]?.x)).toContain("expr-key:21(decimal:2:normalized)");
    expect(x2ValueStateText(states[7]?.x)).toContain("expr-key:21(decimal:2:normalized)");
    expect(x2ValueStateText(states[8]?.x)).toContain("expr-key:21(decimal:2:normalized)");
  });

  it("x2 value dataflow drops address-local opaque expr facts on backedges but keeps stable expr keys", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      store("1"),
      label("again"),
      loop("again"),
      recall("1"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[3]?.x)).toContain("expr-key:21(decimal:2:normalized)");
    expect(x2ValueStateText(states[3]?.x)).not.toContain("expr:1");
    expect(x2ValueStateText(states[3]?.memory?.["1"]) ?? []).toContain("expr-key:21(decimal:2:normalized)");
    expect(x2ValueStateText(states[3]?.memory?.["1"]) ?? []).not.toContain("expr:1");
  });

  it("x2 value dataflow drops address-local opaque expr facts across call returns but keeps stable expr keys", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      call("helper"),
      store("1"),
      jump("done"),
      label("helper"),
      ret(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[3]?.x)).toContain("expr-key:21(decimal:2:normalized)");
    expect(x2ValueStateText(states[3]?.x)).not.toContain("expr:1");
    expect(x2ValueStateText(states[4]?.memory?.["1"]) ?? []).toContain("expr-key:21(decimal:2:normalized)");
    expect(x2ValueStateText(states[4]?.memory?.["1"]) ?? []).not.toContain("expr:1");
  });

  it("x2-hidden-temp-restore uses stable expr keys across repeated pure unary computations", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      store("1"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      recall("1"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
  });

  it("x2-hidden-temp-restore uses stable expr keys across repeated stack-consuming computations", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      store("3"),
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      recall("3"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[10]).toMatchObject({ kind: "plain", opcode: 0x0a });
  });

  it("x2-hidden-temp-restore uses canonical stable expr keys for commutative computations", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      store("3"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      recall("3"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[10]).toMatchObject({ kind: "plain", opcode: 0x0a });
  });

  it("x2-hidden-temp-restore uses stable expr keys across repeated X/Y exchange computations", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x14, "X↔Y"),
      plain(0x11, "-"),
      store("3"),
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x14, "X↔Y"),
      plain(0x11, "-"),
      plain(0x0e, "В↑"),
      recall("3"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[12]).toMatchObject({ kind: "plain", opcode: 0x0a });
  });

  it("x2-hidden-temp-restore uses stable expr keys across repeated Y->X stack copies", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x3e, "Y->X"),
      plain(0x10, "+"),
      store("3"),
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x3e, "Y->X"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      recall("3"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[12]).toMatchObject({ kind: "plain", opcode: 0x0a });
  });

  it("x2-hidden-temp-restore uses stable expr keys across repeated Y-keeping computations", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x36, "К max"),
      plain(0x37, "К ∧"),
      store("3"),
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x36, "К max"),
      plain(0x37, "К ∧"),
      plain(0x0e, "В↑"),
      recall("3"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[12]).toMatchObject({ kind: "plain", opcode: 0x0a });
  });

  it("x2-hidden-temp-restore uses stable expr keys across repeated closed sign-change", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x0b, "/-/"),
      store("2"),
      recall("1"),
      plain(0x0b, "/-/"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
  });

  it("x2-hidden-temp-restore keeps register-dependent expr keys stable across direct-return helpers", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      call("transparent"),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[12]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps register-dependent expr keys stable across proved indirect-return helpers", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      knownTargetIndirectCall("7", 2),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[12]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps register-dependent expr keys stable when helpers read the source", () => {
    const program: IrOp[] = [
      jump("main"),
      label("read_source"),
      recall("1"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      call("read_source"),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[12]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps register-dependent expr keys stable when proved indirect helpers read the source", () => {
    const program: IrOp[] = [
      jump("main"),
      label("read_source"),
      recall("1"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      knownTargetIndirectCall("7", 2),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[12]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps register-dependent expr keys stable through nested helpers that read the source", () => {
    const program: IrOp[] = [
      jump("main"),
      label("read_source"),
      recall("1"),
      ret(),
      label("outer"),
      call("read_source"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      call("outer"),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[15]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps register-dependent expr recalls when nested helpers overwrite the source", () => {
    const program: IrOp[] = [
      jump("main"),
      label("overwrite_source"),
      plain(0x05, "5"),
      store("1"),
      ret(),
      label("outer"),
      call("overwrite_source"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      call("outer"),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);

    expect(restored.applied).toBe(0);
    expect(restored.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore keeps register-dependent expr recalls when a helper overwrites the source", () => {
    const program: IrOp[] = [
      jump("main"),
      label("overwrite_source"),
      plain(0x05, "5"),
      store("1"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      call("overwrite_source"),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);

    expect(restored.applied).toBe(0);
    expect(restored.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore keeps register-dependent expr keys stable across stable indirect selector reads", () => {
    const program: IrOp[] = [
      recall("8"),
      plain(0x31, "К |x|"),
      store("2"),
      knownTargetIndirectStore("8", "3"),
      recall("8"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[7]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 2);
  });

  it("x2-hidden-temp-restore keeps register-dependent expr recalls across mutating indirect selector reads", () => {
    const program: IrOp[] = [
      recall("4"),
      plain(0x31, "К |x|"),
      store("2"),
      knownTargetIndirectStore("4", "3"),
      recall("4"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);

    expect(restored.applied).toBe(0);
    expect(restored.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore keeps register-dependent expr keys stable across direct conditional fallthroughs", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      cjump("done"),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
      label("done"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[7]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps register-dependent expr keys stable across counted-loop fallthroughs", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      loop("done"),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
      label("done"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[7]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps register-dependent expr keys stable across proved indirect conditional fallthroughs", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      knownTargetIndirectCjump("7", 9),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
      label("done"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[7]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps register-dependent expr recalls when a branch overwrites the source", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      cjump("overwrite_source"),
      label("join"),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
      label("overwrite_source"),
      plain(0x05, "5"),
      store("1"),
      jump("join"),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);

    expect(restored.applied).toBe(0);
    expect(restored.ops).toEqual(program);
  });

  it("x2 value dataflow invalidates register-dependent expr keys after source overwrite", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      plain(0x05, "5"),
      store("1"),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[3]?.memory?.["2"])).toContain("expr-key:31(reg:1)");
    expect(x2ValueStateText(states[5]?.memory?.["2"])).not.toContain("expr-key:31(reg:1)");
    expect(x2ValueStateText(states[8]?.x2)).toContain("expr-key:31(reg:1)");
  });

  it("x2 value dataflow invalidates register-dependent expr keys after mutating indirect jump selectors", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      knownTargetIndirectJump("1", 4),
      label("tail"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[3]?.memory?.["2"])).toContain("expr-key:31(reg:1)");
    expect(x2ValueStateText(states[5]?.x)).not.toContain("expr-key:31(reg:1)");
    expect(x2ValueStateText(states[5]?.memory?.["2"])).not.toContain("expr-key:31(reg:1)");
  });

  it("x2 value dataflow invalidates register-dependent expr keys after mutating indirect call selectors", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      knownTargetIndirectCall("1", 6),
      plain(0x0e, "В↑"),
      recall("2"),
      label("callee"),
      plain(0x20, "Fπ"),
      ret(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[3]?.memory?.["2"])).toContain("expr-key:31(reg:1)");
    expect(x2ValueStateText(states[7]?.x)).not.toContain("expr-key:31(reg:1)");
    expect(x2ValueStateText(states[7]?.memory?.["2"]) ?? []).not.toContain("expr-key:31(reg:1)");
  });

  it("x2 value dataflow invalidates register-dependent expr keys after mutating indirect store selectors", () => {
    const mutatingProgram: IrOp[] = [
      recall("4"),
      plain(0x31, "К |x|"),
      store("2"),
      knownTargetIndirectStore("4", "3"),
      halt(),
    ];
    const stableProgram: IrOp[] = [
      recall("8"),
      plain(0x31, "К |x|"),
      store("2"),
      knownTargetIndirectStore("8", "3"),
      halt(),
    ];
    const mutatingStates = computeX2ValueStates(mutatingProgram, { trackRegisterMemory: true });
    const stableStates = computeX2ValueStates(stableProgram, { trackRegisterMemory: true });

    expect(x2ValueStateText(mutatingStates[3]?.memory?.["2"])).toContain("expr-key:31(reg:4)");
    expect(x2ValueStateText(mutatingStates[4]?.memory?.["2"]) ?? []).not.toContain("expr-key:31(reg:4)");
    expect(x2ValueStateText(stableStates[3]?.memory?.["2"])).toContain("expr-key:31(reg:8)");
    expect(x2ValueStateText(stableStates[4]?.memory?.["2"])).toContain("expr-key:31(reg:8)");
  });

  it("x2 value dataflow invalidates register-dependent expr keys only on mutating indirect conditional jump edges", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      knownTargetIndirectCjump("1", 7),
      plain(0x20, "Fπ"),
      jump("done"),
      label("target"),
      plain(0x0e, "В↑"),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[4]?.memory?.["2"])).toContain("expr-key:31(reg:1)");
    expect(states[7]).toBeDefined();
    expect(x2ValueStateText(states[7]?.x) ?? []).not.toContain("expr-key:31(reg:1)");
    expect(x2ValueStateText(states[7]?.memory?.["2"]) ?? []).not.toContain("expr-key:31(reg:1)");
  });

  it("x2-hidden-temp-restore keeps expr-key scratch recalls after a source register overwrite", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x31, "К |x|"),
      store("2"),
      plain(0x05, "5"),
      store("1"),
      recall("1"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[5]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(restored.ops[8]).toEqual(program[8]);
  });

  it("x2-hidden-temp-restore uses stable register sources after a later X2 sync", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      plain(0x0d, "Cx"),
      recall("1"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[4]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps register-source recalls after the source register is overwritten", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      plain(0x05, "5"),
      store("1"),
      recall("1"),
      plain(0x10, "+"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);

    expect(restored.applied).toBe(0);
    expect(restored.ops).toEqual(program);
  });

  it("x2 value dataflow seeds stable expr keys from structural shape operands", () => {
    const program: IrOp[] = [
      recall("1", "preload const FABC"),
      plain(0x31, "К |x|"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toContain("expr-key:31(shape:hex:FABC:mantissa)");
    expect(x2ValueStateText(states[2]?.x)).toContain("expr-key:31(reg:1)");
  });

  it("x2 value dataflow derives unary decimal facts from exact decimal display-shape operands", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0x31, "К |x|"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ShapeStateText(states[3]?.xShape)).toEqual(["exponent:1:8:decimal"]);
    expect(x2ValueStateText(states[4]?.x)).toContain("decimal:100000000:normalized");
    expect(x2ValueStateText(states[4]?.x)).not.toContain("expr-key:31(shape:exponent:1:8:decimal)");
    expect(x2ValueStateText(states[4]?.x)).not.toContain("expr-key:31(decimal:100000000:normalized)");
  });

  it("x2 value dataflow skips decimal display-shape expr keys when the same value fact is present", () => {
    const valueBackedShape: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["decimal:100000000:normalized"]),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["exponent:1:8:decimal"]),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(valueBackedShape, plain(0x16, "F e^x"), "normal", {}, 0);

    expect(x2ValueStateText(result?.x)).toContain("expr-key:16(decimal:100000000:normalized)");
    expect(x2ValueStateText(result?.x)).not.toContain("expr-key:16(shape:exponent:1:8:decimal)");
  });

  it("x2 value dataflow uses restored structural mantissas as stable expr keys", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x16, "F e^x"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[3]?.xShape)).toEqual(["hex-exponent:Г:2"]);
    expect(x2ValueStateText(states[4]?.x)).toContain("expr-key:16(shape:hex:Г00:mantissa)");
    expect(x2ValueStateText(states[4]?.x)).not.toContain("expr-key:16(shape:hex-exponent:Г:2)");
  });

  it("x2 value dataflow keeps structural stable expr operands ordered", () => {
    const leftStructural: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x06, "6"),
      plain(0x12, "×"),
      halt(),
    ];
    const rightStructural: IrOp[] = [
      plain(0x01, "1"),
      plain(0x06, "6"),
      plain(0x0e, "В↑"),
      recall("1", "preload const A"),
      plain(0x12, "×"),
      halt(),
    ];
    const leftStates = computeX2ValueStates(leftStructural, { trackRegisterMemory: true });
    const rightStates = computeX2ValueStates(rightStructural, { trackRegisterMemory: true });

    expect(x2ValueStateText(leftStates[5]?.x)).toContain(
      "expr-key:12(shape:hex:A:mantissa,decimal:16:normalized)",
    );
    expect(x2ValueStateText(leftStates[5]?.x)).not.toContain("decimal:160:normalized");
    expect(x2ShapeStateText(leftStates[5]?.xShape)).toEqual(["mantissa:000:decimal"]);
    expect(x2ValueStateText(rightStates[5]?.x)).toContain("decimal:160:normalized");
    expect(x2ValueStateText(rightStates[5]?.x)).not.toContain(
      "expr-key:12(shape:hex:A:mantissa,decimal:16:normalized)",
    );
    expect(x2ShapeStateText(rightStates[5]?.xShape)).toEqual(["mantissa:160:decimal"]);
  });

  it("x2 value dataflow materializes decimal stable expr-key shapes on explicit X2 sync", () => {
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)"]),
      x2: new Set(),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(legacy, plain(0x0e, "В↑"), "normal", {}, 0);

    expect(x2ValueStateText(result?.x2)).toContain("decimal:4:normalized");
    expect(x2ShapeStateText(result?.x2Shape)).toContain("mantissa:4:decimal");
  });

  it("x2 value dataflow materializes structural stable expr-key shapes on explicit X2 sync", () => {
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:31(shape:hex:-8F:mantissa)"]),
      x2: new Set(),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(legacy, plain(0x0e, "В↑"), "normal", {}, 0);

    expect(x2ValueStateText(result?.x2)).toContain("expr-key:31(shape:hex:-8F:mantissa)");
    expect(x2ShapeStateText(result?.x2Shape)).toContain("hex:8F:mantissa");
  });

  it("x2 value dataflow materializes structural stable expr-key shapes through stack lift", () => {
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:31(shape:hex:-FACE:mantissa)"]),
      x2: new Set(),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(legacy, plain(0x0e, "В↑"), "normal", {}, 0);

    expect(x2ShapeStateText(result?.xShape)).toContain("hex:FACE:mantissa");
    expect(x2ShapeStateText(result?.yShape)).toContain("hex:FACE:mantissa");
    expect(x2ShapeStateText(result?.x2Shape)).toContain("hex:FACE:mantissa");
  });

  it("x2 value dataflow materializes decimal stable expr-key facts on direct recall X2 sync", () => {
    const legacy: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      entry: { kind: "closed" },
      memory: {
        "1": new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)"]),
      },
    };
    const result = transferX2ValueStateForEdge(legacy, recall("1"), "normal", { trackRegisterMemory: true }, 0);

    expect(x2ValueStateText(result?.x2)).toContain("expr-key:22(decimal:2:normalized)");
    expect(x2ValueStateText(result?.x2)).toContain("decimal:4:normalized");
    expect(x2ShapeStateText(result?.x2Shape)).toContain("mantissa:4:decimal");
  });

  it("x2 value dataflow materializes structural stable expr-key shapes on known indirect recall X2 sync", () => {
    const legacy: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      entry: { kind: "closed" },
      memory: {
        "2": new Set<X2ValueFact>(["expr-key:31(shape:hex:-8F:mantissa)"]),
      },
    };
    const result = transferX2ValueStateForEdge(
      legacy,
      knownTargetIndirectRecall("7", "2"),
      "normal",
      { trackRegisterMemory: true },
      0,
    );

    expect(x2ValueStateText(result?.x2)).toContain("expr-key:31(shape:hex:-8F:mantissa)");
    expect(x2ShapeStateText(result?.x2Shape)).toContain("hex:8F:mantissa");
  });

  it("x2 value dataflow stores materialized decimal stable expr-key facts in register memory", () => {
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)"]),
      x2: new Set(),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(legacy, store("1"), "normal", { trackRegisterMemory: true }, 0);

    expect(x2ValueStateText(result?.memory?.["1"])).toContain("expr-key:22(decimal:2:normalized)");
    expect(x2ValueStateText(result?.memory?.["1"])).toContain("decimal:4:normalized");
    expect(x2ShapeStateText(result?.shapeMemory?.["1"])).toContain("mantissa:4:decimal");
  });

  it("x2 value dataflow stores materialized structural stable expr-key shapes in indirect register memory", () => {
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:31(shape:hex:-8F:mantissa)"]),
      x2: new Set(),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(
      legacy,
      knownTargetIndirectStore("7", "2"),
      "normal",
      { trackRegisterMemory: true },
      0,
    );

    expect(x2ValueStateText(result?.memory?.["2"])).toContain("expr-key:31(shape:hex:-8F:mantissa)");
    expect(x2ShapeStateText(result?.shapeMemory?.["2"])).toContain("hex:8F:mantissa");
  });

  it("x2 recall proof derives structural sync shapes from stable value memory", () => {
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      x2Shape: new Set<X2ShapeFact>(["hex:8F:mantissa"]),
      entry: { kind: "closed" },
      memory: {
        "1": new Set<X2ValueFact>(["expr-key:31(shape:hex:-8F:mantissa)"]),
      },
    };

    expect(recallValueProof(recall("1"), state)?.x2SyncShape).toBe(true);
  });

  it("x2 recall proof derives decimal display sync shapes from stable value memory", () => {
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      x2Shape: new Set<X2ShapeFact>(["mantissa:4:decimal"]),
      entry: { kind: "closed" },
      memory: {
        "1": new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)"]),
      },
    };

    expect(recallValueProof(recall("1"), state)?.x2SyncDisplayValue).toBe(true);
  });

  it("x2 state predicates derive structural X2 shape from stable expr-key values", () => {
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set<X2ValueFact>(["expr-key:31(shape:hex:-A:mantissa)"]),
      entry: { kind: "closed" },
    };

    expect(x2StateHasStructuralShapeX2(state)).toBe(true);
    expect(x2StateHasOnlyDotSafeStructuralMantissaX2(state)).toBe(true);
    expect(x2StateHasUnsafeDotRestoreShapeX2(state)).toBe(true);
  });

  it("x2 state predicates compare stable expr-key structural shapes without explicit shape facts", () => {
    const state: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:31(shape:hex:-A:mantissa)"]),
      x2: new Set<X2ValueFact>(["expr-key:31(shape:hex:A:mantissa)"]),
      entry: { kind: "closed" },
    };

    expect(x2StateHasSameStructuralShapeInXAndX2(state)).toBe(true);
    expect(x2StateHasSameDotSafeStructuralMantissaInXAndX2(state)).toBe(true);
    expect(x2StateHasSameDotRestoreValueInXAndX2(state)).toBe(true);
  });

  it("x2 state predicates expose stable expr-key visible display shapes", () => {
    const integer: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)"]),
      x2: new Set(),
      entry: { kind: "closed" },
    };
    const fractional: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:35(decimal:0.5:normalized)"]),
      x2: new Set(),
      entry: { kind: "closed" },
    };

    expect(x2ShapeSetHasExactIntegerDisplay(integer.xShape)).toBe(false);
    expect(x2ShapeSetHasExactIntegerDisplay(effectiveVisibleXStateShape(integer))).toBe(true);
    expect([...x2ShapeSetRestoredVisibleDecimals(effectiveVisibleXStateShape(fractional))]).toEqual(["0.5"]);
  });

  it("x2 value dataflow joins materialized stable decimal facts across mixed paths", () => {
    const left: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)"]),
      x2: new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)"]),
      memory: {
        "1": new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)"]),
      },
      entry: { kind: "closed" },
    };
    const right: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)", "decimal:4:normalized"]),
      x2: new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)", "decimal:4:normalized"]),
      xShape: new Set<X2ShapeFact>(["mantissa:4:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["mantissa:4:decimal"]),
      memory: {
        "1": new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)", "decimal:4:normalized"]),
      },
      shapeMemory: {
        "1": new Set<X2ShapeFact>(["mantissa:4:decimal"]),
      },
      entry: { kind: "closed" },
    };
    const joined = joinX2ValueDataflowStates(left, right, true);

    expect(x2ValueStateText(joined.x)).toContain("expr-key:22(decimal:2:normalized)");
    expect(x2ValueStateText(joined.x)).toContain("decimal:4:normalized");
    expect(x2ShapeStateText(joined.xShape)).toContain("mantissa:4:decimal");
    expect(x2ValueStateText(joined.x2)).toContain("decimal:4:normalized");
    expect(x2ShapeStateText(joined.x2Shape)).toContain("mantissa:4:decimal");
    expect(x2ValueStateText(joined.memory?.["1"])).toContain("decimal:4:normalized");
    expect(x2ShapeStateText(joined.shapeMemory?.["1"])).toContain("mantissa:4:decimal");
  });

  it("x2 value dataflow joins stable structural shape facts across mixed paths", () => {
    const left: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:31(shape:hex:-A:mantissa)"]),
      x2: new Set<X2ValueFact>(["expr-key:31(shape:hex:-A:mantissa)"]),
      memory: {
        "1": new Set<X2ValueFact>(["expr-key:31(shape:hex:-A:mantissa)"]),
      },
      entry: { kind: "closed" },
    };
    const right: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:31(shape:hex:-A:mantissa)"]),
      x2: new Set<X2ValueFact>(["expr-key:31(shape:hex:-A:mantissa)"]),
      xShape: new Set<X2ShapeFact>(["hex:A:mantissa"]),
      x2Shape: new Set<X2ShapeFact>(["hex:A:mantissa"]),
      memory: {
        "1": new Set<X2ValueFact>(["expr-key:31(shape:hex:-A:mantissa)"]),
      },
      shapeMemory: {
        "1": new Set<X2ShapeFact>(["hex:A:mantissa"]),
      },
      entry: { kind: "closed" },
    };
    const joined = joinX2ValueDataflowStates(left, right, true);

    expect(x2ShapeStateText(joined.xShape)).toContain("hex:A:mantissa");
    expect(x2ShapeStateText(joined.x2Shape)).toContain("hex:A:mantissa");
    expect(x2ShapeStateText(joined.shapeMemory?.["1"])).toContain("hex:A:mantissa");
  });

  it("x2 value dataflow join keeps raw decimals distinct from normalized decimals", () => {
    const left: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["decimal:02:unnormalized"]),
      x2: new Set<X2ValueFact>(["decimal:02:unnormalized"]),
      entry: { kind: "closed" },
    };
    const right: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["decimal:2:normalized"]),
      x2: new Set<X2ValueFact>(["decimal:2:normalized"]),
      entry: { kind: "closed" },
    };
    const joined = joinX2ValueDataflowStates(left, right);

    expect(x2ValueStateText(joined.x)).toEqual([]);
    expect(x2ValueStateText(joined.x2)).toEqual([]);
  });

  it("x2 value dataflow canonicalizes structural shape sources inside stable expr keys", () => {
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([
        "expr-key:16(shape:hex-exponent:Г:2)",
        "expr-key:16(shape:super-exponent:FA:2)",
      ]),
      x2: new Set(),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(legacy, plain(0x31, "К |x|"), "normal", {}, 0);

    expect(x2ValueStateText(result?.x)).toContain("expr-key:31(expr-key:16(shape:hex:Г00:mantissa))");
    expect(x2ValueStateText(result?.x)).toContain("expr-key:31(expr-key:16(shape:hex:FA00:mantissa))");
    expect(x2ValueStateText(result?.x)).not.toContain("expr-key:31(expr-key:16(shape:hex-exponent:Г:2))");
    expect(x2ValueStateText(result?.x)).not.toContain("expr-key:31(expr-key:16(shape:super-exponent:FA:2))");
  });

  it("x2 value dataflow canonicalizes exact structural decimal shape sources inside stable expr keys", () => {
    const exact = "expr-key:31(shape:hex:123:mantissa)" as X2ValueFact;
    const exactExponent = "expr-key:31(shape:hex-exponent:123:1)" as X2ValueFact;
    const rawLeadingZero = "expr-key:31(shape:hex:0123:mantissa)" as X2ValueFact;
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([exact, exactExponent, rawLeadingZero]),
      x2: new Set(),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(legacy, plain(0x54, "К НОП"), "normal", {}, 0);

    expect(x2ValueStateText(result?.x)).toContain("expr-key:31(decimal:123:normalized)");
    expect(x2ValueStateText(result?.x)).toContain("expr-key:31(decimal:1230:normalized)");
    expect(x2ValueStateText(result?.x)).toContain("expr-key:31(shape:hex:0123:mantissa)");
    expect(x2ValueStateText(result?.x)).not.toContain(exact);
    expect(x2ValueStateText(result?.x)).not.toContain(exactExponent);
  });

  it("x2 value dataflow canonicalizes stable expr keys in set and memory operations", () => {
    const raw = "expr-key:16(shape:hex-exponent:Г:2)" as X2ValueFact;
    const canonical = "expr-key:16(shape:hex:Г00:mantissa)" as X2ValueFact;
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([raw]),
      x2: new Set<X2ValueFact>([raw]),
      entry: { kind: "closed" },
    };
    const stored = transferX2ValueStateForEdge(legacy, store("1"), "normal", { trackRegisterMemory: true }, 0);
    const recalled = transferX2ValueStateForEdge(stored, recall("1"), "normal", { trackRegisterMemory: true }, 1);

    expect(x2ValueSetHasIntersection(new Set([raw]), new Set([canonical]))).toBe(true);
    expect(x2ValueStateText(stored?.x)).toContain(canonical);
    expect(x2ValueStateText(stored?.x)).not.toContain(raw);
    expect(x2ValueStateText(stored?.memory?.["1"])).toContain(canonical);
    expect(x2ValueStateText(stored?.memory?.["1"])).not.toContain(raw);
    expect(x2ValueStateText(recalled?.x)).toContain(canonical);
    expect(x2ValueStateText(recalled?.x)).not.toContain(raw);
  });

  it("x2 value dataflow matches stable expr keys canonically in value sets", () => {
    const raw = "expr-key:16(shape:hex-exponent:Г:2)" as X2ValueFact;
    const canonical = "expr-key:16(shape:hex:Г00:mantissa)" as X2ValueFact;

    expect(x2ValueSetHasFact(new Set([raw]), canonical)).toBe(true);
    expect(x2ValueSetHasFact(new Set([canonical]), raw)).toBe(true);
  });

  it("x2 value dataflow drops stable expr keys with invalid shape operands", () => {
    const invalidSuper = "expr-key:16(shape:super:8A)" as X2ValueFact;
    const invalidHex = "expr-key:16(shape:hex:8Ж:mantissa)" as X2ValueFact;
    const valid = "expr-key:16(shape:super:FA)" as X2ValueFact;
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([invalidSuper, invalidHex, valid]),
      x2: new Set<X2ValueFact>([invalidSuper, invalidHex, valid]),
      memory: {
        "2": new Set<X2ValueFact>([invalidSuper, valid]),
      },
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(legacy, plain(0x31, "К |x|"), "normal", { trackRegisterMemory: true }, 0);

    expect(x2ValueSetHasFact(new Set([invalidSuper]), invalidSuper)).toBe(false);
    expect(x2ValueSetHasIntersection(new Set([invalidHex]), new Set([invalidHex]))).toBe(false);
    expect(x2ValueStateText(result?.x)).toContain("expr-key:31(expr-key:16(shape:super:FA))");
    expect(x2ValueStateText(result?.x)).not.toContain("expr-key:31(expr-key:16(shape:super:8A))");
    expect(x2ValueStateText(result?.x)).not.toContain("expr-key:31(expr-key:16(shape:hex:8Ж:mantissa))");
  });

  it("x2 value dataflow drops invalid shape facts from state and shape memory", () => {
    const legacy: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:BAD:decimal", "super:8A", "super:FA", "hex:8Ж:mantissa"]),
      x2Shape: new Set<X2ShapeFact>(["mantissa:BAD:decimal", "super:8A", "super:FA", "hex:8Ж:mantissa"]),
      shapeMemory: {
        "2": new Set<X2ShapeFact>(["mantissa:BAD:decimal", "super:8A", "super:FA", "hex:8Ж:mantissa"]),
      },
      entry: { kind: "closed" },
    };
    const cloned = transferX2ValueStateForEdge(legacy, label("clone"), "normal", { trackRegisterMemory: true }, 0);
    const recalled = transferX2ValueStateForEdge(cloned, recall("2"), "normal", { trackRegisterMemory: true }, 1);

    expect(x2ShapeStateText(cloned?.xShape)).toEqual(["super:FA"]);
    expect(x2ShapeStateText(cloned?.x2Shape)).toEqual(["super:FA"]);
    expect(x2ShapeStateText(cloned?.shapeMemory?.["2"])).toEqual(["super:FA"]);
    expect(x2ShapeStateText(recalled?.xShape)).toEqual(["super:FA"]);
    expect(x2ShapeStateText(recalled?.x2Shape)).toEqual(["super:FA"]);
  });

  it("x2 value dataflow drops invalid shape facts while restoring X through dot", () => {
    const legacy: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      x2Shape: new Set<X2ShapeFact>([
        "mantissa:02:decimal",
        "super:8A",
        "super:FA",
        "hex:8Ж:mantissa",
      ]),
      entry: { kind: "closed" },
    };
    const restored = transferX2ValueStateForEdge(legacy, plain(0x0a, "."), "normal", {}, 0);

    expect(x2ShapeStateText(restored?.xShape)).toEqual(["mantissa:2:decimal", "super:FA"]);
    expect(x2ShapeStateText(restored?.x2Shape)).toEqual(["mantissa:02:decimal", "super:FA"]);
  });

  it("x2 value dataflow canonicalizes structural exponent contexts", () => {
    const activeLegacy: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      entry: { kind: "closed" },
      structuralEntry: {
        kind: "exponent",
        mantissa: new Set<X2ShapeFact>(["super:8A", "super:FA", "hex:8Ж:mantissa"]),
        exponent: new Set([" 2 ", "BAD"]),
      },
    };
    const vpLegacy: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      entry: { kind: "closed" },
      structuralVpContext: {
        kind: "exponent",
        mantissa: new Set<X2ShapeFact>(["super:8A", "super:FA", "hex:8Ж:mantissa"]),
        exponent: new Set([" 3 ", "BAD"]),
      },
    };
    const invalidLegacy: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      entry: { kind: "closed" },
      structuralEntry: {
        kind: "exponent",
        mantissa: new Set<X2ShapeFact>(["super:8A", "hex:8Ж:mantissa"]),
        exponent: new Set(["2"]),
      },
    };

    const active = analyzeX2VpShapeContext(
      transferX2ValueStateForEdge(activeLegacy, label("active"), "normal", { trackRegisterMemory: true }, 0),
    );
    const vpContext = analyzeX2VpShapeContext(
      transferX2ValueStateForEdge(vpLegacy, label("vp"), "normal", { trackRegisterMemory: true }, 0),
    );
    const invalid = analyzeX2VpShapeContext(
      transferX2ValueStateForEdge(invalidLegacy, label("invalid"), "normal", { trackRegisterMemory: true }, 0),
    );

    expect(active.kind).toBe("active-structural-exponent");
    expect(x2ShapeStateText(active.shape)).toEqual(["super:FA"]);
    expect([...(active.exponent ?? [])]).toEqual(["2"]);
    expect(vpContext.kind).toBe("vp-structural-exponent-context");
    expect(x2ShapeStateText(vpContext.shape)).toEqual(["super:FA"]);
    expect([...(vpContext.exponent ?? [])]).toEqual(["3"]);
    expect(invalid.kind).toBe("unknown");
  });

  it("x2 value dataflow canonicalizes decimal entry and VP contexts", () => {
    const openLegacy: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      entry: { kind: "open", raw: new Set([" 01 ", "BAD"]) },
    };
    const activeLegacy: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      entry: {
        kind: "exponent",
        mantissa: new Set([" 02 ", "100000000", "BAD"]),
        exponent: new Set([" 3 ", "BAD"]),
      },
    };
    const vpLegacy: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      entry: { kind: "closed" },
      vpContext: {
        kind: "exponent",
        mantissa: new Set([" 5 ", "BAD"]),
        exponent: new Set([" -1 ", "BAD"]),
      },
    };
    const invalidLegacy: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      entry: {
        kind: "exponent",
        mantissa: new Set(["BAD"]),
        exponent: new Set(["2"]),
      },
    };

    const open = transferX2ValueStateForEdge(openLegacy, label("open"), "normal", { trackRegisterMemory: true }, 0);
    const active = analyzeX2VpShapeContext(
      transferX2ValueStateForEdge(activeLegacy, label("active"), "normal", { trackRegisterMemory: true }, 0),
    );
    const vpContext = analyzeX2VpShapeContext(
      transferX2ValueStateForEdge(vpLegacy, label("vp"), "normal", { trackRegisterMemory: true }, 0),
    );
    const invalid = analyzeX2VpShapeContext(
      transferX2ValueStateForEdge(invalidLegacy, label("invalid"), "normal", { trackRegisterMemory: true }, 0),
    );

    expect(x2EntryStateText(open)).toBe("open:01");
    expect(active.kind).toBe("active-exponent");
    expect([...(active.mantissa ?? [])].sort()).toEqual(["02", "100000000"]);
    expect([...(active.exponent ?? [])]).toEqual(["3"]);
    expect(vpContext.kind).toBe("vp-exponent-context");
    expect([...(vpContext.mantissa ?? [])]).toEqual(["5"]);
    expect([...(vpContext.exponent ?? [])]).toEqual(["-1"]);
    expect(invalid.kind).toBe("unknown");
  });

  it("x2 value dataflow canonicalizes stable expr survivors after selector mutation", () => {
    const raw = "expr-key:16(shape:hex-exponent:Г:2)" as X2ValueFact;
    const canonical = "expr-key:16(shape:hex:Г00:mantissa)" as X2ValueFact;
    const dependent = "expr-key:31(reg:1)" as X2ValueFact;
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([raw, dependent]),
      y: new Set<X2ValueFact>([raw]),
      x2: new Set<X2ValueFact>([raw, dependent]),
      memory: {
        "2": new Set<X2ValueFact>([raw, dependent]),
      },
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(
      legacy,
      indirectJump("1"),
      "normal",
      { trackRegisterMemory: true },
      0,
    );

    expect(x2ValueStateText(result?.x)).toContain(canonical);
    expect(x2ValueStateText(result?.x)).not.toContain(raw);
    expect(x2ValueStateText(result?.x)).not.toContain(dependent);
    expect(x2ValueStateText(result?.y)).toContain(canonical);
    expect(x2ValueStateText(result?.x2)).toContain(canonical);
    expect(x2ValueStateText(result?.x2)).not.toContain(dependent);
    expect(x2ValueStateText(result?.memory?.["2"])).toContain(canonical);
    expect(x2ValueStateText(result?.memory?.["2"])).not.toContain(dependent);
  });

  it("x2 value dataflow canonicalizes stable expr memory while cloning through labels", () => {
    const raw = "expr-key:16(shape:hex-exponent:Г:2)" as X2ValueFact;
    const canonical = "expr-key:16(shape:hex:Г00:mantissa)" as X2ValueFact;
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([raw]),
      x2: new Set<X2ValueFact>([raw]),
      memory: {
        "2": new Set<X2ValueFact>([raw]),
      },
      shapeMemory: {
        "2": new Set<X2ShapeFact>(["hex-exponent:Г:2"]),
      },
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(
      legacy,
      label("clone"),
      "normal",
      { trackRegisterMemory: true },
      0,
    );

    expect(x2ValueStateText(result?.x2)).toContain(canonical);
    expect(x2ValueStateText(result?.memory?.["2"])).toContain(canonical);
    expect(x2ShapeStateText(result?.shapeMemory?.["2"])).toEqual(["hex-exponent:Г:2"]);
    expect(result?.memory).not.toBe(legacy.memory);
    expect(result?.shapeMemory).not.toBe(legacy.shapeMemory);
  });

  it("x2 value dataflow canonicalizes stable expr memory through preserving plain ops", () => {
    const raw = "expr-key:16(shape:hex-exponent:Г:2)" as X2ValueFact;
    const canonical = "expr-key:16(shape:hex:Г00:mantissa)" as X2ValueFact;
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([raw]),
      x2: new Set<X2ValueFact>([raw]),
      memory: {
        "2": new Set<X2ValueFact>([raw]),
      },
      shapeMemory: {
        "2": new Set<X2ShapeFact>(["hex-exponent:Г:2"]),
      },
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(
      legacy,
      plain(0x54, "К НОП"),
      "normal",
      { trackRegisterMemory: true },
      0,
    );

    expect(x2ValueStateText(result?.x)).toContain(canonical);
    expect(x2ValueStateText(result?.x2)).toContain(canonical);
    expect(x2ValueStateText(result?.memory?.["2"])).toContain(canonical);
    expect(x2ValueStateText(result?.memory?.["2"])).not.toContain(raw);
    expect(x2ShapeStateText(result?.shapeMemory?.["2"])).toEqual(["hex-exponent:Г:2"]);
    expect(result?.memory).not.toBe(legacy.memory);
    expect(result?.shapeMemory).not.toBe(legacy.shapeMemory);
  });

  it("x2 value dataflow uses canonical stable expr keys for closed sign-change proofs", () => {
    const raw = "expr-key:16(shape:hex-exponent:Г:2)" as X2ValueFact;
    const canonical = "expr-key:16(shape:hex:Г00:mantissa)" as X2ValueFact;
    const state: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([raw]),
      x2: new Set<X2ValueFact>([canonical]),
      entry: { kind: "closed" },
    };
    const signed = transferX2ValueStateForEdge(state, plain(0x0b, "/-/"), "normal", {}, 0);

    expect(x2ValueStateText(signed?.x)).toContain("expr-key:0B(expr-key:16(shape:hex:Г00:mantissa))");
    expect(x2ValueStateText(signed?.x)).toContain("expr:0");
    expect(x2ValueStateText(signed?.x)).not.toContain("expr-key:0B(expr-key:16(shape:hex-exponent:Г:2))");
    expect(x2ValueStateText(signed?.x2)).toEqual(x2ValueStateText(signed?.x));
  });

  it("x2 value dataflow sign-changes materialized stable expr-key shapes", () => {
    const structuralSource = "expr-key:31(shape:hex:-A:mantissa)" as X2ValueFact;
    const structuralState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([structuralSource]),
      x2: new Set<X2ValueFact>([structuralSource]),
      entry: { kind: "closed" },
    };
    const signedStructural = transferX2ValueStateForEdge(
      structuralState,
      plain(0x0b, "/-/"),
      "normal",
      {},
      0,
    );

    expect(x2ValueStateText(signedStructural?.x)).toContain("expr-key:0B(shape:hex:A:mantissa)");
    expect(x2ShapeStateText(signedStructural?.xShape)).toEqual(["hex:-A:mantissa"]);
    expect(x2ShapeStateText(signedStructural?.x2Shape)).toEqual(["hex:-A:mantissa"]);

    const decimalSource = "expr-key:22(decimal:2:normalized)" as X2ValueFact;
    const decimalState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([decimalSource]),
      x2: new Set<X2ValueFact>([decimalSource]),
      entry: { kind: "closed" },
    };
    const signedDecimal = transferX2ValueStateForEdge(decimalState, plain(0x0b, "/-/"), "normal", {}, 0);

    expect(x2ValueStateText(signedDecimal?.x)).toEqual(["decimal:-4:normalized"]);
    expect(x2ValueStateText(signedDecimal?.x2)).toEqual(["decimal:-4:normalized"]);
    expect(x2ShapeStateText(signedDecimal?.xShape)).toEqual(["mantissa:-4:decimal"]);
    expect(x2ShapeStateText(signedDecimal?.x2Shape)).toEqual(["mantissa:-4:decimal"]);
  });

  it("x2 value dataflow creates VP sources from materialized stable expr-key shapes", () => {
    const structuralSource = "expr-key:31(shape:hex:-A:mantissa)" as X2ValueFact;
    const structuralState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([structuralSource]),
      x2: new Set<X2ValueFact>([structuralSource]),
      entry: { kind: "closed" },
    };
    const structuralSync = transferX2ValueStateForEdge(
      structuralState,
      plain(0xf0, "F0"),
      "normal",
      {},
      0,
    );

    expect(x2VpEntryShapeText(structuralSync)).toEqual(["hex:A:mantissa"]);

    const decimalSource = "expr-key:22(decimal:2:normalized)" as X2ValueFact;
    const decimalState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([decimalSource]),
      x2: new Set<X2ValueFact>([decimalSource]),
      entry: { kind: "closed" },
    };
    const decimalSync = transferX2ValueStateForEdge(decimalState, plain(0xf0, "F0"), "normal", {}, 0);

    expect(x2VpEntryMantissaText(decimalSync)).toEqual(["4"]);
    expect(x2VpEntrySignShapeText(decimalSync)).toEqual(["mantissa:4:decimal"]);
  });

  it("x2 value dataflow derives unary decimal facts from shape-only decimal mantissas", () => {
    const shapeOnly: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      entry: { kind: "closed" },
    };
    const valueBacked: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["decimal:100:normalized"]),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      entry: { kind: "closed" },
    };
    const shapeOnlyResult = transferX2ValueStateForEdge(shapeOnly, plain(0x31, "К |x|"), "normal", {}, 0);
    const valueBackedResult = transferX2ValueStateForEdge(valueBacked, plain(0x31, "К |x|"), "normal", {}, 0);

    expect(x2ValueStateText(shapeOnlyResult?.x)).toContain("decimal:100:normalized");
    expect(x2ValueStateText(shapeOnlyResult?.x)).not.toContain("expr-key:31(shape:mantissa:100:decimal)");
    expect(x2ValueStateText(valueBackedResult?.x)).not.toContain("expr-key:31(shape:mantissa:100:decimal)");
  });

  it("x2 value dataflow treats raw decimal value facts as numeric sources for pure computations", () => {
    const rawValueState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["decimal:02:unnormalized"]),
      x2: new Set(),
      xShape: new Set(),
      entry: { kind: "closed" },
    };
    const rawBinaryState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["decimal:02:unnormalized"]),
      y: new Set<X2ValueFact>(["decimal:3:normalized"]),
      x2: new Set(),
      xShape: new Set(),
      yShape: new Set(),
      entry: { kind: "closed" },
    };
    const stable = transferX2ValueStateForEdge(rawValueState, plain(0x1c, "F sin"), "normal", {}, 0);
    const unary = transferX2ValueStateForEdge(rawValueState, plain(0x31, "К |x|"), "normal", {}, 0);
    const binary = transferX2ValueStateForEdge(rawBinaryState, plain(0x10, "+"), "normal", {}, 0);

    expect(x2ValueStateText(stable?.x)).toContain("expr-key:1C(decimal:2:normalized)");
    expect(x2ValueStateText(unary?.x)).toContain("decimal:2:normalized");
    expect(x2ValueStateText(binary?.x)).toContain("decimal:5:normalized");
  });

  it("x2 value dataflow derives unary display shapes from shape-only exact decimal sources", () => {
    const shapeOnly: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(shapeOnly, plain(0x35, "К {x}"), "normal", {}, 0);

    expect(x2ShapeStateText(result?.xShape)).toEqual(["exponent:5:-1:decimal"]);
    expect(x2ValueStateText(result?.x)).toContain("decimal:0.5:normalized");
    expect(x2ValueStateText(result?.x)).not.toContain("expr-key:35(shape:exponent:5:-1:decimal)");
  });

  it("x2 value dataflow seeds stable expr keys from constant stack producers", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x20, "F pi"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toEqual([
      "decimal:3.1415926:normalized",
      "expr-key:20()",
    ]);
    expect(x2ShapeStateText(states[2]?.xShape)).toEqual(["mantissa:3.1415926:decimal"]);
    expect(x2ValueStateText(states[2]?.y)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["decimal:2:normalized"]);
  });

  it("x2 value dataflow stores stable constant display shapes", () => {
    const program: IrOp[] = [
      plain(0x20, "F pi"),
      store("3"),
      plain(0x0d, "Cx"),
      recall("3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[2]?.shapeMemory?.["3"])).toEqual(["mantissa:3.1415926:decimal"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["mantissa:3.1415926:decimal"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["mantissa:3.1415926:decimal"]);
  });

  it("x2 value dataflow derives concrete decimal facts from F pi", () => {
    const program: IrOp[] = [
      plain(0x20, "F pi"),
      plain(0x34, "К [x]"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toEqual([
      "decimal:3:normalized",
      "expr:1",
    ]);
  });

  it("x2 value dataflow evaluates stable expr keys recursively as concrete sources", () => {
    const constantState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:20()"]),
      x2: new Set(),
      entry: { kind: "closed" },
    };
    const structuralState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:31(shape:hex:-8F:mantissa)"]),
      x2: new Set(),
      entry: { kind: "closed" },
    };

    const constant = transferX2ValueStateForEdge(constantState, plain(0x34, "К [x]"), "normal", {}, 0);
    const structural = transferX2ValueStateForEdge(structuralState, plain(0x32, "К ЗН"), "normal", {}, 0);

    expect(x2ValueStateText(constant?.x)).toContain("decimal:3:normalized");
    expect(x2ValueStateText(constant?.x)).not.toContain("expr-key:34(expr-key:20())");
    expect(x2ValueStateText(structural?.x)).toContain("decimal:1:normalized");
    expect(x2ShapeStateText(structural?.xShape)).toContain("mantissa:1:decimal");
    expect(x2ValueStateText(structural?.x)).not.toContain(
      "expr-key:32(expr-key:31(shape:hex:-8F:mantissa))",
    );
  });

  it("x2-hidden-temp-restore uses stable expr keys across repeated constant stack producers", () => {
    const program: IrOp[] = [
      plain(0x20, "F pi"),
      store("3"),
      plain(0x0d, "Cx"),
      plain(0x20, "F pi"),
      plain(0x0e, "В↑"),
      recall("3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[5]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses structural shape expr keys across different registers", () => {
    const program: IrOp[] = [
      recall("1", "preload const FABC"),
      plain(0x31, "К |x|"),
      store("3"),
      recall("2", "preload const FABC"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses decimal exponent display-shape expr keys", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0x31, "К |x|"),
      store("3"),
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["exponent:1:8:decimal"]);
    expect(x2ShapeStateText(states[10]?.x2Shape)).toEqual(["exponent:1:8:decimal"]);
    expect(x2ShapeSetsHaveSameDotSafeDecimal(states[4]?.xShape, states[10]?.x2Shape)).toBe(false);
    expect(x2ShapeSetsHaveSameDecimalDisplayShape(states[4]?.xShape, states[10]?.x2Shape)).toBe(true);
    expect(restored.applied).toBe(1);
    expect(restored.ops[10]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses emulator-pinned hex multiply facts after X2 sync", () => {
    const program: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      store("3"),
      plain(0xf0, "F* empty F0"),
      recall("3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[7]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses emulator-pinned hex B multiply facts after X2 sync", () => {
    const program: IrOp[] = [
      recall("1", "preload const B"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      store("3"),
      plain(0xf0, "F* empty F0"),
      recall("3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[7]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses emulator-pinned structural square facts after X2 sync", () => {
    const program: IrOp[] = [
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      store("3"),
      plain(0x0d, "Cx"),
      recall("2", "preload const 0B"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      recall("3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[7]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses emulator-pinned super square facts after X2 sync", () => {
    const program: IrOp[] = [
      recall("1", "preload const FA"),
      plain(0x22, "F x^2"),
      store("3"),
      plain(0x0d, "Cx"),
      plain(0x00, "0"),
      plain(0xf0, "F* empty F0"),
      recall("3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses emulator-pinned hex addition facts after X2 sync", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0e, "В↑"),
      plain(0x04, "4"),
      plain(0x10, "+"),
      store("3"),
      plain(0x0d, "Cx"),
      recall("2", "preload const Г"),
      plain(0x0e, "В↑"),
      plain(0x04, "4"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      recall("3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[11]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses emulator-pinned hex exponent multiply facts after X2 sync", () => {
    const program: IrOp[] = [
      recall("2", "preload const 1"),
      plain(0x0e, "В↑"),
      recall("1", "preload const ГE-2"),
      plain(0x12, "×"),
      store("3"),
      plain(0xf0, "F* empty F0"),
      recall("3"),
      halt(),
    ];
    const extendedProgram: IrOp[] = [
      recall("1", "preload const AE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 15"),
      plain(0x12, "×"),
      store("3"),
      plain(0xf0, "F* empty F0"),
      recall("3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const extendedRestored = x2HiddenTempRestore.run(extendedProgram, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);
    const extendedDse = deadStoreElimination.run(extendedRestored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
    expect(extendedRestored.applied).toBe(1);
    expect(extendedRestored.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(extendedDse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(extendedDse.ops)).toBe(machineCellCount(extendedProgram) - 1);
  });

  it("x2-hidden-temp-restore uses structural hex sign facts after X2 sync", () => {
    const program: IrOp[] = [
      recall("1", "preload const -8F"),
      plain(0x32, "К ЗН"),
      store("3"),
      plain(0x0d, "Cx"),
      recall("2", "preload const -8F"),
      plain(0x32, "К ЗН"),
      plain(0x0e, "В↑"),
      recall("3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[7]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses structural closed sign-change expr keys after X2 sync", () => {
    const program: IrOp[] = [
      recall("1", "preload const FABC"),
      plain(0x0b, "/-/"),
      store("3"),
      recall("2", "preload const FABC"),
      plain(0x0b, "/-/"),
      plain(0x0e, "В↑"),
      recall("3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses restored structural shape expr keys after exponent shift", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г00"),
      plain(0x31, "К |x|"),
      store("3"),
      recall("2", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x31, "К |x|"),
      plain(0x0e, "В↑"),
      recall("3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[8]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "3")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2 value dataflow seeds concrete and opaque expr facts from pure stack-consuming computations", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[4]?.x)).toEqual([
      "decimal:3:normalized",
      "expr:3",
    ]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[5]?.x)).toEqual([
      "decimal:3:normalized",
      "expr:3",
    ]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual([
      "decimal:3:normalized",
      "expr:3",
    ]);
    expect(x2ValueStateText(states[6]?.x)).toEqual([
      "decimal:3:normalized",
      "expr:3",
      "reg:2",
    ]);
    expect(x2ValueStateText(states[6]?.x2)).toEqual([
      "decimal:3:normalized",
      "expr:3",
      "reg:2",
    ]);
  });

  it("x2 value dataflow keeps only machine-representable wide binary decimal results concrete", () => {
    const exactScientificProgram: IrOp[] = [
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x10, "+"),
      halt(),
    ];
    const nonRepresentableProgram: IrOp[] = [
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      halt(),
    ];

    expect(x2ValueStateText(computeX2ValueStates(exactScientificProgram)[11]?.x)).toEqual([
      "decimal:100000000:normalized",
      "expr:10",
    ]);
    expect(x2ShapeStateText(computeX2ValueStates(exactScientificProgram)[11]?.xShape)).toEqual([
      "exponent:1:8:decimal",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(nonRepresentableProgram)[11]?.x)).toEqual([
      "expr-key:10(decimal:2:normalized,decimal:99999999:normalized)",
      "expr:10",
    ]);
  });

  it("x2 value dataflow seeds concrete multiply and finite division facts", () => {
    const multiplyProgram: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x12, "*"),
      halt(),
    ];
    const divisionProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x04, "4"),
      plain(0x13, "/"),
      halt(),
    ];
    const multiplyStates = computeX2ValueStates(multiplyProgram);
    const divisionStates = computeX2ValueStates(divisionProgram);

    expect(x2ValueStateText(multiplyStates[4]?.x)).toEqual([
      "decimal:6:normalized",
      "expr:3",
    ]);
    expect(x2ValueStateText(divisionStates[4]?.x)).toEqual([
      "decimal:0.25:normalized",
      "expr:3",
    ]);
  });

  it("x2 value dataflow seeds display shapes for exact binary results", () => {
    const additionStates = computeX2ValueStates([
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      halt(),
    ]);
    const subtractionStates = computeX2ValueStates([
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x11, "-"),
      halt(),
    ]);
    const multiplyStates = computeX2ValueStates([
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x12, "*"),
      halt(),
    ]);
    const integerDivisionStates = computeX2ValueStates([
      plain(0x06, "6"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x13, "/"),
      halt(),
    ]);
    const fractionalDivisionStates = computeX2ValueStates([
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x04, "4"),
      plain(0x13, "/"),
      halt(),
    ]);
    const maxStates = computeX2ValueStates([
      plain(0x05, "5"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x36, "К max"),
      halt(),
    ]);

    expect(x2ShapeStateText(additionStates[4]?.xShape)).toEqual(["mantissa:3:decimal"]);
    expect(x2ShapeStateText(subtractionStates[4]?.xShape)).toEqual(["mantissa:-1:decimal"]);
    expect(x2ShapeStateText(multiplyStates[4]?.xShape)).toEqual(["mantissa:6:decimal"]);
    expect(x2ShapeStateText(integerDivisionStates[4]?.xShape)).toEqual(["mantissa:2:decimal"]);
    expect(x2ShapeStateText(fractionalDivisionStates[4]?.xShape)).toEqual(["exponent:2.5:-1:decimal"]);
    expect(x2ShapeStateText(maxStates[4]?.xShape)).toEqual(["mantissa:0:decimal"]);
  });

  it("x2 value dataflow keeps non-terminating division opaque", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x13, "/"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[4]?.x)).toEqual([
      "expr-key:13(decimal:1:normalized,decimal:3:normalized)",
      "expr:3",
    ]);
  });

  it("x2 value dataflow models concrete К max including its zero quirk", () => {
    const maxProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x36, "К max"),
      halt(),
    ];
    const zeroQuirkProgram: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x36, "К max"),
      halt(),
    ];
    const maxStates = computeX2ValueStates(maxProgram);
    const zeroQuirkStates = computeX2ValueStates(zeroQuirkProgram);

    expect(x2ValueStateText(maxStates[4]?.x)).toEqual([
      "decimal:2:normalized",
      "expr:3",
    ]);
    expect(x2ValueStateText(zeroQuirkStates[4]?.x)).toEqual([
      "decimal:0:normalized",
      "expr:3",
    ]);
  });

  it("x2 value dataflow canonicalizes stable expr keys for commutative binary computations", () => {
    const plusProgram: IrOp[] = [
      recall("2"),
      plain(0x0e, "В↑"),
      recall("1"),
      plain(0x10, "+"),
      halt(),
    ];
    const minusProgram: IrOp[] = [
      recall("2"),
      plain(0x0e, "В↑"),
      recall("1"),
      plain(0x11, "-"),
      halt(),
    ];
    const plusStates = computeX2ValueStates(plusProgram);
    const minusStates = computeX2ValueStates(minusProgram);

    expect(x2ValueStateText(plusStates[4]?.x)).toEqual([
      "expr-key:10(reg:1,reg:2)",
      "expr:3",
    ]);
    expect(x2ValueStateText(minusStates[4]?.x)).toEqual([
      "expr-key:11(reg:2,reg:1)",
      "expr:3",
    ]);
  });

  it("x2 value dataflow transfers value facts through X/Y exchange", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x14, "X↔Y"),
      plain(0x11, "-"),
      plain(0x0e, "В↑"),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:1:normalized"]);
    expect(x2ValueStateText(states[4]?.y)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[5]?.x)).toEqual([
      "decimal:1:normalized",
      "expr:4",
    ]);
    expect(x2ValueStateText(states[6]?.x2)).toEqual([
      "decimal:1:normalized",
      "expr:4",
    ]);
  });

  it("x2 value dataflow canonicalizes stable expr keys through stack value copies", () => {
    const raw = "expr-key:16(shape:hex-exponent:Г:2)" as X2ValueFact;
    const canonical = "expr-key:16(shape:hex:Г00:mantissa)" as X2ValueFact;
    const legacy: X2ValueDataflowState = {
      x: new Set<X2ValueFact>([raw]),
      x2: new Set<X2ValueFact>([raw]),
      entry: { kind: "closed" },
    };
    const exchanged = transferX2ValueStateForEdge(legacy, plain(0x14, "X↔Y"), "normal", {}, 0);
    const copied = transferX2ValueStateForEdge(exchanged, plain(0x3e, "Y->X"), "normal", {}, 1);

    expect(x2ValueStateText(exchanged?.y)).toContain(canonical);
    expect(x2ValueStateText(exchanged?.y)).not.toContain(raw);
    expect(x2ValueStateText(copied?.x)).toContain(canonical);
    expect(x2ValueStateText(copied?.x)).not.toContain(raw);
  });

  it("x2 value dataflow copies Y facts through Y->X while preserving hidden X2", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x3e, "Y->X"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:1:normalized"]);
    expect(x2ValueStateText(states[4]?.y)).toEqual(["decimal:1:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:2:normalized"]);
  });

  it("x2 value dataflow copies structural Y shapes through Y->X", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      plain(0x0e, "В↑"),
      recall("2", "preload const BEEF"),
      plain(0x3e, "Y->X"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2ShapeStateText(states[4]?.yShape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["hex:BEEF:mantissa"]);
  });

  it("x2 value dataflow preserves Y through documented Y-keeping computations", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x36, "К max"),
      plain(0x37, "К ∧"),
      plain(0x0e, "В↑"),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[4]?.y)).toEqual(["decimal:1:normalized"]);
    expect(x2ValueStateText(states[5]?.x)).toEqual([
      "decimal:8:normalized",
      "expr:4",
    ]);
    expect(x2ValueStateText(states[5]?.y)).toEqual(["decimal:1:normalized"]);
    expect(x2ValueStateText(states[6]?.x2)).toEqual([
      "decimal:8:normalized",
      "expr:4",
    ]);
  });

  it("x2 value dataflow models concrete MK-61 bitwise decimal facts", () => {
    const bitOrProgram: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x04, "4"),
      plain(0x38, "К ∨"),
      halt(),
    ];
    const bitXorProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x06, "6"),
      plain(0x0e, "В↑"),
      plain(0x07, "7"),
      plain(0x39, "К ⊕"),
      halt(),
    ];
    const bitNotProgram: IrOp[] = [
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x3a, "К ИНВ"),
      halt(),
    ];
    const hexNibbleProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x09, "9"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x07, "7"),
      plain(0x38, "К ∨"),
      halt(),
    ];

    expect(x2ValueStateText(computeX2ValueStates(bitOrProgram)[4]?.x)).toEqual([
      "decimal:8:normalized",
      "expr:3",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(bitXorProgram)[5]?.x)).toEqual([
      "decimal:8.6:normalized",
      "expr:4",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(bitNotProgram)[9]?.x)).toEqual([
      "decimal:8.6666666:normalized",
      "expr:8",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(hexNibbleProgram)[6]?.x)).toEqual([
      "expr-key:38(decimal:17:normalized,decimal:19:normalized)",
      "expr:5",
    ]);
  });

  it("x2 value dataflow keeps hex-cell bitwise results as structural shape facts", () => {
    const decimalHexNibbleProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x09, "9"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x07, "7"),
      plain(0x38, "К ∨"),
      halt(),
    ];
    const decimalNotProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x09, "9"),
      plain(0x3a, "К ИНВ"),
      halt(),
    ];
    const structuralProgram: IrOp[] = [
      recall("1", "preload const 8A000000"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 85000000"),
      plain(0x38, "К ∨"),
      halt(),
    ];
    const structuralNotProgram: IrOp[] = [
      recall("1", "preload const 8A000000"),
      plain(0x3a, "К ИНВ"),
      halt(),
    ];
    const glyphMappedProgram: IrOp[] = [
      recall("1", "preload const 8Е000000"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 81000000"),
      plain(0x38, "К ∨"),
      halt(),
    ];
    const unknownGlyphProgram: IrOp[] = [
      recall("1", "preload const 8Ж000000"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 81000000"),
      plain(0x38, "К ∨"),
      halt(),
    ];

    expect(x2ShapeStateText(computeX2ValueStates(decimalHexNibbleProgram)[6]?.xShape)).toEqual([
      "hex:8.F000000:mantissa",
    ]);
    expect(x2ShapeStateText(computeX2ValueStates(decimalNotProgram)[3]?.xShape)).toEqual([
      "hex:8.6FFFFFF:mantissa",
    ]);
    expect(x2ShapeStateText(computeX2ValueStates(structuralProgram)[4]?.xShape)).toEqual([
      "hex:8.F000000:mantissa",
    ]);
    expect(x2ShapeStateText(computeX2ValueStates(structuralNotProgram)[2]?.xShape)).toEqual([
      "hex:8.5FFFFFF:mantissa",
    ]);
    expect(x2ShapeStateText(computeX2ValueStates(glyphMappedProgram)[4]?.xShape)).toEqual([
      "hex:8.F000000:mantissa",
    ]);
    expect(x2ShapeStateText(computeX2ValueStates(unknownGlyphProgram)[4]?.xShape)).toEqual([]);
  });

  it("x2 value dataflow lowers decimal-only structural bitwise results to value facts", () => {
    const structuralAndProgram: IrOp[] = [
      recall("1", "preload const 8A000000"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 85000000"),
      plain(0x37, "К ∧"),
      halt(),
    ];
    const structuralNotProgram: IrOp[] = [
      recall("1", "preload const 8F999999"),
      plain(0x3a, "К ИНВ"),
      halt(),
    ];

    expect(x2ValueStateText(computeX2ValueStates(structuralAndProgram)[4]?.x)).toContain(
      "decimal:8:normalized",
    );
    expect(x2ShapeStateText(computeX2ValueStates(structuralAndProgram)[4]?.xShape)).toEqual([
      "hex:8.0000000:mantissa",
      "mantissa:8.0000000:decimal",
    ]);
    const structuralNotValues = x2ValueStateText(computeX2ValueStates(structuralNotProgram)[2]?.x);
    expect(structuralNotValues).toContain(
      "decimal:8.0666666:normalized",
    );
    expect(structuralNotValues).not.toContain("expr-key:3A(shape:hex:8F999999:mantissa)");
    expect(x2ShapeStateText(computeX2ValueStates(structuralNotProgram)[2]?.xShape)).toEqual([
      "hex:8.0666666:mantissa",
      "mantissa:8.0666666:decimal",
    ]);
  });

  it("x2 value dataflow suppresses structural unary expr keys when a decimal result is proved", () => {
    const programs: Array<[IrOp[], number, string, string]> = [
      [
        [
          recall("1", "preload const B"),
          plain(0x22, "F x^2"),
          halt(),
        ],
        2,
        "decimal:10:normalized",
        "expr-key:22(shape:hex:B:mantissa)",
      ],
      [
        [
          recall("1", "preload const -8F"),
          plain(0x32, "К ЗН"),
          halt(),
        ],
        2,
        "decimal:-1:normalized",
        "expr-key:32(shape:hex:-8F:mantissa)",
      ],
      [
        [
          recall("1", "preload const 8F999999"),
          plain(0x3a, "К ИНВ"),
          halt(),
        ],
        2,
        "decimal:8.0666666:normalized",
        "expr-key:3A(shape:hex:8F999999:mantissa)",
      ],
    ];

    for (const [program, index, concrete, opaqueShapeKey] of programs) {
      const values = x2ValueStateText(computeX2ValueStates(program)[index]?.x);
      expect(values).toContain(concrete);
      expect(values).not.toContain(opaqueShapeKey);
    }
  });

  it("x2 value dataflow suppresses decimal unary expr keys through the shared concrete evaluator", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x22, "F x^2"),
      halt(),
    ];
    const values = x2ValueStateText(computeX2ValueStates(program)[2]?.x);

    expect(values).toContain("decimal:4:normalized");
    expect(values).not.toContain("expr-key:22(decimal:2:normalized)");
  });

  it("x2 value dataflow models documented single-digit hex subtract-one decimal facts", () => {
    function subtractOneProgram(literal: string, right: Extract<IrOp, { kind: "plain" }> = plain(0x01, "1")): IrOp[] {
      return [
        recall("1", `preload const ${literal}`),
        plain(0x0e, "В↑"),
        right,
        plain(0x11, "-"),
        halt(),
      ];
    }
    function subtractOneSquareProgram(literal: string): IrOp[] {
      return [
        recall("1", `preload const ${literal}`),
        plain(0x0e, "В↑"),
        plain(0x01, "1"),
        plain(0x11, "-"),
        plain(0x22, "F x^2"),
        halt(),
      ];
    }

    expect(x2ValueStateText(computeX2ValueStates(subtractOneProgram("С"))[4]?.x) ?? []).toContain(
      "decimal:1:normalized",
    );
    expect(x2ValueStateText(computeX2ValueStates(subtractOneProgram("Г"))[4]?.x) ?? []).toContain(
      "decimal:2:normalized",
    );
    expect(x2ValueStateText(computeX2ValueStates(subtractOneProgram("Е"))[4]?.x) ?? []).toContain(
      "decimal:3:normalized",
    );
    expect(x2ValueStateText(computeX2ValueStates(subtractOneProgram("C"))[4]?.x) ?? []).toContain(
      "decimal:1:normalized",
    );
    expect(x2ValueStateText(computeX2ValueStates(subtractOneProgram("D"))[4]?.x) ?? []).toContain(
      "decimal:2:normalized",
    );
    expect(x2ValueStateText(computeX2ValueStates(subtractOneProgram("E"))[4]?.x) ?? []).toContain(
      "decimal:3:normalized",
    );
    expect(x2ValueStateText(computeX2ValueStates(subtractOneSquareProgram("С"))[5]?.x) ?? []).toContain(
      "decimal:1:normalized",
    );
    expect(x2ValueStateText(computeX2ValueStates(subtractOneSquareProgram("Г"))[5]?.x) ?? []).toContain(
      "decimal:4:normalized",
    );
    expect(x2ValueStateText(computeX2ValueStates(subtractOneSquareProgram("Е"))[5]?.x) ?? []).toContain(
      "decimal:9:normalized",
    );

    expect(x2ValueStateText(computeX2ValueStates(subtractOneProgram("B"))[4]?.x) ?? []).toContain(
      "decimal:0:normalized",
    );
    expect(x2ValueStateText(computeX2ValueStates(subtractOneProgram("F"))[4]?.x) ?? []).not.toContain(
      "decimal:0:normalized",
    );
    expect(x2ValueStateText(computeX2ValueStates(subtractOneProgram("C.0"))[4]?.x) ?? []).not.toContain(
      "decimal:1:normalized",
    );
    expect(x2ValueStateText(computeX2ValueStates(subtractOneProgram("С", plain(0x02, "2")))[4]?.x) ?? [])
      .not.toContain("decimal:1:normalized");
  });

  it("x2 value dataflow models emulator-pinned single hex digit square facts", () => {
    function squareProgram(literal: string): IrOp[] {
      return [
        recall("1", `preload const ${literal}`),
        plain(0x22, "F x^2"),
        halt(),
      ];
    }
    function syncedSquareProgram(literal: string): IrOp[] {
      return [
        recall("1", `preload const ${literal}`),
        plain(0x22, "F x^2"),
        plain(0xf0, "F* empty F0"),
        halt(),
      ];
    }

    expect(x2ValueStateText(computeX2ValueStates(squareProgram("A"))[2]?.x) ?? [])
      .toContain("decimal:0:normalized");
    expect(x2ValueStateText(computeX2ValueStates(squareProgram("B"))[2]?.x) ?? [])
      .toContain("decimal:10:normalized");
    expect(x2ValueStateText(computeX2ValueStates(squareProgram("С"))[2]?.x) ?? [])
      .toContain("decimal:20:normalized");
    expect(x2ValueStateText(computeX2ValueStates(squareProgram("Г"))[2]?.x) ?? [])
      .toContain("decimal:30:normalized");
    expect(x2ValueStateText(computeX2ValueStates(squareProgram("Е"))[2]?.x) ?? [])
      .toContain("decimal:0:normalized");
    expect(x2ValueStateText(computeX2ValueStates(squareProgram("F"))[2]?.x) ?? [])
      .toContain("decimal:0:normalized");
    expect(x2ValueStateText(computeX2ValueStates(squareProgram("0C"))[2]?.x) ?? [])
      .toContain("decimal:20:normalized");
    expect(x2ValueStateText(computeX2ValueStates(squareProgram("-0D"))[2]?.x) ?? [])
      .toContain("decimal:30:normalized");

    expect(x2ShapeStateText(computeX2ValueStates(squareProgram("A"))[2]?.xShape)).toEqual([
      "mantissa:00:decimal",
    ]);
    expect(x2ShapeStateText(computeX2ValueStates(syncedSquareProgram("A"))[3]?.x2Shape)).toEqual([
      "mantissa:0:decimal",
    ]);
    const bStates = computeX2ValueStates(squareProgram("B"));
    expect(x2ValueStateText(bStates[2]?.x) ?? [])
      .not.toContain("expr-key:22(shape:hex:B:mantissa)");
    expect(x2ShapeStateText(bStates[2]?.xShape)).toEqual(["mantissa:10:decimal"]);
    expect(x2ValueStateText(computeX2ValueStates(squareProgram("B0"))[2]?.x) ?? [])
      .not.toContain("decimal:10:normalized");
    expect(x2ValueStateText(computeX2ValueStates(squareProgram("C.0"))[2]?.x) ?? [])
      .not.toContain("decimal:20:normalized");
  });

  it("x2 value dataflow models emulator-pinned single-digit hex addition facts", () => {
    function hexLeftPlusDecimalProgram(literal: string, digit: Extract<IrOp, { kind: "plain" }>): IrOp[] {
      return [
        recall("1", `preload const ${literal}`),
        plain(0x0e, "В↑"),
        digit,
        plain(0x10, "+"),
        halt(),
      ];
    }
    function decimalLeftPlusHexProgram(digit: Extract<IrOp, { kind: "plain" }>, literal: string): IrOp[] {
      return [
        digit,
        plain(0x0e, "В↑"),
        recall("1", `preload const ${literal}`),
        plain(0x10, "+"),
        halt(),
      ];
    }
    function hexLeftPlusDecimalLiteralProgram(literal: string, decimal: string): IrOp[] {
      return [
        recall("1", `preload const ${literal}`),
        plain(0x0e, "В↑"),
        recall("2", `preload const ${decimal}`),
        plain(0x10, "+"),
        halt(),
      ];
    }
    function decimalLeftPlusHexLiteralProgram(decimal: string, literal: string): IrOp[] {
      return [
        recall("2", `preload const ${decimal}`),
        plain(0x0e, "В↑"),
        recall("1", `preload const ${literal}`),
        plain(0x10, "+"),
        halt(),
      ];
    }

    expect(x2ValueStateText(computeX2ValueStates(hexLeftPlusDecimalProgram("С", plain(0x04, "4")))[4]?.x) ?? [])
      .toContain("decimal:16:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftPlusDecimalProgram("B", plain(0x00, "0")))[4]?.x) ?? [])
      .toContain("decimal:11:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftPlusDecimalProgram("Г", plain(0x03, "3")))[4]?.x) ?? [])
      .toContain("decimal:16:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftPlusDecimalProgram("C", plain(0x04, "4")))[4]?.x) ?? [])
      .toContain("decimal:16:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftPlusDecimalProgram("D", plain(0x03, "3")))[4]?.x) ?? [])
      .toContain("decimal:16:normalized");
    expect(x2ValueStateText(computeX2ValueStates(decimalLeftPlusHexProgram(plain(0x04, "4"), "С"))[4]?.x) ?? [])
      .toContain("decimal:0:normalized");
    expect(x2ValueStateText(computeX2ValueStates(decimalLeftPlusHexProgram(plain(0x00, "0"), "B"))[4]?.x) ?? [])
      .toContain("decimal:1:normalized");
    expect(x2ValueStateText(computeX2ValueStates(decimalLeftPlusHexProgram(plain(0x03, "3"), "Г"))[4]?.x) ?? [])
      .toContain("decimal:0:normalized");

    expect(x2ValueStateText(computeX2ValueStates(hexLeftPlusDecimalProgram("Г", plain(0x04, "4")))[4]?.x) ?? [])
      .toContain("decimal:17:normalized");
    expect(x2ValueStateText(computeX2ValueStates(decimalLeftPlusHexProgram(plain(0x03, "3"), "С"))[4]?.x) ?? [])
      .toContain("decimal:5:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftPlusDecimalLiteralProgram("A", "18"))[4]?.x) ?? [])
      .toContain("decimal:28:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftPlusDecimalLiteralProgram("С", "16"))[4]?.x) ?? [])
      .toContain("decimal:28:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(hexLeftPlusDecimalLiteralProgram("С", "16"))[4]?.xShape))
      .toEqual(["mantissa:28:decimal"]);
    expect(x2ValueStateText(computeX2ValueStates(decimalLeftPlusHexLiteralProgram("18", "Е"))[4]?.x) ?? [])
      .toContain("decimal:32:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(decimalLeftPlusHexLiteralProgram("18", "Е"))[4]?.xShape))
      .toEqual(["mantissa:32:decimal"]);
    expect(x2ValueStateText(computeX2ValueStates(hexLeftPlusDecimalLiteralProgram("F", "18"))[4]?.x) ?? [])
      .not.toContain("decimal:33:normalized");
  });

  it("x2 value dataflow models emulator-pinned single hex digit subtract table", () => {
    function hexLeftMinusDecimalProgram(literal: string, digit: Extract<IrOp, { kind: "plain" }>): IrOp[] {
      return [
        recall("1", `preload const ${literal}`),
        plain(0x0e, "В↑"),
        digit,
        plain(0x11, "-"),
        halt(),
      ];
    }
    function decimalLeftMinusHexProgram(digit: Extract<IrOp, { kind: "plain" }>, literal: string): IrOp[] {
      return [
        digit,
        plain(0x0e, "В↑"),
        recall("1", `preload const ${literal}`),
        plain(0x11, "-"),
        halt(),
      ];
    }
    function hexLeftMinusDecimalLiteralProgram(literal: string, decimal: string): IrOp[] {
      return [
        recall("1", `preload const ${literal}`),
        plain(0x0e, "В↑"),
        recall("2", `preload const ${decimal}`),
        plain(0x11, "-"),
        halt(),
      ];
    }
    function decimalLeftMinusHexLiteralProgram(decimal: string, literal: string): IrOp[] {
      return [
        recall("2", `preload const ${decimal}`),
        plain(0x0e, "В↑"),
        recall("1", `preload const ${literal}`),
        plain(0x11, "-"),
        halt(),
      ];
    }

    expect(x2ValueStateText(computeX2ValueStates(hexLeftMinusDecimalProgram("A", plain(0x00, "0")))[4]?.x) ?? [])
      .toContain("decimal:10:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftMinusDecimalProgram("B", plain(0x01, "1")))[4]?.x) ?? [])
      .toContain("decimal:0:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftMinusDecimalProgram("С", plain(0x02, "2")))[4]?.x) ?? [])
      .toContain("decimal:0:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftMinusDecimalProgram("Г", plain(0x03, "3")))[4]?.x) ?? [])
      .toContain("decimal:0:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftMinusDecimalProgram("Е", plain(0x05, "5")))[4]?.x) ?? [])
      .toContain("decimal:9:normalized");
    expect(x2ValueStateText(computeX2ValueStates(decimalLeftMinusHexProgram(plain(0x00, "0"), "С"))[4]?.x) ?? [])
      .toContain("decimal:-2:normalized");
    expect(x2ValueStateText(computeX2ValueStates(decimalLeftMinusHexProgram(plain(0x00, "0"), "B"))[4]?.x) ?? [])
      .toContain("decimal:-1:normalized");
    expect(x2ValueStateText(computeX2ValueStates(decimalLeftMinusHexProgram(plain(0x02, "2"), "С"))[4]?.x) ?? [])
      .toContain("decimal:-10:normalized");
    expect(x2ValueStateText(computeX2ValueStates(decimalLeftMinusHexProgram(plain(0x04, "4"), "Е"))[4]?.x) ?? [])
      .toContain("decimal:-10:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(decimalLeftMinusHexProgram(plain(0x00, "0"), "С"))[4]?.xShape))
      .toEqual(["mantissa:-2:decimal"]);
    expect(x2ValueStateText(computeX2ValueStates(hexLeftMinusDecimalLiteralProgram("A", "18"))[4]?.x) ?? [])
      .toContain("decimal:-8:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftMinusDecimalLiteralProgram("Е", "16"))[4]?.x) ?? [])
      .toContain("decimal:-2:normalized");
    expect(x2ValueStateText(computeX2ValueStates(decimalLeftMinusHexLiteralProgram("18", "B"))[4]?.x) ?? [])
      .toContain("decimal:23:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(decimalLeftMinusHexLiteralProgram("18", "B"))[4]?.xShape))
      .toEqual(["mantissa:23:decimal"]);
    expect(x2ValueStateText(computeX2ValueStates(decimalLeftMinusHexLiteralProgram("10", "Е"))[4]?.x) ?? [])
      .toContain("decimal:12:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(decimalLeftMinusHexLiteralProgram("10", "Е"))[4]?.xShape))
      .toEqual(["mantissa:12:decimal"]);
    expect(x2ValueStateText(computeX2ValueStates(hexLeftMinusDecimalLiteralProgram("F", "18"))[4]?.x) ?? [])
      .not.toContain("decimal:-3:normalized");
  });

  it("x2 value dataflow models emulator-pinned hex A times 18 leading-zero result", () => {
    const hexATimes18Program: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      halt(),
    ];
    const reverseProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x0e, "В↑"),
      recall("1", "preload const A"),
      plain(0x12, "×"),
      halt(),
    ];
    const syncedProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ];

    const states = computeX2ValueStates(hexATimes18Program);
    const reverseStates = computeX2ValueStates(reverseProgram);
    const syncedStates = computeX2ValueStates(syncedProgram);
    expect(x2ValueStateText(states[5]?.x) ?? []).toContain("decimal:20:normalized");
    expect(x2ValueStateText(states[5]?.x) ?? [])
      .not.toContain("expr-key:12(decimal:18:normalized,shape:hex:A:mantissa)");
    expect(x2ValueStateText(states[5]?.x2) ?? []).toContain("decimal:18:normalized");
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["mantissa:020:decimal"]);
    expect(x2ShapeStateText(states[5]?.x2Shape)).toEqual(["mantissa:18:decimal"]);
    expect(x2ShapeStateText(syncedStates[6]?.x2Shape)).toEqual(["mantissa:20:decimal"]);
    expect(x2ValueStateText(reverseStates[5]?.x) ?? []).toContain("decimal:180:normalized");
    expect(x2ValueStateText(reverseStates[5]?.x) ?? []).not.toContain("decimal:20:normalized");
    expect(x2ShapeStateText(reverseStates[5]?.xShape)).toEqual(["mantissa:180:decimal"]);
  });

  it("x2 value dataflow uses exact decimal display shapes as binary operands", () => {
    const shapeOnly: X2ValueDataflowState = {
      y: new Set<X2ValueFact>(["decimal:2:normalized"]),
      x: new Set(),
      x2: new Set(),
      yShape: new Set(),
      xShape: new Set<X2ShapeFact>(["exponent:1:7:decimal"]),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(shapeOnly, plain(0x10, "+"), "normal", {}, 0);

    expect(x2ValueStateText(result?.x) ?? []).toContain("decimal:10000002:normalized");
    expect(x2ShapeStateText(result?.xShape)).toEqual(["mantissa:10000002:decimal"]);
  });

  it("x2 value dataflow uses exact decimal structural shapes as visible decimal operands", () => {
    const exactStructural: X2ValueDataflowState = {
      y: new Set(),
      x: new Set<X2ValueFact>(["decimal:4:normalized"]),
      x2: new Set(),
      yShape: new Set<X2ShapeFact>(["hex:123:mantissa"]),
      xShape: new Set(),
      entry: { kind: "closed" },
    };
    const rawStructural: X2ValueDataflowState = {
      ...exactStructural,
      yShape: new Set<X2ShapeFact>(["hex:0123:mantissa"]),
    };

    const exactResult = transferX2ValueStateForEdge(exactStructural, plain(0x10, "+"), "normal", {}, 0);
    const rawResult = transferX2ValueStateForEdge(rawStructural, plain(0x10, "+"), "normal", {}, 0);

    expect(x2ValueStateText(exactResult?.x) ?? []).toContain("decimal:127:normalized");
    expect(x2ShapeStateText(exactResult?.xShape)).toEqual(["mantissa:127:decimal"]);
    expect(x2ValueStateText(rawResult?.x) ?? []).not.toContain("decimal:127:normalized");
    expect(x2ShapeStateText(rawResult?.xShape)).toEqual([]);
  });

  it("x2 value dataflow feeds exact decimal display shapes to structural hex arithmetic", () => {
    const shapeOnly: X2ValueDataflowState = {
      y: new Set(),
      x: new Set(),
      x2: new Set(),
      yShape: new Set<X2ShapeFact>(["hex:A:mantissa"]),
      xShape: new Set<X2ShapeFact>(["exponent:1.8:1:decimal"]),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(shapeOnly, plain(0x12, "×"), "normal", {}, 0);

    expect(x2ValueStateText(result?.x) ?? []).toContain("decimal:20:normalized");
    expect(x2ValueStateText(result?.x) ?? [])
      .not.toContain("expr-key:12(shape:hex:A:mantissa,shape:exponent:1.8:1:decimal)");
    expect(x2ShapeStateText(result?.xShape)).toEqual(["mantissa:020:decimal"]);
  });

  it("x2 value dataflow keeps structural arithmetic value and display-shape products paired", () => {
    function binaryProgram(left: string, right: string, op: Extract<IrOp, { kind: "plain" }>): IrOp[] {
      return [
        recall("1", `preload const ${left}`),
        plain(0x0e, "В↑"),
        recall("2", `preload const ${right}`),
        op,
        halt(),
      ];
    }

    const cases: ReadonlyArray<{
      readonly left: string;
      readonly right: string;
      readonly op: Extract<IrOp, { kind: "plain" }>;
      readonly value: string;
      readonly shape: X2ShapeFact;
    }> = [
      { left: "С", right: "16", op: plain(0x10, "+"), value: "28", shape: "mantissa:28:decimal" },
      { left: "18", right: "B", op: plain(0x11, "-"), value: "23", shape: "mantissa:23:decimal" },
      { left: "A", right: "18", op: plain(0x12, "×"), value: "20", shape: "mantissa:020:decimal" },
      {
        left: "Е",
        right: "18",
        op: plain(0x13, "÷"),
        value: "0.77777777",
        shape: "exponent:7.7777777:-1:decimal",
      },
    ];

    for (const testCase of cases) {
      const states = computeX2ValueStates(binaryProgram(testCase.left, testCase.right, testCase.op));
      expect(x2ValueStateText(states[4]?.x) ?? []).toContain(`decimal:${testCase.value}:normalized`);
      expect(x2ShapeStateText(states[4]?.xShape)).toEqual([testCase.shape]);
    }
  });

  it("x2 value dataflow models emulator-pinned single hex digit multiply table", () => {
    const leftHex: IrOp[] = [
      recall("1", "preload const С"),
      plain(0x0e, "В↑"),
      plain(0x05, "5"),
      plain(0x12, "×"),
      halt(),
    ];
    const leftHexLeadingZero: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      halt(),
    ];
    const rightHex: IrOp[] = [
      plain(0x09, "9"),
      plain(0x0e, "В↑"),
      recall("1", "preload const Г"),
      plain(0x12, "×"),
      halt(),
    ];
    const rightHexZero: IrOp[] = [
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x0e, "В↑"),
      recall("1", "preload const Е"),
      plain(0x12, "×"),
      halt(),
    ];
    const leftHexSeven: IrOp[] = [
      recall("1", "preload const Е"),
      plain(0x0e, "В↑"),
      plain(0x07, "7"),
      plain(0x12, "×"),
      halt(),
    ];
    const rightHexSix: IrOp[] = [
      plain(0x06, "6"),
      plain(0x0e, "В↑"),
      recall("1", "preload const A"),
      plain(0x12, "×"),
      halt(),
    ];
    const leftHexBLeadingZero: IrOp[] = [
      recall("1", "preload const B"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      halt(),
    ];
    const leftHexAWideZero: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x06, "6"),
      plain(0x12, "×"),
      halt(),
    ];
    const leftHexBSeventeen: IrOp[] = [
      recall("1", "preload const B"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x07, "7"),
      plain(0x12, "×"),
      halt(),
    ];
    const rightHexB: IrOp[] = [
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x0e, "В↑"),
      recall("1", "preload const B"),
      plain(0x12, "×"),
      halt(),
    ];
    const rightHexCWide: IrOp[] = [
      plain(0x01, "1"),
      plain(0x06, "6"),
      plain(0x0e, "В↑"),
      recall("1", "preload const С"),
      plain(0x12, "×"),
      halt(),
    ];

    const leftHexStates = computeX2ValueStates(leftHex);
    expect(x2ValueStateText(leftHexStates[4]?.x)).toContain("decimal:32:normalized");
    expect(x2ShapeStateText(leftHexStates[4]?.xShape)).toEqual(["mantissa:32:decimal"]);
    const leftHexLeadingZeroStates = computeX2ValueStates(leftHexLeadingZero);
    expect(x2ValueStateText(leftHexLeadingZeroStates[5]?.x)).toContain("decimal:20:normalized");
    expect(x2ShapeStateText(leftHexLeadingZeroStates[5]?.xShape)).toEqual(["mantissa:020:decimal"]);
    const rightHexStates = computeX2ValueStates(rightHex);
    expect(x2ValueStateText(rightHexStates[4]?.x)).toContain("decimal:90:normalized");
    expect(x2ShapeStateText(rightHexStates[4]?.xShape)).toEqual(["mantissa:90:decimal"]);
    const rightHexZeroStates = computeX2ValueStates(rightHexZero);
    expect(x2ValueStateText(rightHexZeroStates[5]?.x)).toContain("decimal:0:normalized");
    expect(x2ShapeStateText(rightHexZeroStates[5]?.xShape)).toEqual(["mantissa:0:decimal"]);
    const leftHexSevenStates = computeX2ValueStates(leftHexSeven);
    expect(x2ValueStateText(leftHexSevenStates[4]?.x)).toContain("decimal:54:normalized");
    expect(x2ShapeStateText(leftHexSevenStates[4]?.xShape)).toEqual(["mantissa:54:decimal"]);
    const rightHexSixStates = computeX2ValueStates(rightHexSix);
    expect(x2ValueStateText(rightHexSixStates[4]?.x)).toContain("decimal:60:normalized");
    expect(x2ShapeStateText(rightHexSixStates[4]?.xShape)).toEqual(["mantissa:60:decimal"]);
    const leftHexBLeadingZeroStates = computeX2ValueStates(leftHexBLeadingZero);
    expect(x2ValueStateText(leftHexBLeadingZeroStates[5]?.x)).toContain("decimal:54:normalized");
    expect(x2ShapeStateText(leftHexBLeadingZeroStates[5]?.xShape)).toEqual(["mantissa:054:decimal"]);
    const leftHexAWideZeroStates = computeX2ValueStates(leftHexAWideZero);
    expect(x2ValueStateText(leftHexAWideZeroStates[5]?.x)).toContain("decimal:0:normalized");
    expect(x2ShapeStateText(leftHexAWideZeroStates[5]?.xShape)).toEqual(["mantissa:000:decimal"]);
    const leftHexBSeventeenStates = computeX2ValueStates(leftHexBSeventeen);
    expect(x2ValueStateText(leftHexBSeventeenStates[5]?.x)).toContain("decimal:43:normalized");
    expect(x2ShapeStateText(leftHexBSeventeenStates[5]?.xShape)).toEqual(["mantissa:043:decimal"]);
    const rightHexBStates = computeX2ValueStates(rightHexB);
    expect(x2ValueStateText(rightHexBStates[5]?.x)).toContain("decimal:180:normalized");
    expect(x2ShapeStateText(rightHexBStates[5]?.xShape)).toEqual(["mantissa:180:decimal"]);
    const rightHexCWideStates = computeX2ValueStates(rightHexCWide);
    expect(x2ValueStateText(rightHexCWideStates[5]?.x)).toContain("decimal:160:normalized");
    expect(x2ShapeStateText(rightHexCWideStates[5]?.xShape)).toEqual(["mantissa:160:decimal"]);
  });

  it("x2 value dataflow models emulator-pinned structural hex pair arithmetic", () => {
    function hexPairProgram(left: string, right: string, op: Extract<IrOp, { kind: "plain" }>): IrOp[] {
      return [
        recall("1", `preload const ${left}`),
        plain(0x0e, "В↑"),
        recall("2", `preload const ${right}`),
        op,
        halt(),
      ];
    }

    const plusStates = computeX2ValueStates(hexPairProgram("A", "B", plain(0x10, "+")));
    expect(x2ValueStateText(plusStates[4]?.x) ?? []).toContain("decimal:5:normalized");
    expect(x2ShapeStateText(plusStates[4]?.xShape)).toEqual(["mantissa:5:decimal"]);

    const plusCarryStates = computeX2ValueStates(hexPairProgram("Г", "Е", plain(0x10, "+")));
    expect(x2ValueStateText(plusCarryStates[4]?.x) ?? []).toContain("decimal:11:normalized");
    expect(x2ShapeStateText(plusCarryStates[4]?.xShape)).toEqual(["mantissa:11:decimal"]);

    const minusStates = computeX2ValueStates(hexPairProgram("A", "Е", plain(0x11, "-")));
    expect(x2ValueStateText(minusStates[4]?.x) ?? []).toContain("decimal:-4:normalized");
    expect(x2ShapeStateText(minusStates[4]?.xShape)).toEqual(["mantissa:-4:decimal"]);

    const multiplyStates = computeX2ValueStates(hexPairProgram("B", "Г", plain(0x12, "×")));
    expect(x2ValueStateText(multiplyStates[4]?.x) ?? []).toContain("decimal:10:normalized");
    expect(x2ShapeStateText(multiplyStates[4]?.xShape)).toEqual(["mantissa:10:decimal"]);

    const multiplyLeadingZeroStates = computeX2ValueStates(hexPairProgram("A", "С", plain(0x12, "×")));
    expect(x2ValueStateText(multiplyLeadingZeroStates[4]?.x) ?? []).toContain("decimal:0:normalized");
    expect(x2ShapeStateText(multiplyLeadingZeroStates[4]?.xShape)).toEqual(["mantissa:00:decimal"]);

    const divideIdentityStates = computeX2ValueStates(hexPairProgram("Г", "Г", plain(0x13, "÷")));
    expect(x2ValueStateText(divideIdentityStates[4]?.x) ?? []).toContain("decimal:1:normalized");
    expect(x2ShapeStateText(divideIdentityStates[4]?.xShape)).toEqual(["mantissa:1:decimal"]);

    const divideScientificStates = computeX2ValueStates(hexPairProgram("A", "Г", plain(0x13, "÷")));
    expect(x2ValueStateText(divideScientificStates[4]?.x) ?? []).toContain("decimal:0.4:normalized");
    expect(x2ShapeStateText(divideScientificStates[4]?.xShape)).toEqual(["exponent:4:-1:decimal"]);

    const divideWideStates = computeX2ValueStates(hexPairProgram("С", "B", plain(0x13, "÷")));
    expect(x2ValueStateText(divideWideStates[4]?.x) ?? []).toContain("decimal:1.2525252:normalized");
    expect(x2ShapeStateText(divideWideStates[4]?.xShape)).toEqual(["mantissa:1.2525252:decimal"]);

    const divideDecimalStates = computeX2ValueStates(hexPairProgram("Г", "8", plain(0x13, "÷")));
    expect(x2ValueStateText(divideDecimalStates[4]?.x) ?? []).toContain("decimal:1.625:normalized");
    expect(x2ShapeStateText(divideDecimalStates[4]?.xShape)).toEqual(["mantissa:1.625:decimal"]);

    const divideDecimalScientificStates = computeX2ValueStates(hexPairProgram("Е", "18", plain(0x13, "÷")));
    expect(x2ValueStateText(divideDecimalScientificStates[4]?.x) ?? []).toContain("decimal:0.77777777:normalized");
    expect(x2ShapeStateText(divideDecimalScientificStates[4]?.xShape)).toEqual(["exponent:7.7777777:-1:decimal"]);

    const divideDecimalWideZeroStates = computeX2ValueStates(hexPairProgram("A", "10", plain(0x13, "÷")));
    expect(x2ValueStateText(divideDecimalWideZeroStates[4]?.x) ?? []).toContain("decimal:0:normalized");
    expect(x2ShapeStateText(divideDecimalWideZeroStates[4]?.xShape)).toEqual(["exponent:0:-1:decimal"]);

    const divideDecimalWideStates = computeX2ValueStates(hexPairProgram("Г", "16", plain(0x13, "÷")));
    expect(x2ValueStateText(divideDecimalWideStates[4]?.x) ?? []).toContain("decimal:0.8125:normalized");
    expect(x2ShapeStateText(divideDecimalWideStates[4]?.xShape)).toEqual(["exponent:8.125:-1:decimal"]);

    const reverseDivideStates = computeX2ValueStates(hexPairProgram("9", "B", plain(0x13, "÷")));
    expect(x2ValueStateText(reverseDivideStates[4]?.x) ?? []).toContain("decimal:0.04444443:normalized");
    expect(x2ShapeStateText(reverseDivideStates[4]?.xShape)).toEqual(["exponent:0.4444443:-1:decimal"]);

    const reverseDivideZeroExponentStates = computeX2ValueStates(hexPairProgram("5", "Г", plain(0x13, "÷")));
    expect(x2ValueStateText(reverseDivideZeroExponentStates[4]?.x) ?? []).toContain("decimal:0:normalized");
    expect(x2ShapeStateText(reverseDivideZeroExponentStates[4]?.xShape)).toEqual(["exponent:0:-1:decimal"]);

    const reverseDivideWideStates = computeX2ValueStates(hexPairProgram("16", "B", plain(0x13, "÷")));
    expect(x2ValueStateText(reverseDivideWideStates[4]?.x) ?? []).toContain("decimal:9.2525252:normalized");
    expect(x2ShapeStateText(reverseDivideWideStates[4]?.xShape)).toEqual(["mantissa:9.2525252:decimal"]);

    const reverseDivideCWideStates = computeX2ValueStates(hexPairProgram("10", "С", plain(0x13, "÷")));
    expect(x2ValueStateText(reverseDivideCWideStates[4]?.x) ?? []).toContain("decimal:9.9099099:normalized");
    expect(x2ShapeStateText(reverseDivideCWideStates[4]?.xShape)).toEqual(["mantissa:9.9099099:decimal"]);

    const reverseDivideELeadingZeroStates = computeX2ValueStates(hexPairProgram("15", "Е", plain(0x13, "÷")));
    expect(x2ValueStateText(reverseDivideELeadingZeroStates[4]?.x) ?? []).toContain("decimal:0.2292929:normalized");
    expect(x2ShapeStateText(reverseDivideELeadingZeroStates[4]?.xShape)).toEqual(["mantissa:0.2292929:decimal"]);

    const reverseARejectedStates = computeX2ValueStates(hexPairProgram("10", "A", plain(0x13, "÷")));
    expect(x2ValueStateText(reverseARejectedStates[4]?.x) ?? []).not.toContain("decimal:0:normalized");
    expect(x2ShapeStateText(reverseARejectedStates[4]?.xShape)).toEqual([]);

    const reverseRejectedStates = computeX2ValueStates(hexPairProgram("3", "С", plain(0x13, "÷")));
    expect(x2ValueStateText(reverseRejectedStates[4]?.x) ?? []).not.toContain("decimal:0:normalized");
    expect(x2ShapeStateText(reverseRejectedStates[4]?.xShape)).toEqual([]);

    const rejectedFStates = computeX2ValueStates(hexPairProgram("F", "A", plain(0x10, "+")));
    expect(x2ValueStateText(rejectedFStates[4]?.x) ?? []).not.toContain("decimal:0:normalized");
    expect(x2ShapeStateText(rejectedFStates[4]?.xShape)).toEqual([]);
  });

  it("x2 value dataflow models emulator-pinned structural hex exponent division", () => {
    function binaryProgram(left: string, right: string): IrOp[] {
      return [
        recall("1", `preload const ${left}`),
        plain(0x0e, "В↑"),
        recall("2", `preload const ${right}`),
        plain(0x13, "÷"),
        halt(),
      ];
    }

    const leftExponentStates = computeX2ValueStates(binaryProgram("ГE-2", "2"));
    expect(x2ValueStateText(leftExponentStates[4]?.x) ?? []).toContain("decimal:0.065:normalized");
    expect(x2ShapeStateText(leftExponentStates[4]?.xShape)).toEqual(["exponent:6.5:-2:decimal"]);

    const leftAExponentStates = computeX2ValueStates(binaryProgram("AE-2", "1"));
    expect(x2ValueStateText(leftAExponentStates[4]?.x) ?? []).toContain("decimal:0:normalized");
    expect(x2ShapeStateText(leftAExponentStates[4]?.xShape)).toEqual(["exponent:0:-2:decimal"]);

    const leftBWideExponentStates = computeX2ValueStates(binaryProgram("BE-2", "18"));
    expect(x2ValueStateText(leftBWideExponentStates[4]?.x) ?? []).toContain("decimal:0.0061111111:normalized");
    expect(x2ShapeStateText(leftBWideExponentStates[4]?.xShape)).toEqual(["exponent:6.1111111:-3:decimal"]);

    const leftAWideExponentStates = computeX2ValueStates(binaryProgram("AE-2", "10"));
    expect(x2ValueStateText(leftAWideExponentStates[4]?.x) ?? []).toContain("decimal:0:normalized");
    expect(x2ShapeStateText(leftAWideExponentStates[4]?.xShape)).toEqual(["exponent:0:-3:decimal"]);

    const leftGammaSeventeenExponentStates = computeX2ValueStates(binaryProgram("ГE-2", "17"));
    expect(x2ValueStateText(leftGammaSeventeenExponentStates[4]?.x) ?? [])
      .toContain("decimal:0.0076470588:normalized");
    expect(x2ShapeStateText(leftGammaSeventeenExponentStates[4]?.xShape)).toEqual([
      "exponent:7.6470588:-3:decimal",
    ]);

    const leftGammaExponentMinusOneStates = computeX2ValueStates(binaryProgram("ГE-1", "5"));
    expect(x2ValueStateText(leftGammaExponentMinusOneStates[4]?.x) ?? [])
      .toContain("decimal:0.26:normalized");
    expect(x2ShapeStateText(leftGammaExponentMinusOneStates[4]?.xShape)).toEqual([
      "exponent:2.6:-1:decimal",
    ]);

    const leftAExponentMinusThreeStates = computeX2ValueStates(binaryProgram("AE-3", "1"));
    expect(x2ValueStateText(leftAExponentMinusThreeStates[4]?.x) ?? [])
      .toContain("decimal:0:normalized");
    expect(x2ShapeStateText(leftAExponentMinusThreeStates[4]?.xShape)).toEqual([
      "exponent:0:-3:decimal",
    ]);

    const leftGammaExponentPlusOneStates = computeX2ValueStates(binaryProgram("ГE1", "8"));
    expect(x2ValueStateText(leftGammaExponentPlusOneStates[4]?.x) ?? [])
      .toContain("decimal:16.25:normalized");
    expect(x2ShapeStateText(leftGammaExponentPlusOneStates[4]?.xShape)).toEqual([
      "mantissa:16.25:decimal",
    ]);

    const rightExponentStates = computeX2ValueStates(binaryProgram("5", "ГE-2"));
    expect(x2ValueStateText(rightExponentStates[4]?.x) ?? []).toContain("decimal:0:normalized");
    expect(x2ShapeStateText(rightExponentStates[4]?.xShape)).toEqual(["mantissa:00:decimal"]);

    const rightARejectedStates = computeX2ValueStates(binaryProgram("1", "AE-2"));
    expect(x2ValueStateText(rightARejectedStates[4]?.x) ?? []).not.toContain("decimal:0:normalized");
    expect(x2ShapeStateText(rightARejectedStates[4]?.xShape)).toEqual([]);

    const rightBDivideStates = computeX2ValueStates(binaryProgram("18", "BE-2"));
    expect(x2ValueStateText(rightBDivideStates[4]?.x) ?? []).toContain("decimal:943.43434:normalized");
    expect(x2ShapeStateText(rightBDivideStates[4]?.xShape)).toEqual(["mantissa:943.43434:decimal"]);

    const rightBMidDivideStates = computeX2ValueStates(binaryProgram("15", "BE-2"));
    expect(x2ValueStateText(rightBMidDivideStates[4]?.x) ?? []).toContain("decimal:900:normalized");
    expect(x2ShapeStateText(rightBMidDivideStates[4]?.xShape)).toEqual(["mantissa:900:decimal"]);

    const rightGammaLeadingZeroDivideStates = computeX2ValueStates(binaryProgram("12", "ГE-2"));
    expect(x2ValueStateText(rightGammaLeadingZeroDivideStates[4]?.x) ?? []).toContain("decimal:0:normalized");
    expect(x2ShapeStateText(rightGammaLeadingZeroDivideStates[4]?.xShape)).toEqual(["mantissa:000:decimal"]);

    const rightCDivideRejectedStates = computeX2ValueStates(binaryProgram("3", "CE-2"));
    expect(x2ValueStateText(rightCDivideRejectedStates[4]?.x) ?? []).not.toContain("decimal:943.43434:normalized");
    expect(x2ShapeStateText(rightCDivideRejectedStates[4]?.xShape)).toEqual([]);

    const wideRightExponentStates = computeX2ValueStates(binaryProgram("16", "ГE-2"));
    expect(x2ValueStateText(wideRightExponentStates[4]?.x) ?? []).toContain("decimal:920:normalized");
    expect(x2ShapeStateText(wideRightExponentStates[4]?.xShape)).toEqual(["mantissa:920:decimal"]);

    const rightELeadingZeroStates = computeX2ValueStates(binaryProgram("16", "ЕE-2"));
    expect(x2ValueStateText(rightELeadingZeroStates[4]?.x) ?? []).toContain("decimal:52.92929:normalized");
    expect(x2ShapeStateText(rightELeadingZeroStates[4]?.xShape)).toEqual(["mantissa:052.92929:decimal"]);

    const rightBExponentMinusThreeDivideStates = computeX2ValueStates(binaryProgram("18", "BE-3"));
    expect(x2ValueStateText(rightBExponentMinusThreeDivideStates[4]?.x) ?? [])
      .toContain("decimal:9434.3434:normalized");
    expect(x2ShapeStateText(rightBExponentMinusThreeDivideStates[4]?.xShape)).toEqual([
      "mantissa:9434.3434:decimal",
    ]);

    const rightBExponentPlusOneDivideStates = computeX2ValueStates(binaryProgram("18", "BE1"));
    expect(x2ValueStateText(rightBExponentPlusOneDivideStates[4]?.x) ?? [])
      .toContain("decimal:0.94343434:normalized");
    expect(x2ShapeStateText(rightBExponentPlusOneDivideStates[4]?.xShape)).toEqual([
      "exponent:9.4343434:-1:decimal",
    ]);

    const rightGammaLeadingZeroExponentMinusThreeDivideStates = computeX2ValueStates(binaryProgram("12", "ГE-3"));
    expect(x2ValueStateText(rightGammaLeadingZeroExponentMinusThreeDivideStates[4]?.x) ?? [])
      .toContain("decimal:0:normalized");
    expect(x2ShapeStateText(rightGammaLeadingZeroExponentMinusThreeDivideStates[4]?.xShape)).toEqual([
      "mantissa:0000:decimal",
    ]);

    const rightELeadingZeroExponentMinusOneStates = computeX2ValueStates(binaryProgram("16", "ЕE-1"));
    expect(x2ValueStateText(rightELeadingZeroExponentMinusOneStates[4]?.x) ?? [])
      .toContain("decimal:5.292929:normalized");
    expect(x2ShapeStateText(rightELeadingZeroExponentMinusOneStates[4]?.xShape)).toEqual([
      "mantissa:05.292929:decimal",
    ]);

    const rejectedDivisionByZeroStates = computeX2ValueStates(binaryProgram("ГE-2", "0"));
    expect(x2ValueStateText(rejectedDivisionByZeroStates[4]?.x) ?? []).not.toContain("decimal:0:normalized");
    expect(x2ShapeStateText(rejectedDivisionByZeroStates[4]?.xShape)).toEqual([]);
  });

  it("x2 value dataflow models emulator-pinned structural hex exponent add/sub", () => {
    function binaryProgram(left: string, right: string, opcode: number, mnemonic: string): IrOp[] {
      return [
        recall("1", `preload const ${left}`),
        plain(0x0e, "В↑"),
        recall("2", `preload const ${right}`),
        plain(opcode, mnemonic),
        halt(),
      ];
    }

    const leftPlusStates = computeX2ValueStates(binaryProgram("ГE-2", "0", 0x10, "+"));
    expect(x2ValueStateText(leftPlusStates[4]?.x) ?? []).toContain("decimal:0.13:normalized");
    expect(x2ShapeStateText(leftPlusStates[4]?.xShape)).toEqual(["exponent:1.3:-1:decimal"]);

    const leftBPlusStates = computeX2ValueStates(binaryProgram("BE-2", "0", 0x10, "+"));
    expect(x2ValueStateText(leftBPlusStates[4]?.x) ?? []).toContain("decimal:0.11:normalized");
    expect(x2ShapeStateText(leftBPlusStates[4]?.xShape)).toEqual(["exponent:1.1:-1:decimal"]);

    const rightPlusStates = computeX2ValueStates(binaryProgram("9", "ГE-2", 0x10, "+"));
    expect(x2ValueStateText(rightPlusStates[4]?.x) ?? []).toContain("decimal:9.13:normalized");
    expect(x2ShapeStateText(rightPlusStates[4]?.xShape)).toEqual(["mantissa:9.13:decimal"]);

    const rightEPlusStates = computeX2ValueStates(binaryProgram("6", "ЕE-2", 0x10, "+"));
    expect(x2ValueStateText(rightEPlusStates[4]?.x) ?? []).toContain("decimal:6.14:normalized");
    expect(x2ShapeStateText(rightEPlusStates[4]?.xShape)).toEqual(["mantissa:6.14:decimal"]);

    const leftMinusStates = computeX2ValueStates(binaryProgram("ГE-2", "1", 0x11, "-"));
    expect(x2ValueStateText(leftMinusStates[4]?.x) ?? []).toContain("decimal:-0.87:normalized");
    expect(x2ShapeStateText(leftMinusStates[4]?.xShape)).toEqual(["exponent:-8.7:-1:decimal"]);

    const leftBMinusStates = computeX2ValueStates(binaryProgram("BE-2", "1", 0x11, "-"));
    expect(x2ValueStateText(leftBMinusStates[4]?.x) ?? []).toContain("decimal:-0.89:normalized");
    expect(x2ShapeStateText(leftBMinusStates[4]?.xShape)).toEqual(["exponent:-8.9:-1:decimal"]);

    const rightMinusStates = computeX2ValueStates(binaryProgram("0", "ГE-2", 0x11, "-"));
    expect(x2ValueStateText(rightMinusStates[4]?.x) ?? []).toContain("decimal:0.03:normalized");
    expect(x2ShapeStateText(rightMinusStates[4]?.xShape)).toEqual(["exponent:3:-2:decimal"]);

    const rightBMinusStates = computeX2ValueStates(binaryProgram("0", "BE-2", 0x11, "-"));
    expect(x2ValueStateText(rightBMinusStates[4]?.x) ?? []).toContain("decimal:0.05:normalized");
    expect(x2ShapeStateText(rightBMinusStates[4]?.xShape)).toEqual(["exponent:5:-2:decimal"]);

    const rightBSeventeenMinusStates = computeX2ValueStates(binaryProgram("17", "BE-2", 0x11, "-"));
    expect(x2ValueStateText(rightBSeventeenMinusStates[4]?.x) ?? []).toContain("decimal:17.05:normalized");
    expect(x2ShapeStateText(rightBSeventeenMinusStates[4]?.xShape)).toEqual(["mantissa:17.05:decimal"]);

    const leftGammaExponentMinusThreePlusStates = computeX2ValueStates(binaryProgram("ГE-3", "9", 0x10, "+"));
    expect(x2ValueStateText(leftGammaExponentMinusThreePlusStates[4]?.x) ?? []).toContain("decimal:9.013:normalized");
    expect(x2ShapeStateText(leftGammaExponentMinusThreePlusStates[4]?.xShape)).toEqual([
      "mantissa:9.013:decimal",
    ]);

    const rightBExponentMinusOnePlusStates = computeX2ValueStates(binaryProgram("9", "BE-1", 0x10, "+"));
    expect(x2ValueStateText(rightBExponentMinusOnePlusStates[4]?.x) ?? []).toContain("decimal:10.1:normalized");
    expect(x2ShapeStateText(rightBExponentMinusOnePlusStates[4]?.xShape)).toEqual([
      "mantissa:10.1:decimal",
    ]);

    const leftGammaExponentMinusOneMinusStates = computeX2ValueStates(binaryProgram("ГE-1", "9", 0x11, "-"));
    expect(x2ValueStateText(leftGammaExponentMinusOneMinusStates[4]?.x) ?? []).toContain("decimal:-7.7:normalized");
    expect(x2ShapeStateText(leftGammaExponentMinusOneMinusStates[4]?.xShape)).toEqual([
      "mantissa:-7.7:decimal",
    ]);

    const rightBExponentMinusThreeMinusStates = computeX2ValueStates(binaryProgram("9", "BE-3", 0x11, "-"));
    expect(x2ValueStateText(rightBExponentMinusThreeMinusStates[4]?.x) ?? []).toContain("decimal:9.005:normalized");
    expect(x2ShapeStateText(rightBExponentMinusThreeMinusStates[4]?.xShape)).toEqual([
      "mantissa:9.005:decimal",
    ]);

    const rightEExponentMinusOneMinusStates = computeX2ValueStates(binaryProgram("9", "ЕE-1", 0x11, "-"));
    expect(x2ValueStateText(rightEExponentMinusOneMinusStates[4]?.x) ?? []).toContain("decimal:9.2:normalized");
    expect(x2ShapeStateText(rightEExponentMinusOneMinusStates[4]?.xShape)).toEqual([
      "mantissa:9.2:decimal",
    ]);

    const leftAExponentZeroPlusStates = computeX2ValueStates(binaryProgram("AE0", "9", 0x10, "+"));
    expect(x2ValueStateText(leftAExponentZeroPlusStates[4]?.x) ?? []).toContain("decimal:19:normalized");
    expect(x2ShapeStateText(leftAExponentZeroPlusStates[4]?.xShape)).toEqual([
      "mantissa:19:decimal",
    ]);

    const rightAExponentZeroPlusStates = computeX2ValueStates(binaryProgram("9", "AE0", 0x10, "+"));
    expect(x2ValueStateText(rightAExponentZeroPlusStates[4]?.x) ?? []).toContain("decimal:3:normalized");
    expect(x2ShapeStateText(rightAExponentZeroPlusStates[4]?.xShape)).toEqual([
      "mantissa:3:decimal",
    ]);

    const leftGammaExponentZeroMinusStates = computeX2ValueStates(binaryProgram("ГE0", "9", 0x11, "-"));
    expect(x2ValueStateText(leftGammaExponentZeroMinusStates[4]?.x) ?? []).toContain("decimal:4:normalized");
    expect(x2ShapeStateText(leftGammaExponentZeroMinusStates[4]?.xShape)).toEqual([
      "mantissa:4:decimal",
    ]);

    const rightGammaExponentZeroMinusStates = computeX2ValueStates(binaryProgram("9", "ГE0", 0x11, "-"));
    expect(x2ValueStateText(rightGammaExponentZeroMinusStates[4]?.x) ?? []).toContain("decimal:-4:normalized");
    expect(x2ShapeStateText(rightGammaExponentZeroMinusStates[4]?.xShape)).toEqual([
      "mantissa:-4:decimal",
    ]);

    const unsupportedExponentStates = computeX2ValueStates(binaryProgram("ГE1", "1", 0x10, "+"));
    expect(x2ValueStateText(unsupportedExponentStates[4]?.x) ?? []).not.toContain("decimal:31:normalized");
    expect(x2ShapeStateText(unsupportedExponentStates[4]?.xShape)).toEqual([]);
  });

  it("x2 value dataflow stores hex A times 18 display shape but recalls normalized VP source", () => {
    const program: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      store("3"),
      plain(0x0d, "Cx"),
      recall("3"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[6]?.memory?.["3"]) ?? []).toContain("decimal:20:normalized");
    expect(x2ShapeStateText(states[6]?.shapeMemory?.["3"])).toEqual(["mantissa:020:decimal"]);
    expect(x2ValueStateText(states[8]?.x) ?? []).toContain("decimal:20:normalized");
    expect(x2ShapeStateText(states[8]?.xShape)).toEqual([
      "mantissa:020:decimal",
      "mantissa:20:decimal",
    ]);
    expect(x2EntryStateText(states[9])).toBe("exponent:20:");
    expect(x2EntryStateText(states[10])).toBe("exponent:20:3");
    expect(x2ValueStateText(states[10]?.x) ?? []).toContain("decimal:20000:normalized");
  });

  it("x2 value dataflow models emulator-pinned hex exponent multiplication facts", () => {
    const decimalLeftProgram: IrOp[] = [
      recall("2", "preload const 1"),
      plain(0x0e, "В↑"),
      recall("1", "preload const ГE-2"),
      plain(0x12, "×"),
      halt(),
    ];
    const decimalWideLeftProgram: IrOp[] = [
      recall("2", "preload const 16"),
      plain(0x0e, "В↑"),
      recall("1", "preload const ГE-2"),
      plain(0x12, "×"),
      halt(),
    ];
    const hexLeftProgram: IrOp[] = [
      recall("1", "preload const ГE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 5"),
      plain(0x12, "×"),
      halt(),
    ];

    expect(x2ValueStateText(computeX2ValueStates(decimalLeftProgram)[4]?.x) ?? [])
      .toContain("decimal:0.1:normalized");
    expect(x2ValueStateText(computeX2ValueStates(decimalWideLeftProgram)[4]?.x) ?? [])
      .toContain("decimal:1.6:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftProgram)[4]?.x) ?? [])
      .toContain("decimal:0.53:normalized");
    expect(x2ValueStateText(computeX2ValueStates(hexLeftProgram)[4]?.x) ?? [])
      .not.toContain("decimal:0.5:normalized");

    const hexLeftMinusOneProgram: IrOp[] = [
      recall("1", "preload const ГE-1"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 5"),
      plain(0x12, "×"),
      halt(),
    ];
    expect(x2ValueStateText(computeX2ValueStates(hexLeftMinusOneProgram)[4]?.x) ?? [])
      .toContain("decimal:5.3:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(hexLeftMinusOneProgram)[4]?.xShape)).toEqual([
      "mantissa:5.3:decimal",
    ]);

    const hexBMinusOneWideProgram: IrOp[] = [
      recall("1", "preload const BE-1"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 18"),
      plain(0x12, "×"),
      halt(),
    ];
    expect(x2ValueStateText(computeX2ValueStates(hexBMinusOneWideProgram)[4]?.x) ?? [])
      .toContain("decimal:5.4:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(hexBMinusOneWideProgram)[4]?.xShape)).toEqual([
      "mantissa:05.4:decimal",
    ]);

    const hexAMinusThreeProgram: IrOp[] = [
      recall("1", "preload const AE-3"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 1"),
      plain(0x12, "×"),
      halt(),
    ];
    expect(x2ValueStateText(computeX2ValueStates(hexAMinusThreeProgram)[4]?.x) ?? [])
      .toContain("decimal:0:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(hexAMinusThreeProgram)[4]?.xShape)).toEqual([
      "exponent:0:-3:decimal",
    ]);

    const hexCPlusOneWideProgram: IrOp[] = [
      recall("1", "preload const CE1"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 16"),
      plain(0x12, "×"),
      halt(),
    ];
    expect(x2ValueStateText(computeX2ValueStates(hexCPlusOneWideProgram)[4]?.x) ?? [])
      .toContain("decimal:9040:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(hexCPlusOneWideProgram)[4]?.xShape)).toEqual([
      "mantissa:9040:decimal",
    ]);

    const hexAZeroExponentStates = computeX2ValueStates([
      recall("1", "preload const AE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 1"),
      plain(0x12, "×"),
      halt(),
    ]);
    expect(x2ValueStateText(hexAZeroExponentStates[4]?.x) ?? []).toContain("decimal:0:normalized");
    expect(x2ShapeStateText(hexAZeroExponentStates[4]?.xShape)).toEqual(["exponent:0:-2:decimal"]);

    const hexBWideStates = computeX2ValueStates([
      recall("1", "preload const BE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 18"),
      plain(0x12, "×"),
      halt(),
    ]);
    expect(x2ValueStateText(hexBWideStates[4]?.x) ?? []).toContain("decimal:0.54:normalized");
    expect(x2ShapeStateText(hexBWideStates[4]?.xShape)).toEqual(["mantissa:0.54:decimal"]);

    const hexAWideStates = computeX2ValueStates([
      recall("1", "preload const AE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 15"),
      plain(0x12, "×"),
      halt(),
    ]);
    expect(x2ValueStateText(hexAWideStates[4]?.x) ?? []).toContain("decimal:9.9:normalized");
    expect(x2ShapeStateText(hexAWideStates[4]?.xShape)).toEqual(["mantissa:9.9:decimal"]);

    const decimalTimesCStates = computeX2ValueStates([
      recall("2", "preload const 17"),
      plain(0x0e, "В↑"),
      recall("1", "preload const CE-2"),
      plain(0x12, "×"),
      halt(),
    ]);
    expect(x2ValueStateText(decimalTimesCStates[4]?.x) ?? []).toContain("decimal:1.7:normalized");
    expect(x2ShapeStateText(decimalTimesCStates[4]?.xShape)).toEqual(["mantissa:1.7:decimal"]);

    const decimalTimesEStates = computeX2ValueStates([
      recall("2", "preload const 18"),
      plain(0x0e, "В↑"),
      recall("1", "preload const ЕE-2"),
      plain(0x12, "×"),
      halt(),
    ]);
    expect(x2ValueStateText(decimalTimesEStates[4]?.x) ?? []).toContain("decimal:0:normalized");
    expect(x2ShapeStateText(decimalTimesEStates[4]?.xShape)).toEqual(["mantissa:0:decimal"]);

    const decimalTimesAMinusThreeStates = computeX2ValueStates([
      recall("2", "preload const 16"),
      plain(0x0e, "В↑"),
      recall("1", "preload const AE-3"),
      plain(0x12, "×"),
      halt(),
    ]);
    expect(x2ValueStateText(decimalTimesAMinusThreeStates[4]?.x) ?? [])
      .toContain("decimal:0.16:normalized");
    expect(x2ShapeStateText(decimalTimesAMinusThreeStates[4]?.xShape)).toEqual([
      "exponent:1.6:-1:decimal",
    ]);

    const decimalTimesBMinusOneStates = computeX2ValueStates([
      recall("2", "preload const 18"),
      plain(0x0e, "В↑"),
      recall("1", "preload const BE-1"),
      plain(0x12, "×"),
      halt(),
    ]);
    expect(x2ValueStateText(decimalTimesBMinusOneStates[4]?.x) ?? [])
      .toContain("decimal:18:normalized");
    expect(x2ShapeStateText(decimalTimesBMinusOneStates[4]?.xShape)).toEqual(["mantissa:18:decimal"]);

    const decimalTimesGammaPlusOneStates = computeX2ValueStates([
      recall("2", "preload const 18"),
      plain(0x0e, "В↑"),
      recall("1", "preload const ГE1"),
      plain(0x12, "×"),
      halt(),
    ]);
    expect(x2ValueStateText(decimalTimesGammaPlusOneStates[4]?.x) ?? [])
      .toContain("decimal:1800:normalized");
    expect(x2ShapeStateText(decimalTimesGammaPlusOneStates[4]?.xShape)).toEqual(["mantissa:1800:decimal"]);

    const decimalTimesEPlusOneStates = computeX2ValueStates([
      recall("2", "preload const 18"),
      plain(0x0e, "В↑"),
      recall("1", "preload const ЕE1"),
      plain(0x12, "×"),
      halt(),
    ]);
    expect(x2ValueStateText(decimalTimesEPlusOneStates[4]?.x) ?? [])
      .toContain("decimal:0:normalized");
    expect(x2ShapeStateText(decimalTimesEPlusOneStates[4]?.xShape)).toEqual(["mantissa:0:decimal"]);
  });

  it("x2 value dataflow treats closed single-hex exponent mantissas as exponent operands", () => {
    const closedExponentOperand: X2ValueDataflowState = {
      y: new Set(),
      x: new Set<X2ValueFact>(["decimal:5:normalized"]),
      x2: new Set(),
      yShape: new Set<X2ShapeFact>(["hex:0.0Г:mantissa"]),
      xShape: new Set<X2ShapeFact>(["mantissa:5:decimal"]),
      entry: { kind: "closed" },
    };
    const ambiguousTail: X2ValueDataflowState = {
      ...closedExponentOperand,
      yShape: new Set<X2ShapeFact>(["hex:0.0Г0:mantissa"]),
    };
    const positiveShift: X2ValueDataflowState = {
      ...closedExponentOperand,
      yShape: new Set<X2ShapeFact>(["hex:Г00:mantissa"]),
    };

    const result = transferX2ValueStateForEdge(closedExponentOperand, plain(0x12, "×"), "normal", {}, 0);
    const rejected = transferX2ValueStateForEdge(ambiguousTail, plain(0x12, "×"), "normal", {}, 0);
    const positiveRejected = transferX2ValueStateForEdge(positiveShift, plain(0x12, "×"), "normal", {}, 0);

    expect(x2ValueStateText(result?.x) ?? []).toContain("decimal:0.53:normalized");
    expect(x2ValueStateText(result?.x) ?? [])
      .not.toContain("expr-key:12(decimal:5:normalized,shape:hex:0.0Г:mantissa)");
    expect(x2ShapeStateText(result?.xShape)).toEqual(["exponent:5.3:-1:decimal"]);
    expect(x2ValueStateText(rejected?.x) ?? []).not.toContain("decimal:0.53:normalized");
    expect(x2ValueStateText(positiveRejected?.x) ?? []).not.toContain("decimal:0.53:normalized");
  });

  it("x2 value dataflow preserves hex exponent multiply display shape for VP source", () => {
    const program: IrOp[] = [
      recall("2", "preload const 1"),
      plain(0x0e, "В↑"),
      recall("1", "preload const ГE-2"),
      plain(0x12, "×"),
      store("3"),
      plain(0x0d, "Cx"),
      recall("3"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[4]?.x) ?? []).toContain("decimal:0.1:normalized");
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual([
      "exponent:1:-1:decimal",
    ]);
    expect(x2ShapeStateText(states[5]?.shapeMemory?.["3"])).toEqual([
      "exponent:1:-1:decimal",
    ]);
    expect(x2ValueStateText(states[7]?.x) ?? []).toContain("decimal:0.1:normalized");
    expect(x2ShapeStateText(states[7]?.xShape)).toEqual([
      "exponent:1:-1:decimal",
    ]);
    expect(x2EntryStateText(states[8])).toBe("exponent:0.1:");
    expect(x2ValueStateText(states[9]?.x) ?? []).toContain("decimal:100:normalized");
  });

  it("x2 value dataflow keeps operand-specific hex exponent multiply display shapes", () => {
    const decimalWideLeftProgram: IrOp[] = [
      recall("2", "preload const 16"),
      plain(0x0e, "В↑"),
      recall("1", "preload const ГE-2"),
      plain(0x12, "×"),
      halt(),
    ];
    const hexLeftFractionProgram: IrOp[] = [
      recall("1", "preload const ГE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 5"),
      plain(0x12, "×"),
      halt(),
    ];
    const hexLeftWideProgram: IrOp[] = [
      recall("1", "preload const ГE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 16"),
      plain(0x12, "×"),
      halt(),
    ];

    expect(x2ShapeStateText(computeX2ValueStates(decimalWideLeftProgram)[4]?.xShape)).toEqual([
      "mantissa:1.6:decimal",
    ]);
    expect(x2ShapeStateText(computeX2ValueStates(hexLeftFractionProgram)[4]?.xShape)).toEqual([
      "exponent:5.3:-1:decimal",
    ]);
    expect(x2ShapeStateText(computeX2ValueStates(hexLeftWideProgram)[4]?.xShape)).toEqual([
      "mantissa:9.2:decimal",
    ]);
  });

  it("x2 value dataflow models exact MK-61 degree/minute conversion facts", () => {
    const toMinutesProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x03, "3"),
      plain(0x26, "К °->′"),
      halt(),
    ];
    const toSecondsProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x01, "1"),
      plain(0x04, "4"),
      plain(0x00, "0"),
      plain(0x04, "4"),
      plain(0x02, "2"),
      plain(0x2a, "К °->′\""),
      halt(),
    ];
    const fromSecondsProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x03, "3"),
      plain(0x04, "4"),
      plain(0x05, "5"),
      plain(0x30, "К °<-′\""),
      halt(),
    ];
    const fromMinutesProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x05, "5"),
      plain(0x33, "К °<-′"),
      halt(),
    ];
    const nonTerminatingToMinutesProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0x26, "К °->′"),
      halt(),
    ];

    expect(x2ValueStateText(computeX2ValueStates(toMinutesProgram)[4]?.x)).toEqual([
      "decimal:1.5:normalized",
      "expr:3",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(toSecondsProgram)[8]?.x)).toEqual([
      "decimal:1.2345:normalized",
      "expr:7",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(fromSecondsProgram)[7]?.x)).toEqual([
      "decimal:1.14042:normalized",
      "expr:6",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(fromMinutesProgram)[5]?.x)).toEqual([
      "decimal:1.15:normalized",
      "expr:4",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(nonTerminatingToMinutesProgram)[4]?.x)).toEqual([
      "expr-key:26(decimal:1.5:normalized)",
      "expr:3",
    ]);
  });

  it("x2 value dataflow models exact documented function special cases", () => {
    const unaryCases: Array<{
      readonly name: string;
      readonly input: IrOp[];
      readonly op: Extract<IrOp, { kind: "plain" }>;
      readonly index: number;
      readonly expected: string;
    }> = [
      { name: "e^0", input: [plain(0x00, "0")], op: plain(0x16, "F e^x"), index: 2, expected: "decimal:1:normalized" },
      { name: "lg(1)", input: [plain(0x01, "1")], op: plain(0x17, "F lg"), index: 2, expected: "decimal:0:normalized" },
      { name: "ln(1)", input: [plain(0x01, "1")], op: plain(0x18, "F ln"), index: 2, expected: "decimal:0:normalized" },
      { name: "asin(0)", input: [plain(0x00, "0")], op: plain(0x19, "F sin^-1"), index: 2, expected: "decimal:0:normalized" },
      { name: "acos(1)", input: [plain(0x01, "1")], op: plain(0x1a, "F cos^-1"), index: 2, expected: "decimal:0:normalized" },
      { name: "atan(0)", input: [plain(0x00, "0")], op: plain(0x1b, "F tg^-1"), index: 2, expected: "decimal:0:normalized" },
      { name: "sin(0)", input: [plain(0x00, "0")], op: plain(0x1c, "F sin"), index: 2, expected: "decimal:0:normalized" },
      { name: "cos(0)", input: [plain(0x00, "0")], op: plain(0x1d, "F cos"), index: 2, expected: "decimal:1:normalized" },
      { name: "tg(0)", input: [plain(0x00, "0")], op: plain(0x1e, "F tg"), index: 2, expected: "decimal:0:normalized" },
    ];

    for (const { input, op, index, expected } of unaryCases) {
      const states = computeX2ValueStates([...input, op, halt()]);
      expect(x2ValueStateText(states[index]?.x), op.meta.mnemonic).toContain(expected);
    }

    const nonSpecial = computeX2ValueStates([
      plain(0x02, "2"),
      plain(0x16, "F e^x"),
      halt(),
    ]);
    expect(x2ValueStateText(nonSpecial[2]?.x)).toEqual([
      "expr-key:16(decimal:2:normalized)",
      "expr:1",
    ]);
  });

  it("x2 value dataflow models exact F x^y zero-base and identity cases only", () => {
    const zeroBaseProgram: IrOp[] = [
      plain(0x03, "3"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x24, "F x^y"),
      halt(),
    ];
    const baseOneProgram: IrOp[] = [
      plain(0x07, "7"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x24, "F x^y"),
      halt(),
    ];
    const exponentZeroProgram: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x24, "F x^y"),
      halt(),
    ];
    const exponentOneProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x24, "F x^y"),
      halt(),
    ];
    const approximateProgram: IrOp[] = [
      plain(0x03, "3"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x24, "F x^y"),
      halt(),
    ];

    expect(x2ValueStateText(computeX2ValueStates(zeroBaseProgram)[4]?.x)).toEqual([
      "decimal:0:normalized",
      "expr:3",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(baseOneProgram)[4]?.x)).toEqual([
      "decimal:1:normalized",
      "expr:3",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(exponentZeroProgram)[4]?.x)).toEqual([
      "decimal:1:normalized",
      "expr:3",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(exponentOneProgram)[6]?.x)).toEqual([
      "decimal:1.2:normalized",
      "expr:5",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(approximateProgram)[4]?.x)).toEqual([
      "expr-key:24(decimal:3:normalized,decimal:2:normalized)",
      "expr:3",
    ]);
  });

  it("x2 value dataflow does not seed expr facts from non-whitelisted or role-bearing plain ops", () => {
    const randomProgram: IrOp[] = [
      plain(0x02, "2"),
      plain(0x3b, "К СЧ"),
      halt(),
    ];
    const displayProgram: IrOp[] = [
      plain(0x02, "2"),
      { kind: "plain", opcode: 0x21, meta: { mnemonic: "F sqrt", roles: ["display-byte"] } },
      halt(),
    ];

    expect(x2ValueStateText(computeX2ValueStates(randomProgram)[2]?.x)).toEqual([]);
    expect(x2ValueStateText(computeX2ValueStates(displayProgram)[2]?.x)).toEqual([]);
  });

  it("x2 value dataflow models closed decimal sign-change after X2 sync", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:-2:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:-2:normalized"]);
  });

  it("x2 value dataflow keeps closed scientific decimal sign-change display-shaped", () => {
    const program: IrOp[] = [
      recall("2", "preload const 1E8"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[2]?.x)).toEqual(["decimal:-100000000:normalized"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["decimal:-100000000:normalized"]);
    expect(x2ShapeStateText(states[2]?.xShape)).toEqual(["exponent:-1:8:decimal"]);
    expect(x2ShapeStateText(states[2]?.x2Shape)).toEqual(["exponent:-1:8:decimal"]);
    expect(x2EntryStateText(states[3])).toBe("exponent:-100000000:");
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:-10000000000:normalized"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual([
      "exponent:-100000000:2:decimal",
      "exponent:-1:10:decimal",
    ]);
  });

  it("x2 value dataflow sign-changes shape-only decimal exponent displays without value promotion", () => {
    const sameExponentState: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["exponent:1:8:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:1:8:decimal"]),
      entry: { kind: "closed" },
    };
    const equivalentDisplayState: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      entry: { kind: "closed" },
    };

    const sameExponent = transferX2ValueStateForEdge(sameExponentState, plain(0x0b, "/-/"), "normal");
    const equivalentDisplay = transferX2ValueStateForEdge(equivalentDisplayState, plain(0x0b, "/-/"), "normal");

    expect(x2ValueStateText(sameExponent?.x)).toEqual(["expr-key:0B(shape:exponent:1:8:decimal)"]);
    expect(x2ValueStateText(sameExponent?.x2)).toEqual(["expr-key:0B(shape:exponent:1:8:decimal)"]);
    expect(x2ShapeStateText(sameExponent?.xShape)).toEqual(["exponent:-1:8:decimal"]);
    expect(x2ShapeStateText(sameExponent?.x2Shape)).toEqual(["exponent:-1:8:decimal"]);
    expect(x2ShapeSetSafety(sameExponent?.x2Shape)).toBe("errorProne");
    expect(x2StateHasUnsafeDotRestoreShapeX2(sameExponent)).toBe(true);

    expect(x2ValueStateText(equivalentDisplay?.x)).toEqual(["expr-key:0B(shape:mantissa:100:decimal)"]);
    expect(x2ValueStateText(equivalentDisplay?.x2)).toEqual(["expr-key:0B(shape:mantissa:100:decimal)"]);
    expect(x2ShapeStateText(equivalentDisplay?.xShape)).toEqual([
      "exponent:-100:0:decimal",
      "mantissa:-100:decimal",
    ]);
    expect(x2ShapeStateText(equivalentDisplay?.x2Shape)).toEqual([
      "exponent:-100:0:decimal",
      "mantissa:-100:decimal",
    ]);
    expect(x2ShapeSetSafety(equivalentDisplay?.x2Shape)).toBe("errorProne");
  });

  it("x2 value dataflow sign-changes mixed decimal value and exact display-shape sources", () => {
    const mixedValueAndShape: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["decimal:0.5:normalized"]),
      x2: new Set(),
      xShape: new Set(),
      x2Shape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
      entry: { kind: "closed" },
    };
    const rawShapeOnly: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:0.5:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
      entry: { kind: "closed" },
    };
    const hiddenValueAndVisibleShape: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set<X2ValueFact>(["decimal:0.5:normalized"]),
      xShape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
      x2Shape: new Set(),
      entry: { kind: "closed" },
    };
    const hiddenValueAndRawVisibleShape: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set<X2ValueFact>(["decimal:0.5:normalized"]),
      xShape: new Set<X2ShapeFact>(["mantissa:0.5:decimal"]),
      x2Shape: new Set(),
      entry: { kind: "closed" },
    };

    const mixed = transferX2ValueStateForEdge(mixedValueAndShape, plain(0x0b, "/-/"), "normal");
    const raw = transferX2ValueStateForEdge(rawShapeOnly, plain(0x0b, "/-/"), "normal");
    const hiddenValue = transferX2ValueStateForEdge(hiddenValueAndVisibleShape, plain(0x0b, "/-/"), "normal");
    const hiddenValueRaw = transferX2ValueStateForEdge(
      hiddenValueAndRawVisibleShape,
      plain(0x0b, "/-/"),
      "normal",
    );

    expect(x2ValueStateText(mixed?.x)).toEqual(["expr-key:0B(shape:exponent:5:-1:decimal)"]);
    expect(x2ValueStateText(mixed?.x2)).toEqual(["expr-key:0B(shape:exponent:5:-1:decimal)"]);
    expect(x2ShapeStateText(mixed?.xShape)).toEqual(["exponent:-5:-1:decimal"]);
    expect(x2ShapeStateText(mixed?.x2Shape)).toEqual(["exponent:-5:-1:decimal"]);
    expect(x2ShapeSetSafety(mixed?.x2Shape)).toBe("errorProne");
    expect(x2StateHasUnsafeDotRestoreShapeX2(mixed)).toBe(true);
    expect(x2ValueStateText(raw?.x)).toEqual([]);
    expect(x2ShapeStateText(raw?.xShape)).toEqual([]);
    expect(x2ValueStateText(hiddenValue?.x)).toEqual(["decimal:-0.5:normalized"]);
    expect(x2ValueStateText(hiddenValue?.x2)).toEqual(["decimal:-0.5:normalized"]);
    expect(x2ShapeStateText(hiddenValue?.xShape)).toEqual(["exponent:-5:-1:decimal"]);
    expect(x2ShapeStateText(hiddenValue?.x2Shape)).toEqual(["exponent:-5:-1:decimal"]);
    expect(x2ValueStateText(hiddenValueRaw?.x)).toEqual([]);
    expect(x2ShapeStateText(hiddenValueRaw?.xShape)).toEqual([]);
  });

  it("x2 value dataflow creates decimal value facts from display shapes on return X2 sync", () => {
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["exponent:1:8:decimal"]),
      x2Shape: new Set(),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(state, ret(), "normal");

    expect(x2ValueStateText(result?.x2)).toContain("decimal:100000000:normalized");
    expect(x2ShapeStateText(result?.x2Shape)).toEqual(["exponent:1:8:decimal"]);
  });

  it("x2 value dataflow sign-changes decimal X2 shapes with visible decimal values", () => {
    const dotSafeShapeAndValue: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["decimal:2:normalized"]),
      x2: new Set(),
      xShape: new Set(),
      x2Shape: new Set<X2ShapeFact>(["mantissa:2:decimal"]),
      entry: { kind: "closed" },
    };
    const rawShapeAndValue: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["decimal:2:normalized"]),
      x2: new Set(),
      xShape: new Set(),
      x2Shape: new Set<X2ShapeFact>(["mantissa:02:decimal"]),
      entry: { kind: "closed" },
    };

    const dotSafe = transferX2ValueStateForEdge(dotSafeShapeAndValue, plain(0x0b, "/-/"), "normal");
    const raw = transferX2ValueStateForEdge(rawShapeAndValue, plain(0x0b, "/-/"), "normal");

    expect(x2ValueStateText(dotSafe?.x)).toEqual(["decimal:-2:normalized"]);
    expect(x2ValueStateText(dotSafe?.x2)).toEqual(["decimal:-2:normalized"]);
    expect(x2ShapeStateText(dotSafe?.xShape)).toEqual(["mantissa:-2:decimal"]);
    expect(x2ShapeStateText(dotSafe?.x2Shape)).toEqual(["mantissa:-2:decimal"]);
    expect(x2ValueStateText(raw?.x)).toEqual(["decimal:-2:normalized"]);
    expect(x2ValueStateText(raw?.x2)).toEqual(["decimal:-02:unnormalized"]);
    expect(x2ShapeStateText(raw?.xShape)).toEqual(["mantissa:-2:decimal"]);
    expect(x2ShapeStateText(raw?.x2Shape)).toEqual(["mantissa:-02:decimal"]);
  });

  it("x2 value dataflow carries non-zero closed sign-change into a following ВП", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:-2:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:-2:normalized"]);
    expect(x2EntryStateText(states[4])).toBe("exponent:-2:");
    expect(x2EntryStateText(states[5])).toBe("exponent:-2:3");
  });

  it("x2 value dataflow carries open mantissa sign-change into a following ВП", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[3])).toBe("exponent:-2:");
    expect(x2EntryStateText(states[4])).toBe("exponent:-2:3");
  });

  it("x2 value dataflow models closed zero sign-change as normalized X with signed-zero X2", () => {
    const program: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toEqual(["decimal:0:normalized"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["decimal:-0:unnormalized"]);
  });

  it("x2 value dataflow models signed zero as a distinct ВП mantissa shape", () => {
    const program: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[3])).toBe("exponent:-0:");
    expect(x2EntryStateText(states[4])).toBe("exponent:-0:3");
  });

  it("x2 VP shape context exposes active mantissa restore-run proofs", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);
    const context = analyzeX2VpShapeContext(states[2]);

    expect(context).toMatchObject({
      kind: "active-mantissa",
      phase: "active-entry",
      source: "decimal",
      restoresX2: false,
      canCancelExponentSignPair: false,
    });
    expect([...(context.mantissa ?? [])]).toEqual(["02"]);
    expect(x2StateCanDiscardRestoreRunBeforeProvedVp(states[2], states[5])).toBe(true);
  });

  it("x2 VP restore-run proof compares active mantissas through display source keys", () => {
    const base = computeX2ValueStates([halt()])[0]!;
    const activeMantissa: X2ValueDataflowState = {
      ...base,
      entry: { kind: "open", raw: new Set(["100"]) },
    };
    const sameExponentSource: X2ValueDataflowState = {
      ...base,
      vpEntryShape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
    };
    const differentExponentSource: X2ValueDataflowState = {
      ...base,
      vpEntryShape: new Set<X2ShapeFact>(["exponent:10:0:decimal"]),
    };
    const activeZero: X2ValueDataflowState = {
      ...base,
      entry: { kind: "open", raw: new Set(["0"]) },
    };
    const signedZeroSource: X2ValueDataflowState = {
      ...base,
      vpEntryMantissa: new Set(["-0"]),
    };

    expect(x2StateCanDiscardRestoreRunBeforeProvedVp(activeMantissa, sameExponentSource)).toBe(true);
    expect(x2StateCanDiscardRestoreRunBeforeProvedVp(activeMantissa, differentExponentSource)).toBe(false);
    expect(x2StateCanDiscardRestoreRunBeforeProvedVp(activeZero, signedZeroSource)).toBe(false);
  });

  it("x2 VP shape context uses structural source proofs for restore runs before ВП", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(analyzeX2VpShapeContext(states[1]).kind).toBe("none");
    expect(x2StateCanDiscardRestoreRunBeforeProvedVp(states[1], states[4])).toBe(true);
  });

  it("x2 value dataflow models VP first-digit structural splice after empty X2-preserving gap", () => {
    const program: IrOp[] = [
      recall("1", "preload const A"),
      recall("2", "preload const 8A0"),
      plain(0x14, "←→"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });
    const beforeVp = states[4];
    const activeExponent = analyzeX2VpShapeContext(states[5]);
    expect(x2ShapeStateText(beforeVp?.vpEntryShape)).toEqual(["hex:AA0:mantissa"]);
    expect(activeExponent).toMatchObject({
      kind: "active-structural-exponent",
      source: "structural",
      hasExponentDigit: false,
      restoresX2: true,
    });
    expect(x2ShapeStateText(activeExponent.shape)).toEqual(["hex:AA0:mantissa"]);
    expect(x2ShapeStateText(states[6]?.x2Shape)).toEqual(["hex-exponent:AA0:3"]);
  });

  it("x2 value dataflow recognizes branch-target ВП through address gaps", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x20, "F pi"),
      cjump("target"),
      jump("done"),
      label("target"),
      orphanAddress(54),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2VpEntryMantissaText(states[7])).toEqual(["3"]);
    expect(states[7]?.vpEntryMantissaTransient).toBe(true);
    expect(x2EntryStateText(states[8])).toBe("exponent:3:");
    expect(x2EntryStateText(states[9])).toBe("exponent:3:3");
  });

  it("x2 value dataflow models structural VP first-digit splice over decimal X2 tails", () => {
    const program: IrOp[] = [
      recall("1", "preload const A"),
      recall("2", "preload const 800"),
      plain(0x14, "←→"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });
    const beforeVp = states[4];
    const activeExponent = analyzeX2VpShapeContext(states[5]);

    expect(x2ShapeStateText(beforeVp?.x2Shape)).toEqual(["mantissa:800:decimal"]);
    expect(x2ShapeStateText(beforeVp?.vpEntryShape)).toEqual(["hex:A00:mantissa"]);
    expect(activeExponent).toMatchObject({
      kind: "active-structural-exponent",
      source: "structural",
    });
    expect(x2ShapeStateText(activeExponent.shape)).toEqual(["hex:A00:mantissa"]);
    expect(x2ShapeStateText(states[6]?.x2Shape)).toEqual(["hex-exponent:A00:3"]);
  });

  it("x2 value dataflow models structural VP first-digit splice over closed decimal exponent displays", () => {
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["hex:A:mantissa"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:1:2:decimal"]),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(state, plain(0x54, "КНОП"), "normal", {}, 0);

    expect(x2VpEntryShapeText(result)).toEqual(["hex:A00:mantissa"]);
    expect(result?.vpEntryShapeTransient).toBeUndefined();

    const unsafeFractional: X2ValueDataflowState = {
      ...state,
      x2Shape: new Set<X2ShapeFact>(["exponent:1:-1:decimal"]),
    };
    const fractionalResult = transferX2ValueStateForEdge(unsafeFractional, plain(0x54, "КНОП"), "normal", {}, 0);
    expect(fractionalResult?.vpEntryShape).toBeUndefined();

    const wideScientific: X2ValueDataflowState = {
      ...state,
      x2Shape: new Set<X2ShapeFact>(["exponent:100000000:2:decimal"]),
    };
    const wideResult = transferX2ValueStateForEdge(wideScientific, plain(0x54, "КНОП"), "normal", {}, 0);
    expect(wideResult?.vpEntryShape).toBeUndefined();
  });

  it("x2 value dataflow models decimal VP first-digit splice over decimal X2 tails", () => {
    const immediate: IrOp[] = [
      recall("1", "preload const 3"),
      recall("2", "preload const 800"),
      plain(0x14, "←→"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const throughEmpty: IrOp[] = [
      recall("1", "preload const 3"),
      recall("2", "preload const 800"),
      plain(0x14, "←→"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const decimalPointTail: IrOp[] = [
      recall("1", "preload const 3"),
      recall("2", "preload const 8.00"),
      plain(0x14, "←→"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const nonEmptyTransient: IrOp[] = [
      recall("1", "preload const 4"),
      recall("2", "preload const 800"),
      plain(0x14, "←→"),
      plain(0x20, "Fπ"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const nonEmptyThenEmpty: IrOp[] = [
      recall("1", "preload const 4"),
      recall("2", "preload const 800"),
      plain(0x14, "←→"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];

    const immediateStates = computeX2ValueStates(immediate, { trackRegisterMemory: true });
    expect(x2VpEntryMantissaText(immediateStates[3])).toEqual(["800"]);
    expect(immediateStates[3]?.vpEntryMantissaTransient).toBe(true);
    expect(x2EntryStateText(immediateStates[4])).toBe("exponent:800:");
    expect(x2EntryStateText(immediateStates[5])).toBe("exponent:800:3");
    expect(x2ValueStateText(immediateStates[5]?.x)).toEqual(["decimal:800000:normalized"]);

    const emptyStates = computeX2ValueStates(throughEmpty, { trackRegisterMemory: true });
    expect(x2VpEntryMantissaText(emptyStates[4])).toEqual(["300"]);
    expect(emptyStates[4]?.vpEntryMantissaTransient).toBeUndefined();
    expect(x2EntryStateText(emptyStates[5])).toBe("exponent:300:");
    expect(x2EntryStateText(emptyStates[6])).toBe("exponent:300:3");
    expect(x2ValueStateText(emptyStates[6]?.x)).toEqual(["decimal:300000:normalized"]);

    const decimalPointStates = computeX2ValueStates(decimalPointTail, { trackRegisterMemory: true });
    expect(x2VpEntryMantissaText(decimalPointStates[4])).toEqual(["3"]);
    expect(decimalPointStates[4]?.vpEntryMantissaTransient).toBeUndefined();
    expect(x2EntryStateText(decimalPointStates[5])).toBe("exponent:3:");
    expect(x2ValueStateText(decimalPointStates[6]?.x)).toEqual(["decimal:3000:normalized"]);

    const transientStates = computeX2ValueStates(nonEmptyTransient, { trackRegisterMemory: true });
    expect(x2VpEntryMantissaText(transientStates[4])).toEqual(["400"]);
    expect(transientStates[4]?.vpEntryMantissaTransient).toBe(true);
    expect(x2EntryStateText(transientStates[5])).toBe("exponent:400:");

    const laterEmptyStates = computeX2ValueStates(nonEmptyThenEmpty, { trackRegisterMemory: true });
    expect(x2VpEntryMantissaText(laterEmptyStates[5])).toEqual(["300"]);
    expect(laterEmptyStates[5]?.vpEntryMantissaTransient).toBeUndefined();
    expect(x2EntryStateText(laterEmptyStates[6])).toBe("exponent:300:");
  });

  it("x2 value dataflow models decimal VP first-digit splice over safe closed decimal exponent displays", () => {
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:3:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:1:2:decimal"]),
      entry: { kind: "closed" },
    };
    const result = transferX2ValueStateForEdge(state, plain(0x54, "КНОП"), "normal", {}, 0);

    expect(x2VpEntryMantissaText(result)).toEqual(["300"]);
    expect(result?.vpEntryMantissaTransient).toBeUndefined();

    const negativeDisplay: X2ValueDataflowState = {
      ...state,
      x2Shape: new Set<X2ShapeFact>(["exponent:-1:2:decimal"]),
    };
    const negativeResult = transferX2ValueStateForEdge(negativeDisplay, plain(0x54, "КНОП"), "normal", {}, 0);
    expect(negativeResult?.vpEntryMantissa).toBeUndefined();

    const fractionalDisplay: X2ValueDataflowState = {
      ...state,
      x2Shape: new Set<X2ShapeFact>(["exponent:1:-1:decimal"]),
    };
    const fractionalResult = transferX2ValueStateForEdge(fractionalDisplay, plain(0x54, "КНОП"), "normal", {}, 0);
    expect(fractionalResult?.vpEntryMantissa).toBeUndefined();

    const wideScientific: X2ValueDataflowState = {
      ...state,
      x2Shape: new Set<X2ShapeFact>(["exponent:100000000:2:decimal"]),
    };
    const wideResult = transferX2ValueStateForEdge(wideScientific, plain(0x54, "КНОП"), "normal", {}, 0);
    expect(wideResult?.vpEntryMantissa).toBeUndefined();
  });

  it("x2 value dataflow models transient VP first-digit splice after non-empty X2-preserving ops", () => {
    const program: IrOp[] = [
      recall("1", "preload const A"),
      recall("2", "preload const 8A0"),
      plain(0x14, "←→"),
      plain(0x20, "Fπ"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });
    const beforeVp = states[4];
    const activeExponent = analyzeX2VpShapeContext(states[5]);

    expect(x2ShapeStateText(beforeVp?.xShape)).toEqual(["mantissa:3.1415926:decimal"]);
    expect(x2ShapeStateText(beforeVp?.vpEntryShape)).toEqual(["hex:AA0:mantissa"]);
    expect(beforeVp?.vpEntryShapeTransient).toBe(true);
    expect(activeExponent).toMatchObject({
      kind: "active-structural-exponent",
      source: "structural",
    });
    expect(x2ShapeStateText(activeExponent.shape)).toEqual(["hex:AA0:mantissa"]);
  });

  it("x2 value dataflow first-digit splice materializes stable expr-key shapes", () => {
    const decimalState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)"]),
      x2: new Set<X2ValueFact>(["expr-key:22(decimal:10:normalized)"]),
      entry: { kind: "closed" },
    };
    const decimalAfterPi = transferX2ValueStateForEdge(decimalState, plain(0x20, "Fπ"), "normal", {}, 0);

    expect(x2VpEntryMantissaText(decimalAfterPi)).toContain("400");
    expect(decimalAfterPi?.vpEntryMantissaTransient).toBe(true);

    const structuralState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:31(shape:hex:-A:mantissa)"]),
      x2: new Set<X2ValueFact>(["expr-key:31(shape:hex:-8A0:mantissa)"]),
      entry: { kind: "closed" },
    };
    const structuralAfterPi = transferX2ValueStateForEdge(structuralState, plain(0x20, "Fπ"), "normal", {}, 0);

    expect(x2VpEntryShapeText(structuralAfterPi)).toContain("hex:AA0:mantissa");
    expect(structuralAfterPi?.vpEntryShapeTransient).toBe(true);
  });

  it("x2 value dataflow flow/store VP splices materialize stable expr-key shapes", () => {
    const decimalState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:22(decimal:2:normalized)"]),
      x2: new Set<X2ValueFact>(["expr-key:22(decimal:12:normalized)"]),
      entry: { kind: "closed" },
    };
    const decimalStore = transferX2ValueStateForEdge(decimalState, store("1"), "normal", {}, 0);
    const decimalIndirectFlow = transferX2ValueStateForEdge(
      decimalState,
      knownTargetIndirectJump("8", 4),
      "jump",
      { targetStartsWithVp: true },
      0,
    );

    expect(x2VpEntryMantissaText(decimalStore)).toContain("44");
    expect(decimalStore?.vpEntryShapeTransient).toBeUndefined();
    expect(x2VpEntryMantissaText(decimalIndirectFlow)).toContain("744");
    expect(decimalIndirectFlow?.vpEntryMantissaTransient).toBe(true);

    const structuralState: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:31(shape:hex:-A:mantissa)"]),
      x2: new Set<X2ValueFact>(["expr-key:31(shape:hex:-FACE:mantissa)"]),
      entry: { kind: "closed" },
    };
    const structuralStore = transferX2ValueStateForEdge(structuralState, store("1"), "normal", {}, 0);
    const structuralDirectFlow = transferX2ValueStateForEdge(
      structuralState,
      jump("vp"),
      "jump",
      { targetStartsWithVp: true },
      0,
    );
    const structuralIndirectFlow = transferX2ValueStateForEdge(
      structuralState,
      knownTargetIndirectJump("8", 4),
      "jump",
      { targetStartsWithVp: true },
      0,
    );

    expect(x2VpEntryShapeText(structuralStore)).toEqual(["hex:ACE:mantissa"]);
    expect(structuralStore?.vpEntryShapeTransient).toBe(true);
    expect(x2VpEntryShapeText(structuralDirectFlow)).toContain("hex:AACE:mantissa");
    expect(structuralDirectFlow?.vpEntryShapeTransient).toBe(true);
    expect(x2VpEntryShapeText(structuralIndirectFlow)).toContain("hex:7ACE:mantissa");
    expect(structuralIndirectFlow?.vpEntryShapeTransient).toBe(true);
  });

  it("x2 value dataflow drops transient VP first-digit sources across a later empty op", () => {
    const program: IrOp[] = [
      recall("1", "preload const A"),
      recall("2", "preload const 8A0"),
      plain(0x14, "←→"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });
    const afterPi = states[4];
    const beforeVp = states[5];
    const activeExponent = analyzeX2VpShapeContext(states[6]);

    expect(x2ShapeStateText(afterPi?.vpEntryShape)).toEqual(["hex:AA0:mantissa"]);
    expect(afterPi?.vpEntryShapeTransient).toBe(true);
    expect(x2ShapeStateText(beforeVp?.vpEntryShape)).toEqual(["hex:3A0:mantissa"]);
    expect(beforeVp?.vpEntryShapeTransient).toBeUndefined();
    expect(x2ShapeStateText(activeExponent.shape)).toEqual(["hex:3A0:mantissa"]);
    expect(x2ShapeStateText(states[7]?.x2Shape)).toEqual(["hex-exponent:3A0:3"]);
  });

  it("x2 VP restore-gap scanner shares label and role safety", () => {
    const safeGap: IrOp[] = [
      plain(0x02, "2"),
      plain(0x54, "КНОП"),
      label("marker"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const roleBearingGap: IrOp[] = [
      plain(0x02, "2"),
      { kind: "plain", opcode: 0x54, meta: { mnemonic: "КНОП", roles: ["display-byte"] } },
      plain(0x0c, "ВП"),
      halt(),
    ];
    const displayCommentGap: IrOp[] = [
      plain(0x02, "2"),
      { kind: "plain", opcode: 0x54, meta: { mnemonic: "КНОП", comment: "display spacer" } },
      plain(0x0c, "ВП"),
      halt(),
    ];
    const orphanAddressGap: IrOp[] = [
      plain(0x02, "2"),
      plain(0x54, "КНОП"),
      orphanAddress(54),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ];

    expect(x2HasOnlyRestoreGapBeforeVp(safeGap, 1)).toBe(true);
    expect(x2HasOnlyRestoreGapBeforeVp(roleBearingGap, 1)).toBe(false);
    expect(x2HasOnlyRestoreGapBeforeVp(displayCommentGap, 1)).toBe(false);
    expect(x2HasOnlyRestoreGapBeforeVp(orphanAddressGap, 1)).toBe(true);
  });

  it("x2 replacement-dot VP restore-gap scanner treats the inserted dot as the first restore", () => {
    const immediateVp: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const transparentGap: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      call("transparent"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const roleBearingGap: IrOp[] = [
      plain(0x02, "2"),
      { kind: "plain", opcode: 0x54, meta: { mnemonic: "КНОП", roles: ["display-byte"] } },
      plain(0x0c, "ВП"),
      halt(),
    ];

    expect(x2HasOnlyRestoreGapBeforeVp(immediateVp, 1)).toBe(false);
    expect(x2ReplacementDotHasOnlyRestoreGapBeforeVp(immediateVp, 1)).toBe(true);
    expect(x2ReplacementDotHasOnlyRestoreGapBeforeVp(
      transparentGap,
      6,
      directReturnAnalysisContext(transparentGap),
    )).toBe(true);
    expect(x2ReplacementDotHasOnlyRestoreGapBeforeVp(roleBearingGap, 1)).toBe(false);
  });

  it("x2 VP restore-gap scanner crosses only transparent return helpers with context", () => {
    const directReturnGap: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      call("transparent"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const observingReturnGap: IrOp[] = [
      jump("main"),
      label("observer"),
      store("1"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      call("observer"),
      plain(0x0c, "ВП"),
      halt(),
    ];

    expect(x2HasOnlyRestoreGapBeforeVp(directReturnGap, 6)).toBe(false);
    expect(x2HasOnlyRestoreGapBeforeVp(directReturnGap, 6, directReturnAnalysisContext(directReturnGap))).toBe(true);
    expect(x2HasOnlyRestoreGapBeforeVp(observingReturnGap, 6, directReturnAnalysisContext(observingReturnGap))).toBe(false);
  });

  it("x2 VP restore-gap scanner crosses nested transparent return helper chains", () => {
    const nestedReturnGap: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("transparent"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      call("outer"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const observingNestedReturnGap: IrOp[] = [
      jump("main"),
      label("observer"),
      plain(0x0a, "."),
      ret(),
      label("outer"),
      call("observer"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      call("outer"),
      plain(0x0c, "ВП"),
      halt(),
    ];

    expect(x2HasOnlyRestoreGapBeforeVp(nestedReturnGap, 9)).toBe(false);
    expect(x2HasOnlyRestoreGapBeforeVp(
      nestedReturnGap,
      9,
      directReturnAnalysisContext(nestedReturnGap),
    )).toBe(true);
    expect(x2HasOnlyRestoreGapBeforeVp(
      observingNestedReturnGap,
      9,
      directReturnAnalysisContext(observingNestedReturnGap),
    )).toBe(false);
  });

  it("x2 closed sign-change dot source crosses only transparent return helpers with context", () => {
    const transparentGap: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0a, "."),
      halt(),
    ];
    const observingGap: IrOp[] = [
      jump("main"),
      label("observer"),
      plain(0x0a, "."),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      call("observer"),
      plain(0x0a, "."),
      halt(),
    ];
    const transparentStates = computeX2ValueStates(transparentGap);
    const observingStates = computeX2ValueStates(observingGap);

    expect(x2CanUseClosedSignChangeDotSourceAt(transparentGap, 9, transparentStates[9])).toBe(false);
    expect(
      x2CanUseClosedSignChangeDotSourceAt(
        transparentGap,
        9,
        transparentStates[9],
        directReturnAnalysisContext(transparentGap),
      ),
    ).toBe(true);
    expect(
      x2CanUseClosedSignChangeDotSourceAt(
        observingGap,
        9,
        observingStates[9],
        directReturnAnalysisContext(observingGap),
      ),
    ).toBe(false);
  });

  it("x2 closed sign-change dot source crosses address gaps", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      orphanAddress(54),
      plain(0x0a, "."),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2CanUseClosedSignChangeDotSourceAt(program, 4, states[4])).toBe(true);
  });

  it("x2 closed sign-change dot source crosses nested transparent return helper chains", () => {
    const transparentGap: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("transparent"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      call("outer"),
      plain(0x0a, "."),
      halt(),
    ];
    const observingGap: IrOp[] = [
      jump("main"),
      label("observer"),
      plain(0x0a, "."),
      ret(),
      label("outer"),
      call("observer"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      call("outer"),
      plain(0x0a, "."),
      halt(),
    ];
    const transparentStates = computeX2ValueStates(transparentGap);
    const observingStates = computeX2ValueStates(observingGap);

    expect(x2CanUseClosedSignChangeDotSourceAt(transparentGap, 12, transparentStates[12])).toBe(false);
    expect(
      x2CanUseClosedSignChangeDotSourceAt(
        transparentGap,
        12,
        transparentStates[12],
        directReturnAnalysisContext(transparentGap),
      ),
    ).toBe(true);
    expect(
      x2CanUseClosedSignChangeDotSourceAt(
        observingGap,
        12,
        observingStates[12],
        directReturnAnalysisContext(observingGap),
      ),
    ).toBe(false);
  });

  it("x2 closed sign-change dot source treats display-sensitive restore cells as barriers", () => {
    const displaySign: IrOp = {
      kind: "plain",
      opcode: 0x0b,
      meta: { mnemonic: "/-/", comment: "display sign" },
    };
    const displayEmpty: IrOp = {
      kind: "plain",
      opcode: 0x54,
      meta: { mnemonic: "КНОП", comment: "display spacer" },
    };
    const displaySignProgram: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      displaySign,
      plain(0x0a, "."),
      halt(),
    ];
    const displayEmptyProgram: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      displayEmpty,
      plain(0x0a, "."),
      halt(),
    ];
    const signStates = computeX2ValueStates(displaySignProgram);
    const emptyStates = computeX2ValueStates(displayEmptyProgram);

    expect(x2CanUseClosedSignChangeDotSourceAt(displaySignProgram, 3, signStates[3])).toBe(false);
    expect(x2CanUseClosedSignChangeDotSourceAt(displayEmptyProgram, 4, emptyStates[4])).toBe(false);
  });

  it("x2 closed sign-change dot source accepts restored-visible decimal equality", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      store("2"),
      store("3"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[5]?.x)).toEqual(["decimal:-2:normalized"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["decimal:-02:unnormalized"]);
    expect(x2CanUseClosedSignChangeDotSourceAt(program, 5, states[5])).toBe(true);
  });

  it("x2 closed sign-change dot source keeps unsafe restored-visible structural shapes", () => {
    const program: IrOp[] = [
      recall("1", "preload const D"),
      store("2"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2CanUseClosedSignChangeDotSourceAt(program, 4, states[4])).toBe(false);
  });

  it("x2 normalized decimal restore-gap crosses only transparent return helpers with context", () => {
    const transparentGap: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К1"),
      call("transparent"),
      plain(0x0a, "."),
      halt(),
    ];
    const observingGap: IrOp[] = [
      jump("main"),
      label("observer"),
      plain(0x0a, "."),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К1"),
      call("observer"),
      plain(0x0a, "."),
      halt(),
    ];

    expect(x2NormalizedDecimalRestoreGapIsFreeStanding(transparentGap, 10)).toBe(false);
    expect(
      x2NormalizedDecimalRestoreGapIsFreeStanding(
        transparentGap,
        10,
        directReturnAnalysisContext(transparentGap),
      ),
    ).toBe(true);
    expect(
      x2NormalizedDecimalRestoreGapIsFreeStanding(
        observingGap,
        10,
        directReturnAnalysisContext(observingGap),
      ),
    ).toBe(false);
  });

  it("x2 source dot-restore admission shares transparent return-helper crossing", () => {
    const transparentGap: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К1"),
      call("transparent"),
      plain(0x0a, "."),
      halt(),
    ];
    const states = computeX2ValueStates(transparentGap);
    const context = directReturnAnalysisContext(transparentGap);

    expect(x2CanUseSourceDotRestoreAt(transparentGap, 10, states[10], false, false, true)).toBe(false);
    expect(x2CanUseSourceDotRestoreAt(transparentGap, 10, states[10], false, false, false, context)).toBe(false);
    expect(x2CanUseSourceDotRestoreAt(transparentGap, 10, states[10], false, false, true, context)).toBe(true);
  });

  it("x2 normalized decimal restore-gap crosses nested transparent return helper chains", () => {
    const transparentGap: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("transparent"),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К1"),
      call("outer"),
      plain(0x0a, "."),
      halt(),
    ];
    const observingGap: IrOp[] = [
      jump("main"),
      label("observer"),
      plain(0x0a, "."),
      ret(),
      label("outer"),
      call("observer"),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К1"),
      call("outer"),
      plain(0x0a, "."),
      halt(),
    ];

    expect(x2NormalizedDecimalRestoreGapIsFreeStanding(transparentGap, 13)).toBe(false);
    expect(
      x2NormalizedDecimalRestoreGapIsFreeStanding(
        transparentGap,
        13,
        directReturnAnalysisContext(transparentGap),
      ),
    ).toBe(true);
    expect(
      x2NormalizedDecimalRestoreGapIsFreeStanding(
        observingGap,
        13,
        directReturnAnalysisContext(observingGap),
      ),
    ).toBe(false);
  });

  it("x2 value dataflow keeps signed zero sticky across repeated sign-change before ВП", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[4])).toBe("exponent:-0:");
    expect(x2EntryStateText(states[5])).toBe("exponent:-0:3");
  });

  it("x2 value dataflow treats dot in open number entry as decimal input", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[2])).toBe("open:1.");
    expect(x2ValueStateText(states[2]?.x)).toEqual(["decimal:1:normalized"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["decimal:1.:unnormalized"]);
    expect(x2ShapeStateText(states[2]?.xShape)).toEqual(["mantissa:1:decimal"]);
    expect(x2ShapeStateText(states[2]?.x2Shape)).toEqual(["mantissa:1.:decimal"]);
  });

  it("x2 value dataflow tracks fractional decimal digit runs", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[3])).toBe("open:1.2");
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:1.2:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:1.2:normalized"]);
    expect(x2ShapeStateText(states[3]?.xShape)).toEqual(["mantissa:1.2:decimal"]);
    expect(x2ShapeStateText(states[3]?.x2Shape)).toEqual(["mantissa:1.2:decimal"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:1.2:normalized", "reg:2"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:1.2:normalized", "reg:2"]);
  });

  it("x2 value dataflow keeps duplicate decimal point during direct open-number entry", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x0a, "."),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[3])).toBe("open:1.");
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:1:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:1.:unnormalized"]);
    expect(x2ShapeStateText(states[3]?.xShape)).toEqual(["mantissa:1:decimal"]);
    expect(x2ShapeStateText(states[3]?.x2Shape)).toEqual(["mantissa:1.:decimal"]);
  });

  it("x2 value dataflow follows direct repeated-dot entry example", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0a, "."),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[5])).toBe("open:1.23");
    expect(x2ValueStateText(states[5]?.x)).toEqual(["decimal:1.23:normalized"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["decimal:1.23:normalized"]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["mantissa:1.23:decimal"]);
    expect(x2ShapeStateText(states[5]?.x2Shape)).toEqual(["mantissa:1.23:decimal"]);
  });

  it("x2 value dataflow treats jumped-to decimal point as restore before new digit entry", () => {
    const program: IrOp[] = [
      jump("second_dot"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      label("second_dot"),
      plain(0x0a, "."),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[6])).toBe("closed");
    expect(x2ValueStateText(states[6]?.x)).toEqual([]);
    expect(x2ValueStateText(states[6]?.x2)).toEqual([]);
    expect(x2EntryStateText(states[7])).toBe("open:3");
    expect(x2ValueStateText(states[7]?.x)).toEqual(["decimal:3:normalized"]);
    expect(x2ValueStateText(states[7]?.x2)).toEqual(["decimal:3:normalized"]);
  });

  it("x2 value dataflow keeps leading-zero fractional X2 separate from normalized X", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[4])).toBe("open:01.2");
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:1.2:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:01.2:unnormalized"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["mantissa:1.2:decimal"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["mantissa:01.2:decimal"]);
  });

  it("x2 value dataflow computes non-negative decimal fractional parts while preserving X2", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:1.2:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:1.2:normalized"]);
    expect(x2ValueStateText(states[5]?.x)).toEqual([
      "decimal:0.2:normalized",
      "expr:4",
    ]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["exponent:2:-1:decimal"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["decimal:1.2:normalized"]);
  });

  it("x2 value dataflow computes negative non-integer decimal fractional values with exponent display shape", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[5]?.x)).toEqual(["decimal:-1.2:normalized"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["decimal:-1.2:normalized"]);
    expect(x2ValueStateText(states[6]?.x)).toEqual([
      "decimal:-0.2:normalized",
      "expr:5",
    ]);
    expect(x2ShapeStateText(states[6]?.xShape)).toEqual(["exponent:-2:-1:decimal"]);
    expect(x2ValueStateText(states[6]?.x2)).toEqual(["decimal:-1.2:normalized"]);
  });

  it("x2 value dataflow keeps fractional display exponent normalized by first significant digit", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      plain(0x00, "0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[8]?.x)).toContain("decimal:0.0012:normalized");
    expect(x2ShapeStateText(states[8]?.xShape)).toEqual(["exponent:1.2:-3:decimal"]);
  });

  it("x2 value dataflow computes concrete decimal integer parts while preserving X2", () => {
    const positiveProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      halt(),
    ];
    const negativeProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      halt(),
    ];
    const negativeFractionProgram: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      halt(),
    ];
    const positiveStates = computeX2ValueStates(positiveProgram);
    const negativeStates = computeX2ValueStates(negativeProgram);
    const negativeFractionStates = computeX2ValueStates(negativeFractionProgram);

    expect(x2ValueStateText(positiveStates[5]?.x)).toEqual([
      "decimal:1:normalized",
      "expr:4",
    ]);
    expect(x2ValueStateText(positiveStates[5]?.x2)).toEqual(["decimal:1.2:normalized"]);
    expect(x2ValueStateText(negativeStates[6]?.x)).toEqual([
      "decimal:-1:normalized",
      "expr:5",
    ]);
    expect(x2ValueStateText(negativeStates[6]?.x2)).toEqual(["decimal:-1.2:normalized"]);
    expect(x2ValueStateText(negativeFractionStates[6]?.x)).toEqual([
      "decimal:0:normalized",
      "expr:5",
    ]);
    expect(x2ValueStateText(negativeFractionStates[6]?.x2)).toEqual(["decimal:-0.2:normalized"]);
  });

  it("x2 value dataflow computes concrete decimal abs and sign while preserving X2", () => {
    const absProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x31, "К |x|"),
      halt(),
    ];
    const negativeSignProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x32, "К ЗН"),
      halt(),
    ];
    const zeroSignProgram: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x32, "К ЗН"),
      halt(),
    ];
    const absStates = computeX2ValueStates(absProgram);
    const negativeSignStates = computeX2ValueStates(negativeSignProgram);
    const zeroSignStates = computeX2ValueStates(zeroSignProgram);

    expect(x2ValueStateText(absStates[6]?.x)).toEqual([
      "decimal:1.2:normalized",
      "expr:5",
    ]);
    expect(x2ValueStateText(absStates[6]?.x2)).toEqual(["decimal:-1.2:normalized"]);
    expect(x2ValueStateText(negativeSignStates[6]?.x)).toEqual([
      "decimal:-1:normalized",
      "expr:5",
    ]);
    expect(x2ShapeStateText(negativeSignStates[6]?.xShape)).toEqual(["mantissa:-1:decimal"]);
    expect(x2ValueStateText(negativeSignStates[6]?.x2)).toEqual(["decimal:-1.2:normalized"]);
    expect(x2ValueStateText(zeroSignStates[2]?.x)).toEqual([
      "decimal:0:normalized",
      "expr:1",
    ]);
    expect(x2ShapeStateText(zeroSignStates[2]?.xShape)).toEqual(["mantissa:0:decimal"]);
    expect(x2ValueStateText(zeroSignStates[2]?.x2)).toEqual(["decimal:0:normalized"]);
  });

  it("x2 value dataflow models emulator-pinned structural hex sign facts", () => {
    const positiveProgram: IrOp[] = [
      recall("1", "preload const 8F"),
      plain(0x32, "К ЗН"),
      halt(),
    ];
    const negativeProgram: IrOp[] = [
      recall("1", "preload const -8F"),
      plain(0x32, "К ЗН"),
      halt(),
    ];
    const leadingHexProgram: IrOp[] = [
      recall("1", "preload const 0A"),
      plain(0x32, "К ЗН"),
      halt(),
    ];
    const exponentProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x32, "К ЗН"),
      halt(),
    ];
    const unsafeFProgram: IrOp[] = [
      recall("1", "preload const F"),
      plain(0x32, "К ЗН"),
      halt(),
    ];
    const unsafeSuperProgram: IrOp[] = [
      recall("1", "preload const FA"),
      plain(0x32, "К ЗН"),
      halt(),
    ];

    expect(x2ValueStateText(computeX2ValueStates(positiveProgram)[2]?.x) ?? [])
      .toContain("decimal:1:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(positiveProgram)[2]?.xShape))
      .toEqual(["mantissa:1:decimal"]);
    expect(x2ValueStateText(computeX2ValueStates(positiveProgram)[2]?.x) ?? [])
      .not.toContain("expr-key:32(shape:hex:8F:mantissa)");
    expect(x2ValueStateText(computeX2ValueStates(negativeProgram)[2]?.x) ?? [])
      .toContain("decimal:-1:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(negativeProgram)[2]?.xShape))
      .toEqual(["mantissa:-1:decimal"]);
    expect(x2ValueStateText(computeX2ValueStates(leadingHexProgram)[2]?.x) ?? [])
      .toContain("decimal:1:normalized");
    expect(x2ValueStateText(computeX2ValueStates(exponentProgram)[5]?.x) ?? [])
      .toContain("decimal:1:normalized");
    expect(x2ValueStateText(computeX2ValueStates(exponentProgram)[5]?.x) ?? [])
      .not.toContain("expr-key:32(shape:hex-exponent:A:2)");
    expect(x2ValueStateText(computeX2ValueStates(unsafeFProgram)[2]?.x) ?? [])
      .not.toContain("decimal:1:normalized");
    expect(x2ValueStateText(computeX2ValueStates(unsafeFProgram)[2]?.x) ?? [])
      .not.toContain("decimal:0:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(unsafeFProgram)[2]?.xShape)).toEqual([]);
    expect(x2ValueStateText(computeX2ValueStates(unsafeSuperProgram)[2]?.x) ?? [])
      .not.toContain("decimal:0:normalized");
    expect(x2ShapeStateText(computeX2ValueStates(unsafeSuperProgram)[2]?.xShape)).toEqual([]);
  });

  it("x2 value dataflow computes concrete unary arithmetic facts while preserving X2", () => {
    const squareProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x22, "F x^2"),
      halt(),
    ];
    const reciprocalProgram: IrOp[] = [
      plain(0x04, "4"),
      plain(0x23, "F 1/x"),
      halt(),
    ];
    const sqrtProgram: IrOp[] = [
      plain(0x09, "9"),
      plain(0x21, "F sqrt"),
      halt(),
    ];
    const irrationalSqrtProgram: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      halt(),
    ];
    const pow10Program: IrOp[] = [
      plain(0x03, "3"),
      plain(0x15, "F 10^x"),
      halt(),
    ];
    const negativePow10Program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x15, "F 10^x"),
      halt(),
    ];

    expect(x2ValueStateText(computeX2ValueStates(squareProgram)[5]?.x)).toEqual([
      "decimal:1.44:normalized",
      "expr:4",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(squareProgram)[5]?.x2)).toEqual(["decimal:1.2:normalized"]);
    expect(x2ValueStateText(computeX2ValueStates(reciprocalProgram)[2]?.x)).toEqual([
      "decimal:0.25:normalized",
      "expr:1",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(sqrtProgram)[2]?.x)).toEqual([
      "decimal:3:normalized",
      "expr:1",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(irrationalSqrtProgram)[2]?.x)).toEqual([
      "expr-key:21(decimal:2:normalized)",
      "expr:1",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(pow10Program)[2]?.x)).toEqual([
      "decimal:1000:normalized",
      "expr:1",
    ]);
    expect(x2ValueStateText(computeX2ValueStates(negativePow10Program)[3]?.x)).toEqual([
      "decimal:0.01:normalized",
      "expr:2",
    ]);
  });

  it("x2 value dataflow seeds display shapes for exact unary results", () => {
    const cases: Array<{
      readonly program: IrOp[];
      readonly index: number;
      readonly shape: readonly string[];
    }> = [
      {
        program: [plain(0x03, "3"), plain(0x22, "F x^2"), halt()],
        index: 2,
        shape: ["mantissa:9:decimal"],
      },
      {
        program: [plain(0x09, "9"), plain(0x21, "F sqrt"), halt()],
        index: 2,
        shape: ["mantissa:3:decimal"],
      },
      {
        program: [plain(0x03, "3"), plain(0x15, "F 10^x"), halt()],
        index: 2,
        shape: ["mantissa:1000:decimal"],
      },
      {
        program: [plain(0x08, "8"), plain(0x15, "F 10^x"), halt()],
        index: 2,
        shape: ["exponent:1:8:decimal"],
      },
      {
        program: [plain(0x04, "4"), plain(0x23, "F 1/x"), halt()],
        index: 2,
        shape: ["exponent:2.5:-1:decimal"],
      },
      {
        program: [plain(0x02, "2"), plain(0x0b, "/-/"), plain(0x31, "К |x|"), halt()],
        index: 3,
        shape: ["mantissa:2:decimal"],
      },
      {
        program: [plain(0x01, "1"), plain(0x0a, "."), plain(0x02, "2"), plain(0x34, "К [x]"), halt()],
        index: 4,
        shape: ["mantissa:1:decimal"],
      },
      {
        program: [plain(0x00, "0"), plain(0x1d, "F cos"), halt()],
        index: 2,
        shape: ["mantissa:1:decimal"],
      },
    ];

    for (const testCase of cases) {
      const states = computeX2ValueStates(testCase.program);
      expect(x2ShapeStateText(states[testCase.index]?.xShape)).toEqual(testCase.shape);
    }
  });

  it("x2 value dataflow keeps exact fractional and scientific unary results display-shaped", () => {
    const reciprocalStates = computeX2ValueStates([
      plain(0x04, "4"),
      plain(0x23, "F 1/x"),
      halt(),
    ]);
    const negativePow10States = computeX2ValueStates([
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x15, "F 10^x"),
      halt(),
    ]);
    const widePow10States = computeX2ValueStates([
      plain(0x08, "8"),
      plain(0x15, "F 10^x"),
      halt(),
    ]);

    expect(x2ValueStateText(reciprocalStates[2]?.x)).toContain("decimal:0.25:normalized");
    expect(x2ShapeStateText(reciprocalStates[2]?.xShape)).toEqual(["exponent:2.5:-1:decimal"]);
    expect(x2ValueStateText(negativePow10States[3]?.x)).toContain("decimal:0.01:normalized");
    expect(x2ShapeStateText(negativePow10States[3]?.xShape)).toEqual(["exponent:1:-2:decimal"]);
    expect(x2ValueStateText(widePow10States[2]?.x)).toEqual([
      "decimal:100000000:normalized",
      "expr:1",
    ]);
    expect(x2ShapeStateText(widePow10States[2]?.xShape)).toEqual(["exponent:1:8:decimal"]);
  });

  it("x2 value dataflow models negative integer fractional parts as visible signed-zero", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:0:normalized", "expr:3"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:-2:normalized"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["mantissa:-0:decimal"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["mantissa:-2:decimal"]);
    expect(x2ShapeSetSafety(states[4]?.xShape)).toBe("errorProne");
  });

  it("x2 value dataflow uses synced signed-zero fractional shape as VP source", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["mantissa:-0:decimal"]);
    expect(x2ShapeStateText(states[5]?.x2Shape)).toEqual(["mantissa:-0:decimal"]);
    expect(x2EntryStateText(states[6])).toBe("exponent:-0:");
    expect(x2EntryStateText(states[7])).toBe("exponent:-0:3");
  });

  it("x2 value dataflow carries signed-zero VP source through dot restore", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ShapeStateText(states[6]?.xShape)).toEqual(["mantissa:0:decimal"]);
    expect(x2ShapeStateText(states[6]?.x2Shape)).toEqual(["mantissa:-0:decimal"]);
    expect(x2EntryStateText(states[7])).toBe("exponent:-0:");
    expect(x2EntryStateText(states[8])).toBe("exponent:-0:3");
  });

  it("x2 value dataflow uses raw decimal dot restores as VP mantissa sources", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      store("1"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:02:unnormalized"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:02:unnormalized"]);
    expect(x2VpEntryMantissaText(states[4])).toEqual(["2"]);
    expect(x2EntryStateText(states[5])).toBe("exponent:2:");
    expect(x2EntryStateText(states[6])).toBe("exponent:2:3");
  });

  it("x2 value dataflow keeps synced signed-zero sticky through closed sign-change", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[6]?.x)).toEqual(["decimal:0:normalized"]);
    expect(x2ValueStateText(states[6]?.x2)).toEqual(["decimal:-0:unnormalized"]);
    expect(x2EntryStateText(states[7])).toBe("exponent:-0:");
    expect(x2EntryStateText(states[8])).toBe("exponent:-0:3");
  });

  it("x2 value dataflow tracks sign-change during fractional decimal entry", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[4])).toBe("closed");
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:-1.2:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:-1.2:normalized"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["mantissa:-1.2:decimal"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["mantissa:-1.2:decimal"]);
  });

  it("x2 value dataflow tracks exponent-entry state after ВП", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[3])).toBe("exponent:12:");
    expect(x2EntryStateText(states[4])).toBe("exponent:12:3");
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:12000:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual([]);
    expect(x2EntryStateText(states[5])).toBe("closed");
    expect(x2ValueStateText(states[5]?.x)).toEqual(["decimal:12000:normalized", "reg:2"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["decimal:12000:normalized", "reg:2"]);
  });

  it("x2 value dataflow closes exponent-entry values through direct conditionals", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      cjump("jumped"),
      plain(0x20, "F pi"),
      jump("done"),
      label("jumped"),
      plain(0x20, "F pi"),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:5000:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:5000:normalized"]);
    expect(x2ValueStateText(states[7]?.x)).toEqual(["decimal:5000:normalized"]);
    expect(x2ValueStateText(states[7]?.x2)).toEqual(["decimal:5000:normalized"]);
  });

  it("x2 value dataflow closes exponent-entry values through direct returns", () => {
    const program: IrOp[] = [
      label("main"),
      call("load"),
      store("2"),
      halt(),
      label("load"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      ret(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toEqual(["decimal:5000:normalized"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["decimal:5000:normalized"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:5000:normalized", "reg:2"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:5000:normalized", "reg:2"]);
  });

  it("x2 immediate-sync dataflow follows direct returns to call continuations", () => {
    const program: IrOp[] = [
      label("main"),
      call("load"),
      plain(0x0a, "."),
      halt(),
      label("load"),
      recall("1"),
      ret(),
    ];
    const states = computeX2ImmediateSyncStates(program);

    expect(states[2]).toBe(true);
  });

  it("x2 value dataflow models ВП after a proved closed decimal X2 sync", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[3])).toBe("closed");
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:2:normalized"]);
    expect(x2EntryStateText(states[4])).toBe("exponent:2:");
    expect(x2EntryStateText(states[5])).toBe("exponent:2:3");
  });

  it("x2 value dataflow models ВП after a conditional fallthrough X2 sync", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      cjump("done"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[3])).toBe("closed");
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:2:normalized"]);
    expect(x2EntryStateText(states[4])).toBe("exponent:2:");
    expect(x2EntryStateText(states[5])).toBe("exponent:2:3");
  });

  it("x2 value dataflow models ВП after a loop fallthrough X2 sync", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      loop("done"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[3])).toBe("closed");
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:2:normalized"]);
    expect(x2EntryStateText(states[4])).toBe("exponent:2:");
    expect(x2EntryStateText(states[5])).toBe("exponent:2:3");
  });

  it("x2 value dataflow normalizes structural shapes on path-sensitive X2 sync", () => {
    const conditionalProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const loopProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      loop("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const returnProgram: IrOp[] = [
      label("main"),
      call("load"),
      plain(0x0a, "."),
      halt(),
      label("load"),
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      ret(),
    ];
    const conditionalStates = computeX2ValueStates(conditionalProgram, { trackRegisterMemory: true });
    const loopStates = computeX2ValueStates(loopProgram, { trackRegisterMemory: true });
    const returnStates = computeX2ValueStates(returnProgram, { trackRegisterMemory: true });

    for (const state of [conditionalStates[3], loopStates[3], returnStates[2]]) {
      expect(x2ShapeStateText(state?.xShape)).toEqual(["mantissa:00:decimal"]);
      expect(x2ShapeStateText(state?.x2Shape)).toEqual(["mantissa:0:decimal"]);
    }
  });

  it("x2 value dataflow drops only the mutated loop-counter alias", () => {
    const program: IrOp[] = [
      recall("0"),
      loop("done"),
      plain(0x54, "К НОП"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[1]?.x)).toEqual(["reg:0"]);
    expect(x2ValueStateText(states[2]?.x)).toEqual(["expr:1"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["expr:1"]);
  });

  it("x2 value dataflow creates structural VP-entry shape on conditional fallthrough X2 sync", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      store("1"),
      cjump("done"),
      plain(0x54, "К НОП"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2VpEntryShapeText(states[2])).toEqual(["hex:.70Е2-6С:mantissa"]);
    expect(states[2]?.vpEntryShapeTransient).toBe(true);
    expect(x2VpEntryShapeText(states[3])).toEqual(["hex:8.70Е2-6С:mantissa"]);
    expect(x2VpEntryShapeText(states[5])).toEqual([]);
  });

  it("x2 value dataflow creates decimal value facts from display shapes on conditional X2 sync", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      cjump("done"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ShapeStateText(states[3]?.x2Shape)).toEqual(["exponent:1:8:decimal"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual([]);
    expect(x2ValueStateText(states[4]?.x2)).toContain("decimal:100000000:normalized");
  });

  it("x2 value dataflow creates structural VP-entry shape on loop fallthrough X2 sync", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      store("1"),
      loop("done"),
      plain(0x54, "К НОП"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2VpEntryShapeText(states[2])).toEqual(["hex:.70Е2-6С:mantissa"]);
    expect(states[2]?.vpEntryShapeTransient).toBe(true);
    expect(x2VpEntryShapeText(states[3])).toEqual(["hex:8.70Е2-6С:mantissa"]);
    expect(x2VpEntryShapeText(states[5])).toEqual([]);
  });

  it("x2 value dataflow keeps structural exponent-entry shape after VP", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[2]?.x)).toEqual([]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual([]);
    expect(x2ShapeStateText(states[2]?.xShape)).toEqual(["hex-exponent:FACE:"]);
    expect(x2ShapeStateText(states[3]?.xShape)).toEqual(["hex-exponent:FACE:-"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex-exponent:FACE:"]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex-exponent:FACE:3"]);
    expect(x2StructuralVpContextStateText(states[6])).toBe("exponent:hex:FACE:mantissa:3");
    expect(x2StateIsClosedPlainContext(states[6])).toBe(false);
  });

  it("x2 value dataflow models closed structural exponent mantissa sign-change after X2 sync", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex-exponent:FACE:3"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["hex-exponent:FACE:3"]);
    expect(x2StateIsClosedPlainContext(states[4])).toBe(true);
    expect(x2ValueStateText(states[5]?.x)).toEqual([
      "expr-key:0B(shape:hex:FACE000:mantissa)",
      "expr:4",
    ]);
    expect(x2ValueStateText(states[5]?.x)).not.toContain("expr-key:0B(shape:hex-exponent:FACE:3)");
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex-exponent:-FACE:3"]);
    expect(x2ShapeStateText(states[5]?.x2Shape)).toEqual(["hex-exponent:-FACE:3"]);
    expect(x2VpEntryShapeText(states[5])).toEqual(["hex:-FACE000:mantissa"]);
    expect(x2StateIsClosedPlainContext(states[5])).toBe(true);
  });

  it("x2 value dataflow sign-changes normalized structural arithmetic after path-sensitive X2 sync", () => {
    const conditionalProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      cjump("done"),
      plain(0x0b, "/-/"),
      halt(),
      label("done"),
      halt(),
    ];
    const returnProgram: IrOp[] = [
      label("main"),
      call("load"),
      plain(0x0b, "/-/"),
      halt(),
      label("load"),
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      ret(),
    ];
    const conditionalStates = computeX2ValueStates(conditionalProgram, { trackRegisterMemory: true });
    const returnStates = computeX2ValueStates(returnProgram, { trackRegisterMemory: true });

    for (const state of [conditionalStates[4], returnStates[3]]) {
      expect(x2ValueStateText(state?.x)).toEqual(["decimal:0:normalized"]);
      expect(x2ValueStateText(state?.x2)).toEqual(["decimal:-0:unnormalized"]);
      expect(x2ShapeStateText(state?.xShape)).toEqual(["mantissa:0:decimal"]);
      expect(x2ShapeStateText(state?.x2Shape)).toEqual(["mantissa:-0:decimal"]);
    }
  });

  it("x2 value dataflow keeps indirect conditional fallthrough ВП structural-only", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      knownTargetIndirectCjump("8", 5),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
      label("target"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[2])).toBe("closed");
    expect(x2ValueStateText(states[2]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["decimal:2:normalized"]);
    expect(x2EntryStateText(states[3])).toBe("unknown");
  });

  it("x2 value dataflow uses indirect jump as a ВП first-digit source", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x00, "0"),
      knownTargetIndirectJump("8", 3),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2VpEntryMantissaText(states[3])).toEqual(["70"]);
    expect(states[3]?.vpEntryMantissaTransient).toBe(true);
    expect(x2EntryStateText(states[4])).toBe("exponent:70:");
    expect(x2EntryStateText(states[5])).toBe("exponent:70:3");
  });

  it("x2 value dataflow uses 8 as the indirect ВП first digit for zero X2", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      knownTargetIndirectJump("8", 2),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2VpEntryMantissaText(states[2])).toEqual(["8"]);
    expect(states[2]?.vpEntryMantissaTransient).toBe(true);
    expect(x2EntryStateText(states[3])).toBe("exponent:8:");
    expect(x2EntryStateText(states[4])).toBe("exponent:8:3");
  });

  it("x2 value dataflow uses indirect conditional jump edges as ВП first-digit sources", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x00, "0"),
      knownTargetIndirectCjump("8", 5),
      jump("done"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2VpEntryMantissaText(states[4])).toEqual(["70"]);
    expect(x2EntryStateText(states[5])).toBe("exponent:70:");
    expect(x2EntryStateText(states[6])).toBe("exponent:70:3");
  });

  it("x2 value dataflow uses indirect jump as a structural ВП first-digit source", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      knownTargetIndirectJump("8", 2),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2VpEntryShapeText(states[2])).toEqual(["hex:7ACE:mantissa"]);
    expect(states[2]?.vpEntryShapeTransient).toBe(true);
    expect(x2ShapeStateText(states[3]?.xShape)).toEqual(["hex-exponent:7ACE:"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex-exponent:7ACE:3"]);
  });

  it("x2 value dataflow uses direct jump as a ВП first-digit source", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x20, "F pi"),
      jump("target"),
      label("target"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2VpEntryMantissaText(states[5])).toEqual(["3"]);
    expect(states[5]?.vpEntryMantissaTransient).toBe(true);
    expect(x2EntryStateText(states[6])).toBe("exponent:3:");
    expect(x2EntryStateText(states[7])).toBe("exponent:3:3");
  });

  it("x2 value dataflow uses direct conditional jump edges as ВП first-digit sources", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x20, "F pi"),
      cjump("target"),
      jump("done"),
      label("target"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2VpEntryMantissaText(states[6])).toEqual(["3"]);
    expect(states[6]?.vpEntryMantissaTransient).toBe(true);
    expect(x2EntryStateText(states[7])).toBe("exponent:3:");
    expect(x2EntryStateText(states[8])).toBe("exponent:3:3");
  });

  it("x2 value dataflow uses counted-loop jump edges as indirect ВП first-digit sources", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x20, "F pi"),
      loop("target"),
      jump("done"),
      label("target"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2VpEntryMantissaText(states[6])).toEqual(["7"]);
    expect(states[6]?.vpEntryMantissaTransient).toBe(true);
    expect(x2EntryStateText(states[7])).toBe("exponent:7:");
    expect(x2EntryStateText(states[8])).toBe("exponent:7:3");
  });

  it("x2 value dataflow uses 8 as the counted-loop ВП first digit for zero X2", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0xf0, "F* empty F0"),
      plain(0x20, "F pi"),
      loop("target"),
      jump("done"),
      label("target"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2VpEntryMantissaText(states[6])).toEqual(["8"]);
    expect(states[6]?.vpEntryMantissaTransient).toBe(true);
    expect(x2EntryStateText(states[7])).toBe("exponent:8:");
    expect(x2EntryStateText(states[8])).toBe("exponent:8:3");
  });

  it("x2 value dataflow uses direct jump as a structural ВП first-digit source", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      plain(0x20, "F pi"),
      jump("target"),
      label("target"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2VpEntryShapeText(states[4])).toEqual(["hex:3ACE:mantissa"]);
    expect(states[4]?.vpEntryShapeTransient).toBe(true);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex-exponent:3ACE:"]);
    expect(x2ShapeStateText(states[6]?.xShape)).toEqual(["hex-exponent:3ACE:3"]);
  });

  it("x2 value dataflow uses counted-loop jump edges as structural indirect ВП sources", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      plain(0x20, "F pi"),
      loop("target"),
      jump("done"),
      label("target"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2VpEntryShapeText(states[5])).toEqual(["hex:7ACE:mantissa"]);
    expect(states[5]?.vpEntryShapeTransient).toBe(true);
    expect(x2ShapeStateText(states[6]?.xShape)).toEqual(["hex-exponent:7ACE:"]);
    expect(x2ShapeStateText(states[7]?.xShape)).toEqual(["hex-exponent:7ACE:3"]);
  });

  it("x2 value dataflow preserves raw sign sources on conditional jump edges", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      cjump("target"),
      jump("done"),
      label("target"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      jump("end"),
      label("done"),
      halt(),
      label("end"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2VpEntryMantissaText(states[7])).toBeUndefined();
    expect(x2VpEntrySignMantissaText(states[7])).toEqual(["02"]);
    expect(x2EntryStateText(states[9])).toBe("exponent:-02:");
    expect(x2EntryStateText(states[10])).toBe("exponent:-02:3");
  });

  it("x2 value dataflow preserves closed sign-change sources on conditional jump edges", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0b, "/-/"),
      cjump("target"),
      jump("done"),
      label("target"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      jump("end"),
      label("done"),
      halt(),
      label("end"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2VpEntryMantissaText(states[5])).toEqual(["-02"]);
    expect(x2VpEntrySignMantissaText(states[5])).toEqual(["-02"]);
    expect(x2VpEntryMantissaText(states[8])).toBeUndefined();
    expect(x2VpEntrySignMantissaText(states[8])).toEqual(["-02"]);
    expect(x2EntryStateText(states[10])).toBe("exponent:02:");
    expect(x2EntryStateText(states[11])).toBe("exponent:02:3");
  });

  it("x2 value edge transfer preserves explicit structural sign-shape sources on conditional jump edges", () => {
    const source: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set(),
      x2Shape: new Set(),
      entry: { kind: "closed" },
      vpEntrySignShape: new Set<X2ShapeFact>(["hex:FACE:mantissa"]),
    };
    const afterJump = transferX2ValueStateForEdge(
      source,
      cjump("target"),
      "jump",
      { trackRegisterMemory: true },
    );
    const afterSign = transferX2ValueStateForEdge(
      afterJump,
      plain(0x0b, "/-/"),
      "normal",
      { trackRegisterMemory: true },
    );
    const afterVp = transferX2ValueStateForEdge(
      afterSign,
      plain(0x0c, "ВП"),
      "normal",
      { trackRegisterMemory: true },
    );
    const activeExponent = analyzeX2VpShapeContext(afterVp);

    expect(x2VpEntryShapeText(afterJump)).toBeUndefined();
    expect(x2VpEntrySignShapeText(afterJump)).toEqual(["hex:FACE:mantissa"]);
    expect(x2ShapeStateText(afterSign?.xShape)).toEqual(["hex:-FACE:mantissa"]);
    expect(activeExponent).toMatchObject({
      kind: "active-structural-exponent",
      source: "structural",
    });
    expect(x2ShapeStateText(activeExponent.shape)).toEqual(["hex:-FACE:mantissa"]);
  });

  it("x2 value edge transfer preserves explicit decimal display sign-shape sources", () => {
    const source: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set(),
      x2Shape: new Set(),
      entry: { kind: "closed" },
      vpEntrySignShape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
    };
    const afterJump = transferX2ValueStateForEdge(
      source,
      cjump("target"),
      "jump",
      { trackRegisterMemory: true },
    );
    const afterSign = transferX2ValueStateForEdge(
      afterJump,
      plain(0x0b, "/-/"),
      "normal",
      { trackRegisterMemory: true },
    );

    expect(x2VpEntryShapeText(afterJump)).toBeUndefined();
    expect(x2VpEntrySignShapeText(afterJump)).toEqual(["exponent:5:-1:decimal"]);
    expect(x2ValueStateText(afterSign?.x)).toEqual(["expr-key:0B(shape:exponent:5:-1:decimal)"]);
    expect(x2ShapeStateText(afterSign?.xShape)).toContain("exponent:-5:-1:decimal");
    expect(x2VpEntryShapeText(afterSign)).toBeUndefined();
    expect(x2VpEntrySignShapeText(afterSign)).toContain("exponent:-5:-1:decimal");
  });

  it("x2 value edge transfer seeds decimal display sign-shapes on conditional fallthrough sync", () => {
    const source: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
      entry: { kind: "closed" },
    };
    const afterFallthrough = transferX2ValueStateForEdge(
      source,
      cjump("target"),
      "fallthrough",
      { trackRegisterMemory: true },
    );

    expect(x2VpEntryMantissaText(afterFallthrough)).toEqual(["0.5"]);
    expect(x2VpEntryShapeText(afterFallthrough)).toBeUndefined();
    expect(x2VpEntrySignShapeText(afterFallthrough)).toEqual(["exponent:5:-1:decimal"]);
  });

  it("x2 value dataflow carries a closed decimal ВП sync through empty op gaps", () => {
    const throughEmpty: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];

    expect(x2EntryStateText(computeX2ValueStates(throughEmpty)[4])).toBe("exponent:2:");
  });

  it("x2 value dataflow models direct store as a decimal ВП splice source", () => {
    const afterSyncedStore: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const leadingZero: IrOp[] = [
      plain(0x00, "0"),
      plain(0x05, "5"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const allZero: IrOp[] = [
      plain(0x00, "0"),
      plain(0x00, "0"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const zeroTail: IrOp[] = [
      plain(0x01, "1"),
      plain(0x00, "0"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const openPointZeroTail: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const fractional: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const negative: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const negativeFractional: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const negativeZero: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0b, "/-/"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];

    const afterSyncedStoreStates = computeX2ValueStates(afterSyncedStore);
    expect(x2EntryStateText(afterSyncedStoreStates[4])).toBe("exponent:0.:");
    expect(x2EntryStateText(afterSyncedStoreStates[5])).toBe("exponent:0.:3");
    const leadingZeroStates = computeX2ValueStates(leadingZero);
    expect(x2EntryStateText(leadingZeroStates[4])).toBe("exponent:5:");
    expect(x2EntryStateText(leadingZeroStates[5])).toBe("exponent:5:3");
    const allZeroStates = computeX2ValueStates(allZero);
    expect(x2EntryStateText(allZeroStates[4])).toBe("exponent:00:");
    expect(x2EntryStateText(allZeroStates[5])).toBe("exponent:00:3");
    const zeroTailStates = computeX2ValueStates(zeroTail);
    expect(x2EntryStateText(zeroTailStates[4])).toBe("exponent:0.:");
    expect(x2EntryStateText(zeroTailStates[5])).toBe("exponent:0.:3");
    const openPointZeroTailStates = computeX2ValueStates(openPointZeroTail);
    expect(x2EntryStateText(openPointZeroTailStates[4])).toBe("exponent:0.:");
    expect(x2EntryStateText(openPointZeroTailStates[5])).toBe("exponent:0.:3");
    const fractionalStates = computeX2ValueStates(fractional);
    expect(x2EntryStateText(fractionalStates[5])).toBe("exponent:0.2:");
    expect(x2EntryStateText(fractionalStates[6])).toBe("exponent:0.2:3");
    const negativeStates = computeX2ValueStates(negative);
    expect(x2EntryStateText(negativeStates[4])).toBe("exponent:-9:");
    expect(x2EntryStateText(negativeStates[5])).toBe("exponent:-9:3");
    const negativeFractionalStates = computeX2ValueStates(negativeFractional);
    expect(x2EntryStateText(negativeFractionalStates[6])).toBe("exponent:-9.2:");
    expect(x2EntryStateText(negativeFractionalStates[7])).toBe("exponent:-9.2:3");
    const negativeZeroStates = computeX2ValueStates(negativeZero);
    expect(x2EntryStateText(negativeZeroStates[4])).toBe("exponent:-1:");
    expect(x2EntryStateText(negativeZeroStates[5])).toBe("exponent:-1:3");
  });

  it("x2 value dataflow models closed sign after direct store from the original X2 mantissa", () => {
    const afterSyncedStore: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const zeroTail: IrOp[] = [
      plain(0x01, "1"),
      plain(0x00, "0"),
      store("1"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const leadingZero: IrOp[] = [
      plain(0x00, "0"),
      plain(0x05, "5"),
      store("1"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const allZero: IrOp[] = [
      plain(0x00, "0"),
      plain(0x00, "0"),
      store("1"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const negative: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      store("1"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const doubleSign: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const throughEmpty: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];

    const afterSyncedStoreStates = computeX2ValueStates(afterSyncedStore);
    expect(x2EntryStateText(afterSyncedStoreStates[5])).toBe("exponent:-2:");
    expect(x2EntryStateText(afterSyncedStoreStates[6])).toBe("exponent:-2:3");
    const zeroTailStates = computeX2ValueStates(zeroTail);
    expect(x2EntryStateText(zeroTailStates[5])).toBe("exponent:-10:");
    expect(x2EntryStateText(zeroTailStates[6])).toBe("exponent:-10:3");
    const leadingZeroStates = computeX2ValueStates(leadingZero);
    expect(x2EntryStateText(leadingZeroStates[5])).toBe("exponent:-05:");
    expect(x2EntryStateText(leadingZeroStates[6])).toBe("exponent:-05:3");
    const allZeroStates = computeX2ValueStates(allZero);
    expect(x2EntryStateText(allZeroStates[5])).toBe("exponent:-0:");
    expect(x2EntryStateText(allZeroStates[6])).toBe("exponent:-0:3");
    const negativeStates = computeX2ValueStates(negative);
    expect(x2EntryStateText(negativeStates[5])).toBe("exponent:2:");
    expect(x2EntryStateText(negativeStates[6])).toBe("exponent:2:3");
    const doubleSignStates = computeX2ValueStates(doubleSign);
    expect(x2EntryStateText(doubleSignStates[6])).toBe("exponent:2:");
    expect(x2EntryStateText(doubleSignStates[7])).toBe("exponent:2:3");
    const throughEmptyStates = computeX2ValueStates(throughEmpty);
    expect(x2EntryStateText(throughEmptyStates[6])).toBe("exponent:-2:");
    expect(x2EntryStateText(throughEmptyStates[7])).toBe("exponent:-2:3");
  });

  it("x2 value dataflow models structural store-to-ВП splice shape-only", () => {
    const directStore: IrOp[] = [
      recall("2", "preload const FACE"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const indirectStore: IrOp[] = [
      recall("2", "preload const FACE"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];

    const directStates = computeX2ValueStates(directStore, { trackRegisterMemory: true });
    expect(x2ShapeStateText(directStates[2]?.x2Shape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2VpEntryShapeText(directStates[2])).toEqual(["hex:ACE:mantissa"]);
    expect(directStates[2]?.vpEntryShapeTransient).toBe(true);
    expect(x2ShapeStateText(directStates[3]?.xShape)).toEqual(["hex-exponent:ACE:"]);
    expect(x2ShapeStateText(directStates[4]?.xShape)).toEqual(["hex-exponent:ACE:3"]);
    expect(x2ValueStateText(directStates[4]?.x)).toEqual([]);
    const indirectStates = computeX2ValueStates(indirectStore, { trackRegisterMemory: true });
    expect(x2ShapeStateText(indirectStates[2]?.x2Shape)).toEqual(["hex:FACE:mantissa"]);
    expect(x2VpEntryShapeText(indirectStates[2])).toEqual(["hex:ACE:mantissa"]);
    expect(indirectStates[2]?.vpEntryShapeTransient).toBe(true);
    expect(x2ShapeStateText(indirectStates[3]?.xShape)).toEqual(["hex-exponent:ACE:"]);
    expect(x2ShapeStateText(indirectStates[4]?.xShape)).toEqual(["hex-exponent:ACE:3"]);
    expect(x2ValueStateText(indirectStates[4]?.x)).toEqual([]);

    const throughEmpty: IrOp[] = [
      recall("2", "preload const FACE"),
      store("1"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const emptyStates = computeX2ValueStates(throughEmpty, { trackRegisterMemory: true });
    expect(x2VpEntryShapeText(emptyStates[2])).toEqual(["hex:ACE:mantissa"]);
    expect(emptyStates[2]?.vpEntryShapeTransient).toBe(true);
    expect(x2VpEntryShapeText(emptyStates[3])).toEqual(["hex:FACE:mantissa"]);
    expect(emptyStates[3]?.vpEntryShapeTransient).toBeUndefined();
    expect(x2ShapeStateText(emptyStates[4]?.xShape)).toEqual(["hex-exponent:FACE:"]);
    expect(x2ShapeStateText(emptyStates[5]?.xShape)).toEqual(["hex-exponent:FACE:3"]);
  });

  it("x2 value dataflow models structural exponent store-to-ВП splice through restored shape", () => {
    const directStore: IrOp[] = [
      recall("2", "preload const ГE2"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const indirectStore: IrOp[] = [
      recall("2", "preload const ГE2"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const throughEmpty: IrOp[] = [
      recall("2", "preload const ГE2"),
      store("1"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];

    const directStates = computeX2ValueStates(directStore, { trackRegisterMemory: true });
    expect(x2ShapeStateText(directStates[1]?.x2Shape)).toEqual(["hex-exponent:Г:2"]);
    expect(x2ShapeStateText(directStates[2]?.x2Shape)).toEqual(["hex-exponent:Г:2"]);
    expect(x2VpEntryShapeText(directStates[2])).toEqual(["hex:00:mantissa"]);
    expect(directStates[2]?.vpEntryShapeTransient).toBe(true);
    expect(x2ShapeStateText(directStates[3]?.xShape)).toEqual(["hex-exponent:00:"]);
    expect(x2ShapeStateText(directStates[4]?.xShape)).toEqual(["hex-exponent:00:3"]);

    const indirectStates = computeX2ValueStates(indirectStore, { trackRegisterMemory: true });
    expect(x2ShapeStateText(indirectStates[2]?.x2Shape)).toEqual(["hex-exponent:Г:2"]);
    expect(x2VpEntryShapeText(indirectStates[2])).toEqual(["hex:00:mantissa"]);
    expect(indirectStates[2]?.vpEntryShapeTransient).toBe(true);
    expect(x2ShapeStateText(indirectStates[3]?.xShape)).toEqual(["hex-exponent:00:"]);
    expect(x2ShapeStateText(indirectStates[4]?.xShape)).toEqual(["hex-exponent:00:3"]);

    const emptyStates = computeX2ValueStates(throughEmpty, { trackRegisterMemory: true });
    expect(x2VpEntryShapeText(emptyStates[2])).toEqual(["hex:00:mantissa"]);
    expect(emptyStates[2]?.vpEntryShapeTransient).toBe(true);
    expect(x2VpEntryShapeText(emptyStates[3])).toEqual(["hex:Г00:mantissa"]);
    expect(emptyStates[3]?.vpEntryShapeTransient).toBeUndefined();
    expect(x2ShapeStateText(emptyStates[4]?.xShape)).toEqual(["hex-exponent:Г00:"]);
    expect(x2ShapeStateText(emptyStates[5]?.xShape)).toEqual(["hex-exponent:Г00:3"]);
  });

  it("x2 value dataflow models indirect store as a decimal ВП splice source", () => {
    const knownIndirect: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const unknownIndirect: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      indirectStore("7"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const leadingZero: IrOp[] = [
      plain(0x00, "0"),
      plain(0x05, "5"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const negative: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const negativeZero: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0b, "/-/"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];

    const knownIndirectStates = computeX2ValueStates(knownIndirect, { trackRegisterMemory: true });
    expect(x2EntryStateText(knownIndirectStates[4])).toBe("exponent:0.:");
    expect(x2EntryStateText(knownIndirectStates[5])).toBe("exponent:0.:3");
    const unknownIndirectStates = computeX2ValueStates(unknownIndirect, { trackRegisterMemory: true });
    expect(x2EntryStateText(unknownIndirectStates[4])).toBe("exponent:0.:");
    expect(x2EntryStateText(unknownIndirectStates[5])).toBe("exponent:0.:3");
    const leadingZeroStates = computeX2ValueStates(leadingZero, { trackRegisterMemory: true });
    expect(x2EntryStateText(leadingZeroStates[4])).toBe("exponent:5:");
    expect(x2EntryStateText(leadingZeroStates[5])).toBe("exponent:5:3");
    const negativeStates = computeX2ValueStates(negative, { trackRegisterMemory: true });
    expect(x2EntryStateText(negativeStates[4])).toBe("exponent:-9:");
    expect(x2EntryStateText(negativeStates[5])).toBe("exponent:-9:3");
    const negativeZeroStates = computeX2ValueStates(negativeZero, { trackRegisterMemory: true });
    expect(x2EntryStateText(negativeZeroStates[4])).toBe("exponent:-1:");
    expect(x2EntryStateText(negativeZeroStates[5])).toBe("exponent:-1:3");
  });

  it("x2 value dataflow models closed sign after indirect store from the original X2 mantissa", () => {
    const knownIndirect: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const unknownIndirect: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      indirectStore("7"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const negative: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];

    const knownIndirectStates = computeX2ValueStates(knownIndirect, { trackRegisterMemory: true });
    expect(x2EntryStateText(knownIndirectStates[5])).toBe("exponent:-2:");
    expect(x2EntryStateText(knownIndirectStates[6])).toBe("exponent:-2:3");
    const unknownIndirectStates = computeX2ValueStates(unknownIndirect, { trackRegisterMemory: true });
    expect(x2EntryStateText(unknownIndirectStates[5])).toBe("exponent:-2:");
    expect(x2EntryStateText(unknownIndirectStates[6])).toBe("exponent:-2:3");
    const negativeStates = computeX2ValueStates(negative, { trackRegisterMemory: true });
    expect(x2EntryStateText(negativeStates[5])).toBe("exponent:2:");
    expect(x2EntryStateText(negativeStates[6])).toBe("exponent:2:3");
  });

  it("x2 value dataflow keeps active leading-zero exponent-entry X2 structural-only", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[4])).toBe("exponent:05:3");
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:5000:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual([]);
  });

  it("x2 value dataflow records active exponent-entry shape without a dot-safe X2 value fact", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual([
      "exponent:05:3:decimal",
      "mantissa:5000:decimal",
    ]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual([]);
  });

  it("x2 value dataflow records safe exponent-entry shape and normalized mantissa shape", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ShapeStateText(states[3]?.x2Shape)).toEqual([
      "exponent:5:3:decimal",
      "mantissa:5000:decimal",
    ]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:5000:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual([]);
  });

  it("x2 shape dataflow normalizes visible shape through dot while preserving hidden raw shape", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["mantissa:2:decimal"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["mantissa:02:decimal"]);
  });

  it("x2 value dataflow models closed sign-change from normalized X and raw X2 shape", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[5]?.x)).toEqual(["decimal:-2:normalized"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["decimal:-02:unnormalized"]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["mantissa:-2:decimal"]);
    expect(x2ShapeStateText(states[5]?.x2Shape)).toEqual(["mantissa:-02:decimal"]);
  });

  it("x2 value dataflow models closed sign-change from exact decimal display-shape equality", () => {
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      entry: { kind: "closed" },
    };
    const next = transferX2ValueStateForEdge(state, plain(0x0b, "/-/"), "normal", {}, 0);

    expect(x2ValueStateText(next?.x)).toEqual(["decimal:-100:normalized"]);
    expect(x2ValueStateText(next?.x2)).toEqual(["decimal:-100:normalized"]);
    expect(x2ShapeStateText(next?.xShape)).toEqual(["mantissa:-100:decimal"]);
    expect(x2ShapeStateText(next?.x2Shape)).toEqual(["mantissa:-100:decimal"]);
  });

  it("x2 value dataflow keeps sign-change in exponent-entry state", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[4])).toBe("exponent:12:-");
    expect(x2EntryStateText(states[5])).toBe("exponent:12:-3");
    expect(x2ValueStateText(states[5]?.x)).toEqual(["decimal:0.012:normalized"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual([]);
  });

  it("x2 value dataflow preserves VP exponent context through X2-preserving gaps", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[5])).toBe("closed");
    expect(x2VpContextStateText(states[5])).toBe("exponent:12:3");
    expect(x2EntryStateText(states[6])).toBe("closed");
    expect(x2VpContextStateText(states[6])).toBe("exponent:12:3");
    expect(x2EntryStateText(states[7])).toBe("closed");
    expect(x2VpContextStateText(states[7])).toBe("exponent:12:-3");
  });

  it("x2 VP shape context analysis classifies active entries and closed VP contexts for splicing", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(analyzeX2VpShapeContext(states[3])).toMatchObject({
      kind: "active-exponent",
      phase: "active-entry",
      source: "decimal",
      hasExponentDigit: false,
      restoresX2: true,
      canCancelExponentSignPair: true,
      canDiscardSeparatorBeforeNonDigit: false,
      canDiscardSeparatorBeforeSignChange: false,
      canDiscardRestoreBeforeFreshDigit: false,
    });
    expect(analyzeX2VpShapeContext(states[4])).toMatchObject({
      kind: "active-exponent",
      phase: "active-entry",
      source: "decimal",
      hasExponentDigit: true,
      canDiscardSeparatorBeforeNonDigit: true,
      canCancelExponentSignPair: true,
    });
    expect(analyzeX2VpShapeContext(states[6])).toMatchObject({
      kind: "vp-exponent-context",
      phase: "vp-context",
      source: "decimal",
      hasExponentDigit: true,
      restoresX2: true,
      canDiscardSeparatorBeforeNonDigit: false,
      canDiscardSeparatorBeforeSignChange: true,
      canDiscardRestoreBeforeFreshDigit: true,
      canCancelExponentSignPair: false,
    });
  });

  it("x2 value dataflow proves a closed integer exponent-entry after an X2 sync", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[4])).toBe("closed");
    expect(x2VpContextStateText(states[4])).toBe("none");
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:5000:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:5000:normalized"]);
  });

  it("x2 value dataflow proves a closed leading-zero exponent-entry after an X2 sync", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[5])).toBe("closed");
    expect(x2VpContextStateText(states[5])).toBe("none");
    expect(x2ValueStateText(states[5]?.x)).toEqual(["decimal:5000:normalized"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["decimal:5000:normalized"]);
  });

  it("x2 value dataflow preserves decimal exponent shapes through closed sign-change", () => {
    const plainExponent: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const leadingZeroExponent: IrOp[] = [
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const plainStates = computeX2ValueStates(plainExponent);
    const leadingZeroStates = computeX2ValueStates(leadingZeroExponent);

    expect(x2ValueStateText(plainStates[5]?.x)).toEqual(["decimal:-5000:normalized"]);
    expect(x2ValueStateText(plainStates[5]?.x2)).toEqual(["decimal:-5000:normalized"]);
    expect(x2ShapeStateText(plainStates[5]?.x2Shape)).toEqual([
      "exponent:-5:3:decimal",
      "mantissa:-5000:decimal",
    ]);
    expect(x2ValueStateText(leadingZeroStates[6]?.x)).toEqual(["decimal:-5000:normalized"]);
    expect(x2ValueStateText(leadingZeroStates[6]?.x2)).toEqual(["decimal:-5000:normalized"]);
    expect(x2ShapeStateText(leadingZeroStates[6]?.x2Shape)).toEqual([
      "exponent:-05:3:decimal",
      "mantissa:-5000:decimal",
    ]);
  });

  it("x2 value dataflow keeps active all-zero exponent-entry X2 structural-only", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x00, "0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[4])).toBe("exponent:00:3");
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:10000:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual([]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual([
      "exponent:00:3:decimal",
      "mantissa:10000:decimal",
    ]);
  });

  it("x2 value dataflow proves a closed fractional exponent-entry after an X2 sync", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[5])).toBe("closed");
    expect(x2VpContextStateText(states[5])).toBe("none");
    expect(x2ValueStateText(states[5]?.x)).toEqual(["decimal:0.005:normalized"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["decimal:0.005:normalized"]);
  });

  it("x2 value dataflow proves a closed signed-mantissa exponent-entry after an X2 sync", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[5])).toBe("closed");
    expect(x2VpContextStateText(states[5])).toBe("none");
    expect(x2ValueStateText(states[5]?.x)).toEqual(["decimal:-5000:normalized"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["decimal:-5000:normalized"]);
  });

  it("x2 value dataflow proves a closed signed fractional exponent-entry after an X2 sync", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[6])).toBe("closed");
    expect(x2VpContextStateText(states[6])).toBe("none");
    expect(x2ValueStateText(states[6]?.x)).toEqual(["decimal:-0.005:normalized"]);
    expect(x2ValueStateText(states[6]?.x2)).toEqual(["decimal:-0.005:normalized"]);
  });

  it("x2 value dataflow materializes signed decimal VP-context restores", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[5])).toBe("closed");
    expect(x2VpContextStateText(states[5])).toBe("exponent:5:-2");
    expect(x2ValueStateText(states[5]?.x)).toEqual(["decimal:0.05:normalized"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["decimal:0.05:normalized"]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["exponent:5:-2:decimal"]);
    expect(x2ShapeStateText(states[5]?.x2Shape)).toEqual(["exponent:5:-2:decimal"]);
  });

  it("x2 value dataflow re-enters signed decimal VP context through first-digit splice", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[8])).toBe("exponent:3:-2");
    expect(x2VpContextStateText(states[8])).toBe("exponent:3:-2");
    expect(x2ValueStateText(states[8]?.x)).toEqual(["decimal:0.03:normalized"]);
    expect(x2ShapeStateText(states[8]?.x2Shape)).toEqual(["exponent:3:-2:decimal"]);
  });

  it("x2 value dataflow materializes signed structural VP-context restores", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[5])).toBe("closed");
    expect(x2StructuralVpContextStateText(states[5])).toBe("exponent:hex:Г:mantissa:-2");
    expect(x2ValueStateText(states[5]?.x)).toEqual([]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual([]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex-exponent:Г:-2"]);
    expect(x2ShapeStateText(states[5]?.x2Shape)).toEqual(["hex-exponent:Г:-2"]);
  });

  it("x2 value dataflow re-enters signed structural VP context through first-digit splice", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[8])).toBe("closed");
    expect(x2StructuralVpContextStateText(states[8])).toBe("exponent:hex:3.0Г:mantissa:-2");
    expect(x2ShapeStateText(states[8]?.xShape)).toEqual(["hex-exponent:3.0Г:-2"]);
    expect(x2ShapeStateText(states[8]?.x2Shape)).toEqual(["hex-exponent:3.0Г:-2"]);
  });

  it("x2 value dataflow uses fractional decimal recalls as VP-entry source", () => {
    const program: IrOp[] = [
      recall("2", "preload const 1.2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2EntryStateText(states[2])).toBe("exponent:1.2:");
    expect(x2EntryStateText(states[3])).toBe("exponent:1.2:3");
    expect(x2ValueStateText(states[3]?.x)).toEqual(["decimal:1200:normalized"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual([]);
    expect(x2ShapeStateText(states[3]?.x2Shape)).toEqual([
      "exponent:1.2:3:decimal",
      "mantissa:1200:decimal",
    ]);
  });

  it("x2 value dataflow carries fractional VP-entry source through an X2 sync", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:1.2:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:1.2:normalized"]);
    expect(x2EntryStateText(states[5])).toBe("exponent:1.2:");
    expect(x2EntryStateText(states[6])).toBe("exponent:1.2:3");
    expect(x2ValueStateText(states[6]?.x)).toEqual(["decimal:1200:normalized"]);
    expect(x2ValueStateText(states[6]?.x2)).toEqual([]);
    expect(x2ShapeStateText(states[6]?.x2Shape)).toEqual([
      "exponent:1.2:3:decimal",
      "mantissa:1200:decimal",
    ]);
  });

  it("x2 value dataflow carries scientific decimal VP-entry sources through dot restore", () => {
    const wideProgram: IrOp[] = [
      plain(0x08, "8"),
      plain(0x15, "F 10^x"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ];
    const smallProgram: IrOp[] = [
      plain(0x08, "8"),
      plain(0x0b, "/-/"),
      plain(0x15, "F 10^x"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ];
    const wideStates = computeX2ValueStates(wideProgram);
    const smallStates = computeX2ValueStates(smallProgram);

    expect(x2EntryStateText(wideStates[5])).toBe("exponent:100000000:");
    expect(x2EntryStateText(wideStates[6])).toBe("exponent:100000000:2");
    expect(x2ValueStateText(wideStates[6]?.x)).toEqual(["decimal:10000000000:normalized"]);
    expect(x2ValueStateText(wideStates[6]?.x2)).toEqual([]);
    expect(x2ShapeStateText(wideStates[6]?.x2Shape)).toEqual([
      "exponent:100000000:2:decimal",
      "exponent:1:10:decimal",
    ]);
    expect(x2EntryStateText(smallStates[6])).toBe("exponent:0.00000001:");
    expect(x2EntryStateText(smallStates[7])).toBe("exponent:0.00000001:2");
    expect(x2ValueStateText(smallStates[7]?.x)).toEqual(["decimal:0.000001:normalized"]);
    expect(x2ValueStateText(smallStates[7]?.x2)).toEqual([]);
    expect(x2ShapeStateText(smallStates[7]?.x2Shape)).toEqual([
      "exponent:0.00000001:2:decimal",
      "exponent:1:-6:decimal",
    ]);
  });

  it("x2 value dataflow proves a closed fractional-mantissa exponent-entry after an X2 sync", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[7])).toBe("closed");
    expect(x2VpContextStateText(states[7])).toBe("none");
    expect(x2ValueStateText(states[7]?.x)).toEqual(["decimal:1200:normalized"]);
    expect(x2ValueStateText(states[7]?.x2)).toEqual(["decimal:1200:normalized"]);
    expect(x2ShapeStateText(states[7]?.x2Shape)).toEqual([
      "exponent:1.2:3:decimal",
      "mantissa:1200:decimal",
    ]);
  });

  it("x2 value dataflow clears VP exponent context when a new digit starts", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x04, "4"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[5])).toBe("closed");
    expect(x2VpContextStateText(states[5])).toBe("exponent:12:3");
    expect(x2EntryStateText(states[6])).toBe("open:4");
    expect(x2VpContextStateText(states[6])).toBe("none");
  });

  it("x2 value dataflow recalls concrete decimal facts stored in registers", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[5]?.x)).toEqual(["decimal:2:normalized", "reg:1"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["decimal:2:normalized", "reg:1"]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["mantissa:2:decimal"]);
    expect(x2ShapeStateText(states[5]?.x2Shape)).toEqual(["mantissa:2:decimal"]);
  });

  it("x2 value dataflow recalls arbitrary expr facts stored in registers", () => {
    const program: IrOp[] = [
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[3]?.x)).toEqual(["expr:0", "reg:1"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["expr:0", "reg:1"]);
    expect(x2ValueStateText(states[5]?.x)).toEqual(["expr:0", "reg:1"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["expr:0", "reg:1"]);
  });

  it("recall value proof treats stored expr facts as value syncs", () => {
    const program: IrOp[] = [
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      store("1"),
      plain(0x54, "К НОП"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(recallValueProof(recall("1"), states[4])).toEqual({
      register: "1",
      inX: true,
      x2SyncRegister: "1",
      x2SyncValue: true,
    });
  });

  it("recall value proof uses structural shape memory as in-X evidence", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      recall("3", "preload const FACE"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(recallValueProof(recall("2"), states[3])).toEqual({
      register: "2",
      inX: true,
      x2SyncRegister: undefined,
      x2SyncValue: false,
      x2SyncShape: true,
    });
  });

  it("recall value proof uses stable expr-key structural shapes as X and X2 evidence", () => {
    const state: X2ValueDataflowState = {
      x: new Set<X2ValueFact>(["expr-key:31(shape:hex:-A:mantissa)"]),
      x2: new Set<X2ValueFact>(["expr-key:31(shape:hex:-A:mantissa)"]),
      entry: { kind: "closed" },
    };

    expect(recallValueProof(recall("2", "preload const A"), state)).toEqual({
      register: "2",
      inX: true,
      x2SyncRegister: undefined,
      x2SyncValue: false,
      x2SyncShape: true,
    });
  });

  it("recall value proof uses stored structural exponent shapes as in-X evidence", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      store("2"),
      recall("3", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex-exponent:FACE:3"]);
    expect(x2ShapeStateText(states[9]?.xShape)).toEqual(["hex-exponent:FACE:3"]);
    expect(recallValueProof(recall("2"), states[9])).toEqual({
      register: "2",
      inX: true,
      x2SyncRegister: undefined,
      x2SyncValue: false,
      x2SyncShape: true,
    });
  });

  it("recall value proof matches closed structural exponent shifts to mantissa-shaped preloads", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      recall("2", "preload const Г00"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });
    const result = flowXReuse.run(program, ctx);

    expect(x2ShapeStateText(states[3]?.xShape)).toEqual(["hex-exponent:Г:2"]);
    expect(recallValueProof(recall("2", "preload const Г00"), states[3])).toEqual({
      register: "2",
      inX: true,
      x2SyncRegister: undefined,
      x2SyncValue: false,
      x2SyncShape: true,
    });
    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ]);
  });

  it("recall value proof matches negative structural exponent shifts to mantissa-shaped preloads", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x02, "2"),
      recall("2", "preload const 0.0Г"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });
    const result = flowXReuse.run(program, ctx);

    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex-exponent:Г:-2"]);
    expect(recallValueProof(recall("2", "preload const 0.0Г"), states[4])).toEqual({
      register: "2",
      inX: true,
      x2SyncRegister: undefined,
      x2SyncValue: false,
      x2SyncShape: true,
    });
    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x02, "2"),
      halt(),
    ]);
  });

  it("recall value proof matches preloaded structural exponent notation", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x02, "2"),
      recall("2", "preload const ГE-2"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });
    const result = flowXReuse.run(program, ctx);

    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex-exponent:Г:-2"]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex-exponent:Г:-2"]);
    expect(recallValueProof(recall("2", "preload const ГE-2"), states[4])).toEqual({
      register: "2",
      inX: true,
      x2SyncRegister: undefined,
      x2SyncValue: false,
      x2SyncShape: true,
    });
    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x02, "2"),
      halt(),
    ]);
  });

  it("recall value proof uses stored signed structural exponent shapes after closed sign-change", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      store("2"),
      recall("3", "preload const -FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex-exponent:-FACE:3"]);
    expect(x2ShapeStateText(states[10]?.xShape)).toEqual(["hex-exponent:-FACE:3"]);
    expect(recallValueProof(recall("2"), states[10])).toEqual({
      register: "2",
      inX: true,
      x2SyncRegister: undefined,
      x2SyncValue: false,
      x2SyncShape: true,
    });
  });

  it("recall value proof uses decimal display shape memory as in-X evidence only", () => {
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["exponent:1:8:decimal"]),
      x2Shape: new Set(),
      entry: { kind: "closed" },
      memory: {},
      shapeMemory: {
        "2": new Set<X2ShapeFact>(["exponent:1:8:decimal"]),
      },
    };

    expect(recallValueProof(recall("2"), state)).toEqual({
      register: "2",
      inX: true,
      x2SyncRegister: undefined,
      x2SyncValue: false,
    });
  });

  it("recall value proof uses decimal display shape memory as an X2 value sync after a proved sync", () => {
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set<X2ValueFact>(["decimal:100000000:normalized"]),
      xShape: new Set<X2ShapeFact>(["exponent:1:8:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:1:8:decimal"]),
      entry: { kind: "closed" },
      memory: {},
      shapeMemory: {
        "2": new Set<X2ShapeFact>(["exponent:1:8:decimal"]),
      },
    };

    expect(recallValueProof(recall("2"), state)).toEqual({
      register: "2",
      inX: true,
      x2SyncRegister: undefined,
      x2SyncValue: false,
      x2SyncDisplayValue: true,
      x2SyncVpShape: true,
    });
    expect(analyzeRecallRemoval([recall("2"), plain(0x54, "КНОП"), plain(0x0a, "."), halt()], 0, undefined, state))
      .toMatchObject({
        redundantSyncValue: false,
        redundantSyncDisplayValue: true,
        x2SyncRedundant: true,
        exposesX2Restore: false,
        removable: true,
      });
  });

  it("recall value proof uses dot-safe hidden X2 display shape as a restored-visible sync", () => {
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      entry: { kind: "closed" },
      memory: {},
      shapeMemory: {
        "2": new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      },
    };

    expect(recallValueProof(recall("2"), state)).toEqual({
      register: "2",
      inX: true,
      x2SyncRegister: undefined,
      x2SyncValue: false,
      x2SyncDisplayValue: true,
      x2SyncVpShape: true,
    });
    expect(analyzeRecallRemoval([recall("2"), plain(0x54, "КНОП"), plain(0x0a, "."), halt()], 0, undefined, state))
      .toMatchObject({
        redundantSyncValue: false,
        redundantSyncDisplayValue: true,
        x2SyncRedundant: true,
        exposesX2Restore: false,
        removable: true,
      });
    expect(analyzeRecallRemoval([recall("2"), plain(0x54, "КНОП"), plain(0x0b, "/-/"), halt()], 0, undefined, state))
      .toMatchObject({
        redundantSyncValue: false,
        redundantSyncDisplayValue: true,
        exposesX2Restore: true,
        removable: false,
      });
  });

  it("recall value proof uses mixed decimal value and exact display-shape in-X evidence", () => {
    const exactShapeState: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["exponent:5:-1:decimal"]),
      x2Shape: new Set(),
      entry: { kind: "closed" },
      memory: {},
      shapeMemory: {},
    };
    const rawShapeState: X2ValueDataflowState = {
      ...exactShapeState,
      xShape: new Set<X2ShapeFact>(["mantissa:0.5:decimal"]),
    };

    expect(recallValueProof(recall("2", "preload const 0.5"), exactShapeState)).toEqual({
      register: "2",
      inX: true,
      x2SyncRegister: undefined,
      x2SyncValue: false,
    });
    expect(recallValueProof(recall("2", "preload const 0.5"), rawShapeState)).toEqual({
      register: "2",
      inX: false,
      x2SyncRegister: undefined,
      x2SyncValue: false,
    });
  });

  it("recall value proof uses decimal display shape equality as a VP-only X2 sync", () => {
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      entry: { kind: "closed" },
      memory: {},
      shapeMemory: {
        "2": new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      },
    };

    expect(recallValueProof(recall("2"), state)).toEqual({
      register: "2",
      inX: true,
      x2SyncRegister: undefined,
      x2SyncValue: false,
      x2SyncVpShape: true,
    });
  });

  it("recall removal preserves immediate VP context through VP-only decimal shape sync", () => {
    const program: IrOp[] = [
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      entry: { kind: "closed" },
      vpEntryMantissa: new Set(["100"]),
      memory: {},
      shapeMemory: {
        "2": new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      },
    };

    expect(analyzeRecallRemoval(program, 0, undefined, state)).toMatchObject({
      redundantSyncValue: false,
      redundantSyncShape: false,
      x2SyncRedundant: false,
      exposesStackLift: false,
      exposesX2Restore: false,
      removable: true,
      valueProof: {
        register: "2",
        inX: true,
        x2SyncValue: false,
        x2SyncVpShape: true,
      },
    });
  });

  it("recall removal preserves delayed VP context through VP-only decimal shape sync", () => {
    const program: IrOp[] = [
      recall("2"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      entry: { kind: "closed" },
      memory: {},
      shapeMemory: {
        "2": new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      },
    };

    expect(analyzeRecallRemoval(program, 0, undefined, state)).toMatchObject({
      redundantSyncValue: false,
      redundantSyncShape: false,
      x2SyncRedundant: false,
      exposesStackLift: false,
      exposesX2Restore: false,
      removable: true,
      valueProof: {
        register: "2",
        inX: true,
        x2SyncValue: false,
        x2SyncVpShape: true,
      },
    });
  });

  it("recall removal keeps delayed dot and sign restores after VP-only decimal shape sync", () => {
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      entry: { kind: "closed" },
      memory: {},
      shapeMemory: {
        "2": new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      },
    };

    for (const restore of [plain(0x0a, "."), plain(0x0b, "/-/")]) {
      expect(analyzeRecallRemoval([recall("2"), plain(0x54, "КНОП"), restore, halt()], 0, undefined, state))
        .toMatchObject({
          redundantSyncValue: false,
          redundantSyncShape: false,
          x2SyncRedundant: false,
          exposesStackLift: false,
          exposesX2Restore: true,
          removable: false,
          valueProof: {
            register: "2",
            inX: true,
            x2SyncValue: false,
            x2SyncVpShape: true,
          },
        });
    }
  });

  it("recall removal preserves active mantissa VP context through display source keys", () => {
    const program: IrOp[] = [
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const state: X2ValueDataflowState = {
      x: new Set(),
      x2: new Set(),
      xShape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      x2Shape: new Set<X2ShapeFact>(["mantissa:100:decimal"]),
      entry: { kind: "open", raw: new Set(["100"]) },
      memory: {},
      shapeMemory: {
        "2": new Set<X2ShapeFact>(["exponent:100:0:decimal"]),
      },
    };

    expect(analyzeRecallRemoval(program, 0, undefined, state)).toMatchObject({
      redundantSyncValue: false,
      redundantSyncShape: false,
      x2SyncRedundant: false,
      exposesStackLift: false,
      exposesX2Restore: false,
      removable: true,
      valueProof: {
        register: "2",
        inX: true,
        x2SyncValue: false,
        x2SyncVpShape: true,
      },
    });
  });

  it("recall removal analysis treats structural X2 shape equality as a redundant sync", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      recall("3", "preload const FACE"),
      recall("2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const x2RegisterStates = computeX2RegisterStates(program);
    const x2ValueStates = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(analyzeRecallRemoval(program, 3, x2RegisterStates[3], x2ValueStates[3])).toMatchObject({
      register: "2",
      redundantSyncRegister: undefined,
      redundantSyncValue: false,
      redundantSyncShape: true,
      x2SyncRedundant: true,
      exposesStackLift: false,
      exposesX2Restore: false,
      removable: true,
    });
  });

  it("recall removal analysis accepts immediate ВП when the structural VP source is unchanged", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      plain(0x0d, "Cx"),
      recall("3", "preload const FACE"),
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const x2RegisterStates = computeX2RegisterStates(program);
    const x2ValueStates = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(analyzeRecallRemoval(program, 4, x2RegisterStates[4], x2ValueStates[4])).toMatchObject({
      register: "2",
      redundantSyncShape: true,
      x2SyncRedundant: true,
      exposesStackLift: false,
      exposesX2Restore: false,
      removable: true,
    });
  });

  it("recall removal analysis accepts immediate ВП when the decimal VP source is unchanged", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x0d, "Cx"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const x2RegisterStates = computeX2RegisterStates(program);
    const x2ValueStates = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(analyzeRecallRemoval(program, 6, x2RegisterStates[6], x2ValueStates[6])).toMatchObject({
      register: "2",
      redundantSyncValue: true,
      x2SyncRedundant: true,
      exposesStackLift: false,
      exposesX2Restore: false,
      removable: true,
    });
  });

  it("recall removal analysis accepts sign ВП when the decimal sign source is unchanged", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      recall("2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const x2RegisterStates = computeX2RegisterStates(program);
    const x2ValueStates = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(analyzeRecallRemoval(program, 3, x2RegisterStates[3], x2ValueStates[3])).toMatchObject({
      register: "2",
      redundantSyncValue: true,
      x2SyncRedundant: true,
      exposesStackLift: false,
      exposesX2Restore: false,
      removable: true,
    });
  });

  it("recall removal analysis accepts sign ВП when the structural sign source is unchanged", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      recall("2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const x2RegisterStates = computeX2RegisterStates(program);
    const x2ValueStates = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(analyzeRecallRemoval(program, 2, x2RegisterStates[2], x2ValueStates[2])).toMatchObject({
      register: "2",
      redundantSyncShape: true,
      x2SyncRedundant: true,
      exposesStackLift: false,
      exposesX2Restore: false,
      removable: true,
    });
  });

  it("recall removal analysis accepts sign ВП through transparent return helpers", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      recall("2"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const x2RegisterStates = computeX2RegisterStates(program);
    const x2ValueStates = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(
      analyzeRecallRemoval(
        program,
        8,
        x2RegisterStates[8],
        x2ValueStates[8],
        directReturnAnalysisContext(program),
      ),
    ).toMatchObject({
      register: "2",
      redundantSyncValue: true,
      x2SyncRedundant: true,
      exposesStackLift: false,
      exposesX2Restore: false,
      removable: true,
    });
  });

  it("recall removal analysis accepts stable indirect immediate ВП when the decimal VP source is unchanged", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      knownTargetIndirectStore("8", "2"),
      plain(0x0d, "Cx"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      knownTargetIndirectRecall("8", "2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const x2RegisterStates = computeX2RegisterStates(program);
    const x2ValueStates = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(analyzeRecallRemoval(program, 6, x2RegisterStates[6], x2ValueStates[6])).toMatchObject({
      register: "2",
      redundantSyncValue: true,
      x2SyncRedundant: true,
      exposesStackLift: false,
      exposesX2Restore: false,
      removable: true,
    });
  });

  it("recall removal analysis keeps immediate ВП when a store reset the decimal VP source", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x0d, "Cx"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const x2RegisterStates = computeX2RegisterStates(program);
    const x2ValueStates = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(analyzeRecallRemoval(program, 7, x2RegisterStates[7], x2ValueStates[7])).toMatchObject({
      register: "2",
      redundantSyncValue: true,
      x2SyncRedundant: true,
      exposesStackLift: false,
      exposesX2Restore: true,
      removable: false,
    });
  });

  it("recall removal analysis keeps immediate ВП when a store reset the structural VP source", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      plain(0x0d, "Cx"),
      recall("3", "preload const FACE"),
      store("4"),
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const x2RegisterStates = computeX2RegisterStates(program);
    const x2ValueStates = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(analyzeRecallRemoval(program, 5, x2RegisterStates[5], x2ValueStates[5])).toMatchObject({
      register: "2",
      redundantSyncShape: true,
      x2SyncRedundant: true,
      exposesStackLift: false,
      exposesX2Restore: true,
      removable: false,
    });
  });

  it("recall removal analysis combines stack-lift and X2 value-sync safety", () => {
    const program: IrOp[] = [
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      store("1"),
      plain(0x54, "К НОП"),
      recall("1"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const x2RegisterStates = computeX2RegisterStates(program);
    const x2ValueStates = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(analyzeRecallRemoval(program, 4, x2RegisterStates[4], x2ValueStates[4])).toMatchObject({
      register: "1",
      redundantSyncRegister: "1",
      redundantSyncValue: true,
      x2SyncRedundant: true,
      exposesStackLift: false,
      exposesX2Restore: false,
      removable: true,
    });
  });

  it("x2 value dataflow recalls concrete decimal facts through proved indirect memory", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      knownTargetIndirectRecall("7", "1"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:2:normalized", "reg:1"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:2:normalized", "reg:1"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["mantissa:2:decimal"]);
    expect(x2ShapeStateText(states[4]?.x2Shape)).toEqual(["mantissa:2:decimal"]);
  });

  it("x2 value dataflow forgets register memory on unknown indirect stores", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      store("1"),
      plain(0x03, "3"),
      indirectStore("7"),
      recall("1"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[5]?.x)).toEqual(["reg:1"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["reg:1"]);
  });

  it("x2 value dataflow intersects register memory at joins", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      cjump("store_path"),
      jump("join"),
      label("store_path"),
      plain(0x02, "2"),
      store("1"),
      label("join"),
      recall("1"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[8]?.x)).toEqual(["reg:1"]);
    expect(x2ValueStateText(states[8]?.x2)).toEqual(["reg:1"]);
    expect(x2ShapeStateText(states[8]?.xShape)).toEqual([]);
    expect(x2ShapeStateText(states[8]?.x2Shape)).toEqual([]);
  });

  it("x2 value dataflow refuses overlong exponent-entry state", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x03, "3"),
      plain(0x04, "4"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[5])).toBe("unknown");
    expect(x2ValueStateText(states[5]?.x)).toEqual([]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual([]);
  });

  it("x2-hidden-temp-restore turns closed exponent-entry scratch recalls into dot restores", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      store("2"),
      plain(0x35, "К {x}"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore turns opaque scratch recalls into dot restores", () => {
    const program: IrOp[] = [
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      store("2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[5]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore turns opaque sign-change scratch recalls into dot restores", () => {
    const program: IrOp[] = [
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      plain(0x0b, "/-/"),
      store("2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses pure-unary expr facts after an explicit X2 sync", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      store("2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses pure stack-consuming expr facts after an explicit X2 sync", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      store("2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[8]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses decimal register memory without an X2 register alias", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      recall("1"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "1")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses fractional decimal scratch values", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      store("1"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      recall("1"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "1")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses closed fractional exponent-entry scratch values", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      recall("1"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[10]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "1")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses raw leading-zero fractional scratch recalls when only visible X is observed", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      store("1"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      recall("1"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[7]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "1")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps raw leading-zero fractional scratch recalls before observable VP context", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      store("1"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      recall("1"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);

    expect(restored.applied).toBe(0);
    expect(restored.ops).toEqual(program);
  });

  it("x2-literal-restore replaces a repeated normalized digit run with dot", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated fractional digit run with dot", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 1.2 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated signed fractional digit run with dot", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -1.2 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated literal after a recalled decimal register", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated literal after a preloaded decimal recall", () => {
    const program: IrOp[] = [
      recall("2", "preload const 12"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("2", "preload const 12"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore keeps repeated literals after hex-like preloaded recalls", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore keeps numeric entry while structural VP context is active", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x03, "3"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });
    const result = x2LiteralRestore.run(program, ctx);

    expect(x2StructuralVpContextStateText(states[4])).toBe("exponent:hex:FACE:mantissa:3");
    expect(x2StateIsClosedPlainContext(states[4])).toBe(false);
    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore replaces a repeated leading-zero digit run with dot", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 02 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated signed leading-zero digit run with dot", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -02 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces leading-zero runs after X2 normalization when only visible X is observed", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 02 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces signed leading-zero runs after X2 normalization", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -02 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated digit run immediately after an X2 sync", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated signed digit run with dot", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -12 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a signed run after modeled sign-change through empty ops", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x55, "К 1"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x55, "К 1"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -12 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore keeps signed runs after role-bearing empty ops", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      { kind: "plain", opcode: 0x55, meta: { mnemonic: "К 1", roles: ["display-byte"] } },
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore replaces a signed digit run immediately after an X2 sync", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -12 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated signed single digit with dot", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -5 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated positive integer exponent literal with dot", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 5000 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated fractional-mantissa exponent literal with dot", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(4);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 1200 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore keeps repeated exponent literals while VP context is still observable", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore replaces repeated literals before a redundant context restore through a gap", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x54, "К НОП"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      plain(0x54, "К НОП"),
      plain(0x0b, "/-/"),
      halt(),
    ]);
  });

  it("x2-literal-restore keeps repeated literals before an immediate context restore", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore replaces a repeated fractional exponent literal with dot", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 0.005 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated signed fractional-mantissa exponent literal with dot", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(5);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -1200 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated fractional-mantissa negative-exponent literal with dot", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(5);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 0.0012 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated signed-mantissa exponent literal with dot", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -5000 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated signed fractional exponent literal with dot", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(4);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -0.005 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore replaces a repeated leading-zero exponent literal after it is closed", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 5000 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore keeps positive single digits because dot would not save a cell", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x05, "5"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore uses path-sensitive conditional fallthrough X2 sync", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      cjump("done"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      cjump("done"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("x2-literal-restore uses path-sensitive loop fallthrough X2 sync", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      loop("done"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      loop("done"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("x2-literal-restore uses proved indirect conditional preserved normalized X2", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      knownTargetIndirectCjump("7", 6),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      knownTargetIndirectCjump("7", 6),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("x2-literal-restore uses visible leading-zero literals through proved indirect conditionals", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      knownTargetIndirectCjump("7", 6),
      plain(0x00, "0"),
      plain(0x02, "2"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      knownTargetIndirectCjump("7", 6),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 02 from hidden X2 temp" } },
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("x2-literal-restore uses direct return X2 sync", () => {
    const program: IrOp[] = [
      label("main"),
      call("load"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
      label("load"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      ret(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      label("main"),
      call("load"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
      label("load"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      ret(),
    ]);
  });

  it("x2-literal-restore uses known indirect return X2 sync", () => {
    const program: IrOp[] = [
      label("main"),
      knownTargetIndirectCall("7", 4),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
      label("load"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      ret(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      label("main"),
      knownTargetIndirectCall("7", 4),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
      label("load"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      ret(),
    ]);
  });

  it("x2-literal-restore keeps conditional fallthrough literals whose stack lift is consumed", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      cjump("done"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore keeps a signed digit run when its stack lift is consumed", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore keeps its inserted X2-sensitive dot explicit", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    ];
    const restored = x2LiteralRestore.run(program, ctx);
    const nooped = x2NoopRestore.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(nooped.applied).toBe(0);
    expect(nooped.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore keeps a repeated digit run when its stack lift is consumed", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore uses a previous stack lift when X and Y already match", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      plain(0x10, "+"),
      halt(),
    ]);
  });

  it("x2-literal-restore keeps duplicate-Y literals when deeper stack state is consumed", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore replaces visible leading-zero digit runs through preserving gaps", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x20, "Fπ"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x20, "Fπ"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 02 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore keeps visible leading-zero digit runs before closed sign restores", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore replaces digit runs before immediate ВП context after X2-preserving ops", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("x2-literal-restore keeps fractional digit runs that would change a following ВП context", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore keeps digit runs that would change ВП context through preserving gaps", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore replaces normalized digit runs before empty-op ВП context", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-literal-restore replaces normalized digit runs before immediate ВП context", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-literal-restore replaces normalized digit runs before transparent return helpers and ВП", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x55, "К 1"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      plain(0x55, "К 1"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-literal-restore replaces normalized digit runs before proved indirect return helpers and ВП", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x55, "К 1"),
      knownTargetIndirectCall("7", 2),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      plain(0x55, "К 1"),
      knownTargetIndirectCall("7", 2),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-literal-restore keeps leading-zero digit runs before transparent return helpers and ВП", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore keeps leading-zero digit runs before proved indirect return helpers and ВП", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      knownTargetIndirectCall("7", 2),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore replaces signed digit runs before restore-run ВП context", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x54, "К НОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -12 from hidden X2 temp" } },
      plain(0x54, "К НОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-literal-restore keeps leading-zero digit runs before empty-op ВП context", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore keeps digit runs before role-bearing empty-op ВП context", () => {
    const roleEmpty: IrOp = {
      kind: "plain",
      opcode: 0x55,
      meta: { mnemonic: "К 1", roles: ["display-byte"] },
    };
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      roleEmpty,
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore keeps digit runs before role-bearing sign-change ВП context", () => {
    const roleSign: IrOp = {
      kind: "plain",
      opcode: 0x0b,
      meta: { mnemonic: "/-/", roles: ["display-byte"] },
    };
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      roleSign,
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore replaces exponent literals before empty-op ВП context", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 5000 from hidden X2 temp" } },
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ]);
  });

  it("x2-literal-restore replaces exponent literals before immediate ВП context", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 5000 from hidden X2 temp" } },
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ]);
  });

  it("x2-literal-restore replaces signed-mantissa exponent literals before empty-op ВП context", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -5000 from hidden X2 temp" } },
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ]);
  });

  it("x2-literal-restore replaces fractional exponent literals before empty-op ВП context", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 0.005 from hidden X2 temp" } },
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ]);
  });

  it("x2-literal-restore keeps leading-zero exponent literals before empty-op ВП context", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore replaces a repeated literal before a branch with no reachable X2 restore", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      cjump("done"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      cjump("done"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("x2-literal-restore uses concrete integer-part X2 facts from К [x]", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0a, "."),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0a, "."),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses concrete abs X2 facts from К |x|", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x31, "К |x|"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x31, "К |x|"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 1.2 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses concrete binary addition X2 facts", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x05, "5"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 15 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses concrete multiply and finite division X2 facts", () => {
    const multiplyProgram: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x06, "6"),
      plain(0x12, "*"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    ];
    const divisionProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x04, "4"),
      plain(0x13, "/"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x05, "5"),
      halt(),
    ];
    const multiplyResult = x2LiteralRestore.run(multiplyProgram, ctx);
    const divisionResult = x2LiteralRestore.run(divisionProgram, ctx);

    expect(multiplyResult.applied).toBe(1);
    expect(multiplyResult.ops).toEqual([
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x06, "6"),
      plain(0x12, "*"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
    ]);
    expect(divisionResult.applied).toBe(3);
    expect(divisionResult.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x04, "4"),
      plain(0x13, "/"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 0.25 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses emulator-pinned hex multiply facts after X2 sync", () => {
    const hexInYProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x02, "2"),
      plain(0x00, "0"),
      halt(),
    ];
    const hexInXProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x0e, "В↑"),
      recall("1", "preload const A"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x00, "0"),
      halt(),
    ];
    const hexBInYProgram: IrOp[] = [
      recall("1", "preload const B"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x04, "4"),
      halt(),
    ];
    const hexInYResult = x2LiteralRestore.run(hexInYProgram, ctx);
    const hexInXResult = x2LiteralRestore.run(hexInXProgram, ctx);
    const hexBInYResult = x2LiteralRestore.run(hexBInYProgram, ctx);

    expect(hexInYResult.applied).toBe(1);
    expect(hexInYResult.ops).toEqual([
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 20 from hidden X2 temp" } },
      halt(),
    ]);
    expect(hexInXResult.applied).toBe(2);
    expect(hexInXResult.ops).toEqual([
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x0e, "В↑"),
      recall("1", "preload const A"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 180 from hidden X2 temp" } },
      halt(),
    ]);
    expect(hexBInYResult.applied).toBe(1);
    expect(hexBInYResult.ops).toEqual([
      recall("1", "preload const B"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 54 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses structural hex sign facts after X2 sync", () => {
    const program: IrOp[] = [
      recall("1", "preload const -8F"),
      plain(0x32, "К ЗН"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const -8F"),
      plain(0x32, "К ЗН"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal -1 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses structural square facts after normalized X2 sync", () => {
    const bSquareProgram: IrOp[] = [
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x00, "0"),
      halt(),
    ];
    const aSquareProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x00, "0"),
      halt(),
    ];
    const bSquareResult = x2LiteralRestore.run(bSquareProgram, ctx);
    const aSquareResult = x2LiteralRestore.run(aSquareProgram, ctx);

    expect(bSquareResult.applied).toBe(1);
    expect(bSquareResult.ops).toEqual([
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 10 from hidden X2 temp" } },
      halt(),
    ]);
    expect(aSquareResult.applied).toBe(1);
    expect(aSquareResult.ops).toEqual([
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 00 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses super square zero facts after X2 sync", () => {
    const program: IrOp[] = [
      recall("1", "preload const FA"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("1", "preload const FA"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 0.0 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses emulator-pinned hex exponent multiply facts after X2 sync", () => {
    const decimalLeftProgram: IrOp[] = [
      recall("2", "preload const 1"),
      plain(0x0e, "В↑"),
      recall("1", "preload const ГE-2"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x01, "1"),
      halt(),
    ];
    const hexLeftProgram: IrOp[] = [
      recall("1", "preload const ГE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 5"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0x03, "3"),
      halt(),
    ];
    const extendedHexLeftProgram: IrOp[] = [
      recall("1", "preload const AE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 15"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x09, "9"),
      plain(0x0a, "."),
      plain(0x09, "9"),
      halt(),
    ];
    const extendedDecimalLeftProgram: IrOp[] = [
      recall("2", "preload const 17"),
      plain(0x0e, "В↑"),
      recall("1", "preload const CE-2"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x07, "7"),
      halt(),
    ];
    const decimalLeftResult = x2LiteralRestore.run(decimalLeftProgram, ctx);
    const hexLeftResult = x2LiteralRestore.run(hexLeftProgram, ctx);
    const extendedHexLeftResult = x2LiteralRestore.run(extendedHexLeftProgram, ctx);
    const extendedDecimalLeftResult = x2LiteralRestore.run(extendedDecimalLeftProgram, ctx);

    expect(decimalLeftResult.applied).toBe(2);
    expect(decimalLeftResult.ops).toEqual([
      recall("2", "preload const 1"),
      plain(0x0e, "В↑"),
      recall("1", "preload const ГE-2"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 0.1 from hidden X2 temp" } },
      halt(),
    ]);
    expect(hexLeftResult.applied).toBe(3);
    expect(hexLeftResult.ops).toEqual([
      recall("1", "preload const ГE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 5"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 0.53 from hidden X2 temp" } },
      halt(),
    ]);
    expect(extendedHexLeftResult.applied).toBe(2);
    expect(extendedHexLeftResult.ops).toEqual([
      recall("1", "preload const AE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 15"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 9.9 from hidden X2 temp" } },
      halt(),
    ]);
    expect(extendedDecimalLeftResult.applied).toBe(2);
    expect(extendedDecimalLeftResult.ops).toEqual([
      recall("2", "preload const 17"),
      plain(0x0e, "В↑"),
      recall("1", "preload const CE-2"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 1.7 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses concrete К max X2 facts", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x05, "5"),
      plain(0x36, "К max"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x05, "5"),
      plain(0x36, "К max"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses concrete unary arithmetic X2 facts", () => {
    const squareProgram: IrOp[] = [
      plain(0x04, "4"),
      plain(0x22, "F x^2"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x06, "6"),
      halt(),
    ];
    const reciprocalProgram: IrOp[] = [
      plain(0x04, "4"),
      plain(0x23, "F 1/x"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x05, "5"),
      halt(),
    ];
    const sqrtProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x04, "4"),
      plain(0x04, "4"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    ];
    const pow10Program: IrOp[] = [
      plain(0x03, "3"),
      plain(0x15, "F 10^x"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x00, "0"),
      plain(0x00, "0"),
      plain(0x00, "0"),
      halt(),
    ];
    const squareResult = x2LiteralRestore.run(squareProgram, ctx);
    const reciprocalResult = x2LiteralRestore.run(reciprocalProgram, ctx);
    const sqrtResult = x2LiteralRestore.run(sqrtProgram, ctx);
    const pow10Result = x2LiteralRestore.run(pow10Program, ctx);

    expect(squareResult.applied).toBe(1);
    expect(squareResult.ops).toEqual([
      plain(0x04, "4"),
      plain(0x22, "F x^2"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 16 from hidden X2 temp" } },
      halt(),
    ]);
    expect(reciprocalResult.applied).toBe(3);
    expect(reciprocalResult.ops).toEqual([
      plain(0x04, "4"),
      plain(0x23, "F 1/x"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 0.25 from hidden X2 temp" } },
      halt(),
    ]);
    expect(sqrtResult.applied).toBe(1);
    expect(sqrtResult.ops).toEqual([
      plain(0x01, "1"),
      plain(0x04, "4"),
      plain(0x04, "4"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 12 from hidden X2 temp" } },
      halt(),
    ]);
    expect(pow10Result.applied).toBe(3);
    expect(pow10Result.ops).toEqual([
      plain(0x03, "3"),
      plain(0x15, "F 10^x"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 1000 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses concrete F pi derived X2 facts", () => {
    const program: IrOp[] = [
      plain(0x20, "F pi"),
      plain(0x34, "К [x]"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x20, "F pi"),
      plain(0x34, "К [x]"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 3.0 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses exact documented function special-case X2 facts", () => {
    const cosZeroProgram: IrOp[] = [
      plain(0x00, "0"),
      plain(0x1d, "F cos"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    ];
    const powIdentityProgram: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x24, "F x^y"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    ];
    const cosZeroResult = x2LiteralRestore.run(cosZeroProgram, ctx);
    const powIdentityResult = x2LiteralRestore.run(powIdentityProgram, ctx);

    expect(cosZeroResult.applied).toBe(2);
    expect(cosZeroResult.ops).toEqual([
      plain(0x00, "0"),
      plain(0x1d, "F cos"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 1.0 from hidden X2 temp" } },
      halt(),
    ]);
    expect(powIdentityResult.applied).toBe(2);
    expect(powIdentityResult.ops).toEqual([
      plain(0x00, "0"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x24, "F x^y"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 1.0 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses exact F x^y zero-base X2 facts", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x24, "F x^y"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x24, "F x^y"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 0.0 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses exact F x^y exponent-one X2 facts", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x24, "F x^y"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x24, "F x^y"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 1.2 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses scientific exact decimal X2 facts", () => {
    const program: IrOp[] = [
      plain(0x08, "8"),
      plain(0x15, "F 10^x"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x08, "8"),
      plain(0x15, "F 10^x"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 100000000 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses scientific VP-source X2 facts after a closing sync", () => {
    const program: IrOp[] = [
      plain(0x08, "8"),
      plain(0x15, "F 10^x"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x01, "1"),
      plain(0x00, "0"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x08, "8"),
      plain(0x15, "F 10^x"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 10000000000 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses values copied through Y->X after a later X2 sync", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x3e, "Y->X"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x3e, "Y->X"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 1.0 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses concrete MK-61 bitwise X2 facts", () => {
    const bitXorProgram: IrOp[] = [
      plain(0x01, "1"),
      plain(0x06, "6"),
      plain(0x0e, "В↑"),
      plain(0x07, "7"),
      plain(0x39, "К ⊕"),
      plain(0x0e, "В↑"),
      plain(0x08, "8"),
      plain(0x0a, "."),
      plain(0x06, "6"),
      halt(),
    ];
    const bitNotProgram: IrOp[] = [
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x3a, "К ИНВ"),
      plain(0x0e, "В↑"),
      plain(0x08, "8"),
      plain(0x0a, "."),
      plain(0x06, "6"),
      plain(0x06, "6"),
      plain(0x06, "6"),
      plain(0x06, "6"),
      plain(0x06, "6"),
      plain(0x06, "6"),
      plain(0x06, "6"),
      halt(),
    ];
    const bitXorResult = x2LiteralRestore.run(bitXorProgram, ctx);
    const bitNotResult = x2LiteralRestore.run(bitNotProgram, ctx);

    expect(bitXorResult.applied).toBe(2);
    expect(bitXorResult.ops).toEqual([
      plain(0x01, "1"),
      plain(0x06, "6"),
      plain(0x0e, "В↑"),
      plain(0x07, "7"),
      plain(0x39, "К ⊕"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 8.6 from hidden X2 temp" } },
      halt(),
    ]);
    expect(bitNotResult.applied).toBe(8);
    expect(bitNotResult.ops).toEqual([
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x3a, "К ИНВ"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 8.6666666 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses decimal-only structural bitwise X2 facts", () => {
    const program: IrOp[] = [
      recall("1", "preload const 8A000000"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 85000000"),
      plain(0x37, "К ∧"),
      plain(0x0e, "В↑"),
      plain(0x08, "8"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("1", "preload const 8A000000"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 85000000"),
      plain(0x37, "К ∧"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 8.0 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore uses exact MK-61 degree/minute conversion X2 facts", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x05, "5"),
      plain(0x33, "К °<-′"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x01, "1"),
      plain(0x05, "5"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x05, "5"),
      plain(0x33, "К °<-′"),
      plain(0x0e, "В↑"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore literal 1.15 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-literal-restore keeps a repeated literal before a branch target X2 restore", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      cjump("restore"),
      halt(),
      label("restore"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore removes a safe dot when X already has the X2 register value", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      store("2"),
      plain(0x54, "К НОП"),
      halt(),
    ]);
  });

  it("x2-noop-restore keeps dot when it is a decimal separator", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore removes dot immediately after a recall X2 sync", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot immediately after Cx zero sync", () => {
    const program: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-noop-restore uses path-sensitive conditional fallthrough X2 sync", () => {
    const program: IrOp[] = [
      recall("1"),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      cjump("done"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot before a branch with no reachable X2 restore", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x0a, "."),
      cjump("done"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      cjump("done"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("x2-noop-restore keeps dot before a branch target X2 restore", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x0a, "."),
      cjump("restore"),
      halt(),
      label("restore"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore uses path-sensitive loop fallthrough X2 sync", () => {
    const program: IrOp[] = [
      recall("1"),
      loop("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      loop("done"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot after proved indirect conditional preserved normalized X2", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      knownTargetIndirectCjump("7", 5),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      knownTargetIndirectCjump("7", 5),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot after proved indirect conditional preserved visible leading-zero X2", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      knownTargetIndirectCjump("7", 5),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      knownTargetIndirectCjump("7", 5),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot immediately after direct return X2 sync", () => {
    const program: IrOp[] = [
      label("main"),
      call("load"),
      plain(0x0a, "."),
      halt(),
      label("load"),
      recall("1"),
      ret(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      label("main"),
      call("load"),
      halt(),
      label("load"),
      recall("1"),
      ret(),
    ]);
  });

  it("x2-noop-restore removes dot immediately after known indirect return X2 sync", () => {
    const program: IrOp[] = [
      label("main"),
      knownTargetIndirectCall("7", 3),
      plain(0x0a, "."),
      halt(),
      label("load"),
      recall("1"),
      ret(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      label("main"),
      knownTargetIndirectCall("7", 3),
      halt(),
      label("load"),
      recall("1"),
      ret(),
    ]);
  });

  it("x2-hidden-temp-restore uses immediate sync after an X2-preserving scratch alias", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      store("1"),
      plain(0x54, "К НОП"),
      recall("1"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      store("1"),
      plain(0x54, "К НОП"),
      { kind: "plain", opcode: 0x0a, meta: { mnemonic: ".", comment: "restore 1 from hidden X2 temp" } },
      halt(),
    ]);
  });

  it("x2-noop-restore removes a safe dot for a proved zero value, not only registers", () => {
    const program: IrOp[] = [
      plain(0x0d, "Cx"),
      orphanAddress(54),
      orphanAddress(55),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x0d, "Cx"),
      orphanAddress(54),
      orphanAddress(55),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot after a proved normalized decimal literal", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      store("3"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      store("3"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot after a proved normalized signed decimal literal", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      store("2"),
      store("3"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      store("2"),
      store("3"),
      halt(),
    ]);
  });

  it("x2-noop-restore keeps dot in active exponent-entry context", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore removes dot after a visible-equivalent leading-zero X2 literal", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      store("2"),
      store("3"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      store("2"),
      store("3"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes decimal dot restores before hard X/X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes open-entry dots before hard X/X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0a, "."),
      plain(0x54, "КНОП"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes exponent-entry dots before hard X/X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0a, "."),
      plain(0x54, "КНОП"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite keeps register-only dot restores before overwrite", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite removes immediate-sync dot restores before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x0a, "."),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes opaque expr dot restores before hard overwrite", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      plain(0x54, "КНОП"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes direct recalls before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes stable indirect recalls before hard overwrite", () => {
    const program: IrOp[] = [
      knownTargetIndirectRecall("7", "1"),
      plain(0x54, "КНОП"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite keeps recalls whose stack lift is consumed after overwrite", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x0d, "Cx"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite removes dead stack-shifting producers before hard overwrite", () => {
    const program: IrOp[] = [
      plain(0x20, "F pi"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite keeps stack-shifting producers whose lift is consumed", () => {
    const program: IrOp[] = [
      plain(0x20, "F pi"),
      plain(0x0d, "Cx"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite keeps display-role stack-shifting producers", () => {
    const program: IrOp[] = [
      { kind: "plain", opcode: 0x20, meta: { mnemonic: "F pi", roles: ["display-byte"] } },
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite removes dot after a preloaded decimal recall", () => {
    const program: IrOp[] = [
      recall("1", "preload const 8.1020088E14"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite keeps dot after a hex-like preloaded recall", () => {
    const program: IrOp[] = [
      recall("1", "preload const 8.70Е2-6С"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite removes emulator-pinned structural dot before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1", "preload const A"),
      store("2"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(4);
    expect(result.ops).toEqual([
      recall("1", "preload const A"),
      store("2"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes safe structural VP-dot before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1", "preload const D"),
      plain(0x0c, "ВП"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes safe structural VP-dot across address gaps", () => {
    const program: IrOp[] = [
      recall("1", "preload const D"),
      plain(0x0c, "ВП"),
      orphanAddress(54),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      orphanAddress(54),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite keeps unsafe structural VP-dot before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1", "preload const C"),
      plain(0x0c, "ВП"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite keeps unsafe structural dot before hard overwrite", () => {
    const dProgram: IrOp[] = [
      recall("1", "preload const D"),
      store("2"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const fProgram: IrOp[] = [
      recall("1", "preload const F"),
      store("2"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
    ];

    expect(x2DeadRestoreBeforeOverwrite.run(dProgram, ctx).ops).toEqual(dProgram);
    expect(x2DeadRestoreBeforeOverwrite.run(fProgram, ctx).ops).toEqual(fProgram);
  });

  it("x2-dead-restore-before-overwrite removes structural sign restore before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes closed structural exponent sign before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes a same-segment dead restore run before hard overwrite", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes dead restore runs across orphan address gaps", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      orphanAddress(54),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      orphanAddress(54),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes dead restore runs before unused recall stack lifts", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x55, "К1"),
      recall("1"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      recall("1"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes dead recalls before unused recall stack lifts", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x55, "К1"),
      recall("2"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("2"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite keeps restore runs before observed recall stack lifts", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      recall("1"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite removes dead restore runs before unused raw stack-shifting X2 producers", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x55, "К1"),
      plain(0x61, "П->X 1"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x61, "П->X 1"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite keeps restore runs before observed raw stack-shifting X2 producers", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x61, "П->X 1"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite keeps restore runs before stack-shifting constants that preserve X2", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x20, "F pi"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite keeps display-sensitive separators in dead restore runs", () => {
    const displayEmpty: IrOp = {
      kind: "plain",
      opcode: 0x55,
      meta: { mnemonic: "К1", comment: "display separator" },
    };
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      displayEmpty,
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite keeps restore runs separated by labels", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      label("entry"),
      plain(0x0b, "/-/"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      label("entry"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes open mantissa sign before hard overwrite", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes active exponent sign before hard overwrite", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes VP-context sign before hard overwrite", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes structural ВП restore before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1", "preload const 8.70Е2-6С"),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes dot-restored structural ВП before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes dot-restored structural sign before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes normalized path-sensitive structural dots", () => {
    const conditionalProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      cjump("done"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
      label("done"),
      halt(),
    ];
    const returnProgram: IrOp[] = [
      label("main"),
      call("load"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
      label("load"),
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      ret(),
    ];

    expect(x2DeadRestoreBeforeOverwrite.run(conditionalProgram, ctx).ops).toEqual([
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      cjump("done"),
      plain(0x0d, "Cx"),
      halt(),
      label("done"),
      halt(),
    ]);
    expect(x2DeadRestoreBeforeOverwrite.run(returnProgram, ctx).ops).toEqual([
      label("main"),
      call("load"),
      plain(0x0d, "Cx"),
      halt(),
      label("load"),
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      ret(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes repeated active ВП before hard overwrite", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(4);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes VP-context ВП restore before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes structural ВП after direct return sync", () => {
    const program: IrOp[] = [
      label("main"),
      call("load"),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
      label("load"),
      recall("1", "preload const 8.70Е2-6С"),
      ret(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      label("main"),
      call("load"),
      plain(0x0d, "Cx"),
      halt(),
      label("load"),
      recall("1", "preload const 8.70Е2-6С"),
      ret(),
    ]);
  });

  it("x2-dead-restore-before-overwrite keeps return-helper crossings with display-sensitive cells", () => {
    const program: IrOp[] = [
      label("main"),
      call("load"),
      plain(0x0c, "ВП"),
      call("display_gap"),
      plain(0x0d, "Cx"),
      halt(),
      label("load"),
      recall("1", "preload const 8.70Е2-6С"),
      ret(),
      label("display_gap"),
      { kind: "plain", opcode: 0x55, meta: { mnemonic: "К1", comment: "display separator" } },
      ret(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite uses only the fallthrough conditional structural ВП source", () => {
    const fallthroughProgram: IrOp[] = [
      recall("1", "preload const 8.70Е2-6С"),
      cjump("done"),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
      label("done"),
      halt(),
    ];
    const jumpProgram: IrOp[] = [
      recall("1", "preload const 8.70Е2-6С"),
      cjump("target"),
      halt(),
      label("target"),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];

    expect(x2DeadRestoreBeforeOverwrite.run(fallthroughProgram, ctx).ops).toEqual([
      recall("1", "preload const 8.70Е2-6С"),
      cjump("done"),
      plain(0x0d, "Cx"),
      halt(),
      label("done"),
      halt(),
    ]);
    expect(x2DeadRestoreBeforeOverwrite.run(jumpProgram, ctx).ops).toEqual(jumpProgram);
  });

  it("x2-dead-restore-before-overwrite uses loop fallthrough structural ВП source", () => {
    const program: IrOp[] = [
      recall("1", "preload const 8.70Е2-6С"),
      loop("done"),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("1", "preload const 8.70Е2-6С"),
      loop("done"),
      plain(0x0d, "Cx"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes dot after a recalled decimal register", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes sign restore after a recalled decimal register", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite removes dead sign and ВП restores before hard overwrite", () => {
    const signProgram: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const vpProgram: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x54, "КНОП"),
      plain(0x0d, "Cx"),
      halt(),
    ];

    expect(x2DeadRestoreBeforeOverwrite.run(signProgram, ctx).ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
    expect(x2DeadRestoreBeforeOverwrite.run(vpProgram, ctx).ops).toEqual([
      plain(0x05, "5"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite crosses transparent direct-return helpers before hard overwrite", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x0d, "Cx"),
      plain(0x0a, "."),
      call("transparent"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x0d, "Cx"),
      call("transparent"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite crosses known indirect-return helpers before hard overwrite", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x0d, "Cx"),
      plain(0x0a, "."),
      knownTargetIndirectCall("7", 2),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x0d, "Cx"),
      knownTargetIndirectCall("7", 2),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite crosses nested transparent return helpers before hard overwrite", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("transparent"),
      ret(),
      label("main"),
      plain(0x0d, "Cx"),
      plain(0x0a, "."),
      call("outer"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("transparent"),
      ret(),
      label("main"),
      plain(0x0d, "Cx"),
      call("outer"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("x2-dead-restore-before-overwrite keeps restores before nested helpers that restore X2", () => {
    const program: IrOp[] = [
      jump("main"),
      label("restore"),
      plain(0x0a, "."),
      ret(),
      label("outer"),
      call("restore"),
      ret(),
      label("main"),
      plain(0x0d, "Cx"),
      plain(0x0a, "."),
      call("outer"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite keeps restores before helpers that restore X2", () => {
    const program: IrOp[] = [
      jump("main"),
      label("restore"),
      plain(0x0a, "."),
      ret(),
      label("main"),
      plain(0x0d, "Cx"),
      plain(0x0a, "."),
      call("restore"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-dead-restore-before-overwrite keeps restores before helpers that store X", () => {
    const program: IrOp[] = [
      jump("main"),
      label("store_x"),
      store("2"),
      ret(),
      label("main"),
      plain(0x0d, "Cx"),
      plain(0x0a, "."),
      call("store_x"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore removes dot after F* syncs a leading-zero literal", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot after stack lift syncs a leading-zero literal", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot after structural arithmetic is normalized by X2 sync", () => {
    const squareProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      halt(),
    ];
    const multiplyProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      halt(),
    ];
    const squareResult = x2NoopRestore.run(squareProgram, ctx);
    const multiplyResult = x2NoopRestore.run(multiplyProgram, ctx);

    expect(squareResult.applied).toBe(1);
    expect(squareResult.ops).toEqual([
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
    expect(multiplyResult.applied).toBe(1);
    expect(multiplyResult.ops).toEqual([
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot after path-sensitive structural arithmetic syncs", () => {
    const conditionalProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const loopProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      loop("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const returnProgram: IrOp[] = [
      label("main"),
      call("load"),
      plain(0x0a, "."),
      halt(),
      label("load"),
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      ret(),
    ];

    expect(x2NoopRestore.run(conditionalProgram, ctx).ops).toEqual([
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      cjump("done"),
      halt(),
      label("done"),
      halt(),
    ]);
    expect(x2NoopRestore.run(loopProgram, ctx).ops).toEqual([
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      loop("done"),
      halt(),
      label("done"),
      halt(),
    ]);
    expect(x2NoopRestore.run(returnProgram, ctx).ops).toEqual([
      label("main"),
      call("load"),
      halt(),
      label("load"),
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      ret(),
    ]);
  });

  it("x2-noop-restore removes dot immediately after modeled closed sign-change", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot after modeled closed sign-change through empty ops", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x54, "К НОП"),
      plain(0x55, "К 1"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x54, "К НОП"),
      plain(0x55, "К 1"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot after modeled closed sign-change through address gaps", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      orphanAddress(54),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      orphanAddress(54),
      halt(),
    ]);
  });

  it("x2-noop-restore keeps dot after modeled sign-change through role-bearing empty ops", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      { kind: "plain", opcode: 0x54, meta: { mnemonic: "К НОП", roles: ["display-byte"] } },
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore removes dot after modeled opaque closed sign-change", () => {
    const program: IrOp[] = [
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      plain(0x0b, "/-/"),
      halt(),
    ]);
  });

  it("x2-noop-restore keeps dot after modeled structural closed sign-change", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore removes dot after an emulator-pinned structural sign pair", () => {
    const program: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const A"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    ]);
  });

  it("x2-noop-restore keeps unsafe structural sign-pair dots", () => {
    const dProgram: IrOp[] = [
      recall("1", "preload const D"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      halt(),
    ];
    const fProgram: IrOp[] = [
      recall("1", "preload const F"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      halt(),
    ];

    expect(x2NoopRestore.run(dProgram, ctx).ops).toEqual(dProgram);
    expect(x2NoopRestore.run(fProgram, ctx).ops).toEqual(fProgram);
  });

  it("x2-noop-restore keeps structural sign-pair dot before observable VP context", () => {
    const program: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore removes dot after modeled fractional closed sign-change", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot after a synced fractional literal", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot after a synced fractional exponent-entry", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("vp-splice removes a fractional closed-context sign pair", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("vp-splice keeps a fractional closed-context sign pair when it shields dot", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore keeps dot after closed sign-change when it shapes a following ВП", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore keeps dot after empty-op closed sign-change when it shapes a following ВП", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore keeps dot after address-gap closed sign-change when it shapes a following ВП", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      orphanAddress(54),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore removes dot after a visible-equivalent signed leading-zero X2 literal", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      store("2"),
      store("3"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      store("2"),
      store("3"),
      halt(),
    ]);
  });

  it("x2-noop-restore keeps dot when it would change the next ВП context", () => {
    const program: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x54, "К НОП"),
      plain(0x55, "К 1"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore removes dot before empty-op ВП when the VP source is unchanged", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot before immediate ВП when the VP source is unchanged", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes repeated raw decimal dot before sign-gap ВП", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0a, "."),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot before transparent return helpers and immediate ВП", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2 restore-gap scanner shares transparent helper and barrier semantics", () => {
    const transparentProgram: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x55, "К 1"),
      label("marker"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const blockedProgram: IrOp[] = [
      plain(0x0b, "/-/"),
      { kind: "plain", opcode: 0x55, meta: { mnemonic: "К 1", roles: ["display-byte"] } },
      plain(0x0c, "ВП"),
      halt(),
    ];

    expect(x2RestoreGapBeforeVp(
      transparentProgram,
      5,
      directReturnAnalysisContext(transparentProgram),
    )).toEqual({
      vpIndex: 9,
      blockedIndex: undefined,
      sawRestoreGap: true,
      sawSignRestore: true,
    });
    expect(x2RestoreGapBeforeVp(
      blockedProgram,
      0,
      directReturnAnalysisContext(blockedProgram),
    )).toEqual({
      vpIndex: undefined,
      blockedIndex: 1,
      sawRestoreGap: true,
      sawSignRestore: true,
    });
  });

  it("x2-noop-restore removes dot before transparent return helpers and empty-op ВП", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      plain(0x55, "К 1"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К 1"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot before marker labels and empty-op ВП when the VP source is unchanged", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      label("marker"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      label("marker"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot before a proved restore-run ВП source", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      plain(0x0b, "/-/"),
      plain(0x54, "К НОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x54, "К НОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot before a store-backed sign ВП source", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0a, "."),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-noop-restore removes dot before a store-backed sign-pair ВП source", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0a, "."),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("x2-noop-restore keeps dot before role-bearing empty-op ВП context", () => {
    const roleEmpty: IrOp = {
      kind: "plain",
      opcode: 0x55,
      meta: { mnemonic: "К 1", roles: ["display-byte"] },
    };
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      roleEmpty,
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore keeps dot before role-bearing sign-change ВП context", () => {
    const roleSign: IrOp = {
      kind: "plain",
      opcode: 0x0b,
      meta: { mnemonic: "/-/", roles: ["display-byte"] },
    };
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      roleSign,
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore keeps immediate post-sync dot when it shapes the next ВП context", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore keeps dot when it would change the next sign-change context", () => {
    const program: IrOp[] = [
      plain(0x0d, "Cx"),
      store("2"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore keeps dot when X no longer has the hidden X2 value", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      plain(0x35, "К {x}"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-noop-restore removes dot-safe structural dots after a free-standing gap", () => {
    const program: IrOp[] = [
      recall("1", "preload const C"),
      store("2"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const C"),
      store("2"),
      halt(),
    ]);
  });

  it("x2-noop-restore keeps unsafe structural dots after a free-standing gap", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore replaces a safe recall with dot so DSE can remove the scratch store", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      plain(0x35, "К {x}"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[3]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses value dataflow after a dot restore", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x35, "К {x}"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      store("2"),
      plain(0x35, "К {x}"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[6]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses modeled closed sign-change as a dot source", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      store("2"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x55, "К 1"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[9]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses store-backed sign ВП source as a dot escape", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("2"),
      store("1"),
      recall("2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[4]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "1")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 2);
  });

  it("x2-hidden-temp-restore uses recall VP-shape proof as a dot escape", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("2"),
      store("1"),
      recall("2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[5]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 2);
  });

  it("x2-hidden-temp-restore uses structural VP-shape proof as a dot escape", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      recall("2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[2]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps structural scratch recalls", () => {
    const program: IrOp[] = [
      recall("1", "preload const 8.70Е2-6С"),
      store("2"),
      plain(0x20, "Fπ"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);

    expect(restored.applied).toBe(0);
    expect(restored.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore uses emulator-pinned dot-safe structural scratch recalls", () => {
    const program: IrOp[] = [
      recall("1", "preload const C"),
      store("2"),
      plain(0x20, "Fπ"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[3]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses dot-safe structural scratch recalls before immediate VP context", () => {
    const program: IrOp[] = [
      recall("1", "preload const C"),
      store("2"),
      recall("2"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[2]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps structural scratch recalls before sign VP context", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      recall("2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);

    expect(restored.applied).toBe(0);
    expect(restored.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore handles stable indirect scratch stores and recalls", () => {
    const program: IrOp[] = [
      recall("1"),
      knownTargetIndirectStore("8", "2"),
      plain(0x35, "К {x}"),
      knownTargetIndirectRecall("8", "2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[3]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "indirect-store")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore crosses unreferenced marker labels", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      label("marker"),
      plain(0x35, "К {x}"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[4]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore crosses direct conditional fallthrough X2 sync", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      cjump("done"),
      recall("2"),
      halt(),
      label("done"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[3]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore crosses counted-loop fallthrough X2 sync for non-counter scratch", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      loop("done"),
      recall("2"),
      halt(),
      label("done"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[3]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore uses normalized structural shapes after path-sensitive syncs", () => {
    const conditionalProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      store("2"),
      cjump("done"),
      recall("2"),
      halt(),
      label("done"),
      halt(),
    ];
    const loopProgram: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      store("2"),
      loop("done"),
      recall("2"),
      halt(),
      label("done"),
      halt(),
    ];
    const returnProgram: IrOp[] = [
      jump("main"),
      label("sync"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      store("2"),
      call("sync"),
      recall("2"),
      halt(),
    ];

    for (const program of [conditionalProgram, loopProgram, returnProgram]) {
      const restored = x2HiddenTempRestore.run(program, ctx);
      const dse = deadStoreElimination.run(restored.ops, ctx);

      expect(restored.applied).toBe(1);
      expect(restored.ops.some((op) => op.kind === "plain" && op.opcode === 0x0a)).toBe(true);
      expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
      expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
    }
  });

  it("x2-hidden-temp-restore crosses stable known indirect conditional fallthrough", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("2"),
      knownTargetIndirectCjump("7", 6),
      recall("2"),
      halt(),
      label("done"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[4]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps recalls when an indirect conditional mutates the scratch selector", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      store("2"),
      knownTargetIndirectCjump("2", 5),
      recall("2"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore crosses simple direct-return X2 syncs that ignore scratch", () => {
    const program: IrOp[] = [
      jump("main"),
      label("sync"),
      plain(0x0d, "Cx"),
      ret(),
      label("main"),
      plain(0x0d, "Cx"),
      store("2"),
      call("sync"),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[8]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore crosses known indirect-return X2 syncs that ignore scratch", () => {
    const program: IrOp[] = [
      jump("main"),
      label("sync"),
      plain(0x0d, "Cx"),
      ret(),
      label("main"),
      plain(0x0d, "Cx"),
      store("2"),
      knownTargetIndirectCall("7", 2),
      recall("2"),
      halt(),
    ];
    const restored = x2HiddenTempRestore.run(program, ctx);
    const dse = deadStoreElimination.run(restored.ops, ctx);

    expect(restored.applied).toBe(1);
    expect(restored.ops[8]).toMatchObject({ kind: "plain", opcode: 0x0a });
    expect(dse.ops.some((op) => op.kind === "store" && op.register === "2")).toBe(false);
    expect(machineCellCount(dse.ops)).toBe(machineCellCount(program) - 1);
  });

  it("x2-hidden-temp-restore keeps recalls when a direct-return callee reads scratch", () => {
    const program: IrOp[] = [
      jump("main"),
      label("use_scratch"),
      recall("2"),
      ret(),
      label("main"),
      recall("1"),
      store("2"),
      call("use_scratch"),
      recall("2"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore keeps recalls across unknown indirect memory in direct-return callees", () => {
    const program: IrOp[] = [
      jump("main"),
      label("maybe_reads_scratch"),
      indirectRecall("7"),
      plain(0x0d, "Cx"),
      ret(),
      label("main"),
      plain(0x0d, "Cx"),
      store("2"),
      call("maybe_reads_scratch"),
      recall("2"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore keeps counted-loop counter scratch recalls", () => {
    const program: IrOp[] = [
      recall("1"),
      store("0"),
      loop("done"),
      recall("0"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore keeps recalls across referenced labels", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      jump("entry"),
      label("entry"),
      plain(0x35, "К {x}"),
      recall("2"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore requires a safe dot restore gap", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      recall("2"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore ignores repeated state recalls that do not free a scratch store", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      plain(0x20, "F pi"),
      plain(0x35, "К {x}"),
      recall("2"),
      store("2"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore does not use stale aliases after an overwrite", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      plain(0x35, "К {x}"),
      store("2"),
      plain(0x54, "К НОП"),
      recall("2"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore keeps recalls whose stack lift is consumed", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      plain(0x35, "К {x}"),
      recall("2"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore keeps duplicate-Y recalls without a previous stack/X2 producer", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      recall("2"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-hidden-temp-restore uses a previous recall lift when X and Y already match", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      store("2"),
      recall("1"),
      {
        kind: "plain",
        opcode: 0x0a,
        meta: { mnemonic: ".", comment: "restore 2 from hidden X2 temp" },
      },
      plain(0x10, "+"),
      halt(),
    ]);
  });

  it("x2-hidden-temp-restore keeps duplicate-Y recalls when deeper stack state is consumed", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = x2HiddenTempRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("dead-store-elimination removes a store overwritten before any read", () => {
    const program: IrOp[] = [
      store("1"),
      plain(0x0d, "Cx"),
      store("1"),
      recall("1"),
      halt(),
    ];
    const result = deadStoreElimination.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.filter((op) => op.kind === "store").length).toBe(1);
  });

  it("dead-store-elimination removes dead stable indirect stores with proved targets", () => {
    const program: IrOp[] = [
      knownTargetIndirectStore("8", "2"),
      halt(),
    ];
    const result = deadStoreElimination.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([halt()]);
  });

  it("dead-store-elimination keeps mutating indirect stores even when the target is dead", () => {
    const program: IrOp[] = [
      knownTargetIndirectStore("4", "2"),
      halt(),
    ];
    const result = deadStoreElimination.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("dead-store-elimination keeps stores that are read before next assignment", () => {
    const program: IrOp[] = [
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      halt(),
    ];
    const result = deadStoreElimination.run(program, ctx);
    expect(result.applied).toBe(0);
  });

  it("dead-store-elimination tracks liveness across a halt because the user can resume", () => {
    const program: IrOp[] = [
      store("1"),
      halt(),
      recall("1"),
      halt(),
    ];
    const result = deadStoreElimination.run(program, ctx);
    expect(result.applied).toBe(0);
  });

  it("dead-store-elimination tracks reads inside numeric subroutine calls", () => {
    const program: IrOp[] = [
      store("8"),
      numericCall(4),
      store("8"),
      halt(),
      recall("8"),
      ret(),
    ];
    const result = deadStoreElimination.run(program, ctx);

    expect(result.ops[0]).toMatchObject({ kind: "store", register: "8" });
  });

  it("dead-store-elimination tracks reads behind proved indirect jumps", () => {
    const program: IrOp[] = [
      store("1"),
      knownTargetIndirectJump("8", 3),
      halt(),
      label("target"),
      recall("1"),
      halt(),
    ];
    const result = deadStoreElimination.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("dead-store-elimination tracks reads inside proved indirect subroutine calls", () => {
    const program: IrOp[] = [
      store("1"),
      knownTargetIndirectCall("8", 3),
      halt(),
      label("callee"),
      recall("1"),
      ret(),
    ];
    const result = deadStoreElimination.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("dead-store-elimination keeps subroutine stores that are read after return", () => {
    const program: IrOp[] = [
      label("main"),
      call("bump"),
      recall("1"),
      halt(),
      label("bump"),
      store("1"),
      ret(),
    ];
    const result = deadStoreElimination.run(program, ctx);

    expect(result.applied).toBe(0);
  });

  it("dead-store-elimination keeps dead stores that provide ВП/X2 restore context", () => {
    const program: IrOp[] = [
      plain(0x20, "F pi"),
      plain(0x35, "К {x}"),
      store("1"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = deadStoreElimination.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse drops П->X r when X already holds that register's value", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
    ];
    const result = lastXReuse.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.length).toBe(1);
    expect(result.ops[0]!.kind).toBe("store");
  });

  it("last-x-reuse drops stable indirect recall when X already holds its proved target", () => {
    const program: IrOp[] = [
      store("5"),
      knownTargetIndirectRecall("7", "5"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      store("5"),
      halt(),
    ]);
  });

  it("last-x-reuse tracks X through a stable indirect store with a proved target", () => {
    const program: IrOp[] = [
      knownTargetIndirectStore("8", "5"),
      recall("5"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      knownTargetIndirectStore("8", "5"),
      halt(),
    ]);
  });

  it("last-x-reuse tracks X through a mutating indirect store without dropping the store side effect", () => {
    const program: IrOp[] = [
      knownTargetIndirectStore("4", "5"),
      recall("5"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      knownTargetIndirectStore("4", "5"),
      halt(),
    ]);
  });

  it("last-x-reuse keeps mutating indirect recalls after indirect stores", () => {
    const program: IrOp[] = [
      knownTargetIndirectStore("8", "5"),
      knownTargetIndirectRecall("4", "5"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse keeps predecrement indirect recall even when its target is proved", () => {
    const program: IrOp[] = [
      store("5"),
      knownTargetIndirectRecall("1", "5"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse keeps recall that syncs X2 before ВП restores it", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse keeps recall that syncs X2 before sign-change restores it", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse drops redundant X2 sync when X2 already holds the same register", () => {
    const program: IrOp[] = [
      recall("1"),
      store("1"),
      recall("1"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      store("1"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("last-x-reuse keeps X through direct conditional fallthrough", () => {
    const program: IrOp[] = [
      recall("1"),
      cjump("done"),
      recall("1"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      cjump("done"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("last-x-reuse keeps non-counter X through counted loop fallthrough", () => {
    const program: IrOp[] = [
      recall("4"),
      loop("done"),
      recall("4"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("4"),
      loop("done"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("last-x-reuse drops the counted-loop counter alias on fallthrough", () => {
    const program: IrOp[] = [
      recall("0"),
      loop("done"),
      recall("0"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse drops redundant X2 sync before ВП through proved indirect flow", () => {
    const program: IrOp[] = [
      recall("1"),
      store("1"),
      recall("1"),
      knownTargetIndirectJump("8", 5),
      halt(),
      label("target"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      store("1"),
      knownTargetIndirectJump("8", 5),
      halt(),
      label("target"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("last-x-reuse uses value X2 aliases from literal stores before ВП gaps", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x54, "КНОП"),
      recall("2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x54, "КНОП"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("last-x-reuse uses decimal register memory when X was rebuilt as the same literal", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x0d, "Cx"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      recall("2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x0d, "Cx"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("last-x-reuse drops preloaded decimal recall when X was rebuilt as the same literal", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      recall("2", "preload const 12"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("last-x-reuse drops structural recall when X was rebuilt as the same hex shape", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      plain(0x0d, "Cx"),
      recall("3", "preload const FACE"),
      recall("2"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      store("2"),
      plain(0x0d, "Cx"),
      recall("3", "preload const FACE"),
      halt(),
    ]);
  });

  it("last-x-reuse drops structural exponent preload recall when X was rebuilt as the same exponent shape", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x02, "2"),
      recall("2", "preload const ГE-2"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x02, "2"),
      halt(),
    ]);
  });

  it("last-x-reuse drops structural recall before immediate ВП when the VP source already matches", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      plain(0x0d, "Cx"),
      recall("3", "preload const FACE"),
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      store("2"),
      plain(0x0d, "Cx"),
      recall("3", "preload const FACE"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("last-x-reuse drops decimal recall before immediate ВП when the VP source already matches", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x0d, "Cx"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x0d, "Cx"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("last-x-reuse drops decimal recall before sign ВП when the sign source already matches", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      recall("2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("last-x-reuse drops structural recall before sign ВП when the sign source already matches", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      recall("2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      store("2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("last-x-reuse drops decimal recall before sign ВП through a transparent return helper", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      recall("2"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("last-x-reuse drops stable indirect decimal recall before immediate ВП when the VP source already matches", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      knownTargetIndirectStore("8", "2"),
      plain(0x0d, "Cx"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      knownTargetIndirectRecall("8", "2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      knownTargetIndirectStore("8", "2"),
      plain(0x0d, "Cx"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("last-x-reuse keeps decimal recall before immediate ВП when a store reset the VP source", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x0d, "Cx"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse keeps structural recall before immediate ВП when a store reset the VP source", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      plain(0x0d, "Cx"),
      recall("3", "preload const FACE"),
      store("4"),
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse drops structural recall before preserving gap and ВП when X2 shape already matches", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      plain(0x0d, "Cx"),
      recall("3", "preload const FACE"),
      recall("2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      store("2"),
      plain(0x0d, "Cx"),
      recall("3", "preload const FACE"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("last-x-reuse keeps redundant X2 sync before immediate ВП context", () => {
    const program: IrOp[] = [
      recall("1"),
      store("1"),
      recall("1"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse drops recall when X2 holds an alias written from the same X value", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      recall("2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      store("2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("last-x-reuse drops recall when a direct return syncs X2 before ВП", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      call("noop"),
      plain(0x0c, "ВП"),
      halt(),
      label("noop"),
      ret(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      store("1"),
      call("noop"),
      plain(0x0c, "ВП"),
      halt(),
      label("noop"),
      ret(),
    ]);
  });

  it("last-x-reuse drops recall when a known indirect return syncs X2 before ВП", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      knownTargetIndirectCall("7", 5),
      plain(0x0c, "ВП"),
      halt(),
      label("noop"),
      ret(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      store("1"),
      knownTargetIndirectCall("7", 5),
      plain(0x0c, "ВП"),
      halt(),
      label("noop"),
      ret(),
    ]);
  });

  it("last-x-reuse preserves X facts through documented empty operators", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x54, "К НОП"),
      plain(0x55, "К 1"),
      plain(0x56, "К 2"),
      recall("1"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      plain(0x54, "К НОП"),
      plain(0x55, "К 1"),
      plain(0x56, "К 2"),
      halt(),
    ]);
  });

  it("last-x-reuse preserves X facts through unreferenced labels", () => {
    const program: IrOp[] = [
      recall("1"),
      label("marker"),
      plain(0x54, "К НОП"),
      recall("1"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      label("marker"),
      plain(0x54, "К НОП"),
      halt(),
    ]);
  });

  it("last-x-reuse clears X facts at referenced labels", () => {
    const program: IrOp[] = [
      recall("1"),
      jump("entry"),
      label("entry"),
      recall("1"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse clears X facts at procedure starts", () => {
    const program: IrOp[] = [
      recall("1"),
      procStart("helper"),
      recall("1"),
      procEnd("helper"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse clears X facts at numeric-address labels", () => {
    const program: IrOp[] = [
      recall("1"),
      numericJump(3),
      label("entry"),
      recall("1"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse clears X facts at proved indirect-flow labels", () => {
    const program: IrOp[] = [
      recall("1"),
      knownTargetIndirectJump("8", 2),
      label("entry"),
      recall("1"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse treats unknown indirect flow as a label entry hazard", () => {
    const program: IrOp[] = [
      recall("1"),
      indirectJump("8"),
      label("entry"),
      recall("1"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse keeps recall that lifts the stack for an immediate binary op", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse keeps recall whose stack lift reaches a later binary op", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      plain(0x35, "К {x}"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse drops recall before proved indirect flow when the stack lift is dead", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      knownTargetIndirectJump("8", 3),
      label("target"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      store("1"),
      knownTargetIndirectJump("8", 3),
      label("target"),
      halt(),
    ]);
  });

  it("last-x-reuse keeps recall whose stack lift reaches a binary op through proved indirect flow", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      knownTargetIndirectJump("8", 3),
      label("target"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse keeps recall whose stack lift reaches a binary op after a direct call returns", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      call("frac"),
      plain(0x10, "+"),
      halt(),
      label("frac"),
      plain(0x35, "К {x}"),
      ret(),
    ];
    const result = lastXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("last-x-reuse refuses to fire across a stop barrier", () => {
    const program: IrOp[] = [
      store("1"),
      halt(),
      recall("1"),
    ];
    const result = lastXReuse.run(program, ctx);
    expect(result.applied).toBe(0);
  });

  it("last-x-reuse refuses to fire across an ALU op that clobbers X", () => {
    const program: IrOp[] = [
      store("1"),
      plain(0x10, "+"),
      recall("1"),
    ];
    const result = lastXReuse.run(program, ctx);
    expect(result.applied).toBe(0);
  });

  it("flow-x-reuse drops recall reached through a direct jump with X preserved", () => {
    const program: IrOp[] = [
      recall("4"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      recall("4"),
      store("5"),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.optimizations[0]?.name).toBe("flow-x-reuse");
    expect(result.ops.filter((op) => op.kind === "recall" && op.register === "4")).toHaveLength(1);
  });

  it("flow-x-reuse follows proved stable indirect flow targets", () => {
    const program: IrOp[] = [
      recall("4"),
      knownTargetIndirectJump("8", 3),
      halt(),
      label("tail"),
      recall("4"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.filter((op) => op.kind === "recall" && op.register === "4")).toHaveLength(1);
  });

  it("flow-x-reuse refuses unknown indirect flow targets", () => {
    const program: IrOp[] = [
      recall("4"),
      indirectJump("8"),
      halt(),
      label("tail"),
      recall("4"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("flow-x-reuse preserves unrelated X facts across mutating proved indirect flow", () => {
    const program: IrOp[] = [
      recall("4"),
      knownTargetIndirectJump("1", 3),
      halt(),
      label("tail"),
      recall("4"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.filter((op) => op.kind === "recall" && op.register === "4")).toHaveLength(1);
  });

  it("flow-x-reuse clears the mutated selector X fact across proved indirect flow", () => {
    const program: IrOp[] = [
      recall("1"),
      knownTargetIndirectJump("1", 3),
      halt(),
      label("tail"),
      recall("1"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("flow-x-reuse drops stable indirect recall reached with its proved target already in X", () => {
    const program: IrOp[] = [
      recall("5"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      knownTargetIndirectRecall("7", "5"),
      store("6"),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.some((op) => op.kind === "indirect-recall")).toBe(false);
  });

  it("flow-x-reuse keeps preincrement indirect recall because removing it would skip selector mutation", () => {
    const program: IrOp[] = [
      recall("5"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      knownTargetIndirectRecall("4", "5"),
      store("6"),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("flow-x-reuse keeps recall that syncs X2 before a preserving op and ВП", () => {
    const program: IrOp[] = [
      store("4"),
      recall("4"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("flow-x-reuse drops redundant X2 sync before a preserving op and ВП", () => {
    const program: IrOp[] = [
      recall("4"),
      store("5"),
      recall("4"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("4"),
      store("5"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("flow-x-reuse uses value X2 aliases from literal stores before ВП gaps", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      recall("4"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("flow-x-reuse uses store-backed sign sources before sign ВП gaps", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      recall("4"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("flow-x-reuse uses structural sign sources before sign ВП gaps", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("4"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      recall("4"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      store("4"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("flow-x-reuse uses store-backed sign sources before sign ВП through transparent return helpers", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      recall("4"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("flow-x-reuse uses decimal register memory after X is rebuilt across CFG flow", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      plain(0x0d, "Cx"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      recall("4"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      plain(0x0d, "Cx"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("flow-x-reuse drops preloaded decimal recall after X is rebuilt across CFG flow", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      recall("4", "preload const 12"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      jump("tail"),
      plain(0x00, "0"),
      label("tail"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("flow-x-reuse keeps redundant X2 sync before immediate ВП context", () => {
    const program: IrOp[] = [
      recall("4"),
      store("5"),
      recall("4"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("flow-x-reuse uses a direct conditional fallthrough X2 sync", () => {
    const program: IrOp[] = [
      recall("4"),
      cjump("skip"),
      recall("4"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
      label("skip"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("4"),
      cjump("skip"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
      label("skip"),
      halt(),
    ]);
  });

  it("flow-x-reuse uses counted-loop fallthrough X proof for non-counter registers", () => {
    const program: IrOp[] = [
      recall("4"),
      loop("skip"),
      recall("4"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
      label("skip"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("4"),
      loop("skip"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
      label("skip"),
      halt(),
    ]);
  });

  it("flow-x-reuse keeps counted-loop counter fallthrough recalls", () => {
    const program: IrOp[] = [
      recall("0"),
      loop("skip"),
      recall("0"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
      label("skip"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("flow-x-reuse drops recall when a direct return syncs X2 before ВП", () => {
    const program: IrOp[] = [
      store("4"),
      recall("4"),
      call("noop"),
      plain(0x0c, "ВП"),
      halt(),
      label("noop"),
      ret(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      store("4"),
      call("noop"),
      plain(0x0c, "ВП"),
      halt(),
      label("noop"),
      ret(),
    ]);
  });

  it("flow-x-reuse drops recall when a known indirect return syncs X2 before ВП", () => {
    const program: IrOp[] = [
      store("4"),
      recall("4"),
      knownTargetIndirectCall("7", 5),
      plain(0x0c, "ВП"),
      halt(),
      label("noop"),
      ret(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      store("4"),
      knownTargetIndirectCall("7", 5),
      plain(0x0c, "ВП"),
      halt(),
      label("noop"),
      ret(),
    ]);
  });

  it("flow-x-reuse keeps recall that lifts the stack for an immediate binary op", () => {
    const program: IrOp[] = [
      store("4"),
      recall("4"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("flow-x-reuse keeps recall whose stack lift survives X-only ops before binary use", () => {
    const program: IrOp[] = [
      store("4"),
      recall("4"),
      plain(0x35, "К {x}"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("flow-x-reuse drops recalls on both condition paths when the tested X value is preserved", () => {
    const program: IrOp[] = [
      recall("2"),
      cjump("false"),
      recall("2"),
      store("3"),
      jump("end"),
      label("false"),
      recall("2"),
      store("4"),
      label("end"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops.filter((op) => op.kind === "recall" && op.register === "2")).toHaveLength(1);
  });

  it("flow-x-reuse keeps recall at a merge when predecessors disagree about X", () => {
    const program: IrOp[] = [
      recall("1"),
      cjump("right"),
      recall("2"),
      jump("join"),
      label("right"),
      recall("3"),
      label("join"),
      recall("2"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);
    expect(result.ops.at(-2)).toMatchObject({ kind: "recall", register: "2" });
  });

  it("flow-x-reuse ignores direct call continuations unless the callee returns", () => {
    const program: IrOp[] = [
      recall("1"),
      cjump("join"),
      call("terminal"),
      label("join"),
      recall("1"),
      halt(),
      label("terminal"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.filter((op) => op.kind === "recall" && op.register === "1")).toHaveLength(1);
  });

  it("flow-x-reuse carries X facts into direct callees", () => {
    const program: IrOp[] = [
      recall("4"),
      call("callee"),
      jump("end"),
      label("callee"),
      recall("4"),
      ret(),
      label("end"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("4"),
      call("callee"),
      jump("end"),
      label("callee"),
      ret(),
      label("end"),
      halt(),
    ]);
  });

  it("flow-x-reuse carries returned X facts back to direct call continuations", () => {
    const program: IrOp[] = [
      call("load"),
      recall("4"),
      halt(),
      label("load"),
      recall("4"),
      ret(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      call("load"),
      halt(),
      label("load"),
      recall("4"),
      ret(),
    ]);
  });

  it("flow-x-reuse carries X facts through proved indirect calls", () => {
    const program: IrOp[] = [
      recall("4"),
      knownTargetIndirectCall("8", 4),
      jump("end"),
      label("callee"),
      recall("4"),
      ret(),
      label("end"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("4"),
      knownTargetIndirectCall("8", 4),
      jump("end"),
      label("callee"),
      ret(),
      label("end"),
      halt(),
    ]);
  });

  it("flow-x-reuse intersects disagreeing return-time X facts", () => {
    const program: IrOp[] = [
      call("choose"),
      recall("1"),
      halt(),
      label("choose"),
      cjump("right"),
      recall("1"),
      ret(),
      label("right"),
      recall("2"),
      ret(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("flow-x-reuse preserves X facts through documented empty operators", () => {
    const program: IrOp[] = [
      recall("2"),
      cjump("target"),
      plain(0x54, "К НОП"),
      jump("join"),
      label("target"),
      plain(0x55, "К 1"),
      label("join"),
      plain(0x56, "К 2"),
      recall("2"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("2"),
      cjump("target"),
      plain(0x54, "К НОП"),
      jump("join"),
      label("target"),
      plain(0x55, "К 1"),
      label("join"),
      plain(0x56, "К 2"),
      halt(),
    ]);
  });

  it("flow-x-reuse reuses X across counted loop backedges for non-counter registers", () => {
    const program: IrOp[] = [
      recall("2"),
      label("body"),
      recall("2"),
      loop("body"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("2"),
      label("body"),
      loop("body"),
      halt(),
    ]);
  });

  it("flow-x-reuse keeps counted-loop counter recalls after the loop mutates the register", () => {
    const program: IrOp[] = [
      recall("0"),
      label("body"),
      recall("0"),
      loop("body"),
      halt(),
    ];
    const result = flowXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("flow-x-reuse avoids programs with absolute or indirect flow targets", () => {
    const absolute: IrOp[] = [recall("1"), numericJump(4), recall("1"), halt()];
    const indirect: IrOp[] = [recall("1"), indirectJump("7"), recall("1"), halt()];

    expect(flowXReuse.run(absolute, ctx).applied).toBe(0);
    expect(flowXReuse.run(indirect, ctx).applied).toBe(0);
  });

  it("pre-shift-stack-lift removes В↑ already supplied by following recall", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes В↑ after recall when the recall already supplies X2 sync", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes В↑ after an existing stack lift and X2 sync", () => {
    const firstLift: IrOp = { kind: "plain", opcode: 0x0e, meta: { mnemonic: "В↑", comment: "first lift" } };
    const secondLift: IrOp = { kind: "plain", opcode: 0x0e, meta: { mnemonic: "В↑", comment: "second lift" } };
    const program: IrOp[] = [
      firstLift,
      secondLift,
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      firstLift,
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift keeps post-recall В↑ when its stack lift is consumed", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift keeps post-recall В↑ after display-sensitive recalls", () => {
    const program: IrOp[] = [
      recall("1", "display digit"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift removes post-producer В↑ through transparent stack/X2 gaps", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x54, "К НОП"),
      label("local_gap"),
      store("2"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      plain(0x54, "К НОП"),
      label("local_gap"),
      store("2"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift keeps post-producer В↑ through gaps when its stack lift is consumed", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x54, "К НОП"),
      store("2"),
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift stops post-producer scanning at stack-consuming gaps", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift stops post-producer scanning at X-changing gaps", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x22, "F x^2"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift stops post-producer scanning at targeted entry labels", () => {
    const program: IrOp[] = [
      recall("1"),
      label("entry"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
      jump("entry"),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift removes post-producer В↑ through direct conditional fallthrough", () => {
    const program: IrOp[] = [
      recall("1"),
      cjump("done"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes post-producer В↑ through counted-loop fallthrough", () => {
    const program: IrOp[] = [
      recall("1"),
      loop("done"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      loop("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes post-producer В↑ through proved indirect conditional fallthrough", () => {
    const program: IrOp[] = [
      recall("1"),
      knownTargetIndirectCjump("7", 5),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      knownTargetIndirectCjump("7", 5),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift keeps post-producer В↑ across unknown indirect conditionals", () => {
    const program: IrOp[] = [
      recall("1"),
      indirectCjump("7"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift removes post-producer В↑ through X-preserving direct-return helpers", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      store("2"),
      ret(),
      label("main"),
      recall("1"),
      call("noop"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      store("2"),
      ret(),
      label("main"),
      recall("1"),
      call("noop"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes post-producer В↑ through proved indirect-return helpers", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      recall("1"),
      knownTargetIndirectCall("7", 2),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      recall("1"),
      knownTargetIndirectCall("7", 2),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes post-producer В↑ through nested X-preserving direct-return helpers", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("outer"),
      call("noop"),
      ret(),
      label("main"),
      recall("1"),
      call("outer"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("outer"),
      call("noop"),
      ret(),
      label("main"),
      recall("1"),
      call("outer"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes В↑ before a path-safe fallthrough X2 sync", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x54, "К НОП"),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes В↑ through a proved indirect gap before a fallthrough X2 sync", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      knownTargetIndirectCjump("7", 8),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      knownTargetIndirectCjump("7", 8),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift keeps В↑ before a proved indirect gap when the jump edge observes X2", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      knownTargetIndirectCjump("7", 5),
      halt(),
      label("restore"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift removes В↑ before a plain X-preserving X2 sync", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x54, "К НОП"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift keeps В↑ before a plain X2 sync when the lift is consumed", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0xf0, "F* empty F0"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift removes В↑ after a plain X-preserving X2 sync", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes В↑ after a hard X/X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x0d, "Cx"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift keeps В↑ after a hard X/X2 overwrite when the lift is consumed", () => {
    const program: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift keeps В↑ after a plain X2 sync when the lift is consumed", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift removes В↑ after a direct fallthrough X2 sync", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      cjump("done"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      cjump("done"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift keeps В↑ after a conditional when the jump edge enters the lift", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      cjump("entry"),
      label("entry"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift removes В↑ after a transparent return-call X2 sync", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      call("noop"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      call("noop"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes В↑ after a stack-preserving return helper that changes X", () => {
    const program: IrOp[] = [
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      recall("1"),
      call("square"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      recall("1"),
      call("square"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift stops post-plain-sync scanning at X-changing gaps", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x22, "F x^2"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift keeps В↑ before a fallthrough sync when the jump edge observes X2", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      cjump("restore"),
      halt(),
      label("restore"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift keeps post-producer В↑ before nested helpers that restore X2", () => {
    const program: IrOp[] = [
      jump("main"),
      label("restore"),
      plain(0x0a, "."),
      ret(),
      label("outer"),
      call("restore"),
      ret(),
      label("main"),
      recall("1"),
      call("outer"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift keeps post-producer В↑ before display-sensitive nested helper calls", () => {
    const displayCall: Extract<IrOp, { kind: "call" }> = {
      kind: "call",
      target: "noop",
      opcode: 0x53,
      meta: { mnemonic: "ПП", comment: "display helper call" },
      targetMeta: {},
    };
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("outer"),
      displayCall,
      ret(),
      label("main"),
      recall("1"),
      call("outer"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift keeps В↑ after helpers that change X when the lift is consumed", () => {
    const program: IrOp[] = [
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      recall("1"),
      call("square"),
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("stack/X2 effect analysis classifies recall as combined lift and X2 sync", () => {
    expect(analyzeX2StackEffect(recall("2"))).toMatchObject({
      stackShifts: true,
      x2Affects: true,
      stackLiftAndX2Sync: true,
      hardX2OverwriteWithoutStackUse: false,
    });
    expect(analyzeX2StackEffect(plain(0x0e, "В↑"))).toMatchObject({
      stackShifts: true,
      x2Affects: true,
      stackLiftAndX2Sync: true,
    });
    expect(analyzeX2StackEffect(plain(0x20, "F pi"))).toMatchObject({
      stackShifts: true,
      x2Preserves: true,
      stackLiftAndX2Sync: false,
    });
    expect(analyzeX2StackEffect(plain(0x0d, "Cx"))).toMatchObject({
      stackPreserves: true,
      x2Affects: true,
      hardX2OverwriteWithoutStackUse: true,
    });
  });

  it("stack/X2 scheduler helpers find producers and dead overwrites through transparent gaps", () => {
    const producerProgram: IrOp[] = [
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      label("marker"),
      store("2"),
      recall("1"),
      halt(),
    ];
    const overwriteProgram: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("noop"),
      plain(0x0d, "Cx"),
      halt(),
    ];

    expect(x2NextStackShiftingProducerIndex(
      producerProgram,
      1,
      directReturnAnalysisContext(producerProgram),
    )).toBe(4);
    expect(x2NextHardX2OverwriteIndex(
      overwriteProgram,
      6,
      directReturnAnalysisContext(overwriteProgram),
    )).toBe(7);
  });

  it("stack/X2 scheduler helpers find X-preserving syncs through shared path proofs", () => {
    const indirectSyncProgram: IrOp[] = [
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      knownTargetIndirectCjump("7", 7),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    ];
    const returnSyncProgram: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("noop"),
      plain(0x0a, "."),
      halt(),
    ];

    expect(x2NextXPreservingX2SyncIndex(
      indirectSyncProgram,
      1,
      directReturnAnalysisContext(indirectSyncProgram),
    )).toBe(3);
    expect(x2NextXPreservingX2SyncIndex(
      returnSyncProgram,
      6,
      directReturnAnalysisContext(returnSyncProgram),
    )).toBe(6);
  });

  it("stack/X2 scheduler helpers find return X2 syncs through X-changing stack-preserving helpers", () => {
    const nextReturnSyncProgram: IrOp[] = [
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("square"),
      plain(0x0a, "."),
      halt(),
    ];
    const previousReturnSyncProgram: IrOp[] = [
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      call("square"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      halt(),
    ];
    const restoringHelperProgram: IrOp[] = [
      jump("main"),
      label("restore"),
      plain(0x0a, "."),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("restore"),
      halt(),
    ];
    const nextReturnSyncThroughXChangingGapProgram: IrOp[] = [
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      plain(0x22, "F x^2"),
      call("square"),
      plain(0x0a, "."),
      halt(),
    ];
    const restoreGapProgram: IrOp[] = [
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      call("square"),
      halt(),
    ];

    expect(x2NextStackPreservingReturnX2SyncIndex(
      nextReturnSyncProgram,
      6,
      directReturnAnalysisContext(nextReturnSyncProgram),
    )).toBe(6);
    expect(x2PreviousStackPreservingReturnX2SyncIndex(
      previousReturnSyncProgram,
      7,
      directReturnAnalysisContext(previousReturnSyncProgram),
    )).toBe(5);
    expect(x2NextStackPreservingReturnX2SyncIndex(
      restoringHelperProgram,
      6,
      directReturnAnalysisContext(restoringHelperProgram),
    )).toBeUndefined();
    expect(x2NextStackPreservingReturnX2SyncIndex(
      nextReturnSyncThroughXChangingGapProgram,
      6,
      directReturnAnalysisContext(nextReturnSyncThroughXChangingGapProgram),
    )).toBe(7);
    expect(x2NextStackPreservingReturnX2SyncIndex(
      restoreGapProgram,
      6,
      directReturnAnalysisContext(restoreGapProgram),
    )).toBeUndefined();
  });

  it("stack/X2 scheduler helpers find previous X-preserving syncs conservatively", () => {
    const fallthroughSyncProgram: IrOp[] = [
      plain(0x02, "2"),
      cjump("done"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      halt(),
      label("done"),
      halt(),
    ];
    const returnSyncProgram: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      call("noop"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      halt(),
    ];
    const numericTargetProgram: IrOp[] = [
      plain(0x02, "2"),
      numericCjump(3),
      plain(0x0e, "В↑"),
      halt(),
    ];

    expect(x2PreviousXPreservingX2SyncIndex(
      fallthroughSyncProgram,
      3,
      directReturnAnalysisContext(fallthroughSyncProgram),
    )).toBe(1);
    expect(x2PreviousXPreservingX2SyncIndex(
      returnSyncProgram,
      8,
      directReturnAnalysisContext(returnSyncProgram),
    )).toBe(6);
    expect(x2PreviousXPreservingX2SyncIndex(
      numericTargetProgram,
      2,
      directReturnAnalysisContext(numericTargetProgram),
    )).toBeUndefined();
  });

  it("stack/X2 scheduler helpers find previous hard overwrites through transparent gaps", () => {
    const hardOverwriteProgram: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x54, "К НОП"),
      store("2"),
      plain(0x0e, "В↑"),
      halt(),
    ];
    const blockingProgram: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x22, "F x^2"),
      plain(0x0e, "В↑"),
      halt(),
    ];

    expect(x2PreviousHardX2OverwriteIndex(
      hardOverwriteProgram,
      3,
      directReturnAnalysisContext(hardOverwriteProgram),
    )).toBe(0);
    expect(x2PreviousHardX2OverwriteIndex(
      blockingProgram,
      2,
      directReturnAnalysisContext(blockingProgram),
    )).toBeUndefined();
  });

  it("stack/X2 scheduler helpers refuse context-sensitive X2 restore gaps", () => {
    const restoreGapProgram: IrOp[] = [
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ];
    const directReturnRestoreProgram: IrOp[] = [
      jump("main"),
      label("restore"),
      plain(0x0a, "."),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("restore"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ];

    expect(x2NextStackShiftingProducerIndex(
      restoreGapProgram,
      1,
      directReturnAnalysisContext(restoreGapProgram),
    )).toBeUndefined();
    expect(x2NextStackShiftingProducerIndex(
      directReturnRestoreProgram,
      6,
      directReturnAnalysisContext(directReturnRestoreProgram),
    )).toBeUndefined();
  });

  it("pre-shift-stack-lift keeps В↑ before context-sensitive X2 restore gaps", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift removes В↑ before indirect recall", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      indirectRecall("7"),
      plain(0x12, "*"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      indirectRecall("7"),
      plain(0x12, "*"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes В↑ before F pi when the following constant push supplies Y", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      plain(0x20, "F pi"),
      plain(0x12, "*"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x20, "F pi"),
      plain(0x12, "*"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift skips stack-preserving gap ops before the producer", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      label("marker"),
      store("2"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x54, "К НОП"),
      label("marker"),
      store("2"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift crosses direct conditional fallthrough when the other edge cannot observe stack or X2", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      cjump("done"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      cjump("done"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift crosses counted-loop fallthrough before a hard X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      loop("done"),
      plain(0x0d, "Cx"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      loop("done"),
      plain(0x0d, "Cx"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift crosses simple direct-return callees before a following recall", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("noop"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      call("noop"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift crosses nested direct-return callees before a following recall", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("outer"),
      call("noop"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("outer"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("outer"),
      call("noop"),
      ret(),
      label("main"),
      call("outer"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift crosses known indirect-return callees before a following recall", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      knownTargetIndirectCall("7", 2),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      knownTargetIndirectCall("7", 2),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift crosses proved indirect conditional fallthrough before a following recall", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      knownTargetIndirectCjump("7", 5),
      recall("1"),
      plain(0x12, "*"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      knownTargetIndirectCjump("7", 5),
      recall("1"),
      plain(0x12, "*"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift keeps lifts across proved indirect conditionals when the target consumes stack", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      knownTargetIndirectCjump("7", 5),
      recall("1"),
      plain(0x12, "*"),
      halt(),
      label("consume"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift crosses simple direct-return callees before hard X2 overwrite", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("noop"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      call("noop"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes В↑ before a transparent direct-return X2 sync", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("noop"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      call("noop"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes В↑ before a stack-preserving return helper that changes X", () => {
    const program: IrOp[] = [
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      call("square"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      call("square"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes В↑ before a return X2 sync through an X-changing gap", () => {
    const program: IrOp[] = [
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x22, "F x^2"),
      call("square"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x22, "F x^2"),
      call("square"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes В↑ before a transparent known-indirect return X2 sync", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      knownTargetIndirectCall("7", 2),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      knownTargetIndirectCall("7", 2),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift removes В↑ before a linear В/О X2 sync", () => {
    const program: IrOp[] = [
      jump("main"),
      label("sync"),
      plain(0x0e, "В↑"),
      ret(),
      label("main"),
      call("sync"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("sync"),
      ret(),
      label("main"),
      call("sync"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift keeps В↑ before В/О when a caller consumes the stack lift", () => {
    const program: IrOp[] = [
      jump("main"),
      label("sync"),
      plain(0x0e, "В↑"),
      ret(),
      label("main"),
      call("sync"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift keeps В↑ before a direct-return sync when the lift reaches a stack consumer", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("noop"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift keeps В↑ before direct-return callees that consume stack", () => {
    const program: IrOp[] = [
      jump("main"),
      label("consume"),
      plain(0x10, "+"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("consume"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift keeps В↑ before direct-return callees that restore X2", () => {
    const program: IrOp[] = [
      jump("main"),
      label("restore"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("restore"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift keeps В↑ across conditionals when the skipped edge observes X2", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      cjump("done"),
      recall("1"),
      halt(),
      label("done"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift keeps В↑ across conditionals when the skipped edge consumes the stack lift", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      cjump("done"),
      recall("1"),
      halt(),
      label("done"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift stops gap scanning at stack-consuming commands", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      recall("1"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift removes В↑ made dead before a hard X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      store("2"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x54, "К НОП"),
      store("2"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift keeps В↑ before hard X2 overwrite when the stack lift is consumed", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      plain(0x0d, "Cx"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift collapses adjacent В↑ lifts when the deeper duplicate is unused", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      plain(0x0e, "В↑"),
      plain(0x12, "*"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x0e, "В↑"),
      plain(0x12, "*"),
      halt(),
    ]);
  });

  it("pre-shift-stack-lift keeps В↑ when the deeper stack difference reaches a later consumer", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      recall("1"),
      plain(0x12, "*"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("pre-shift-stack-lift keeps В↑ before stack-exposing commands", () => {
    const program: IrOp[] = [
      plain(0x0e, "В↑"),
      recall("1"),
      plain(0x25, "F reverse"),
      halt(),
    ];
    const result = preShiftStackLift.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("branch-target-x-reuse drops recall when a condition target preserves X", () => {
    const program: IrOp[] = [
      recall("6"),
      cjump("target"),
      jump("end"),
      label("target"),
      recall("6"),
      plain(0x32, "К ЗН"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.optimizations[0]?.name).toBe("branch-target-x-reuse");
    expect(result.ops.filter((op) => op.kind === "recall" && op.register === "6")).toHaveLength(1);
    const target = result.ops.findIndex((op) => op.kind === "label" && op.name === "target");
    expect(result.ops[target + 1]).toMatchObject({ kind: "plain", opcode: 0x32 });
  });

  it("branch-target-x-reuse treats stop as a no-fallthrough target separator", () => {
    const program: IrOp[] = [
      recall("6"),
      cjump("target"),
      halt(),
      label("target"),
      recall("6"),
      plain(0x32, "К ЗН"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("6"),
      cjump("target"),
      halt(),
      label("target"),
      plain(0x32, "К ЗН"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse drops recall when a counted-loop target preserves non-counter X", () => {
    const program: IrOp[] = [
      recall("6"),
      loop("target"),
      halt(),
      label("target"),
      recall("6"),
      plain(0x32, "К ЗН"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("6"),
      loop("target"),
      halt(),
      label("target"),
      plain(0x32, "К ЗН"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse keeps counted-loop counter target recalls", () => {
    const program: IrOp[] = [
      recall("0"),
      loop("target"),
      halt(),
      label("target"),
      recall("0"),
      plain(0x32, "К ЗН"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("branch-target-x-reuse drops stable indirect recall when the condition already tested its target", () => {
    const program: IrOp[] = [
      knownTargetIndirectRecall("7", "6"),
      cjump("target"),
      jump("end"),
      label("target"),
      knownTargetIndirectRecall("8", "6"),
      plain(0x32, "К ЗН"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.filter((op) => op.kind === "indirect-recall")).toHaveLength(1);
    const target = result.ops.findIndex((op) => op.kind === "label" && op.name === "target");
    expect(result.ops[target + 1]).toMatchObject({ kind: "plain", opcode: 0x32 });
  });

  it("branch-target-x-reuse follows proved stable indirect conditional targets", () => {
    const program: IrOp[] = [
      recall("6"),
      knownTargetIndirectCjump("8", 4),
      jump("end"),
      label("target"),
      recall("6"),
      plain(0x32, "К ЗН"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("6"),
      knownTargetIndirectCjump("8", 4),
      jump("end"),
      label("target"),
      plain(0x32, "К ЗН"),
      label("end"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse keeps proved indirect target recalls for mutating selectors", () => {
    const program: IrOp[] = [
      recall("1"),
      knownTargetIndirectCjump("1", 4),
      jump("end"),
      label("target"),
      recall("1"),
      plain(0x32, "К ЗН"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("branch-target-x-reuse keeps recall that syncs X2 before ВП in the target", () => {
    const program: IrOp[] = [
      recall("6"),
      cjump("target"),
      jump("end"),
      label("target"),
      recall("6"),
      plain(0x0c, "ВП"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("branch-target-x-reuse drops redundant target X2 sync before preserving op and ВП", () => {
    const program: IrOp[] = [
      recall("6"),
      cjump("target"),
      jump("end"),
      label("target"),
      recall("6"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("6"),
      cjump("target"),
      jump("end"),
      label("target"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      label("end"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse crosses transparent target prefix before a redundant recall", () => {
    const program: IrOp[] = [
      recall("6"),
      cjump("target"),
      jump("end"),
      label("target"),
      plain(0x54, "КНОП"),
      recall("6"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("6"),
      cjump("target"),
      jump("end"),
      label("target"),
      plain(0x54, "КНОП"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      label("end"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse handles numeric conditional targets", () => {
    const program: IrOp[] = [
      recall("6"),
      numericCjump(4),
      halt(),
      recall("6"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("6"),
      numericCjump(4),
      halt(),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse uses projected X2 register state at numeric targets", () => {
    const program: IrOp[] = [
      recall("6"),
      numericCjump(4),
      halt(),
      recall("6"),
      plain(0x54, "К НОП"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("6"),
      numericCjump(4),
      halt(),
      plain(0x54, "К НОП"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse keeps target recall when an alias label is another entry", () => {
    const program: IrOp[] = [
      recall("6"),
      cjump("target"),
      jump("alias"),
      label("target"),
      label("alias"),
      recall("6"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("branch-target-x-reuse drops target recall before sign ВП through a transparent return helper", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      recall("4"),
      cjump("target"),
      halt(),
      label("target"),
      recall("4"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      recall("4"),
      cjump("target"),
      halt(),
      label("target"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse uses decimal value proof at unique targets", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      store("6"),
      recall("1", "preload const 2"),
      cjump("target"),
      jump("end"),
      label("target"),
      recall("6"),
      plain(0x32, "К ЗН"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      store("6"),
      recall("1", "preload const 2"),
      cjump("target"),
      jump("end"),
      label("target"),
      plain(0x32, "К ЗН"),
      label("end"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse uses structural shape proof at unique targets", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      store("6"),
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      cjump("target"),
      jump("end"),
      label("target"),
      recall("6"),
      plain(0x32, "К ЗН"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      store("6"),
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      cjump("target"),
      jump("end"),
      label("target"),
      plain(0x32, "К ЗН"),
      label("end"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse uses structural exponent preload proof at unique targets", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x02, "2"),
      store("6"),
      recall("2", "preload const ГE-2"),
      cjump("target"),
      jump("end"),
      label("target"),
      recall("6"),
      plain(0x32, "К ЗН"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x02, "2"),
      store("6"),
      recall("2", "preload const ГE-2"),
      cjump("target"),
      jump("end"),
      label("target"),
      plain(0x32, "К ЗН"),
      label("end"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse crosses X-preserving store prefixes at unique targets", () => {
    const program: IrOp[] = [
      recall("4"),
      cjump("target"),
      jump("end"),
      label("target"),
      store("5"),
      recall("4"),
      plain(0x35, "К {x}"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("4"),
      cjump("target"),
      jump("end"),
      label("target"),
      store("5"),
      plain(0x35, "К {x}"),
      label("end"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse uses target-prefix stores as in-X value proofs", () => {
    const program: IrOp[] = [
      recall("4"),
      cjump("target"),
      jump("end"),
      label("target"),
      store("6"),
      recall("6"),
      plain(0x35, "К {x}"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("4"),
      cjump("target"),
      jump("end"),
      label("target"),
      store("6"),
      plain(0x35, "К {x}"),
      label("end"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse crosses proved indirect store prefixes at unique targets", () => {
    const program: IrOp[] = [
      recall("4"),
      cjump("target"),
      jump("end"),
      label("target"),
      knownTargetIndirectStore("8", "5"),
      recall("4"),
      plain(0x35, "К {x}"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("4"),
      cjump("target"),
      jump("end"),
      label("target"),
      knownTargetIndirectStore("8", "5"),
      plain(0x35, "К {x}"),
      label("end"),
      halt(),
    ]);
  });

  it("branch-target-x-reuse keeps recall that lifts the stack for a target binary op", () => {
    const program: IrOp[] = [
      recall("6"),
      cjump("target"),
      jump("end"),
      label("target"),
      recall("6"),
      plain(0x10, "+"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("branch-target-x-reuse keeps target recall whose stack lift reaches a later binary op", () => {
    const program: IrOp[] = [
      recall("6"),
      cjump("target"),
      jump("end"),
      label("target"),
      recall("6"),
      plain(0x35, "К {x}"),
      plain(0x10, "+"),
      label("end"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("branch-target-x-reuse keeps recall when the target has a fallthrough predecessor", () => {
    const program: IrOp[] = [
      recall("6"),
      cjump("target"),
      plain(0x01, "1"),
      label("target"),
      recall("6"),
      halt(),
    ];
    const result = branchTargetXReuse.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("jump-thread chases jump-to-jump trampolines to the final target", () => {
    const program: IrOp[] = [
      jump("A"),
      label("A"),
      jump("B"),
      label("B"),
      halt(),
    ];
    const result = jumpThread.run(program, ctx);
    expect(result.applied).toBeGreaterThanOrEqual(1);
    const firstJump = result.ops.find((op) => op.kind === "jump") as Extract<IrOp, { kind: "jump" }>;
    expect(firstJump.target).toBe("B");
  });

  it("jump-to-next-threading drops БП immediately before its label", () => {
    const program: IrOp[] = [
      jump("END"),
      label("END"),
      halt(),
    ];
    const result = jumpToNextThreading.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.find((op) => op.kind === "jump")).toBeUndefined();
  });

  it("tail-call-lowering specializes procedures called only with one jump continuation", () => {
    const program: IrOp[] = [
      label("main"),
      call("finish_turn"),
      jump("loop"),
      label("loop"),
      halt(),
      label("finish_turn"),
      cjump("done"),
      halt(),
      label("done"),
      ret(),
    ];
    const result = tailCallLowering.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops[1]).toMatchObject({ kind: "jump", target: "finish_turn" });
    expect(result.ops.some((op) => op.kind === "return")).toBe(false);
    expect(result.ops.at(-1)).toMatchObject({ kind: "jump", target: "loop" });
  });

  it("tail-call-lowering refuses mixed continuations for the same procedure", () => {
    const program: IrOp[] = [
      label("main"),
      call("finish_turn"),
      jump("loop"),
      label("other"),
      call("finish_turn"),
      jump("menu"),
      label("finish_turn"),
      ret(),
    ];
    const result = tailCallLowering.run(program, ctx);

    expect(result.applied).toBe(0);
  });

  it("tail-call-lowering turns call plus return jump into a direct tail jump", () => {
    const program: IrOp[] = [
      label("main"),
      call("finish_turn"),
      jump("return_here"),
      label("return_here"),
      ret(),
      cjump("finish_turn"),
      label("finish_turn"),
      ret(),
    ];
    const result = tailCallLowering.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[1]).toMatchObject({ kind: "jump", target: "finish_turn" });
    expect(result.ops[2]).toMatchObject({ kind: "label", name: "return_here" });
  });

  it("tail-call-lowering sees tail returns through compiler labels", () => {
    const program: IrOp[] = [
      label("main"),
      call("finish_turn"),
      label("if_end"),
      ret(),
      cjump("finish_turn"),
      label("finish_turn"),
      ret(),
    ];
    const result = tailCallLowering.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[1]).toMatchObject({ kind: "jump", target: "finish_turn" });
    expect(result.ops[2]).toMatchObject({ kind: "label", name: "if_end" });
  });

  it("tail-branch-inversion inverts a branch whose then path is only a tail jump", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      cjump("else_path"),
      jump("terminal_tail"),
      label("else_path"),
      plain(0x02, "2"),
      halt(),
      label("terminal_tail"),
      halt(),
    ];
    const result = tailBranchInversion.run(program, {
      options: { ...noopOptions, tailBranchInversion: true },
    });

    expect(result.applied).toBe(1);
    expect(result.optimizations[0]?.name).toBe("tail-branch-inversion");
    expect(result.ops[1]).toMatchObject({
      kind: "cjump",
      condition: "!=0",
      target: "terminal_tail",
      opcode: 0x57,
    });
    expect(result.ops).not.toContainEqual({ kind: "label", name: "else_path" });
  });

  it("return-suffix-gadget jumps into a matching subroutine tail", () => {
    const program: IrOp[] = [
      label("first"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      ret(),
      label("second"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      ret(),
    ];
    const result = returnSuffixGadget.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.optimizations[0]?.name).toBe("return-suffix-gadget");
    expect(result.ops).toContainEqual({ kind: "label", name: "__return_suffix_gadget_0" });
    const second = result.ops.findIndex((op) => op.kind === "label" && op.name === "second");
    expect(result.ops[second + 1]).toMatchObject({
      kind: "jump",
      target: "__return_suffix_gadget_0",
    });
    expect(machineCellCount(result.ops)).toBeLessThan(machineCellCount(program));
  });

  it("return-suffix-gadget calls a return tail when the caller continues afterward", () => {
    const program: IrOp[] = [
      label("helper"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = returnSuffixGadget.run(program, ctx);

    expect(result.applied).toBe(1);
    const main = result.ops.findIndex((op) => op.kind === "label" && op.name === "main");
    expect(result.ops[main + 1]).toMatchObject({
      kind: "call",
      target: "__return_suffix_gadget_0",
    });
    expect(result.ops[main + 2]).toMatchObject({ kind: "plain", opcode: 0x03 });
    expect(machineCellCount(result.ops)).toBeLessThan(machineCellCount(program));
  });

  it("return-suffix-gadget calls into an existing tail-call body", () => {
    const program: IrOp[] = [
      label("main"),
      recall("7"),
      store("6"),
      call("inspect"),
      jump("main"),
      label("bat_jump"),
      call("random_coord"),
      store("6"),
      jump("inspect"),
      label("inspect"),
      recall("6"),
      ret(),
      label("random_coord"),
      plain(0x3b, "К СЧ"),
      ret(),
    ];
    const result = returnSuffixGadget.run(program, ctx);

    expect(result.applied).toBe(1);
    const main = result.ops.findIndex((op) => op.kind === "label" && op.name === "main");
    expect(result.ops[main + 2]).toMatchObject({
      kind: "call",
      target: "__return_suffix_gadget_0",
    });
    expect(result.ops[main + 3]).toMatchObject({ kind: "jump", target: "main" });
    expect(result.ops).toContainEqual({ kind: "label", name: "__return_suffix_gadget_0" });
    expect(machineCellCount(result.ops)).toBe(machineCellCount(program) - 1);
  });

  it("return-suffix-gadget avoids programs with absolute numeric flow targets", () => {
    const program: IrOp[] = [
      label("first"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      ret(),
      label("second"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      ret(),
      numericCall(34),
    ];
    const result = returnSuffixGadget.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("shared-terminal-tail jumps into matching straight-line tails before terminal flow", () => {
    const program: IrOp[] = [
      label("first"),
      recall("1"),
      store("2"),
      plain(0x0d, "Cx"),
      store("1"),
      indirectJump("e"),
      label("second"),
      recall("1"),
      store("2"),
      plain(0x0d, "Cx"),
      store("1"),
      indirectJump("e"),
    ];
    const result = sharedTerminalTail.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.optimizations[0]?.name).toBe("shared-terminal-tail");
    expect(result.ops).toContainEqual({ kind: "label", name: "__shared_terminal_tail_0" });
    const second = result.ops.findIndex((op) => op.kind === "label" && op.name === "second");
    expect(result.ops[second + 1]).toMatchObject({
      kind: "jump",
      target: "__shared_terminal_tail_0",
    });
    expect(machineCellCount(result.ops)).toBe(machineCellCount(program) - 3);
  });

  it("shared-terminal-tail jumps into matching stop tails before terminal flow", () => {
    const program: IrOp[] = [
      label("first"),
      plain(0x00, "0"),
      pause(),
      indirectJump("8"),
      label("second"),
      plain(0x00, "0"),
      pause(),
      indirectJump("8"),
    ];
    const result = sharedTerminalTail.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.optimizations[0]?.name).toBe("shared-terminal-tail");
    expect(result.ops).toContainEqual({ kind: "label", name: "__shared_terminal_tail_0" });
    const second = result.ops.findIndex((op) => op.kind === "label" && op.name === "second");
    expect(result.ops[second + 1]).toMatchObject({
      kind: "jump",
      target: "__shared_terminal_tail_0",
    });
    expect(machineCellCount(result.ops)).toBe(machineCellCount(program) - 1);
  });

  it("shared-terminal-tail avoids programs with absolute numeric flow targets", () => {
    const program: IrOp[] = [
      label("first"),
      recall("1"),
      store("2"),
      jump("done"),
      label("second"),
      recall("1"),
      store("2"),
      jump("done"),
      numericJump(20),
      label("done"),
      halt(),
    ];
    const result = sharedTerminalTail.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("indirect-selector-integer-part removes a redundant int after a proved fractional selector mutation", () => {
    const program: IrOp[] = [
      markedFractionalIndirectRecall("d"),
      plain(0x04, "4"),
      recall("d"),
      plain(0x34, "К [x]"),
      plain(0x11, "-"),
    ];
    const result = indirectSelectorIntegerPart.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.optimizations[0]?.name).toBe("indirect-selector-integer-part-reuse");
    expect(result.ops.map((op) => op.kind === "plain" ? op.opcode : -1)).not.toContain(0x34);
    expect(machineCellCount(result.ops)).toBe(machineCellCount(program) - 1);
  });

  it("indirect-selector-integer-part requires an explicit selector proof marker", () => {
    const program: IrOp[] = [
      indirectRecall("d"),
      plain(0x04, "4"),
      recall("d"),
      plain(0x34, "К [x]"),
      plain(0x11, "-"),
    ];
    const result = indirectSelectorIntegerPart.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("indirect-selector-integer-part clears the integer proof after overwriting the selector register", () => {
    const program: IrOp[] = [
      markedFractionalIndirectRecall("d"),
      plain(0x00, "0"),
      store("d"),
      recall("d"),
      plain(0x34, "К [x]"),
    ];
    const result = indirectSelectorIntegerPart.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("duplicate-failure-tail shares pause-only tails with matching terminal flow", () => {
    const program: IrOp[] = [
      label("first_pause"),
      pause(),
      label("first_continue"),
      indirectJump("8"),
      label("second_pause"),
      pause(),
      label("second_continue"),
      indirectJump("8"),
    ];
    const result = duplicateFailureTail.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.optimizations[0]?.name).toBe("duplicate-failure-tail-merge");
    expect(result.optimizations[0]?.detail).toContain("pause-only");
    expect(result.ops.some((op) => op.kind === "label" && op.name === "first_pause")).toBe(false);
    expect(result.ops.some((op) => op.kind === "label" && op.name === "first_continue")).toBe(false);
    expect(machineCellCount(result.ops)).toBe(machineCellCount(program) - 2);
  });

  it("shared-straight-line-helper extracts repeated non-terminal bodies", () => {
    const program: IrOp[] = [
      label("first"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      recall("4"),
      store("5"),
      plain(0x20, "F pi"),
      label("second"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      recall("4"),
      store("5"),
      plain(0x21, "F sqrt"),
      halt(),
    ];
    const result = sharedStraightLineHelper.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.optimizations[0]?.name).toBe("shared-straight-line-helper");
    expect(result.ops).toContainEqual({ kind: "label", name: "__shared_straight_line_helper_0" });
    expect(result.ops.filter((op) => op.kind === "call" && op.target === "__shared_straight_line_helper_0")).toHaveLength(2);
    expect(result.ops.at(-1)).toMatchObject({ kind: "return" });
    expect(machineCellCount(result.ops)).toBeLessThan(machineCellCount(program));
  });

  it("shared-straight-line-helper extracts repeated bodies that contain direct calls", () => {
    const program: IrOp[] = [
      label("first"),
      recall("1"),
      call("normalize"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      plain(0x20, "F pi"),
      label("second"),
      recall("1"),
      call("normalize"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      plain(0x21, "F sqrt"),
      label("normalize"),
      plain(0x34, "К [x]"),
      ret(),
    ];
    const result = sharedStraightLineHelper.run(program, {
      options: { ...noopOptions, sharedStraightLineCallBodies: true },
    });

    expect(result.applied).toBe(2);
    expect(result.ops.filter((op) => op.kind === "call" && op.target === "__shared_straight_line_helper_0")).toHaveLength(2);
    expect(result.ops).toContainEqual(expect.objectContaining({
      kind: "call",
      target: "normalize",
      opcode: 0x53,
    }));
    expect(machineCellCount(result.ops)).toBeLessThan(machineCellCount(program));
  });

  it("shared-straight-line-helper keeps direct-call bodies behind the speculative flag", () => {
    const program: IrOp[] = [
      recall("1"),
      call("normalize"),
      store("2"),
      plain(0x01, "1"),
      recall("1"),
      call("normalize"),
      store("2"),
      plain(0x02, "2"),
      label("normalize"),
      plain(0x34, "К [x]"),
      ret(),
    ];
    const result = sharedStraightLineHelper.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("shared-straight-line-helper keeps helper returns away from X2 restore boundaries", () => {
    const program: IrOp[] = [
      label("first"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      plain(0x01, "1"),
      label("second"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = sharedStraightLineHelper.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("shared-straight-line-helper adds internal entries for repeated helper suffixes", () => {
    const program: IrOp[] = [
      label("first"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      recall("4"),
      store("5"),
      plain(0x20, "F pi"),
      label("second"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      recall("4"),
      store("5"),
      plain(0x21, "F sqrt"),
      label("suffix"),
      plain(0x10, "+"),
      store("3"),
      recall("4"),
      store("5"),
      plain(0x17, "F lg"),
      halt(),
    ];
    const result = sharedStraightLineHelper.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.optimizations.some((optimization) => optimization.name === "multi-entry-straight-line-helper")).toBe(true);
    expect(result.ops.filter((op) => op.kind === "call" && op.target === "__shared_straight_line_helper_0")).toHaveLength(2);
    expect(result.ops.filter((op) => op.kind === "call" && op.target === "__shared_straight_line_helper_1")).toHaveLength(1);
    const helper = result.ops.findIndex((op) => op.kind === "label" && op.name === "__shared_straight_line_helper_0");
    const entry = result.ops.findIndex((op) => op.kind === "label" && op.name === "__shared_straight_line_helper_1");
    expect(helper).toBeGreaterThanOrEqual(0);
    expect(entry).toBeGreaterThan(helper);
    expect(machineCellCount(result.ops)).toBeLessThan(machineCellCount(program));
  });

  it("shared-straight-line-helper avoids programs with absolute numeric flow targets", () => {
    const program: IrOp[] = [
      numericJump(9),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
    ];
    const result = sharedStraightLineHelper.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("stable-indirect-flow replaces direct numeric branches when a stable selector already holds the address", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("7"),
      numericJump(12),
      halt(),
    ];
    const result = stableIndirectFlow.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.optimizations[0]?.name).toBe("stable-indirect-flow");
    expect(result.ops[3]).toMatchObject({ kind: "indirect-jump", register: "7", opcode: 0x87 });
  });

  it("stable-indirect-flow replaces numeric conditional branches with indirect conditionals", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("7"),
      numericCjump(12),
      halt(),
    ];
    const result = stableIndirectFlow.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[3]).toMatchObject({ kind: "indirect-cjump", register: "7", opcode: 0xe7 });
  });

  it("stable-indirect-flow refuses non-stable selectors because they mutate during addressing", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("4"),
      numericJump(13),
      halt(),
    ];
    const result = stableIndirectFlow.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops[3]?.kind).toBe("jump");
  });

  it("stable-indirect-flow refuses unresolved labels without a layout proof", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("7"),
      jump("late_label"),
      label("late_label"),
      halt(),
    ];
    const result = stableIndirectFlow.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops[3]?.kind).toBe("jump");
  });

  it("preloaded-indirect-flow rewrites address-stable backward numeric branches through formal alias selectors", () => {
    const program: IrOp[] = [
      ...Array.from({ length: 13 }, () => plain(0x00, "0")),
      halt(),
      numericJump(13),
    ];
    const result = preloadedIndirectFlow.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.at(-1)).toMatchObject({ kind: "indirect-jump", register: "7", opcode: 0x87 });
    expect(result.preloads).toEqual([{ register: "7", value: "C5", countsAgainstProgram: false }]);
    expect(result.optimizations[0]?.name).toBe("preloaded-indirect-flow");
  });

  it("preloaded-indirect-flow refuses forward rewrites that would shift numeric target addresses", () => {
    const program: IrOp[] = [
      numericJump(48),
      halt(),
    ];
    const result = preloadedIndirectFlow.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops[0]?.kind).toBe("jump");
  });

  it("preloaded-indirect-flow selects FA..FF only for proved one-command continuations", () => {
    const program: IrOp[] = [
      ...Array.from({ length: 48 }, () => plain(0x00, "0")),
      plain(0x09, "9"),
      numericJump(1),
      numericJump(48),
    ];
    const result = preloadedIndirectFlow.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops.at(-1)).toMatchObject({ kind: "indirect-jump", register: "8", opcode: 0x88 });
    expect(result.preloads).toEqual([
      { register: "7", value: "B3", countsAgainstProgram: false },
      { register: "8", value: "FA", countsAgainstProgram: false },
    ]);
    expect(result.optimizations.some((optimization) => optimization.name === "preloaded-super-dark-flow")).toBe(true);
  });

  it("runtime-indirect-call-flow borrows a dead stable register for repeated backward helper calls", () => {
    const program: IrOp[] = [
      plain(0x09, "9"),
      store("7"),
      label("helper"),
      plain(0x00, "0"),
      { kind: "return", opcode: 0x52, meta: { mnemonic: "В/О" } },
      call("helper"),
      call("helper"),
      call("helper"),
      call("helper"),
      call("helper"),
    ];
    const result = runtimeIndirectCallFlow.run(program, ctx);

    expect(result.applied).toBe(5);
    expect(result.optimizations[0]?.name).toBe("runtime-indirect-call-flow");
    expect(result.ops.slice(5, 7)).toEqual([
      { kind: "plain", opcode: 2, meta: { mnemonic: "2", comment: "runtime indirect call selector 2" } },
      { kind: "store", register: "7", opcode: 0x47, meta: { mnemonic: "X->П 7", comment: "runtime indirect call selector 2" } },
    ]);
    expect(result.ops.filter((op) => op.kind === "indirect-call" && op.register === "7")).toHaveLength(5);
  });

  it("runtime-indirect-call-flow refuses registers used by the helper body", () => {
    const program: IrOp[] = [
      label("helper"),
      recall("7"),
      recall("8"),
      recall("9"),
      recall("a"),
      recall("b"),
      recall("c"),
      recall("d"),
      recall("e"),
      { kind: "return", opcode: 0x52, meta: { mnemonic: "В/О" } },
      call("helper"),
      call("helper"),
      call("helper"),
      call("helper"),
      call("helper"),
    ];
    const result = runtimeIndirectCallFlow.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops.filter((op) => op.kind === "call")).toHaveLength(5);
  });

  it("stable-indirect-flow drops selector knowledge after calls", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("7"),
      { kind: "call", target: "maybe_mutates", opcode: 0x53, meta: { mnemonic: "ПП" }, targetMeta: {} },
      numericJump(12),
      label("maybe_mutates"),
      ret(),
    ];
    const result = stableIndirectFlow.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops[4]?.kind).toBe("jump");
  });

  it("indirect-memory-table drops selector knowledge after conditional control-flow splits", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      store("7"),
      cjump("maybe_mutates"),
      recall("2"),
      label("maybe_mutates"),
      halt(),
    ];
    const result = indirectMemoryTable.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops[3]).toMatchObject({ kind: "recall", register: "2" });
  });

  it("indirect-memory-table keeps the target register visible to later liveness passes", () => {
    const program: IrOp[] = [
      store("2"),
      plain(0x02, "2"),
      store("7"),
      recall("2"),
      halt(),
    ];
    const rewritten = indirectMemoryTable.run(program, ctx);
    const recallOp = rewritten.ops.find((op) => op.kind === "indirect-recall");
    const pruned = deadStoreElimination.run(rewritten.ops, ctx);

    expect(recallOp?.kind).toBe("indirect-recall");
    if (recallOp?.kind === "indirect-recall") {
      expect(recallOp.register).toBe("7");
      expect(recallOp.meta?.comment).toContain("indirect-memory-target=2");
    }
    expect(pruned.ops).toContainEqual(expect.objectContaining({ kind: "store", register: "2" }));
  });

  it("indirect-memory-table refuses display-focus-sensitive memory reads", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("7"),
      {
        kind: "recall",
        register: "2",
        opcode: 0x62,
        meta: { mnemonic: "П->X 2", comment: "display digit" },
      },
      halt(),
    ];
    const result = indirectMemoryTable.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops[3]).toMatchObject({ kind: "recall", register: "2" });
  });

  it("dead-code-after-halt prunes ops unreachable from the entry CFG", () => {
    const program: IrOp[] = [
      jump("END"),
      plain(0x00, "0"),
      plain(0x10, "+"),
      label("END"),
      halt(),
    ];
    const result = deadCodeAfterHalt.run(program, ctx);
    expect(result.applied).toBeGreaterThanOrEqual(2);
  });

  it("dead-code-after-halt keeps compiler-known indirect jump targets", () => {
    const program: IrOp[] = [
      {
        kind: "indirect-jump",
        register: "7",
        opcode: 0x87,
        meta: { mnemonic: "К БП 7", comment: "preloaded R7=C5 indirect-target=4 indirect flow" },
      },
      plain(0x09, "9"),
      plain(0x08, "8"),
      plain(0x07, "7"),
      halt(),
    ];
    const result = deadCodeAfterHalt.run(program, ctx);

    expect(result.ops.some((op) => op.kind === "stop")).toBe(true);
    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x09)).toBe(false);
  });

  it("dead-code-after-halt prunes call continuations when the callee never returns", () => {
    const program: IrOp[] = [
      { kind: "call", target: "terminal", opcode: 0x53, meta: { mnemonic: "ПП" }, targetMeta: {} },
      plain(0x09, "9"),
      label("terminal"),
      halt(),
    ];
    const result = deadCodeAfterHalt.run(program, ctx);

    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x09)).toBe(false);
    expect(result.applied).toBeGreaterThanOrEqual(1);
  });

  it("dead-code-after-halt keeps call continuations reached by return", () => {
    const program: IrOp[] = [
      { kind: "call", target: "returns", opcode: 0x53, meta: { mnemonic: "ПП" }, targetMeta: {} },
      plain(0x09, "9"),
      halt(),
      label("returns"),
      ret(),
    ];
    const result = deadCodeAfterHalt.run(program, ctx);

    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x09)).toBe(true);
  });

  it("dead-proc-elimination removes emitted procedures with no remaining references", () => {
    const program: IrOp[] = [
      { kind: "jump", target: "live", opcode: 0x51, meta: { mnemonic: "БП" }, targetMeta: {} },
      procStart("dead"),
      plain(0x09, "9"),
      ret(),
      procEnd("dead"),
      procStart("live"),
      halt(),
      procEnd("live"),
    ];
    const result = deadProcElimination.run(program, ctx);

    expect(result.optimizations.some((item) => item.name === "dead-proc-elimination")).toBe(true);
    expect(result.ops.some((op) => op.kind === "label" && op.name === "dead")).toBe(false);
    expect(result.ops.some((op) => op.kind === "label" && op.name === "live")).toBe(true);
  });

  it("dead-proc-elimination keeps procedures reached through proven indirect calls", () => {
    const program: IrOp[] = [
      {
        kind: "indirect-call",
        register: "7",
        opcode: 0xa7,
        meta: { mnemonic: "К ПП 7", comment: "preloaded R7=2 indirect-target=2 indirect flow" },
      },
      halt(),
      procStart("helper"),
      plain(0x09, "9"),
      ret(),
      procEnd("helper"),
    ];
    const result = deadProcElimination.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops.some((op) => op.kind === "label" && op.name === "helper")).toBe(true);
  });

  it("dead-proc-elimination preserves procedure fallthrough tail calls", () => {
    const program: IrOp[] = [
      { kind: "call", target: "wait", opcode: 0x53, meta: { mnemonic: "ПП" }, targetMeta: {} },
      halt(),
      procStart("wait"),
      procEnd("wait"),
      procStart("resolve"),
      plain(0x09, "9"),
      ret(),
      procEnd("resolve"),
    ];
    const result = deadProcElimination.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops.some((op) => op.kind === "label" && op.name === "resolve")).toBe(true);
  });

  it("store-recall-peephole drops the П->X immediately after X->П to the same register", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.length).toBe(2);
  });

  it("store-recall-peephole drops a stable indirect recall after a same-target indirect store", () => {
    const program: IrOp[] = [
      knownTargetIndirectStore("8", "2"),
      knownTargetIndirectRecall("8", "2"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      knownTargetIndirectStore("8", "2"),
      halt(),
    ]);
  });

  it("store-recall-peephole drops recall when value proof shows X is unchanged", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      store("6"),
      recall("1", "preload const 2"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      store("6"),
      halt(),
    ]);
  });

  it("store-recall-peephole drops structural recall when shape proof shows X is unchanged", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("6"),
      recall("2", "preload const FACE"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      store("6"),
      halt(),
    ]);
  });

  it("store-recall-peephole drops structural recall before immediate ВП across address gaps", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("6"),
      recall("2", "preload const FACE"),
      orphanAddress(54),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      store("6"),
      orphanAddress(54),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("store-recall-peephole drops structural exponent preload recall when shape proof shows X is unchanged", () => {
    const program: IrOp[] = [
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x02, "2"),
      store("6"),
      recall("2", "preload const ГE-2"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x02, "2"),
      store("6"),
      halt(),
    ]);
  });

  it("store-recall-peephole keeps mutating indirect store/recall pairs", () => {
    const program: IrOp[] = [
      knownTargetIndirectStore("4", "2"),
      knownTargetIndirectRecall("4", "2"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("store-recall-peephole keeps recall that syncs X2 before ВП", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("store-recall-peephole keeps recall that syncs X2 before sign-change", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("store-recall-peephole drops recall before sign ВП when the sign source already matches", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      recall("2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("store-recall-peephole drops structural recall before sign ВП when the sign source already matches", () => {
    const program: IrOp[] = [
      recall("1", "preload const FACE"),
      store("2"),
      recall("2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const FACE"),
      store("2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("store-recall-peephole drops recall before sign ВП through a transparent return helper", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      recall("2"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("store-recall-peephole keeps recall through X2-preserving ops before ВП", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("store-recall-peephole drops redundant X2 sync through preserving ops before ВП", () => {
    const program: IrOp[] = [
      recall("2"),
      store("2"),
      recall("2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("2"),
      store("2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("store-recall-peephole uses value X2 aliases from literal stores before ВП gaps", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      recall("2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      plain(0x20, "F pi"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("store-recall-peephole keeps value-backed X2 sync before immediate ВП context", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("2"),
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("store-recall-peephole keeps redundant X2 sync before immediate ВП context", () => {
    const program: IrOp[] = [
      recall("2"),
      store("2"),
      recall("2"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("store-recall-peephole drops recall before fallthrough ВП after a direct conditional X2 sync", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      cjump("skip"),
      plain(0x0c, "ВП"),
      halt(),
      label("skip"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).not.toContainEqual(recall("2"));
  });

  it("store-recall-peephole drops recall before fallthrough ВП after a counted-loop X2 sync", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      loop("skip"),
      plain(0x0c, "ВП"),
      halt(),
      label("skip"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).not.toContainEqual(recall("2"));
  });

  it("store-recall-peephole keeps loop-counter recalls before counted-loop target ВП", () => {
    const program: IrOp[] = [
      store("0"),
      recall("0"),
      loop("restore"),
      halt(),
      label("restore"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("store-recall-peephole keeps recall before jump-target ВП after a direct conditional", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      cjump("restore"),
      halt(),
      label("restore"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("store-recall-peephole keeps recall that lifts the stack for an immediate binary op", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("store-recall-peephole keeps recall whose stack lift reaches a later binary op", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      plain(0x35, "К {x}"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("store-recall-peephole keeps recall whose stack lift reaches a binary op after a direct call returns", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      call("frac"),
      plain(0x10, "+"),
      halt(),
      label("frac"),
      plain(0x35, "К {x}"),
      ret(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("store-recall-peephole drops recall before a direct call return that syncs X2 before ВП", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      call("noop"),
      plain(0x0c, "ВП"),
      halt(),
      label("noop"),
      ret(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      store("2"),
      call("noop"),
      plain(0x0c, "ВП"),
      halt(),
      label("noop"),
      ret(),
    ]);
  });

  it("store-recall-peephole drops recall before a known indirect call return that syncs X2 before ВП", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      knownTargetIndirectCall("7", 5),
      plain(0x0c, "ВП"),
      halt(),
      label("noop"),
      ret(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      store("2"),
      knownTargetIndirectCall("7", 5),
      plain(0x0c, "ВП"),
      halt(),
      label("noop"),
      ret(),
    ]);
  });

  it("store-recall-peephole still drops recall before a fresh digit entry", () => {
    const program: IrOp[] = [
      store("2"),
      recall("2"),
      plain(0x01, "1"),
      halt(),
    ];
    const result = storeRecallPeephole.run(program, ctx);

    expect(result.applied).toBe(1);
  });

  it("liveness propagates uses backwards through a loop", () => {
    const program: IrOp[] = [
      label("loop"),
      recall("3"),
      plain(0x10, "+"),
      jump("loop"),
    ];
    const info = computeLiveness(program);
    expect(info.liveIn[1]!.has("3")).toBe(true);
    expect(info.liveOut[3]!.has("3")).toBe(true);
  });

  it("liveness does not propagate through a direct call continuation unless the callee returns", () => {
    const program: IrOp[] = [
      call("terminal"),
      recall("3"),
      label("terminal"),
      halt(),
    ];
    const info = computeLiveness(program);

    expect(info.liveOut[0]!.has("3")).toBe(false);
  });

  it("liveness propagates direct call continuations through return", () => {
    const program: IrOp[] = [
      call("returns"),
      recall("3"),
      halt(),
      label("returns"),
      ret(),
    ];
    const info = computeLiveness(program);

    expect(info.liveOut[0]!.has("3")).toBe(true);
  });

  it("passes round-trip through MachineItem lowering after rewriting", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      jump("END"),
      label("END"),
      halt(),
    ];
    const after = jumpToNextThreading.run(
      storeRecallPeephole.run(program, ctx).ops,
      ctx,
    );
    const items = lowerIrToMachine(after.ops);
    const stores = items.filter((item) => item.kind === "op" && item.opcode === 0x41);
    expect(stores.length).toBe(1);
    const jumps = items.filter((item) => item.kind === "op" && item.opcode === 0x51);
    expect(jumps.length).toBe(0);
  });

  it("register-coalesce rewrites non-overlapping direct register live ranges", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      halt(),
      store("2"),
      recall("2"),
    ];
    const result = registerCoalesce.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.some((op) => (op.kind === "store" || op.kind === "recall") && op.register === "2")).toBe(false);
  });

  it("def-aware mapping refuses a merge that a dead store would clobber", () => {
    // R1 = read; R2 = R1; R1 = 0 (dead store, R1 never read again) while R2 is
    // still live; halt reads R2. Plain liveness misses that the dead store still
    // physically overwrites R1, so R1 and R2 look non-overlapping. Def-aware mode
    // counts the store as occupying R1, exposing the interference. This is the
    // soundness guard for reclaiming coalesce-freed registers through allocation,
    // where the dead source statement is re-lowered rather than removed.
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      store("2"),
      store("1"),
      recall("2"),
      halt(),
    ];
    expect(computeNonOverlappingRegisterMapping(program).size).toBe(1);
    expect(computeNonOverlappingRegisterMapping(program, { defAware: true }).size).toBe(0);
  });

  it("register-coalesce refuses registers live at entry", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      recall("2"),
    ];
    const result = registerCoalesce.run(program, ctx);
    expect(result.applied).toBe(0);
  });

  it("register-coalesce refuses display-focus-sensitive direct register access", () => {
    const program: IrOp[] = [
      store("1"),
      recall("1"),
      halt(),
      {
        kind: "store",
        register: "2",
        opcode: 0x42,
        meta: { mnemonic: "X->П 2", comment: "display rendered digit" },
      },
      {
        kind: "recall",
        register: "2",
        opcode: 0x62,
        meta: { mnemonic: "П->X 2", comment: "display rendered digit" },
      },
    ];
    const result = registerCoalesce.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops.at(-1)).toMatchObject({ kind: "recall", register: "2" });
  });

  it("arithmetic-if-pass collapses byte-identical simplified branches", () => {
    const program: IrOp[] = [
      cjump("else"),
      plain(0x01, "1"),
      halt(),
      jump("end"),
      label("else"),
      plain(0x01, "1"),
      halt(),
      label("end"),
    ];
    const result = arithmeticIfPass.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.find((op) => op.kind === "cjump")).toBeUndefined();
    expect(result.ops.filter((op) => op.kind === "stop").length).toBe(1);
  });

  it("arithmetic-if-pass refuses branches whose effects differ", () => {
    const program: IrOp[] = [
      cjump("else"),
      plain(0x01, "1"),
      halt(),
      jump("end"),
      label("else"),
      plain(0x02, "2"),
      halt(),
      label("end"),
    ];
    const result = arithmeticIfPass.run(program, ctx);
    expect(result.applied).toBe(0);
  });

  it("r0-fractional-sentinel removes redundant direct R3 recall after indirect R0 recall", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      indirectRecall("0"),
      recall("3"),
    ];
    const result = r0FractionalSentinel.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.at(-1)?.kind).toBe("indirect-recall");
  });

  it("r0-fractional-sentinel recognizes multi-digit fractional R0 literals", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("0"),
      indirectRecall("0"),
      recall("3"),
    ];
    const result = r0FractionalSentinel.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.at(-1)?.kind).toBe("indirect-recall");
  });

  it("r0-fractional-sentinel recognizes explicit leading-dot R0 literals", () => {
    const program: IrOp[] = [
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      indirectRecall("0"),
      recall("3"),
    ];
    const result = r0FractionalSentinel.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.at(-1)?.kind).toBe("indirect-recall");
  });

  it("r0-fractional-sentinel refuses when R0 is live after the indirect access", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      indirectRecall("0"),
      recall("3"),
      recall("0"),
    ];
    const result = r0FractionalSentinel.run(program, ctx);
    expect(result.applied).toBe(0);
  });

  it("r0-fractional-sentinel removes redundant direct R3 store after indirect R0 store", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      indirectStore("0"),
      store("3"),
    ];
    const result = r0FractionalSentinel.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.at(-1)?.kind).toBe("indirect-store");
  });

  it("r0-fractional-sentinel preserves fractional R0 through stores to other registers", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      store("1"),
      indirectRecall("0"),
      recall("3"),
    ];
    const result = r0FractionalSentinel.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.at(-1)?.kind).toBe("indirect-recall");
  });

  it("r0-fractional-sentinel preserves fractional R0 through unrelated indirect memory", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      indirectRecall("7"),
      numericJump(99),
    ];
    const result = r0FractionalSentinel.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.at(-1)).toMatchObject({
      kind: "indirect-jump",
      register: "0",
      opcode: 0x80,
    });
  });

  it("r0-fractional-sentinel treats unrelated indirect flow as an R0 proof barrier", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      indirectJump("7"),
      numericJump(99),
    ];
    const result = r0FractionalSentinel.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("r0-fractional-sentinel removes redundant R0 sentinel store after fractional R0 indirect recall", () => {
    const sentinelRecall: IrOp = {
      ...recall("e"),
      meta: { mnemonic: "П->X e", comment: "preload const -99999999" },
    };
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      indirectRecall("0"),
      sentinelRecall,
      store("0"),
      recall("0"),
    ];
    const result = r0FractionalSentinel.run(program, ctx);
    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([program[0], program[1], program[2], program[3], program[4], program[5]]);
  });

  it("r0-fractional-sentinel recognizes a direct -99999999 sentinel literal", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      indirectStore("0"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x0b, "/-/"),
      store("0"),
    ];
    const result = r0FractionalSentinel.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.at(-1)?.kind).toBe("plain");
    expect(result.ops.at(-1)).toMatchObject({ opcode: 0x0b });
  });

  it("r0-fractional-sentinel removes a redundant R0 sentinel recall", () => {
    const sentinelRecall: IrOp = {
      ...recall("e"),
      meta: { mnemonic: "П->X e", comment: "preload const -99999999" },
    };
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      indirectStore("0"),
      sentinelRecall,
      recall("0"),
    ];
    const result = r0FractionalSentinel.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([program[0], program[1], program[2], program[3], program[4], program[5]]);
  });

  it("r0-fractional-sentinel rewrites a direct jump to 99 through fractional R0 when R0 is dead", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      numericJump(99),
    ];
    const result = r0FractionalSentinel.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.at(-1)).toMatchObject({
      kind: "indirect-jump",
      register: "0",
      opcode: 0x80,
    });
    expect(machineCellCount(result.ops)).toBe(machineCellCount(program) - 1);
  });

  it("r0-fractional-sentinel keeps a direct jump to 99 when R0 is live at the target", () => {
    const padding = Array.from({ length: 93 }, () => plain(0x54, "КНОП"));
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      numericJump(99),
      ...padding,
      recall("0"),
    ];
    const result = r0FractionalSentinel.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("r0-fractional-sentinel rewrites a direct conditional jump to 99 through fractional R0 when R0 is dead", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      numericCjump(99),
      halt(),
    ];
    const result = r0FractionalSentinel.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[4]).toMatchObject({
      kind: "indirect-cjump",
      condition: "==0",
      register: "0",
      opcode: 0xe0,
    });
    expect(machineCellCount(result.ops)).toBe(machineCellCount(program) - 1);
  });

  it("r0-fractional-sentinel keeps a direct conditional jump to 99 when R0 is live after it", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      numericCjump(99),
      recall("0"),
    ];
    const result = r0FractionalSentinel.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("r0-fractional-sentinel rewrites a direct call to 99 through fractional R0 when R0 is dead", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      numericCall(99),
      halt(),
      ...Array.from({ length: 92 }, () => plain(0x54, "КНОП")),
      ret(),
    ];
    const result = r0FractionalSentinel.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[4]).toMatchObject({
      kind: "indirect-call",
      register: "0",
      opcode: 0xa0,
    });
    expect(machineCellCount(result.ops)).toBe(machineCellCount(program) - 1);
  });

  it("r0-fractional-sentinel refuses non-sentinel R0 stores", () => {
    const otherRecall: IrOp = {
      ...recall("e"),
      meta: { mnemonic: "П->X e", comment: "preload const -123" },
    };
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      store("0"),
      indirectRecall("0"),
      otherRecall,
      store("0"),
    ];
    const result = r0FractionalSentinel.run(program, ctx);
    expect(result.applied).toBe(0);
  });

  it("indirect-memory-table rewrites direct recall through an existing stable selector", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      store("7"),
      recall("2"),
      halt(),
    ];
    const result = indirectMemoryTable.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.optimizations[0]?.name).toBe("indirect-memory-table");
    expect(result.ops[2]).toMatchObject({ kind: "indirect-recall", register: "7", opcode: 0xd7 });
  });

  it("indirect-memory-table uses the two-digit register-target aliases", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x03, "3"),
      store("7"),
      recall("d"),
      halt(),
    ];
    const result = indirectMemoryTable.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[3]).toMatchObject({ kind: "indirect-recall", register: "7", opcode: 0xd7 });
  });

  it("indirect-memory-table rewrites direct store through an existing stable selector", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      store("8"),
      plain(0x09, "9"),
      store("2"),
      halt(),
    ];
    const result = indirectMemoryTable.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops[3]).toMatchObject({ kind: "indirect-store", register: "8", opcode: 0xb8 });
  });

  it("vp-splice removes adjacent exponent sign toggles", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes exponent sign toggles after closed decimal X2-sync ВП", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes exponent sign toggles after a recalled decimal register ВП", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes exponent sign toggles after decimal first-digit ВП splice", () => {
    const program: IrOp[] = [
      recall("1", "preload const 3"),
      recall("2", "preload const 800"),
      plain(0x14, "←→"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      recall("1", "preload const 3"),
      recall("2", "preload const 800"),
      plain(0x14, "←→"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes exponent sign toggles after structural X2-sync ВП", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes exponent sign toggles after dot-restored structural X2 ВП", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("2", "preload const FACE"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes exponent sign toggles after dot-restored structural exponent source", () => {
    const program: IrOp[] = [
      recall("2", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("2", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes exponent sign toggles after conditional fallthrough X2-sync ВП", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      cjump("done"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      cjump("done"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("vp-splice removes non-zero sign pairs after normalized structural arithmetic syncs", () => {
    const conditionalProgram: IrOp[] = [
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      cjump("done"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    ];
    const returnProgram: IrOp[] = [
      label("main"),
      call("load"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("load"),
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      ret(),
    ];

    expect(vpSplice.run(conditionalProgram, ctx).ops).toEqual([
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      cjump("done"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    ]);
    expect(vpSplice.run(returnProgram, ctx).ops).toEqual([
      label("main"),
      call("load"),
      plain(0x0c, "ВП"),
      halt(),
      label("load"),
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      ret(),
    ]);
  });

  it("vp-splice keeps zero sign pairs after normalized structural arithmetic syncs", () => {
    const program: IrOp[] = [
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      cjump("done"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice removes a structural sign pair before ВП after conditional fallthrough X2 sync", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      store("1"),
      cjump("done"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("2", "preload const 8.70Е2-6С"),
      store("1"),
      cjump("done"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("vp-splice removes a structural sign pair before ВП after loop fallthrough X2 sync", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      store("1"),
      loop("done"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("2", "preload const 8.70Е2-6С"),
      store("1"),
      loop("done"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    ]);
  });

  it("vp-splice removes a structural sign pair before ВП after direct return X2 sync", () => {
    const program: IrOp[] = [
      label("main"),
      call("load"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("load"),
      recall("2", "preload const 8.70Е2-6С"),
      ret(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      label("main"),
      call("load"),
      plain(0x0c, "ВП"),
      halt(),
      label("load"),
      recall("2", "preload const 8.70Е2-6С"),
      ret(),
    ]);
  });

  it("vp-splice removes a structural sign pair before ВП after known indirect return X2 sync", () => {
    const program: IrOp[] = [
      label("main"),
      knownTargetIndirectCall("7", 5),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("load"),
      recall("2", "preload const 8.70Е2-6С"),
      ret(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      label("main"),
      knownTargetIndirectCall("7", 5),
      plain(0x0c, "ВП"),
      halt(),
      label("load"),
      recall("2", "preload const 8.70Е2-6С"),
      ret(),
    ]);
  });

  it("vp-splice removes a structural sign pair before transparent return helpers and ВП", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("2", "preload const 8.70Е2-6С"),
      call("transparent"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("vp-splice removes an open mantissa sign pair before transparent return helpers and ВП", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes a mixed structural restore run before transparent return helpers and ВП", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x55, "К1"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("2", "preload const 8.70Е2-6С"),
      call("transparent"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("vp-splice removes exponent sign toggles after store-backed decimal ВП splice", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes exponent sign toggles after indirect-store-backed decimal ВП splice", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes exponent sign toggles after store-backed structural ВП splice", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("2", "preload const FACE"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes exponent sign toggles after indirect-store-backed structural ВП splice", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("2", "preload const FACE"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice keeps closed sign toggles before store-backed ВП when they change the source", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice keeps closed sign toggles before indirect-store-backed ВП when they change the source", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      knownTargetIndirectStore("7", "1"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice removes a full empty run before ВП", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      plain(0x56, "К2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice keeps display-sensitive empty cells before ВП", () => {
    const displayEmpty: IrOp = {
      kind: "plain",
      opcode: 0x54,
      meta: { mnemonic: "КНОП", comment: "display spacer" },
    };
    const program: IrOp[] = [
      plain(0x02, "2"),
      displayEmpty,
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice removes adjacent ВП only after active number-entry ВП", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice keeps adjacent ВП after closed-context X2 restore", () => {
    const program: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x0c, "ВП"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice removes an empty run plus redundant ВП in one segment", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice keeps an empty run before adjacent ВП after closed-context X2 restore", () => {
    const program: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x0c, "ВП"),
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x0d, "Cx"),
      plain(0x0c, "ВП"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("vp-splice removes an empty run before ВП across marker labels", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x54, "КНОП"),
      label("marker"),
      plain(0x55, "К1"),
      label("entry"),
      plain(0x56, "К2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      label("marker"),
      label("entry"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes an empty run before ВП across orphan address gaps", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x54, "КНОП"),
      orphanAddress(54),
      plain(0x55, "К1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      orphanAddress(54),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes an empty run before transparent return helpers and ВП", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes an empty run before known indirect return helpers and ВП", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      knownTargetIndirectCall("7", 2),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      knownTargetIndirectCall("7", 2),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes an empty run before nested transparent return helpers and ВП", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("transparent"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      call("outer"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("transparent"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      call("outer"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice keeps an empty run before return helpers that restore X2", () => {
    const program: IrOp[] = [
      jump("main"),
      label("restore"),
      plain(0x0a, "."),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      call("restore"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice keeps an empty run before nested return helpers that restore X2", () => {
    const program: IrOp[] = [
      jump("main"),
      label("restore"),
      plain(0x0a, "."),
      ret(),
      label("outer"),
      call("restore"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      call("outer"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice removes an empty separator after an exponent digit before a non-digit command", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      halt(),
    ]);
  });

  it("vp-splice removes an empty separator after a decimal display-shape VP source exponent digit", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      halt(),
    ]);
  });

  it("vp-splice keeps an empty separator before a following exponent digit", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice keeps an empty separator before a labeled exponent digit", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      label("digit"),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice removes an empty separator before a labeled non-digit command", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      label("close"),
      plain(0x20, "Fπ"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      label("close"),
      plain(0x20, "Fπ"),
      halt(),
    ]);
  });

  it("vp-splice removes an empty separator before an orphan address gap and non-digit command", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      orphanAddress(54),
      plain(0x20, "Fπ"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      orphanAddress(54),
      plain(0x20, "Fπ"),
      halt(),
    ]);
  });

  it("vp-splice keeps an empty separator before an orphan address gap and exponent digit", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      orphanAddress(54),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice removes an empty separator before VP-context sign-change after preserving gaps", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      halt(),
    ]);
  });

  it("vp-splice removes a VP-context sign pair before fresh digit entry", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x04, "4"),
      halt(),
    ]);
  });

  it("vp-splice removes a VP-context sign pair and empty op before fresh digit entry", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x04, "4"),
      halt(),
    ]);
  });

  it("vp-splice removes a single VP-context sign before fresh digit entry", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x04, "4"),
      halt(),
    ]);
  });

  it("vp-splice removes a single VP-context sign and empty op before fresh digit entry", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x04, "4"),
      halt(),
    ]);
  });

  it("vp-splice removes a VP-context empty run before fresh digit entry", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x04, "4"),
      halt(),
    ]);
  });

  it("vp-splice preserves labels while removing a VP-context restore run before fresh digit entry", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      label("entry"),
      plain(0x0b, "/-/"),
      plain(0x55, "К1"),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      label("entry"),
      plain(0x04, "4"),
      halt(),
    ]);
  });

  it("vp-splice removes VP-context restore runs before orphan address gaps and fresh digit entry", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      orphanAddress(54),
      plain(0x55, "К1"),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      orphanAddress(54),
      plain(0x04, "4"),
      halt(),
    ]);
  });

  it("vp-splice removes VP-context restore runs before transparent direct-return helpers and fresh digit entry", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      call("transparent"),
      plain(0x04, "4"),
      halt(),
    ]);
  });

  it("vp-splice removes VP-context restore runs before proved indirect-return helpers and fresh digit entry", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      knownTargetIndirectCall("7", 2),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      knownTargetIndirectCall("7", 2),
      plain(0x04, "4"),
      halt(),
    ]);
  });

  it("vp-splice keeps VP-context restore runs before direct-return helpers that observe X", () => {
    const program: IrOp[] = [
      jump("main"),
      label("observer"),
      store("1"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      call("observer"),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice keeps an active exponent sign before an exponent digit", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x04, "4"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice keeps a VP-context sign pair when its X2 restore is observable", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice keeps a single VP-context sign when its X2 restore is observable", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice removes a VP-context sign before a dead X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("vp-splice removes a VP-context sign pair before a dead X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("vp-splice removes a mixed VP-context restore run before a dead X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("vp-splice removes an active exponent restore run before a dead X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("vp-splice removes VP-context empty separators before a dead X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("vp-splice removes VP-context empty separators before transparent direct-return helpers and dead overwrite", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x55, "К1"),
      call("transparent"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      call("transparent"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("vp-splice removes VP-context restore runs before orphan address gaps and dead overwrite", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      orphanAddress(54),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      orphanAddress(54),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("vp-splice removes VP-context empty separators before known indirect-return helpers and dead overwrite", () => {
    const program: IrOp[] = [
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x55, "К1"),
      knownTargetIndirectCall("7", 2),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      knownTargetIndirectCall("7", 2),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("vp-splice keeps VP-context empty separators before helpers that restore X2", () => {
    const program: IrOp[] = [
      jump("main"),
      label("restore"),
      plain(0x0a, "."),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x55, "К1"),
      call("restore"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice keeps VP-context empty separators before helpers that store X", () => {
    const program: IrOp[] = [
      jump("main"),
      label("store_x"),
      store("2"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x55, "К1"),
      call("store_x"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice removes an active exponent empty separator before a dead X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      plain(0x56, "К2"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("vp-splice preserves labels while removing proved VP-context empty separators", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      label("entry"),
      plain(0x56, "К2"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      label("entry"),
      plain(0x0d, "Cx"),
      halt(),
    ]);
  });

  it("vp-splice keeps a no-digit VP-context separator but removes a following sign before fresh digit", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes closed-context restore runs before fresh digit entry", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice keeps a closed-context sign before a following ВП", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice removes a closed-context decimal sign pair", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("vp-splice removes a closed-context register-valued sign pair", () => {
    const program: IrOp[] = [
      recall("1"),
      store("2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("1"),
      store("2"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("vp-splice removes a closed-context opaque sign pair", () => {
    const program: IrOp[] = [
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      halt(),
    ]);
  });

  it("vp-splice removes a closed-context structural shape sign pair", () => {
    const shapedSign: IrOp = {
      kind: "plain",
      opcode: 0x0b,
      meta: { mnemonic: "/-/", roles: ["display-byte"] },
    };
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      shapedSign,
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("2", "preload const 8.70Е2-6С"),
      shapedSign,
      halt(),
    ]);
  });

  it("vp-splice removes a closed-context structural exponent sign pair", () => {
    const program: IrOp[] = [
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("vp-splice removes a structural sign pair before proved structural ВП entry", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("vp-splice removes a mixed structural restore run before proved structural ВП entry", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0c, "ВП"),
      halt(),
    ]);
  });

  it("vp-splice removes a non-zero closed-context sign pair before proved ВП", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes a non-zero open mantissa sign pair before proved ВП", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes a mixed open mantissa restore run before proved ВП", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice removes a mixed open mantissa restore run before proved ВП across orphan address gaps", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      orphanAddress(54),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(3);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x02, "2"),
      orphanAddress(54),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-splice keeps display-sensitive signs in open mantissa restore runs", () => {
    const displaySign: IrOp = {
      kind: "plain",
      opcode: 0x0b,
      meta: { mnemonic: "/-/", comment: "display sign" },
    };
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      displaySign,
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice keeps a closed-context sign pair when it shapes a following ВП", () => {
    const program: IrOp[] = [
      plain(0x0d, "Cx"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice keeps a zero mantissa sign pair before ВП because signed zero is sticky", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice keeps a mixed zero mantissa restore run before ВП because signed zero is sticky", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-splice keeps adjacent mantissa sign toggles before a following digit", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole removes fractional op already supplied by a display ВП boundary", () => {
    const program: IrOp[] = [
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "display X2 boundary" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "display frac" } },
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x35)).toBe(false);
  });

  it("vp-x2-peephole removes fractional op already supplied by an ordinary X2 ВП boundary", () => {
    const program: IrOp[] = [
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after X2 restore" } },
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.optimizations[0]?.detail).toContain("ВП/X2 boundary");
    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x35)).toBe(false);
  });

  it("vp-x2-peephole keeps К {x} after unmarked ordinary opcode context", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole removes unmarked К {x} after a proved ВП/X2 boundary", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      halt(),
    ]);
  });

  it("vp-x2-peephole removes К {x} after a proved ВП/X2 boundary through empty ops", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      halt(),
    ]);
  });

  it("vp-x2-peephole keeps К {x} after a role-bearing empty op", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      { kind: "plain", opcode: 0x55, meta: { mnemonic: "К1", roles: ["display-byte"] } },
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole removes К {x} after a proved boundary through marker labels", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      plain(0x54, "КНОП"),
      label("marker"),
      plain(0x55, "К1"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      plain(0x54, "КНОП"),
      label("marker"),
      plain(0x55, "К1"),
      halt(),
    ]);
  });

  it("vp-x2-peephole removes К {x} after a proved boundary through X-preserving stores", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      store("2"),
      plain(0x0e, "В↑"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      store("2"),
      plain(0x0e, "В↑"),
      halt(),
    ]);
  });

  it("vp-x2-peephole removes a no-op К {x} for a proved closed fractional X value", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("vp-x2-peephole removes a no-op К {x} for an exact fractional display shape", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x01, "1"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x01, "1"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("vp-x2-peephole removes a no-op К [x] for an exact integer display shape", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("vp-x2-peephole keeps К [x] when integer-part would normalize the display shape", () => {
    const leadingZero: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x34, "К [x]"),
      halt(),
    ];
    const rawIntegerPart: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x34, meta: { mnemonic: "К [x]", raw: true } },
      halt(),
    ];

    expect(vpX2Peephole.run(leadingZero, ctx).applied).toBe(0);
    expect(vpX2Peephole.run(rawIntegerPart, ctx).applied).toBe(0);
  });

  it("vp-x2-peephole removes a no-op К [x] for exact closed exponent integer displays", () => {
    const positive: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      halt(),
    ];
    const negative: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      halt(),
    ];

    expect(vpX2Peephole.run(positive, ctx).ops).toEqual([
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
    expect(vpX2Peephole.run(negative, ctx).ops).toEqual([
      plain(0x01, "1"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("vp-x2-peephole removes a no-op К |x| for an exact non-negative integer display shape", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x31, "К |x|"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("vp-x2-peephole removes a no-op К |x| for an exact non-negative scientific display shape", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x13, "/"),
      plain(0x31, "К |x|"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x13, "/"),
      halt(),
    ]);
  });

  it("vp-x2-peephole keeps К |x| for negative or raw integer display shapes", () => {
    const negative: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x31, "К |x|"),
      halt(),
    ];
    const rawAbs: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      { kind: "plain", opcode: 0x31, meta: { mnemonic: "К |x|", raw: true } },
      halt(),
    ];

    expect(vpX2Peephole.run(negative, ctx).applied).toBe(0);
    expect(vpX2Peephole.run(rawAbs, ctx).applied).toBe(0);
  });

  it("vp-x2-peephole keeps no-op-looking К {x} during active fractional entry", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0x35, "К {x}"),
      plain(0x06, "6"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole keeps a fractional no-op К {x} when its X2 sync reaches dot", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole removes a fractional no-op К {x} before dot when X2 already has the same value", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("vp-x2-peephole removes a negative fractional no-op К {x} after an X2 sync", () => {
    const program: IrOp[] = [
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      halt(),
    ]);
  });

  it("vp-x2-peephole removes a repeated negative-integer fractional no-op after visible zero is proved", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ]);
  });

  it("vp-x2-peephole keeps the first negative-integer fractional op before its later signed-zero sync", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole removes a fractional no-op К {x} before dot through a preserving gap even when X2 differs", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x35, "К {x}"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    ]);
  });

  it("vp-x2-peephole keeps an immediate fractional no-op К {x} dot boundary", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole removes К {x} after a proved boundary through transparent direct-return helpers", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      store("2"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      call("noop"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("noop"),
      store("2"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      call("noop"),
      halt(),
    ]);
  });

  it("vp-x2-peephole removes К {x} after a proved boundary through nested transparent return helpers", () => {
    const program: IrOp[] = [
      jump("main"),
      label("noop"),
      store("2"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("noop"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      call("outer"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      jump("main"),
      label("noop"),
      store("2"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("noop"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      call("outer"),
      halt(),
    ]);
  });

  it("vp-x2-peephole keeps К {x} after direct-return helpers that change X", () => {
    const program: IrOp[] = [
      jump("main"),
      label("load"),
      plain(0x20, "F pi"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      call("load"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole keeps К {x} after nested return helpers that change X", () => {
    const program: IrOp[] = [
      jump("main"),
      label("load"),
      plain(0x20, "F pi"),
      ret(),
      label("outer"),
      call("load"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      call("outer"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole keeps К {x} across role-bearing X-preserving gaps", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      { kind: "plain", opcode: 0x0e, meta: { mnemonic: "В↑", roles: ["display-byte"] } },
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole keeps К {x} across referenced labels", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      jump("entry"),
      label("entry"),
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole keeps raw К {x} after a proved ВП/X2 boundary", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", raw: true } },
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole removes a marked ВП/X2 boundary reached by a direct conditional jump edge", () => {
    const program: IrOp[] = [
      recall("1"),
      cjump("target"),
      jump("end"),
      label("target"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      label("end"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x35)).toBe(false);
  });

  it("vp-x2-peephole removes a marked ВП/X2 boundary reached by a counted-loop jump edge", () => {
    const program: IrOp[] = [
      recall("1"),
      loop("target"),
      jump("end"),
      label("target"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      label("end"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x35)).toBe(false);
  });

  it("vp-x2-peephole removes a marked ВП/X2 boundary reached by a proved stable indirect jump", () => {
    const program: IrOp[] = [
      recall("1"),
      knownTargetIndirectJump("8", 3),
      halt(),
      label("target"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x35)).toBe(false);
  });

  it("vp-x2-peephole removes a marked ВП/X2 boundary reached by a proved stable indirect conditional jump edge", () => {
    const program: IrOp[] = [
      recall("1"),
      knownTargetIndirectCjump("8", 4),
      jump("end"),
      label("target"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      halt(),
      label("end"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x35)).toBe(false);
  });

  it("vp-x2-peephole keeps fraction after an unmarked direct conditional fallthrough X2 sync", () => {
    const program: IrOp[] = [
      recall("1"),
      cjump("target"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      label("target"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole keeps fraction after an unmarked counted-loop fallthrough X2 sync", () => {
    const program: IrOp[] = [
      recall("1"),
      loop("target"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      label("target"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole removes fraction after a marked indirect conditional fallthrough boundary", () => {
    const program: IrOp[] = [
      recall("1"),
      knownTargetIndirectCjump("8", 6),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "ordinary X2 restore boundary" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      jump("end"),
      label("target"),
      halt(),
      label("end"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x35)).toBe(false);
  });

  it("vp-x2-peephole keeps an unmarked ВП at a CFG join", () => {
    const program: IrOp[] = [
      recall("1"),
      cjump("target"),
      label("join"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      jump("end"),
      label("target"),
      jump("join"),
      label("end"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole keeps fraction after immediate ВП context", () => {
    const program: IrOp[] = [
      recall("1"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole keeps fraction after ВП without a proved X2 source", () => {
    const program: IrOp[] = [
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole refuses ordinary exponent ВП boundaries", () => {
    const program: IrOp[] = [
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "exponent entry" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac" } },
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);
    expect(result.applied).toBe(0);
  });

  it("redundant-prologue-elimination drops a duplicated display+halt before a jump back to its loop head", () => {
    const program: IrOp[] = [
      label("main"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      halt(),
      recall("3"),
      store("3"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      halt(),
      jump("main"),
    ];
    const result = redundantPrologueElimination.run(program, ctx);
    expect(result.applied).toBe(1);
    expect(result.ops.length).toBe(program.length - 4);
    expect(result.ops[result.ops.length - 1]!.kind).toBe("jump");
  });

  it("redundant-prologue-elimination preserves labels inside the removed command range", () => {
    const program: IrOp[] = [
      label("main"),
      recall("1"),
      halt(),
      recall("3"),
      store("3"),
      recall("1"),
      label("dispatch_end"),
      halt(),
      jump("main"),
    ];
    const result = redundantPrologueElimination.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toContainEqual(label("dispatch_end"));
  });

  it("redundant-prologue-elimination refuses to fire when the only body is the duplicate prologue", () => {
    const program: IrOp[] = [
      label("main"),
      plain(0x00, "0"),
      halt(),
      jump("main"),
    ];
    const result = redundantPrologueElimination.run(program, ctx);
    expect(result.applied).toBe(0);
    expect(result.ops.length).toBe(program.length);
  });

  it("redundant-prologue-elimination refuses when prologues differ on a recall", () => {
    const program: IrOp[] = [
      label("main"),
      recall("1"),
      halt(),
      recall("3"),
      store("3"),
      recall("2"),
      halt(),
      jump("main"),
    ];
    const result = redundantPrologueElimination.run(program, ctx);
    expect(result.applied).toBe(0);
  });

  it("return acts as a true terminator that breaks fall-through analysis", () => {
    const program: IrOp[] = [
      ret(),
      plain(0x00, "0"),
      plain(0x10, "+"),
      halt(),
    ];
    const result = deadCodeAfterHalt.run(program, ctx);
    expect(result.applied).toBeGreaterThanOrEqual(2);
  });
});
