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
  it("compiles basic.m61 deterministically", () => {
    const result = compileM61(loadExample("basic"));
    expect(formatListing(result)).toMatchSnapshot("basic.listing");
    expect(formatHex(result)).toMatchSnapshot("basic.hex");
    expect(formatExplain(result)).toMatchSnapshot("basic.explain");
  });

  it("compiles tiny-game.m61 deterministically", () => {
    const result = compileM61(loadExample("tiny-game"));
    expect(formatListing(result)).toMatchSnapshot("tiny-game.listing");
    expect(formatHex(result)).toMatchSnapshot("tiny-game.hex");
  });

  it("compiles lunar.m61 deterministically", () => {
    const result = compileM61(loadExample("lunar"));
    expect(formatListing(result)).toMatchSnapshot("lunar.listing");
    expect(formatHex(result)).toMatchSnapshot("lunar.hex");
    expect(result.report.steps).toBeLessThanOrEqual(105);
  });

  it("safe optimizer keeps egg blocks out", () => {
    const safe = compileM61(loadExample("tiny-game"), { opt: "safe" });
    expect(formatHex(safe)).toMatchSnapshot("tiny-game.safe.hex");
    expect(safe.report.warnings.some((w) => w.includes("Skipped egg"))).toBe(true);
  });
});
