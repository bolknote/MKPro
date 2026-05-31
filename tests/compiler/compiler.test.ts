import { describe, expect, it } from "vitest";
import { CompileError, compileMKPro } from "../../src/core/index.ts";
import { compileLoweringVariantForTest } from "../../src/core/compiler.ts";
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
  loop {
    halt(0)
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
  loop {
    x = read()
    value = 0 - x
    halt(value)
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
  loop {
    value = 5 + 2
    halt(value)
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
  loop {
    value = value
    halt(value)
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
  loop {
    flag = 1
    maybe_score()
    halt(score)
  }
  fn maybe_score() {
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

  it("fuses tail copy assignments before state liveness", () => {
    const result = compileOk(`
program TailCopy {
  state {
    pos: counter 0..99 = 1
    next: counter 0..99 = 0
    dir: counter -9..9 = 1
  }
  loop {
    try_move()
  }
  fn try_move() {
    next = pos + dir
    enter_next()
  }
  fn enter_next() {
    pos = next
    halt(pos)
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "tail-copy-assignment-fusion")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "dead-state-elimination")).toBe(true);
    expect(result.report.registers.next).toBeUndefined();
    expect(result.ast.procs.some((proc) => proc.name === "enter_next")).toBe(false);
  });

  it("folds signed match pairs through abs/sign only when the lowering variant is enabled", () => {
    const source = `
program SignedMatchPair {
  state {
    key: counter -10..10 = 0
    result: packed = 0
  }
  loop {
    key = read()
    match key {
      4 => go_left()
      2 => go(0.0000002)
      6 => go(-0.000001)
      8 => go(-0.0000002)
      5 => go(1)
      -5 => go(-1)
      0 => break_wall()
      10 => search()
      otherwise => ignored()
    }
  }
  fn go_left() {
    result = 4
    halt(result)
  }
  fn go(step) {
    result = step
    halt(result)
  }
  fn break_wall() {
    halt(0)
  }
  fn search() {
    halt(10)
  }
  fn ignored() {
    halt(-10)
  }
}
`;

    const base = compileLoweringVariantForTest(source, { budget: 999 }, {});
    const folded = compileLoweringVariantForTest(source, { budget: 999 }, { signedAbsMatchPairs: true });
    const lowered = JSON.stringify(folded.ast.entries[0]?.body);

    expect(lowered).toContain('"callee":"abs"');
    expect(lowered).toContain('"callee":"sign"');
    expect(folded.steps.length).toBeLessThan(base.steps.length);
  });

  it("branches directly on a single-use input without storing it", () => {
    const result = compileOk(`
program InputBranch {
  loop {
    target = read()
    if target >= 0 {
      halt(1)
    }
    else {
      halt(-1)
    }
  }
}
`);

    expect(result.report.registers.target).toBeUndefined();
    expect(result.report.optimizations.some((item) => item.name === "ephemeral-input-branch")).toBe(true);
    expect(result.steps.map((step) => step.hex).slice(0, 3)).toEqual(["50", "59", "05"]);
  });

  it("dispatches directly on a single-use input without storing it", () => {
    const result = compileOk(`
program InputDispatch {
  loop {
    target = read()
    match target {
      1 => halt(1)
      2 => halt(2)
      otherwise => halt(0)
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "ephemeral-input-dispatch")).toBe(true);
    expect(result.steps.map((step) => step.hex)[0]).toBe("50");
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



  loop {
    alpha()
    beta()
    alpha()
    beta()
    show(b, c, d)
  }

  fn alpha() {
    if a >= 0 {
      b = 1
    }
    c = 2
    d = 3
  }

  fn beta() {
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

  fn pay() {
    energy--
  }

  fn drained() {
    halt(-999)
  }

  loop {
    action = read()
    match action {
      1 => left()
      2 => right()
      3 => up()
      otherwise => halt(pos)
    }
  }

  fn left() {
    pay()
    if energy > 0 {
      pos += 1
    }
    else {
      drained()
    }
    halt(pos)
  }

  fn right() {
    pay()
    if energy > 0 {
      pos += 10
    }
    else {
      drained()
    }
    halt(pos)
  }

  fn up() {
    pay()
    if energy > 0 {
      pos += 100
    }
    else {
      drained()
    }
    halt(pos)
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
  loop {
    bump()
    bump()
    halt(score)
  }
  fn bump() {
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

  loop {
    if entered == 0 {
      entered = 1
      value++
      show(value)
    }
    else {
      key = read()
      value += key
      show(value)
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
  loop {
    value = -5 + 7
    halt(value)
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
  loop {
    x = read()
    value = 2 * (2 + x)
    halt(value)
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
  loop {
    x = read()
    value = -5 * (2 - x)
    halt(value)
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
  loop {
    x = read()
    y = read()
    value = 2 * (x + y + 3) - (x - 4 * y)
    halt(value)
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
  loop {
    height = height - speed - accel / 2
    halt(height)
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
  loop {
    x = read()
    value = 5 * (2 + 3 * (2 + x))
    halt(value)
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
  loop {
    value = bit_or(2, 4)
    halt(value)
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
  loop {
    value = bit_or(2, 4) + bit_and(7, 3) + bit_xor(6, 3) + max(2, 9) + abs(-3) + sign(-12) + int(2.9) + frac(2.75) + inv(2) + pow(2, 3) + pow10(2)
    halt(value)
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
  loop {
    value = bit_not(99999999)
    halt(value)
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
  loop {
    value = max(0, 5)
    halt(value)
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
  loop {
    x = read()
    y = read()
    value = max(pi(), sqr(x)) + inv(y) + pow(x, y) + bit_and(x, y) + bit_or(x, y) + bit_xor(x, y) + bit_not(x)
    halt(value)
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    for (const opcode of ["20", "22", "23", "24", "36", "37", "38", "39", "3A"]) {
      expect(opcodes).toContain(opcode);
    }
  });

  it("lowers random range integer draws without cycling the MK-61 generator through К [x]", () => {
    const result = compileOk(`
program RandomRangeSugar {
  state {
    roll: counter 0..9 = 0
    span: packed = 10
  }
  loop {
    roll = int(random(1, span))
    halt(roll)
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(opcodes).toContain("3B");
    expect(opcodes).toContain("0E");
    expect(opcodes).toContain("35");
    expect(opcodes).not.toContain("34");
    expect(result.report.optimizations.some((item) => item.name === "int-random-range-lowering")).toBe(true);
  });

  it("lowers scaled random integer draws through x-frac(x)", () => {
    const result = compileOk(`
program RandomScaledInteger {
  state {
    roll: counter 1..9 = 1
  }
  loop {
    roll = int(random() * 9) + 1
    halt(roll)
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(opcodes.slice(0, 8)).toEqual(["3B", "09", "12", "0E", "35", "11", "01", "10"]);
    expect(opcodes).not.toContain("34");
    expect(result.report.optimizations.some((item) => item.name === "int-random-range-lowering")).toBe(true);
  });

  it("accepts fractional random ranges with ROM-friendly integer flooring", () => {
    const result = compileOk(`
program FractionalRandomRangeSugar {
  state {
    value: packed = 0
  }
  loop {
    value = int(random(0.5, 2.5))
    halt(value)
  }
}
`);

    expect(result.steps.some((step) => step.hex === "3B")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "random-range-lowering")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "int-random-range-lowering")).toBe(true);
    expect(result.steps.map((step) => step.hex)).not.toContain("34");
  });

  it("lowers one-argument random calls as zero-based ranges", () => {
    const result = compileOk(`
program RandomMax {
  loop {
    halt(int(random(9)))
  }
}
`);

    expect(result.steps.some((step) => step.hex === "3B")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "random-range-lowering")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "int-random-range-lowering")).toBe(true);
    expect(result.steps.map((step) => step.hex)).not.toContain("34");
  });

  it("lowers int(random()) without using the MK-61 integer opcode", () => {
    const result = compileOk(`
program RandomUnitInteger {
  loop {
    halt(int(random()))
  }
}
`);

    expect(result.steps.some((step) => step.hex === "3B")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "int-random-range-lowering")).toBe(true);
    expect(result.steps.map((step) => step.hex)).not.toContain("34");
  });

  it("lowers random(domain) through a random coordinate draw", () => {
    const result = compileOk(`
program RandomDomain {
  cave: board(1..20, 1..1)
  state {
    room: coord(cave) = 1
  }
  loop {
    room = random(cave)
    halt(room)
  }
}
`);

    expect(result.steps.some((step) => step.hex === "3B")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "random-range-lowering")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "int-random-range-lowering")).toBe(true);
  });

  it("rejects random calls with too many arguments", () => {
    expect(() =>
      compileOk(`
program RandomRangeArity {
  loop {
    halt(random(1, 2, 3))
  }
}
`),
    ).toThrow(/random\(\) expects zero, one, or two arguments, got 3/u);
  });

  it("lowers coord_list random() through independent random setup draws", () => {
    const result = compileOk(`
program CoordListRandom {
  field: board(0..9, 0..9)
  state {
    cell: coord(field) = 11
    spots: coord_list(field, 3) = random()
    bearing: counter 0..9 = 0
  }
  loop {
    bearing = line_count(spots, cell)
    halt(bearing)
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "setup-coord-list-indirect-random-unique")).toBe(false);
    expect(result.report.setupProgram?.steps.filter((step) => step.hex === "3B").length ?? 0).toBeGreaterThanOrEqual(3);
  });

  it("lowers coord_list random_unique() through compact unique setup", () => {
    const result = compileOk(`
program CoordListRandomUnique {
  field: board(0..9, 0..9)
  state {
    cell: coord(field) = 11
    foxes: coord_list(field, 3) = random_unique()
    bearing: counter 0..9 = 0
  }
  loop {
    bearing = line_count(foxes, cell)
    halt(bearing)
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name.includes("coord-list-indirect-random-unique"))).toBe(true);
    expect(result.report.setupProgram?.steps.some((step) => step.comment?.includes("random coord collision"))).toBe(true);
  });

  it("lowers coord_list random(min, max) through independent range setup draws", () => {
    const result = compileOk(`
program CoordListRandomRange {
  field: board(0..9, 0..9)
  state {
    cell: coord(field) = 11
    spots: coord_list(field, 3) = int(random(0, 100))
    bearing: counter 0..9 = 0
  }
  loop {
    bearing = line_count(spots, cell)
    halt(bearing)
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name.includes("coord-list-indirect-random-unique"))).toBe(false);
    expect(result.report.optimizations.some((item) => item.name === "setup-random-range-lowering")).toBe(true);
    expect(result.report.setupProgram?.steps.filter((step) => step.hex === "3B").length ?? 0).toBeGreaterThanOrEqual(3);
  });

  it("lowers mask, cell, and packed digit helpers from V2 formulas", () => {
    const result = compileOk(`
program FormulaHelpers {
  state {
    mask: packed = 0
    value: packed = 0
  }
  loop {
    value = bit_mask(5) + bit_has(mask, 5)
    mask = bit_set(mask, 5)
    value = value + bit_clear(mask, 5) + bit_toggle(mask, 5)
    value = value + cell_mask(1, 2) + cell_has(mask, 1, 2)
    mask = cell_set(mask, 1, 2)
    value = value + cell_clear(mask, 1, 2) + cell_toggle(mask, 1, 2)
    value = value + digit_at(1234, 2) + digit_add(1000, 1, 7) + digit_set(1234, 2, 9)
    halt(value)
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
  cave: board(row_scan)
  loop {
    if pos in marks {
      marks -= pos
      halt(1)
    }
    else {
      halt(0)
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


  loop {
    cell = read()
    if cell in foxes {
      show(hit_value)
    }
    bearing = line_count(foxes, cell)
    show(bearing)
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "bit-mask-condition-helper")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "spatial-hit-condition-helper")).toBe(false);
    expect(result.report.steps).toBeLessThanOrEqual(99);
  });

  it("shares repeated membership checks through the selected helper", () => {
    const result = compileOk(`
program RepeatedMembershipProbe {
  grid: board(1..4, 1..4)
  state {
    cell: coord(grid)
    occupied: cells(grid)
    score: counter 0..9 = 0
  }
  loop {
    cell = read()
    if cell in occupied {
      score++
    }
    if cell in occupied {
      score++
    }
    halt(score)
  }
}
`);

    const helperUses = result.report.optimizations.filter((item) => item.name === "spatial-hit-condition-helper");
    expect(helperUses).toHaveLength(2);
    expect(result.report.optimizations.some((item) => item.name === "bit-mask-condition-helper")).toBe(false);
  });

  it("hoists common branch tails before MK-61 code generation", () => {
    const result = compileOk(`
program CommonBranchTail {
  state {
    selector: counter 0..9 = 0
    value: counter 0..9 = 0
  }

  loop {
    if selector == 0 {
      value = 1
      show(value)
    }
    else {
      value = 2
      show(value)
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

  loop {
    if selector == 1 {
      value = 1
      show(value)
    }
    else {
      if selector == 2 {
        value = 2
        show(value)
      }
      else {
        value = 3
        show(value)
      }
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "common-branch-tail-hoisting")).toBe(true);
    expect(result.steps.filter((step) => step.hex === "50")).toHaveLength(1);
  });

  it("keeps residual dispatch deltas positive when subtraction is shorter", () => {
    const result = compileOk(`
program PositiveResidualDispatch {
  state {
    key: counter 0..10 = stack.X
  }

  loop {
    match key {
      0 => halt(0)
      2 => halt(2)
      10 => halt(10)
      otherwise => halt(9)
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "numeric-dispatch-residual-chain")).toBe(true);
    expect(result.steps.filter((step) => step.comment === "dispatch residual compare").map((step) => step.hex))
      .toEqual(["11", "11"]);
    expect(result.steps.some((step) => step.comment === "negative number")).toBe(false);
  });

  it("reuses the zero residual inside a matching dispatch case", () => {
    const result = compileOk(`
program DispatchKnownZeroCase {
  state {
    key: counter 0..9 = stack.X
  }

  loop {
    match key {
      0 => halt(0)
      2 => halt(2)
      otherwise => halt(9)
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "numeric-dispatch-residual-chain")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "known-zero-reuse")).toBe(true);
    expect(result.steps.slice(0, 4).map((step) => step.hex)).toEqual(["60", "5E", "04", "50"]);
  });

  it("keeps scratch-free residual dispatch valid after default-case merge leaves one case", () => {
    const result = compileLoweringVariantForTest(`
program SingleCaseResidualFallback {
  state {
    a: counter 0..9 = 0
    b: counter 0..9 = 0
    value: counter 0..9 = 0
  }

  loop {
    if a + b == 1 {
      value = 0
    }
    else {
      if a + b == 2 {
        value = 1
      }
      else {
        value = 0
      }
    }
    halt(value)
  }
}
`, { budget: 999, analysis: true }, {
      canonicalizeIfChains: true,
      freeResidualDispatchScratch: true,
    });

    expect(result.report.optimizations.some((item) => item.name === "dispatch-default-merge")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "numeric-dispatch-residual-chain")).toBe(true);
    expect(result.diagnostics.filter((diagnostic) => diagnostic.level === "error")).toHaveLength(0);
  });

  it("collapses compact direction dispatch shells with no residual cases", () => {
    const result = compileOk(`
program CompactDirectionOnly {
  state {
    key: counter -9..9 = 2
    dir: packed = 0
  }
  loop {
    match key {
      2, 4, 6, 8 => go(direction(key))
      otherwise => halt(0)
    }
  }
  fn go(delta) {
    dir = delta
    halt(dir)
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

  loop {
    match key {
      1 => go(1)
      2 => go(2)
      3 => go(3)
      otherwise => halt(0)
    }
  }
  fn go(delta) {
    pos = pos + delta
    show(pos)
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

  loop {
    match selector {
      1 => show(a, b, c)
      2 => show(a, b, c)
      3 => show(a, b, c)
      otherwise => halt(0)
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

  loop {
    show(a, b)
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
  loop {
    a = digit_at(map, pos - int(pos / 10) * 10)
    b = digit_at(map, pos - int(pos / 10) * 10)
    c = digit_at(map, pos - int(pos / 10) * 10)
    halt(a + b + c)
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
  loop {
    ones = value - 10 * int(value / 10)
    halt(ones)
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "remainder-fraction-lowering")).toBe(true);
  });

  it("tests integer remainders against zero without rescaling the fraction", () => {
    const result = compileOk(`
program RemainderZeroTest {
  state {
    value: packed = 25
  }
  loop {
    if value - 5 * int(value / 5) == 0 {
      halt(1)
    }
    halt(0)
  }
}
`);
    const comments = result.steps.map((step) => step.comment);

    expect(result.report.optimizations.some((item) => item.name === "remainder-zero-test-lowering")).toBe(true);
    expect(comments).toContain("remainder zero fractional part");
    expect(comments).not.toContain("remainder scale");
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
  loop {
    mine += cell
    seen += cell
    halt(mine + seen)
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


  loop {
    show(warning_value)
    show(memory_value)
    answer = read()
    show(warning_value)
    show(memory_value)
    answer = read()
    show(warning_value)
    show(memory_value)
    answer = read()
    show(warning_value)
    show(memory_value)
    answer = read()
    show(warning_value)
    show(memory_value)
    answer = read()
    show(warning_value)
    show(memory_value)
    answer = read()
    halt(answer)
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "show-sequence-helper")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "show-sequence-helper-call")).toBe(true);
  });

  it("removes lowered rule procs that become unreachable", () => {
    const result = compileOk(`
program DeadLoweredRules {
  state {
    tile: counter 0..9 = 0
    energy: counter 0..9 = 9
  }
  loop {
    match tile {
      1 => pool_exit()
      2 => ladder_exit()
      3 => shaft_exit()
      otherwise => clear_exit()
    }
    halt(energy)
  }
  fn clear_exit() {
    energy++
  }
  fn pool_exit() {
    energy = 0
  }
  fn ladder_exit() {
    energy -= 5
  }
  fn shaft_exit() {
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
  loop {
    hack()
    halt(result)
  }
  fn hack() {
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
  loop {
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
    halt(out)
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
  loop {
    raw {
      clobbers X
      preserves state
      code {
        БП 13
      }
    }
    halt(0)
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
  loop {
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
    halt(out)
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
  loop {
    raw {
      clobbers X
      preserves state
      code {
        БП C5
      }
    }
    halt(0)
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
  loop {
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
  loop {
    halt(__mkpro_score)
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
  loop {
    accel = burn * 10 / fuel - 9.8
    halt(accel)
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
  loop {
    if flag == 1 {
      result = 50
    }
    else {
      result = 10
    }
    halt(result)
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



  loop {
    if room < 6 {
      room++
      shown = room
    }
    else {
      show(room, shown)
    }
    halt(room)
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "residual-guarded-update")).toBe(true);
    expect(result.steps.some((step) => step.comment === "residual guarded update room")).toBe(true);
  });

  it("moves independent guarded self-updates forward to reuse comparison residuals", () => {
    const result = compileOk(`
program DelayedResidualGuardedUpdate {
  state {
    dynamite: counter 0..9 = 4
    blocked: packed = 7
    player: packed = 0
  }
  loop {
    if dynamite >= 2 {
      player = blocked
      dynamite -= 2
      show(player)
    }
    else {
      halt(0)
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "residual-guarded-update")).toBe(true);
    expect(result.steps.some((step) => step.comment === "residual guarded update dynamite")).toBe(false);
    expect(result.steps.some((step) => step.comment === "set dynamite")).toBe(true);
  });

  it("reuses delayed guarded update residuals inside nested shared-failure guards", () => {
    const result = compileOk(`
program NestedDelayedResidualGuardedUpdate {
  state {
    dynamite: counter 0..9 = 4
    blocked: packed = 7
    player: packed = 0
  }
  loop {
    if blocked != 0 {
      if dynamite >= 2 {
        player = blocked
        dynamite -= 2
        show(player)
      }
      else {
        halt(0)
      }
    }
    else {
      halt(0)
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "nested-guard-shared-failure")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "residual-guarded-update")).toBe(true);
    expect(result.steps.filter((step) => step.comment === "set dynamite")).toHaveLength(1);
  });

  it("uses counter ranges for saturating unit updates", () => {
    const result = compileOk(`
program ResourceRange {
  state {
    fuel: counter 0..9 = 4
  }
  loop {
    if fuel > 0 {
      fuel--
    }
    halt(fuel)
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
  loop {
    if fuel <= -1 {
      fuel--
      show(0)
    }
    halt(fuel)
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "comparison-boundary-normalization")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "zero-condition-test")).toBe(true);
  });

  it("normalizes subtraction-against-zero comparisons when the negated difference enables a zero test", () => {
    const result = compileOk(`
program DifferenceZeroNormalize {
  state {
    left: packed = 3
    right: packed = 5
    result: packed = 0
  }
  loop {
    if left - right <= 0 {
      result = 1
    }
    else {
      result = 2
    }
    halt(result)
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "comparison-boundary-normalization")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "zero-condition-test")).toBe(true);
    expect(result.report.optimizations.some((item) => item.detail.includes("right - left >= 0"))).toBe(true);
  });

  it("replaces boolean-selected V2 stops with terminal selection", () => {
    const result = compileOk(`
program BranchlessStop {
  state {
    flag: flag = 0
  }
  loop {
    if flag == 1 {
      halt(50)
    }
    else {
      halt(10)
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

  loop {
    if flag == 1 {
      show(crash_value)
      halt(-999)
    }
    else {
      halt(1)
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

  loop {
    if score >= 5 {
      fail()
    }
    else {
      other()
    }
  }
  fn fail() {
    show(crash_value)
    halt(-999)
  }
  fn other() {
    if score < 2 {
      fail()
    }
    else {
      halt(2)
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

  loop {
    if score < 5 {
      score = score * 2
    }
    else {
      show(fail_value)
      halt(-999)
    }
    halt(score)
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
  loop {
    if flag == 1 {
      a++
      b--
    }
    halt(a + b)
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
  loop {
    if score >= 100 {
      bonus++
    }
    halt(bonus)
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

  loop {
    if strength <= 0 {
      exhausted()
    }
    value++
    if value >= 9 {
      exhausted()
    }
    halt(value)
  }
  fn exhausted() {
    show(fail_value)
    halt(-999)
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
  loop {
    if counter > 0 {
      halt(1)
    }
    else {
      halt(0)
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
  loop {
    if abs(speed) <= 5 {
      halt(777)
    }
    else {
      halt(666)
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
  loop {
    if score >= 100 {
      halt(1)
    }
    else {
      halt(0)
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
  loop {
    if score >= 100 {
      halt(1)
    }
    halt(0)
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
  loop {
    halt(1)
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
  loop {
    value = 1
    halt(value)
  }
}
`, { budget: 1, analysis: true });

    expect(result.report.budgetReport.exceeded).toBe(true);
    expect(result.diagnostics.some((diagnostic) => diagnostic.code === "BUDGET_EXCEEDED" && diagnostic.level === "warning")).toBe(true);
    expect(result.steps.length).toBeGreaterThan(1);
  });

  it("keeps analysis output inspectable past the byte address range", () => {
    const pauses = Array.from({ length: 130 }, () => "    show(0)").join("\n");
    const result = compileMKPro(`
program AnalyzeHuge {
  loop {
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
  loop {
    halt(1)
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



  fn overwrite() {
    scratch = 2
    shown = scratch
  }

  loop {
    scratch = 1
    overwrite()
    show(shown)
  }
}
`;
    const optimized = compileOk(source, { budget: 999, analysis: true });
    const unoptimized = compileOk(source, { budget: 999, analysis: true, disableInterproceduralOpts: true });

    expect(optimized.report.optimizations.some((item) => item.name === "interprocedural-dead-store")).toBe(true);
    expect(optimized.report.steps).toBeLessThan(unoptimized.report.steps);
  });

  it("keeps stores read only by while control flow", () => {
    const result = compileOk(`
program WhileControlStore {
  state {
    money: counter -9..9 = 1
    ticks: counter 0..3 = 0
    slot: counter 1..4 = 1
    dead: packed = 0
  }

  fn fail() {
    show("1ЕС")
  }

  loop {
    money -= 1
    if money < 0 {
      fail()
    }
    else {
      ticks = 3
      slot = 1
      dead = 0
      while ticks >= 1 {
        slot++
        ticks--
      }
      halt(slot)
    }
  }
}
`, { budget: 999, analysis: true });

    expect(result.steps.some((step) => step.comment === "set ticks")).toBe(true);
  });

  it("lowers initialized decrementing while loops through FL counters", () => {
    const result = compileOk(`
program CountedWhile {
  state {
    ticks: counter 0..3 = 0
    total: counter 0..9 = 0
  }

  loop {
    ticks = 3
    total = 0
    while ticks >= 1 {
      total += 1
      ticks -= 1
    }
    halt(total)
  }
}
`, { budget: 999, analysis: true });

    expect(result.report.optimizations.some((item) => item.name === "initialized-counted-while-loop")).toBe(true);
    expect(result.steps.some((step) => step.comment === "counted while ticks")).toBe(true);
  });

  it("fuses resource decrement and underflow terminal branch", () => {
    const result = compileOk(`
program ResourceUnderflow {
  state {
    food: counter 0..9 = 2
  }

  loop {
    food--
    if food < 0 {
      halt("ЕГГ0Г")
    }
    halt(food)
  }
}
`, { budget: 999, analysis: true });

    expect(result.report.optimizations.some((item) => item.name === "decrement-underflow-branch")).toBe(true);
    expect(result.steps.some((step) => step.comment === "decrement underflow food")).toBe(true);
  });

  it("keeps a read key on stack while checking resource underflow before dispatch", () => {
    const result = compileOk(`
program ReadKeyResourceUnderflow {
  state {
    food: counter 0..9 = 2
    pos: counter 0..9 = 1
  }

  loop {
    show(pos)
    key = read()
    food--
    if food < 0 {
      loop {
      }
    }
    match key {
      1 => halt(1)
      otherwise => halt(0)
    }
  }
}
`, { budget: 999, analysis: true });

    expect(result.report.optimizations.some((item) => item.name === "show-read-decrement-underflow-fusion")).toBe(true);
    expect(result.steps.some((step) => step.comment === "read key")).toBe(false);
    expect(result.steps.some((step) => step.comment === "restore read key")).toBe(true);
  });

  it("keeps stores read by a value-returning function called from an assignment expression", () => {
    const result = compileOk(`
program DseFunctionExpressionCall {
  state {
    source: packed = 7
    current: packed = 0
    shown: packed = 0
  }

  fn use_current() {
    shown = current
    return shown
  }

  loop {
    current = source + 1
    source = use_current()
    show(source)
  }
}
`, { budget: 999, analysis: true });

    expect(result.steps.some((step) => step.comment === "set current")).toBe(true);
  });

  it("lowers constant indexed state access to the selected scalar register", () => {
    const result = compileOk(`
program IndexedConstantState {
  state {
    slots: packed[1..3] = 0
    x: packed = 0
  }

  loop {
    x = read()
    slots[2] = x
    x = 0
    halt(slots[2])
  }
}
`, { budget: 999, analysis: true, disableInterproceduralOpts: true });

    expect(result.report.optimizations.some((item) => item.name === "constant-indexed-state-resolution")).toBe(true);
    expect(result.steps.some((step) => step.comment === "set slots_2")).toBe(true);
    expect(result.steps.some((step) => step.comment === "recall slots_2")).toBe(true);
    expect(result.steps.some((step) => step.mnemonic?.startsWith("К X->П"))).toBe(false);
  });

  it("exposes constant indexed state to small-set helper patterns", () => {
    const result = compileOk(`
program IndexedSmallSet {
  cave: board(1..20, 1..1)

  state {
    room: coord(cave) = 1
    slots: coord[1..2](cave) = random(cave)
    clue: counter 0..9 = 0
  }

  loop {
    if near_any(room, 1, slots[1], slots[2]) >= 0 {
      clue = 1
    }
    if near_any(room, 1, slots[1], slots[2]) >= 0 {
      clue = 2
    }
    if near_any(room, 1, slots[1], slots[2]) >= 0 {
      halt(clue)
    }
  }
}
`, { budget: 999, analysis: true });

    expect(result.report.optimizations.some((item) => item.name === "constant-indexed-state-resolution")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "near-any-helper-lowering")).toBe(true);
  });

  it("lowers dynamic indexed group state through MK-61 indirect memory commands", () => {
    const result = compileOk(`
program IndexedGroupState {
  state {
    line: group(1..3) {
      front: packed = 10
      robots: packed = 0
    }
    i: counter 1..3 = 2
    order: packed = 3
  }

  loop {
    line[i].robots += order
    line[i].front -= 1
    show(line[i].robots, line[i].front)
  }
}
`, { budget: 999, analysis: true });

    expect(result.steps.some((step) => step.comment === "indexed set line.robots")).toBe(true);
    expect(result.steps.some((step) => step.comment === "indexed set line.front")).toBe(true);
    expect(result.steps.some((step) => step.mnemonic?.startsWith("К X->П"))).toBe(true);
    expect(result.steps.some((step) => step.mnemonic?.startsWith("К П->X"))).toBe(true);
  });

  it("specializes constant indexed rule calls before lowering", () => {
    const result = compileOk(`
program ConstantIndexedRuleSpecialization {
  state {
    line: group(1..2) {
      front: packed = 10
    }
  }

  loop {
    touch(1)
    touch(2)
    halt(line[1].front + line[2].front)
  }

  fn touch(slot) {
    show(slot)
    line[slot].front += 1
  }
}
`, { budget: 999, analysis: true, disableInterproceduralOpts: true });

    expect(result.ast.procs.some((proc) => proc.name === "touch")).toBe(false);
    expect(result.report.optimizations.some((item) => item.name === "constant-indexed-state-resolution")).toBe(true);
    expect(result.steps.some((step) => step.comment === "set line_front_1")).toBe(true);
    expect(result.steps.some((step) => step.comment === "set line_front_2")).toBe(true);
    expect(result.steps.some((step) => step.mnemonic?.startsWith("К X->П"))).toBe(false);
  });

  it("fuses indexed stores followed by pointer increments through preincrement indirect stores", () => {
    const result = compileOk(`
program IndexedPreincrementStore {
  state {
    slots: packed[2..4] = 0
    pointer: counter 1..4 = 1
    value: packed = 7
  }

  loop {
    slots[pointer + 1] = value
    pointer++
    halt(slots[2])
  }
}
`, { budget: 999, analysis: true, disableInterproceduralOpts: true });

    expect(result.report.optimizations.some((item) => item.name === "preincrement-indexed-store")).toBe(true);
    expect(result.steps.some((step) => step.comment === "preincrement indexed set slots")).toBe(true);
    expect(result.steps.some((step) => step.comment === "increment pointer")).toBe(false);
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



  loop {
    cell = read()
    if cell in occupied {
      show(cell, player_marks, occupied)
    }
    else {
      player_marks += cell
      occupied += cell
      show(cell, player_marks, occupied)
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

  loop {
    if bit_and(marks, frac(pos)) != 0 {
      marks = bit_and(marks, bit_not(frac(pos)))
    }
    halt(marks)
  }
}
`, { budget: 999, analysis: true });

    expect(result.report.optimizations.some((item) => item.name === "cell-membership-clear-reuse")).toBe(true);
  });

  it("orders commutative calls to reuse derivations of the current X", () => {
    const result = compileOk(`
program CurrentXCommutativeCall {
  state {
    pos: packed = 1.0000001
    walls: packed = 8.1
    blocked: packed = 0
  }

  loop {
    blocked = pos + 0.0000001
    if bit_and(walls, frac(blocked)) != 0 {
      halt(1)
    }
    else {
      halt(0)
    }
  }
}
`, { budget: 999, analysis: true });

    const comments = result.steps.map((step) => step.comment ?? "");
    const setBlocked = comments.indexOf("set blocked");
    const bitAnd = comments.indexOf("bit_and()");
    expect(comments).toContain("current-X frac");
    expect(comments.slice(setBlocked + 1, bitAnd)).not.toContain("recall blocked");
    expect(result.report.optimizations.some((item) =>
      item.name === "stack-current-x-scheduling" &&
      item.detail.includes("frac(blocked)") &&
      item.detail.includes("bit_and")
    )).toBe(true);
  });

  it("preserves false-branch X through a unit decrement before reuse", () => {
    const result = compileOk(`
program FalseBranchXReuse {
  state {
    target: packed = 0
    wumpus: packed = 1
    arrows: counter 0..5 = 2
  }

  loop {
    target = read()
    if target >= 0 {
      halt(1)
    }
    else {
      shoot()
    }
  }

  fn shoot() {
    arrows--

    if target + wumpus == 0 {
      halt(2)
    }
    else {
      halt(arrows)
    }
  }
}
`, { budget: 999, analysis: true });

    const optimizationNames = result.report.optimizations.map((item) => item.name);
    expect(optimizationNames).toContain("x-preserving-false-branch");
    expect(optimizationNames).toContain("fl-unit-decrement");
    expect(optimizationNames).toContain("stack-current-x-scheduling");
  });

  it("lowers bounded R4..R6 unit increments through indirect pre-increment", () => {
    const result = compileOk(`
program IndirectUnitIncrement {
  state {
    a: packed = 0
    b: packed = 0
    c: packed = 0
    d: packed = 0
    score: counter 0..9 = 0
  }

  loop {
    score = score + 1
    halt(a + b + c + d + score)
  }
}
`, { budget: 999, analysis: true });

    expect(result.report.registers.score).toBe("4");
    expect(result.steps.map((step) => step.hex)).toContain("D4");
    expect(result.report.optimizations.some((item) => item.name === "indirect-incdec-counter")).toBe(true);
  });

  it("shares identical nested guard failure branches", () => {
    const result = compileOk(`
program NestedGuardFailure {
  state {
    a: counter 0..9 = 1
    b: counter 0..9 = 1
    score: counter 0..9 = 0
  }

  loop {
    if a != 0 {
      if b != 0 {
        score = 1
      }
      else {
        show(0)
      }
    }
    else {
      show(0)
    }
    halt(score)
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

  loop {
    show(pos)
    key = read()
    match key {
      1 => score_point()
      otherwise => halt(0)
    }
  }
  fn score_point() {
    score = 1
    show(pos)
  }
}
`, { budget: 999, analysis: true });

    expect(result.report.optimizations.some((item) => item.name === "terminal-loop-screen-elision")).toBe(true);
  });
});
