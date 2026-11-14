#pragma once

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/late_bound_decimal_selector.hpp"
#include "mkpro/core/result.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::core::detail {

using JointOriginMap = std::map<std::string, int>;

inline std::optional<int> joint_origin_register(std::string_view name) {
  if (name.size() != 1U) return std::nullopt;
  char digit = name.front();
  if (digit >= 'A' && digit <= 'F') digit += 'a' - 'A';
  const auto position = std::string_view{"0123456789abcdef"}.find(digit);
  if (position == std::string_view::npos) return std::nullopt;
  return static_cast<int>(position);
}

inline bool joint_charged_flow(const MachineItem& item) {
  return std::find(item.roles.begin(), item.roles.end(),
                   "late-decimal-selector-consumer") != item.roles.end();
}

inline bool joint_stable_flow_operand(const MachineItem& item) {
  const int family = item.opcode & 0xf0;
  return item.kind == MachineItemKind::Op &&
      (family == 0x70 || family == 0x80 || family == 0x90 ||
       family == 0xa0 || family == 0xc0 || family == 0xe0) &&
      (item.opcode & 15) >= 7 && (item.opcode & 15) <= 14;
}

inline std::optional<std::map<int, int>> joint_canonical_origins(
    const JointOriginMap& origins) {
  std::map<int, int> result;
  for (const auto& [name, origin] : origins) {
    const auto reg = joint_origin_register(name);
    if (!reg || *reg < 7 || *reg > 14 || origin < 0 ||
        !result.emplace(*reg, origin).second)
      return std::nullopt;
  }
  return result;
}

// Transport recorded word origins, not speculative command positions.
// This is bookkeeping for a separately proved semantic edge, not a proof.
inline std::optional<JointOriginMap> joint_target_origins(
    const std::vector<MachineItem>& items, const JointOriginMap& bindings,
    const JointOriginMap& encoded_selectors) {
  const auto encoded = joint_canonical_origins(encoded_selectors);
  if (!encoded) return std::nullopt;
  JointOriginMap overrides;
  for (const auto& item : items) {
    if (!item.indirect_flow_targets || joint_charged_flow(item)) continue;
    const int reg = item.opcode & 15;
    if (!encoded->contains(reg)) continue;
    if (!joint_stable_flow_operand(item) || item.indirect_flow_targets->empty())
      return std::nullopt;
    for (const auto& target : *item.indirect_flow_targets) {
      const auto* label = std::get_if<std::string>(&target);
      if (!label || !bindings.contains(*label)) return std::nullopt;
      const auto [previous, inserted] = overrides.emplace(*label, encoded->at(reg));
      if (!inserted && previous->second != encoded->at(reg)) return std::nullopt;
    }
  }
  auto result = bindings;
  for (const auto& [label, origin] : overrides) result[label] = origin;
  return result;
}

inline std::optional<std::map<int, const PreloadReport*>> joint_preload_index(
    const std::vector<PreloadReport>& preloads) {
  std::map<int, const PreloadReport*> result;
  for (const auto& preload : preloads) {
    const auto reg = joint_origin_register(preload.register_name);
    if (!reg || !result.emplace(*reg, &preload).second) return std::nullopt;
  }
  return result;
}

inline bool joint_literal_word(const PreloadReport& word) {
  return !word.counts_against_program && !word.setup_target_name &&
      !word.setup_expression && !word.setup_expression_text;
}

// A representation proof can change the literal word after family allocation.
// Re-decode only that proved word. Bindings must independently require its
// new address; generating an address is never permission to change data.
inline std::optional<JointOriginMap> transport_joint_selector_word_origins(
    const std::vector<MachineItem>& items, const JointOriginMap& bindings,
    const std::vector<PreloadReport>& before, const std::vector<PreloadReport>& after,
    const JointOriginMap& encoded_selectors, AddressSpaceModel model) {
  const auto old_words = joint_preload_index(before);
  const auto new_words = joint_preload_index(after);
  const auto encoded = joint_canonical_origins(encoded_selectors);
  if (!old_words || !new_words || !encoded) return std::nullopt;
  std::map<int, std::vector<std::string>> uses;
  for (const auto& item : items) {
    if (!item.indirect_flow_targets || joint_charged_flow(item)) continue;
    const int reg = item.opcode & 15;
    if (!encoded->contains(reg)) continue;
    if (!joint_stable_flow_operand(item) || item.indirect_flow_targets->empty())
      return std::nullopt;
    for (const auto& target : *item.indirect_flow_targets) {
      const auto* label = std::get_if<std::string>(&target);
      if (!label || !bindings.contains(*label)) return std::nullopt;
      uses[reg].push_back(*label);
    }
  }
  JointOriginMap result;
  for (const auto& [reg, labels] : uses) {
    if (!old_words->contains(reg) || !new_words->contains(reg)) return std::nullopt;
    const auto& old_word = *old_words->at(reg);
    const auto& new_word = *new_words->at(reg);
    if (!joint_literal_word(old_word) || !joint_literal_word(new_word))
      return std::nullopt;
    const std::string name(1, "0123456789abcdef"[reg]);
    try {
      const auto old = evaluate_indirect_address(name, old_word.value,
                                                 IndirectOperationKind::Flow, model);
      if (!old || !old->actual_flow_target || !old->formal_address ||
          old->formal_address->kind != FormalAddressKind::Official ||
          *old->actual_flow_target != encoded->at(reg))
        return std::nullopt;
      int origin = encoded->at(reg);
      if (old_word.value != new_word.value) {
        const auto now = evaluate_indirect_address(name, new_word.value,
                                                   IndirectOperationKind::Flow, model);
        if (!now || !now->actual_flow_target || !now->formal_address ||
            now->formal_address->kind != FormalAddressKind::Official)
          return std::nullopt;
        origin = *now->actual_flow_target;
        for (const auto& label : labels)
          if (bindings.at(label) != origin) return std::nullopt;
      }
      result.emplace(name, origin);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }
  // Released, unused families carry no runtime-origin equation.
  return result;
}



// A selector-only memory difference is handled by the carrier observer guard.
// This independent relation proves that entering its changed literal cannot
// escape through stack/X1/X2. The store is a ROM-certified entry closer; control
// and return targets remain obligations of the final relocation/binder proof.

// Together these facts describe all charge continuations of one already
// selected program, before choosing an order for its components.
class JointChargeContinuations {
 public:
  JointChargeContinuations(const std::vector<MachineItem>& items, PostLayoutExecutionView view)
      : items_(items), view_(view) {}

  bool stack_is_private(const LateBoundDecimalSelectorProof& charge,
                        const std::map<int, std::set<std::string>>& families,
                        bool precise_entry_facts = false) {
    if (!prepared_) prepare();
    if (!valid_ || !commands_.contains(charge.target_label) ||
        charge.low_item_index >= rows_by_item_.size()) return false;
    return prove(charge, families, precise_entry_facts);
  }

 private:
  void prepare() {
    prepared_ = true;
    if (!view_.proved() || view_.size() != view_.successors().size()) return;
    rows_by_item_.resize(items_.size());
    std::size_t first_label = 0;
    for (std::size_t i = 0; i < items_.size(); ++i) {
      if (items_[i].kind == MachineItemKind::Label) continue;
      for (std::size_t label = first_label; label < i; ++label)
        if (!commands_.emplace(items_[label].name, i).second) return;
      first_label = i + 1;
    }
    if (first_label != items_.size()) return;
    for (std::size_t s = 0; s < view_.size(); ++s) {
      const auto item = view_.state(s).item_index;
      if (item >= items_.size()) return;
      rows_by_item_[item].push_back(s);
    }
    valid_ = true;
  }

  bool prove(const LateBoundDecimalSelectorProof& charge,
             const std::map<int, std::set<std::string>>& families, bool precise_entry_facts);

  const std::vector<MachineItem>& items_;
  PostLayoutExecutionView view_;
  std::map<std::string_view, std::size_t> commands_;
  PostLayoutSuccessors rows_by_item_;
  std::map<std::pair<bool, std::vector<std::size_t>>, bool> continued_;
  bool prepared_ = false;
  bool valid_ = false;
};

inline bool JointChargeContinuations::prove(
    const LateBoundDecimalSelectorProof& charge,
    const std::map<int, std::set<std::string>>& families, bool precise_entry_facts) {
  const auto target = commands_.at(charge.target_label);
  const auto& successors = view_.successors();
  std::vector<std::size_t> pending, entries;
  for (const auto state : rows_by_item_[charge.low_item_index]) {
    if (successors[state].empty()) return false;
    pending.insert(pending.end(), successors[state].begin(), successors[state].end());
  }
  std::vector<bool> visited(view_.size(), false);
  std::size_t visit_count = 0;
  while (!pending.empty()) {
    const auto state = pending.back();
    pending.pop_back();
    if (state >= view_.size()) return false;
    if (visited[state]) continue;
    visited[state] = true;
    if (++visit_count > 50000U) return false;
    const auto& item = items_[view_.state(state).item_index];
    if (item.kind != MachineItemKind::Op || item.raw || item.manual_interaction) return false;
    const int opcode = item.opcode;
    const int family = opcode & 0xf0;
    const auto& next = successors[state];
    if (next.empty()) return false;
    if (opcode >= 0x47 && opcode <= 0x4e) {
      const auto carrier = families.find(opcode & 15);
      if (carrier == families.end() || !selector_charge_entry_closer_opcode(opcode))
        return false;
      const bool owns_command = std::any_of(carrier->second.begin(), carrier->second.end(),
          [&](const auto& label) {
            const auto command = commands_.find(label);
            return command != commands_.end() && command->second == target;
          });
      if (!owns_command) return false;
      entries.insert(entries.end(), next.begin(), next.end());
      continue;
    }
    // The separate relocation proof still owns exact target correlation
    // and return frames; these commands are transparent only to entry data.
    if (opcode != 0x51 && opcode != 0x52 && opcode != 0x53 &&
        !(family == 0x80 && (opcode & 15) <= 14) &&
        !(family == 0xa0 && (opcode & 15) <= 14)) return false;
    pending.insert(pending.end(), next.begin(), next.end());
  }
  std::sort(entries.begin(), entries.end());
  entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
  if (entries.empty()) return false;
  const StackValueEqualityState initial{{false,true,true,true},false,true};
  const auto continuation = [&](bool precise) {
    auto [entry, inserted] = continued_.try_emplace({precise, entries}, false);
    if (inserted)
      entry->second = prove_post_layout_stack_state_equality_on_view(
          items_, view_, entries, initial,
          {.refine_runtime_selectors=false, .numeric_entry_closed=true,
           .transport_closed_entry_through_control=precise,
           .use_indirect_store_entry_facts=precise});
    return entry->second;
  };
  return continuation(false) || (precise_entry_facts && continuation(true));
}

inline bool joint_runtime_charge_stack_is_private(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const LateBoundDecimalSelectorProof& charge,
    const std::map<int,std::set<std::string>>& families,
    bool precise_entry_facts = false) {
  JointChargeContinuations continuations(items, {flow});
  return continuations.stack_is_private(charge, families, precise_entry_facts);
}

// A typed runtime charge is not automatically a private numeric value.
// A data read of its carrier pins the original encoded targets. Independently
// require stack-continuation nonobservation for every literal producer:
// a private address register can still leak its word through Z/T or X1/X2.
inline std::optional<JointOriginMap> protected_joint_runtime_charge_targets(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow* prepared_flow = nullptr,
    const std::vector<PreloadReport>* preloads = nullptr,
    bool precise_entry_facts = false,
    const JointOriginMap* pending_origins = nullptr,
    std::shared_ptr<const AuthoritativePostLayoutControlFlow> owned_flow = {}) {
  std::map<int, std::set<std::string>> families;
  for (const auto& item : items) {
    if (!joint_charged_flow(item)) continue;
    if (item.kind != MachineItemKind::Op || !item.indirect_flow_targets)
      return std::nullopt;
    for (const auto& target : *item.indirect_flow_targets) {
      const auto* label = std::get_if<std::string>(&target);
      if (!label) return std::nullopt;
      families[item.opcode & 15].insert(*label);
    }
  }
  if (families.empty()) return JointOriginMap{};
  constexpr unsigned all_registers = 0xffffU;
  const auto register_bit = [](int reg) { return 1U << static_cast<unsigned>(reg & 15); };
  const auto register_mask = [&](const std::vector<int>& registers) {
    unsigned mask = 0;
    for (const int reg : registers)
      if (reg >= 0 && reg <= 15) mask |= register_bit(reg);
    return mask;
  };
  unsigned observed = 0, directly_written = 0;
  InlineProofRow<std::size_t, 2> indirect_stores;
  for (std::size_t at = 0; at < items.size(); ++at) {
    const auto& item = items[at];
    if (item.kind != MachineItemKind::Op) continue;
    if (item.raw) return std::nullopt;
    const auto code = item.opcode;
    const int family = code & 0xf0;
    if (code >= 0x60 && code <= 0x6f) observed |= register_bit(code);
    if (code >= 0x40 && code <= 0x4f) directly_written |= register_bit(code);
    if (family == 0xb0 || family == 0xd0) observed |= register_bit(code);
    if (family == 0xd0)
      observed |= item.indirect_memory_targets ? register_mask(*item.indirect_memory_targets)
                                               : all_registers;
    if (family == 0xb0 && preloads) indirect_stores.push_back(at);
  }
  std::set<std::string> protected_labels;
  for (const auto& [reg, targets] : families) {
    if ((observed & register_bit(reg)) == 0U) continue;
    // Self-modifying low selectors need their own counter/word certificate.
    if (reg < 7 || reg > 14) return std::nullopt;
    protected_labels.insert(targets.begin(),targets.end());
  }
  const auto charges = inspect_deferred_decimal_selector_charges(items);
  if (!charges.diagnostics.empty()) return std::nullopt;
  const auto protected_result = [&]() -> std::optional<JointOriginMap> {
    JointOriginMap result;
    for (const auto& charge : charges.charges) {
      if (!protected_labels.contains(charge.target_label)) continue;
      const int high = items[charge.high_item_index].opcode;
      const int low = items[charge.low_item_index].opcode;
      if (high < 0 || high > 9 || low < 0 || low > 9) return std::nullopt;
      const int encoded = charge.single_cell ? low : high * 10 + low;
      const auto [old,inserted] = result.emplace(charge.target_label,encoded);
      if (!inserted && old->second != encoded) return std::nullopt;
    }
    for (const auto& label : protected_labels)
      if (!result.contains(label)) return std::nullopt;
    return result;
  };
  // No stack-privacy question remains when every charge is already pinned by
  // an observed register. With no pending origins there is no additional
  // origin validation to perform before returning the same encoded bindings.
  if (!pending_origins && std::all_of(charges.charges.begin(),charges.charges.end(),
      [&](const auto& charge){return protected_labels.contains(charge.target_label);}))
    return protected_result();
  if (!prepared_flow) {
    PostLayoutControlFlowOptions options; options.empty_return_target=1;
    owned_flow=shared_post_layout_control_flow(items,options);
    prepared_flow=owned_flow.get();
  }

  // A conditional data view only: encoded words identify their original
  // callback commands even while a pending placement has moved the labels.
  // Unsupported/ambiguous origins retain the conservative complete graph.
  std::shared_ptr<const SelectorConsistentExecutionGraph> selection;
  JointOriginMap encoded_origins;
  int address=0;
  bool consistent=true;
  for (const auto& item:items) {
    if (item.kind==MachineItemKind::Label) encoded_origins[item.name]=address;
    else ++address;
  }
  if (pending_origins) {
    for (const auto& [label,origin] : *pending_origins) {
      if (!encoded_origins.contains(label) || origin < 0) return std::nullopt;
      encoded_origins[label] = origin;
    }
  }
  std::map<std::string,int> word_origins;
  const auto record=[&](const std::string& label,int value) {
    // A charge can name another alias of the same command. Its encoded
    // digits establish that alias; independently supplied origins constrain
    // only the identities they actually name. Contradictions still reject.
    if (pending_origins && pending_origins->contains(label) &&
        pending_origins->at(label) != value) consistent=false;
    const auto [at,inserted]=word_origins.emplace(label,value);
    if (!inserted && at->second!=value) consistent=false;
    encoded_origins[label]=value;
  };
  for (const auto& charge:charges.charges) {
    const int high=items[charge.high_item_index].opcode;
    const int low=items[charge.low_item_index].opcode;
    if (high<0 || high>9 || low<0 || low>9) { consistent=false; break; }
    record(charge.target_label,charge.single_cell?low:high*10+low);
  }
  if (preloads) {
    unsigned written = directly_written;
    for (const auto at : indirect_stores) {
      const auto aliases = prepared_flow->indirect_memory_targets.find(at);
      if (aliases == prepared_flow->indirect_memory_targets.end()) {
        written = all_registers;
        break;
      }
      written |= register_mask(aliases->second);
    }
    const auto words=joint_preload_index(*preloads);
    if (!words) consistent=false;
    std::array<std::optional<int>, 16> decoded_addresses;
    if (words) for (const auto& item:items) {
      if (joint_charged_flow(item) || !joint_stable_flow_operand(item) ||
          !item.indirect_flow_targets) continue;
      const int reg=item.opcode & 15;
      if (!words->contains(reg)) continue;

      // A Main preload is not a runtime constant after any possible write.
      // Do not record it as an origin, but do not discard independently
      // established callback charge origins either. The selected graph must
      // derive this mutable family's rows from the program, or retain all
      // its declared alternatives. No value is assumed for an unknown row.
      if ((written & register_bit(reg)) != 0U) continue;
      auto& decoded_address = decoded_addresses[static_cast<std::size_t>(reg)];
      if (!decoded_address) {
        const auto& word=*words->at(reg);
        if (!joint_literal_word(word)) { consistent=false; break; }
        try {
          const auto decoded=evaluate_indirect_address(word.register_name,word.value,
              IndirectOperationKind::Flow,prepared_flow->address_space_model);
          if (!decoded || !decoded->actual_flow_target || !decoded->formal_address ||
              decoded->formal_address->kind != FormalAddressKind::Official) {
            consistent=false; break;
          }
          decoded_address = decoded->actual_flow_target;
        } catch (const std::exception&) { consistent=false; break; }
      }
      for (const auto& target:*item.indirect_flow_targets) {
        const auto* label=std::get_if<std::string>(&target);
        if (!label) { consistent=false; break; }
        record(*label,*decoded_address);
      }
    }
  }
  if (consistent) {
    selection = owned_flow && owned_flow.get() == prepared_flow
        ? shared_deferred_selector_consistent_execution_graph(items, owned_flow, encoded_origins)
        : shared_deferred_selector_consistent_execution_graph(items, *prepared_flow, encoded_origins);
  }
  JointChargeContinuations continuations(items, {*prepared_flow,
      selection && selection->proved ? selection.get() : nullptr});
  for (const auto& charge:charges.charges)
    if (!protected_labels.contains(charge.target_label) &&
        !continuations.stack_is_private(charge, families, precise_entry_facts))
      protected_labels.insert(charge.target_label);
  return protected_result();
}

} // namespace mkpro::core::detail
