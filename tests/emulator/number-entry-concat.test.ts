import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

function program(body: string): string {
  return `program P {
  state {
    r: counter 0..9999 = 0
  }

  turn {
    read x
    r = ${body}
    stop r
  }
}
`;
}

// Runs a single-read program: enter `inputDigits` at the read stop, then read
// the value shown at the result stop.
function runWithInput(source: string, inputDigits: string[]): string {
  const compiled = compileMKPro(source);
  const calc = new MK61();
  calc.loadProgram(compiled.steps.map((step) => step.opcode));
  for (const key of ["В/О", "С/П", ...inputDigits, "С/П"]) {
    calc.pressSequence([key]);
    calc.runUntilStable({ maxFrames: 400, stableFrames: 3 });
  }
  return calc.displayText().replace(/\s/gu, "");
}

// Regression guard for the MK-61 number-entry concatenation bug: two number
// literals (or a freshly read input followed by a literal) must not merge into
// one number on the calculator. Each program below produced a wrong result
// before the fix (e.g. x*3+1 emitted "1 3" -> 13, x/4 emitted a read straight
// into "4" -> 104).
describe("number-entry concatenation regression", () => {
  it("x * 3 + 1 keeps the two literals separate", () => {
    // x = 10 -> 3*10 + 1 = 31 (was 35 when "1 3" merged into 13)
    expect(runWithInput(program("x * 3 + 1"), ["1", "0"])).toBe("31,");
  });

  it("x / 4 finalizes the read input before the literal", () => {
    // x = 10 -> 10 / 4 = 2.5 (was 0 when "4" concatenated onto the input)
    expect(runWithInput(program("x / 4"), ["1", "0"])).toMatch(/2,5/u);
  });

  it("2 * (2 + x) distributes without merging 2 and 4", () => {
    // x = 5 -> 2*(2+5) = 14 (was 210 when "4 2" merged into 42)
    expect(runWithInput(program("2 * (2 + x)"), ["5"])).toBe("14,");
  });

  it("(x + 2) * 3 stays correct with two constants in the linear form", () => {
    // x = 4 -> (4+2)*3 = 18
    expect(runWithInput(program("(x + 2) * 3"), ["4"])).toBe("18,");
  });
});
