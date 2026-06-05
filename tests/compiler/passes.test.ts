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
  computeX2ImmediateSyncStates,
  computeX2RegisterStates,
  computeX2ValueStates,
  parseX2ShapeFact,
  recallValueProof,
  x2ShapeDataModelForFact,
  x2ShapeSetsHaveSameDotSafeDecimal,
  x2ShapeSetsHaveSameStructuralShape,
  x2ShapeSetSafety,
  type X2ShapeSet,
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

function plain(opcode: number, mnemonic: string): IrOp {
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

function x2VpEntryShapeText(state: ReturnType<typeof computeX2ValueStates>[number]): string[] | undefined {
  return state === undefined ? undefined : x2ShapeStateText(state.vpEntryShape);
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
      "mantissa:810200880000000:decimal",
    ]);
    expect(x2ShapeStateText(states[1]?.x2Shape)).toEqual([
      "mantissa:810200880000000:decimal",
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

  it("x2 value dataflow keeps closed super sign-change shape-only", () => {
    const program: IrOp[] = [
      recall("2", "preload const FA"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[2]?.x)).toEqual([]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual([]);
    expect(x2ShapeStateText(states[2]?.xShape)).toEqual(["super:-FA"]);
    expect(x2ShapeStateText(states[2]?.x2Shape)).toEqual(["super:-FA"]);
  });

  it("x2 value dataflow keeps closed hex sign-change shape-only", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[2]?.x)).toEqual([]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual([]);
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

  it("x2 value dataflow keeps recalled stored hex sign-change shape-only", () => {
    const program: IrOp[] = [
      recall("2", "preload const 8.70Е2-6С"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      plain(0x0b, "/-/"),
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[5]?.x)).toEqual([]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual([]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex:-8.70Е2-6С:mantissa"]);
    expect(x2ShapeStateText(states[5]?.x2Shape)).toEqual(["hex:-8.70Е2-6С:mantissa"]);
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

  it("x2 shape algebra classifies decimal, exponent, hex, and super facts", () => {
    expect(parseX2ShapeFact("mantissa:02:decimal")).toEqual({
      kind: "decimal-mantissa",
      raw: "02",
      normalized: "2",
      safety: "dotSafeDecimal",
    });
    expect(parseX2ShapeFact("exponent:5::decimal")).toEqual({
      kind: "decimal-exponent",
      mantissa: "5",
      exponent: "",
      normalized: undefined,
      safety: "errorProne",
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
    expect(parseX2ShapeFact("super:FA")).toEqual({
      kind: "super-mantissa",
      raw: "FA",
      safety: "structuralOnly",
    });
    expect(parseX2ShapeFact("super-exponent:FA:3")).toEqual({
      kind: "super-exponent",
      mantissa: "FA",
      exponent: "3",
      safety: "structuralOnly",
    });
  });

  it("x2 shape algebra keeps leading-zero and structural shapes out of no-op equality", () => {
    expect(x2ShapeSetSafety(new Set(["mantissa:2:decimal"]))).toBe("dotSafeDecimal");
    expect(x2ShapeSetSafety(new Set(["hex:FABC:mantissa"]))).toBe("structuralOnly");
    expect(x2ShapeSetSafety(new Set(["exponent:5::decimal"]))).toBe("errorProne");
    expect(
      x2ShapeSetsHaveSameDotSafeDecimal(
        new Set(["mantissa:2:decimal"]),
        new Set(["mantissa:2:decimal"]),
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
        new Set(["super:FA"]),
        new Set(["hex:FA:mantissa"]),
      ),
    ).toBe(false);
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
    expect(x2ShapeDataModelForFact("super:FA")).toMatchObject({
      kind: "mantissa",
      radix: "super",
      raw: "FA",
      digits: ["F", "A"],
      safety: "structuralOnly",
    });
    expect(x2ShapeDataModelForFact("exponent:5:3:decimal")).toMatchObject({
      kind: "exponent-entry",
      exponentRaw: "3",
      exponentDigits: ["3"],
      normalizedDecimal: "5000",
      safety: "errorProne",
    });
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

  it("x2 value dataflow restores X from hidden X2 through dot", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      store("2"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2ValueStateText(states[2]?.x)).toEqual([]);
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

    expect(x2ValueStateText(states[3]?.x)).toEqual([]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["decimal:02:unnormalized"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["decimal:2:normalized"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["decimal:02:unnormalized"]);
  });

  it("x2 value dataflow keeps dot-restored leading-zero decimals out of VP-entry sources", () => {
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
    expect(x2EntryStateText(states[5])).toBe("unknown");
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

    expect(x2ValueStateText(states[1]?.x)).toEqual([]);
    expect(x2ValueStateText(states[1]?.x2)).toEqual([]);
    expect(x2ValueStateText(states[2]?.x)).toEqual(["expr:1"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["expr:1"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["expr:1", "reg:2"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["expr:1", "reg:2"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["expr:1", "reg:2"]);
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

    expect(x2ValueStateText(states[2]?.x)).toEqual(["expr:1"]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual(["expr:1"]);
    expect(x2ValueStateText(states[3]?.x)).toEqual(["same:unknown"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["same:unknown"]);
    expect(x2ValueStateText(states[4]?.x)).toEqual(["reg:2", "same:unknown"]);
    expect(x2ValueStateText(states[4]?.x2)).toEqual(["reg:2", "same:unknown"]);
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

    expect(x2ValueStateText(states[2]?.x)).toEqual([]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual([]);
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

    expect(x2VpEntryShapeText(states[2])).toEqual([]);
    expect(x2VpEntryShapeText(states[3])).toEqual(["hex:8.70Е2-6С:mantissa"]);
    expect(x2VpEntryShapeText(states[5])).toEqual([]);
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

    expect(x2VpEntryShapeText(states[2])).toEqual([]);
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
      halt(),
    ];
    const states = computeX2ValueStates(program, { trackRegisterMemory: true });

    expect(x2ValueStateText(states[2]?.x)).toEqual([]);
    expect(x2ValueStateText(states[2]?.x2)).toEqual([]);
    expect(x2ShapeStateText(states[2]?.xShape)).toEqual(["hex-exponent:FACE:"]);
    expect(x2ShapeStateText(states[3]?.xShape)).toEqual(["hex-exponent:FACE:-"]);
    expect(x2ShapeStateText(states[4]?.xShape)).toEqual(["hex-exponent:FACE:"]);
    expect(x2ShapeStateText(states[5]?.xShape)).toEqual(["hex-exponent:FACE:3"]);
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

  it("x2 value dataflow does not infer a ВП entry source on a conditional jump edge", () => {
    const program: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      cjump("target"),
      halt(),
      label("target"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const states = computeX2ValueStates(program);

    expect(x2EntryStateText(states[5])).toBe("closed");
    expect(x2EntryStateText(states[6])).toBe("unknown");
  });

  it("x2 value dataflow carries a closed decimal ВП sync only through empty op gaps", () => {
    const throughEmpty: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];
    const afterStore: IrOp[] = [
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    ];

    expect(x2EntryStateText(computeX2ValueStates(throughEmpty)[4])).toBe("exponent:2:");
    expect(x2EntryStateText(computeX2ValueStates(afterStore)[4])).toBe("unknown");
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

    expect(x2ValueStateText(states[3]?.x)).toEqual(["expr:1", "reg:1"]);
    expect(x2ValueStateText(states[3]?.x2)).toEqual(["expr:1", "reg:1"]);
    expect(x2ValueStateText(states[5]?.x)).toEqual(["expr:1", "reg:1"]);
    expect(x2ValueStateText(states[5]?.x2)).toEqual(["expr:1", "reg:1"]);
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

  it("x2-literal-restore keeps leading-zero runs after X2 normalization", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      halt(),
    ];
    const result = x2LiteralRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
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

  it("x2-literal-restore keeps leading-zero digit runs", () => {
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

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("x2-literal-restore keeps digit runs that would change a following ВП context", () => {
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

  it("x2-noop-restore keeps dot after a leading-zero X2 literal", () => {
    const program: IrOp[] = [
      plain(0x00, "0"),
      plain(0x02, "2"),
      store("2"),
      store("3"),
      plain(0x0a, "."),
      halt(),
    ];
    const result = x2NoopRestore.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
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

  it("x2-dead-restore-before-overwrite removes dot after a preloaded decimal recall", () => {
    const program: IrOp[] = [
      recall("1", "preload const 8.1020088E14"),
      plain(0x0a, "."),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const 8.1020088E14"),
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

  it("x2-dead-restore-before-overwrite removes structural sign restore before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1", "preload const 8.70Е2-6С"),
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

  it("x2-dead-restore-before-overwrite removes structural ВП restore before hard overwrite", () => {
    const program: IrOp[] = [
      recall("1", "preload const 8.70Е2-6С"),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = x2DeadRestoreBeforeOverwrite.run(program, ctx);

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      recall("1", "preload const 8.70Е2-6С"),
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

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
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

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
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

  it("x2-noop-restore keeps dot after a signed leading-zero X2 literal", () => {
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

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
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

  it("last-x-reuse keeps structural recall before immediate ВП context", () => {
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

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
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
      stackLiftAndX2Sync: false,
    });
    expect(analyzeX2StackEffect(plain(0x0d, "Cx"))).toMatchObject({
      stackPreserves: true,
      x2Affects: true,
      hardX2OverwriteWithoutStackUse: true,
    });
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

  it("vp-splice does not infer closed decimal ВП shape through a preceding store", () => {
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

  it("vp-splice removes a VP-context sign pair before empty-op fresh digit entry", () => {
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

    expect(result.applied).toBe(2);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
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

  it("vp-splice removes a single VP-context sign before empty-op fresh digit entry", () => {
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

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      plain(0x04, "4"),
      halt(),
    ]);
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

  it("vp-splice removes an active exponent sign before a dead X2 overwrite", () => {
    const program: IrOp[] = [
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0d, "Cx"),
      halt(),
    ];
    const result = vpSplice.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x54, "КНОП"),
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

  it("vp-x2-peephole proves an ordinary ВП/X2 boundary from opcode context", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      halt(),
    ]);
  });

  it("vp-x2-peephole removes unmarked К {x} after a proved ВП/X2 boundary", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      plain(0x35, "К {x}"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops).toEqual([
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      halt(),
    ]);
  });

  it("vp-x2-peephole removes К {x} after a proved ВП/X2 boundary through empty ops", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
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
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      halt(),
    ]);
  });

  it("vp-x2-peephole keeps К {x} after a role-bearing empty op", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
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
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
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
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      plain(0x54, "КНОП"),
      label("marker"),
      plain(0x55, "К1"),
      halt(),
    ]);
  });

  it("vp-x2-peephole keeps К {x} across referenced labels", () => {
    const program: IrOp[] = [
      recall("1"),
      plain(0x20, "F pi"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
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
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", raw: true } },
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(0);
    expect(result.ops).toEqual(program);
  });

  it("vp-x2-peephole proves a ВП/X2 boundary through a direct conditional jump edge", () => {
    const program: IrOp[] = [
      recall("1"),
      cjump("target"),
      jump("end"),
      label("target"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      label("end"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x35)).toBe(false);
  });

  it("vp-x2-peephole proves a ВП/X2 boundary through a counted-loop jump edge", () => {
    const program: IrOp[] = [
      recall("1"),
      loop("target"),
      jump("end"),
      label("target"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      label("end"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x35)).toBe(false);
  });

  it("vp-x2-peephole proves a ВП/X2 boundary through a proved stable indirect jump", () => {
    const program: IrOp[] = [
      recall("1"),
      knownTargetIndirectJump("8", 3),
      halt(),
      label("target"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x35)).toBe(false);
  });

  it("vp-x2-peephole proves a ВП/X2 boundary through a proved stable indirect conditional jump edge", () => {
    const program: IrOp[] = [
      recall("1"),
      knownTargetIndirectCjump("8", 4),
      jump("end"),
      label("target"),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
      { kind: "plain", opcode: 0x35, meta: { mnemonic: "К {x}", comment: "frac after restore" } },
      halt(),
      label("end"),
      halt(),
    ];
    const result = vpX2Peephole.run(program, ctx);

    expect(result.applied).toBe(1);
    expect(result.ops.some((op) => op.kind === "plain" && op.opcode === 0x35)).toBe(false);
  });

  it("vp-x2-peephole keeps fraction after a direct conditional fallthrough X2 sync", () => {
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

  it("vp-x2-peephole keeps fraction after a counted-loop fallthrough X2 sync", () => {
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

  it("vp-x2-peephole removes fraction after an indirect conditional preserves an X2 boundary", () => {
    const program: IrOp[] = [
      recall("1"),
      knownTargetIndirectCjump("8", 6),
      { kind: "plain", opcode: 0x0c, meta: { mnemonic: "ВП", comment: "mantissa splice" } },
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

  it("vp-x2-peephole requires every CFG entry to prove the ВП/X2 boundary", () => {
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
