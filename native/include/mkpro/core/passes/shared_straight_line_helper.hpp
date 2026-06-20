#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult shared_straight_line_helper(const std::vector<IrOp>& ops, const PassContext& context);
IrPass shared_straight_line_helper_pass();

} // namespace mkpro::core::passes
