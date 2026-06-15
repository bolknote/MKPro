import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { compileMKProCached as compileMKPro } from "../helpers/compile-cache.ts";

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

// Cyrillic/display rendering MK-61 uses for the hex digits A..F. A formal dark
// selector such as "C5" must be loaded into a register in this representation
// for the calculator's indirect flow to decode it.
const MK61_HEX_DIGITS: Record<string, string> = {
  A: "-",
  B: "L",
  C: "С",
  D: "Г",
  E: "Е",
  F: "_",
};

function mk61HexLiteral(text: string): string {
  return [...text.toUpperCase()].map((digit) => MK61_HEX_DIGITS[digit] ?? digit).join("");
}

const DATA_REGISTERS = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"];

interface Scenario {
  registers?: Record<string, string>;
  keys: string[];
}

// Deterministic input scripts that drive human.mkpro through its turn logic.
const SCENARIOS: Scenario[] = [
  { keys: ["В/О", "С/П"] },
  { registers: { "1": "0", "2": "5" }, keys: ["В/О", "С/П", "Сx", "2", "С/П", "С/П"] },
  { registers: { "1": "3", "2": "5" }, keys: ["В/О", "С/П", "Сx", "8", "С/П", "С/П"] },
  { registers: { "1": "7", "2": "9" }, keys: ["В/О", "С/П", "Сx", "6", "С/П"] },
];

interface Observation {
  display: string;
  stopped: boolean;
  registers: Record<string, string>;
}

function observe(
  codes: number[],
  scenario: Scenario,
  preloadRegisters: Record<string, string>,
  excluded: ReadonlySet<string>,
): Observation {
  const calc = new MK61();
  calc.loadProgram(codes);
  for (const [register, value] of Object.entries(scenario.registers ?? {})) {
    calc.setRegister(register, value);
  }
  for (const [register, value] of Object.entries(preloadRegisters)) {
    calc.setRegister(register, mk61HexLiteral(value));
  }
  calc.pressSequence(scenario.keys);
  const run = calc.runUntilStable({ maxFrames: 600, stableFrames: 5 });
  const registers: Record<string, string> = {};
  for (const register of DATA_REGISTERS) {
    if (excluded.has(register)) continue;
    registers[register] = calc.readRegister(register).trim();
  }
  return { display: calc.displayText().trim(), stopped: run.stopped, registers };
}

describe("post-layout indirect-flow behavioral equivalence (real emulator)", () => {
  const source = readFileSync(resolve("examples/human.mkpro"), "utf8");
  const before = compileMKPro(source);
  const after = compileMKPro(source, { indirectFlowRescueAbove: 0 });

  const beforeCodes = before.steps.map((step) => step.opcode);
  const afterCodes = after.steps.map((step) => step.opcode);

  it("actually rewrites at least one branch and shrinks the program", () => {
    expect(after.report.steps).toBeLessThan(before.report.steps);
    expect(after.report.preloads.some((preload) => /[A-F]/iu.test(preload.value))).toBe(true);
    expect(afterCodes.some((opcode) => opcode >= 0x80 && opcode <= 0x8e)).toBe(true);
  });

  it("reaches the target through a proven dark-entry formal address", () => {
    const names = after.report.optimizations.map((optimization) => optimization.name);
    expect(names).toContain("dark-entry-layout");
  });

  // Only the selector registers the rewrite *adds* matter here; shared game-state
  // preloads (e.g. counters) are identical in both builds and are driven by the
  // scenario, so they must not be overwritten.
  const beforePreloadKeys = new Set(
    before.report.preloads.map((preload) => `${preload.register}=${preload.value}`),
  );
  const preloadRegisters: Record<string, string> = {};
  for (const preload of after.report.preloads) {
    if (beforePreloadKeys.has(`${preload.register}=${preload.value}`)) continue;
    preloadRegisters[preload.register] = preload.value;
  }
  // The selector registers are scratch in the rewritten program; exclude them
  // from the data-register comparison (they intentionally differ).
  const excluded = new Set(Object.keys(preloadRegisters));

  for (const [index, scenario] of SCENARIOS.entries()) {
    it(`scenario #${index} behaves identically before and after the rewrite`, () => {
      const original = observe(beforeCodes, scenario, {}, excluded);
      const rewritten = observe(afterCodes, scenario, preloadRegisters, excluded);
      expect(rewritten.stopped).toBe(original.stopped);
      expect(rewritten.display).toBe(original.display);
      expect(rewritten.registers).toEqual(original.registers);
    });
  }
});
