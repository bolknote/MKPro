#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult branch_target_x_reuse(const std::vector<IrOp>& ops, const PassContext& context);
IrPass branch_target_x_reuse_pass();

} // namespace mkpro::core::passes
