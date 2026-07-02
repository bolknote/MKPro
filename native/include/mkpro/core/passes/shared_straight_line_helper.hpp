#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult shared_straight_line_helper(const std::vector<IrOp>& ops, const PassContext& context);
IrPass shared_straight_line_helper_pass();

// Callee-hole generalization: extracts one skeleton from straight-line regions
// that are identical except for the target of their single repeated leaf call.
PassResult callee_hole_straight_line_helper(const std::vector<IrOp>& ops,
                                            const PassContext& context);
IrPass callee_hole_straight_line_helper_pass();

} // namespace mkpro::core::passes
