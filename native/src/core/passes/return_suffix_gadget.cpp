#include "mkpro/core/passes/return_suffix_gadget.hpp"

#include "mkpro/core/passes/outline.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

struct BodyOccurrence {
  std::string key;
  int start = 0;
  int end = 0;
  int cells = 0;
};

struct BodyTarget {
  std::string key;
  int start = 0;
  int body_end = 0;
  int end = 0;
  int cells = 0;
};

struct SelectedGadget {
  enum class Kind {
    Jump,
    Call,
  };

  Kind kind = Kind::Jump;
  std::string label;
  int target_start = 0;
  std::optional<BodyTarget> body_target;
  std::vector<OutlineOccurrence> suffix_replacements;
  std::vector<BodyOccurrence> body_replacements;
};

struct StoreResult {
  IrKind kind = IrKind::Store;
  std::string register_name;
};

bool has_shared_straight_line_helper_label(const std::vector<IrOp>& ops) {
  constexpr std::string_view kPrefix = "__shared_straight_line_helper_";
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label && op.name.starts_with(kPrefix))
      return true;
  }
  return false;
}

bool is_shareable_return(const IrOp& op) {
  return op.kind == IrKind::Return && !has_rewrite_barrier(op);
}

bool is_shareable_body_op(const IrOp& op) {
  if (has_rewrite_barrier(op))
    return false;
  switch (op.kind) {
  case IrKind::Store:
  case IrKind::Recall:
  case IrKind::IndirectStore:
  case IrKind::IndirectRecall:
  case IrKind::Call:
  case IrKind::IndirectCall:
  case IrKind::Stop:
  case IrKind::Plain:
    return true;
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Loop:
  case IrKind::IndirectJump:
  case IrKind::IndirectCondJump:
  case IrKind::Return:
  case IrKind::OrphanAddress:
    return false;
  }
  return false;
}

bool is_callable_body_op(const IrOp& op) {
  return is_shareable_body_op(op) && op.kind != IrKind::Stop;
}

bool is_tail_call_gadget_body_op(const IrOp& op) {
  if (has_rewrite_barrier(op))
    return false;
  switch (op.kind) {
  case IrKind::Store:
  case IrKind::Recall:
  case IrKind::IndirectStore:
  case IrKind::IndirectRecall:
  case IrKind::Plain:
    return true;
  case IrKind::Label:
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
    return false;
  }
  return false;
}

std::string formal_opcode_key(const IrOp& op) {
  return op.target_meta.formal_opcode.has_value() ? std::to_string(*op.target_meta.formal_opcode)
                                                  : std::string();
}

std::string op_key(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Store:
    return "store:" + std::to_string(op.opcode) + ":" + op.register_name;
  case IrKind::Recall:
    return "recall:" + std::to_string(op.opcode) + ":" + op.register_name;
  case IrKind::IndirectStore:
    return "indirect-store:" + std::to_string(op.opcode) + ":" + op.register_name;
  case IrKind::IndirectRecall:
    return "indirect-recall:" + std::to_string(op.opcode) + ":" + op.register_name;
  case IrKind::Call:
    return "call:" + std::to_string(op.opcode) + ":" + target_key(op.target) + ":" +
           formal_opcode_key(op);
  case IrKind::IndirectCall:
    return "indirect-call:" + std::to_string(op.opcode) + ":" + op.register_name;
  case IrKind::Stop:
    return "stop:" + std::to_string(op.opcode);
  case IrKind::Plain:
    return "plain:" + std::to_string(op.opcode);
  case IrKind::Return:
    return "return:" + std::to_string(op.opcode);
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Loop:
  case IrKind::IndirectJump:
  case IrKind::IndirectCondJump:
  case IrKind::OrphanAddress:
    return ir_kind_name(op.kind);
  }
  return {};
}

std::string suffix_body_key(const std::string& key) {
  const std::size_t cut = key.rfind('\n');
  if (cut == std::string::npos)
    return {};
  return key.substr(0, cut);
}

std::map<std::string, std::vector<BodyTarget>> collect_body_targets(
    const std::vector<IrOp>& ops, const std::vector<OutlineCandidate>& candidates) {
  std::map<std::string, std::vector<BodyTarget>> result;
  for (const OutlineCandidate& candidate : candidates) {
    for (const OutlineOccurrence& occurrence : candidate.occurrences) {
      const std::string body_key = suffix_body_key(occurrence.key);
      const int body_cells =
          occurrence.cells - cells_per_op(ops.at(static_cast<std::size_t>(occurrence.end)));
      if (body_key.empty() || body_cells <= 2)
        continue;
      result[body_key].push_back(BodyTarget{
          .key = body_key,
          .start = occurrence.start,
          .body_end = occurrence.end - 1,
          .end = occurrence.end,
          .cells = body_cells,
      });
    }
  }
  return result;
}

std::map<std::string, std::vector<BodyOccurrence>> collect_body_occurrences(
    const std::vector<IrOp>& ops, const std::set<std::string>& wanted_keys) {
  std::map<std::string, std::vector<BodyOccurrence>> result;
  if (wanted_keys.empty())
    return result;

  for (int start = 0; start < static_cast<int>(ops.size()); ++start) {
    std::vector<std::string> parts;
    int cells = 0;
    for (int end = start; end < static_cast<int>(ops.size()); ++end) {
      const IrOp& op = ops.at(static_cast<std::size_t>(end));
      if (!is_callable_body_op(op))
        break;
      parts.push_back(op_key(op));
      cells += cells_per_op(op);
      if (cells <= 2)
        continue;

      std::string key;
      for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0)
          key += '\n';
        key += parts.at(index);
      }
      if (!wanted_keys.contains(key))
        continue;
      result[key].push_back(BodyOccurrence{
          .key = key,
          .start = start,
          .end = end,
          .cells = cells,
      });
    }
  }
  return result;
}

std::map<std::string, std::vector<BodyTarget>> collect_tail_call_targets(
    const std::vector<IrOp>& ops) {
  std::map<std::string, std::vector<BodyTarget>> result;
  for (int jump_index = 0; jump_index < static_cast<int>(ops.size()); ++jump_index) {
    const IrOp& jump = ops.at(static_cast<std::size_t>(jump_index));
    if (jump.kind != IrKind::Jump || !std::holds_alternative<std::string>(jump.target) ||
        has_rewrite_barrier(jump)) {
      continue;
    }

    std::vector<std::string> parts = {"target:" + target_key(jump.target)};
    int cells = cells_per_op(jump);
    for (int start = jump_index - 1; start >= 0; --start) {
      const IrOp& op = ops.at(static_cast<std::size_t>(start));
      if (!is_tail_call_gadget_body_op(op))
        break;

      parts.insert(parts.begin(), op_key(op));
      cells += cells_per_op(op);
      const int body_cells = cells - cells_per_op(jump);
      if (body_cells <= 0)
        continue;

      std::string key;
      for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0)
          key += '\n';
        key += parts.at(index);
      }
      result[key].push_back(BodyTarget{
          .key = key,
          .start = start,
          .body_end = jump_index - 1,
          .end = jump_index,
          .cells = body_cells,
      });
    }
  }
  return result;
}

std::map<std::string, std::vector<BodyOccurrence>> collect_tail_call_occurrences(
    const std::vector<IrOp>& ops, const std::set<std::string>& wanted_keys) {
  std::map<std::string, std::vector<BodyOccurrence>> result;
  if (wanted_keys.empty())
    return result;

  for (int start = 0; start < static_cast<int>(ops.size()); ++start) {
    std::vector<std::string> parts;
    int cells = 0;
    for (int call_index = start; call_index < static_cast<int>(ops.size()); ++call_index) {
      const IrOp& op = ops.at(static_cast<std::size_t>(call_index));
      if (op.kind == IrKind::Call && std::holds_alternative<std::string>(op.target) &&
          !has_rewrite_barrier(op)) {
        std::string key;
        for (const std::string& part : parts) {
          if (!key.empty())
            key += '\n';
          key += part;
        }
        if (!key.empty())
          key += '\n';
        key += "target:" + target_key(op.target);
        if (cells > 0 && wanted_keys.contains(key)) {
          result[key].push_back(BodyOccurrence{
              .key = key,
              .start = start,
              .end = call_index,
              .cells = cells + cells_per_op(op),
          });
        }
        break;
      }
      if (!is_tail_call_gadget_body_op(op))
        break;
      parts.push_back(op_key(op));
      cells += cells_per_op(op);
    }
  }
  return result;
}

bool same_target_body(const BodyOccurrence& occurrence, const BodyTarget& target) {
  return occurrence.start == target.start && occurrence.end == target.body_end;
}

std::vector<SelectedGadget> select_body_calls(
    const std::map<std::string, std::vector<BodyTarget>>& targets,
    const std::map<std::string, std::vector<BodyOccurrence>>& occurrences, LabelAllocator& labels,
    std::set<int>& protected_indexes) {
  std::vector<SelectedGadget> selected;
  std::vector<std::string> keys;
  keys.reserve(targets.size());
  for (const auto& [key, unused] : targets) {
    (void)unused;
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end(), [&](const std::string& left, const std::string& right) {
    const int left_cells = targets.at(left).empty() ? 0 : targets.at(left).front().cells;
    const int right_cells = targets.at(right).empty() ? 0 : targets.at(right).front().cells;
    if (right_cells != left_cells)
      return right_cells < left_cells;
    return left < right;
  });

  for (const std::string& key : keys) {
    std::vector<BodyTarget> available_targets;
    for (const BodyTarget& target : targets.at(key)) {
      if (!range_intersects(protected_indexes, target.start, target.end))
        available_targets.push_back(target);
    }
    if (available_targets.empty())
      continue;
    const BodyTarget target = available_targets.front();

    std::vector<BodyOccurrence> replacements;
    const auto occurrences_for_key = occurrences.find(key);
    if (occurrences_for_key != occurrences.end()) {
      for (const BodyOccurrence& occurrence : occurrences_for_key->second) {
        if (!same_target_body(occurrence, target) &&
            !range_intersects(protected_indexes, occurrence.start, occurrence.end)) {
          replacements.push_back(occurrence);
        }
      }
    }
    if (replacements.empty())
      continue;

    int saved_cells = 0;
    for (const BodyOccurrence& occurrence : replacements)
      saved_cells += occurrence.cells - 2;
    if (saved_cells <= 0)
      continue;

    selected.push_back(SelectedGadget{
        .kind = SelectedGadget::Kind::Call,
        .label = labels.next(),
        .target_start = target.start,
        .body_target = target,
        .body_replacements = replacements,
    });

    mark_range(protected_indexes, target.start, target.end);
    for (const BodyOccurrence& occurrence : replacements)
      mark_range(protected_indexes, occurrence.start, occurrence.end);
  }
  return selected;
}

std::vector<SelectedGadget> select_gadgets(const std::vector<OutlineCandidate>& candidates,
                                           const std::vector<IrOp>& ops) {
  LabelAllocator labels(ops, "__return_suffix_gadget_");
  std::set<int> protected_indexes;
  std::vector<SelectedGadget> selected;

  for (const SelectedOutline& suffix : select_shared_suffixes(candidates, labels, protected_indexes)) {
    selected.push_back(SelectedGadget{
        .kind = SelectedGadget::Kind::Jump,
        .label = suffix.label,
        .target_start = suffix.target.start,
        .suffix_replacements = suffix.replacements,
    });
  }

  const std::map<std::string, std::vector<BodyTarget>> body_targets =
      collect_body_targets(ops, candidates);
  std::set<std::string> body_keys;
  for (const auto& [key, unused] : body_targets) {
    (void)unused;
    body_keys.insert(key);
  }
  std::vector<SelectedGadget> body_calls =
      select_body_calls(body_targets, collect_body_occurrences(ops, body_keys), labels,
                        protected_indexes);
  selected.insert(selected.end(), body_calls.begin(), body_calls.end());

  const std::map<std::string, std::vector<BodyTarget>> tail_call_targets =
      collect_tail_call_targets(ops);
  std::set<std::string> tail_call_keys;
  for (const auto& [key, unused] : tail_call_targets) {
    (void)unused;
    tail_call_keys.insert(key);
  }
  std::vector<SelectedGadget> tail_calls =
      select_body_calls(tail_call_targets, collect_tail_call_occurrences(ops, tail_call_keys),
                        labels, protected_indexes);
  selected.insert(selected.end(), tail_calls.begin(), tail_calls.end());
  return selected;
}

std::optional<StoreResult> body_return_x(const std::vector<IrOp>& ops, const BodyTarget& target) {
  const IrOp& final = ops.at(static_cast<std::size_t>(target.body_end));
  if (has_rewrite_barrier(final))
    return std::nullopt;
  if (final.kind == IrKind::Store)
    return StoreResult{.kind = IrKind::Store, .register_name = final.register_name};
  if (final.kind == IrKind::IndirectStore)
    return StoreResult{.kind = IrKind::IndirectStore, .register_name = final.register_name};
  return std::nullopt;
}

bool recall_matches_store(const IrOp* op, const StoreResult& store) {
  if (op == nullptr || has_rewrite_barrier(*op))
    return false;
  return (store.kind == IrKind::Store && op->kind == IrKind::Recall &&
          op->register_name == store.register_name) ||
         (store.kind == IrKind::IndirectStore && op->kind == IrKind::IndirectRecall &&
          op->register_name == store.register_name);
}

IrOp gadget_jump(const std::string& label, const IrOp& source) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.target = label;
  op.opcode = 0x51;
  op.meta.mnemonic = "БП";
  op.meta.comment = "return suffix gadget";
  op.meta.source_line = source.meta.source_line;
  op.target_meta.comment = "return suffix gadget";
  return op;
}

IrOp gadget_call(const std::string& label, const IrOp& source) {
  IrOp op;
  op.kind = IrKind::Call;
  op.target = label;
  op.opcode = 0x53;
  op.meta.mnemonic = "ПП";
  op.meta.comment = "proc call return suffix gadget";
  op.meta.source_line = source.meta.source_line;
  op.target_meta.comment = "return suffix gadget target";
  return op;
}

} // namespace

PassResult return_suffix_gadget(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  if (has_numeric_outline_flow_target(ops))
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
  if (has_shared_straight_line_helper_label(ops))
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const std::vector<OutlineCandidate> candidates = collect_suffix_candidates(
      ops, SuffixCollectionConfig{
               .is_terminal = is_shareable_return,
               .is_body_op = is_shareable_body_op,
               .op_key = op_key,
           });
  const std::vector<SelectedGadget> selected = select_gadgets(candidates, ops);
  if (selected.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::map<int, std::vector<std::string>> target_labels;
  struct Replacement {
    SelectedGadget::Kind kind = SelectedGadget::Kind::Jump;
    int end = 0;
    std::string label;
    bool skip_next_recall = false;
  };
  std::map<int, Replacement> replacement_by_start;
  int applied = 0;
  int saved_cells = 0;
  int jumps = 0;
  int calls = 0;
  int reused_return_x = 0;

  for (const SelectedGadget& gadget : selected) {
    target_labels[gadget.target_start].push_back(gadget.label);

    if (gadget.kind == SelectedGadget::Kind::Jump) {
      for (const OutlineOccurrence& replacement : gadget.suffix_replacements) {
        replacement_by_start[replacement.start] = Replacement{
            .kind = SelectedGadget::Kind::Jump,
            .end = replacement.end,
            .label = gadget.label,
        };
        ++applied;
        saved_cells += replacement.cells - 2;
        ++jumps;
      }
      continue;
    }

    const std::optional<StoreResult> return_x =
        gadget.body_target.has_value() ? body_return_x(ops, *gadget.body_target) : std::nullopt;
    for (const BodyOccurrence& replacement : gadget.body_replacements) {
      const IrOp* next =
          replacement.end + 1 < static_cast<int>(ops.size())
              ? &ops.at(static_cast<std::size_t>(replacement.end + 1))
              : nullptr;
      const bool skip_next_recall = return_x.has_value() && recall_matches_store(next, *return_x);
      replacement_by_start[replacement.start] = Replacement{
          .kind = SelectedGadget::Kind::Call,
          .end = replacement.end,
          .label = gadget.label,
          .skip_next_recall = skip_next_recall,
      };
      ++applied;
      saved_cells += replacement.cells - 2;
      if (skip_next_recall) {
        ++saved_cells;
        ++reused_return_x;
      }
      ++calls;
    }
  }

  std::vector<IrOp> result;
  result.reserve(ops.size() + target_labels.size());
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const auto labels_at = target_labels.find(index);
    if (labels_at != target_labels.end()) {
      for (const std::string& label : labels_at->second) {
        IrOp label_op;
        label_op.kind = IrKind::Label;
        label_op.name = label;
        result.push_back(std::move(label_op));
      }
    }

    const auto replacement = replacement_by_start.find(index);
    if (replacement != replacement_by_start.end()) {
      result.push_back(replacement->second.kind == SelectedGadget::Kind::Jump
                           ? gadget_jump(replacement->second.label,
                                         ops.at(static_cast<std::size_t>(index)))
                           : gadget_call(replacement->second.label,
                                         ops.at(static_cast<std::size_t>(index))));
      index = replacement->second.skip_next_recall ? replacement->second.end + 1
                                                   : replacement->second.end;
      continue;
    }

    result.push_back(ops.at(static_cast<std::size_t>(index)));
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "return-suffix-gadget",
                  .detail =
                      "Shared " + std::to_string(applied) + " return/tail-call gadget" +
                      (applied == 1 ? "" : "s") + " (" + std::to_string(jumps) + " jump, " +
                      std::to_string(calls) + " call; " + std::to_string(saved_cells) + " cell" +
                      (saved_cells == 1 ? "" : "s") + " saved" +
                      (reused_return_x == 0
                           ? std::string()
                           : ", " + std::to_string(reused_return_x) + " returned-X recall" +
                                 (reused_return_x == 1 ? "" : "s") + " reused") +
                      ").",
              },
          },
  };
}

IrPass return_suffix_gadget_pass() {
  return IrPass{
      .name = "return-suffix-gadget",
      .run = return_suffix_gadget,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
