#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult early_exact_stack_dead_store_elimination(const std::vector<IrOp>& ops,
                                                    const PassContext& context);
PassResult dead_store_elimination(const std::vector<IrOp>& ops, const PassContext& context);
PassResult finalization_dead_store_elimination(const std::vector<IrOp>& ops,
                                               const PassContext& context);
IrPass early_exact_stack_dead_store_elimination_pass();
IrPass dead_store_elimination_pass();

} // namespace mkpro::core::passes
