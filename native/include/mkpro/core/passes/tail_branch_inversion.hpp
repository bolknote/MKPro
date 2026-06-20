#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult tail_branch_inversion(const std::vector<IrOp>& ops, const PassContext& context);
IrPass tail_branch_inversion_pass();

} // namespace mkpro::core::passes
