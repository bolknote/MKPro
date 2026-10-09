#include "mkpro/core/passes/selector_charge_literal_sinking.hpp"

#include "mkpro/core/late_bound_decimal_selector.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/selector_writeback.hpp"
#include "mkpro/core/layout_search_artifact.hpp"

#include <algorithm>
#include <bit>
#include <map>
#include <optional>
#include <set>
#include <string_view>

namespace mkpro::core::passes {
namespace {

std::optional<std::string> role(const MachineItem& item, std::string_view prefix) {
  std::optional<std::string> result;
  for (const auto& value : item.roles) {
    if (!value.starts_with(prefix)) continue;
    if (result || value.size() == prefix.size()) return std::nullopt;
    result = value.substr(prefix.size());
  }
  return result;
}

std::size_t next(const std::vector<MachineItem>& items, std::size_t index) {
  while (++index < items.size() && items[index].kind == MachineItemKind::Label) {}
  return index;
}

std::optional<std::size_t> previous(const std::vector<MachineItem>& items,
                                     std::size_t index) {
  while (index > 0)
    if (items[--index].kind != MachineItemKind::Label) return index;
  return std::nullopt;
}

struct Index {
  std::map<std::string, std::size_t> labels;
  std::vector<int> addresses;
  int cells = 0;
};

Index index_of(const std::vector<MachineItem>& items) {
  Index result;
  for (std::size_t i = 0; i < items.size(); ++i) {
    result.addresses.push_back(result.cells);
    if (items[i].kind == MachineItemKind::Label)
      result.labels.emplace(items[i].name, next(items, i));
    else
      ++result.cells;
  }
  return result;
}

// Ordinary numeric operands become labels before any speculative movement.
// No formal/side-space operand is reinterpreted as a linear address.
std::optional<std::vector<MachineItem>> symbolize(const std::vector<MachineItem>& items) {
  std::map<int, std::size_t> commands;
  std::set<std::string> names;
  int address = 0;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const auto& item = items[i];
    if (item.raw || item.formal_opcode || item.indirect_flow_formal_targets)
      return std::nullopt;
    if (item.kind == MachineItemKind::Label) names.insert(item.name);
    else {
      if (item.kind == MachineItemKind::Op) commands.emplace(address, i);
      ++address;
    }
  }
  if (has_executable_address_words(items)) return std::nullopt;
  auto result = items;
  std::map<std::size_t, std::string> generated;
  std::size_t serial = 0;
  const auto convert = [&](IrTarget& target) {
    const auto* numeric = std::get_if<int>(&target);
    if (!numeric) return true;
    const auto command = commands.find(*numeric);
    if (command == commands.end()) return false;
    auto found = generated.find(command->second);
    if (found == generated.end()) {
      std::string name;
      do { name = "__selector_data_target_" + std::to_string(serial++); }
      while (names.contains(name));
      names.insert(name);
      found = generated.emplace(command->second, std::move(name)).first;
    }
    target = found->second;
    return true;
  };
  for (auto& item : result) {
    if (item.kind == MachineItemKind::Address && !convert(item.target)) return std::nullopt;
    if (item.indirect_flow_targets)
      for (auto& target : *item.indirect_flow_targets)
        if (!convert(target)) return std::nullopt;
  }
  std::vector<MachineItem> named;
  for (std::size_t i = 0; i < result.size(); ++i) {
    if (const auto found = generated.find(i); found != generated.end())
      named.push_back(MachineItem::label(found->second));
    named.push_back(std::move(result[i]));
  }
  return named;
}

struct Family {
  std::size_t store = 0;
  std::size_t source = 0;
  std::size_t leaf = 0;
  std::string identity;
  std::string literal;
  std::set<std::size_t> charges;
  std::set<std::size_t> digits;
};

std::optional<Family> family_at(const std::vector<MachineItem>& items,
                                const AuthoritativePostLayoutControlFlow& flow,
                                std::size_t store, bool verify_entry_bindings = true) {
  const auto id = role(items[store], "selector-sunk-literal-id:");
  const auto literal = role(items[store], "selector-sunk-literal-value:");
  if (!id || !literal || literal->empty() || literal->size() > 2 ||
      !std::all_of(literal->begin(), literal->end(), [](char c) { return c >= '0' && c <= '9'; }) ||
      literal->front() == '0' || items[store].kind != MachineItemKind::Op ||
      items[store].opcode < 0x47 || items[store].opcode > 0x4e ||
      std::find(items[store].roles.begin(), items[store].roles.end(),
                "selector-sunk-literal-coalesced") != items[store].roles.end())
    return std::nullopt;
  Family family{.store = store, .identity = *id, .literal = *literal};
  const int reg = items[store].opcode & 15;
  const std::string register_name(1, "0123456789abcde"[reg]);
  std::optional<std::size_t> source;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (role(items[i], "selector-sunk-literal-source:") == id) {
      if (source) return std::nullopt;
      source = i;
    }
    if (role(items[i], "late-decimal-selector-high:") &&
        role(items[i], "late-decimal-selector-register:") == register_name)
      family.charges.insert(i);
    if (items[i].kind != MachineItemKind::Op) continue;
    if (items[i].opcode == 0x60 + reg || (items[i].opcode == 0x40 + reg && i != store))
      return std::nullopt;
    const bool memory = (items[i].opcode >= 0xb0 && items[i].opcode <= 0xbe) ||
                        (items[i].opcode >= 0xd0 && items[i].opcode <= 0xde);
    if (memory) {
      const auto targets = flow.indirect_memory_targets.find(i);
      if ((items[i].opcode & 15) == reg || targets == flow.indirect_memory_targets.end() ||
          std::find(targets->second.begin(), targets->second.end(), reg) !=
              targets->second.end()) return std::nullopt;
    }
  }
  if (!source || !family.charges.contains(*source) || family.charges.size() < 2)
    return std::nullopt;
  family.source = *source;
  const auto leaf = role(items[*source], "late-decimal-selector-high:");
  const auto index = index_of(items);
  if (!leaf || !index.labels.contains(*leaf) || index.labels.at(*leaf) >= items.size())
    return std::nullopt;
  family.leaf = index.labels.at(*leaf);
  std::size_t digit = store;
  for (const char value : *literal) {
    digit = next(items, digit);
    if (digit >= items.size() || items[digit].kind != MachineItemKind::Op ||
        items[digit].opcode != value - '0' || items[digit].manual_interaction ||
        !items[digit].roles.empty()) return std::nullopt;
    family.digits.insert(digit);
  }
  std::set<std::size_t> actual_charges;
  for (const auto& entry : flow.external_entries)
    if (entry.entry.item_index == store) return std::nullopt;
  for (std::size_t state = 0; state < flow.execution_successors.size(); ++state) {
    for (const auto successor : flow.execution_successors[state]) {
      if (flow.execution_states[successor].item_index != store) continue;
      auto incoming = flow.execution_states[state].item_index;
      const int transfer = items[incoming].opcode;
      // Natural-target layout can shorten this same proved transfer to an
      // immutable indirect call/jump. Numeric charge provenance follows its
      // authoritative edge, not the original two-cell spelling.
      if (transfer == 0x51 || transfer == 0x53 ||
          (transfer >= 0x87 && transfer <= 0x8e) ||
          (transfer >= 0xa7 && transfer <= 0xae)) {
        const auto low = previous(items, incoming);
        if (!low) return std::nullopt;
        incoming = *low;
      }
      const auto high = previous(items, incoming);
      if (!high || next(items, *high) != incoming || !family.charges.contains(*high))
        return std::nullopt;
      actual_charges.insert(*high);
    }
  }
  if (actual_charges != family.charges) return std::nullopt;
  if (verify_entry_bindings) for (const auto high : family.charges) {
    const auto low = next(items, high);
    if (low >= items.size() || !prove_selector_charge_sunk_literal_entry(
        items, flow, store, high, items[high].opcode * 10 + items[low].opcode))
      return std::nullopt;
  }
  return family;
}

bool transport_is_exact(const AuthoritativePostLayoutControlFlow& before,
                        const AuthoritativePostLayoutControlFlow& after,
                        const std::vector<std::optional<std::size_t>>& mapping,
                        const std::vector<MachineItem>* original_items = nullptr,
                        int relocated_private_selector = -1,
                        const std::set<std::size_t>& rebound_companions = {}) {
  const auto mapped = [&](std::size_t item) {
    return item < mapping.size() ? mapping[item] : std::nullopt;
  };
  for (const auto& [source, targets] : before.indirect_flow_targets) {
    const auto output = mapped(source);
    if (!output) return false;
    const auto found = after.indirect_flow_targets.find(*output);
    if (found == after.indirect_flow_targets.end() || found->second.size() != targets.size())
      return false;
    for (const auto& target : targets)
      if (std::none_of(found->second.begin(), found->second.end(), [&](const auto& delivered) {
          return mapped(target.item_index) == std::optional<std::size_t>(delivered.item_index) &&
                 (target.address == delivered.address || rebound_companions.contains(source) ||
                  (original_items && relocated_private_selector >= 7 &&
                   ((*original_items)[source].opcode & 15) == relocated_private_selector &&
                   std::find((*original_items)[source].roles.begin(),
                             (*original_items)[source].roles.end(), "late-decimal-selector-consumer") !=
                       (*original_items)[source].roles.end()));
        })) return false;
  }
  for (const auto& [source, targets] : before.indirect_memory_targets) {
    const auto output = mapped(source);
    if (!output) return false;
    const auto found = after.indirect_memory_targets.find(*output);
    if (found == after.indirect_memory_targets.end() || found->second != targets) return false;
  }
  if (before.external_entries.size() != after.external_entries.size()) return false;
  for (const auto& entry : before.external_entries)
    if (std::none_of(after.external_entries.begin(), after.external_entries.end(),
        [&](const auto& output) {
          if (mapped(entry.entry.item_index) != std::optional<std::size_t>(output.entry.item_index) ||
              entry.kind != output.kind || entry.manual_interaction != output.manual_interaction ||
              entry.formal_opcode != output.formal_opcode ||
              entry.return_stack.size() != output.return_stack.size() ||
              entry.formal_return_stack != output.formal_return_stack) return false;
          for (std::size_t i = 0; i < entry.return_stack.size(); ++i)
            if (mapped(entry.return_stack[i].item_index) !=
                std::optional<std::size_t>(output.return_stack[i].item_index)) return false;
          return true;
        })) return false;
  if (before.empty_return_target.has_value() != after.empty_return_target.has_value()) return false;
  return !before.empty_return_target ||
      (before.empty_return_target->address == after.empty_return_target->address &&
       mapped(before.empty_return_target->item_index) ==
           std::optional<std::size_t>(after.empty_return_target->item_index));
}

SelectorChargeLiteralLayoutResult close_literal_layout(
    const std::vector<MachineItem>& placed, const std::vector<PreloadReport>& preloads,
    const PostLayoutControlFlowOptions& flow_options, const Family& proved_family,
    int input_cells) {
  SelectorChargeLiteralLayoutResult result;
  const auto named = symbolize(placed);
  if (!named) return result;
  const auto before = build_post_layout_control_flow(*named, flow_options);
  if (!before.proved) return result;
  std::optional<Family> family;
  for (std::size_t i = 0; i < named->size(); ++i)
    if (role((*named)[i], "selector-sunk-literal-id:") == proved_family.identity)
      family = family_at(*named, before, i, false);
  if (!family || family->literal != proved_family.literal) return result;
  std::vector<MachineItem> shortened;
  std::vector<std::optional<std::size_t>> mapping(named->size());
  for (std::size_t i = 0; i < named->size(); ++i) {
    if (family->digits.contains(i)) continue;
    mapping[i] = shortened.size();
    shortened.push_back((*named)[i]);
    if (i == family->store)
      shortened.back().roles.push_back("selector-sunk-literal-coalesced");
  }
  const auto bound = rebind_late_bound_decimal_selectors(shortened,
      {.minimum_target_address = 0, .maximum_target_address = 99});
  if (!bound.diagnostics.empty()) {
    result.reasons.push_back("final selector binding: " + bound.diagnostics.front().message);
    return result;
  }
  const auto after = build_post_layout_control_flow(bound.items, flow_options);
  const int reg = (*named)[family->store].opcode & 15;
  if (!after.proved) return result;
  auto delivered_preloads = preloads;
  const auto final_index = index_of(bound.items);
  std::set<std::size_t> rebound_companions;
  std::map<int, std::pair<int, int>> rebound_targets;
  for (const auto& [source, targets] : before.indirect_flow_targets) {
    const int selector = (*named)[source].opcode & 15;
    if (selector == reg) continue;
    for (const auto& target : targets) {
      if (target.item_index >= mapping.size() || !mapping[target.item_index]) return result;
      const int address = final_index.addresses[*mapping[target.item_index]];
      if (address == target.address) continue;
      if (targets.size() != 1 || selector < 7 || selector > 14) return result;
      const auto [previous_target, inserted] = rebound_targets.emplace(
          selector, std::pair{target.address, address});
      if (!inserted && previous_target->second != std::pair{target.address, address})
        return result;
      if (inserted) {
        const std::string name(1, "0123456789abcde"[selector]);
        std::optional<std::size_t> preload_index;
        for (std::size_t i = 0; i < preloads.size(); ++i) {
          if (register_from_text(preloads[i].register_name) != name) continue;
          if (preload_index) return result;
          preload_index = i;
        }
        if (!preload_index) return result;
        const auto value = rebind_stable_preloaded_indirect_flow_selector(
            *named, preloads[*preload_index], before, target.address, address,
            flow_options.address_space_model);
        if (!value) {
          result.reasons.push_back("literal erasure cannot rebind companion R" + name);
          return result;
        }
        delivered_preloads[*preload_index].value = *value;
      }
      rebound_companions.insert(source);
    }
  }
  if (!transport_is_exact(before, after, mapping, &*named, reg, rebound_companions)) {
    result.reasons.push_back("erasure moved a non-rebindable target or observation");
    return result;
  }
  // The input family was proved with its real literal before placement.
  // Intermediate selector digits may be unbound: only these final entry
  // contracts discharge both address delivery and literal/data equality.
  for (const auto high : family->charges) {
    if (!mapping[high] || !mapping[family->store]) return result;
    const auto low = next(bound.items, *mapping[high]);
    if (low >= bound.items.size() || !prove_selector_charge_sunk_literal_entry(
        bound.items, after, *mapping[family->store], *mapping[high],
        bound.items[*mapping[high]].opcode * 10 + bound.items[low].opcode)) {
      result.reasons.push_back("final literal/selector/stack entry contract failed");
      return result;
    }
  }
  result.items = bound.items;
  result.preloads = std::move(delivered_preloads);
  result.final_control_flow = after;
  result.applied = 1;
  result.removed_cells = input_cells - index_of(bound.items).cells;
  result.optimizations = {{
      .name = "selector-data-literal-coalescing",
      .detail = "Coalesced an ordinary literal with a private runtime selector after exact "
                "placement and erasure; all " + std::to_string(family->charges.size()) +
                " entries prove final address/data equality, stack/X1/X2 and return contexts. "
                "Complete layout delta: " + std::to_string(-result.removed_cells) + " cell(s).",
  }};
  return result;
}

// Erase first, but publish only after exact layout has discharged every debt.
// The provisional artifact is not executable output: its private selector may
// not yet equal the removed literal, and immutable companion selectors can
// temporarily point past their typed targets.
SelectorChargeLiteralLayoutResult layout_after_literal_erasure(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& before,
    const PostLayoutControlFlowOptions& flow_options, const Family& family,
    const NaturalTargetComponentLayoutOptions& layout_options) {
  SelectorChargeLiteralLayoutResult best;
  std::vector<MachineItem> erased;
  std::vector<std::optional<std::size_t>> mapping(items.size());
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (family.digits.contains(i)) continue;
    mapping[i] = erased.size();
    erased.push_back(items[i]);
    if (i == family.store)
      erased.back().roles.push_back("selector-sunk-literal-coalesced");
  }
  const auto rebound = rebind_late_bound_decimal_selectors(
      erased, {.minimum_target_address = 0, .maximum_target_address = 99});
  if (!rebound.diagnostics.empty()) return best;
  erased = rebound.items;
  const auto provisional = build_post_layout_control_flow(erased, flow_options);
  if (!provisional.proved) return best;
  const auto index = index_of(erased);
  const int reg = items[family.store].opcode & 15;
  const int data_address = std::stoi(family.literal);
  if (data_address > official_program_last_address(layout_options.address_space_model))
    return best;
  auto options = layout_options;
  options.maximum_subset_states = std::min<std::size_t>(options.maximum_subset_states, 512);
  options.maximum_anchor_combinations = 32;
  options.required_absolute_targets = {
      {.target_item = *mapping[family.leaf], .target_address = data_address}};
  std::map<std::size_t, int> fixed{{*mapping[family.leaf], data_address}};
  // Earlier edits in the same transaction may already owe exact addresses.
  // Remap obligations by command identity; never drop them or refresh a
  // growth allowance just because another edit removes cells.
  for (const auto& required : layout_options.required_absolute_targets) {
    if (required.target_item >= mapping.size() || !mapping[required.target_item]) return best;
    const auto output = *mapping[required.target_item];
    const auto [position, added] = fixed.emplace(output, required.target_address);
    if (!added && position->second != required.target_address) return best;
    if (added) options.required_absolute_targets.push_back({output, required.target_address});
  }
  options.deferred_selector_reconciliations.clear();
  for (const auto& debt : layout_options.deferred_selector_reconciliations) {
    if (debt.source_item >= mapping.size() || debt.target_item >= mapping.size() ||
        !mapping[debt.source_item] || !mapping[debt.target_item]) return best;
    options.deferred_selector_reconciliations.push_back({
        *mapping[debt.source_item], *mapping[debt.target_item], debt.final_target_address});
  }
  auto delivered_preloads = preloads;
  std::map<int, std::pair<int, int>> moved_targets;
  std::set<std::size_t> staged_companions;
  for (const auto& [source, targets] : before.indirect_flow_targets) {
    const int selector = items[source].opcode & 15;
    if (selector == reg) continue;
    if (std::any_of(layout_options.deferred_selector_reconciliations.begin(),
        layout_options.deferred_selector_reconciliations.end(),
        [&](const auto& debt) { return debt.source_item == source; })) {
      staged_companions.insert(source);
      continue;
    }
    for (const auto& target : targets) {
      if (target.item_index >= mapping.size() || !mapping[target.item_index])
        return best;
      const auto output = *mapping[target.item_index];
      const int temporary_address = index.addresses[output];
      if (temporary_address == target.address) continue;
      if (targets.size() != 1 || selector < 7 || selector > 14) return best;
      const auto [found, inserted] = moved_targets.emplace(
          selector, std::pair{target.address, temporary_address});
      if (!inserted && found->second != std::pair{target.address, temporary_address})
        return best;
      const std::string name(1, "0123456789abcde"[selector]);
      std::optional<std::size_t> preload_index;
      for (std::size_t i = 0; i < preloads.size(); ++i) {
        if (register_from_text(preloads[i].register_name) != name) continue;
        if (preload_index) return best;
        preload_index = i;
      }
      if (!preload_index) return best;
      const auto value = rebind_stable_preloaded_indirect_flow_selector(
          items, preloads[*preload_index], before, target.address,
          temporary_address, layout_options.address_space_model);
      if (value) {
        delivered_preloads[*preload_index].value = *value;
      } else {
        // Do not retune observable data. The final geometry must restore its
        // original command identity at the unchanged decoded address.
        const auto [position, added] = fixed.emplace(output, target.address);
        if (!added && position->second != target.address) return best;
        if (added) options.required_absolute_targets.push_back({
            .target_item = output, .target_address = target.address});
        options.deferred_selector_reconciliations.push_back({
            .source_item = *mapping[source], .target_item = output,
            .final_target_address = target.address});
      }
      staged_companions.insert(source);
    }
  }
  // This first proof transports identities, aliases, observations and return
  // frames. It does not discharge the deliberately staged address/data debts.
  if (!transport_is_exact(before, provisional, mapping, &items, reg, staged_companions))
    return best;
  const auto bounded = late_bound_decimal_selector_target_labels(erased);
  if (!bounded) return best;
  options.required_bounded_target_labels = *bounded;
  options.maximum_bounded_target_address = 99;
  options.allow_size_neutral_bounded_layout = true;
  options.constrain_rebindable_selector_targets = true;
  options.allow_size_neutral_absolute_layout = true;
  options.allow_size_neutral_flow_rebind = true;
  // Spend at most one less cell than the erasure earned. Even a bridge must
  // be repaid inside this transaction; the caller never receives a larger IR.
  options.maximum_transactional_growth_cells =
      static_cast<int>(family.digits.size()) - 1;
  const int input_cells = index_of(items).cells;
  for (const bool neutral : {false, true}) {
    options.require_size_neutral_absolute_layout = neutral;
    const auto placed = optimize_natural_target_component_layout(
        erased, delivered_preloads, provisional, options);
    if (placed.applied <= 0 || !placed.plan.proved ||
        !placed.plan.absolute_targets_proved ||
        !placed.plan.deferred_selector_reconciliations_proved ||
        !placed.plan.final_artifact_proved || !placed.plan.final_control_flow.proved) {
      if (!placed.plan.reasons.empty())
        best.reasons.push_back("erasure-aware layout: " + placed.plan.reasons.back());
      continue;
    }
    const int saved = input_cells - index_of(placed.items).cells;
    if (saved <= 0 || (best.applied > 0 && saved <= best.removed_cells)) continue;
    const auto delivered = rebind_late_bound_decimal_selectors(
        placed.items, {.minimum_target_address = 0, .maximum_target_address = 99});
    if (!delivered.diagnostics.empty()) continue;
    const auto final_flow = build_post_layout_control_flow(delivered.items, flow_options);
    std::vector<std::optional<std::size_t>> identity(placed.items.size());
    for (std::size_t i = 0; i < identity.size(); ++i) identity[i] = i;
    if (!final_flow.proved || !transport_is_exact(
        placed.plan.final_control_flow, final_flow, identity)) continue;
    const auto& final_items = delivered.items;
    std::optional<std::size_t> store;
    bool unique_store = true;
    std::set<std::size_t> charges;
    for (std::size_t i = 0; i < final_items.size(); ++i) {
      if (role(final_items[i], "selector-sunk-literal-id:") == family.identity) {
        if (store) unique_store = false;
        store = i;
      }
      if (role(final_items[i], "late-decimal-selector-high:") &&
          role(final_items[i], "late-decimal-selector-register:") ==
              std::string(1, "0123456789abcde"[reg]))
        charges.insert(i);
    }
    if (!unique_store || !store || charges.size() != family.charges.size()) continue;
    bool entries_proved = true;
    for (const auto high : charges) {
      const auto low = next(final_items, high);
      if (low >= final_items.size() || !prove_selector_charge_sunk_literal_entry(
          final_items, final_flow, *store, high,
          final_items[high].opcode * 10 + final_items[low].opcode)) {
        entries_proved = false;
        break;
      }
    }
    if (!entries_proved) {
      best.reasons.push_back("erasure-aware layout: final data/selector/stack contract failed");
      continue;
    }
    best.items = final_items;
    best.preloads = placed.preloads;
    best.final_control_flow = final_flow;
    best.applied = 1;
    best.removed_cells = saved;
    best.optimizations = {{
        .name = "selector-data-literal-coalescing",
        .detail = "Reused a private selector as ordinary data; independently proved all final "
                  "entries, stack/X1/X2 and return contexts."}, {
        .name = "selector-data-literal-erasure-layout",
        .detail = "Solved exact layout after removing " +
            std::to_string(family.digits.size()) +
            " literal cells, reconciling immutable companion targets without changing their data. "
            "Complete artifact: " + std::to_string(input_cells) + " -> " +
            std::to_string(input_cells - saved) + " cells."}};
  }
  return best;
}

struct ReleasedCallFamily {
  int reg = -1;
  std::set<std::size_t> calls;
};

// Collect whole immutable families, including ones which do not overlap the
// desired literal address. A permutation of selectors can remove a geometric
// obstruction without any individual family being an immediate blocker.
std::vector<ReleasedCallFamily> releasable_call_families(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& flow, AddressSpaceModel model) {
  std::vector<ReleasedCallFamily> result;
  for (int reg = 7; reg <= 14; ++reg) {
    const std::string name(1, "0123456789abcde"[reg]);
    const PreloadReport* preload = nullptr;
    bool valid = true;
    for (const auto& value : preloads) {
      if (register_from_text(value.register_name) != name) continue;
      if (preload) valid = false;
      preload = &value;
    }
    if (!valid || !preload || preload->setup_expression || preload->setup_target_name)
      continue;
    const auto decoded = evaluate_indirect_address(
        name, preload->value, IndirectOperationKind::Flow, model);
    if (!decoded || !decoded->actual_flow_target ||
        noncanonical_indirect_flow_entries(decoded)) continue;
    ReleasedCallFamily family{.reg = reg};
    std::optional<std::size_t> target;
    for (std::size_t i = 0; i < items.size(); ++i) {
      const auto& item = items[i];
      if (item.kind != MachineItemKind::Op) continue;
      if (item.raw || item.opcode == 0x40 + reg) { valid = false; break; }
      const bool memory = (item.opcode >= 0xb0 && item.opcode <= 0xbe) ||
                          (item.opcode >= 0xd0 && item.opcode <= 0xde);
      if (memory) {
        const auto aliases = flow.indirect_memory_targets.find(i);
        if ((item.opcode & 15) == reg || aliases == flow.indirect_memory_targets.end() ||
            std::find(aliases->second.begin(), aliases->second.end(), reg) !=
                aliases->second.end()) { valid = false; break; }
      }
      const auto targets = flow.indirect_flow_targets.find(i);
      if (targets == flow.indirect_flow_targets.end() || (item.opcode & 15) != reg)
        continue;
      const bool continuous_call = item.opcode == 0xa0 + reg &&
          item.manual_interaction &&
          item.manual_interaction->kind == ManualInteractionAnchorKind::ContinuousResume;
      if ((item.opcode != 0xa0 + reg && item.opcode != 0x80 + reg) ||
          (item.manual_interaction && !continuous_call) || targets->second.size() != 1 ||
          targets->second.front().address != *decoded->actual_flow_target ||
          (target && *target != targets->second.front().item_index)) {
        valid = false; break;
      }
      const auto& indirect = opcode_by_code(item.opcode);
      const auto& direct = opcode_by_code(item.opcode == 0xa0 + reg ? 0x53 : 0x51);
      if (indirect.stack_effect != direct.stack_effect ||
          indirect.x2_effect != direct.x2_effect ||
          indirect.conditional_x2_effect || direct.conditional_x2_effect ||
          (!indirect_writeback_preserves_literal_value(*decoded, preload->value) &&
           !selector_writeback_is_unobserved(items, flow, i, *preload, model))) {
        valid = false; break;
      }
      target = targets->second.front().item_index;
      family.calls.insert(i);
    }
    if (valid && !family.calls.empty()) result.push_back(std::move(family));
  }
  return result;
}

std::vector<SelectorChargeLiteralLayoutResult> joint_family_erasure_layout(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& before,
    const PostLayoutControlFlowOptions& flow_options,
    const NaturalTargetComponentLayoutOptions& layout_options) {
  std::vector<SelectorChargeLiteralLayoutResult> results;
  const auto groups = releasable_call_families(
      items, preloads, before, layout_options.address_space_model);
  if (groups.empty()) return results;
  const auto input_index = index_of(items);
  std::vector<Family> families;
  for (std::size_t i = 0; i < items.size(); ++i)
    if (auto family = family_at(items, before, i)) families.push_back(std::move(*family));
  if (families.empty()) return results;

  // Cardinality-first deterministic subsets, never a Cartesian product of
  // every pass's beam. At most three complete families/eight operands are
  // released per transaction; the incumbent search keeps its original budget.
  std::vector<unsigned> subsets;
  const unsigned end = 1U << static_cast<unsigned>(groups.size());
  for (unsigned mask = 1; mask < end; ++mask)
    if (std::popcount(mask) <= 3) subsets.push_back(mask);
  std::stable_sort(subsets.begin(), subsets.end(), [](unsigned left, unsigned right) {
    return std::pair{std::popcount(left), left} < std::pair{std::popcount(right), right};
  });
  int attempts = 0;
  for (unsigned mask : subsets) {
    std::set<std::size_t> calls;
    std::string names;
    for (std::size_t group = 0; group < groups.size(); ++group) {
      if ((mask & (1U << static_cast<unsigned>(group))) == 0) continue;
      calls.insert(groups[group].calls.begin(), groups[group].calls.end());
      if (!names.empty()) names += ',';
      names += "R" + std::string(1, "0123456789abcde"[groups[group].reg]);
    }
    if (calls.size() > 8) continue;
    std::vector<MachineItem> expanded;
    std::vector<std::optional<std::size_t>> mapping(items.size()), reverse;
    PostLayoutExecutionRelocationOptions transport;
    for (std::size_t i = 0; i < items.size(); ++i) {
      auto item = items[i];
      mapping[i] = expanded.size();
      reverse.push_back(i);
      if (calls.contains(i)) {
        transport.direct_to_indirect_flow_items.push_back(expanded.size());
        item.opcode = item.opcode >= 0xa0 ? 0x53 : 0x51;
        item.mnemonic = opcode_by_code(item.opcode).name;
        item.indirect_flow_targets.reset();
      }
      expanded.push_back(std::move(item));
      if (calls.contains(i)) {
        expanded.push_back(MachineItem::address(items[i].indirect_flow_targets->front()));
        reverse.push_back(std::nullopt);
      }
    }
    const auto index = index_of(expanded);
    if (before.empty_return_target &&
        index.addresses[*mapping[before.empty_return_target->item_index]] !=
            before.empty_return_target->address) continue;
    const auto flow = build_post_layout_control_flow(expanded, flow_options);
    if (!flow.proved || !prove_post_layout_execution_relocation(
        expanded, items, flow, before, reverse, transport).proved) continue;
    auto options = layout_options;
    auto delivered_preloads = preloads;
    std::map<std::size_t, int> fixed;
    bool complete = true;
    for (const auto& [source, targets] : before.indirect_flow_targets) {
      if (calls.contains(source)) continue;
      const auto& item = items[source];
      if (std::any_of(item.roles.begin(), item.roles.end(), [](const auto& value) {
          return value == "late-decimal-selector-consumer" ||
                 value.starts_with("runtime-charged-selector");
      })) continue;
      if (targets.size() != 1) { complete = false; break; }
      const auto& target = targets.front();
      const auto output = *mapping[target.item_index];
      const int temporary_address = index.addresses[output];
      if (temporary_address == target.address) continue;
      const std::string name(1, "0123456789abcde"[item.opcode & 15]);
      const auto original = std::find_if(preloads.begin(), preloads.end(),
          [&](const auto& value) { return register_from_text(value.register_name) == name; });
      auto delivered = std::find_if(delivered_preloads.begin(), delivered_preloads.end(),
          [&](const auto& value) { return register_from_text(value.register_name) == name; });
      if (original == preloads.end() || delivered == delivered_preloads.end()) {
        complete = false; break;
      }
      const auto rebound = rebind_stable_preloaded_indirect_flow_selector(
          items, *original, before, target.address, temporary_address,
          layout_options.address_space_model);
      if (rebound) { delivered->value = *rebound; continue; }
      const auto [position, added] = fixed.emplace(output, target.address);
      if (!added && position->second != target.address) { complete = false; break; }
      if (added) options.required_absolute_targets.push_back({output, target.address});
      options.deferred_selector_reconciliations.push_back({
          *mapping[source], output, target.address});
    }
    if (!complete) continue;
    options.allow_standalone_fallthrough_jump_fold = true;
    for (const auto& original_family : families) {
      if (++attempts > 12) return results;
      const auto family = family_at(expanded, flow, *mapping[original_family.store], false);
      if (!family || family->identity != original_family.identity ||
          family->literal != original_family.literal ||
          family->charges.size() != original_family.charges.size()) continue;
      auto closed = layout_after_literal_erasure(
          expanded, delivered_preloads, flow, flow_options, *family, options);
      if (!closed.applied) continue;
      const int saved = input_index.cells - index_of(closed.items).cells;
      // This is an enabling edge, not a published optimization. Existing
      // complete-artifact search must repay the entire (at most two-cell) debt.
      if (saved < -2) continue;
      closed.removed_cells = saved;
      closed.optimizations.insert(closed.optimizations.begin(), {
          .name = "joint-selector-allocation-and-erasure",
          .detail = "Solved " + std::to_string(std::popcount(mask)) +
              " complete call families (" + names + "), " +
              std::to_string(calls.size()) +
              " temporary direct operands and literal erasure in one placement; "
              "verified typed entries, stack/X1/X2, aliases and exact return contexts. "
              "Complete artifact: " + std::to_string(input_index.cells) + " -> " +
              std::to_string(input_index.cells - saved) +
              " cells. Any remaining debt must be repaid before selection."});
      const LayoutSearchArtifact candidate{closed.items, closed.preloads, {}};
      if (std::none_of(results.begin(), results.end(), [&](const auto& previous) {
          return same_layout_search_artifact(candidate,
              LayoutSearchArtifact{previous.items, previous.preloads, {}});
      })) results.push_back(std::move(closed));
    }
  }
  return results;
}

} // namespace

SelectorChargeLiteralLayoutResult optimize_selector_data_literal_coalescing(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control,
    const NaturalTargetComponentLayoutOptions& layout_options) {
  SelectorChargeLiteralLayoutResult best{.items = items, .preloads = preloads};
  if (!control.proved || !layout_options.required_absolute_targets.empty()) return best;
  const auto symbolic = symbolize(items);
  if (!symbolic) return best;
  PostLayoutControlFlowOptions flow_options;
  flow_options.address_space_model = layout_options.address_space_model;
  if (control.empty_return_target) flow_options.empty_return_target = control.empty_return_target->address;
  const auto flow = build_post_layout_control_flow(*symbolic, flow_options);
  if (!flow.proved) return best;
  const int input_cells = index_of(items).cells;
  int attempted = 0;
  for (std::size_t store = 0; store < symbolic->size(); ++store) {
    const auto family = family_at(*symbolic, flow, store);
    if (!family) continue;
    if (++attempted > 8) break;
    for (const int erased_prefix : {0, static_cast<int>(family->digits.size())}) {
    const int target_address = std::stoi(family->literal) + erased_prefix;
    if (target_address > official_program_last_address(layout_options.address_space_model)) continue;
    for (const bool neutral_placement : {false, true}) {
    auto options = layout_options;
    options.maximum_subset_states = std::min<std::size_t>(options.maximum_subset_states, 512);
    options.required_absolute_targets = {{.target_item = family->leaf, .target_address = target_address}};
    options.allow_size_neutral_absolute_layout = true;
    options.require_size_neutral_absolute_layout = neutral_placement;
    options.allow_size_neutral_flow_rebind = true;
    options.maximum_transactional_growth_cells = static_cast<int>(family->digits.size());
    const auto targets = late_bound_decimal_selector_target_labels(*symbolic);
    if (!targets) continue;
    options.required_bounded_target_labels.assign(targets->begin(), targets->end());
    options.maximum_bounded_target_address = 99 + erased_prefix;
    options.allow_size_neutral_bounded_layout = true;
    options.constrain_rebindable_selector_targets = true;

    auto placed_items = *symbolic;
    auto placed_preloads = preloads;
    auto placed_flow = flow;
    if (index_of(*symbolic).addresses[family->leaf] != target_address) {
      const auto placed = optimize_natural_target_component_layout(*symbolic, preloads, flow, options);
      if (placed.applied <= 0 || !placed.plan.proved || !placed.plan.final_artifact_proved ||
          !placed.plan.absolute_targets_proved || !placed.plan.final_control_flow.proved) {
        if (best.reasons.size() < 8)
          best.reasons.push_back("no proved layout at data value " + family->literal +
              (placed.plan.reasons.empty() ? "" : ": " + placed.plan.reasons.front()));
        continue;
      }
      placed_items = placed.items;
      placed_preloads = placed.preloads;
      placed_flow = build_post_layout_control_flow(placed_items, flow_options);
    }
    auto closed = close_literal_layout(placed_items, placed_preloads, flow_options, *family, input_cells);
    if (closed.applied > 0 && closed.removed_cells > 0 &&
        (best.applied == 0 || closed.removed_cells > best.removed_cells))
      best = std::move(closed);
    else if (!closed.reasons.empty() && best.reasons.size() < 8)
      best.reasons.push_back(closed.reasons.front());
    }
    }
    if (best.applied == 0 || best.removed_cells < static_cast<int>(family->digits.size())) {
      auto erased = layout_after_literal_erasure(
          *symbolic, preloads, flow, flow_options, *family, layout_options);
      if (erased.applied > 0 &&
          (best.applied == 0 || erased.removed_cells > best.removed_cells))
        best = std::move(erased);
      else for (const auto& reason : erased.reasons)
        if (best.reasons.size() < 8) best.reasons.push_back(reason);
    }
  }
  return best;
}

SelectorDataLiteralLayoutFrontier selector_data_literal_layout_frontier(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control,
    const NaturalTargetComponentLayoutOptions& layout_options) {
  SelectorDataLiteralLayoutFrontier frontier;
  const auto ordinary = optimize_selector_data_literal_coalescing(
      items, preloads, control, layout_options);
  if (ordinary.applied > 0) frontier.alternatives.push_back(ordinary);
  if (!control.proved || !layout_options.required_absolute_targets.empty() ||
      !layout_options.deferred_selector_reconciliations.empty()) return frontier;
  const auto symbolic = symbolize(items);
  if (!symbolic) return frontier;
  PostLayoutControlFlowOptions flow_options;
  flow_options.address_space_model = layout_options.address_space_model;
  if (control.empty_return_target)
    flow_options.empty_return_target = control.empty_return_target->address;
  const auto before = build_post_layout_control_flow(*symbolic, flow_options);
  if (!before.proved) return frontier;
  const auto original_index = index_of(*symbolic);
  const auto reject = [&](const std::string& reason) {
    if (frontier.reasons.size() < 16) frontier.reasons.push_back(reason);
  };
  // A placement conflict concerns fallthrough-closed intervals, not labels
  // or source procedures. Conservatively stop at the first control transfer.
  const auto straight_width = [&](std::size_t start) {
    int width = 0;
    for (std::size_t i = start; i < symbolic->size(); ++i) {
      if ((*symbolic)[i].kind == MachineItemKind::Label) continue;
      ++width;
      const auto ir = raise_machine_to_ir({(*symbolic)[i]});
      if (ir.empty()) break;
      const auto kind = ir.front().kind;
      if (kind != IrKind::Plain && kind != IrKind::Store && kind != IrKind::Recall &&
          kind != IrKind::IndirectStore && kind != IrKind::IndirectRecall) break;
    }
    return width;
  };
  int attempts = 0;
  for (std::size_t store = 0; store < symbolic->size(); ++store) {
    const auto family = family_at(*symbolic, before, store);
    if (!family) continue;
    const int data_address = std::stoi(family->literal);
    const int data_end = data_address + straight_width(family->leaf);
    for (int reg = 7; reg <= 14; ++reg) {
      const std::string name(1, "0123456789abcde"[reg]);
      const PreloadReport* preload = nullptr;
      bool unique = true;
      for (const auto& value : preloads) {
        if (register_from_text(value.register_name) != name) continue;
        if (preload) unique = false;
        preload = &value;
      }
      if (!unique || !preload || preload->setup_expression || preload->setup_target_name)
        continue;
      const auto evaluation = evaluate_indirect_address(name, preload->value,
          IndirectOperationKind::Flow, layout_options.address_space_model);
      if (!evaluation || !evaluation->actual_flow_target ||
          noncanonical_indirect_flow_entries(evaluation).has_value()) continue;
      std::set<std::size_t> calls;
      std::optional<PostLayoutCommandIdentity> target;
      bool complete = true;
      for (std::size_t i = 0; i < symbolic->size(); ++i) {
        const auto& item = (*symbolic)[i];
        if (item.kind != MachineItemKind::Op) continue;
        if (item.opcode == 0x40 + reg) { complete = false; break; }
        const auto memory = before.indirect_memory_targets.find(i);
        if (memory != before.indirect_memory_targets.end() &&
            ((item.opcode & 15) == reg ||
             ((item.opcode & 0xf0) == 0xb0 &&
              std::find(memory->second.begin(), memory->second.end(), reg) != memory->second.end()))) {
          complete = false; break;
        }
        const auto flow = before.indirect_flow_targets.find(i);
        if (flow == before.indirect_flow_targets.end() || (item.opcode & 15) != reg) continue;
        if ((item.opcode != 0xa0 + reg && item.opcode != 0x80 + reg) ||
            item.manual_interaction || flow->second.size() != 1 ||
            flow->second.front().address != *evaluation->actual_flow_target ||
            (target && target->item_index != flow->second.front().item_index)) {
          complete = false; break;
        }
        const auto& indirect = opcode_by_code(item.opcode);
        const auto& direct = opcode_by_code(item.opcode == 0xa0 + reg ? 0x53 : 0x51);
        if (indirect.stack_effect != direct.stack_effect || indirect.x2_effect != direct.x2_effect ||
            indirect.conditional_x2_effect || direct.conditional_x2_effect ||
            (!indirect_writeback_preserves_literal_value(*evaluation, preload->value) &&
             !selector_writeback_is_unobserved(*symbolic, before, i, *preload,
                                               layout_options.address_space_model))) {
          complete = false; break;
        }
        target = flow->second.front();
        calls.insert(i);
      }
      if (!complete || !target || calls.empty() || calls.size() > family->digits.size()) continue;
      // Release only an actual geometric blocker. This bounds the frontier
      // without depending on a source function, literal, or program identity.
      if (target->item_index == family->leaf || target->address >= data_end ||
          target->address + straight_width(target->item_index) <= data_address) continue;
      if (++attempts > 8) return frontier;

      std::vector<MachineItem> expanded;
      std::vector<std::optional<std::size_t>> forward(symbolic->size()), reverse;
      PostLayoutExecutionRelocationOptions transport;
      const auto referenced_label = [&](const std::string& label) {
        for (const auto& item : *symbolic) {
          if (item.kind == MachineItemKind::Address && item.target == IrTarget{label}) return true;
          if (item.indirect_flow_targets && std::find(item.indirect_flow_targets->begin(),
              item.indirect_flow_targets->end(), IrTarget{label}) != item.indirect_flow_targets->end()) return true;
          for (const auto& value : item.roles)
            if (value.find(label) != std::string::npos) return true;
        }
        return false;
      };
      for (std::size_t i = 0; i < symbolic->size(); ++i) {
        auto item = (*symbolic)[i];
        if (item.kind == MachineItemKind::Label && !item.procedure_boundary && !item.procedure_name &&
            !item.manual_interaction && item.roles.empty() && !referenced_label(item.name)) {
          const auto command = original_index.labels.at(item.name);
          if (calls.contains(command) && (*symbolic)[command].opcode == 0x80 + reg &&
              std::none_of(before.external_entries.begin(), before.external_entries.end(),
                  [&](const auto& entry) { return entry.entry.item_index == command; }))
            continue;
        }
        forward[i] = expanded.size();
        reverse.push_back(i);
        if (calls.contains(i)) {
          transport.direct_to_indirect_flow_items.push_back(expanded.size());
          item.opcode = item.opcode == 0xa0 + reg ? 0x53 : 0x51;
          item.mnemonic = opcode_by_code(item.opcode).name;
          item.indirect_flow_targets.reset();
        }
        expanded.push_back(std::move(item));
        if (calls.contains(i)) {
          expanded.push_back(MachineItem::address((*symbolic)[i].indirect_flow_targets->front()));
          reverse.push_back(std::nullopt);
        }
      }
      const auto expanded_index = index_of(expanded);
      if (before.empty_return_target &&
          expanded_index.addresses[*forward[before.empty_return_target->item_index]] !=
              before.empty_return_target->address) continue;
      const auto expanded_flow = build_post_layout_control_flow(expanded, flow_options);
      // The existing direct-to-indirect proof is relational: use it in
      // reverse to prove added operands, branch labels and every return frame.
      const auto transported = prove_post_layout_execution_relocation(
          expanded, *symbolic, expanded_flow, before, reverse, transport);
      if (!transported.proved) {
        reject("call release transport: " + (transported.reasons.empty() ?
            std::string("unproved") : transported.reasons.front()));
        continue;
      }
      auto options = layout_options;
      auto candidate_preloads = preloads;
      options.maximum_subset_states = std::min<std::size_t>(options.maximum_subset_states, 512);
      options.maximum_anchor_combinations = 32;
      options.required_absolute_targets = {{.target_item = *forward[family->leaf],
                                           .target_address = data_address}};
      std::map<std::size_t, int> fixed{{*forward[family->leaf], data_address}};
      // Expanded operands can temporarily shift unrelated selectors. Keep
      // those exact old addresses as explicit obligations of this transaction;
      // no invalid intermediate preload is ever delivered to the user.
      for (const auto& [source, targets] : before.indirect_flow_targets) {
        if (calls.contains(source)) continue;
        const auto& item = (*symbolic)[source];
        const bool runtime = std::any_of(item.roles.begin(), item.roles.end(), [](const auto& value) {
          return value == "late-decimal-selector-consumer" ||
                 value.starts_with("runtime-charged-selector");
        });
        if (runtime) continue;
        if (targets.size() != 1) { complete = false; break; }
        const auto output_target = *forward[targets.front().item_index];
        const int temporary_address = expanded_index.addresses[output_target];
        if (temporary_address == targets.front().address) continue;
        const std::string selector(1, "0123456789abcde"[item.opcode & 15]);
        auto value = std::find_if(candidate_preloads.begin(), candidate_preloads.end(),
            [&](const auto& report) { return register_from_text(report.register_name) == selector; });
        if (value != candidate_preloads.end()) {
          const auto original_value = std::find_if(preloads.begin(), preloads.end(),
              [&](const auto& report) { return register_from_text(report.register_name) == selector; });
          const auto rebound = rebind_stable_preloaded_indirect_flow_selector(
              *symbolic, *original_value, before, targets.front().address, temporary_address,
              layout_options.address_space_model);
          if (rebound) { value->value = *rebound; continue; }
        }
        const auto [position, inserted] = fixed.emplace(output_target, targets.front().address);
        if (!inserted && position->second != targets.front().address) { complete = false; break; }
        if (inserted) options.required_absolute_targets.push_back({
            .target_item = output_target, .target_address = targets.front().address});
        options.deferred_selector_reconciliations.push_back({
            .source_item = *forward[source], .target_item = output_target,
            .final_target_address = targets.front().address});
      }
      if (!complete) continue;
      const auto bounded = late_bound_decimal_selector_target_labels(expanded);
      if (!bounded) continue;
      options.required_bounded_target_labels = *bounded;
      // Fractional address tails have the same two-digit delivery limit as
      // runtime charges. Feed it to geometry, not just the rejecting binder.
      for (const auto& [source, targets] : expanded_flow.indirect_flow_targets) {
        const std::string selector(1, "0123456789abcde"[expanded[source].opcode & 15]);
        const bool fractional = std::any_of(candidate_preloads.begin(), candidate_preloads.end(),
            [&](const auto& report) { return report.retunable_natural_fractional_prefix &&
                register_from_text(report.register_name) == selector; });
        if (!fractional) continue;
        for (const auto& destination : targets) {
          if (destination.labels.empty()) { complete = false; break; }
          options.required_bounded_target_labels.push_back(destination.labels.front());
        }
      }
      if (!complete) continue;
      std::sort(options.required_bounded_target_labels.begin(), options.required_bounded_target_labels.end());
      options.required_bounded_target_labels.erase(std::unique(options.required_bounded_target_labels.begin(),
          options.required_bounded_target_labels.end()), options.required_bounded_target_labels.end());
      options.maximum_bounded_target_address = 99;
      options.allow_size_neutral_bounded_layout = true;
      options.allow_size_neutral_absolute_layout = true;
      options.require_size_neutral_absolute_layout = false;
      options.allow_size_neutral_flow_rebind = true;
      // One separately proved fallthrough stitch can repay its command and
      // operand. Let the exact layout see that small debt before ranking the
      // fully closed artifact, rather than requiring every prefix to win.
      const int bridge_credit = std::any_of(expanded.begin(), expanded.end(), [](const auto& item) {
        return item.kind == MachineItemKind::Op && item.opcode == 0x51;
      }) ? 2 : 0;
      options.allow_standalone_fallthrough_jump_fold = bridge_credit > 0;
      options.maximum_transactional_growth_cells =
          static_cast<int>(family->digits.size() - calls.size()) + bridge_credit;
      for (const int erased_prefix : {0, static_cast<int>(family->digits.size())}) {
      if (data_address + erased_prefix > official_program_last_address(layout_options.address_space_model)) continue;
      options.required_absolute_targets.front().target_address = data_address + erased_prefix;
      options.maximum_bounded_target_address = 99 + erased_prefix;
      for (const bool neutral_placement : {false, true}) {
      options.require_size_neutral_absolute_layout = neutral_placement;
      const auto placed = optimize_natural_target_component_layout(
          expanded, candidate_preloads, expanded_flow, options);
      if (placed.applied <= 0 || !placed.plan.proved || !placed.plan.final_artifact_proved ||
          !placed.plan.absolute_targets_proved || !placed.plan.deferred_selector_reconciliations_proved) {
        reject("call release layout R" + name + ": " + (placed.plan.reasons.empty() ?
            std::string("unproved") : placed.plan.reasons.back()));
        continue;
      }
      auto coalesced = close_literal_layout(placed.items, placed.preloads, flow_options,
                                            *family, original_index.cells);
      if (coalesced.applied <= 0) {
        reject("call release coalescing R" + name + ": " + (coalesced.reasons.empty() ?
            std::string("unproved") : coalesced.reasons.front()));
        continue;
      }
      const int saved = original_index.cells - index_of(coalesced.items).cells;
      if (saved < -bridge_credit) continue;
      coalesced.removed_cells = saved;
      coalesced.optimizations.insert(coalesced.optimizations.begin(), {
          .name = "selector-anchor-release-transaction",
          .detail = "Released immutable call selector R" + name + " at " +
              std::to_string(target->address) + ", expanding " + std::to_string(calls.size()) +
              " transfer(s); exact placement and data/address reuse preserve all proof obligations. "
              "Intermediate size delta: " + std::to_string(-saved) +
              " cell(s); only a smaller fully repaid artifact may be selected."});
      frontier.alternatives.push_back(std::move(coalesced));
      if (frontier.alternatives.size() >= 8) return frontier;
      }
      }
    }
  }
  return frontier;
}

SelectorDataLiteralLayoutFrontier joint_selector_allocation_layout_frontier(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control,
    const NaturalTargetComponentLayoutOptions& layout_options) {
  SelectorDataLiteralLayoutFrontier frontier;
  if (!control.proved || !layout_options.required_absolute_targets.empty() ||
      !layout_options.deferred_selector_reconciliations.empty()) return frontier;
  const auto symbolic = symbolize(items);
  if (!symbolic) return frontier;
  PostLayoutControlFlowOptions flow_options;
  flow_options.address_space_model = layout_options.address_space_model;
  if (control.empty_return_target)
    flow_options.empty_return_target = control.empty_return_target->address;
  const auto before = build_post_layout_control_flow(*symbolic, flow_options);
  if (!before.proved) return frontier;
  auto options = layout_options;
  options.reallocate_released_address_only_selectors = true;
  auto joint = joint_family_erasure_layout(
      *symbolic, preloads, before, flow_options, options);
  std::stable_sort(joint.begin(), joint.end(), [](const auto& left, const auto& right) {
    return left.removed_cells > right.removed_cells;
  });
  if (joint.size() > 4) joint.resize(4);
  frontier.alternatives = std::move(joint);
  return frontier;
}

} // namespace mkpro::core::passes
