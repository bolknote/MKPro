#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult indirect_selector_integer_part(const std::vector<IrOp>& ops, const PassContext& context);
IrPass indirect_selector_integer_part_pass();

} // namespace mkpro::core::passes
