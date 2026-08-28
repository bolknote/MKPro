#include "mkpro/core/passes/redundant_literal_reload.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

bool is_rewrite_safe_digit(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode >= 0x00 && op.opcode <= 0x09 &&
         !has_rewrite_barrier(op) && !is_display_focus_sensitive(op) &&
         op.meta.roles.empty() && op.target_meta.roles.empty() &&
         op.meta.semantic_call_origins.empty() && !op.meta.tactic.has_value() &&
         !op.procedure_boundary.has_value() && op.name.empty() &&
         op.register_name.empty() && op.condition.empty() && op.counter.empty() &&
         op.semantic.empty() && !op.meta.manual_interaction.has_value() &&
         !op.meta.indirect_flow_targets.has_value() &&
         !op.meta.indirect_memory_targets.has_value() &&
         !op.meta.logical_register_name.has_value() &&
         !op.meta.logical_indirect_memory_targets.has_value() &&
         !op.meta.logical_register_analysis && !op.meta.discarded_indirect_recall_value &&
         !op.meta.borrowed_entry_phase_selector;
}

bool same_linear_scope(const IrOp& left, const IrOp& right) {
  return left.procedure_name == right.procedure_name && left.hidden == right.hidden;
}

bool is_x_preserving_entry_closing_store(const IrOp& op, const IrOp& digit) {
  return op.kind == IrKind::Store && !has_rewrite_barrier(op) &&
         !is_display_focus_sensitive(op) && !op.meta.manual_interaction.has_value() &&
         same_linear_scope(op, digit);
}

bool leaves_number_entry_open(const IrOp& op) {
  if (op.kind == IrKind::Plain && op.opcode >= 0x00 && op.opcode <= 0x0c)
    return true;
  return op.kind == IrKind::Stop &&
         (op.semantic == "input" || op.meta.stop_disposition == StopDisposition::Resumable ||
          op.meta.manual_interaction.has_value());
}

bool number_entry_is_closed_before(const std::vector<IrOp>& ops, int index,
                                   const IrOp& digit,
                                   const std::set<int>& label_entries) {
  int cursor = index - 1;
  while (cursor >= 0 && ops.at(static_cast<std::size_t>(cursor)).kind == IrKind::Label) {
    if (label_entries.contains(cursor))
      return false;
    --cursor;
  }
  if (cursor < 0)
    return true;
  const IrOp& previous = ops.at(static_cast<std::size_t>(cursor));
  if (previous.kind == IrKind::OrphanAddress || !same_linear_scope(previous, digit)) {
    return false;
  }
  return !leaves_number_entry_open(previous);
}

std::optional<int> previous_same_digit_through_stores(const std::vector<IrOp>& ops,
                                                      int reload_index,
                                                      const std::set<int>& label_entries) {
  const IrOp& reload = ops.at(static_cast<std::size_t>(reload_index));
  int cursor = reload_index - 1;
  bool crossed_store = false;
  while (cursor >= 0) {
    const IrOp& op = ops.at(static_cast<std::size_t>(cursor));
    if (op.kind == IrKind::Label) {
      if (label_entries.contains(cursor))
        return std::nullopt;
      --cursor;
      continue;
    }
    if (is_x_preserving_entry_closing_store(op, reload)) {
      crossed_store = true;
      --cursor;
      continue;
    }
    break;
  }
  if (!crossed_store || cursor < 0)
    return std::nullopt;

  const IrOp& producer = ops.at(static_cast<std::size_t>(cursor));
  if (!is_rewrite_safe_digit(producer) || producer.opcode != reload.opcode ||
      !same_linear_scope(producer, reload) ||
      !number_entry_is_closed_before(ops, cursor, producer, label_entries)) {
    return std::nullopt;
  }
  return cursor;
}

bool reload_entry_is_not_observable(const std::vector<IrOp>& ops, int reload_index,
                                    const std::set<int>& label_entries) {
  int next = reload_index + 1;
  while (next < static_cast<int>(ops.size()) &&
         ops.at(static_cast<std::size_t>(next)).kind == IrKind::Label) {
    if (label_entries.contains(next))
      return false;
    ++next;
  }
  if (next >= static_cast<int>(ops.size()))
    return false;
  return is_x_preserving_entry_closing_store(
      ops.at(static_cast<std::size_t>(next)),
      ops.at(static_cast<std::size_t>(reload_index)));
}

std::set<int> explicit_label_entry_indexes(const std::vector<IrOp>& ops) {
  std::set<std::string> string_targets;
  std::set<int> numeric_targets;
  for (const IrOp& op : ops) {
    switch (op.kind) {
    case IrKind::Jump:
    case IrKind::CondJump:
    case IrKind::Call:
    case IrKind::Loop:
      if (const std::string* label = std::get_if<std::string>(&op.target))
        string_targets.insert(*label);
      else if (const int* address = std::get_if<int>(&op.target))
        numeric_targets.insert(*address);
      break;
    case IrKind::IndirectJump:
    case IrKind::IndirectCall:
    case IrKind::IndirectCondJump:
      if (op.meta.indirect_flow_targets.has_value()) {
        for (const IrTarget& target : *op.meta.indirect_flow_targets) {
          if (const std::string* label = std::get_if<std::string>(&target))
            string_targets.insert(*label);
          else if (const int* address = std::get_if<int>(&target))
            numeric_targets.insert(*address);
        }
      } else if (const std::optional<int> target = known_indirect_flow_target(op);
                 target.has_value()) {
        numeric_targets.insert(*target);
      }
      for (const std::string& label : computed_dispatch_target_labels(op))
        string_targets.insert(label);
      break;
    case IrKind::Label:
    case IrKind::Store:
    case IrKind::Recall:
    case IrKind::IndirectStore:
    case IrKind::IndirectRecall:
    case IrKind::Return:
    case IrKind::Stop:
    case IrKind::Plain:
    case IrKind::OrphanAddress:
      break;
    }
  }

  std::set<int> result;
  int address = 0;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label) {
      if (op.procedure_boundary.has_value() || string_targets.contains(op.name) ||
          numeric_targets.contains(address)) {
        result.insert(index);
      }
      continue;
    }
    address += cells_per_op(op);
  }
  return result;
}

bool physical_flow_targets_stay_stable(const std::vector<IrOp>& ops,
                                       int reload_index) {
  int reload_address = 0;
  for (int index = 0; index < reload_index; ++index)
    reload_address += cells_per_op(ops.at(static_cast<std::size_t>(index)));

  const std::map<std::string, int> label_addresses = calculate_label_addresses(ops);
  const auto indirect_target_stays_stable = [&](const IrTarget& target) {
    const std::optional<int> address = target_address(target, label_addresses);
    return address.has_value() && *address < reload_address;
  };

  for (const IrOp& op : ops) {
    switch (op.kind) {
    case IrKind::Jump:
    case IrKind::CondJump:
    case IrKind::Call:
    case IrKind::Loop:
      if (const int* target = std::get_if<int>(&op.target);
          target != nullptr && *target >= reload_address) {
        return false;
      }
      break;
    case IrKind::IndirectJump:
    case IrKind::IndirectCall:
    case IrKind::IndirectCondJump: {
      bool proved_target = false;
      if (op.meta.indirect_flow_targets.has_value()) {
        if (op.meta.indirect_flow_targets->empty())
          return false;
        for (const IrTarget& target : *op.meta.indirect_flow_targets) {
          if (!indirect_target_stays_stable(target))
            return false;
          proved_target = true;
        }
      } else if (const std::optional<int> target = known_indirect_flow_target(op);
                 target.has_value()) {
        if (*target >= reload_address)
          return false;
        proved_target = true;
      }
      for (const std::string& label : computed_dispatch_target_labels(op)) {
        if (!indirect_target_stays_stable(IrTarget{label}))
          return false;
        proved_target = true;
      }
      if (!proved_target)
        return false;
      break;
    }
    case IrKind::Label:
    case IrKind::Store:
    case IrKind::Recall:
    case IrKind::IndirectStore:
    case IrKind::IndirectRecall:
    case IrKind::Return:
    case IrKind::Stop:
    case IrKind::Plain:
    case IrKind::OrphanAddress:
      break;
    }
  }
  return true;
}

} // namespace

PassResult redundant_literal_reload(const std::vector<IrOp>& ops,
                                    const PassContext& context) {
  (void)context;
  const std::set<int> label_entries = explicit_label_entry_indexes(ops);
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& reload = ops.at(static_cast<std::size_t>(index));
    const bool safe_digit = is_rewrite_safe_digit(reload);
    if (!safe_digit)
      continue;
    const bool repeated =
        previous_same_digit_through_stores(ops, index, label_entries).has_value();
    const bool entry_closed = reload_entry_is_not_observable(ops, index, label_entries);
    const bool targets_stable = physical_flow_targets_stay_stable(ops, index);
    const bool stack_exposed = removing_stack_lift_can_expose_stack(ops, index);
    const bool x2_exposed = removing_recall_can_expose_x2_restore(ops, index);
    if (!repeated || !entry_closed || !targets_stable || stack_exposed || x2_exposed)
      continue;

    std::vector<IrOp> result;
    result.reserve(ops.size() - 1U);
    result.insert(result.end(), ops.begin(), ops.begin() + index);
    result.insert(result.end(), ops.begin() + index + 1, ops.end());
    return PassResult{
        .ops = std::move(result),
        .applied = 1,
        .optimizations = {AppliedOptimization{
            .name = "redundant-literal-reload",
            .detail = "Removed one repeated one-cell literal reload after proving that "
                      "the visible X value is unchanged and the extra stack/X2 sync "
                      "converges before observation.",
        }},
    };
  }

  return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
}

IrPass redundant_literal_reload_pass() {
  return IrPass{
      .name = "redundant-literal-reload",
      .run = redundant_literal_reload,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
