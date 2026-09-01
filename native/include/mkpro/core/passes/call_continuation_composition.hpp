#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult call_continuation_composition(const std::vector<IrOp>& ops,
                                         const PassContext& context);
IrPass call_continuation_composition_pass();

} // namespace mkpro::core::passes
