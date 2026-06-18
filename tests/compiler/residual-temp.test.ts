import { describe, expect, it } from "vitest";
import { compileLoweringVariantForTest } from "../../src/core/compiler.ts";

describe("dead source residual temp reuse", () => {
  it("reuses a dead source register for a residual temp without changing source code", () => {
    const result = compileLoweringVariantForTest(`
program ResidualTemp {
  state {
    command: packed = 0
    step: packed = 0
  }

  loop {
    command = read()
    step = command - 5

    unless step {
      halt(0)
    }
    else {
      step = int(1 / step)

      unless step {
        step = sign(command - 5)
        halt(step)
      }
      else {
        halt(step)
      }
    }
  }
}
`, { analysis: true, budget: 999999 }, { deadSourceResidualTempReuse: true });

    expect(result.report.optimizations.map((item) => item.name)).toContain("dead-source-residual-temp-reuse");
    expect(result.steps.some((step) => step.comment === "set command")).toBe(true);
    expect(result.steps.some((step) => step.comment === "set step" && step.address < 10)).toBe(false);
  });
});
