#include "mkpro/core/passes/redundant_literal_reload.hpp"

#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

bool has_only_finalization_identity_roles(const std::vector<CellRole>& roles) {
  return std::all_of(roles.begin(), roles.end(), [](const CellRole& role) {
    return role.starts_with("finalization-cell-origin:");
  });
}

bool is_rewrite_safe_digit(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode >= 0x00 && op.opcode <= 0x09 &&
         !has_rewrite_barrier(op) && !is_display_focus_sensitive(op) &&
         has_only_finalization_identity_roles(op.meta.roles) &&
         has_only_finalization_identity_roles(op.target_meta.roles) &&
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
    if (const std::string* label = std::get_if<std::string>(&target)) {
      const auto address = label_addresses.find(*label);
      return address != label_addresses.end() && address->second < reload_address;
    }
    const int* address = std::get_if<int>(&target);
    return address != nullptr && *address < reload_address;
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

IrKind machine_opcode_kind(int opcode) {
  const std::vector<IrOp> raised =
      raise_machine_to_ir({MachineItem::op(opcode, opcode_by_code(opcode).name)});
  return raised.empty() ? IrKind::Plain : raised.front().kind;
}

std::optional<std::size_t> machine_item_at_address(
    const std::vector<MachineItem>& items, int target_address) {
  int address = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (items.at(item_index).kind == MachineItemKind::Label)
      continue;
    if (address == target_address)
      return item_index;
    ++address;
  }
  return std::nullopt;
}

AuthoritativePostLayoutControlFlow build_reload_equivalence_flow(
    const std::vector<MachineItem>& items) {
  AuthoritativePostLayoutControlFlow flow = build_post_layout_control_flow(items);
  if (flow.proved)
    return flow;

  PostLayoutControlFlowOptions options;
  options.empty_return_target = IrTarget{1};
  return build_post_layout_control_flow(items, options);
}

bool equality_state_merge(StackValueEqualityState& destination,
                          const StackValueEqualityState& incoming) {
  StackValueEqualityState merged = destination;
  for (std::size_t slot = 0; slot < merged.stack_equal.size(); ++slot)
    merged.stack_equal.at(slot) = destination.stack_equal.at(slot) && incoming.stack_equal.at(slot);
  merged.x2_equal = destination.x2_equal && incoming.x2_equal;
  if (merged.stack_equal == destination.stack_equal && merged.x2_equal == destination.x2_equal)
    return false;
  destination = merged;
  return true;
}

bool is_stack_equivalence_flow(IrKind kind) {
  switch (kind) {
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Loop:
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump:
    return true;
  case IrKind::Label:
  case IrKind::Store:
  case IrKind::Recall:
  case IrKind::IndirectStore:
  case IrKind::IndirectRecall:
  case IrKind::Return:
  case IrKind::Stop:
  case IrKind::Plain:
  case IrKind::OrphanAddress:
    return false;
  }
  return false;
}

// Compare the original reload with a same-size K NOP replacement.  The two
// executions have equal X, but the reload has introduced one extra stack lift
// and an X2 synchronization.  Exact execution states keep calls with distinct
// return stacks separate; the rewrite is accepted only when every reachable
// path erases that difference before arithmetic, a return, a stop, or any
// conditional whose visible X could differ.
bool repeated_literal_lift_converges_through_cfg(const std::vector<IrOp>& ops,
                                                 int reload_index) {
  const bool trace = std::getenv("MKPRO_NATIVE_TRACE_REDUNDANT_LITERAL_RELOAD") != nullptr;
  const auto reject = [&](const std::string& reason) {
    if (trace)
      std::cerr << "[redundant-literal-reload-proof] " << reason << "\n";
    return false;
  };
  int reload_address = 0;
  for (int index = 0; index < reload_index; ++index)
    reload_address += cells_per_op(ops.at(static_cast<std::size_t>(index)));

  std::vector<MachineItem> machine = lower_ir_to_machine(ops);
  const std::optional<std::size_t> reload_item =
      machine_item_at_address(machine, reload_address);
  if (!reload_item.has_value() || machine.at(*reload_item).kind != MachineItemKind::Op)
    return reject("reload item is not executable");

  MachineItem& replacement = machine.at(*reload_item);
  replacement.opcode = 0x54;
  replacement.mnemonic = opcode_by_code(0x54).name;

  const AuthoritativePostLayoutControlFlow flow = build_reload_equivalence_flow(machine);
  if (!flow.proved || flow.execution_states.size() != flow.execution_successors.size()) {
    std::string reason = "authoritative CFG failed";
    if (!flow.reasons.empty())
      reason += ": " + flow.reasons.front();
    return reject(reason);
  }

  std::vector<std::optional<StackValueEqualityState>> incoming(flow.execution_states.size());
  std::deque<std::size_t> worklist;
  const StackValueEqualityState after_removed_reload{
      .stack_equal = {true, false, false, false},
      .x2_equal = false,
  };

  bool seeded = false;
  for (std::size_t state_index = 0; state_index < flow.execution_states.size(); ++state_index) {
    if (flow.execution_states.at(state_index).address != reload_address)
      continue;
    if (flow.execution_successors.at(state_index).size() != 1U)
      return reject("reload does not have one fallthrough successor");
    const std::size_t successor = flow.execution_successors.at(state_index).front();
    if (successor >= incoming.size())
      return reject("reload successor is outside execution-state graph");
    if (!incoming.at(successor).has_value()) {
      incoming.at(successor) = after_removed_reload;
      worklist.push_back(successor);
    } else if (equality_state_merge(*incoming.at(successor), after_removed_reload)) {
      worklist.push_back(successor);
    }
    seeded = true;
  }
  if (!seeded) {
    if (trace)
      std::cerr << "[redundant-literal-reload-proof] reload is unreachable from every "
                   "authoritative entry; deletion is vacuously stack-safe\n";
    return true;
  }

  while (!worklist.empty()) {
    const std::size_t state_index = worklist.front();
    worklist.pop_front();
    if (!incoming.at(state_index).has_value())
      return reject("worklist state has no equality input");
    StackValueEqualityState state = *incoming.at(state_index);
    if (stack_values_fully_equal(state))
      continue;

    const PostLayoutExecutionState& execution = flow.execution_states.at(state_index);
    if (execution.item_index >= machine.size())
      return reject("execution state points outside machine items");
    const MachineItem& item = machine.at(execution.item_index);
    if (item.kind != MachineItemKind::Op)
      return reject("execution state reaches non-op item");

    const IrKind kind = machine_opcode_kind(item.opcode);
    if (kind == IrKind::Stop)
      return reject("unequal stack reaches stop at " + std::to_string(execution.address));
    if (kind == IrKind::Return) {
      if (transfer_stack_value_equality(state, item.opcode,
          StackValueEqualityStepKind::Plain) ==
          StackValueEqualityTransfer::Rejected) {
        return reject("unequal stack reaches return at " +
                      std::to_string(execution.address));
      }
    } else if (is_stack_equivalence_flow(kind)) {
      if ((kind == IrKind::CondJump || kind == IrKind::IndirectCondJump) &&
          !state.stack_equal.at(0)) {
        return reject("unequal X reaches conditional at " +
                      std::to_string(execution.address));
      }
      // Both executions have identical registers and return stacks.  With an
      // equal condition value they therefore take the same CFG edge.  Keeping
      // the prior X2 equality here is conservative for direct conditionals:
      // their fallthrough edge may synchronize X2, but never makes an equal
      // X2 pair unequal.
    } else {
      StackValueEqualityStepKind step_kind = StackValueEqualityStepKind::Plain;
      if (kind == IrKind::Recall || kind == IrKind::IndirectRecall)
        step_kind = StackValueEqualityStepKind::Recall;
      else if (kind == IrKind::Store || kind == IrKind::IndirectStore)
        step_kind = StackValueEqualityStepKind::Store;
      const StackValueEqualityTransfer transfer =
          transfer_stack_value_equality(state, item.opcode, step_kind);
      if (transfer == StackValueEqualityTransfer::Rejected)
        return reject("stack consumer observes difference at " +
                      std::to_string(execution.address) + " opcode=" +
                      std::to_string(item.opcode));
      if (transfer == StackValueEqualityTransfer::Converged)
        continue;
    }

    const std::vector<std::size_t>& successors = flow.execution_successors.at(state_index);
    if (successors.empty())
      return reject("unequal stack reaches terminal CFG state at " +
                    std::to_string(execution.address));
    for (const std::size_t successor : successors) {
      if (successor >= incoming.size())
        return reject("CFG successor is outside execution-state graph");
      if (!incoming.at(successor).has_value()) {
        incoming.at(successor) = state;
        worklist.push_back(successor);
      } else if (equality_state_merge(*incoming.at(successor), state)) {
        worklist.push_back(successor);
      }
    }
  }
  return true;
}

std::optional<std::string> selector_role_target(const MachineItem& item,
                                                std::string_view prefix) {
  std::optional<std::string> target;
  for (const CellRole& role : item.roles) {
    if (!role.starts_with(prefix))
      continue;
    const std::string value = role.substr(prefix.size());
    if (value.empty() || (target.has_value() && *target != value))
      return std::nullopt;
    target = value;
  }
  return target;
}

std::map<std::string, int> machine_label_addresses(
    const std::vector<MachineItem>& items) {
  std::map<std::string, int> labels;
  int address = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      labels.emplace(item.name, address);
    else
      ++address;
  }
  return labels;
}

bool x2_only_difference_converges_after_address(
    const std::vector<MachineItem>& items, int seed_address,
    int required_predecessor_address) {
  const bool trace = std::getenv("MKPRO_NATIVE_TRACE_REDUNDANT_LITERAL_RELOAD") != nullptr;
  const auto reject = [&](const std::string& reason) {
    if (trace)
      std::cerr << "[single-digit-selector-proof] " << reason << "\n";
    return false;
  };
  const AuthoritativePostLayoutControlFlow flow =
      build_reload_equivalence_flow(items);
  if (!flow.proved || flow.execution_states.size() != flow.execution_successors.size())
    return reject("authoritative CFG failed");

  std::vector<std::vector<std::size_t>> predecessors(flow.execution_states.size());
  for (std::size_t source = 0; source < flow.execution_successors.size(); ++source) {
    for (const std::size_t target : flow.execution_successors.at(source)) {
      if (target >= predecessors.size())
        return reject("CFG successor is outside execution-state graph");
      predecessors.at(target).push_back(source);
    }
  }

  std::vector<std::optional<StackValueEqualityState>> incoming(flow.execution_states.size());
  std::deque<std::size_t> worklist;
  const StackValueEqualityState x2_only_difference{
      .stack_equal = {true, true, true, true},
      .x2_equal = false,
  };
  bool seeded = false;
  for (std::size_t state_index = 0; state_index < flow.execution_states.size(); ++state_index) {
    if (flow.execution_states.at(state_index).address != seed_address)
      continue;
    const std::vector<std::size_t>& incoming_edges = predecessors.at(state_index);
    if (incoming_edges.empty())
      return reject("surviving digit is an independent external entry");
    for (const std::size_t predecessor : incoming_edges) {
      if (flow.execution_states.at(predecessor).address != required_predecessor_address)
        return reject("surviving digit has a non-leading-zero predecessor");
    }
    if (flow.execution_successors.at(state_index).size() != 1U)
      return reject("surviving digit does not have one fallthrough successor");
    const std::size_t successor = flow.execution_successors.at(state_index).front();
    if (!incoming.at(successor).has_value()) {
      incoming.at(successor) = x2_only_difference;
      worklist.push_back(successor);
    } else if (equality_state_merge(*incoming.at(successor), x2_only_difference)) {
      worklist.push_back(successor);
    }
    seeded = true;
  }
  if (!seeded)
    return reject("selector charge is unreachable from every admitted entry");

  while (!worklist.empty()) {
    const std::size_t state_index = worklist.front();
    worklist.pop_front();
    if (!incoming.at(state_index).has_value())
      return reject("worklist state has no equality input");
    StackValueEqualityState state = *incoming.at(state_index);
    if (stack_values_fully_equal(state))
      continue;

    const PostLayoutExecutionState& execution = flow.execution_states.at(state_index);
    if (execution.item_index >= items.size() ||
        items.at(execution.item_index).kind != MachineItemKind::Op) {
      return reject("execution state points outside executable items");
    }
    const MachineItem& item = items.at(execution.item_index);
    const IrKind kind = machine_opcode_kind(item.opcode);
    if (kind == IrKind::Stop)
      return reject("unequal X2 reaches stop at " + std::to_string(execution.address));
    if (kind == IrKind::Return) {
      if (transfer_stack_value_equality(state, item.opcode,
                                        StackValueEqualityStepKind::Plain) ==
          StackValueEqualityTransfer::Rejected) {
        return reject("unequal X2 reaches return at " +
                      std::to_string(execution.address));
      }
    } else if (!is_stack_equivalence_flow(kind)) {
      StackValueEqualityStepKind step_kind = StackValueEqualityStepKind::Plain;
      if (kind == IrKind::Recall || kind == IrKind::IndirectRecall)
        step_kind = StackValueEqualityStepKind::Recall;
      else if (kind == IrKind::Store || kind == IrKind::IndirectStore)
        step_kind = StackValueEqualityStepKind::Store;
      const StackValueEqualityTransfer transfer =
          transfer_stack_value_equality(state, item.opcode, step_kind);
      if (transfer == StackValueEqualityTransfer::Rejected) {
        return reject("X2 consumer observes leading-zero spelling at " +
                      std::to_string(execution.address));
      }
      if (transfer == StackValueEqualityTransfer::Converged)
        continue;
    }

    const std::vector<std::size_t>& successors = flow.execution_successors.at(state_index);
    if (successors.empty())
      return reject("unequal X2 reaches terminal CFG state");
    for (const std::size_t successor : successors) {
      if (successor >= incoming.size())
        return reject("CFG successor is outside execution-state graph");
      if (!incoming.at(successor).has_value()) {
        incoming.at(successor) = state;
        worklist.push_back(successor);
      } else if (equality_state_merge(*incoming.at(successor), state)) {
        worklist.push_back(successor);
      }
    }
  }
  return true;
}

} // namespace

std::optional<std::size_t>
post_layout_redundant_literal_reload_item(const std::vector<MachineItem>& items) {
  const std::vector<IrOp> ops = raise_machine_to_ir(items);
  const std::set<int> label_entries = explicit_label_entry_indexes(ops);
  const bool trace = std::getenv("MKPRO_NATIVE_TRACE_REDUNDANT_LITERAL_RELOAD") != nullptr;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& reload = ops.at(static_cast<std::size_t>(index));
    const bool safe = is_rewrite_safe_digit(reload);
    if (!safe)
      continue;
    const bool repeated =
        previous_same_digit_through_stores(ops, index, label_entries).has_value();
    const bool entry_closed = reload_entry_is_not_observable(ops, index, label_entries);
    const bool cfg = repeated && entry_closed &&
                     repeated_literal_lift_converges_through_cfg(ops, index);
    if (trace) {
      int address = 0;
      for (int prior = 0; prior < index; ++prior)
        address += cells_per_op(ops.at(static_cast<std::size_t>(prior)));
      std::cerr << "[redundant-literal-reload] address=" << address
                << " digit=" << reload.opcode << " repeated=" << repeated
                << " entry=" << entry_closed << " cfg=" << cfg << "\n";
    }
    if (!repeated || !entry_closed || !cfg)
      continue;

    int reload_address = 0;
    for (int prior = 0; prior < index; ++prior)
      reload_address += cells_per_op(ops.at(static_cast<std::size_t>(prior)));
    const std::optional<std::size_t> item = machine_item_at_address(items, reload_address);
    if (item.has_value() && items.at(*item).kind == MachineItemKind::Op &&
        items.at(*item).opcode == reload.opcode) {
      return item;
    }
  }
  return std::nullopt;
}

std::optional<SingleDigitLateSelectorPlan>
post_layout_single_digit_late_selector_plan(const std::vector<MachineItem>& items) {
  constexpr std::string_view kHighPrefix = "late-decimal-selector-high:";
  constexpr std::string_view kLowPrefix = "late-decimal-selector-low:";
  const std::map<std::string, int> labels = machine_label_addresses(items);
  int high_address = 0;
  for (std::size_t high_item = 0; high_item + 1U < items.size(); ++high_item) {
    const MachineItem& high = items.at(high_item);
    if (high.kind == MachineItemKind::Label)
      continue;
    const int current_address = high_address++;
    const std::optional<std::string> target =
        selector_role_target(high, kHighPrefix);
    if (!target.has_value() || high.kind != MachineItemKind::Op || high.raw ||
        high.opcode != 0x00 || high.mnemonic != "0") {
      continue;
    }
    std::size_t low_item = high_item + 1U;
    while (low_item < items.size() &&
           items.at(low_item).kind == MachineItemKind::Label) {
      ++low_item;
    }
    if (low_item >= items.size())
      continue;
    const MachineItem& low = items.at(low_item);
    const std::optional<std::string> low_target =
        selector_role_target(low, kLowPrefix);
    if (!low_target.has_value() || *low_target != *target ||
        low.kind != MachineItemKind::Op || low.raw || low.opcode < 0x00 ||
        low.opcode > 0x09 || low.mnemonic != std::to_string(low.opcode)) {
      continue;
    }
    const auto label = labels.find(*target);
    if (label == labels.end() || label->second < 0 || label->second > 9 ||
        low.opcode != label->second || current_address <= 0)
      continue;
    const std::optional<std::size_t> previous_item =
        machine_item_at_address(items, current_address - 1);
    if (!previous_item.has_value() ||
        items.at(*previous_item).kind != MachineItemKind::Op ||
        items.at(*previous_item).opcode <= 0x0c) {
      continue;
    }
    if (!x2_only_difference_converges_after_address(
            items, current_address + 1, current_address)) {
      continue;
    }
    return SingleDigitLateSelectorPlan{
        .leading_zero_item = high_item,
        .low_digit_item = low_item,
        .leading_zero_address = current_address,
        .target_address = label->second,
        .target_label = *target,
    };
  }
  return std::nullopt;
}

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
    const bool legacy_proof = !stack_exposed && !x2_exposed;
    const bool cfg_proof = !legacy_proof && repeated && entry_closed && targets_stable &&
                           repeated_literal_lift_converges_through_cfg(ops, index);
    if (!repeated || !entry_closed || !targets_stable || (!legacy_proof && !cfg_proof))
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
                      "converges before observation" +
                      std::string(cfg_proof ? " across the exact call/return CFG." : "."),
        }},
    };
  }

  return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
}

PassResult finalization_redundant_literal_reload(
    const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  const std::vector<MachineItem> items = lower_ir_to_machine(ops);
  const std::optional<std::size_t> reload =
      post_layout_redundant_literal_reload_item(items);
  if (!reload.has_value())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  int erased_address = 0;
  for (std::size_t item_index = 0; item_index < *reload; ++item_index) {
    if (items.at(item_index).kind != MachineItemKind::Label)
      ++erased_address;
  }
  int address = 0;
  for (std::size_t op_index = 0; op_index < ops.size(); ++op_index) {
    if (ops.at(op_index).kind == IrKind::Label)
      continue;
    if (address == erased_address) {
      if (cells_per_op(ops.at(op_index)) != 1)
        break;
      std::vector<IrOp> result = ops;
      result.erase(result.begin() + static_cast<std::ptrdiff_t>(op_index));
      return PassResult{
          .ops = std::move(result),
          .applied = 1,
          .optimizations =
              {
                  AppliedOptimization{
                      .name = "finalization-redundant-literal-reload",
                      .detail = "Removed one repeated digit reload after exact "
                                "interprocedural stack/X2 convergence; final layout "
                                "must re-prove every direct, indirect, and dual-use "
                                "selector target.",
                  },
              },
      };
    }
    address += cells_per_op(ops.at(op_index));
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
