#include "mkpro/core/terminal_retry_sentinel_plan.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mkpro::core {

namespace {

constexpr int kDirectCallOpcode = 0x53;
constexpr int kReturnOpcode = 0x52;
constexpr int kStopOpcode = 0x50;
constexpr int kOrOpcode = 0x38;
constexpr int kSubtractOpcode = 0x11;
constexpr int kDirectXZeroOpcode = 0x5e;
constexpr int kFirstDirectStoreOpcode = 0x40;
constexpr int kLastDirectStoreOpcode = 0x4e;
constexpr int kFirstDirectRecallOpcode = 0x60;
constexpr int kLastDirectRecallOpcode = 0x6e;
constexpr int kFirstIndirectOpcode = 0x70;
constexpr int kLastIndirectOpcode = 0xee;
constexpr int kFirstIndirectJumpOpcode = 0x80;
constexpr int kLastIndirectJumpOpcode = 0x8e;
constexpr int kFirstIndirectCallOpcode = 0xa0;
constexpr int kFirstIndirectXZeroOpcode = 0xe0;
constexpr int kRegisterCount = 15;
constexpr int kSourcePrefixCells = 14;
constexpr int kReplacementPrefixCells = 7;

constexpr std::array<std::string_view, kRegisterCount> kRegisterNames{
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};

struct ArtifactIndex {
  std::vector<int> item_addresses;
  std::vector<std::size_t> cell_items;
  std::map<std::string, int> label_addresses;
  std::map<int, std::vector<std::string>> labels_at_address;
  std::set<std::string> duplicate_labels;
  int cells = 0;
};

struct RegisterAccess {
  std::size_t item_index = 0;
  int address = -1;
  int opcode = -1;
  bool direct = false;
  bool store = false;
  bool recall = false;
  bool indirect = false;
};

ArtifactIndex index_artifact(const std::vector<MachineItem>& items) {
  ArtifactIndex index;
  index.item_addresses.resize(items.size(), 0);
  int address = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    index.item_addresses.at(item_index) = address;
    if (item.kind == MachineItemKind::Label) {
      if (index.label_addresses.contains(item.name))
        index.duplicate_labels.insert(item.name);
      index.label_addresses[item.name] = address;
      index.labels_at_address[address].push_back(item.name);
    } else {
      index.cell_items.push_back(item_index);
      ++address;
    }
  }
  index.cells = address;
  return index;
}

void add_reason(TerminalRetrySentinelPlan& plan, std::string reason) {
  if (std::find(plan.reasons.begin(), plan.reasons.end(), reason) == plan.reasons.end())
    plan.reasons.push_back(std::move(reason));
}

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

std::string normalize_value(std::string value) {
  value = trim_ascii(std::move(value));
  std::replace(value.begin(), value.end(), ',', '.');
  for (char& ch : value) {
    if (ch >= 'A' && ch <= 'Z')
      ch = static_cast<char>(ch - 'A' + 'a');
  }
  return value;
}

std::string register_text(int register_index) {
  if (register_index < 0 || register_index >= kRegisterCount)
    return {};
  return std::string(kRegisterNames.at(static_cast<std::size_t>(register_index)));
}

std::optional<int> direct_store_register(const MachineItem& item) {
  if (item.kind != MachineItemKind::Op || item.opcode < kFirstDirectStoreOpcode ||
      item.opcode > kLastDirectStoreOpcode) {
    return std::nullopt;
  }
  return item.opcode - kFirstDirectStoreOpcode;
}

std::optional<int> direct_recall_register(const MachineItem& item) {
  if (item.kind != MachineItemKind::Op || item.opcode < kFirstDirectRecallOpcode ||
      item.opcode > kLastDirectRecallOpcode) {
    return std::nullopt;
  }
  return item.opcode - kFirstDirectRecallOpcode;
}

std::optional<int> opcode_register(const MachineItem& item) {
  if (const std::optional<int> store = direct_store_register(item))
    return store;
  if (const std::optional<int> recall = direct_recall_register(item))
    return recall;
  if (item.kind != MachineItemKind::Op)
    return std::nullopt;
  if (item.opcode == 0x4f || item.opcode == 0x6f)
    return 0;
  if (item.opcode < kFirstIndirectOpcode || item.opcode > kLastIndirectOpcode)
    return std::nullopt;
  const int low = item.opcode & 0x0f;
  return low == 0x0f ? 0 : low;
}

std::vector<RegisterAccess> collect_register_accesses(const std::vector<MachineItem>& items,
                                                       const ArtifactIndex& index,
                                                       int register_index) {
  std::vector<RegisterAccess> result;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    const std::optional<int> accessed = opcode_register(item);
    if (!accessed.has_value() || *accessed != register_index)
      continue;
    const bool direct_store = direct_store_register(item) == accessed;
    const bool direct_recall = direct_recall_register(item) == accessed;
    result.push_back(RegisterAccess{
        .item_index = item_index,
        .address = index.item_addresses.at(item_index),
        .opcode = item.opcode,
        .direct = direct_store || direct_recall,
        .store = direct_store,
        .recall = direct_recall,
        .indirect = !direct_store && !direct_recall,
    });
  }
  return result;
}

TerminalRetryCellRef cell_ref(const ArtifactIndex& index, std::size_t item_index) {
  return TerminalRetryCellRef{
      .item_index = item_index,
      .address = index.item_addresses.at(item_index),
  };
}

std::optional<std::size_t> cell_item_at(const ArtifactIndex& index, int address) {
  if (address < 0 || address >= static_cast<int>(index.cell_items.size()))
    return std::nullopt;
  return index.cell_items.at(static_cast<std::size_t>(address));
}

std::optional<std::string> address_label(const MachineItem& item) {
  if (item.kind != MachineItemKind::Address || !std::holds_alternative<std::string>(item.target))
    return std::nullopt;
  return std::get<std::string>(item.target);
}

std::vector<std::size_t> preload_indices_for_register(const std::vector<PreloadReport>& preloads,
                                                       int register_index) {
  std::vector<std::size_t> result;
  const std::string wanted = register_text(register_index);
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    std::string candidate = preloads.at(index).register_name;
    if (candidate.size() == 2U && (candidate.front() == 'R' || candidate.front() == 'r'))
      candidate.erase(candidate.begin());
    for (char& ch : candidate) {
      if (ch >= 'A' && ch <= 'E')
        ch = static_cast<char>(ch - 'A' + 'a');
    }
    if (candidate == wanted)
      result.push_back(index);
  }
  return result;
}

std::optional<int> preload_flow_target(const PreloadReport& preload, int stable_register) {
  try {
    const std::optional<IndirectAddressEvaluation> evaluated = evaluate_indirect_address(
        register_text(stable_register), preload.value, IndirectOperationKind::Flow);
    if (!evaluated.has_value() || !evaluated->actual_flow_target.has_value())
      return std::nullopt;
    return *evaluated->actual_flow_target;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool admitted_inputs_are_unique(const std::vector<int>& values) {
  if (values.empty())
    return false;
  std::set<int> unique(values.begin(), values.end());
  return unique.size() == values.size();
}

bool admitted_inputs_stay_in_manual_header(const std::vector<int>& values,
                                           int selector_register) {
  if (!admitted_inputs_are_unique(values) || selector_register < 0 ||
      selector_register > 3) {
    return false;
  }
  for (const int value : values) {
    const std::optional<IndirectAddressEvaluation> evaluated =
        evaluate_indirect_address(register_text(selector_register), std::to_string(value),
                                  IndirectOperationKind::Flow);
    if (!evaluated.has_value() || !evaluated->actual_flow_target.has_value() ||
        !evaluated->result_value.has_value() || *evaluated->actual_flow_target < 0 ||
        *evaluated->actual_flow_target > 3 ||
        *evaluated->result_value != std::to_string(value - 1)) {
      return false;
    }
  }
  return true;
}

TerminalRetryRegisterAccessRef remap_access_ref(const RegisterAccess& access) {
  return TerminalRetryRegisterAccessRef{
      .cell = TerminalRetryCellRef{.item_index = access.item_index, .address = access.address},
      .opcode = access.opcode,
  };
}

bool is_stable_register(int register_index) {
  return register_index >= 7 && register_index < kRegisterCount;
}

MachineItem role_op(int opcode, std::string role) {
  MachineItem item = MachineItem::op(opcode, opcode_by_code(opcode).name);
  item.roles.push_back(std::move(role));
  return item;
}

} // namespace

TerminalRetrySentinelPlan discover_terminal_retry_sentinel_plan(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const std::map<std::string, std::string>& logical_registers,
    const TerminalRetrySentinelDiscoveryOptions& options) {
  TerminalRetrySentinelPlan plan;
  const ArtifactIndex index = index_artifact(items);
  plan.input_cells = index.cells;
  plan.source_prefix_cells = kSourcePrefixCells;
  plan.replacement_prefix_cells = kReplacementPrefixCells;
  plan.projected_delta = kReplacementPrefixCells - kSourcePrefixCells;
  plan.projected_cells = index.cells + plan.projected_delta;

  if (!options.manual_pp_then_run_proved)
    add_reason(plan, "manual PP-then-run entry protocol is not proved");
  if (!admitted_inputs_are_unique(options.admitted_low_inputs))
    add_reason(plan, "low input domain is empty or contains duplicate values");
  if (index.cells < kSourcePrefixCells)
    add_reason(plan, "artifact is shorter than the retry prefix");
  if (!index.duplicate_labels.empty())
    add_reason(plan, "artifact contains duplicate labels");

  std::array<std::optional<std::size_t>, kSourcePrefixCells> prefix{};
  for (int address = 0; address < kSourcePrefixCells; ++address)
    prefix.at(static_cast<std::size_t>(address)) = cell_item_at(index, address);
  if (std::any_of(prefix.begin(), prefix.end(), [](const auto& item) { return !item.has_value(); })) {
    add_reason(plan, "retry prefix is not physically contiguous at 00..13");
    return plan;
  }
  const auto item_at = [&](int address) -> const MachineItem& {
    return items.at(*prefix.at(static_cast<std::size_t>(address)));
  };
  const auto item_index_at = [&](int address) -> std::size_t {
    return *prefix.at(static_cast<std::size_t>(address));
  };

  const auto header_labels = index.labels_at_address.find(0);
  if (header_labels == index.labels_at_address.end() || header_labels->second.size() != 1U) {
    add_reason(plan, "retry header at physical 00 does not have one unique label");
  } else {
    plan.header_label = header_labels->second.front();
  }
  for (int address = 1; address < kSourcePrefixCells; ++address) {
    if (index.labels_at_address.contains(address))
      add_reason(plan, "retry prefix has an externally enterable internal label");
  }

  const std::optional<int> display_register = direct_recall_register(item_at(0));
  const std::optional<int> first_input_register = direct_store_register(item_at(2));
  const std::optional<int> second_input_register = direct_store_register(item_at(3));
  const std::optional<int> occupied_register = direct_store_register(item_at(7));
  const std::optional<int> sentinel_register = direct_recall_register(item_at(11));
  const std::optional<int> sentinel_store_register = direct_store_register(item_at(12));

  if (!display_register.has_value() || item_at(1).kind != MachineItemKind::Op ||
      item_at(1).opcode != kStopOpcode || !first_input_register.has_value() ||
      !second_input_register.has_value() || *first_input_register == *second_input_register) {
    add_reason(plan, "manual prompt is not recall; STOP; two distinct entered stores");
  }
  if (item_at(4).kind != MachineItemKind::Op || item_at(4).opcode != kDirectCallOpcode ||
      !address_label(item_at(5)).has_value()) {
    add_reason(plan, "local entry is not a direct helper call with a symbolic operand");
  }
  if (item_at(6).kind != MachineItemKind::Op || item_at(6).opcode != kOrOpcode ||
      !occupied_register.has_value() || item_at(8).kind != MachineItemKind::Op ||
      item_at(8).opcode != kSubtractOpcode) {
    add_reason(plan, "occupied retry prefix is not OR; store; subtract");
  }
  if (item_at(9).kind != MachineItemKind::Op || item_at(9).opcode != kDirectXZeroOpcode ||
      !address_label(item_at(10)).has_value()) {
    add_reason(plan, "changed-value branch is not a direct x=0 branch");
  }
  if (!sentinel_register.has_value() || !sentinel_store_register.has_value() ||
      !display_register.has_value() || *sentinel_store_register != *display_register ||
      !is_stable_register(*sentinel_register)) {
    add_reason(plan, "retry arm does not recall one stable sentinel and store it to display state");
  }
  if (item_at(13).kind != MachineItemKind::Op ||
      item_at(13).opcode < kFirstIndirectJumpOpcode ||
      item_at(13).opcode > kLastIndirectJumpOpcode) {
    add_reason(plan, "retry arm does not end in one indirect jump");
  }

  const std::optional<std::string> helper_label = address_label(item_at(5));
  const std::optional<std::string> success_label = address_label(item_at(10));
  if (helper_label.has_value())
    plan.helper_label = *helper_label;
  if (success_label.has_value())
    plan.success_label = *success_label;
  if (success_label.has_value()) {
    const auto found = index.label_addresses.find(*success_label);
    if (found == index.label_addresses.end() || found->second != kSourcePrefixCells)
      add_reason(plan, "changed-value success edge does not land immediately after retry arm");
  }

  if (display_register.has_value())
    plan.display_register = *display_register;
  if (first_input_register.has_value())
    plan.first_input_register = *first_input_register;
  if (second_input_register.has_value())
    plan.second_input_register = *second_input_register;
  if (occupied_register.has_value())
    plan.occupied_register = *occupied_register;
  if (sentinel_register.has_value())
    plan.sentinel_register = *sentinel_register;
  plan.retry_flow_register = item_at(13).opcode & 0x0f;

  plan.header_recall = cell_ref(index, item_index_at(0));
  plan.prompt_stop = cell_ref(index, item_index_at(1));
  plan.first_input_store = cell_ref(index, item_index_at(2));
  plan.second_input_store = cell_ref(index, item_index_at(3));
  plan.local_helper_call = TerminalRetryCallRef{
      .call = cell_ref(index, item_index_at(4)),
      .operand = cell_ref(index, item_index_at(5)),
  };
  plan.occupied_join = cell_ref(index, item_index_at(6));
  plan.occupied_store = cell_ref(index, item_index_at(7));
  plan.changed_subtract = cell_ref(index, item_index_at(8));
  plan.changed_branch = TerminalRetryCallRef{
      .call = cell_ref(index, item_index_at(9)),
      .operand = cell_ref(index, item_index_at(10)),
  };
  plan.sentinel_recall = cell_ref(index, item_index_at(11));
  plan.sentinel_store = cell_ref(index, item_index_at(12));
  plan.retry_flow = cell_ref(index, item_index_at(13));
  if (success_label.has_value()) {
    const auto success_item = index.label_addresses.find(*success_label);
    if (success_item != index.label_addresses.end()) {
      const std::optional<std::size_t> success_cell = cell_item_at(index, success_item->second);
      if (success_cell.has_value())
        plan.success_entry = cell_ref(index, *success_cell);
    }
  }

  if (!plan.header_label.empty()) {
    for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
      if (item_index >= item_index_at(0) && item_index <= item_index_at(13))
        continue;
      const std::optional<std::string> target = address_label(items.at(item_index));
      if (!target.has_value())
        continue;
      const auto target_address = index.label_addresses.find(*target);
      if (target_address != index.label_addresses.end() && target_address->second > 0 &&
          target_address->second < kSourcePrefixCells) {
        add_reason(plan, "external flow enters the removable retry prefix");
      }
    }
  }

  if (!plan.helper_label.empty()) {
    for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
      const std::optional<std::string> target = address_label(items.at(item_index));
      if (!target.has_value() || *target != plan.helper_label)
        continue;
      if (item_index == 0 || items.at(item_index - 1U).kind != MachineItemKind::Op ||
          items.at(item_index - 1U).opcode != kDirectCallOpcode) {
        add_reason(plan, "helper target has a non-call reference");
        continue;
      }
      plan.complete_helper_calls.push_back(TerminalRetryCallRef{
          .call = cell_ref(index, item_index - 1U),
          .operand = cell_ref(index, item_index),
      });
    }
    if (plan.complete_helper_calls.empty())
      add_reason(plan, "helper has no complete direct-call set");
  }

  if (sentinel_register.has_value()) {
    const std::vector<std::size_t> sentinel_preloads =
        preload_indices_for_register(preloads, *sentinel_register);
    if (sentinel_preloads.size() != 1U) {
      add_reason(plan, "sentinel register does not have one exact -99999999 preload");
    } else if (normalize_value(preloads.at(sentinel_preloads.front()).value) != "-99999999") {
      add_reason(plan, "sentinel register does not have one exact -99999999 preload");
    } else {
      plan.removed_sentinel_preload_index = sentinel_preloads.front();
      plan.removed_sentinel_preload_value = preloads.at(sentinel_preloads.front()).value;
    }
    const std::vector<RegisterAccess> sentinel_accesses =
        collect_register_accesses(items, index, *sentinel_register);
    if (sentinel_accesses.size() != 1U ||
        sentinel_accesses.front().item_index != item_index_at(11) ||
        !sentinel_accesses.front().recall) {
      add_reason(plan, "sentinel register has an extra use outside the removable retry arm");
    }
  }

  const auto retry_targets = options.proved_indirect_flow_targets.find(item_index_at(13));
  if (retry_targets == options.proved_indirect_flow_targets.end() ||
      retry_targets->second != std::vector<int>({0})) {
    add_reason(plan, "source retry jump lacks a complete sole-target header proof");
  }

  int low_slot = -1;
  if (first_input_register.has_value() && second_input_register.has_value()) {
    for (int candidate = 3; candidate >= 0; --candidate) {
      if (candidate == *first_input_register || candidate == *second_input_register)
        continue;
      if (!preload_indices_for_register(preloads, candidate).empty())
        continue;
      const std::vector<RegisterAccess> accesses = collect_register_accesses(items, index, candidate);
      if (accesses.empty())
        continue;
      const auto before_success = std::find_if(accesses.begin(), accesses.end(), [](const auto& a) {
        return a.address < kSourcePrefixCells;
      });
      if (before_success != accesses.end())
        continue;
      if (!accesses.front().direct || !accesses.front().store)
        continue;
      if (low_slot != -1) {
        add_reason(plan, "more than one dead low input/slot register satisfies the proof");
        low_slot = -2;
        break;
      }
      low_slot = candidate;
    }
  }
  if (low_slot < 0) {
    if (low_slot != -2)
      add_reason(plan, "no dead low input/slot register has a first dominating direct store");
  } else {
    plan.low_input_slot_register = low_slot;
    if (!admitted_inputs_stay_in_manual_header(options.admitted_low_inputs, low_slot)) {
      add_reason(plan,
                 "a proved low input leaves the four-cell manual return header after "
                 "indirect predecrement");
    }
  }

  int constant_register = -1;
  std::size_t constant_preload_index = 0;
  int constant_target = -1;
  if (low_slot >= 0 && first_input_register.has_value() && second_input_register.has_value()) {
    for (int candidate = 0; candidate <= 3; ++candidate) {
      if (candidate == low_slot || candidate == *first_input_register ||
          candidate == *second_input_register)
        continue;
      const std::vector<std::size_t> candidate_preloads =
          preload_indices_for_register(preloads, candidate);
      if (candidate_preloads.size() != 1U)
        continue;
      const std::vector<RegisterAccess> accesses = collect_register_accesses(items, index, candidate);
      if (accesses.empty() ||
          !std::all_of(accesses.begin(), accesses.end(), [](const RegisterAccess& access) {
            return access.direct && access.recall && !access.store;
          })) {
        continue;
      }
      if (!sentinel_register.has_value())
        continue;
      const std::optional<int> target =
          preload_flow_target(preloads.at(candidate_preloads.front()), *sentinel_register);
      if (!target.has_value() || *target <= 0)
        continue;
      if (constant_register != -1) {
        add_reason(plan, "more than one immutable low constant can be rehomed");
        constant_register = -2;
        break;
      }
      constant_register = candidate;
      constant_preload_index = candidate_preloads.front();
      constant_target = *target;
    }
  }
  if (constant_register < 0) {
    if (constant_register != -2)
      add_reason(plan, "no unique immutable low constant can be rehomed to the sentinel register");
  } else {
    plan.immutable_constant_register = constant_register;
    plan.rehomed_constant_flow_target = constant_target;
  }

  const auto logical_owner_count = [&](int physical_register) {
    return static_cast<int>(std::count_if(
        logical_registers.begin(), logical_registers.end(), [&](const auto& entry) {
          try {
            return register_index(entry.second) == physical_register;
          } catch (const std::exception&) {
            return false;
          }
        }));
  };
  if (constant_register >= 0 && logical_owner_count(constant_register) != 0)
    add_reason(plan, "immutable constant carrier also owns logical state");
  if (sentinel_register.has_value() && logical_owner_count(*sentinel_register) != 0)
    add_reason(plan, "sentinel carrier also owns logical state");

  int dynamic_register = -1;
  if (constant_register >= 0 && sentinel_register.has_value()) {
    for (int candidate = 7; candidate < kRegisterCount; ++candidate) {
      if (candidate == *sentinel_register || candidate == plan.display_register ||
          candidate == plan.occupied_register || candidate == plan.retry_flow_register)
        continue;
      if (!preload_indices_for_register(preloads, candidate).empty())
        continue;
      const std::vector<RegisterAccess> accesses = collect_register_accesses(items, index, candidate);
      if (accesses.empty() ||
          !std::all_of(accesses.begin(), accesses.end(), [](const RegisterAccess& access) {
            return access.direct;
          }) ||
          !std::any_of(accesses.begin(), accesses.end(), [](const RegisterAccess& access) {
            return access.store;
          }) ||
          !std::any_of(accesses.begin(), accesses.end(), [](const RegisterAccess& access) {
            return access.recall;
          })) {
        continue;
      }
      if (dynamic_register != -1) {
        add_reason(plan, "more than one direct-only stable dynamic register can be rehomed");
        dynamic_register = -2;
        break;
      }
      dynamic_register = candidate;
    }
  }
  if (dynamic_register < 0) {
    if (dynamic_register != -2)
      add_reason(plan, "no unique direct-only stable dynamic register can move to the low carrier");
  } else {
    plan.direct_only_dynamic_register = dynamic_register;
    plan.free_stable_raw_seed_register = dynamic_register;
  }

  if (constant_register >= 0 && sentinel_register.has_value()) {
    const std::vector<RegisterAccess> accesses =
        collect_register_accesses(items, index, constant_register);
    TerminalRetryRegisterRemap remap{
        .from_register = constant_register,
        .to_register = *sentinel_register,
        .moves_preload = true,
        .preload_index = constant_preload_index,
        .preload_value = preloads.at(constant_preload_index).value,
        .accesses = {},
    };
    for (const RegisterAccess& access : accesses)
      remap.accesses.push_back(remap_access_ref(access));
    plan.register_remaps.push_back(std::move(remap));
  }
  if (dynamic_register >= 0 && constant_register >= 0) {
    const std::vector<RegisterAccess> accesses =
        collect_register_accesses(items, index, dynamic_register);
    TerminalRetryRegisterRemap remap{
        .from_register = dynamic_register,
        .to_register = constant_register,
        .moves_preload = false,
        .preload_index = 0,
        .preload_value = {},
        .accesses = {},
    };
    for (const RegisterAccess& access : accesses)
      remap.accesses.push_back(remap_access_ref(access));
    plan.register_remaps.push_back(std::move(remap));
  }

  plan.projected_logical_registers = logical_registers;
  if (dynamic_register >= 0 && constant_register >= 0) {
    for (auto& [name, physical] : plan.projected_logical_registers) {
      (void)name;
      try {
        if (register_index(physical) == dynamic_register)
          physical = register_text(constant_register);
      } catch (const std::exception&) {
        add_reason(plan, "logical register map contains an invalid physical register");
      }
    }
  }

  for (int address = kSourcePrefixCells; address < index.cells; ++address)
    plan.address_reindex.emplace_back(address, address + plan.projected_delta);

  if (second_input_register.has_value() && low_slot >= 0 && sentinel_register.has_value()) {
    plan.replacement_prefix = {
        role_op(kReturnOpcode, "terminal-retry:bare-return-zero"),
        role_op(kFirstDirectRecallOpcode + *second_input_register,
                "terminal-retry:prompt-second-input-recall"),
        role_op(kFirstDirectRecallOpcode + low_slot,
                "terminal-retry:prompt-low-slot-recall"),
        role_op(kStopOpcode, "terminal-retry:prompt-stop"),
        role_op(kFirstDirectStoreOpcode + low_slot, "terminal-retry:first-input-store"),
        role_op(kFirstIndirectCallOpcode + *sentinel_register,
                "terminal-retry:planned-helper-call"),
        role_op(kFirstIndirectXZeroOpcode + low_slot,
                "terminal-retry:low-selector-conditional"),
    };
  }

  plan.bare_return_zero_proved = plan.replacement_prefix.size() == 7U &&
                                  plan.replacement_prefix.front().opcode == kReturnOpcode;
  plan.manual_pp_then_run_proved = options.manual_pp_then_run_proved;
  plan.target_prompt_stop_address = 3;
  plan.target_first_input_store_address = 4;
  plan.target_helper_call_address = 5;
  plan.target_helper_entry_address = constant_target;
  plan.target_retry_conditional_address = 6;
  plan.admitted_low_inputs = options.admitted_low_inputs;
  std::sort(plan.admitted_low_inputs.begin(), plan.admitted_low_inputs.end());

  plan.proved = plan.reasons.empty();
  return plan;
}

} // namespace mkpro::core
