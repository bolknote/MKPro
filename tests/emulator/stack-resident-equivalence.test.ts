import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { compileLoweringVariantForTest } from "../../src/core/compiler.ts";
import type { PreloadReport } from "../../src/core/types.ts";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  setRegister(register: string, value: string): void;
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
  readRegister(register: string): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("../emulator/mk61.cjs") as { MK61: Mk61Constructor };

const MK61_HEX_DIGITS: Record<string, string> = { A: "-", B: "L", C: "С", D: "Г", E: "Е", F: "_" };

function mk61HexLiteral(text: string): string {
  return [...text.toUpperCase()].map((digit) => MK61_HEX_DIGITS[digit] ?? digit).join("");
}

function observe(
  codes: number[],
  keys: string[],
  preloads: readonly PreloadReport[],
  registersToCompare: readonly string[],
) {
  const calc = new MK61();
  for (const preload of preloads) calc.setRegister(preload.register, mk61HexLiteral(preload.value));
  calc.loadProgram(codes);
  calc.pressSequence(keys);
  const run = calc.runUntilStable({ maxFrames: 800, stableFrames: 5 });
  const registers: Record<string, string> = {};
  for (const register of registersToCompare) registers[register] = calc.readRegister(register).trim();
  return { display: calc.displayText().trim(), stopped: run.stopped, registers };
}

const DUAL_STACK_SOURCE = `
program DualStackEq {
  state {
    x: packed = 3
    y: packed = 4
    z: packed = 0
  }
  loop {
    a = x
    b = y
    z = a + b
    halt(z)
  }
}
`;

describe("stack-resident temp behavioral equivalence (real emulator)", () => {
  const baseline = compileLoweringVariantForTest(DUAL_STACK_SOURCE, { budget: 999999 }, {});
  const stackResident = compileLoweringVariantForTest(DUAL_STACK_SOURCE, { budget: 999999 }, { stackResidentTemps: true });
  const registers = [...new Set(Object.values(stackResident.report.registers))];

  it("stack-resident variant fires and is not larger than the baseline variant", () => {
    expect(stackResident.report.optimizations.some((entry) => entry.name === "stack-resident-temps")).toBe(true);
    expect(stackResident.steps.length).toBeLessThanOrEqual(baseline.steps.length);
  });

  it("keeps the same field-to-register allocation", () => {
    expect(stackResident.report.registers).toEqual(baseline.report.registers);
  });

  it("matches baseline emulator output for a short run", () => {
    const keys = ["В/О", "С/П"];
    const before = observe(
      baseline.steps.map((step) => step.opcode),
      keys,
      baseline.report.preloads,
      registers,
    );
    const after = observe(
      stackResident.steps.map((step) => step.opcode),
      keys,
      stackResident.report.preloads,
      registers,
    );
    expect(after.display).toBe(before.display);
    expect(after.stopped).toBe(before.stopped);
    expect(after.registers[stackResident.report.registers.z]).toBe(before.registers[baseline.report.registers.z]);
  });
});

const PENDING_PROGRAMS = [
  "examples/pending-optimizer/cave-highlevel-baseline.mkpro",
  "examples/pending-optimizer/cave-treasure.mkpro",
  "examples/pending-optimizer/giants-country.mkpro",
  "examples/pending-optimizer/labyrinth777.mkpro",
  "examples/pending-optimizer/rambo-iii.mkpro",
  "examples/pending-optimizer/teleport.mkpro",
  "examples/pending-optimizer/tic-tac-toe-4x4.mkpro",
] as const;

describe("stack-resident temp variant compiles pending programs", () => {
  for (const file of PENDING_PROGRAMS) {
    it(`compiles ${file} with stackResidentTemps`, () => {
      const source = readFileSync(file, "utf8");
      const result = compileLoweringVariantForTest(source, { budget: 999999, analysis: true }, { stackResidentTemps: true });
      expect(result.diagnostics.some((diagnostic) => diagnostic.level === "error")).toBe(false);
      expect(result.steps.length).toBeGreaterThan(0);
    });
  }
});
