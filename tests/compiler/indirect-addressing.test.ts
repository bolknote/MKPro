import { describe, expect, it } from "vitest";
import {
  IndirectAddressModel,
  evaluateIndirectAddress,
  indirectSelectorMutation,
  isStableIndirectSelector,
  superDarkTarget,
} from "../../src/core/indirect-addressing.ts";

describe("indirect address model", () => {
  it("maps selector registers to the MK-61 mutation groups", () => {
    expect(indirectSelectorMutation("0")).toBe("pre-decrement");
    expect(indirectSelectorMutation("3")).toBe("pre-decrement");
    expect(indirectSelectorMutation("4")).toBe("pre-increment");
    expect(indirectSelectorMutation("6")).toBe("pre-increment");
    expect(indirectSelectorMutation("7")).toBe("stable");
    expect(indirectSelectorMutation("e")).toBe("stable");
    expect(isStableIndirectSelector("9")).toBe(true);
    expect(isStableIndirectSelector("2")).toBe(false);
  });

  it("uses ordinary integer flow targets and two-digit memory targets", () => {
    expect(evaluateIndirectAddress("7", 4, "flow")?.flowTarget).toBe(4);
    expect(evaluateIndirectAddress("7", 123, "flow")?.flowTarget).toBe(23);
    expect(evaluateIndirectAddress("7", 9, "memory")?.memoryTarget).toBe("9");
    expect(evaluateIndirectAddress("7", 10, "memory")?.memoryTarget).toBe("a");
    expect(evaluateIndirectAddress("7", 12, "memory")?.memoryTarget).toBe("c");
    expect(evaluateIndirectAddress("7", 14, "memory")?.memoryTarget).toBe("e");
    expect(evaluateIndirectAddress("7", 15, "memory")?.memoryTarget).toBe("0");
    expect(evaluateIndirectAddress("7", 16, "memory")?.memoryTarget).toBe("0");
    expect(evaluateIndirectAddress("7", 17, "memory")?.memoryTarget).toBe("1");
    expect(evaluateIndirectAddress("7", 23, "memory")?.memoryTarget).toBe("d");
    expect(evaluateIndirectAddress("7", 99, "memory")?.memoryTarget).toBe("3");
    expect(evaluateIndirectAddress("7", 123, "memory")?.memoryTarget).toBe("d");
    expect(evaluateIndirectAddress("7", -1, "memory")?.memoryTarget).toBe("b");
    expect(evaluateIndirectAddress("7", -123, "memory")?.memoryTarget).toBe("d");
  });

  it("uses the same two-digit memory target table for hex-like values", () => {
    expect(evaluateIndirectAddress("7", "0a", "memory")?.memoryTarget).toBe("a");
    expect(evaluateIndirectAddress("7", "0f", "memory")?.memoryTarget).toBe("0");
    expect(evaluateIndirectAddress("7", "1a", "memory")?.memoryTarget).toBe("4");
    expect(evaluateIndirectAddress("7", "ff", "memory")?.memoryTarget).toBe("9");
  });

  it("applies R0..R3 pre-decrement before selecting a target", () => {
    const one = evaluateIndirectAddress("0", 2, "flow");
    const nine = evaluateIndirectAddress("3", 10, "flow");

    expect(one?.flowTarget).toBe(1);
    expect(one?.resultValue).toBe("1");
    expect(nine?.flowTarget).toBe(9);
    expect(evaluateIndirectAddress("0", 0, "flow")?.flowTarget).toBe(99);
    expect(evaluateIndirectAddress("0", 0, "memory")?.memoryTarget).toBe("3");
    expect(evaluateIndirectAddress("0", 0, "memory")?.resultValue).toBe("-99999999");
  });

  it("applies R4..R6 pre-increment before selecting a target", () => {
    const flow = evaluateIndirectAddress("4", 9, "flow");
    expect(flow?.flowTarget).toBe(10);
    expect(flow?.resultValue).toBe("10");
    expect(evaluateIndirectAddress("6", 9, "memory")?.memoryTarget).toBe("a");
  });

  it("models fractional R0 as the sentinel case", () => {
    const flow = evaluateIndirectAddress("0", 0.5, "flow");
    const memory = evaluateIndirectAddress("0", "0,5", "memory");

    expect(flow?.flowTarget).toBe(99);
    expect(flow?.resultValue).toBe("-99999999");
    expect(memory?.memoryTarget).toBe("3");
    expect(memory?.resultValue).toBe("-99999999");
  });

  it("models FA..FF stable-register super-dark targets", () => {
    const fa = evaluateIndirectAddress("7", "FA", "flow");
    const ff = evaluateIndirectAddress("e", "ff", "flow");

    expect(fa?.flowTarget).toBe(0xfa);
    expect(fa?.actualFlowTarget).toBe(48);
    expect(fa?.formalAddress?.kind).toBe("super-dark");
    expect(fa?.superDark).toEqual({ formal: 0xfa, entryAddress: 48, continuationAddress: 1 });
    expect(ff?.superDark).toEqual({ formal: 0xff, entryAddress: 53, continuationAddress: 6 });
    expect(superDarkTarget(0xf9)).toBeUndefined();
  });

  it("maps hex-looking dark addresses to their actual physical entry", () => {
    const c5 = evaluateIndirectAddress("7", "C5", "flow");
    const nineF = evaluateIndirectAddress("7", "9F", "flow");
    const ac = evaluateIndirectAddress("7", "AC", "flow");

    expect(c5?.flowTarget).toBe(0xc5);
    expect(c5?.actualFlowTarget).toBe(13);
    expect(c5?.formalAddress?.kind).toBe("dark");
    expect(nineF?.actualFlowTarget).toBe(0);
    expect(ac?.actualFlowTarget).toBe(0);
  });

  it("exposes the same behavior through the IndirectAddressModel facade", () => {
    expect(IndirectAddressModel.mutationForRegister("7")).toBe("stable");
    expect(IndirectAddressModel.stableSelector("e")).toBe(true);
    expect(IndirectAddressModel.evaluate("7", 12, "flow")?.flowTarget).toBe(12);
    expect(IndirectAddressModel.evaluate("7", "C5", "flow")?.actualFlowTarget).toBe(13);
    expect(IndirectAddressModel.memoryTargetFromTransformed("12")).toBe("c");
    expect(IndirectAddressModel.memoryTargetFromTransformed("ff")).toBe("9");
    expect(IndirectAddressModel.superDarkTarget(0xfa)?.entryAddress).toBe(48);
  });
});
