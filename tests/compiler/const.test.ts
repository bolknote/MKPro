import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { compileMKPro, ParseError, parseProgram } from "../../src/core/index.ts";

describe("compile-time const", () => {
  it("parses program-level const declarations", () => {
    const ast = parseProgram(`
program Demo {
  const CAP = 10000
  const TWICE = CAP * 2
  state {
    score: counter 0..9 = 0
  }
  loop {
    halt(score)
  }
}
`);
    expect(ast.v2?.consts).toEqual([
      { kind: "v2_const", name: "CAP", expr: "10000", line: 3 },
      { kind: "v2_const", name: "TWICE", expr: "CAP * 2", line: 4 },
    ]);
  });

  it("rejects assignment to a const", () => {
    expect(() =>
      parseProgram(`
program Bad {
  const CAP = 1
  state { x: counter 0..9 = 0 }
  loop { CAP = 2 }
}
`),
    ).toThrow(ParseError);
  });

  it("rejects const shadowing state", () => {
    expect(() =>
      parseProgram(`
program Bad {
  state { cap: counter 0..9 = 0 }
  const cap = 1
  loop { halt(0) }
}
`),
    ).toThrow(/shadows state/u);
  });

  it("rejects non-numeric const expressions", () => {
    expect(() =>
      parseProgram(`
program Bad {
  const x = random()
  state { n: counter 0..9 = 0 }
  loop { halt(0) }
}
`),
    ).toThrow(ParseError);
  });

  it("reports const-inline and folds dependent const values", () => {
    const result = compileMKPro(
      `
program Caps {
  const CAP = 10000
  const DOUBLE = CAP * 2
  state {
    x: counter 0..9 = 0
  }
  loop {
    x = int(DOUBLE / 10000)
    halt(x)
  }
}
`,
      { budget: 999999, analysis: true },
    );
    expect(result.report.optimizations.some((entry) => entry.name === "const-inline")).toBe(true);
    expect(result.report.optimizations.some((entry) => entry.name === "expression-constant-folder")).toBe(true);
  });

  it("allows const references in indexed initializer lists", () => {
    const result = compileMKPro(
      `
program Bank {
  const RESERVE = 25000
  state {
    cells: packed[1..2] = [0, RESERVE]
  }
  loop {
    halt(cells[2])
  }
}
`,
      { budget: 999999, analysis: true },
    );
    expect(result.steps.length).toBeLessThanOrEqual(20);
    expect(result.report.registers.cells_2).toBe("2");
  });

  it("keeps rambo-iii at the locked size with named consts", () => {
    const result = compileMKPro(
      readFileSync(resolve("examples/rambo-iii.mkpro"), "utf8"),
      { budget: 999999, analysis: true },
    );
    expect(result.steps.length).toBe(104);
    expect(result.report.optimizations.some((entry) => entry.name === "const-inline")).toBe(true);
  });
});
