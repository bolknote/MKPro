#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult redundant_prologue_elimination(const std::vector<IrOp>& ops,
                                          const PassContext& context);
IrPass redundant_prologue_elimination_pass();

} // namespace mkpro::core::passes
