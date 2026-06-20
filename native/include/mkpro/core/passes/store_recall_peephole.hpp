#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult store_recall_peephole(const std::vector<IrOp>& ops, const PassContext& context);
IrPass store_recall_peephole_pass();

} // namespace mkpro::core::passes
