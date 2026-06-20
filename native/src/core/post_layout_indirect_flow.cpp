#include "mkpro/core/post_layout_indirect_flow.hpp"

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/passes/preloaded_indirect_flow.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
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

struct OverlayExecutable {
  int opcode = 0;
  std::string mnemonic;
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

struct BranchRewrite {
  int branch_index = 0;
  int address_index = 0;
  int opcode = 0;
  std::string mnemonic;
  std::string comment;
  std::optional<int> source_line;
};

struct RetargetedMachine {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
};

std::vector<passes::AppliedOptimization> post_layout_optimizations(int applied);

bool is_direct_flow(const IrOp& op) {
  return op.kind == IrKind::Jump || op.kind == IrKind::Call || op.kind == IrKind::CondJump ||
         op.kind == IrKind::Loop;
}

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

std::optional<int> fixed_address_actual_target(const MachineItem& address) {
  if (address.kind != MachineItemKind::Address)
    return std::nullopt;
  if (address.formal_opcode.has_value())
    return formal_address_info(*address.formal_opcode).actual;
  if (const auto* numeric = std::get_if<int>(&address.target))
    return *numeric;
  return std::nullopt;
}

std::optional<int> address_opcode_for_item(const std::vector<MachineItem>& items,
                                           const MachineItem& item) {
  if (item.kind != MachineItemKind::Address)
    return std::nullopt;
  if (item.formal_opcode.has_value())
    return *item.formal_opcode;

  const std::map<std::string, int> labels = machine_label_addresses(items);
  const std::optional<int> target = resolved_machine_target(item.target, labels);
  if (!target.has_value())
    return std::nullopt;
  try {
    return official_address_to_opcode(*target);
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
      opcode = address_opcode_for_item(items, item);
    }
    if (!opcode.has_value())
      return true;

    visiting.insert(address);
    bool result = true;
    if (*opcode == 0x52) {
      result = true;
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

std::optional<OverlayExecutable> overlay_executable_at(const std::vector<MachineItem>& items,
                                                       int index) {
  if (index < 0 || index >= static_cast<int>(items.size()))
    return std::nullopt;
  const MachineItem& item = items.at(static_cast<std::size_t>(index));
  if (item.kind == MachineItemKind::Op) {
    return OverlayExecutable{.opcode = item.opcode, .mnemonic = item.mnemonic};
  }
  if (item.kind != MachineItemKind::Address)
    return std::nullopt;
  const std::optional<int> opcode = address_opcode_for_item(items, item);
  if (!opcode.has_value())
    return std::nullopt;
  return OverlayExecutable{.opcode = *opcode, .mnemonic = "address byte"};
}

bool can_overlay_executable_cell_at(const std::vector<MachineItem>& items, int index) {
  const std::optional<OverlayExecutable> executable = overlay_executable_at(items, index);
  if (!executable.has_value())
    return false;
  if (!is_address_taking_opcode(executable->opcode))
    return true;
  const int operand_index = index + 1;
  return operand_index >= 0 && operand_index < static_cast<int>(items.size()) &&
         items.at(static_cast<std::size_t>(operand_index)).kind == MachineItemKind::Address;
}

std::optional<int> next_executable_opcode(const std::vector<MachineItem>& items, int start) {
  for (int index = start; index < static_cast<int>(items.size()); ++index) {
    if (items.at(static_cast<std::size_t>(index)).kind == MachineItemKind::Label)
      continue;
    const std::optional<OverlayExecutable> executable = overlay_executable_at(items, index);
    if (executable.has_value())
      return executable->opcode;
    return std::nullopt;
  }
  return std::nullopt;
}

bool can_move_overlay_opcode_to(const std::vector<MachineItem>& source_items, int source_index,
                                const std::vector<MachineItem>& target_items, int target_index,
                                int opcode) {
  if (!is_number_entry_opcode(opcode))
    return true;
  const std::optional<int> source_next = next_executable_opcode(source_items, source_index + 1);
  const std::optional<int> target_next = next_executable_opcode(target_items, target_index + 1);
  return !target_next.has_value() || !is_number_entry_opcode(*target_next) ||
         (source_next.has_value() && is_number_entry_opcode(*source_next));
}

MachineItem overlay_address_item(MachineItem address, const std::string& overlaid) {
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
  address.formal_opcode = formal_opcode;
  const std::string overlay_comment = "formal address/code overlay for " + overlaid;
  if (address.comment.has_value() && !address.comment->empty()) {
    address.comment = *address.comment + "; " + overlay_comment;
  } else {
    address.comment = overlay_comment;
  }
  return address;
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
                                                       const std::string& overlaid) {
  MachineItem ordinary = overlay_address_item(address, overlaid);
  const std::optional<int> ordinary_opcode = address_opcode_for_item(candidate, ordinary);
  if (ordinary_opcode.has_value() && *ordinary_opcode == executable_opcode)
    return ordinary;

  const std::optional<int> target = resolved_address_target(candidate, ordinary);
  if (!target.has_value())
    return std::nullopt;
  const FormalAddressInfo formal = formal_address_info(executable_opcode);
  if (formal.actual != *target)
    return std::nullopt;

  MachineItem formal_address =
      overlay_address_item_with_formal_opcode(address, executable_opcode, overlaid);
  const std::optional<int> formal_opcode = address_opcode_for_item(candidate, formal_address);
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

std::optional<PostLayoutIndirectFlowResult>
apply_address_code_overlay(const std::vector<MachineItem>& items) {
  const std::map<int, int> address_by_item = machine_address_by_item_index(items);
  MachineReturnAnalyzer target_may_return{
      .items = items,
      .layout = machine_layout(items),
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
    if (!can_overlay_executable_cell_at(items, labels_end))
      continue;

    const std::optional<OverlayExecutable> executable = overlay_executable_at(items, labels_end);
    if (!executable.has_value())
      continue;

    const auto address_cell_it = address_by_item.find(index + 1);
    if (address_cell_it == address_by_item.end())
      continue;
    const std::optional<int> fixed_target = fixed_address_actual_target(address);
    if (fixed_target.has_value() && *fixed_target > address_cell_it->second)
      continue;

    const std::vector<MachineItem> provisional =
        immediate_overlay_candidate(items, index, labels_end, address);
    const int provisional_address_index = index + 1 + (labels_end - (index + 2));
    if (!can_move_overlay_opcode_to(items, labels_end, provisional, provisional_address_index,
                                    executable->opcode)) {
      continue;
    }

    const std::optional<MachineItem> candidate_address =
        choose_overlay_address_item(provisional, address, executable->opcode, executable->mnemonic);
    if (!candidate_address.has_value())
      continue;

    std::vector<MachineItem> candidate =
        immediate_overlay_candidate(items, index, labels_end, *candidate_address);
    const std::optional<int> overlaid_opcode =
        address_opcode_for_item(candidate, *candidate_address);
    if (!overlaid_opcode.has_value() || *overlaid_opcode != executable->opcode)
      continue;
    if (machine_cell_count(candidate) >= machine_cell_count(items))
      continue;

    return PostLayoutIndirectFlowResult{
        .items = std::move(candidate),
        .applied = 1,
    };
  }

  return std::nullopt;
}

std::vector<IrOp> numeric_target_view(const std::vector<IrOp>& ops,
                                      const std::map<std::string, int>& labels) {
  std::vector<IrOp> numeric;
  numeric.reserve(ops.size());
  for (IrOp op : ops) {
    if (is_direct_flow(op)) {
      if (const auto* label = std::get_if<std::string>(&op.target)) {
        const auto address_it = labels.find(*label);
        if (address_it != labels.end())
          op.target = address_it->second;
      }
    }
    numeric.push_back(std::move(op));
  }
  return numeric;
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

std::map<int, std::string> first_label_by_address(const std::map<std::string, int>& labels) {
  std::map<int, std::string> result;
  for (const auto& [label, address] : labels) {
    if (!result.contains(address))
      result[address] = label;
  }
  return result;
}

std::optional<int> direct_flow_target_address(const IrOp& op,
                                              const std::map<std::string, int>& labels) {
  if (!is_direct_flow(op))
    return std::nullopt;
  return passes::target_address(op.target, labels);
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

std::optional<std::string>
first_spare_stable_register(const std::vector<IrOp>& ops,
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

std::string official_label(int target) {
  if (target <= 99)
    return std::to_string(target / 10) + std::to_string(target % 10);
  return "A" + std::to_string(target - 100);
}

std::string selector_for_target(int target) {
  if (target <= 47)
    return formal_label_from_ordinal(target + 112);
  return official_label(target);
}

std::optional<std::string> selector_for_actual_target(int target) {
  if (target < 0 || target > 104)
    return std::nullopt;
  return selector_for_target(target);
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

IrMeta append_comment(IrMeta meta, const std::string& comment) {
  if (meta.comment.has_value() && !meta.comment->empty()) {
    meta.comment = *meta.comment + "; " + comment;
  } else {
    meta.comment = comment;
  }
  return meta;
}

IrOp shifted_forward_indirect_op(const IrOp& op, const std::string& register_name,
                                 const std::string& selector_value, int final_target) {
  const int offset = register_index(register_name);
  IrOp result = op;
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
  result.meta = append_comment(result.meta, "preloaded R" + register_name + "=" + selector_value +
                                                " indirect-target=" + std::to_string(final_target) +
                                                " shifted-forward indirect flow");
  return result;
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
apply_fractional_r0_flow_rewrite(const std::vector<MachineItem>& items) {
  const std::vector<IrOp> ir = raise_machine_to_ir(items);
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

struct ForwardGroup {
  int target = 0;
  int final_target = 0;
  std::vector<int> indices;
};

std::optional<ForwardGroup> first_shifted_forward_group(const std::vector<IrOp>& ops) {
  const std::map<std::string, int> labels = passes::calculate_label_addresses(ops);
  const std::map<int, std::string> labels_by_address = first_label_by_address(labels);
  const std::vector<int> addresses = address_by_index(ops);
  std::map<int, std::vector<int>> grouped;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind != IrKind::Jump && op.kind != IrKind::Call && op.kind != IrKind::CondJump)
      continue;
    const std::optional<int> target = direct_flow_target_address(op, labels);
    if (!target.has_value() || *target <= addresses.at(index))
      continue;
    if (!labels_by_address.contains(*target))
      continue;
    grouped[*target].push_back(static_cast<int>(index));
  }

  std::optional<ForwardGroup> best;
  for (const auto& [target, indices] : grouped) {
    const int rewrites_before_target =
        static_cast<int>(std::count_if(indices.begin(), indices.end(), [&](int index) {
          return addresses.at(static_cast<std::size_t>(index)) < target;
        }));
    if (rewrites_before_target == 0)
      continue;
    const int final_target = target - rewrites_before_target;
    if (final_target < 0 || final_target > 104)
      continue;
    ForwardGroup candidate{
        .target = target,
        .final_target = final_target,
        .indices = indices,
    };
    if (!best.has_value() || candidate.indices.size() > best->indices.size())
      best = std::move(candidate);
  }
  return best;
}

std::optional<PostLayoutIndirectFlowResult>
apply_shifted_forward_group(const std::vector<MachineItem>& items,
                            const std::set<std::string>& reserved) {
  const std::vector<IrOp> ir = raise_machine_to_ir(items);
  const std::optional<ForwardGroup> group = first_shifted_forward_group(ir);
  const std::optional<std::string> selector = first_spare_stable_register(ir, reserved);
  if (!group.has_value() || !selector.has_value())
    return std::nullopt;

  const std::string selector_value = selector_for_target(group->final_target);
  const std::optional<IndirectAddressEvaluation> decoded =
      evaluate_indirect_address(*selector, selector_value, IndirectOperationKind::Flow);
  if (!decoded.has_value() || !decoded->actual_flow_target.has_value() ||
      *decoded->actual_flow_target != group->final_target) {
    return std::nullopt;
  }

  std::set<int> selected(group->indices.begin(), group->indices.end());
  std::vector<IrOp> rewritten;
  rewritten.reserve(ir.size());
  for (std::size_t index = 0; index < ir.size(); ++index) {
    const IrOp& op = ir.at(index);
    if (selected.contains(static_cast<int>(index))) {
      rewritten.push_back(
          shifted_forward_indirect_op(op, *selector, selector_value, group->final_target));
      continue;
    }
    rewritten.push_back(op);
  }

  std::vector<MachineItem> lowered = lower_ir_to_machine(rewritten);
  if (machine_cell_count(lowered) >= machine_cell_count(items))
    return std::nullopt;

  return PostLayoutIndirectFlowResult{
      .items = std::move(lowered),
      .preloads =
          {
              PreloadReport{
                  .register_name = *selector,
                  .value = selector_value,
                  .counts_against_program = false,
              },
          },
      .optimizations = post_layout_optimizations(static_cast<int>(group->indices.size())),
      .applied = static_cast<int>(group->indices.size()),
  };
}

MachineItem indirect_jump_machine_op(const std::string& register_name, const std::string& comment,
                                     const std::optional<int>& source_line = std::nullopt) {
  MachineItem item = MachineItem::op(0x80 + register_index(register_name), "К БП " + register_name);
  item.comment = comment;
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

std::optional<std::string> retargeted_selector_value(const std::string& register_name,
                                                     const std::string& previous_value,
                                                     int shifted_target) {
  if (const std::optional<std::string> candidate = selector_for_actual_target(shifted_target)) {
    const std::optional<IndirectAddressEvaluation> decoded =
        evaluate_indirect_address(register_name, *candidate, IndirectOperationKind::Flow);
    if (decoded.has_value() && decoded->actual_flow_target == shifted_target)
      return candidate;
  }

  const std::optional<IndirectAddressEvaluation> previous =
      evaluate_indirect_address(register_name, previous_value, IndirectOperationKind::Flow);
  if (previous.has_value() && previous->actual_flow_target == shifted_target)
    return previous_value;
  return std::nullopt;
}

std::optional<RetargetedMachine> retarget_selector_preloads_after_machine_deletion(
    const std::vector<MachineItem>& before_items, std::vector<MachineItem> after_items,
    const std::vector<PreloadReport>& preloads, int removed_address) {
  std::vector<PreloadReport> next_preloads;
  next_preloads.reserve(preloads.size());
  for (const PreloadReport& preload : preloads) {
    const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
        preload.register_name, preload.value, IndirectOperationKind::Flow);
    if (!decoded.has_value() || !decoded->actual_flow_target.has_value())
      return std::nullopt;
    const int target = *decoded->actual_flow_target;
    const int shifted_target = target > removed_address ? target - 1 : target;
    const std::optional<std::string> selector_value =
        shifted_target == target
            ? std::optional<std::string>(preload.value)
            : retargeted_selector_value(preload.register_name, preload.value, shifted_target);
    if (!selector_value.has_value())
      return std::nullopt;
    const std::optional<IndirectAddressEvaluation> shifted = evaluate_indirect_address(
        preload.register_name, *selector_value, IndirectOperationKind::Flow);
    if (!shifted.has_value() || shifted->actual_flow_target != shifted_target)
      return std::nullopt;
    next_preloads.push_back(PreloadReport{
        .register_name = preload.register_name,
        .value = *selector_value,
        .counts_against_program = preload.counts_against_program,
    });
  }

  (void)before_items;
  return RetargetedMachine{
      .items = std::move(after_items),
      .preloads = std::move(next_preloads),
  };
}

std::vector<StopTailReuseBase> stop_tail_reuse_bases(const std::vector<MachineItem>& items,
                                                     const std::vector<PreloadReport>& preloads) {
  const std::vector<MachineCell> cells = machine_cells(items);
  std::vector<StopTailReuseBase> bases;
  for (const PreloadReport& preload : preloads) {
    const std::optional<IndirectAddressEvaluation> decoded = evaluate_indirect_address(
        preload.register_name, preload.value, IndirectOperationKind::Flow);
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
                             const std::vector<PreloadReport>& preloads) {
  const std::vector<StopTailReuseBase> bases = stop_tail_reuse_bases(items, preloads);
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
                                   source.source_line));
      continue;
    }
    result.push_back(items.at(static_cast<std::size_t>(index)));
  }
  return result;
}

std::optional<BranchRewrite>
find_existing_selector_flow_rewrite(const std::vector<MachineItem>& items,
                                    const std::vector<PreloadReport>& preloads) {
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
          preload.register_name, preload.value, IndirectOperationKind::Flow);
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
      };
    }
  }
  return std::nullopt;
}

std::optional<BranchRewrite>
find_branch_to_stop_tail_selector_rewrite(const std::vector<MachineItem>& items,
                                          const std::vector<PreloadReport>& preloads) {
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
    const std::optional<IndirectAddressEvaluation> decoded =
        evaluate_indirect_address(*register_name, *selector_value, IndirectOperationKind::Flow);
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
    };
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
      if (rewrite.source_line.has_value())
        item.source_line = *rewrite.source_line;
      result.push_back(item);
      continue;
    }
    result.push_back(items.at(static_cast<std::size_t>(index)));
  }
  return result;
}

std::vector<passes::AppliedOptimization> post_layout_optimizations(int applied) {
  if (applied == 0)
    return {};
  return {
      passes::AppliedOptimization{
          .name = "preloaded-indirect-flow",
          .detail = "Activated post-layout: replaced " + std::to_string(applied) +
                    " direct branch/call(s) with proven preloaded indirect flow.",
      },
  };
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
  if (machine_cell_count(items) <= rescue_above) {
    return PostLayoutIndirectFlowResult{
        .items = items,
    };
  }

  std::vector<MachineItem> current = items;
  std::vector<PreloadReport> preloads;
  int applied = 0;
  for (int round = 0; round < kMaxRewrites; ++round) {
    const std::vector<IrOp> ir = raise_machine_to_ir(current);
    const std::map<std::string, int> labels = passes::calculate_label_addresses(ir);
    const std::vector<IrOp> numeric = numeric_target_view(ir, labels);
    CompileOptions pass_options = options;
    pass_options.preloaded_indirect_flow = true;
    std::set<std::string> reserved;
    for (const auto& [register_name, value] : pass_options.preloaded_constant_registers) {
      (void)value;
      reserved.insert(register_name);
    }
    const std::optional<PostLayoutIndirectFlowResult> shifted =
        apply_shifted_forward_group(current, reserved);
    const passes::PassResult pass =
        passes::run_preloaded_indirect_flow(numeric, passes::PassContext{.options = pass_options});

    std::optional<PostLayoutIndirectFlowResult> selected;
    if (pass.applied == 0) {
      if (!shifted.has_value())
        break;
      selected = shifted;
    } else {
      std::vector<MachineItem> lowered = lower_ir_to_machine(pass.ops);
      if (machine_cell_count(lowered) < machine_cell_count(current)) {
        selected = PostLayoutIndirectFlowResult{
            .items = std::move(lowered),
            .preloads = pass.preloads,
            .optimizations = pass.optimizations,
            .applied = pass.applied,
        };
      }
      if (shifted.has_value() &&
          (!selected.has_value() || shifted->applied > selected->applied)) {
        selected = shifted;
      }
    }

    if (!selected.has_value())
      break;

    current = std::move(selected->items);
    preloads.insert(preloads.end(), selected->preloads.begin(), selected->preloads.end());
    applied += selected->applied;
    if (machine_cell_count(current) <= rescue_above)
      break;
  }

  if (applied == 0) {
    return PostLayoutIndirectFlowResult{
        .items = items,
    };
  }

  return PostLayoutIndirectFlowResult{
      .items = std::move(current),
      .preloads = std::move(preloads),
      .optimizations = post_layout_optimizations(applied),
      .applied = applied,
  };
}

PostLayoutIndirectFlowResult
optimize_post_layout_fractional_r0_flow(const std::vector<MachineItem>& items,
                                        const std::vector<PreloadReport>& existing_flow_preloads) {
  if (!existing_flow_preloads.empty()) {
    return PostLayoutIndirectFlowResult{
        .items = items,
    };
  }

  std::vector<MachineItem> current = items;
  int applied = 0;
  for (int round = 0; round < 8; ++round) {
    const std::optional<PostLayoutIndirectFlowResult> result =
        apply_fractional_r0_flow_rewrite(current);
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
optimize_post_layout_address_code_overlay(const std::vector<MachineItem>& items) {
  std::vector<MachineItem> current = items;
  int applied = 0;
  for (int round = 0; round < 16; ++round) {
    const std::optional<PostLayoutIndirectFlowResult> result = apply_address_code_overlay(current);
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
optimize_post_layout_stop_tail_reuse(const std::vector<MachineItem>& items,
                                     const std::vector<PreloadReport>& preloads) {
  std::vector<MachineItem> current = items;
  std::vector<PreloadReport> current_preloads = preloads;
  int stop_tail_applied = 0;
  int existing_selector_applied = 0;

  for (int round = 0; round < kMaxRewrites; ++round) {
    const std::map<int, int> address_by_item = machine_address_by_item_index(current);

    if (const std::optional<BranchRewrite> rewrite =
            find_existing_selector_flow_rewrite(current, current_preloads)) {
      const auto removed_it = address_by_item.find(rewrite->address_index);
      if (removed_it == address_by_item.end())
        break;
      std::vector<MachineItem> candidate = apply_branch_rewrite(current, *rewrite);
      if (machine_cell_count(candidate) >= machine_cell_count(current))
        break;
      const std::optional<RetargetedMachine> retargeted =
          retarget_selector_preloads_after_machine_deletion(current, std::move(candidate),
                                                            current_preloads, removed_it->second);
      if (!retargeted.has_value())
        break;
      current = retargeted->items;
      current_preloads = retargeted->preloads;
      ++existing_selector_applied;
      continue;
    }

    if (const std::optional<BranchRewrite> rewrite =
            find_branch_to_stop_tail_selector_rewrite(current, current_preloads)) {
      const auto removed_it = address_by_item.find(rewrite->address_index);
      if (removed_it == address_by_item.end())
        break;
      std::vector<MachineItem> candidate = apply_branch_rewrite(current, *rewrite);
      if (machine_cell_count(candidate) >= machine_cell_count(current))
        break;
      const std::optional<RetargetedMachine> retargeted =
          retarget_selector_preloads_after_machine_deletion(current, std::move(candidate),
                                                            current_preloads, removed_it->second);
      if (!retargeted.has_value())
        break;
      current = retargeted->items;
      current_preloads = retargeted->preloads;
      ++stop_tail_applied;
      continue;
    }

    if (const std::optional<StopTailReuseRewrite> rewrite =
            find_stop_tail_reuse_rewrite(current, current_preloads)) {
      const auto removed_it = address_by_item.find(rewrite->remove_index);
      if (removed_it == address_by_item.end())
        break;
      std::vector<MachineItem> candidate = apply_stop_tail_reuse_rewrite(current, *rewrite);
      if (machine_cell_count(candidate) >= machine_cell_count(current))
        break;
      const std::optional<RetargetedMachine> retargeted =
          retarget_selector_preloads_after_machine_deletion(current, std::move(candidate),
                                                            current_preloads, removed_it->second);
      if (!retargeted.has_value())
        break;
      current = retargeted->items;
      current_preloads = retargeted->preloads;
      ++stop_tail_applied;
      continue;
    }

    break;
  }

  const int applied = stop_tail_applied + existing_selector_applied;
  if (applied == 0) {
    return PostLayoutIndirectFlowResult{
        .items = items,
        .preloads = preloads,
    };
  }

  std::vector<passes::AppliedOptimization> optimizations;
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

  return PostLayoutIndirectFlowResult{
      .items = std::move(current),
      .preloads = std::move(current_preloads),
      .optimizations = std::move(optimizations),
      .applied = applied,
  };
}

} // namespace mkpro::core
