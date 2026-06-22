import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
  programCounter(): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

interface StopObservation {
  stopped: boolean;
  pc: string;
  display: string;
}

function programFromCells(cells: Record<number, number>, length: number): number[] {
  const program = new Array<number>(length).fill(0x50);
  for (const [addressText, opcode] of Object.entries(cells)) {
    program[Number(addressText)] = opcode;
  }
  return program;
}

function addr(address: number): number {
  return Math.floor(address / 10) * 16 + (address % 10);
}

function runStopTrace(program: number[], stops: number): StopObservation[] {
  const calc = new MK61();
  calc.loadProgram(program);
  calc.pressSequence(["В/О"]);

  const trace: StopObservation[] = [];
  for (let index = 0; index < stops; index += 1) {
    calc.pressSequence(["С/П"]);
    const run = calc.runUntilStable({ maxFrames: 900, stableFrames: 5 });
    trace.push({
      stopped: run.stopped,
      pc: calc.programCounter(),
      display: calc.displayText().trim(),
    });
  }
  return trace;
}

function directScriptProgram(): number[] {
  return programFromCells(
    {
      0: 0x51,
      1: addr(18),
      2: 0x51,
      3: addr(52),
      18: 0x53,
      19: addr(26),
      20: 0x0d,
      21: 0x01,
      22: 0x50,
      23: 0x50,
      26: 0x53,
      27: addr(34),
      28: 0x0d,
      29: 0x02,
      30: 0x50,
      31: 0x51,
      32: addr(20),
      34: 0x53,
      35: addr(42),
      36: 0x0d,
      37: 0x03,
      38: 0x50,
      39: 0x51,
      40: addr(28),
      42: 0x53,
      43: addr(50),
      44: 0x0d,
      45: 0x04,
      46: 0x50,
      47: 0x51,
      48: addr(36),
      50: 0x53,
      51: addr(2),
      52: 0x0d,
      53: 0x05,
      54: 0x50,
      55: 0x51,
      56: addr(44),
    },
    64,
  );
}

function returnStackScriptProgram(): number[] {
  const program = directScriptProgram();
  program[2] = 0x52;
  program[3] = 0x50;
  program[31] = 0x52;
  program[32] = 0x50;
  program[39] = 0x52;
  program[40] = 0x50;
  program[47] = 0x52;
  program[48] = 0x50;
  program[55] = 0x52;
  program[56] = 0x50;
  return program;
}

function dirtyOverflowDispatchProgram(): number[] {
  return programFromCells(
    {
      0: 0x51,
      1: addr(26),
      2: 0x52,
      26: 0x53,
      27: addr(34),
      28: 0x0d,
      29: 0x01,
      30: 0x50,
      31: 0x52,
      34: 0x53,
      35: addr(42),
      36: 0x0d,
      37: 0x02,
      38: 0x50,
      39: 0x52,
      42: 0x53,
      43: addr(50),
      44: 0x0d,
      45: 0x03,
      46: 0x50,
      47: 0x52,
      50: 0x53,
      51: addr(58),
      52: 0x0d,
      53: 0x04,
      54: 0x50,
      55: 0x52,
      58: 0x53,
      59: addr(2),
      60: 0x0d,
      61: 0x05,
      62: 0x50,
      63: 0x52,
      78: 0x0d,
      79: 0x09,
      80: 0x50,
    },
    86,
  );
}

describe("return-stack scripted continuations (real MK-61 microcode)", () => {
  it("matches direct БП tails at every observable stop", () => {
    const direct = runStopTrace(directScriptProgram(), 5);
    const scripted = runStopTrace(returnStackScriptProgram(), 5);

    expect(scripted).toEqual(direct);
    expect(scripted.map((step) => step.pc)).toEqual(["55", "47", "39", "31", "23"]);
    expect(scripted.map((step) => step.display)).toEqual(["5,", "4,", "3,", "2,", "1,"]);
  });

  it("can deliberately dispatch through the dirty 77 overflow return target", () => {
    const trace = runStopTrace(dirtyOverflowDispatchProgram(), 6);

    expect(trace.map((step) => step.pc)).toEqual(["63", "55", "47", "39", "31", "81"]);
    expect(trace.map((step) => step.display)).toEqual(["5,", "4,", "3,", "2,", "1,", "9,"]);
  });
});
