import { describe, expect, it } from "vitest";
import {
  findOpcodeName,
  opcodeByCode,
  opcodeCatalog,
} from "../../src/core/index.ts";

describe("opcode catalog", () => {
  it("covers all 256 opcodes", () => {
    expect(opcodeCatalog.length).toBe(256);
    expect(opcodeByCode.size).toBe(256);
  });

  it("looks up mnemonics by both Cyrillic and Latin variants", () => {
    expect(findOpcodeName("К СЧ")?.code).toBe(0x3b);
    expect(findOpcodeName("K СЧ")?.code).toBe(0x3b);
    expect(findOpcodeName("k сч")?.code).toBe(0x3b);
  });

  it("looks up storage opcodes with Cyrillic Х", () => {
    expect(findOpcodeName("Х->П 0")?.code).toBe(0x40);
    expect(findOpcodeName("X->П 0")?.code).toBe(0x40);
  });

  it("looks up raw hex strings", () => {
    expect(findOpcodeName("3B")?.code).toBe(0x3b);
    expect(findOpcodeName("50")?.code).toBe(0x50);
  });

  it("keeps historical opcode risk metadata descriptive", () => {
    const undoc = opcodeByCode.get(0xf7);
    expect(undoc?.risk).toBe("undocumented");
    expect(undoc?.enterable).not.toContain("manual");
  });

  it("models 5F as a raw display transform, not a hang opcode", () => {
    const opcode = opcodeByCode.get(0x5f);
    expect(opcode?.name).toBe("raw display 5F");
    expect(opcode?.risk).toBe("undocumented");
  });
});
