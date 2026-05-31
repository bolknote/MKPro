import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";

// Size guard. The compiled cell count of every example program must never
// grow past these locked baselines. Shrinking is allowed and expected; when a
// program gets smaller, lower the matching number here so the guard keeps
// catching regressions.
//
// Cell count is taken from `report.steps`, which equals the number of program
// cells emitted by `mk-pro compile ... --out hex`.

const EXAMPLE_BASELINE: Record<string, number> = {
  "99-bottles": 53,
  alaram: 81,
  basic: 8,
  "cave-sketch": 52,
  "dangerous-loading": 89,
  dungeon: 84,
  "e-94-digits": 64,
  "functions-demo": 29,
  "fox-hunt-100": 105,
  "fox-hunt-mk61": 65,
  "game-100-pig": 103,
  human: 25,
  "jack-pot": 101,
  lunar: 47,
  "minesweeper-9x7": 100,
  "minesweeper-9x9": 90,
  "raja-yoga": 88,
  "sea-battle": 78,
  "tiny-game": 24,
  wumpus: 103,
};

const EXAMPLE_COMPILE_ERRORS: Record<string, RegExp> = {};

// pending-optimizer programs overflow the physical MK-61 address space, so
// they are measured with a large budget and analysis mode, mirroring
// `mk-pro compile ... --out hex --budget 999999 --analysis`. The goal is to
// shrink these toward 105; the guard only enforces that they never grow.
// The numbers below are the current locked ceilings, not historical deltas.
const PENDING_BASELINE: Record<string, number> = {
  "cave-highlevel-baseline": 157,
  "cave-treasure": 172,
  "giants-country": 162,
  labyrinth777: 221,
  "rambo-iii": 201,
  teleport: 236,
  "tic-tac-toe-4x4": 264,
  "treasure-hunter-2": 135,
};

const PENDING_COMPILE_ERRORS: Record<string, RegExp> = {};

function exampleSteps(relativePath: string, analysis: boolean): number {
  const source = readFileSync(resolve(`examples/${relativePath}.mkpro`), "utf8");
  const options = analysis ? { budget: 999999, analysis: true } : {};
  return compileMKPro(source, options).report.steps;
}

describe("example size guard", () => {
  for (const [name, baseline] of Object.entries(EXAMPLE_BASELINE)) {
    it(`${name}.mkpro stays within ${baseline} cells`, () => {
      expect(exampleSteps(name, false)).toBeLessThanOrEqual(baseline);
    });
  }

  for (const [name, message] of Object.entries(EXAMPLE_COMPILE_ERRORS)) {
    it(`${name}.mkpro records the current compile blocker`, () => {
      expect(() => exampleSteps(name, false)).toThrow(message);
    });
  }

  for (const [name, baseline] of Object.entries(PENDING_BASELINE)) {
    it(`pending-optimizer/${name}.mkpro stays within ${baseline} cells`, () => {
      expect(exampleSteps(`pending-optimizer/${name}`, true)).toBeLessThanOrEqual(baseline);
    });
  }

  for (const [name, message] of Object.entries(PENDING_COMPILE_ERRORS)) {
    it(`pending-optimizer/${name}.mkpro records the current compile blocker`, () => {
      expect(() => exampleSteps(`pending-optimizer/${name}`, true)).toThrow(message);
    });
  }
});
