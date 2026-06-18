import { describe, expect, it } from "vitest";
import { evaluateIndirectAddress } from "../../src/core/indirect-addressing.ts";
import {
  optimizePostLayoutAddressCodeOverlay,
  optimizePostLayoutFractionalR0Flow,
  optimizePostLayoutIndirectFlow,
  optimizePostLayoutStopTailReuse,
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

function jumpAddress(target: number): MachineItem[] {
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

  it("recovers label identity for numeric forward targets before proving shifts", () => {
    const program: MachineItem[] = [
      { kind: "label", name: "main" },
      ...jumpAddress(3),
      digit(),
      { kind: "label", name: "target" },
      halt(),
    ];
    const result = optimizePostLayoutIndirectFlow(program, options, 0);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.preloads).toEqual([{ register: "7", value: "B4", countsAgainstProgram: false }]);
    expect(evaluateIndirectAddress("7", "B4", "flow")?.actualFlowTarget).toBe(2);
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

  it("reuses an existing preloaded stop tail", () => {
    const program: MachineItem[] = [
      { kind: "label", name: "base" },
      halt(),
      op(0x8b, "К БП b"),
      digit(),
      { kind: "label", name: "duplicate" },
      halt(),
      op(0x8b, "К БП b"),
      digit(),
    ];
    const result = optimizePostLayoutStopTailReuse(program, [
      { register: "8", value: "B2", countsAgainstProgram: false },
    ]);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    const duplicateIndex = result.items.findIndex((item) => item.kind === "label" && item.name === "duplicate");
    expect(result.items[duplicateIndex + 1]).toMatchObject({ kind: "op", opcode: 0x88 });
    expect(result.optimizations[0]?.name).toBe("post-layout-stop-tail-reuse");
  });

  it("reuses zero-prefixed stop tails and retargets shifted selectors", () => {
    const program: MachineItem[] = [
      { kind: "label", name: "base" },
      halt(),
      op(0x8b, "К БП b"),
      ...Array.from({ length: 8 }, () => digit()),
      { kind: "label", name: "zero_tail" },
      digit(),
      halt(),
      op(0x8b, "К БП b"),
      { kind: "label", name: "late_target" },
      halt(),
    ];
    const result = optimizePostLayoutStopTailReuse(program, [
      { register: "8", value: "B2", countsAgainstProgram: false },
      { register: "7", value: "13", countsAgainstProgram: false },
    ]);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    const zeroIndex = result.items.findIndex((item) => item.kind === "label" && item.name === "zero_tail");
    expect(result.items[zeroIndex + 1]).toMatchObject({ kind: "op", opcode: 0x00 });
    expect(result.items[zeroIndex + 2]).toMatchObject({ kind: "op", opcode: 0x88 });
    expect(result.preloads).toContainEqual({ register: "7", value: "C4", countsAgainstProgram: false });
  });

  it("rewrites direct branches to reused stop-tail selector shims", () => {
    const program: MachineItem[] = [
      { kind: "label", name: "base" },
      halt(),
      op(0x8b, "К БП b"),
      op(0x57, "F x!=0"),
      { kind: "address", target: "shim" },
      digit(),
      { kind: "label", name: "shim" },
      op(0x88, "К БП 8"),
    ];
    const result = optimizePostLayoutStopTailReuse(program, [
      { register: "8", value: "B2", countsAgainstProgram: false },
    ]);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items.some((item) => item.kind === "address" && item.target === "shim")).toBe(false);
    expect(result.items.find((item) => item.kind === "op" && item.opcode === 0x78)).toBeDefined();
  });

  it("reuses existing selector preloads for ordinary direct flow and retargets shifts", () => {
    const program: MachineItem[] = [
      { kind: "label", name: "main" },
      digit(),
      ...jump("target"),
      digit(),
      { kind: "label", name: "target" },
      halt(),
    ];
    const result = optimizePostLayoutStopTailReuse(program, [
      { register: "8", value: "B6", countsAgainstProgram: false },
    ]);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items.some((item) => item.kind === "address" && item.target === "target")).toBe(false);
    expect(result.items.find((item) => item.kind === "op" && item.opcode === 0x88)).toBeDefined();
    expect(result.preloads).toEqual([{ register: "8", value: "B5", countsAgainstProgram: false }]);
    expect(evaluateIndirectAddress("8", "B5", "flow")?.actualFlowTarget).toBe(3);
    expect(result.optimizations.some((item) => item.name === "post-layout-existing-selector-flow")).toBe(true);
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

  it("keeps fractional R0 flow proof through unrelated indirect memory", () => {
    const program: MachineItem[] = [
      op(0x00, "0"),
      op(0x0a, "."),
      op(0x05, "5"),
      op(0x40, "X->П 0"),
      op(0xd7, "К П->X 7"),
      ...jump("target"),
      ...Array.from({ length: 93 }, () => digit()),
      { kind: "label", name: "target" },
      halt(),
    ];
    const result = optimizePostLayoutFractionalR0Flow(program);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items.some((item) => item.kind === "op" && item.opcode === 0x80)).toBe(true);
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

  it("overlays a labeled single-cell op onto a direct jump address byte after layout proof", () => {
    const program: MachineItem[] = [
      ...jump("target"),
      { kind: "label", name: "entry" },
      op(0x07, "7"),
      ...Array.from({ length: 5 }, () => digit()),
      { kind: "label", name: "target" },
      halt(),
    ];
    const result = optimizePostLayoutAddressCodeOverlay(program);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items[0]).toMatchObject({ kind: "op", opcode: 0x51 });
    expect(result.items[1]).toMatchObject({ kind: "label", name: "entry" });
    expect(result.items[2]).toMatchObject({ kind: "address", target: "target" });
    expect(result.items.some((item) => item.kind === "op" && item.opcode === 0x07)).toBe(false);
    expect(result.optimizations[0]?.name).toBe("address-code-overlay");
  });

  it("overlays a branch target onto its own address byte when that byte is executable", () => {
    const program: MachineItem[] = [
      ...jump("target"),
      { kind: "label", name: "target" },
      op(0x01, "1"),
      halt(),
    ];
    const result = optimizePostLayoutAddressCodeOverlay(program);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items[0]).toMatchObject({ kind: "op", opcode: 0x51 });
    expect(result.items[1]).toMatchObject({ kind: "label", name: "target" });
    expect(result.items[2]).toMatchObject({ kind: "address", target: "target" });
    expect(result.items.some((item) => item.kind === "op" && item.opcode === 0x01)).toBe(false);
  });

  it("overlays a labeled address-taking op while keeping its address operand", () => {
    const program: MachineItem[] = [
      ...jump("target"),
      { kind: "label", name: "entry" },
      op(0x51, "БП"),
      { kind: "address", target: "done" },
      ...Array.from({ length: 48 }, () => digit()),
      { kind: "label", name: "target" },
      halt(),
      { kind: "label", name: "done" },
      halt(),
    ];
    const result = optimizePostLayoutAddressCodeOverlay(program);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items[0]).toMatchObject({ kind: "op", opcode: 0x51 });
    expect(result.items[1]).toMatchObject({ kind: "label", name: "entry" });
    expect(result.items[2]).toMatchObject({ kind: "address", target: "target" });
    expect(result.items[3]).toMatchObject({ kind: "address", target: "done" });
  });

  it("overlays a labeled orphan address byte onto another branch address byte", () => {
    const program: MachineItem[] = [
      ...jump("target"),
      { kind: "label", name: "entry" },
      { kind: "address", target: 7 },
      ...Array.from({ length: 5 }, () => digit()),
      { kind: "label", name: "target" },
      halt(),
    ];
    const result = optimizePostLayoutAddressCodeOverlay(program);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items[0]).toMatchObject({ kind: "op", opcode: 0x51 });
    expect(result.items[1]).toMatchObject({ kind: "label", name: "entry" });
    expect(result.items[2]).toMatchObject({ kind: "address", target: "target" });
  });

  it("overlays a labeled formal address byte as executable code", () => {
    const program: MachineItem[] = [
      ...jump("target"),
      { kind: "label", name: "entry" },
      { kind: "address", target: 55, formalOpcode: 0x55, comment: "formal K1 byte" },
      ...Array.from({ length: 53 }, () => digit()),
      { kind: "label", name: "target" },
      halt(),
    ];
    const result = optimizePostLayoutAddressCodeOverlay(program);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items[0]).toMatchObject({ kind: "op", opcode: 0x51 });
    expect(result.items[1]).toMatchObject({ kind: "label", name: "entry" });
    expect(result.items[2]).toMatchObject({ kind: "address", target: "target" });
    expect(result.items.some((item) => item.kind === "address" && item.formalOpcode === 0x55)).toBe(false);
  });

  it("uses a formal address alias when the overlaid executable byte targets the same label", () => {
    const program: MachineItem[] = [
      ...jump("target"),
      { kind: "label", name: "entry" },
      op(0xb5, "К X->П 5"),
      digit(),
      { kind: "label", name: "target" },
      halt(),
    ];
    const result = optimizePostLayoutAddressCodeOverlay(program);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items[0]).toMatchObject({ kind: "op", opcode: 0x51 });
    expect(result.items[1]).toMatchObject({ kind: "label", name: "entry" });
    expect(result.items[2]).toMatchObject({ kind: "address", target: "target", formalOpcode: 0xb5 });
    expect(result.items.some((item) => item.kind === "op" && item.opcode === 0xb5)).toBe(false);
  });

  it("overlays a formal branch address byte when its actual target is before the removed cell", () => {
    const program: MachineItem[] = [
      { kind: "label", name: "top" },
      { kind: "op", opcode: 0x51, mnemonic: "БП" },
      { kind: "address", target: 105, formalOpcode: 0xa5, comment: "short-side formal address to 00" },
      { kind: "label", name: "entry" },
      op(0xa5, "К ПП 5"),
      halt(),
    ];
    const result = optimizePostLayoutAddressCodeOverlay(program);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items[1]).toMatchObject({ kind: "op", opcode: 0x51 });
    expect(result.items[2]).toMatchObject({ kind: "label", name: "entry" });
    expect(result.items[3]).toMatchObject({ kind: "address", target: 105, formalOpcode: 0xa5 });
    expect(result.items.some((item) => item.kind === "op" && item.opcode === 0xa5)).toBe(false);
  });

  it("refuses fixed numeric branch address overlays whose target would shift", () => {
    const program: MachineItem[] = [
      { kind: "op", opcode: 0x51, mnemonic: "БП" },
      { kind: "address", target: 7 },
      { kind: "label", name: "entry" },
      op(0x07, "7"),
      ...Array.from({ length: 5 }, () => digit()),
      halt(),
    ];
    const result = optimizePostLayoutAddressCodeOverlay(program);

    expect(result.applied).toBe(0);
    expect(result.items).toEqual(program);
  });

  it("keeps a labeled op when the jump address byte would not match after removal", () => {
    const program: MachineItem[] = [
      ...jump("target"),
      { kind: "label", name: "entry" },
      op(0x07, "7"),
      ...Array.from({ length: 4 }, () => digit()),
      { kind: "label", name: "target" },
      halt(),
    ];
    const result = optimizePostLayoutAddressCodeOverlay(program);

    expect(result.applied).toBe(0);
    expect(result.items).toEqual(program);
  });

  it("overlays a call continuation when the call target is proved terminal", () => {
    const program: MachineItem[] = [
      ...call("target"),
      { kind: "label", name: "entry" },
      op(0x07, "7"),
      ...Array.from({ length: 5 }, () => digit()),
      { kind: "label", name: "target" },
      ...jump("target"),
    ];
    const result = optimizePostLayoutAddressCodeOverlay(program);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items[0]).toMatchObject({ kind: "op", opcode: 0x53 });
    expect(result.items[1]).toMatchObject({ kind: "label", name: "entry" });
    expect(result.items[2]).toMatchObject({ kind: "address", target: "target" });
    expect(result.items.some((item) => item.kind === "op" && item.opcode === 0x07)).toBe(false);
  });

  it("overlays a separate labeled one-cell entry onto a returning call address byte", () => {
    const program: MachineItem[] = [
      ...call("sub"),
      digit(),
      ...jump("after"),
      { kind: "label", name: "entry" },
      op(0x07, "7"),
      digit(),
      digit(),
      { kind: "label", name: "sub" },
      { kind: "op", opcode: 0x52, mnemonic: "В/О" },
      { kind: "label", name: "after" },
      halt(),
      ...jump("entry"),
    ];
    const result = optimizePostLayoutAddressCodeOverlay(program);

    expect(result.applied).toBe(1);
    expect(cellCount(result.items)).toBe(cellCount(program) - 1);
    expect(result.items[0]).toMatchObject({ kind: "op", opcode: 0x53 });
    expect(result.items[1]).toMatchObject({ kind: "label", name: "entry" });
    expect(result.items[2]).toMatchObject({ kind: "address", target: "sub" });
    expect(result.items.some((item) => item.kind === "op" && item.opcode === 0x07)).toBe(false);
  });

  it("keeps a separate labeled entry when ordinary fallthrough reaches it", () => {
    const program: MachineItem[] = [
      ...call("sub"),
      digit(),
      { kind: "label", name: "entry" },
      op(0x07, "7"),
      digit(),
      digit(),
      digit(),
      { kind: "label", name: "sub" },
      { kind: "op", opcode: 0x52, mnemonic: "В/О" },
      ...jump("entry"),
    ];
    const result = optimizePostLayoutAddressCodeOverlay(program);

    expect(result.applied).toBe(0);
    expect(result.items).toEqual(program);
  });

  it("does not overlay returning call or conditional continuations onto their address bytes", () => {
    for (const branch of [
      [{ kind: "op", opcode: 0x53, mnemonic: "ПП" } as MachineItem, { kind: "address", target: "target" } as MachineItem],
      [{ kind: "op", opcode: 0x57, mnemonic: "F x!=0" } as MachineItem, { kind: "address", target: "target" } as MachineItem],
    ]) {
      const program: MachineItem[] = [
        ...branch,
        { kind: "label", name: "entry" },
        op(0x07, "7"),
        ...Array.from({ length: 5 }, () => digit()),
        { kind: "label", name: "target" },
        { kind: "op", opcode: 0x52, mnemonic: "В/О" },
      ];
      const result = optimizePostLayoutAddressCodeOverlay(program);

      expect(result.applied).toBe(0);
      expect(result.items).toEqual(program);
    }
  });
});
