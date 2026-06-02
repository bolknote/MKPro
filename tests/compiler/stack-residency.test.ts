import { describe, expect, it } from "vitest";
import {
  canLowerStackResidentExpression,
  findStackResidentFusionSite,
  statementPreservesStackResidency,
  stackResidentRestoreOps,
  summarizeStackResidencyCandidatesInBlock,
} from "../../src/core/emit/stack-residency-analysis.ts";
import { compileLoweringVariantForTest } from "../../src/core/compiler.ts";
import type { ConditionAst, ExpressionAst, StatementAst } from "../../src/core/types.ts";

function compileOk(source: string, stackResidentTemps = true) {
  const result = compileLoweringVariantForTest(source, { budget: 999999 }, { stackResidentTemps });
  if (result.diagnostics.some((d) => d.level === "error")) {
    throw new Error(result.diagnostics.map((d) => d.message).join("\n"));
  }
  return result;
}

function id(name: string): ExpressionAst {
  return { kind: "identifier", name };
}

function num(raw: string): ExpressionAst {
  return { kind: "number", raw };
}

function compare(left: ExpressionAst, op: ConditionAst["op"], right: ExpressionAst): ConditionAst {
  return { left, op, right };
}

describe("stack-residency analysis", () => {
  it("detects a straight-line dual-temp fusion site", () => {
    const body: StatementAst[] = [
      { kind: "assign", target: "a", expr: id("x"), line: 1 },
      { kind: "assign", target: "b", expr: id("y"), line: 2 },
      { kind: "assign", target: "c", expr: { kind: "binary", op: "+", left: id("a"), right: id("b") }, line: 3 },
      { kind: "if", condition: compare(id("c"), "==", num("0")), thenBody: [], line: 4 },
    ];
    const block = summarizeStackResidencyCandidatesInBlock(body);
    expect(block.fusionSites).toBe(1);
    expect(block.controlFlowFusions).toBe(0);
    expect(block.maxLiveTemps).toBeGreaterThanOrEqual(2);
  });

  it("maps restore ops for deep stack slots", () => {
    expect(stackResidentRestoreOps(1, 2)).toEqual([]);
    expect(stackResidentRestoreOps(0, 2)).toEqual(["swap"]);
    expect(stackResidentRestoreOps(0, 3)).toEqual(["reverse", "swap"]);
    expect(stackResidentRestoreOps(0, 4)).toEqual(["reverse", "reverse", "swap"]);
  });

  it("accepts a binary consumer over two temps", () => {
    const expr: ExpressionAst = {
      kind: "binary",
      op: "-",
      left: id("a"),
      right: id("b"),
    };
    expect(canLowerStackResidentExpression(expr, ["a", "b"])).toBe(true);
  });

  it("finds fusion sites across stack-preserving if gaps", () => {
    const body: StatementAst[] = [
      { kind: "assign", target: "a", expr: id("x"), line: 1 },
      { kind: "assign", target: "b", expr: id("y"), line: 2 },
      {
        kind: "if",
        condition: compare(id("x"), "==", num("0")),
        thenBody: [],
        line: 3,
      },
      { kind: "assign", target: "c", expr: { kind: "binary", op: "+", left: id("a"), right: id("b") }, line: 4 },
    ];
    const site = findStackResidentFusionSite(body, 0);
    expect(site?.crossesControlFlow).toBe(true);
    expect(site?.temps.map((segment) => segment.assign.target)).toEqual(["a", "b"]);
  });

  it("treats empty if/else as stack-preserving for unrelated temps", () => {
    const stmt: StatementAst = {
      kind: "if",
      condition: compare(id("x"), "==", num("0")),
      thenBody: [],
      elseBody: [],
      line: 1,
    };
    expect(statementPreservesStackResidency(stmt, new Set(["a"]))).toBe(true);
  });
});

describe("stack-resident temp lowering", () => {
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

  it("fuses temps separated by a stack-preserving if", () => {
    const result = compileOk(`
program CrossFlowIf {
  state {
    x: packed = 1
    y: packed = 2
    z: packed = 0
    gate: packed = 0
  }
  loop {
    a = x
    b = y
    if gate == 1 {
      loop {
      }
    }
    z = a + b
    halt(z)
  }
}
`);
    expect(result.report.optimizations.some((entry) => entry.name === "stack-resident-control-flow")).toBe(true);
  });
});
