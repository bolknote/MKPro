import { describe, expect, it } from "vitest";
import { CompileError, compileMKPro } from "../../src/core/index.ts";

const PROGRAM_WITH_IMPLICIT_ASSIGN = `
program Test {
  state {
    score: counter 0..9 = 0
  }

  loop {
    temp = score + 1
    score = temp
    show(score)
  }
}
`;

const PROGRAM_WITH_SILENT_READ = `
program Test {
  state {
    score: counter 0..9 = 0
  }

  loop {
    show(score)
    key = read()
    score = key
  }
}
`;

const FULLY_DECLARED_PROGRAM = `
program Test {
  state {
    score: counter 0..9 = 0
    temp: packed = 0
    key: packed = 0
  }

  loop {
    temp = score + 1
    score = temp
    show(score)
    key = read()
    score = key
  }
}
`;

describe("implicit allocation strictness", () => {
  it("warns on assignment to an undeclared variable by default", () => {
    const result = compileMKPro(PROGRAM_WITH_IMPLICIT_ASSIGN, { budget: 999 });
    const warning = result.diagnostics.find((diagnostic) =>
      diagnostic.level === "warning" && diagnostic.message.includes("Implicit allocation for undeclared variable 'temp'")
    );
    expect(warning).toBeDefined();
    expect(warning?.message).toContain("state { ... }");
  });

  it("keeps read() targets silent by default", () => {
    const result = compileMKPro(PROGRAM_WITH_SILENT_READ, { budget: 999 });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.level === "warning")).toHaveLength(0);
    expect(result.diagnostics.filter((diagnostic) => diagnostic.level === "error")).toHaveLength(0);
  });

  it("rejects undeclared assignment targets with strict allocation", () => {
    expect(() => compileMKPro(PROGRAM_WITH_IMPLICIT_ASSIGN, { budget: 999, strictAllocation: true }))
      .toThrow(CompileError);
    try {
      compileMKPro(PROGRAM_WITH_IMPLICIT_ASSIGN, { budget: 999, strictAllocation: true });
      expect.unreachable("strict allocation must reject the implicit assignment");
    } catch (error) {
      expect(error).toBeInstanceOf(CompileError);
      const diagnostics = (error as CompileError).diagnostics;
      expect(diagnostics.some((diagnostic) =>
        diagnostic.level === "error" && diagnostic.message.includes("Undeclared variable 'temp' (strict allocation)")
      )).toBe(true);
    }
  });

  it("rejects silent read() scratch targets with strict allocation", () => {
    try {
      compileMKPro(PROGRAM_WITH_SILENT_READ, { budget: 999, strictAllocation: true });
      expect.unreachable("strict allocation must reject the silent read target");
    } catch (error) {
      expect(error).toBeInstanceOf(CompileError);
      const diagnostics = (error as CompileError).diagnostics;
      expect(diagnostics.some((diagnostic) =>
        diagnostic.level === "error" && diagnostic.message.includes("Undeclared variable 'key' (strict allocation)")
      )).toBe(true);
    }
  });

  it("accepts a fully declared program with strict allocation", () => {
    const result = compileMKPro(FULLY_DECLARED_PROGRAM, { budget: 999, strictAllocation: true });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.level === "error")).toHaveLength(0);
    expect(result.diagnostics.filter((diagnostic) =>
      diagnostic.message.includes("Implicit allocation") || diagnostic.message.includes("strict allocation")
    )).toHaveLength(0);
  });
});
