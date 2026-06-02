import { describe, expect, it } from "vitest";
import { createRequire } from "node:module";
import { CompileError, compileMKPro } from "../../src/core/index.ts";
import type { CompileOptions } from "../../src/core/index.ts";

const require = createRequire(import.meta.url);

interface Calc {
  loadProgram: (codes: number[]) => { diagnostics: string[] };
  setRegister: (register: string, value: string) => void;
  readRegister: (register: string) => string;
  pressSequence: (keys: string[]) => void;
  runUntilStable: (options: { maxFrames: number; stableFrames: number }) => { stopped: boolean };
}
const { MK61 } = require("../emulator/mk61.cjs") as {
  MK61: new (options?: { extended?: boolean }) => Calc;
};

function compile(source: string, options: Partial<CompileOptions> = { budget: 999999, analysis: true }) {
  return compileMKPro(source, options);
}

function program(fn: string): string {
  return `
program SafeMinMax {
  state {
    a: counter -99..99 = 0
    b: counter -99..99 = 0
    out: packed = 0
  }
  loop {
    out = ${fn}(a, b)
    halt(out)
  }
}`;
}

// Compile once, then drive the operands purely through registers so the
// runtime (non-folded) arithmetic path is exercised on the ROM emulator.
function runtimeResult(fn: string, a: number, b: number): number {
  const result = compile(program(fn));
  const calc = new MK61({ extended: true });
  for (const preload of result.report.preloads) calc.setRegister(preload.register, preload.value);
  expect(calc.loadProgram(result.steps.map((step) => step.opcode)).diagnostics).toEqual([]);
  calc.setRegister(result.report.registers.a!, String(a));
  calc.setRegister(result.report.registers.b!, String(b));
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 1200, stableFrames: 8 });
  // halt(out) shows the freshly computed value from X without spilling it to a
  // numbered register, so read X directly.
  const raw = calc.readRegister("x").replace(/\s/gu, "").replace(",", ".");
  return Number.parseFloat(raw.endsWith(".") ? raw.slice(0, -1) : raw);
}

describe("quirk-free safe_max / safe_min", () => {
  it("lowers safe_max without emitting К max (0x36)", () => {
    const result = compile(program("safe_max"));
    expect(result.steps.some((step) => step.opcode === 0x36)).toBe(false);
    expect(result.report.optimizations.some((item) => item.name === "quirk-free-minmax-lowering")).toBe(true);
  });

  it("lowers safe_min without emitting К max (0x36)", () => {
    const result = compile(program("safe_min"));
    expect(result.steps.some((step) => step.opcode === 0x36)).toBe(false);
  });

  it("plain max still lowers through the hardware К max opcode", () => {
    const result = compile(program("max"));
    expect(result.steps.some((step) => step.opcode === 0x36)).toBe(true);
  });

  it("rejects impure (non-duplicable) operands with a clear diagnostic", () => {
    expect(() =>
      compile(`
program P {
  state { out: packed = 0 }
  loop {
    out = safe_max(random(), 5)
    halt(out)
  }
}`),
    ).toThrow(/duplicable operands/u);
  });

  it("computes a true maximum on hardware, including the zero cases К max gets wrong", () => {
    expect(runtimeResult("safe_max", 5, 0)).toBe(5);
    expect(runtimeResult("safe_max", 0, 5)).toBe(5);
    expect(runtimeResult("safe_max", 3, 7)).toBe(7);
    expect(runtimeResult("safe_max", -5, 0)).toBe(0);
    expect(runtimeResult("safe_max", 0, -5)).toBe(0);
    expect(runtimeResult("safe_max", -2, -7)).toBe(-2);
    expect(runtimeResult("safe_max", 4, 4)).toBe(4);
  });

  it("computes a true minimum on hardware, including the zero cases", () => {
    expect(runtimeResult("safe_min", 5, 0)).toBe(0);
    expect(runtimeResult("safe_min", 0, 5)).toBe(0);
    expect(runtimeResult("safe_min", -5, 0)).toBe(-5);
    expect(runtimeResult("safe_min", 0, -5)).toBe(-5);
    expect(runtimeResult("safe_min", 3, 7)).toBe(3);
    expect(runtimeResult("safe_min", -2, -7)).toBe(-7);
  });

  it("contrasts with the hardware К max zero quirk on the same inputs", () => {
    // safe_max returns the true maximum; plain max mirrors the hardware quirk
    // where a zero operand collapses the result to zero.
    expect(runtimeResult("safe_max", 5, 0)).toBe(5);
    expect(runtimeResult("max", 5, 0)).toBe(0);
  });

  it("folds constant safe_max/safe_min with true semantics (no zero-is-greatest)", () => {
    const safe = compile(`
program P {
  state { out: packed = 0 }
  loop {
    out = safe_max(5, 0)
    halt(out)
  }
}`);
    // safe_max(5, 0) folds to 5; the listing loads 5 and never emits К max.
    expect(safe.steps.some((step) => step.opcode === 0x36)).toBe(false);

    const quirk = compile(`
program P {
  state { out: packed = 0 }
  loop {
    out = max(5, 0)
    halt(out)
  }
}`);
    expect(quirk.report.optimizations.some((item) => item.name === "expression-constant-folder")).toBe(true);
  });
});
