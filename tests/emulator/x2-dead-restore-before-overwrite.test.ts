import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { runIrPasses } from "../../src/core/passes/index.ts";
import type { MachineItem } from "../../src/core/types.ts";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  setRegister(register: string, value: string): Mk61Instance;
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { signature: string };
  displayText(): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

const DOT = 0x0a;
const SIGN_CHANGE = 0x0b;
const VP = 0x0c;
const CX = 0x0d;
const FPI = 0x20;
const FRAC = 0x35;
const K1 = 0x55;
const STOP = 0x50;
const RECALL1 = 0x61;

function op(opcode: number, mnemonic = opcode.toString(16)): MachineItem {
  return { kind: "op", opcode, mnemonic };
}

function codes(items: readonly MachineItem[]): number[] {
  return items
    .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
    .map((item) => item.opcode);
}

function optimizedCodes(program: readonly number[]): number[] {
  return codes(runIrPasses(program.map((opcode) => op(opcode)), {
    delivery: "hex",
    budget: 105,
    analysis: false,
  }).items);
}

function display(program: readonly number[], registers: Record<string, string> = {}): string {
  const calc = new MK61();
  for (const [register, value] of Object.entries(registers)) calc.setRegister(register, value);
  calc.loadProgram([...program]);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 300, stableFrames: 5 });
  return calc.displayText().replace(/\s/gu, "");
}

function signature(program: readonly number[], registers: Record<string, string> = {}): string {
  const calc = new MK61();
  for (const [register, value] of Object.entries(registers)) calc.setRegister(register, value);
  calc.loadProgram([...program]);
  calc.pressSequence(["В/О", "С/П"]);
  return calc.runUntilStable({ maxFrames: 300, stableFrames: 5 }).signature;
}

describe("dead X2 restore before overwrite", () => {
  it("removes decimal restore cells whose X result is overwritten by Cx", () => {
    const dotProgram = [0x00, 0x02, FRAC, DOT, K1, CX, STOP];
    const signProgram = [0x00, 0x02, FRAC, SIGN_CHANGE, CX, STOP];
    const vpProgram = [0x05, VP, K1, CX, STOP];

    expect(optimizedCodes(dotProgram)).toEqual([0x00, 0x02, FRAC, CX, STOP]);
    expect(display(optimizedCodes(dotProgram))).toBe(display(dotProgram));

    expect(optimizedCodes(signProgram)).toEqual([0x00, 0x02, FRAC, CX, STOP]);
    expect(display(optimizedCodes(signProgram))).toBe(display(signProgram));

    expect(optimizedCodes(vpProgram)).toEqual([0x05, CX, STOP]);
    expect(display(optimizedCodes(vpProgram))).toBe(display(vpProgram));
  });

  it("keeps register-only dot restores because a preloaded hex register can error", () => {
    const program = [RECALL1, FPI, DOT, CX, STOP];
    const optimized = optimizedCodes(program);

    expect(optimized).toEqual(program);
    expect(signature(optimized, { "1": "Г" })).toContain("ЕГГ0Г");
  });
});
