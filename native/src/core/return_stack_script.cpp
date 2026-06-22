#include "mkpro/core/return_stack_script.hpp"

#include <algorithm>
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

ReturnStackStartupLayoutPlan build_return_stack_startup_layout(
    const std::vector<ReturnStackTailBlock>& tails,
    const std::vector<MachineItem>& entry_body,
    const std::string& entry_label,
    const ReturnStackStartupLayoutOptions& options) {
  ReturnStackStartupLayoutPlan plan;
  plan.transitions = static_cast<int>(tails.size());
  plan.existing_call_sites =
      std::max(0, std::min(options.existing_call_sites, plan.transitions));
  plan.strategy = plan.existing_call_sites > 0 ? "existing-callsite-layout"
                                               : "one-shot-startup-prologue";
  if (tails.size() < 2 || tails.size() > static_cast<std::size_t>(kMaxScriptReturns)) {
    plan.rejection_reason = "return-stack startup layout requires 2..5 transitions";
    return plan;
  }

  plan.injected_call_sites = static_cast<int>(tails.size());
  plan.paid_call_sites = std::max(0, plan.injected_call_sites - plan.existing_call_sites);
  plan.transition_savings = static_cast<int>(tails.size());
  plan.charge_cost = plan.paid_call_sites * 2;
  plan.net_savings = plan.transition_savings - plan.charge_cost;
  plan.profitable = plan.net_savings >= options.min_net_savings;
  if (!plan.profitable) {
    plan.rejection_reason = "return-stack startup layout does not meet strict net-savings "
                            "threshold";
  }

  for (std::size_t index = 0; index < tails.size(); ++index) {
    MachineItem charge_label = MachineItem::label("__return_stack_charge_" +
                                                  std::to_string(index));
    charge_label.hidden = true;
    plan.items.push_back(std::move(charge_label));

    plan.items.push_back(MachineItem::op(0x53, "ПП"));
    const std::string target = index + 1U < tails.size()
                                   ? "__return_stack_charge_" + std::to_string(index + 1U)
                                   : entry_label;
    plan.items.push_back(MachineItem::address(target));

    plan.items.push_back(MachineItem::label(tails.at(index).label));
    plan.items.insert(plan.items.end(), tails.at(index).body.begin(), tails.at(index).body.end());
  }

  plan.items.push_back(MachineItem::label(entry_label));
  plan.items.insert(plan.items.end(), entry_body.begin(), entry_body.end());
  return plan;
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

PostLayoutIndirectFlowResult
optimize_post_layout_return_stack_script(const std::vector<MachineItem>& items) {
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
