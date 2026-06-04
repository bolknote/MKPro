import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { runIrPasses } from "../../src/core/passes/index.ts";
import { raiseMachineToIr } from "../../src/core/ir.ts";
import type { MachineItem } from "../../src/core/types.ts";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

function display(codes: number[]): string {
  const calc = new MK61();
  calc.loadProgram(codes);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 300, stableFrames: 4 });
  return calc.displayText().replace(/\s/gu, "");
}

const VP = 0x0c;
const KNOP = 0x54;
const K1 = 0x55;
const K2 = 0x56;
const STOP = 0x50;

describe("ВП exponent-entry splice collapse (vp-splice)", () => {
  it("ВП ВП and empty-op ВП produce the same value as a single ВП on the emulator", () => {
    // 5 ВП 3 == 5e3
    expect(display([0x05, VP, 0x03, STOP])).toContain("5000");
    // 5 ВП ВП 3 collapses to the same result.
    expect(display([0x05, VP, VP, 0x03, STOP])).toContain("5000");
    // 5 КНОП ВП 3 collapses to the same result.
    expect(display([0x05, KNOP, VP, 0x03, STOP])).toContain("5000");
    // К1 and К2 are the same empty-op class for this exponent-entry boundary.
    expect(display([0x05, K1, VP, 0x03, STOP])).toContain("5000");
    expect(display([0x05, K2, VP, 0x03, STOP])).toContain("5000");
  });

  it("the pass rewrites a freestanding ВП ВП / empty-op ВП run to a single ВП", () => {
    const program: MachineItem[] = [
      { kind: "op", opcode: 0x05, mnemonic: "5" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: KNOP, mnemonic: "КНОП" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: K1, mnemonic: "К 1" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: K2, mnemonic: "К 2" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    // Sanity: the synthetic program raises cleanly into IR.
    expect(raiseMachineToIr(program).length).toBeGreaterThan(0);

    const result = runIrPasses(program, { delivery: "hex", budget: 105, analysis: false });
    const names = result.optimizations.map((optimization) => optimization.name);
    expect(names).toContain("vp-exponent-splice");

    const codes = result.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    // The four redundant cells (one extra ВП and three empty ops) are gone.
    expect(codes.filter((opcode) => opcode === VP).length).toBe(1);
    expect(codes.filter((opcode) => opcode === KNOP).length).toBe(0);
    expect(codes.filter((opcode) => opcode === K1).length).toBe(0);
    expect(codes.filter((opcode) => opcode === K2).length).toBe(0);

    // The collapsed program still computes 5e3.
    expect(display(codes)).toContain("5000");
  });
});
