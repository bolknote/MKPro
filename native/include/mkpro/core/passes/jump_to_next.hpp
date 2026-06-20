#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult jump_to_next_threading(const std::vector<IrOp>& ops, const PassContext& context);
IrPass jump_to_next_threading_pass();

} // namespace mkpro::core::passes
