import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import {
  compileMKPro,
  formatExplain,
  formatHex,
  formatListing,
} from "../../src/core/index.ts";

function loadExample(name: string): string {
  return readFileSync(resolve(`examples/${name}.mkpro`), "utf8");
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
    expect(formatListing(result)).toContain("show landed");
    expect(formatHex(result).length).toBeGreaterThan(0);
    expect(result.report.steps).toBeLessThanOrEqual(105);
  });

  it("always uses maximum optimizer defaults for tiny-game", () => {
    const result = compileMKPro(loadExample("tiny-game"));
    expect(formatHex(result).length).toBeGreaterThan(0);
    expect(result.report.candidates.some((candidate) => candidate.variant === "fallthrough-compare-chain" && candidate.selected)).toBe(true);
    expect(formatExplain(result)).not.toMatch(/unsafe/u);
  });
});
