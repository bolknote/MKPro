import { describe, expect, it } from "vitest";
import {
  analyzeStackResidencyWindows,
  canLowerStackResidentExpression,
  stackResidentRestoreOps,
} from "../../src/core/emit/stack-residency-analysis.ts";
import { compileLoweringVariantForTest } from "../../src/core/compiler.ts";
import type { StatementAst } from "../../src/core/types.ts";

function compileOk(source: string, stackResidentTemps = true) {
  const result = compileLoweringVariantForTest(source, { budget: 999999 }, { stackResidentTemps });
  if (result.diagnostics.some((d) => d.level === "error")) {
    throw new Error(result.diagnostics.map((d) => d.message).join("\n"));
  }
  return result;
}

describe("stack-residency analysis", () => {
  it("detects straight-line windows and dual-temp triples", () => {
    const body: StatementAst[] = [
      { kind: "assign", target: "a", expr: { kind: "identifier", name: "x" }, line: 1 },
      { kind: "assign", target: "b", expr: { kind: "identifier", name: "y" }, line: 2 },
      { kind: "assign", target: "c", expr: { kind: "binary", op: "+", left: { kind: "identifier", name: "a" }, right: { kind: "identifier", name: "b" } }, line: 3 },
      { kind: "if", condition: { kind: "compare", op: "==", left: { kind: "identifier", name: "c" }, right: { kind: "number", raw: "0" } }, thenBody: [], line: 4 },
    ];
    const windows = analyzeStackResidencyWindows(body);
    expect(windows).toHaveLength(1);
    expect(windows[0]!.dualTempTriples).toBe(1);
    expect(windows[0]!.maxLiveTemps).toBeGreaterThanOrEqual(2);
  });

  it("maps restore ops for deep stack slots", () => {
    expect(stackResidentRestoreOps(1, 2)).toEqual([]);
    expect(stackResidentRestoreOps(0, 2)).toEqual(["swap"]);
    expect(stackResidentRestoreOps(0, 3)).toEqual(["reverse", "swap"]);
    expect(stackResidentRestoreOps(0, 4)).toEqual(["reverse", "reverse", "swap"]);
  });

  it("accepts a binary consumer over two temps", () => {
    const expr = {
      kind: "binary" as const,
      op: "-",
      left: { kind: "identifier" as const, name: "a" },
      right: { kind: "identifier" as const, name: "b" },
    };
    expect(canLowerStackResidentExpression(expr, ["a", "b"])).toBe(true);
  });
});

describe("stack-resident temp lowering", () => {
  it("fuses two single-use temps into one stack-resident add", () => {
    const result = compileOk(`
program DualStack {
  state {
    x: packed = 1
    y: packed = 2
    z: packed = 0
  }
  loop {
    a = x
    b = y
    z = a + b
    halt(z)
  }
}
`);
    const hex = result.steps.map((step) => step.hex).join(" ");
    expect(hex).not.toMatch(/П→X.*П→X/);
    expect(result.report.optimizations.some((entry) => entry.name === "stack-resident-temps")).toBe(true);
    expect(result.steps.length).toBeLessThan(20);
  });

  it("shrinks dual-temp programs versus the default lowering variant", () => {
    const source = `
program Off {
  state {
    x: packed = 1
    y: packed = 2
    z: packed = 0
  }
  loop {
    a = x
    b = y
    z = a + b
    halt(z)
  }
}
`;
    const baseline = compileLoweringVariantForTest(source, { budget: 999999 }, {});
    const enabled = compileOk(source);
    expect(enabled.steps.length).toBeLessThanOrEqual(baseline.steps.length);
  });
});
