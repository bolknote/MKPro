#include "mkpro/core/passes/shared_straight_line_helper.hpp"

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/passes/outline.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <string_view>
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
    "__callee_hole_helper_",
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
    return allow_direct_calls;
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

bool x2_restore_boundaries_are_internal(const std::vector<IrOp>& ops, int start, int end,
                                        bool calls_affect_x2 = false) {
  for (int index = start; index <= end; ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (!is_x2_restore_op(op))
      continue;
    if (index == start)
      return false;
    const IrOp& previous = ops.at(static_cast<std::size_t>(index - 1));
    // The dynamic predecessor of an op following a subroutine call is the
    // callee's В/О (an X2-affecting op) in both the original layout and the
    // extracted helper, so a call boundary determines the X2 state itself.
    const X2Effect previous_effect = calls_affect_x2 && previous.kind == IrKind::Call
                                         ? X2Effect::Affects
                                         : x2_effect_for_boundary(previous);
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

namespace {

// ---- Callee-hole skeleton extraction --------------------------------------
//
// Two straight-line regions that differ only in the target of their repeated
// leaf call collapse into one skeleton whose leaf call is К ПП r. Each caller
// charges r with its leaf's numeric address (literal digits + X->П r) right
// before calling the skeleton, mirroring the runtime indirect-call discipline:
// leaves must already live at stable backward addresses, and the final program
// carries proof comments checked by callee_hole_indirect_call_targets_proved.

constexpr std::array<std::string_view, 8> kHoleSelectorRegisters = {
    "7", "8", "9", "a", "b", "c", "d", "e",
};

struct HoleOccurrence {
  int start = 0;
  int end = 0;
  int cells = 0;
  int leaf_target = -1;
  std::string leaf_label;
  int call_count = 0;
  // Per-call (in region order) frozen numeric targets and labels; positions
  // whose targets differ across occurrences become the hole, positions whose
  // targets agree stay direct calls inside the skeleton.
  std::vector<int> call_targets;
  std::vector<std::string> call_labels;
};

struct HoleCandidate {
  std::string key;
  std::vector<HoleOccurrence> occurrences;
  int cells = 0;
};

struct SelectedHoleHelper {
  std::string label;
  std::vector<IrOp> body;
  std::vector<HoleOccurrence> occurrences;
  std::string register_name;
  // Leaf label by frozen numeric address; the labels keep the leaves alive in
  // the CFG and the addresses are re-validated by the final static proof.
  std::map<int, std::string> leaf_labels;
  // Call positions (in region order) routed through the hole register; other
  // call positions target the same label in every occurrence and stay direct.
  std::vector<bool> hole_positions;
  int cells = 0;
};

std::string hole_op_key(const IrOp& op) {
  if (op.kind == IrKind::Call)
    return "call:<hole>";
  return op_key(op);
}

int hole_charge_cost(int leaf_target) {
  // Literal digits of the leaf address + X->П r + ПП skeleton (2 cells).
  return static_cast<int>(std::to_string(leaf_target).size()) + 1 + 2;
}

std::set<std::string> hole_used_registers(const std::vector<IrOp>& ops) {
  std::set<std::string> used;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Store || op.kind == IrKind::Recall ||
        op.kind == IrKind::IndirectStore || op.kind == IrKind::IndirectRecall ||
        op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
        op.kind == IrKind::IndirectCondJump) {
      used.insert(op.register_name);
    }
    // Indirect memory ops write through a selector into data registers that
    // never appear as direct register names; their annotated target sets keep
    // those registers off the selector candidate list.
    if (!op.meta.comment.has_value())
      continue;
    constexpr std::string_view kTargetsMarker = "indirect-memory-targets=";
    const std::size_t marker = op.meta.comment->find(kTargetsMarker);
    if (marker == std::string::npos)
      continue;
    std::size_t pos = marker + kTargetsMarker.size();
    std::string name;
    const auto flush = [&]() {
      if (!name.empty())
        used.insert(name);
      name.clear();
    };
    while (pos < op.meta.comment->size()) {
      const char symbol = (*op.meta.comment)[pos];
      if (symbol == ',') {
        flush();
        ++pos;
        continue;
      }
      if (std::isalnum(static_cast<unsigned char>(symbol)) == 0)
        break;
      name.push_back(symbol);
      ++pos;
    }
    flush();
  }
  return used;
}

std::vector<HoleCandidate> collect_hole_candidates(const std::vector<IrOp>& ops,
                                                   const std::map<std::string, int>& labels) {
  std::map<std::string, std::vector<HoleOccurrence>> by_key;
  std::set<int> protected_indexes = generated_body_indexes(ops, /*allow_direct_calls=*/true);
  const std::set<int> display_indexes = display_sensitive_block_indexes(ops);
  protected_indexes.insert(display_indexes.begin(), display_indexes.end());

  for (int start = 0; start < static_cast<int>(ops.size()); ++start) {
    if (protected_indexes.contains(start))
      continue;
    if (starts_at_x2_restore(ops, start) || starts_after_x2_restore(ops, start))
      continue;
    std::vector<std::string> parts;
    int cells = 0;
    std::vector<int> call_targets;
    std::vector<std::string> call_labels;
    for (int end = start; end < static_cast<int>(ops.size()); ++end) {
      if (protected_indexes.contains(end))
        break;
      const IrOp& op = ops.at(static_cast<std::size_t>(end));
      if (!is_shareable_body_op(op, /*allow_direct_calls=*/true))
        break;
      if (op.kind == IrKind::Call) {
        const std::string* label = std::get_if<std::string>(&op.target);
        const std::optional<int> target = target_address(op.target, labels);
        // Every call must be addressed through a label; targets beyond the
        // physical 00..А4 window are still collected because the regions
        // shrink to charges and the selection stage recomputes (and then
        // validates) the post-rewrite leaf addresses.
        if (label == nullptr || !target.has_value() || *target < 0)
          break;
        call_targets.push_back(*target);
        call_labels.push_back(*label);
      }
      parts.push_back(hole_op_key(op));
      cells += cells_per_op(op);
      if (call_targets.empty() || cells < kMinBodyCells)
        continue;
      // A region ending in a call may sit right before an X2-restore op: the
      // restore's dynamic predecessor is a В/О (leaf's originally, the
      // skeleton's after extraction), which affects X2 either way.
      if (ends_before_x2_restore(ops, end) && op.kind != IrKind::Call)
        continue;
      if (!x2_restore_boundaries_are_internal(ops, start, end, /*calls_affect_x2=*/true))
        continue;
      const std::string key = join_parts(parts);
      by_key[key].push_back(HoleOccurrence{
          .start = start,
          .end = end,
          .cells = cells,
          .call_count = static_cast<int>(call_targets.size()),
          .call_targets = call_targets,
          .call_labels = call_labels,
      });
    }
  }

  std::vector<HoleCandidate> result;
  result.reserve(by_key.size());
  for (auto& [key, occurrences] : by_key) {
    const int cells = occurrences.empty() ? 0 : occurrences.front().cells;
    result.push_back(HoleCandidate{
        .key = key,
        .occurrences = std::move(occurrences),
        .cells = cells,
    });
  }
  return result;
}

int hole_net_savings(const std::vector<HoleOccurrence>& occurrences, int cells) {
  int savings = 0;
  for (const HoleOccurrence& occurrence : occurrences)
    savings += occurrence.cells - hole_charge_cost(occurrence.leaf_target);
  // Each hole call shrinks from a two-cell ПП to a one-cell К ПП in the body.
  const int hole_calls = occurrences.empty() ? 0 : occurrences.front().call_count;
  return savings - (cells - hole_calls + 1);
}

// Decide which call positions become the hole: positions whose frozen targets
// differ across occurrences route through the selector register, positions
// whose targets agree everywhere stay direct calls inside the skeleton. Every
// occurrence must charge a single leaf address, so all of its hole positions
// have to share one target.
std::optional<std::vector<bool>> assign_hole_positions(std::vector<HoleOccurrence>& occurrences) {
  if (occurrences.empty())
    return std::nullopt;
  const std::size_t positions = occurrences.front().call_targets.size();
  for (const HoleOccurrence& occurrence : occurrences) {
    if (occurrence.call_targets.size() != positions)
      return std::nullopt;
  }
  std::vector<bool> hole_positions(positions, false);
  for (std::size_t position = 0; position < positions; ++position) {
    for (const HoleOccurrence& occurrence : occurrences) {
      if (occurrence.call_targets.at(position) != occurrences.front().call_targets.at(position)) {
        hole_positions[position] = true;
        break;
      }
    }
  }
  if (std::none_of(hole_positions.begin(), hole_positions.end(), [](bool hole) { return hole; }))
    return std::nullopt;
  for (HoleOccurrence& occurrence : occurrences) {
    int leaf_target = -1;
    std::string leaf_label;
    int hole_calls = 0;
    for (std::size_t position = 0; position < positions; ++position) {
      if (!hole_positions.at(position))
        continue;
      const int target = occurrence.call_targets.at(position);
      if (leaf_target >= 0 && target != leaf_target)
        return std::nullopt;
      leaf_target = target;
      leaf_label = occurrence.call_labels.at(position);
      ++hole_calls;
    }
    occurrence.leaf_target = leaf_target;
    occurrence.leaf_label = leaf_label;
    occurrence.call_count = hole_calls;
  }
  return hole_positions;
}

std::vector<SelectedHoleHelper> select_hole_helpers(const std::vector<IrOp>& ops,
                                                    const std::map<std::string, int>& labels,
                                                    int official_last) {
  const std::vector<HoleCandidate> candidates = collect_hole_candidates(ops, labels);
  if (candidates.empty())
    return {};

  const std::vector<int> addresses = [&] {
    std::vector<int> result;
    result.reserve(ops.size());
    int address = 0;
    for (const IrOp& op : ops) {
      result.push_back(address);
      address += cells_per_op(op);
    }
    return result;
  }();

  std::vector<HoleCandidate> ordered;
  ordered.reserve(candidates.size());
  for (const HoleCandidate& candidate : candidates) {
    HoleCandidate analyzed = candidate;
    if (!assign_hole_positions(analyzed.occurrences).has_value())
      continue;
    ordered.push_back(std::move(analyzed));
  }
  std::sort(ordered.begin(), ordered.end(), [](const HoleCandidate& left,
                                               const HoleCandidate& right) {
    const int left_savings = hole_net_savings(left.occurrences, left.cells);
    const int right_savings = hole_net_savings(right.occurrences, right.cells);
    if (right_savings != left_savings)
      return right_savings < left_savings;
    if (right.cells != left.cells)
      return right.cells < left.cells;
    return left.key < right.key;
  });

  LabelAllocator hole_labels(ops, "__callee_hole_helper_");
  std::set<int> protected_indexes;
  std::set<std::string> taken_registers = hole_used_registers(ops);
  std::vector<SelectedHoleHelper> selected;

  for (const HoleCandidate& candidate : ordered) {
    std::vector<HoleOccurrence> occurrences;
    for (const HoleOccurrence& occurrence : candidate.occurrences) {
      if (range_intersects(protected_indexes, occurrence.start, occurrence.end))
        continue;
      const bool overlaps_selected =
          std::any_of(occurrences.begin(), occurrences.end(), [&](const HoleOccurrence& other) {
            return ranges_overlap(other.start, other.end, occurrence.start, occurrence.end);
          });
      if (overlaps_selected)
        continue;
      occurrences.push_back(occurrence);
    }
    if (occurrences.size() < 2U)
      continue;
    // Re-run the position analysis on the surviving occurrences: dropping an
    // occurrence can change which positions vary.
    const std::optional<std::vector<bool>> hole_positions = assign_hole_positions(occurrences);
    if (!hole_positions.has_value())
      continue;
    std::map<int, std::string> leaf_labels;
    for (const HoleOccurrence& occurrence : occurrences)
      leaf_labels[occurrence.leaf_target] = occurrence.leaf_label;
    // With a single distinct leaf the plain straight-line helper already
    // handles the shape; the hole is only worth its charges for >=2 leaves.
    if (leaf_labels.size() < 2U)
      continue;
    if (hole_net_savings(occurrences, candidate.cells) <= 0)
      continue;

    // The charges freeze numeric leaf addresses. Leaves strictly before the
    // first rewritten op keep their addresses; leaves at or after it shift
    // when the regions shrink to charges, so their post-rewrite addresses are
    // computed here instead (iterating, because the charge length depends on
    // the frozen digit count). Retargeting is only sound for the first helper
    // selected in a run: a second selection would shift these frozen digits.
    const int first_modified =
        std::min_element(occurrences.begin(), occurrences.end(),
                         [](const HoleOccurrence& left, const HoleOccurrence& right) {
                           return left.start < right.start;
                         })
            ->start;
    const int first_modified_address = addresses.at(static_cast<std::size_t>(first_modified));
    const bool leaves_stable =
        std::all_of(occurrences.begin(), occurrences.end(), [&](const HoleOccurrence& occurrence) {
          return occurrence.leaf_target < first_modified_address &&
                 occurrence.leaf_target <= official_last;
        });
    bool retargeted = false;
    if (!leaves_stable) {
      if (!selected.empty())
        continue;
      struct RegionSpan {
        int start_address = 0;
        int cells = 0;
      };
      std::vector<RegionSpan> spans;
      spans.reserve(occurrences.size());
      for (const HoleOccurrence& occurrence : occurrences) {
        spans.push_back(RegionSpan{
            .start_address = addresses.at(static_cast<std::size_t>(occurrence.start)),
            .cells = occurrence.cells,
        });
      }
      const bool leaf_inside_region =
          std::any_of(occurrences.begin(), occurrences.end(), [&](const HoleOccurrence& occurrence) {
            return std::any_of(spans.begin(), spans.end(), [&](const RegionSpan& span) {
              return occurrence.leaf_target >= span.start_address &&
                     occurrence.leaf_target < span.start_address + span.cells;
            });
          });
      if (leaf_inside_region)
        continue;

      std::vector<int> final_targets(occurrences.size(), 0);
      for (std::size_t index = 0; index < occurrences.size(); ++index)
        final_targets[index] = occurrences.at(index).leaf_target;
      bool converged = false;
      for (int iteration = 0; iteration < 4 && !converged; ++iteration) {
        converged = true;
        for (std::size_t index = 0; index < occurrences.size(); ++index) {
          const int original = occurrences.at(index).leaf_target;
          int shift = 0;
          for (std::size_t other = 0; other < occurrences.size(); ++other) {
            if (spans.at(other).start_address + spans.at(other).cells > original)
              continue;
            shift += spans.at(other).cells - hole_charge_cost(final_targets.at(other));
          }
          const int updated = original - shift;
          if (updated != final_targets.at(index)) {
            final_targets[index] = updated;
            converged = false;
          }
        }
      }
      const bool targets_valid =
          converged && std::all_of(final_targets.begin(), final_targets.end(),
                                   [&](int target) {
                                     return target >= 0 && target <= official_last;
                                   });
      if (!targets_valid)
        continue;
      for (std::size_t index = 0; index < occurrences.size(); ++index)
        occurrences[index].leaf_target = final_targets.at(index);
      leaf_labels.clear();
      for (const HoleOccurrence& occurrence : occurrences)
        leaf_labels[occurrence.leaf_target] = occurrence.leaf_label;
      if (leaf_labels.size() < 2U)
        continue;
      if (hole_net_savings(occurrences, candidate.cells) <= 0)
        continue;
      retargeted = true;
    }

    const auto register_it =
        std::find_if(kHoleSelectorRegisters.begin(), kHoleSelectorRegisters.end(),
                     [&](std::string_view name) {
                       return !taken_registers.contains(std::string(name));
                     });
    if (register_it == kHoleSelectorRegisters.end())
      continue;

    SelectedHoleHelper helper;
    helper.label = hole_labels.next();
    helper.body.assign(ops.begin() + occurrences.front().start,
                       ops.begin() + occurrences.front().end + 1);
    helper.occurrences = occurrences;
    helper.register_name = std::string(*register_it);
    helper.leaf_labels = leaf_labels;
    helper.hole_positions = *hole_positions;
    helper.cells = candidate.cells;
    taken_registers.insert(helper.register_name);
    selected.push_back(std::move(helper));
    for (const HoleOccurrence& occurrence : occurrences)
      mark_range(protected_indexes, occurrence.start, occurrence.end);
    // Frozen retargeted digits assume no further rewrites shift the leaves.
    if (retargeted)
      break;
  }
  return selected;
}

std::vector<IrOp> hole_charge_ops(int leaf_target, const std::string& register_name,
                                  const std::string& helper_label, const IrOp& source) {
  std::vector<IrOp> result;
  const std::string text = std::to_string(leaf_target);
  const std::string selector_comment = "runtime indirect call selector " + text;
  for (const char digit : text) {
    IrOp op;
    op.kind = IrKind::Plain;
    op.opcode = digit - '0';
    op.meta.mnemonic = std::string(1, digit);
    op.meta.comment = selector_comment;
    op.meta.source_line = source.meta.source_line;
    result.push_back(std::move(op));
  }
  IrOp store;
  store.kind = IrKind::Store;
  store.register_name = register_name;
  store.opcode = 0x40 + register_index(register_name);
  store.meta.mnemonic = "X->П " + register_name;
  store.meta.comment = selector_comment;
  store.meta.source_line = source.meta.source_line;
  result.push_back(std::move(store));

  IrOp call;
  call.kind = IrKind::Call;
  call.target = helper_label;
  call.opcode = 0x53;
  call.meta.mnemonic = "ПП";
  call.meta.comment = "callee-hole skeleton call";
  call.meta.source_line = source.meta.source_line;
  call.target_meta.comment = "callee-hole skeleton";
  result.push_back(std::move(call));
  return result;
}

std::string hole_leaf_targets_text(const std::map<int, std::string>& leaf_labels) {
  std::string text;
  for (const auto& [target, label] : leaf_labels) {
    if (!text.empty())
      text += ",";
    text += std::to_string(target) + ":" + label;
  }
  return text;
}

} // namespace

PassResult callee_hole_straight_line_helper(const std::vector<IrOp>& ops,
                                            const PassContext& context) {
  if (!context.options.callee_hole_straight_line_helper)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
  if (has_numeric_outline_flow_target(ops))
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const std::map<std::string, int> labels = calculate_label_addresses(ops);
  const int official_last = official_program_last_address(
      address_space_model_for_feature_profile(context.options.feature_profile));
  const std::vector<SelectedHoleHelper> selected =
      select_hole_helpers(ops, labels, official_last);
  if (selected.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  struct HoleReplacement {
    int end = 0;
    const SelectedHoleHelper* helper = nullptr;
    int leaf_target = -1;
  };
  std::map<int, HoleReplacement> replacement_by_start;
  int applied = 0;
  int saved_cells = 0;
  for (const SelectedHoleHelper& helper : selected) {
    saved_cells += hole_net_savings(helper.occurrences, helper.cells);
    for (const HoleOccurrence& occurrence : helper.occurrences) {
      replacement_by_start[occurrence.start] = HoleReplacement{
          .end = occurrence.end,
          .helper = &helper,
          .leaf_target = occurrence.leaf_target,
      };
      ++applied;
    }
  }

  std::vector<IrOp> result;
  result.reserve(ops.size());
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const auto replacement = replacement_by_start.find(index);
    if (replacement != replacement_by_start.end()) {
      const std::vector<IrOp> charge =
          hole_charge_ops(replacement->second.leaf_target, replacement->second.helper->register_name,
                          replacement->second.helper->label,
                          ops.at(static_cast<std::size_t>(index)));
      result.insert(result.end(), charge.begin(), charge.end());
      index = replacement->second.end;
      continue;
    }
    result.push_back(ops.at(static_cast<std::size_t>(index)));
  }

  for (const SelectedHoleHelper& helper : selected) {
    IrOp label;
    label.kind = IrKind::Label;
    label.name = helper.label;
    result.push_back(std::move(label));

    const std::string hole_comment =
        "callee-hole indirect call; leaf-targets=" + hole_leaf_targets_text(helper.leaf_labels);
    std::size_t call_position = 0;
    for (const IrOp& op : helper.body) {
      if (op.kind == IrKind::Call) {
        const bool is_hole = call_position < helper.hole_positions.size() &&
                             helper.hole_positions.at(call_position);
        ++call_position;
        if (is_hole) {
          IrOp hole = op;
          hole.kind = IrKind::IndirectCall;
          hole.register_name = helper.register_name;
          hole.target = 0;
          hole.target_meta = {};
          hole.opcode = 0xa0 + register_index(helper.register_name);
          hole.meta.mnemonic = "К ПП " + helper.register_name;
          hole.meta.comment = hole_comment;
          result.push_back(std::move(hole));
          continue;
        }
      }
      result.push_back(mark_helper_body_op(op));
    }

    IrOp ret;
    ret.kind = IrKind::Return;
    ret.opcode = 0x52;
    ret.meta.mnemonic = "В/О";
    ret.meta.comment = "callee-hole helper return";
    result.push_back(std::move(ret));
  }

  // Mark each leaf's entry op so the final static proof can re-derive its
  // address from the delivered listing and compare it with the frozen charge.
  for (const SelectedHoleHelper& helper : selected) {
    for (const auto& [target, label] : helper.leaf_labels) {
      (void)target;
      const std::string marker = "callee-hole leaf entry " + label;
      for (std::size_t index = 0; index < result.size(); ++index) {
        const IrOp& op = result.at(index);
        if (op.kind != IrKind::Label || op.name != label)
          continue;
        for (std::size_t entry = index + 1U; entry < result.size(); ++entry) {
          IrOp& entry_op = result.at(entry);
          if (entry_op.kind == IrKind::Label)
            continue;
          if (!entry_op.meta.comment.has_value()) {
            entry_op.meta.comment = marker;
          } else if (entry_op.meta.comment->find(marker) == std::string::npos) {
            entry_op.meta.comment = *entry_op.meta.comment + "; " + marker;
          }
          break;
        }
        break;
      }
    }
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "callee-hole-straight-line-helper",
                  .detail = "Merged " + std::to_string(applied) +
                            " straight-line region(s) differing only in their leaf call into " +
                            std::to_string(selected.size()) + " skeleton(s) with a К ПП dispatch (" +
                            std::to_string(saved_cells) + " cell(s) saved).",
              },
          },
  };
}

IrPass callee_hole_straight_line_helper_pass() {
  return IrPass{
      .name = "callee-hole-straight-line-helper",
      .run = callee_hole_straight_line_helper,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
