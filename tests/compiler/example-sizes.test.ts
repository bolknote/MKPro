import { readdirSync, readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";

// Exact size baseline. Before adding a size optimization, lock the current
// compiled cell count here; when a program intentionally gets smaller, update
// the matching number in the same change so the diff shows the saving clearly.
//
// Cell count is taken from `report.steps`, which equals the number of program
// cells emitted by `mk-pro compile ... --out hex`.

const EXAMPLE_BASELINE: Record<string, number> = {
  "99-bottles": 53,
  alaram: 75,
  basic: 8,
  "cave-sketch": 47,
  clock: 43,
  "dangerous-loading": 84,
  dungeon: 82,
  "e-94-digits": 64,
  "functions-demo": 28,
  "fox-hunt-100": 103,
  "fox-hunt-mk61": 65,
  "game-100-pig": 103,
  human: 23,
  "jack-pot": 99,
  labyrinth777: 105,
  lunar: 47,
  "minesweeper-9x7": 83,
  "minesweeper-9x9": 81,
  "raja-yoga": 87,
  "rambo-iii": 105,
  "sea-battle": 80,
  teleport: 97,
  "tiny-game": 23,
  "treasure-hunter-2": 100,
  wumpus: 100,
};

const EXAMPLE_COMPILE_ERRORS: Record<string, RegExp> = {};

// pending-optimizer programs may overflow the physical MK-61 address space, so
// they are measured with a large budget and analysis mode, mirroring
// `mk-pro compile ... --out hex --budget 999999 --analysis`. The goal is to
// shrink these toward 105; intentional shrinkage should update these exact
// baselines just like top-level examples.
const PENDING_BASELINE: Record<string, number> = {
  "cave-highlevel-baseline": 135,
  "cave-treasure": 141,
  "giants-country": 105,
  "tic-tac-toe-4x4": 258,
};

const PENDING_COMPILE_ERRORS: Record<string, RegExp> = {};

function exampleSteps(relativePath: string, analysis: boolean): number {
  const source = readFileSync(resolve(`examples/${relativePath}.mkpro`), "utf8");
  const options = analysis ? { budget: 999999, analysis: true } : {};
  return compileMKPro(source, options).report.steps;
}

function exampleNames(relativeDir: string): string[] {
  return readdirSync(resolve("examples", relativeDir), { withFileTypes: true })
    .filter((entry) => entry.isFile() && entry.name.endsWith(".mkpro"))
    .map((entry) => entry.name.replace(/\.mkpro$/u, ""))
    .sort();
}

describe("example size guard", () => {
  it("records every top-level example", () => {
    expect(Object.keys(EXAMPLE_BASELINE).sort()).toEqual(exampleNames("."));
  });

  for (const [name, baseline] of Object.entries(EXAMPLE_BASELINE)) {
    it(`${name}.mkpro is locked at ${baseline} cells`, () => {
      expect(exampleSteps(name, false)).toBe(baseline);
    }, 15000);
  }

  for (const [name, message] of Object.entries(EXAMPLE_COMPILE_ERRORS)) {
    it(`${name}.mkpro records the current compile blocker`, () => {
      expect(() => exampleSteps(name, false)).toThrow(message);
    });
  }

  it("records every pending-optimizer example", () => {
    expect(Object.keys(PENDING_BASELINE).sort()).toEqual(exampleNames("pending-optimizer"));
  });

  for (const [name, baseline] of Object.entries(PENDING_BASELINE)) {
    it(`pending-optimizer/${name}.mkpro is locked at ${baseline} cells`, () => {
      expect(exampleSteps(`pending-optimizer/${name}`, true)).toBe(baseline);
    }, 15000);
  }

  for (const [name, message] of Object.entries(PENDING_COMPILE_ERRORS)) {
    it(`pending-optimizer/${name}.mkpro records the current compile blocker`, () => {
      expect(() => exampleSteps(`pending-optimizer/${name}`, true)).toThrow(message);
    });
  }
});
