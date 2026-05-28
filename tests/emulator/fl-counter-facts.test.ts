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

// Foundational fact behind the fl-reinit idea: an F Lx loop with counter R0 = N
// runs exactly N iterations and always exits with the counter register equal to
// 1. This is the value liveness would need to prove a redundant "set counter = 1"
// before a following loop. (No generated program emits a single-iteration F Lx
// loop, so this is the documented premise rather than an active rewrite.)
describe("ROM fact: F L0 loop count and exit value", () => {
  function runLoop(n: string): { iterations: string; counter: string } {
    const calc = new MK61();
    // 0:ИП1 1:"1" 2:+ 3:П1 4:F L0 5:addr00 6:С/П  (R1 counts iterations)
    calc.loadProgram([0x61, 0x01, 0x10, 0x41, 0x5d, 0x00, 0x50]);
    calc.setRegister("0", n);
    calc.setRegister("1", "0");
    calc.press("В/О");
    calc.press("С/П");
    calc.runUntilStable({ maxFrames: 4000, stableFrames: 5 });
    return { iterations: calc.readRegister("1").trim(), counter: calc.readRegister("0").trim() };
  }

  for (const n of ["1", "2", "3", "5"]) {
    it(`R0 = ${n} runs ${n} iterations and exits with the counter at 1`, () => {
      const { iterations, counter } = runLoop(n);
      expect(Number.parseInt(iterations, 10)).toBe(Number.parseInt(n, 10));
      expect(Number.parseInt(counter, 10)).toBe(1);
    });
  }
});
