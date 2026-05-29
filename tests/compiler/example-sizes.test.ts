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
  alaram: 82,
  basic: 8,
  "cave-sketch": 54,
  "dangerous-loading": 89,
  dungeon: 88,
  "fox-hunt-100": 97,
  "fox-hunt-mk61": 88,
  "game-100-pig": 103,
  human: 30,
  lunar: 47,
  "minesweeper-9x7": 77,
  "minesweeper-9x9": 77,
  "raja-yoga": 88,
  "sea-battle": 67,
  "tiny-game": 26,
  wumpus: 104,
};

// pending-optimizer programs overflow the physical MK-61 address space, so
// they are measured with a large budget and analysis mode, mirroring
// `mk-pro compile ... --out hex --budget 999999 --analysis`. The goal is to
// shrink these toward 105; the guard only enforces that they never grow.
// The numbers below are the current locked ceilings, not historical deltas.
const PENDING_BASELINE: Record<string, number> = {
  "cave-highlevel-baseline": 134,
  "cave-treasure": 150,
  "giants-country": 180,
  labyrinth777: 210,
  teleport: 221,
  "tic-tac-toe-4x4": 219,
  "treasure-hunter-2": 141,
};

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

  for (const [name, baseline] of Object.entries(PENDING_BASELINE)) {
    it(`pending-optimizer/${name}.mkpro stays within ${baseline} cells`, () => {
      expect(exampleSteps(`pending-optimizer/${name}`, true)).toBeLessThanOrEqual(baseline);
    });
  }
});
