import { describe, expect, it } from "vitest";
import { verifySuperDarkSuffixLayout } from "../../src/core/index.ts";
import type { LayoutIrCell } from "../../src/core/index.ts";

function cell(address: number, opcode = 0x00, roles: LayoutIrCell["roles"] = ["exec"]): LayoutIrCell {
  return { address, opcode, roles, tactic: "" };
}

describe("super-dark layout verifier", () => {
  it("proves the exact FA..FF entry and continuation matrix", () => {
    const layout = Array.from({ length: 60 }, (_, address) => cell(address, address % 10));
    layout.push({ address: 60, opcode: 0x87, roles: ["exec"], tactic: "super-dark indirect dispatch" });
    const proof = verifySuperDarkSuffixLayout(layout);

    expect(proof.proved).toBe(true);
    expect(proof.reasons).toEqual([]);
    expect(proof.dispatchCells).toEqual([
      { address: 60, opcode: 0x87, register: "7", tactic: "super-dark indirect dispatch" },
    ]);
    expect(proof.pairs.map((pair) => [pair.formal, pair.entryAddress, pair.continuationAddress])).toEqual([
      [0xfa, 48, 1],
      [0xfb, 49, 2],
      [0xfc, 50, 3],
      [0xfd, 51, 4],
      [0xfe, 52, 5],
      [0xff, 53, 6],
    ]);
  });

  it("rejects a suffix-compatible matrix without an actual super-dark dispatcher", () => {
    const layout = Array.from({ length: 60 }, (_, address) => cell(address, address % 10));
    const proof = verifySuperDarkSuffixLayout(layout);

    expect(proof.proved).toBe(false);
    expect(proof.reasons.join("\n")).toMatch(/no super-dark К БП R dispatch cell/u);
  });

  it("rejects entries that would consume a second address cell", () => {
    const layout = Array.from({ length: 60 }, (_, address) => cell(address, address % 10));
    layout.push({ address: 60, opcode: 0x87, roles: ["exec"], tactic: "super-dark indirect dispatch" });
    layout[50] = cell(50, 0x51);
    const proof = verifySuperDarkSuffixLayout(layout);

    expect(proof.proved).toBe(false);
    expect(proof.reasons.join("\n")).toMatch(/FC entry 50 is a two-cell address-taking command/u);
  });

  it("rejects non-executable continuations with a precise reason", () => {
    const layout = Array.from({ length: 60 }, (_, address) => cell(address, address % 10));
    layout.push({ address: 60, opcode: 0x87, roles: ["exec"], tactic: "super-dark indirect dispatch" });
    layout[4] = cell(4, 0x04, ["address"]);
    const proof = verifySuperDarkSuffixLayout(layout);

    expect(proof.proved).toBe(false);
    expect(proof.reasons.join("\n")).toMatch(/FD continuation 4 is not executable/u);
  });
});
