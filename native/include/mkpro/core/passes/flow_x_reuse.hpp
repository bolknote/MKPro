#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult flow_x_reuse(const std::vector<IrOp>& ops, const PassContext& context);
IrPass flow_x_reuse_pass();
PassResult stack_lift_recall_forwarding(const std::vector<IrOp>& ops,
                                        const PassContext& context);
IrPass stack_lift_recall_forwarding_pass();

} // namespace mkpro::core::passes
