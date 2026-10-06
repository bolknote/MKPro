#include "mkpro/core/passes/helper_entry_store_factoring.hpp"

#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace mkpro::core::passes {
namespace {

struct Index {
  std::vector<int> addresses;
  std::map<int, std::size_t> cells;
  std::map<std::string, std::size_t> labels;
};

Index index_items(const std::vector<MachineItem>& items) {
  Index index;
  int address = 0;
  for (std::size_t i = 0; i < items.size(); ++i) {
    index.addresses.push_back(address);
    if (items[i].kind == MachineItemKind::Label)
      index.labels.emplace(items[i].name, i);
    else
      index.cells.emplace(address++, i);
  }
  return index;
}

std::optional<std::size_t> previous_cell(const std::vector<MachineItem>& items,
                                         std::size_t i) {
  while (i > 0) {
    if (items[--i].kind != MachineItemKind::Label) return i;
  }
  return std::nullopt;
}

bool direct_recall(const MachineItem& item) {
  return item.kind == MachineItemKind::Op &&
         item.opcode >= 0x60 && item.opcode <= 0x6e;
}

bool direct_store(const MachineItem& item) {
  return item.kind == MachineItemKind::Op &&
         item.opcode >= 0x40 && item.opcode <= 0x4e;
}

bool call(const MachineItem& item) {
  return item.kind == MachineItemKind::Op &&
         (item.opcode == 0x53 ||
          (item.opcode >= 0xa0 && item.opcode <= 0xae));
}

bool exclusive_predecessor(const AuthoritativePostLayoutControlFlow& flow,
                           std::size_t item, std::size_t predecessor) {
  for (const auto& entry : flow.external_entries)
    if (entry.entry.item_index == item) return false;
  bool reached = false;
  for (std::size_t state = 0; state < flow.execution_states.size(); ++state) {
    for (const std::size_t next : flow.execution_successors.at(state)) {
      if (flow.execution_states.at(next).item_index != item) continue;
      reached = true;
      if (flow.execution_states[state].item_index != predecessor) return false;
    }
  }
  return reached;
}

struct Argument {
  std::size_t producer;
  int reg;
  bool stored;
};

// Recall/swap pairs preserve the incoming X argument while preparing Y. A
// store can cross such a pair only if the recalled register is different.
// This is a bounded structural value proof, not a source-name match.
std::optional<Argument> argument_before_call(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow, std::size_t call_item) {
  std::size_t next = call_item;
  std::set<int> recalled;
  for (int pairs = 0; pairs <= 4; ++pairs) {
    const auto previous = previous_cell(items, next);
    if (!previous || items[*previous].raw ||
        items[*previous].manual_interaction ||
        !exclusive_predecessor(flow, next, *previous)) return std::nullopt;
    const auto& producer = items[*previous];
    if (direct_store(producer) || direct_recall(producer)) {
      const int reg = producer.opcode & 0x0f;
      if (recalled.contains(reg)) return std::nullopt;
      // Do not change the call's selector or overwrite its hardware writeback.
      if (items[call_item].opcode != 0x53 &&
          (items[call_item].opcode & 0x0f) == reg) return std::nullopt;
      return Argument{*previous, reg, direct_store(producer)};
    }
    if (producer.kind != MachineItemKind::Op || producer.opcode != 0x14)
      return std::nullopt;
    const auto recall = previous_cell(items, *previous);
    if (!recall || !direct_recall(items[*recall]) || items[*recall].raw ||
        items[*recall].manual_interaction ||
        !exclusive_predecessor(flow, *previous, *recall)) return std::nullopt;
    recalled.insert(items[*recall].opcode & 0x0f);
    next = *recall;
  }
  return std::nullopt;
}

std::optional<std::size_t> call_target(
    const std::vector<MachineItem>& items, const Index& index,
    const AuthoritativePostLayoutControlFlow& flow, std::size_t i) {
  if (items[i].opcode != 0x53) {
    const auto found = flow.indirect_flow_targets.find(i);
    if (found == flow.indirect_flow_targets.end() || found->second.size() != 1U)
      return std::nullopt;
    return found->second.front().item_index;
  }
  const auto operand = index.cells.find(index.addresses[i] + 1);
  if (operand == index.cells.end()) return std::nullopt;
  const auto* label = std::get_if<std::string>(&items[operand->second].target);
  if (!label || !index.labels.contains(*label)) return std::nullopt;
  const int address = index.addresses[index.labels.at(*label)];
  const auto target = index.cells.find(address);
  return target == index.cells.end() ? std::nullopt
                                     : std::optional{target->second};
}

bool closed_call_entry(const AuthoritativePostLayoutControlFlow& flow,
                       std::size_t target, const std::set<std::size_t>& calls) {
  for (const auto& entry : flow.external_entries)
    if (entry.entry.item_index == target) return false;
  bool reached = false;
  for (std::size_t state = 0; state < flow.execution_states.size(); ++state) {
    for (const std::size_t next : flow.execution_successors.at(state)) {
      if (flow.execution_states.at(next).item_index != target) continue;
      reached = true;
      if (!calls.contains(flow.execution_states[state].item_index)) return false;
    }
  }
  return reached;
}

} // namespace

PassResult helper_entry_store_factoring(const std::vector<IrOp>& ops,
                                       const PassContext& context) {
  PassResult unchanged{.ops = ops};
  const auto items = lower_ir_to_machine(ops);
  if (items.empty()) return unchanged;
  // This transformation precedes layout. Never move frozen numeric addresses,
  // encoded side-space entries, or decimal runtime selectors.
  for (const auto& item : items) {
    if (item.raw || item.formal_opcode ||
        (item.kind == MachineItemKind::Address &&
         !std::holds_alternative<std::string>(item.target))) return unchanged;
    if (item.indirect_flow_targets) {
      for (const auto& target : *item.indirect_flow_targets)
        if (!std::holds_alternative<std::string>(target)) return unchanged;
    }
    for (const auto& role : item.roles)
      if (role.find("late-decimal-selector") != std::string::npos) return unchanged;
  }
  PostLayoutControlFlowOptions options;
  options.empty_return_target = 1;
  const auto flow = build_post_layout_control_flow(items, options);
  if (!flow.proved) return unchanged;
  const auto index = index_items(items);
  std::map<std::size_t, std::set<std::size_t>> families;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (!call(items[i])) continue;
    const auto target = call_target(items, index, flow, i);
    if (target) families[*target].insert(i);
  }

  for (const auto& [root, calls] : families) {
    if (calls.size() < 2U || !closed_call_entry(flow, root, calls)) continue;
    std::optional<int> reg;
    std::set<std::size_t> stores;
    std::optional<std::string> logical_name;
    bool eligible = true;
    for (const auto caller : calls) {
      if (items[caller].manual_interaction) { eligible = false; break; }
      const auto argument = argument_before_call(items, flow, caller);
      if (!argument || (reg && *reg != argument->reg)) {
        eligible = false; break;
      }
      reg = argument->reg;
      if (argument->stored) {
        for (const auto& entry : flow.external_entries)
          if (entry.entry.item_index == argument->producer &&
              entry.kind != ExternalEntryKind::Main) eligible = false;
        const auto& name = items[argument->producer].logical_register_name;
        if (!stores.empty() && name != logical_name) eligible = false;
        logical_name = name;
        stores.insert(argument->producer);
      }
    }
    if (!eligible || stores.size() < 2U || stores.contains(root)) continue;

    // All visible stack values and registers agree after the entry store.
    // Demand independent convergence of both hidden registers rather than
    // assuming that store commutes with PP's hidden entry effects.
    StackValueEqualityState equality{{true, true, true, true}, false, false};
    if (!prove_post_layout_stack_entry_equality(
            items, flow, index.addresses[root], equality)) continue;

    auto entry_store = items[*stores.begin()];
    entry_store.comment = "shared helper argument store";
    std::vector<MachineItem> rewritten;
    rewritten.reserve(items.size() + 1U - stores.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
      if (i == root) rewritten.push_back(entry_store);
      if (!stores.contains(i)) rewritten.push_back(items[i]);
    }
    const auto final_flow = build_post_layout_control_flow(rewritten, options);
    if (!final_flow.proved) continue;
    // Entry labels precede the inserted store; removed stores leave their
    // symbolic labels on the same caller continuation. No numeric relocation.
    auto result_ops = raise_machine_to_ir(
        rewritten, effective_optimizer_feature_profile(context.options));
    const int saved = static_cast<int>(stores.size()) - 1;
    return PassResult{
        .ops = std::move(result_ops),
        .applied = 1,
        .optimizations = {{
            .name = "helper-entry-store-factoring",
            .detail = "Sank " + std::to_string(stores.size()) +
                      " argument stores into one closed helper entry; all " +
                      std::to_string(calls.size()) +
                      " calls prove X equals the argument, with no selector "
                      "alias and independent stack/X1/X2 convergence. Removed " +
                      std::to_string(saved) + " cells before layout.",
        }},
    };
  }
  return unchanged;
}

IrPass helper_entry_store_factoring_pass() {
  return IrPass{.name = "helper-entry-store-factoring",
                .run = helper_entry_store_factoring, .layout_safe = false};
}

} // namespace mkpro::core::passes
