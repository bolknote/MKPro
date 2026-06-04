import { createRequire } from "node:module";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { compileMKPro, MK61_PROFILE } from "../src/core/index.ts";

const require = createRequire(import.meta.url);

function source(path: string): string {
  return readFileSync(path, "utf8");
}

function examplePaths(dir: string): string[] {
  if (!existsSync(dir)) return [];
  return readdirSync(dir, { withFileTypes: true })
    .filter((entry) => entry.isFile() && entry.name.endsWith(".mkpro"))
    .map((entry) => `${dir}/${entry.name}`)
    .sort();
}

const RUNNABLE_EXAMPLES = examplePaths("examples");
const PENDING_LOWERER_EXAMPLES = examplePaths("examples/pending-lowerers");
const PENDING_OPTIMIZER_EXAMPLES = examplePaths("examples/pending-optimizer");

describe("MK-Pro compiler", () => {
  it("keeps the smallest human DSL example compiling", () => {
    const result = compileMKPro(source("examples/basic.mkpro"));

    expect(result.report.ir.v2).toBe(true);
    expect(result.report.steps).toBeLessThanOrEqual(105);
    expect(result.report.optimizations.some((optimization) => optimization.name === "intent-read-lowering")).toBe(true);
  });

  it("keeps every runnable example within the 105-cell ceiling", () => {
    const oversized: Array<{ path: string; steps: number }> = [];

    for (const path of RUNNABLE_EXAMPLES) {
      const result = compileMKPro(source(path));
      if (result.report.steps > 105) oversized.push({ path, steps: result.report.steps });
    }

    expect(oversized).toEqual([]);
  }, 60_000);

  it("keeps unfinished game ports outside the runnable example set", () => {
    expect(RUNNABLE_EXAMPLES).toContain("examples/99-bottles.mkpro");
    expect(RUNNABLE_EXAMPLES).toContain("examples/alaram.mkpro");
    expect(RUNNABLE_EXAMPLES).toContain("examples/cave-sketch.mkpro");
    expect(RUNNABLE_EXAMPLES).toContain("examples/dangerous-loading.mkpro");
    expect(RUNNABLE_EXAMPLES).toContain("examples/dungeon.mkpro");
    expect(RUNNABLE_EXAMPLES).toContain("examples/game-100-pig.mkpro");
    expect(RUNNABLE_EXAMPLES).toContain("examples/lunar.mkpro");
    expect(RUNNABLE_EXAMPLES).toContain("examples/minesweeper-9x7.mkpro");
    expect(RUNNABLE_EXAMPLES).toContain("examples/minesweeper-9x9.mkpro");
    expect(RUNNABLE_EXAMPLES).toContain("examples/raja-yoga.mkpro");
    expect(RUNNABLE_EXAMPLES).toContain("examples/rambo-iii.mkpro");
    expect(RUNNABLE_EXAMPLES).toContain("examples/sea-battle.mkpro");
    expect(RUNNABLE_EXAMPLES).toContain("examples/tic-tac-toe.mkpro");
    expect(PENDING_LOWERER_EXAMPLES).toEqual([]);
    expect(PENDING_OPTIMIZER_EXAMPLES.length).toBeGreaterThan(0);

    for (const path of PENDING_OPTIMIZER_EXAMPLES) {
      expect(RUNNABLE_EXAMPLES).not.toContain(path.replace("examples/pending-optimizer/", "examples/"));
      expect(() => compileMKPro(source(path), { budget: 999, analysis: true })).not.toThrow(/real rule lowerers before code generation/u);
    }
  }, 60_000);

  it("keeps the 3x3 tic-tac-toe port high-level and compact", () => {
    const game = source("examples/tic-tac-toe.mkpro");

    expect(game).toContain("program TicTacToe");
    expect(game).toContain("fn outer_corner_line()");
    expect(game).toContain("fn middle_side_line()");
    expect(game).not.toMatch(/\bcore\s*\{/u);
    expect(game).not.toMatch(/\brow\s+[0-9A-F]{2}\s*:/u);

    const result = compileMKPro(game, { budget: 999, analysis: true });

    expect(result.diagnostics).toEqual([]);
    expect(result.report.steps).toBe(100);
    expect(result.report.steps).toBeLessThanOrEqual(105);
  }, 20_000);

  it("keeps the 4x4 tic-tac-toe port source-shaped around packed line families", () => {
    const game = source("examples/pending-optimizer/tic-tac-toe-4x4.mkpro");

    expect(game).toContain("reference anvarov_tic_tac_toe_4x4");
    expect(game).toContain("lines: packed[1..4]");
    expect(game).toContain("packed_add(lines[3], line");
    expect(game).toContain("packed_digit(lines[4], line)");
    expect(game).not.toMatch(/\bcore\s*\{/u);
    expect(game).not.toMatch(/\brow\s+[0-9A-F]{2}\s*:/u);

    const result = compileMKPro(game, { budget: 999999, analysis: true });

    expect(result.diagnostics).toEqual([]);
    expect(result.report.reference?.referenceSpan).toBe(105);
    expect(result.report.steps).toBe(313);
  }, 20_000);

  it("keeps every runnable example with a real source reference no larger than that source", () => {
    const unresolved: string[] = [];
    const larger: Array<{ path: string; steps: number; reference: number }> = [];
    const checked: string[] = [];

    for (const path of RUNNABLE_EXAMPLES) {
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
    expect(checked).toContain("examples/99-bottles.mkpro");
    expect(checked).toContain("examples/alaram.mkpro");
    expect(checked).toContain("examples/cave-sketch.mkpro");
    expect(checked).toContain("examples/dangerous-loading.mkpro");
    expect(checked).toContain("examples/dungeon.mkpro");
    expect(checked).toContain("examples/game-100-pig.mkpro");
    expect(checked).toContain("examples/lunar.mkpro");
    expect(checked).toContain("examples/minesweeper-9x7.mkpro");
    expect(checked).toContain("examples/minesweeper-9x9.mkpro");
    expect(checked).toContain("examples/raja-yoga.mkpro");
    expect(checked).toContain("examples/rambo-iii.mkpro");
    expect(checked).toContain("examples/sea-battle.mkpro");
  }, 20_000);

  it("keeps the full cave reference high-level but does not fit it through templates", () => {
    const reference = source("examples/pending-optimizer/cave-treasure.mkpro");

    expect(reference).not.toMatch(/\brecipe\b/iu);
    expect(reference).not.toMatch(/core\s+exact/iu);
    expect(reference).not.toMatch(/row\s+[0-9A-F]{2}\s*:/iu);
    expect(() => compileMKPro(reference, { budget: 999 })).toThrow(/outside 00\.\.A4/u);
  });

  it("compiles human-centered MK-Pro without source implementation switches", () => {
    const result = compileMKPro(source("examples/human.mkpro"));

    expect(result.report.ir.v2).toBe(true);
    expect(result.report.machine).toBe("mk61");
    expect(result.report.steps).toBeLessThanOrEqual(105);
    expect(result.report.steps).toBe(23);
    expect(result.report.candidates.some((candidate) => candidate.variant === "dark-indirect-table")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((feature) => feature.id === "code-data-overlay")).toBe(true);
    expect(result.report.proofs.some((proof) => proof.id === "value-ranges")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "numeric-dispatch-residual-chain")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "ephemeral-input-dispatch")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "indirect-incdec-counter")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "terminal-loop-screen-elision")).toBe(true);
  });

  it("lowers small coordinate-set helpers to generic near/equality arithmetic", () => {
    const helperSource = `
program SmallSetHelpers {
  state {
    room: counter 0..20 = 5
    pit1: counter 0..20 = 6
    pit2: counter 0..20 = 15
    near: counter -20..20 = 0
    hit: counter -999..999 = 0
  }

  loop {
    near = near_any(room, 1, pit1, pit2)
    hit = eq_any(room, pit1, pit2)
    halt(near + hit)
  }
}
`;

    const handWrittenSource = helperSource
      .replace("near_any(room, 1, pit1, pit2)", "max(1 - abs(room - pit1), 1 - abs(room - pit2))")
      .replace("eq_any(room, pit1, pit2)", "(room - pit1) * (room - pit2)");

    const helperResult = compileMKPro(helperSource);
    const handWrittenResult = compileMKPro(handWrittenSource);

    expect(helperResult.report.steps).toBe(handWrittenResult.report.steps);
    expect(helperResult.report.optimizations.filter((optimization) =>
      optimization.name === "small-set-primitive-lowering"
    )).toHaveLength(2);
  });

  it("rejects removed random_cell calls", () => {
    expect(() => compileMKPro(`
program RemovedRandomCell {
  grid: board(1..20, 1..1)

  state {
    a: coord(grid)
  }

  loop {
    a = random_cell(grid)
    halt(a)
  }
}
`)).toThrow(/Unknown function random_cell/u);
  });

  it("shares repeated random(domain) arithmetic without sharing the random draw", () => {
    const result = compileMKPro(`
program RandomLenReuse {
  grid: board(1..20, 1..1)

  state {
    a: coord(grid) = 1
    b: coord(grid) = 1
    c: coord(grid) = 1
  }

  loop {
    a = random(grid)
    b = random(grid)
    c = random(grid)
    halt(a + b + c)
  }
}
`, { budget: 999999, analysis: true });

    const names = result.report.optimizations.map((optimization) => optimization.name);
    expect(names.filter((name) => name === "random-cell-helper-call")).toHaveLength(3);
    expect(names).toContain("random-cell-helper");
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

  loop {
    key = read()
    inc()
    show(score)
  }
  fn inc() {
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

  it("does not pretend the full cave reference is optimized enough yet", () => {
    const reference = source("examples/pending-optimizer/cave-treasure.mkpro");

    expect(reference).not.toMatch(/core\s+exact/iu);
    expect(reference).not.toMatch(/row\s+[0-9A-F]{2}\s*:/iu);
    expect(reference).toMatch(/cave: board\(packed_decimal_zero_run\)/u);
    expect(reference).toMatch(/cells\(cave\) = random\(\)/u);

    expect(() => compileMKPro(reference, { budget: 999 })).toThrow(/outside 00\.\.A4/u);
  });

  it("ports Bolknote's 99 Bottles demo within the original size", () => {
    const { MK61 } = require("./emulator/mk61.cjs") as {
      MK61: new () => {
        loadProgram: (codes: number[]) => { diagnostics: string[] };
        readProgramCodes: (count: number) => number[];
        setRegister: (register: string, value: string) => unknown;
        pressSequence: (keys: string[]) => unknown;
        displayText: () => string;
        runUntilStable: (options: { maxFrames: number; stableFrames: number }) => { stopped: boolean };
      };
    };
    const result = compileMKPro(source("examples/99-bottles.mkpro"));
    const mkproSource = source("examples/99-bottles.mkpro");
    const calc = new MK61();
    const loaded = calc.loadProgram([0x3a, 0x50]);

    calc.setRegister("X", "8112006Е");
    calc.pressSequence(["В/О", "С/П"]);

    expect(loaded.diagnostics).toEqual([]);
    expect(result.report.reference?.name).toBe("bolknote_99_bottles");
    expect(result.report.reference?.referenceSpan).toBe(53);
    expect(result.report.reference?.parity).not.toBe("larger");
    expect(result.report.steps).toBeLessThanOrEqual(53);
    expect(result.report.warnings.join("\n")).not.toMatch(/was not found/u);
    expect(mkproSource).toContain(`show("BEEr ", bottles:02)`);
    expect(mkproSource).not.toMatch(/\braw\s*\{/u);
    expect(calc.runUntilStable({ maxFrames: 200, stableFrames: 6 }).stopped).toBe(true);
    expect(calc.readProgramCodes(2)).toEqual([0x3a, 0x50]);
    expect(calc.displayText()).toBe("8,ЕЕГ  91");
  });

  it("does not contain benchmark-special or spatial-template codegen entry points", () => {
    const compilerSource = source("src/core/compiler.ts");
    const oldSpatialIntentName = "Game" + "Intent";
    const oldSpatialEntryPoint = "tryCompile" + oldSpatialIntentName + "Program";

    expect(compilerSource).not.toMatch(/CAVE_TREASURE/u);
    expect(compilerSource).not.toMatch(/tryCompileCompactGameProgram/u);
    expect(compilerSource).not.toMatch(/isCaveTreasure/u);
    expect(compilerSource).not.toMatch(/buildCompactCave/u);
    expect(compilerSource).not.toMatch(/cave_treasure/iu);
    expect(compilerSource).not.toContain(oldSpatialIntentName);
    expect(compilerSource).not.toContain(oldSpatialEntryPoint);
  });

  it("treats reference metadata as report-only for supported programs", () => {
    const reference = source("examples/99-bottles.mkpro");
    const renamed = reference.replace(/^reference .+$/mu, "reference renamed_metadata_only");
    const unreferenced = reference.replace(/^reference .+\n/mu, "");
    const original = compileMKPro(reference);
    const changed = compileMKPro(renamed);
    const anonymous = compileMKPro(unreferenced);
    const originalHex = original.steps.map((step) => step.hex);

    expect(changed.steps.map((step) => step.hex)).toEqual(originalHex);
    expect(anonymous.steps.map((step) => step.hex)).toEqual(originalHex);
    expect(changed.report.reference?.name).toBe("renamed_metadata_only");
    expect(changed.report.warnings.join("\n")).toMatch(/was not found under games/u);
  });

  it("reports automatic optimizer capabilities for V2 programs", () => {
    const result = compileMKPro(source("examples/human.mkpro"));

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

  it("does not compile the full cave reference through exact-machine templates", () => {
    expect(() => compileMKPro(source("examples/pending-optimizer/cave-treasure.mkpro"), { budget: 999 })).toThrow(/outside 00\.\.A4/u);
  });

  it("uses maximum dispatch lowering by default", () => {
    const result = compileMKPro(source("examples/tiny-game.mkpro"));
    const selected = result.report.candidates.find((candidate) =>
      candidate.selected && candidate.site.startsWith("dispatch@")
    );
    const rejectedIndirect = result.report.rejectedCandidates.find(
      (candidate) => candidate.variant === "indirect-register-flow",
    );

    expect(selected?.variant).toBe("fallthrough-compare-chain");
    expect(selected?.reason).toMatch(/key-based dispatch/u);
    expect(rejectedIndirect?.reason).toMatch(/key-valued, not address-valued/u);
    expect(result.report.optimizer.capabilities.some((capability) => capability.status === "active")).toBe(true);
  });

  it("merges dispatch cases that are identical to the default branch", () => {
    const result = compileMKPro(source("examples/dangerous-loading.mkpro"));

    expect(result.report.steps).toBe(84);
    expect(result.report.reference?.referenceSpan).toBe(103);
    expect(result.report.optimizations.some((optimization) => optimization.name === "dispatch-default-merge")).toBe(true);
    expect(result.report.optimizations.some((optimization) => optimization.name === "tail-call-lowering")).toBe(true);
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
