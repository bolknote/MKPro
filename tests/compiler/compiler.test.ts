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

  it("drops identity assignments before register allocation", () => {
    const result = compileOk(`
program IdentityAssignment {
  state {
    value: packed = 0
  }
  turn {
    value = value
    stop value
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "identity-assignment-elimination")).toBe(true);
  });

  it("inlines single-use constant-guarded calls before state liveness", () => {
    const result = compileOk(`
program GuardedCall {
  state {
    flag: flag = 0
    score: counter 0..9 = 0
  }
  turn {
    flag = 1
    maybe_score
    stop score
  }
  rule maybe_score {
    if flag == 1 {
      score++
      flag = 0
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "constant-guarded-call-inline")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "dead-state-elimination")).toBe(true);
    expect(result.report.registers.flag).toBeUndefined();
  });

  it("branches directly on a single-use input without storing it", () => {
    const result = compileOk(`
program InputBranch {
  turn {
    read target
    if target >= 0 {
      stop 1
    }
    else {
      stop -1
    }
  }
}
`);

    expect(result.report.registers.target).toBeUndefined();
    expect(result.report.optimizations.some((item) => item.name === "ephemeral-input-branch")).toBe(true);
    expect(result.steps.map((step) => step.hex).slice(0, 3)).toEqual(["50", "59", "05"]);
  });

  it("shares return suffix gadgets across compiled rule procedures", () => {
    const result = compileOk(`
program ReturnSuffixGadget {
  state {
    a: packed = 0
    b: packed = 0
    c: packed = 0
    d: packed = 0
  }

  screen main {
    show b, c, d
  }

  turn {
    alpha
    beta
    alpha
    beta
    show main
  }

  rule alpha {
    if a >= 0 {
      b = 1
    }
    c = 2
    d = 3
  }

  rule beta {
    if a >= 0 {
      b = 4
    }
    c = 2
    d = 3
  }
}
`, { budget: 999, analysis: true, disableInterproceduralOpts: true });

    expect(result.report.optimizations.some((item) => item.name === "return-suffix-gadget")).toBe(true);
    expect(result.steps.some((step) => step.comment === "return suffix gadget")).toBe(true);
  });

  it("extracts repeated guarded prologues into return-to-continuation gadgets", () => {
    const result = compileOk(`
program GuardedPrologueGadget {
  state {
    action: packed = 0
    energy: counter 0..9 = 9
    pos: packed = 0
  }

  rule pay {
    energy--
  }

  rule drained {
    stop -999
  }

  turn {
    read action
    match action {
      1 => left
      2 => right
      3 => up
      otherwise => stop pos
    }
  }

  rule left {
    pay
    if energy > 0 {
      pos += 1
    }
    else {
      drained
    }
    stop pos
  }

  rule right {
    pay
    if energy > 0 {
      pos += 10
    }
    else {
      drained
    }
    stop pos
  }

  rule up {
    pay
    if energy > 0 {
      pos += 100
    }
    else {
      drained
    }
    stop pos
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "guarded-prologue-gadget")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "guarded-prologue-gadget-layout")).toBe(true);
    expect(result.steps.length).toBeLessThan(62);
  });

  it("inlines tiny multi-use rules when that beats a subroutine", () => {
    const result = compileOk(`
program TinyMultiUseRule {
  state {
    score: counter 0..9 = 0
  }
  turn {
    bump
    bump
    stop score
  }
  rule bump {
    score++
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "size-model-rule-inline")).toBe(true);
  });

  it("hoists one-shot turn initializers out of the loop", () => {
    const result = compileOk(`
program OneShotInit {
  state {
    entered: flag = 0
    value: counter 0..9 = 0
  }
  screen main {
    show value
  }
  turn {
    if entered == 0 {
      entered = 1
      value++
      show main
    }
    else {
      read key
      value += key
      show main
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "one-shot-loop-init-hoist")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "dead-state-elimination")).toBe(true);
    expect(result.report.registers.entered).toBeUndefined();
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

  it("reassociates signed update deltas to avoid unary negation before addition", () => {
    const assignSource = `
program SignedUpdate {
  state {
    height: packed = 100
    speed: packed = 3
    accel: packed = 2
  }
  turn {
    height = height - speed - accel / 2
    stop height
  }
}
`;
    const updateSource = assignSource.replace(
      "height = height - speed - accel / 2",
      "height += - speed - accel / 2",
    );

    const assign = compileOk(assignSource);
    const update = compileOk(updateSource);

    expect(update.report.steps).toBe(assign.report.steps);
    expect(update.steps.map((step) => step.hex)).toEqual(assign.steps.map((step) => step.hex));
    expect(update.report.optimizations.some((item) => item.name === "expression-constant-folder")).toBe(true);
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

  it("tests cell membership through a shared bit mask helper before line_count", () => {
    const result = compileOk(`
program FoxProbe {
  field: board(0..9, 0..9)
  state {
    cell: coord(field)
    foxes: cells(field) = random()
    hit_value: packed = -20
    bearing: counter 0..9 = 0
  }
  screen hit {
    show hit_value
  }
  screen report {
    show bearing
  }
  turn {
    read cell
    if cell in foxes {
      show hit
    }
    bearing = line_count(foxes, cell)
    show report
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "bit-mask-condition-helper")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "spatial-hit-condition-helper")).toBe(false);
    expect(result.report.steps).toBeLessThanOrEqual(92);
  });

  it("shares repeated bit_has membership checks when the helper is smaller", () => {
    const result = compileOk(`
program RepeatedMembershipProbe {
  grid: board(1..4, 1..4)
  state {
    cell: coord(grid)
    occupied: cells(grid)
    score: counter 0..9 = 0
  }
  turn {
    read cell
    if cell in occupied {
      score++
    }
    if cell in occupied {
      score++
    }
    stop score
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "spatial-hit-condition-helper")).toBe(true);
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

  it("hoists common dispatch tails before MK-61 code generation", () => {
    const result = compileOk(`
program CommonDispatchTail {
  state {
    selector: counter 0..9 = 0
    value: counter 0..9 = 0
  }
  screen view {
    show value
  }
  turn {
    if selector == 1 {
      value = 1
      show view
    }
    else {
      if selector == 2 {
        value = 2
        show view
      }
      else {
        value = 3
        show view
      }
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "common-branch-tail-hoisting")).toBe(true);
    expect(result.steps.filter((step) => step.hex === "50")).toHaveLength(1);
  });

  it("collapses compact direction dispatch shells with no residual cases", () => {
    const result = compileOk(`
program CompactDirectionOnly {
  state {
    key: counter -9..9 = 2
    dir: packed = 0
  }
  turn {
    match key {
      2, 4, 6, 8 => go direction(key)
      otherwise => stop 0
    }
  }
  rule go delta {
    dir = delta
    stop dir
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "compact-dispatch-simplification")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "direction-cardinal-lowering")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "dispatch-lowering")).toBe(false);
  });

  it("passes single-use rule parameters through X for shared rule entries", () => {
    const result = compileOk(`
program XParamRule {
  state {
    key: counter 0..9 = 1
    pos: packed = 0
  }
  screen view {
    show pos
  }
  turn {
    match key {
      1 => go 1
      2 => go 2
      3 => go 3
      otherwise => stop 0
    }
  }
  rule go delta {
    pos = pos + delta
    show view
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "x-param-proc-call")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "x-param-proc-entry")).toBe(true);
    expect(result.steps.some((step) => step.comment === "set delta")).toBe(false);
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

  it("packs multi-field show values instead of adding them", () => {
    const result = compileOk(`
program DisplayFields {
  state {
    a: counter 0..9 = 2
    b: counter 0..9 = 5
  }
  screen view {
    show a, b
  }
  turn {
    show view
  }
}
`);

    expect(result.steps.some((step) => step.comment === "packed display field shift")).toBe(true);
    expect(result.steps.some((step) => step.comment === "packed display field append")).toBe(true);
    expect(result.steps.some((step) => step.comment === "packed display combine")).toBe(false);
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

  it("collapses repeated encounter challenges into formula-driven effect logic", () => {
    const result = compileOk(`
program SharedChallengeDemo {
  state {
    tile: counter 0..5 = 0
    challenge: packed = 0
    answer: packed = 0
    warning_value: packed = 7
    score: counter 0..99 = 0
    strength: counter 0..99 = 10
    plans: cells(cave) = random()
    pos: coord(cave) = 1
  }
  world cave {
    position pos {
      encoding decimal_player
    }
  }
  screen warning {
    show warning_value
  }
  screen memory {
    show challenge
  }
  turn {
    encounter tile
    stop score + strength + plans
  }
  encounters tile {
    0 {
      show 0
    }
    1 {
      challenge tile as challenge using warning, memory, answer {
        success {
          strength += 3
          plans -= pos
        }
        failure {
          strength -= 1
        }
      }
    }
    2 {
      challenge tile as challenge using warning, memory, answer {
        success {
          strength += 2
          score += 1
          plans -= pos
        }
        failure {
          strength -= 2
        }
      }
    }
    3 {
      challenge tile as challenge using warning, memory, answer {
        success {
          strength += 1
          score += 2
          plans -= pos
        }
        failure {
          strength -= 3
        }
      }
    }
    4 {
      challenge tile as challenge using warning, memory, answer {
        success {
          score += 3
          plans -= pos
        }
        failure {
          strength -= 4
        }
      }
    }
  }
}
`);
    const activeCapabilities = new Set(
      result.report.optimizer.capabilities
        .filter((capability) => capability.status === "active")
        .map((capability) => capability.id),
    );

    expect(result.ast.procs.some((proc) => proc.name.startsWith("encounter_effects_"))).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "shared-challenge-effect-lowering")).toBe(true);
    expect(activeCapabilities.has("shared-challenge-effect-lowering")).toBe(true);
  });

  it("keeps exceptional encounter effects inside a shared challenge helper", () => {
    const result = compileOk(`
program SharedChallengeExceptionDemo {
  state {
    tile: counter 0..4 = 0
    challenge: packed = 0
    answer: packed = 0
    warning_value: packed = 7
    score: counter 0..99 = 0
    strength: counter 0..99 = 10
  }
  screen warning {
    show warning_value
  }
  screen memory {
    show challenge
  }
  turn {
    encounter tile
    stop score + strength
  }
  encounters tile {
    1 {
      challenge tile as challenge using warning, memory, answer {
        success {
          bonus
        }
        failure {
          strength--
        }
      }
    }
    2 {
      challenge tile as challenge using warning, memory, answer {
        success {
          strength += 2
        }
        failure {
          strength -= 2
        }
      }
    }
    3 {
      challenge tile as challenge using warning, memory, answer {
        success {
          strength++
          score++
        }
        failure {
          strength -= 3
        }
      }
    }
    4 {
      challenge tile as challenge using warning, memory, answer {
        success {
          score += 2
        }
        failure {
          strength -= 4
        }
      }
    }
  }
  rule bonus {
    score += 9
  }
}
`);
    const encounter = result.ast.procs.find((proc) => proc.name === "encounter");
    const dispatch = encounter?.body[0];
    const caseValues = dispatch?.kind === "dispatch"
      ? dispatch.cases.map((item) => item.value).filter((value) => value.kind === "number").map((value) => value.raw)
      : [];

    expect(result.ast.procs.some((proc) => proc.name.startsWith("encounter_effects_"))).toBe(true);
    expect(caseValues).not.toContain("1");
    expect(result.report.optimizations.some((item) => item.name === "shared-challenge-effect-lowering")).toBe(true);
  });

  it("removes lowered rule procs that become unreachable", () => {
    const result = compileOk(`
program DeadLoweredRules {
  state {
    tile: counter 0..9 = 0
    energy: counter 0..9 = 9
  }
  turn {
    match tile {
      1 => pool_exit
      2 => ladder_exit
      3 => shaft_exit
      otherwise => clear_exit
    }
    stop energy
  }
  rule clear_exit {
    energy++
  }
  rule pool_exit {
    energy = 0
  }
  rule ladder_exit {
    energy -= 5
  }
  rule shaft_exit {
    energy -= 6
  }
}
`);
    const procNames = new Set(result.ast.procs.map((proc) => proc.name));

    expect(result.report.optimizations.some((item) => item.name === "dead-proc-elimination")).toBe(true);
    expect(procNames.has("pool_exit")).toBe(false);
    expect(procNames.has("ladder_exit")).toBe(false);
    expect(procNames.has("shaft_exit")).toBe(false);
    expect(procNames.has("clear_exit")).toBe(false);
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

  it("reuses comparison residuals for guarded self-updates", () => {
    const result = compileOk(`
program ResidualGuardedUpdate {
  state {
    room: counter 0..6 = 0
    shown: packed = 0
  }

  screen main {
    show room shown
  }

  turn {
    if room < 6 {
      room++
      shown = room
    }
    else {
      show main
    }
    stop room
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "residual-guarded-update")).toBe(true);
    expect(result.steps.some((step) => step.comment === "residual guarded update room")).toBe(true);
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
      other
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
      score = score * 2
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

  it("removes dead stores across rule calls before register allocation", () => {
    const source = `
program DseAcrossCall {
  state {
    shown: packed = 0
    scratch: packed = 0
  }

  screen main {
    show shown
  }

  rule overwrite {
    scratch = 2
    shown = scratch
  }

  turn {
    scratch = 1
    overwrite
    show main
  }
}
`;
    const optimized = compileOk(source, { budget: 999, analysis: true });
    const unoptimized = compileOk(source, { budget: 999, analysis: true, disableInterproceduralOpts: true });

    expect(optimized.report.optimizations.some((item) => item.name === "interprocedural-dead-store")).toBe(true);
    expect(optimized.report.steps).toBeLessThan(unoptimized.report.steps);
  });

  it("reuses one failed membership mask for adjacent set updates", () => {
    const result = compileOk(`
program MembershipMaskRun {
  grid: board(1..4, 1..4)

  state {
    cell: coord(grid)
    player_marks: cells(grid)
    occupied: cells(grid)
  }

  screen board {
    show cell, player_marks, occupied
  }

  turn {
    read cell
    if cell in occupied {
      show board
    }
    else {
      player_marks += cell
      occupied += cell
      show board
    }
  }
}
`, { budget: 999, analysis: true });

    expect(result.report.optimizations.some((item) => item.name === "cell-membership-mask-run-reuse")).toBe(true);
  });

  it("reuses a precomputed mask membership result when clearing the same mask", () => {
    const result = compileOk(`
program MaskMembershipClear {
  state {
    pos: packed = 1.0000008
    marks: packed = 0
  }

  turn {
    if bit_and(marks, frac(pos)) != 0 {
      marks = bit_and(marks, bit_not(frac(pos)))
    }
    stop marks
  }
}
`, { budget: 999, analysis: true });

    expect(result.report.optimizations.some((item) => item.name === "cell-membership-clear-reuse")).toBe(true);
  });

  it("preserves false-branch X through a unit decrement before reuse", () => {
    const result = compileOk(`
program FalseBranchXReuse {
  state {
    target: packed = 0
    wumpus: packed = 1
    arrows: counter 0..5 = 2
  }

  turn {
    read target
    if target >= 0 {
      stop 1
    }
    else {
      shoot
    }
  }

  rule shoot {
    arrows--

    if target + wumpus == 0 {
      stop 2
    }
    else {
      stop arrows
    }
  }
}
`, { budget: 999, analysis: true });

    const optimizationNames = result.report.optimizations.map((item) => item.name);
    expect(optimizationNames).toContain("x-preserving-false-branch");
    expect(optimizationNames).toContain("fl-unit-decrement");
    expect(optimizationNames).toContain("stack-current-x-scheduling");
  });

  it("shares identical nested guard failure branches", () => {
    const result = compileOk(`
program NestedGuardFailure {
  state {
    a: counter 0..9 = 1
    b: counter 0..9 = 1
    score: counter 0..9 = 0
  }

  turn {
    if a != 0 {
      if b != 0 {
        score = 1
      }
      else {
        show 0
      }
    }
    else {
      show 0
    }
    stop score
  }
}
`, { budget: 999, analysis: true });

    expect(result.report.optimizations.some((item) => item.name === "nested-guard-shared-failure")).toBe(true);
  });

  it("elides terminal shows already provided by the next loop header", () => {
    const result = compileOk(`
program TerminalLoopScreen {
  state {
    pos: counter 0..9 = 1
    score: counter 0..9 = 0
  }
  screen main {
    show pos
  }
  turn {
    show main
    read key
    match key {
      1 => score_point
      otherwise => stop 0
    }
  }
  rule score_point {
    score = 1
    show main
  }
}
`, { budget: 999, analysis: true });

    expect(result.report.optimizations.some((item) => item.name === "terminal-loop-screen-elision")).toBe(true);
  });
});
