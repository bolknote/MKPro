#include "mkpro/core/passes/vp_splice.hpp"

#include "mkpro/core/passes/helpers.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mkpro::core::passes {

namespace {

// Mirrors vp-splice.ts: the candidate ordering used to greedily apply splices.
const std::vector<std::string> kStageOrder = {
    "duplicate-vp",
    "proved-vp",
    "exponent-boundary",
    "hard-overwrite-terminal",
    "sign-pair-before-fresh-digit",
    "fresh-digit-terminal",
    "closed-sign-pair",
};

const std::vector<std::string> kSourceMatchReasonOrder = {
    "same-exponent-context", "active-mantissa-source", "entry-source",
    "explicit-sign-source",  "nonzero-sign-source",    "source-mismatch",
};

const std::vector<std::string> kSignRestoreSourceProofReasonOrder = {
    "shape-transition",  "source-match-explicit-sign", "source-match-nonzero-sign",
    "shared-sign-source", "no-sign-restore-source",
};

int stage_rank(const std::string& stage) {
  for (std::size_t i = 0; i < kStageOrder.size(); ++i) {
    if (kStageOrder.at(i) == stage)
      return static_cast<int>(i);
  }
  return static_cast<int>(kStageOrder.size());
}

std::optional<int> ranked_index(const std::vector<std::string>& order,
                                const std::optional<std::string>& value) {
  if (!value.has_value())
    return std::nullopt;
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (order.at(i) == *value)
      return static_cast<int>(i);
  }
  return static_cast<int>(order.size());
}

int compare_optional_ranks(const std::optional<int>& left, const std::optional<int>& right) {
  if (!left.has_value() || !right.has_value())
    return 0;
  return *left - *right;
}

int compare_source_proof_reason(const X2VpSpliceCandidate& left, const X2VpSpliceCandidate& right) {
  const int source_diff =
      compare_optional_ranks(ranked_index(kSourceMatchReasonOrder, left.source_match_reason),
                             ranked_index(kSourceMatchReasonOrder, right.source_match_reason));
  if (source_diff != 0)
    return source_diff;
  return compare_optional_ranks(
      ranked_index(kSignRestoreSourceProofReasonOrder, left.sign_restore_source_proof_reason),
      ranked_index(kSignRestoreSourceProofReasonOrder, right.sign_restore_source_proof_reason));
}

int first_removable_index(const std::vector<int>& removable) {
  return *std::min_element(removable.begin(), removable.end());
}

int last_removable_index(const std::vector<int>& removable) {
  return *std::max_element(removable.begin(), removable.end());
}

// Faithful port of vp-splice.ts compareCandidates.
int compare_candidates(const X2VpSpliceCandidate& left, const X2VpSpliceCandidate& right,
                       int left_start, int right_start) {
  const int first_diff =
      first_removable_index(left.removable_indexes) - first_removable_index(right.removable_indexes);
  if (first_diff != 0)
    return first_diff;

  const int last_diff =
      last_removable_index(right.removable_indexes) - last_removable_index(left.removable_indexes);
  if (last_diff != 0)
    return last_diff;

  const int length_diff = static_cast<int>(right.removable_indexes.size()) -
                          static_cast<int>(left.removable_indexes.size());
  if (length_diff != 0)
    return length_diff;

  const int source_diff = compare_source_proof_reason(left, right);
  if (source_diff != 0)
    return source_diff;

  const int stage_diff = stage_rank(left.stage) - stage_rank(right.stage);
  if (stage_diff != 0)
    return stage_diff;

  return left_start - right_start;
}

struct VpSpliceSelection {
  int start_index = 0;
  X2VpSpliceCandidate candidate;
};

std::string stage_counts_detail(const std::map<std::string, int>& stage_counts) {
  std::vector<std::string> parts;
  for (const std::string& stage : kStageOrder) {
    const auto it = stage_counts.find(stage);
    if (it != stage_counts.end())
      parts.push_back(stage + "=" + std::to_string(it->second));
  }
  if (parts.empty())
    return "";
  std::string joined;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0)
      joined += ", ";
    joined += parts.at(i);
  }
  return " Stages: " + joined + ".";
}

} // namespace

PassResult vp_splice(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  if (ops.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const std::vector<std::optional<X2ValueDataflowState>> x2_value_states =
      compute_x2_value_states(ops, X2ValueStatesOptions{.track_register_memory = true});
  const DirectReturnAnalysisContext analysis_context = direct_return_analysis_context(ops);

  X2VpSplicePlannerOptions planner_options;
  planner_options.is_decimal_digit = [](const IrOp& op, int) {
    return op.kind == IrKind::Plain && op.opcode >= 0 && op.opcode <= 9 &&
           !has_rewrite_barrier(op) && !is_display_focus_sensitive(op);
  };
  planner_options.is_hard_x2_overwrite_without_stack_use = [](const IrOp& op, int) {
    return analyze_x2_stack_effect(op).hard_x2_overwrite_without_stack_use;
  };

  std::vector<VpSpliceSelection> candidates;
  for (int i = 1; i < static_cast<int>(ops.size()); ++i) {
    std::vector<X2VpSpliceCandidate> planned =
        x2_plan_vp_splice_candidates_at(ops, i, x2_value_states, analysis_context, planner_options);
    for (X2VpSpliceCandidate& candidate : planned) {
      if (candidate.removable_indexes.empty())
        continue;
      candidates.push_back(VpSpliceSelection{i, std::move(candidate)});
    }
  }

  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const VpSpliceSelection& left, const VpSpliceSelection& right) {
                     return compare_candidates(left.candidate, right.candidate, left.start_index,
                                               right.start_index) < 0;
                   });

  std::set<int> remove;
  std::map<std::string, int> stage_counts;
  for (const VpSpliceSelection& selection : candidates) {
    const std::vector<int>& removable = selection.candidate.removable_indexes;
    const bool can_apply =
        !removable.empty() &&
        std::none_of(removable.begin(), removable.end(),
                     [&](int index) { return remove.find(index) != remove.end(); });
    if (!can_apply)
      continue;
    for (int index : removable)
      remove.insert(index);
    stage_counts[selection.candidate.stage] += static_cast<int>(removable.size());
  }

  if (remove.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size() - remove.size());
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    if (remove.find(index) == remove.end())
      result.push_back(ops.at(static_cast<std::size_t>(index)));
  }

  const std::string detail =
      "Collapsed " + std::to_string(remove.size()) +
      " redundant ВП/empty/sign cell(s) around an X2 boundary (active-entry ВП ВП -> ВП, "
      "КНОП/К1/К2 ВП -> ВП, exponent-digit empty separators, VP-context /-/ separators/signs "
      "before fresh digits/dead overwrites, exponent /-/ /-/ -> empty, mantissa /-/ and empty "
      "restore runs before proved ВП/fresh digit -> empty, closed value /-/ /-/ -> empty)." +
      stage_counts_detail(stage_counts);

  return PassResult{
      .ops = std::move(result),
      .applied = static_cast<int>(remove.size()),
      .optimizations =
          {
              AppliedOptimization{
                  .name = "vp-exponent-splice",
                  .detail = detail,
              },
          },
  };
}

IrPass vp_splice_pass() {
  return IrPass{
      .name = "vp-splice",
      .run = vp_splice,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
