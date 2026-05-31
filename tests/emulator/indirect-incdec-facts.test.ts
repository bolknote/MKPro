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

// Documented MK-61 fact behind indirect-incdec-counter / r0-indirect-counter:
// an indirect access pre-decrements selector registers R0..R3 and pre-increments
// R4..R6 (R7..RE unchanged). Crucially the pre-decrement is a true arithmetic -1
// that reaches 0 (and below), so `К П->X r` is the correct compact lowering of a
// standalone `x--` on a zero-reachable counter. F Lx, by contrast, clamps a
// positive counter at 1 and is only safe in the fused decrement-and-branch forms.
function afterIndirectAccess(registerIndex: number, init: string): string {
  const calc = new MK61();
  calc.loadProgram([0xd0 + registerIndex, 0x50]); // К П->X R ; С/П
  const name = registerIndex.toString();
  calc.setRegister(name, init);
  calc.press("В/О");
  calc.press("С/П");
  calc.runUntilStable({ maxFrames: 100, stableFrames: 5 });
  return calc.readRegister(name).trim();
}

describe("ROM fact: indirect addressing register step", () => {
  for (const r of [0, 1, 2, 3]) {
    it(`R${r} pre-decrements on indirect access`, () => {
      expect(Number.parseInt(afterIndirectAccess(r, "5"), 10)).toBe(4);
    });
  }
  for (const r of [4, 5, 6]) {
    it(`R${r} pre-increments on indirect access`, () => {
      expect(Number.parseInt(afterIndirectAccess(r, "5"), 10)).toBe(6);
    });
  }
  for (const r of [7, 9]) {
    it(`R${r} is unchanged on indirect access`, () => {
      expect(Number.parseInt(afterIndirectAccess(r, "5"), 10)).toBe(5);
    });
  }

  // The property that makes this a correct unit decrement (unlike F Lx, which
  // keeps a 1 at 1): the pre-decrement actually reaches 0.
  for (const r of [0, 1, 2, 3]) {
    it(`R${r} pre-decrement reaches 0 from 1`, () => {
      expect(Number.parseInt(afterIndirectAccess(r, "1"), 10)).toBe(0);
    });
  }
});
