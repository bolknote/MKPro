#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult jump_thread(const std::vector<IrOp>& ops, const PassContext& context);
IrPass jump_thread_pass();

} // namespace mkpro::core::passes
