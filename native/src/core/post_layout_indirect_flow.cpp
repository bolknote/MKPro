#include "mkpro/core/post_layout_indirect_flow.hpp"

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/passes/preloaded_indirect_flow.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core {

namespace {

constexpr int kMaxRewrites = 64;
constexpr std::array<std::string_view, 8> kStableRegisters = {"7", "8", "9", "a",
                                                              "b", "c", "d", "e"};
constexpr std::array<int, 10> kAddressTakingOpcodes = {
    0x51, 0x53, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e,
};

bool trace_post_layout_enabled() {
  static const bool enabled = std::getenv("MKPRO_NATIVE_TRACE_POST_LAYOUT") != nullptr;
  return enabled;
}

AddressSpaceModel address_space_model_for_options(const CompileOptions& options) {
  return address_space_model_for_feature_profile(effective_optimizer_feature_profile(options));
}

struct OverlayExecutable {
  int opcode = 0;
  std::string mnemonic;
};

// The address-code overlay folds an executable cell into a branch operand byte,
// deleting exactly one cell. `removed_cell_address` is the pre-overlay address
// of the executable that disappeared; `overlaid_cell_address` is the pre-overlay
// address of the branch operand that now also executes that opcode. Selectors
// that targeted a shifted cell must be retargeted with this mapping so preloaded
// indirect flow keeps reaching the right helper entry (DEFECT 4).
struct AddressCodeOverlayApplication {
  std::vector<MachineItem> items;
  int removed_cell_address = 0;
  int overlaid_cell_address = 0;
};

struct MachineLayout {
  std::map<std::string, int> labels;
  std::map<int, int> item_index_by_address;
  std::map<int, int> address_by_item_index;
};

struct MachineCell {
  int address = 0;
  int item_index = 0;
  const MachineItem* item = nullptr;
};

std::optional<int> known_indirect_flow_target_comment(const std::optional<std::string>& comment,
                                                      AddressSpaceModel model);

struct StopTailReuseBase {
  std::string register_name;
  int target = 0;
  int continuation_opcode = 0;
};

struct StopTailReuseRewrite {
  StopTailReuseBase base;
  int replace_index = 0;
  int remove_index = 0;
  bool zero_prefixed = false;
};

struct EmptyStackTailCallRewrite {
  int call_index = 0;
  int call_address_index = 0;
  int loop_back_index = 0;
  std::optional<int> loop_back_address_index;
  // The call, its operand, and the loop-back can all disappear when the
  // selected callee is already the next physical component.
  bool natural_fallthrough = false;
  std::string mnemonic;
  std::string comment;
  std::optional<int> source_line;
};

struct BranchRewrite {
  int branch_index = 0;
  int address_index = 0;
  int opcode = 0;
  std::string mnemonic;
  std::string comment;
  std::optional<int> source_line;
  IrTarget target = 0;
};

struct RetargetedMachine {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
};

struct RetargetedIr {
  std::vector<IrOp> ops;
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
};

struct NumericTargetView {
  std::vector<IrOp> numeric;
  std::vector<std::optional<std::string>> target_labels;
};

struct SelectorValue {
  std::string value;
  bool existing = false;
};

struct RewriteStep {
  std::vector<IrOp> ops;
  std::vector<MachineItem> items;
  PreloadReport preload;
  bool super_dark = false;
  bool dark_entry = false;
  std::vector<int> converted_addresses;
  std::vector<int> protected_targets;
  bool existing_preload = false;
  bool borrowed_entry_phase = false;
  int converted = 0;
};

struct MergeDuplicateSelectorsResult {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  int merged = 0;
};

bool is_address_taking_opcode(int opcode) {
  return std::find(kAddressTakingOpcodes.begin(), kAddressTakingOpcodes.end(), opcode) !=
         kAddressTakingOpcodes.end();
}

bool is_number_entry_opcode(int opcode) {
  return (opcode >= 0x00 && opcode <= 0x09) || opcode == 0x0a || opcode == 0x0b || opcode == 0x0c;
}

std::map<std::string, int> machine_label_addresses(const std::vector<MachineItem>& items) {
  std::map<std::string, int> labels;
  int address = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label) {
      labels[item.name] = address;
    } else {
      ++address;
    }
  }
  return labels;
}

std::map<int, int> machine_address_by_item_index(const std::vector<MachineItem>& items) {
  std::map<int, int> addresses;
  int address = 0;
  for (int index = 0; index < static_cast<int>(items.size()); ++index) {
    if (items.at(static_cast<std::size_t>(index)).kind == MachineItemKind::Label)
      continue;
    addresses[index] = address;
    ++address;
  }
  return addresses;
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
    layout.address_by_item_index[index] = address;
    layout.item_index_by_address[address] = index;
    ++address;
  }
  return layout;
}

std::vector<MachineCell> machine_cells(const std::vector<MachineItem>& items) {
  std::vector<MachineCell> cells;
  int address = 0;
  for (int index = 0; index < static_cast<int>(items.size()); ++index) {
    const MachineItem& item = items.at(static_cast<std::size_t>(index));
    if (item.kind == MachineItemKind::Label)
      continue;
    cells.push_back(MachineCell{
        .address = address,
        .item_index = index,
        .item = &item,
    });
    ++address;
  }
  return cells;
}

std::optional<MachineCell> machine_cell_at(const std::vector<MachineCell>& cells, int address) {
  for (const MachineCell& cell : cells) {
    if (cell.address == address)
      return cell;
  }
  return std::nullopt;
}

std::map<int, std::vector<std::string>>
machine_labels_by_address(const std::vector<MachineItem>& items) {
  std::map<int, std::vector<std::string>> labels;
  int address = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label) {
      labels[address].push_back(item.name);
    } else {
      ++address;
    }
  }
  return labels;
}

std::set<std::string> referenced_machine_labels(const std::vector<MachineItem>& items) {
  std::set<std::string> referenced;
  for (const MachineItem& item : items) {
    if (item.kind != MachineItemKind::Address)
      continue;
    if (const auto* label = std::get_if<std::string>(&item.target))
      referenced.insert(*label);
  }
  return referenced;
}

std::set<std::string> referenced_machine_entry_labels(
    const std::vector<MachineItem>& items) {
  std::set<std::string> referenced = referenced_machine_labels(items);
  for (const MachineItem& item : items) {
    if (!item.indirect_flow_targets.has_value())
      continue;
    for (const IrTarget& target : *item.indirect_flow_targets) {
      if (const auto* label = std::get_if<std::string>(&target))
        referenced.insert(*label);
    }
  }
  return referenced;
}

bool has_machine_role(const MachineItem& item, std::string_view role) {
  return std::find(item.roles.begin(), item.roles.end(), role) != item.roles.end();
}

bool address_has_referenced_label(const std::map<int, std::vector<std::string>>& labels_by_address,
                                  const std::set<std::string>& referenced_labels, int address) {
  const auto labels_it = labels_by_address.find(address);
  if (labels_it == labels_by_address.end())
    return false;
  for (const std::string& label : labels_it->second) {
    if (referenced_labels.contains(label))
      return true;
  }
  return false;
}

std::optional<int> resolved_machine_target(const IrTarget& target,
                                           const std::map<std::string, int>& labels) {
  if (const auto* address = std::get_if<int>(&target))
    return *address;
  const auto* label = std::get_if<std::string>(&target);
  if (label == nullptr)
    return std::nullopt;
  const auto it = labels.find(*label);
  if (it == labels.end())
    return std::nullopt;
  return it->second;
}

std::optional<int> fixed_address_actual_target(const MachineItem& address,
                                               AddressSpaceModel model) {
  if (address.kind != MachineItemKind::Address)
    return std::nullopt;
  if (address.formal_opcode.has_value())
    return formal_address_info(*address.formal_opcode, model).actual;
  if (const auto* numeric = std::get_if<int>(&address.target))
    return *numeric;
  return std::nullopt;
}

bool fixed_address_targets_survive_removal(const std::vector<MachineItem>& items,
                                           int removed_address, AddressSpaceModel model) {
  for (const MachineItem& item : items) {
    if (item.kind != MachineItemKind::Address)
      continue;
    const std::optional<int> fixed_target = fixed_address_actual_target(item, model);
    if (fixed_target.has_value() && *fixed_target >= removed_address)
      return false;
  }
  return true;
}

bool fixed_indirect_flow_targets_survive_removal(const std::vector<MachineItem>& items,
                                                 int removed_address) {
  for (const MachineItem& item : items) {
    if (!item.indirect_flow_targets.has_value())
      continue;
    for (const IrTarget& target : *item.indirect_flow_targets) {
      const auto* numeric = std::get_if<int>(&target);
      if (numeric != nullptr && *numeric >= removed_address)
        return false;
    }
  }
  return true;
}

std::optional<int> address_opcode_for_item(const std::vector<MachineItem>& items,
                                           const MachineItem& item, AddressSpaceModel model) {
  if (item.kind != MachineItemKind::Address)
    return std::nullopt;
  if (item.formal_opcode.has_value())
    return *item.formal_opcode;

  const std::map<std::string, int> labels = machine_label_addresses(items);
  const std::optional<int> target = resolved_machine_target(item.target, labels);
  if (!target.has_value())
    return std::nullopt;
  try {
    return official_address_to_opcode(*target, model);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<IrTarget> direct_address_target(const std::vector<MachineItem>& items,
                                              int item_index) {
  const int address_index = item_index + 1;
  if (address_index < 0 || address_index >= static_cast<int>(items.size()))
    return std::nullopt;
  const MachineItem& address = items.at(static_cast<std::size_t>(address_index));
  if (address.kind != MachineItemKind::Address)
    return std::nullopt;
  return address.target;
}

struct MachineReturnAnalyzer {
  const std::vector<MachineItem>& items;
  MachineLayout layout;
  AddressSpaceModel model = AddressSpaceModel::Standard;
  std::set<int> visiting;
  std::map<int, bool> memo;

  bool may_return_from(int address) {
    const auto memo_it = memo.find(address);
    if (memo_it != memo.end())
      return memo_it->second;
    if (visiting.contains(address))
      return false;
    const auto index_it = layout.item_index_by_address.find(address);
    if (index_it == layout.item_index_by_address.end())
      return false;

    const int item_index = index_it->second;
    const MachineItem& item = items.at(static_cast<std::size_t>(item_index));
    std::optional<int> opcode;
    if (item.kind == MachineItemKind::Op) {
      opcode = item.opcode;
    } else if (item.kind == MachineItemKind::Address) {
      opcode = address_opcode_for_item(items, item, model);
    }
    if (!opcode.has_value())
      return true;

    visiting.insert(address);
    bool result = true;
    if (*opcode == 0x52) {
      result = true;
    } else if (*opcode == 0x29 &&
               item.stop_disposition == StopDisposition::Terminal) {
      result = false;
    } else if (*opcode == 0x29 &&
               item.stop_disposition == StopDisposition::Resumable) {
      result = may_return_from(address + 2);
    } else if (*opcode == 0x50) {
      result = false;
    } else if (*opcode == 0x51) {
      const std::optional<IrTarget> target = direct_address_target(items, item_index);
      const std::optional<int> target_address =
          target.has_value() ? resolved_machine_target(*target, layout.labels) : std::nullopt;
      result = target_address.has_value() ? may_return_from(*target_address) : true;
    } else if (*opcode == 0x53) {
      const std::optional<IrTarget> target = direct_address_target(items, item_index);
      const std::optional<int> target_address =
          target.has_value() ? resolved_machine_target(*target, layout.labels) : std::nullopt;
      result = target_address.has_value()
                   ? (may_return_from(*target_address) ? may_return_from(address + 2) : false)
                   : true;
    } else if (is_address_taking_opcode(*opcode)) {
      const std::optional<IrTarget> target = direct_address_target(items, item_index);
      const std::optional<int> target_address =
          target.has_value() ? resolved_machine_target(*target, layout.labels) : std::nullopt;
      result = target_address.has_value()
                   ? (may_return_from(*target_address) || may_return_from(address + 2))
                   : true;
    } else if (*opcode >= 0x80 && *opcode <= 0xee) {
      result = true;
    } else {
      result = may_return_from(address + 1);
    }
    visiting.erase(address);
    memo[address] = result;
    return result;
  }

  bool operator()(const IrTarget& target) {
    const std::optional<int> target_address = resolved_machine_target(target, layout.labels);
    return target_address.has_value() ? may_return_from(*target_address) : true;
  }
};

bool can_overlay_address_continuation(MachineReturnAnalyzer& target_may_return,
                                      const MachineItem& branch, const MachineItem& address) {
  if (branch.opcode == 0x51)
    return true;
  if (branch.opcode == 0x53)
    return !target_may_return(address.target);
  return false;
}

std::optional<int> previous_non_label_index(const std::vector<MachineItem>& items, int before) {
  for (int index = before - 1; index >= 0; --index) {
    if (items.at(static_cast<std::size_t>(index)).kind != MachineItemKind::Label)
      return index;
  }
  return std::nullopt;
}

bool labels_have_no_linear_fallthrough(const std::vector<MachineItem>& items, int labels_start,
                                       MachineReturnAnalyzer& target_may_return) {
  const std::optional<int> previous_index = previous_non_label_index(items, labels_start);
  if (!previous_index.has_value())
    return false;
  const MachineItem& previous = items.at(static_cast<std::size_t>(*previous_index));
  if (previous.kind == MachineItemKind::Op)
    return previous.opcode == 0x50 || previous.opcode == 0x52;
  if (previous.kind != MachineItemKind::Address)
    return false;

  const std::optional<int> branch_index = previous_non_label_index(items, *previous_index);
  if (!branch_index.has_value())
    return false;
  const MachineItem& branch = items.at(static_cast<std::size_t>(*branch_index));
  if (branch.kind != MachineItemKind::Op)
    return false;
  if (branch.opcode == 0x51)
    return true;
  if (branch.opcode == 0x53)
    return !target_may_return(previous.target);
  return false;
}

std::optional<OverlayExecutable> overlay_executable_at(const std::vector<MachineItem>& items,
                                                       int index, AddressSpaceModel model) {
  if (index < 0 || index >= static_cast<int>(items.size()))
    return std::nullopt;
  const MachineItem& item = items.at(static_cast<std::size_t>(index));
  if (item.kind == MachineItemKind::Op) {
    return OverlayExecutable{.opcode = item.opcode, .mnemonic = item.mnemonic};
  }
  if (item.kind != MachineItemKind::Address)
    return std::nullopt;
  const std::optional<int> opcode = address_opcode_for_item(items, item, model);
  if (!opcode.has_value())
    return std::nullopt;
  return OverlayExecutable{.opcode = *opcode, .mnemonic = "address byte"};
}

bool can_overlay_executable_cell_at(const std::vector<MachineItem>& items, int index,
                                    AddressSpaceModel model) {
  const std::optional<OverlayExecutable> executable = overlay_executable_at(items, index, model);
  if (!executable.has_value())
    return false;
  if (!is_address_taking_opcode(executable->opcode))
    return true;
  const int operand_index = index + 1;
  return operand_index >= 0 && operand_index < static_cast<int>(items.size()) &&
         items.at(static_cast<std::size_t>(operand_index)).kind == MachineItemKind::Address;
}

std::optional<int> next_executable_opcode(const std::vector<MachineItem>& items, int start,
                                          AddressSpaceModel model) {
  for (int index = start; index < static_cast<int>(items.size()); ++index) {
    if (items.at(static_cast<std::size_t>(index)).kind == MachineItemKind::Label)
      continue;
    const std::optional<OverlayExecutable> executable = overlay_executable_at(items, index, model);
    if (executable.has_value())
      return executable->opcode;
    return std::nullopt;
  }
  return std::nullopt;
}

bool can_move_overlay_opcode_to(const std::vector<MachineItem>& source_items, int source_index,
                                const std::vector<MachineItem>& target_items, int target_index,
                                int opcode, AddressSpaceModel model) {
  if (!is_number_entry_opcode(opcode))
    return true;
  const std::optional<int> source_next =
      next_executable_opcode(source_items, source_index + 1, model);
  const std::optional<int> target_next =
      next_executable_opcode(target_items, target_index + 1, model);
  return !target_next.has_value() || !is_number_entry_opcode(*target_next) ||
         (source_next.has_value() && is_number_entry_opcode(*source_next));
}

MachineItem overlay_address_item(MachineItem address, const std::string& overlaid) {
  if (std::find(address.roles.begin(), address.roles.end(), "address") == address.roles.end())
    address.roles.push_back("address");
  if (std::find(address.roles.begin(), address.roles.end(), "exec") == address.roles.end())
    address.roles.push_back("exec");
  const std::string overlay_comment = "address/code overlay for " + overlaid;
  if (address.comment.has_value() && !address.comment->empty()) {
    address.comment = *address.comment + "; " + overlay_comment;
  } else {
    address.comment = overlay_comment;
  }
  return address;
}

MachineItem overlay_address_item_with_formal_opcode(MachineItem address, int formal_opcode,
                                                    const std::string& overlaid) {
  if (std::find(address.roles.begin(), address.roles.end(), "address") == address.roles.end())
    address.roles.push_back("address");
  if (std::find(address.roles.begin(), address.roles.end(), "exec") == address.roles.end())
    address.roles.push_back("exec");
  address.formal_opcode = formal_opcode;
  const std::string overlay_comment = "formal address/code overlay for " + overlaid;
  if (address.comment.has_value() && !address.comment->empty()) {
    address.comment = *address.comment + "; " + overlay_comment;
  } else {
    address.comment = overlay_comment;
  }
  return address;
}

std::optional<std::string>
address_code_overlay_mnemonic(const std::optional<std::string>& comment) {
  if (!comment.has_value())
    return std::nullopt;
  constexpr std::string_view kPrefix = "address/code overlay for ";
  const std::size_t marker = comment->rfind(kPrefix);
  if (marker == std::string::npos)
    return std::nullopt;
  std::string mnemonic = comment->substr(marker + kPrefix.size());
  if (const std::size_t separator = mnemonic.find(';'); separator != std::string::npos)
    mnemonic.erase(separator);
  while (!mnemonic.empty() && std::isspace(static_cast<unsigned char>(mnemonic.front())) != 0)
    mnemonic.erase(mnemonic.begin());
  while (!mnemonic.empty() && std::isspace(static_cast<unsigned char>(mnemonic.back())) != 0)
    mnemonic.pop_back();
  return mnemonic.empty() ? std::nullopt : std::optional<std::string>{std::move(mnemonic)};
}

bool final_address_code_overlay_artifacts_match(const std::vector<MachineItem>& items,
                                                int expected, AddressSpaceModel model) {
  int matched = 0;
  for (const MachineItem& item : items) {
    if (item.kind != MachineItemKind::Address)
      continue;
    const std::optional<std::string> mnemonic =
        address_code_overlay_mnemonic(item.comment);
    if (!mnemonic.has_value())
      continue;
    const bool has_address_role =
        std::find(item.roles.begin(), item.roles.end(), "address") != item.roles.end();
    const bool has_exec_role =
        std::find(item.roles.begin(), item.roles.end(), "exec") != item.roles.end();
    const std::optional<int> opcode = address_opcode_for_item(items, item, model);
    if (!has_address_role || !has_exec_role || !opcode.has_value() ||
        opcode_by_code(*opcode).name != *mnemonic) {
      return false;
    }
    ++matched;
  }
  return matched == expected;
}

bool has_super_dark_overlay_continuation(const std::vector<MachineItem>& items) {
  int address = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      continue;
    const bool executable_overlay =
        item.kind == MachineItemKind::Address && address >= 1 && address <= 6 &&
        address_code_overlay_mnemonic(item.comment).has_value() &&
        std::find(item.roles.begin(), item.roles.end(), "exec") != item.roles.end();
    if (executable_overlay)
      return true;
    ++address;
  }
  return false;
}

std::optional<int> resolved_address_target(const std::vector<MachineItem>& items,
                                           const MachineItem& address) {
  if (address.kind != MachineItemKind::Address)
    return std::nullopt;
  const std::map<std::string, int> labels = machine_label_addresses(items);
  return resolved_machine_target(address.target, labels);
}

std::optional<MachineItem> choose_overlay_address_item(const std::vector<MachineItem>& candidate,
                                                       const MachineItem& address,
                                                       int executable_opcode,
                                                       const std::string& overlaid,
                                                       AddressSpaceModel model) {
  MachineItem ordinary = overlay_address_item(address, overlaid);
  const std::optional<int> ordinary_opcode = address_opcode_for_item(candidate, ordinary, model);
  if (ordinary_opcode.has_value() && *ordinary_opcode == executable_opcode)
    return ordinary;

  const std::optional<int> target = resolved_address_target(candidate, ordinary);
  if (!target.has_value())
    return std::nullopt;
  const FormalAddressInfo formal = formal_address_info(executable_opcode, model);
  if (formal.actual != *target)
    return std::nullopt;

  MachineItem formal_address =
      overlay_address_item_with_formal_opcode(address, executable_opcode, overlaid);
  const std::optional<int> formal_opcode =
      address_opcode_for_item(candidate, formal_address, model);
  if (!formal_opcode.has_value() || *formal_opcode != executable_opcode)
    return std::nullopt;
  return formal_address;
}

std::vector<MachineItem> immediate_overlay_candidate(const std::vector<MachineItem>& items,
                                                     int branch_index, int labels_end,
                                                     const MachineItem& address) {
  std::vector<MachineItem> candidate;
  candidate.reserve(items.size() - 1);

  candidate.insert(candidate.end(), items.begin(), items.begin() + branch_index + 1);
  candidate.insert(candidate.end(), items.begin() + branch_index + 2, items.begin() + labels_end);
  candidate.push_back(address);
  candidate.insert(candidate.end(), items.begin() + labels_end + 1, items.end());
  return candidate;
}

std::optional<int> direct_jump_continuation_target(const std::vector<MachineItem>& items,
                                                   int start_index, const MachineLayout& layout) {
  int branch_index = start_index;
  while (branch_index < static_cast<int>(items.size()) &&
         items.at(static_cast<std::size_t>(branch_index)).kind == MachineItemKind::Label) {
    ++branch_index;
  }
  if (branch_index >= static_cast<int>(items.size()))
    return std::nullopt;
  const MachineItem& branch = items.at(static_cast<std::size_t>(branch_index));
  if (branch.kind != MachineItemKind::Op || branch.opcode != 0x51)
    return std::nullopt;
  const std::optional<IrTarget> target = direct_address_target(items, branch_index);
  return target.has_value() ? resolved_machine_target(*target, layout.labels) : std::nullopt;
}

bool overlay_opcode_has_safe_continuation(const std::vector<MachineItem>& items,
                                          const MachineItem& item, int target_continuation_index,
                                          int source_continuation_index, int removed_cell_address,
                                          const MachineLayout& layout, AddressSpaceModel model) {
  if (item.kind != MachineItemKind::Op)
    return false;
  if (item.opcode == 0x52)
    return true;
  if (item.opcode >= 0x80 && item.opcode <= 0x8e) {
    const std::optional<int> target = known_indirect_flow_target_comment(item.comment, model);
    return target.has_value() && *target < removed_cell_address;
  }

  const std::optional<int> target_continuation =
      direct_jump_continuation_target(items, target_continuation_index, layout);
  const std::optional<int> source_continuation =
      direct_jump_continuation_target(items, source_continuation_index, layout);
  return target_continuation.has_value() && source_continuation.has_value() &&
         *target_continuation == *source_continuation;
}

std::optional<AddressCodeOverlayApplication>
apply_address_code_overlay(const std::vector<MachineItem>& items, AddressSpaceModel model) {
  const std::map<int, int> address_by_item = machine_address_by_item_index(items);
  const MachineLayout layout = machine_layout(items);
  MachineReturnAnalyzer target_may_return{
      .items = items,
      .layout = layout,
      .model = model,
  };
  for (int index = 0; index < static_cast<int>(items.size()) - 2; ++index) {
    const MachineItem& branch = items.at(static_cast<std::size_t>(index));
    const MachineItem& address = items.at(static_cast<std::size_t>(index + 1));
    if (branch.kind != MachineItemKind::Op || address.kind != MachineItemKind::Address)
      continue;
    if (!can_overlay_address_continuation(target_may_return, branch, address))
      continue;

    int labels_end = index + 2;
    while (labels_end < static_cast<int>(items.size()) &&
           items.at(static_cast<std::size_t>(labels_end)).kind == MachineItemKind::Label) {
      ++labels_end;
    }
    if (labels_end == index + 2)
      continue;
    if (!can_overlay_executable_cell_at(items, labels_end, model))
      continue;

    const std::optional<OverlayExecutable> executable =
        overlay_executable_at(items, labels_end, model);
    if (!executable.has_value())
      continue;

    const auto address_cell_it = address_by_item.find(index + 1);
    if (address_cell_it == address_by_item.end())
      continue;
    const std::optional<int> fixed_target = fixed_address_actual_target(address, model);
    if (fixed_target.has_value() && *fixed_target > address_cell_it->second)
      continue;

    const std::vector<MachineItem> provisional =
        immediate_overlay_candidate(items, index, labels_end, address);
    const int provisional_address_index = index + 1 + (labels_end - (index + 2));
    if (!can_move_overlay_opcode_to(items, labels_end, provisional, provisional_address_index,
                                    executable->opcode, model)) {
      continue;
    }

    const std::optional<MachineItem> candidate_address = choose_overlay_address_item(
        provisional, address, executable->opcode, executable->mnemonic, model);
    if (!candidate_address.has_value())
      continue;

    std::vector<MachineItem> candidate =
        immediate_overlay_candidate(items, index, labels_end, *candidate_address);
    const std::optional<int> overlaid_opcode =
        address_opcode_for_item(candidate, *candidate_address, model);
    if (!overlaid_opcode.has_value() || *overlaid_opcode != executable->opcode)
      continue;
    if (machine_cell_count(candidate) >= machine_cell_count(items))
      continue;

    const auto executable_cell_it = address_by_item.find(labels_end);
    if (executable_cell_it == address_by_item.end())
      continue;
    return AddressCodeOverlayApplication{
        .items = std::move(candidate),
        .removed_cell_address = executable_cell_it->second,
        .overlaid_cell_address = address_cell_it->second,
    };
  }

  const std::set<std::string> referenced = referenced_machine_labels(items);
  for (int index = 0; index < static_cast<int>(items.size()) - 2; ++index) {
    const MachineItem& branch = items.at(static_cast<std::size_t>(index));
    const MachineItem& address = items.at(static_cast<std::size_t>(index + 1));
    if (branch.kind != MachineItemKind::Op || address.kind != MachineItemKind::Address)
      continue;
    if (branch.opcode != 0x51)
      continue;

    for (int labels_start = index + 3; labels_start < static_cast<int>(items.size()) - 1;
         ++labels_start) {
      if (items.at(static_cast<std::size_t>(labels_start)).kind != MachineItemKind::Label)
        continue;

      int labels_end = labels_start;
      std::vector<MachineItem> labels;
      while (labels_end < static_cast<int>(items.size()) &&
             items.at(static_cast<std::size_t>(labels_end)).kind == MachineItemKind::Label) {
        labels.push_back(items.at(static_cast<std::size_t>(labels_end)));
        ++labels_end;
      }
      const bool has_referenced_label =
          std::any_of(labels.begin(), labels.end(),
                      [&](const MachineItem& label) { return referenced.contains(label.name); });
      if (labels.empty() || !has_referenced_label)
        continue;
      if (!labels_have_no_linear_fallthrough(items, labels_start, target_may_return))
        continue;
      if (!can_overlay_executable_cell_at(items, labels_end, model))
        continue;

      const std::optional<OverlayExecutable> executable =
          overlay_executable_at(items, labels_end, model);
      if (!executable.has_value() || is_address_taking_opcode(executable->opcode))
        continue;
      const auto removed_address_it = layout.address_by_item_index.find(labels_end);
      if (removed_address_it == layout.address_by_item_index.end())
        continue;
      // Relocating a distant label onto the branch address byte changes the
      // cell after the overlaid opcode. This is sound when that cell is
      // unreachable (В/О or a proved unconditional indirect jump), or when the
      // old and new continuations are both direct jumps to the same target.
      // Later indirect targets would shift and cannot be repaired when the
      // selector is computed at runtime.
      if (!overlay_opcode_has_safe_continuation(
              items, items.at(static_cast<std::size_t>(labels_end)), index + 2, labels_end + 1,
              removed_address_it->second, layout, model))
        continue;

      if (!fixed_address_targets_survive_removal(items, removed_address_it->second, model)) {
        continue;
      }

      const std::optional<int> branch_target_address =
          resolved_machine_target(address.target, layout.labels);
      if (branch_target_address.has_value() &&
          *branch_target_address == removed_address_it->second &&
          !can_overlay_address_continuation(target_may_return, branch, address)) {
        continue;
      }

      std::vector<MachineItem> provisional;
      provisional.reserve(items.size() - 1);
      provisional.insert(provisional.end(), items.begin(), items.begin() + index + 1);
      provisional.insert(provisional.end(), labels.begin(), labels.end());
      provisional.push_back(address);
      provisional.insert(provisional.end(), items.begin() + index + 2,
                         items.begin() + labels_start);
      provisional.insert(provisional.end(), items.begin() + labels_end + 1, items.end());

      const int provisional_address_index = index + 1 + static_cast<int>(labels.size());
      if (!can_move_overlay_opcode_to(items, labels_end, provisional, provisional_address_index,
                                      executable->opcode, model)) {
        continue;
      }

      const std::optional<MachineItem> candidate_address = choose_overlay_address_item(
          provisional, address, executable->opcode, executable->mnemonic, model);
      if (!candidate_address.has_value())
        continue;

      std::vector<MachineItem> candidate;
      candidate.reserve(items.size() - 1);
      candidate.insert(candidate.end(), items.begin(), items.begin() + index + 1);
      candidate.insert(candidate.end(), labels.begin(), labels.end());
      candidate.push_back(*candidate_address);
      candidate.insert(candidate.end(), items.begin() + index + 2, items.begin() + labels_start);
      candidate.insert(candidate.end(), items.begin() + labels_end + 1, items.end());

      const std::optional<int> overlaid_opcode =
          address_opcode_for_item(candidate, *candidate_address, model);
      if (!overlaid_opcode.has_value() || *overlaid_opcode != executable->opcode)
        continue;
      if (machine_cell_count(candidate) >= machine_cell_count(items))
        continue;

      const auto operand_address_it = layout.address_by_item_index.find(index + 1);
      if (operand_address_it == layout.address_by_item_index.end())
        continue;
      return AddressCodeOverlayApplication{
          .items = std::move(candidate),
          .removed_cell_address = removed_address_it->second,
          .overlaid_cell_address = operand_address_it->second,
      };
    }
  }

  return std::nullopt;
}

std::optional<int> resolved_operand_target(const MachineItem& item,
                                           const MachineLayout& layout,
                                           AddressSpaceModel model) {
  if (item.kind != MachineItemKind::Address)
    return std::nullopt;
  if (item.formal_opcode.has_value()) {
    try {
      return formal_address_info(*item.formal_opcode, model).actual;
    } catch (...) {
      return std::nullopt;
    }
  }
  return resolved_machine_target(item.target, layout.labels);
}

std::optional<int> mapped_machine_address(
    int old_address, const MachineLayout& before_layout,
    const MachineLayout& after_layout,
    const std::vector<std::optional<std::size_t>>& old_to_new_item) {
  const auto old_item = before_layout.item_index_by_address.find(old_address);
  if (old_item == before_layout.item_index_by_address.end() ||
      old_item->second < 0 ||
      static_cast<std::size_t>(old_item->second) >= old_to_new_item.size()) {
    return std::nullopt;
  }
  const std::optional<std::size_t> mapped =
      old_to_new_item.at(static_cast<std::size_t>(old_item->second));
  if (!mapped.has_value())
    return std::nullopt;
  const auto new_address =
      after_layout.address_by_item_index.find(static_cast<int>(*mapped));
  return new_address == after_layout.address_by_item_index.end()
             ? std::nullopt
             : std::optional<int>{new_address->second};
}

std::optional<std::vector<int>> mapped_return_addresses(
    const std::vector<int>& addresses, const MachineLayout& before_layout,
    const MachineLayout& after_layout,
    const std::vector<std::optional<std::size_t>>& old_to_new_item) {
  std::vector<int> mapped;
  mapped.reserve(addresses.size());
  for (const int address : addresses) {
    const std::optional<int> next =
        mapped_machine_address(address, before_layout, after_layout, old_to_new_item);
    if (!next.has_value())
      return std::nullopt;
    mapped.push_back(*next);
  }
  return mapped;
}

bool same_conditional_x2_effect(const OpcodeInfo& left, const OpcodeInfo& right) {
  if (left.conditional_x2_effect.has_value() !=
      right.conditional_x2_effect.has_value()) {
    return false;
  }
  return !left.conditional_x2_effect.has_value() ||
         (left.conditional_x2_effect->fallthrough ==
              right.conditional_x2_effect->fallthrough &&
          left.conditional_x2_effect->jump ==
              right.conditional_x2_effect->jump);
}

bool final_error_padding_overlay_artifact_matches(
    const std::vector<MachineItem>& before,
    const std::vector<MachineItem>& after,
    const std::vector<std::optional<std::size_t>>& old_to_new_item,
    int old_padding_index, int old_return_index, AddressSpaceModel model) {
  if (machine_cell_count(after) + 1 != machine_cell_count(before))
    return false;
  const MachineLayout before_layout = machine_layout(before);
  const MachineLayout after_layout = machine_layout(after);
  const auto before_padding_address =
      before_layout.address_by_item_index.find(old_padding_index);
  const auto before_return_address =
      before_layout.address_by_item_index.find(old_return_index);
  if (before_padding_address == before_layout.address_by_item_index.end() ||
      before_return_address == before_layout.address_by_item_index.end() ||
      !old_to_new_item.at(static_cast<std::size_t>(old_return_index)).has_value()) {
    return false;
  }
  const std::size_t moved_return_index =
      *old_to_new_item.at(static_cast<std::size_t>(old_return_index));
  const auto after_return_address =
      after_layout.address_by_item_index.find(static_cast<int>(moved_return_index));
  if (after_return_address == after_layout.address_by_item_index.end() ||
      after_return_address->second != before_padding_address->second ||
      before_return_address->second <= before_padding_address->second) {
    return false;
  }

  for (std::size_t old_index = 0; old_index < before.size(); ++old_index) {
    if (static_cast<int>(old_index) == old_padding_index)
      continue;
    if (!old_to_new_item.at(old_index).has_value())
      return false;
    const std::size_t new_index = *old_to_new_item.at(old_index);
    if (new_index >= after.size())
      return false;
    const MachineItem& old_item = before.at(old_index);
    const MachineItem& new_item = after.at(new_index);
    if (old_item.kind != new_item.kind)
      return false;
    if (old_item.kind == MachineItemKind::Label && old_item.name != new_item.name)
      return false;
    if (old_item.kind == MachineItemKind::Op && old_item.opcode != new_item.opcode)
      return false;
  }

  for (std::size_t old_index = 0; old_index < before.size(); ++old_index) {
    const MachineItem& old_item = before.at(old_index);
    if (old_item.kind == MachineItemKind::Label) {
      const auto old_address = before_layout.labels.find(old_item.name);
      const auto new_address = after_layout.labels.find(old_item.name);
      if (old_address == before_layout.labels.end() ||
          new_address == after_layout.labels.end()) {
        return false;
      }
      const std::optional<int> mapped =
          mapped_machine_address(old_address->second, before_layout, after_layout,
                                 old_to_new_item);
      if (!mapped.has_value() || *mapped != new_address->second)
        return false;
      continue;
    }
    if (old_item.kind == MachineItemKind::Address) {
      const std::size_t new_index = *old_to_new_item.at(old_index);
      const MachineItem& new_item = after.at(new_index);
      const std::optional<int> old_target =
          resolved_operand_target(old_item, before_layout, model);
      const std::optional<int> new_target =
          resolved_operand_target(new_item, after_layout, model);
      if (!old_target.has_value() || !new_target.has_value())
        return false;
      const std::optional<int> mapped =
          mapped_machine_address(*old_target, before_layout, after_layout,
                                 old_to_new_item);
      if (!mapped.has_value() || *mapped != *new_target)
        return false;
      continue;
    }
    if (!old_item.indirect_flow_targets.has_value())
      continue;
    const MachineItem& new_item =
        after.at(*old_to_new_item.at(old_index));
    if (!new_item.indirect_flow_targets.has_value() ||
        new_item.indirect_flow_targets->size() !=
            old_item.indirect_flow_targets->size()) {
      return false;
    }
    for (std::size_t target_index = 0;
         target_index < old_item.indirect_flow_targets->size(); ++target_index) {
      const std::optional<int> old_target = resolved_machine_target(
          old_item.indirect_flow_targets->at(target_index), before_layout.labels);
      const std::optional<int> new_target = resolved_machine_target(
          new_item.indirect_flow_targets->at(target_index), after_layout.labels);
      if (!old_target.has_value() || !new_target.has_value())
        return false;
      const std::optional<int> mapped =
          mapped_machine_address(*old_target, before_layout, after_layout,
                                 old_to_new_item);
      if (!mapped.has_value() || *mapped != *new_target)
        return false;
    }
  }

  PostLayoutControlFlowOptions control_options;
  control_options.address_space_model = model;
  const AuthoritativePostLayoutControlFlow before_control =
      build_post_layout_control_flow(before, control_options);
  const AuthoritativePostLayoutControlFlow after_control =
      build_post_layout_control_flow(after, control_options);
  if (!before_control.proved || !after_control.proved ||
      before_control.maximum_observed_return_depth !=
          after_control.maximum_observed_return_depth) {
    return false;
  }

  using StateKey = std::tuple<std::size_t, std::vector<int>>;
  const auto before_state_key =
      [&](const PostLayoutExecutionState& state) -> std::optional<StateKey> {
    if (state.item_index >= old_to_new_item.size() ||
        !old_to_new_item.at(state.item_index).has_value()) {
      return std::nullopt;
    }
    const std::optional<std::vector<int>> returns =
        mapped_return_addresses(state.return_stack, before_layout, after_layout,
                                old_to_new_item);
    if (!returns.has_value())
      return std::nullopt;
    return StateKey{*old_to_new_item.at(state.item_index), *returns};
  };
  const auto after_state_key = [](const PostLayoutExecutionState& state) {
    return StateKey{state.item_index, state.return_stack};
  };

  std::set<StateKey> before_states;
  bool old_return_reachable = false;
  for (const PostLayoutExecutionState& state : before_control.execution_states) {
    if (static_cast<int>(state.item_index) == old_padding_index)
      return false;
    old_return_reachable =
        old_return_reachable ||
        static_cast<int>(state.item_index) == old_return_index;
    const std::optional<StateKey> key = before_state_key(state);
    if (!key.has_value())
      return false;
    const MachineItem& old_item = before.at(state.item_index);
    const MachineItem& new_item = after.at(std::get<0>(*key));
    if (old_item.kind != MachineItemKind::Op ||
        new_item.kind != MachineItemKind::Op ||
        old_item.opcode != new_item.opcode ||
        old_item.stop_disposition != new_item.stop_disposition) {
      return false;
    }
    const OpcodeInfo& old_info = opcode_by_code(old_item.opcode);
    const OpcodeInfo& new_info = opcode_by_code(new_item.opcode);
    if (old_info.stack_effect != new_info.stack_effect ||
        old_info.x2_effect != new_info.x2_effect ||
        !same_conditional_x2_effect(old_info, new_info)) {
      return false;
    }
    before_states.insert(*key);
  }
  if (!old_return_reachable)
    return false;

  std::set<StateKey> after_states;
  for (const PostLayoutExecutionState& state : after_control.execution_states)
    after_states.insert(after_state_key(state));
  if (before_states != after_states)
    return false;

  using EdgeKey = std::pair<StateKey, StateKey>;
  std::set<EdgeKey> before_edges;
  for (std::size_t state_index = 0;
       state_index < before_control.execution_states.size(); ++state_index) {
    const std::optional<StateKey> source =
        before_state_key(before_control.execution_states.at(state_index));
    if (!source.has_value())
      return false;
    for (const std::size_t successor :
         before_control.execution_successors.at(state_index)) {
      const std::optional<StateKey> target =
          before_state_key(before_control.execution_states.at(successor));
      if (!target.has_value())
        return false;
      before_edges.emplace(*source, *target);
    }
  }
  std::set<EdgeKey> after_edges;
  for (std::size_t state_index = 0;
       state_index < after_control.execution_states.size(); ++state_index) {
    const StateKey source =
        after_state_key(after_control.execution_states.at(state_index));
    for (const std::size_t successor :
         after_control.execution_successors.at(state_index)) {
      after_edges.emplace(
          source, after_state_key(after_control.execution_states.at(successor)));
    }
  }
  if (before_edges != after_edges)
    return false;

  using EntryKey =
      std::tuple<std::size_t, std::vector<int>, int, bool, int, int, int>;
  const auto manual_key = [](const std::optional<ManualInteractionAnchor>& manual) {
    return std::tuple<bool, int, int, int>{
        manual.has_value(),
        manual.has_value() ? manual->protocol_id : -1,
        manual.has_value() ? manual->phase : -1,
        manual.has_value() ? static_cast<int>(manual->kind) : -1,
    };
  };
  std::set<EntryKey> before_entries;
  for (const PostLayoutExternalEntryState& entry :
       before_control.external_entries) {
    if (entry.entry.item_index >= old_to_new_item.size() ||
        !old_to_new_item.at(entry.entry.item_index).has_value()) {
      return false;
    }
    std::vector<int> returns;
    returns.reserve(entry.return_stack.size());
    for (const PostLayoutCommandIdentity& slot : entry.return_stack) {
      const std::optional<int> mapped =
          mapped_machine_address(slot.address, before_layout, after_layout,
                                 old_to_new_item);
      if (!mapped.has_value())
        return false;
      returns.push_back(*mapped);
    }
    const auto [has_manual, protocol, phase, kind] =
        manual_key(entry.manual_interaction);
    before_entries.emplace(
        *old_to_new_item.at(entry.entry.item_index), std::move(returns),
        static_cast<int>(entry.kind), has_manual, protocol, phase, kind);
  }
  std::set<EntryKey> after_entries;
  for (const PostLayoutExternalEntryState& entry :
       after_control.external_entries) {
    std::vector<int> returns;
    returns.reserve(entry.return_stack.size());
    for (const PostLayoutCommandIdentity& slot : entry.return_stack)
      returns.push_back(slot.address);
    const auto [has_manual, protocol, phase, kind] =
        manual_key(entry.manual_interaction);
    after_entries.emplace(entry.entry.item_index, std::move(returns),
                          static_cast<int>(entry.kind), has_manual, protocol,
                          phase, kind);
  }
  return before_entries == after_entries;
}

std::optional<AddressCodeOverlayApplication>
apply_error_padding_code_overlay(const std::vector<MachineItem>& items,
                                 AddressSpaceModel model) {
  bool has_candidate_padding = false;
  for (int index = 0; index + 1 < static_cast<int>(items.size()); ++index) {
    const MachineItem& error = items.at(static_cast<std::size_t>(index));
    const MachineItem& padding = items.at(static_cast<std::size_t>(index + 1));
    if (error.kind == MachineItemKind::Op && error.opcode == 0x29 &&
        error.stop_disposition == StopDisposition::Resumable &&
        padding.kind == MachineItemKind::Op &&
        has_machine_role(padding, kResumableErrorPaddingRole)) {
      has_candidate_padding = true;
      break;
    }
  }
  if (!has_candidate_padding)
    return std::nullopt;

  PostLayoutControlFlowOptions control_options;
  control_options.address_space_model = model;
  const AuthoritativePostLayoutControlFlow control =
      build_post_layout_control_flow(items, control_options);
  if (!control.proved)
    return std::nullopt;

  const MachineLayout layout = machine_layout(items);
  MachineReturnAnalyzer target_may_return{
      .items = items,
      .layout = layout,
      .model = model,
  };
  const std::set<std::string> referenced =
      referenced_machine_entry_labels(items);

  for (int error_index = 0;
       error_index + 1 < static_cast<int>(items.size()); ++error_index) {
    const MachineItem& error =
        items.at(static_cast<std::size_t>(error_index));
    const int padding_index = error_index + 1;
    const MachineItem& padding =
        items.at(static_cast<std::size_t>(padding_index));
    if (error.kind != MachineItemKind::Op || error.opcode != 0x29 ||
        error.stop_disposition != StopDisposition::Resumable ||
        padding.kind != MachineItemKind::Op ||
        !has_machine_role(padding, kResumableErrorPaddingRole)) {
      continue;
    }
    const auto padding_address =
        layout.address_by_item_index.find(padding_index);
    if (padding_address == layout.address_by_item_index.end())
      continue;
    const bool padding_reachable =
        std::any_of(control.execution_states.begin(),
                    control.execution_states.end(),
                    [&](const PostLayoutExecutionState& state) {
                      return state.item_index ==
                             static_cast<std::size_t>(padding_index);
                    });
    if (padding_reachable)
      continue;

    for (int labels_start = padding_index + 1;
         labels_start < static_cast<int>(items.size()) - 1; ++labels_start) {
      if (items.at(static_cast<std::size_t>(labels_start)).kind !=
          MachineItemKind::Label) {
        continue;
      }
      int return_index = labels_start;
      std::vector<MachineItem> labels;
      while (return_index < static_cast<int>(items.size()) &&
             items.at(static_cast<std::size_t>(return_index)).kind ==
                 MachineItemKind::Label) {
        labels.push_back(items.at(static_cast<std::size_t>(return_index)));
        ++return_index;
      }
      if (return_index >= static_cast<int>(items.size()))
        continue;
      const MachineItem& candidate_return =
          items.at(static_cast<std::size_t>(return_index));
      if (candidate_return.kind != MachineItemKind::Op ||
          candidate_return.opcode != 0x52 ||
          candidate_return.stop_disposition != StopDisposition::Unknown ||
          candidate_return.manual_interaction.has_value()) {
        continue;
      }
      const bool separately_addressed =
          std::any_of(labels.begin(), labels.end(),
                      [&](const MachineItem& label) {
                        return referenced.contains(label.name);
                      });
      if (!separately_addressed ||
          !labels_have_no_linear_fallthrough(items, labels_start,
                                             target_may_return)) {
        continue;
      }
      const bool return_reachable =
          std::any_of(control.execution_states.begin(),
                      control.execution_states.end(),
                      [&](const PostLayoutExecutionState& state) {
                        return state.item_index ==
                               static_cast<std::size_t>(return_index);
                      });
      if (!return_reachable)
        continue;
      const auto return_address =
          layout.address_by_item_index.find(return_index);
      if (return_address == layout.address_by_item_index.end() ||
          return_address->second <= padding_address->second ||
          !fixed_address_targets_survive_removal(
              items, return_address->second, model) ||
          !fixed_indirect_flow_targets_survive_removal(
              items, return_address->second)) {
        continue;
      }

      std::vector<MachineItem> candidate;
      candidate.reserve(items.size() - 1U);
      std::vector<std::optional<std::size_t>> old_to_new_item(items.size());
      for (int index = 0; index < static_cast<int>(items.size()); ++index) {
        if (index == padding_index) {
          for (int label_index = labels_start; label_index < return_index;
               ++label_index) {
            old_to_new_item.at(static_cast<std::size_t>(label_index)) =
                candidate.size();
            candidate.push_back(
                items.at(static_cast<std::size_t>(label_index)));
          }
          MachineItem moved = candidate_return;
          if (!has_machine_role(moved, kResumableErrorPaddingRole))
            moved.roles.push_back(kResumableErrorPaddingRole);
          const std::string overlay_comment =
              "error padding/code overlay for " + moved.mnemonic;
          if (moved.comment.has_value() && !moved.comment->empty())
            moved.comment = *moved.comment + "; " + overlay_comment;
          else
            moved.comment = overlay_comment;
          old_to_new_item.at(static_cast<std::size_t>(return_index)) =
              candidate.size();
          candidate.push_back(std::move(moved));
          continue;
        }
        if (index >= labels_start && index <= return_index)
          continue;
        old_to_new_item.at(static_cast<std::size_t>(index)) =
            candidate.size();
        candidate.push_back(items.at(static_cast<std::size_t>(index)));
      }
      if (!final_error_padding_overlay_artifact_matches(
              items, candidate, old_to_new_item, padding_index, return_index,
              model)) {
        continue;
      }
      return AddressCodeOverlayApplication{
          .items = std::move(candidate),
          .removed_cell_address = return_address->second,
          .overlaid_cell_address = padding_address->second,
      };
    }
  }
  return std::nullopt;
}

std::optional<std::string> retargeted_selector_value(const std::string& register_name,
                                                     const std::string& previous_value,
                                                     int shifted_target, AddressSpaceModel model);

// Retarget preloaded indirect-flow selectors after an address-code overlay
// deleted one cell. The overlay shifts every cell at or beyond
// `removed_cell_address`; a selector whose decoded flow target sits in that
// region must be rewritten to the shifted address (and the cell that was folded
// into the branch operand now executes from `overlaid_cell_address`). Returns
// std::nullopt when any selector cannot be safely retargeted, in which case the
// caller leaves the overlay unapplied so behavior is preserved.
std::optional<std::vector<PreloadReport>> retarget_selector_preloads_after_overlay(
    const std::vector<MachineItem>& after_items, const std::vector<PreloadReport>& preloads,
    int removed_cell_address, int overlaid_cell_address, AddressSpaceModel model) {
  const std::vector<MachineCell> after_cells = machine_cells(after_items);
  std::vector<PreloadReport> next;
  next.reserve(preloads.size());
  for (const PreloadReport& preload : preloads) {
    const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
        preload.register_name, preload.value, IndirectOperationKind::Flow, model);
    if (!decoded.has_value() || !decoded->actual_flow_target.has_value()) {
      next.push_back(preload);
      continue;
    }
    const int target = *decoded->actual_flow_target;
    int new_target = target;
    if (target == removed_cell_address)
      new_target = overlaid_cell_address;
    else if (target > removed_cell_address)
      new_target = target - 1;
    if (new_target == target) {
      next.push_back(preload);
      continue;
    }
    if (!machine_cell_at(after_cells, new_target).has_value())
      return std::nullopt;
    const std::optional<std::string> selector_value =
        retargeted_selector_value(preload.register_name, preload.value, new_target, model);
    if (!selector_value.has_value())
      return std::nullopt;
    const std::optional<IndirectAddressEvaluation> shifted = evaluate_indirect_address(
        preload.register_name, *selector_value, IndirectOperationKind::Flow, model);
    if (!shifted.has_value() || shifted->actual_flow_target != new_target)
      return std::nullopt;
    next.push_back(PreloadReport{
        .register_name = preload.register_name,
        .value = *selector_value,
        .counts_against_program = preload.counts_against_program,
    });
  }
  return next;
}

NumericTargetView numeric_target_view(const std::vector<IrOp>& ops,
                                      const std::map<std::string, int>& labels) {
  NumericTargetView view;
  view.numeric.reserve(ops.size());
  view.target_labels.reserve(ops.size());
  std::map<int, std::string> labels_by_address;
  int label_scan_address = 0;
  for (const IrOp& op : ops) {
    if (op.kind != IrKind::Label) {
      label_scan_address += passes::cells_per_op(op);
      continue;
    }
    const int address = label_scan_address;
    const std::string& label = op.name;
    if (!labels_by_address.contains(address))
      labels_by_address[address] = label;
  }
  for (IrOp op : ops) {
    if (op.kind == IrKind::Jump || op.kind == IrKind::Call || op.kind == IrKind::CondJump) {
      if (const auto* label = std::get_if<std::string>(&op.target)) {
        const auto address_it = labels.find(*label);
        if (address_it != labels.end()) {
          const std::string target_label = *label;
          op.target = address_it->second;
          view.numeric.push_back(std::move(op));
          view.target_labels.push_back(std::move(target_label));
          continue;
        }
      } else if (const auto* target = std::get_if<int>(&op.target)) {
        auto label_it = labels_by_address.find(*target);
        view.numeric.push_back(std::move(op));
        view.target_labels.push_back(label_it == labels_by_address.end()
                                         ? std::optional<std::string>{}
                                         : std::optional<std::string>{label_it->second});
        continue;
      }
    }
    view.numeric.push_back(std::move(op));
    view.target_labels.push_back(std::nullopt);
  }
  return view;
}

std::vector<int> address_by_index(const std::vector<IrOp>& ops) {
  std::vector<int> addresses;
  addresses.reserve(ops.size());
  int address = 0;
  for (const IrOp& op : ops) {
    addresses.push_back(address);
    address += passes::cells_per_op(op);
  }
  return addresses;
}

std::set<std::string> used_registers(const std::vector<IrOp>& ops) {
  std::set<std::string> used;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Store || op.kind == IrKind::Recall || op.kind == IrKind::IndirectStore ||
        op.kind == IrKind::IndirectRecall || op.kind == IrKind::IndirectJump ||
        op.kind == IrKind::IndirectCall || op.kind == IrKind::IndirectCondJump) {
      used.insert(op.register_name);
    }
  }
  return used;
}

std::optional<std::string> first_spare_stable_register(const std::vector<IrOp>& ops,
                                                       const std::set<std::string>& reserved = {}) {
  const std::set<std::string> used = used_registers(ops);
  for (const std::string_view candidate_view : kStableRegisters) {
    const std::string candidate(candidate_view);
    if (!used.contains(candidate) && !reserved.contains(candidate))
      return candidate;
  }
  return std::nullopt;
}

std::string uppercase_hex_digit(int value) {
  if (value < 10)
    return std::to_string(value);
  return std::string(1, static_cast<char>('A' + value - 10));
}

std::string formal_label_from_ordinal(int ordinal) {
  return uppercase_hex_digit(ordinal / 10) + uppercase_hex_digit(ordinal % 10);
}

std::string selector_for_target(int target, AddressSpaceModel model) {
  // See preloaded_indirect_flow.cpp selector_for_target: dark formal aliases for
  // targets 28..47 ("E0".."F9") are not raw-load-stable (E-prefix throws as
  // exponent notation, F-prefix loses its leading nibble), so committing them as
  // a RAW selector preload mis-delivers the address. Only the B/C/D-prefixed
  // aliases (targets 0..27) survive raw loading; otherwise use the raw-stable
  // plain decimal address.
  if (target <= 27)
    return formal_label_from_ordinal(target + 112);
  return format_official_address(target, model);
}

std::optional<std::string> selector_for_actual_target(int target, AddressSpaceModel model) {
  if (target < 0 || target > official_program_last_address(model))
    return std::nullopt;
  return selector_for_target(target, model);
}

std::string register_name_from_index(int index) {
  if (index <= 9)
    return std::to_string(index);
  return std::string(1, static_cast<char>('a' + index - 10));
}

std::optional<std::string> register_from_indirect_opcode(int opcode) {
  const int offset = opcode & 0x0f;
  if (offset > 14)
    return std::nullopt;
  const int base = opcode - offset;
  if (base != 0x70 && base != 0x80 && base != 0x90 && base != 0xa0 && base != 0xc0 &&
      base != 0xe0) {
    return std::nullopt;
  }
  return register_name_from_index(offset);
}

std::optional<int> indirect_branch_opcode_for_register(int opcode,
                                                       const std::string& register_name) {
  const int offset = register_index(register_name);
  switch (opcode) {
  case 0x51:
    return 0x80 + offset;
  case 0x53:
    return 0xa0 + offset;
  case 0x57:
    return 0x70 + offset;
  case 0x59:
    return 0x90 + offset;
  case 0x5c:
    return 0xc0 + offset;
  case 0x5e:
    return 0xe0 + offset;
  default:
    return std::nullopt;
  }
}

int indirect_cond_base(const std::string& condition) {
  if (condition == "!=0")
    return 0x70;
  if (condition == ">=0")
    return 0x90;
  if (condition == "<0")
    return 0xc0;
  return 0xe0;
}

std::string indirect_cond_name(const std::string& condition) {
  if (condition == "==0")
    return "x=0";
  if (condition == "!=0")
    return "x!=0";
  return "x" + condition;
}

std::string indirect_branch_mnemonic(int opcode, const std::string& register_name) {
  const int base = opcode - register_index(register_name);
  switch (base) {
  case 0x70:
    return "К x≠0 " + register_name;
  case 0x80:
    return "К БП " + register_name;
  case 0x90:
    return "К x≥0 " + register_name;
  case 0xa0:
    return "К ПП " + register_name;
  case 0xc0:
    return "К x<0 " + register_name;
  case 0xe0:
    return "К x=0 " + register_name;
  default:
    return "К БП " + register_name;
  }
}

bool is_direct_branch_op(const IrOp& op) {
  return op.kind == IrKind::Jump || op.kind == IrKind::Call || op.kind == IrKind::CondJump;
}

// DEFECT 1 (conditional-branch conversion): a direct conditional jump
// (`F x≠0 NN`, IrKind::CondJump) is rewritten into an indirect conditional
// (`К x≠0 R`). On the MK-61 the indirect conditional branches through the
// int-part of the selector register; that is only equivalent to the direct form
// when (a) the register has no indirect-addressing side effect and (b) the
// decoded int-part equals the proven target. The aggressive rescue only ever
// borrows the STABLE registers 7..E (IndirectSelectorMutation::Stable -> no
// pre-inc/pre-dec side effect) and every rewrite is validated to decode to the
// branch target, so the conversion is provably equivalent. The selector value
// is additionally constrained to be raw-load-stable (see selector_for_target),
// which guarantees the int-part actually delivered at runtime is the proven
// target. Conversions through side-effecting registers 0..6 are never produced.
bool is_convertible_post_layout_branch(const IrOp& op) {
  return is_direct_branch_op(op);
}

bool is_indirect_branch_op(const IrOp& op) {
  return op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
         op.kind == IrKind::IndirectCondJump;
}

IrMeta append_comment(IrMeta meta, const std::string& comment) {
  if (meta.comment.has_value() && !meta.comment->empty()) {
    meta.comment = *meta.comment + "; " + comment;
  } else {
    meta.comment = comment;
  }
  return meta;
}

bool has_role(const std::vector<CellRole>& roles, std::string_view role) {
  return std::find(roles.begin(), roles.end(), role) != roles.end();
}

void restore_statement_proc_call_comment(IrMeta& meta) {
  constexpr std::string_view kCallFunctionPrefix = "call function";
  if (!has_role(meta.roles, "statement-proc-call") || !meta.comment.has_value() ||
      !meta.comment->starts_with(kCallFunctionPrefix)) {
    return;
  }
  meta.comment = "proc call" + meta.comment->substr(kCallFunctionPrefix.size());
}

IrOp indirect_flow_op(const IrOp& op, const std::string& register_name,
                      const std::string& selector_value, int target, bool super_dark) {
  const int offset = register_index(register_name);
  const std::string suffix = "preloaded R" + register_name + "=" + selector_value +
                             " indirect-target=" + std::to_string(target) +
                             (super_dark ? " super-dark" : "") + " shifted-forward indirect flow";
  IrOp result = op;
  result.meta.indirect_flow_targets = std::vector<IrTarget>{
      std::holds_alternative<std::string>(op.target) ? op.target : IrTarget{target}};
  result.register_name = register_name;
  result.target = 0;
  result.target_meta = {};
  if (op.kind == IrKind::Jump) {
    result.kind = IrKind::IndirectJump;
    result.opcode = 0x80 + offset;
    result.meta.mnemonic = "К БП " + register_name;
  } else if (op.kind == IrKind::Call) {
    result.kind = IrKind::IndirectCall;
    result.opcode = 0xa0 + offset;
    result.meta.mnemonic = "К ПП " + register_name;
  } else {
    result.kind = IrKind::IndirectCondJump;
    result.opcode = indirect_cond_base(op.condition) + offset;
    result.meta.mnemonic = "К " + indirect_cond_name(op.condition) + " " + register_name;
  }
  if (result.kind == IrKind::IndirectCall)
    restore_statement_proc_call_comment(result.meta);
  result.meta = append_comment(result.meta, suffix);
  return result;
}

IrOp borrowed_entry_phase_flow_op(const IrOp& op, const std::string& register_name,
                                  const std::string& selector_value, int target) {
  IrOp result = indirect_flow_op(op, register_name, selector_value, target, false);
  result.meta.borrowed_entry_phase_selector = true;
  return result;
}

std::optional<std::string> fractional_selector_suffix(const std::string& value) {
  static const std::regex pattern(R"(^(\d+)(\.\d+)$)");
  std::smatch match;
  if (!std::regex_match(value, match, pattern) || match[1].str() == "0")
    return std::nullopt;
  return match[2].str();
}

std::string regex_escape(std::string_view value) {
  std::string escaped;
  for (const char ch : value) {
    switch (ch) {
    case '.':
    case '^':
    case '$':
    case '|':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '*':
    case '+':
    case '?':
    case '\\':
      escaped.push_back('\\');
      break;
    default:
      break;
    }
    escaped.push_back(ch);
  }
  return escaped;
}

std::optional<std::string> replace_indirect_target_comment(
    const std::optional<std::string>& comment, const std::string& old_register_name,
    const std::string& new_register_name, const std::string& selector_value, int target) {
  if (!comment.has_value())
    return std::nullopt;
  const std::regex pattern("preloaded R" + regex_escape(old_register_name) +
                               R"(=[^\s;]+ indirect-target=\d+)",
                           std::regex_constants::icase);
  if (!std::regex_search(*comment, pattern))
    return comment;
  return std::regex_replace(*comment, pattern,
                            "preloaded R" + new_register_name + "=" + selector_value +
                                " indirect-target=" + std::to_string(target));
}

std::optional<std::string>
replace_indirect_target_comment(const std::optional<std::string>& comment,
                                const std::string& register_name, const std::string& selector_value,
                                int target) {
  return replace_indirect_target_comment(comment, register_name, register_name, selector_value,
                                         target);
}

std::vector<MachineItem>
retarget_machine_selector_comments(std::vector<MachineItem> items,
                                   const std::map<std::string, std::string>& selector_by_register,
                                   AddressSpaceModel model) {
  const std::vector<MachineCell> cells = machine_cells(items);
  const std::map<int, std::vector<std::string>> labels_by_address =
      machine_labels_by_address(items);
  for (MachineItem& item : items) {
    if (item.kind != MachineItemKind::Op)
      continue;
    const std::optional<std::string> register_name = register_from_indirect_opcode(item.opcode);
    if (!register_name.has_value())
      continue;
    const auto selector_it = selector_by_register.find(*register_name);
    if (selector_it == selector_by_register.end())
      continue;
    const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
        *register_name, selector_it->second, IndirectOperationKind::Flow, model);
    if (!decoded.has_value() || !decoded->actual_flow_target.has_value())
      continue;
    const int target = *decoded->actual_flow_target;
    const std::optional<MachineCell> target_cell = machine_cell_at(cells, target);
    if (!target_cell.has_value() || target_cell->item == nullptr ||
        target_cell->item->kind != MachineItemKind::Op) {
      continue;
    }
    const auto aliases = labels_by_address.find(target);
    item.indirect_flow_targets = std::vector<IrTarget>{
        aliases != labels_by_address.end() && !aliases->second.empty()
            ? IrTarget{aliases->second.front()}
            : IrTarget{target}};
    item.comment = replace_indirect_target_comment(
        item.comment, *register_name, selector_it->second, target);
  }
  return items;
}

std::optional<SelectorValue> selector_value_for_register(const std::vector<PreloadReport>& preloads,
                                                         const std::string& register_name,
                                                         const CompileOptions& options) {
  for (const PreloadReport& preload : preloads) {
    if (preload.register_name == register_name)
      return SelectorValue{.value = preload.value, .existing = false};
  }
  const auto existing = options.preloaded_constant_registers.find(register_name);
  if (existing == options.preloaded_constant_registers.end())
    return std::nullopt;
  return SelectorValue{.value = existing->second, .existing = true};
}

bool is_super_dark_rewrite(const IrOp& op) {
  return op.meta.comment.has_value() && op.meta.comment->find("super-dark") != std::string::npos;
}

bool is_dark_entry_target(const IndirectAddressEvaluation& decoded) {
  return decoded.formal_address.has_value() &&
         decoded.formal_address->kind != FormalAddressKind::Official &&
         decoded.formal_address->kind != FormalAddressKind::SuperDark;
}

FeatureProfile feature_profile_for_address_space(AddressSpaceModel model) {
  return model == AddressSpaceModel::Mk61SMiniExpanded
             ? FeatureProfile::Mk61SMiniExpanded
             : FeatureProfile::Standard;
}

std::set<std::string>
borrowed_entry_phase_selector_registers(const std::vector<MachineItem>& items) {
  std::set<std::string> result;
  for (const MachineItem& item : items) {
    if (item.kind != MachineItemKind::Op || !item.borrowed_entry_phase_selector) {
      continue;
    }
    const std::optional<std::string> register_name = register_from_indirect_opcode(item.opcode);
    if (register_name.has_value() && is_stable_indirect_selector(*register_name))
      result.insert(*register_name);
  }
  return result;
}

MergeDuplicateSelectorsResult merge_duplicate_selectors(const std::vector<MachineItem>& items,
                                                        const std::vector<PreloadReport>& preloads,
                                                        AddressSpaceModel model) {
  const std::set<std::string> borrowed_registers = borrowed_entry_phase_selector_registers(items);
  std::map<std::string, std::string> canonical_by_value;
  std::map<std::string, std::string> remap;
  std::map<std::string, std::string> value_by_kept_register;
  std::vector<PreloadReport> kept;
  kept.reserve(preloads.size());

  for (const PreloadReport& preload : preloads) {
    const std::string canonical_key =
        borrowed_registers.contains(preload.register_name)
            ? preload.value + "#borrowed-entry-phase:R" + preload.register_name
            : preload.value;
    const auto canonical_it = canonical_by_value.find(canonical_key);
    if (canonical_it == canonical_by_value.end()) {
      canonical_by_value[canonical_key] = preload.register_name;
      value_by_kept_register[preload.register_name] = preload.value;
      kept.push_back(preload);
      continue;
    }
    if (preload.register_name != canonical_it->second)
      remap[preload.register_name] = canonical_it->second;
  }

  if (remap.empty()) {
    return MergeDuplicateSelectorsResult{
        .items = items,
        .preloads = preloads,
    };
  }

  std::vector<IrOp> ir = raise_machine_to_ir(items, feature_profile_for_address_space(model));
  for (IrOp& op : ir) {
    if (!is_indirect_branch_op(op))
      continue;
    const auto remap_it = remap.find(op.register_name);
    if (remap_it == remap.end())
      continue;
    const std::string source_register = op.register_name;
    const std::string target_register = remap_it->second;
    const auto selector_it = value_by_kept_register.find(target_register);
    if (selector_it == value_by_kept_register.end())
      return MergeDuplicateSelectorsResult{
          .items = items,
          .preloads = preloads,
      };
    const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
        target_register, selector_it->second, IndirectOperationKind::Flow, model);
    if (!decoded.has_value() || !decoded->actual_flow_target.has_value())
      return MergeDuplicateSelectorsResult{
          .items = items,
          .preloads = preloads,
      };
    op.opcode = op.opcode - register_index(source_register) + register_index(target_register);
    op.meta.mnemonic = indirect_branch_mnemonic(op.opcode, target_register);
    op.meta.comment =
        replace_indirect_target_comment(op.meta.comment, source_register, target_register,
                                        selector_it->second, *decoded->actual_flow_target);
    op.register_name = target_register;
  }

  return MergeDuplicateSelectorsResult{
      .items = lower_ir_to_machine(ir),
      .preloads = std::move(kept),
      .merged = static_cast<int>(remap.size()),
  };
}

IrOp fractional_r0_flow_op(const IrOp& op) {
  IrOp result = op;
  result.register_name = "0";
  result.target = 0;
  result.target_meta = {};
  if (op.kind == IrKind::Jump) {
    result.kind = IrKind::IndirectJump;
    result.opcode = 0x80;
    result.meta.mnemonic = "К БП 0";
    result.meta = append_comment(result.meta, "post-layout fractional R0 flow to 99");
  } else if (op.kind == IrKind::Call) {
    result.kind = IrKind::IndirectCall;
    result.opcode = 0xa0;
    result.meta.mnemonic = "К ПП 0";
    result.meta = append_comment(result.meta, "post-layout fractional R0 call to 99");
  } else {
    result.kind = IrKind::IndirectCondJump;
    result.opcode = indirect_cond_base(op.condition);
    result.meta.mnemonic = "К " + indirect_cond_name(op.condition) + " 0";
    result.meta = append_comment(result.meta, "post-layout fractional R0 conditional flow to 99");
  }
  return result;
}

bool is_fractional_r0_literal_before_store(const std::vector<IrOp>& ops, int store_index) {
  int index = store_index - 1;
  bool has_nonzero_fraction_digit = false;
  while (index >= 0) {
    const IrOp& digit = ops.at(static_cast<std::size_t>(index));
    if (digit.kind != IrKind::Plain || digit.opcode < 0x00 || digit.opcode > 0x09)
      break;
    if (digit.opcode > 0)
      has_nonzero_fraction_digit = true;
    --index;
  }
  if (!has_nonzero_fraction_digit || index < 0)
    return false;
  const IrOp& dot = ops.at(static_cast<std::size_t>(index));
  if (dot.kind != IrKind::Plain || dot.opcode != 0x0a)
    return false;
  if (index == 0)
    return true;
  const IrOp& zero = ops.at(static_cast<std::size_t>(index - 1));
  return zero.kind == IrKind::Plain && zero.opcode == 0x00;
}

bool preserves_fractional_r0_fact(const IrOp& op) {
  return op.kind == IrKind::Plain || op.kind == IrKind::Recall || op.kind == IrKind::Label ||
         (op.kind == IrKind::Store && op.register_name != "0") ||
         (op.kind == IrKind::IndirectRecall && op.register_name != "0") ||
         (op.kind == IrKind::IndirectStore && op.register_name != "0");
}

std::optional<int> target_address_after_replacing_op(const std::vector<IrOp>& ops,
                                                     int replace_index,
                                                     const std::string& target_label) {
  int address = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind == IrKind::Label) {
      if (op.name == target_label)
        return address;
      continue;
    }
    address += static_cast<int>(index) == replace_index ? 1 : passes::cells_per_op(op);
  }
  return std::nullopt;
}

std::optional<int> find_fractional_r0_flow_rewrite(const std::vector<IrOp>& ops) {
  bool r0_fractional = false;
  for (std::size_t raw_index = 0; raw_index < ops.size(); ++raw_index) {
    const int index = static_cast<int>(raw_index);
    const IrOp& op = ops.at(raw_index);
    if (passes::has_rewrite_barrier(op)) {
      r0_fractional = false;
      continue;
    }
    if (op.kind == IrKind::Store && op.register_name == "0") {
      r0_fractional = is_fractional_r0_literal_before_store(ops, index);
      continue;
    }
    if (preserves_fractional_r0_fact(op))
      continue;

    if (r0_fractional &&
        (op.kind == IrKind::Jump || op.kind == IrKind::Call || op.kind == IrKind::CondJump)) {
      std::optional<int> final_target;
      if (const auto* label = std::get_if<std::string>(&op.target)) {
        final_target = target_address_after_replacing_op(ops, index, *label);
      } else if (const auto* target = std::get_if<int>(&op.target)) {
        final_target = *target;
      }
      if (final_target.has_value() && *final_target == 99)
        return index;
    }
    r0_fractional = false;
  }
  return std::nullopt;
}

std::optional<PostLayoutIndirectFlowResult>
apply_fractional_r0_flow_rewrite(const std::vector<MachineItem>& items,
                                 FeatureProfile feature_profile) {
  const std::vector<IrOp> ir = raise_machine_to_ir(items, feature_profile);
  const std::optional<int> rewrite = find_fractional_r0_flow_rewrite(ir);
  if (!rewrite.has_value())
    return std::nullopt;

  std::vector<IrOp> rewritten = ir;
  rewritten.at(static_cast<std::size_t>(*rewrite)) =
      fractional_r0_flow_op(rewritten.at(static_cast<std::size_t>(*rewrite)));
  std::vector<MachineItem> lowered = lower_ir_to_machine(rewritten);
  if (machine_cell_count(lowered) >= machine_cell_count(items))
    return std::nullopt;
  return PostLayoutIndirectFlowResult{
      .items = std::move(lowered),
      .optimizations =
          {
              passes::AppliedOptimization{
                  .name = "r0-fractional-sentinel",
                  .detail = "Activated post-layout: replaced direct flow whose target resolves "
                            "to address 99 with fractional R0 indirect flow.",
              },
          },
      .applied = 1,
  };
}

MachineItem indirect_jump_machine_op(const std::string& register_name, const std::string& comment,
                                     IrTarget target,
                                     const std::optional<int>& source_line = std::nullopt) {
  MachineItem item = MachineItem::op(0x80 + register_index(register_name), "К БП " + register_name);
  item.comment = comment;
  item.indirect_flow_targets = std::vector<IrTarget>{std::move(target)};
  if (source_line.has_value())
    item.source_line = *source_line;
  return item;
}

std::optional<std::string> preload_value_for_register(const std::vector<PreloadReport>& preloads,
                                                      const std::string& register_name) {
  for (const PreloadReport& preload : preloads) {
    if (preload.register_name == register_name)
      return preload.value;
  }
  return std::nullopt;
}

std::optional<int> known_indirect_flow_target_comment(const std::optional<std::string>& comment,
                                                      AddressSpaceModel model) {
  if (!comment.has_value())
    return std::nullopt;
  constexpr std::string_view kMarker = "indirect-target=";
  const std::size_t marker = comment->find(kMarker);
  if (marker == std::string::npos)
    return std::nullopt;

  std::size_t cursor = marker + kMarker.size();
  if (cursor >= comment->size() ||
      std::isdigit(static_cast<unsigned char>(comment->at(cursor))) == 0) {
    return std::nullopt;
  }

  int target = 0;
  while (cursor < comment->size() &&
         std::isdigit(static_cast<unsigned char>(comment->at(cursor))) != 0) {
    target = (target * 10) + (comment->at(cursor) - '0');
    ++cursor;
  }
  if (cursor < comment->size()) {
    const char boundary = comment->at(cursor);
    if (std::isspace(static_cast<unsigned char>(boundary)) == 0 && boundary != ';' &&
        boundary != ',') {
      return std::nullopt;
    }
  }
  if (target < 0 || target > official_program_last_address(model))
    return std::nullopt;
  return target;
}

std::optional<int> first_procedure_start_address(const std::vector<MachineItem>& items) {
  int address = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label) {
      if (item.procedure_boundary == "start")
        return address;
      continue;
    }
    ++address;
  }
  return std::nullopt;
}

std::optional<int> known_machine_indirect_jump_target(const MachineItem& item,
                                                      const std::vector<PreloadReport>& preloads,
                                                      AddressSpaceModel model) {
  if (item.kind != MachineItemKind::Op)
    return std::nullopt;
  const std::optional<std::string> register_name = register_from_indirect_opcode(item.opcode);
  if (!register_name.has_value() || item.opcode - register_index(*register_name) != 0x80)
    return std::nullopt;

  if (const std::optional<int> comment_target =
          known_indirect_flow_target_comment(item.comment, model))
    return comment_target;

  const std::optional<std::string> selector_value =
      preload_value_for_register(preloads, *register_name);
  if (selector_value.has_value()) {
    const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
        *register_name, *selector_value, IndirectOperationKind::Flow, model);
    if (decoded.has_value())
      return decoded->actual_flow_target;
  }

  return std::nullopt;
}

bool is_indirect_flow_machine_opcode(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0x70 || family == 0x80 || family == 0x90 || family == 0xa0 || family == 0xc0 ||
         family == 0xe0;
}

struct KnownMachineIndirectFlowTarget {
  int address = 0;
  bool retargetable_preload = false;
};

std::optional<KnownMachineIndirectFlowTarget> known_machine_indirect_flow_target(
    const MachineItem& item, const std::vector<PreloadReport>& preloads, AddressSpaceModel model) {
  if (item.kind != MachineItemKind::Op || !is_indirect_flow_machine_opcode(item.opcode))
    return std::nullopt;
  const std::optional<int> comment_target = known_indirect_flow_target_comment(item.comment, model);
  const std::optional<std::string> register_name = register_from_indirect_opcode(item.opcode);
  if (!register_name.has_value())
    return std::nullopt;
  const std::optional<std::string> selector_value =
      preload_value_for_register(preloads, *register_name);
  const std::optional<IndirectAddressEvaluation> decoded =
      selector_value.has_value() ? evaluate_indirect_address(*register_name, *selector_value,
                                                             IndirectOperationKind::Flow, model)
                                 : std::nullopt;
  const std::optional<int> preload_target =
      decoded.has_value() ? decoded->actual_flow_target : std::nullopt;
  if (comment_target.has_value() && preload_target.has_value() &&
      *comment_target != *preload_target) {
    return std::nullopt;
  }
  if (preload_target.has_value()) {
    return KnownMachineIndirectFlowTarget{
        .address = *preload_target,
        .retargetable_preload = true,
    };
  }
  if (comment_target.has_value()) {
    return KnownMachineIndirectFlowTarget{
        .address = *comment_target,
        .retargetable_preload = false,
    };
  }
  return std::nullopt;
}

bool tail_removal_has_no_external_entries(const std::vector<MachineItem>& items,
                                          const std::vector<PreloadReport>& preloads,
                                          const std::set<int>& removed_item_indices,
                                          const std::set<int>& removed_addresses,
                                          int first_removed_address, AddressSpaceModel model) {
  const std::map<std::string, int> labels = machine_label_addresses(items);
  std::set<std::string> referenced_labels;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (removed_item_indices.contains(static_cast<int>(item_index)))
      continue;
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Address)
      continue;
    if (const auto* label = std::get_if<std::string>(&item.target)) {
      referenced_labels.insert(*label);
      continue;
    }
    try {
      const std::optional<int> fixed_target = fixed_address_actual_target(item, model);
      if (!fixed_target.has_value() || *fixed_target >= first_removed_address)
        return false;
    } catch (const std::exception&) {
      return false;
    }
  }

  const std::map<int, std::vector<std::string>> labels_by_address =
      machine_labels_by_address(items);
  for (const int removed_address : removed_addresses) {
    if (address_has_referenced_label(labels_by_address, referenced_labels, removed_address))
      return false;
  }

  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (removed_item_indices.contains(static_cast<int>(item_index)))
      continue;
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Op || !is_indirect_flow_machine_opcode(item.opcode))
      continue;

    const std::vector<IrOp> raised = raise_machine_to_ir({item});
    if (raised.size() != 1U)
      return false;
    const std::vector<std::string> target_labels =
        passes::computed_dispatch_target_labels(raised.front());
    if (!target_labels.empty()) {
      for (const std::string& label : target_labels) {
        const auto target = labels.find(label);
        if (target == labels.end() || removed_addresses.contains(target->second))
          return false;
      }
      continue;
    }

    const std::optional<KnownMachineIndirectFlowTarget> target =
        known_machine_indirect_flow_target(item, preloads, model);
    if (!target.has_value() || removed_addresses.contains(target->address) ||
        (!target->retargetable_preload && target->address >= first_removed_address)) {
      return false;
    }
  }
  return true;
}

std::string empty_stack_tail_call_comment(const MachineItem& call) {
  std::string comment = "empty-stack tail call";
  if (call.comment.has_value()) {
    constexpr std::string_view kProcCallPrefix = "proc call";
    constexpr std::string_view kCallFunctionPrefix = "call function";
    if (call.comment->starts_with(kProcCallPrefix)) {
      comment = "empty-stack tail call" +
                call.comment->substr(static_cast<std::size_t>(kProcCallPrefix.size()));
    } else if (call.comment->starts_with(kCallFunctionPrefix)) {
      comment = "empty-stack tail call" +
                call.comment->substr(static_cast<std::size_t>(kCallFunctionPrefix.size()));
    } else {
      comment = *call.comment;
    }
  }
  if (!comment.empty())
    comment += "; ";
  comment += "empty-return-stack loop head";
  return comment;
}

std::optional<EmptyStackTailCallRewrite>
find_empty_stack_tail_call_rewrite(const std::vector<MachineItem>& items,
                                   const std::vector<PreloadReport>& preloads,
                                   AddressSpaceModel model) {
  const std::vector<MachineCell> cells = machine_cells(items);
  const std::map<std::string, int> labels = machine_label_addresses(items);
  const std::optional<int> first_proc = first_procedure_start_address(items);
  if (!first_proc.has_value())
    return std::nullopt;

  for (std::size_t index = 0; index + 2 < cells.size(); ++index) {
    const MachineCell& call = cells.at(index);
    const MachineCell& address = cells.at(index + 1);
    const MachineCell& loop_back = cells.at(index + 2);
    if (call.address >= *first_proc)
      break;
    if (call.item == nullptr || address.item == nullptr || loop_back.item == nullptr ||
        call.item->kind != MachineItemKind::Op || call.item->opcode != 0x53 ||
        address.item->kind != MachineItemKind::Address) {
      continue;
    }
    std::optional<int> loop_back_address_index;
    if (known_machine_indirect_jump_target(*loop_back.item, preloads, model) != 0) {
      if (loop_back.item->kind != MachineItemKind::Op || loop_back.item->opcode != 0x51 ||
          index + 3 >= cells.size()) {
        continue;
      }
      const MachineCell& loop_back_address = cells.at(index + 3);
      if (loop_back_address.item == nullptr ||
          loop_back_address.item->kind != MachineItemKind::Address ||
          resolved_machine_target(loop_back_address.item->target, labels) != 0) {
        continue;
      }
      loop_back_address_index = loop_back_address.item_index;
    }

    EmptyStackTailCallRewrite rewrite{
        .call_index = call.item_index,
        .call_address_index = address.item_index,
        .loop_back_index = loop_back.item_index,
        .loop_back_address_index = loop_back_address_index,
        .natural_fallthrough =
            resolved_machine_target(address.item->target, labels) == *first_proc &&
            call.address + (loop_back_address_index.has_value() ? 4 : 3) == *first_proc,
        .mnemonic = "БП",
        .comment = empty_stack_tail_call_comment(*call.item),
        .source_line = call.item->source_line,
    };
    std::set<int> removed_item_indices{rewrite.loop_back_index};
    std::set<int> removed_addresses{loop_back.address};
    int first_removed_address = loop_back.address;
    if (rewrite.loop_back_address_index.has_value()) {
      removed_item_indices.insert(*rewrite.loop_back_address_index);
      removed_addresses.insert(loop_back.address + 1);
    }
    if (rewrite.natural_fallthrough) {
      removed_item_indices.insert(rewrite.call_index);
      removed_item_indices.insert(rewrite.call_address_index);
      removed_addresses.insert(call.address);
      removed_addresses.insert(address.address);
      first_removed_address = call.address;
    }
    if (!tail_removal_has_no_external_entries(items, preloads, removed_item_indices,
                                              removed_addresses, first_removed_address, model)) {
      continue;
    }
    return rewrite;
  }
  return std::nullopt;
}

std::vector<MachineItem>
apply_empty_stack_tail_call_rewrite(const std::vector<MachineItem>& items,
                                    const EmptyStackTailCallRewrite& rewrite) {
  std::vector<MachineItem> result;
  result.reserve(items.size() - (rewrite.loop_back_address_index.has_value() ? 2 : 1));
  for (int index = 0; index < static_cast<int>(items.size()); ++index) {
    if (index == rewrite.loop_back_index || index == rewrite.loop_back_address_index ||
        (rewrite.natural_fallthrough &&
         (index == rewrite.call_index || index == rewrite.call_address_index)))
      continue;
    if (index == rewrite.call_index) {
      MachineItem item = MachineItem::op(0x51, rewrite.mnemonic);
      item.comment = rewrite.comment;
      if (rewrite.source_line.has_value())
        item.source_line = *rewrite.source_line;
      result.push_back(std::move(item));
      continue;
    }
    result.push_back(items.at(static_cast<std::size_t>(index)));
  }
  return result;
}

std::optional<std::string> retargeted_selector_value(const std::string& register_name,
                                                     const std::string& previous_value,
                                                     int shifted_target, AddressSpaceModel model) {
  if (const std::optional<std::string> suffix = fractional_selector_suffix(previous_value)) {
    const std::string candidate = std::to_string(shifted_target) + *suffix;
    const std::optional<IndirectAddressEvaluation> decoded =
        evaluate_indirect_address(register_name, candidate, IndirectOperationKind::Flow, model);
    if (decoded.has_value() && decoded->actual_flow_target == shifted_target)
      return candidate;
  }
  return selector_for_actual_target(shifted_target, model);
}

std::optional<int> first_executable_op_index_at_address(const std::vector<IrOp>& ops,
                                                        const std::vector<int>& addresses,
                                                        int target) {
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (addresses.at(index) == target && passes::cells_per_op(ops.at(index)) > 0)
      return static_cast<int>(index);
  }
  return std::nullopt;
}

std::optional<RetargetedIr> retarget_existing_selectors_after_shift(
    const std::vector<IrOp>& before_ops, const std::vector<IrOp>& after_ops,
    const std::vector<PreloadReport>& preloads, AddressSpaceModel model) {
  if (preloads.empty()) {
    return RetargetedIr{
        .ops = after_ops,
        .items = lower_ir_to_machine(after_ops),
        .preloads = {},
    };
  }
  const std::vector<int> before_addresses = address_by_index(before_ops);
  const std::vector<int> after_addresses = address_by_index(after_ops);
  std::vector<PreloadReport> next_preloads;
  next_preloads.reserve(preloads.size());
  std::map<std::string, std::string> next_by_register;

  for (const PreloadReport& preload : preloads) {
    const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
        preload.register_name, preload.value, IndirectOperationKind::Flow, model);
    if (!decoded.has_value() || !decoded->actual_flow_target.has_value())
      return std::nullopt;
    const std::optional<int> target_index = first_executable_op_index_at_address(
        before_ops, before_addresses, *decoded->actual_flow_target);
    if (!target_index.has_value())
      return std::nullopt;
    const int shifted_target = after_addresses.at(static_cast<std::size_t>(*target_index));
    const std::optional<std::string> selector_value =
        shifted_target == *decoded->actual_flow_target
            ? std::optional<std::string>(preload.value)
            : retargeted_selector_value(preload.register_name, preload.value, shifted_target,
                                        model);
    if (!selector_value.has_value())
      return std::nullopt;
    const std::optional<IndirectAddressEvaluation> shifted = evaluate_indirect_address(
        preload.register_name, *selector_value, IndirectOperationKind::Flow, model);
    if (!shifted.has_value() || shifted->actual_flow_target != shifted_target)
      return std::nullopt;
    next_preloads.push_back(PreloadReport{
        .register_name = preload.register_name,
        .value = *selector_value,
        .counts_against_program = preload.counts_against_program,
    });
    next_by_register[preload.register_name] = *selector_value;
  }

  std::vector<IrOp> retargeted = after_ops;
  for (IrOp& op : retargeted) {
    if (!is_indirect_branch_op(op))
      continue;
    const auto selector_it = next_by_register.find(op.register_name);
    if (selector_it == next_by_register.end())
      continue;
    const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
        op.register_name, selector_it->second, IndirectOperationKind::Flow, model);
    if (!decoded.has_value() || !decoded->actual_flow_target.has_value())
      continue;
    op.meta.comment = replace_indirect_target_comment(
        op.meta.comment, op.register_name, selector_it->second, *decoded->actual_flow_target);
  }

  return RetargetedIr{
      .ops = retargeted,
      .items = lower_ir_to_machine(retargeted),
      .preloads = std::move(next_preloads),
  };
}

std::optional<RetargetedMachine> retarget_selector_preloads_after_machine_deletion(
    const std::vector<MachineItem>& before_items, std::vector<MachineItem> after_items,
    const std::vector<PreloadReport>& preloads, std::vector<int> removed_item_indices,
    int replaced_item_index, AddressSpaceModel model) {
  std::ranges::sort(removed_item_indices);
  removed_item_indices.erase(std::unique(removed_item_indices.begin(), removed_item_indices.end()),
                             removed_item_indices.end());
  const std::vector<MachineCell> before_cells = machine_cells(before_items);
  const std::map<int, int> after_address_by_item_index = machine_address_by_item_index(after_items);
  std::vector<PreloadReport> next_preloads;
  next_preloads.reserve(preloads.size());
  std::map<std::string, std::string> next_by_register;
  for (const PreloadReport& preload : preloads) {
    const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
        preload.register_name, preload.value, IndirectOperationKind::Flow, model);
    if (!decoded.has_value() || !decoded->actual_flow_target.has_value())
      return std::nullopt;
    const int target = *decoded->actual_flow_target;
    const std::optional<MachineCell> before_cell = machine_cell_at(before_cells, target);
    if (!before_cell.has_value())
      return std::nullopt;
    if (std::ranges::binary_search(removed_item_indices, before_cell->item_index) ||
        before_cell->item_index == replaced_item_index) {
      return std::nullopt;
    }
    const int removed_before =
        static_cast<int>(std::ranges::lower_bound(removed_item_indices, before_cell->item_index) -
                         removed_item_indices.begin());
    const int after_item_index = before_cell->item_index - removed_before;
    const auto shifted_target_it = after_address_by_item_index.find(after_item_index);
    if (shifted_target_it == after_address_by_item_index.end())
      return std::nullopt;
    const int shifted_target = shifted_target_it->second;
    const std::optional<std::string> selector_value =
        shifted_target == target ? std::optional<std::string>(preload.value)
                                 : retargeted_selector_value(preload.register_name, preload.value,
                                                             shifted_target, model);
    if (!selector_value.has_value())
      return std::nullopt;
    const std::optional<IndirectAddressEvaluation> shifted = evaluate_indirect_address(
        preload.register_name, *selector_value, IndirectOperationKind::Flow, model);
    if (!shifted.has_value() || shifted->actual_flow_target != shifted_target)
      return std::nullopt;
    next_preloads.push_back(PreloadReport{
        .register_name = preload.register_name,
        .value = *selector_value,
        .counts_against_program = preload.counts_against_program,
    });
    next_by_register[preload.register_name] = *selector_value;
  }

  return RetargetedMachine{
      .items = retarget_machine_selector_comments(std::move(after_items), next_by_register, model),
      .preloads = std::move(next_preloads),
  };
}

std::optional<RewriteStep>
validate_rewrite_at(int index, const std::vector<IrOp>& ir, const std::vector<IrOp>& numeric,
                    const std::vector<std::optional<std::string>>& target_labels,
                    const passes::PassResult& pass, const std::vector<MachineItem>& items,
                    const CompileOptions& options) {
  const IrOp& rewritten = pass.ops.at(static_cast<std::size_t>(index));
  const IrOp& original = numeric.at(static_cast<std::size_t>(index));
  if (!is_indirect_branch_op(rewritten) || !is_convertible_post_layout_branch(original))
    return std::nullopt;
  const std::optional<std::string>& label = target_labels.at(static_cast<std::size_t>(index));
  if (!label.has_value())
    return std::nullopt;
  const std::optional<SelectorValue> selector =
      selector_value_for_register(pass.preloads, rewritten.register_name, options);
  if (!selector.has_value())
    return std::nullopt;

  std::vector<IrOp> candidate = ir;
  candidate.at(static_cast<std::size_t>(index)) = rewritten;
  const std::map<std::string, int> final_labels = passes::calculate_label_addresses(candidate);
  const auto target_it = final_labels.find(*label);
  if (target_it == final_labels.end())
    return std::nullopt;

  const AddressSpaceModel model = address_space_model_for_options(options);
  const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
      rewritten.register_name, selector->value, IndirectOperationKind::Flow, model);
  if (!decoded.has_value() || decoded->actual_flow_target != target_it->second)
    return std::nullopt;

  std::vector<MachineItem> candidate_items = lower_ir_to_machine(candidate);
  if (machine_cell_count(candidate_items) >= machine_cell_count(items))
    return std::nullopt;

  const std::vector<int> addresses = address_by_index(ir);
  return RewriteStep{
      .ops = std::move(candidate),
      .items = std::move(candidate_items),
      .preload =
          PreloadReport{
              .register_name = rewritten.register_name,
              .value = selector->value,
              .counts_against_program = false,
          },
      .super_dark = is_super_dark_rewrite(rewritten),
      .dark_entry = is_dark_entry_target(*decoded),
      .converted_addresses = {addresses.at(static_cast<std::size_t>(index))},
      .protected_targets = {target_it->second},
      .existing_preload = selector->existing,
      .converted = 1,
  };
}

std::optional<RewriteStep> validate_rewrite_group(
    const std::vector<int>& indices, const std::vector<IrOp>& ir, const std::vector<IrOp>& numeric,
    const std::vector<std::optional<std::string>>& target_labels, const passes::PassResult& pass,
    const std::vector<MachineItem>& items, const CompileOptions& options) {
  if (indices.empty())
    return std::nullopt;
  const IrOp& first = pass.ops.at(static_cast<std::size_t>(indices.front()));
  if (!is_indirect_branch_op(first))
    return std::nullopt;
  const std::string register_name = first.register_name;
  const std::optional<SelectorValue> selector =
      selector_value_for_register(pass.preloads, register_name, options);
  if (!selector.has_value())
    return std::nullopt;

  std::set<int> index_set(indices.begin(), indices.end());
  std::vector<IrOp> candidate = ir;
  for (const int index : indices)
    candidate.at(static_cast<std::size_t>(index)) = pass.ops.at(static_cast<std::size_t>(index));

  const std::map<std::string, int> final_labels = passes::calculate_label_addresses(candidate);
  const AddressSpaceModel model = address_space_model_for_options(options);
  const std::optional<IndirectAddressEvaluation> decoded =
      evaluate_indirect_address(register_name, selector->value, IndirectOperationKind::Flow, model);
  if (!decoded.has_value())
    return std::nullopt;

  bool super_dark = false;
  std::vector<int> protected_targets;
  for (const int index : indices) {
    const IrOp& rewritten = pass.ops.at(static_cast<std::size_t>(index));
    const IrOp& original = numeric.at(static_cast<std::size_t>(index));
    if (!is_indirect_branch_op(rewritten) || !is_convertible_post_layout_branch(original) ||
        rewritten.register_name != register_name) {
      return std::nullopt;
    }
    const std::optional<std::string>& label = target_labels.at(static_cast<std::size_t>(index));
    if (!label.has_value())
      return std::nullopt;
    const auto target_it = final_labels.find(*label);
    if (target_it == final_labels.end() || decoded->actual_flow_target != target_it->second)
      return std::nullopt;
    if (is_super_dark_rewrite(rewritten))
      super_dark = true;
    protected_targets.push_back(target_it->second);
  }

  std::vector<MachineItem> candidate_items = lower_ir_to_machine(candidate);
  if (machine_cell_count(candidate_items) >= machine_cell_count(items))
    return std::nullopt;

  const std::vector<int> addresses = address_by_index(ir);
  std::vector<int> converted_addresses;
  converted_addresses.reserve(indices.size());
  for (const int index : indices)
    converted_addresses.push_back(addresses.at(static_cast<std::size_t>(index)));

  return RewriteStep{
      .ops = std::move(candidate),
      .items = std::move(candidate_items),
      .preload =
          PreloadReport{
              .register_name = register_name,
              .value = selector->value,
              .counts_against_program = false,
          },
      .super_dark = super_dark,
      .dark_entry = is_dark_entry_target(*decoded),
      .converted_addresses = std::move(converted_addresses),
      .protected_targets = std::move(protected_targets),
      .existing_preload = selector->existing,
      .converted = static_cast<int>(indices.size()),
  };
}

std::optional<RewriteStep> better_rewrite(std::optional<RewriteStep> current,
                                          std::optional<RewriteStep> candidate) {
  if (!candidate.has_value())
    return current;
  if (!current.has_value())
    return candidate;
  if (candidate->converted != current->converted)
    return candidate->converted > current->converted ? candidate : current;
  if (candidate->super_dark != current->super_dark)
    return candidate->super_dark ? candidate : current;
  if (candidate->dark_entry != current->dark_entry)
    return candidate->dark_entry ? candidate : current;
  if (candidate->existing_preload != current->existing_preload)
    return candidate->existing_preload ? candidate : current;
  return current;
}

bool fixed_point_selector_register_is_overwritten(const std::vector<IrOp>& ops,
                                                  const std::string& register_name) {
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Store && op.register_name == register_name)
      return true;
    if (op.kind != IrKind::IndirectStore)
      continue;
    const std::optional<std::set<std::string>> targets = passes::known_indirect_memory_targets(op);
    if (!targets.has_value() || targets->contains(register_name))
      return true;
  }
  return false;
}

std::optional<RewriteStep> apply_existing_selector_exact_backward_rewrite(
    const std::vector<IrOp>& ir, const std::vector<std::optional<std::string>>& target_labels,
    const std::vector<MachineItem>& items, const CompileOptions& options) {
  if (options.preloaded_constant_registers.empty())
    return std::nullopt;

  const AddressSpaceModel model = address_space_model_for_options(options);
  const std::vector<int> addresses = address_by_index(ir);
  const std::map<std::string, int> labels = passes::calculate_label_addresses(ir);
  std::optional<RewriteStep> best;

  for (std::size_t index = 0; index < ir.size(); ++index) {
    const IrOp& original = ir.at(index);
    if (passes::has_rewrite_barrier(original) || !is_convertible_post_layout_branch(original)) {
      continue;
    }
    std::optional<int> target_address;
    if (target_labels.at(index).has_value()) {
      const auto target = labels.find(*target_labels.at(index));
      if (target != labels.end())
        target_address = target->second;
    } else if (const auto* numeric_target = std::get_if<int>(&original.target)) {
      target_address = *numeric_target;
    }
    if (!target_address.has_value() || *target_address >= addresses.at(index))
      continue;
    for (const auto& [register_name, selector_value] : options.preloaded_constant_registers) {
      const bool stable = is_stable_indirect_selector(register_name);
      const bool overwritten = fixed_point_selector_register_is_overwritten(ir, register_name);
      if (!stable || overwritten)
        continue;
      const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
          register_name, selector_value, IndirectOperationKind::Flow, model);
      if (!decoded.has_value() || decoded->actual_flow_target != *target_address)
        continue;

      std::vector<IrOp> candidate = ir;
      const bool super_dark = decoded->super_dark.has_value() &&
                              decoded->super_dark->entry_address == *target_address;
      candidate.at(index) = indirect_flow_op(original, register_name, selector_value,
                                             *target_address, super_dark);
      if (target_labels.at(index).has_value()) {
        const std::map<std::string, int> final_labels =
            passes::calculate_label_addresses(candidate);
        const auto final_target = final_labels.find(*target_labels.at(index));
        if (final_target == final_labels.end() || final_target->second != *target_address)
          continue;
      }
      std::vector<MachineItem> candidate_items = lower_ir_to_machine(candidate);
      if (machine_cell_count(candidate_items) >= machine_cell_count(items))
        continue;

      best = better_rewrite(std::move(best), RewriteStep{
                                                 .ops = std::move(candidate),
                                                 .items = std::move(candidate_items),
                                                 .preload =
                                                     PreloadReport{
                                                         .register_name = register_name,
                                                         .value = selector_value,
                                                         .counts_against_program = false,
                                                     },
                                                 .super_dark = super_dark,
                                                 .dark_entry = is_dark_entry_target(*decoded),
                                                 .converted_addresses = {addresses.at(index)},
                                                 .protected_targets = {*target_address},
                                                 .existing_preload = true,
                                                 .converted = 1,
                                             });
    }
  }
  return best;
}

std::optional<RewriteStep> apply_existing_selector_fixed_point_rewrite(
    const std::vector<IrOp>& ir, const std::vector<std::optional<std::string>>& target_labels,
    const std::vector<MachineItem>& items, const CompileOptions& options) {
  if (!options.dual_use_constant_indirect_flow || !options.forward_indirect_flow ||
      options.preloaded_constant_registers.empty()) {
    return std::nullopt;
  }

  const AddressSpaceModel model = address_space_model_for_options(options);
  const std::vector<int> addresses = address_by_index(ir);
  const std::map<std::string, int> original_labels = passes::calculate_label_addresses(ir);
  std::optional<RewriteStep> best;

  for (std::size_t index = 0; index < ir.size(); ++index) {
    const IrOp& original = ir.at(index);
    if (passes::has_rewrite_barrier(original) || !is_convertible_post_layout_branch(original) ||
        !target_labels.at(index).has_value()) {
      continue;
    }
    const std::string& target_label = *target_labels.at(index);
    const auto original_target = original_labels.find(target_label);
    if (original_target == original_labels.end() ||
        original_target->second <= addresses.at(index)) {
      continue;
    }

    for (const auto& [register_name, selector_value] : options.preloaded_constant_registers) {
      if (!is_stable_indirect_selector(register_name) ||
          fixed_point_selector_register_is_overwritten(ir, register_name)) {
        continue;
      }

      std::vector<IrOp> candidate = ir;
      candidate.at(index) = indirect_flow_op(original, register_name, selector_value, 0, false);
      const std::map<std::string, int> final_labels = passes::calculate_label_addresses(candidate);
      const auto final_target = final_labels.find(target_label);
      if (final_target == final_labels.end() || final_target->second == original_target->second) {
        continue;
      }

      const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
          register_name, selector_value, IndirectOperationKind::Flow, model);
      if (!decoded.has_value() || decoded->actual_flow_target != final_target->second)
        continue;
      const bool super_dark = decoded->super_dark.has_value() &&
                              decoded->super_dark->entry_address == final_target->second;
      candidate.at(index) = indirect_flow_op(original, register_name, selector_value,
                                             final_target->second, super_dark);
      std::vector<MachineItem> candidate_items = lower_ir_to_machine(candidate);
      if (machine_cell_count(candidate_items) >= machine_cell_count(items))
        continue;

      best = better_rewrite(std::move(best), RewriteStep{
                                                 .ops = std::move(candidate),
                                                 .items = std::move(candidate_items),
                                                 .preload =
                                                     PreloadReport{
                                                         .register_name = register_name,
                                                         .value = selector_value,
                                                         .counts_against_program = false,
                                                     },
                                                 .super_dark = super_dark,
                                                 .dark_entry = is_dark_entry_target(*decoded),
                                                 .converted_addresses = {addresses.at(index)},
                                                 .protected_targets = {final_target->second},
                                                 .existing_preload = true,
                                                 .converted = 1,
                                             });
    }
  }
  return best;
}

PostLayoutBorrowedSelectorProof borrowed_entry_phase_proof(const std::vector<MachineItem>& items,
                                                           AddressSpaceModel model) {
  PostLayoutControlFlowOptions options;
  options.address_space_model = model;
  PostLayoutBorrowedSelectorProof proof =
      prove_post_layout_borrowed_entry_selectors(items, options);
  if (proof.proved)
    return proof;
  options.empty_return_target = IrTarget{1};
  PostLayoutBorrowedSelectorProof empty_return_proof =
      prove_post_layout_borrowed_entry_selectors(items, options);
  return empty_return_proof.proved ? empty_return_proof : proof;
}

std::optional<RewriteStep> validate_generated_selector_rewrite_group(
    const std::vector<int>& indices, const std::vector<IrOp>& ir,
    const std::vector<std::optional<std::string>>& target_labels, const std::string& register_name,
    const std::vector<MachineItem>& items, AddressSpaceModel model, bool borrowed_entry_phase) {
  if (indices.empty())
    return std::nullopt;

  std::set<int> index_set(indices.begin(), indices.end());
  std::vector<IrOp> provisional;
  provisional.reserve(ir.size());
  for (std::size_t index = 0; index < ir.size(); ++index) {
    const IrOp& op = ir.at(index);
    if (index_set.contains(static_cast<int>(index)) && is_direct_branch_op(op)) {
      provisional.push_back(indirect_flow_op(op, register_name, "00", 0, false));
    } else {
      provisional.push_back(op);
    }
  }
  const std::map<std::string, int> final_labels = passes::calculate_label_addresses(provisional);

  std::optional<int> final_target;
  for (const int index : indices) {
    if (!is_convertible_post_layout_branch(ir.at(static_cast<std::size_t>(index))))
      return std::nullopt;
    const std::optional<std::string>& label = target_labels.at(static_cast<std::size_t>(index));
    if (!label.has_value())
      return std::nullopt;
    const auto target_it = final_labels.find(*label);
    if (target_it == final_labels.end())
      return std::nullopt;
    if (!final_target.has_value()) {
      final_target = target_it->second;
    } else if (*final_target != target_it->second) {
      return std::nullopt;
    }
  }
  if (!final_target.has_value())
    return std::nullopt;

  const std::optional<std::string> selector_value =
      selector_for_actual_target(*final_target, model);
  if (!selector_value.has_value())
    return std::nullopt;
  const std::optional<IndirectAddressEvaluation> decoded =
      evaluate_indirect_address(register_name, *selector_value, IndirectOperationKind::Flow, model);
  if (!decoded.has_value() || decoded->actual_flow_target != *final_target)
    return std::nullopt;

  std::vector<IrOp> candidate;
  candidate.reserve(ir.size());
  for (std::size_t index = 0; index < ir.size(); ++index) {
    const IrOp& op = ir.at(index);
    if (index_set.contains(static_cast<int>(index)) && is_direct_branch_op(op)) {
      candidate.push_back(
          borrowed_entry_phase
              ? borrowed_entry_phase_flow_op(op, register_name, *selector_value, *final_target)
              : indirect_flow_op(op, register_name, *selector_value, *final_target, false));
    } else {
      candidate.push_back(op);
    }
  }
  std::vector<MachineItem> candidate_items = lower_ir_to_machine(candidate);
  if (machine_cell_count(candidate_items) >= machine_cell_count(items))
    return std::nullopt;
  if (borrowed_entry_phase) {
    const PostLayoutBorrowedSelectorProof proof =
        borrowed_entry_phase_proof(candidate_items, model);
    if (!proof.proved) {
      if (trace_post_layout_enabled()) {
        std::cerr << "[post-layout] borrowed-entry proof rejected R" << register_name;
        for (const std::string& reason : proof.reasons)
          std::cerr << "; " << reason;
        std::cerr << "\n";
      }
      return std::nullopt;
    }
  }

  const std::vector<int> addresses = address_by_index(ir);
  std::vector<int> converted_addresses;
  converted_addresses.reserve(indices.size());
  for (const int index : indices)
    converted_addresses.push_back(addresses.at(static_cast<std::size_t>(index)));

  return RewriteStep{
      .ops = std::move(candidate),
      .items = std::move(candidate_items),
      .preload =
          PreloadReport{
              .register_name = register_name,
              .value = *selector_value,
              .counts_against_program = false,
          },
      .super_dark = false,
      .dark_entry = is_dark_entry_target(*decoded),
      .converted_addresses = std::move(converted_addresses),
      .protected_targets = {*final_target},
      .existing_preload = false,
      .borrowed_entry_phase = borrowed_entry_phase,
      .converted = static_cast<int>(indices.size()),
  };
}

std::optional<RewriteStep>
apply_forward_rewrite(const std::vector<IrOp>& ir,
                      const std::vector<std::optional<std::string>>& target_labels,
                      const std::vector<MachineItem>& items, const std::set<std::string>& reserved,
                      AddressSpaceModel model) {
  const bool trace = trace_post_layout_enabled();
  const std::optional<std::string> register_name = first_spare_stable_register(ir, reserved);
  if (!register_name.has_value()) {
    if (trace) {
      const std::set<std::string> used = used_registers(ir);
      std::cerr << "[post-layout] forward no spare register; used=";
      for (const std::string& name : used)
        std::cerr << name;
      std::cerr << " reserved=";
      for (const std::string& name : reserved)
        std::cerr << name;
      std::cerr << "\n";
    }
    return std::nullopt;
  }

  const std::map<std::string, int> labels = passes::calculate_label_addresses(ir);
  const std::vector<int> addresses = address_by_index(ir);
  std::vector<std::pair<std::string, std::vector<int>>> groups;
  for (std::size_t index = 0; index < ir.size(); ++index) {
    const IrOp& op = ir.at(index);
    if (passes::has_rewrite_barrier(op) || !is_convertible_post_layout_branch(op))
      continue;
    const std::optional<std::string>& label = target_labels.at(index);
    if (!label.has_value())
      continue;
    const auto target_it = labels.find(*label);
    if (target_it == labels.end() || target_it->second <= addresses.at(index))
      continue;
    auto group_it = std::find_if(groups.begin(), groups.end(),
                                 [&](const auto& group) { return group.first == *label; });
    if (group_it == groups.end()) {
      groups.push_back({*label, {static_cast<int>(index)}});
    } else {
      group_it->second.push_back(static_cast<int>(index));
    }
  }

  if (trace) {
    std::cerr << "[post-layout] forward spare=R" << *register_name << " groups=" << groups.size()
              << "\n";
    for (const auto& [label, indices] : groups) {
      const auto target_it = labels.find(label);
      std::cerr << "[post-layout] forward group label=" << label
                << " target=" << (target_it == labels.end() ? -1 : target_it->second)
                << " sites=" << indices.size() << "\n";
    }
  }

  std::optional<RewriteStep> best;
  for (const auto& [label, indices] : groups) {
    (void)label;
    std::optional<RewriteStep> candidate = validate_generated_selector_rewrite_group(
        indices, ir, target_labels, *register_name, items, model,
        /*borrowed_entry_phase=*/false);
    if (trace) {
      std::cerr << "[post-layout] forward candidate label=" << label
                << " valid=" << (candidate.has_value() ? "yes" : "no");
      if (candidate.has_value()) {
        std::cerr << " converted=" << candidate->converted << " selector=R"
                  << candidate->preload.register_name << "=" << candidate->preload.value
                  << " cells_after=" << machine_cell_count(candidate->items);
      }
      std::cerr << "\n";
    }
    best = better_rewrite(std::move(best), std::move(candidate));
  }
  return best;
}

bool ir_op_reads_register(const IrOp& op, const std::string& register_name) {
  if (op.kind == IrKind::Recall && op.register_name == register_name)
    return true;
  if (op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
      op.kind == IrKind::IndirectCondJump || op.kind == IrKind::IndirectStore ||
      op.kind == IrKind::IndirectRecall) {
    if (op.register_name == register_name)
      return true;
  }
  if (op.kind != IrKind::IndirectRecall)
    return false;
  const std::optional<std::set<std::string>> targets = passes::known_indirect_memory_targets(op);
  return !targets.has_value() || targets->contains(register_name);
}

std::optional<RewriteStep>
apply_borrowed_entry_phase_rewrite(const std::vector<IrOp>& ir,
                                   const std::vector<std::optional<std::string>>& target_labels,
                                   const std::vector<MachineItem>& items,
                                   const std::set<std::string>& reserved, AddressSpaceModel model) {
  // A globally spare register is both simpler and strictly stronger. Entry
  // phase borrowing is considered only when ordinary spare allocation failed.
  if (first_spare_stable_register(ir, reserved).has_value())
    return std::nullopt;

  const std::set<std::string> used = used_registers(ir);
  std::optional<RewriteStep> best;
  for (const std::string_view candidate_view : kStableRegisters) {
    const std::string register_name(candidate_view);
    if (!used.contains(register_name) || reserved.contains(register_name))
      continue;

    std::optional<int> first_store;
    std::optional<int> first_read;
    for (std::size_t index = 0; index < ir.size(); ++index) {
      const IrOp& op = ir.at(index);
      if (!first_store.has_value() && op.kind == IrKind::Store &&
          op.register_name == register_name) {
        first_store = static_cast<int>(index);
      }
      if (!first_read.has_value() && ir_op_reads_register(op, register_name))
        first_read = static_cast<int>(index);
    }
    // This cheap filter keeps candidate search bounded. The authoritative
    // cyclic proof below remains the correctness boundary.
    if (!first_store.has_value() || (first_read.has_value() && *first_read < *first_store)) {
      continue;
    }

    std::vector<std::pair<std::string, std::vector<int>>> groups;
    for (std::size_t index = 0; index < ir.size() && static_cast<int>(index) < *first_store;
         ++index) {
      const IrOp& op = ir.at(index);
      if (passes::has_rewrite_barrier(op) || !is_convertible_post_layout_branch(op) ||
          !target_labels.at(index).has_value()) {
        continue;
      }
      const std::string& label = *target_labels.at(index);
      auto group = std::find_if(groups.begin(), groups.end(),
                                [&](const auto& entry) { return entry.first == label; });
      if (group == groups.end()) {
        groups.push_back({label, {static_cast<int>(index)}});
      } else {
        group->second.push_back(static_cast<int>(index));
      }
    }

    for (const auto& [unused_label, indices] : groups) {
      (void)unused_label;
      std::optional<RewriteStep> group_candidate = validate_generated_selector_rewrite_group(
          indices, ir, target_labels, register_name, items, model,
          /*borrowed_entry_phase=*/true);
      const bool group_proved = group_candidate.has_value();
      best = better_rewrite(std::move(best), std::move(group_candidate));
      if (group_proved || indices.size() <= 1U)
        continue;
      for (const int index : indices) {
        best = better_rewrite(std::move(best),
                              validate_generated_selector_rewrite_group(
                                  {index}, ir, target_labels, register_name, items, model,
                                  /*borrowed_entry_phase=*/true));
      }
    }
  }
  return best;
}

CompileOptions options_with_reserved_preloads(const CompileOptions& options,
                                              const std::vector<PreloadReport>& preloads) {
  CompileOptions result = options;
  for (const PreloadReport& preload : preloads)
    result.preloaded_constant_registers[preload.register_name] = preload.value;
  return result;
}

std::optional<RewriteStep> apply_one_rewrite(const std::vector<MachineItem>& items,
                                             const CompileOptions& options,
                                             const std::vector<PreloadReport>& existing_preloads) {
  const bool trace = trace_post_layout_enabled();
  CompileOptions round_options = options_with_reserved_preloads(options, existing_preloads);
  const AddressSpaceModel model = address_space_model_for_options(round_options);

  std::set<std::string> reserved;
  for (const auto& [register_name, value] : round_options.preloaded_constant_registers) {
    (void)value;
    reserved.insert(register_name);
  }

  const std::vector<IrOp> ir =
      raise_machine_to_ir(items, effective_optimizer_feature_profile(round_options));
  const std::map<std::string, int> labels = passes::calculate_label_addresses(ir);
  const NumericTargetView view = numeric_target_view(ir, labels);
  if (trace) {
    const std::vector<int> numeric_addresses = address_by_index(view.numeric);
    int direct_branches = 0;
    int numeric_targets = 0;
    int backward_targets = 0;
    for (std::size_t index = 0; index < view.numeric.size(); ++index) {
      const IrOp& op = view.numeric.at(index);
      if (!is_direct_branch_op(op))
        continue;
      ++direct_branches;
      const auto* target = std::get_if<int>(&op.target);
      if (target == nullptr)
        continue;
      ++numeric_targets;
      if (*target <= numeric_addresses.at(index))
        ++backward_targets;
    }
    std::cerr << "[post-layout] numeric-view direct=" << direct_branches
              << " numeric_targets=" << numeric_targets << " backward_targets=" << backward_targets
              << "\n";
  }
  std::optional<RewriteStep> best;

  const passes::PassResult pass = passes::run_preloaded_indirect_flow(
      view.numeric, passes::PassContext{.options = round_options},
      passes::IndirectFlowOptions{.relax_max_target_guard = true,
                                  .allow_forward_targets = round_options.forward_indirect_flow});
  if (trace) {
    std::cerr << "[post-layout] apply-one cells=" << machine_cell_count(items)
              << " ir=" << ir.size() << " pass_applied=" << pass.applied
              << " pass_preloads=" << pass.preloads.size()
              << " existing_preloads=" << existing_preloads.size() << "\n";
  }
  if (pass.applied > 0) {
    std::vector<std::pair<std::string, std::vector<int>>> groups;
    for (std::size_t index = 0; index < pass.ops.size(); ++index) {
      const IrOp& op = pass.ops.at(index);
      const IrOp& original = view.numeric.at(index);
      if (!is_indirect_branch_op(op) || !is_direct_branch_op(original) ||
          !view.target_labels.at(index).has_value()) {
        continue;
      }
      auto group_it = std::find_if(groups.begin(), groups.end(), [&](const auto& group) {
        return group.first == op.register_name;
      });
      if (group_it == groups.end()) {
        groups.push_back({op.register_name, {static_cast<int>(index)}});
      } else {
        group_it->second.push_back(static_cast<int>(index));
      }
    }
    for (const auto& [register_name, indices] : groups) {
      (void)register_name;
      std::optional<RewriteStep> group = validate_rewrite_group(
          indices, ir, view.numeric, view.target_labels, pass, items, round_options);
      if (!group.has_value()) {
        group = validate_rewrite_at(indices.front(), ir, view.numeric, view.target_labels, pass,
                                    items, round_options);
      }
      if (trace) {
        std::cerr << "[post-layout] group register=" << register_name
                  << " indices=" << indices.size()
                  << " valid=" << (group.has_value() ? "yes" : "no") << "\n";
      }
      best = better_rewrite(std::move(best), std::move(group));
    }
  }

  best = better_rewrite(std::move(best), apply_existing_selector_fixed_point_rewrite(
                                             ir, view.target_labels, items, round_options));
  best = better_rewrite(std::move(best), apply_existing_selector_exact_backward_rewrite(
                                             ir, view.target_labels, items, round_options));

  std::optional<RewriteStep> forward =
      apply_forward_rewrite(ir, view.target_labels, items, reserved, model);
  if (trace) {
    std::cerr << "[post-layout] forward_valid=" << (forward.has_value() ? "yes" : "no");
    if (forward.has_value()) {
      std::cerr << " converted=" << forward->converted << " selector=R"
                << forward->preload.register_name << "=" << forward->preload.value;
    }
    if (best.has_value()) {
      std::cerr << " best_before_forward converted=" << best->converted << " selector=R"
                << best->preload.register_name << "=" << best->preload.value;
    }
    std::cerr << "\n";
  }
  best = better_rewrite(std::move(best), std::move(forward));
  if (!best.has_value() && round_options.aggressive_post_layout_indirect_flow) {
    best = better_rewrite(std::move(best), apply_borrowed_entry_phase_rewrite(
                                               ir, view.target_labels, items, reserved, model));
  }
  return best;
}

std::vector<StopTailReuseBase> stop_tail_reuse_bases(const std::vector<MachineItem>& items,
                                                     const std::vector<PreloadReport>& preloads,
                                                     AddressSpaceModel model) {
  const std::vector<MachineCell> cells = machine_cells(items);
  std::vector<StopTailReuseBase> bases;
  for (const PreloadReport& preload : preloads) {
    const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
        preload.register_name, preload.value, IndirectOperationKind::Flow, model);
    if (!decoded.has_value() || !decoded->actual_flow_target.has_value())
      continue;
    const int target = *decoded->actual_flow_target;
    const std::optional<MachineCell> stop = machine_cell_at(cells, target);
    const std::optional<MachineCell> continuation = machine_cell_at(cells, target + 1);
    if (!stop.has_value() || !continuation.has_value() || stop->item == nullptr ||
        continuation->item == nullptr) {
      continue;
    }
    if (stop->item->kind != MachineItemKind::Op || stop->item->opcode != 0x50 ||
        continuation->item->kind != MachineItemKind::Op || continuation->item->opcode < 0x80 ||
        continuation->item->opcode > 0x8e) {
      continue;
    }
    bases.push_back(StopTailReuseBase{
        .register_name = preload.register_name,
        .target = target,
        .continuation_opcode = continuation->item->opcode,
    });
  }
  return bases;
}

std::optional<StopTailReuseRewrite>
find_stop_tail_reuse_rewrite(const std::vector<MachineItem>& items,
                             const std::vector<PreloadReport>& preloads, AddressSpaceModel model) {
  const std::vector<StopTailReuseBase> bases = stop_tail_reuse_bases(items, preloads, model);
  if (bases.empty())
    return std::nullopt;
  const std::vector<MachineCell> cells = machine_cells(items);
  const std::map<int, std::vector<std::string>> labels = machine_labels_by_address(items);
  const std::set<std::string> referenced = referenced_machine_labels(items);

  for (const StopTailReuseBase& base : bases) {
    for (const MachineCell& cell : cells) {
      if (cell.address <= base.target || cell.item == nullptr)
        continue;
      const std::optional<MachineCell> next = machine_cell_at(cells, cell.address + 1);
      const std::optional<MachineCell> after_next = machine_cell_at(cells, cell.address + 2);

      if (cell.item->kind == MachineItemKind::Op && cell.item->opcode == 0x50 && next.has_value() &&
          next->item != nullptr && next->item->kind == MachineItemKind::Op &&
          next->item->opcode == base.continuation_opcode &&
          !address_has_referenced_label(labels, referenced, next->address)) {
        return StopTailReuseRewrite{
            .base = base,
            .replace_index = cell.item_index,
            .remove_index = next->item_index,
            .zero_prefixed = false,
        };
      }

      if (cell.item->kind == MachineItemKind::Op &&
          (cell.item->opcode == 0x00 || cell.item->opcode == 0x0d) && next.has_value() &&
          next->item != nullptr && next->item->kind == MachineItemKind::Op &&
          next->item->opcode == 0x50 && after_next.has_value() && after_next->item != nullptr &&
          after_next->item->kind == MachineItemKind::Op &&
          after_next->item->opcode == base.continuation_opcode &&
          !address_has_referenced_label(labels, referenced, next->address) &&
          !address_has_referenced_label(labels, referenced, after_next->address)) {
        return StopTailReuseRewrite{
            .base = base,
            .replace_index = next->item_index,
            .remove_index = after_next->item_index,
            .zero_prefixed = true,
        };
      }
    }
  }
  return std::nullopt;
}

std::vector<MachineItem> apply_stop_tail_reuse_rewrite(const std::vector<MachineItem>& items,
                                                       const StopTailReuseRewrite& rewrite) {
  std::vector<MachineItem> result;
  result.reserve(items.size() - 1);
  const MachineItem& source = items.at(static_cast<std::size_t>(rewrite.replace_index));
  for (int index = 0; index < static_cast<int>(items.size()); ++index) {
    if (index == rewrite.remove_index)
      continue;
    if (index == rewrite.replace_index) {
      result.push_back(
          indirect_jump_machine_op(rewrite.base.register_name,
                                   std::string(rewrite.zero_prefixed ? "zero then " : "") +
                                       "reuse stop tail at " + std::to_string(rewrite.base.target),
                                   rewrite.base.target,
                                   source.source_line));
      continue;
    }
    result.push_back(items.at(static_cast<std::size_t>(index)));
  }
  return result;
}

std::optional<BranchRewrite>
find_existing_selector_flow_rewrite(const std::vector<MachineItem>& items,
                                    const std::vector<PreloadReport>& preloads,
                                    AddressSpaceModel model) {
  const std::vector<MachineCell> cells = machine_cells(items);
  const std::map<std::string, int> labels = machine_label_addresses(items);
  const std::map<int, std::vector<std::string>> labels_by_address =
      machine_labels_by_address(items);
  const std::set<std::string> referenced = referenced_machine_labels(items);

  for (std::size_t index = 0; index + 1 < cells.size(); ++index) {
    const MachineCell& branch = cells.at(index);
    const MachineCell& address = cells.at(index + 1);
    if (branch.item == nullptr || address.item == nullptr ||
        branch.item->kind != MachineItemKind::Op ||
        address.item->kind != MachineItemKind::Address) {
      continue;
    }
    const std::optional<int> target = resolved_machine_target(address.item->target, labels);
    if (!target.has_value())
      continue;
    for (const PreloadReport& preload : preloads) {
      const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
          preload.register_name, preload.value, IndirectOperationKind::Flow, model);
      if (!decoded.has_value() || decoded->actual_flow_target != *target)
        continue;
      const std::optional<int> opcode =
          indirect_branch_opcode_for_register(branch.item->opcode, preload.register_name);
      if (!opcode.has_value())
        continue;
      if (address_has_referenced_label(labels_by_address, referenced, address.address))
        continue;
      std::string comment = "reused preloaded R" + preload.register_name + "=" + preload.value +
                            " indirect-target=" + std::to_string(*target) + " direct flow";
      if (branch.item->comment.has_value() && !branch.item->comment->empty())
        comment = *branch.item->comment + "; " + comment;
      return BranchRewrite{
          .branch_index = branch.item_index,
          .address_index = address.item_index,
          .opcode = *opcode,
          .mnemonic = indirect_branch_mnemonic(*opcode, preload.register_name),
          .comment = comment,
          .source_line = branch.item->source_line,
          .target = address.item->target,
      };
    }
  }
  return std::nullopt;
}

std::optional<BranchRewrite>
find_branch_to_stop_tail_selector_rewrite(const std::vector<MachineItem>& items,
                                          const std::vector<PreloadReport>& preloads,
                                          AddressSpaceModel model) {
  const std::vector<MachineCell> cells = machine_cells(items);
  const std::map<std::string, int> labels = machine_label_addresses(items);
  const std::map<int, std::vector<std::string>> labels_by_address =
      machine_labels_by_address(items);
  const std::set<std::string> referenced = referenced_machine_labels(items);

  for (std::size_t index = 0; index + 1 < cells.size(); ++index) {
    const MachineCell& branch = cells.at(index);
    const MachineCell& address = cells.at(index + 1);
    if (branch.item == nullptr || address.item == nullptr ||
        branch.item->kind != MachineItemKind::Op ||
        address.item->kind != MachineItemKind::Address) {
      continue;
    }
    const std::optional<int> target = resolved_machine_target(address.item->target, labels);
    if (!target.has_value())
      continue;
    const std::optional<MachineCell> target_cell = machine_cell_at(cells, *target);
    if (!target_cell.has_value() || target_cell->item == nullptr ||
        target_cell->item->kind != MachineItemKind::Op) {
      continue;
    }
    const std::optional<std::string> register_name =
        register_from_indirect_opcode(target_cell->item->opcode);
    if (!register_name.has_value() ||
        target_cell->item->opcode - register_index(*register_name) != 0x80) {
      continue;
    }
    const std::optional<std::string> selector_value =
        preload_value_for_register(preloads, *register_name);
    if (!selector_value.has_value())
      continue;
    const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
        *register_name, *selector_value, IndirectOperationKind::Flow, model);
    if (!decoded.has_value() || !decoded->actual_flow_target.has_value())
      continue;
    const std::optional<MachineCell> stop = machine_cell_at(cells, *decoded->actual_flow_target);
    if (!stop.has_value() || stop->item == nullptr || stop->item->kind != MachineItemKind::Op ||
        stop->item->opcode != 0x50) {
      continue;
    }
    const std::optional<int> opcode =
        indirect_branch_opcode_for_register(branch.item->opcode, *register_name);
    if (!opcode.has_value())
      continue;
    if (address_has_referenced_label(labels_by_address, referenced, address.address))
      continue;
    std::string comment =
        "branch to reused stop tail at " + std::to_string(*decoded->actual_flow_target);
    if (branch.item->comment.has_value() && !branch.item->comment->empty())
      comment = *branch.item->comment + "; " + comment;
    return BranchRewrite{
        .branch_index = branch.item_index,
        .address_index = address.item_index,
        .opcode = *opcode,
        .mnemonic = indirect_branch_mnemonic(*opcode, *register_name),
        .comment = comment,
        .source_line = branch.item->source_line,
        .target = labels_by_address.contains(*decoded->actual_flow_target) &&
                          !labels_by_address.at(*decoded->actual_flow_target).empty()
                      ? IrTarget{labels_by_address.at(*decoded->actual_flow_target).front()}
                      : IrTarget{*decoded->actual_flow_target},
    };
  }
  return std::nullopt;
}

// Replace a direct `БП` to physical 01 with a one-cell `В/О` when every
// execution state of the branch has an empty return stack. The MK-61
// continues at physical 01 after an empty-stack В/О (pinned emulator fact),
// so the converted command transfers to the same target with the same stack.
// Deletion of the operand cell is admitted only when it cannot break any
// non-retargetable address encoding: every numeric direct operand and every
// typed indirect target must lie before the erased operand (this pass runs
// before selector values exist, so no preload retargeting is attempted).
std::optional<BranchRewrite>
find_empty_stack_loop_return_rewrite(const std::vector<MachineItem>& raw_items,
                                     AddressSpaceModel model) {
  const std::optional<std::vector<MachineItem>> normalized =
      normalize_natural_target_overflow_formals(raw_items, model);
  if (!normalized.has_value() || normalized->size() != raw_items.size())
    return std::nullopt;
  const std::vector<MachineItem>& items = *normalized;
  const std::vector<MachineCell> cells = machine_cells(items);
  const std::map<std::string, int> labels = machine_label_addresses(items);
  const std::map<int, std::vector<std::string>> labels_by_address =
      machine_labels_by_address(items);
  const std::set<std::string> referenced = referenced_machine_labels(items);

  std::optional<AuthoritativePostLayoutControlFlow> flow;

  for (std::size_t index = 0; index + 1 < cells.size(); ++index) {
    const MachineCell& branch = cells.at(index);
    const MachineCell& address = cells.at(index + 1);
    if (branch.item == nullptr || address.item == nullptr ||
        branch.item->kind != MachineItemKind::Op ||
        address.item->kind != MachineItemKind::Address || branch.item->opcode != 0x51) {
      continue;
    }
    const std::optional<int> target = resolved_machine_target(address.item->target, labels);
    if (!target.has_value() || *target != 1)
      continue;
    if (address_has_referenced_label(labels_by_address, referenced, address.address))
      continue;

    if (!flow.has_value()) {
      PostLayoutControlFlowOptions control_options;
      control_options.address_space_model = model;
      control_options.empty_return_target = 1;
      flow = build_post_layout_control_flow(items, control_options);
      if (!flow->proved) {
        PostLayoutControlFlowOptions bare_options;
        bare_options.address_space_model = model;
        flow = build_post_layout_control_flow(items, bare_options);
      }
      if (!flow->proved) {
        if (trace_post_layout_enabled()) {
          std::cerr << "[empty-stack-loop-return] control flow not proved:";
          for (const std::string& reason : flow->reasons)
            std::cerr << " | " << reason;
          std::cerr << "\n";
        }
        return std::nullopt;
      }
    }

    // Every execution state of the branch must carry an empty return stack;
    // an unexplored branch (no execution state) fails closed.
    bool reachable = false;
    bool always_empty_stack = true;
    for (const PostLayoutExecutionState& state : flow->execution_states) {
      if (state.item_index != static_cast<std::size_t>(branch.item_index))
        continue;
      reachable = true;
      if (!state.return_stack.empty()) {
        always_empty_stack = false;
        break;
      }
    }
    if (!reachable || !always_empty_stack) {
      if (trace_post_layout_enabled()) {
        std::cerr << "[empty-stack-loop-return] branch at " << branch.address
                  << (reachable ? " has a non-empty return stack" : " is unexplored") << "\n";
      }
      continue;
    }

    const auto deletion_is_encoding_safe = [&]() {
      for (const MachineItem& item : items) {
        if (item.kind == MachineItemKind::Address &&
            std::holds_alternative<int>(item.target) &&
            std::get<int>(item.target) >= address.address) {
          if (trace_post_layout_enabled()) {
            std::cerr << "[empty-stack-loop-return] numeric operand target "
                      << std::get<int>(item.target) << " blocks deletion at "
                      << address.address << "\n";
          }
          return false;
        }
      }
      for (const auto& [item_index, identities] : flow->indirect_flow_targets) {
        // At this pre-binding stage a symbolic indirect flow names its targets
        // by label only; the late binder and the retunable-selector machinery
        // derive every encoded address from the final layout, so such targets
        // follow the shifted labels. A source without complete label-typed
        // target meta may carry an already-frozen numeric encoding and fails
        // closed.
        const MachineItem& source = items.at(item_index);
        const bool symbolic =
            source.indirect_flow_targets.has_value() &&
            !source.indirect_flow_targets->empty() &&
            std::all_of(source.indirect_flow_targets->begin(),
                        source.indirect_flow_targets->end(), [](const IrTarget& flow_target) {
                          return std::holds_alternative<std::string>(flow_target);
                        });
        if (symbolic)
          continue;
        for (const PostLayoutCommandIdentity& identity : identities) {
          if (identity.address >= address.address) {
            if (trace_post_layout_enabled()) {
              std::cerr << "[empty-stack-loop-return] indirect target " << identity.address
                        << " (labels:";
              for (const std::string& label : identity.labels)
                std::cerr << " " << label;
              std::cerr << ") from opcode " << source.opcode << " comment "
                        << source.comment.value_or("<none>") << " blocks deletion at "
                        << address.address << "\n";
            }
            return false;
          }
        }
      }
      return true;
    };
    if (!deletion_is_encoding_safe()) {
      if (trace_post_layout_enabled()) {
        std::cerr << "[empty-stack-loop-return] deletion unsafe for operand at "
                  << address.address << "\n";
      }
      continue;
    }

    // The exact comment is the ecosystem-wide marker (see
    // return_acts_as_address_one_jump in the CFG helpers): a later
    // machine-to-IR raise must keep modeling this В/О with its physical-01
    // continuation instead of treating it as a terminal return.
    return BranchRewrite{
        .branch_index = branch.item_index,
        .address_index = address.item_index,
        .opcode = 0x52,
        .mnemonic = "В/О",
        .comment = "optimized БП 01",
        .source_line = branch.item->source_line,
        .target = address.item->target,
    };
  }
  return std::nullopt;
}

// Replace a direct `ПП addr` / `БП addr` with a one-cell indirect flow through
// a stable register whose exact value at the branch site is proved by the
// flow-sensitive stable-register value analysis over the authoritative
// execution-state graph. Unlike the preloaded rewrites above, the selector
// value here may come from a runtime charge (digit entry stored into the
// register) that the program already performs for another indirect consumer.
//
// Conditionals are excluded (they would need the separate X2 equivalence
// proof), and the deletion is admitted only when it cannot break any
// non-retargetable address encoding: the converted flow must be backward
// (target before the erased operand), every numeric direct operand and every
// typed indirect target whose register has no retargetable preload must also
// lie before the erased operand.
std::optional<BranchRewrite>
find_charged_selector_flow_rewrite(const std::vector<MachineItem>& raw_items,
                                   const std::vector<PreloadReport>& preloads,
                                   AddressSpaceModel model) {
  // Beyond-window artifacts carry resolver-generated wrapped formal operands;
  // recover their authoritative identities before building the execution
  // graph, exactly like the natural-target layout pass does. The candidate
  // scan uses the normalized items, while the caller deletes by item index,
  // so the normalization must preserve the item sequence.
  const std::optional<std::vector<MachineItem>> normalized =
      normalize_natural_target_overflow_formals(raw_items, model);
  if (!normalized.has_value() || normalized->size() != raw_items.size())
    return std::nullopt;
  const std::vector<MachineItem>& items = *normalized;
  const std::vector<MachineCell> cells = machine_cells(items);
  const std::map<std::string, int> labels = machine_label_addresses(items);
  const std::map<int, std::vector<std::string>> labels_by_address =
      machine_labels_by_address(items);
  const std::set<std::string> referenced = referenced_machine_labels(items);
  std::set<std::string> preloaded_registers;
  for (const PreloadReport& preload : preloads)
    preloaded_registers.insert(preload.register_name);

  std::optional<AuthoritativePostLayoutControlFlow> flow;
  std::optional<StableRegisterValueFlow> values;

  for (std::size_t index = 0; index + 1 < cells.size(); ++index) {
    const MachineCell& branch = cells.at(index);
    const MachineCell& address = cells.at(index + 1);
    if (branch.item == nullptr || address.item == nullptr ||
        branch.item->kind != MachineItemKind::Op ||
        address.item->kind != MachineItemKind::Address ||
        (branch.item->opcode != 0x51 && branch.item->opcode != 0x53)) {
      continue;
    }
    const std::optional<int> target = resolved_machine_target(address.item->target, labels);
    if (!target.has_value() || *target >= address.address)
      continue;
    if (address_has_referenced_label(labels_by_address, referenced, address.address))
      continue;

    if (!flow.has_value()) {
      // The MK-61 continues at physical 01 when В/О runs on an empty return
      // stack. Artifacts whose cell 01 is an address operand can only be
      // proved without that entry, which stays sound because the builder
      // fails closed when an empty-stack В/О is actually reachable.
      PostLayoutControlFlowOptions control_options;
      control_options.address_space_model = model;
      control_options.empty_return_target = 1;
      flow = build_post_layout_control_flow(items, control_options);
      if (!flow->proved) {
        PostLayoutControlFlowOptions bare_options;
        bare_options.address_space_model = model;
        flow = build_post_layout_control_flow(items, bare_options);
      }
      if (!flow->proved) {
        if (trace_post_layout_enabled())
          std::cerr << "[charged-selector] control flow not proved\n";
        return std::nullopt;
      }
      values = analyze_stable_register_value_flow(items, preloads, *flow, model);
      if (!values->proved) {
        if (trace_post_layout_enabled()) {
          std::cerr << "[charged-selector] value flow not proved:";
          for (const std::string& reason : values->reasons)
            std::cerr << " " << reason;
          std::cerr << "\n";
        }
        return std::nullopt;
      }
    }

    // The deletion machinery retargets preloaded selectors, and label
    // operands follow their items; everything else must stay put after this
    // candidate's operand cell is erased.
    const auto deletion_is_encoding_safe = [&]() {
      for (const MachineItem& item : items) {
        if (item.kind == MachineItemKind::Address &&
            std::holds_alternative<int>(item.target) &&
            std::get<int>(item.target) >= address.address) {
          return false;
        }
      }
      for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
        const auto typed = flow->indirect_flow_targets.find(item_index);
        if (typed == flow->indirect_flow_targets.end())
          continue;
        const std::optional<std::string> register_name =
            register_from_indirect_opcode(items.at(item_index).opcode);
        if (register_name.has_value() && preloaded_registers.contains(*register_name))
          continue;
        for (const PostLayoutCommandIdentity& identity : typed->second) {
          if (identity.address >= address.address)
            return false;
        }
      }
      return true;
    };
    if (!deletion_is_encoding_safe()) {
      if (trace_post_layout_enabled()) {
        std::cerr << "[charged-selector] deletion unsafe for operand at "
                  << address.address << "\n";
      }
      continue;
    }

    const auto known =
        values->before_item.find(static_cast<std::size_t>(branch.item_index));
    if (known == values->before_item.end()) {
      if (trace_post_layout_enabled()) {
        std::cerr << "[charged-selector] no value state at branch address "
                  << branch.address << "\n";
      }
      continue;
    }
    if (trace_post_layout_enabled()) {
      std::cerr << "[charged-selector] branch at " << branch.address << " target " << *target
                << " values:";
      for (std::size_t slot = 0; slot < known->second.size(); ++slot) {
        if (known->second[slot].has_value())
          std::cerr << " R" << "789abcde"[slot] << "=" << *known->second[slot];
      }
      std::cerr << "\n";
    }
    for (std::size_t slot = 0; slot < known->second.size(); ++slot) {
      const std::optional<std::string>& value = known->second[slot];
      if (!value.has_value())
        continue;
      const std::string register_name(1, "789abcde"[slot]);
      std::optional<IndirectAddressEvaluation> decoded;
      try {
        decoded = evaluate_indirect_address(register_name, *value,
                                            IndirectOperationKind::Flow, model);
      } catch (const std::exception&) {
        continue;
      }
      if (!decoded.has_value() || decoded->actual_flow_target != *target)
        continue;
      // The write-back must not change the observable register value.
      if (!decoded->result_value.has_value() || *decoded->result_value != *value)
        continue;
      const std::optional<int> opcode =
          indirect_branch_opcode_for_register(branch.item->opcode, register_name);
      if (!opcode.has_value())
        continue;
      if (trace_post_layout_enabled()) {
        std::cerr << "[charged-selector] converting branch at " << branch.address
                  << " opcode " << branch.item->opcode << " through R" << register_name
                  << "=" << *value << " target " << *target << "\n";
      }
      std::string comment = "reused runtime-charged R" + register_name + "=" + *value +
                            " indirect-target=" + std::to_string(*target) + " direct flow";
      if (branch.item->comment.has_value() && !branch.item->comment->empty())
        comment = *branch.item->comment + "; " + comment;
      return BranchRewrite{
          .branch_index = branch.item_index,
          .address_index = address.item_index,
          .opcode = *opcode,
          .mnemonic = indirect_branch_mnemonic(*opcode, register_name),
          .comment = comment,
          .source_line = branch.item->source_line,
          .target = address.item->target,
      };
    }
  }
  return std::nullopt;
}

std::vector<MachineItem> apply_branch_rewrite(const std::vector<MachineItem>& items,
                                              const BranchRewrite& rewrite) {
  std::vector<MachineItem> result;
  result.reserve(items.size() - 1);
  for (int index = 0; index < static_cast<int>(items.size()); ++index) {
    if (index == rewrite.address_index)
      continue;
    if (index == rewrite.branch_index) {
      MachineItem item = MachineItem::op(rewrite.opcode, rewrite.mnemonic);
      item.comment = rewrite.comment;
      item.indirect_flow_targets = std::vector<IrTarget>{rewrite.target};
      if (rewrite.source_line.has_value())
        item.source_line = *rewrite.source_line;
      result.push_back(item);
      continue;
    }
    result.push_back(items.at(static_cast<std::size_t>(index)));
  }
  return result;
}

} // namespace

int machine_cell_count(const std::vector<MachineItem>& items) {
  int count = 0;
  for (const MachineItem& item : items) {
    if (item.kind != MachineItemKind::Label)
      ++count;
  }
  return count;
}

PostLayoutIndirectFlowResult
optimize_post_layout_indirect_flow(const std::vector<MachineItem>& items,
                                   const CompileOptions& options, int rescue_above) {
  const bool trace = trace_post_layout_enabled();
  if (trace) {
    std::cerr << "[post-layout] start cells=" << machine_cell_count(items)
              << " rescue_above=" << rescue_above
              << " aggressive=" << options.aggressive_post_layout_indirect_flow
              << " preloaded=" << options.preloaded_indirect_flow << "\n";
  }
  if (machine_cell_count(items) <= rescue_above) {
    if (trace)
      std::cerr << "[post-layout] skipped by threshold\n";
    return PostLayoutIndirectFlowResult{
        .items = items,
    };
  }

  std::vector<MachineItem> current = items;
  std::vector<PreloadReport> preloads;
  int applied = 0;
  int super_dark_applied = 0;
  int dark_entry_applied = 0;
  int existing_selector_applied = 0;
  int borrowed_entry_phase_applied = 0;
  std::vector<int> immutable_targets;
  const AddressSpaceModel model = address_space_model_for_options(options);
  for (int round = 0; round < kMaxRewrites; ++round) {
    const std::optional<RewriteStep> step = apply_one_rewrite(current, options, preloads);
    if (!step.has_value()) {
      if (trace)
        std::cerr << "[post-layout] round=" << round << " no-step\n";
      break;
    }
    if (trace) {
      std::cerr << "[post-layout] round=" << round << " step converted=" << step->converted
                << " selector=R" << step->preload.register_name << "=" << step->preload.value
                << " cells_before=" << machine_cell_count(current)
                << " cells_after_step=" << machine_cell_count(step->items)
                << " dark=" << step->dark_entry << " super_dark=" << step->super_dark
                << " existing=" << step->existing_preload
                << " borrowed-entry=" << step->borrowed_entry_phase << "\n";
    }
    const bool crosses_immutable_target = std::any_of(
        step->converted_addresses.begin(), step->converted_addresses.end(), [&](int address) {
          return std::any_of(immutable_targets.begin(), immutable_targets.end(),
                             [address](int target) { return address < target; });
        });
    if (crosses_immutable_target)
      break;

    const std::vector<IrOp> before_ops =
        raise_machine_to_ir(current, effective_optimizer_feature_profile(options));
    const std::optional<RetargetedIr> retargeted =
        retarget_existing_selectors_after_shift(before_ops, step->ops, preloads, model);
    if (!retargeted.has_value()) {
      if (trace)
        std::cerr << "[post-layout] round=" << round << " retarget failed\n";
      break;
    }

    current = retargeted->items;
    preloads = retargeted->preloads;
    if (step->existing_preload) {
      existing_selector_applied += step->converted;
      immutable_targets.insert(immutable_targets.end(), step->protected_targets.begin(),
                               step->protected_targets.end());
    } else {
      preloads.push_back(step->preload);
    }
    applied += step->converted;
    if (step->super_dark)
      ++super_dark_applied;
    if (step->dark_entry)
      ++dark_entry_applied;
    if (step->borrowed_entry_phase)
      borrowed_entry_phase_applied += step->converted;
  }

  if (applied == 0) {
    if (trace)
      std::cerr << "[post-layout] no rewrites applied\n";
    return PostLayoutIndirectFlowResult{
        .items = items,
    };
  }

  MergeDuplicateSelectorsResult merge = merge_duplicate_selectors(current, preloads, model);
  current = std::move(merge.items);
  preloads = std::move(merge.preloads);
  if (borrowed_entry_phase_applied > 0 && !borrowed_entry_phase_proof(current, model).proved) {
    if (trace)
      std::cerr << "[post-layout] final borrowed-entry proof failed\n";
    return PostLayoutIndirectFlowResult{.items = items};
  }

  std::vector<passes::AppliedOptimization> optimizations = {
      passes::AppliedOptimization{
          .name = "preloaded-indirect-flow",
          .detail = "Activated post-layout: replaced " + std::to_string(applied) +
                    " direct branch/call(s) with proven preloaded indirect flow.",
      },
  };
  if (super_dark_applied > 0) {
    optimizations.push_back(passes::AppliedOptimization{
        .name = "preloaded-super-dark-flow",
        .detail = "Selected " + std::to_string(super_dark_applied) +
                  " FA..FF one-command super-dark dispatch" +
                  (super_dark_applied == 1 ? "" : "es") +
                  ", each proven to fall through to its 01..06 continuation.",
    });
  }
  if (dark_entry_applied > 0) {
    optimizations.push_back(passes::AppliedOptimization{
        .name = "dark-entry-layout",
        .detail = "Pointed " + std::to_string(dark_entry_applied) +
                  " branch(es) at an executable suffix through a proven dark-entry formal "
                  "address (beyond the official program window).",
    });
  }
  if (borrowed_entry_phase_applied > 0) {
    optimizations.push_back(passes::AppliedOptimization{
        .name = "borrowed-entry-phase-selector",
        .detail = "Borrowed an allocated register's dead entry phase for " +
                  std::to_string(borrowed_entry_phase_applied) + " indirect-flow selector use" +
                  (borrowed_entry_phase_applied == 1 ? "" : "s") +
                  " after exact cyclic (PC, return-stack) proof.",
    });
  }
  if (merge.merged > 0) {
    optimizations.push_back(passes::AppliedOptimization{
        .name = "constants-dual-use",
        .detail = "Shared " + std::to_string(merge.merged) + " duplicate selector register" +
                  (merge.merged == 1 ? "" : "s") +
                  ": one stored constant now drives several dispatch sites, freeing the rest.",
    });
  }
  if (existing_selector_applied > 0) {
    optimizations.push_back(passes::AppliedOptimization{
        .name = "constants-dual-use",
        .detail = "Reused existing setup constant preload" +
                  std::string(existing_selector_applied == 1 ? "" : "s") +
                  " as immutable indirect-flow selector" +
                  (existing_selector_applied == 1 ? "" : "s") + " for " +
                  std::to_string(existing_selector_applied) + " branch/call site" +
                  (existing_selector_applied == 1 ? "" : "s") + ".",
    });
  }
  return PostLayoutIndirectFlowResult{
      .items = std::move(current),
      .preloads = std::move(preloads),
      .optimizations = std::move(optimizations),
      .applied = applied,
  };
}

PostLayoutIndirectFlowResult
optimize_post_layout_fractional_r0_flow(const std::vector<MachineItem>& items,
                                        const std::vector<PreloadReport>& existing_flow_preloads,
                                        const CompileOptions& options) {
  if (!existing_flow_preloads.empty()) {
    return PostLayoutIndirectFlowResult{
        .items = items,
    };
  }

  std::vector<MachineItem> current = items;
  int applied = 0;
  for (int round = 0; round < 8; ++round) {
    const std::optional<PostLayoutIndirectFlowResult> result =
        apply_fractional_r0_flow_rewrite(current,
                                         effective_optimizer_feature_profile(options));
    if (!result.has_value())
      break;
    current = result->items;
    applied += result->applied;
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
                  .name = "r0-fractional-sentinel",
                  .detail = "Activated post-layout: replaced " + std::to_string(applied) +
                            " direct flow op(s) whose target resolves to address 99 with "
                            "fractional R0 indirect flow.",
              },
          },
      .applied = applied,
  };
}

PostLayoutIndirectFlowResult
optimize_post_layout_address_code_overlay(const std::vector<MachineItem>& items,
                                          const std::vector<PreloadReport>& preloads,
                                          const CompileOptions& options) {
  const AddressSpaceModel model = address_space_model_for_options(options);
  std::vector<MachineItem> current = items;
  std::vector<PreloadReport> current_preloads = preloads;
  int applied = 0;
  for (int round = 0; round < 16; ++round) {
    std::optional<AddressCodeOverlayApplication> result =
        apply_address_code_overlay(current, model);
    if (!result.has_value())
      break;
    const std::optional<std::vector<PreloadReport>> retargeted =
        retarget_selector_preloads_after_overlay(result->items, current_preloads,
                                                 result->removed_cell_address,
                                                 result->overlaid_cell_address, model);
    if (!retargeted.has_value())
      break;
    std::map<std::string, std::string> selector_by_register;
    for (const PreloadReport& preload : *retargeted)
      selector_by_register[preload.register_name] = preload.value;
    current =
        retarget_machine_selector_comments(std::move(result->items), selector_by_register, model);
    current_preloads = *retargeted;
    ++applied;
  }

  if (applied == 0) {
    return PostLayoutIndirectFlowResult{
        .items = items,
        .preloads = preloads,
    };
  }

  return PostLayoutIndirectFlowResult{
      .items = std::move(current),
      .preloads = std::move(current_preloads),
      .optimizations =
          {
              passes::AppliedOptimization{
                  .name = "address-code-overlay",
                  .detail = "Overlaid " + std::to_string(applied) + " executable cell" +
                            (applied == 1 ? "" : "s") + " onto direct-jump address byte" +
                            (applied == 1 ? "" : "s") + " after post-layout proof.",
              },
          },
      .applied = applied,
  };
}

PostLayoutIndirectFlowResult
optimize_post_layout_error_padding_code_overlay(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const CompileOptions& options) {
  const AddressSpaceModel model = address_space_model_for_options(options);
  std::vector<MachineItem> current = items;
  std::vector<PreloadReport> current_preloads = preloads;
  int applied = 0;
  for (int round = 0; round < 16; ++round) {
    const std::optional<AddressCodeOverlayApplication> result =
        apply_error_padding_code_overlay(current, model);
    if (!result.has_value())
      break;
    const std::optional<std::vector<PreloadReport>> retargeted =
        retarget_selector_preloads_after_overlay(
            result->items, current_preloads, result->removed_cell_address,
            result->overlaid_cell_address, model);
    if (!retargeted.has_value())
      break;
    std::map<std::string, std::string> selector_by_register;
    for (const PreloadReport& preload : *retargeted)
      selector_by_register[preload.register_name] = preload.value;
    current = retarget_machine_selector_comments(
        std::move(result->items), selector_by_register, model);
    current_preloads = *retargeted;
    ++applied;
  }

  if (applied == 0) {
    return PostLayoutIndirectFlowResult{
        .items = items,
        .preloads = preloads,
    };
  }
  return PostLayoutIndirectFlowResult{
      .items = std::move(current),
      .preloads = std::move(current_preloads),
      .optimizations =
          {
              passes::AppliedOptimization{
                  .name = "error-padding-code-overlay",
                  .detail =
                      "Moved " + std::to_string(applied) +
                      " separately addressed one-cell В/О command" +
                      (applied == 1 ? "" : "s") +
                      " into a retained resumable ЕГГ0Г padding cell after "
                      "final CFG/address/stack/X2/return-stack proof.",
              },
          },
      .applied = applied,
  };
}

PostLayoutIndirectFlowResult
optimize_post_layout_code_overlays(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const CompileOptions& options) {
  PostLayoutIndirectFlowResult error_padding =
      optimize_post_layout_error_padding_code_overlay(items, preloads, options);
  PostLayoutIndirectFlowResult address =
      optimize_post_layout_address_code_overlay(
          error_padding.items, error_padding.preloads, options);
  std::vector<passes::AppliedOptimization> optimizations =
      std::move(error_padding.optimizations);
  optimizations.insert(optimizations.end(), address.optimizations.begin(),
                       address.optimizations.end());
  return PostLayoutIndirectFlowResult{
      .items = std::move(address.items),
      .preloads = std::move(address.preloads),
      .optimizations = std::move(optimizations),
      .applied = error_padding.applied + address.applied,
  };
}

PostLayoutIndirectFlowResult
optimize_post_layout_super_dark_address_overlay(const std::vector<MachineItem>& items,
                                                const CompileOptions& options,
                                                int rescue_above) {
  PostLayoutIndirectFlowResult baseline =
      optimize_post_layout_indirect_flow(items, options, rescue_above);
  const PostLayoutIndirectFlowResult overlay =
      optimize_post_layout_address_code_overlay(items, {}, options);
  if (overlay.applied == 0 || !has_super_dark_overlay_continuation(overlay.items))
    return baseline;

  PostLayoutIndirectFlowResult combined =
      optimize_post_layout_indirect_flow(overlay.items, options, rescue_above);
  const AddressSpaceModel model = address_space_model_for_options(options);
  const bool selected_super_dark =
      std::any_of(combined.optimizations.begin(), combined.optimizations.end(),
                  [](const passes::AppliedOptimization& optimization) {
                    return optimization.name == "preloaded-super-dark-flow";
                  });
  if (!selected_super_dark ||
      !final_address_code_overlay_artifacts_match(combined.items, overlay.applied, model) ||
      machine_cell_count(combined.items) >= machine_cell_count(baseline.items)) {
    return baseline;
  }

  std::vector<passes::AppliedOptimization> optimizations = overlay.optimizations;
  optimizations.insert(optimizations.end(), combined.optimizations.begin(),
                       combined.optimizations.end());
  optimizations.push_back(passes::AppliedOptimization{
      .name = "super-dark-address-code-overlay",
      .detail = "Packed " + std::to_string(overlay.applied) +
                " super-dark continuation cell" + (overlay.applied == 1 ? "" : "s") +
                " into a direct-flow address byte before selecting FA..FF dispatch.",
  });
  combined.applied += overlay.applied;
  combined.optimizations = std::move(optimizations);
  return combined;
}

PostLayoutIndirectFlowResult
optimize_post_layout_empty_stack_loop_return(const std::vector<MachineItem>& items,
                                             const CompileOptions& options) {
  const AddressSpaceModel model = address_space_model_for_options(options);
  std::vector<MachineItem> current = items;
  int applied = 0;

  for (int round = 0; round < kMaxRewrites; ++round) {
    const std::optional<BranchRewrite> rewrite =
        find_empty_stack_loop_return_rewrite(current, model);
    if (!rewrite.has_value())
      break;

    // Apply by hand: the converted command is a plain В/О, not indirect flow.
    std::vector<MachineItem> candidate;
    candidate.reserve(current.size() - 1);
    for (int index = 0; index < static_cast<int>(current.size()); ++index) {
      if (index == rewrite->address_index)
        continue;
      if (index == rewrite->branch_index) {
        MachineItem item = MachineItem::op(rewrite->opcode, rewrite->mnemonic);
        item.comment = rewrite->comment;
        if (rewrite->source_line.has_value())
          item.source_line = *rewrite->source_line;
        candidate.push_back(std::move(item));
        continue;
      }
      candidate.push_back(current.at(static_cast<std::size_t>(index)));
    }
    if (machine_cell_count(candidate) >= machine_cell_count(current))
      break;

    // The converted artifact must independently re-prove with the pinned
    // empty-return continuation, and that continuation must still be the
    // physical cell 01 the deleted operand named.
    const std::optional<std::vector<MachineItem>> verified =
        normalize_natural_target_overflow_formals(candidate, model);
    if (!verified.has_value())
      break;
    PostLayoutControlFlowOptions verify_options;
    verify_options.address_space_model = model;
    verify_options.empty_return_target = 1;
    const AuthoritativePostLayoutControlFlow verify =
        build_post_layout_control_flow(*verified, verify_options);
    if (!verify.proved || !verify.empty_return_target.has_value() ||
        verify.empty_return_target->address != 1) {
      if (trace_post_layout_enabled())
        std::cerr << "[empty-stack-loop-return] converted artifact failed re-proof\n";
      break;
    }

    current = std::move(candidate);
    ++applied;
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
                  .name = "post-layout-empty-stack-loop-return",
                  .detail = "Replaced " + std::to_string(applied) + " direct jump" +
                            (applied == 1 ? "" : "s") +
                            " to physical 01 with one-cell В/О after proving an empty "
                            "return stack in every execution state (the MK-61 continues "
                            "at 01 after an empty-stack В/О).",
              },
          },
      .applied = applied,
  };
}

PostLayoutIndirectFlowResult
optimize_post_layout_charged_selector_flow(const std::vector<MachineItem>& items,
                                           const std::vector<PreloadReport>& preloads,
                                           const CompileOptions& options) {
  const AddressSpaceModel model = address_space_model_for_options(options);
  std::vector<MachineItem> current = items;
  std::vector<PreloadReport> current_preloads = preloads;
  int applied = 0;

  for (int round = 0; round < kMaxRewrites; ++round) {
    const std::optional<BranchRewrite> rewrite =
        find_charged_selector_flow_rewrite(current, current_preloads, model);
    if (!rewrite.has_value())
      break;
    std::vector<MachineItem> candidate = apply_branch_rewrite(current, *rewrite);
    if (machine_cell_count(candidate) >= machine_cell_count(current))
      break;
    const std::optional<RetargetedMachine> retargeted =
        retarget_selector_preloads_after_machine_deletion(
            current, std::move(candidate), current_preloads, {rewrite->address_index},
            rewrite->branch_index, model);
    if (!retargeted.has_value())
      break;
    current = retargeted->items;
    current_preloads = retargeted->preloads;
    ++applied;
  }

  if (applied == 0) {
    return PostLayoutIndirectFlowResult{
        .items = items,
        .preloads = preloads,
    };
  }
  return PostLayoutIndirectFlowResult{
      .items = std::move(current),
      .preloads = std::move(current_preloads),
      .optimizations =
          {
              passes::AppliedOptimization{
                  .name = "post-layout-charged-selector-flow",
                  .detail = "Replaced " + std::to_string(applied) + " direct branch/call" +
                            (applied == 1 ? "" : "s") +
                            " with indirect flow through a stable register whose "
                            "runtime-charged value is proved on every path by the "
                            "execution-state graph.",
              },
          },
      .applied = applied,
  };
}

PostLayoutIndirectFlowResult
optimize_post_layout_stop_tail_reuse(const std::vector<MachineItem>& items,
                                     const std::vector<PreloadReport>& preloads,
                                     const CompileOptions& options) {
  const AddressSpaceModel model = address_space_model_for_options(options);
  std::vector<MachineItem> current = items;
  std::vector<PreloadReport> current_preloads = preloads;
  int stop_tail_applied = 0;
  int existing_selector_applied = 0;
  int charged_selector_applied = 0;
  int empty_stack_tail_call_applied = 0;
  int empty_stack_tail_fallthrough_applied = 0;

  for (int round = 0; round < kMaxRewrites; ++round) {
    const std::map<int, int> address_by_item = machine_address_by_item_index(current);

    if (const std::optional<EmptyStackTailCallRewrite> rewrite =
            find_empty_stack_tail_call_rewrite(current, current_preloads, model)) {
      std::vector<MachineItem> candidate = apply_empty_stack_tail_call_rewrite(current, *rewrite);
      if (machine_cell_count(candidate) >= machine_cell_count(current))
        break;
      std::vector<int> removed_indices = {rewrite->loop_back_index};
      if (rewrite->loop_back_address_index.has_value())
        removed_indices.push_back(*rewrite->loop_back_address_index);
      if (rewrite->natural_fallthrough) {
        removed_indices.push_back(rewrite->call_index);
        removed_indices.push_back(rewrite->call_address_index);
      }
      const std::optional<RetargetedMachine> retargeted =
          retarget_selector_preloads_after_machine_deletion(
              current, std::move(candidate), current_preloads, std::move(removed_indices),
              rewrite->natural_fallthrough ? -1 : rewrite->call_index, model);
      if (!retargeted.has_value())
        break;
      current = retargeted->items;
      current_preloads = retargeted->preloads;
      ++empty_stack_tail_call_applied;
      if (rewrite->natural_fallthrough)
        ++empty_stack_tail_fallthrough_applied;
      continue;
    }

    if (const std::optional<BranchRewrite> rewrite =
            find_existing_selector_flow_rewrite(current, current_preloads, model)) {
      std::vector<MachineItem> candidate = apply_branch_rewrite(current, *rewrite);
      if (machine_cell_count(candidate) >= machine_cell_count(current))
        break;
      const std::optional<RetargetedMachine> retargeted =
          retarget_selector_preloads_after_machine_deletion(
              current, std::move(candidate), current_preloads, {rewrite->address_index},
              rewrite->branch_index, model);
      if (!retargeted.has_value())
        break;
      current = retargeted->items;
      current_preloads = retargeted->preloads;
      ++existing_selector_applied;
      continue;
    }

    if (const std::optional<BranchRewrite> rewrite =
            find_branch_to_stop_tail_selector_rewrite(current, current_preloads, model)) {
      std::vector<MachineItem> candidate = apply_branch_rewrite(current, *rewrite);
      if (machine_cell_count(candidate) >= machine_cell_count(current))
        break;
      const std::optional<RetargetedMachine> retargeted =
          retarget_selector_preloads_after_machine_deletion(
              current, std::move(candidate), current_preloads, {rewrite->address_index},
              rewrite->branch_index, model);
      if (!retargeted.has_value())
        break;
      current = retargeted->items;
      current_preloads = retargeted->preloads;
      ++stop_tail_applied;
      continue;
    }

    if (const std::optional<StopTailReuseRewrite> rewrite =
            find_stop_tail_reuse_rewrite(current, current_preloads, model)) {
      std::vector<MachineItem> candidate = apply_stop_tail_reuse_rewrite(current, *rewrite);
      if (machine_cell_count(candidate) >= machine_cell_count(current))
        break;
      const std::optional<RetargetedMachine> retargeted =
          retarget_selector_preloads_after_machine_deletion(
              current, std::move(candidate), current_preloads, {rewrite->remove_index},
              rewrite->replace_index, model);
      if (!retargeted.has_value())
        break;
      current = retargeted->items;
      current_preloads = retargeted->preloads;
      ++stop_tail_applied;
      continue;
    }

    if (const std::optional<BranchRewrite> rewrite =
            find_charged_selector_flow_rewrite(current, current_preloads, model)) {
      std::vector<MachineItem> candidate = apply_branch_rewrite(current, *rewrite);
      if (machine_cell_count(candidate) >= machine_cell_count(current))
        break;
      const std::optional<RetargetedMachine> retargeted =
          retarget_selector_preloads_after_machine_deletion(
              current, std::move(candidate), current_preloads, {rewrite->address_index},
              rewrite->branch_index, model);
      if (!retargeted.has_value())
        break;
      current = retargeted->items;
      current_preloads = retargeted->preloads;
      ++charged_selector_applied;
      continue;
    }

    break;
  }

  const int applied = stop_tail_applied + existing_selector_applied +
                      charged_selector_applied + empty_stack_tail_call_applied;
  if (applied == 0) {
    return PostLayoutIndirectFlowResult{
        .items = items,
        .preloads = preloads,
    };
  }

  std::vector<passes::AppliedOptimization> optimizations;
  if (empty_stack_tail_call_applied > 0) {
    optimizations.push_back(passes::AppliedOptimization{
        .name = "post-layout-empty-stack-tail-call",
        .detail = "Replaced " + std::to_string(empty_stack_tail_call_applied) +
                  " terminal main-loop call" + (empty_stack_tail_call_applied == 1 ? "" : "s") +
                  " with direct jump(s) whose final В/О returns through the empty stack to the "
                  "loop head.",
    });
  }
  if (empty_stack_tail_fallthrough_applied > 0) {
    optimizations.push_back(passes::AppliedOptimization{
        .name = "post-layout-empty-stack-tail-fallthrough",
        .detail = "Removed " + std::to_string(empty_stack_tail_fallthrough_applied) +
                  " empty-stack tail transfer" +
                  (empty_stack_tail_fallthrough_applied == 1 ? "" : "s") +
                  " whose callee was the next physical component.",
    });
  }
  if (stop_tail_applied > 0) {
    optimizations.push_back(passes::AppliedOptimization{
        .name = "post-layout-stop-tail-reuse",
        .detail = "Replaced " + std::to_string(stop_tail_applied) + " repeated stop tail" +
                  (stop_tail_applied == 1 ? "" : "s") +
                  " with proven preloaded indirect jumps to an existing stop tail.",
    });
  }
  if (existing_selector_applied > 0) {
    optimizations.push_back(passes::AppliedOptimization{
        .name = "post-layout-existing-selector-flow",
        .detail = "Replaced " + std::to_string(existing_selector_applied) + " direct branch/call" +
                  (existing_selector_applied == 1 ? "" : "s") +
                  " with already-proven preloaded selector flow after retargeting shifted "
                  "selectors.",
    });
  }
  if (charged_selector_applied > 0) {
    optimizations.push_back(passes::AppliedOptimization{
        .name = "post-layout-charged-selector-flow",
        .detail = "Replaced " + std::to_string(charged_selector_applied) + " direct branch/call" +
                  (charged_selector_applied == 1 ? "" : "s") +
                  " with indirect flow through a stable register whose runtime-charged value "
                  "is proved on every path by the execution-state graph.",
    });
  }

  return PostLayoutIndirectFlowResult{
      .items = std::move(current),
      .preloads = std::move(current_preloads),
      .optimizations = std::move(optimizations),
      .applied = applied,
  };
}

} // namespace mkpro::core
