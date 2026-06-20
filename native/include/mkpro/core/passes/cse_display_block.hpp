#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult cse_display_block(const std::vector<IrOp>& ops, const PassContext& context);
IrPass cse_display_block_pass();

} // namespace mkpro::core::passes
