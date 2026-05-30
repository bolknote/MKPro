import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { compileMKPro, formatProgramTokens, formatSetupProgram } from "../../src/core/index.ts";

const require = createRequire(import.meta.url);

interface RunResult {
  stopped: boolean;
  frames: number;
}

interface Mk61Instance {
  setRegister(register: string, value: string): void;
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  press(key: string): void;
  pressSequence(keys: string[]): void;
  inputNumber(value: string, options?: { clear?: boolean }): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): RunResult;
  displayText(): string;
  readRegister(register: string): string;
}

type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

// Compile a one-turn program that reads its inputs, runs the bit machinery, and
// halts via `stop`. Returns the integer the program displays.
function runProbe(source: string, inputs: number[]): number {
  const result = compileMKPro(source, { budget: 999 });
  const codes = result.steps.map((step) => step.opcode);
  const calc = new MK61();

  const setup = formatSetupProgram(result);
  if (setup !== undefined) {
    expect(calc.loadProgram(setup).diagnostics).toEqual([]);
    calc.pressSequence(["В/О", "С/П"]);
    calc.runUntilStable({ maxFrames: 2000, stableFrames: 6 });
  }

  expect(calc.loadProgram(formatProgramTokens(result.steps)).diagnostics).toEqual([]);
  void codes;

  // Constant preloads (e.g. the 0.5 rounding bias) are symbolic initial register
  // values the operator keys in before the run; apply them to the emulator.
  for (const preload of (result.report.preloads ?? []) as Array<{ register: string; value: string }>) {
    calc.setRegister(preload.register, preload.value);
  }

  calc.pressSequence(["В/О", "С/П"]);
  for (const value of inputs) {
    calc.inputNumber(String(value));
    calc.press("С/П");
  }
  calc.runUntilStable({ maxFrames: 2000, stableFrames: 6 });
  return Number.parseInt(calc.displayText().replace(/[,\s]/gu, ""), 10);
}

// Single-row cave: bits live directly in the linear room index, exercising the
// fractional-nibble cell-mask representation (8.HHHHHHH).
const MEMBERSHIP_PROGRAM = `program BitMembership {
  cave: board(1..20, 1..1)

  state {
    marks: cells(cave)
    a: coord(cave)
    b: coord(cave)
    c: coord(cave)
    answer: counter 0..1 = 0
  }

  turn {
    read a
    read b
    read c
    marks += a
    marks += b
    answer = 0
    if c in marks {
      answer = 1
    }
    stop answer
  }
}`;

const CLEAR_PROGRAM = `program BitClear {
  cave: board(1..20, 1..1)

  state {
    marks: cells(cave)
    a: coord(cave)
    c: coord(cave)
    answer: counter 0..1 = 0
  }

  turn {
    read a
    read c
    marks += c
    marks -= a
    answer = 0
    if c in marks {
      answer = 1
    }
    stop answer
  }
}`;

const NEIGHBOR_PROGRAM = `program NeighborCount {
  cave: board(1..20, 1..1)

  state {
    marks: cells(cave)
    a: coord(cave)
    b: coord(cave)
    probe: coord(cave)
    answer: counter 0..9 = 0
  }

  turn {
    read a
    read b
    read probe
    marks += a
    marks += b
    answer = neighbor_count(marks, probe)
    stop answer
  }
}`;

describe("MK-61 fractional-nibble cell masks round-trip on the emulator", () => {
  it("bit_set then bit_has reports membership as 0/1", () => {
    // c present (== a) -> 1; c present (== b) -> 1; c absent -> 0.
    expect(runProbe(MEMBERSHIP_PROGRAM, [3, 7, 3])).toBe(1);
    expect(runProbe(MEMBERSHIP_PROGRAM, [3, 7, 7])).toBe(1);
    expect(runProbe(MEMBERSHIP_PROGRAM, [3, 7, 5])).toBe(0);
  });

  it("membership is exact across nibble boundaries (cells 1..20 span five nibbles)", () => {
    // Cell index n lands in fractional nibble floor((n-1)/4); these pairs cross
    // every nibble boundary, so a leaky mask would alias neighbours.
    expect(runProbe(MEMBERSHIP_PROGRAM, [4, 5, 4])).toBe(1);
    expect(runProbe(MEMBERSHIP_PROGRAM, [4, 5, 5])).toBe(1);
    expect(runProbe(MEMBERSHIP_PROGRAM, [4, 5, 1])).toBe(0);
    expect(runProbe(MEMBERSHIP_PROGRAM, [1, 20, 1])).toBe(1);
    expect(runProbe(MEMBERSHIP_PROGRAM, [1, 20, 20])).toBe(1);
    expect(runProbe(MEMBERSHIP_PROGRAM, [1, 20, 16])).toBe(0);
    expect(runProbe(MEMBERSHIP_PROGRAM, [8, 9, 8])).toBe(1);
    expect(runProbe(MEMBERSHIP_PROGRAM, [8, 9, 9])).toBe(1);
    expect(runProbe(MEMBERSHIP_PROGRAM, [8, 9, 12])).toBe(0);
  });

  it("bit_clear removes exactly the cleared cell", () => {
    // marks = {c}; clearing a removes the cell only when a == c.
    expect(runProbe(CLEAR_PROGRAM, [3, 3])).toBe(0); // cleared the queried cell
    expect(runProbe(CLEAR_PROGRAM, [7, 3])).toBe(1); // cleared an absent cell; 3 remains
    expect(runProbe(CLEAR_PROGRAM, [5, 5])).toBe(0);
    expect(runProbe(CLEAR_PROGRAM, [20, 20])).toBe(0);
    expect(runProbe(CLEAR_PROGRAM, [1, 20])).toBe(1); // cross-nibble: clearing 1 leaves 20
  });

  it("neighbor_count counts set cells adjacent to the probe on a single row", () => {
    // marks = {a,b}; neighbours of probe are probe-1 and probe+1.
    expect(runProbe(NEIGHBOR_PROGRAM, [4, 6, 5])).toBe(2); // 4 and 6 both border 5
    expect(runProbe(NEIGHBOR_PROGRAM, [4, 9, 5])).toBe(1); // only 4 borders 5
    expect(runProbe(NEIGHBOR_PROGRAM, [9, 12, 5])).toBe(0); // neither borders 5
    expect(runProbe(NEIGHBOR_PROGRAM, [3, 10, 4])).toBe(1); // 3 borders 4
  });
});
