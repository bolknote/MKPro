import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";
import { compileLoweringVariantForTest } from "../../src/core/compiler.ts";

function compileOk(source: string) {
  return compileMKPro(source, { analysis: true, budget: 999 });
}

describe("bit_mask quotient reuse", () => {
  it("reuses one computed cell mask across adjacent set updates", () => {
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

    const names = result.report.optimizations.map((item) => item.name);
    expect(names).toContain("bit-set-mask-cse");
    expect(names).toContain("mask-stack-op-reuse");
    const comments = result.steps.map((step) => step.comment ?? "");
    const scratchIndex = comments.indexOf("cell bit mask scratch");
    const firstSetIndex = comments.indexOf("bit_set with reused mask");
    expect(firstSetIndex).toBeGreaterThan(scratchIndex);
    expect(comments.slice(scratchIndex + 1, firstSetIndex)).not.toContain("reuse cell bit mask");
    expect(result.steps.filter((step) =>
      step.opcode === 0x53 && step.comment?.startsWith("expr cell_mask(")
    )).toHaveLength(1);
    expect(comments.filter((comment) => comment === "bit_set with reused mask")).toHaveLength(2);
    expect(comments.filter((comment) => comment === "reuse cell bit mask")).toHaveLength(1);
  });

  it("shares one bit_mask helper across independent teleport membership checks", () => {
    const result = compileMKPro(`
program SharedBitMaskHelper {
  grid: board(1..7, 1..4)
  state {
    cell: coord(grid)
    first: cells(grid)
    second: cells(grid)
    score: counter 0..9 = 0
  }
  loop {
    cell = read()
    if cell in first {
      score++
    }
    if cell in second {
      score++
    }
    halt(score)
  }
}
`, { analysis: true, budget: 999999, indirectFlowRescueAbove: 999999 });
    const names = result.report.optimizations.map((item) => item.name);

    expect(result.report.steps).toBe(43);
    expect(names).toContain("shared-bit-mask-helper-layout");
    expect(names).toContain("bit-mask-condition-helper");
    expect(names).toContain("bit-mask-helper");
  });

  it("reuses one scratch register for repeated independent bit clears", () => {
    const result = compileLoweringVariantForTest(`
program RepeatedBitClearScratch {
  grid: board(1..7, 1..4)
  state {
    pos: coord(grid) = 11
    plans: cells(grid)
    answer: counter 0..9 = 0
    memory: counter 0..9 = 1
    a: counter 0..9 = 1
    b: counter 0..9 = 2
    c: counter 0..9 = 3
    d: counter 0..9 = 4
    e: counter 0..9 = 5
    f: counter 0..9 = 6
    g: counter 0..9 = 7
    h: counter 0..9 = 8
    i: counter 0..9 = 9
  }
  loop {
    if answer == memory {
      plans -= pos
    }
    if a != b {
      plans -= pos
    }
    if c != d {
      plans -= pos
    }
    if e != f {
      plans -= pos
    }
    halt(plans + answer + memory + a + b + c + d + e + f + g + h + i)
  }
}
`, { analysis: true, budget: 999999, indirectFlowRescueAbove: 999999 }, {});
    const names = result.report.optimizations.map((item) => item.name);

    expect(names.filter((name) => name === "single-bit-mask-op")).toHaveLength(4);
    expect(result.report.registers.plans).toBeDefined();
    expect(result.report.registers.pos).toBeDefined();
  });
});
