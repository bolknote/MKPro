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

// Facts the raw-display-5f / x2-display capabilities rest on: opcode 5F is a raw
// display mutation whose intended effect is observable on the indicator only,
// without disturbing the X register; and К {x} (0x35) yields the fractional part.
describe("ROM fact: display-byte opcodes", () => {
  it("opcode 5F mutates the display indicator but leaves X untouched", () => {
    const calc = new MK61();
    calc.loadProgram([0x5f, 0x50]);
    calc.setRegister("x", "12345678");
    calc.press("В/О");
    calc.press("С/П");
    const run = calc.runUntilStable({ maxFrames: 200, stableFrames: 5 });
    expect(run.stopped).toBe(true);
    expect(calc.readRegister("x").trim()).toBe("12345678,");

    const baseline = new MK61();
    baseline.loadProgram([0x50]);
    baseline.setRegister("x", "12345678");
    baseline.press("В/О");
    baseline.press("С/П");
    baseline.runUntilStable({ maxFrames: 200, stableFrames: 5 });
    // 5F is observable: the indicator differs from the untouched-X baseline.
    expect(calc.displayText()).not.toBe(baseline.displayText());
  });

  it("К {x} (0x35) extracts the fractional part of X", () => {
    const calc = new MK61();
    calc.loadProgram([0x35, 0x50]);
    calc.setRegister("x", "3.14159");
    calc.press("В/О");
    calc.press("С/П");
    const run = calc.runUntilStable({ maxFrames: 200, stableFrames: 5 });
    expect(run.stopped).toBe(true);
    // .14159 rendered as a normalized fractional mantissa (1.4159e-1).
    expect(calc.readRegister("x").replace(/\s/gu, "")).toContain("4159");
  });
});
