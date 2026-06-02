import { describe, expect, it } from "vitest";
import {
  DEFAULT_BOARD_WIDTH,
  cellMaskExpression,
  cellMaskRowConstant,
  gridNormExpression,
  numberExpression,
  positiveGridNormExpression,
} from "../../src/core/emit/lowering-helpers.ts";

// Group 3 generalization: the square-board macros are width-parametric. The
// coordinate wrap (`% width`) and diagonal fold (`+ width`) are exactly
// derivable, so they take a width that defaults to the original 4-wide grid
// (byte-for-byte). The fractional cell-mask constant is hardware-fitted per
// width, so it lives in a verified table and is the documented limit on
// supporting other widths.
describe("square-board macros are width-parametric", () => {
  const operand = numberExpression(5);

  it("defaults to the original 4-wide grid", () => {
    expect(DEFAULT_BOARD_WIDTH).toBe(4);
    // The default lowering is identical to explicitly passing width 4, so no
    // existing 4x4 board moves.
    expect(gridNormExpression(operand)).toEqual(gridNormExpression(operand, 4));
    expect(positiveGridNormExpression(operand)).toEqual(positiveGridNormExpression(operand, 4));
    expect(cellMaskExpression(operand, operand)).toEqual(cellMaskExpression(operand, operand, 4));
  });

  it("wraps coordinates modulo the requested width", () => {
    // width 3 must thread `3` through the int/frac wrap divisor and the
    // out-of-range fold; width 4 must thread `4`. Differing widths produce
    // structurally different expressions.
    const wrap3 = JSON.stringify(gridNormExpression(operand, 3));
    const wrap4 = JSON.stringify(gridNormExpression(operand, 4));
    expect(wrap3).toContain('"raw":"3"');
    expect(wrap3).not.toContain('"raw":"4"');
    expect(wrap4).toContain('"raw":"4"');
    expect(wrap3).not.toBe(wrap4);

    const diag3 = JSON.stringify(positiveGridNormExpression(operand, 3));
    expect(diag3).toContain('"raw":"3"');
    expect(diag3).not.toContain('"raw":"4"');
  });

  it("keeps the hardware-verified cell-mask constant keyed by width", () => {
    expect(cellMaskRowConstant(4)).toBe(0.22600029);
    // Other widths need an on-hardware-verified fractional constant; the
    // generator refuses to fabricate one rather than miscompile silently.
    expect(() => cellMaskRowConstant(3)).toThrow(/hardware-verified/u);
    expect(() => cellMaskExpression(operand, operand, 5)).toThrow(/hardware-verified/u);
  });
});
