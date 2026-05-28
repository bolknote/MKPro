import { describe, expect, it } from "vitest";
import { evaluateIndirectAddress } from "../../src/core/indirect-addressing.ts";
import { optimizePostLayoutIndirectFlow } from "../../src/core/post-layout-indirect-flow.ts";
import type { MachineItem, RegisterName } from "../../src/core/types.ts";

const options = { delivery: "manual" as const, budget: 999999, analysis: true };

function digit(): MachineItem {
  return { kind: "op", opcode: 0x00, mnemonic: "0" };
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
});
