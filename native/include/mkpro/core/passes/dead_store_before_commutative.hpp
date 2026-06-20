#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult dead_store_before_commutative(const std::vector<IrOp>& ops,
                                         const PassContext& context);
IrPass dead_store_before_commutative_pass();

} // namespace mkpro::core::passes
