#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult return_zero_jump(const std::vector<IrOp>& ops, const PassContext& context);
IrPass return_zero_jump_pass();

} // namespace mkpro::core::passes
