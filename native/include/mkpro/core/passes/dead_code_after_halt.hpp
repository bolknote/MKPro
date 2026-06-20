#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult dead_code_after_halt(const std::vector<IrOp>& ops, const PassContext& context);
IrPass dead_code_after_halt_pass();

} // namespace mkpro::core::passes
