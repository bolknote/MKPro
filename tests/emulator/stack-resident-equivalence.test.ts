import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { compileLoweringVariantForTest } from "../../src/core/compiler.ts";
import type { PreloadReport } from "../../src/core/types.ts";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  setRegister(register: string, value: string): void;
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
  readRegister(register: string): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("../emulator/mk61.cjs") as { MK61: Mk61Constructor };

const MK61_HEX_DIGITS: Record<string, string> = { A: "-", B: "L", C: "С", D: "Г", E: "Е", F: "_" };

function mk61HexLiteral(text: string): string {
  return [...text.toUpperCase()].map((digit) => MK61_HEX_DIGITS[digit] ?? digit).join("");
}

function observe(
  codes: number[],
  keys: string[],
  preloads: readonly PreloadReport[],
  registersToCompare: readonly string[],
) {
  const calc = new MK61();
  for (const preload of preloads) calc.setRegister(preload.register, mk61HexLiteral(preload.value));
  calc.loadProgram(codes);
  calc.pressSequence(keys);
  const run = calc.runUntilStable({ maxFrames: 800, stableFrames: 5 });
  const registers: Record<string, string> = {};
  for (const register of registersToCompare) registers[register] = calc.readRegister(register).trim();
  return { display: calc.displayText().trim(), stopped: run.stopped, registers };
}

const DUAL_STACK_SOURCE = `
program DualStackEq {
  state {
    x: packed = 3
    y: packed = 4
    z: packed = 0
  }
  loop {
    a = x
    b = y
    z = a + b
    halt(z)
  }
}
`;

const CROSS_FLOW_SOURCE = `
program CrossFlowEq {
  state {
    x: packed = 3
    y: packed = 4
    z: packed = 0
    gate: packed = 0
  }
  loop {
    a = x
    b = y
    if gate == 1 {
      loop {
      }
    }
    z = a + b
    halt(z)
  }
}
`;

describe("stack-resident temp behavioral equivalence (real emulator)", () => {
  const baseline = compileLoweringVariantForTest(DUAL_STACK_SOURCE, { budget: 999999 }, {});
  const stackResident = compileLoweringVariantForTest(DUAL_STACK_SOURCE, { budget: 999999 }, { stackResidentTemps: true });
  const registers = [...new Set(Object.values(stackResident.report.registers))];

  it("stack-resident variant fires and is not larger than the baseline variant", () => {
    expect(stackResident.report.optimizations.some((entry) => entry.name === "stack-resident-temps")).toBe(true);
    expect(stackResident.steps.length).toBeLessThanOrEqual(baseline.steps.length);
  });

  it("keeps the same field-to-register allocation", () => {
    expect(stackResident.report.registers).toEqual(baseline.report.registers);
  });

  it("matches baseline emulator output for a short run", () => {
    const keys = ["В/О", "С/П"];
    const before = observe(
      baseline.steps.map((step) => step.opcode),
      keys,
      baseline.report.preloads,
      registers,
    );
    const after = observe(
      stackResident.steps.map((step) => step.opcode),
      keys,
      stackResident.report.preloads,
      registers,
    );
    expect(after.display).toBe(before.display);
    expect(after.stopped).toBe(before.stopped);
    const zRegister = stackResident.report.registers.z;
    expect(zRegister).toBe(baseline.report.registers.z);
    if (zRegister === undefined) throw new Error("z register missing");
    expect(after.registers[zRegister]).toBe(before.registers[zRegister]);
  });
});

describe("stack-resident control-flow fusion behavioral equivalence (real emulator)", () => {
  const baseline = compileLoweringVariantForTest(CROSS_FLOW_SOURCE, { budget: 999999 }, {});
  const stackResident = compileLoweringVariantForTest(CROSS_FLOW_SOURCE, { budget: 999999 }, { stackResidentTemps: true });
  const registers = [...new Set(Object.values(stackResident.report.registers))];

  it("control-flow fusion fires and is not larger than the baseline variant", () => {
    expect(stackResident.report.optimizations.some((entry) => entry.name === "stack-resident-control-flow")).toBe(true);
    expect(stackResident.steps.length).toBeLessThanOrEqual(baseline.steps.length);
  });

  it("matches baseline emulator output for a short run", () => {
    const keys = ["В/О", "С/П"];
    const before = observe(
      baseline.steps.map((step) => step.opcode),
      keys,
      baseline.report.preloads,
      registers,
    );
    const after = observe(
      stackResident.steps.map((step) => step.opcode),
      keys,
      stackResident.report.preloads,
      registers,
    );
    expect(after.display).toBe(before.display);
    expect(after.stopped).toBe(before.stopped);
    const zRegister = stackResident.report.registers.z;
    expect(zRegister).toBe(baseline.report.registers.z);
    if (zRegister === undefined) throw new Error("z register missing");
    expect(after.registers[zRegister]).toBe(before.registers[zRegister]);
  });
});

// Exercises the generalized repeated-unary-arg canonicalization: the routed call
// is `sqr` (not `pow10`), and the two matching updates are separated by an
// unrelated statement, so only the non-adjacent, function-agnostic grouping can
// canonicalize them. The rewrite must stay behaviorally identical to baseline.
const REPEATED_UNARY_ARG_SOURCE = `
program RepeatedUnaryArgEq {
  state {
    a: packed = 2
    b: packed = 5
    c: packed = 100
    d: packed = 0
  }
  loop {
    c -= sqr(a)
    d = a + b
    c -= sqr(b)
    halt(c)
  }
}
`;

describe("repeated unary-call argument canonicalization behavioral equivalence (real emulator)", () => {
  const baseline = compileLoweringVariantForTest(REPEATED_UNARY_ARG_SOURCE, { budget: 999999 }, {});
  const canonicalized = compileLoweringVariantForTest(
    REPEATED_UNARY_ARG_SOURCE,
    { budget: 999999 },
    { canonicalizeRepeatedUnaryUpdateArgs: true },
  );

  it("canonicalizes a non-pow10, non-adjacent repeated unary-call run through one hidden scratch", () => {
    expect(canonicalized.diagnostics.some((diagnostic) => diagnostic.level === "error")).toBe(false);
    expect(canonicalized.report.optimizations.some((entry) => entry.name === "repeated-unary-update-arg-temp")).toBe(true);
    expect(Object.keys(canonicalized.report.registers).some((name) => name.startsWith("__mkpro_unary_arg_"))).toBe(true);
  });

  it("matches baseline emulator output for a short run", () => {
    const keys = ["В/О", "С/П"];
    // Each variant allocates fields to registers independently (the scratch
    // shifts the canonical allocation), so compare each variant's own `c` cell.
    const baselineC = baseline.report.registers.c;
    const canonicalizedC = canonicalized.report.registers.c;
    if (baselineC === undefined || canonicalizedC === undefined) throw new Error("c register missing");
    const before = observe(baseline.steps.map((step) => step.opcode), keys, baseline.report.preloads, [baselineC]);
    const after = observe(canonicalized.steps.map((step) => step.opcode), keys, canonicalized.report.preloads, [canonicalizedC]);
    expect(after.display).toBe(before.display);
    expect(after.stopped).toBe(before.stopped);
    expect(after.registers[canonicalizedC]).toBe(before.registers[baselineC]);
  });
});

const PENDING_PROGRAMS: ReadonlyArray<{ file: string; options: Record<string, unknown> }> = [
  { file: "examples/pending-optimizer/cave-treasure.mkpro", options: { stackResidentTemps: true } },
  { file: "examples/pending-optimizer/giants-country.mkpro", options: { stackResidentTemps: true } },
  {
    file: "examples/pending-optimizer/tic-tac-toe-4x4.mkpro",
    options: {
      stackResidentTemps: true,
      xParamValueFunctions: true,
      canonicalizeRepeatedUnaryUpdateArgs: true,
      coalesceCopies: true,
    },
  },
] as const;

describe("stack-resident temp variant compiles pending programs", () => {
  for (const { file, options } of PENDING_PROGRAMS) {
    it(`compiles ${file} with stackResidentTemps`, () => {
      const source = readFileSync(file, "utf8");
      const result = compileLoweringVariantForTest(source, { budget: 999999, analysis: true }, options);
      expect(result.diagnostics.some((diagnostic) => diagnostic.level === "error")).toBe(false);
      expect(result.steps.length).toBeGreaterThan(0);
    });
  }
});
