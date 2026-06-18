import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import {
  compileMKPro,
  formatExplain,
  formatKeys,
  formatListing,
  formatProgramPatch,
  formatSetupBlock,
  type CompileResult,
} from "../../src/core/index.ts";
import { buildManualProgramPatchReport } from "../../src/core/program-patch.ts";

describe("setup formatting", () => {
  it("turns high-level state initializers into an emulator setup block", () => {
    const source = readFileSync(resolve("examples/game-100-pig.mkpro"), "utf8");
    const result = compileMKPro(source);

    expect(formatSetupBlock(result)).toBe("`R5=0; R0=0; R7=0; R4=0; R6=0; Ra=20; Rb=100; Rc=1E3; Rd=1E7; Re=8,-00-000; R8=L3; R9=С4`");
    expect(formatExplain(result)).toContain("player_total -> R5: 0");
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
    const keys = formatKeys(compileMKPro(source, { budget: 999, analysis: true }));

    expect(keys).not.toContain("`");
    expect(keys.split(/\r?\n/u)[0]).toBe("1");
  });

  it("shows generated setup in the default listing output", () => {
    const source = readFileSync(resolve("examples/wumpus.mkpro"), "utf8");
    const listing = formatListing(compileMKPro(source, { budget: 999, analysis: true }));

    expect(listing).toContain("# Setup Listing");
    expect(listing).toContain("setup wumpus");
    expect(listing).toContain("# Main Listing");
  });

  it("shows setup-block preload keys in over-budget analysis listings", () => {
    const source = readFileSync(resolve("examples/rambo-iii.mkpro"), "utf8");
    const listing = formatListing(compileMKPro(source, { analysis: true }));

    expect(listing).toContain("Setup Block:");
    expect(listing).toContain(
      "`R0=25; R8=0.5; R1=5000.999; R2=5000.999; R3=5000.999; R4=0; R5=0; R6=0; R7=25000; Re=8.1020088E14; Ra=97; Rb=Е4`",
    );
    expect(listing).toContain("# Setup Listing");
    expect(listing).toContain("   00 |   -  | 25");
    expect(listing).toContain("   02 |   -  | 0.5");
    expect(listing).toContain("   04 |   -  | 5000.999");
    expect(listing).toContain("   05 |  41  | X→П 1");
    expect(listing).toContain("   06 |  42  | X→П 2");
    expect(listing).toContain("   07 |  43  | X→П 3");
    expect(listing).toContain("   12 |   -  | 25000");
    expect(listing).toContain("   15 |  4E  | X→П e");
    expect(listing).toContain("# Main Listing");
  });

  it("collapses multi-key number entry in main listings", () => {
    const result = {
      steps: [
        { address: 0, opcode: 0x02, hex: "02", mnemonic: "2" },
        { address: 1, opcode: 0x05, hex: "05", mnemonic: "5" },
        { address: 2, opcode: 0x51, hex: "51", mnemonic: "БП", comment: "loop back" },
        { address: 3, opcode: 0x00, hex: "00", mnemonic: "00", comment: "loop back" },
        { address: 4, opcode: 0x01, hex: "01", mnemonic: "1" },
        { address: 5, opcode: 0x02, hex: "02", mnemonic: "2" },
        { address: 6, opcode: 0x03, hex: "03", mnemonic: "3" },
        { address: 7, opcode: 0x04, hex: "04", mnemonic: "4" },
        { address: 8, opcode: 0x05, hex: "05", mnemonic: "5" },
      ],
      report: {
        preloads: [],
      },
    } as unknown as CompileResult;
    const listing = formatListing(result);

    expect(listing).toContain("   00 | 02 05     | 25");
    expect(listing).not.toContain("   01 |  05  | 5");
    expect(listing).toContain("   03 | 00        | 00");
    expect(listing).toContain("   04 | 01 ... 05 | 12345");
  });

  it("shows startup stack inputs inside the setup listing", () => {
    const source = readFileSync(resolve("examples/99-bottles.mkpro"), "utf8");
    const result = compileMKPro(source);
    const listing = formatListing(result);
    const keys = formatKeys(result);

    expect(listing).toContain("# Setup Listing");
    expect(listing).toContain("   -- |   -  | enter bottles");
    expect(listing).toContain("enter any value 0..99 in X");
    expect(listing).toContain("X→П 0");
    expect(listing).toContain("# Main Listing");
    expect(keys.split(/\r?\n/u).slice(0, 5)).toEqual([
      "X→П 0",
      "С/П",
      "В/О",
      "<enter bottles: any value 0..99 in X>",
      "С/П",
    ]);
  });

  it("prints manual patch keys for patchable F-prefixed opcodes", () => {
    const steps = Array.from({ length: 54 }, (_, address) => ({
      address,
      opcode: address === 53 ? 0xf7 : 0x54,
      hex: address === 53 ? "F7" : "54",
      mnemonic: address === 53 ? "F* empty F7" : "К НОП",
    }));
    const result = {
      ast: {},
      steps,
      report: {
        preloads: [],
        programPatch: buildManualProgramPatchReport(steps),
      },
    } as unknown as CompileResult;

    const listing = formatListing(result);
    const keys = formatKeys(result);

    expect(listing).toContain("# Patch Listing");
    expect(listing).toContain("placeholder for F7; apply Patch Listing");
    expect(listing).toContain("F АВТ ; 5 ; 0 ; F 10ˣ ; F x² ; ВП ; 3 ; 7 ; . ; 0");
    expect(formatProgramPatch(result)).toContain("F7");
    expect(keys).toContain("<patch F7 at 53: egg-f-prefix>");
    expect(keys.split(/\r?\n/u).slice(52, 56)).toEqual([
      "К НОП",
      "К НОП",
      "<patch F7 at 53: egg-f-prefix>",
      "F АВТ",
    ]);
  });

  it("builds patch reports after final manual layout", () => {
    const filler = Array.from({ length: 50 }, () => "К НОП").join("\n");
    const result = compileMKPro(`
program PatchDemo {
  loop {
    raw {
      clobbers X
      preserves state
      code {
${filler}
F2
      }
    }
    halt()
  }
}
`, { delivery: "manual", budget: 999, analysis: true });

    expect(result.report.programPatch?.steps[0]).toMatchObject({
      address: 50,
      hex: "F2",
      method: "egg-f-prefix",
    });
    expect(formatListing(result)).toContain("placeholder for F2; apply Patch Listing");
  });

  it("derives manual patch keys for keys output even without manual delivery", () => {
    const result = {
      ast: {},
      steps: [
        { address: 50, opcode: 0xf2, hex: "F2", mnemonic: "F* empty F2" },
      ],
      report: {
        preloads: [],
      },
    } as unknown as CompileResult;

    expect(formatKeys(result).split(/\r?\n/u).slice(0, 12)).toEqual([
      "К НОП",
      "<patch F2 at 50: egg-f-prefix>",
      "F АВТ",
      "5",
      "0",
      "F 10ˣ",
      "F x²",
      "ВП",
      "0",
      "2",
      ".",
      "0",
    ]);
  });

  it("warns in keys output when no manual patch sequence is known", () => {
    const result = {
      ast: {},
      steps: [
        { address: 12, opcode: 0x3e, hex: "3E", mnemonic: "Y->X" },
      ],
      report: {
        preloads: [],
      },
    } as unknown as CompileResult;

    expect(formatKeys(result)).toContain("<warning: No supported manual patch sequence for 3E at 12.>");
  });

  it("exports the literal error trap as the keyboard-enterable К ÷ with no patch warning", () => {
    const result = compileMKPro(`
program LiteralErrorStop {
  loop {
    show("ЕГГОГ")
    halt()
  }
}
`);

    const keys = formatKeys(result);
    expect(keys.split(/\r?\n/u)).toContain("К ÷");
    expect(keys).not.toContain("К /");
    expect(keys).not.toContain("No supported manual patch sequence");
  });
});
