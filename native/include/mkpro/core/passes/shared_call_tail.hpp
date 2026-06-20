#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult shared_call_tail(const std::vector<IrOp>& ops, const PassContext& context);
IrPass shared_call_tail_pass();

} // namespace mkpro::core::passes
