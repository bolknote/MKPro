#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

// Reuse a producer's selector-equivalent value before layout. The numerical
// value need not be equal, but the first admitted indirect access must make
// the two register states exactly equal before any ordinary observation.
PassResult indirect_selector_seed_reuse(const std::vector<IrOp>& ops,
                                        const PassContext& context);
IrPass indirect_selector_seed_reuse_pass();

} // namespace mkpro::core::passes
