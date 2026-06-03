import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";

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
const VUP = 0x0e; // В↑
const INT = 0x34; // К [x]
const FRAC = 0x35; // К {x}
const XY = 0x14; // X<->Y
const ST0 = 0x40; // X->П 0
const ADD = 0x10;
const DIV = 0x13;
const MUL = 0x12;
const STOP = 0x50;

const SOURCE = `program Split {
  state {
    hi: counter 0..999 = 0
    lo: counter 0..999 = 0
  }

  loop {
    x = read()
    hi = int(x / 4)
    lo = frac(x / 4)
    halt(hi)
    halt(lo)
  }
}
`;

function refInt(value: string): string {
  const calc = new MK61();
  calc.setRegister("1", value);
  calc.loadProgram([IP1, INT, STOP]);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 200, stableFrames: 4 });
  return calc.displayText().replace(/\s/gu, "");
}

function refFrac(value: string): string {
  const calc = new MK61();
  calc.setRegister("1", value);
  calc.loadProgram([IP1, FRAC, STOP]);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 200, stableFrames: 4 });
  return calc.displayText().replace(/\s/gu, "");
}

// Runs the shared-tail idiom ИП1 В↑ К[x] П0 X<->Y К{x} and returns both parts:
// R0 holds the integer part, X (display) holds the fractional part.
function sharedTail(value: string): { int: string; frac: string } {
  const calc = new MK61();
  calc.setRegister("1", value);
  calc.loadProgram([IP1, VUP, INT, ST0, XY, FRAC, STOP]);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 200, stableFrames: 4 });
  return {
    int: calc.readRegister("0").trim(),
    frac: calc.displayText().replace(/\s/gu, ""),
  };
}

function oneBasedModulo4(value: string): string {
  const calc = new MK61();
  calc.setRegister("1", value);
  calc.loadProgram([IP1, INT, 0x03, ADD, 0x04, DIV, FRAC, 0x04, MUL, 0x01, ADD, STOP]);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 200, stableFrames: 4 });
  return calc.displayText().replace(/\s/gu, "");
}

describe("int/frac shared-tail fusion (subroutine-part)", () => {
  it("the compiler emits the В↑ / X↔Y shared tail and evaluates the operand once", () => {
    const compiled = compileMKPro(SOURCE);
    const names = compiled.report.optimizations.map((optimization) => optimization.name);
    expect(names).toContain("int-frac-shared-tail");
    const codes = compiled.steps.map((step) => step.opcode);
    // Single divide -> operand (x / 4) is evaluated exactly once, not twice.
    expect(codes.filter((opcode) => opcode === 0x13).length).toBe(1);
    // The shared tail uses В↑, X↔Y, К [x] and К {x}.
    expect(codes).toContain(VUP);
    expect(codes).toContain(XY);
    expect(codes).toContain(INT);
    expect(codes).toContain(FRAC);
  });

  it("derives int and frac through the shared tail identically to separate К[x]/К{x}", () => {
    for (const value of ["2.5", "-2.5", "0.25", "-0.25", "8", "-8"]) {
      const tail = sharedTail(value);
      expect(tail.int).toBe(refInt(value));
      expect(tail.frac).toBe(refFrac(value));
    }
  }, 20_000);

  it("supports branchless one-based modulo for non-negative integer inputs", () => {
    const expected = new Map([
      ["0", "4,"],
      ["1", "1,"],
      ["2", "2,"],
      ["3", "3,"],
      ["4", "4,"],
      ["5", "1,"],
      ["8", "4,"],
      ["9", "1,"],
    ]);
    for (const [value, display] of expected) {
      expect(oneBasedModulo4(value)).toBe(display);
    }
  });
});
