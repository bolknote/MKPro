#include "mkpro/core/passes/outline.hpp"

#include "mkpro/core/passes/helpers.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <string>
#include <utility>
#include <variant>

namespace mkpro::core::passes {

LabelAllocator::LabelAllocator(const std::vector<IrOp>& ops, std::string prefix)
    : prefix_(std::move(prefix)) {
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label)
      existing_.insert(op.name);
  }
}

std::string LabelAllocator::next() {
  int suffix = counter_;
  while (existing_.contains(prefix_ + std::to_string(suffix)))
    ++suffix;
  counter_ = suffix + 1;
  std::string label = prefix_ + std::to_string(suffix);
  existing_.insert(label);
  return label;
}

std::string target_key(const IrTarget& target) {
  if (const auto* numeric = std::get_if<int>(&target))
    return "#" + std::to_string(*numeric);
  return std::get<std::string>(target);
}

bool has_numeric_outline_flow_target(const std::vector<IrOp>& ops) {
  for (const IrOp& op : ops) {
    if ((op.kind == IrKind::Jump || op.kind == IrKind::CondJump || op.kind == IrKind::Call ||
         op.kind == IrKind::Loop) &&
        std::holds_alternative<int>(op.target)) {
      return true;
    }
  }
  return false;
}

bool range_intersects(const std::set<int>& indexes, int start, int end) {
  for (int index = start; index <= end; ++index) {
    if (indexes.contains(index))
      return true;
  }
  return false;
}

void mark_range(std::set<int>& indexes, int start, int end) {
  for (int index = start; index <= end; ++index)
    indexes.insert(index);
}

std::vector<OutlineCandidate> collect_suffix_candidates(const std::vector<IrOp>& ops,
                                                        const SuffixCollectionConfig& config) {
  std::map<std::string, std::vector<OutlineOccurrence>> by_key;

  for (int end = 0; end < static_cast<int>(ops.size()); ++end) {
    const IrOp& final = ops.at(static_cast<std::size_t>(end));
    if (!config.is_terminal(final))
      continue;

    std::vector<std::string> parts = {config.op_key(final)};
    int cells = cells_per_op(final);
    for (int start = end - 1; start >= 0; --start) {
      const IrOp& op = ops.at(static_cast<std::size_t>(start));
      if (!config.is_body_op(op))
        break;

      parts.insert(parts.begin(), config.op_key(op));
      cells += cells_per_op(op);
      if (cells <= 2)
        continue;

      std::string key;
      for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0)
          key += '\n';
        key += parts.at(index);
      }
      by_key[key].push_back(OutlineOccurrence{
          .key = key,
          .start = start,
          .end = end,
          .cells = cells,
      });
    }
  }

  std::vector<OutlineCandidate> result;
  result.reserve(by_key.size());
  for (auto& [key, occurrences] : by_key) {
    const int cells = occurrences.empty() ? 0 : occurrences.front().cells;
    result.push_back(OutlineCandidate{
        .key = std::move(key),
        .occurrences = std::move(occurrences),
        .cells = cells,
    });
  }
  return result;
}

std::vector<SelectedOutline> select_shared_suffixes(
    const std::vector<OutlineCandidate>& candidates, LabelAllocator& labels,
    std::set<int>& protected_indexes) {
  std::vector<SelectedOutline> selected;
  std::vector<OutlineCandidate> ordered = candidates;
  std::sort(ordered.begin(), ordered.end(), [](const OutlineCandidate& left,
                                               const OutlineCandidate& right) {
    const int left_savings =
        (static_cast<int>(left.occurrences.size()) - 1) * (left.cells - 2);
    const int right_savings =
        (static_cast<int>(right.occurrences.size()) - 1) * (right.cells - 2);
    if (right_savings != left_savings)
      return right_savings < left_savings;
    if (right.cells != left.cells)
      return right.cells < left.cells;
    return left.key < right.key;
  });

  for (const OutlineCandidate& candidate : ordered) {
    std::vector<OutlineOccurrence> available;
    for (const OutlineOccurrence& occurrence : candidate.occurrences) {
      if (!range_intersects(protected_indexes, occurrence.start, occurrence.end))
        available.push_back(occurrence);
    }
    if (available.size() < 2)
      continue;

    const OutlineOccurrence target = available.front();
    std::vector<OutlineOccurrence> replacements(available.begin() + 1, available.end());
    const int saved_cells = std::accumulate(
        replacements.begin(), replacements.end(), 0,
        [](int sum, const OutlineOccurrence& occurrence) { return sum + occurrence.cells - 2; });
    if (saved_cells <= 0)
      continue;

    selected.push_back(SelectedOutline{
        .label = labels.next(),
        .target = target,
        .replacements = replacements,
    });

    mark_range(protected_indexes, target.start, target.end);
    for (const OutlineOccurrence& replacement : replacements)
      mark_range(protected_indexes, replacement.start, replacement.end);
  }

  return selected;
}

} // namespace mkpro::core::passes
