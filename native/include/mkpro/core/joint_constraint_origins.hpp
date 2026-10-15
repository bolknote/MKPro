#pragma once

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/late_bound_decimal_selector.hpp"
#include "mkpro/core/result.hpp"

#include <algorithm>
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
      !word.setup_expression && !word.setup_expression_text && !word.setup_source_line;
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

inline bool joint_runtime_charge_stack_is_private(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const LateBoundDecimalSelectorProof& charge,
    const std::map<int,std::set<std::string>>& families) {
  if (!flow.proved || flow.execution_states.size()!=flow.execution_successors.size())
    return false;
  std::vector<std::size_t> pending, entries;
  for (std::size_t state=0;state<flow.execution_states.size();++state) {
    if (flow.execution_states[state].item_index!=charge.low_item_index) continue;
    if (flow.execution_successors[state].empty())
      return false;
    pending.insert(pending.end(),flow.execution_successors[state].begin(),
                   flow.execution_successors[state].end());
  }
  std::set<std::size_t> visited;
  while (!pending.empty()) {
    const auto state=pending.back(); pending.pop_back();
    if (state>=flow.execution_states.size()) return false;
    if (!visited.insert(state).second) continue;
    if (visited.size()>50000U) return false;
    const auto& execution=flow.execution_states[state];
    if (execution.item_index>=items.size()) return false;
    const auto& item=items[execution.item_index];
    if (item.kind!=MachineItemKind::Op || item.raw || item.manual_interaction) return false;
    const int opcode=item.opcode;
    const int family=opcode & 0xf0;
    const auto& next=flow.execution_successors[state];
    if (next.empty()) return false;
    if (opcode>=0x47 && opcode<=0x4e) {
      const auto carrier=families.find(opcode & 15);
      if (carrier==families.end() || !selector_charge_entry_closer_opcode(opcode))
        return false;
      bool aliases=carrier->second.contains(charge.target_label);
      int address=0;
      for (const auto& candidate:items) {
        if (candidate.kind==MachineItemKind::Label)
          aliases=aliases || (address==charge.target_address &&
                             carrier->second.contains(candidate.name));
        else ++address;
      }
      if (!aliases) return false;
      entries.insert(entries.end(),next.begin(),next.end());
      continue;
    }
    // These are transparent only for data/entry state. The independent exact
    // relocation proof still owns target correlation and every return frame.
    if (opcode!=0x51 && opcode!=0x52 && opcode!=0x53 &&
        !(family==0x80 && (opcode & 15)<=14) &&
        !(family==0xa0 && (opcode & 15)<=14)) return false;
    pending.insert(pending.end(),next.begin(),next.end());
  }
  std::sort(entries.begin(),entries.end());
  entries.erase(std::unique(entries.begin(),entries.end()),entries.end());
  if (entries.empty()) return false;
  return prove_post_layout_stack_state_equality(items,flow,entries,
      {{false,true,true,true},false,true},
      {.refine_runtime_selectors=false,.numeric_entry_closed=true});
}

// A typed runtime charge is not automatically a private numeric value.
// A data read of its carrier pins the original encoded targets. Independently
// require stack-continuation nonobservation for every literal producer:
// a private address register can still leak its word through Z/T or X1/X2.
inline std::optional<JointOriginMap> protected_joint_runtime_charge_targets(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow* prepared_flow = nullptr,
    const std::vector<PreloadReport>* preloads = nullptr) {
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
  std::set<std::string> protected_labels;
  for (const auto& [reg, targets] : families) {
    bool observed = false;
    for (const auto& item : items) {
      if (item.kind != MachineItemKind::Op) continue;
      if (item.raw) return std::nullopt;
      const int family = item.opcode & 0xf0;
      if (item.opcode == 0x60 + reg ||
          ((family == 0xb0 || family == 0xd0) && (item.opcode & 15) == reg))
        observed = true;
      if (family == 0xd0 && (!item.indirect_memory_targets ||
          std::find(item.indirect_memory_targets->begin(),
                    item.indirect_memory_targets->end(),reg) !=
                    item.indirect_memory_targets->end()))
        observed = true;
    }
    if (!observed) continue;
    // Self-modifying low selectors need their own counter/word certificate.
    if (reg < 7 || reg > 14) return std::nullopt;
    protected_labels.insert(targets.begin(),targets.end());
  }
  if (families.empty()) return JointOriginMap{};
  const auto charges = rebind_late_bound_decimal_selectors(
      items,{.minimum_target_address=0,.maximum_target_address=99});
  if (!charges.diagnostics.empty()) return std::nullopt;
  std::optional<AuthoritativePostLayoutControlFlow> owned_flow;
  if (!prepared_flow) {
    PostLayoutControlFlowOptions options; options.empty_return_target=1;
    owned_flow=build_post_layout_control_flow(items,options);
    prepared_flow=&*owned_flow;
  }

  // A conditional data view only: encoded words identify their original
  // callback commands even while a pending placement has moved the labels.
  // Unsupported/ambiguous origins retain the conservative complete graph.
  auto privacy_flow=*prepared_flow;
  JointOriginMap encoded_origins;
  int address=0;
  bool consistent=true;
  for (const auto& item:items) {
    if (item.kind==MachineItemKind::Label) encoded_origins[item.name]=address;
    else ++address;
  }
  std::map<std::string,int> word_origins;
  const auto record=[&](const std::string& label,int value) {
    const auto [at,inserted]=word_origins.emplace(label,value);
    if (!inserted && at->second!=value) consistent=false;
    encoded_origins[label]=value;
  };
  for (const auto& charge:charges.proofs) {
    const int high=items[charge.high_item_index].opcode;
    const int low=items[charge.low_item_index].opcode;
    if (high<0 || high>9 || low<0 || low>9) { consistent=false; break; }
    record(charge.target_label,charge.single_cell?low:high*10+low);
  }
  if (preloads) {
    const auto words=joint_preload_index(*preloads);
    if (!words) consistent=false;
    if (words) for (const auto& item:items) {
      if (joint_charged_flow(item) || !joint_stable_flow_operand(item) ||
          !item.indirect_flow_targets) continue;
      const int reg=item.opcode & 15;
      if (!words->contains(reg)) continue;

      bool written=false;
      for (std::size_t at=0;at<items.size();++at) {
        const auto& definition=items[at];
        if (definition.kind!=MachineItemKind::Op) continue;
        if (definition.opcode==0x40+reg) { written=true; break; }
        if ((definition.opcode & 0xf0)!=0xb0) continue;
        const auto aliases=prepared_flow->indirect_memory_targets.find(at);
        if (aliases==prepared_flow->indirect_memory_targets.end() ||
            std::find(aliases->second.begin(),aliases->second.end(),reg)!=aliases->second.end()) {
          written=true; break;
        }
      }
      if (written) { consistent=false; break; }
      const auto& word=*words->at(reg);
      if (!joint_literal_word(word)) { consistent=false; break; }
      try {
      const auto decoded=evaluate_indirect_address(word.register_name,word.value,
          IndirectOperationKind::Flow,prepared_flow->address_space_model);
      if (!decoded || !decoded->actual_flow_target || !decoded->formal_address ||
          decoded->formal_address->kind!=FormalAddressKind::Official) {
        consistent=false; break;
      }
      for (const auto& target:*item.indirect_flow_targets) {
        const auto* label=std::get_if<std::string>(&target);
        if (!label) { consistent=false; break; }
        record(*label,*decoded->actual_flow_target);
      }
      } catch (const std::exception&) { consistent=false; break; }
    }
  }
  if (consistent) {
    const auto selected=build_deferred_selector_consistent_execution_graph(
        items,*prepared_flow,encoded_origins).execution;
    if (selected.proved) {
      privacy_flow.execution_states.clear();
      for (const auto state:selected.original_states)
        privacy_flow.execution_states.push_back(prepared_flow->execution_states.at(state));
      privacy_flow.execution_successors=selected.successors;
      privacy_flow.execution_edges=selected.execution_edges;
    }
  }
  for (const auto& charge:charges.proofs)
    if (!protected_labels.contains(charge.target_label) &&
        !joint_runtime_charge_stack_is_private(items,privacy_flow,charge,families))
      protected_labels.insert(charge.target_label);
  JointOriginMap result;
  for (const auto& charge : charges.proofs) {
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
}

} // namespace mkpro::core::detail
