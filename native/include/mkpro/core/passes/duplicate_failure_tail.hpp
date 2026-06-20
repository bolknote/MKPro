#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult duplicate_failure_tail(const std::vector<IrOp>& ops, const PassContext& context);
IrPass duplicate_failure_tail_pass();

} // namespace mkpro::core::passes
