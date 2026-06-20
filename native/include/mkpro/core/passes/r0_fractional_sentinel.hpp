#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult r0_fractional_sentinel(const std::vector<IrOp>& ops, const PassContext& context);
IrPass r0_fractional_sentinel_pass();

} // namespace mkpro::core::passes
