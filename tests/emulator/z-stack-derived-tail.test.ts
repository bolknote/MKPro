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
const DIV = 0x13;
const INT = 0x34; // К [x]
const FRAC = 0x35; // К {x}
const SIGN = 0x32; // К ЗН
const ABS = 0x31; // К |x|
const XY = 0x14; // X<->Y
const FREVERSE = 0x25; // F reverse
const ST0 = 0x40; // X->П 0
const ST1 = 0x41; // X->П 1
const ST2 = 0x42; // X->П 2
const STOP = 0x50;

const SOURCE = `program StackDerived {
  state {
    raw: packed = 0
    whole: packed = 0
    part: packed = 0
    signum: packed = 0
    magnitude: packed = 0
  }

  loop {
    raw = read()
    whole = int(raw / 10)
    part = frac(raw / 10)
    signum = sign(raw / 10)
    magnitude = abs(raw / 10)
    halt(whole)
    halt(part)
    halt(signum)
    halt(magnitude)
  }
}
`;

function displayOf(codes: number[], value: string): string {
  const calc = new MK61();
  calc.setRegister("1", value);
  calc.loadProgram(codes);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 200, stableFrames: 4 });
  return calc.displayText().replace(/\s/gu, "");
}

function refUnary(opcode: number, value: string): string {
  return displayOf([IP1, opcode, STOP], value);
}

function sharedStackTail(value: string): { int: string; frac: string; sign: string; abs: string } {
  return {
    int: displayOf([IP1, VUP, VUP, VUP, INT, STOP], value),
    frac: displayOf([IP1, VUP, VUP, VUP, INT, ST0, XY, FRAC, STOP], value),
    sign: displayOf([IP1, VUP, VUP, VUP, INT, ST0, XY, FRAC, ST1, FREVERSE, XY, SIGN, STOP], value),
    abs: displayOf([
      IP1,
      VUP,
      VUP,
      VUP,
      INT,
      ST0,
      XY,
      FRAC,
      ST1,
      FREVERSE,
      XY,
      SIGN,
      ST2,
      FREVERSE,
      XY,
      ABS,
      STOP,
    ], value),
  };
}

describe("Z-stack derived unary tail reuse", () => {
  it("emits В↑ / F reverse / X↔Y and evaluates the shared operand once", () => {
    const compiled = compileMKPro(SOURCE);
    const names = compiled.report.optimizations.map((optimization) => optimization.name);
    expect(names).toContain("z-stack-derived-value-reuse");
    const codes = compiled.steps.map((step) => step.opcode);
    expect(codes.filter((opcode) => opcode === DIV).length).toBe(1);
    expect(codes.filter((opcode) => opcode === VUP).length).toBeGreaterThanOrEqual(3);
    expect(codes.filter((opcode) => opcode === FREVERSE).length).toBeGreaterThanOrEqual(2);
    expect(codes).toContain(XY);
  });

  it("derives int, frac, sign and abs through saved stack copies identically to separate calls", () => {
    for (const value of ["2.5", "-2.5", "0.25", "-0.25", "8", "-8"]) {
      const tail = sharedStackTail(value);
      expect(tail.int).toBe(refUnary(INT, value));
      expect(tail.frac).toBe(refUnary(FRAC, value));
      expect(tail.sign).toBe(refUnary(SIGN, value));
      expect(tail.abs).toBe(refUnary(ABS, value));
    }
  }, 20_000);
});
