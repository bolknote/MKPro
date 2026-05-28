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
  alaram: 94,
  basic: 8,
  "cave-sketch": 85,
  "dangerous-loading": 89,
  dungeon: 94,
  "fox-hunt-100": 99,
  "game-100-pig": 93,
  human: 28,
  lunar: 51,
  "minesweeper-9x7": 79,
  "minesweeper-9x9": 79,
  "raja-yoga": 84,
  "sea-battle": 67,
  "tiny-game": 24,
};

// pending-optimizer programs overflow the physical MK-61 address space, so
// they are measured with a large budget and analysis mode, mirroring
// `mk-pro compile ... --out hex --budget 999999 --analysis`. The goal is to
// shrink these toward 105; the guard only enforces that they never grow.
// cave-highlevel-baseline (+2), cave-treasure (+2) and tic-tac-toe-4x4 (+1)
// grew slightly when the number-entry concatenation bug was fixed: a read/digit
// immediately followed by another literal now keeps a finalizing store or a В↑
// separator so the two values no longer merge on the MK-61 (e.g. 1 then 3 -> 13).
const PENDING_BASELINE: Record<string, number> = {
  "cave-highlevel-baseline": 253,
  "cave-treasure": 294,
  "giants-country": 225,
  labyrinth777: 242,
  teleport: 242,
  "tic-tac-toe-4x4": 295,
  "treasure-hunter-2": 185,
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
