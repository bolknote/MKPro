import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  setRegister(register: string, value: string): Mk61Instance;
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

// Expensive repeated selector (a + b) makes single-evaluation dispatch strictly
// smaller than the if/else-if chain, so the canonicalization variant is the one
// selected by the smallest-variant rule.
const SOURCE = `program IfChainProbe {
  state {
    a: counter 0..9 = 0
    b: counter 0..9 = 0
    result: counter 0..99 = 0
  }
  turn {
    if a + b == 1 {
      result = 11
    }
    else {
      if a + b == 2 {
        result = 22
      }
      else {
        if a + b == 3 {
          result = 33
        }
        else {
          result = 99
        }
      }
    }
    halt(result)
  }
}`;

const compiled = compileMKPro(SOURCE, { budget: 999 });

function run(a: number, b: number): number {
  const calc = new MK61();
  calc.loadProgram(compiled.steps.map((step) => step.opcode));
  for (const preload of compiled.report.preloads) {
    if (preload.countsAgainstProgram === false) calc.setRegister(preload.register, String(preload.value));
  }
  calc.setRegister(compiled.report.registers.a!, String(a));
  calc.setRegister(compiled.report.registers.b!, String(b));
  calc.setRegister(compiled.report.registers.result!, "0");
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 400, stableFrames: 5 });
  return Number(calc.displayText().replace(/\s/gu, "").replace(/,$/u, "").replace(",", "."));
}

const expected = (sum: number): number => (sum === 1 ? 11 : sum === 2 ? 22 : sum === 3 ? 33 : 99);

describe("if/else-if chain dispatch canonicalization", () => {
  it("is selected when single evaluation of the repeated selector is cheaper", () => {
    const names = compiled.report.optimizations.map((optimization) => optimization.name);
    expect(names).toContain("if-chain-dispatch-canonicalization");
  });

  it("preserves first-match priority and the default arm on the emulator", () => {
    for (const [a, b] of [[0, 1], [1, 1], [2, 1], [0, 0], [4, 5], [3, 0], [0, 2], [1, 2]] as const) {
      expect(run(a, b)).toBe(expected(a + b));
    }
  });
});
