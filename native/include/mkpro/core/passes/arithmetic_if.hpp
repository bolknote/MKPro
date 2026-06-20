#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult arithmetic_if(const std::vector<IrOp>& ops, const PassContext& context);
IrPass arithmetic_if_pass();

} // namespace mkpro::core::passes
