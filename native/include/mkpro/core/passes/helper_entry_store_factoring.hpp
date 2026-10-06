#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

// Sink repeated argument stores into a closed helper entry. Every call must
// deliver the same register value in X; calls which already recall that
// register admit an idempotent entry store. This runs before address layout.
PassResult helper_entry_store_factoring(const std::vector<IrOp>& ops,
                                       const PassContext& context);
IrPass helper_entry_store_factoring_pass();

} // namespace mkpro::core::passes
