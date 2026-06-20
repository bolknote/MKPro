#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult pre_shift_stack_lift(const std::vector<IrOp>& ops, const PassContext& context);
IrPass pre_shift_stack_lift_pass();

} // namespace mkpro::core::passes
