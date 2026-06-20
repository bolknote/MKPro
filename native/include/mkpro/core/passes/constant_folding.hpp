#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult constant_folding(const std::vector<IrOp>& ops, const PassContext& context);
IrPass constant_folding_pass();

} // namespace mkpro::core::passes
