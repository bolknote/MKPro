#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult stable_indirect_flow(const std::vector<IrOp>& ops, const PassContext& context);
IrPass stable_indirect_flow_pass();

PassResult indirect_memory_table(const std::vector<IrOp>& ops, const PassContext& context);
IrPass indirect_memory_table_pass();

} // namespace mkpro::core::passes
