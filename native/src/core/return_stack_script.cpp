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

std::vector<FlowReference> direct_flow_references(const std::vector<MachineItem>& items,
                                                  const MachineLayout& layout) {
  std::vector<FlowReference> result;
  for (int index = 0; index + 1 < static_cast<int>(items.size()); ++index) {
    if (const std::optional<FlowReference> ref =
            direct_flow_reference_at(items, layout, index)) {
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
  if (op_index + 1 >= static_cast<int>(items.size()))
    return std::nullopt;
  const MachineItem& op = items.at(static_cast<std::size_t>(op_index));
  const MachineItem& target = items.at(static_cast<std::size_t>(op_index + 1));
  if (op.kind != MachineItemKind::Op || op.opcode != 0x53 || op.raw ||
      target.kind != MachineItemKind::Address || !address_target_is_label(target)) {
    return std::nullopt;
  }
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
    if (item.opcode == 0x52 || item.opcode == 0x53 || is_address_taking_opcode(item.opcode) ||
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

  if (!terminal_stop_from(items, layout, cursor))
    return std::nullopt;

  ScriptPlan plan{
      .calls = std::move(calls),
      .jumps = std::move(jumps),
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
    if (item.kind != MachineItemKind::Op || item.opcode != 0x53)
      continue;
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
  if (tail.kind != IrKind::Jump)
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

std::vector<MachineItem> materialize_layout_items(
    const ReturnStackLayoutOpportunity& opportunity,
    const std::vector<std::size_t>& tail_order) {
  std::vector<MachineItem> items;
  for (std::size_t physical_index = 0; physical_index < tail_order.size();
       ++physical_index) {
    const ReturnStackTailBlock& tail =
        opportunity.tails.at(tail_order.at(physical_index));
    MachineItem charge_label = MachineItem::label(charge_label_name(physical_index));
    charge_label.hidden = true;
    items.push_back(std::move(charge_label));

    items.push_back(MachineItem::op(0x53, "ПП"));
    const std::string target = physical_index + 1U < tail_order.size()
                                   ? charge_label_name(physical_index + 1U)
                                   : opportunity.entry_label;
    items.push_back(MachineItem::address(target));

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
};

struct IrTailChainCandidate {
  ReturnStackLayoutOpportunity opportunity;
  std::set<std::string> moved_tail_labels;
  std::string original_entry_label;
  std::string generated_entry_label;
  std::size_t entry_block_index = 0;
};

std::optional<std::vector<IrLabelBlock>> split_label_blocks(const std::vector<IrOp>& ops,
                                                            std::string& rejection_reason) {
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
      blocks.push_back(IrLabelBlock{.label = op.name});
      continue;
    }
    if (blocks.empty()) {
      rejection_reason = "return-stack IR tail layout requires a leading entry label";
      return std::nullopt;
    }
    blocks.back().body.push_back(op);
  }
  if (blocks.size() < 3U) {
    rejection_reason = "return-stack IR tail layout requires one entry block and at least two tail blocks";
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

bool ir_op_can_reference_label(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Loop:
    return true;
  default:
    return false;
  }
}

std::map<std::string, int> direct_ir_label_ref_counts(const std::vector<IrLabelBlock>& blocks) {
  std::map<std::string, int> counts;
  for (const IrLabelBlock& block : blocks) {
    for (const IrOp& op : block.body) {
      if (!ir_op_can_reference_label(op))
        continue;
      if (const std::optional<std::string> target = ir_label_target(op))
        ++counts[*target];
    }
  }
  return counts;
}

bool ir_block_ends_without_fallthrough(const IrLabelBlock& block) {
  if (block.body.empty())
    return false;
  const IrOp& tail = block.body.back();
  if (tail.kind == IrKind::Jump || tail.kind == IrKind::IndirectJump ||
      tail.kind == IrKind::Return || tail.kind == IrKind::Stop) {
    return true;
  }
  return tail.opcode == 0x50 || tail.opcode == 0x51 || tail.opcode == 0x52;
}

bool has_unproved_fallthrough_into_block(const std::vector<IrLabelBlock>& blocks,
                                         std::size_t block_index) {
  if (block_index == 0U)
    return false;
  return !ir_block_ends_without_fallthrough(blocks.at(block_index - 1U));
}

bool tail_chain_has_only_internal_tail_entries(
    const std::vector<IrLabelBlock>& blocks, const IrTailChainCandidate& candidate,
    const std::map<std::string, int>& ref_counts, std::string& rejection_reason) {
  std::map<std::string, int> expected_tail_refs;
  const std::map<std::string, std::size_t> by_label = block_index_by_label(blocks);
  std::optional<std::string> cursor = terminal_jump_target_from_ir_body(candidate.opportunity.entry_body);
  while (cursor.has_value()) {
    ++expected_tail_refs[*cursor];
    const auto block_index = by_label.find(*cursor);
    if (block_index == by_label.end())
      break;
    const std::optional<std::string> next =
        terminal_jump_target_from_ir_body(blocks.at(block_index->second).body);
    if (!next.has_value())
      break;
    cursor = next;
  }

  for (const ReturnStackTailBlock& tail : candidate.opportunity.tails) {
    const int actual_refs = ref_counts.contains(tail.label) ? ref_counts.at(tail.label) : 0;
    const int expected_refs = expected_tail_refs.contains(tail.label) ? expected_tail_refs.at(tail.label) : 0;
    if (actual_refs != expected_refs || expected_refs != 1) {
      rejection_reason = "return-stack IR tail layout requires unique internal entry into tail block " +
                         tail.label;
      return false;
    }
    const auto tail_index = by_label.find(tail.label);
    if (tail_index == by_label.end()) {
      rejection_reason = "return-stack IR tail layout lost a candidate tail block";
      return false;
    }
    if (has_unproved_fallthrough_into_block(blocks, tail_index->second)) {
      rejection_reason = "return-stack IR tail layout cannot move tail block " + tail.label +
                         " because a previous block can fall through into it";
      return false;
    }
  }
  return true;
}

std::optional<IrTailChainCandidate> embedded_tail_chain_opportunity(
    const std::vector<IrLabelBlock>& blocks, std::string& rejection_reason) {
  const std::map<std::string, std::size_t> by_label = block_index_by_label(blocks);
  const std::map<std::string, int> ref_counts = direct_ir_label_ref_counts(blocks);

  for (std::size_t entry_index = 0; entry_index < blocks.size(); ++entry_index) {
    const IrLabelBlock& entry = blocks.at(entry_index);
    std::optional<std::string> cursor = terminal_jump_target_from_ir_body(entry.body);
    if (!cursor.has_value())
      continue;

    std::set<std::string> seen_tail_labels;
    std::vector<ReturnStackTailBlock> tails;
    bool valid_chain = true;
    while (cursor.has_value()) {
      const auto block_it = by_label.find(*cursor);
      if (block_it == by_label.end() || block_it->second == entry_index ||
          seen_tail_labels.contains(*cursor)) {
        valid_chain = false;
        break;
      }
      const IrLabelBlock& tail = blocks.at(block_it->second);
      seen_tail_labels.insert(tail.label);
      tails.push_back(ReturnStackTailBlock{
          .label = tail.label,
          .body = tail.body,
      });

      const std::optional<std::string> next = terminal_jump_target_from_ir_body(tail.body);
      if (next.has_value()) {
        cursor = next;
        continue;
      }
      if (!terminal_stop_from_ir_body(tail.body))
        valid_chain = false;
      break;
    }

    if (!valid_chain || tails.size() < 2U || tails.size() > static_cast<std::size_t>(kMaxScriptReturns))
      continue;

    IrTailChainCandidate candidate{
        .opportunity = ReturnStackLayoutOpportunity{
            .tails = tails,
            .entry_body = entry.body,
            .entry_label = "__return_stack_entry_" + std::to_string(entry_index),
            .entry_at_address_zero = entry_index == 0U,
            .single_start_jump = entry_index != 0U,
        },
        .moved_tail_labels = seen_tail_labels,
        .original_entry_label = entry.label,
        .generated_entry_label = "__return_stack_entry_" + std::to_string(entry_index),
        .entry_block_index = entry_index,
    };

    if (!tail_chain_has_only_internal_tail_entries(blocks, candidate, ref_counts, rejection_reason))
      continue;
    return candidate;
  }

  if (rejection_reason.empty()) {
    rejection_reason = "return-stack IR tail layout found no movable entry block that jumps through a 2..5 tail chain ending in С/П";
  }
  return std::nullopt;
}

void append_ir_block_as_machine(std::vector<MachineItem>& out, const IrLabelBlock& block) {
  out.push_back(MachineItem::label(block.label));
  append_ir_items(out, block.body);
}

std::vector<MachineItem> materialize_embedded_tail_chain_layout(
    const std::vector<IrLabelBlock>& blocks, const IrTailChainCandidate& candidate,
    const ReturnStackLayoutOpportunityAnalysis& analysis) {
  const ReturnStackStartupLayoutPlan plan = materialize_return_stack_layout(analysis);
  std::vector<MachineItem> out;
  for (std::size_t index = 0; index < blocks.size(); ++index) {
    const IrLabelBlock& block = blocks.at(index);
    if (candidate.moved_tail_labels.contains(block.label))
      continue;
    if (index != candidate.entry_block_index) {
      append_ir_block_as_machine(out, block);
      continue;
    }

    MachineItem wrapper = MachineItem::label(candidate.original_entry_label);
    wrapper.hidden = false;
    out.push_back(std::move(wrapper));
    out.insert(out.end(), plan.items.begin(), plan.items.end());
  }
  return out;
}

std::optional<int> valid_concrete_existing_call_site_count(
    const ReturnStackLayoutOpportunity& opportunity,
    const std::vector<std::size_t>& tail_order, ReturnStackStartupLayoutPlan& plan) {
  if (opportunity.existing_call_sites.empty())
    return std::nullopt;

  std::set<std::pair<std::string, std::string>> required;
  for (std::size_t physical_index = 0; physical_index < tail_order.size();
       ++physical_index) {
    const std::string expected_target =
        physical_index + 1U < tail_order.size() ? charge_label_name(physical_index + 1U)
                                                : opportunity.entry_label;
    const std::string expected_continuation =
        opportunity.tails.at(tail_order.at(physical_index)).label;
    required.insert({expected_target, expected_continuation});
  }

  std::set<std::pair<std::string, std::string>> used_edges;
  std::set<int> used_sources;
  int valid = 0;
  int invalid = 0;
  for (const ReturnStackExistingCallSite& site : opportunity.existing_call_sites) {
    const std::pair<std::string, std::string> edge{site.target_label,
                                                   site.continuation_label};
    if (site.source_address < 0 || used_sources.contains(site.source_address) ||
        !required.contains(edge) || used_edges.contains(edge)) {
      ++invalid;
      continue;
    }
    used_sources.insert(site.source_address);
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

  plan.one_shot_proved = input.entry_at_address_zero || input.single_start_jump;
  if (plan.one_shot_proved) {
    plan.proofs.push_back(input.entry_at_address_zero
                              ? "charging chain starts at address 00"
                              : "charging chain is reached by a single start jump");
  }

  plan.no_backward_charge_jumps = true;
  for (int index = 0; index < plan.transitions; ++index) {
    const auto label_it = layout.labels.find("__return_stack_charge_" + std::to_string(index));
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
    const auto label_it = layout.labels.find("__return_stack_charge_" + std::to_string(index));
    if (label_it == layout.labels.end()) {
      plan.no_external_charge_entries = false;
      break;
    }
    const auto previous_label_it =
        layout.labels.find("__return_stack_charge_" + std::to_string(index - 1));
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
    const auto final_charge_it =
        layout.labels.find("__return_stack_charge_" + std::to_string(plan.transitions - 1));
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

ReturnStackIrTailLayoutSearch analyze_return_stack_ir_tail_layout(
    const std::vector<IrOp>& ops, const ReturnStackStartupLayoutOptions& options) {
  ReturnStackIrTailLayoutSearch search;
  std::string rejection;
  const std::optional<std::vector<IrLabelBlock>> blocks = split_label_blocks(ops, rejection);
  if (!blocks.has_value()) {
    search.rejection_reason = rejection;
    return search;
  }
  const std::optional<IrTailChainCandidate> candidate =
      embedded_tail_chain_opportunity(*blocks, rejection);
  if (!candidate.has_value()) {
    search.rejection_reason = rejection;
    return search;
  }
  search.has_opportunity = true;
  search.analysis = analyze_return_stack_layout_opportunity(candidate->opportunity, options);
  if (search.analysis.plan.profitable) {
    search.materialized = true;
    search.materialized_items =
        materialize_embedded_tail_chain_layout(*blocks, *candidate, search.analysis);
  }
  if (!search.analysis.plan.rejection_reason.empty())
    search.rejection_reason = search.analysis.plan.rejection_reason;
  return search;
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
    if (item.opcode == 0x53) {
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

  for (int round = 0; round < kMaxRewriteRounds; ++round) {
    const std::optional<ScriptPlan> plan = find_best_script_plan(current);
    if (!plan.has_value())
      break;
    std::vector<MachineItem> candidate = apply_script_plan(current, *plan);
    if (machine_cell_count(candidate) >= machine_cell_count(current))
      break;
    if (!return_stack_candidate_beats_address_overlay(current, candidate))
      break;
    applied += static_cast<int>(plan->jumps.size());
    ++scripts;
    current = std::move(candidate);
  }

  if (applied == 0) {
    return PostLayoutIndirectFlowResult{
        .items = items,
    };
  }

  return PostLayoutIndirectFlowResult{
      .items = std::move(current),
      .optimizations =
          {
              passes::AppliedOptimization{
                  .name = "return-stack-script",
                  .detail = "Replaced " + std::to_string(applied) +
                            " fixed scripted continuation jump" +
                            (applied == 1 ? "" : "s") +
                            " with В/О using " + std::to_string(scripts) +
                            " proven existing ПП charge chain" +
                            (scripts == 1 ? "" : "s") + ".",
              },
          },
      .applied = applied,
  };
}

} // namespace mkpro::core
