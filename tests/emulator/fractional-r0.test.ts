import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  setRegister(register: string, value: string): void;
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  press(key: string): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
  readRegister(register: string): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

// Ground-truth facts that the r0-fractional-sentinel and fractional-indirect-
// addressing capabilities depend on: a fractional R0 (0 < R0 < 1) used as an
// indirect selector addresses R3 (data) or jumps to 99 (flow), and in both
// cases overwrites R0 with the -99999999 sentinel.
describe("ROM fact: fractional R0 indirect addressing", () => {
  it("К П->X 0 with fractional R0 recalls R3 and leaves the -99999999 sentinel in R0", () => {
    for (const r0 of ["0.5", "0.1", "0.7"]) {
      const calc = new MK61();
      calc.loadProgram([0xd0, 0x50]); // К П->X 0 ; С/П
      calc.setRegister("0", r0);
      calc.setRegister("3", "42");
      calc.press("В/О");
      calc.press("С/П");
      const run = calc.runUntilStable({ maxFrames: 200, stableFrames: 5 });
      expect(run.stopped).toBe(true);
      expect(calc.readRegister("x").trim()).toBe("42,");
      expect(calc.readRegister("0").trim()).toBe("-99999999,");
    }
  });

  it("К БП 0 with fractional R0 jumps to address 99 and leaves the -99999999 sentinel in R0", () => {
    const program = Array.from({ length: 105 }, () => 0x50);
    program[0] = 0x80; // К БП 0
    program[1] = 0x01; // marker that must be skipped
    program[99] = 0x07; // marker reached only by jumping to 99
    program[100] = 0x50;
    const calc = new MK61();
    calc.loadProgram(program);
    calc.setRegister("0", "0.5");
    calc.press("В/О");
    calc.press("С/П");
    const run = calc.runUntilStable({ maxFrames: 300, stableFrames: 5 });
    expect(run.stopped).toBe(true);
    expect(calc.displayText().trim()).toContain("7");
    expect(calc.readRegister("0").trim()).toBe("-99999999,");
  });
});
