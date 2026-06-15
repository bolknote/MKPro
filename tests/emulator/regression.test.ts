import { readFileSync, readdirSync } from "node:fs";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { formatProgramTokens, formatSetupProgram } from "../../src/core/index.ts";
import { compileMKProCached as compileMKPro } from "../helpers/compile-cache.ts";

const require = createRequire(import.meta.url);

interface RunResult {
  stopped: boolean;
  frames: number;
}

interface Mk61Instance {
  setRegister(register: string, value: string): void;
  loadProgram(codes: number[] | string): { diagnostics: string[] };
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

const { MK61, parseProgramText } = require("./mk61.cjs") as {
  MK61: Mk61Constructor;
  parseProgramText: (source: string) => { codes: number[]; diagnostics: string[] };
};

function exampleFiles(): string[] {
  const dir = resolve("examples");
  return readdirSync(dir)
    .filter((name) => name.endsWith(".mkpro"))
    .map((name) => resolve(dir, name));
}

function pendingOptimizerFiles(): string[] {
  const dir = resolve("examples/pending-optimizer");
  return readdirSync(dir)
    .filter((name) => name.endsWith(".mkpro"))
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
    name: "basic.mkpro computes 3 + 4 = 7 via the input/show/halt cycle",
    example: "basic.mkpro",
    keys: ["В/О", "С/П", "3", "С/П", "4", "С/П", "С/П"],
    expectStop: true,
    expectDisplayMatches: /^7\.?$|7\b/,
  },
  {
    name: "lunar.mkpro fits within budget and accepts initial state",
    example: "lunar.mkpro",
    registers: { "2": "100", "3": "500", "4": "5" },
    keys: ["В/О", "С/П"],
    expectStop: true,
  },
  {
    name: "tiny-game.mkpro boots and stops awaiting first key",
    example: "tiny-game.mkpro",
    keys: ["В/О", "С/П"],
    expectStop: true,
  },
  {
    name: "human.mkpro boots and stops awaiting first action",
    example: "human.mkpro",
    keys: ["В/О", "С/П"],
    expectStop: true,
  },
  {
    name: "human.mkpro train (key=2) keeps program stable after prologue-elimination",
    example: "human.mkpro",
    registers: { "1": "0", "2": "5" },
    keys: ["В/О", "С/П", "2", "С/П", "С/П"],
    expectStop: true,
  },
  {
    name: "human.mkpro spend (key=8) keeps program stable after prologue-elimination",
    example: "human.mkpro",
    registers: { "1": "3", "2": "5" },
    keys: ["В/О", "С/П", "8", "С/П", "С/П"],
    expectStop: true,
  },
  {
    name: "tiny-game.mkpro drain (key=4) keeps program stable after prologue-elimination",
    example: "tiny-game.mkpro",
    registers: { "0": "80000078", "2": "8" },
    keys: ["В/О", "С/П", "4", "С/П", "С/П"],
    expectStop: true,
  },
];

describe("emulator regression", () => {
  const files = exampleFiles();
  expect(files.length).toBeGreaterThanOrEqual(5);

  it("parses Anvarov's Latin FBx spelling as F Вx", () => {
    const parsed = parseProgramText("30 FBx");
    expect(parsed.diagnostics).toEqual([]);
    expect(parsed.codes[30]).toBe(0x0f);
  });

  for (const file of files) {
    const name = file.split("/").pop()!;
    it(`loads ${name} into the headless emulator with zero diagnostics`, () => {
      const source = readFileSync(file, "utf8");
      const result = compileMKPro(source);
      const codes = result.steps.map((step) => step.opcode);
      const calc = new MK61();
      const loaded = calc.loadProgram(codes);
      expect(loaded.diagnostics).toEqual([]);
      expect(calc.readProgramCodes(codes.length)).toEqual(codes);
    }, 15000);
  }

  for (const file of pendingOptimizerFiles()) {
    const name = file.split("/").pop()!;
    it(`keeps pending optimizer ${name} before emulator loading`, () => {
      const source = readFileSync(file, "utf8");
      expect(() => compileMKPro(source, { budget: 999 })).not.toThrow(/real rule lowerers before code generation/u);
    });
  }

  for (const scenario of SCENARIOS) {
    it(scenario.name, () => {
      const file = resolve("examples", scenario.example);
      const source = readFileSync(file, "utf8");
      const result = compileMKPro(source);
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

  it("runs the original cave robber stake flow without a fresh random draw", () => {
    const source = `
program StackStopRiskProbe {
  state {
    stake_value: counter 0..99 = 2
    fight_entry: counter 0..99 = 0
    result: counter 0..99 = 0
  }

  loop {
    result = robber_fight(stake_value)
    halt(result)
  }

  fn robber_fight(stake) {
    show(stake)
    fight_entry = read()
    return int(stake * (1 + sin(fight_entry)))
  }
}
`;
    const result = compileMKPro(source, { budget: 999, analysis: true });
    expect(result.report.optimizations.some((item) => item.name === "show-read-stack-stop-risk-lowering")).toBe(true);

    const runChoice = (keys: string[]): string => {
      const calc = new MK61();
      const loaded = calc.loadProgram(result.steps.map((step) => step.opcode));
      expect(loaded.diagnostics).toEqual([]);
      for (const preload of result.report.preloads) {
        calc.setRegister(preload.register, preload.value);
      }

      calc.pressSequence(["В/О", "С/П"]);
      expect(calc.runUntilStable({ maxFrames: 500, stableFrames: 6 }).stopped).toBe(true);
      expect(calc.displayText()).toMatch(/^2,?$/u);

      calc.pressSequence(keys);
      expect(calc.runUntilStable({ maxFrames: 500, stableFrames: 6 }).stopped).toBe(true);
      return calc.displayText();
    };

    expect(runChoice(["0", "С/П"])).toMatch(/^2,?$/u);
    expect(runChoice(["В↑", "С/П"])).toMatch(/^3,?$/u);
  });

  it("runs a generalized stack-stop risk (cos) flow keeping the stake in Y", () => {
    // Same kept-in-Y / transform-in-X machine shape as the sin form, but with a
    // different intrinsic, proving the generalized fusion stays runtime-correct.
    const source = `
program StakeCosProbe {
  state {
    stake_value: counter 0..99 = 2
    result: counter 0..99 = 0
  }

  loop {
    result = robber_fight(stake_value)
    halt(result)
  }

  fn robber_fight(stake) {
    show(stake)
    return int(stake * (1 + cos(read())))
  }
}
`;
    const result = compileMKPro(source, { budget: 999, analysis: true });
    expect(result.report.optimizations.some((item) => item.name === "x-param-stack-stop-risk-read")).toBe(true);

    const runChoice = (keys: string[]): string => {
      const calc = new MK61();
      const loaded = calc.loadProgram(result.steps.map((step) => step.opcode));
      expect(loaded.diagnostics).toEqual([]);
      for (const preload of result.report.preloads) {
        calc.setRegister(preload.register, preload.value);
      }

      calc.pressSequence(["В/О", "С/П"]);
      expect(calc.runUntilStable({ maxFrames: 500, stableFrames: 6 }).stopped).toBe(true);
      expect(calc.displayText()).toMatch(/^2,?$/u);

      calc.pressSequence(keys);
      expect(calc.runUntilStable({ maxFrames: 500, stableFrames: 6 }).stopped).toBe(true);
      return calc.displayText();
    };

    // cos(0) = 1 -> int(2 * (1 + 1)) = 4 (calculator is in radians/degrees mode
    // agnostic here because the input is exactly 0).
    expect(runChoice(["0", "С/П"])).toMatch(/^4,?$/u);
  });

  it("runs fused resource underflow as an error stop only after the counter is exhausted", () => {
    const source = `
program ResourceUnderflowProbe {
  state {
    food: counter 0..9 = 0
  }

  loop {
    food--
    if food < 0 {
      halt("ЕГГ0Г")
    }
    halt(food)
  }
}
`;
    const result = compileMKPro(source, { budget: 999, analysis: true });
    expect(result.report.optimizations.some((item) => item.name === "decrement-underflow-domain-guard")).toBe(true);

    const runWithFood = (food: string): string => {
      const calc = new MK61();
      const loaded = calc.loadProgram(result.steps.map((step) => step.opcode));
      expect(loaded.diagnostics).toEqual([]);
      calc.setRegister(result.report.registers.food!, food);
      calc.pressSequence(["В/О", "С/П"]);
      expect(calc.runUntilStable({ maxFrames: 500, stableFrames: 6 }).stopped).toBe(true);
      return calc.displayText().toUpperCase();
    };

    expect(runWithFood("2")).toMatch(/^1,?$/u);
    expect(runWithFood("0")).toContain("ЕГГ");
  });

  it("runs wumpus setup before the main program", () => {
    const file = resolve("examples/wumpus.mkpro");
    const source = readFileSync(file, "utf8");
    const result = compileMKPro(source, { budget: 999, analysis: true });
    const setupProgram = formatSetupProgram(result);
    expect(setupProgram).toBeDefined();
    expect(result.report.steps).toBeLessThanOrEqual(105);

    const calc = new MK61();
    const setupLoaded = calc.loadProgram(setupProgram!);
    expect(setupLoaded.diagnostics).toEqual([]);

    calc.pressSequence(["В/О", "С/П"]);
    const setupRun = calc.runUntilStable({ maxFrames: 1000, stableFrames: 8 });
    expect(setupRun.stopped).toBe(true);

    for (const field of ["wumpus", "hazard_pit_1", "hazard_pit_2", "hazard_bat_1", "hazard_bat_2"]) {
      const register = result.report.registers[field];
      expect(register).toBeDefined();
      if (register === undefined) throw new Error(`missing register for ${field}`);
      const value = readIntegerRegister(calc, register);
      expect(value).toBeGreaterThanOrEqual(1);
      expect(value).toBeLessThanOrEqual(20);
    }

    const mainLoaded = calc.loadProgram(formatProgramTokens(result.steps));
    expect(mainLoaded.diagnostics).toEqual([]);
  });

  it("kills the wumpus player when the arrow counter is exhausted", () => {
    // Regression for the F Lx clamp bug: `arrows--` lowered through F L0 never
    // reached 0, so the `if arrows == 0 { dead() }` loss condition was dead code.
    const source = readFileSync(resolve("examples/wumpus.mkpro"), "utf8");
    const result = compileMKPro(source, { budget: 999, analysis: true });
    const reg = result.report.registers;

    const calc = new MK61();
    expect(calc.loadProgram(formatProgramTokens(result.steps)).diagnostics).toEqual([]);

    // Fixed layout: wumpus far from room 1 so every -5 shot misses.
    const stateRegs = new Set<string>(Object.values(reg));
    for (const preload of result.report.preloads) {
      if (!stateRegs.has(preload.register)) calc.setRegister(preload.register, preload.value);
    }
    calc.setRegister(reg.room!, "1");
    calc.setRegister(reg.target!, "0");
    calc.setRegister(reg.arrows!, "5");
    calc.setRegister(reg.clue!, "0");
    calc.setRegister(reg.wumpus!, "10");
    calc.setRegister(reg.hazard_pit_1!, "15");
    calc.setRegister(reg.hazard_pit_2!, "16");
    calc.setRegister(reg.hazard_bat_1!, "18");
    calc.setRegister(reg.hazard_bat_2!, "19");

    // Each turn has two display stops: room.arrows and clue. read() is not a
    // separate stop -- resuming from the clue stop consumes the value in X, so a
    // shot is fired by entering -5 while the clue is shown and pressing С/П.
    const shootMiss = (): string => {
      calc.press("С/П"); // room.arrows -> clue stop
      calc.runUntilStable({ maxFrames: 800, stableFrames: 6 });
      calc.inputNumber("5", { clear: true });
      calc.press("/-/"); // MK-61 negates after the digits: -5 fires a shot
      calc.press("С/П"); // resume from clue -> read() takes -5
      calc.runUntilStable({ maxFrames: 1500, stableFrames: 6 });
      return calc.displayText().toUpperCase();
    };

    // Start: the first turn stops on its room.arrows display.
    calc.pressSequence(["В/О", "С/П"]);
    calc.runUntilStable({ maxFrames: 800, stableFrames: 6 });

    let died = false;
    for (let shot = 1; shot <= 6 && !died; shot += 1) {
      const display = shootMiss();
      if (display.includes("Г")) {
        died = true;
        expect(shot).toBe(5); // five arrows, death on the fifth miss
        break;
      }
    }
    expect(died).toBe(true);
  });
});

function readIntegerRegister(calc: Mk61Instance, register: string): number {
  return Number.parseInt(calc.readRegister(register).replace(",", ""), 10);
}
