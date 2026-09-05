#pragma once

#include "mkpro/core/passes/helpers.hpp"

#include <functional>

namespace mkpro::core::passes {

std::vector<IrOp> canonicalize_outlining_arithmetic_tails(
    const std::vector<IrOp>& ops, int& groups,
    const std::function<bool(std::size_t)>& accepts_continuation);

PassResult return_suffix_gadget(const std::vector<IrOp>& ops, const PassContext& context);
IrPass return_suffix_gadget_pass();

} // namespace mkpro::core::passes
