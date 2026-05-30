import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";

function compileOk(source: string) {
  return compileMKPro(source, { analysis: true, budget: 999 });
}

describe("bit_mask quotient reuse", () => {
  it("reuses the index/4 quotient while computing a shared bit mask", () => {
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
    halt(mine + seen)
  }
}
`);

    const names = result.report.optimizations.map((item) => item.name);
    expect(names).toContain("bit-set-mask-cse");
    expect(names).toContain("bit-mask-quotient-reuse");
    expect(result.steps.filter((step) => step.opcode === 0x13 && step.comment === "bit mask quotient")).toHaveLength(1);
    expect(result.steps.filter((step) => step.opcode === 0x13 && step.comment === "bit mask fractional place")).toHaveLength(1);
    expect(result.steps.map((step) => step.comment)).toContain("bit mask quotient");
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
  turn {
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
`, { analysis: true, budget: 999999 });
    const names = result.report.optimizations.map((item) => item.name);

    expect(result.report.steps).toBe(51);
    expect(names).toContain("shared-bit-mask-helper-layout");
    expect(names).toContain("bit-mask-condition-helper");
    expect(names).toContain("bit-mask-helper");
  });
});
