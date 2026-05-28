import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  setRegister(register: string, value: string): void;
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  press(key: string): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  readRegister(register: string): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

function logOf(opcode: number, x: string): string {
  const calc = new MK61();
  calc.loadProgram([opcode, 0x50]);
  calc.setRegister("x", x);
  calc.press("В/О");
  calc.press("С/П");
  calc.runUntilStable({ maxFrames: 200, stableFrames: 5 });
  return calc.readRegister("x").trim();
}

// Rationale guard for NOT implementing the "Flg selector" idiom (using F lg/F ln
// + a sign branch to discriminate x>1 vs x<1). That idiom requires lg(x) < 0 for
// 0 < x < 1. On the reference emulator the log opcodes return the POSITIVE
// magnitude for sub-unit inputs, so the sign branch cannot discriminate and the
// optimization would be incorrect. If this behavior ever changes, revisit.
describe("premise check: log sign selector is NOT usable", () => {
  it("the emulator can represent negative mantissas", () => {
    const calc = new MK61();
    calc.loadProgram([0x50]);
    calc.setRegister("x", "-5");
    calc.press("В/О");
    calc.press("С/П");
    calc.runUntilStable({ maxFrames: 50, stableFrames: 5 });
    expect(calc.readRegister("x").trim().startsWith("-")).toBe(true);
  });

  it("F lg (0x17) of a sub-unit value is NOT negative (premise fails)", () => {
    const half = logOf(0x17, "0.5");
    expect(half.startsWith("-")).toBe(false);
    // Same magnitude as lg(2), confirming the sign is dropped.
    expect(half.replace(/\s/gu, "")).toBe(logOf(0x17, "2").replace(/\s/gu, ""));
  });

  it("F ln (0x18) of a sub-unit value is NOT negative (premise fails)", () => {
    expect(logOf(0x18, "0.5").startsWith("-")).toBe(false);
  });
});
