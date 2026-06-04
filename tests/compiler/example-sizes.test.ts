import { readdirSync, readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";
import { EXAMPLE_BASELINE, PENDING_BASELINE } from "./example-baselines.ts";

// Exact size baseline lives in `example-baselines.ts` so the `examples:check`
// no-regression script can share it. This test locks each program at its exact
// baseline; the script enforces the weaker `<= baseline` guard for ad-hoc runs.

const EXAMPLE_COMPILE_ERRORS: Record<string, RegExp> = {};

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
    }, 30000);
  }

  for (const [name, message] of Object.entries(PENDING_COMPILE_ERRORS)) {
    it(`pending-optimizer/${name}.mkpro records the current compile blocker`, () => {
      expect(() => exampleSteps(`pending-optimizer/${name}`, true)).toThrow(message);
    });
  }
});
