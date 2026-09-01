#include "mkpro/core/shared_helper_dual_mode_layout.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>

namespace mkpro::core {

namespace {

constexpr int kSideSpaceOrdinalBase = 112;
constexpr int kSideSpaceLastPhysical = 47;
constexpr int kOfficialTailStart = 48;

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
                                        [](const MachineItem& item) {
                                          return item.kind != MachineItemKind::Label;
                                        }));
}

struct ArtifactIndex {
  std::vector<int> addresses;
  std::map<std::string, int> label_addresses;
  std::map<int, std::vector<std::string>> labels_by_address;
  std::map<int, std::size_t> cells;
  int cell_count = 0;
};

ArtifactIndex index_artifact(const std::vector<MachineItem>& items) {
  ArtifactIndex result;
  result.addresses.resize(items.size());
  int address = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    result.addresses.at(item_index) = address;
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Label) {
      result.label_addresses[item.name] = address;
      result.labels_by_address[address].push_back(item.name);
      continue;
    }
    result.cells[address] = item_index;
    ++address;
  }
  result.cell_count = address;
  return result;
}

bool role_is(const CellRole& role, std::string_view prefix) {
  return role.size() >= prefix.size() &&
         std::string_view(role).substr(0, prefix.size()) == prefix;
}

bool has_role(const MachineItem& item, const std::string& role) {
  return std::find(item.roles.begin(), item.roles.end(), role) != item.roles.end();
}

void add_role(MachineItem& item, const std::string& role) {
  if (!has_role(item, role))
    item.roles.push_back(role);
}

void erase_transaction_roles(MachineItem& item, const std::string& marker) {
  const std::string prefix = marker + ":";
  std::erase_if(item.roles, [&](const CellRole& role) {
    return role_is(role, prefix) &&
           !role_is(role, prefix + "origin:");
  });
}

std::optional<std::size_t> unique_role_item(
    const std::vector<MachineItem>& items, const std::string& role) {
  std::optional<std::size_t> result;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (!has_role(items.at(item_index), role))
      continue;
    if (result.has_value())
      return std::nullopt;
    result = item_index;
  }
  return result;
}

std::optional<std::size_t> origin_id(const MachineItem& item,
                                     const std::string& marker) {
  const std::string prefix = marker + ":origin:";
  for (const CellRole& role : item.roles) {
    if (!role_is(role, prefix))
      continue;
    std::size_t value = 0;
    const std::string_view digits(role.data() + prefix.size(),
                                  role.size() - prefix.size());
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size())
      return value;
  }
  return std::nullopt;
}

std::string unique_marker(const std::vector<MachineItem>& items) {
  for (int suffix = 0;; ++suffix) {
    const std::string marker =
        "shared-helper-dual-mode-" + std::to_string(suffix);
    bool used = false;
    for (const MachineItem& item : items) {
      used = std::any_of(item.roles.begin(), item.roles.end(),
                         [&](const CellRole& role) {
                           return role_is(role, marker + ":");
                         });
      if (used)
        break;
    }
    if (!used)
      return marker;
  }
}

std::optional<std::size_t> next_cell_item(const std::vector<MachineItem>& items,
                                          std::size_t after) {
  for (++after; after < items.size(); ++after) {
    if (items.at(after).kind != MachineItemKind::Label)
      return after;
  }
  return std::nullopt;
}

bool is_straight_body_opcode(int opcode) {
  if (opcode_by_code(opcode).takes_address)
    return false;
  const std::vector<IrOp> raised =
      raise_machine_to_ir({MachineItem::op(opcode, opcode_by_code(opcode).name)});
  const IrKind kind = raised.empty() ? IrKind::Plain : raised.front().kind;
  switch (kind) {
  case IrKind::Store:
  case IrKind::Recall:
  case IrKind::IndirectStore:
  case IrKind::IndirectRecall:
  case IrKind::Plain:
    return true;
  default:
    return false;
  }
}

bool is_return_opcode(int opcode) {
  const std::vector<IrOp> raised =
      raise_machine_to_ir({MachineItem::op(opcode, opcode_by_code(opcode).name)});
  return !raised.empty() && raised.front().kind == IrKind::Return;
}

bool is_direct_store_opcode(int opcode) {
  return opcode >= 0x40 && opcode <= 0x4e;
}

bool reachable_item_has_only_predecessor(
    const AuthoritativePostLayoutControlFlow& control, std::size_t item_index,
    std::size_t predecessor_item_index) {
  bool reached = false;
  for (const PostLayoutExternalEntryState& entry : control.external_entries) {
    if (entry.entry.item_index == item_index)
      return false;
  }
  for (std::size_t state_index = 0; state_index < control.execution_states.size(); ++state_index) {
    if (state_index >= control.execution_successors.size())
      return false;
    for (const std::size_t successor : control.execution_successors.at(state_index)) {
      if (successor >= control.execution_states.size())
        return false;
      if (control.execution_states.at(successor).item_index != item_index)
        continue;
      reached = true;
      if (control.execution_states.at(state_index).item_index != predecessor_item_index)
        return false;
    }
  }
  return reached;
}

bool is_indirect_call_opcode(int opcode) {
  const std::vector<IrOp> raised =
      raise_machine_to_ir({MachineItem::op(opcode, opcode_by_code(opcode).name)});
  return !raised.empty() && raised.front().kind == IrKind::IndirectCall;
}

bool is_side_alias(const FormalAddressInfo& info) {
  return (info.kind == FormalAddressKind::LongSide ||
          info.kind == FormalAddressKind::Dark) &&
         !info.one_command;
}

int formal_side_opcode(int physical_address, AddressSpaceModel model) {
  const int ordinal = kSideSpaceOrdinalBase + physical_address;
  for (int high = 0x0b; high <= 0x0f; ++high) {
    const int low = ordinal - high * 10;
    if (low < 0 || low > 0x0f)
      continue;
    const int opcode = high * 16 + low;
    try {
      const FormalAddressInfo info = formal_address_info(opcode, model);
      if (is_side_alias(info) && info.actual == physical_address)
        return opcode;
    } catch (const std::exception&) {
      continue;
    }
  }
  return -1;
}

std::map<std::size_t, std::vector<int>> proved_target_addresses(
    const AuthoritativePostLayoutControlFlow& flow) {
  std::map<std::size_t, std::vector<int>> result;
  for (const auto& [item, targets] : flow.indirect_flow_targets) {
    std::vector<int> addresses;
    addresses.reserve(targets.size());
    for (const PostLayoutCommandIdentity& target : targets)
      addresses.push_back(target.address);
    result.emplace(item, std::move(addresses));
  }
  return result;
}

bool runtime_bound_selector(const MachineItem& item) {
  return std::any_of(item.roles.begin(), item.roles.end(), [](const CellRole& role) {
    return role.find("late-decimal-selector") != std::string::npos ||
           role.find("callee-hole") != std::string::npos ||
           role.find("runtime-charged-selector") != std::string::npos;
  });
}

std::optional<std::size_t> preload_for_register(
    const std::vector<PreloadReport>& preloads, int reg) {
  std::optional<std::size_t> result;
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    int candidate = -1;
    try {
      candidate = register_index(register_from_text(preloads.at(index).register_name));
    } catch (const std::exception&) {
      continue;
    }
    if (candidate != reg)
      continue;
    if (result.has_value())
      return std::nullopt;
    result = index;
  }
  return result;
}

void append_comment(MachineItem& item, const std::string& text) {
  if (item.comment.has_value() && !item.comment->empty())
    item.comment = *item.comment + "; " + text;
  else
    item.comment = text;
}

struct ProcedureBlock {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::size_t target_item = 0;
  int start_address = -1;
  int cells = 0;
  std::string target_label;
};

std::optional<ProcedureBlock> closed_procedure_block(
    const std::vector<MachineItem>& items, const ArtifactIndex& index,
    std::size_t target_item) {
  if (target_item >= items.size() ||
      items.at(target_item).kind != MachineItemKind::Op) {
    return std::nullopt;
  }
  const int target_address = index.addresses.at(target_item);
  std::optional<std::size_t> begin;
  for (std::size_t cursor = target_item + 1U; cursor > 0; --cursor) {
    const std::size_t item_index = cursor - 1U;
    if (index.addresses.at(item_index) != target_address)
      break;
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Label &&
        item.procedure_boundary == "start") {
      begin = item_index;
    }
  }
  if (!begin.has_value())
    return std::nullopt;

  std::optional<std::size_t> end;
  for (std::size_t item_index = target_item; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item_index > target_item && item.kind == MachineItemKind::Label &&
        item.procedure_boundary == "start") {
      break;
    }
    if (item.kind == MachineItemKind::Label &&
        item.procedure_boundary == "end") {
      end = item_index + 1U;
      break;
    }
  }
  if (!end.has_value()) {
    std::size_t cursor = target_item;
    while (cursor < items.size()) {
      if (items.at(cursor).kind == MachineItemKind::Op &&
          is_return_opcode(items.at(cursor).opcode)) {
        end = cursor + 1U;
        while (*end < items.size() &&
               items.at(*end).kind == MachineItemKind::Label &&
               items.at(*end).procedure_boundary == "end") {
          ++*end;
        }
        break;
      }
      ++cursor;
    }
  }
  if (!end.has_value() || *end <= *begin)
    return std::nullopt;

  std::optional<std::size_t> last_command;
  int cells = 0;
  for (std::size_t item_index = *begin; item_index < *end; ++item_index) {
    if (items.at(item_index).kind == MachineItemKind::Label)
      continue;
    last_command = item_index;
    ++cells;
  }
  if (!last_command.has_value() ||
      items.at(*last_command).kind != MachineItemKind::Op ||
      !is_return_opcode(items.at(*last_command).opcode)) {
    return std::nullopt;
  }
  const auto labels = index.labels_by_address.find(target_address);
  if (labels == index.labels_by_address.end() || labels->second.empty())
    return std::nullopt;
  return ProcedureBlock{
      .begin = *begin,
      .end = *end,
      .target_item = target_item,
      .start_address = target_address,
      .cells = cells,
      .target_label = labels->second.front(),
  };
}

} // namespace

SharedHelperDualModePreparation prepare_shared_helper_dual_mode_layout(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& control_flow,
    AddressSpaceModel model) {
  SharedHelperDualModePreparation result;
  result.items = items;
  if (!control_flow.proved) {
    result.reasons.push_back("dual-mode preparation requires authoritative control flow");
    return result;
  }

  const SharedHelperContinuationOptions continuation_options{
      .address_space_model = model,
      .proved_indirect_flow_targets = proved_target_addresses(control_flow),
  };
  std::set<std::string> labels;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      labels.insert(item.name);
  }
  std::tuple<bool, std::size_t, std::size_t> best_rejection_score{};
  for (const std::string& label : labels) {
    SharedHelperContinuationProof candidate =
        verify_shared_helper_continuation(items, label, continuation_options);
    if (candidate.proved) {
      result.continuation = std::move(candidate);
      break;
    }
    const std::tuple score{
        candidate.calls.size() == 3U &&
            candidate.ordinary_call_item_indices.size() == 2U,
        candidate.ordinary_call_item_indices.size(), candidate.calls.size()};
    if (score > best_rejection_score) {
      best_rejection_score = score;
      result.continuation = std::move(candidate);
    }
  }
  if (!result.continuation.proved) {
    result.reasons = result.continuation.reasons;
    return result;
  }
  if (result.continuation.helper_body_cells <= 0 ||
      result.continuation.helper_body_cells > kOfficialTailStart) {
    result.reasons.push_back("shared helper body cannot end at the F9 boundary");
    return result;
  }

  const std::optional<std::size_t> target = next_cell_item(
      items, result.continuation.helper_label_item_index);
  if (!target.has_value() || *target >= items.size() ||
      items.at(*target).kind != MachineItemKind::Op) {
    result.reasons.push_back("shared helper has no executable root item");
    return result;
  }

  result.marker = unique_marker(items);
  for (std::size_t item_index = 0; item_index < result.items.size(); ++item_index) {
    if (result.items.at(item_index).kind == MachineItemKind::Op) {
      add_role(result.items.at(item_index),
               result.marker + ":origin:" + std::to_string(item_index));
    }
  }
  add_role(result.items.at(*target), result.marker + ":helper-root");
  add_role(result.items.at(result.continuation.helper_return_item_index),
           result.marker + ":helper-return");

  int ordinary = 0;
  std::vector<std::size_t> trailing_stores;
  int trailing_store_opcode = -1;
  for (const SharedHelperContinuationCall& call : result.continuation.calls) {
    if (call.ordinary) {
      const std::string prefix =
          result.marker + ":ordinary-" + std::to_string(ordinary++);
      add_role(result.items.at(call.call_item_index), prefix + ":call");
      add_role(result.items.at(call.join_item_index), prefix + ":join");
      add_role(result.items.at(call.store_item_index), prefix + ":store");
      const std::optional<std::size_t> trailing =
          next_cell_item(items, call.store_item_index);
      if (trailing.has_value() && *trailing == call.store_item_index + 1U &&
          items.at(*trailing).kind == MachineItemKind::Op &&
          is_direct_store_opcode(items.at(*trailing).opcode) &&
          reachable_item_has_only_predecessor(control_flow, *trailing,
                                              call.store_item_index) &&
          (trailing_store_opcode < 0 || trailing_store_opcode == items.at(*trailing).opcode)) {
        trailing_store_opcode = items.at(*trailing).opcode;
        trailing_stores.push_back(*trailing);
        add_role(result.items.at(*trailing), prefix + ":trailing-store");
      }
    } else {
      add_role(result.items.at(call.call_item_index),
               result.marker + ":divergent-call");
    }
  }
  if (ordinary != 2) {
    result.reasons.push_back("dual-mode helper does not have exactly two ordinary calls");
    return result;
  }
  if (trailing_stores.size() == 2U) {
    result.trailing_store_opcode = trailing_store_opcode;
    result.trailing_store_item_indices = std::move(trailing_stores);
  }

  result.helper_target_item_index = *target;
  result.required_final_start_address =
      kOfficialTailStart - result.continuation.helper_body_cells;
  result.applied = 1;
  return result;
}

SharedHelperDualModeSelectorExchangeResult
exchange_shared_helper_dual_mode_selector_families(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const SharedHelperDualModePreparation& preparation,
    AddressSpaceModel model) {
  SharedHelperDualModeSelectorExchangeResult result;
  result.items = items;
  result.preloads = preloads;
  result.control_flow = control_flow;
  const auto reject = [&](std::string reason) {
    result.reasons.push_back(std::move(reason));
    return result;
  };
  if (!control_flow.proved || preparation.applied <= 0 ||
      !preparation.continuation.proved) {
    return reject("selector exchange requires a proved dual-mode preparation");
  }

  std::optional<int> fixed_register;
  std::set<std::size_t> helper_flow_items;
  for (const SharedHelperContinuationCall& call : preparation.continuation.calls) {
    if (!call.indirect || call.call_item_index >= items.size() ||
        !is_indirect_call_opcode(items.at(call.call_item_index).opcode)) {
      return reject("dual-mode call family is not uniformly indirect");
    }
    const int reg = items.at(call.call_item_index).opcode & 0x0f;
    if (fixed_register.has_value() && *fixed_register != reg)
      return reject("dual-mode calls already use more than one selector family");
    fixed_register = reg;
    helper_flow_items.insert(call.call_item_index);
  }
  if (!fixed_register.has_value())
    return reject("dual-mode call family has no selector register");
  const std::optional<std::size_t> fixed_preload =
      preload_for_register(preloads, *fixed_register);
  if (!fixed_preload.has_value())
    return reject("dual-mode selector has no delivered preload");
  if (preloads.at(*fixed_preload).retunable_natural_fractional_prefix.has_value())
    return reject("dual-mode selector is already retunable");

  const ArtifactIndex index = index_artifact(items);
  const std::optional<std::size_t> helper_target = next_cell_item(
      items, preparation.continuation.helper_label_item_index);
  if (!helper_target.has_value())
    return reject("dual-mode helper has no executable target identity");
  const std::optional<ProcedureBlock> helper_block =
      closed_procedure_block(items, index, *helper_target);
  if (!helper_block.has_value())
    return reject("dual-mode helper is not one closed procedure component");

  for (const auto& [flow_item, targets] : control_flow.indirect_flow_targets) {
    if (flow_item >= items.size() || items.at(flow_item).kind != MachineItemKind::Op)
      return reject("selector exchange saw an invalid indirect-flow identity");
    if ((items.at(flow_item).opcode & 0x0f) != *fixed_register)
      continue;
    if (targets.size() != 1U ||
        targets.front().item_index != helper_block->target_item ||
        !helper_flow_items.contains(flow_item)) {
      return reject("fixed selector has another indirect-flow use");
    }
  }

  struct FlexibleFamily {
    int reg = -1;
    std::size_t preload = 0;
    ProcedureBlock block;
    std::set<std::size_t> flow_items;
  };
  std::optional<FlexibleFamily> flexible;
  for (int reg = 0; reg <= 14; ++reg) {
    if (reg == *fixed_register)
      continue;
    const std::optional<std::size_t> preload = preload_for_register(preloads, reg);
    if (!preload.has_value() ||
        !preloads.at(*preload).retunable_natural_fractional_prefix.has_value()) {
      continue;
    }
    std::optional<std::size_t> target_item;
    std::set<std::size_t> flow_items;
    bool complete = true;
    for (const auto& [flow_item, targets] : control_flow.indirect_flow_targets) {
      if (flow_item >= items.size() || items.at(flow_item).kind != MachineItemKind::Op ||
          (items.at(flow_item).opcode & 0x0f) != reg) {
        continue;
      }
      if (targets.size() != 1U ||
          (target_item.has_value() && *target_item != targets.front().item_index)) {
        complete = false;
        break;
      }
      target_item = targets.front().item_index;
      flow_items.insert(flow_item);
    }
    if (!complete || !target_item.has_value() || flow_items.empty() ||
        *target_item == helper_block->target_item) {
      continue;
    }
    const std::optional<ProcedureBlock> block =
        closed_procedure_block(items, index, *target_item);
    if (!block.has_value() || helper_block->end != block->begin)
      continue;
    if (flexible.has_value())
      return reject("more than one adjacent retunable selector family is eligible");
    flexible = FlexibleFamily{
        .reg = reg,
        .preload = *preload,
        .block = *block,
        .flow_items = std::move(flow_items),
    };
  }
  if (!flexible.has_value())
    return reject("no adjacent closed component uses one retunable selector family");

  for (const auto& [flow_item, targets] : control_flow.indirect_flow_targets) {
    for (const PostLayoutCommandIdentity& target : targets) {
      if (target.item_index != helper_block->target_item &&
          target.item_index != flexible->block.target_item) {
        continue;
      }
      if (!helper_flow_items.contains(flow_item) &&
          !flexible->flow_items.contains(flow_item)) {
        return reject("a swapped component has an external indirect-flow family");
      }
    }
  }
  for (const PostLayoutExternalEntryState& entry : control_flow.external_entries) {
    const int address = entry.entry.address;
    if ((address >= helper_block->start_address &&
         address < helper_block->start_address + helper_block->cells) ||
        (address >= flexible->block.start_address &&
         address < flexible->block.start_address + flexible->block.cells)) {
      return reject("a swapped component is an externally admitted entry");
    }
  }

  const int new_helper_address =
      helper_block->start_address + flexible->block.cells;
  const std::optional<std::string> rebound =
      rebind_stable_preloaded_indirect_flow_selector(
          items, result.preloads.at(flexible->preload), control_flow,
          flexible->block.start_address, new_helper_address, model);
  if (!rebound.has_value())
    return reject("retunable selector could not be rebound after component exchange");
  result.preloads.at(flexible->preload).value = *rebound;

  for (const std::size_t flow_item : helper_flow_items) {
    MachineItem& item = result.items.at(flow_item);
    item.opcode = (item.opcode & 0xf0) | flexible->reg;
    item.mnemonic = opcode_by_code(item.opcode).name;
    item.indirect_flow_targets =
        std::vector<IrTarget>{preparation.continuation.helper_label};
  }
  for (const std::size_t flow_item : flexible->flow_items) {
    MachineItem& item = result.items.at(flow_item);
    item.opcode = (item.opcode & 0xf0) | *fixed_register;
    item.mnemonic = opcode_by_code(item.opcode).name;
    item.indirect_flow_targets =
        std::vector<IrTarget>{flexible->block.target_label};
  }

  std::vector<MachineItem> exchanged;
  exchanged.reserve(result.items.size());
  exchanged.insert(exchanged.end(), result.items.begin(),
                   result.items.begin() + static_cast<std::ptrdiff_t>(helper_block->begin));
  exchanged.insert(exchanged.end(),
                   result.items.begin() + static_cast<std::ptrdiff_t>(flexible->block.begin),
                   result.items.begin() + static_cast<std::ptrdiff_t>(flexible->block.end));
  exchanged.insert(exchanged.end(),
                   result.items.begin() + static_cast<std::ptrdiff_t>(helper_block->begin),
                   result.items.begin() + static_cast<std::ptrdiff_t>(helper_block->end));
  exchanged.insert(exchanged.end(),
                   result.items.begin() + static_cast<std::ptrdiff_t>(flexible->block.end),
                   result.items.end());
  result.items = std::move(exchanged);

  PostLayoutControlFlowOptions flow_options;
  flow_options.address_space_model = model;
  flow_options.empty_return_target = 1;
  result.control_flow = build_post_layout_control_flow(result.items, flow_options);
  if (!result.control_flow.proved) {
    return reject("selector-family exchange failed final command-identity CFG proof" +
                  (result.control_flow.reasons.empty()
                       ? std::string{}
                       : ": " + result.control_flow.reasons.front()));
  }
  result.applied = 1;
  result.optimizations.push_back(passes::AppliedOptimization{
      .name = "indirect-selector-family-exchange",
      .detail = "Exchanged two adjacent independently returning helper components so a "
                "fixed selector retained its address and a retunable decimal-prefix "
                "selector followed the relocated family; re-proved every indirect "
                "target and external entry by command identity.",
  });
  return result;
}

SharedHelperDualModeSelectorExchangeResult
exchange_profitable_direct_indirect_call_families(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    AddressSpaceModel model) {
  SharedHelperDualModeSelectorExchangeResult rejected;
  rejected.items = items;
  rejected.preloads = preloads;
  rejected.control_flow = control_flow;
  if (!control_flow.proved) {
    rejected.reasons.push_back(
        "call-family exchange requires authoritative control flow");
    return rejected;
  }

  const ArtifactIndex index = index_artifact(items);
  const auto direct_target_item = [&](std::size_t source)
      -> std::optional<std::pair<std::size_t, std::size_t>> {
    if (source >= items.size() || items.at(source).kind != MachineItemKind::Op ||
        items.at(source).opcode != 0x53) {
      return std::nullopt;
    }
    const std::optional<std::size_t> operand = next_cell_item(items, source);
    if (!operand.has_value() ||
        items.at(*operand).kind != MachineItemKind::Address) {
      return std::nullopt;
    }
    const MachineItem& address = items.at(*operand);
    std::optional<int> target_address;
    if (address.formal_opcode.has_value()) {
      try {
        target_address =
            formal_address_info(*address.formal_opcode, model).actual;
      } catch (const std::exception&) {
        return std::nullopt;
      }
    } else if (const auto* label = std::get_if<std::string>(&address.target)) {
      const auto found = index.label_addresses.find(*label);
      if (found != index.label_addresses.end())
        target_address = found->second;
    } else if (const auto* numeric = std::get_if<int>(&address.target)) {
      target_address = *numeric;
    }
    if (!target_address.has_value())
      return std::nullopt;
    const auto target = index.cells.find(*target_address);
    if (target == index.cells.end() ||
        items.at(target->second).kind != MachineItemKind::Op) {
      return std::nullopt;
    }
    return std::pair{target->second, *operand};
  };

  struct DirectCall {
    std::size_t source = 0;
    std::size_t operand = 0;
  };
  std::map<std::size_t, std::vector<DirectCall>> direct_calls_by_target;
  for (std::size_t source = 0; source < items.size(); ++source) {
    const auto target = direct_target_item(source);
    if (target.has_value()) {
      direct_calls_by_target[target->first].push_back(
          DirectCall{.source = source, .operand = target->second});
    }
  }

  struct Candidate {
    int reg = -1;
    std::size_t preload = 0;
    ProcedureBlock direct_block;
    ProcedureBlock indirect_block;
    std::vector<DirectCall> direct_calls;
    std::vector<std::size_t> indirect_calls;
    int saving = 0;
  };
  std::vector<Candidate> candidates;
  for (int reg = 7; reg <= 14; ++reg) {
    const std::optional<std::size_t> preload = preload_for_register(preloads, reg);
    if (!preload.has_value() ||
        !preloads.at(*preload).retunable_natural_fractional_prefix.has_value()) {
      continue;
    }
    std::optional<std::size_t> indirect_target;
    std::vector<std::size_t> indirect_calls;
    bool complete = true;
    for (const auto& [flow_item, targets] : control_flow.indirect_flow_targets) {
      if (flow_item >= items.size() || items.at(flow_item).kind != MachineItemKind::Op ||
          (items.at(flow_item).opcode & 0x0f) != reg) {
        continue;
      }
      if (!is_indirect_call_opcode(items.at(flow_item).opcode) ||
          targets.size() != 1U ||
          (indirect_target.has_value() &&
           *indirect_target != targets.front().item_index)) {
        complete = false;
        break;
      }
      indirect_target = targets.front().item_index;
      indirect_calls.push_back(flow_item);
    }
    if (!complete || !indirect_target.has_value() || indirect_calls.empty())
      continue;
    const std::optional<ProcedureBlock> indirect_block =
        closed_procedure_block(items, index, *indirect_target);
    if (!indirect_block.has_value())
      continue;

    for (const auto& [direct_target, direct_calls] : direct_calls_by_target) {
      if (direct_target == *indirect_target ||
          direct_calls.size() <= indirect_calls.size()) {
        continue;
      }
      const std::optional<ProcedureBlock> direct_block =
          closed_procedure_block(items, index, direct_target);
      if (!direct_block.has_value())
        continue;
      candidates.push_back(Candidate{
          .reg = reg,
          .preload = *preload,
          .direct_block = *direct_block,
          .indirect_block = *indirect_block,
          .direct_calls = direct_calls,
          .indirect_calls = indirect_calls,
          .saving = static_cast<int>(direct_calls.size() - indirect_calls.size()),
      });
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& left, const Candidate& right) {
              return std::tie(right.saving, left.direct_block.start_address,
                              left.indirect_block.start_address, left.reg) <
                     std::tie(left.saving, right.direct_block.start_address,
                              right.indirect_block.start_address, right.reg);
            });
  if (candidates.empty()) {
    rejected.reasons.push_back(
        "no profitable direct/retunable-indirect call families were proved");
    return rejected;
  }

  struct DirectFlowIdentity {
    std::size_t source_item = 0;
    std::size_t target_item = 0;
  };
  std::vector<DirectFlowIdentity> direct_flows;
  for (std::size_t source = 0; source < items.size(); ++source) {
    const MachineItem& command = items.at(source);
    if (command.kind != MachineItemKind::Op ||
        !opcode_by_code(command.opcode).takes_address) {
      continue;
    }
    const std::optional<std::size_t> operand = next_cell_item(items, source);
    if (!operand.has_value() ||
        items.at(*operand).kind != MachineItemKind::Address) {
      rejected.reasons.push_back("direct flow lost its address operand");
      return rejected;
    }
    const MachineItem& address = items.at(*operand);
    std::optional<int> target_address;
    if (address.formal_opcode.has_value()) {
      try {
        target_address = formal_address_info(*address.formal_opcode, model).actual;
      } catch (const std::exception&) {
        rejected.reasons.push_back("direct flow has an undecodable formal address");
        return rejected;
      }
    } else if (const auto* label = std::get_if<std::string>(&address.target)) {
      const auto found = index.label_addresses.find(*label);
      if (found != index.label_addresses.end())
        target_address = found->second;
    } else if (const auto* numeric = std::get_if<int>(&address.target)) {
      target_address = *numeric;
    }
    if (!target_address.has_value() || !index.cells.contains(*target_address)) {
      rejected.reasons.push_back("direct flow target is not an executable identity");
      return rejected;
    }
    direct_flows.push_back({source, index.cells.at(*target_address)});
  }

  for (const Candidate& candidate : candidates) {
    SharedHelperDualModeSelectorExchangeResult attempt;
    attempt.items = items;
    attempt.preloads = preloads;
    attempt.control_flow = control_flow;
    std::string failure;
    const auto fail = [&](std::string reason) {
      failure = std::move(reason);
      return false;
    };

    std::set<std::size_t> direct_sources;
    std::set<std::size_t> direct_operands;
    for (const DirectCall& call : candidate.direct_calls) {
      direct_sources.insert(call.source);
      direct_operands.insert(call.operand);
    }
    const std::set<std::size_t> indirect_sources(
        candidate.indirect_calls.begin(), candidate.indirect_calls.end());

    std::vector<MachineItem> rewritten;
    rewritten.reserve(items.size() - direct_operands.size() +
                      indirect_sources.size());
    std::map<std::size_t, std::size_t> final_item_by_original;
    for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
      if (direct_operands.contains(item_index))
        continue;
      MachineItem item = items.at(item_index);
      if (direct_sources.contains(item_index)) {
        item.opcode = 0xd0 | candidate.reg;
        item.mnemonic = opcode_by_code(item.opcode).name;
        item.indirect_flow_targets =
            std::vector<IrTarget>{candidate.direct_block.target_label};
        append_comment(item, "profitable indirect call-family exchange");
      } else if (indirect_sources.contains(item_index)) {
        item.opcode = 0x53;
        item.mnemonic = opcode_by_code(item.opcode).name;
        item.indirect_flow_targets.reset();
        append_comment(item, "profitable direct call-family exchange");
      }
      final_item_by_original.emplace(item_index, rewritten.size());
      rewritten.push_back(std::move(item));
      if (indirect_sources.contains(item_index)) {
        MachineItem operand =
            MachineItem::address(candidate.indirect_block.target_label);
        append_comment(operand, "direct call-family target");
        rewritten.push_back(std::move(operand));
      }
    }
    if (cell_count(rewritten) != cell_count(items) - candidate.saving) {
      fail("call-family exchange produced an unexpected cell count");
      rejected.reasons.push_back(failure);
      continue;
    }

    ArtifactIndex final_index = index_artifact(rewritten);
    for (const DirectFlowIdentity& flow : direct_flows) {
      if (direct_sources.contains(flow.source_item))
        continue;
      if (!final_item_by_original.contains(flow.source_item) ||
          !final_item_by_original.contains(flow.target_item)) {
        fail("call-family exchange lost a direct-flow identity");
        break;
      }
      const std::size_t final_source =
          final_item_by_original.at(flow.source_item);
      const std::size_t final_target =
          final_item_by_original.at(flow.target_item);
      const std::optional<std::size_t> final_operand =
          next_cell_item(rewritten, final_source);
      if (!final_operand.has_value() ||
          rewritten.at(*final_operand).kind != MachineItemKind::Address) {
        fail("relocated direct flow lost its address operand");
        break;
      }
      MachineItem& address = rewritten.at(*final_operand);
      const int final_target_address = final_index.addresses.at(final_target);
      const auto labels = final_index.labels_by_address.find(final_target_address);
      if (labels != final_index.labels_by_address.end() && !labels->second.empty())
        address.target = labels->second.front();
      else
        address.target = final_target_address;
      address.formal_opcode.reset();
    }
    if (!failure.empty()) {
      rejected.reasons.push_back(failure);
      continue;
    }

    struct RebindRequirement {
      int old_target = -1;
      int new_target = -1;
      std::size_t original_flow_item = 0;
    };
    std::map<int, RebindRequirement> required_by_register;
    for (const auto& [original_flow_item, targets] :
         control_flow.indirect_flow_targets) {
      if (indirect_sources.contains(original_flow_item))
        continue;
      if (!final_item_by_original.contains(original_flow_item)) {
        fail("call-family exchange lost an indirect-flow identity");
        break;
      }
      MachineItem& final_flow =
          rewritten.at(final_item_by_original.at(original_flow_item));
      std::vector<IrTarget> rebound_targets;
      std::vector<std::pair<int, int>> target_moves;
      for (const PostLayoutCommandIdentity& target : targets) {
        if (!final_item_by_original.contains(target.item_index)) {
          fail("call-family exchange erased an indirect target identity");
          break;
        }
        const std::size_t final_target =
            final_item_by_original.at(target.item_index);
        const int final_address = final_index.addresses.at(final_target);
        const auto labels = final_index.labels_by_address.find(final_address);
        if (labels != final_index.labels_by_address.end() && !labels->second.empty())
          rebound_targets.emplace_back(labels->second.front());
        else
          rebound_targets.emplace_back(final_address);
        target_moves.emplace_back(target.address, final_address);
      }
      if (!failure.empty())
        break;
      final_flow.indirect_flow_targets = std::move(rebound_targets);
      if (targets.size() != 1U)
        continue;
      const int reg = final_flow.opcode & 0x0f;
      const RebindRequirement requirement{
          .old_target = target_moves.front().first,
          .new_target = target_moves.front().second,
          .original_flow_item = original_flow_item,
      };
      const auto [existing, inserted] = required_by_register.emplace(reg, requirement);
      if (!inserted &&
          (existing->second.old_target != requirement.old_target ||
           existing->second.new_target != requirement.new_target) &&
          !runtime_bound_selector(final_flow)) {
        fail("one selector register needs incompatible target rebinding");
        break;
      }
    }
    if (!failure.empty()) {
      rejected.reasons.push_back(failure);
      continue;
    }

    const int final_direct_target = final_index.addresses.at(
        final_item_by_original.at(candidate.direct_block.target_item));
    required_by_register[candidate.reg] = RebindRequirement{
        .old_target = candidate.indirect_block.start_address,
        .new_target = final_direct_target,
        .original_flow_item = candidate.indirect_calls.front(),
    };
    for (const auto& [reg, requirement] : required_by_register) {
      if (requirement.old_target == requirement.new_target)
        continue;
      const std::optional<std::size_t> preload =
          preload_for_register(attempt.preloads, reg);
      if (!preload.has_value()) {
        if (!final_item_by_original.contains(requirement.original_flow_item) ||
            !runtime_bound_selector(
                rewritten.at(final_item_by_original.at(
                    requirement.original_flow_item)))) {
          fail("moving indirect target lacks a stable or runtime selector");
          break;
        }
        continue;
      }
      PreloadReport& report = attempt.preloads.at(*preload);
      std::optional<std::string> rebound =
          rebind_stable_preloaded_indirect_flow_selector(
              items, report, control_flow, requirement.old_target,
              requirement.new_target, model);
      if (!rebound.has_value()) {
        rebound = rebind_proved_natural_fractional_selector_preload(
            items, report, requirement.old_target,
            requirement.new_target, model);
      }
      if (!rebound.has_value()) {
        fail("selector R" + std::to_string(reg) +
             " could not be rebound after call-family exchange");
        break;
      }
      report.value = *rebound;
    }
    if (!failure.empty()) {
      rejected.reasons.push_back(failure);
      continue;
    }

    PostLayoutControlFlowOptions flow_options;
    flow_options.address_space_model = model;
    flow_options.empty_return_target = 1;
    attempt.control_flow =
        build_post_layout_control_flow(rewritten, flow_options);
    if (!attempt.control_flow.proved) {
      rejected.reasons.push_back(
          "call-family exchange failed final command-identity CFG proof" +
          (attempt.control_flow.reasons.empty()
               ? std::string{}
               : ": " + attempt.control_flow.reasons.front()));
      continue;
    }
    attempt.items = std::move(rewritten);
    attempt.applied = 1;
    attempt.optimizations.push_back(passes::AppliedOptimization{
        .name = "direct-indirect-call-family-exchange",
        .detail = "Exchanged " +
                  std::to_string(candidate.direct_calls.size()) +
                  " direct calls with " +
                  std::to_string(candidate.indirect_calls.size()) +
                  " calls through one retunable selector; preserved both "
                  "closed procedure identities and removed " +
                  std::to_string(candidate.saving) + " cell(s).",
    });
    return attempt;
  }

  if (rejected.reasons.empty())
    rejected.reasons.push_back("all profitable call-family exchanges failed proof");
  return rejected;
}

SharedHelperDualModeLayoutResult finalize_shared_helper_dual_mode_layout(
    const SharedHelperDualModePreparation& preparation,
    const std::vector<MachineItem>& placed_items,
    const std::vector<PreloadReport>& placed_preloads,
    const AuthoritativePostLayoutControlFlow& placed_control_flow,
    AddressSpaceModel model) {
  SharedHelperDualModeLayoutResult result;
  result.items = placed_items;
  result.preloads = placed_preloads;
  result.proof.continuation_proved = preparation.continuation.proved;
  result.proof.input_cells = cell_count(placed_items);
  const auto reject = [&](std::string reason) {
    result.proof.reasons.push_back(std::move(reason));
    return result;
  };
  if (preparation.applied <= 0 || !preparation.continuation.proved)
    return reject("dual-mode preparation certificate is absent");
  if (!placed_control_flow.proved)
    return reject("placed artifact lacks authoritative control flow");

  const std::string& marker = preparation.marker;
  const auto helper_root = unique_role_item(placed_items, marker + ":helper-root");
  const auto helper_return = unique_role_item(placed_items, marker + ":helper-return");
  const auto divergent_call =
      unique_role_item(placed_items, marker + ":divergent-call");
  const auto ordinary0_call =
      unique_role_item(placed_items, marker + ":ordinary-0:call");
  const auto ordinary0_join =
      unique_role_item(placed_items, marker + ":ordinary-0:join");
  const auto ordinary0_store =
      unique_role_item(placed_items, marker + ":ordinary-0:store");
  const auto ordinary1_call =
      unique_role_item(placed_items, marker + ":ordinary-1:call");
  const auto ordinary1_join =
      unique_role_item(placed_items, marker + ":ordinary-1:join");
  const auto ordinary1_store =
      unique_role_item(placed_items, marker + ":ordinary-1:store");
  const auto ordinary0_trailing_store =
      unique_role_item(placed_items, marker + ":ordinary-0:trailing-store");
  const auto ordinary1_trailing_store =
      unique_role_item(placed_items, marker + ":ordinary-1:trailing-store");
  const bool shared_trailing_store = preparation.trailing_store_opcode >= 0;
  if (!helper_root || !helper_return || !divergent_call ||
      !ordinary0_call || !ordinary0_join || !ordinary0_store ||
      !ordinary1_call || !ordinary1_join || !ordinary1_store ||
      (shared_trailing_store &&
       (!ordinary0_trailing_store || !ordinary1_trailing_store))) {
    return reject("exact dual-mode command identities did not survive component layout");
  }
  const auto direct_call_operand = [&](std::size_t call_item)
      -> std::optional<std::size_t> {
    if (placed_items.at(call_item).kind != MachineItemKind::Op ||
        placed_items.at(call_item).opcode != 0x53) {
      return std::nullopt;
    }
    const std::optional<std::size_t> operand =
        next_cell_item(placed_items, call_item);
    if (!operand.has_value() ||
        placed_items.at(*operand).kind != MachineItemKind::Address) {
      return std::nullopt;
    }
    return operand;
  };
  const std::optional<std::size_t> divergent_operand =
      direct_call_operand(*divergent_call);
  const std::optional<std::size_t> ordinary0_operand =
      direct_call_operand(*ordinary0_call);
  const std::optional<std::size_t> ordinary1_operand =
      direct_call_operand(*ordinary1_call);
  const bool divergent_indirect =
      is_indirect_call_opcode(placed_items.at(*divergent_call).opcode);
  const bool ordinary0_indirect =
      is_indirect_call_opcode(placed_items.at(*ordinary0_call).opcode);
  const bool ordinary1_indirect =
      is_indirect_call_opcode(placed_items.at(*ordinary1_call).opcode);
  if ((!divergent_operand.has_value() && !divergent_indirect) ||
      (!ordinary0_operand.has_value() && !ordinary0_indirect) ||
      (!ordinary1_operand.has_value() && !ordinary1_indirect)) {
    return reject("dual-mode call identity is neither direct nor a proved indirect call");
  }
  if (placed_items.at(*ordinary0_join).opcode != preparation.continuation.join_opcode ||
      placed_items.at(*ordinary1_join).opcode != preparation.continuation.join_opcode ||
      placed_items.at(*ordinary0_store).opcode != preparation.continuation.store_opcode ||
      placed_items.at(*ordinary1_store).opcode != preparation.continuation.store_opcode ||
      (shared_trailing_store &&
       (placed_items.at(*ordinary0_trailing_store).opcode !=
            preparation.trailing_store_opcode ||
        placed_items.at(*ordinary1_trailing_store).opcode !=
            preparation.trailing_store_opcode))) {
    return reject("ordinary continuation bytes changed during component layout");
  }

  MachineItem shared_join = placed_items.at(*ordinary0_join);
  MachineItem shared_store = placed_items.at(*ordinary0_store);
  erase_transaction_roles(shared_join, marker);
  erase_transaction_roles(shared_store, marker);
  add_role(shared_join, marker + ":shared-join");
  add_role(shared_store, marker + ":shared-store");
  append_comment(shared_join, "shared official helper continuation");
  append_comment(shared_store, "shared official helper continuation");
  std::optional<MachineItem> shared_trailing;
  if (shared_trailing_store) {
    shared_trailing = placed_items.at(*ordinary0_trailing_store);
    erase_transaction_roles(*shared_trailing, marker);
    add_role(*shared_trailing, marker + ":shared-trailing-store");
    append_comment(*shared_trailing, "shared official helper continuation");
  }

  struct DirectFlowIdentity {
    std::size_t source_item = 0;
    std::size_t target_item = 0;
  };
  const ArtifactIndex placed_index = index_artifact(placed_items);
  std::vector<DirectFlowIdentity> direct_flows;
  for (std::size_t source = 0; source < placed_items.size(); ++source) {
    const MachineItem& command = placed_items.at(source);
    if (command.kind != MachineItemKind::Op ||
        !opcode_by_code(command.opcode).takes_address ||
        source == *divergent_call) {
      continue;
    }
    const std::optional<std::size_t> operand = next_cell_item(placed_items, source);
    if (!operand.has_value() ||
        placed_items.at(*operand).kind != MachineItemKind::Address) {
      return reject("direct flow lost its adjacent address operand");
    }
    const MachineItem& address = placed_items.at(*operand);
    std::optional<int> target_address;
    if (address.formal_opcode.has_value()) {
      try {
        target_address = formal_address_info(*address.formal_opcode, model).actual;
      } catch (const std::exception&) {
        return reject("direct flow has an undecodable formal address");
      }
    } else if (const auto* label = std::get_if<std::string>(&address.target)) {
      const auto found = placed_index.label_addresses.find(*label);
      if (found != placed_index.label_addresses.end())
        target_address = found->second;
    } else if (const auto* numeric = std::get_if<int>(&address.target)) {
      target_address = *numeric;
    }
    if (!target_address.has_value() ||
        !placed_index.cells.contains(*target_address)) {
      return reject("direct flow target cannot be identified before relocation");
    }
    direct_flows.push_back({source, placed_index.cells.at(*target_address)});
  }

  std::set<std::size_t> erased{
      *ordinary0_join, *ordinary0_store, *ordinary1_join, *ordinary1_store};
  if (shared_trailing_store) {
    erased.insert(*ordinary0_trailing_store);
    erased.insert(*ordinary1_trailing_store);
  }
  std::vector<MachineItem> rewritten;
  rewritten.reserve(placed_items.size());
  std::map<std::size_t, std::size_t> final_item_by_placed_item;
  const int divergent_formal = formal_side_opcode(
      preparation.required_final_start_address, model);
  if (divergent_formal < 0)
    return reject("no exact B2..F9 alias exists for the divergent helper entry");
  for (std::size_t item_index = 0; item_index < placed_items.size(); ++item_index) {
    if (item_index == *helper_return) {
      const std::size_t final_join = rewritten.size();
      rewritten.push_back(shared_join);
      const std::size_t final_store = rewritten.size();
      rewritten.push_back(shared_store);
      final_item_by_placed_item.emplace(*ordinary0_join, final_join);
      final_item_by_placed_item.emplace(*ordinary1_join, final_join);
      final_item_by_placed_item.emplace(*ordinary0_store, final_store);
      final_item_by_placed_item.emplace(*ordinary1_store, final_store);
      if (shared_trailing_store) {
        const std::size_t final_trailing = rewritten.size();
        rewritten.push_back(*shared_trailing);
        final_item_by_placed_item.emplace(*ordinary0_trailing_store, final_trailing);
        final_item_by_placed_item.emplace(*ordinary1_trailing_store, final_trailing);
      }
    }
    if (erased.contains(item_index))
      continue;
    MachineItem item = placed_items.at(item_index);
    if ((ordinary0_operand.has_value() &&
         item_index == *ordinary0_operand) ||
        (ordinary1_operand.has_value() &&
         item_index == *ordinary1_operand)) {
      // The neutral component layout has already resolved this direct operand
      // for the pre-rewrite address.  The atomic tail merge moves the helper
      // root, so retain the typed target identity and let the final resolver
      // encode its new official address instead of publishing a stale byte.
      item.target = preparation.continuation.helper_label;
      item.formal_opcode.reset();
    }
    if (divergent_operand.has_value() &&
        item_index == *divergent_operand) {
      item.formal_opcode = divergent_formal;
      append_comment(item, "divergent side-space helper entry");
    }
    if (divergent_indirect && item_index == *divergent_call) {
      item.opcode = 0x53;
      item.mnemonic = opcode_by_code(item.opcode).name;
      item.indirect_flow_targets.reset();
      append_comment(item, "direct divergent dual-mode call");
      final_item_by_placed_item.emplace(item_index, rewritten.size());
      rewritten.push_back(std::move(item));
      MachineItem side_entry =
          MachineItem::address(preparation.continuation.helper_label);
      side_entry.formal_opcode = divergent_formal;
      append_comment(side_entry, "divergent side-space helper entry");
      rewritten.push_back(std::move(side_entry));
      continue;
    }
    final_item_by_placed_item.emplace(item_index, rewritten.size());
    rewritten.push_back(std::move(item));
  }

  ArtifactIndex final_index = index_artifact(rewritten);
  std::map<std::size_t, std::size_t> final_item_by_origin;
  for (std::size_t item_index = 0; item_index < rewritten.size(); ++item_index) {
    const std::optional<std::size_t> origin = origin_id(rewritten.at(item_index), marker);
    if (!origin.has_value())
      continue;
    if (!final_item_by_origin.emplace(*origin, item_index).second)
      return reject("dual-mode rewrite duplicated a command identity");
  }

  for (const DirectFlowIdentity& flow : direct_flows) {
    if (!final_item_by_placed_item.contains(flow.source_item) ||
        !final_item_by_placed_item.contains(flow.target_item)) {
      return reject("dual-mode rewrite erased a direct-flow command identity");
    }
    const std::size_t final_source =
        final_item_by_placed_item.at(flow.source_item);
    const std::size_t final_target =
        final_item_by_placed_item.at(flow.target_item);
    const std::optional<std::size_t> final_operand =
        next_cell_item(rewritten, final_source);
    if (!final_operand.has_value() ||
        rewritten.at(*final_operand).kind != MachineItemKind::Address) {
      return reject("relocated direct flow lost its address operand");
    }
    MachineItem& address = rewritten.at(*final_operand);
    const int final_target_address = final_index.addresses.at(final_target);
    const auto labels = final_index.labels_by_address.find(final_target_address);
    if (labels != final_index.labels_by_address.end() && !labels->second.empty())
      address.target = labels->second.front();
    else
      address.target = final_target_address;
    address.formal_opcode.reset();
  }

  struct RebindRequirement {
    int old_target = -1;
    int new_target = -1;
    std::size_t placed_flow_item = 0;
  };
  std::map<int, RebindRequirement> required_by_register;
  for (const auto& [placed_flow_item, targets] :
       placed_control_flow.indirect_flow_targets) {
    if (divergent_indirect && placed_flow_item == *divergent_call)
      continue;
    if (placed_flow_item >= placed_items.size())
      return reject("placed indirect-flow identity is out of range");
    if (!final_item_by_placed_item.contains(placed_flow_item))
      return reject("dual-mode rewrite lost an indirect-flow command identity");
    MachineItem& final_flow_item =
        rewritten.at(final_item_by_placed_item.at(placed_flow_item));
    std::vector<IrTarget> rebound_targets;
    std::vector<std::pair<int, int>> target_moves;
    for (const PostLayoutCommandIdentity& target : targets) {
      if (target.item_index >= placed_items.size())
        return reject("placed indirect target identity is out of range");
      if (!final_item_by_placed_item.contains(target.item_index))
        return reject("dual-mode rewrite erased an indirect-flow target");
      const std::size_t final_target_item =
          final_item_by_placed_item.at(target.item_index);
      const int final_target_address = final_index.addresses.at(final_target_item);
      const auto labels = final_index.labels_by_address.find(final_target_address);
      if (labels != final_index.labels_by_address.end() && !labels->second.empty())
        rebound_targets.emplace_back(labels->second.front());
      else
        rebound_targets.emplace_back(final_target_address);
      target_moves.emplace_back(target.address, final_target_address);
    }
    final_flow_item.indirect_flow_targets = std::move(rebound_targets);

    if (targets.size() != 1U)
      continue;
    const int reg = final_flow_item.opcode & 0x0f;
    const RebindRequirement requirement{
        .old_target = target_moves.front().first,
        .new_target = target_moves.front().second,
        .placed_flow_item = placed_flow_item,
    };
    const auto [existing, inserted] = required_by_register.emplace(reg, requirement);
    if (!inserted &&
        (existing->second.old_target != requirement.old_target ||
         existing->second.new_target != requirement.new_target)) {
      if (!runtime_bound_selector(final_flow_item))
        return reject("one selector register would require incompatible target rebinding");
    }
  }

  for (const auto& [reg, requirement] : required_by_register) {
    if (requirement.old_target == requirement.new_target)
      continue;
    const std::optional<std::size_t> preload =
        preload_for_register(result.preloads, reg);
    if (!final_item_by_placed_item.contains(requirement.placed_flow_item))
      return reject("selector command identity disappeared during rebinding");
    const MachineItem& final_flow_item =
        rewritten.at(final_item_by_placed_item.at(requirement.placed_flow_item));
    if (!preload.has_value()) {
      if (!runtime_bound_selector(final_flow_item))
        return reject("moving indirect target has neither a preload nor a runtime binder");
      continue;
    }
    PreloadReport& report = result.preloads.at(*preload);
    std::optional<std::string> rebound =
        rebind_stable_preloaded_indirect_flow_selector(
            placed_items, report, placed_control_flow,
            requirement.old_target, requirement.new_target, model);
    if (!rebound.has_value()) {
      // A transparent layout bridge is a newly synthesized consumer and has
      // no pre-layout command marker for the stricter command-identity
      // helper.  The authoritative graph above nevertheless proves that all
      // uses of this selector move as one target family.  Retunable natural
      // literals can therefore use their ordinary exact prefix rebinder.
      rebound = rebind_proved_natural_fractional_selector_preload(
          placed_items, report, requirement.old_target,
          requirement.new_target, model);
    }
    if (!rebound.has_value())
      return reject("stable selector preload index " + std::to_string(reg) +
                    " could not be rebound by command identity " +
                    std::to_string(requirement.old_target) + "->" +
                    std::to_string(requirement.new_target));
    if (*rebound != report.value) {
      report.value = *rebound;
      ++result.proof.rebound_preloads;
    }
  }
  result.proof.selector_rebind_proved = true;

  final_index = index_artifact(rewritten);
  const auto root_address = final_index.label_addresses.find(
      preparation.continuation.helper_label);
  if (root_address == final_index.label_addresses.end() ||
      root_address->second != preparation.required_final_start_address) {
    return reject("final helper root does not land on the required F9 suffix start");
  }
  result.proof.body_start_address = root_address->second;
  result.proof.body_end_address = kSideSpaceLastPhysical;
  result.proof.shared_join_address = kOfficialTailStart;
  result.proof.shared_store_address = kOfficialTailStart + 1;
  result.proof.shared_trailing_store_address =
      shared_trailing_store ? kOfficialTailStart + 2 : -1;
  result.proof.official_return_address =
      kOfficialTailStart + 2 + (shared_trailing_store ? 1 : 0);
  for (int address = result.proof.body_start_address;
       address <= result.proof.body_end_address; ++address) {
    const auto cell = final_index.cells.find(address);
    if (cell == final_index.cells.end() ||
        rewritten.at(cell->second).kind != MachineItemKind::Op ||
        !is_straight_body_opcode(rewritten.at(cell->second).opcode)) {
      return reject("final side-space body is not straight-line through F9");
    }
  }
  const auto join_cell = final_index.cells.find(result.proof.shared_join_address);
  const auto store_cell = final_index.cells.find(result.proof.shared_store_address);
  const auto trailing_store_cell =
      shared_trailing_store
          ? final_index.cells.find(result.proof.shared_trailing_store_address)
          : final_index.cells.end();
  const auto return_cell = final_index.cells.find(result.proof.official_return_address);
  const auto zero_cell = final_index.cells.find(0);
  if (join_cell == final_index.cells.end() || store_cell == final_index.cells.end() ||
      return_cell == final_index.cells.end() || zero_cell == final_index.cells.end() ||
      rewritten.at(join_cell->second).opcode != preparation.continuation.join_opcode ||
      rewritten.at(store_cell->second).opcode != preparation.continuation.store_opcode ||
      (shared_trailing_store &&
       (trailing_store_cell == final_index.cells.end() ||
        rewritten.at(trailing_store_cell->second).opcode !=
            preparation.trailing_store_opcode)) ||
      !is_return_opcode(rewritten.at(return_cell->second).opcode) ||
      !is_return_opcode(rewritten.at(zero_cell->second).opcode)) {
    return reject("final dual-mode body/tail/return geometry is not exact");
  }
  result.proof.exact_layout_proved = true;

  const int formal = formal_side_opcode(result.proof.body_start_address, model);
  int ordinary_calls = 0;
  int divergent_calls = 0;
  for (std::size_t item_index = 0; item_index < rewritten.size(); ++item_index) {
    const MachineItem& item = rewritten.at(item_index);
    if (item.kind != MachineItemKind::Op)
      continue;
    if (has_role(item, marker + ":ordinary-0:call") ||
        has_role(item, marker + ":ordinary-1:call")) {
      if (item.opcode == 0x53) {
        const std::optional<std::size_t> operand =
            next_cell_item(rewritten, item_index);
        if (!operand.has_value() ||
            rewritten.at(*operand).kind != MachineItemKind::Address)
          return reject("ordinary direct helper call lost its address operand");
        const MachineItem& address = rewritten.at(*operand);
        std::optional<int> target_address;
        if (const auto* label = std::get_if<std::string>(&address.target)) {
          const auto found = final_index.label_addresses.find(*label);
          if (found != final_index.label_addresses.end())
            target_address = found->second;
        } else if (const auto* numeric = std::get_if<int>(&address.target)) {
          target_address = *numeric;
        }
        if (address.formal_opcode.has_value() || !target_address.has_value() ||
            *target_address != preparation.required_final_start_address) {
          return reject("ordinary direct helper call no longer uses the official entry");
        }
      } else if (is_indirect_call_opcode(item.opcode)) {
        if (!item.indirect_flow_targets.has_value() ||
            item.indirect_flow_targets->size() != 1U) {
          return reject("ordinary indirect helper call lacks one typed target");
        }
        const IrTarget& target = item.indirect_flow_targets->front();
        std::optional<int> target_address;
        if (const auto* label = std::get_if<std::string>(&target)) {
          const auto found = final_index.label_addresses.find(*label);
          if (found != final_index.label_addresses.end())
            target_address = found->second;
        } else if (const auto* numeric = std::get_if<int>(&target)) {
          target_address = *numeric;
        }
        if (!target_address.has_value() ||
            *target_address != preparation.required_final_start_address) {
          return reject("ordinary indirect helper call no longer uses the official entry");
        }
      } else {
        return reject("ordinary helper call changed control-flow kind");
      }
      ++ordinary_calls;
    }
    if (has_role(item, marker + ":divergent-call")) {
      const std::optional<std::size_t> operand = next_cell_item(rewritten, item_index);
      if (!operand.has_value() ||
          rewritten.at(*operand).kind != MachineItemKind::Address)
        return reject("divergent helper call lost its address operand");
      const MachineItem& address = rewritten.at(*operand);
      if (!address.formal_opcode.has_value() || *address.formal_opcode != formal)
        return reject("divergent helper call no longer uses the proved side entry");
      const FormalAddressInfo info = formal_address_info(*address.formal_opcode, model);
      if (!is_side_alias(info) || info.actual != result.proof.body_start_address)
        return reject("divergent formal address does not resolve to the helper body");
      ++divergent_calls;
    }
  }
  if (ordinary_calls != 2 || divergent_calls != 1)
    return reject("final artifact changed the proved two-plus-one call family");
  result.proof.side_entry_proved = true;

  PostLayoutControlFlowOptions flow_options;
  flow_options.address_space_model = model;
  flow_options.empty_return_target = 1;
  result.proof.final_control_flow =
      build_post_layout_control_flow(rewritten, flow_options);
  if (!result.proof.final_control_flow.proved) {
    return reject("final command-identity CFG is not authoritative" +
                  (result.proof.final_control_flow.reasons.empty()
                       ? std::string{}
                       : ": " + result.proof.final_control_flow.reasons.front()));
  }
  result.proof.final_control_flow_proved = true;

  result.proof.output_cells = final_index.cell_count;
  result.proof.removed_cells =
      result.proof.input_cells - result.proof.output_cells;
  const int divergent_conversion_cells = divergent_indirect ? 1 : 0;
  const int expected_removed =
      preparation.continuation.continuation_cells + (shared_trailing_store ? 1 : 0) -
      divergent_conversion_cells;
  if (result.proof.removed_cells != expected_removed ||
      result.proof.removed_cells <= 0) {
    return reject("dual-mode rewrite did not realize the proved net tail saving");
  }

  result.proof.proved = true;
  result.items = std::move(rewritten);
  result.applied = 1;
  result.removed_cells = result.proof.removed_cells;
  result.optimizations.push_back(passes::AppliedOptimization{
      .name = "shared-helper-dual-mode-layout",
      .detail = "Moved two byte-identical commutative/store continuations" +
                std::string(shared_trailing_store ? " plus their identical trailing store"
                                                  : "") +
                " into one "
                "official helper tail and sent the divergent call through the same "
                "straight-line body via its proved F9 side-space entry; removed " +
                std::to_string(result.removed_cells) +
                " cells after selector, stack/X2, and return-stack proofs.",
  });
  return result;
}

} // namespace mkpro::core
