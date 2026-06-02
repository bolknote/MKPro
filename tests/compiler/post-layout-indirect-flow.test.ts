import { describe, expect, it } from "vitest";
import { evaluateIndirectAddress } from "../../src/core/indirect-addressing.ts";
import {
  optimizePostLayoutFractionalR0Flow,
  optimizePostLayoutIndirectFlow,
} from "../../src/core/post-layout-indirect-flow.ts";
import type { MachineItem, RegisterName } from "../../src/core/types.ts";

const options = { delivery: "manual" as const, budget: 999999, analysis: true };

function digit(): MachineItem {
  return { kind: "op", opcode: 0x00, mnemonic: "0" };
}

function halt(): MachineItem {
  return { kind: "op", opcode: 0x50, mnemonic: "С/П" };
}

function op(opcode: number, mnemonic: string): MachineItem {
  return { kind: "op", opcode, mnemonic };
}

function jump(target: string): MachineItem[] {
  return [
    { kind: "op", opcode: 0x51, mnemonic: "БП" },
    { kind: "address", target },
  ];
}

function cjump(target: string): MachineItem[] {
  return [
    { kind: "op", opcode: 0x5e, mnemonic: "F x=0" },
    { kind: "address", target },
  ];
}

function call(target: string): MachineItem[] {
  return [
    { kind: "op", opcode: 0x53, mnemonic: "ПП" },
    { kind: "address", target },
  ];
}

function programWithBackwardJump(fillerCells: number): MachineItem[] {
  const items: MachineItem[] = [{ kind: "label", name: "top" }];
  for (let i = 0; i < fillerCells; i += 1) items.push(digit());
  items.push({ kind: "op", opcode: 0x51, mnemonic: "БП" });
  items.push({ kind: "address", target: "top" });
  return items;
}

function cellCount(items: readonly MachineItem[]): number {
  return items.filter((item) => item.kind !== "label").length;
}

describe("post-layout indirect flow", () => {
  it("rescues an over-limit program by proving a preloaded indirect backward jump", () => {
    const program = programWithBackwardJump(110); // 110 + 2 (БП addr) = 112 cells
    const result = optimizePostLayoutIndirectFlow(program, options);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);

    const indirectJump = result.items.find(
      (item) => item.kind === "op" && item.opcode >= 0x80 && item.opcode <= 0x8e,
    );
    expect(indirectJump).toBeDefined();

    // The direct БП + address pair is gone.
    expect(result.items.some((item) => item.kind === "op" && item.opcode === 0x51)).toBe(false);
    expect(result.items.some((item) => item.kind === "address")).toBe(false);

    // The emitted preload must decode back to the target's address (0), which
    // is exactly the safety invariant the optimizer proves before accepting.
    expect(result.preloads).toHaveLength(1);
    const preload = result.preloads[0]!;
    expect(preload.countsAgainstProgram).toBe(false);
    const decoded = evaluateIndirectAddress(preload.register as RegisterName, preload.value, "flow");
    expect(decoded?.actualFlowTarget).toBe(0);
  });

  it("leaves an in-budget program byte-identical (no preload requirement added)", () => {
    const program = programWithBackwardJump(10); // 12 cells, well under 105
    const result = optimizePostLayoutIndirectFlow(program, options);

    expect(result.applied).toBe(0);
    expect(result.preloads).toHaveLength(0);
    expect(result.items).toEqual(program);
  });

  it("never grows a program and stops rewriting once it fits the official window", () => {
    const program = programWithBackwardJump(104); // 106 cells, one over the limit
    const result = optimizePostLayoutIndirectFlow(program, options);

    expect(cellCount(result.items)).toBeLessThanOrEqual(cellCount(program));
    expect(cellCount(result.items)).toBeLessThanOrEqual(105);
  });

  it("proves shifted forward targets before rewriting direct branches", () => {
    const program: MachineItem[] = [
      { kind: "label", name: "main" },
      ...jump("skip"),
      digit(),
      { kind: "label", name: "skip" },
      halt(),
    ];
    const result = optimizePostLayoutIndirectFlow(program, options, 0);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.preloads).toEqual([{ register: "7", value: "B4", countsAgainstProgram: false }]);

    const decoded = evaluateIndirectAddress("7", "B4", "flow");
    expect(decoded?.actualFlowTarget).toBe(2);
    expect(result.items.some((item) => item.kind === "address")).toBe(false);
  });

  it("groups repeated forward branches to the same shifted target", () => {
    const program: MachineItem[] = [
      { kind: "label", name: "main" },
      ...cjump("end"),
      digit(),
      ...cjump("end"),
      digit(),
      { kind: "label", name: "end" },
      halt(),
    ];
    const result = optimizePostLayoutIndirectFlow(program, options, 0);

    expect(result.applied).toBe(2);
    expect(cellCount(result.items)).toBe(cellCount(program) - 2);
    expect(result.preloads).toEqual([{ register: "7", value: "B6", countsAgainstProgram: false }]);

    const indirectConditions = result.items.filter(
      (item) => item.kind === "op" && item.opcode === 0xe7,
    );
    expect(indirectConditions).toHaveLength(2);
    expect(evaluateIndirectAddress("7", "B6", "flow")?.actualFlowTarget).toBe(4);
  });

  it("rewrites fractional R0 flow when replacing the branch puts its label target at address 99", () => {
    const program: MachineItem[] = [
      op(0x00, "0"),
      op(0x0a, "."),
      op(0x05, "5"),
      op(0x40, "X->П 0"),
      ...jump("target"),
      ...Array.from({ length: 94 }, () => digit()),
      { kind: "label", name: "target" },
      halt(),
    ];
    const result = optimizePostLayoutFractionalR0Flow(program);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items.some((item) => item.kind === "op" && item.opcode === 0x80)).toBe(true);
    expect(result.items.some((item) => item.kind === "address")).toBe(false);
  });

  it("keeps fractional R0 label flow when the target would not land at address 99", () => {
    const program: MachineItem[] = [
      op(0x00, "0"),
      op(0x0a, "."),
      op(0x05, "5"),
      op(0x40, "X->П 0"),
      ...jump("target"),
      ...Array.from({ length: 93 }, () => digit()),
      { kind: "label", name: "target" },
      halt(),
    ];
    const result = optimizePostLayoutFractionalR0Flow(program);

    expect(result.applied).toBe(0);
    expect(result.items).toEqual(program);
  });

  it("rewrites fractional R0 calls when the resolved label target lands at address 99", () => {
    const program: MachineItem[] = [
      op(0x00, "0"),
      op(0x0a, "."),
      op(0x05, "5"),
      op(0x40, "X->П 0"),
      ...call("target"),
      ...Array.from({ length: 94 }, () => digit()),
      { kind: "label", name: "target" },
      { kind: "op", opcode: 0x52, mnemonic: "В/О" },
    ];
    const result = optimizePostLayoutFractionalR0Flow(program);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items.some((item) => item.kind === "op" && item.opcode === 0xa0)).toBe(true);
  });
});
