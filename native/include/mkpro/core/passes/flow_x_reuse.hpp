#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult flow_x_reuse(const std::vector<IrOp>& ops, const PassContext& context);
IrPass flow_x_reuse_pass();

} // namespace mkpro::core::passes
