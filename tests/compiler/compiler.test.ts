import { describe, expect, it } from "vitest";
import { CompileError, compileM61, formatHex } from "../../src/core/index.ts";

function compileOk(source: string) {
  return compileM61(source);
}

describe("compiler semantics", () => {
  it("emits a final С/П after the main entry", () => {
    const result = compileOk(`
machine mk61
entry main {
  pause 0
}
`);
    const last = result.steps.at(-1);
    expect(last?.hex).toBe("50");
  });

  it("conditional jumps follow MK-61 'true=fall-through' convention for ==", () => {
    const result = compileOk(`
machine mk61
store a
store b
entry main {
  a = 1
  b = 1
  if a == b {
    pause 7
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);
    // The comparator '5E' is 'F x=0' which jumps when x != 0 (i.e. mismatch).
    expect(opcodes).toContain("5E");
  });

  it("switch evaluates expression once via scratch register", () => {
    const result = compileOk(`
machine mk61
entry main {
  switch random() {
    case 1 {
      halt 1
    }
    case 2 {
      halt 2
    }
  }
}
`);
    const randoms = result.steps.filter((step) => step.hex === "3B");
    expect(randoms.length).toBe(1);
  });

  it("lowers MK-61 primitive functions to their single opcodes", () => {
    const result = compileOk(`
machine mk61
entry main {
  pause pi()
  pause sqr(2)
  pause inv(2)
  pause max(1, 2)
  pause bit_and(1, 2)
  pause bit_or(1, 2)
  pause bit_xor(1, 2)
  pause bit_not(1)
  pause to_min(1)
  pause to_sec(1)
  pause from_min(1)
  pause from_sec(1)
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    for (const opcode of ["20", "22", "23", "36", "37", "38", "39", "3A", "26", "2A", "33", "30"]) {
      expect(opcodes).toContain(opcode);
    }
  });

  it("accepts decimal constants inside v2 formulas", () => {
    const result = compileOk(`
target mk61
budget 999 cells
program DecimalFormula {
  input burn: number
  state {
    fuel: resource 0..999 = 140
    accel: packed = 0
  }
  turn {
    accel = burn * 10 / fuel - 9.8
    stop 0
  }
}
`);

    expect(result.steps.length).toBeGreaterThan(0);
  });

  it("lowers 4x4 grid and packed-line helpers for tic-tac-toe style games", () => {
    const result = compileOk(`
machine mk61
budget 999 cells
store mask
store lines
entry main {
  pause norm4(5)
  pause diag_left_index(3, 4)
  pause diag_right_index(3, 4)
  pause cell_mask(3, 4)
  pause cell_used(mask, 3, 4)
  mask = cell_mark(mask, 3, 4)
  lines = packed4_add(lines, 2, 1)
  pause packed4_digit(lines, 2)
  pause packed4_score(lines, 2)
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(result.report.optimizations.some((item) => item.name === "tic-tac-toe-primitive-lowering")).toBe(true);
    for (const opcode of ["15", "22", "32", "34", "35", "36", "37", "38"]) {
      expect(opcodes).toContain(opcode);
    }
  });

  it("uses 4x4 helper register conventions and reuses adjacent cell masks", () => {
    const result = compileOk(`
machine mk61
store x
store y
store mask
store lines
store used
entry main {
  used = cell_used(mask, x, y)
  mask = cell_mark(mask, x, y)
  pause packed4_add(lines, x, 1)
}
`);

    expect(result.report.registers.x).toBe("1");
    expect(result.report.registers.y).toBe("2");
    expect(result.report.registers.mask).toBe("9");
    expect(result.report.registers.lines).toBe("4");
    expect(result.report.optimizations.some((item) => item.name === "tic-tac-toe-cell-mask-cse")).toBe(true);
    expect(Object.keys(result.report.registers).some((name) => name.startsWith("__ttt_mask_"))).toBe(false);
  });

  it("replaces shorter boolean if/else assignments with arithmetic selection", () => {
    const result = compileOk(`
machine mk61
state {
  flag: flag = 0
}
store result
entry main {
  if flag == 1 {
    result = 50
  }
  else {
    result = 10
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(result.report.optimizations.some((item) => item.name === "branch-removal")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-select")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((item) => item.id === "branch-removal")).toBe(true);
    expect(result.report.proofs.some((item) => item.id === "branch-equivalence")).toBe(true);
    expect(opcodes).not.toContain("5E");
    expect(opcodes).not.toContain("51");
  });

  it("keeps ordinary branches when the selector is not proven boolean", () => {
    const result = compileOk(`
machine mk61
store flag = 0
store result
entry main {
  if flag == 1 {
    result = 50
  }
  else {
    result = 10
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(result.report.optimizations.some((item) => item.name === "branch-removal")).toBe(false);
    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-select")).toBe(false);
    expect(opcodes).toContain("5E");
    expect(opcodes).toContain("51");
  });

  it("replaces boolean-guarded additions and subtractions with masked arithmetic", () => {
    const result = compileOk(`
machine mk61
state {
  used: flag = 0
}
store score
store fuel
entry main {
  if used == 1 {
    score = score + 5
  }
  if used != 0 {
    fuel = fuel - 2
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(result.report.optimizations.filter((item) => item.name === "branch-removal").length).toBe(2);
    expect(result.report.optimizations.filter((item) => item.name === "arithmetic-if-update").length).toBe(2);
    expect(opcodes).not.toContain("5E");
  });

  it("replaces max/min if branches with arithmetic extrema", () => {
    const result = compileOk(`
machine mk61
store a
store b
store hi
store lo
entry main {
  if a > b {
    hi = a
  }
  else {
    hi = b
  }
  if a < b {
    lo = a
  }
  else {
    lo = b
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(result.report.optimizations.some((item) => item.name === "branch-removal")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-max")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-min")).toBe(true);
    expect(opcodes).toContain("36");
    expect(opcodes).not.toContain("5C");
    expect(opcodes).not.toContain("51");
  });

  it("replaces clamp and abs branches when they are shorter", () => {
    const result = compileOk(`
machine mk61
store x
entry main {
  if x < 0 {
    x = 0
  }
  if x < 0 {
    x = -x
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(result.report.optimizations.some((item) => item.name === "branch-removal")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-max")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-abs")).toBe(true);
    expect(opcodes).toContain("36");
    expect(opcodes).toContain("31");
    expect(opcodes).not.toContain("5C");
  });

  it("replaces boolean-selected pause and halt branches", () => {
    const result = compileOk(`
machine mk61
state {
  flag: flag = 0
}
entry main {
  if flag == 1 {
    pause 50
  }
  else {
    pause 10
  }
  if flag == 0 {
    halt 0
  }
  else {
    halt 9
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(result.report.optimizations.filter((item) => item.name === "arithmetic-if-terminal-select").length).toBe(2);
    expect(opcodes).not.toContain("5E");
    expect(opcodes).not.toContain("51");
  });

  it("extends terminal-select to comparison conditions when branchless is shorter", () => {
    const result = compileOk(`
machine mk61
state {
  counter: range 0..9 = 0
}
entry main {
  if counter > 0 {
    halt 1
  }
  else {
    halt 0
  }
}
`);
    const applied = result.report.optimizations.filter((item) => item.name === "arithmetic-if-terminal-select");
    expect(applied.length).toBe(1);
    const opcodes = result.steps.map((step) => step.hex);
    expect(opcodes).not.toContain("51");
  });

  it("rejects branchless terminal-select when the branched form is shorter", () => {
    const result = compileOk(`
machine mk61
state {
  speed: range -99..99 = 0
}
entry main {
  if abs(speed) <= 5 {
    halt 777
  }
  else {
    halt 666
  }
}
`);
    const applied = result.report.optimizations.filter((item) => item.name === "arithmetic-if-terminal-select");
    expect(applied.length).toBe(0);
  });

  it("replaces conditional move, sign toggle, and saturating updates", () => {
    const result = compileOk(`
machine mk61
state {
  flag: flag = 0
  fuel: range 0..9 = 0
  level: range 0..9 = 0
}
store x
entry main {
  if flag == 1 {
    x = 0
  }
  if flag != 0 {
    x = -x
  }
  else {
    x = x
  }
  if fuel > 0 {
    fuel = fuel - 1
  }
  if level < 9 {
    level = level + 1
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-conditional-move")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-sign-toggle")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-max")).toBe(true);
    expect(opcodes).not.toContain("5E");
  });

  it("replaces comparison-to-boolean and boolean algebra branches", () => {
    const result = compileOk(`
machine mk61
state {
  a: flag = 0
  b: flag = 0
}
store x
store eq
store neq
store both
store either
entry main {
  if x == 7 {
    eq = 1
  }
  else {
    eq = 0
  }
  if x != 7 {
    neq = 1
  }
  else {
    neq = 0
  }
  if a == 1 {
    both = b
  }
  else {
    both = 0
  }
  if a == 1 {
    either = 1
  }
  else {
    either = b
  }
}
`);

    expect(result.report.optimizations.filter((item) => item.name === "arithmetic-if-comparison-mask").length).toBe(2);
    expect(result.report.optimizations.filter((item) => item.name === "arithmetic-if-boolean-algebra").length).toBe(2);
  });

  it("replaces adjacent lower and upper clamp branches with a double clamp", () => {
    const result = compileOk(`
machine mk61
store x
entry main {
  if x < 1 {
    x = 1
  }
  if x > 9 {
    x = 9
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(result.report.optimizations.some((item) => item.name === "arithmetic-if-double-clamp")).toBe(true);
    expect(opcodes).toContain("36");
    expect(opcodes).not.toContain("5C");
  });

  it("detects cyclic constants", () => {
    expect(() =>
      compileM61(`
machine mk61
const a = b
const b = a
entry main {
  pause a
}
`),
    ).toThrow(CompileError);
  });

  it("warns about undeclared assignment targets", () => {
    const result = compileM61(`
machine mk61
entry main {
  spelled_wrong = 5
  pause spelled_wrong
}
`);
    const warnings = result.diagnostics.filter((d) => d.level === "warning");
    expect(warnings.some((w) => w.message.includes("spelled_wrong"))).toBe(true);
  });

  it("peephole removes X->П r ; П->X r at synthetic boundaries", () => {
    const result = compileM61(`
machine mk61
store y
entry main {
  y = ask 0
  pause y
}
`);
    // After X->П 1 (input y), the redundant П->X 1 should be elided
    // before the pause prefix П->X 1.
    const hex = formatHex(result);
    expect(hex.split(/\s+/u).filter((h) => h === "61").length).toBeLessThanOrEqual(1);
  });

  it("peephole does NOT cross a core { } boundary", () => {
    const result = compileM61(`
machine mk61
store y prefer R1
entry main {
  y = 5
  core {
    Пх1
  }
}
`);
    const codes = result.steps.map((step) => step.hex);
    // X->П 1 (set y) followed by П->X 1 from core{} must remain.
    const storeIdx = codes.findIndex((c) => c === "41");
    expect(storeIdx).toBeGreaterThan(-1);
    expect(codes[storeIdx + 1]).toBe("61");
  });

  it("uses Danilov В/О jump only when the return-stack proof holds", () => {
    const result = compileM61(`
machine mk61
entry main {
  core {
    0
  }
  loop {
    pause 2
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    expect(result.report.optimizations.some((item) => item.name === "return-zero-jump")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((item) => item.id === "return-empty-stack-jump")).toBe(true);
    expect(result.report.proofs.some((item) => item.id === "return-stack-empty" && item.status === "proved")).toBe(true);
    expect(opcodes).toContain("52");
    expect(opcodes).not.toContain("51");
  });

  it("keeps Danilov error-stop idioms behind explicit trap semantics", () => {
    const result = compileM61(`
machine mk61
entry main {
  trap zero 0
}
`);

    expect(result.steps.some((step) => step.hex === "23")).toBe(true);
    expect(result.report.optimizations.some((item) => item.name === "error-stop")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((item) => item.id === "error-stops")).toBe(true);
    expect(result.report.unsafeUnverified.some((item) => item.includes("trap zero"))).toBe(true);
  });

  it("budget exceeded surfaces as a hard error", () => {
    let body = "";
    for (let i = 0; i < 60; i += 1) body += "pause 1234567\n";
    expect(() =>
      compileM61(`
machine mk61
budget steps <= 20
entry main {
${body}
}
`),
    ).toThrow(CompileError);
  });

  it("budget defaults to 105", () => {
    const result = compileOk(`
machine mk61
entry main {
  pause 1
}
`);
    expect(result.report.budget).toBe(105);
  });

  it("delivery=manual flags dangerous opcodes as unsafe", () => {
    const result = compileM61(
      `
machine mk61
entry main {
  core {
    К *
  }
}
`,
      { delivery: "manual" },
    );
    expect(result.report.unsafeUnverified.length).toBeGreaterThan(0);
  });
});
