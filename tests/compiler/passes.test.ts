import { describe, expect, it } from "vitest";
import { lowerIrToMachine } from "../../src/core/ir.ts";
import { deadCodeAfterHalt } from "../../src/core/passes/dead-code-after-halt.ts";
import { deadStoreElimination } from "../../src/core/passes/dead-store-elimination.ts";
import { jumpThread } from "../../src/core/passes/jump-thread.ts";
import { jumpToNextThreading } from "../../src/core/passes/jump-to-next.ts";
import { lastXReuse } from "../../src/core/passes/last-x-reuse.ts";
import { computeLiveness } from "../../src/core/passes/liveness-analysis.ts";
import { redundantPrologueElimination } from "../../src/core/passes/redundant-prologue.ts";
import { storeRecallPeephole } from "../../src/core/passes/store-recall-peephole.ts";
import type { IrOp, RegisterName } from "../../src/core/types.ts";

const noopOptions = { opt: "max" as const, delivery: "manual" as const, budget: 105, warnUnsafe: false };
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

  it("dead-store-elimination ignores stores guarded by unsafeReason", () => {
    const guarded: IrOp = {
      kind: "store",
      register: "1",
      opcode: 0x41,
      meta: { mnemonic: "X->П 1", unsafeReason: "raw opcode" },
    };
    const program: IrOp[] = [guarded, halt()];
    const result = deadStoreElimination.run(program, ctx);
    expect(result.applied).toBe(0);
    expect(result.ops.length).toBe(2);
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
