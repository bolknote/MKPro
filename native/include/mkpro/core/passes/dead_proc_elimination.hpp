#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult dead_proc_elimination(const std::vector<IrOp>& ops, const PassContext& context);
IrPass dead_proc_elimination_pass();

} // namespace mkpro::core::passes
