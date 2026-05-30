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

const SOURCE = `program NearAnyHelperProbe {
  state {
    room: counter 0..20 = 0
    a: counter 0..20 = 0
    b: counter 0..20 = 0
    c: counter 0..20 = 0
    d: counter 0..20 = 0
    result: counter 0..9 = 0
  }

  turn {
    result = 0
    if near_any(room, 1, a, b) >= 0 {
      result = 1
    }
    if near_any(room, 1, c, d) >= 0 {
      result = result + 2
    }
    halt(result)
  }
}
`;

const compiled = compileMKPro(SOURCE, { budget: 999 });

function runProbe(values: Record<string, number>): string {
  const calc = new MK61();
  calc.loadProgram(compiled.steps.map((step) => step.opcode));
  for (const [name, value] of Object.entries(values)) {
    calc.setRegister(compiled.report.registers[name]!, String(value));
  }
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 400, stableFrames: 5 });
  return calc.displayText().replace(/\s/gu, "");
}

describe("near_any shared helper", () => {
  it("is selected only after repeated compatible proximity predicates", () => {
    const names = compiled.report.optimizations.map((optimization) => optimization.name);
    expect(names).toContain("near-any-helper-lowering");
    expect(names).toContain("near-any-helper");
  });

  it("preserves the first candidate margin on the stack for max()", () => {
    const base = { room: 10, a: 8, b: 12, c: 7, d: 14, result: 0 };

    expect(runProbe(base)).toContain("0");
    expect(runProbe({ ...base, b: 11 })).toContain("1");
    expect(runProbe({ ...base, d: 9 })).toContain("2");
    expect(runProbe({ ...base, b: 11, d: 9 })).toContain("3");
  });
});
