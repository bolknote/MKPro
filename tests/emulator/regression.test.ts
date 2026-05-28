import { readFileSync, readdirSync } from "node:fs";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { compileM61 } from "../../src/core/index.ts";

const require = createRequire(import.meta.url);

interface RunResult {
  stopped: boolean;
  frames: number;
}

interface Mk61Instance {
  setRegister(register: string, value: string): void;
  loadProgram(codes: number[]): { diagnostics: string[] };
  readProgramCodes(count: number): number[];
  press(key: string): void;
  pressSequence(keys: string[]): void;
  inputNumber(value: string, options?: { clear?: boolean }): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): RunResult;
  displayText(): string;
  readRegister(register: string): string;
  programCounter(): string | number;
}

type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;

const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

function exampleFiles(): string[] {
  const dir = resolve("examples");
  return readdirSync(dir)
    .filter((name) => name.endsWith(".m61"))
    .map((name) => resolve(dir, name));
}

interface Scenario {
  name: string;
  example: string;
  registers?: Record<string, string>;
  keys?: string[];
  expectStop?: boolean;
  expectDisplayMatches?: RegExp;
  expectPc?: string | number;
  expectRegister?: { name: string; matches: RegExp };
  maxFrames?: number;
}

const SCENARIOS: Scenario[] = [
  {
    name: "basic.m61 computes 3 + 4 = 7 via the input/show/halt cycle",
    example: "basic.m61",
    keys: ["В/О", "С/П", "3", "С/П", "4", "С/П", "С/П"],
    expectStop: true,
    expectDisplayMatches: /^7\.?$|7\b/,
  },
  {
    name: "lunar.m61 fits within budget and accepts initial state",
    example: "lunar.m61",
    registers: { "2": "100", "3": "500", "4": "5" },
    keys: ["В/О", "С/П"],
    expectStop: true,
  },
  {
    name: "tiny-game.m61 boots and stops awaiting first key",
    example: "tiny-game.m61",
    keys: ["В/О", "С/П"],
    expectStop: true,
  },
  {
    name: "human.m61 boots and stops awaiting first action",
    example: "human.m61",
    keys: ["В/О", "С/П"],
    expectStop: true,
  },
  {
    name: "human.m61 train (key=2) keeps program stable after prologue-elimination",
    example: "human.m61",
    registers: { "1": "0", "2": "5" },
    keys: ["В/О", "С/П", "2", "С/П", "С/П"],
    expectStop: true,
  },
  {
    name: "human.m61 spend (key=8) keeps program stable after prologue-elimination",
    example: "human.m61",
    registers: { "1": "3", "2": "5" },
    keys: ["В/О", "С/П", "8", "С/П", "С/П"],
    expectStop: true,
  },
  {
    name: "tiny-game.m61 drain (key=4) keeps program stable after prologue-elimination",
    example: "tiny-game.m61",
    registers: { "0": "80000078", "2": "8" },
    keys: ["В/О", "С/П", "4", "С/П", "С/П"],
    expectStop: true,
  },
  {
    name: "cave-treasure.m61 enters the dark-overlay loop and stabilises",
    example: "cave-treasure.m61",
    registers: {
      "4": "2",
      "5": "10",
      "6": "ГE-02",
      "7": "5E-1",
      "8": "-52",
      "9": "4,_3E-08",
    },
    keys: ["БП", "4", "4", "4", "4", "П→X", "9", "F", "0", "С/П"],
    expectStop: true,
    expectDisplayMatches: /^1,0000001$/u,
    expectPc: "01",
    maxFrames: 600,
  },
];

describe("emulator regression", () => {
  const files = exampleFiles();
  expect(files.length).toBeGreaterThanOrEqual(16);

  for (const file of files) {
    const name = file.split("/").pop()!;
    it(`loads ${name} into the headless emulator with zero diagnostics`, () => {
      const source = readFileSync(file, "utf8");
      const result = compileM61(source);
      const codes = result.steps.map((step) => step.opcode);
      const calc = new MK61();
      const loaded = calc.loadProgram(codes);
      expect(loaded.diagnostics).toEqual([]);
      expect(calc.readProgramCodes(codes.length)).toEqual(codes);
    });
  }

  for (const scenario of SCENARIOS) {
    it(scenario.name, () => {
      const file = resolve("examples", scenario.example);
      const source = readFileSync(file, "utf8");
      const result = compileM61(source);
      const codes = result.steps.map((step) => step.opcode);
      const calc = new MK61();
      const loaded = calc.loadProgram(codes);
      expect(loaded.diagnostics).toEqual([]);

      for (const [register, value] of Object.entries(scenario.registers ?? {})) {
        calc.setRegister(register, value);
      }
      if (scenario.keys) {
        calc.pressSequence(scenario.keys);
      }
      const run = calc.runUntilStable({ maxFrames: scenario.maxFrames ?? 400, stableFrames: 5 });
      if (scenario.expectStop !== undefined) {
        expect(run.stopped).toBe(scenario.expectStop);
      }
      if (scenario.expectDisplayMatches) {
        expect(calc.displayText()).toMatch(scenario.expectDisplayMatches);
      }
      if (scenario.expectPc !== undefined) {
        expect(calc.programCounter()).toBe(scenario.expectPc);
      }
      if (scenario.expectRegister) {
        expect(calc.readRegister(scenario.expectRegister.name)).toMatch(scenario.expectRegister.matches);
      }
    });
  }
});
