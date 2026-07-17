#pragma once

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"

#include <map>
#include <optional>
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

struct PrecoloredRegisterAllocationOptions {
  int color_count = 15;
  std::map<std::string, int> fixed_colors;
  std::map<std::string, int> preferred_colors;
};

// Exact DSATUR coloring for the source-level allocator. Fixed nodes model raw
// hardware-register uses and layout contracts; preferred colors only stabilize
// listings and never weaken the interference proof.
std::optional<std::map<std::string, int>> color_precolored_register_graph(
    const RegisterInterferenceGraph& graph,
    const PrecoloredRegisterAllocationOptions& options);

PassResult register_coalesce(const std::vector<IrOp>& ops, const PassContext& context);
IrPass register_coalesce_pass();

} // namespace mkpro::core::passes
