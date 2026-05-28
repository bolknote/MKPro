import { describe, expect, it } from "vitest";
import {
  formalAddressInfo,
  formalAddressOrdinal,
  formatFormalAddressOpcode,
  officialAddressToOpcode,
  parseFormalAddressOpcode,
} from "../../src/core/index.ts";

describe("MK-61 formal address space", () => {
  it("keeps official addresses encoded as documented program cells", () => {
    expect(officialAddressToOpcode(0)).toBe(0x00);
    expect(officialAddressToOpcode(99)).toBe(0x99);
    expect(officialAddressToOpcode(100)).toBe(0xa0);
    expect(officialAddressToOpcode(104)).toBe(0xa4);
    expect(() => officialAddressToOpcode(105)).toThrow(/outside 00\.\.A4/u);
  });

  it("maps formal side branches through Anvarov's address-space table", () => {
    expect(formalAddressInfo(0xa5)).toMatchObject({ ordinal: 105, actual: 0, kind: "short-side" });
    expect(formalAddressInfo(0xb1)).toMatchObject({ ordinal: 111, actual: 6, kind: "short-side" });
    expect(formalAddressInfo(0xb2)).toMatchObject({ ordinal: 112, actual: 0, kind: "long-side" });
    expect(formalAddressInfo(0xc5)).toMatchObject({ ordinal: 125, actual: 13, kind: "dark" });
    expect(formalAddressInfo(0xf9)).toMatchObject({ ordinal: 159, actual: 47, kind: "dark" });
  });

  it("maps super-dark formal addresses to one-command branches with continuations", () => {
    expect(formalAddressInfo(0xfa)).toMatchObject({
      ordinal: 160,
      actual: 48,
      kind: "super-dark",
      oneCommand: true,
      extra: 1,
    });
    expect(formalAddressInfo(0xff)).toMatchObject({
      ordinal: 165,
      actual: 53,
      kind: "super-dark",
      oneCommand: true,
      extra: 6,
    });
  });

  it("treats hex low nibbles as arithmetic address digits", () => {
    expect(formalAddressOrdinal(0x9f)).toBe(105);
    expect(formalAddressInfo(0x9f).actual).toBe(0);
    expect(formalAddressInfo(0xac).ordinal).toBe(112);
    expect(formalAddressInfo(0xac).actual).toBe(0);
  });

  it("parses and formats formal address bytes without losing dark labels", () => {
    expect(parseFormalAddressOpcode("C5")).toBe(0xc5);
    expect(parseFormalAddressOpcode(".5")).toBe(0xa5);
    expect(formatFormalAddressOpcode(0xfb)).toBe("FB");
  });
});
