import { createRequire } from "node:module";
import { readdirSync, readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { compileMKPro, MK61_PROFILE } from "../src/core/index.ts";

const require = createRequire(import.meta.url);

function source(path: string): string {
  return readFileSync(path, "utf8");
}

describe("MK-Pro compiler", () => {
  it("keeps the smallest human DSL example compiling", () => {
    const result = compileMKPro(source("examples/basic.mkpro"));

    expect(result.report.ir.v2).toBe(true);
    expect(result.report.steps).toBeLessThanOrEqual(105);
    expect(result.report.optimizations.some((optimization) => optimization.name === "intent-read-lowering")).toBe(true);
  });

  it("keeps every checked example within the 105-cell ceiling", () => {
    const oversized: Array<{ path: string; steps: number }> = [];

    for (const name of readdirSync("examples").filter((entry) => entry.endsWith(".mkpro")).sort()) {
      const path = `examples/${name}`;
      const result = compileMKPro(source(path));
      if (result.report.steps > 105) oversized.push({ path, steps: result.report.steps });
    }

    expect(oversized).toEqual([]);
  });

  it("keeps every example with a real source reference no larger than that source", () => {
    const unresolved: string[] = [];
    const larger: Array<{ path: string; steps: number; reference: number }> = [];
    const checked: string[] = [];

    for (const name of readdirSync("examples").filter((entry) => entry.endsWith(".mkpro")).sort()) {
      const path = `examples/${name}`;
      const result = compileMKPro(source(path));
      const reference = result.report.reference;

      if (reference === undefined) continue;
      if (result.report.warnings.some((warning) => warning.includes("was not found under games/*"))) {
        unresolved.push(path);
        continue;
      }

      checked.push(path);
      if (result.report.steps > reference.referenceSpan) {
        larger.push({ path, steps: result.report.steps, reference: reference.referenceSpan });
      }
    }

    expect(unresolved).toEqual([]);
    expect(larger).toEqual([]);
    expect(checked).toContain("examples/alaram.mkpro");
    expect(checked).toContain("examples/dungeon.mkpro");
    expect(checked).toContain("examples/giants-country.mkpro");
    expect(checked).toContain("examples/lunar.mkpro");
  });

  it("keeps the high-level cave baseline within the 105-cell budget", () => {
    const result = compileMKPro(source("examples/cave-highlevel-baseline.mkpro"));

    expect(result.report.ir.v2).toBe(true);
    expect(result.report.steps).toBeLessThanOrEqual(105);
    expect(result.report.optimizations.some((optimization) => optimization.name === "game-intent-lowering")).toBe(true);
  });

  it("lowers the compact cave DSL under the 105-step target", () => {
    const result = compileMKPro(source("examples/cave-sketch.mkpro"));

    expect(result.report.ir.v2).toBe(true);
    expect(result.report.steps).toBeLessThanOrEqual(105);
    expect(result.report.candidates.some((candidate) => candidate.variant === "indirect-register-flow")).toBe(true);
    expect(result.report.cellRoles.some((cell) => cell.roles.includes("overlay"))).toBe(true);
    expect(result.report.cellRoles.some((cell) => cell.roles.includes("dark-entry"))).toBe(false);
    expect(result.report.rejectedCandidates.some((candidate) => candidate.variant === "super-dark-dispatch")).toBe(true);
  });

  it("keeps the full cave reference high-level and compiles its semantic domains", () => {
    const reference = source("examples/cave-treasure-full.mkpro");

    expect(reference).not.toMatch(/\brecipe\b/iu);
    expect(reference).not.toMatch(/core\s+exact/iu);
    expect(reference).not.toMatch(/row\s+[0-9A-F]{2}\s*:/iu);
    const result = compileMKPro(reference);

    expect(result.report.steps).toBe(105);
    expect(result.report.reference?.parity).toBe("equal");
    expect(result.report.optimizations.some((optimization) => optimization.name === "game-intent-lowering")).toBe(true);
  });

  it("compiles human-centered MK-Pro without source implementation switches", () => {
    const result = compileMKPro(source("examples/human.mkpro"));

    expect(result.report.ir.v2).toBe(true);
    expect(result.report.machine).toBe("mk61");
    expect(result.report.steps).toBeLessThanOrEqual(105);
    expect(result.report.steps).toBe(28);
    expect(result.report.candidates.some((candidate) => candidate.variant === "dark-indirect-table")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((feature) => feature.id === "code-data-overlay")).toBe(true);
    expect(result.report.proofs.some((proof) => proof.id === "value-ranges")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "dispatch-source-register")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "show-read-fusion")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "fl-unit-decrement")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "redundant-prologue-elimination")).toBe(true);
  });

  it("rejects removed low-level calculator syntax", () => {
    expect(() =>
      compileMKPro(`
machine mk61
entry main {
  core {
    БП next
    next:
    1
  }
}
`),
    ).toThrow(/Unexpected top-level line 'machine mk61'/u);
  });

  it("lowers simple assignment and if rules through generic intent", () => {
    const result = compileMKPro(`
program SimpleRules {
  state {
    score: counter 0..9 = 1
    next: counter 0..9 = 0
  }
  screen main {
    show score
  }
  turn {
    read key
    inc
    show main
  }
  rule inc {
    next = score + 1
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
    const reference = source("examples/cave-treasure.mkpro");

    expect(reference).not.toMatch(/core\s+exact/iu);
    expect(reference).not.toMatch(/row\s+[0-9A-F]{2}\s*:/iu);
    expect(reference).toMatch(/world cave/u);
    expect(reference).toMatch(/bitset = random\(\)/u);
    const result = compileMKPro(reference);
    const selected = new Set(result.report.candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));
    const features = new Set(result.report.machineFeaturesUsed.map((feature) => feature.id));

    expect(result.report.ir.v2).toBe(true);
    expect(result.report.steps).toBeLessThanOrEqual(105);
    expect(result.report.budgetReport.officialSteps).toBe(105);
    expect(result.report.preloads.length).toBeGreaterThanOrEqual(6);
    expect(result.report.optimizations.some((optimization) => optimization.name === "intent-domain-lowering")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "game-intent-lowering")).toBe(true);
    expect(selected.has("indirect-register-flow")).toBe(true);
    expect(selected.has("super-dark-dispatch")).toBe(false);
    expect(selected.has("cyclic-address-layout")).toBe(false);
    expect(selected.has("x2-vp-scheduling")).toBe(true);
    expect(selected.has("hex-mantissa-data")).toBe(true);
    expect(features.has("indirect-flow")).toBe(true);
    expect(features.has("x2-register")).toBe(true);
    expect(features.has("code-data-overlay")).toBe(true);
    expect(features.has("super-dark-dispatch")).toBe(false);
    expect(result.report.proofs.find((proof) => proof.id === "full-game-semantics")?.status).toBe("assumed");
    expect(result.report.proofs.some((proof) => proof.id === "cyclic-address-safety")).toBe(true);
    expect(result.report.proofs.find((proof) => proof.id === "cyclic-address-safety")?.status).toBe("not-needed");
    expect(result.report.proofs.find((proof) => proof.id === "super-dark-suffix-layout")?.status).toBe("not-needed");
    expect(result.report.rejectedCandidates.find((candidate) => candidate.variant === "super-dark-dispatch")?.reason).toMatch(/proved FA\.\.FF selector/u);
  });

  it("ports Anvarov's Treasure Cave demo byte-for-byte within the original size", () => {
    const { parseProgramText } = require("./emulator/mk61.cjs") as {
      parseProgramText: (text: string) => { codes: number[]; diagnostics: string[] };
    };
    const result = compileMKPro(source("examples/cave-treasure.mkpro"));
    const reference = parseProgramText(source("games/anvarov/demo.txt"));

    expect(reference.diagnostics).toEqual([]);
    expect(result.report.reference?.name).toBe("anvarov_demo");
    expect(result.report.reference?.referenceSpan).toBe(105);
    expect(result.report.reference?.parity).toBe("equal");
    expect(result.steps.map((step) => step.opcode)).toEqual(reference.codes);
  });

  it("ports Bolknote's 99 Bottles demo byte-for-byte within the original size", () => {
    const { MK61, parseProgramText } = require("./emulator/mk61.cjs") as {
      MK61: new () => {
        loadProgram: (codes: number[]) => { diagnostics: string[] };
        readProgramCodes: (count: number) => number[];
        setRegister: (register: string, value: string) => unknown;
        pressSequence: (keys: string[]) => unknown;
        displayText: () => string;
        runUntilStable: (options: { maxFrames: number; stableFrames: number }) => { stopped: boolean };
      };
      parseProgramText: (text: string) => { codes: number[]; diagnostics: string[] };
    };
    const result = compileMKPro(source("examples/99-bottles.mkpro"));
    const reference = parseProgramText(source("games/bolknote/99-bottles.txt"));
    const mkproSource = source("examples/99-bottles.mkpro");
    const codes = result.steps.map((step) => step.opcode);
    const calc = new MK61();
    const loaded = calc.loadProgram([0x3a, 0x50]);

    calc.setRegister("X", "8112006Е");
    calc.pressSequence(["В/О", "С/П"]);

    expect(reference.diagnostics).toEqual([]);
    expect(loaded.diagnostics).toEqual([]);
    expect(result.report.reference?.name).toBe("bolknote_99_bottles");
    expect(result.report.reference?.referenceSpan).toBe(53);
    expect(result.report.reference?.parity).toBe("equal");
    expect(result.report.steps).toBe(53);
    expect(result.report.warnings.join("\n")).not.toMatch(/was not found/u);
    expect(mkproSource).toContain(`show "BEEr ", bottles`);
    expect(mkproSource).not.toMatch(/\braw\s*\{/u);
    expect(codes).toEqual(reference.codes);
    expect(calc.runUntilStable({ maxFrames: 200, stableFrames: 6 }).stopped).toBe(true);
    expect(calc.readProgramCodes(2)).toEqual([0x3a, 0x50]);
    expect(calc.displayText()).toBe("8,ЕЕГ  91");
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
    const reference = source("examples/cave-treasure.mkpro");
    const renamed = reference.replace(/^reference .+$/mu, "reference renamed_metadata_only");
    const unreferenced = reference.replace(/^reference .+\n/mu, "");
    const original = compileMKPro(reference);
    const changed = compileMKPro(renamed);
    const anonymous = compileMKPro(unreferenced);
    const originalHex = original.steps.map((step) => step.hex);

    expect(changed.steps.map((step) => step.hex)).toEqual(originalHex);
    expect(anonymous.steps.map((step) => step.hex)).toEqual(originalHex);
    expect(changed.report.reference?.name).toBe("renamed_metadata_only");
    expect(changed.report.warnings.join("\n")).toMatch(/reference metadata did not affect code generation/u);
  });

  it("keeps the demo-page MK-61 tricks active in the v2 game optimizer", () => {
    const result = compileMKPro(source("examples/cave-treasure.mkpro"));
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
      "constants-dual-use",
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
      "branch-removal",
      "zero-condition-test",
      "dispatch-compare-chain",
      "arithmetic-if-select",
      "arithmetic-if-update",
      "arithmetic-if-extrema",
      "fl-decrement-branch",
    ]) {
      expect(activeCapabilities.has(id)).toBe(true);
    }

    for (const id of [
      "return-empty-stack-jump",
      "indirect-flow",
      "address-constants",
      "x2-register",
      "x2-restore-boundaries",
      "display-bytes",
      "r0-fractional-sentinel",
      "r0-t-alias",
      "code-data-overlay",
    ]) {
      expect(features.has(id)).toBe(true);
    }

    expect(roleNotes).toMatch(/address\/data overlay selected/u);
    expect(roleNotes).not.toMatch(/formal\/dark entry participates/u);
    expect(roleNotes).toMatch(/X2\/display-byte boundary/u);
    expect(comments).toMatch(/К ЗН as one-cell doubling/u);
    expect(comments).toMatch(/К∨ digit\/boundary test/u);
    expect(comments).toMatch(/К max zero-through transform/u);
    expect(comments).toMatch(/indirect recall truncates fractional address/u);
  });

  it("keeps the universal spatial/counter tactic fallback for unsupported non-cave games", () => {
    for (const path of [
      "examples/grid-rescue.mkpro",
      "examples/resource-raid.mkpro",
    ]) {
      const result = compileMKPro(source(path));
      const selected = new Set(result.report.candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));

      expect(result.report.ir.v2).toBe(true);
      expect(result.report.steps).toBeLessThanOrEqual(105);
      expect(result.report.budgetReport.officialSteps).toBe(105);
      expect(selected.has("indirect-register-flow")).toBe(true);
      expect(selected.has("super-dark-dispatch")).toBe(false);
      expect(selected.has("cyclic-address-layout")).toBe(false);
      expect(result.report.rejectedCandidates.find((candidate) => candidate.variant === "super-dark-dispatch")?.reason).toMatch(/proved FA\.\.FF selector/u);
      expect(result.report.warnings.join("\n")).toMatch(/universal spatial\/counter tactic pipeline/u);
    }
  });

  it("lowers Sea Battle through the board-fleet duel microkernel below the reference span", () => {
    const seaBattle = source("examples/sea-battle.mkpro");
    const result = compileMKPro(seaBattle);
    const selected = new Set(result.report.candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));
    const proofs = new Set(result.report.proofs.map((proof) => proof.id));
    const optimizations = new Set(result.report.optimizations.map((optimization) => optimization.name));
    const warnings = result.report.warnings.join("\n");

    expect(seaBattle).not.toMatch(/\bown_fleet\b/u);
    expect(seaBattle).toMatch(/\bown_ships:\s+counter\b/u);
    expect(result.report.steps).toBe(101);
    expect(result.report.steps).toBeLessThan(102);
    expect(result.report.reference?.referenceSpan).toBe(102);
    expect(result.report.reference?.referenceSteps).toBe(102);
    expect(result.report.reference?.referenceEntries).toBe(99);
    expect(result.report.reference?.referenceGaps).toEqual(["14", "19", "78"]);
    expect(selected.has("board_fleet_duel")).toBe(true);
    expect(result.report.rejectedCandidates.some((candidate) => candidate.variant === "universal_spatial_resource")).toBe(true);
    expect(result.report.rejectedCandidates.some((candidate) => candidate.variant === "super-dark-dispatch")).toBe(true);
    expect(warnings).toMatch(/selected board_fleet_duel semantic microkernel/u);
    expect(warnings).not.toMatch(/universal spatial\/counter tactic pipeline/u);
    expect(proofs.has("reference-size-beaten")).toBe(true);
    expect(proofs.has("shape-features-covered")).toBe(true);
    expect(proofs.has("fleet-duel-lowering-covered")).toBe(true);
    expect(optimizations.has("fleet-duel-lowering")).toBe(true);
  });

  it("selects shape-specific GameIntent microkernels below the real reference spans", () => {
    const cases = [
      {
        path: "examples/fox-hunt-100.mkpro",
        shape: "board_line_count",
        steps: 104,
        referenceSpan: 105,
        referenceEntries: 101,
        referenceGaps: ["10", "88", "94", "98"],
      },
      {
        path: "examples/minesweeper-9x9.mkpro",
        shape: "board_neighbor_count",
        steps: 96,
        referenceSpan: 97,
        referenceEntries: 95,
        referenceGaps: ["76", "78"],
      },
      {
        path: "examples/treasure-hunter-2.mkpro",
        shape: "world_table",
        steps: 104,
        referenceSpan: 105,
        referenceEntries: 102,
        referenceGaps: ["91", "94", "95"],
      },
      {
        path: "examples/dangerous-loading.mkpro",
        shape: "lane_resource",
        steps: 102,
        referenceSpan: 103,
        referenceEntries: 101,
        referenceGaps: ["34", "50"],
      },
    ] as const;
    const hexFingerprints = new Set<string>();

    for (const testCase of cases) {
      const result = compileMKPro(source(testCase.path));
      const selected = new Set(result.report.candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));
      const proofs = new Set(result.report.proofs.map((proof) => proof.id));

      hexFingerprints.add(result.steps.map((step) => step.hex).join(" "));
      expect(result.report.steps).toBe(testCase.steps);
      expect(result.report.steps).toBeLessThan(testCase.referenceSpan);
      expect(result.report.reference?.referenceSpan).toBe(testCase.referenceSpan);
      expect(result.report.reference?.referenceSteps).toBe(testCase.referenceSpan);
      expect(result.report.reference?.referenceEntries).toBe(testCase.referenceEntries);
      expect(result.report.reference?.referenceGaps).toEqual(testCase.referenceGaps);
      expect(selected.has(testCase.shape)).toBe(true);
      expect(result.report.rejectedCandidates.some((candidate) => candidate.variant === "universal_spatial_resource")).toBe(true);
      expect(result.report.warnings.join("\n")).toMatch(new RegExp(`selected ${testCase.shape} semantic microkernel`, "u"));
      expect(proofs.has("reference-size-beaten")).toBe(true);
      expect(proofs.has("shape-features-covered")).toBe(true);
      expect(proofs.has("query-lowering-covered")).toBe(true);
    }

    expect(hexFingerprints.size).toBe(cases.length);
  });

  it("ports Lord_BSS Alaram below the original listing size", () => {
    const alaram = source("examples/alaram.mkpro");
    const result = compileMKPro(alaram);
    const selected = new Set(result.report.candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));

    expect(alaram).toMatch(/Lord_BSS pmk210/u);
    expect(result.report.steps).toBe(102);
    expect(result.report.reference?.referenceSpan).toBe(105);
    expect(result.report.reference?.referenceSteps).toBe(105);
    expect(result.report.reference?.referenceEntries).toBe(105);
    expect(result.report.reference?.referenceGaps).toEqual([]);
    expect(result.report.reference?.parity).toBe("smaller");
    expect(selected.has("lane_resource")).toBe(true);
    expect(result.report.warnings.join("\n")).not.toMatch(/was not found/u);
  });

  it("ports Lord_BSS Dungeon below the original listing size", () => {
    const dungeon = source("examples/dungeon.mkpro");
    const result = compileMKPro(dungeon);
    const selected = new Set(result.report.candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));

    expect(dungeon).toMatch(/Lord_BSS pmk164/u);
    expect(result.report.steps).toBe(104);
    expect(result.report.reference?.referenceSpan).toBe(105);
    expect(result.report.reference?.referenceSteps).toBe(105);
    expect(result.report.reference?.referenceEntries).toBe(105);
    expect(result.report.reference?.referenceGaps).toEqual([]);
    expect(result.report.reference?.parity).toBe("smaller");
    expect(selected.has("world_table")).toBe(true);
    expect(result.report.warnings.join("\n")).not.toMatch(/was not found/u);
  });

  it("records board and world query constructs in GameIntent reports", () => {
    for (const path of [
      "examples/fox-hunt-100.mkpro",
      "examples/minesweeper-9x9.mkpro",
      "examples/treasure-hunter-2.mkpro",
      "examples/dangerous-loading.mkpro",
      "examples/alaram.mkpro",
      "examples/dungeon.mkpro",
      "examples/giants-country.mkpro",
    ]) {
      const result = compileMKPro(source(path));

      expect(result.report.optimizations.some((optimization) => optimization.name === "spatial-query-lowering")).toBe(true);
      expect(result.report.proofs.some((proof) => proof.id === "spatial-query-semantics")).toBe(true);
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
    const result = compileMKPro(source("examples/cave-treasure.mkpro"));
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

  it("reports automatic optimizer capabilities for V2 programs", () => {
    const result = compileMKPro(source("examples/cave-sketch.mkpro"));

    expect(result.report.ir.lowered).toBe(true);
    expect(result.report.ir.v2).toBe(true);
    expect(result.report.optimizer.automatic).toBe(true);
    expect(result.report.optimizer.capabilities.some((capability) => capability.id === "x2-display-register")).toBe(true);
    expect(result.report.optimizer.capabilities.some((capability) => capability.id === "r0-alias-indirect")).toBe(true);
    expect(result.report.optimizer.capabilities.some((capability) => capability.id === "super-dark-dispatch")).toBe(true);
  });

  it("marks stack-current-x-scheduling active when the matching optimization fires", () => {
    const result = compileMKPro(source("examples/lunar.mkpro"));
    const applied = new Set(result.report.optimizations.map((optimization) => optimization.name));
    expect(applied.has("stack-current-x-scheduling")).toBe(true);
    const capability = result.report.optimizer.capabilities.find(
      (entry) => entry.id === "stack-current-x-scheduling",
    );
    expect(capability?.status).toBe("active");
  });

  it("records exact MK-61 emulator facts for advanced dispatch hacks", () => {
    const factIds = new Set(MK61_PROFILE.emulatorFacts.map((fact) => fact.id));
    const featureIds = new Set(MK61_PROFILE.features.map((feature) => feature.id));

    expect(featureIds.has("super-dark-dispatch")).toBe(true);
    expect(featureIds.has("r0-fractional-sentinel")).toBe(true);
    expect(featureIds.has("raw-display-5f")).toBe(true);
    expect(factIds.has("super-dark-fa-ff-indirect")).toBe(true);
    expect(factIds.has("r0-fractional-jump-99")).toBe(true);
    expect(factIds.has("r0-fractional-selects-r3")).toBe(true);
    expect(factIds.has("raw-display-5f")).toBe(true);
    expect(MK61_PROFILE.emulatorFacts.find((fact) => fact.id === "r0-star-f-aliases")?.detail).toMatch(/do not preserve R0/u);
  });

  it("always uses exact-machine lowerings for the full cave reference", () => {
    const result = compileMKPro(source("examples/cave-treasure.mkpro"));
    const applied = new Set(result.report.optimizations.map((optimization) => optimization.name));
    expect(applied.has("super-dark-dispatch")).toBe(false);
    expect(applied.has("x2-display-byte-scheduling")).toBe(true);
    expect(result.report.rejectedCandidates.find((candidate) => candidate.variant === "super-dark-dispatch")?.reason).toMatch(/proved FA\.\.FF selector/u);
    expect(result.report.steps).toBeLessThanOrEqual(105);
  });

  it("uses maximum dispatch lowering by default", () => {
    const result = compileMKPro(source("examples/tiny-game.mkpro"));
    const selected = result.report.candidates.find((candidate) => candidate.selected);
    const rejectedIndirect = result.report.rejectedCandidates.find(
      (candidate) => candidate.variant === "indirect-register-flow",
    );

    expect(selected?.variant).toBe("fallthrough-compare-chain");
    expect(selected?.reason).toMatch(/key-based dispatch/u);
    expect(rejectedIndirect?.reason).toMatch(/key-valued, not address-valued/u);
    expect(result.report.optimizer.capabilities.some((capability) => capability.status === "active")).toBe(true);
  });

  it("considers generic dark dispatch only through proof-backed candidates", () => {
    const result = compileMKPro(source("examples/human.mkpro"));
    const dark = result.report.candidates.find((candidate) => candidate.variant === "super-dark-dispatch");

    expect(dark?.selected).toBe(false);
    expect(dark?.reason).toMatch(/layout proof/u);
    expect(dark?.reason).not.toMatch(/unsafe|blocked/u);
  });

  it("can load compiled output into the headless emulator wrapper", () => {
    const { MK61 } = require("./emulator/mk61.cjs") as { MK61: new () => { loadProgram: (codes: number[]) => void; readProgramCodes: (count: number) => number[] } };
    const result = compileMKPro(source("examples/basic.mkpro"));
    const calc = new MK61();
    const codes = result.steps.map((step) => step.opcode);

    calc.loadProgram(codes);

    expect(calc.readProgramCodes(codes.length)).toEqual(codes);
  });

  it("does not expose unsafe optimizer fields", () => {
    const result = compileMKPro(source("examples/tiny-game.mkpro"), { budget: 999 });
    const json = JSON.stringify(result.report);
    expect(json).not.toMatch(/unsafe|unsafe-unverified/u);
    expect(result.report.optimizer.capabilities.every((capability) => capability.status !== "planned" || capability.detail.length > 0)).toBe(true);
  });
});
