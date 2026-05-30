import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";

describe("residual else-if lowering", () => {
  it("reuses the failed equality residual for the next equality test", () => {
    const result = compileMKPro(`
program ResidualElseIfProbe {
  state {
    pos: packed = 2
    food: counter 0..99 = 0
    dynamite: counter 0..9 = 0
    treasure: counter 0..9 = 0
  }
  turn {
    if int(pos) == 1 {
      food += 9
      halt(food)
    }
    else {
      if int(pos) == 2 {
        dynamite += 4
        halt(dynamite)
      }
      else {
        treasure++
        halt(treasure)
      }
    }
  }
}
`);

    expect(result.report.optimizations.some((item) => item.name === "residual-elseif-compare")).toBe(true);
    expect(result.steps.map((step) => step.comment)).toContain("residual else-if compare");
  });
});
