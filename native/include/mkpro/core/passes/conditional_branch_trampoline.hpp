#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult conditional_branch_trampoline(const std::vector<IrOp>& ops,
                                         const PassContext& context);
IrPass conditional_branch_trampoline_pass();

} // namespace mkpro::core::passes
