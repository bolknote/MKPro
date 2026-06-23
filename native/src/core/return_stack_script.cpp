#include "mkpro/core/return_stack_script.hpp"

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core {

namespace {

constexpr int kReturnStackDepth = 5;
constexpr int kMaxScriptReturns = 5;
constexpr int kMaxRewriteRounds = 8;

struct MachineLayout {
  std::map<std::string, int> labels;
  std::map<int, int> item_index_by_address;
  std::map<int, int> address_by_item_index;
};

struct FlowReference {
  int op_index = 0;
  int address_index = 0;
  int source_address = 0;
  int target_address = 0;
  int opcode = 0;
};

struct CallSite {
  int op_index = 0;
  int address_index = 0;
  int source_address = 0;
  int target_address = 0;
  int continuation_address = 0;
};

struct TerminalJump {
  int op_index = 0;
  int address_index = 0;
  int source_address = 0;
  int target_address = 0;
};

struct ScriptPlan {
  std::vector<CallSite> calls;
  std::vector<TerminalJump> jumps;
  std::optional<TerminalJump> dirty_jump;
};

bool is_address_taking_opcode(int opcode) {
  switch (opcode) {
  case 0x51:
  case 0x53:
  case 0x57:
  case 0x58:
  case 0x59:
  case 0x5a:
  case 0x5b:
  case 0x5c:
  case 0x5d:
  case 0x5e:
    return true;
  default:
    return false;
  }
}

bool is_indirect_flow_opcode(int opcode) {
  const int base = opcode & 0xf0;
  return base == 0x70 || base == 0x80 || base == 0x90 || base == 0xa0 || base == 0xc0 ||
         base == 0xe0;
}

bool is_proven_indirect_call_opcode(int opcode) {
  return (opcode & 0xf0) == 0xa0;
}

std::optional<int> proven_indirect_target_from_comment(const MachineItem& item) {
  if (!item.comment.has_value())
    return std::nullopt;

  constexpr const char* kMarker = "indirect-target=";
  const std::string& comment = *item.comment;
  const std::size_t marker = comment.find(kMarker);
  if (marker == std::string::npos)
    return std::nullopt;

  std::size_t cursor = marker + std::string(kMarker).size();
  int value = 0;
  bool saw_digit = false;
  while (cursor < comment.size() && comment.at(cursor) >= '0' && comment.at(cursor) <= '9') {
    saw_digit = true;
    value = value * 10 + (comment.at(cursor) - '0');
    ++cursor;
  }
  if (!saw_digit)
    return std::nullopt;
  return value;
}

MachineLayout machine_layout(const std::vector<MachineItem>& items) {
  MachineLayout layout;
  int address = 0;
  for (int index = 0; index < static_cast<int>(items.size()); ++index) {
    const MachineItem& item = items.at(static_cast<std::size_t>(index));
    if (item.kind == MachineItemKind::Label) {
      layout.labels[item.name] = address;
      continue;
    }
    layout.item_index_by_address[address] = index;
    layout.address_by_item_index[index] = address;
    ++address;
  }
  return layout;
}

std::optional<int> resolved_target(const MachineItem& address,
                                   const MachineLayout& layout) {
  if (address.kind != MachineItemKind::Address)
    return std::nullopt;
  if (const auto* numeric = std::get_if<int>(&address.target))
    return *numeric;
  const auto* label = std::get_if<std::string>(&address.target);
  if (label == nullptr)
    return std::nullopt;
  const auto label_it = layout.labels.find(*label);
  if (label_it == layout.labels.end())
    return std::nullopt;
  return label_it->second;
}

bool address_target_is_label(const MachineItem& address) {
  return address.kind == MachineItemKind::Address &&
         std::holds_alternative<std::string>(address.target);
}

std::optional<FlowReference> direct_flow_reference_at(
    const std::vector<MachineItem>& items, const MachineLayout& layout, int op_index) {
  if (op_index < 0 || op_index + 1 >= static_cast<int>(items.size()))
    return std::nullopt;
  const MachineItem& op = items.at(static_cast<std::size_t>(op_index));
  const MachineItem& address = items.at(static_cast<std::size_t>(op_index + 1));
  if (op.kind != MachineItemKind::Op || address.kind != MachineItemKind::Address)
    return std::nullopt;
  if (!is_address_taking_opcode(op.opcode))
    return std::nullopt;
  const auto source_it = layout.address_by_item_index.find(op_index);
  if (source_it == layout.address_by_item_index.end())
    return std::nullopt;
  const std::optional<int> target = resolved_target(address, layout);
  if (!target.has_value())
    return std::nullopt;
  return FlowReference{
      .op_index = op_index,
      .address_index = op_index + 1,
      .source_address = source_it->second,
      .target_address = *target,
      .opcode = op.opcode,
  };
}

std::optional<FlowReference> proven_indirect_call_reference_at(
    const std::vector<MachineItem>& items, const MachineLayout& layout, int op_index) {
  if (op_index < 0 || op_index >= static_cast<int>(items.size()))
    return std::nullopt;
  const MachineItem& op = items.at(static_cast<std::size_t>(op_index));
  if (op.kind != MachineItemKind::Op || op.raw || !is_proven_indirect_call_opcode(op.opcode))
    return std::nullopt;
  const auto source_it = layout.address_by_item_index.find(op_index);
  if (source_it == layout.address_by_item_index.end())
    return std::nullopt;
  const std::optional<int> target = proven_indirect_target_from_comment(op);
  if (!target.has_value())
    return std::nullopt;
  return FlowReference{
      .op_index = op_index,
      .address_index = op_index,
      .source_address = source_it->second,
      .target_address = *target,
      .opcode = op.opcode,
  };
}

std::vector<FlowReference> direct_flow_references(const std::vector<MachineItem>& items,
                                                  const MachineLayout& layout) {
  std::vector<FlowReference> result;
  for (int index = 0; index + 1 < static_cast<int>(items.size()); ++index) {
    if (const std::optional<FlowReference> ref =
            direct_flow_reference_at(items, layout, index)) {
      result.push_back(*ref);
      continue;
    }
    if (const std::optional<FlowReference> ref =
            proven_indirect_call_reference_at(items, layout, index)) {
      result.push_back(*ref);
    }
  }
  return result;
}

std::map<int, std::vector<FlowReference>> incoming_by_target(
    const std::vector<FlowReference>& refs) {
  std::map<int, std::vector<FlowReference>> incoming;
  for (const FlowReference& ref : refs)
    incoming[ref.target_address].push_back(ref);
  return incoming;
}

std::optional<CallSite> call_site_at_address(const std::vector<MachineItem>& items,
                                             const MachineLayout& layout, int address) {
  const auto item_it = layout.item_index_by_address.find(address);
  if (item_it == layout.item_index_by_address.end())
    return std::nullopt;
  const int op_index = item_it->second;
  const MachineItem& op = items.at(static_cast<std::size_t>(op_index));
  if (op.kind != MachineItemKind::Op || op.raw)
    return std::nullopt;

  if (op.opcode == 0x53) {
    if (op_index + 1 >= static_cast<int>(items.size()))
      return std::nullopt;
    const MachineItem& target = items.at(static_cast<std::size_t>(op_index + 1));
    if (target.kind != MachineItemKind::Address || !address_target_is_label(target))
      return std::nullopt;
    const std::optional<int> target_address = resolved_target(target, layout);
    if (!target_address.has_value())
      return std::nullopt;
    return CallSite{
        .op_index = op_index,
        .address_index = op_index + 1,
        .source_address = address,
        .target_address = *target_address,
        .continuation_address = address + 2,
    };
  }

  if (!is_proven_indirect_call_opcode(op.opcode))
    return std::nullopt;
  const std::optional<int> target_address = proven_indirect_target_from_comment(op);
  if (!target_address.has_value()) {
    return std::nullopt;
  }
  return CallSite{
      .op_index = op_index,
      .address_index = op_index,
      .source_address = address,
      .target_address = *target_address,
      .continuation_address = address + 1,
  };
}

std::optional<TerminalJump> terminal_jump_from(const std::vector<MachineItem>& items,
                                               const MachineLayout& layout, int start_address) {
  std::set<int> seen;
  int address = start_address;
  for (int steps = 0; steps < machine_cell_count(items) + 1; ++steps) {
    if (seen.contains(address))
      return std::nullopt;
    seen.insert(address);
    const auto item_it = layout.item_index_by_address.find(address);
    if (item_it == layout.item_index_by_address.end())
      return std::nullopt;
    const int op_index = item_it->second;
    const MachineItem& item = items.at(static_cast<std::size_t>(op_index));
    if (item.kind != MachineItemKind::Op)
      return std::nullopt;
    if (item.raw)
      return std::nullopt;
    if (item.opcode == 0x51) {
      if (op_index + 1 >= static_cast<int>(items.size()))
        return std::nullopt;
      const MachineItem& target = items.at(static_cast<std::size_t>(op_index + 1));
      if (target.kind != MachineItemKind::Address || !address_target_is_label(target))
        return std::nullopt;
      const std::optional<int> target_address = resolved_target(target, layout);
      if (!target_address.has_value())
        return std::nullopt;
      return TerminalJump{
          .op_index = op_index,
          .address_index = op_index + 1,
          .source_address = address,
          .target_address = *target_address,
      };
    }
    if (item.opcode == 0x50 || item.opcode == 0x52 || item.opcode == 0x53 ||
        is_address_taking_opcode(item.opcode) || is_indirect_flow_opcode(item.opcode)) {
      return std::nullopt;
    }
    ++address;
  }
  return std::nullopt;
}

bool terminal_return_from(const std::vector<MachineItem>& items, const MachineLayout& layout,
                          int start_address) {
  std::set<int> seen;
  int address = start_address;
  for (int steps = 0; steps < machine_cell_count(items) + 1; ++steps) {
    if (seen.contains(address))
      return false;
    seen.insert(address);
    const auto item_it = layout.item_index_by_address.find(address);
    if (item_it == layout.item_index_by_address.end())
      return false;
    const MachineItem& item = items.at(static_cast<std::size_t>(item_it->second));
    if (item.kind != MachineItemKind::Op || item.raw)
      return false;
    if (item.opcode == 0x52)
      return true;
    if (item.opcode == 0x50 || item.opcode == 0x51 || item.opcode == 0x53 ||
        is_address_taking_opcode(item.opcode) || is_indirect_flow_opcode(item.opcode)) {
      return false;
    }
    ++address;
  }
  return false;
}

bool terminal_stop_from(const std::vector<MachineItem>& items, const MachineLayout& layout,
                        int start_address) {
  std::set<int> seen;
  int address = start_address;
  for (int steps = 0; steps < machine_cell_count(items) + 1; ++steps) {
    if (seen.contains(address))
      return false;
    seen.insert(address);
    const auto item_it = layout.item_index_by_address.find(address);
    if (item_it == layout.item_index_by_address.end())
      return false;
    const MachineItem& item = items.at(static_cast<std::size_t>(item_it->second));
    if (item.kind != MachineItemKind::Op || item.raw)
      return false;
    if (item.opcode == 0x50)
      return true;
    if (item.opcode == 0x53) {
      const std::optional<CallSite> call = call_site_at_address(items, layout, address);
      if (!call.has_value() || !terminal_return_from(items, layout, call->target_address))
        return false;
      address = call->continuation_address;
      continue;
    }
    if (item.opcode == 0x52 || is_address_taking_opcode(item.opcode) ||
        is_indirect_flow_opcode(item.opcode)) {
      return false;
    }
    ++address;
  }
  return false;
}

bool incoming_refs_are_exactly(const std::map<int, std::vector<FlowReference>>& incoming,
                               int target_address, const std::set<int>& allowed_op_indexes) {
  const auto incoming_it = incoming.find(target_address);
  if (incoming_it == incoming.end())
    return allowed_op_indexes.empty();
  if (incoming_it->second.size() != allowed_op_indexes.size())
    return false;
  for (const FlowReference& ref : incoming_it->second) {
    if (!allowed_op_indexes.contains(ref.op_index))
      return false;
  }
  return true;
}

bool script_plan_has_unique_incoming(const ScriptPlan& plan,
                                     const std::map<int, std::vector<FlowReference>>& incoming,
                                     int script_start_address) {
  if (plan.calls.empty() || plan.jumps.empty())
    return false;

  for (std::size_t index = 1; index < plan.calls.size(); ++index) {
    if (!incoming_refs_are_exactly(
            incoming, plan.calls.at(index).source_address,
            {plan.calls.at(index - 1U).op_index})) {
      return false;
    }
  }

  if (!incoming_refs_are_exactly(incoming, script_start_address,
                                 {plan.calls.back().op_index})) {
    return false;
  }

  for (std::size_t index = 0; index < plan.jumps.size(); ++index) {
    const int target = plan.calls.at(plan.calls.size() - 1U - index).continuation_address;
    if (!incoming_refs_are_exactly(incoming, target, {plan.jumps.at(index).op_index}))
      return false;
  }

  return true;
}

std::optional<ScriptPlan> script_plan_from_call_chain(
    const std::vector<MachineItem>& items, const MachineLayout& layout,
    const std::map<int, std::vector<FlowReference>>& incoming, int start_address) {
  std::vector<CallSite> calls;
  std::set<int> seen_call_addresses;
  int address = start_address;
  for (int depth = 0; depth < kMaxScriptReturns; ++depth) {
    if (seen_call_addresses.contains(address))
      return std::nullopt;
    const std::optional<CallSite> call = call_site_at_address(items, layout, address);
    if (!call.has_value())
      return std::nullopt;
    seen_call_addresses.insert(address);
    calls.push_back(*call);
    address = call->target_address;
    if (!call_site_at_address(items, layout, address).has_value())
      break;
  }

  if (calls.size() < 2 || calls.size() > static_cast<std::size_t>(kMaxScriptReturns))
    return std::nullopt;

  const int script_start_address = address;
  std::vector<TerminalJump> jumps;
  int cursor = script_start_address;
  for (std::size_t index = 0; index < calls.size(); ++index) {
    const int expected_target = calls.at(calls.size() - 1U - index).continuation_address;
    const std::optional<TerminalJump> jump = terminal_jump_from(items, layout, cursor);
    if (!jump.has_value() || jump->target_address != expected_target)
      return std::nullopt;
    jumps.push_back(*jump);
    cursor = expected_target;
  }

  std::optional<TerminalJump> dirty_jump;
  if (!terminal_stop_from(items, layout, cursor)) {
    if (calls.size() != static_cast<std::size_t>(kMaxScriptReturns))
      return std::nullopt;
    dirty_jump = terminal_jump_from(items, layout, cursor);
    if (!dirty_jump.has_value())
      return std::nullopt;
  }

  ScriptPlan plan{
      .calls = std::move(calls),
      .jumps = std::move(jumps),
      .dirty_jump = std::move(dirty_jump),
  };
  if (!script_plan_has_unique_incoming(plan, incoming, script_start_address))
    return std::nullopt;
  return plan;
}

std::optional<ScriptPlan> find_best_script_plan(const std::vector<MachineItem>& items) {
  const MachineLayout layout = machine_layout(items);
  const std::vector<FlowReference> refs = direct_flow_references(items, layout);
  const std::map<int, std::vector<FlowReference>> incoming = incoming_by_target(refs);

  std::optional<ScriptPlan> best;
  for (const auto& [address, item_index] : layout.item_index_by_address) {
    const MachineItem& item = items.at(static_cast<std::size_t>(item_index));
    if (item.kind != MachineItemKind::Op ||
        (item.opcode != 0x53 && !is_proven_indirect_call_opcode(item.opcode))) {
      continue;
    }
    const std::optional<ScriptPlan> plan =
        script_plan_from_call_chain(items, layout, incoming, address);
    if (!plan.has_value())
      continue;
    if (!best.has_value() || plan->jumps.size() > best->jumps.size())
      best = *plan;
  }
  return best;
}

std::string append_comment(std::optional<std::string> current, const std::string& suffix) {
  if (current.has_value() && !current->empty())
    return *current + "; " + suffix;
  return suffix;
}

std::vector<MachineItem> apply_script_plan(const std::vector<MachineItem>& items,
                                           const ScriptPlan& plan) {
  std::set<int> replace_op_indexes;
  std::set<int> remove_address_indexes;
  for (const TerminalJump& jump : plan.jumps) {
    replace_op_indexes.insert(jump.op_index);
    remove_address_indexes.insert(jump.address_index);
  }
  if (plan.dirty_jump.has_value()) {
    replace_op_indexes.insert(plan.dirty_jump->op_index);
    remove_address_indexes.insert(plan.dirty_jump->address_index);
  }

  std::vector<MachineItem> result;
  result.reserve(items.size() - remove_address_indexes.size());
  for (int index = 0; index < static_cast<int>(items.size()); ++index) {
    if (remove_address_indexes.contains(index))
      continue;
    MachineItem item = items.at(static_cast<std::size_t>(index));
    if (replace_op_indexes.contains(index)) {
      item.opcode = 0x52;
      item.mnemonic = "В/О";
      item.comment = append_comment(item.comment, "return-stack scripted continuation");
    }
    result.push_back(std::move(item));
  }
  return result;
}

int script_plan_rewrite_count(const ScriptPlan& plan) {
  return static_cast<int>(plan.jumps.size()) + (plan.dirty_jump.has_value() ? 1 : 0);
}

std::set<int> removed_address_indexes_for_script_plan(const ScriptPlan& plan) {
  std::set<int> result;
  for (const TerminalJump& jump : plan.jumps)
    result.insert(jump.address_index);
  if (plan.dirty_jump.has_value())
    result.insert(plan.dirty_jump->address_index);
  return result;
}

int adjusted_address_after_removing_indexes(const MachineLayout& layout, int address,
                                            const std::set<int>& removed_item_indexes) {
  int shift = 0;
  for (const int item_index : removed_item_indexes) {
    const auto address_it = layout.address_by_item_index.find(item_index);
    if (address_it != layout.address_by_item_index.end() && address_it->second < address)
      ++shift;
  }
  return address - shift;
}

std::vector<int> adjusted_return_stack_from_calls(const MachineLayout& layout,
                                                  const ScriptPlan& plan) {
  const std::set<int> removed = removed_address_indexes_for_script_plan(plan);
  std::vector<int> stack;
  stack.reserve(plan.calls.size());
  for (const CallSite& call : plan.calls) {
    const int adjusted_source =
        adjusted_address_after_removing_indexes(layout, call.source_address, removed);
    stack.push_back(adjusted_source + 1);
  }
  return stack;
}

bool dirty_overflow_script_plan_proved(const MachineLayout& original_layout,
                                       const std::vector<MachineItem>& candidate,
                                       const ScriptPlan& plan) {
  if (!plan.dirty_jump.has_value())
    return true;

  const std::set<int> removed = removed_address_indexes_for_script_plan(plan);
  const int adjusted_dirty_target = adjusted_address_after_removing_indexes(
      original_layout, plan.dirty_jump->target_address, removed);
  const DirtyReturnStackDispatchPlan dirty = plan_dirty_return_stack_dispatch(
      adjusted_return_stack_from_calls(original_layout, plan),
      static_cast<int>(plan.calls.size()) + 1, candidate,
      DirtyReturnStackDispatchOptions{.size_rescue = true});
  if (!dirty.enabled || !dirty.layout_proved || dirty.dirty_targets.empty())
    return false;
  return std::all_of(dirty.dirty_targets.begin(), dirty.dirty_targets.end(),
                     [&](const int target) { return target == adjusted_dirty_target; });
}

std::optional<DirtyReturnStackDispatchAllocationPlan> repair_dirty_overflow_script_candidate(
    const MachineLayout& original_layout, const std::vector<MachineItem>& candidate,
    const ScriptPlan& plan) {
  if (!plan.dirty_jump.has_value())
    return std::nullopt;

  const std::set<int> removed = removed_address_indexes_for_script_plan(plan);
  const int adjusted_dirty_target = adjusted_address_after_removing_indexes(
      original_layout, plan.dirty_jump->target_address, removed);
  DirtyReturnStackDispatchAllocationPlan allocation =
      allocate_dirty_return_stack_dispatch_layout(
          adjusted_return_stack_from_calls(original_layout, plan),
          static_cast<int>(plan.calls.size()) + 1, candidate,
          DirtyReturnStackDispatchOptions{.size_rescue = true});
  if (!allocation.allocated || !allocation.dispatch.layout_proved)
    return std::nullopt;
  if (allocation.dispatch.dirty_targets.empty())
    return std::nullopt;
  const bool targets_match =
      std::all_of(allocation.dispatch.dirty_targets.begin(),
                  allocation.dispatch.dirty_targets.end(),
                  [&](const int target) { return target == adjusted_dirty_target; });
  if (!targets_match)
    return std::nullopt;
  return allocation;
}

int best_cell_count_with_address_overlay(const std::vector<MachineItem>& items) {
  const PostLayoutIndirectFlowResult overlay = optimize_post_layout_address_code_overlay(items);
  return machine_cell_count(overlay.items);
}

bool return_stack_candidate_beats_address_overlay(const std::vector<MachineItem>& current,
                                                  const std::vector<MachineItem>& candidate) {
  const int current_best = best_cell_count_with_address_overlay(current);
  const int candidate_best = best_cell_count_with_address_overlay(candidate);
  return candidate_best < current_best;
}

void append_ir_items(std::vector<MachineItem>& out, const std::vector<IrOp>& ops) {
  std::vector<MachineItem> lowered = lower_ir_to_machine(ops);
  out.insert(out.end(), lowered.begin(), lowered.end());
}

std::string charge_label_name(std::size_t index) {
  return "__return_stack_charge_" + std::to_string(index);
}

std::optional<std::string> terminal_jump_target_from_ir_body(
    const std::vector<IrOp>& body) {
  if (body.empty())
    return std::nullopt;
  const IrOp& tail = body.back();
  if (tail.kind != IrKind::Jump && tail.opcode != 0x51)
    return std::nullopt;
  const auto* label = std::get_if<std::string>(&tail.target);
  if (label != nullptr && !label->empty())
    return *label;
  return std::nullopt;
}

bool terminal_stop_from_ir_body(const std::vector<IrOp>& body) {
  if (body.empty())
    return false;
  const IrOp& tail = body.back();
  return tail.kind == IrKind::Stop || tail.opcode == 0x50;
}

std::vector<std::size_t> default_tail_order(const ReturnStackLayoutOpportunity& opportunity) {
  std::vector<std::size_t> order;
  order.reserve(opportunity.tails.size());
  for (std::size_t index = 0; index < opportunity.tails.size(); ++index)
    order.push_back(index);
  return order;
}

std::optional<std::vector<std::size_t>> proved_tail_order_from_ir(
    const ReturnStackLayoutOpportunity& opportunity) {
  std::map<std::string, std::size_t> tail_by_label;
  for (std::size_t index = 0; index < opportunity.tails.size(); ++index) {
    const std::string& label = opportunity.tails.at(index).label;
    if (label.empty() || tail_by_label.contains(label))
      return std::nullopt;
    tail_by_label[label] = index;
  }

  std::optional<std::string> cursor =
      terminal_jump_target_from_ir_body(opportunity.entry_body);
  if (!cursor.has_value())
    return std::nullopt;

  std::vector<std::size_t> execution_order;
  std::set<std::string> seen_labels;
  while (cursor.has_value()) {
    const auto tail_it = tail_by_label.find(*cursor);
    if (tail_it == tail_by_label.end())
      return std::nullopt;
    if (seen_labels.contains(*cursor))
      return std::nullopt;
    seen_labels.insert(*cursor);
    execution_order.push_back(tail_it->second);

    const ReturnStackTailBlock& tail = opportunity.tails.at(tail_it->second);
    const std::optional<std::string> next =
        terminal_jump_target_from_ir_body(tail.body);
    if (next.has_value()) {
      cursor = next;
      continue;
    }
    if (!terminal_stop_from_ir_body(tail.body))
      return std::nullopt;
    break;
  }

  if (execution_order.size() != opportunity.tails.size())
    return std::nullopt;

  std::reverse(execution_order.begin(), execution_order.end());
  return execution_order;
}

std::map<std::string, ReturnStackExistingCallSite> existing_callsite_by_continuation(
    const ReturnStackLayoutOpportunity& opportunity) {
  std::map<std::string, ReturnStackExistingCallSite> result;
  std::set<std::string> used_labels;
  for (const ReturnStackExistingCallSite& site : opportunity.existing_call_sites) {
    if (site.label.empty() || site.continuation_label.empty() || site.target_label.empty())
      continue;
    if (!used_labels.insert(site.label).second)
      continue;
    if (result.contains(site.continuation_label))
      continue;
    result[site.continuation_label] = site;
  }
  return result;
}

std::string charge_label_for_physical_tail(
    const ReturnStackLayoutOpportunity& opportunity, const std::vector<std::size_t>& tail_order,
    std::size_t physical_index,
    const std::map<std::string, ReturnStackExistingCallSite>& existing_by_continuation) {
  const std::string& tail_label = opportunity.tails.at(tail_order.at(physical_index)).label;
  const auto existing_it = existing_by_continuation.find(tail_label);
  if (existing_it != existing_by_continuation.end())
    return existing_it->second.label;
  return charge_label_name(physical_index);
}

std::string charge_target_for_physical_tail(
    const ReturnStackLayoutOpportunity& opportunity, const std::vector<std::size_t>& tail_order,
    std::size_t physical_index,
    const std::map<std::string, ReturnStackExistingCallSite>& existing_by_continuation) {
  if (physical_index + 1U >= tail_order.size())
    return opportunity.entry_label;
  return charge_label_for_physical_tail(opportunity, tail_order, physical_index + 1U,
                                        existing_by_continuation);
}

std::vector<MachineItem> materialize_layout_items(
    const ReturnStackLayoutOpportunity& opportunity,
    const std::vector<std::size_t>& tail_order) {
  const std::map<std::string, ReturnStackExistingCallSite> existing_by_continuation =
      existing_callsite_by_continuation(opportunity);
  std::vector<MachineItem> items;
  for (std::size_t physical_index = 0; physical_index < tail_order.size();
       ++physical_index) {
    const ReturnStackTailBlock& tail =
        opportunity.tails.at(tail_order.at(physical_index));
    MachineItem charge_label = MachineItem::label(charge_label_for_physical_tail(
        opportunity, tail_order, physical_index, existing_by_continuation));
    charge_label.hidden = !existing_by_continuation.contains(tail.label);
    items.push_back(std::move(charge_label));

    items.push_back(MachineItem::op(0x53, "ПП"));
    items.push_back(MachineItem::address(charge_target_for_physical_tail(
        opportunity, tail_order, physical_index, existing_by_continuation)));

    items.push_back(MachineItem::label(tail.label));
    append_ir_items(items, tail.body);
  }

  items.push_back(MachineItem::label(opportunity.entry_label));
  append_ir_items(items, opportunity.entry_body);
  return items;
}

struct IrLabelBlock {
  std::string label;
  std::vector<IrOp> body;
  bool hidden = false;
};

struct IrCfg {
  std::map<std::string, std::set<std::string>> predecessors;
  std::map<std::string, std::set<std::string>> successors;
};

struct IrTailChainCandidate {
  ReturnStackLayoutOpportunity opportunity;
  std::set<std::string> moved_tail_labels;
  std::string original_entry_label;
  std::string generated_entry_label;
  std::size_t entry_block_index = 0;
  bool wrap_original_entry_label = true;
};

struct IrTailChainSearchStats {
  int entry_candidates = 0;
  int valid_chain_candidates = 0;
  int short_chain_candidates = 0;
  int too_long_chain_candidates = 0;
  int broken_chain_candidates = 0;
  int unresolved_chain_candidates = 0;
  int repeated_chain_candidates = 0;
  int nonterminal_chain_candidates = 0;
  std::vector<std::string> nonterminal_break_labels;
  int external_entry_rejections = 0;
};

enum class IrTailChainBrokenReason {
  None,
  Unresolved,
  Repeated,
  NonTerminal,
};

struct IrCallContinuation {
  std::size_t block_index = 0;
  std::set<std::string> alias_labels;
};

struct IrResolvedTailBlock {
  std::string label;
  std::vector<IrOp> body;
  std::set<std::string> moved_labels;
};

struct ExtractedIrFragments {
  int tail_fragments = 0;
  int existing_callsite_fragments = 0;

  int total() const {
    return tail_fragments + existing_callsite_fragments;
  }
};


bool return_stack_candidate_better(const std::optional<IrTailChainCandidate>& current,
                                   const IrTailChainCandidate& candidate) {
  if (!current.has_value())
    return true;
  const std::size_t candidate_existing =
      candidate.opportunity.existing_call_sites.size();
  const std::size_t current_existing =
      current->opportunity.existing_call_sites.size();
  if (candidate_existing != current_existing)
    return candidate_existing > current_existing;

  const std::size_t candidate_tails = candidate.opportunity.tails.size();
  const std::size_t current_tails = current->opportunity.tails.size();
  if (candidate_tails != current_tails)
    return candidate_tails > current_tails;

  return candidate.entry_block_index < current->entry_block_index;
}

void remember_return_stack_rejection(std::string& remembered,
                                     const std::string& candidate) {
  if (candidate.empty())
    return;
  const bool remembered_external =
      remembered.find("external CFG entry") != std::string::npos;
  const bool candidate_external =
      candidate.find("external CFG entry") != std::string::npos;
  if (remembered.empty() || (!remembered_external && candidate_external))
    remembered = candidate;
}

std::string unique_synthetic_entry_label(const std::set<std::string>& labels) {
  std::string candidate = "__return_stack_synthetic_entry";
  int suffix = 0;
  while (labels.contains(candidate)) {
    ++suffix;
    candidate = "__return_stack_synthetic_entry_" + std::to_string(suffix);
  }
  return candidate;
}

std::string unique_internal_basic_block_label(const std::set<std::string>& labels,
                                              int ordinal) {
  std::string candidate = "__return_stack_basic_block_" + std::to_string(ordinal);
  int suffix = ordinal;
  while (labels.contains(candidate)) {
    ++suffix;
    candidate = "__return_stack_basic_block_" + std::to_string(suffix);
  }
  return candidate;
}

bool raw_indirect_jump_opcode(int opcode) {
  return opcode >= 0x80 && opcode <= 0x8e;
}

bool raw_indirect_call_opcode(int opcode) {
  return opcode >= 0xa0 && opcode <= 0xae;
}

bool raw_indirect_cond_jump_opcode(int opcode) {
  return (opcode >= 0x70 && opcode <= 0x7e) ||
         (opcode >= 0x90 && opcode <= 0x9e) ||
         (opcode >= 0xc0 && opcode <= 0xce) ||
         (opcode >= 0xe0 && opcode <= 0xee);
}

bool raw_indirect_control_opcode(int opcode) {
  return raw_indirect_jump_opcode(opcode) || raw_indirect_call_opcode(opcode) ||
         raw_indirect_cond_jump_opcode(opcode);
}

bool ir_op_splits_internal_basic_block(const IrOp& op) {
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
    return true;
  default:
    break;
  }
  switch (op.opcode) {
  case 0x50:
  case 0x51:
  case 0x52:
  case 0x53:
  case 0x57:
  case 0x58:
  case 0x59:
  case 0x5a:
  case 0x5b:
  case 0x5c:
  case 0x5d:
  case 0x5e:
    return true;
  default:
    return raw_indirect_control_opcode(op.opcode);
  }
}

void split_internal_basic_blocks(std::vector<IrLabelBlock>& blocks,
                                 std::set<std::string>& labels) {
  std::vector<IrLabelBlock> rewritten;
  rewritten.reserve(blocks.size());
  int synthetic_ordinal = 0;

  for (IrLabelBlock& block : blocks) {
    if (block.body.empty()) {
      rewritten.push_back(std::move(block));
      continue;
    }

    std::string current_label = block.label;
    bool current_hidden = block.hidden;
    std::vector<IrOp> current_body;
    for (std::size_t index = 0; index < block.body.size(); ++index) {
      current_body.push_back(std::move(block.body.at(index)));
      if (index + 1U >= block.body.size() ||
          !ir_op_splits_internal_basic_block(current_body.back())) {
        continue;
      }

      rewritten.push_back(IrLabelBlock{
          .label = std::move(current_label),
          .body = std::move(current_body),
          .hidden = current_hidden,
      });
      current_label = unique_internal_basic_block_label(labels, synthetic_ordinal++);
      labels.insert(current_label);
      current_hidden = true;
      current_body.clear();
    }

    if (!current_body.empty()) {
      rewritten.push_back(IrLabelBlock{
          .label = std::move(current_label),
          .body = std::move(current_body),
          .hidden = current_hidden,
      });
    }
  }

  blocks = std::move(rewritten);
}

std::optional<std::vector<IrLabelBlock>> split_label_blocks(const std::vector<IrOp>& ops,
                                                            std::string& rejection_reason) {
  std::set<std::string> declared_labels;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label && !op.name.empty())
      declared_labels.insert(op.name);
  }
  const std::string synthetic_entry = unique_synthetic_entry_label(declared_labels);

  std::vector<IrLabelBlock> blocks;
  std::set<std::string> labels;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label) {
      if (op.name.empty()) {
        rejection_reason = "return-stack IR tail layout requires named labels";
        return std::nullopt;
      }
      if (!labels.insert(op.name).second) {
        rejection_reason = "return-stack IR tail layout requires unique labels";
        return std::nullopt;
      }
      blocks.push_back(IrLabelBlock{.label = op.name, .hidden = op.hidden});
      continue;
    }
    if (blocks.empty()) {
      labels.insert(synthetic_entry);
      blocks.push_back(IrLabelBlock{.label = synthetic_entry, .hidden = true});
    }
      blocks.back().body.push_back(op);
    }
  split_internal_basic_blocks(blocks, labels);
  if (blocks.size() < 2U) {
    rejection_reason = "return-stack IR tail layout requires an entry block and at least one tail block";
    return std::nullopt;
  }
  return blocks;
}

std::map<std::string, std::size_t> block_index_by_label(const std::vector<IrLabelBlock>& blocks) {
  std::map<std::string, std::size_t> result;
  for (std::size_t index = 0; index < blocks.size(); ++index)
    result[blocks.at(index).label] = index;
  return result;
}

std::optional<std::string> ir_label_target(const IrOp& op) {
  const auto* label = std::get_if<std::string>(&op.target);
  if (label == nullptr || label->empty())
    return std::nullopt;
  return *label;
}

std::optional<std::string> direct_symbolic_call_target(const IrOp& op) {
  if (op.kind != IrKind::Call && op.opcode != 0x53)
    return std::nullopt;
  return ir_label_target(op);
}

bool ir_op_can_reference_label(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Loop:
    return true;
  default:
    break;
  }
  switch (op.opcode) {
  case 0x51:
  case 0x53:
  case 0x57:
  case 0x58:
  case 0x59:
  case 0x5a:
  case 0x5b:
  case 0x5c:
  case 0x5d:
  case 0x5e:
    return true;
  default:
    return false;
  }
}

bool ir_op_always_transfers_control(const IrOp& op) {
  if (op.kind == IrKind::Jump || op.kind == IrKind::IndirectJump ||
      op.kind == IrKind::Return || op.kind == IrKind::Stop) {
    return true;
  }
  return op.opcode == 0x50 || op.opcode == 0x51 || op.opcode == 0x52 ||
         raw_indirect_jump_opcode(op.opcode);
}

void add_cfg_edge(IrCfg& cfg, const std::string& from, const std::string& to) {
  if (from.empty() || to.empty())
    return;
  cfg.successors[from].insert(to);
  cfg.predecessors[to].insert(from);
}

IrCfg build_ir_cfg(const std::vector<IrLabelBlock>& blocks) {
  IrCfg cfg;
  const std::map<std::string, std::size_t> by_label = block_index_by_label(blocks);
  for (std::size_t index = 0; index < blocks.size(); ++index) {
    const IrLabelBlock& block = blocks.at(index);
    cfg.predecessors[block.label];
    cfg.successors[block.label];

    for (const IrOp& op : block.body) {
      if (!ir_op_can_reference_label(op))
        continue;
      const std::optional<std::string> target = ir_label_target(op);
      if (target.has_value() && by_label.contains(*target))
        add_cfg_edge(cfg, block.label, *target);
    }

    const bool has_fallthrough =
        block.body.empty() || !ir_op_always_transfers_control(block.body.back());
    if (has_fallthrough && index + 1U < blocks.size())
      add_cfg_edge(cfg, block.label, blocks.at(index + 1U).label);
  }
  return cfg;
}

IrOp synthetic_ir_jump_to_label(const std::string& target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = target;
  op.meta.mnemonic = "БП";
  op.meta.comment = "return-stack extracted tail fragment";
  return op;
}

bool terminal_tail_fragment_candidate(const IrLabelBlock& block) {
  if (block.body.size() < 3U)
    return false;
  return terminal_jump_target_from_ir_body(block.body).has_value() ||
         terminal_stop_from_ir_body(block.body);
}

std::optional<std::size_t> terminal_tail_fragment_suffix_start(
    const IrLabelBlock& block) {
  if (!terminal_tail_fragment_candidate(block))
    return std::nullopt;

  const std::size_t suffix_start = block.body.size() - 2U;
  if (ir_op_always_transfers_control(block.body.at(suffix_start - 1U)))
    return std::nullopt;

  for (std::size_t index = suffix_start; index + 1U < block.body.size(); ++index) {
    if (ir_op_always_transfers_control(block.body.at(index)))
      return std::nullopt;
  }
  return suffix_start;
}

bool terminal_call_fragment_candidate(const IrLabelBlock& block) {
  if (block.body.size() < 2U)
    return false;
  const IrOp& terminal = block.body.back();
  if (!direct_symbolic_call_target(terminal).has_value())
    return false;
  return !ir_op_always_transfers_control(block.body.at(block.body.size() - 2U));
}

std::string unique_tail_fragment_label(const std::set<std::string>& labels, int ordinal) {
  std::string candidate = "__return_stack_tail_fragment_" + std::to_string(ordinal);
  int suffix = ordinal;
  while (labels.contains(candidate)) {
    ++suffix;
    candidate = "__return_stack_tail_fragment_" + std::to_string(suffix);
  }
  return candidate;
}

std::string unique_callsite_fragment_label(const std::set<std::string>& labels, int ordinal) {
  std::string candidate = "__return_stack_callsite_fragment_" + std::to_string(ordinal);
  int suffix = ordinal;
  while (labels.contains(candidate)) {
    ++suffix;
    candidate = "__return_stack_callsite_fragment_" + std::to_string(suffix);
  }
  return candidate;
}

bool ir_meta_equal(const IrMeta& left, const IrMeta& right) {
  return left.mnemonic == right.mnemonic && left.comment == right.comment &&
         left.source_line == right.source_line && left.raw == right.raw &&
         left.roles == right.roles && left.tactic == right.tactic;
}

bool ir_target_meta_equal(const IrTargetMeta& left, const IrTargetMeta& right) {
  return left.comment == right.comment && left.source_line == right.source_line &&
         left.roles == right.roles && left.formal_opcode == right.formal_opcode;
}

bool ir_op_equal_for_tail_fragment(const IrOp& left, const IrOp& right) {
  return left.kind == right.kind && left.name == right.name &&
         left.procedure_boundary == right.procedure_boundary &&
         left.procedure_name == right.procedure_name && left.hidden == right.hidden &&
         left.register_name == right.register_name && left.condition == right.condition &&
         left.counter == right.counter && left.opcode == right.opcode &&
         left.target == right.target && left.semantic == right.semantic &&
         ir_meta_equal(left.meta, right.meta) &&
         ir_target_meta_equal(left.target_meta, right.target_meta);
}

bool ir_tail_fragment_body_equal(const std::vector<IrOp>& left,
                                 const std::vector<IrOp>& right) {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (!ir_op_equal_for_tail_fragment(left.at(index), right.at(index)))
      return false;
  }
  return true;
}

std::optional<std::string> existing_tail_fragment_label_for_body(
    const std::vector<IrLabelBlock>& fragments, const std::vector<IrOp>& body) {
  for (const IrLabelBlock& fragment : fragments) {
    if (ir_tail_fragment_body_equal(fragment.body, body))
      return fragment.label;
  }
  return std::nullopt;
}

bool valid_terminal_tail_fragment_suffix_start(const IrLabelBlock& block,
                                               std::size_t suffix_start) {
  if (!terminal_tail_fragment_candidate(block) || suffix_start == 0U ||
      suffix_start + 1U >= block.body.size()) {
    return false;
  }
  if (ir_op_always_transfers_control(block.body.at(suffix_start - 1U)))
    return false;

  for (std::size_t index = suffix_start; index + 1U < block.body.size(); ++index) {
    if (ir_op_always_transfers_control(block.body.at(index)))
      return false;
  }
  return true;
}

bool ir_tail_fragment_suffix_equal(const IrLabelBlock& left, std::size_t left_start,
                                   const IrLabelBlock& right, std::size_t right_start) {
  const std::size_t left_size = left.body.size() - left_start;
  if (right.body.size() - right_start != left_size)
    return false;
  for (std::size_t offset = 0; offset < left_size; ++offset) {
    if (!ir_op_equal_for_tail_fragment(left.body.at(left_start + offset),
                                       right.body.at(right_start + offset))) {
      return false;
    }
  }
  return true;
}

std::optional<std::size_t> common_terminal_tail_fragment_suffix_start(
    const std::vector<IrLabelBlock>& blocks, std::size_t block_index) {
  const IrLabelBlock& block = blocks.at(block_index);
  const std::optional<std::size_t> fallback = terminal_tail_fragment_suffix_start(block);
  if (!fallback.has_value())
    return std::nullopt;

  const std::size_t max_suffix_length = block.body.size() - 1U;
  for (std::size_t suffix_length = max_suffix_length; suffix_length >= 2U; --suffix_length) {
    const std::size_t suffix_start = block.body.size() - suffix_length;
    if (!valid_terminal_tail_fragment_suffix_start(block, suffix_start)) {
      if (suffix_length == 2U)
        break;
      continue;
    }

    bool shared = false;
    for (std::size_t other_index = 0; other_index < blocks.size(); ++other_index) {
      if (other_index == block_index)
        continue;
      const IrLabelBlock& other = blocks.at(other_index);
      if (other.body.size() <= suffix_length)
        continue;
      const std::size_t other_start = other.body.size() - suffix_length;
      if (!valid_terminal_tail_fragment_suffix_start(other, other_start))
        continue;
      if (ir_tail_fragment_suffix_equal(block, suffix_start, other, other_start)) {
        shared = true;
        break;
      }
    }
    if (shared)
      return suffix_start;
    if (suffix_length == 2U)
      break;
  }

  return fallback;
}

ExtractedIrFragments extract_terminal_tail_fragments(std::vector<IrLabelBlock>& blocks) {
  std::set<std::string> labels;
  for (const IrLabelBlock& block : blocks)
    labels.insert(block.label);

  const std::size_t original_count = blocks.size();
  std::vector<IrLabelBlock> rewritten;
  rewritten.reserve(original_count * 2U);
  std::vector<IrLabelBlock> appended_fragments;
  ExtractedIrFragments extracted;
  for (std::size_t index = 0; index < original_count; ++index) {
    IrLabelBlock block = blocks.at(index);
    if (terminal_call_fragment_candidate(block)) {
      const std::string fragment_label =
          unique_callsite_fragment_label(labels, extracted.existing_callsite_fragments);
      labels.insert(fragment_label);

      std::vector<IrOp> suffix;
      suffix.push_back(std::move(block.body.back()));
      block.body.pop_back();
      block.body.push_back(synthetic_ir_jump_to_label(fragment_label));

      rewritten.push_back(std::move(block));
      rewritten.push_back(IrLabelBlock{
          .label = fragment_label,
          .body = std::move(suffix),
          .hidden = true,
      });
      ++extracted.existing_callsite_fragments;
      continue;
    }

    const std::optional<std::size_t> suffix_start =
        common_terminal_tail_fragment_suffix_start(blocks, index);
    if (!suffix_start.has_value()) {
      rewritten.push_back(std::move(block));
      continue;
    }

    std::vector<IrOp> suffix(block.body.begin() + static_cast<std::ptrdiff_t>(*suffix_start),
                             block.body.end());
    std::string fragment_label;
    const std::optional<std::string> existing_fragment =
        existing_tail_fragment_label_for_body(appended_fragments, suffix);
    if (existing_fragment.has_value()) {
      fragment_label = *existing_fragment;
    } else {
      fragment_label = unique_tail_fragment_label(labels, extracted.total());
      labels.insert(fragment_label);
      appended_fragments.push_back(IrLabelBlock{
          .label = fragment_label,
          .body = suffix,
          .hidden = true,
      });
      ++extracted.tail_fragments;
    }

    block.body.erase(block.body.begin() + static_cast<std::ptrdiff_t>(*suffix_start),
                     block.body.end());
    block.body.push_back(synthetic_ir_jump_to_label(fragment_label));

    rewritten.push_back(std::move(block));
  }
  for (IrLabelBlock& fragment : appended_fragments)
    rewritten.push_back(std::move(fragment));
  blocks = std::move(rewritten);
  return extracted;
}

std::string cfg_canonical_symbolic_callsite_target_label(
    const std::vector<IrLabelBlock>& blocks, const std::map<std::string, std::size_t>& by_label,
    const IrCfg& cfg, const std::string& target_label) {
  std::string cursor = target_label;
  std::set<std::string> seen;
  while (true) {
    const auto block_it = by_label.find(cursor);
    if (block_it == by_label.end() || !seen.insert(cursor).second)
      return target_label;

    const IrLabelBlock& block = blocks.at(block_it->second);
    if (!block.body.empty())
      return block.label;

    const auto successor_it = cfg.successors.find(block.label);
    if (successor_it == cfg.successors.end() || successor_it->second.size() != 1U)
      return target_label;
    cursor = *successor_it->second.begin();
  }
}

std::vector<std::string> symbolic_existing_callsite_hint_targets(
    const std::vector<IrLabelBlock>& blocks) {
  const std::map<std::string, std::size_t> by_label = block_index_by_label(blocks);
  const IrCfg cfg = build_ir_cfg(blocks);
  std::set<std::string> targets;
  for (std::size_t index = 0; index + 1U < blocks.size(); ++index) {
    const IrLabelBlock& block = blocks.at(index);
    if (block.body.empty())
      continue;
    const IrOp& tail = block.body.back();
    const std::optional<std::string> target = direct_symbolic_call_target(tail);
    if (!target.has_value())
      continue;
    targets.insert(cfg_canonical_symbolic_callsite_target_label(blocks, by_label, cfg, *target));
  }
  return std::vector<std::string>(targets.begin(), targets.end());
}

std::map<std::string, int> symbolic_existing_callsite_hint_counts_by_target(
    const std::vector<IrLabelBlock>& blocks) {
  const std::map<std::string, std::size_t> by_label = block_index_by_label(blocks);
  const IrCfg cfg = build_ir_cfg(blocks);
  std::map<std::string, int> counts;
  for (std::size_t index = 0; index + 1U < blocks.size(); ++index) {
    const IrLabelBlock& block = blocks.at(index);
    if (block.body.empty())
      continue;
    const IrOp& tail = block.body.back();
    const std::optional<std::string> target = direct_symbolic_call_target(tail);
    if (!target.has_value())
      continue;
    ++counts[cfg_canonical_symbolic_callsite_target_label(blocks, by_label, cfg, *target)];
  }
  return counts;
}

int count_symbolic_existing_callsite_hints(const std::vector<IrLabelBlock>& blocks) {
  int hints = 0;
  for (std::size_t index = 0; index + 1U < blocks.size(); ++index) {
    const IrLabelBlock& block = blocks.at(index);
    if (block.body.empty())
      continue;
    const IrOp& tail = block.body.back();
    if (!direct_symbolic_call_target(tail).has_value())
      continue;
    ++hints;
  }
  return hints;
}

std::optional<std::string> single_call_block_target(const IrLabelBlock& block) {
  if (block.body.size() != 1U)
    return std::nullopt;
  return direct_symbolic_call_target(block.body.front());
}

bool noop_return_block(const IrLabelBlock& block) {
  if (block.body.size() != 1U)
    return false;
  const IrOp& op = block.body.front();
  return op.kind == IrKind::Return || op.opcode == 0x52;
}

std::string unique_same_target_entry_label(const std::set<std::string>& labels,
                                           int ordinal) {
  std::string candidate = "__return_stack_same_target_entry_" + std::to_string(ordinal);
  int suffix = ordinal;
  while (labels.contains(candidate)) {
    ++suffix;
    candidate = "__return_stack_same_target_entry_" + std::to_string(suffix);
  }
  return candidate;
}

std::vector<std::vector<std::size_t>> bounded_noop_call_subgroups(
    const std::vector<std::size_t>& call_indices) {
  std::vector<std::vector<std::size_t>> result;
  if (call_indices.size() < 2U)
    return result;

  constexpr std::size_t kMaxNoopSubgroups = 256U;
  const std::size_t max_size =
      std::min(call_indices.size(), static_cast<std::size_t>(kMaxScriptReturns));
  std::set<std::vector<std::size_t>> seen;
  auto add_group = [&](std::vector<std::size_t> group) {
    if (result.size() >= kMaxNoopSubgroups)
      return;
    if (seen.insert(group).second)
      result.push_back(std::move(group));
  };

  for (std::size_t size = max_size; size >= 2U; --size) {
    for (std::size_t start = 0; start + size <= call_indices.size(); ++start) {
      add_group(std::vector<std::size_t>(
          call_indices.begin() + static_cast<std::ptrdiff_t>(start),
          call_indices.begin() + static_cast<std::ptrdiff_t>(start + size)));
    }

    std::vector<std::size_t> current;
    auto choose = [&](auto& self, std::size_t offset) -> void {
      if (result.size() >= kMaxNoopSubgroups)
        return;
      if (current.size() == size) {
        add_group(current);
        return;
      }
      const std::size_t remaining = size - current.size();
      for (std::size_t index = offset; index + remaining <= call_indices.size(); ++index) {
        current.push_back(call_indices.at(index));
        self(self, index + 1U);
        current.pop_back();
        if (result.size() >= kMaxNoopSubgroups)
          return;
      }
    };
    choose(choose, 0U);

    if (size == 2U)
      break;
  }
  return result;
}

std::optional<IrCallContinuation> cfg_non_empty_block_from_label(
    const std::vector<IrLabelBlock>& blocks, const std::map<std::string, std::size_t>& by_label,
    const IrCfg& cfg, const std::string& start_label) {
  std::string cursor = start_label;
  std::set<std::string> seen;
  IrCallContinuation continuation;
  while (true) {
    const auto block_it = by_label.find(cursor);
    if (block_it == by_label.end() || !seen.insert(cursor).second)
      return std::nullopt;

    const IrLabelBlock& block = blocks.at(block_it->second);
    if (!block.body.empty()) {
      continuation.block_index = block_it->second;
      return continuation;
    }

    continuation.alias_labels.insert(block.label);
    const auto successor_it = cfg.successors.find(block.label);
    if (successor_it == cfg.successors.end() || successor_it->second.size() != 1U)
      return std::nullopt;
    cursor = *successor_it->second.begin();
  }
}

std::optional<IrCallContinuation> cfg_call_continuation_block(
    const std::vector<IrLabelBlock>& blocks, const std::map<std::string, std::size_t>& by_label,
    const IrCfg& cfg, std::size_t call_index, const std::string& call_target) {
  if (call_index + 1U >= blocks.size())
    return std::nullopt;

  const IrLabelBlock& call_block = blocks.at(call_index);
  std::string cursor = blocks.at(call_index + 1U).label;
  const auto successor_it = cfg.successors.find(call_block.label);
  if (successor_it == cfg.successors.end() || !successor_it->second.contains(cursor) ||
      cursor == call_target) {
    return std::nullopt;
  }

  return cfg_non_empty_block_from_label(blocks, by_label, cfg, cursor);
}

std::optional<std::string> cfg_noop_return_target_label(
    const std::vector<IrLabelBlock>& blocks, const std::map<std::string, std::size_t>& by_label,
    const IrCfg& cfg, const std::string& target_label) {
  const std::optional<IrCallContinuation> resolved =
      cfg_non_empty_block_from_label(blocks, by_label, cfg, target_label);
  if (!resolved.has_value() || !noop_return_block(blocks.at(resolved->block_index)))
    return std::nullopt;
  return blocks.at(resolved->block_index).label;
}

std::vector<std::vector<std::size_t>> chain_derived_noop_call_subgroups(
    const std::vector<IrLabelBlock>& blocks, const std::map<std::string, std::size_t>& by_label,
    const IrCfg& cfg, const std::vector<std::size_t>& call_indices,
    const std::map<std::size_t, std::string>& target_by_call) {
  std::map<std::string, std::size_t> call_by_continuation;
  std::map<std::string, std::vector<IrOp>> body_by_continuation;
  for (const std::size_t call_index : call_indices) {
    const auto target_it = target_by_call.find(call_index);
    if (target_it == target_by_call.end())
      continue;
    const std::optional<IrCallContinuation> continuation =
        cfg_call_continuation_block(blocks, by_label, cfg, call_index, target_it->second);
    if (!continuation.has_value())
      continue;
    const IrLabelBlock& continuation_block = blocks.at(continuation->block_index);
    call_by_continuation.emplace(continuation_block.label, call_index);
    body_by_continuation.emplace(continuation_block.label, continuation_block.body);
  }

  std::vector<std::vector<std::size_t>> result;
  std::set<std::vector<std::size_t>> seen_groups;
  for (const auto& [start_label, start_call_index] : call_by_continuation) {
    (void)start_call_index;
    std::vector<std::size_t> group;
    std::set<std::string> seen_labels;
    std::string cursor = start_label;
    while (true) {
      const auto call_it = call_by_continuation.find(cursor);
      const auto body_it = body_by_continuation.find(cursor);
      if (call_it == call_by_continuation.end() || body_it == body_by_continuation.end() ||
          !seen_labels.insert(cursor).second) {
        break;
      }
      group.push_back(call_it->second);

      const std::optional<std::string> next = terminal_jump_target_from_ir_body(body_it->second);
      if (next.has_value()) {
        if (group.size() >= static_cast<std::size_t>(kMaxScriptReturns))
          break;
        cursor = *next;
        continue;
      }
      if (terminal_stop_from_ir_body(body_it->second) && group.size() >= 2U &&
          seen_groups.insert(group).second) {
        result.push_back(group);
      }
      break;
    }
  }
  return result;
}

bool cfg_labels_have_only_allowed_predecessors(
    const IrCfg& cfg, const std::set<std::string>& labels,
    const std::set<std::string>& allowed_predecessors) {
  for (const std::string& label : labels) {
    const std::set<std::string> predecessors =
        cfg.predecessors.contains(label) ? cfg.predecessors.at(label)
                                         : std::set<std::string>{};
    for (const std::string& predecessor : predecessors) {
      if (!allowed_predecessors.contains(predecessor))
        return false;
    }
  }
  return true;
}

std::optional<IrResolvedTailBlock> cfg_resolved_tail_block_from_label(
    const std::vector<IrLabelBlock>& blocks, const std::map<std::string, std::size_t>& by_label,
    const IrCfg& cfg, const std::string& start_label) {
  const std::optional<IrCallContinuation> resolved =
      cfg_non_empty_block_from_label(blocks, by_label, cfg, start_label);
  if (!resolved.has_value())
    return std::nullopt;

  const IrLabelBlock& block = blocks.at(resolved->block_index);
  IrResolvedTailBlock tail{
      .label = block.label,
      .body = block.body,
      .moved_labels = resolved->alias_labels,
  };
  tail.moved_labels.insert(block.label);
  if (terminal_jump_target_from_ir_body(tail.body).has_value() ||
      terminal_stop_from_ir_body(tail.body)) {
    return tail;
  }

  const std::optional<std::string> call_target = single_call_block_target(block);
  if (!call_target.has_value())
    return std::nullopt;
  const std::optional<IrCallContinuation> continuation =
      cfg_call_continuation_block(blocks, by_label, cfg, resolved->block_index, *call_target);
  if (!continuation.has_value())
    return std::nullopt;

  const IrLabelBlock& continuation_block = blocks.at(continuation->block_index);
  std::vector<IrOp> combined = block.body;
  combined.insert(combined.end(), continuation_block.body.begin(), continuation_block.body.end());
  if (!terminal_stop_from_ir_body(combined))
    return std::nullopt;

  std::set<std::string> protected_continuation_labels = continuation->alias_labels;
  protected_continuation_labels.insert(continuation_block.label);
  std::set<std::string> allowed_predecessors = continuation->alias_labels;
  allowed_predecessors.insert(block.label);
  if (!cfg_labels_have_only_allowed_predecessors(cfg, protected_continuation_labels,
                                                 allowed_predecessors)) {
    return std::nullopt;
  }

  tail.body = std::move(combined);
  tail.moved_labels.insert(continuation->alias_labels.begin(),
                           continuation->alias_labels.end());
  tail.moved_labels.insert(continuation_block.label);
  return tail;
}

bool existing_call_chain_has_safe_cfg_entries(
    const IrCfg& cfg, const std::set<std::string>& moved_labels,
    const std::string& first_call_label, const std::string& entry_label,
    std::string& rejection_reason) {
  for (const std::string& label : moved_labels) {
    if (label == first_call_label) {
      const std::set<std::string> predecessors =
          cfg.predecessors.contains(label) ? cfg.predecessors.at(label) : std::set<std::string>{};
      if (predecessors.size() > 1U ||
          std::any_of(predecessors.begin(), predecessors.end(), [&](const std::string& predecessor) {
            return moved_labels.contains(predecessor);
          })) {
        rejection_reason = "return-stack existing ПП chain has external CFG entry into " + label;
        return false;
      }
      continue;
    }
    if (label == entry_label)
      continue;
    const std::set<std::string> predecessors =
        cfg.predecessors.contains(label) ? cfg.predecessors.at(label) : std::set<std::string>{};
    for (const std::string& predecessor : predecessors) {
      if (!moved_labels.contains(predecessor)) {
        rejection_reason = "return-stack existing ПП chain has external CFG entry into " + label;
        return false;
      }
    }
  }
  return true;
}

std::optional<IrTailChainCandidate> existing_call_chain_opportunity(
    const std::vector<IrLabelBlock>& blocks, std::string& rejection_reason) {
  const std::map<std::string, std::size_t> by_label = block_index_by_label(blocks);
  const IrCfg cfg = build_ir_cfg(blocks);
  std::optional<IrTailChainCandidate> best_candidate;
  for (std::size_t first_call_index = 0; first_call_index < blocks.size(); ++first_call_index) {
    const std::optional<std::string> first_target =
        single_call_block_target(blocks.at(first_call_index));
    if (!first_target.has_value())
      continue;

    std::vector<std::size_t> call_indices;
    std::vector<ReturnStackTailBlock> tails;
    std::vector<ReturnStackExistingCallSite> existing_sites;
    std::set<std::string> moved_labels;
    std::set<std::string> seen_calls;
    std::size_t call_index = first_call_index;
    bool valid = true;
    while (true) {
      if (call_index + 1U >= blocks.size()) {
        valid = false;
        break;
      }
      const IrLabelBlock& call_block = blocks.at(call_index);
      const std::optional<std::string> target = single_call_block_target(call_block);
      if (!target.has_value() || seen_calls.contains(call_block.label)) {
        valid = false;
        break;
      }
      seen_calls.insert(call_block.label);
      call_indices.push_back(call_index);

      const std::optional<IrCallContinuation> continuation_block =
          cfg_call_continuation_block(blocks, by_label, cfg, call_index, *target);
      if (!continuation_block.has_value()) {
        valid = false;
        break;
      }

      const IrLabelBlock& continuation = blocks.at(continuation_block->block_index);
      moved_labels.insert(call_block.label);
      moved_labels.insert(continuation_block->alias_labels.begin(),
                          continuation_block->alias_labels.end());
      moved_labels.insert(continuation.label);
      tails.push_back(ReturnStackTailBlock{
          .label = continuation.label,
          .body = continuation.body,
      });
      existing_sites.push_back(ReturnStackExistingCallSite{
          .label = call_block.label,
          .target_label = *target,
          .continuation_label = continuation.label,
          .source_address = -1,
      });

      const std::optional<IrCallContinuation> target_block =
          cfg_non_empty_block_from_label(blocks, by_label, cfg, *target);
      if (!target_block.has_value()) {
        valid = false;
        break;
      }
      moved_labels.insert(target_block->alias_labels.begin(),
                          target_block->alias_labels.end());
      if (!single_call_block_target(blocks.at(target_block->block_index)).has_value()) {
        const std::size_t entry_index = target_block->block_index;
        if (tails.size() < 2U || tails.size() > static_cast<std::size_t>(kMaxScriptReturns)) {
          valid = false;
          break;
        }
        if (!terminal_jump_target_from_ir_body(blocks.at(entry_index).body).has_value()) {
          valid = false;
          break;
        }
        moved_labels.insert(blocks.at(entry_index).label);
        existing_sites.back().target_label = blocks.at(entry_index).label;
        for (std::size_t index = 0; index + 1U < existing_sites.size(); ++index)
          existing_sites.at(index).target_label = blocks.at(call_indices.at(index + 1U)).label;
        IrTailChainCandidate candidate{
            .opportunity = ReturnStackLayoutOpportunity{
                .tails = tails,
                .entry_body = blocks.at(entry_index).body,
                .entry_label = blocks.at(entry_index).label,
                .existing_call_sites = existing_sites,
                .entry_at_address_zero = first_call_index == 0U,
                .single_start_jump = first_call_index != 0U,
            },
            .moved_tail_labels = moved_labels,
            .original_entry_label = blocks.at(first_call_index).label,
            .generated_entry_label = blocks.at(entry_index).label,
            .entry_block_index = first_call_index,
            .wrap_original_entry_label = false,
        };
        if (!existing_call_chain_has_safe_cfg_entries(
                cfg, moved_labels, blocks.at(first_call_index).label, blocks.at(entry_index).label,
                rejection_reason)) {
          valid = false;
          break;
        }
        if (return_stack_candidate_better(best_candidate, candidate))
          best_candidate = std::move(candidate);
        break;
      }
      call_index = target_block->block_index;
    }
    if (!valid && rejection_reason.empty())
      rejection_reason = "return-stack existing ПП chain candidate was incomplete";
  }
  return best_candidate;
}

std::optional<IrTailChainCandidate> same_target_call_group_opportunity(
    const std::vector<IrLabelBlock>& blocks, std::string& rejection_reason) {
  const std::map<std::string, std::size_t> by_label = block_index_by_label(blocks);
  const IrCfg cfg = build_ir_cfg(blocks);
  std::optional<IrTailChainCandidate> best_candidate;
  std::map<std::string, std::vector<std::size_t>> calls_by_target;
  std::map<std::size_t, std::string> target_by_call;
  for (std::size_t index = 0; index < blocks.size(); ++index) {
    const std::optional<std::string> target = single_call_block_target(blocks.at(index));
    if (!target.has_value())
      continue;
    const std::optional<IrCallContinuation> entry_block =
        cfg_non_empty_block_from_label(blocks, by_label, cfg, *target);
    if (!entry_block.has_value())
      continue;
    const IrLabelBlock& entry = blocks.at(entry_block->block_index);
    if (!terminal_jump_target_from_ir_body(entry.body).has_value())
      continue;
    calls_by_target[entry.label].push_back(index);
    target_by_call[index] = *target;
  }

  for (const auto& [target_label, call_indices] : calls_by_target) {
    if (call_indices.size() < 2U ||
        call_indices.size() > static_cast<std::size_t>(kMaxScriptReturns)) {
      continue;
    }

    const auto entry_it = by_label.find(target_label);
    if (entry_it == by_label.end())
      continue;
    const IrLabelBlock& entry = blocks.at(entry_it->second);

    std::vector<ReturnStackTailBlock> tails;
    std::vector<ReturnStackExistingCallSite> existing_sites;
    std::map<std::string, std::size_t> site_by_continuation;
    std::set<std::string> moved_labels;
    bool valid = true;
    for (const std::size_t call_index : call_indices) {
      const auto target_it = target_by_call.find(call_index);
      if (target_it == target_by_call.end()) {
        valid = false;
        break;
      }
      const std::optional<IrCallContinuation> target_block =
          cfg_non_empty_block_from_label(blocks, by_label, cfg, target_it->second);
      if (!target_block.has_value() ||
          blocks.at(target_block->block_index).label != entry.label) {
        valid = false;
        break;
      }
      const IrLabelBlock& call_block = blocks.at(call_index);
      const std::optional<IrCallContinuation> continuation_block =
          cfg_call_continuation_block(blocks, by_label, cfg, call_index, target_it->second);
      if (!continuation_block.has_value()) {
        valid = false;
        break;
      }

      const IrLabelBlock& continuation = blocks.at(continuation_block->block_index);
      if (site_by_continuation.contains(continuation.label)) {
        valid = false;
        break;
      }

      site_by_continuation[continuation.label] = existing_sites.size();
      moved_labels.insert(call_block.label);
      moved_labels.insert(target_block->alias_labels.begin(), target_block->alias_labels.end());
      moved_labels.insert(continuation_block->alias_labels.begin(),
                          continuation_block->alias_labels.end());
      moved_labels.insert(continuation.label);
      tails.push_back(ReturnStackTailBlock{
          .label = continuation.label,
          .body = continuation.body,
      });
      existing_sites.push_back(ReturnStackExistingCallSite{
          .label = call_block.label,
          .target_label = target_it->second,
          .continuation_label = continuation.label,
          .source_address = -1,
      });
    }
    if (!valid)
      continue;

    moved_labels.insert(entry.label);
    ReturnStackLayoutOpportunity opportunity{
        .tails = tails,
        .entry_body = entry.body,
        .entry_label = entry.label,
        .existing_call_sites = existing_sites,
    };
    const std::optional<std::vector<std::size_t>> tail_order =
        proved_tail_order_from_ir(opportunity);
    if (!tail_order.has_value() || tail_order->size() != existing_sites.size())
      continue;

    for (std::size_t physical_index = 0; physical_index < tail_order->size();
         ++physical_index) {
      const std::string& continuation_label =
          opportunity.tails.at(tail_order->at(physical_index)).label;
      const auto site_it = site_by_continuation.find(continuation_label);
      if (site_it == site_by_continuation.end()) {
        valid = false;
        break;
      }
      existing_sites.at(site_it->second).target_label =
          physical_index + 1U >= tail_order->size()
              ? entry.label
              : existing_sites
                    .at(site_by_continuation.at(
                        opportunity.tails.at(tail_order->at(physical_index + 1U)).label))
                    .label;
    }
    if (!valid)
      continue;

    opportunity.existing_call_sites = existing_sites;
    const std::string& first_continuation = opportunity.tails.at(tail_order->front()).label;
    const std::string& first_call_label =
        existing_sites.at(site_by_continuation.at(first_continuation)).label;
    const std::size_t entry_block_index = by_label.at(first_call_label);
    IrTailChainCandidate candidate{
        .opportunity = std::move(opportunity),
        .moved_tail_labels = moved_labels,
        .original_entry_label = first_call_label,
        .generated_entry_label = entry.label,
        .entry_block_index = entry_block_index,
        .wrap_original_entry_label = false,
    };
    candidate.opportunity.entry_at_address_zero = entry_block_index == 0U;
    candidate.opportunity.single_start_jump = entry_block_index != 0U;

    if (!existing_call_chain_has_safe_cfg_entries(cfg, moved_labels, first_call_label,
                                                  entry.label, rejection_reason)) {
      continue;
    }
    if (return_stack_candidate_better(best_candidate, candidate))
      best_candidate = std::move(candidate);
  }

  return best_candidate;
}

std::optional<IrTailChainCandidate> same_target_noop_helper_call_group_opportunity(
    const std::vector<IrLabelBlock>& blocks, std::string& rejection_reason) {
  const std::map<std::string, std::size_t> by_label = block_index_by_label(blocks);
  const IrCfg cfg = build_ir_cfg(blocks);
  std::optional<IrTailChainCandidate> best_candidate;
  std::set<std::string> labels;
  for (const IrLabelBlock& block : blocks)
    labels.insert(block.label);

  std::map<std::string, std::vector<std::size_t>> calls_by_target;
  std::map<std::size_t, std::string> target_by_call;
  for (std::size_t index = 0; index < blocks.size(); ++index) {
    const std::optional<std::string> target = single_call_block_target(blocks.at(index));
    if (!target.has_value())
      continue;
    const std::optional<std::string> canonical_target =
        cfg_noop_return_target_label(blocks, by_label, cfg, *target);
    if (!canonical_target.has_value())
      continue;
    calls_by_target[*canonical_target].push_back(index);
    target_by_call[index] = *target;
  }

  int synthetic_ordinal = 0;
  for (const auto& [target_label, all_call_indices] : calls_by_target) {
    (void)target_label;
    std::vector<std::vector<std::size_t>> subgroups =
        chain_derived_noop_call_subgroups(blocks, by_label, cfg, all_call_indices,
                                          target_by_call);
    std::set<std::vector<std::size_t>> seen_subgroups(subgroups.begin(), subgroups.end());
    for (std::vector<std::size_t> subgroup : bounded_noop_call_subgroups(all_call_indices)) {
      if (seen_subgroups.insert(subgroup).second)
        subgroups.push_back(std::move(subgroup));
    }

    for (const std::vector<std::size_t>& call_indices : subgroups) {
      std::vector<ReturnStackTailBlock> tails;
      std::vector<ReturnStackExistingCallSite> existing_sites;
      std::map<std::string, std::size_t> site_by_continuation;
      std::set<std::string> moved_labels;
      bool valid = true;
      for (const std::size_t call_index : call_indices) {
        const auto target_it = target_by_call.find(call_index);
        if (target_it == target_by_call.end()) {
          valid = false;
          break;
        }
        const IrLabelBlock& call_block = blocks.at(call_index);
        const std::optional<IrCallContinuation> continuation_block =
            cfg_call_continuation_block(blocks, by_label, cfg, call_index, target_it->second);
        if (!continuation_block.has_value()) {
          valid = false;
          break;
        }
        const IrLabelBlock& continuation = blocks.at(continuation_block->block_index);
        if (site_by_continuation.contains(continuation.label)) {
          valid = false;
          break;
        }
        site_by_continuation[continuation.label] = existing_sites.size();
        moved_labels.insert(call_block.label);
        moved_labels.insert(continuation_block->alias_labels.begin(),
                            continuation_block->alias_labels.end());
        moved_labels.insert(continuation.label);
        tails.push_back(ReturnStackTailBlock{
            .label = continuation.label,
            .body = continuation.body,
        });
        existing_sites.push_back(ReturnStackExistingCallSite{
            .label = call_block.label,
            .target_label = target_it->second,
            .continuation_label = continuation.label,
            .source_address = -1,
        });
      }
      if (!valid)
        continue;

      for (const ReturnStackTailBlock& tail : tails) {
        const std::optional<std::string> next = terminal_jump_target_from_ir_body(tail.body);
        if (next.has_value() && !site_by_continuation.contains(*next)) {
          valid = false;
          break;
        }
        if (!next.has_value() && !terminal_stop_from_ir_body(tail.body)) {
          valid = false;
          break;
        }
      }
      if (!valid)
        continue;

      const std::string entry_label = unique_same_target_entry_label(labels, synthetic_ordinal++);
      ReturnStackLayoutOpportunity opportunity{
          .tails = tails,
          .entry_body = {synthetic_ir_jump_to_label(tails.front().label)},
          .entry_label = entry_label,
          .existing_call_sites = existing_sites,
      };

      std::optional<std::vector<std::size_t>> tail_order;
      for (const ReturnStackTailBlock& tail : tails) {
        opportunity.entry_body = {synthetic_ir_jump_to_label(tail.label)};
        tail_order = proved_tail_order_from_ir(opportunity);
        if (tail_order.has_value())
          break;
      }
      if (!tail_order.has_value() || tail_order->size() != existing_sites.size())
        continue;

      for (std::size_t physical_index = 0; physical_index < tail_order->size();
           ++physical_index) {
        const std::string& continuation_label =
            opportunity.tails.at(tail_order->at(physical_index)).label;
        const auto site_it = site_by_continuation.find(continuation_label);
        if (site_it == site_by_continuation.end()) {
          valid = false;
          break;
        }
        existing_sites.at(site_it->second).target_label =
            physical_index + 1U >= tail_order->size()
                ? entry_label
                : existing_sites
                      .at(site_by_continuation.at(
                          opportunity.tails.at(tail_order->at(physical_index + 1U)).label))
                      .label;
      }
      if (!valid)
        continue;

      opportunity.existing_call_sites = existing_sites;
      const std::string& first_continuation = opportunity.tails.at(tail_order->front()).label;
      const std::string& first_call_label =
          existing_sites.at(site_by_continuation.at(first_continuation)).label;
      const std::size_t entry_block_index = by_label.at(first_call_label);
      IrTailChainCandidate candidate{
          .opportunity = std::move(opportunity),
          .moved_tail_labels = moved_labels,
          .original_entry_label = first_call_label,
          .generated_entry_label = entry_label,
          .entry_block_index = entry_block_index,
          .wrap_original_entry_label = false,
      };
      candidate.opportunity.entry_at_address_zero = entry_block_index == 0U;
      candidate.opportunity.single_start_jump = entry_block_index != 0U;

      if (!existing_call_chain_has_safe_cfg_entries(cfg, moved_labels, first_call_label,
                                                    entry_label, rejection_reason)) {
        continue;
      }
      if (return_stack_candidate_better(best_candidate, candidate))
        best_candidate = std::move(candidate);
    }
  }

  return best_candidate;
}

std::map<std::string, std::set<std::string>> expected_tail_predecessors(
    const std::vector<IrLabelBlock>& blocks, const IrTailChainCandidate& candidate) {
  std::map<std::string, std::set<std::string>> expected;
  (void)blocks;
  std::map<std::string, ReturnStackTailBlock> tail_by_label;
  for (const ReturnStackTailBlock& tail : candidate.opportunity.tails)
    tail_by_label[tail.label] = tail;
  std::string previous = candidate.original_entry_label;
  std::optional<std::string> cursor =
      terminal_jump_target_from_ir_body(candidate.opportunity.entry_body);
  while (cursor.has_value()) {
    expected[*cursor].insert(previous);
    const auto tail_it = tail_by_label.find(*cursor);
    if (tail_it == tail_by_label.end())
      break;
    const std::optional<std::string> next =
        terminal_jump_target_from_ir_body(tail_it->second.body);
    if (!next.has_value())
      break;
    previous = *cursor;
    cursor = next;
  }
  return expected;
}

std::set<std::string> collapsed_tail_predecessors(
    const std::vector<IrLabelBlock>& blocks, const IrTailChainCandidate& candidate,
    const IrCfg& cfg, const std::string& label) {
  const std::map<std::string, std::size_t> by_label = block_index_by_label(blocks);
  std::set<std::string> result;
  std::set<std::string> seen_aliases;

  auto collect = [&](auto& self, const std::string& current) -> void {
    const std::set<std::string> predecessors =
        cfg.predecessors.contains(current) ? cfg.predecessors.at(current)
                                           : std::set<std::string>{};
    for (const std::string& predecessor : predecessors) {
      const auto predecessor_it = by_label.find(predecessor);
      const bool collapsible_alias =
          candidate.moved_tail_labels.contains(predecessor) &&
          predecessor_it != by_label.end() &&
          blocks.at(predecessor_it->second).body.empty();
      if (collapsible_alias && seen_aliases.insert(predecessor).second) {
        self(self, predecessor);
        continue;
      }
      result.insert(predecessor);
    }
  };

  collect(collect, label);
  return result;
}

bool tail_chain_has_only_internal_tail_entries(
    const std::vector<IrLabelBlock>& blocks, const IrTailChainCandidate& candidate,
    const IrCfg& cfg, std::string& rejection_reason) {
  const std::map<std::string, std::size_t> by_label = block_index_by_label(blocks);
  const std::map<std::string, std::set<std::string>> expected =
      expected_tail_predecessors(blocks, candidate);

  for (const ReturnStackTailBlock& tail : candidate.opportunity.tails) {
    const auto tail_index = by_label.find(tail.label);
    if (tail_index == by_label.end()) {
      rejection_reason = "return-stack IR tail layout lost a candidate tail block";
      return false;
    }

    const std::set<std::string> actual_predecessors =
        collapsed_tail_predecessors(blocks, candidate, cfg, tail.label);
    const std::set<std::string> expected_predecessors =
        expected.contains(tail.label) ? expected.at(tail.label) : std::set<std::string>{};
    if (actual_predecessors != expected_predecessors || expected_predecessors.size() != 1U) {
      rejection_reason = "return-stack IR tail layout requires CFG predecessors of tail block " +
                         tail.label + " to be exactly the internal tail-chain predecessor";
      return false;
    }
  }
  return true;
}

std::optional<IrTailChainCandidate> embedded_tail_chain_opportunity(
    const std::vector<IrLabelBlock>& blocks, std::string& rejection_reason,
    IrTailChainSearchStats* stats = nullptr) {
  const std::map<std::string, std::size_t> by_label = block_index_by_label(blocks);
  const IrCfg cfg = build_ir_cfg(blocks);
  std::optional<IrTailChainCandidate> best_candidate;

  for (std::size_t entry_index = 0; entry_index < blocks.size(); ++entry_index) {
    const IrLabelBlock& entry = blocks.at(entry_index);
    if (entry.hidden && entry_index != 0U &&
        (!cfg.predecessors.contains(entry.label) || cfg.predecessors.at(entry.label).empty())) {
      continue;
    }
    std::optional<std::string> cursor = terminal_jump_target_from_ir_body(entry.body);
    if (!cursor.has_value())
      continue;
    if (stats != nullptr)
      ++stats->entry_candidates;

    std::set<std::string> seen_tail_labels;
    std::vector<std::size_t> tail_indices;
    std::set<std::string> moved_labels;
    bool valid_chain = true;
    IrTailChainBrokenReason broken_reason = IrTailChainBrokenReason::None;
    std::string broken_label;
    while (cursor.has_value()) {
      const std::optional<IrResolvedTailBlock> resolved =
          cfg_resolved_tail_block_from_label(blocks, by_label, cfg, *cursor);
      if (!resolved.has_value()) {
        valid_chain = false;
        const std::optional<IrCallContinuation> raw_block =
            cfg_non_empty_block_from_label(blocks, by_label, cfg, *cursor);
        if (raw_block.has_value()) {
          broken_reason = IrTailChainBrokenReason::NonTerminal;
          broken_label = blocks.at(raw_block->block_index).label;
        } else {
          broken_reason = IrTailChainBrokenReason::Unresolved;
        }
        break;
      }
      const auto tail_index = by_label.find(resolved->label);
      if (tail_index == by_label.end() || tail_index->second == entry_index) {
        valid_chain = false;
        broken_reason = IrTailChainBrokenReason::Unresolved;
        break;
      }
      if (seen_tail_labels.contains(resolved->label)) {
        valid_chain = false;
        broken_reason = IrTailChainBrokenReason::Repeated;
        break;
      }
      seen_tail_labels.insert(resolved->label);
      moved_labels.insert(resolved->moved_labels.begin(), resolved->moved_labels.end());
      tail_indices.push_back(tail_index->second);

      const std::optional<std::string> next = terminal_jump_target_from_ir_body(resolved->body);
      if (next.has_value()) {
        cursor = next;
        continue;
      }
      if (!terminal_stop_from_ir_body(resolved->body)) {
        valid_chain = false;
        broken_reason = IrTailChainBrokenReason::NonTerminal;
        broken_label = resolved->label;
      }
      break;
    }

    if (!valid_chain) {
      if (stats != nullptr) {
        ++stats->broken_chain_candidates;
        switch (broken_reason) {
        case IrTailChainBrokenReason::Unresolved:
          ++stats->unresolved_chain_candidates;
          break;
        case IrTailChainBrokenReason::Repeated:
          ++stats->repeated_chain_candidates;
          break;
        case IrTailChainBrokenReason::NonTerminal:
          ++stats->nonterminal_chain_candidates;
          if (!broken_label.empty() && stats->nonterminal_break_labels.size() < 5U &&
              std::find(stats->nonterminal_break_labels.begin(),
                        stats->nonterminal_break_labels.end(),
                        broken_label) == stats->nonterminal_break_labels.end()) {
            stats->nonterminal_break_labels.push_back(broken_label);
          }
          break;
        case IrTailChainBrokenReason::None:
          break;
        }
      }
      continue;
    }
    if (tail_indices.size() < 2U) {
      if (stats != nullptr)
        ++stats->short_chain_candidates;
      continue;
    }
    if (tail_indices.size() > static_cast<std::size_t>(kMaxScriptReturns)) {
      if (stats != nullptr)
        ++stats->too_long_chain_candidates;
      continue;
    }
    if (stats != nullptr)
      ++stats->valid_chain_candidates;

    std::vector<IrOp> entry_body = entry.body;
    entry_body.back().target = blocks.at(tail_indices.front()).label;

    std::vector<ReturnStackTailBlock> tails;
    tails.reserve(tail_indices.size());
    for (std::size_t index = 0; index < tail_indices.size(); ++index) {
      const std::optional<IrResolvedTailBlock> resolved =
          cfg_resolved_tail_block_from_label(blocks, by_label, cfg,
                                             blocks.at(tail_indices.at(index)).label);
      if (!resolved.has_value()) {
        valid_chain = false;
        break;
      }
      std::vector<IrOp> body = resolved->body;
      if (index + 1U < tail_indices.size())
        body.back().target = blocks.at(tail_indices.at(index + 1U)).label;
      tails.push_back(ReturnStackTailBlock{
          .label = resolved->label,
          .body = std::move(body),
      });
    }
    if (!valid_chain)
      continue;

    IrTailChainCandidate candidate{
        .opportunity = ReturnStackLayoutOpportunity{
            .tails = tails,
            .entry_body = std::move(entry_body),
            .entry_label = "__return_stack_entry_" + std::to_string(entry_index),
            .entry_at_address_zero = entry_index == 0U,
            .single_start_jump = entry_index != 0U,
        },
        .moved_tail_labels = moved_labels,
        .original_entry_label = entry.label,
        .generated_entry_label = "__return_stack_entry_" + std::to_string(entry_index),
        .entry_block_index = entry_index,
    };

    if (!tail_chain_has_only_internal_tail_entries(blocks, candidate, cfg, rejection_reason)) {
      if (stats != nullptr && rejection_reason.find("CFG predecessors") != std::string::npos)
        ++stats->external_entry_rejections;
      continue;
    }
    if (return_stack_candidate_better(best_candidate, candidate))
      best_candidate = std::move(candidate);
  }

  if (best_candidate.has_value())
    return best_candidate;
  if (rejection_reason.empty()) {
    rejection_reason = "return-stack IR tail layout found no movable entry block that jumps through a 2..5 tail chain ending in С/П";
  }
  return std::nullopt;
}

void append_ir_block_as_machine(std::vector<MachineItem>& out, const IrLabelBlock& block) {
  MachineItem label = MachineItem::label(block.label);
  label.hidden = block.hidden;
  out.push_back(std::move(label));
  append_ir_items(out, block.body);
}

std::vector<MachineItem> materialize_embedded_tail_chain_layout(
    const std::vector<IrLabelBlock>& blocks, const IrTailChainCandidate& candidate,
    const ReturnStackLayoutOpportunityAnalysis& analysis) {
  const ReturnStackStartupLayoutPlan plan = materialize_return_stack_layout(analysis);
  std::vector<MachineItem> out;
  for (std::size_t index = 0; index < blocks.size(); ++index) {
    const IrLabelBlock& block = blocks.at(index);
    if (index == candidate.entry_block_index) {
      if (candidate.wrap_original_entry_label) {
        MachineItem wrapper = MachineItem::label(candidate.original_entry_label);
        wrapper.hidden = block.hidden;
        out.push_back(std::move(wrapper));
      }
      out.insert(out.end(), plan.items.begin(), plan.items.end());
      continue;
    }
    if (candidate.moved_tail_labels.contains(block.label))
      continue;
    append_ir_block_as_machine(out, block);
  }
  return out;
}

std::optional<int> valid_concrete_existing_call_site_count(
    const ReturnStackLayoutOpportunity& opportunity,
    const std::vector<std::size_t>& tail_order, ReturnStackStartupLayoutPlan& plan) {
  if (opportunity.existing_call_sites.empty())
    return std::nullopt;

  const std::map<std::string, ReturnStackExistingCallSite> existing_by_continuation =
      existing_callsite_by_continuation(opportunity);
  std::set<std::pair<std::string, std::string>> required;
  for (std::size_t physical_index = 0; physical_index < tail_order.size();
       ++physical_index) {
    const std::string expected_continuation =
        opportunity.tails.at(tail_order.at(physical_index)).label;
    const std::string canonical_target = physical_index + 1U >= tail_order.size()
                                            ? opportunity.entry_label
                                            : charge_label_name(physical_index + 1U);
    const std::string existing_target = charge_target_for_physical_tail(
        opportunity, tail_order, physical_index, existing_by_continuation);
    required.insert({canonical_target, expected_continuation});
    required.insert({existing_target, expected_continuation});
  }

  std::set<std::pair<std::string, std::string>> used_edges;
  std::set<int> used_sources;
  std::set<std::string> used_symbolic_sources;
  int valid = 0;
  int invalid = 0;
  for (const ReturnStackExistingCallSite& site : opportunity.existing_call_sites) {
    const std::pair<std::string, std::string> edge{site.target_label,
                                                   site.continuation_label};
    const bool repeated_source =
        site.source_address >= 0 ? used_sources.contains(site.source_address)
                                 : used_symbolic_sources.contains(site.label);
    if (site.label.empty() || repeated_source || !required.contains(edge) ||
        used_edges.contains(edge)) {
      ++invalid;
      continue;
    }
    if (site.source_address >= 0)
      used_sources.insert(site.source_address);
    else
      used_symbolic_sources.insert(site.label);
    used_edges.insert(edge);
    ++valid;
  }

  if (valid > 0) {
    plan.proofs.push_back("validated " + std::to_string(valid) +
                          " concrete existing ПП callsite" +
                          (valid == 1 ? "" : "s"));
  }
  if (invalid > 0) {
    plan.risk_reasons.push_back("ignored " + std::to_string(invalid) +
                                " existing ПП callsite candidate" +
                                (invalid == 1 ? "" : "s") +
                                " that did not match the generated charge/continuation layout");
  }
  return valid;
}

std::set<int> incoming_op_indexes_for_target(const std::map<int, std::vector<FlowReference>>& incoming,
                                             int target) {
  std::set<int> result;
  const auto incoming_it = incoming.find(target);
  if (incoming_it == incoming.end())
    return result;
  for (const FlowReference& ref : incoming_it->second)
    result.insert(ref.op_index);
  return result;
}

std::optional<int> item_index_for_address(const MachineLayout& layout, int address) {
  const auto it = layout.item_index_by_address.find(address);
  if (it == layout.item_index_by_address.end())
    return std::nullopt;
  return it->second;
}

std::optional<int> executable_opcode_at_item(const std::vector<MachineItem>& items,
                                             const MachineLayout& layout, int item_index,
                                             std::string& rejection_reason) {
  if (item_index < 0 || item_index >= static_cast<int>(items.size())) {
    rejection_reason = "target item is out of range";
    return std::nullopt;
  }
  const MachineItem& item = items.at(static_cast<std::size_t>(item_index));
  if (item.kind == MachineItemKind::Op)
    return item.opcode;
  if (item.kind != MachineItemKind::Address) {
    rejection_reason = "target is not an executable cell";
    return std::nullopt;
  }
  if (item.formal_opcode.has_value()) {
    rejection_reason = "formal/super-dark address cell execution requires a separate proof";
    return std::nullopt;
  }
  const std::optional<int> target = resolved_target(item, layout);
  if (!target.has_value()) {
    rejection_reason = "address cell target is unresolved";
    return std::nullopt;
  }
  try {
    return official_address_to_opcode(*target);
  } catch (const std::exception&) {
    rejection_reason = "address cell target is not an official executable address opcode";
    return std::nullopt;
  }
}

bool is_number_entry_opcode(int opcode) {
  return (opcode >= 0x00 && opcode <= 0x09) || opcode == 0x0a || opcode == 0x0b ||
         opcode == 0x0c || opcode == 0x0d;
}

bool is_trap_opcode(int opcode) {
  switch (opcode) {
  case 0x27:
  case 0x28:
  case 0x29:
  case 0x2b:
  case 0x2c:
  case 0x2d:
  case 0x2e:
  case 0x3c:
    return true;
  default:
    return false;
  }
}

bool adjacent_number_entry(const std::vector<MachineItem>& items, const MachineLayout& layout,
                           int item_index) {
  const auto address_it = layout.address_by_item_index.find(item_index);
  if (address_it == layout.address_by_item_index.end())
    return false;
  for (const int neighbor_address : {address_it->second - 1, address_it->second + 1}) {
    const std::optional<int> neighbor_index = item_index_for_address(layout, neighbor_address);
    if (!neighbor_index.has_value())
      continue;
    std::string reason;
    const std::optional<int> opcode =
        executable_opcode_at_item(items, layout, *neighbor_index, reason);
    if (opcode.has_value() && is_number_entry_opcode(*opcode))
      return true;
  }
  return false;
}

DirtyReturnStackDispatchCellProof prove_dirty_dispatch_cell(
    const std::vector<MachineItem>& items, const MachineLayout& layout, int dirty_return_address,
    int actual_pc, const DirtyReturnStackDispatchOptions& options) {
  DirtyReturnStackDispatchCellProof proof{
      .dirty_return_address = dirty_return_address,
      .actual_pc = actual_pc,
  };
  const std::optional<int> item_index = item_index_for_address(layout, actual_pc);
  if (!item_index.has_value()) {
    proof.rejection_reason = "dirty target has no cell in the materialized layout";
    return proof;
  }

  std::string rejection;
  const std::optional<int> opcode =
      executable_opcode_at_item(items, layout, *item_index, rejection);
  if (!opcode.has_value()) {
    proof.rejection_reason = rejection;
    return proof;
  }
  proof.required_opcode = *opcode;

  if (is_address_taking_opcode(*opcode)) {
    const int operand_index = *item_index + 1;
    if (operand_index >= static_cast<int>(items.size()) ||
        items.at(static_cast<std::size_t>(operand_index)).kind != MachineItemKind::Address ||
        items.at(static_cast<std::size_t>(operand_index)).formal_opcode.has_value() ||
        !resolved_target(items.at(static_cast<std::size_t>(operand_index)), layout).has_value()) {
      proof.rejection_reason = "dirty target is an address-taking opcode without a valid address "
                               "operand cell";
      return proof;
    }
  }
  if (is_number_entry_opcode(*opcode) && adjacent_number_entry(items, layout, *item_index)) {
    proof.rejection_reason = "dirty target number-entry opcode would glue to adjacent number "
                             "entry";
    return proof;
  }
  if (options.expect_fallthrough && (*opcode == 0x50 || is_trap_opcode(*opcode))) {
    proof.rejection_reason = "dirty target stops or traps but the plan requires fallthrough";
    return proof;
  }

  proof.safe = true;
  proof.continuation_proof = "dirty target cell is executable and has a locally safe continuation";
  return proof;
}

MachineItem dirty_dispatch_safe_padding_cell() {
  MachineItem pad = MachineItem::op(0x10, "+");
  pad.comment = "return-stack dirty dispatch safe padding";
  return pad;
}

bool layout_has_numeric_address_targets(const std::vector<MachineItem>& items) {
  return std::any_of(items.begin(), items.end(), [](const MachineItem& item) {
    return item.kind == MachineItemKind::Address && std::holds_alternative<int>(item.target);
  });
}

std::optional<int> insertion_index_preserving_labels_at_address(
    const std::vector<MachineItem>& items, const MachineLayout& layout, int address) {
  const std::optional<int> item_index = item_index_for_address(layout, address);
  if (!item_index.has_value())
    return std::nullopt;

  int insertion_index = *item_index;
  while (insertion_index > 0 &&
         items.at(static_cast<std::size_t>(insertion_index - 1)).kind == MachineItemKind::Label) {
    --insertion_index;
  }
  return insertion_index;
}

std::optional<DirtyReturnStackDispatchCellProof> first_unsafe_dirty_dispatch_cell(
    const DirtyReturnStackDispatchPlan& plan) {
  for (const DirtyReturnStackDispatchCellProof& proof : plan.cell_proofs) {
    if (!proof.safe)
      return proof;
  }
  return std::nullopt;
}

} // namespace

std::vector<int> mk61_return_stack_after_call(std::vector<int> stack,
                                              int stored_return_address) {
  if (stack.size() >= static_cast<std::size_t>(kReturnStackDepth))
    stack.erase(stack.begin());
  stack.push_back(stored_return_address);
  return stack;
}

std::optional<ReturnStackReturnStep> mk61_return_stack_after_return(
    const std::vector<int>& stack) {
  if (stack.empty())
    return std::nullopt;

  const int stored = stack.back();
  std::vector<int> next;
  if (stack.size() == static_cast<std::size_t>(kReturnStackDepth)) {
    const int low_digit = stack.front() % 10;
    next.push_back(low_digit * 10 + low_digit);
    next.insert(next.end(), stack.begin(), stack.end() - 1);
  } else {
    next.insert(next.end(), stack.begin(), stack.end() - 1);
  }

  return ReturnStackReturnStep{
      .stored_return_address = stored,
      .target_address = stored + 1,
      .stack_after_return = std::move(next),
  };
}

std::vector<ReturnStackReturnStep> simulate_mk61_return_stack(std::vector<int> stack,
                                                              int return_count) {
  std::vector<ReturnStackReturnStep> result;
  for (int index = 0; index < return_count; ++index) {
    const std::optional<ReturnStackReturnStep> step = mk61_return_stack_after_return(stack);
    if (!step.has_value())
      break;
    result.push_back(*step);
    stack = step->stack_after_return;
  }
  return result;
}

ReturnStackLayoutOpportunityAnalysis analyze_return_stack_layout_opportunity(
    ReturnStackLayoutOpportunity opportunity, const ReturnStackStartupLayoutOptions& options) {
  ReturnStackLayoutOpportunityAnalysis analysis{
      .opportunity = std::move(opportunity),
      .options = options,
  };
  ReturnStackStartupLayoutPlan& plan = analysis.plan;
  const ReturnStackLayoutOpportunity& input = analysis.opportunity;
  const std::optional<std::vector<std::size_t>> proved_tail_order =
      proved_tail_order_from_ir(input);
  const std::vector<std::size_t> tail_order =
      proved_tail_order.has_value() ? *proved_tail_order : default_tail_order(input);
  plan.tail_order_proved = proved_tail_order.has_value();
  for (const std::size_t tail_index : tail_order)
    plan.ordered_tail_labels.push_back(input.tails.at(tail_index).label);
  plan.items = materialize_layout_items(input, tail_order);
  plan.transitions = static_cast<int>(input.tails.size());
  if (plan.tail_order_proved)
    plan.proofs.push_back("tail blocks are ordered from the entry-chain IR");

  const std::optional<int> concrete_existing_call_sites =
      valid_concrete_existing_call_site_count(input, tail_order, plan);
  plan.existing_call_sites =
      concrete_existing_call_sites.has_value()
          ? std::min(*concrete_existing_call_sites, plan.transitions)
          : std::max(0, std::min(options.existing_call_sites, plan.transitions));
  plan.strategy = plan.existing_call_sites > 0 ? "existing-callsite-layout"
                                               : "one-shot-startup-prologue";

  if (input.tails.size() < 2 || input.tails.size() > static_cast<std::size_t>(kMaxScriptReturns)) {
    plan.rejection_reason = "return-stack startup layout requires 2..5 transitions";
    return analysis;
  }

  plan.injected_call_sites = plan.transitions;
  plan.paid_call_sites = std::max(0, plan.injected_call_sites - plan.existing_call_sites);
  plan.transition_savings = plan.transitions;
  plan.charge_cost = plan.paid_call_sites * 2;
  const int materialized_cells = machine_cell_count(plan.items);
  plan.address_overlay_savings =
      std::max(0, materialized_cells - best_cell_count_with_address_overlay(plan.items));
  plan.address_shift_risk_count = plan.paid_call_sites;
  plan.address_shift_cell_count = plan.paid_call_sites * 2;
  if (plan.address_shift_risk_count > 0) {
    plan.risk_reasons.push_back("injected ПП charge cells shift " +
                                std::to_string(plan.address_shift_cell_count) +
                                " downstream post-layout cells");
  }
  if (plan.address_overlay_savings > 0) {
    plan.proofs.push_back("generated layout still exposes address-code-overlay savings");
  }

  const MachineLayout layout = machine_layout(plan.items);
  const std::vector<FlowReference> refs = direct_flow_references(plan.items, layout);
  const std::map<int, std::vector<FlowReference>> incoming = incoming_by_target(refs);
  const std::map<std::string, ReturnStackExistingCallSite> physical_existing_by_continuation =
      existing_callsite_by_continuation(input);
  auto physical_charge_label = [&](int index) {
    return charge_label_for_physical_tail(input, tail_order, static_cast<std::size_t>(index),
                                          physical_existing_by_continuation);
  };

  plan.one_shot_proved = input.entry_at_address_zero || input.single_start_jump;
  if (plan.one_shot_proved) {
    plan.proofs.push_back(input.entry_at_address_zero
                              ? "charging chain starts at address 00"
                              : "charging chain is reached by a single start jump");
  }

  plan.no_backward_charge_jumps = true;
  for (int index = 0; index < plan.transitions; ++index) {
    const auto label_it = layout.labels.find(physical_charge_label(index));
    if (label_it == layout.labels.end()) {
      plan.no_backward_charge_jumps = false;
      break;
    }
    const std::optional<CallSite> call = call_site_at_address(plan.items, layout, label_it->second);
    if (!call.has_value() || call->target_address <= call->source_address) {
      plan.no_backward_charge_jumps = false;
      break;
    }
  }
  if (plan.no_backward_charge_jumps)
    plan.proofs.push_back("charging chain has no backward ПП edge");

  plan.no_external_charge_entries = true;
  for (int index = 1; index < plan.transitions; ++index) {
    const auto label_it = layout.labels.find(physical_charge_label(index));
    if (label_it == layout.labels.end()) {
      plan.no_external_charge_entries = false;
      break;
    }
    const auto previous_label_it = layout.labels.find(physical_charge_label(index - 1));
    if (previous_label_it == layout.labels.end()) {
      plan.no_external_charge_entries = false;
      break;
    }
    const std::optional<CallSite> previous =
        call_site_at_address(plan.items, layout, previous_label_it->second);
    if (!previous.has_value() ||
        incoming_op_indexes_for_target(incoming, label_it->second) !=
            std::set<int>{previous->op_index}) {
      plan.no_external_charge_entries = false;
      break;
    }
  }
  if (plan.no_external_charge_entries)
    plan.proofs.push_back("no external flow enters the middle of the charging chain");

  const auto entry_it = layout.labels.find(input.entry_label);
  plan.unique_entry_after_charge = false;
  if (entry_it != layout.labels.end() && plan.transitions > 0) {
    const auto final_charge_it = layout.labels.find(physical_charge_label(plan.transitions - 1));
    if (final_charge_it != layout.labels.end()) {
      const std::optional<CallSite> final_call =
          call_site_at_address(plan.items, layout, final_charge_it->second);
      plan.unique_entry_after_charge =
          final_call.has_value() && final_call->target_address == entry_it->second &&
          incoming_op_indexes_for_target(incoming, entry_it->second) ==
              std::set<int>{final_call->op_index};
    }
  }
  if (plan.unique_entry_after_charge)
    plan.proofs.push_back("charging chain has a unique final entry target");

  plan.net_savings = plan.transition_savings + plan.address_overlay_savings - plan.charge_cost;
  plan.allowed_by_size_rescue =
      options.size_rescue && plan.net_savings == 0 && plan.address_overlay_savings > 0;
  plan.profitable =
      (plan.net_savings >= options.min_net_savings && plan.net_savings > 0) ||
      plan.allowed_by_size_rescue;
  if (!plan.tail_order_proved) {
    plan.rejection_reason = "return-stack startup layout cannot prove tail execution order "
                            "from IR";
  } else if (!plan.one_shot_proved) {
    plan.rejection_reason = "return-stack startup layout lacks a one-shot entry proof";
  } else if (!plan.no_backward_charge_jumps) {
    plan.rejection_reason = "return-stack startup layout has a backward charging-chain edge";
  } else if (!plan.no_external_charge_entries) {
    plan.rejection_reason = "return-stack startup layout has external flow into the charging "
                            "chain";
  } else if (!plan.unique_entry_after_charge) {
    plan.rejection_reason = "return-stack startup layout does not charge into a unique entry";
  } else if (!plan.profitable) {
    plan.rejection_reason = "return-stack startup layout does not meet strict net-savings "
                            "threshold";
  }

  return analysis;
}

ReturnStackStartupLayoutPlan materialize_return_stack_layout(
    const ReturnStackLayoutOpportunityAnalysis& analysis) {
  return analysis.plan;
}

ReturnStackStartupLayoutPlan build_return_stack_startup_layout(
    const std::vector<ReturnStackTailBlock>& tails,
    const std::vector<IrOp>& entry_body,
    const std::string& entry_label,
    const ReturnStackStartupLayoutOptions& options) {
  ReturnStackLayoutOpportunity opportunity{
      .tails = tails,
      .entry_body = entry_body,
      .entry_label = entry_label,
  };
  ReturnStackLayoutOpportunityAnalysis analysis =
      analyze_return_stack_layout_opportunity(std::move(opportunity), options);
  return materialize_return_stack_layout(analysis);
}

struct IrTailChainCandidateMaterialization {
  IrTailChainCandidate candidate;
  std::vector<IrLabelBlock> blocks;
};

struct IrTailChainCandidateCollection {
  ReturnStackIrTailLayoutSearch search;
  std::vector<IrTailChainCandidateMaterialization> candidates;
};

IrTailChainCandidateCollection collect_return_stack_ir_tail_candidates(
    const std::vector<IrOp>& ops) {
  IrTailChainCandidateCollection collection;
  std::string rejection;
  std::optional<std::vector<IrLabelBlock>> blocks = split_label_blocks(ops, rejection);
  if (!blocks.has_value()) {
    collection.search.rejection_reason = rejection;
    return collection;
  }
  collection.search.symbolic_existing_callsite_hints =
      count_symbolic_existing_callsite_hints(*blocks);
  collection.search.symbolic_existing_callsite_target_labels =
      symbolic_existing_callsite_hint_targets(*blocks);
  const std::map<std::string, int> symbolic_hint_groups =
      symbolic_existing_callsite_hint_counts_by_target(*blocks);
  collection.search.symbolic_existing_callsite_target_groups =
      static_cast<int>(symbolic_hint_groups.size());
  for (const auto& [target, count] : symbolic_hint_groups) {
    (void)target;
    collection.search.symbolic_existing_callsite_largest_target_group =
        std::max(collection.search.symbolic_existing_callsite_largest_target_group, count);
  }

  auto consider_candidate = [&](std::optional<IrTailChainCandidate> candidate,
                                const std::vector<IrLabelBlock>& source_blocks) {
    if (!candidate.has_value())
      return;
    collection.candidates.push_back(IrTailChainCandidateMaterialization{
        .candidate = std::move(*candidate),
        .blocks = source_blocks,
    });
  };

  std::string remembered_rejection;
  std::string direct_rejection;
  consider_candidate(existing_call_chain_opportunity(*blocks, direct_rejection), *blocks);
  remember_return_stack_rejection(remembered_rejection, direct_rejection);

  std::vector<IrLabelBlock> extracted_blocks = *blocks;
  const ExtractedIrFragments extracted = extract_terminal_tail_fragments(extracted_blocks);
  collection.search.extracted_tail_fragments = extracted.tail_fragments;
  collection.search.extracted_existing_callsite_fragments =
      extracted.existing_callsite_fragments;
  if (extracted.total() > 0) {
    std::string extracted_rejection;
    consider_candidate(existing_call_chain_opportunity(extracted_blocks, extracted_rejection),
                       extracted_blocks);
    remember_return_stack_rejection(remembered_rejection, extracted_rejection);
  }

  std::string retarget_rejection;
  consider_candidate(same_target_call_group_opportunity(extracted_blocks, retarget_rejection),
                     extracted_blocks);
  remember_return_stack_rejection(remembered_rejection, retarget_rejection);

  std::string noop_retarget_rejection;
  consider_candidate(same_target_noop_helper_call_group_opportunity(
                         extracted_blocks, noop_retarget_rejection),
                     extracted_blocks);
  remember_return_stack_rejection(remembered_rejection, noop_retarget_rejection);

  IrTailChainSearchStats stats;
  std::string embedded_rejection;
  consider_candidate(embedded_tail_chain_opportunity(extracted_blocks, embedded_rejection,
                                                     &stats),
                     extracted_blocks);
  remember_return_stack_rejection(remembered_rejection, embedded_rejection);
  collection.search.cfg_tail_entry_candidates = stats.entry_candidates;
  collection.search.cfg_tail_valid_chain_candidates = stats.valid_chain_candidates;
  collection.search.cfg_tail_short_chain_candidates = stats.short_chain_candidates;
  collection.search.cfg_tail_too_long_chain_candidates = stats.too_long_chain_candidates;
  collection.search.cfg_tail_broken_chain_candidates = stats.broken_chain_candidates;
  collection.search.cfg_tail_unresolved_chain_candidates = stats.unresolved_chain_candidates;
  collection.search.cfg_tail_repeated_chain_candidates = stats.repeated_chain_candidates;
  collection.search.cfg_tail_nonterminal_chain_candidates = stats.nonterminal_chain_candidates;
  collection.search.cfg_tail_nonterminal_break_labels = stats.nonterminal_break_labels;
  collection.search.cfg_tail_external_entry_rejections = stats.external_entry_rejections;

  if (collection.candidates.empty())
    collection.search.rejection_reason = remembered_rejection;
  return collection;
}

ReturnStackIrTailLayoutSearch analyze_return_stack_ir_tail_candidate(
    ReturnStackIrTailLayoutSearch search,
    const IrTailChainCandidateMaterialization& materialization,
    const ReturnStackStartupLayoutOptions& options) {
  search.has_opportunity = true;
  search.analysis = analyze_return_stack_layout_opportunity(
      materialization.candidate.opportunity, options);
  const ReturnStackStartupLayoutPlan& plan = search.analysis.plan;
  const bool structurally_proved = plan.tail_order_proved && plan.one_shot_proved &&
                                   plan.no_backward_charge_jumps &&
                                   plan.no_external_charge_entries &&
                                   plan.unique_entry_after_charge;
  if (structurally_proved) {
    search.materialized = true;
    search.materialized_items = materialize_embedded_tail_chain_layout(
        materialization.blocks, materialization.candidate, search.analysis);
  }
  if (!search.analysis.plan.rejection_reason.empty())
    search.rejection_reason = search.analysis.plan.rejection_reason;
  return search;
}

std::optional<std::size_t> strongest_return_stack_ir_candidate_index(
    const std::vector<IrTailChainCandidateMaterialization>& candidates) {
  std::optional<std::size_t> best_index;
  std::optional<IrTailChainCandidate> best_candidate;
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    if (return_stack_candidate_better(best_candidate, candidates.at(index).candidate)) {
      best_candidate = candidates.at(index).candidate;
      best_index = index;
    }
  }
  return best_index;
}

ReturnStackIrTailLayoutSearch analyze_return_stack_ir_tail_layout(
    const std::vector<IrOp>& ops, const ReturnStackStartupLayoutOptions& options) {
  const IrTailChainCandidateCollection collection =
      collect_return_stack_ir_tail_candidates(ops);
  const std::optional<std::size_t> best_index =
      strongest_return_stack_ir_candidate_index(collection.candidates);
  if (!best_index.has_value())
    return collection.search;
  return analyze_return_stack_ir_tail_candidate(
      collection.search, collection.candidates.at(*best_index), options);
}

ReturnStackIrTailLayoutSearch analyze_return_stack_ir_tail_layout_with_pipeline(
    const std::vector<IrOp>& ops, const std::vector<MachineItem>& current_items,
    const CompileOptions& compile_options, const ReturnStackStartupLayoutOptions& options,
    int indirect_flow_rescue_above) {
  const IrTailChainCandidateCollection collection =
      collect_return_stack_ir_tail_candidates(ops);
  if (collection.candidates.empty())
    return collection.search;

  const ReturnStackPostLayoutPipelineReport current_pipeline =
      measure_return_stack_post_layout_pipeline(current_items, compile_options,
                                                indirect_flow_rescue_above);
  std::optional<ReturnStackIrTailLayoutSearch> best_search;
  std::optional<ReturnStackIrTailLayoutSearch> best_fallback_search;
  std::optional<IrTailChainCandidate> best_candidate;
  std::optional<IrTailChainCandidate> best_fallback_candidate;
  int best_final_cells = 0;
  int best_fallback_final_cells = 0;

  for (const IrTailChainCandidateMaterialization& candidate : collection.candidates) {
    ReturnStackIrTailLayoutSearch candidate_search = analyze_return_stack_ir_tail_candidate(
        collection.search, candidate, options);
    if (!candidate_search.materialized)
      continue;

    const ReturnStackPostLayoutPipelineReport candidate_pipeline =
        measure_return_stack_post_layout_pipeline(candidate_search.materialized_items,
                                                  compile_options,
                                                  indirect_flow_rescue_above);
    candidate_search.pipeline_compared = true;
    candidate_search.pipeline_current_final_cells = current_pipeline.final_cells;
    candidate_search.pipeline_candidate_final_cells = candidate_pipeline.final_cells;
    candidate_search.pipeline_candidate_better =
        candidate_pipeline.final_cells < current_pipeline.final_cells;
    const bool eligible = candidate_search.pipeline_candidate_better;
    auto candidate_wins_tie = [&](const std::optional<IrTailChainCandidate>& current) {
      return return_stack_candidate_better(current, candidate.candidate);
    };

    if (!best_fallback_search.has_value() ||
        candidate_pipeline.final_cells < best_fallback_final_cells ||
        (candidate_pipeline.final_cells == best_fallback_final_cells &&
         candidate_wins_tie(best_fallback_candidate))) {
      best_fallback_search = candidate_search;
      best_fallback_candidate = candidate.candidate;
      best_fallback_final_cells = candidate_pipeline.final_cells;
    }

    if (!eligible)
      continue;
    if (!best_search.has_value() || candidate_pipeline.final_cells < best_final_cells ||
        (candidate_pipeline.final_cells == best_final_cells && candidate_wins_tie(best_candidate))) {
      best_search = std::move(candidate_search);
      best_candidate = candidate.candidate;
      best_final_cells = candidate_pipeline.final_cells;
    }
  }

  if (best_search.has_value())
    return *best_search;
  if (best_fallback_search.has_value())
    return *best_fallback_search;

  const std::optional<std::size_t> best_index =
      strongest_return_stack_ir_candidate_index(collection.candidates);
  if (!best_index.has_value())
    return collection.search;
  return analyze_return_stack_ir_tail_candidate(
      collection.search, collection.candidates.at(*best_index), options);
}

ReturnStackScriptOpportunityScan scan_return_stack_script_opportunity(
    const std::vector<MachineItem>& items) {
  ReturnStackScriptOpportunityScan scan;
  if (items.empty()) {
    scan.rejection_reason = "return-stack script requires a non-empty machine layout";
    return scan;
  }

  const MachineLayout layout = machine_layout(items);
  std::set<int> call_addresses;
  std::vector<CallSite> calls;
  for (const auto& [address, item_index] : layout.item_index_by_address) {
    const MachineItem& item = items.at(static_cast<std::size_t>(item_index));
    if (item.kind != MachineItemKind::Op || item.raw)
      continue;
    if (item.opcode == 0x53 || is_proven_indirect_call_opcode(item.opcode)) {
      if (const std::optional<CallSite> call = call_site_at_address(items, layout, address)) {
        calls.push_back(*call);
        call_addresses.insert(call->source_address);
      }
    } else if (item.opcode == 0x51) {
      if (const std::optional<FlowReference> jump =
              direct_flow_reference_at(items, layout, item_index)) {
        const MachineItem& operand = items.at(static_cast<std::size_t>(jump->address_index));
        if (address_target_is_label(operand))
          ++scan.direct_jumps;
      }
    }
  }

  scan.direct_call_sites = static_cast<int>(calls.size());
  for (const CallSite& call : calls) {
    if (call_addresses.contains(call.target_address))
      ++scan.chained_call_sites;
  }

  if (scan.direct_call_sites < 2) {
    scan.rejection_reason = "return-stack script requires at least two direct ПП call sites; found " +
                            std::to_string(scan.direct_call_sites);
  } else if (scan.direct_jumps == 0) {
    scan.rejection_reason = "return-stack script requires at least one direct БП tail; found 0";
  } else if (scan.chained_call_sites == 0) {
    scan.rejection_reason = "return-stack script requires a direct ПП->ПП charge-chain edge; found 0";
  } else {
    scan.possible = true;
    scan.rejection_reason = "return-stack script pre-scan found a possible ПП charge chain and БП tail";
  }
  return scan;
}

std::string explain_return_stack_script_rejection(const std::vector<MachineItem>& items) {
  const ReturnStackScriptOpportunityScan scan = scan_return_stack_script_opportunity(items);
  if (!scan.possible)
    return scan.rejection_reason;

  const std::optional<ScriptPlan> plan = find_best_script_plan(items);
  if (!plan.has_value()) {
    return "return-stack script found a possible charge shape, but no fully proven 2..5 ПП chain "
           "followed by reverse БП continuations and terminal С/П";
  }

  const std::vector<MachineItem> candidate = apply_script_plan(items, *plan);
  if (machine_cell_count(candidate) >= machine_cell_count(items))
    return "return-stack script proof exists, but rewriting it would not reduce cell count";
  if (!return_stack_candidate_beats_address_overlay(items, candidate)) {
    return "return-stack script proof exists, but address-code-overlay is at least as small as "
           "the return-stack rewrite";
  }
  return {};
}

DirtyReturnStackDispatchPlan plan_dirty_return_stack_dispatch(
    std::vector<int> stack, int return_count, const DirtyReturnStackDispatchOptions& options) {
  DirtyReturnStackDispatchPlan plan;
  plan.risk_reason =
      "dirty return-stack dispatch depends on exact post-exhaustion 99/77/55/33-style "
      "stack contents and exact layout addresses";
  const std::vector<ReturnStackReturnStep> steps =
      simulate_mk61_return_stack(std::move(stack), return_count);
  for (std::size_t index = 0; index < steps.size(); ++index) {
    if (index < static_cast<std::size_t>(kReturnStackDepth)) {
      plan.clean_targets.push_back(steps.at(index).target_address);
    } else {
      plan.dirty_return_addresses.push_back(steps.at(index).stored_return_address);
      plan.dirty_targets.push_back(steps.at(index).target_address);
    }
  }
  if (!options.size_rescue) {
    plan.rejection_reason = "dirty return-stack dispatch is disabled outside explicit "
                            "size-rescue mode";
  } else if (static_cast<int>(plan.dirty_targets.size()) < options.min_dirty_targets) {
    plan.rejection_reason = "dirty return-stack dispatch did not expose enough dirty targets";
  } else {
    plan.enabled = true;
  }
  return plan;
}

DirtyReturnStackDispatchPlan plan_dirty_return_stack_dispatch(
    std::vector<int> stack, int return_count, const std::vector<MachineItem>& layout_items,
    const DirtyReturnStackDispatchOptions& options) {
  DirtyReturnStackDispatchPlan plan =
      plan_dirty_return_stack_dispatch(std::move(stack), return_count, options);
  const MachineLayout layout = machine_layout(layout_items);
  bool all_safe = !plan.dirty_targets.empty();
  for (std::size_t index = 0; index < plan.dirty_targets.size(); ++index) {
    DirtyReturnStackDispatchCellProof proof = prove_dirty_dispatch_cell(
        layout_items, layout, plan.dirty_return_addresses.at(index), plan.dirty_targets.at(index),
        options);
    if (!proof.safe)
      all_safe = false;
    plan.cell_proofs.push_back(std::move(proof));
  }
  plan.layout_proved = all_safe;
  if (plan.enabled && !plan.layout_proved) {
    plan.enabled = false;
    plan.rejection_reason = "dirty return-stack dispatch lacks a safe layout proof";
  }
  return plan;
}

DirtyReturnStackDispatchAllocationPlan allocate_dirty_return_stack_dispatch_layout(
    std::vector<int> stack, int return_count, const std::vector<MachineItem>& layout_items,
    const DirtyReturnStackDispatchOptions& options) {
  DirtyReturnStackDispatchAllocationPlan allocation{
      .items = layout_items,
  };

  DirtyReturnStackDispatchPlan target_plan =
      plan_dirty_return_stack_dispatch(stack, return_count, options);
  if (!target_plan.enabled) {
    allocation.dispatch = std::move(target_plan);
    allocation.rejection_reason = allocation.dispatch.rejection_reason;
    return allocation;
  }

  DirtyReturnStackDispatchPlan existing =
      plan_dirty_return_stack_dispatch(stack, return_count, layout_items, options);
  if (existing.enabled && existing.layout_proved) {
    allocation.dispatch = std::move(existing);
    allocation.allocated = true;
    allocation.size_rescue_only = true;
    allocation.control_flow_rewrite_enabled = false;
    return allocation;
  }

  if (!options.size_rescue) {
    allocation.dispatch = std::move(existing);
    allocation.rejection_reason = "dirty return-stack dispatch allocator requires size-rescue mode";
    return allocation;
  }
  if (target_plan.dirty_targets.empty()) {
    allocation.dispatch = std::move(target_plan);
    allocation.rejection_reason = "dirty return-stack dispatch allocator has no dirty targets";
    return allocation;
  }

  const int max_target = *std::max_element(target_plan.dirty_targets.begin(),
                                           target_plan.dirty_targets.end());
  if (max_target < 0 || max_target > 104) {
    allocation.dispatch = std::move(target_plan);
    allocation.rejection_reason =
        "dirty return-stack dispatch allocator target is outside official 00..A4 cells";
    return allocation;
  }

  std::vector<MachineItem> candidate = layout_items;
  int total_padding = 0;
  const int max_rounds = std::max(1, options.max_fixed_point_rounds);
  for (int round = 0; round < max_rounds; ++round) {
    DirtyReturnStackDispatchPlan current =
        plan_dirty_return_stack_dispatch(stack, return_count, candidate, options);
    if (current.enabled && current.layout_proved) {
      allocation.dispatch = std::move(current);
      allocation.items = std::move(candidate);
      allocation.padding_cells = total_padding;
      allocation.fixed_point_rounds = round;
      allocation.allocated = true;
      allocation.size_rescue_only = true;
      allocation.control_flow_rewrite_enabled = false;
      return allocation;
    }

    const std::optional<DirtyReturnStackDispatchCellProof> unsafe =
        first_unsafe_dirty_dispatch_cell(current);
    if (!unsafe.has_value()) {
      allocation.dispatch = std::move(current);
      allocation.rejection_reason = allocation.dispatch.rejection_reason.empty()
                                        ? "dirty return-stack dispatch allocator found no unsafe "
                                          "cell to repair"
                                        : allocation.dispatch.rejection_reason;
      return allocation;
    }

    const int candidate_cells = machine_cell_count(candidate);
    const int target = unsafe->actual_pc;
    if (target < 0 || target > 104) {
      allocation.dispatch = std::move(current);
      allocation.rejection_reason =
          "dirty return-stack dispatch allocator target is outside official 00..A4 cells";
      return allocation;
    }

    const int padding = target >= candidate_cells ? target + 1 - candidate_cells : 1;
    if (total_padding + padding > options.max_padding_cells) {
      allocation.dispatch = std::move(current);
      allocation.rejection_reason = "dirty return-stack dispatch allocator would need " +
                                    std::to_string(total_padding + padding) +
                                    " padding cells, above limit " +
                                    std::to_string(options.max_padding_cells);
      return allocation;
    }

    if (target >= candidate_cells) {
      if (!options.allow_append_padding) {
        allocation.dispatch = std::move(current);
        allocation.rejection_reason =
            "dirty return-stack dispatch allocator append-padding search is disabled";
        return allocation;
      }
      for (int index = 0; index < padding; ++index)
        candidate.push_back(dirty_dispatch_safe_padding_cell());
      total_padding += padding;
      continue;
    }

    if (layout_has_numeric_address_targets(candidate)) {
      allocation.dispatch = std::move(current);
      allocation.rejection_reason =
          "dirty return-stack dispatch allocator cannot shift layouts with numeric address operands";
      return allocation;
    }
    const MachineLayout candidate_layout = machine_layout(candidate);
    const std::optional<int> insertion_index =
        insertion_index_preserving_labels_at_address(candidate, candidate_layout, target);
    if (!insertion_index.has_value()) {
      allocation.dispatch = std::move(current);
      allocation.rejection_reason =
          "dirty return-stack dispatch allocator could not find an insertion point for unsafe "
          "dirty target";
      return allocation;
    }
    candidate.insert(candidate.begin() + *insertion_index, dirty_dispatch_safe_padding_cell());
    total_padding += 1;
  }

  allocation.dispatch = plan_dirty_return_stack_dispatch(stack, return_count, candidate, options);
  if (allocation.dispatch.enabled && allocation.dispatch.layout_proved) {
    allocation.items = std::move(candidate);
    allocation.padding_cells = total_padding;
    allocation.fixed_point_rounds = max_rounds;
    allocation.allocated = true;
    allocation.size_rescue_only = true;
    allocation.control_flow_rewrite_enabled = false;
    return allocation;
  }
  allocation.rejection_reason = "dirty return-stack dispatch allocator did not converge within " +
                                std::to_string(max_rounds) + " fixed-point round" +
                                (max_rounds == 1 ? "" : "s");
  return allocation;
}

std::vector<std::vector<int>> dirty_return_stack_dispatch_candidate_stacks() {
  return {
      {19, 27, 35, 43, 51},
      {27, 35, 43, 51, 59},
      {35, 43, 51, 59, 67},
      {43, 51, 59, 67, 75},
  };
}

std::vector<DirtyReturnStackDispatchAllocationPlan>
allocate_dirty_return_stack_dispatch_layouts(const std::vector<MachineItem>& layout,
                                             const DirtyReturnStackDispatchOptions& options) {
  std::vector<DirtyReturnStackDispatchAllocationPlan> allocations;
  const std::vector<std::vector<int>> stacks =
      dirty_return_stack_dispatch_candidate_stacks();
  const int return_count = kReturnStackDepth + std::max(1, options.min_dirty_targets);
  allocations.reserve(stacks.size());
  for (const std::vector<int>& stack : stacks) {
    allocations.push_back(
        allocate_dirty_return_stack_dispatch_layout(stack, return_count, layout, options));
  }
  return allocations;
}

PostLayoutIndirectFlowResult
optimize_post_layout_return_stack_script(const std::vector<MachineItem>& items) {
  if (!scan_return_stack_script_opportunity(items).possible) {
    return PostLayoutIndirectFlowResult{
        .items = items,
    };
  }

  std::vector<MachineItem> current = items;
  int applied = 0;
  int scripts = 0;
  int dirty_allocator_padding = 0;
  int dirty_allocator_rounds = 0;

  for (int round = 0; round < kMaxRewriteRounds; ++round) {
    const std::optional<ScriptPlan> plan = find_best_script_plan(current);
    if (!plan.has_value())
      break;
    const MachineLayout current_layout = machine_layout(current);
    std::vector<MachineItem> candidate = apply_script_plan(current, *plan);
    if (!dirty_overflow_script_plan_proved(current_layout, candidate, *plan)) {
      const std::optional<DirtyReturnStackDispatchAllocationPlan> allocation =
          repair_dirty_overflow_script_candidate(current_layout, candidate, *plan);
      if (!allocation.has_value())
        break;
      candidate = allocation->items;
      dirty_allocator_padding += allocation->padding_cells;
      dirty_allocator_rounds += allocation->fixed_point_rounds;
      if (!dirty_overflow_script_plan_proved(current_layout, candidate, *plan))
        break;
    }
    if (machine_cell_count(candidate) >= machine_cell_count(current))
      break;
    if (!return_stack_candidate_beats_address_overlay(current, candidate))
      break;
    applied += script_plan_rewrite_count(*plan);
    ++scripts;
    current = std::move(candidate);
  }

  if (applied == 0) {
    return PostLayoutIndirectFlowResult{
        .items = items,
    };
  }

  std::vector<passes::AppliedOptimization> optimizations;
  if (dirty_allocator_padding > 0) {
    optimizations.push_back(passes::AppliedOptimization{
        .name = "return-stack-dirty-dispatch-allocator",
        .detail = "Inserted " + std::to_string(dirty_allocator_padding) +
                  " executable dirty-dispatch cell" +
                  (dirty_allocator_padding == 1 ? "" : "s") + " across " +
                  std::to_string(dirty_allocator_rounds) + " fixed-point repair round" +
                  (dirty_allocator_rounds == 1 ? "" : "s") +
                  " before rewriting dirty overflow continuation jumps.",
    });
  }
  optimizations.push_back(passes::AppliedOptimization{
      .name = "return-stack-script",
      .detail = "Replaced " + std::to_string(applied) +
                " fixed scripted continuation jump" +
                (applied == 1 ? "" : "s") + " with В/О using " +
                std::to_string(scripts) + " proven existing ПП charge chain" +
                (scripts == 1 ? "" : "s") + ".",
  });

  return PostLayoutIndirectFlowResult{
      .items = std::move(current),
      .optimizations = std::move(optimizations),
      .applied = applied,
  };
}

ReturnStackPostLayoutPipelineReport measure_return_stack_post_layout_pipeline(
    const std::vector<MachineItem>& items, const CompileOptions& options,
    int indirect_flow_rescue_above) {
  ReturnStackPostLayoutPipelineReport report{
      .input_cells = machine_cell_count(items),
  };

  const PostLayoutIndirectFlowResult script =
      optimize_post_layout_return_stack_script(items);
  report.return_stack_script_applied = script.applied;

  const PostLayoutIndirectFlowResult overlay =
      optimize_post_layout_address_code_overlay(script.items);
  report.address_overlay_applied = overlay.applied;

  const PostLayoutIndirectFlowResult flow =
      optimize_post_layout_indirect_flow(overlay.items, options, indirect_flow_rescue_above);
  report.indirect_flow_applied = flow.applied;
  report.final_cells = machine_cell_count(flow.items);
  return report;
}

ReturnStackPostLayoutPipelineComparison compare_return_stack_post_layout_pipeline(
    const std::vector<MachineItem>& current, const std::vector<MachineItem>& candidate,
    const CompileOptions& options, int indirect_flow_rescue_above) {
  ReturnStackPostLayoutPipelineComparison comparison{
      .current =
          measure_return_stack_post_layout_pipeline(current, options, indirect_flow_rescue_above),
      .candidate =
          measure_return_stack_post_layout_pipeline(candidate, options, indirect_flow_rescue_above),
  };
  comparison.candidate_better =
      comparison.candidate.final_cells < comparison.current.final_cells;
  return comparison;
}

} // namespace mkpro::core
