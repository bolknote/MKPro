#include "mkpro/core/passes/shared_straight_line_helper.hpp"

#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/passes/outline.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

constexpr int kMinBodyCells = 4;
constexpr int kMinEntryCells = 3;

const std::vector<std::string> kGeneratedBodyLabelPrefixes = {
    "__return_suffix_gadget_",
    "__shared_terminal_tail_",
    "__shared_straight_line_helper_",
};

struct Occurrence {
  std::string key;
  int start = 0;
  int end = 0;
  int cells = 0;
};

struct Candidate {
  std::string key;
  std::vector<Occurrence> occurrences;
  int cells = 0;
};

struct SelectedHelperEntry {
  std::string label;
  int offset = 0;
  std::vector<Occurrence> replacements;
  int cells = 0;
};

struct SelectedHelper {
  std::string label;
  std::vector<IrOp> body;
  std::vector<Occurrence> occurrences;
  int cells = 0;
  std::vector<SelectedHelperEntry> entries;
};

bool string_target_exists(const IrTarget& target) {
  return std::holds_alternative<std::string>(target);
}

std::string call_target_key(const IrTarget& target) {
  if (const auto* text = std::get_if<std::string>(&target))
    return *text;
  return std::to_string(std::get<int>(target));
}

bool is_shareable_body_op(const IrOp& op, bool allow_direct_calls = false) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  switch (op.kind) {
  case IrKind::Store:
  case IrKind::Recall:
  case IrKind::IndirectStore:
  case IrKind::IndirectRecall:
  case IrKind::Plain:
    return true;
  case IrKind::Call:
    return allow_direct_calls && string_target_exists(op.target);
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Loop:
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump:
  case IrKind::Return:
  case IrKind::Stop:
  case IrKind::OrphanAddress:
    return false;
  }
  return false;
}

bool ends_straight_line_segment(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Loop:
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump:
  case IrKind::Return:
  case IrKind::Stop:
  case IrKind::OrphanAddress:
    return true;
  case IrKind::Label:
  case IrKind::Store:
  case IrKind::Recall:
  case IrKind::IndirectStore:
  case IrKind::IndirectRecall:
  case IrKind::Plain:
    return false;
  }
  return false;
}

std::set<int> generated_body_indexes(const std::vector<IrOp>& ops, bool allow_direct_calls) {
  std::set<int> indexes;
  bool protect = false;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label) {
      protect = std::any_of(kGeneratedBodyLabelPrefixes.begin(), kGeneratedBodyLabelPrefixes.end(),
                            [&](const std::string& prefix) {
                              return op.name.starts_with(prefix);
                            });
      continue;
    }
    if (protect)
      indexes.insert(index);
    if (!is_shareable_body_op(op, allow_direct_calls))
      protect = false;
  }
  return indexes;
}

std::set<int> display_sensitive_block_indexes(const std::vector<IrOp>& ops) {
  std::set<int> indexes;
  std::vector<int> segment;
  bool segment_is_sensitive = false;

  auto flush = [&]() {
    if (segment_is_sensitive) {
      for (int index : segment)
        indexes.insert(index);
    }
    segment.clear();
    segment_is_sensitive = false;
  };

  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label) {
      flush();
      continue;
    }
    segment.push_back(index);
    if (is_display_focus_sensitive(op))
      segment_is_sensitive = true;
    if (ends_straight_line_segment(op))
      flush();
  }
  flush();
  return indexes;
}

std::optional<int> next_executable_op_index(const std::vector<IrOp>& ops, int start) {
  return next_executable_index(ops, start);
}

bool starts_at_x2_restore(const std::vector<IrOp>& ops, int start) {
  return start >= 0 && start < static_cast<int>(ops.size()) &&
         is_x2_restore_op(ops.at(static_cast<std::size_t>(start)));
}

bool starts_after_x2_restore(const std::vector<IrOp>& ops, int start) {
  if (start <= 0 || start >= static_cast<int>(ops.size()))
    return false;
  const IrOp& previous = ops.at(static_cast<std::size_t>(start - 1));
  const IrOp& current = ops.at(static_cast<std::size_t>(start));
  return is_x2_restore_op(previous) && !is_x2_affecting_op(current);
}

bool ends_before_x2_restore(const std::vector<IrOp>& ops, int end) {
  const std::optional<int> next = next_executable_op_index(ops, end + 1);
  return next.has_value() && is_x2_restore_op(ops.at(static_cast<std::size_t>(*next)));
}

X2Effect x2_effect_for_boundary(const IrOp& op) {
  if (op.kind == IrKind::Label)
    return X2Effect::Preserves;
  return opcode_by_code(op.opcode).x2_effect;
}

bool x2_restore_boundaries_are_internal(const std::vector<IrOp>& ops, int start, int end) {
  for (int index = start; index <= end; ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (!is_x2_restore_op(op))
      continue;
    if (index == start)
      return false;
    const IrOp& previous = ops.at(static_cast<std::size_t>(index - 1));
    const X2Effect previous_effect = x2_effect_for_boundary(previous);
    if (previous_effect != X2Effect::Affects && previous_effect != X2Effect::Restores)
      return false;
  }
  return true;
}

std::string op_key(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Store:
    return "store:" + op.register_name;
  case IrKind::Recall:
    return "recall:" + op.register_name;
  case IrKind::IndirectStore:
    return "indirect-store:" + op.register_name;
  case IrKind::IndirectRecall:
    return "indirect-recall:" + op.register_name;
  case IrKind::Plain:
    return "plain:" + std::to_string(op.opcode);
  case IrKind::Call:
    return "call:" + call_target_key(op.target);
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Loop:
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump:
  case IrKind::Return:
  case IrKind::Stop:
  case IrKind::OrphanAddress:
    return ir_kind_name(op.kind);
  }
  return {};
}

std::string join_parts(const std::vector<std::string>& parts, std::size_t start = 0) {
  std::string key;
  for (std::size_t index = start; index < parts.size(); ++index) {
    if (!key.empty())
      key += '\n';
    key += parts.at(index);
  }
  return key;
}

std::vector<Candidate> collect_candidates(const std::vector<IrOp>& ops, bool allow_direct_calls) {
  std::map<std::string, std::vector<Occurrence>> by_key;
  std::set<int> protected_indexes = generated_body_indexes(ops, allow_direct_calls);
  const std::set<int> display_indexes = display_sensitive_block_indexes(ops);
  protected_indexes.insert(display_indexes.begin(), display_indexes.end());

  for (int start = 0; start < static_cast<int>(ops.size()); ++start) {
    if (protected_indexes.contains(start))
      continue;
    if (starts_at_x2_restore(ops, start))
      continue;
    if (starts_after_x2_restore(ops, start))
      continue;
    std::vector<std::string> parts;
    int cells = 0;
    for (int end = start; end < static_cast<int>(ops.size()); ++end) {
      if (protected_indexes.contains(end))
        break;
      const IrOp& op = ops.at(static_cast<std::size_t>(end));
      if (!is_shareable_body_op(op, allow_direct_calls))
        break;
      parts.push_back(op_key(op));
      cells += cells_per_op(op);
      if (cells < kMinEntryCells)
        continue;
      if (ends_before_x2_restore(ops, end))
        continue;
      if (!x2_restore_boundaries_are_internal(ops, start, end))
        continue;
      const std::string key = join_parts(parts);
      by_key[key].push_back(Occurrence{
          .key = key,
          .start = start,
          .end = end,
          .cells = cells,
      });
    }
  }

  std::vector<Candidate> result;
  result.reserve(by_key.size());
  for (auto& [key, occurrences] : by_key) {
    const int cells = occurrences.empty() ? 0 : occurrences.front().cells;
    result.push_back(Candidate{
        .key = std::move(key),
        .occurrences = std::move(occurrences),
        .cells = cells,
    });
  }
  return result;
}

int net_savings(int occurrences, int cells) {
  return occurrences * cells - (occurrences * 2 + cells + 1);
}

bool ranges_overlap(int left_start, int left_end, int right_start, int right_end) {
  return left_start <= right_end && right_start <= left_end;
}

void select_internal_entries(std::vector<SelectedHelper>& helpers,
                             const std::vector<Candidate>& candidates, LabelAllocator& labels,
                             std::set<int>& protected_indexes) {
  std::map<std::string, Candidate> candidate_by_key;
  for (const Candidate& candidate : candidates)
    candidate_by_key[candidate.key] = candidate;

  struct InternalEntryCandidate {
    SelectedHelper* helper = nullptr;
    int offset = 0;
    std::string key;
    int cells = 0;
    std::vector<Occurrence> occurrences;
  };
  std::vector<InternalEntryCandidate> entry_candidates;

  for (SelectedHelper& helper : helpers) {
    std::vector<std::string> parts;
    std::vector<int> suffix_cells;
    parts.reserve(helper.body.size());
    suffix_cells.reserve(helper.body.size());
    for (const IrOp& op : helper.body) {
      parts.push_back(op_key(op));
      suffix_cells.push_back(cells_per_op(op));
    }
    for (int offset = 1; offset < static_cast<int>(helper.body.size()); ++offset) {
      const int cells =
          std::accumulate(suffix_cells.begin() + offset, suffix_cells.end(), 0);
      if (cells < kMinEntryCells)
        continue;
      const std::string key = join_parts(parts, static_cast<std::size_t>(offset));
      const auto candidate = candidate_by_key.find(key);
      if (candidate == candidate_by_key.end())
        continue;
      entry_candidates.push_back(InternalEntryCandidate{
          .helper = &helper,
          .offset = offset,
          .key = key,
          .cells = cells,
          .occurrences = candidate->second.occurrences,
      });
    }
  }

  std::sort(entry_candidates.begin(), entry_candidates.end(),
            [](const InternalEntryCandidate& left, const InternalEntryCandidate& right) {
              const int left_savings = static_cast<int>(left.occurrences.size()) * (left.cells - 2);
              const int right_savings =
                  static_cast<int>(right.occurrences.size()) * (right.cells - 2);
              if (right_savings != left_savings)
                return right_savings < left_savings;
              if (right.cells != left.cells)
                return right.cells < left.cells;
              return left.key < right.key;
            });

  for (const InternalEntryCandidate& candidate : entry_candidates) {
    if (candidate.cells <= 2 || candidate.helper == nullptr)
      continue;
    std::vector<Occurrence> replacements;
    for (const Occurrence& occurrence : candidate.occurrences) {
      if (!range_intersects(protected_indexes, occurrence.start, occurrence.end))
        replacements.push_back(occurrence);
    }
    if (replacements.empty())
      continue;

    candidate.helper->entries.push_back(SelectedHelperEntry{
        .label = labels.next(),
        .offset = candidate.offset,
        .replacements = replacements,
        .cells = candidate.cells,
    });
    for (const Occurrence& replacement : replacements)
      mark_range(protected_indexes, replacement.start, replacement.end);
  }
}

void select_anchored_internal_entry_helpers(std::vector<SelectedHelper>& selected,
                                            const std::vector<Candidate>& candidates,
                                            const std::vector<IrOp>& ops, LabelAllocator& labels,
                                            std::set<int>& protected_indexes) {
  std::map<std::string, Candidate> candidate_by_key;
  for (const Candidate& candidate : candidates)
    candidate_by_key[candidate.key] = candidate;

  struct AnchoredInternalEntryCandidate {
    Candidate body_candidate;
    Occurrence anchor;
    int offset = 0;
    std::string key;
    int cells = 0;
    std::vector<Occurrence> replacements;
    int savings = 0;
  };
  std::vector<AnchoredInternalEntryCandidate> entry_candidates;

  for (const Candidate& body_candidate : candidates) {
    if (body_candidate.cells < kMinBodyCells)
      continue;
    for (const Occurrence& anchor : body_candidate.occurrences) {
      if (range_intersects(protected_indexes, anchor.start, anchor.end))
        continue;
      std::vector<IrOp> body(ops.begin() + anchor.start, ops.begin() + anchor.end + 1);
      std::vector<std::string> parts;
      std::vector<int> suffix_cells;
      for (const IrOp& op : body) {
        parts.push_back(op_key(op));
        suffix_cells.push_back(cells_per_op(op));
      }
      for (int offset = 1; offset < static_cast<int>(body.size()); ++offset) {
        const int cells =
            std::accumulate(suffix_cells.begin() + offset, suffix_cells.end(), 0);
        if (cells < kMinEntryCells)
          continue;
        const std::string key = join_parts(parts, static_cast<std::size_t>(offset));
        const auto suffix_candidate = candidate_by_key.find(key);
        if (suffix_candidate == candidate_by_key.end())
          continue;

        std::vector<Occurrence> replacements;
        for (const Occurrence& occurrence : suffix_candidate->second.occurrences) {
          if (!range_intersects(protected_indexes, occurrence.start, occurrence.end) &&
              !ranges_overlap(anchor.start, anchor.end, occurrence.start, occurrence.end)) {
            replacements.push_back(occurrence);
          }
        }
        if (replacements.empty())
          continue;
        const int savings = static_cast<int>(replacements.size()) * (cells - 2) - 3;
        if (savings <= 0)
          continue;
        entry_candidates.push_back(AnchoredInternalEntryCandidate{
            .body_candidate = body_candidate,
            .anchor = anchor,
            .offset = offset,
            .key = key,
            .cells = cells,
            .replacements = replacements,
            .savings = savings,
        });
      }
    }
  }

  std::sort(entry_candidates.begin(), entry_candidates.end(),
            [](const AnchoredInternalEntryCandidate& left,
               const AnchoredInternalEntryCandidate& right) {
              if (right.savings != left.savings)
                return right.savings < left.savings;
              if (right.cells != left.cells)
                return right.cells < left.cells;
              if (right.body_candidate.cells != left.body_candidate.cells)
                return right.body_candidate.cells < left.body_candidate.cells;
              return left.key < right.key;
            });

  for (const AnchoredInternalEntryCandidate& candidate : entry_candidates) {
    if (range_intersects(protected_indexes, candidate.anchor.start, candidate.anchor.end))
      continue;
    std::vector<Occurrence> replacements;
    for (const Occurrence& occurrence : candidate.replacements) {
      if (!range_intersects(protected_indexes, occurrence.start, occurrence.end))
        replacements.push_back(occurrence);
    }
    if (replacements.empty())
      continue;
    const int savings = static_cast<int>(replacements.size()) * (candidate.cells - 2) - 3;
    if (savings <= 0)
      continue;

    SelectedHelper helper;
    helper.label = labels.next();
    helper.body.assign(ops.begin() + candidate.anchor.start, ops.begin() + candidate.anchor.end + 1);
    helper.occurrences = {candidate.anchor};
    helper.cells = candidate.body_candidate.cells;
    helper.entries.push_back(SelectedHelperEntry{
        .label = labels.next(),
        .offset = candidate.offset,
        .replacements = replacements,
        .cells = candidate.cells,
    });
    selected.push_back(helper);
    mark_range(protected_indexes, candidate.anchor.start, candidate.anchor.end);
    for (const Occurrence& replacement : replacements)
      mark_range(protected_indexes, replacement.start, replacement.end);
  }
}

std::vector<SelectedHelper> select_helpers(const std::vector<Candidate>& candidates,
                                           const std::vector<IrOp>& ops) {
  LabelAllocator labels(ops, "__shared_straight_line_helper_");
  std::set<int> protected_indexes;
  std::vector<SelectedHelper> selected;

  std::vector<Candidate> ordered;
  for (const Candidate& candidate : candidates) {
    if (candidate.cells >= kMinBodyCells && candidate.occurrences.size() >= 2)
      ordered.push_back(candidate);
  }
  std::sort(ordered.begin(), ordered.end(), [](const Candidate& left, const Candidate& right) {
    const int left_savings = net_savings(static_cast<int>(left.occurrences.size()), left.cells);
    const int right_savings = net_savings(static_cast<int>(right.occurrences.size()), right.cells);
    if (right_savings != left_savings)
      return right_savings < left_savings;
    if (right.cells != left.cells)
      return right.cells < left.cells;
    return left.key < right.key;
  });

  for (const Candidate& candidate : ordered) {
    std::vector<Occurrence> occurrences;
    for (const Occurrence& occurrence : candidate.occurrences) {
      if (!range_intersects(protected_indexes, occurrence.start, occurrence.end))
        occurrences.push_back(occurrence);
    }
    if (occurrences.size() < 2)
      continue;
    if (net_savings(static_cast<int>(occurrences.size()), candidate.cells) <= 0)
      continue;

    SelectedHelper helper;
    helper.label = labels.next();
    helper.body.assign(ops.begin() + occurrences.front().start, ops.begin() + occurrences.front().end + 1);
    helper.occurrences = occurrences;
    helper.cells = candidate.cells;
    selected.push_back(helper);
    for (const Occurrence& occurrence : occurrences)
      mark_range(protected_indexes, occurrence.start, occurrence.end);
  }

  select_internal_entries(selected, candidates, labels, protected_indexes);
  select_anchored_internal_entry_helpers(selected, candidates, ops, labels, protected_indexes);
  return selected;
}

IrOp helper_call(const std::string& label, const IrOp& source, bool entry = false) {
  IrOp op;
  op.kind = IrKind::Call;
  op.target = label;
  op.opcode = 0x53;
  op.meta.mnemonic = "ПП";
  op.meta.comment = entry ? "shared straight-line helper entry" : "shared straight-line helper";
  op.meta.source_line = source.meta.source_line;
  op.target_meta.comment = "shared straight-line helper";
  return op;
}

IrOp mark_helper_body_op(IrOp op) {
  if (!op.meta.comment.has_value())
    op.meta.comment = "shared straight-line helper body";
  return op;
}

} // namespace

PassResult shared_straight_line_helper(const std::vector<IrOp>& ops,
                                       const PassContext& context) {
  if (has_numeric_outline_flow_target(ops))
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const std::vector<SelectedHelper> selected =
      select_helpers(collect_candidates(ops, context.options.shared_straight_line_call_bodies),
                     ops);
  if (selected.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  struct Replacement {
    int end = 0;
    std::string label;
    bool entry = false;
  };
  std::map<int, Replacement> replacement_by_start;
  int applied = 0;
  int saved_cells = 0;
  int entry_calls = 0;

  for (const SelectedHelper& helper : selected) {
    const int helper_cost = helper.cells + 1;
    int replacement_savings = 0;
    for (const Occurrence& occurrence : helper.occurrences)
      replacement_savings += occurrence.cells - 2;
    int entry_savings = 0;
    for (const SelectedHelperEntry& entry : helper.entries)
      entry_savings += static_cast<int>(entry.replacements.size()) * (entry.cells - 2);
    saved_cells += replacement_savings + entry_savings - helper_cost;

    for (const Occurrence& occurrence : helper.occurrences) {
      replacement_by_start[occurrence.start] = Replacement{
          .end = occurrence.end,
          .label = helper.label,
          .entry = false,
      };
      ++applied;
    }
    for (const SelectedHelperEntry& entry : helper.entries) {
      for (const Occurrence& occurrence : entry.replacements) {
        replacement_by_start[occurrence.start] = Replacement{
            .end = occurrence.end,
            .label = entry.label,
            .entry = true,
        };
        ++applied;
        ++entry_calls;
      }
    }
  }

  std::vector<IrOp> result;
  result.reserve(ops.size());
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const auto replacement = replacement_by_start.find(index);
    if (replacement != replacement_by_start.end()) {
      result.push_back(helper_call(replacement->second.label,
                                   ops.at(static_cast<std::size_t>(index)),
                                   replacement->second.entry));
      index = replacement->second.end;
      continue;
    }
    result.push_back(ops.at(static_cast<std::size_t>(index)));
  }

  for (const SelectedHelper& helper : selected) {
    IrOp label;
    label.kind = IrKind::Label;
    label.name = helper.label;
    result.push_back(std::move(label));

    std::map<int, std::vector<std::string>> entries_by_offset;
    for (const SelectedHelperEntry& entry : helper.entries)
      entries_by_offset[entry.offset].push_back(entry.label);

    for (int index = 0; index < static_cast<int>(helper.body.size()); ++index) {
      const auto labels = entries_by_offset.find(index);
      if (labels != entries_by_offset.end()) {
        for (const std::string& entry_label : labels->second) {
          IrOp entry;
          entry.kind = IrKind::Label;
          entry.name = entry_label;
          result.push_back(std::move(entry));
        }
      }
      result.push_back(mark_helper_body_op(helper.body.at(static_cast<std::size_t>(index))));
    }

    IrOp ret;
    ret.kind = IrKind::Return;
    ret.opcode = 0x52;
    ret.meta.mnemonic = "В/О";
    ret.meta.comment = "shared straight-line helper return";
    result.push_back(std::move(ret));
  }

  std::vector<AppliedOptimization> optimizations = {
      AppliedOptimization{
          .name = "shared-straight-line-helper",
          .detail = "Extracted " + std::to_string(selected.size()) + " straight-line helper" +
                    (selected.size() == 1 ? "" : "s") + " from " + std::to_string(applied) +
                    " repeated body occurrence" + (applied == 1 ? "" : "s") + " (" +
                    std::to_string(saved_cells) + " cell" + (saved_cells == 1 ? "" : "s") +
                    " saved).",
      },
  };
  if (entry_calls > 0) {
    optimizations.push_back(AppliedOptimization{
        .name = "multi-entry-straight-line-helper",
        .detail = "Reused " + std::to_string(entry_calls) + " repeated helper suffix" +
                  (entry_calls == 1 ? "" : "es") + " by adding internal helper entry label" +
                  (entry_calls == 1 ? "" : "s") + ".",
    });
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations = std::move(optimizations),
  };
}

IrPass shared_straight_line_helper_pass() {
  return IrPass{
      .name = "shared-straight-line-helper",
      .run = shared_straight_line_helper,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
