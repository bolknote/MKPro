import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";
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
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

const MK61_HEX_DIGITS: Record<string, string> = { A: "-", B: "L", C: "С", D: "Г", E: "Е", F: "_" };

function mk61HexLiteral(text: string): string {
  return [...text.toUpperCase()].map((digit) => MK61_HEX_DIGITS[digit] ?? digit).join("");
}

// Each script drives a full player/calculator turn: В/О + С/П starts the rule
// chain; after every status stop the player either holds (0) or rolls again.
const SCRIPTS: string[][] = [
  ["В/О", "С/П"],
  ["В/О", "С/П", "0", "С/П"],
  ["В/О", "С/П", "2", "С/П", "0", "С/П"],
  ["В/О", "С/П", "2", "С/П", "2", "С/П", "0", "С/П"],
  ["В/О", "С/П", "5", "С/П", "5", "С/П", "5", "С/П"],
];

interface Observation {
  display: string;
  stopped: boolean;
  registers: Record<string, string>;
}

function observe(
  codes: number[],
  keys: string[],
  preloads: readonly PreloadReport[],
  registersToCompare: readonly string[],
): Observation {
  const calc = new MK61();
  for (const preload of preloads) calc.setRegister(preload.register, mk61HexLiteral(preload.value));
  calc.loadProgram(codes);
  calc.pressSequence(keys);
  const run = calc.runUntilStable({ maxFrames: 800, stableFrames: 5 });
  const registers: Record<string, string> = {};
  for (const register of registersToCompare) registers[register] = calc.readRegister(register).trim();
  return { display: calc.displayText().trim(), stopped: run.stopped, registers };
}

describe("interprocedural value-prop + DSE behavioral equivalence (real emulator)", () => {
  // The real К СЧ stream is timing/layout-sensitive, so this equivalence probe
  // pins the die roll while preserving the same interprocedural rule shape.
  const source = readFileSync(resolve("examples/game-100-pig.mkpro"), "utf8")
    .replace("die = random_cell(die_faces)", "die = 3");
  const before = compileMKPro(source, { disableInterproceduralOpts: true });
  const after = compileMKPro(source);
  const beforeCodes = before.steps.map((step) => step.opcode);
  const afterCodes = after.steps.map((step) => step.opcode);
  const gameRegisters = [...new Set(Object.values(after.report.registers))];

  it("both passes fire and the program shrinks", () => {
    const names = after.report.optimizations.map((optimization) => optimization.name);
    expect(names).toContain("interprocedural-value-propagation");
    expect(names).toContain("interprocedural-dead-store");
    expect(after.report.steps).toBeLessThan(before.report.steps);
  });

  it("keeps the same field-to-register allocation", () => {
    expect(after.report.registers).toEqual(before.report.registers);
  });

  for (const [index, keys] of SCRIPTS.entries()) {
    it(`script #${index} behaves identically before and after optimization`, () => {
      const original = observe(beforeCodes, keys, before.report.preloads, gameRegisters);
      const optimized = observe(afterCodes, keys, after.report.preloads, gameRegisters);
      expect(optimized.stopped).toBe(original.stopped);
      expect(optimized.display).toBe(original.display);
      expect(optimized.registers).toEqual(original.registers);
    });
  }
});
