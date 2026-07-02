#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult return_trampoline(const std::vector<IrOp>& ops, const PassContext& context);
IrPass return_trampoline_pass();

} // namespace mkpro::core::passes
