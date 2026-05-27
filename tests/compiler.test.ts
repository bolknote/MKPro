import { createRequire } from "node:module";
import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { compileM61, MK61_EXACT_PROFILE } from "../src/core/index.ts";

const require = createRequire(import.meta.url);

function source(path: string): string {
  return readFileSync(path, "utf8");
}

describe("M61 compiler", () => {
  it("keeps low-level examples compiling", () => {
    const result = compileM61(source("examples/basic.m61"));

    expect(result.report.ir.v1).toBe(false);
    expect(result.report.steps).toBeLessThanOrEqual(105);
    expect(result.report.registers.x).toBe("0");
  });

  it("honors the source budget when no CLI budget overrides it", () => {
    expect(() => compileM61(source("examples/cave-highlevel-baseline.m61"))).toThrow(/budget is 105/u);
  });

  it("lowers the compact cave DSL under the 105-step target", () => {
    const result = compileM61(source("examples/cave-sketch.m61"));

    expect(result.report.ir.v1).toBe(true);
    expect(result.report.steps).toBeLessThanOrEqual(105);
    expect(result.report.candidates.some((candidate) => candidate.variant === "dark-indirect-table")).toBe(true);
    expect(result.report.cellRoles.some((cell) => cell.roles.includes("overlay"))).toBe(true);
    expect(result.report.cellRoles.some((cell) => cell.roles.includes("dark-entry"))).toBe(true);
  });

  it("keeps the full cave reference high-level and compiles its semantic domains", () => {
    const reference = source("examples/cave-treasure-full.m61");

    expect(reference).not.toMatch(/\brecipe\b/iu);
    expect(reference).not.toMatch(/\ballow\b/iu);
    expect(reference).not.toMatch(/core\s+exact/iu);
    expect(reference).not.toMatch(/row\s+[0-9A-F]{2}\s*:/iu);
    const result = compileM61(reference);

    expect(result.report.steps).toBe(105);
    expect(result.report.reference?.parity).toBe("equal");
    expect(result.report.optimizations.some((optimization) => optimization.name === "game-intent-lowering")).toBe(true);
  });

  it("compiles human-centered M61 without source allow switches", () => {
    const result = compileM61(source("examples/human.m61"));

    expect(result.report.ir.v2).toBe(true);
    expect(result.report.targetProfile).toBe("mk61_exact");
    expect(result.report.steps).toBeLessThanOrEqual(105);
    expect(result.report.steps).toBe(35);
    expect(result.report.warnings.some((warning) => warning.includes("Deprecated allow-list"))).toBe(false);
    expect(result.report.candidates.some((candidate) => candidate.variant === "dark-indirect-table")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((feature) => feature.id === "code-data-overlay")).toBe(true);
    expect(result.report.proofs.some((proof) => proof.id === "value-ranges")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "dispatch-source-register")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "show-read-fusion")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "fl-unit-decrement")).toBe(true);
  });

  it("removes unconditional jumps to the immediately following label", () => {
    const result = compileM61(`
machine mk61
entry main {
  core {
    БП next
    next:
    1
  }
}
`);

    expect(result.steps.some((step) => step.comment === "next")).toBe(false);
    expect(result.report.optimizations.some((optimization) => optimization.name === "jump-to-next-threading")).toBe(true);
  });

  it("lowers simple let and if rules through generic intent", () => {
    const result = compileM61(`
target mk61
budget 105 cells
optimize size

program SimpleRules {
  input key: digit
  state {
    score: counter 0..9 = 1
  }
  screen main {
    show score
    style compact digits
  }
  turn {
    read key
    inc
    show main
  }
  rule inc {
    let next = score + 1
    if next >= 3 {
      score = next
    }
  }
}
`);

    expect(result.report.ir.v2).toBe(true);
    expect(result.report.steps).toBeLessThanOrEqual(105);
    expect(result.report.registers.next).toBeDefined();
    expect(result.report.optimizations.some((optimization) => optimization.name === "inline-block")).toBe(false);
    expect(result.report.proofs.some((proof) => proof.id === "value-ranges")).toBe(true);
  });

  it("compiles the full cave reference through generic semantic lowerers", () => {
    const reference = source("examples/cave-treasure.m61");

    expect(reference).not.toMatch(/\ballow\b/iu);
    expect(reference).not.toMatch(/core\s+exact/iu);
    expect(reference).not.toMatch(/row\s+[0-9A-F]{2}\s*:/iu);
    expect(reference).toMatch(/world cave: grid/u);
    expect(reference).toMatch(/generated random/u);
    expect(reference).toMatch(/terminal at 0 show main/u);
    const result = compileM61(reference);
    const selected = new Set(result.report.candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));
    const features = new Set(result.report.machineFeaturesUsed.map((feature) => feature.id));

    expect(result.report.ir.v2).toBe(true);
    expect(result.report.steps).toBeLessThanOrEqual(105);
    expect(result.report.budgetReport.officialSteps).toBe(105);
    expect(result.report.preloads.length).toBeGreaterThanOrEqual(6);
    expect(result.report.optimizations.some((optimization) => optimization.name === "intent-domain-lowering")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "game-intent-lowering")).toBe(true);
    expect(selected.has("indirect-register-flow")).toBe(true);
    expect(selected.has("super-dark-dispatch")).toBe(true);
    expect(selected.has("cyclic-address-layout")).toBe(true);
    expect(selected.has("x2-vp-scheduling")).toBe(true);
    expect(selected.has("hex-mantissa-data")).toBe(true);
    expect(features.has("indirect-flow")).toBe(true);
    expect(features.has("x2-register")).toBe(true);
    expect(features.has("code-data-overlay")).toBe(true);
    expect(features.has("super-dark-dispatch")).toBe(true);
    expect(result.report.proofs.some((proof) => proof.id === "full-game-semantics")).toBe(true);
    expect(result.report.proofs.some((proof) => proof.id === "cyclic-address-safety")).toBe(true);
  });

  it("does not contain the old benchmark-special backend in compiler source", () => {
    const compilerSource = source("src/core/compiler.ts");

    expect(compilerSource).not.toMatch(/CAVE_TREASURE/u);
    expect(compilerSource).not.toMatch(/tryCompileCompactGameProgram/u);
    expect(compilerSource).not.toMatch(/isCaveTreasure/u);
    expect(compilerSource).not.toMatch(/buildCompactCave/u);
    expect(compilerSource).not.toMatch(/cave_treasure/iu);
  });

  it("treats reference metadata as report-only, not as codegen input", () => {
    const reference = source("examples/cave-treasure.m61");
    const renamed = reference.replace(/^reference .+$/mu, "reference renamed_metadata_only");
    const unreferenced = reference.replace(/^reference .+\n/mu, "");
    const original = compileM61(reference);
    const changed = compileM61(renamed);
    const anonymous = compileM61(unreferenced);
    const originalHex = original.steps.map((step) => step.hex);

    expect(changed.steps.map((step) => step.hex)).toEqual(originalHex);
    expect(anonymous.steps.map((step) => step.hex)).toEqual(originalHex);
    expect(changed.report.reference?.name).toBe("renamed_metadata_only");
    expect(changed.report.warnings.join("\n")).toMatch(/reference metadata did not affect code generation/u);
  });

  it("keeps the demo-page MK-61 tricks active in the v2 game optimizer", () => {
    const result = compileM61(source("examples/cave-treasure.m61"));
    const optimizations = new Set(result.report.optimizations.map((optimization) => optimization.name));
    const activeCapabilities = new Set(
      result.report.optimizer.capabilities
        .filter((capability) => capability.status === "active")
        .map((capability) => capability.id),
    );
    const features = new Set(result.report.machineFeaturesUsed.map((feature) => feature.id));
    const roleNotes = result.report.cellRoles.map((cell) => cell.note).join("\n");
    const comments = result.steps.map((step) => step.comment ?? "").join("\n");

    for (const name of [
      "indirect-register-flow",
      "constants-dual-use",
      "cyclic-address-layout",
      "shared-tail-layout",
      "code-data-overlay",
      "x2-display-byte-scheduling",
      "vp-fraction-restore",
      "hex-mantissa-arithmetic",
      "fractional-indirect-addressing",
      "r0-indirect-counter",
      "kzn-double",
      "kor-digit-test",
      "kmax-zero-through",
      "return-zero-jump",
    ]) {
      expect(optimizations.has(name)).toBe(true);
    }

    for (const id of [
      "address-constant-overlay",
      "cyclic-address-layout",
      "constants-dual-use",
      "dark-entry-layout",
      "super-dark-dispatch",
      "r0-alias-indirect",
      "r0-fractional-sentinel",
      "x2-display-register",
      "vp-fraction-restore",
      "hex-mantissa-arithmetic",
      "fractional-indirect-addressing",
      "kzn-double",
      "kor-digit-test",
      "kmax-zero-through",
      "return-zero-jump",
    ]) {
      expect(activeCapabilities.has(id)).toBe(true);
    }

    for (const id of [
      "return-empty-stack-jump",
      "indirect-flow",
      "dark-entries",
      "address-constants",
      "x2-register",
      "x2-restore-boundaries",
      "display-bytes",
      "r0-fractional-sentinel",
      "r0-t-alias",
      "code-data-overlay",
      "super-dark-dispatch",
    ]) {
      expect(features.has(id)).toBe(true);
    }

    expect(roleNotes).toMatch(/address\/data overlay selected/u);
    expect(roleNotes).toMatch(/formal\/dark entry participates/u);
    expect(roleNotes).toMatch(/X2\/display-byte boundary/u);
    expect(comments).toMatch(/К ЗН as one-cell doubling/u);
    expect(comments).toMatch(/К∨ digit\/boundary test/u);
    expect(comments).toMatch(/К max zero-through transform/u);
    expect(comments).toMatch(/indirect recall truncates fractional address/u);
  });

  it("applies the same spatial/resource tactic pipeline to non-cave games", () => {
    for (const path of ["examples/grid-rescue.m61", "examples/resource-raid.m61", "examples/giants-country.m61"]) {
      const result = compileM61(source(path));
      const selected = new Set(result.report.candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));

      expect(result.report.ir.v2).toBe(true);
      expect(result.report.steps).toBeLessThanOrEqual(105);
      expect(result.report.budgetReport.officialSteps).toBe(105);
      expect(selected.has("indirect-register-flow")).toBe(true);
      expect(selected.has("super-dark-dispatch")).toBe(true);
      expect(selected.has("cyclic-address-layout")).toBe(true);
      expect(result.report.warnings.join("\n")).toMatch(/universal spatial\/resource tactic pipeline/u);
    }
  });

  it("loads the compact cave output into the headless emulator", () => {
    const { MK61 } = require("./emulator/mk61.cjs") as {
      MK61: new () => {
        loadProgram: (codes: number[]) => { diagnostics: string[] };
        readProgramCodes: (count: number) => number[];
        setRegister: (register: string, value: string) => unknown;
        readRegister: (register: string) => string;
        press: (key: string) => unknown;
        pressSequence: (keys: string[]) => unknown;
        inputNumber: (value: string, options?: { clear?: boolean }) => unknown;
        programCounter: () => string;
        displayText: () => string;
        runUntilStable: (options: { maxFrames: number; stableFrames: number }) => { stopped: boolean };
      };
    };
    const result = compileM61(source("examples/cave-treasure.m61"));
    const codes = result.steps.map((step) => step.opcode);
    const calc = new MK61();
    const loaded = calc.loadProgram(codes);

    calc.setRegister("4", "2");
    calc.setRegister("5", "10");
    calc.setRegister("6", "ГE-02");
    calc.setRegister("7", "5E-1");
    calc.setRegister("8", "-52");
    calc.setRegister("9", "4,_3E-08");
    calc.pressSequence(["БП", "4", "4", "4", "4", "П→X", "9", "F", "0", "С/П"]);

    expect(loaded.diagnostics).toEqual([]);
    expect(calc.readProgramCodes(codes.length)).toEqual(codes);
    expect(calc.runUntilStable({ maxFrames: 600, stableFrames: 6 }).stopped).toBe(true);
    expect(calc.programCounter()).toBe("01");
    expect(calc.displayText()).toBe("1,0000001");
    expect(calc.readRegister("e")).toBe("44,");

    calc.inputNumber("2", { clear: true });
    calc.press("С/П");
    expect(calc.runUntilStable({ maxFrames: 600, stableFrames: 6 }).stopped).toBe(true);
    expect(calc.displayText()).toBe("1,0000002");
    expect(calc.readRegister("e")).toBe("43,");
  });

  it("reports automatic optimizer capabilities for compact programs", () => {
    const result = compileM61(source("examples/cave-sketch.m61"));

    expect(result.report.ir.v1).toBe(true);
    expect(result.report.optimizer.automatic).toBe(true);
    expect(result.report.optimizer.capabilities.some((capability) => capability.id === "x2-display-register")).toBe(true);
    expect(result.report.optimizer.capabilities.some((capability) => capability.id === "r0-alias-indirect")).toBe(true);
    expect(result.report.optimizer.capabilities.some((capability) => capability.id === "super-dark-dispatch")).toBe(true);
  });

  it("records exact MK-61 emulator facts for advanced dispatch hacks", () => {
    const factIds = new Set(MK61_EXACT_PROFILE.emulatorFacts.map((fact) => fact.id));
    const featureIds = new Set(MK61_EXACT_PROFILE.features.map((feature) => feature.id));

    expect(featureIds.has("super-dark-dispatch")).toBe(true);
    expect(featureIds.has("r0-fractional-sentinel")).toBe(true);
    expect(featureIds.has("raw-display-5f")).toBe(true);
    expect(factIds.has("super-dark-fa-ff-indirect")).toBe(true);
    expect(factIds.has("r0-fractional-jump-99")).toBe(true);
    expect(factIds.has("r0-fractional-selects-r3")).toBe(true);
    expect(factIds.has("raw-display-5f")).toBe(true);
    expect(MK61_EXACT_PROFILE.emulatorFacts.find((fact) => fact.id === "r0-star-f-aliases")?.detail).toMatch(/do not preserve R0/u);
  });

  it("safe mode refuses the full cave reference and explains required exact-machine lowerings", () => {
    expect(() => compileM61(source("examples/cave-treasure.m61"), { opt: "safe" })).toThrow(/mk61_exact tactics/u);
  });

  it("uses conservative dispatch lowering in safe mode", () => {
    const result = compileM61(source("examples/cave-sketch.m61"), { opt: "safe" });
    const selected = result.report.candidates.find((candidate) => candidate.selected);

    expect(selected?.variant).toBe("safe-compare-chain");
    expect(result.report.cellRoles.some((cell) => cell.roles.includes("overlay"))).toBe(false);
    expect(result.report.cellRoles.some((cell) => cell.roles.includes("dark-entry"))).toBe(false);
  });

  it("can load compiled output into the headless emulator wrapper", () => {
    const { MK61 } = require("./emulator/mk61.cjs") as { MK61: new () => { loadProgram: (codes: number[]) => void; readProgramCodes: (count: number) => number[] } };
    const result = compileM61(source("examples/basic.m61"));
    const calc = new MK61();
    const codes = result.steps.map((step) => step.opcode);

    calc.loadProgram(codes);

    expect(calc.readProgramCodes(codes.length)).toEqual(codes);
  });

  it("blocks unsafe optimizer capabilities in safe mode", () => {
    const result = compileM61(source("examples/cave-sketch.m61"), { opt: "safe", budget: 999 });
    const unsafeCapabilities = result.report.optimizer.capabilities.filter((capability) => capability.unsafe);

    expect(unsafeCapabilities.some((capability) => capability.status === "blocked")).toBe(true);
    expect(result.report.cellRoles.some((cell) => cell.roles.includes("overlay"))).toBe(false);
  });
});
