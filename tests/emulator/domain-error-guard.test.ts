import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { compileLoweringVariantForTest } from "../../src/core/compiler.ts";

const require = createRequire(import.meta.url);

interface RunResult {
  stopped: boolean;
  frames: number;
}

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  setRegister(register: string, value: string): void;
  pressSequence(keys: string[]): void;
  inputNumber(value: string, options?: { clear?: boolean }): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): RunResult;
  displayText(): string;
}

type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;

const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

// Compiles the guard program once for a chosen comparison operator, then runs it
// on the real MK-61 model with `value` keyed in. The taken branch is a pure
// ЕГГОГ trap, so an error display means the guard fired.
function runGuard(op: "<" | "<=" | "==", value: number): { display: string; steps: number; firedDomainGuard: boolean } {
  const source = `
program G {
  state { result: counter 0..99 = 0 }
  loop {
    x = read()
    if x ${op} 0 { halt("ЕГГОГ") }
    result = 7
    halt(result)
  }
}
`;
  const compiled = compileLoweringVariantForTest(source, { budget: 999, analysis: true }, { domainErrorGuards: true });
  const calc = new MK61();
  const loaded = calc.loadProgram(compiled.steps.map((step) => step.opcode));
  expect(loaded.diagnostics).toEqual([]);
  calc.pressSequence(["В/О", "С/П"]);
  calc.inputNumber(String(Math.abs(value)));
  if (value < 0) calc.pressSequence(["/-/"]);
  calc.pressSequence(["С/П"]);
  calc.runUntilStable({ maxFrames: 400, stableFrames: 5 });
  return {
    display: calc.displayText(),
    steps: compiled.steps.length,
    firedDomainGuard: compiled.report.optimizations.some((opt) => opt.name === "domain-error-guard"),
  };
}

function runRangedUpperGuard(value: number): { display: string; steps: number; firedArcTrap: boolean } {
  const source = `
program G {
  state {
    y: counter 0..5 = stack.X
    result: counter 0..99 = 0
  }

  loop {
    if y > 1 { halt("ЕГГОГ") }
    result = 7
    halt(result)
  }
}
`;
  const compiled = compileLoweringVariantForTest(source, { budget: 999, analysis: true }, { domainErrorGuards: true });
  const calc = new MK61();
  const loaded = calc.loadProgram(compiled.steps.map((step) => step.opcode));
  expect(loaded.diagnostics).toEqual([]);
  calc.setRegister(compiled.report.registers.y!, String(value));
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 400, stableFrames: 5 });
  return {
    display: calc.displayText(),
    steps: compiled.steps.length,
    firedArcTrap: compiled.steps.some((step) => step.opcode === 0x19),
  };
}

function runRangedLowerGuard(value: number): { display: string; firedArcTrap: boolean } {
  const source = `
program G {
  state {
    y: counter -5..0 = stack.X
    result: counter 0..99 = 0
  }

  loop {
    if y < -1 { halt("ЕГГОГ") }
    result = 7
    halt(result)
  }
}
`;
  const compiled = compileLoweringVariantForTest(source, { budget: 999, analysis: true }, { domainErrorGuards: true });
  const calc = new MK61();
  const loaded = calc.loadProgram(compiled.steps.map((step) => step.opcode));
  expect(loaded.diagnostics).toEqual([]);
  calc.setRegister(compiled.report.registers.y!, String(value));
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 400, stableFrames: 5 });
  return {
    display: calc.displayText(),
    firedArcTrap: compiled.steps.some((step) => step.opcode === 0x19),
  };
}

function isErrorStop(display: string): boolean {
  return display.toUpperCase().includes("ЕГГ");
}

// Validates that the domain-error-guard rewrite (`if x < 0 { halt("ЕГГОГ") }` ->
// `F √`, `<= 0` -> `F lg`) is semantically faithful on real hardware: it raises
// the ЕГГОГ error exactly on the guarded domain and falls through otherwise,
// matching the boundary semantics of the original conditional trap.
describe("domain-error guard equivalence on the real emulator", () => {
  it("lowers `x < 0` to a self-trapping `F √` that traps iff x < 0", () => {
    const positive = runGuard("<", 5);
    expect(positive.firedDomainGuard).toBe(true);
    expect(isErrorStop(positive.display)).toBe(false);
    expect(positive.display).toContain("7");

    // The boundary value 0 is NOT < 0, so `F √` must not trap (sqrt(0) = 0).
    expect(isErrorStop(runGuard("<", 0).display)).toBe(false);

    expect(isErrorStop(runGuard("<", -4).display)).toBe(true);
  });

  it("lowers `x <= 0` to a self-trapping `F lg` that traps iff x <= 0", () => {
    const positive = runGuard("<=", 5);
    expect(positive.firedDomainGuard).toBe(true);
    expect(isErrorStop(positive.display)).toBe(false);
    expect(positive.display).toContain("7");

    // The boundary value 0 IS <= 0, so `F lg` must trap (lg domain is x > 0).
    expect(isErrorStop(runGuard("<=", 0).display)).toBe(true);

    expect(isErrorStop(runGuard("<=", -4).display)).toBe(true);
  });

  it("lowers `x == 0` to a self-trapping `F 1/x` that traps iff x == 0", () => {
    const positive = runGuard("==", 5);
    expect(positive.firedDomainGuard).toBe(true);
    expect(isErrorStop(positive.display)).toBe(false);
    expect(positive.display).toContain("7");

    // Division by zero traps at exactly 0, regardless of sign — the precise
    // equality semantics, unlike a sign trap which would also fire below zero.
    expect(isErrorStop(runGuard("==", 0).display)).toBe(true);

    expect(isErrorStop(runGuard("==", -3).display)).toBe(false);
  });

  it("lowers ranged `x > 1` to a self-trapping `F sin^-1` within the proved range", () => {
    const zero = runRangedUpperGuard(0);
    expect(zero.firedArcTrap).toBe(true);
    expect(isErrorStop(zero.display)).toBe(false);
    expect(zero.display).toContain("7");

    expect(isErrorStop(runRangedUpperGuard(1).display)).toBe(false);
    expect(isErrorStop(runRangedUpperGuard(2).display)).toBe(true);
    expect(isErrorStop(runRangedUpperGuard(5).display)).toBe(true);
  });

  it("lowers ranged `x < -1` to a self-trapping `F sin^-1` within the proved range", () => {
    const zero = runRangedLowerGuard(0);
    expect(zero.firedArcTrap).toBe(true);
    expect(isErrorStop(zero.display)).toBe(false);
    expect(zero.display).toContain("7");

    expect(isErrorStop(runRangedLowerGuard(-1).display)).toBe(false);
    expect(isErrorStop(runRangedLowerGuard(-2).display)).toBe(true);
    expect(isErrorStop(runRangedLowerGuard(-5).display)).toBe(true);
  });

  it("emits the one-cell domain opcode instead of compare + branch + shared trap", () => {
    const result = runGuard("<", 5);
    // read (С/П) + F √ + (result=7) push 7 + halt + loop-back jump (2 cells).
    expect(result.steps).toBe(6);
  });
});
