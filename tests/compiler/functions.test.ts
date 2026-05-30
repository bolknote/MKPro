import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";
import type { CompileOptions } from "../../src/core/index.ts";

function compileOk(source: string, options: Partial<CompileOptions> = { budget: 999 }) {
  return compileMKPro(source, options);
}

describe("rule functions (return)", () => {
  it("lowers a value-returning rule called in an assignment to ПП/В/О with the result in X", () => {
    const result = compileOk(`
program FunctionDemo {
  state {
    result: counter 0..99 = 0
  }
  rule double n {
    return n + n
  }
  turn {
    read x
    result = double(x)
    stop result
  }
}
`);
    const hex = result.steps.map((step) => step.hex);
    expect(hex).toContain("53"); // ПП (subroutine call)
    expect(hex).toContain("52"); // В/О (return)
    // The function call sets the argument register, calls, and the result is
    // consumed straight from X by the following stop.
    expect(result.report.optimizations.some((opt) => opt.name === "function-call")).toBe(true);
  });

  it("hoists a nested function call into a temporary so the working stack is preserved", () => {
    const result = compileOk(`
program Nested {
  state {
    result: counter 0..99 = 0
  }
  rule inc n {
    return n + 1
  }
  rule dbl n {
    return n + n
  }
  turn {
    read x
    result = dbl(inc(x))
    stop result
  }
}
`);
    expect(result.report.optimizations.some((opt) => opt.name === "function-call-lifting")).toBe(true);
    // Two distinct subroutine calls are emitted, one per function.
    const calls = result.steps.filter((step) => step.hex === "53").length;
    expect(calls).toBe(2);
  });

  it("supports early returns on every branch of a function", () => {
    const result = compileOk(`
program Sign {
  state {
    result: counter 0..99 = 0
  }
  rule sign n {
    if n < 0 {
      return 0
    }
    else {
      return 1
    }
  }
  turn {
    read x
    result = sign(x)
    stop result
  }
}
`);
    expect(result.steps.map((step) => step.hex)).toContain("52");
  });

  it("rejects a return outside of a rule", () => {
    expect(() =>
      compileMKPro(`
program BadReturn {
  turn {
    return 1
  }
}
`)
    ).toThrow(/'return' is only allowed inside a rule/u);
  });

  it("rejects a function that does not return on every path", () => {
    expect(() =>
      compileMKPro(`
program PartialReturn {
  state {
    result: counter 0..99 = 0
  }
  rule maybe n {
    if n < 0 {
      return 0
    }
  }
  turn {
    read x
    result = maybe(x)
    stop result
  }
}
`)
    ).toThrow(/must return a value on every path/u);
  });

  it("rejects direct recursion in a function", () => {
    expect(() =>
      compileMKPro(`
program Recursive {
  state {
    result: counter 0..99 = 0
  }
  rule loopy n {
    return loopy(n)
  }
  turn {
    read x
    result = loopy(x)
    stop result
  }
}
`)
    ).toThrow(/Recursive function 'loopy' is not supported/u);
  });

  it("rejects mutual recursion between functions", () => {
    expect(() =>
      compileMKPro(`
program Mutual {
  state {
    result: counter 0..99 = 0
  }
  rule ping n {
    return pong(n)
  }
  rule pong n {
    return ping(n)
  }
  turn {
    read x
    result = ping(x)
    stop result
  }
}
`)
    ).toThrow(/Recursive function '(ping|pong)' is not supported/u);
  });

  it("requires a value after return", () => {
    expect(() =>
      compileMKPro(`
program EmptyReturn {
  state {
    result: counter 0..99 = 0
  }
  rule nothing n {
    return
  }
  turn {
    read x
    result = nothing(x)
    stop result
  }
}
`)
    ).toThrow(/'return' must return a value/u);
  });
});
