#pragma once

#include "mkpro/core/passes/helpers.hpp"

#include <map>
#include <string>

namespace mkpro::core::passes {

struct RegisterCoalesceMappingOptions {
  bool def_aware = false;
};

std::map<std::string, std::string> compute_non_overlapping_register_mapping(
    const std::vector<IrOp>& ops,
    RegisterCoalesceMappingOptions options = RegisterCoalesceMappingOptions{});

PassResult register_coalesce(const std::vector<IrOp>& ops, const PassContext& context);
IrPass register_coalesce_pass();

} // namespace mkpro::core::passes
