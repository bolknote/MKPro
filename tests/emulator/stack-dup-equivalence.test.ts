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

const SOURCE = `program SquareSum {
  state {
    result: counter 0..99 = 0
  }

  turn {
    x = read()
    y = read()
    result = (x + y) * (x + y)
    halt(result)
  }
}
`;

describe("stack duplication (В↑) of a repeated pure operand", () => {
  const compiled = compileMKPro(SOURCE);

  it("emits a В↑ duplication instead of recomputing (x + y)", () => {
    const names = compiled.report.optimizations.map((optimization) => optimization.name);
    expect(names).toContain("stack-current-x-scheduling");
    // В↑ is opcode 0x0e; the operand (x + y) must be computed exactly once.
    const codes = compiled.steps.map((step) => step.opcode);
    expect(codes.filter((opcode) => opcode === 0x0e).length).toBeGreaterThanOrEqual(1);
    // Only one addition: the operand is not recomputed.
    expect(codes.filter((opcode) => opcode === 0x10).length).toBe(1);
  });

  it("computes (3 + 4)^2 = 49 on the emulator", () => {
    const calc = new MK61();
    calc.loadProgram(compiled.steps.map((step) => step.opcode));
    // Drive the read/stop cycle the way a user would: run to each С/П stop
    // before entering the next value.
    let run = { stopped: false, frames: 0 };
    for (const key of ["В/О", "С/П", "3", "С/П", "4", "С/П"]) {
      calc.pressSequence([key]);
      run = calc.runUntilStable({ maxFrames: 400, stableFrames: 3 });
    }
    expect(run.stopped).toBe(true);
    expect(calc.displayText().replace(/\s/gu, "")).toContain("49");
  });
});
