#include "mkpro/core/passes/selector_charge_literal_sinking.hpp"

#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/register_allocator.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::core::passes {
namespace {

std::optional<std::string> role_suffix(const std::vector<CellRole>& roles,
                                      std::string_view prefix) {
  std::optional<std::string> result;
  for (const auto& role : roles) {
    if (!role.starts_with(prefix))
      continue;
    if (result.has_value() || role.size() == prefix.size())
      return std::nullopt;
    result = role.substr(prefix.size());
  }
  return result;
}

std::optional<std::string> role_suffix(const IrOp& op, std::string_view prefix) {
  return role_suffix(op.meta.roles, prefix);
}

std::optional<std::string> role_suffix(const MachineItem& item, std::string_view prefix) {
  return role_suffix(item.roles, prefix);
}

bool plain_digit(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode >= 0 && op.opcode <= 9 &&
         !has_rewrite_barrier(op) && op.meta.roles.empty() &&
         op.target_meta.roles.empty() && op.meta.semantic_call_origins.empty() &&
         !op.meta.manual_interaction.has_value() && !op.meta.tactic.has_value() &&
         !op.procedure_boundary.has_value() && !op.meta.logical_register_name.has_value();
}

bool symbolic_geometry(const std::vector<IrOp>& ops) {
  for (const auto& op : ops) {
    if (op.meta.raw || op.kind == IrKind::OrphanAddress ||
        op.target_meta.formal_opcode.has_value() ||
        op.meta.indirect_flow_formal_targets.has_value())
      return false;
    if ((op.kind == IrKind::Jump || op.kind == IrKind::CondJump ||
         op.kind == IrKind::Call || op.kind == IrKind::Loop) &&
        !std::holds_alternative<std::string>(op.target))
      return false;
    if (op.meta.indirect_flow_targets.has_value()) {
      for (const auto& target : *op.meta.indirect_flow_targets)
        if (!std::holds_alternative<std::string>(target))
          return false;
    }
  }
  return true;
}

std::size_t next_op(const std::vector<IrOp>& ops, std::size_t index) {
  while (index < ops.size() && ops[index].kind == IrKind::Label)
    ++index;
  return index;
}

struct Charge {
  std::size_t high = 0;
  std::size_t low = 0;
  std::size_t call = 0;
  std::string leaf;
};

std::optional<Charge> charge_before_call(const std::vector<IrOp>& ops,
                                          std::size_t call, std::string_view reg) {
  // Labels inside the two digits/transfer could admit a separate entry.
  if (call < 2 || (ops[call].kind != IrKind::Call && ops[call].kind != IrKind::Jump))
    return std::nullopt;
  const auto high = role_suffix(ops[call - 2], "late-decimal-selector-high:");
  const auto low = role_suffix(ops[call - 1], "late-decimal-selector-low:");
  const auto high_reg = role_suffix(ops[call - 2], "late-decimal-selector-register:");
  const auto low_reg = role_suffix(ops[call - 1], "late-decimal-selector-register:");
  if (!high || !low || *high != *low || !high_reg || !low_reg ||
      *high_reg != reg || *low_reg != reg ||
      ops[call - 2].kind != IrKind::Plain || ops[call - 1].kind != IrKind::Plain ||
      ops[call - 2].opcode < 0 || ops[call - 2].opcode > 9 ||
      ops[call - 1].opcode < 0 || ops[call - 1].opcode > 9 ||
      ops[call - 2].meta.manual_interaction.has_value() ||
      ops[call - 1].meta.manual_interaction.has_value())
    return std::nullopt;
  return Charge{call - 2, call - 1, call, *high};
}

bool incoming_only_from(const AuthoritativePostLayoutControlFlow& flow,
                         std::size_t item, const std::set<std::size_t>& allowed) {
  for (const auto& external : flow.external_entries)
    if (external.entry.item_index == item)
      return false;
  bool reached = false;
  for (std::size_t source = 0; source < flow.execution_successors.size(); ++source) {
    for (const auto target : flow.execution_successors[source]) {
      if (flow.execution_states[target].item_index != item)
        continue;
      reached = true;
      if (!allowed.contains(flow.execution_states[source].item_index))
        return false;
    }
  }
  return reached;
}

bool literal_entry_is_closed(const std::vector<MachineItem>& items,
                             const AuthoritativePostLayoutControlFlow& flow,
                             std::size_t entry) {
  for (const auto& external : flow.external_entries)
    if (external.entry.item_index == entry)
      return false;
  bool reached = false;
  for (std::size_t source = 0; source < flow.execution_successors.size(); ++source) {
    for (const auto target : flow.execution_successors[source]) {
      if (flow.execution_states[target].item_index != entry)
        continue;
      const auto& before = items[flow.execution_states[source].item_index];
      if (before.raw || before.manual_interaction.has_value() ||
          !selector_charge_entry_closer_opcode(before.opcode))
        return false;
      reached = true;
    }
  }
  return reached;
}

// The entry contract is a compiler-owned high/low charge followed by the
// shared store. Refine ONLY its stable selector's indirect edges. Mark every
// possible subsequent write/unsupported selector use as a barrier: the old
// selector identity must never be assumed past its first redefinition.
// The shared equality engine still proves all call contexts and all remaining
// branches; no display/return boundary is exempted from stack/X1/X2 equality.
bool entry_converges(const std::vector<MachineItem>& original,
                      const AuthoritativePostLayoutControlFlow& flow,
                      int entry, int reg, int leaf, bool x_equal) {
  auto items = original;
  auto restricted = flow;
  for (std::size_t index = 0; index < items.size(); ++index) {
    auto& item = items[index];
    if (item.kind != MachineItemKind::Op)
      continue;
    const int opcode = item.opcode;
    bool barrier = opcode == 0x40 + reg;
    const bool memory = (opcode >= 0xb0 && opcode <= 0xbe) ||
                        (opcode >= 0xd0 && opcode <= 0xde);
    if (memory) {
      const auto targets = flow.indirect_memory_targets.find(index);
      barrier = barrier || (opcode & 15) == reg ||
                targets == flow.indirect_memory_targets.end() ||
                (opcode >= 0xb0 && opcode <= 0xbe &&
                 std::find(targets->second.begin(), targets->second.end(), reg) !=
                     targets->second.end());
    }
    const auto indirect = restricted.indirect_flow_targets.find(index);
    if (indirect != restricted.indirect_flow_targets.end() && (opcode & 15) == reg) {
      if (opcode != 0xa0 + reg && opcode != 0x80 + reg)
        barrier = true;
      else {
        auto& targets = indirect->second;
        targets.erase(std::remove_if(targets.begin(), targets.end(),
                                     [leaf](const auto& target) {
                                       return target.address != leaf;
                                     }), targets.end());
        if (targets.empty())
          barrier = true;
      }
    }
    if (barrier)
      item.raw = true;
  }
  for (std::size_t state = 0; state < restricted.execution_states.size(); ++state) {
    const auto& item = items[restricted.execution_states[state].item_index];
    if (item.raw || (item.opcode != 0xa0 + reg && item.opcode != 0x80 + reg))
      continue;
    auto& successors = restricted.execution_successors[state];
    successors.erase(std::remove_if(successors.begin(), successors.end(),
                                    [&](std::size_t next) {
                                      return restricted.execution_states[next].address != leaf;
                                    }), successors.end());
    auto& edges = restricted.execution_edges[state];
    edges.erase(std::remove_if(edges.begin(), edges.end(), [&](const auto& edge) {
                  return restricted.execution_states[edge.target_state].address != leaf;
                }), edges.end());
  }
  // Before the first recall, entry modes differ: the original rotation
  // closes decimal entry whereas the sunk literal leaves it open. A direct
  // recall has an unconditional stack lift and closes entry in both runs.
  // Transfer that boundary explicitly before entering the equal-mode proof.
  StackValueEqualityState equality{{x_equal, false, false, false}, false, false};
  transfer_stack_value_equality(equality, 0x60, StackValueEqualityStepKind::Recall);
  return prove_post_layout_stack_entry_equality(items, restricted, entry + 1, equality);
}

} // namespace

PassResult selector_charge_literal_sinking(const std::vector<IrOp>& ops,
                                           const PassContext& context) {
  const auto unchanged = [&] { return PassResult{.ops = ops}; };
  const bool trace = std::getenv("MKPRO_NATIVE_TRACE_SELECTOR_LITERAL_SINK") != nullptr;
  if (!symbolic_geometry(ops))
    return unchanged();
  std::vector<std::size_t> lifts;
  for (std::size_t i = 1; i + 3 < ops.size(); ++i)
    if (ops[i].kind == IrKind::Plain && ops[i].opcode == 0x0e &&
        plain_digit(ops[i - 1]) &&
        role_suffix(ops[i + 1], "late-decimal-selector-high:"))
      lifts.push_back(i);
  if (lifts.empty())
    return unchanged();

  const auto items = lower_ir_to_machine(ops);
  PostLayoutControlFlowOptions options;
  options.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(context.options));
  const auto flow = build_post_layout_control_flow(items, options);
  if (!flow.proved) {
    if (trace)
      for (const auto& reason : flow.reasons)
        std::cerr << "[selector-charge-literal-sinking] CFG: " << reason << '\n';
    return unchanged();
  }

  std::vector<std::size_t> machine(ops.size());
  std::vector<int> addresses(ops.size());
  std::map<std::string, std::size_t> labels;
  std::size_t item = 0;
  int address = 0;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    machine[i] = item;
    addresses[i] = address;
    if (ops[i].kind == IrKind::Label) {
      if (!labels.emplace(ops[i].name, next_op(ops, i + 1)).second)
        return unchanged();
      ++item;
    } else {
      item += static_cast<std::size_t>(cells_per_op(ops[i]));
      address += cells_per_op(ops[i]);
    }
  }
  for (const auto lift : lifts) {
    const std::size_t call = lift + 3;
    if ((ops[call].kind != IrKind::Call && ops[call].kind != IrKind::Jump) ||
        !std::holds_alternative<std::string>(ops[call].target))
      continue;
    const auto target = labels.find(std::get<std::string>(ops[call].target));
    if (target == labels.end() || target->second >= ops.size())
      continue;
    const std::size_t store = target->second;
    const std::size_t rotate = next_op(ops, store + 1);
    const std::size_t body = next_op(ops, rotate + 1);
    if (body >= ops.size() || ops[body].kind != IrKind::Recall ||
        ops[body].opcode < 0x60 || ops[body].opcode > 0x6e ||
        ops[body].meta.manual_interaction.has_value() ||
        ops[store].kind != IrKind::Store ||
        ops[store].opcode < 0x47 || ops[store].opcode > 0x4e ||
        ops[rotate].kind != IrKind::Plain || ops[rotate].opcode != 0x25 ||
        ops[store].meta.manual_interaction || ops[rotate].meta.manual_interaction ||
        ops[rotate].procedure_boundary.has_value())
      continue;
    const int reg = ops[store].opcode & 15;
    std::vector<Charge> charges;
    bool valid = true;
    std::set<std::size_t> incoming;
    for (std::size_t i = 0; i < ops.size(); ++i) {
      if (ops[i].kind != IrKind::Call && ops[i].kind != IrKind::Jump)
        continue;
      const auto name = std::get_if<std::string>(&ops[i].target);
      const auto entry = name == nullptr ? labels.end() : labels.find(*name);
      if (entry == labels.end() || entry->second != store)
        continue;
      const auto charge = charge_before_call(ops, i, ops[store].register_name);
      if (!charge) { valid = false; break; }
      charges.push_back(*charge);
      incoming.insert(machine[i]);
      valid = valid &&
          incoming_only_from(flow, machine[charge->low], {machine[charge->high]}) &&
          incoming_only_from(flow, machine[i], {machine[charge->low]});
    }
    if (!valid || charges.empty() ||
        !incoming_only_from(flow, machine[store], incoming) ||
        !incoming_only_from(flow, machine[rotate], {machine[store]}))
      continue;
    std::size_t begin = lift;
    while (begin > 0 && plain_digit(ops[begin - 1]))
      --begin;
    if (begin == lift || lift - begin > 8 ||
        !literal_entry_is_closed(items, flow, machine[begin]) ||
        (begin > 0 && ops[begin - 1].kind == IrKind::Plain &&
         ops[begin - 1].opcode >= 0 && ops[begin - 1].opcode <= 0x0c))
      continue;
    for (std::size_t i = begin + 1; i <= lift + 1; ++i)
      valid = valid && incoming_only_from(flow, machine[i], {machine[i - 1]});
    if (!valid)
      continue;

    for (const auto& charge : charges) {
      const auto leaf = labels.find(charge.leaf);
      valid = leaf != labels.end() && leaf->second < ops.size() &&
          entry_converges(items, flow, addresses[body], reg, addresses[leaf->second],
                          charge.call == call);
      if (trace)
        std::cerr << "[selector-charge-literal-sinking] caller=" << charge.call
                  << " constant-caller=" << call << " equality=" << valid << '\n';
      if (!valid)
        break;
    }
    if (!valid)
      continue;
    const std::string identity = std::get<std::string>(ops[call].target);
    std::string literal;
    for (std::size_t digit = begin; digit < lift; ++digit)
      literal += static_cast<char>('0' + ops[digit].opcode);
    std::vector<IrOp> result;
    result.reserve(ops.size() - 2);
    for (std::size_t i = 0; i < ops.size(); ++i) {
      // The same fresh-entry proof also permits the first selector digit
      // to supply the lift. Its final stack is identical to B-up + digits.
      if ((i >= begin && i <= lift) || i == rotate)
        continue;
      result.push_back(ops[i]);
      if (i == lift + 1)
        result.back().meta.roles.push_back("selector-sunk-literal-source:" + identity);
      if (i == store) {
        auto& entry = result.back();
        entry.meta.roles.push_back("selector-sunk-literal-id:" + identity);
        entry.meta.roles.push_back("selector-sunk-literal-value:" + literal);
        if (entry.meta.comment.has_value()) {
          const auto position = entry.meta.comment->find("entry-repair=preserve-xyz");
          if (position != std::string::npos)
            entry.meta.comment->replace(position, std::string("entry-repair=preserve-xyz").size(),
                                        "entry-repair=sunk-literal");
        }
        for (std::size_t digit = begin; digit < lift; ++digit) {
          auto moved = ops[digit];
          moved.procedure_name = ops[store].procedure_name;
          moved.hidden = ops[store].hidden;
          result.push_back(std::move(moved));
        }
      }
    }
    if (!build_post_layout_control_flow(lower_ir_to_machine(result), options).proved)
      continue;
    return PassResult{
        .ops = std::move(result), .applied = 1,
        .optimizations = {{
            .name = "selector-charge-literal-sinking",
            .detail = "Moved an ordinary literal behind a shared selector charge; "
                      "proved every caller's selector identity and stack/X1/X2 "
                      "convergence before observation or selector redefinition. "
                      "Removed the entry rotation and explicit lift before layout.",
        }},
    };
  }
  return unchanged();
}

bool prove_selector_charge_sunk_literal_entry(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    std::size_t store, std::size_t charge_start, int leaf) {
  if (!flow.proved || store >= items.size() || charge_start >= items.size())
    return false;
  const auto id = role_suffix(items[store], "selector-sunk-literal-id:");
  const auto literal = role_suffix(items[store], "selector-sunk-literal-value:");
  if (!id || !literal || literal->size() > 8 ||
      !std::all_of(literal->begin(), literal->end(),
                   [](char c) { return c >= '0' && c <= '9'; }) ||
      items[store].kind != MachineItemKind::Op || items[store].opcode < 0x47 ||
      items[store].opcode > 0x4e || items[store].raw ||
      items[store].manual_interaction.has_value())
    return false;
  const int reg = items[store].opcode & 15;
  std::optional<std::size_t> origin;
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (index != store &&
        role_suffix(items[index], "selector-sunk-literal-id:") == id)
      return false;
    if (role_suffix(items[index], "selector-sunk-literal-source:") == id) {
      if (origin.has_value()) return false;
      origin = index;
    }
  }
  if (!origin || !literal_entry_is_closed(items, flow, *origin))
    return false;

  const auto next = [&](std::size_t index) {
    while (++index < items.size() && items[index].kind == MachineItemKind::Label) {}
    return index;
  };
  std::size_t previous = store;
  for (const char digit : *literal) {
    const std::size_t current = next(previous);
    if (current >= items.size() || items[current].kind != MachineItemKind::Op ||
        items[current].opcode != digit - '0' || items[current].raw ||
        items[current].manual_interaction.has_value() ||
        !incoming_only_from(flow, current, {previous}))
      return false;
    previous = current;
  }
  const std::size_t body = next(previous);
  if (body >= items.size() || items[body].kind != MachineItemKind::Op ||
      items[body].opcode < 0x60 || items[body].opcode > 0x6e ||
      items[body].raw || items[body].manual_interaction.has_value() ||
      !incoming_only_from(flow, body, {previous}))
    return false;

  const std::size_t low = next(charge_start);
  if (low >= items.size() || items[charge_start].raw || items[low].raw ||
      items[charge_start].manual_interaction.has_value() ||
      items[low].manual_interaction.has_value() ||
      items[charge_start].kind != MachineItemKind::Op ||
      items[low].kind != MachineItemKind::Op ||
      items[charge_start].opcode < 0 || items[charge_start].opcode > 9 ||
      items[low].opcode < 0 || items[low].opcode > 9 ||
      items[charge_start].opcode * 10 + items[low].opcode != leaf ||
      !incoming_only_from(flow, low, {charge_start}))
    return false;
  const auto high_target = role_suffix(items[charge_start], "late-decimal-selector-high:");
  if (!high_target || role_suffix(items[low], "late-decimal-selector-low:") != high_target ||
      role_suffix(items[charge_start], "late-decimal-selector-register:") !=
          std::optional<std::string>(register_name_for_index(reg)) ||
      role_suffix(items[low], "late-decimal-selector-register:") !=
          std::optional<std::string>(register_name_for_index(reg)))
    return false;

  int body_address = 0;
  for (std::size_t index = 0; index < body; ++index)
    if (items[index].kind != MachineItemKind::Label) ++body_address;
  return entry_converges(items, flow, body_address, reg, leaf, charge_start == *origin);
}

} // namespace mkpro::core::passes
