#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult entry_stack_input_reuse(const std::vector<IrOp>& ops, const PassContext& context);
IrPass entry_stack_input_reuse_pass();

} // namespace mkpro::core::passes
