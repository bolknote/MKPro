import { describe, expect, it } from "vitest";
import { lowerIrToMachine } from "../../src/core/ir.ts";
import { arithmeticIfPass } from "../../src/core/passes/arithmetic-if.ts";
import { deadCodeAfterHalt } from "../../src/core/passes/dead-code-after-halt.ts";
import { deadStoreElimination } from "../../src/core/passes/dead-store-elimination.ts";
import { indirectMemoryTable, stableIndirectFlow } from "../../src/core/passes/indirect-addressing.ts";
import { jumpThread } from "../../src/core/passes/jump-thread.ts";
import { jumpToNextThreading } from "../../src/core/passes/jump-to-next.ts";
import { lastXReuse } from "../../src/core/passes/last-x-reuse.ts";
import { computeLiveness } from "../../src/core/passes/liveness-analysis.ts";
import { preloadedIndirectFlow } from "../../src/core/passes/preloaded-indirect-flow.ts";
import { r0FractionalSentinel } from "../../src/core/passes/r0-fractional-sentinel.ts";
import { redundantPrologueElimination } from "../../src/core/passes/redundant-prologue.ts";
import { registerCoalesce } from "../../src/core/passes/register-coalesce.ts";
import { storeRecallPeephole } from "../../src/core/passes/store-recall-peephole.ts";
import { tailCallLowering } from "../../src/core/passes/tail-call.ts";
import { vpX2Peephole } from "../../src/core/passes/vp-x2-peephole.ts";
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

function recall(register: RegisterName): IrOp {
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

function indirectStore(register: RegisterName): IrOp {
  return {
    kind: "indirect-store",
    register,
    opcode: 0xb0 + REGISTER_INDEX[register],
    meta: { mnemonic: `К X->П ${register}` },
  };
}

function label(name: string): IrOp {
  return { kind: "label", name };
}

function halt(): IrOp {
  return { kind: "stop", opcode: 0x50, semantic: "halt", meta: { mnemonic: "С/П" } };
}

function ret(): IrOp {
  return { kind: "return", opcode: 0x52, meta: { mnemonic: "В/О" } };
}

describe("ir passes on synthetic programs", () => {
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
