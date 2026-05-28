import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  setRegister(register: string, value: string): void;
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

function runWith(reg: string, codes: number[]): string {
  const calc = new MK61();
  calc.setRegister("1", reg);
  calc.loadProgram(codes);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 200, stableFrames: 4 });
  return calc.displayText();
}

// ROM facts behind the lang-packed-pos candidate: a single packed value N.dddd
// carries an integer position N (recoverable with К [x]) and packed fractional
// sub-coordinates (recoverable with К {x}). This is the dual-use that
// constants-dual-use already exploits; a dedicated "N.0000H" type would only be
// language surface over the same behavior.
describe("packed position dual-use round-trip", () => {
  it("recovers the integer position with К [x]", () => {
    // ИП1, К [x]
    expect(runWith("3.0204", [0x61, 0x34, 0x50])).toBe("3,");
  });

  it("recovers the packed fractional sub-coordinates with К {x}", () => {
    // ИП1, К {x} -> 0.0204 shown in normalized form
    expect(runWith("3.0204", [0x61, 0x35, 0x50])).toBe("2,04     -02");
  });

  it("echoes the packed value unchanged", () => {
    expect(runWith("3.0204", [0x61, 0x50])).toBe("3,0204");
  });
});
