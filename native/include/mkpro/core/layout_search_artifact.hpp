#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/late_bound_decimal_selector.hpp"
#include "mkpro/core/result.hpp"

#include <algorithm>
#include <tuple>
#include <vector>

namespace mkpro::core {

// Program-wide observations, feature profile and setup protocol belong to
// the immutable search root. These are the fields changed by layout edges.
struct LayoutSearchArtifact {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  std::vector<OptimizationReport> optimizations;
};

// A proved permutation still needs its runtime selector digits rebound before
// it can enter another proof boundary. Failure is not an enabling candidate.
inline std::optional<LayoutSearchArtifact> rebind_layout_search_artifact(
    LayoutSearchArtifact artifact,
    const LateBoundDecimalSelectorOptions& options = {
        .minimum_target_address = 0, .maximum_target_address = 99}) {
  auto rebound = rebind_late_bound_decimal_selectors(artifact.items, options);
  if (!rebound.diagnostics.empty())
    return std::nullopt;
  artifact.items = std::move(rebound.items);
  return artifact;
}

inline bool same_layout_search_artifact(const LayoutSearchArtifact& a,
                                        const LayoutSearchArtifact& b) {
  if (a.items.size() != b.items.size() || a.preloads.size() != b.preloads.size())
    return false;
  // Include typed proof/observation metadata, not just bytes or CFG shape.
  // Keep label metadata too; machine_items_equal intentionally compares
  // different subsets of fields for different item kinds.
  const auto item_fields = [](const MachineItem& item) {
    return std::tie(item.kind, item.name, item.procedure_boundary,
                    item.procedure_name, item.hidden, item.opcode,
                    item.mnemonic, item.comment, item.source_line, item.raw,
                    item.roles, item.stop_disposition, item.manual_interaction,
                    item.indirect_flow_targets, item.indirect_flow_formal_targets,
                    item.indirect_memory_targets, item.logical_register_name,
                    item.logical_indirect_memory_targets,
                    item.discarded_indirect_recall_value,
                    item.borrowed_entry_phase_selector,
                    item.semantic_call_origins, item.target, item.formal_opcode);
  };
  const auto preload_fields = [](const PreloadReport& preload) {
    return std::tie(preload.register_name, preload.value,
                    preload.counts_against_program,
                    preload.retunable_natural_fractional_prefix,
                    preload.setup_target_name, preload.setup_expression,
                    preload.setup_expression_text, preload.setup_source_line,
                    preload.lowered_data_value);
  };
  return std::equal(a.items.begin(), a.items.end(), b.items.begin(),
                    [&](const auto& left, const auto& right) {
                      return item_fields(left) == item_fields(right);
                    }) &&
         std::equal(a.preloads.begin(), a.preloads.end(), b.preloads.begin(),
                    [&](const auto& left, const auto& right) {
                      return preload_fields(left) == preload_fields(right);
                    });
}

} // namespace mkpro::core
