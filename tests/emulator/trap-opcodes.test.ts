import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  setRegister(register: string, value: string): void;
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  press(key: string): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

function isErrorStop(display: string): boolean {
  return display.toUpperCase().includes("ЕГГ");
}

function runTrap(opcode: number, x: string): string {
  const calc = new MK61();
  calc.loadProgram([opcode, 0x50]);
  calc.setRegister("x", x);
  calc.press("В/О");
  calc.press("С/П");
  calc.runUntilStable({ maxFrames: 200, stableFrames: 5 });
  return calc.displayText();
}

// Validates the compileTrap opcode mapping (TrapStatementAst -> one-cell error
// stop): each opcode must raise the ЕГГОГ error stop exactly on its guarded
// domain, and compute normally off-domain. This proves the (currently
// explicit-intent-only) trap machinery is semantically sound on real hardware.
describe("ROM fact: domain-error trap opcodes", () => {
  const cases: Array<{ trap: string; opcode: number; bad: string; good: string }> = [
    { trap: "zero (F 1/x)", opcode: 0x23, bad: "0", good: "5" },
    { trap: "negative (F sqrt)", opcode: 0x21, bad: "-4", good: "4" },
    { trap: "nonpositive (F lg)", opcode: 0x17, bad: "0", good: "10" },
    { trap: "gt_one (F sin^-1)", opcode: 0x19, bad: "2", good: "0.5" },
    { trap: "ge_100 (F 10^x)", opcode: 0x15, bad: "100", good: "2" },
    { trap: "frac_ge_06 (К °->′\")", opcode: 0x2a, bad: "1.6", good: "1.5" },
  ];

  for (const { trap, opcode, bad, good } of cases) {
    it(`${trap} error-stops on its domain but not off it`, () => {
      expect(isErrorStop(runTrap(opcode, bad))).toBe(true);
      expect(isErrorStop(runTrap(opcode, good))).toBe(false);
    });
  }

  it("F lg also error-stops on negative input (nonpositive domain)", () => {
    expect(isErrorStop(runTrap(0x17, "-2"))).toBe(true);
  });

  it("frac_ge_06 boundary: 0.6 fractional part traps, just below passes", () => {
    expect(isErrorStop(runTrap(0x2a, "0.6"))).toBe(true);
    expect(isErrorStop(runTrap(0x2a, "0.59"))).toBe(false);
  });
});
