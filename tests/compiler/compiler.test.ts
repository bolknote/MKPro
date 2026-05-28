import { describe, expect, it } from "vitest";
import { CompileError, compileMKPro } from "../../src/core/index.ts";
import type { CompileOptions } from "../../src/core/index.ts";

function compileOk(source: string, options: Partial<CompileOptions> = { budget: 999 }) {
  return compileMKPro(source, options);
}

describe("compiler semantics", () => {
  it("compiles only the V2 program surface", () => {
    expect(() =>
      compileMKPro(`
machine mk61
entry main {
  pause 0
}
`),
    ).toThrow(/Unexpected top-level line 'machine mk61'/u);
  });

  it("emits a final stop for a minimal V2 program", () => {
    const result = compileOk(`
program Minimal {
  turn {
    stop 0
  }
}
`);
    expect(result.report.ir.v2).toBe(true);
    expect(result.steps.some((step) => step.hex === "50")).toBe(true);
  });

  it("lowers MK-61 primitive functions from V2 formulas", () => {
    const result = compileOk(`
program FormulaPrimitives {
  state {
    value: packed = 0
  }
  turn {
    value = max(pi(), sqr(2)) + inv(2) + pow(2, 3) + bit_and(1, 2) + bit_or(1, 2) + bit_xor(1, 2) + bit_not(1)
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    for (const opcode of ["20", "22", "23", "24", "36", "37", "38", "39", "3A"]) {
      expect(opcodes).toContain(opcode);
    }
  });

  it("lowers mask, cell, and packed digit helpers from V2 formulas", () => {
    const result = compileOk(`
program FormulaHelpers {
  state {
    mask: packed = 0
    value: packed = 0
  }
  turn {
    value = bit_mask(5) + bit_has(mask, 5)
    mask = bit_set(mask, 5)
    value = value + bit_clear(mask, 5) + bit_toggle(mask, 5)
    value = value + cell_mask(1, 2) + cell_has(mask, 1, 2)
    mask = cell_set(mask, 1, 2)
    value = value + cell_clear(mask, 1, 2) + cell_toggle(mask, 1, 2)
    value = value + digit_at(1234, 2) + digit_add(1000, 1, 7) + digit_set(1234, 2, 9)
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    for (const opcode of ["15", "24", "34", "35", "37", "38", "39", "3A"]) {
      expect(opcodes).toContain(opcode);
    }
  });

  it("reuses a successful cell membership mask when clearing that cell", () => {
    const result = compileOk(`
program ClearAfterHit {
  state {
    pos: coord(cave) = 12
    marks: cells(cave) = random()
  }
  world cave {
    position pos {
    }
  }
  turn {
    if pos in marks {
      marks -= pos
      stop 1
    }
    else {
      stop 0
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "cell-membership-clear-reuse")).toBe(true);
  });

  it("hoists common branch tails before MK-61 code generation", () => {
    const result = compileOk(`
program CommonBranchTail {
  state {
    selector: counter 0..9 = 0
    value: counter 0..9 = 0
  }
  screen view {
    show value
  }
  turn {
    if selector == 0 {
      value = 1
      show view
    }
    else {
      value = 2
      show view
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "common-branch-tail-hoisting")).toBe(true);
    expect(result.steps.filter((step) => step.hex === "50")).toHaveLength(1);
  });

  it("shares repeated packed display bodies through a normal helper", () => {
    const result = compileOk(`
program RepeatedPackedDisplay {
  state {
    selector: counter 0..9 = 0
    a: packed = 1
    b: packed = 2
    c: packed = 3
  }
  screen view {
    show a, b, c
  }
  turn {
    match selector {
      1 => show view
      2 => show view
      3 => show view
      otherwise => stop 0
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "packed-display-helper")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "packed-display-helper-call")).toBe(true);
  });

  it("shares repeated pure expression bodies through a normal helper", () => {
    const result = compileOk(`
program RepeatedExpression {
  state {
    pos: packed = 23
    map: packed = 123456789
    a: packed = 0
    b: packed = 0
    c: packed = 0
  }
  turn {
    a = digit_at(map, pos - int(pos / 10) * 10)
    b = digit_at(map, pos - int(pos / 10) * 10)
    c = digit_at(map, pos - int(pos / 10) * 10)
    stop a + b + c
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "expression-helper")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "expression-helper-call")).toBe(true);
  });

  it("lowers integer remainders without recomputing the dividend", () => {
    const result = compileOk(`
program RemainderLowering {
  state {
    value: packed = 23
    ones: packed = 0
  }
  turn {
    ones = value - 10 * int(value / 10)
    stop ones
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "remainder-fraction-lowering")).toBe(true);
  });

  it("reuses one cell bit mask across adjacent set updates", () => {
    const result = compileOk(`
program AdjacentSetUpdates {
  grid: board(1..4, 1..4)
  state {
    cell: coord(grid) = 11
    mine: cells(grid)
    seen: cells(grid)
  }
  turn {
    mine += cell
    seen += cell
    stop mine + seen
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "bit-set-mask-cse")).toBe(true);
  });

  it("shares repeated show-show-read sequences", () => {
    const result = compileOk(`
program RepeatedChallengePrompt {
  state {
    warning_value: packed = 7
    memory_value: packed = 3
    answer: packed = 0
    selector: counter 0..9 = 0
  }
  screen warning {
    show warning_value
  }
  screen memory {
    show memory_value
  }
  turn {
    show warning
    show memory
    read answer
    show warning
    show memory
    read answer
    show warning
    show memory
    read answer
    show warning
    show memory
    read answer
    show warning
    show memory
    read answer
    show warning
    show memory
    read answer
    stop answer
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "show-sequence-helper")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "show-sequence-helper-call")).toBe(true);
  });

  it("lowers contracted raw blocks inside V2 rules", () => {
    const result = compileOk(`
program RawRule {
  state {
    value: packed = 2
    result: packed = 0
  }
  turn {
    hack
    stop result
  }
  rule hack {
    raw {
      takes Y = value, X = 3
      returns X -> result
      clobbers X, Y, X1
      preserves state
      code {
        +
        К ИНВ
      }
    }
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(opcodes).toContain("10");
    expect(opcodes).toContain("3A");
    expect(result.report.optimizations.some((item) => item.name === "raw-block-contract")).toBe(true);
    expect(result.report.cellRoles.some((cell) => cell.note?.includes("raw opcode"))).toBe(true);
  });

  it("keeps raw numeric branches behind the optimizer barrier", () => {
    const result = compileOk(`
program StableIndirectFlow {
  state {
    out: packed = 0
  }
  turn {
    raw {
      returns X -> out
      clobbers X, R7
      preserves state
      code {
        1
        2
        X->П R7
        БП 12
        9
      }
    }
    stop out
  }
}
`);
    const selected = new Set(result.report.candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));
    const features = new Set(result.report.machineFeaturesUsed.map((feature) => feature.id));

    expect(result.steps.map((step) => step.hex)).toContain("51");
    expect(result.steps.map((step) => step.hex)).not.toContain("87");
    expect(selected.has("stable-indirect-flow")).toBe(false);
    expect(features.has("indirect-flow")).toBe(false);
    expect(result.report.proofs.some((proof) => proof.id === "indirect-addressing-ranges")).toBe(false);
  });

  it("does not add compiler-owned preloads for raw numeric branches", () => {
    const result = compileOk(`
program PreloadedIndirectFlow {
  turn {
    raw {
      clobbers X
      preserves state
      code {
        БП 13
      }
    }
    stop 0
  }
}
`);
    const selected = new Set(result.report.candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));
    const features = new Set(result.report.machineFeaturesUsed.map((feature) => feature.id));

    expect(result.steps.map((step) => step.hex)).toContain("51");
    expect(result.steps.map((step) => step.hex)).not.toContain("87");
    expect(result.report.preloads).not.toContainEqual({ register: "7", value: "C5", countsAgainstProgram: false });
    expect(selected.has("preloaded-indirect-flow")).toBe(false);
    expect(features.has("indirect-flow")).toBe(false);
    expect(result.report.proofs.some((proof) => proof.id === "indirect-addressing-ranges")).toBe(false);
  });

  it("keeps raw direct memory access exact through a live selector", () => {
    const result = compileOk(`
program IndirectMemoryTable {
  state {
    out: packed = 0
  }
  turn {
    raw {
      returns X -> out
      clobbers X, R7
      preserves state
      code {
        2
        X->П R7
        П->X R2
      }
    }
    stop out
  }
}
`);
    const selected = new Set(result.report.candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));
    const activeCapabilities = new Set(
      result.report.optimizer.capabilities
        .filter((capability) => capability.status === "active")
        .map((capability) => capability.id),
    );
    const features = new Set(result.report.machineFeaturesUsed.map((feature) => feature.id));

    expect(result.steps.map((step) => step.hex)).toContain("62");
    expect(result.steps.map((step) => step.hex)).not.toContain("D7");
    expect(selected.has("indirect-memory-table")).toBe(false);
    expect(activeCapabilities.has("indirect-memory-table")).toBe(false);
    expect(features.has("indirect-memory")).toBe(false);
  });

  it("keeps raw formal dark branch operands as address bytes", () => {
    const result = compileOk(`
program RawFormalAddress {
  turn {
    raw {
      clobbers X
      preserves state
      code {
        БП C5
      }
    }
    stop 0
  }
}
`);
    const cells = result.steps.map((step) => step.hex);

    expect(cells.slice(0, 2)).toEqual(["51", "C5"]);
    expect(result.steps[1]?.mnemonic).toBe("C5");
    expect(result.steps[1]?.comment).toMatch(/formal C5->13/u);
    expect(result.report.cellRoles[1]?.roles).toContain("formal-address");
    expect(result.report.cellRoles[1]?.roles).toContain("dark-entry");
    expect(result.report.proofs.find((proof) => proof.id === "formal-address-operands")?.status).toBe("proved");
  });

  it("reports unknown instructions in contracted raw blocks as compile errors", () => {
    expect(() =>
      compileOk(`
program BadRawOpcode {
  turn {
    raw {
      clobbers X
      preserves state
      code {
        definitely_not_an_opcode
      }
    }
  }
}
`),
    ).toThrow(CompileError);
  });

  it("accepts decimal constants inside V2 formulas", () => {
    const result = compileOk(`
program DecimalFormula {
  state {
    burn: counter 0..99 = 0
    fuel: counter 0..999 = 140
    accel: packed = 0
  }
  turn {
    accel = burn * 10 / fuel - 9.8
    stop accel
  }
}
`);

    expect(result.steps.length).toBeGreaterThan(0);
  });

  it("removes branchy boolean V2 assignments when arithmetic selection is shorter", () => {
    const result = compileOk(`
program BranchlessAssignment {
  state {
    flag: flag = 0
    result: counter 0..99 = 0
  }
  turn {
    if flag == 1 {
      result = 50
    }
    else {
      result = 10
    }
    stop result
  }
}
`);
    expect(result.report.optimizations.some((item) => item.name === "branch-removal")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-select")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((item) => item.id === "branch-removal")).toBe(true);
    expect(result.report.proofs.some((item) => item.id === "branch-equivalence")).toBe(true);
  });

  it("uses counter ranges for saturating unit updates", () => {
    const result = compileOk(`
program ResourceRange {
  state {
    fuel: counter 0..9 = 4
  }
  turn {
    if fuel > 0 {
      fuel--
    }
    stop fuel
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-max")).toBe(true);
    expect(result.report.proofs.some((item) => item.id === "value-ranges")).toBe(true);
  });

  it("normalizes adjacent integer comparison bounds when it enables a zero test", () => {
    const result = compileOk(`
program BoundaryNormalize {
  state {
    fuel: counter 0..9 = 4
  }
  turn {
    if fuel <= -1 {
      fuel--
      show 0
    }
    stop fuel
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "comparison-boundary-normalization")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "zero-condition-test")).toBe(true);
  });

  it("replaces boolean-selected V2 stops with terminal selection", () => {
    const result = compileOk(`
program BranchlessStop {
  state {
    flag: flag = 0
  }
  turn {
    if flag == 1 {
      stop 50
    }
    else {
      stop 10
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-terminal-select")).toBe(true);
  });

  it("extends terminal-select to comparison conditions when branchless is shorter", () => {
    const result = compileOk(`
program BranchlessCompare {
  state {
    counter: counter 0..9 = 0
  }
  turn {
    if counter > 0 {
      stop 1
    }
    else {
      stop 0
    }
  }
}
`);
    const applied = result.report.optimizations.filter((item) => item.name === "arithmetic-if-terminal-select");
    expect(applied.length).toBeGreaterThanOrEqual(1);
  });

  it("records branchless terminal-select as considered when the branched form wins", () => {
    const result = compileOk(`
program LunarLike {
  state {
    speed: counter -99..99 = 0
  }
  turn {
    if abs(speed) <= 5 {
      stop 777
    }
    else {
      stop 666
    }
  }
}
`);
    const applied = result.report.optimizations.filter((item) => item.name === "arithmetic-if-terminal-select");
    expect(applied.length).toBe(0);

    const rejected = result.report.rejectedCandidates.find(
      (entry) => entry.variant === "arithmetic-if-terminal-select",
    );
    expect(rejected).toBeDefined();
    expect(rejected!.reason).toMatch(/branched form was shorter/u);

    const capability = result.report.optimizer.capabilities.find(
      (entry) => entry.id === "arithmetic-if-select",
    );
    expect(capability?.status).toBe("considered");
  });

  it("surfaces budget exceeded as a hard error for V2 programs", () => {
    expect(() =>
      compileMKPro(`
program TooLarge {
  turn {
    stop 1
  }
}
`, { budget: 1 }),
    ).toThrow(CompileError);
  });

  it("keeps overbudget output inspectable in analysis mode", () => {
    const result = compileMKPro(`
program AnalyzeBudget {
  state {
    value: packed = 0
  }
  turn {
    value = 1
    stop value
  }
}
`, { budget: 1, analysis: true });

    expect(result.report.budgetReport.exceeded).toBe(true);
    expect(result.diagnostics.some((diagnostic) => diagnostic.code === "BUDGET_EXCEEDED" && diagnostic.level === "warning")).toBe(true);
    expect(result.steps.length).toBeGreaterThan(1);
  });

  it("keeps analysis output inspectable past the byte address range", () => {
    const pauses = Array.from({ length: 130 }, () => "    show 0").join("\n");
    const result = compileMKPro(`
program AnalyzeHuge {
  turn {
${pauses}
  }
}
`, { budget: 1, analysis: true });

    expect(result.steps.length).toBeGreaterThan(0xff);
    expect(result.diagnostics.some((diagnostic) => diagnostic.message.includes("exceeds formal MK-61 address range"))).toBe(false);
    expect(result.diagnostics.some((diagnostic) => diagnostic.code === "BUDGET_EXCEEDED" && diagnostic.level === "warning")).toBe(true);
  });

  it("keeps the default V2 budget at 105 cells", () => {
    const result = compileMKPro(`
program DefaultBudget {
  turn {
    stop 1
  }
}
`);
    expect(result.report.budget).toBe(105);
  });
});
