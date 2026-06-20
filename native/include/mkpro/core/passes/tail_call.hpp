#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult tail_call_lowering(const std::vector<IrOp>& ops, const PassContext& context);
IrPass tail_call_lowering_pass();

} // namespace mkpro::core::passes
