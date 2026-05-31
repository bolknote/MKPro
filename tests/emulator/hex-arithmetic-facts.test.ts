import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  setRegister(register: string, value: string): Mk61Instance;
  readRegister(register: string): string;
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

const IP1 = 0x61;
const IP2 = 0x62;
const ADD = 0x10;
const STOP = 0x50;

// Adds R1 and R2 via ИП1 ИП2 + so that the second-recalled register lands in X.
function addRegisters(r1: string, r2: string): string {
  const calc = new MK61();
  calc.setRegister("1", r1);
  calc.setRegister("2", r2);
  calc.loadProgram([IP1, IP2, ADD, STOP]);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 300, stableFrames: 4 });
  return calc.displayText().replace(/\s/gu, "");
}

// MK-61 mantissa digit values 10..15 are the famous "letter" glyphs (hex digits
// A..F). These ROM facts pin the undocumented hex-mantissa arithmetic that the
// hex-mantissa-arithmetic capability models.
describe("undocumented MK-61 hex mantissa arithmetic", () => {
  it("stores and renders a hex mantissa digit (Г = digit 13)", () => {
    const calc = new MK61();
    calc.setRegister("1", "Г");
    expect(calc.readRegister("1").trim()).toBe("Г,");
  });

  it("hex digit in Y adds as its decimal value (D + 3 = 16)", () => {
    // R1 = Г(13) recalled first -> ends up in Y; R2 = 3 -> X. Y + X = 13 + 3.
    expect(addRegisters("Г", "3")).toContain("16");
    expect(addRegisters("С", "4")).toContain("16");
  });

  it("hex digit in X collapses the sum to a clean zero in either pairing", () => {
    // R2 = Г(13) recalled second -> lands in X; the BCD correction zeroes the
    // result regardless of the decimal partner. This is the documented
    // hex+decimal zero-generator.
    expect(addRegisters("3", "Г")).toBe("0,");
    expect(addRegisters("4", "С")).toBe("0,");
  });

  it("hex A multiplied by 18 renders a non-normal leading zero", () => {
    const calc = new MK61();
    calc.setRegister("1", "-");
    calc.setRegister("2", "18");
    calc.loadProgram([IP1, IP2, 0x12, STOP]);
    calc.pressSequence(["В/О", "С/П"]);
    calc.runUntilStable({ maxFrames: 300, stableFrames: 4 });
    expect(calc.displayText()).toBe("020,");
    expect(calc.readRegister("x").trim()).toBe("20,");
  });
});
