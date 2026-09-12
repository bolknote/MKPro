#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult shared_straight_line_helper(const std::vector<IrOp>& ops, const PassContext& context);
IrPass shared_straight_line_helper_pass();

// A proved local implementation, not a final machine artifact. Its stack and
// bounded-return contract holds, but the opaque decimal targets still require
// exact final placement in 00..99 and the ordinary final static proof gate.
// Different entry forms are deliberately not dominated by local cell count.
struct CalleeHoleRegionAlternative {
  CalleeHoleRegionChoice choice = CalleeHoleRegionChoice::LocalMinimum;
  PassResult lowering;
  int input_cells = 0;
  int output_cells = 0;
  std::vector<std::string> required_decimal_targets;
};

std::vector<CalleeHoleRegionAlternative> callee_hole_region_alternatives(
    const std::vector<IrOp>& ops, const PassContext& context);

// Callee-hole generalization: extracts one skeleton from straight-line regions
// that are identical except for the target of their single repeated leaf call.
PassResult callee_hole_straight_line_helper(const std::vector<IrOp>& ops,
                                            const PassContext& context);
IrPass callee_hole_straight_line_helper_pass();

} // namespace mkpro::core::passes
