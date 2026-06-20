#pragma once

#include "mkpro/core/ast.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mkpro::core::emit {

enum class StackResidentRestoreOp {
  Swap,
  Reverse,
};

struct StackResidentTempSegment {
  V2Statement assign;
  std::vector<V2Statement> preserve_after;
};

struct StackResidentFusionSite {
  std::vector<StackResidentTempSegment> temps;
  V2Statement consumer;
  std::size_t consumer_index = 0;
  bool crosses_control_flow = false;
};

struct StackResidencySummary {
  int max_live_temps = 0;
  int fusion_sites = 0;
  int control_flow_fusions = 0;
  int single_use_pairs = 0;
  int indexed_consumers = 0;
};

bool expression_references_identifier(const Expression& expression, const std::string& name);
int count_identifier_reads(const Expression& expression, const std::string& name);
bool can_lower_stack_resident_expression(const Expression& expression,
                                         const std::vector<std::string>& temps);
std::vector<StackResidentRestoreOp> stack_resident_restore_ops(std::size_t temp_index,
                                                               std::size_t temp_count);
bool statement_preserves_stack_residency(const V2Statement& statement,
                                         const std::set<std::string>& protected_temps);
std::optional<StackResidentFusionSite> find_stack_resident_fusion_site(
    const std::vector<V2Statement>& statements, std::size_t start);
StackResidencySummary summarize_stack_residency_candidates_in_block(
    const std::vector<V2Statement>& statements);

} // namespace mkpro::core::emit
