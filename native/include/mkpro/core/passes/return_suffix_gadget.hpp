#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult return_suffix_gadget(const std::vector<IrOp>& ops, const PassContext& context);
IrPass return_suffix_gadget_pass();

} // namespace mkpro::core::passes
