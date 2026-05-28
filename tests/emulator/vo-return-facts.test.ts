import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  programCounter(): string;
  readRegister(register: string): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

// ROM fact behind the vo-return-body-reorder candidate: В/О (0x52) with an empty
// return-address stack jumps to program address 0, so it can act as a one-cell
// jump to the head. (return-zero-jump already harvests this when a backward
// jump targets the head; active HEAD/MAIN/SUB reordering remains a candidate.)
describe("В/О empty-stack return target", () => {
  it("jumps to address 0 when the return stack is empty", () => {
    // 00: С/П  01: 1  02: X->П0  03: В/О  04: С/П
    const calc = new MK61();
    calc.loadProgram([0x50, 0x01, 0x40, 0x52, 0x50]);
    calc.pressSequence(["В/О", "С/П"]);
    calc.runUntilStable({ maxFrames: 50, stableFrames: 3 });
    // Resume: runs 1, X->П0, then В/О -> back to address 0.
    calc.pressSequence(["С/П"]);
    calc.runUntilStable({ maxFrames: 50, stableFrames: 3 });
    expect(calc.programCounter()).toBe("00");
    expect(calc.readRegister("0").trim()).toBe("1,");
  });
});
