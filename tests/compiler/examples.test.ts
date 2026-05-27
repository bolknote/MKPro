import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import {
  compileM61,
  formatExplain,
  formatHex,
  formatListing,
} from "../../src/core/index.ts";

function loadExample(name: string): string {
  return readFileSync(resolve(`examples/${name}.m61`), "utf8");
}

describe("examples", () => {
  it("compiles basic.m61 through the V2 report path", () => {
    const result = compileM61(loadExample("basic"));
    const listing = formatListing(result);
    const hex = formatHex(result);
    const explain = formatExplain(result);

    expect(listing).toContain("input digit x");
    expect(hex).toBe("00: 50 40 50 60 10 40 50 60\n08: 50 51 00");
    expect(explain).toContain("Intent IR: lowered=yes, v2=yes");
  });

  it("compiles tiny-game.m61 under budget", () => {
    const result = compileM61(loadExample("tiny-game"));
    expect(formatListing(result)).toContain("input digit key");
    expect(formatHex(result).length).toBeGreaterThan(0);
    expect(result.report.steps).toBeLessThanOrEqual(105);
  });

  it("compiles lunar.m61 under budget", () => {
    const result = compileM61(loadExample("lunar"));
    expect(formatListing(result)).toContain("show landed");
    expect(formatHex(result).length).toBeGreaterThan(0);
    expect(result.report.steps).toBeLessThanOrEqual(105);
  });

  it("always uses maximum optimizer defaults for tiny-game", () => {
    const result = compileM61(loadExample("tiny-game"));
    expect(formatHex(result).length).toBeGreaterThan(0);
    expect(result.report.candidates.some((candidate) => candidate.variant === "fallthrough-compare-chain" && candidate.selected)).toBe(true);
    expect(formatExplain(result)).not.toMatch(/unsafe/u);
  });
});
