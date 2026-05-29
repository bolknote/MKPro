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
    stop mine + seen
  }
}
`);

    const names = result.report.optimizations.map((item) => item.name);
    expect(names).toContain("bit-set-mask-cse");
    expect(names).toContain("bit-mask-quotient-reuse");
    const codes = result.steps.map((step) => step.opcode);
    expect(codes.filter((opcode) => opcode === 0x13).length).toBe(1);
    expect(result.steps.map((step) => step.comment)).toContain("bit mask quotient");
  });
});
