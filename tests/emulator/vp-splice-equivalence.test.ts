import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { runIrPasses } from "../../src/core/passes/index.ts";
import { raiseMachineToIr } from "../../src/core/ir.ts";
import type { MachineItem } from "../../src/core/types.ts";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

function display(codes: number[]): string {
  const calc = new MK61();
  calc.loadProgram(codes);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 300, stableFrames: 4 });
  return calc.displayText().replace(/\s/gu, "");
}

const VP = 0x0c;
const KNOP = 0x54;
const K1 = 0x55;
const K2 = 0x56;
const DOT = 0x0a;
const SIGN_CHANGE = 0x0b;
const CLEAR_X = 0x0d;
const FPI = 0x20;
const STORE1 = 0x41;
const F0 = 0xf0;
const STOP = 0x50;
const RETURN = 0x52;
const CALL = 0x53;

describe("ВП exponent-entry splice collapse (vp-splice)", () => {
  it("ВП ВП and empty-op ВП produce the same value as a single ВП on the emulator", () => {
    // 5 ВП 3 == 5e3
    expect(display([0x05, VP, 0x03, STOP])).toContain("5000");
    // 5 ВП ВП 3 collapses to the same result.
    expect(display([0x05, VP, VP, 0x03, STOP])).toContain("5000");
    // 5 КНОП ВП 3 collapses to the same result.
    expect(display([0x05, KNOP, VP, 0x03, STOP])).toContain("5000");
    // К1 and К2 are the same empty-op class for this exponent-entry boundary.
    expect(display([0x05, K1, VP, 0x03, STOP])).toContain("5000");
    expect(display([0x05, K2, VP, 0x03, STOP])).toContain("5000");

    // The same empty op is inert when a transparent subroutine return is the
    // only thing between it and ВП: В/О resynchronizes the same X into X2.
    expect(display([0x02, K1, CALL, 0x07, VP, 0x03, STOP, KNOP, RETURN])).toBe(
      display([0x02, CALL, 0x06, VP, 0x03, STOP, KNOP, RETURN]),
    );
  });

  it("the pass rewrites a freestanding ВП ВП / empty-op ВП run to a single ВП", () => {
    const program: MachineItem[] = [
      { kind: "op", opcode: 0x05, mnemonic: "5" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: KNOP, mnemonic: "КНОП" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: K1, mnemonic: "К 1" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: K2, mnemonic: "К 2" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    // Sanity: the synthetic program raises cleanly into IR.
    expect(raiseMachineToIr(program).length).toBeGreaterThan(0);

    const result = runIrPasses(program, { delivery: "hex", budget: 105, analysis: false });
    const names = result.optimizations.map((optimization) => optimization.name);
    expect(names).toContain("vp-exponent-splice");

    const codes = result.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    // The four redundant cells (one extra ВП and three empty ops) are gone.
    expect(codes.filter((opcode) => opcode === VP).length).toBe(1);
    expect(codes.filter((opcode) => opcode === KNOP).length).toBe(0);
    expect(codes.filter((opcode) => opcode === K1).length).toBe(0);
    expect(codes.filter((opcode) => opcode === K2).length).toBe(0);

    // The collapsed program still computes 5e3.
    expect(display(codes)).toContain("5000");
  });

  it("empty ops after an exponent digit are removable before non-digit commands", () => {
    // The separator is inert once exponent entry already has a digit and the
    // next command is not another digit.
    expect(display([0x05, VP, 0x03, KNOP, STOP])).toBe(display([0x05, VP, 0x03, STOP]));
    expect(display([0x05, VP, 0x03, K1, STOP])).toBe(display([0x05, VP, 0x03, STOP]));
    expect(display([0x05, VP, 0x03, K2, STOP])).toBe(display([0x05, VP, 0x03, STOP]));
    expect(display([0x05, VP, 0x03, KNOP, SIGN_CHANGE, STOP])).toBe(
      display([0x05, VP, 0x03, SIGN_CHANGE, STOP]),
    );
    expect(display([0x05, VP, 0x03, KNOP, DOT, STOP])).toBe(display([0x05, VP, 0x03, DOT, STOP]));

    // Before another digit, the same empty op changes the number-entry shape and
    // must stay.
    expect(display([0x05, VP, 0x03, KNOP, 0x04, STOP])).not.toBe(display([0x05, VP, 0x03, 0x04, STOP]));
  });

  it("the pass removes an exponent-digit empty separator but keeps one before another digit", () => {
    const removable: MachineItem[] = [
      { kind: "op", opcode: 0x05, mnemonic: "5" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: KNOP, mnemonic: "КНОП" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const removedResult = runIrPasses(removable, { delivery: "hex", budget: 105, analysis: false });
    const removedCodes = removedResult.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(removedCodes).toEqual([0x05, VP, 0x03, SIGN_CHANGE, STOP]);
    expect(display(removedCodes)).toBe(display([0x05, VP, 0x03, KNOP, SIGN_CHANGE, STOP]));

    const kept: MachineItem[] = [
      { kind: "op", opcode: 0x05, mnemonic: "5" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: KNOP, mnemonic: "КНОП" },
      { kind: "op", opcode: 0x04, mnemonic: "4" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const keptResult = runIrPasses(kept, { delivery: "hex", budget: 105, analysis: false });
    const keptCodes = keptResult.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(keptCodes).toEqual([0x05, VP, 0x03, KNOP, 0x04, STOP]);
  });

  it("empty ops before VP-context /-/ remain removable after X2-preserving gaps", () => {
    expect(display([0x01, 0x02, VP, 0x03, FPI, KNOP, SIGN_CHANGE, STOP])).toBe(
      display([0x01, 0x02, VP, 0x03, FPI, SIGN_CHANGE, STOP]),
    );
    expect(display([0x05, VP, 0x03, FPI, SIGN_CHANGE, SIGN_CHANGE, 0x04, STOP])).toBe(
      display([0x05, VP, 0x03, FPI, 0x04, STOP]),
    );
    expect(display([0x05, VP, 0x03, FPI, SIGN_CHANGE, SIGN_CHANGE, KNOP, 0x04, STOP])).toBe(
      display([0x05, VP, 0x03, FPI, KNOP, 0x04, STOP]),
    );
    expect(display([0x05, VP, 0x03, FPI, SIGN_CHANGE, 0x04, STOP])).toBe(
      display([0x05, VP, 0x03, FPI, 0x04, STOP]),
    );
    expect(display([0x05, VP, 0x03, FPI, SIGN_CHANGE, KNOP, 0x04, STOP])).toBe(
      display([0x05, VP, 0x03, FPI, KNOP, 0x04, STOP]),
    );
    expect(display([0x05, VP, 0x03, FPI, SIGN_CHANGE, SIGN_CHANGE, STOP])).not.toBe(
      display([0x05, VP, 0x03, FPI, STOP]),
    );
    expect(display([0x05, VP, SIGN_CHANGE, 0x04, STOP])).not.toBe(display([0x05, VP, 0x04, STOP]));
    expect(display([0x05, VP, 0x03, FPI, SIGN_CHANGE, STOP])).not.toBe(
      display([0x05, VP, 0x03, FPI, STOP]),
    );

    const program: MachineItem[] = [
      { kind: "op", opcode: 0x01, mnemonic: "1" },
      { kind: "op", opcode: 0x02, mnemonic: "2" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: FPI, mnemonic: "Fπ" },
      { kind: "op", opcode: KNOP, mnemonic: "КНОП" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const result = runIrPasses(program, { delivery: "hex", budget: 105, analysis: false });
    const codes = result.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(codes).toEqual([0x01, 0x02, VP, 0x03, FPI, SIGN_CHANGE, STOP]);
    expect(display(codes)).toBe(display([0x01, 0x02, VP, 0x03, FPI, KNOP, SIGN_CHANGE, STOP]));

    const signPairBeforeDigit: MachineItem[] = [
      { kind: "op", opcode: 0x05, mnemonic: "5" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: FPI, mnemonic: "Fπ" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: 0x04, mnemonic: "4" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const pairResult = runIrPasses(signPairBeforeDigit, { delivery: "hex", budget: 105, analysis: false });
    const pairCodes = pairResult.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(pairCodes).toEqual([0x05, VP, 0x03, FPI, 0x04, STOP]);
    expect(display(pairCodes)).toBe(display([0x05, VP, 0x03, FPI, SIGN_CHANGE, SIGN_CHANGE, 0x04, STOP]));

    const signPairBeforeEmptyDigit: MachineItem[] = [
      { kind: "op", opcode: 0x05, mnemonic: "5" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: FPI, mnemonic: "Fπ" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: KNOP, mnemonic: "КНОП" },
      { kind: "op", opcode: 0x04, mnemonic: "4" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const emptyDigitResult = runIrPasses(signPairBeforeEmptyDigit, { delivery: "hex", budget: 105, analysis: false });
    const emptyDigitCodes = emptyDigitResult.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(emptyDigitCodes).toEqual([0x05, VP, 0x03, FPI, 0x04, STOP]);
    expect(display(emptyDigitCodes)).toBe(
      display([0x05, VP, 0x03, FPI, SIGN_CHANGE, SIGN_CHANGE, KNOP, 0x04, STOP]),
    );

    const singleSignBeforeEmptyDigit: MachineItem[] = [
      { kind: "op", opcode: 0x05, mnemonic: "5" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: FPI, mnemonic: "Fπ" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: KNOP, mnemonic: "КНОП" },
      { kind: "op", opcode: 0x04, mnemonic: "4" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const singleResult = runIrPasses(singleSignBeforeEmptyDigit, { delivery: "hex", budget: 105, analysis: false });
    const singleCodes = singleResult.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(singleCodes).toEqual([0x05, VP, 0x03, FPI, 0x04, STOP]);
    expect(display(singleCodes)).toBe(display([0x05, VP, 0x03, FPI, SIGN_CHANGE, KNOP, 0x04, STOP]));
  });

  it("VP-context /-/ before a dead X2 overwrite is removable", () => {
    expect(display([0x05, VP, 0x03, FPI, SIGN_CHANGE, CLEAR_X, STOP])).toBe(
      display([0x05, VP, 0x03, FPI, CLEAR_X, STOP]),
    );
    expect(display([0x05, VP, SIGN_CHANGE, KNOP, CLEAR_X, STOP])).toBe(
      display([0x05, VP, KNOP, CLEAR_X, STOP]),
    );
    expect(display([0x05, VP, K1, K2, CLEAR_X, 0x10, STOP])).toBe(
      display([0x05, VP, CLEAR_X, 0x10, STOP]),
    );
    // The restored X is still dead even when a later binary op reads Y.
    expect(display([0x05, VP, 0x03, FPI, SIGN_CHANGE, CLEAR_X, 0x10, STOP])).toBe(
      display([0x05, VP, 0x03, FPI, CLEAR_X, 0x10, STOP]),
    );

    const program: MachineItem[] = [
      { kind: "op", opcode: 0x05, mnemonic: "5" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: FPI, mnemonic: "Fπ" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: CLEAR_X, mnemonic: "Cx" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const result = runIrPasses(program, { delivery: "hex", budget: 105, analysis: false });
    const codes = result.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(codes).toEqual([0x05, VP, 0x03, CLEAR_X, STOP]);
    expect(display(codes)).toBe(display([0x05, VP, 0x03, FPI, SIGN_CHANGE, CLEAR_X, STOP]));

    const emptyProgram: MachineItem[] = [
      { kind: "op", opcode: 0x05, mnemonic: "5" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: K1, mnemonic: "К1" },
      { kind: "op", opcode: K2, mnemonic: "К2" },
      { kind: "op", opcode: CLEAR_X, mnemonic: "Cx" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const emptyResult = runIrPasses(emptyProgram, { delivery: "hex", budget: 105, analysis: false });
    const emptyCodes = emptyResult.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(emptyCodes).toEqual([0x05, CLEAR_X, STOP]);
    expect(display(emptyCodes)).toBe(display([0x05, VP, K1, K2, CLEAR_X, STOP]));
  });

  it("closed decimal /-/ /-/ pairs collapse only away from VP restore context", () => {
    expect(display([0x00, 0x02, F0, SIGN_CHANGE, SIGN_CHANGE, STOP])).toBe(
      display([0x00, 0x02, F0, STOP]),
    );

    const removable: MachineItem[] = [
      { kind: "op", opcode: 0x00, mnemonic: "0" },
      { kind: "op", opcode: 0x02, mnemonic: "2" },
      { kind: "op", opcode: F0, mnemonic: "F* empty F0" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const removedResult = runIrPasses(removable, { delivery: "hex", budget: 105, analysis: false });
    const removedCodes = removedResult.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(removedCodes).toEqual([0x00, 0x02, F0, STOP]);
    expect(display(removedCodes)).toBe(display([0x00, 0x02, F0, SIGN_CHANGE, SIGN_CHANGE, STOP]));

    // The same-looking pair cannot be dropped before ВП: it changes the
    // previous-command source used by the X2 restore.
    expect(display([CLEAR_X, SIGN_CHANGE, SIGN_CHANGE, VP, STOP])).not.toBe(display([CLEAR_X, VP, STOP]));
    const kept: MachineItem[] = [
      { kind: "op", opcode: CLEAR_X, mnemonic: "Cx" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const keptResult = runIrPasses(kept, { delivery: "hex", budget: 105, analysis: false });
    const keptCodes = keptResult.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(keptCodes).toEqual([CLEAR_X, SIGN_CHANGE, SIGN_CHANGE, VP, STOP]);
  });

  it("closed-context restore runs collapse before fresh digit entry", () => {
    expect(display([0x02, F0, SIGN_CHANGE, 0x03, STOP])).toBe(display([0x02, F0, 0x03, STOP]));
    expect(display([0x02, F0, KNOP, SIGN_CHANGE, K1, 0x03, STOP])).toBe(
      display([0x02, F0, 0x03, STOP]),
    );
    expect(display([0x02, F0, SIGN_CHANGE, VP, 0x03, STOP])).not.toBe(
      display([0x02, F0, VP, 0x03, STOP]),
    );

    const program: MachineItem[] = [
      { kind: "op", opcode: 0x02, mnemonic: "2" },
      { kind: "op", opcode: F0, mnemonic: "F* empty F0" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: KNOP, mnemonic: "КНОП" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const result = runIrPasses(program, { delivery: "hex", budget: 105, analysis: false });
    const codes = result.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(codes).toEqual([0x02, F0, 0x03, STOP]);
    expect(display(codes)).toBe(display([0x02, F0, SIGN_CHANGE, KNOP, SIGN_CHANGE, 0x03, STOP]));
  });

  it("closed decimal X2-sync before ВП exposes exponent-entry sign toggles", () => {
    expect(display([0x02, F0, VP, SIGN_CHANGE, SIGN_CHANGE, 0x03, STOP])).toBe(
      display([0x02, F0, VP, 0x03, STOP]),
    );
    expect(display([0x02, F0, SIGN_CHANGE, SIGN_CHANGE, VP, 0x03, STOP])).toBe(
      display([0x02, F0, VP, 0x03, STOP]),
    );
    expect(display([0x00, 0x02, SIGN_CHANGE, SIGN_CHANGE, VP, 0x03, STOP])).toBe(
      display([0x00, 0x02, VP, 0x03, STOP]),
    );
    expect(display([CLEAR_X, SIGN_CHANGE, SIGN_CHANGE, VP, 0x03, STOP])).not.toBe(
      display([CLEAR_X, VP, 0x03, STOP]),
    );
    expect(display([0x00, SIGN_CHANGE, SIGN_CHANGE, VP, 0x03, STOP])).not.toBe(
      display([0x00, VP, 0x03, STOP]),
    );
    // `X->П` immediately before ВП changes the previous-command context, so
    // the decimal sync proof cannot be inferred just from `X == X2`.
    expect(display([0x02, F0, STORE1, VP, 0x03, STOP])).not.toBe(display([0x02, F0, VP, 0x03, STOP]));

    const program: MachineItem[] = [
      { kind: "op", opcode: 0x02, mnemonic: "2" },
      { kind: "op", opcode: F0, mnemonic: "F* empty F0" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const result = runIrPasses(program, { delivery: "hex", budget: 105, analysis: false });
    const codes = result.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(codes).toEqual([0x02, F0, VP, 0x03, STOP]);
    expect(display(codes)).toBe(display([0x02, F0, VP, SIGN_CHANGE, SIGN_CHANGE, 0x03, STOP]));

    const beforeVpPair: MachineItem[] = [
      { kind: "op", opcode: 0x02, mnemonic: "2" },
      { kind: "op", opcode: F0, mnemonic: "F* empty F0" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const beforeVpResult = runIrPasses(beforeVpPair, { delivery: "hex", budget: 105, analysis: false });
    const beforeVpCodes = beforeVpResult.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(beforeVpCodes).toEqual([0x02, F0, VP, 0x03, STOP]);
    expect(display(beforeVpCodes)).toBe(display([0x02, F0, SIGN_CHANGE, SIGN_CHANGE, VP, 0x03, STOP]));

    const openBeforeVpPair: MachineItem[] = [
      { kind: "op", opcode: 0x00, mnemonic: "0" },
      { kind: "op", opcode: 0x02, mnemonic: "2" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: SIGN_CHANGE, mnemonic: "/-/" },
      { kind: "op", opcode: VP, mnemonic: "ВП" },
      { kind: "op", opcode: 0x03, mnemonic: "3" },
      { kind: "op", opcode: STOP, mnemonic: "С/П" },
    ];
    const openBeforeVpResult = runIrPasses(openBeforeVpPair, { delivery: "hex", budget: 105, analysis: false });
    const openBeforeVpCodes = openBeforeVpResult.items
      .filter((item): item is Extract<MachineItem, { opcode: number }> => "opcode" in item)
      .map((item) => item.opcode);
    expect(openBeforeVpCodes).toEqual([0x00, 0x02, VP, 0x03, STOP]);
    expect(display(openBeforeVpCodes)).toBe(display([0x00, 0x02, SIGN_CHANGE, SIGN_CHANGE, VP, 0x03, STOP]));
  });
});
