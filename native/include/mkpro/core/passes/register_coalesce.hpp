#pragma once

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>

namespace mkpro::core::passes {

struct RegisterCoalesceMappingOptions {
  // Retained for source compatibility. Definitions are always part of the
  // interference graph because ignoring them can produce a wrong program.
  bool def_aware = false;
  // Only the source-level two-pass allocator can safely update logical
  // register reports and setup preloads after reusing an entry-live anchor.
  bool allow_live_at_entry_anchor_reuse = false;
  // Physical registers reserved by source allocation but absent from the
  // emitted body. Including them lets the two-pass allocator prove that a
  // dead allocation can be reclaimed without weakening ordinary IR passes.
  std::set<std::string> allocated_registers;
};

std::map<std::string, std::string> compute_non_overlapping_register_mapping(
    const std::vector<IrOp>& ops,
    RegisterCoalesceMappingOptions options = RegisterCoalesceMappingOptions{});

struct PrecoloredRegisterAllocationOptions {
  int color_count = 15;
  std::map<std::string, int> fixed_colors;
  std::map<std::string, int> preferred_colors;
  // A speculative optimization needs only a valid witness, not a proof that
  // no coloring exists. In this mode failure is inconclusive and never starts
  // the exponential fallback search.
  bool greedy_only = false;
  // Omission means every target register is admissible. An explicitly empty
  // domain is unsatisfiable, including for an otherwise valid fixed color.
  std::map<std::string, std::set<int>> allowed_colors;
  // Optional MRV ordering for bounded speculative list coloring. The default
  // exact/legacy DSATUR ordering is unchanged.
  bool prioritize_constrained_domains = false;
};

// Cost-preserving domains for already lowered logical FL/indirect operations.
// Unknown/inconsistent identities and opcode-F selector aliases fail closed.
// Domains do not replace source regeneration or the final interference proof.
std::optional<std::map<std::string, std::set<int>>>
logical_register_instruction_class_domains(const std::vector<IrOp>& ops);

// Exact DSATUR coloring for the source-level allocator. Fixed nodes model raw
// hardware-register uses and layout contracts; preferred colors only stabilize
// listings and never weaken the interference proof.
std::optional<std::map<std::string, int>> color_precolored_register_graph(
    const RegisterInterferenceGraph& graph,
    const PrecoloredRegisterAllocationOptions& options);

PassResult register_coalesce(const std::vector<IrOp>& ops, const PassContext& context);
// Coalesce reaching-definition webs, not all values ever stored in a register.
// Entry values and observable hardware webs retain their physical homes;
// ordinary FL counter epochs may use any of the four counter registers.
PassResult register_web_copy_coalesce(const std::vector<IrOp>& ops,
                                      const PassContext& context);
IrPass register_web_copy_coalesce_pass();
IrPass register_coalesce_pass();

} // namespace mkpro::core::passes
