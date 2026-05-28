import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import {
  compileMKPro,
  formatExplain,
  formatKeys,
  formatListing,
  formatSetupBlock,
  type CompileResult,
} from "../../src/core/index.ts";

describe("setup formatting", () => {
  it("turns high-level state initializers into an emulator setup block", () => {
    const source = readFileSync(resolve("examples/game-100-pig.mkpro"), "utf8");
    const result = compileMKPro(source);

    expect(formatSetupBlock(result)).toBe("`R3=0; R0=0; R6=0; R5=0; R4=0; Rc=20; Rd=100; Re=-100`");
    expect(formatExplain(result)).toContain("player_total -> R3: 0");
    expect(formatExplain(result)).toContain("Setup Block:");
  });

  it("renders formal-address preloads in the calculator display alphabet", () => {
    const result = {
      report: {
        preloads: [
          { register: "7", value: "C5", countsAgainstProgram: false },
          { register: "8", value: "FF", countsAgainstProgram: false },
          { register: "9", value: "1E-3", countsAgainstProgram: false },
          { register: "a", value: "1|-00", countsAgainstProgram: false },
        ],
      },
    } as CompileResult;

    expect(formatSetupBlock(result)).toBe("`R7=С5; R8=__; R9=1E-3`");
  });

  it("keeps keys output free of emulator setup-block syntax", () => {
    const source = readFileSync(resolve("examples/wumpus.mkpro"), "utf8");
    const keys = formatKeys(compileMKPro(source));

    expect(keys).not.toContain("`");
    expect(keys.split(/\r?\n/u)[0]).toBe("1");
  });

  it("shows generated setup in the default listing output", () => {
    const source = readFileSync(resolve("examples/wumpus.mkpro"), "utf8");
    const listing = formatListing(compileMKPro(source));

    expect(listing).toContain("# Setup Listing");
    expect(listing).toContain("setup wumpus");
    expect(listing).toContain("# Main Listing");
  });
});
