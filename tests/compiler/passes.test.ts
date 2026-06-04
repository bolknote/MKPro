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
import { vpX2Peephole } from "../../src/core/passes/vp-x2-peephole.ts";
import { computeX2RegisterStates } from "../../src/core/passes/helpers.ts";
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

function recall(register: RegisterName): Extract<IrOp, { kind: "recall" }> {
  return {
    kind: "recall",
    register,
    opcode: 0x60 + REGISTER_INDEX[register],
    meta: { mnemonic: `П->X ${register}` },
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

function indirectJump(register: RegisterName): IrOp {
  return {
    kind: "indirect-jump",
    register,
    opcode: 0x80 + REGISTER_INDEX[register],
    meta: { mnemonic: `К БП ${register}` },
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

  it("flow-x-reuse treats counted loop backedges as unknown X predecessors", () => {
    const program: IrOp[] = [
      recall("2"),
      label("body"),
      recall("2"),
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
      plain(0x01, "1"),
      label("second"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      recall("4"),
      store("5"),
      plain(0x02, "2"),
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
      plain(0x01, "1"),
      label("second"),
      recall("1"),
      call("normalize"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      plain(0x02, "2"),
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

  it("shared-straight-line-helper adds internal entries for repeated helper suffixes", () => {
    const program: IrOp[] = [
      label("first"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      recall("4"),
      store("5"),
      plain(0x01, "1"),
      label("second"),
      recall("1"),
      recall("2"),
      plain(0x10, "+"),
      store("3"),
      recall("4"),
      store("5"),
      plain(0x02, "2"),
      label("suffix"),
      plain(0x10, "+"),
      store("3"),
      recall("4"),
      store("5"),
      plain(0x03, "3"),
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
