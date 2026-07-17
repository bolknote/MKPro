#pragma once

#include "mkpro/core/passes/helpers.hpp"

#include <map>
#include <string>

namespace mkpro::core::passes {

struct RegisterCoalesceMappingOptions {
  // Retained for source compatibility. Definitions are always part of the
  // interference graph because ignoring them can produce a wrong program.
  bool def_aware = false;
  // Only the source-level two-pass allocator can safely update logical
  // register reports and setup preloads after reusing an entry-live anchor.
  bool allow_live_at_entry_anchor_reuse = false;
};

std::map<std::string, std::string> compute_non_overlapping_register_mapping(
    const std::vector<IrOp>& ops,
    RegisterCoalesceMappingOptions options = RegisterCoalesceMappingOptions{});

PassResult register_coalesce(const std::vector<IrOp>& ops, const PassContext& context);
IrPass register_coalesce_pass();

} // namespace mkpro::core::passes
