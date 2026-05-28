import { describe, expect, it } from "vitest";
import {
  codeToAddress,
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

  it("decodes formal address bytes as address ordinals", () => {
    expect(codeToAddress(0x99)).toBe(99);
    expect(codeToAddress(0xa4)).toBe(104);
    expect(codeToAddress(0x9f)).toBe(105);
    expect(codeToAddress(0xc5)).toBe(125);
    expect(codeToAddress(0xff)).toBe(165);
  });

  it("accepts command-page notation aliases", () => {
    const aliases: Array<[string, number]> = [
      ["←→", 0x14],
      ["F 10^{x}", 0x15],
      ["F e^{x}", 0x16],
      ["F sin^{-1}", 0x19],
      ["F cos^{-1}", 0x1a],
      ["F tg^{-1}", 0x1b],
      ["F π", 0x20],
      ["F√", 0x21],
      ["F x^{2}", 0x22],
      ["F x^{y}", 0x24],
      ["F↻", 0x25],
      ["К°→′", 0x26],
      ["К−", 0x27],
      ["К×", 0x28],
      ["К÷", 0x29],
      ["К°→′\"", 0x2a],
      ["К°←′\"", 0x30],
      ["К∣x∣", 0x31],
      ["К°←′", 0x33],
      ["К∧", 0x37],
      ["К∨", 0x38],
      ["F x≠0", 0x57],
      ["F x≥0", 0x59],
      ["К x≠0 0", 0x70],
      ["К x≥0 e", 0x9e],
    ];

    for (const [name, code] of aliases) {
      expect(findOpcodeName(name)?.code, name).toBe(code);
    }
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
