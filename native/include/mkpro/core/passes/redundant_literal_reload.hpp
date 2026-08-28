#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult redundant_literal_reload(const std::vector<IrOp>& ops,
                                    const PassContext& context);
IrPass redundant_literal_reload_pass();

} // namespace mkpro::core::passes
