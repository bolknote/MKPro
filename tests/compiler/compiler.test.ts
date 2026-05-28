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

  it("lowers zero-minus expressions through unary sign change", () => {
    const result = compileOk(`
program ZeroMinus {
  state {
    value: packed = 0
  }
  turn {
    read x
    value = 0 - x
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(opcodes).toContain("0B");
    expect(opcodes).not.toContain("11");
  });

  it("folds numeric constant subexpressions before code generation", () => {
    const result = compileOk(`
program ConstantFold {
  state {
    value: packed = 0
  }
  turn {
    value = 5 + 2
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(opcodes[0]).toBe("07");
    expect(opcodes).not.toContain("05");
    expect(opcodes).not.toContain("02");
    expect(opcodes).not.toContain("10");
    expect(result.report.optimizations.some((item) => item.name === "expression-constant-folder")).toBe(true);
  });

  it("folds unary numeric constants before arithmetic", () => {
    const result = compileOk(`
program NegativeConstantFold {
  state {
    value: packed = 0
  }
  turn {
    value = -5 + 7
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(opcodes[0]).toBe("02");
    expect(opcodes).not.toContain("0B");
    expect(opcodes).not.toContain("10");
  });

  it("distributes small constant factors over constant additions", () => {
    const result = compileOk(`
program ConstantDistribute {
  state {
    value: packed = 0
  }
  turn {
    read x
    value = 2 * (2 + x)
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    // 2 * (2 + x) distributes to 2*x + 4. The terms are emitted before the
    // constant so the two literals (2 and 4) never sit next to each other (which
    // would concatenate to 24 on the MK-61); x stays live in X from the read.
    expect(opcodes.slice(2, 6)).toEqual(["02", "12", "04", "10"]);
    expect(result.report.optimizations.some((item) => item.name === "expression-constant-folder")).toBe(true);
  });

  it("distributes negative constant factors over constant subtractions", () => {
    const result = compileOk(`
program NegativeConstantDistribute {
  state {
    value: packed = 0
  }
  turn {
    read x
    value = -5 * (2 - x)
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);
    const multiply = opcodes.indexOf("12");
    const subtract = opcodes.indexOf("11");

    expect(opcodes).toContain("05");
    expect(multiply).toBeGreaterThan(opcodes.indexOf("05"));
    expect(subtract).toBeGreaterThan(multiply);
    expect(opcodes).not.toContain("0B");
    expect(result.report.optimizations.some((item) => item.name === "expression-constant-folder")).toBe(true);
  });

  it("normalizes nested linear expressions with multiple variables", () => {
    const result = compileOk(`
program MultiVariableLinearFold {
  state {
    value: packed = 0
  }
  turn {
    read x
    read y
    value = 2 * (x + y + 3) - (x - 4 * y)
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);
    const afterReads = opcodes.slice(4, opcodes.indexOf("50", 4));

    expect(afterReads).toEqual(["06", "61", "10", "06", "62", "12", "10"]);
    expect(result.report.optimizations.some((item) => item.name === "expression-constant-folder")).toBe(true);
  });

  it("normalizes deeply nested linear constant products", () => {
    const result = compileOk(`
program NestedLinearFold {
  state {
    value: packed = 0
  }
  turn {
    read x
    value = 5 * (2 + 3 * (2 + x))
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(opcodes).toContain("12");
    expect(opcodes).toContain("10");
    expect(result.report.preloads.some((item) => item.value === "40")).toBe(true);
    expect(result.report.preloads.some((item) => item.value === "15")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "expression-constant-folder")).toBe(true);
  });

  it("folds constant bitwise primitive calls before code generation", () => {
    const result = compileOk(`
program ConstantBitwiseCallFold {
  state {
    value: packed = 0
  }
  turn {
    value = bit_or(2, 4)
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(opcodes[0]).toBe("08");
    expect(opcodes).not.toContain("38");
    expect(result.report.optimizations.some((item) => item.name === "expression-constant-folder")).toBe(true);
  });

  it("folds pure constant primitive calls before code generation", () => {
    const result = compileOk(`
program ConstantPrimitiveCallFold {
  state {
    value: packed = 0
  }
  turn {
    value = bit_or(2, 4) + bit_and(7, 3) + bit_xor(6, 3) + max(2, 9) + abs(-3) + sign(-12) + int(2.9) + frac(2.75) + inv(2) + pow(2, 3) + pow10(2)
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(result.report.preloads.some((item) => item.value === "146.25")).toBe(true);
    for (const opcode of ["15", "23", "24", "31", "32", "34", "35", "36", "37", "38", "39"]) {
      expect(opcodes).not.toContain(opcode);
    }
    expect(result.report.optimizations.some((item) => item.name === "expression-constant-folder")).toBe(true);
  });

  it("folds constant bitwise inversion when the MK-61 mantissa result is decimal", () => {
    const result = compileOk(`
program ConstantBitwiseNotFold {
  state {
    value: packed = 0
  }
  turn {
    value = bit_not(99999999)
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(result.report.preloads.some((item) => item.value === "8.6666666")).toBe(true);
    expect(opcodes).not.toContain("3A");
    expect(result.report.optimizations.some((item) => item.name === "expression-constant-folder")).toBe(true);
  });

  it("folds constant max calls with MK-61 zero ordering", () => {
    const result = compileOk(`
program ConstantMaxZeroFold {
  state {
    value: packed = 0
  }
  turn {
    value = max(0, 5)
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(opcodes[0]).toBe("00");
    expect(opcodes).not.toContain("36");
    expect(result.report.optimizations.some((item) => item.name === "expression-constant-folder")).toBe(true);
  });

  it("lowers MK-61 primitive functions from V2 formulas", () => {
    const result = compileOk(`
program FormulaPrimitives {
  state {
    value: packed = 0
  }
  turn {
    read x
    read y
    value = max(pi(), sqr(x)) + inv(y) + pow(x, y) + bit_and(x, y) + bit_or(x, y) + bit_xor(x, y) + bit_not(x)
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

  it("rejects compiler-internal __mkpro names in user source", () => {
    expect(() =>
      compileOk(`
program ReservedName {
  state {
    __mkpro_score: counter 0..9 = 0
  }
  turn {
    stop __mkpro_score
  }
}
`),
    ).toThrow(/reserved compiler-internal prefix/u);
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

  it("does not emit an if-end jump after a terminal then branch", () => {
    const result = compileOk(`
program TerminalThenEnd {
  state {
    flag: flag = 0
    crash_value: packed = -999
  }
  screen crash {
    show crash_value
  }
  turn {
    if flag == 1 {
      show crash
      stop -999
    }
    else {
      stop 1
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "terminal-branch-end-elision")).toBe(true);
  });

  it("branches directly to reusable terminal rules", () => {
    const result = compileOk(`
program DirectTerminalBranch {
  state {
    score: counter 0..9 = 0
    crash_value: packed = -999
  }
  screen crash {
    show crash_value
  }
  turn {
    if score >= 5 {
      fail
    }
    else {
      stop 1
    }
  }
  rule fail {
    show crash
    stop -999
  }
  rule other {
    if score < 2 {
      fail
    }
    else {
      stop 2
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "terminal-if-direct-branch")).toBe(true);
  });

  it("branches to a local terminal tail for multi-command else endings", () => {
    const result = compileOk(`
program LocalTerminalTail {
  state {
    score: counter 0..9 = 0
    fail_value: packed = -999
  }
  screen fail_screen {
    show fail_value
  }
  turn {
    if score < 5 {
      score++
    }
    else {
      show fail_screen
      stop -999
    }
    stop score
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "local-terminal-tail-branch")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "local-terminal-tail")).toBe(true);
  });

  it("uses one selector for multiple guarded updates when shorter", () => {
    const result = compileOk(`
program BooleanMultiUpdate {
  state {
    flag: flag = 0
    a: counter 0..9 = 1
    b: counter 0..9 = 2
  }
  turn {
    if flag == 1 {
      a++
      b--
    }
    stop a + b
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "multi-guarded-update")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "branch-removal")).toBe(true);
  });

  it("keeps negative-zero guarded updates behind the cost gate", () => {
    const result = compileOk(`
program NegativeZeroGuardedUpdate {
  state {
    score: counter 0..999 = 0
    bonus: counter 0..99 = 0
  }
  turn {
    if score >= 100 {
      bonus++
    }
    stop bonus
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "negative-zero-threshold-update")).toBe(false);
    expect(result.report.preloads.some((item) => item.value === "1|-00")).toBe(false);
  });

  it("selects aggressive terminal-if lowering only after full layout wins", () => {
    const result = compileOk(`
program LateLayoutIfVariant {
  state {
    strength: counter -9..9 = 0
    value: counter 0..9 = 0
    fail_value: packed = -999
  }
  screen fail_screen {
    show fail_value
  }
  turn {
    if strength <= 0 {
      exhausted
    }
    value++
    if value >= 9 {
      exhausted
    }
    stop value
  }
  rule exhausted {
    show fail_screen
    stop -999
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "late-layout-if-variant")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "terminal-if-direct-branch")).toBe(true);
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
    const applied = result.report.optimizations.filter((item) =>
      item.name === "arithmetic-if-terminal-select" ||
      item.name === "negative-zero-threshold-terminal-select"
    );
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
    const applied = result.report.optimizations.filter((item) =>
      item.name === "arithmetic-if-terminal-select" ||
      item.name === "negative-zero-threshold-terminal-select"
    );
    expect(applied.length).toBe(0);

    const rejected = result.report.rejectedCandidates.find(
      (entry) => entry.variant === "negative-zero-threshold-terminal-select",
    );
    expect(rejected).toBeDefined();
    expect(rejected!.reason).toMatch(/branched form was shorter/u);

    const capability = result.report.optimizer.capabilities.find(
      (entry) => entry.id === "negative-zero-threshold-selector",
    );
    expect(capability?.status).toBe("considered");
  });

  it("uses a negative-zero threshold selector when it makes terminal if/else shorter", () => {
    const result = compileOk(`
program NegativeZeroThreshold {
  state {
    score: counter 0..999 = 0
  }
  turn {
    if score >= 100 {
      stop 1
    }
    else {
      stop 0
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "negative-zero-threshold-selector")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "negative-zero-threshold-terminal-select")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "branch-removal")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((item) => item.id === "negative-zero-degree")).toBe(true);
    const preload = result.report.preloads.find((item) => item.value === "1|-00");
    expect(preload).toMatchObject({
      register: "d",
      value: "1|-00",
      countsAgainstProgram: false,
    });
    expect(preload?.setupProgram).toContain("4D");
    expect(result.report.steps).toBe(11);
  });

  it("considers negative-zero threshold flow but keeps the shorter ordinary branch", () => {
    const result = compileOk(`
program NegativeZeroFlowCandidate {
  state {
    score: counter 0..999 = 0
  }
  turn {
    if score >= 100 {
      stop 1
    }
    stop 0
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "negative-zero-threshold-flow")).toBe(false);
    const rejected = result.report.rejectedCandidates.find((entry) => entry.variant === "negative-zero-threshold-flow");
    expect(rejected).toBeDefined();
    expect(rejected!.reason).toMatch(/ordinary condition was shorter/u);
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
