import { describe, expect, it } from "vitest";
import { CompileError, compileM61 } from "../../src/core/index.ts";

function compileOk(source: string) {
  return compileM61(source);
}

describe("compiler semantics", () => {
  it("compiles only the V2 target/program surface", () => {
    expect(() =>
      compileM61(`
machine mk61
entry main {
  pause 0
}
`),
    ).toThrow(/Unexpected top-level line 'machine mk61'/u);
  });

  it("emits a final stop for a minimal V2 program", () => {
    const result = compileOk(`
target mk61
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
target mk61
budget 999 cells
program FormulaPrimitives {
  state {
    value: packed = 0
  }
  turn {
    value = max(pi(), sqr(2)) + inv(2) + bit_and(1, 2) + bit_or(1, 2) + bit_xor(1, 2) + bit_not(1)
    stop value
  }
}
`);
    const opcodes = result.steps.map((step) => step.hex);

    for (const opcode of ["20", "22", "23", "36", "37", "38", "39", "3A"]) {
      expect(opcodes).toContain(opcode);
    }
  });

  it("accepts decimal constants inside V2 formulas", () => {
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
    stop accel
  }
}
`);

    expect(result.steps.length).toBeGreaterThan(0);
  });

  it("removes branchy boolean V2 assignments when arithmetic selection is shorter", () => {
    const result = compileOk(`
target mk61
budget 999 cells
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

  it("replaces boolean-selected V2 stops with terminal selection", () => {
    const result = compileOk(`
target mk61
budget 999 cells
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
target mk61
budget 999 cells
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
target mk61
budget 999 cells
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
    const body = Array.from({ length: 60 }, () => "    x = x + 1234567").join("\n");
    expect(() =>
      compileM61(`
target mk61
budget steps <= 20
program TooLarge {
  state {
    x: counter 0..999999 = 0
  }
  turn {
${body}
    stop x
  }
}
`),
    ).toThrow(CompileError);
  });

  it("keeps the default V2 budget at 105 cells", () => {
    const result = compileOk(`
target mk61
program DefaultBudget {
  turn {
    stop 1
  }
}
`);
    expect(result.report.budget).toBe(105);
  });
});
