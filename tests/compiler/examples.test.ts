import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import {
  compileMKPro,
  formatExplain,
  formatHex,
  formatListing,
} from "../../src/core/index.ts";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
  programCounter(): string | number;
  readRegister(register: string): string;
}

type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;

const { MK61, parseProgramText } = require("../emulator/mk61.cjs") as {
  MK61: Mk61Constructor;
  parseProgramText: (source: string) => { codes: number[]; diagnostics: string[] };
};

function loadExample(name: string): string {
  return readFileSync(resolve(`examples/${name}.mkpro`), "utf8");
}

function runE94Observable(codes: number[]): {
  display: string;
  pc: string | number;
  stopped: boolean;
  registers: Record<string, string>;
} {
  const calc = new MK61({ extended: true });
  expect(calc.loadProgram(codes).diagnostics).toEqual([]);
  calc.pressSequence(["В/О", "С/П"]);
  const run = calc.runUntilStable({ maxFrames: 200000, stableFrames: 10 });
  const registers = Object.fromEntries(
    ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"]
      .map((register) => [register, calc.readRegister(register).trim()]),
  );
  return {
    display: calc.displayText().trim(),
    pc: calc.programCounter(),
    stopped: run.stopped,
    registers,
  };
}

describe("examples", () => {
  it("compiles basic.mkpro through the V2 report path", () => {
    const result = compileMKPro(loadExample("basic"));
    const listing = formatListing(result);
    const hex = formatHex(result);
    const explain = formatExplain(result);

    expect(listing).toContain("read x");
    expect(hex).toBe("00: 50 41 50 61 10 50 51 00");
    expect(explain).toContain("Intent IR: lowered=yes, v2=yes");
  });

  it("compiles tiny-game.mkpro under budget", () => {
    const result = compileMKPro(loadExample("tiny-game"));
    expect(formatListing(result)).toContain("read key");
    expect(formatHex(result).length).toBeGreaterThan(0);
    expect(result.report.steps).toBeLessThanOrEqual(105);
  });

  it("compiles lunar.mkpro under budget", () => {
    const result = compileMKPro(loadExample("lunar"));
    expect(formatListing(result)).toContain("show height_view");
    expect(formatListing(result)).toContain("show speed_view");
    expect(formatListing(result)).toContain("show fuel_view");
    expect(formatHex(result).length).toBeGreaterThan(0);
    expect(result.report.steps).toBeLessThanOrEqual(105);
  });

  it("compiles e-94-digits.mkpro from a high-level decimal recurrence", () => {
    const source = loadExample("e-94-digits");
    expect(source).not.toMatch(/\b(?:raw|listing|machine|decimal|e_digits)\b/iu);

    const result = compileMKPro(source, { analysis: true });

    expect(result.report.reference?.referenceSpan).toBe(64);
    expect(result.report.steps).toBeLessThanOrEqual(result.report.reference!.referenceSpan);
    expect(result.report.optimizations.some((item) => item.name === "decimal-factorial-series-lowering")).toBe(true);

    const reference = parseProgramText(readFileSync(resolve("games/anvarov/e-94-digits.txt"), "utf8"));
    expect(reference.diagnostics).toEqual([]);
    expect(runE94Observable(result.steps.map((step) => step.opcode))).toEqual(runE94Observable(reference.codes));
  });

  it("always uses maximum optimizer defaults for tiny-game", () => {
    const result = compileMKPro(loadExample("tiny-game"));
    expect(formatHex(result).length).toBeGreaterThan(0);
    expect(result.report.candidates.some((candidate) => candidate.variant === "fallthrough-compare-chain" && candidate.selected)).toBe(true);
    expect(formatExplain(result)).not.toMatch(/unsafe/u);
  });
});
