#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult shared_terminal_tail(const std::vector<IrOp>& ops, const PassContext& context);
IrPass shared_terminal_tail_pass();

} // namespace mkpro::core::passes
