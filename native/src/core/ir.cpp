#include "mkpro/core/ir.hpp"

#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace mkpro {

namespace {

const std::vector<std::string> kRegisters = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e",
};

constexpr int kDirectStoreBase = 0x40;
constexpr int kDirectRecallBase = 0x60;
constexpr int kIndirectJumpBase = 0x80;
constexpr int kIndirectCallBase = 0xa0;
constexpr int kIndirectStoreBase = 0xb0;
constexpr int kIndirectRecallBase = 0xd0;

const std::map<int, std::string> kIndirectCondBases = {
    {0x70, "!=0"},
    {0x90, ">=0"},
    {0xc0, "<0"},
    {0xe0, "==0"},
};

const std::map<int, std::string> kCondOpcodes = {
    {0x57, "!=0"},
    {0x59, ">=0"},
    {0x5c, "<0"},
    {0x5e, "==0"},
};

const std::map<int, std::string> kLoopOpcodes = {
    {0x5d, "L0"},
    {0x5b, "L1"},
    {0x58, "L2"},
    {0x5a, "L3"},
};

bool takes_address(int opcode) {
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

bool is_in_range(int opcode, int base) {
  return opcode >= base && opcode <= base + 0xe;
}

std::string register_for_offset(int opcode, int base) {
  const int index = opcode - base;
  if (index < 0 || index >= static_cast<int>(kRegisters.size())) {
    throw std::runtime_error("Opcode register offset is out of range");
  }
  return kRegisters.at(static_cast<std::size_t>(index));
}

IrMeta meta_from_op(const MachineItem& item) {
  IrMeta meta;
  meta.mnemonic = item.mnemonic;
  if (item.comment.has_value())
    meta.comment = item.comment;
  if (item.source_line.has_value())
    meta.source_line = item.source_line;
  if (item.raw)
    meta.raw = true;
  meta.roles = item.roles;
  return meta;
}

IrTargetMeta target_meta_from_address(const MachineItem& item) {
  IrTargetMeta meta;
  if (item.comment.has_value())
    meta.comment = item.comment;
  if (item.source_line.has_value())
    meta.source_line = item.source_line;
  if (item.formal_opcode.has_value())
    meta.formal_opcode = item.formal_opcode;
  return meta;
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string stop_semantic_from_comment(const std::optional<std::string>& comment) {
  if (!comment.has_value())
    return "unknown";
  const std::string lowered = lower_ascii(*comment);
  if (starts_with(lowered, "halt"))
    return "halt";
  if (starts_with(lowered, "pause"))
    return "pause";
  if (starts_with(lowered, "show"))
    return "show";
  if (starts_with(lowered, "ask"))
    return "ask";
  if (starts_with(lowered, "input") || starts_with(lowered, "read"))
    return "input";
  if (starts_with(lowered, "implicit final stop"))
    return "halt";
  if (lowered.find("implicit stop") != std::string::npos)
    return "halt";
  return "unknown";
}

MachineItem machine_op_from_meta(int opcode, const IrMeta& meta) {
  MachineItem item = MachineItem::op(opcode, meta.mnemonic);
  if (meta.comment.has_value())
    item.comment = meta.comment;
  if (meta.source_line.has_value())
    item.source_line = meta.source_line;
  if (meta.raw)
    item.raw = true;
  item.roles = meta.roles;
  return item;
}

MachineItem machine_address_from_meta(const IrTarget& target, const IrTargetMeta& meta) {
  MachineItem item = MachineItem::address(target);
  if (meta.comment.has_value())
    item.comment = meta.comment;
  if (meta.source_line.has_value())
    item.source_line = meta.source_line;
  if (meta.formal_opcode.has_value())
    item.formal_opcode = meta.formal_opcode;
  return item;
}

bool targets_equal(const IrTarget& a, const IrTarget& b) {
  return a == b;
}

int numeric_target_or_zero(const IrTarget& target) {
  if (const auto* value = std::get_if<int>(&target))
    return *value;
  return 0;
}

std::string target_to_json(const IrTarget& target);

std::string json_escape(std::string_view value) {
  std::ostringstream out;
  out << '"';
  for (char ch : value) {
    switch (ch) {
    case '\\':
      out << "\\\\";
      break;
    case '"':
      out << "\\\"";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      out << ch;
      break;
    }
  }
  out << '"';
  return out.str();
}

template <typename T, typename Fn>
std::string json_array(const std::vector<T>& values, Fn item_to_json) {
  std::ostringstream out;
  out << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0)
      out << ',';
    out << item_to_json(values.at(index));
  }
  out << ']';
  return out.str();
}

std::string string_array_to_json(const std::vector<std::string>& values) {
  return json_array(values, [](const std::string& value) { return json_escape(value); });
}

void add_field(std::ostringstream& out, bool& first, std::string_view name, std::string value) {
  if (!first)
    out << ',';
  first = false;
  out << json_escape(name) << ':' << value;
}

std::string target_to_json(const IrTarget& target) {
  if (const auto* number = std::get_if<int>(&target))
    return std::to_string(*number);
  return json_escape(std::get<std::string>(target));
}

std::string meta_to_json(const IrMeta& meta) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "mnemonic", json_escape(meta.mnemonic));
  if (meta.comment.has_value())
    add_field(out, first, "comment", json_escape(*meta.comment));
  if (meta.source_line.has_value()) {
    add_field(out, first, "sourceLine", std::to_string(*meta.source_line));
  }
  if (meta.raw)
    add_field(out, first, "raw", "true");
  if (!meta.roles.empty())
    add_field(out, first, "roles", string_array_to_json(meta.roles));
  if (meta.tactic.has_value())
    add_field(out, first, "tactic", json_escape(*meta.tactic));
  out << '}';
  return out.str();
}

std::string target_meta_to_json(const IrTargetMeta& meta) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  if (meta.comment.has_value())
    add_field(out, first, "comment", json_escape(*meta.comment));
  if (meta.source_line.has_value()) {
    add_field(out, first, "sourceLine", std::to_string(*meta.source_line));
  }
  if (!meta.roles.empty())
    add_field(out, first, "roles", string_array_to_json(meta.roles));
  if (meta.formal_opcode.has_value()) {
    add_field(out, first, "formalOpcode", std::to_string(*meta.formal_opcode));
  }
  out << '}';
  return out.str();
}

std::string machine_item_to_json(const MachineItem& item) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  if (item.kind == MachineItemKind::Label) {
    add_field(out, first, "kind", json_escape("label"));
    add_field(out, first, "name", json_escape(item.name));
    if (item.procedure_boundary.has_value()) {
      add_field(out, first, "procedureBoundary", json_escape(*item.procedure_boundary));
    }
    if (item.procedure_name.has_value()) {
      add_field(out, first, "procedureName", json_escape(*item.procedure_name));
    }
    if (item.hidden)
      add_field(out, first, "hidden", "true");
  } else if (item.kind == MachineItemKind::Op) {
    add_field(out, first, "kind", json_escape("op"));
    add_field(out, first, "opcode", std::to_string(item.opcode));
    add_field(out, first, "mnemonic", json_escape(item.mnemonic));
    if (item.comment.has_value())
      add_field(out, first, "comment", json_escape(*item.comment));
    if (item.source_line.has_value()) {
      add_field(out, first, "sourceLine", std::to_string(*item.source_line));
    }
    if (item.raw)
      add_field(out, first, "raw", "true");
  } else {
    add_field(out, first, "kind", json_escape("address"));
    add_field(out, first, "target", target_to_json(item.target));
    if (item.comment.has_value())
      add_field(out, first, "comment", json_escape(*item.comment));
    if (item.source_line.has_value()) {
      add_field(out, first, "sourceLine", std::to_string(*item.source_line));
    }
    if (item.formal_opcode.has_value()) {
      add_field(out, first, "formalOpcode", std::to_string(*item.formal_opcode));
    }
  }
  out << '}';
  return out.str();
}

std::string ir_op_to_json(const IrOp& op) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "kind", json_escape(ir_kind_name(op.kind)));
  if (op.kind == IrKind::Label) {
    add_field(out, first, "name", json_escape(op.name));
    if (op.procedure_boundary.has_value()) {
      add_field(out, first, "procedureBoundary", json_escape(*op.procedure_boundary));
    }
    if (op.procedure_name.has_value()) {
      add_field(out, first, "procedureName", json_escape(*op.procedure_name));
    }
    if (op.hidden)
      add_field(out, first, "hidden", "true");
  } else if (op.kind == IrKind::OrphanAddress) {
    add_field(out, first, "target", target_to_json(op.target));
    add_field(out, first, "meta", target_meta_to_json(op.target_meta));
  } else {
    if (!op.register_name.empty()) {
      add_field(out, first, "register", json_escape(op.register_name));
    }
    if (!op.condition.empty())
      add_field(out, first, "condition", json_escape(op.condition));
    if (!op.counter.empty())
      add_field(out, first, "counter", json_escape(op.counter));
    add_field(out, first, "opcode", std::to_string(op.opcode));
    if (op.kind == IrKind::Jump || op.kind == IrKind::CondJump || op.kind == IrKind::Call ||
        op.kind == IrKind::Loop) {
      add_field(out, first, "target", target_to_json(op.target));
      add_field(out, first, "targetMeta", target_meta_to_json(op.target_meta));
    }
    if (op.kind == IrKind::Stop)
      add_field(out, first, "semantic", json_escape(op.semantic));
    add_field(out, first, "meta", meta_to_json(op.meta));
  }
  out << '}';
  return out.str();
}

std::string layout_cell_to_json(const LayoutIrCell& cell) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "address", std::to_string(cell.address));
  add_field(out, first, "opcode", std::to_string(cell.opcode));
  add_field(out, first, "roles", string_array_to_json(cell.roles));
  add_field(out, first, "tactic", json_escape(cell.tactic));
  out << '}';
  return out.str();
}

void push_basic_opcode_ir(std::vector<IrOp>& result, int opcode, const IrMeta& meta) {
  if (is_in_range(opcode, kDirectStoreBase)) {
    result.push_back(IrOp{.kind = IrKind::Store,
                          .register_name = register_for_offset(opcode, kDirectStoreBase),
                          .opcode = opcode,
                          .meta = meta});
    return;
  }
  if (is_in_range(opcode, kDirectRecallBase)) {
    result.push_back(IrOp{.kind = IrKind::Recall,
                          .register_name = register_for_offset(opcode, kDirectRecallBase),
                          .opcode = opcode,
                          .meta = meta});
    return;
  }
  if (is_in_range(opcode, kIndirectStoreBase)) {
    result.push_back(IrOp{.kind = IrKind::IndirectStore,
                          .register_name = register_for_offset(opcode, kIndirectStoreBase),
                          .opcode = opcode,
                          .meta = meta});
    return;
  }
  if (is_in_range(opcode, kIndirectRecallBase)) {
    result.push_back(IrOp{.kind = IrKind::IndirectRecall,
                          .register_name = register_for_offset(opcode, kIndirectRecallBase),
                          .opcode = opcode,
                          .meta = meta});
    return;
  }
  if (is_in_range(opcode, kIndirectJumpBase)) {
    result.push_back(IrOp{.kind = IrKind::IndirectJump,
                          .register_name = register_for_offset(opcode, kIndirectJumpBase),
                          .opcode = opcode,
                          .meta = meta});
    return;
  }
  if (is_in_range(opcode, kIndirectCallBase)) {
    result.push_back(IrOp{.kind = IrKind::IndirectCall,
                          .register_name = register_for_offset(opcode, kIndirectCallBase),
                          .opcode = opcode,
                          .meta = meta});
    return;
  }
  for (const auto& [base, condition] : kIndirectCondBases) {
    if (is_in_range(opcode, base)) {
      result.push_back(IrOp{.kind = IrKind::IndirectCondJump,
                            .register_name = register_for_offset(opcode, base),
                            .condition = condition,
                            .opcode = opcode,
                            .meta = meta});
      return;
    }
  }
  if (opcode == 0x52) {
    result.push_back(IrOp{.kind = IrKind::Return, .opcode = opcode, .meta = meta});
    return;
  }
  if (opcode == 0x50) {
    result.push_back(IrOp{.kind = IrKind::Stop,
                          .opcode = opcode,
                          .semantic = stop_semantic_from_comment(meta.comment),
                          .meta = meta});
    return;
  }
  result.push_back(IrOp{.kind = IrKind::Plain, .opcode = opcode, .meta = meta});
}

void push_addressed_ir(std::vector<IrOp>& result, int opcode, IrMeta meta, IrTarget target,
                       IrTargetMeta target_meta) {
  if (opcode == 0x51) {
    result.push_back(IrOp{.kind = IrKind::Jump,
                          .opcode = opcode,
                          .target = std::move(target),
                          .meta = std::move(meta),
                          .target_meta = std::move(target_meta)});
    return;
  }
  if (opcode == 0x53) {
    result.push_back(IrOp{.kind = IrKind::Call,
                          .opcode = opcode,
                          .target = std::move(target),
                          .meta = std::move(meta),
                          .target_meta = std::move(target_meta)});
    return;
  }
  const auto condition_it = kCondOpcodes.find(opcode);
  if (condition_it != kCondOpcodes.end()) {
    result.push_back(IrOp{.kind = IrKind::CondJump,
                          .condition = condition_it->second,
                          .opcode = opcode,
                          .target = std::move(target),
                          .meta = std::move(meta),
                          .target_meta = std::move(target_meta)});
    return;
  }
  const auto loop_it = kLoopOpcodes.find(opcode);
  if (loop_it != kLoopOpcodes.end()) {
    result.push_back(IrOp{.kind = IrKind::Loop,
                          .counter = loop_it->second,
                          .opcode = opcode,
                          .target = std::move(target),
                          .meta = std::move(meta),
                          .target_meta = std::move(target_meta)});
    return;
  }
  result.push_back(IrOp{.kind = IrKind::Plain, .opcode = opcode, .meta = std::move(meta)});
}

} // namespace

MachineItem MachineItem::label(std::string label_name) {
  MachineItem item;
  item.kind = MachineItemKind::Label;
  item.name = std::move(label_name);
  return item;
}

MachineItem MachineItem::op(int op_code, std::string op_mnemonic) {
  MachineItem item;
  item.kind = MachineItemKind::Op;
  item.opcode = op_code;
  item.mnemonic = std::move(op_mnemonic);
  return item;
}

MachineItem MachineItem::address(IrTarget address_target) {
  MachineItem item;
  item.kind = MachineItemKind::Address;
  item.target = std::move(address_target);
  return item;
}

std::vector<IrOp> raise_machine_to_ir(const std::vector<MachineItem>& items) {
  std::vector<IrOp> result;
  for (std::size_t index = 0; index < items.size(); ++index) {
    const MachineItem& item = items.at(index);
    if (item.kind == MachineItemKind::Label) {
      result.push_back(IrOp{.kind = IrKind::Label,
                            .name = item.name,
                            .procedure_boundary = item.procedure_boundary,
                            .procedure_name = item.procedure_name,
                            .hidden = item.hidden});
      continue;
    }
    if (item.kind == MachineItemKind::Address) {
      result.push_back(IrOp{.kind = IrKind::OrphanAddress,
                            .target = item.target,
                            .target_meta = target_meta_from_address(item)});
      continue;
    }

    const IrMeta meta = meta_from_op(item);
    const int opcode = item.opcode;
    if (takes_address(opcode)) {
      if (index + 1U >= items.size() || items.at(index + 1U).kind != MachineItemKind::Address) {
        result.push_back(IrOp{.kind = IrKind::Plain, .opcode = opcode, .meta = meta});
        continue;
      }
      const MachineItem& target_item = items.at(index + 1U);
      ++index;
      push_addressed_ir(result, opcode, meta, target_item.target,
                        target_meta_from_address(target_item));
      continue;
    }
    push_basic_opcode_ir(result, opcode, meta);
  }
  return result;
}

std::vector<MachineItem> lower_ir_to_machine(const std::vector<IrOp>& ops) {
  std::vector<MachineItem> result;
  for (const IrOp& op : ops) {
    switch (op.kind) {
    case IrKind::Label: {
      MachineItem item = MachineItem::label(op.name);
      item.procedure_boundary = op.procedure_boundary;
      item.procedure_name = op.procedure_name;
      item.hidden = op.hidden;
      result.push_back(std::move(item));
      break;
    }
    case IrKind::OrphanAddress:
      result.push_back(machine_address_from_meta(op.target, op.target_meta));
      break;
    case IrKind::Store:
    case IrKind::Recall:
    case IrKind::IndirectStore:
    case IrKind::IndirectRecall:
    case IrKind::IndirectJump:
    case IrKind::IndirectCall:
    case IrKind::IndirectCondJump:
    case IrKind::Return:
    case IrKind::Stop:
    case IrKind::Plain:
      result.push_back(machine_op_from_meta(op.opcode, op.meta));
      break;
    case IrKind::Jump:
    case IrKind::CondJump:
    case IrKind::Call:
    case IrKind::Loop:
      result.push_back(machine_op_from_meta(op.opcode, op.meta));
      result.push_back(machine_address_from_meta(op.target, op.target_meta));
      break;
    }
  }
  return result;
}

IrOp make_store(std::string register_name, IrMeta meta) {
  const int opcode = kDirectStoreBase + register_index(register_name);
  if (meta.mnemonic.empty())
    meta.mnemonic = opcode_by_code(opcode).name;
  return IrOp{.kind = IrKind::Store,
              .register_name = std::move(register_name),
              .opcode = opcode,
              .meta = std::move(meta)};
}

IrOp make_recall(std::string register_name, IrMeta meta) {
  const int opcode = kDirectRecallBase + register_index(register_name);
  if (meta.mnemonic.empty())
    meta.mnemonic = opcode_by_code(opcode).name;
  return IrOp{.kind = IrKind::Recall,
              .register_name = std::move(register_name),
              .opcode = opcode,
              .meta = std::move(meta)};
}

std::vector<IrOp> raise_layout_to_ir(const std::vector<LayoutIrCell>& cells) {
  std::vector<IrOp> result;
  for (std::size_t index = 0; index < cells.size(); ++index) {
    const LayoutIrCell& cell = cells.at(index);
    IrMeta meta;
    meta.mnemonic = opcode_by_code(cell.opcode).name;
    if (!cell.roles.empty())
      meta.roles = cell.roles;
    if (!cell.tactic.empty())
      meta.tactic = cell.tactic;

    if (takes_address(cell.opcode)) {
      if (index + 1U >= cells.size()) {
        result.push_back(IrOp{.kind = IrKind::Plain, .opcode = cell.opcode, .meta = meta});
        continue;
      }
      const LayoutIrCell& next = cells.at(index + 1U);
      const bool next_is_address =
          std::find(next.roles.begin(), next.roles.end(), "address") != next.roles.end();
      if (!next_is_address) {
        result.push_back(IrOp{.kind = IrKind::Plain, .opcode = cell.opcode, .meta = meta});
        continue;
      }

      IrTargetMeta target_meta;
      if (!next.tactic.empty())
        target_meta.comment = next.tactic;
      if (!next.roles.empty())
        target_meta.roles = next.roles;
      ++index;
      push_addressed_ir(result, cell.opcode, std::move(meta), next.opcode, std::move(target_meta));
      continue;
    }
    push_basic_opcode_ir(result, cell.opcode, meta);
  }
  return result;
}

LowerLayoutResult lower_ir_to_layout(const std::vector<IrOp>& ops,
                                     const LowerLayoutOptions& options) {
  LowerLayoutResult result;
  int address = 0;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label) {
      result.address_of_label.push_back(LabelAddress{.name = op.name, .address = address});
      continue;
    }
    if (op.kind == IrKind::OrphanAddress) {
      std::vector<CellRole> roles =
          op.target_meta.roles.empty() ? std::vector<CellRole>{"address"} : op.target_meta.roles;
      const int opcode = op.target_meta.formal_opcode.value_or(numeric_target_or_zero(op.target));
      result.cells.push_back(LayoutIrCell{
          .address = address,
          .opcode = opcode,
          .roles = std::move(roles),
          .tactic = op.target_meta.comment.value_or(options.default_tactic),
      });
      ++address;
      continue;
    }

    const std::string tactic =
        op.meta.tactic.value_or(op.meta.comment.value_or(options.default_tactic));
    std::vector<CellRole> roles =
        op.meta.roles.empty() ? std::vector<CellRole>{"exec"} : op.meta.roles;
    result.cells.push_back(LayoutIrCell{
        .address = address,
        .opcode = op.opcode,
        .roles = std::move(roles),
        .tactic = tactic,
    });
    ++address;

    if (op.kind == IrKind::Jump || op.kind == IrKind::CondJump || op.kind == IrKind::Call ||
        op.kind == IrKind::Loop) {
      int target_value = 0;
      if (const auto* numeric = std::get_if<int>(&op.target)) {
        target_value = *numeric;
      } else {
        const std::string& name = std::get<std::string>(op.target);
        const auto it =
            std::find_if(result.address_of_label.begin(), result.address_of_label.end(),
                         [&name](const LabelAddress& label) { return label.name == name; });
        if (it != result.address_of_label.end())
          target_value = it->address;
      }

      std::vector<CellRole> target_roles =
          op.target_meta.roles.empty() ? std::vector<CellRole>{"address"} : op.target_meta.roles;
      result.cells.push_back(LayoutIrCell{
          .address = address,
          .opcode = op.target_meta.formal_opcode.value_or(target_value),
          .roles = std::move(target_roles),
          .tactic = op.target_meta.comment.value_or(tactic),
      });
      ++address;
    }
  }
  return result;
}

bool machine_items_equal(const MachineItem& a, const MachineItem& b) {
  if (a.kind != b.kind)
    return false;
  if (a.kind == MachineItemKind::Label) {
    return a.name == b.name && a.procedure_boundary == b.procedure_boundary &&
           a.procedure_name == b.procedure_name && a.hidden == b.hidden;
  }
  if (a.kind == MachineItemKind::Op) {
    return a.opcode == b.opcode && a.mnemonic == b.mnemonic && a.comment == b.comment &&
           a.source_line == b.source_line && a.raw == b.raw && a.roles == b.roles;
  }
  return targets_equal(a.target, b.target) && a.comment == b.comment &&
         a.source_line == b.source_line && a.formal_opcode == b.formal_opcode;
}

std::string ir_kind_name(IrKind kind) {
  switch (kind) {
  case IrKind::Label:
    return "label";
  case IrKind::Store:
    return "store";
  case IrKind::Recall:
    return "recall";
  case IrKind::IndirectStore:
    return "indirect-store";
  case IrKind::IndirectRecall:
    return "indirect-recall";
  case IrKind::Jump:
    return "jump";
  case IrKind::CondJump:
    return "cjump";
  case IrKind::Call:
    return "call";
  case IrKind::Loop:
    return "loop";
  case IrKind::IndirectJump:
    return "indirect-jump";
  case IrKind::IndirectCall:
    return "indirect-call";
  case IrKind::IndirectCondJump:
    return "indirect-cjump";
  case IrKind::Return:
    return "return";
  case IrKind::Stop:
    return "stop";
  case IrKind::Plain:
    return "plain";
  case IrKind::OrphanAddress:
    return "orphan-address";
  }
  return "plain";
}

std::string machine_items_to_json(const std::vector<MachineItem>& items) {
  return json_array(items, [](const MachineItem& item) { return machine_item_to_json(item); });
}

std::string ir_ops_to_json(const std::vector<IrOp>& ops) {
  return json_array(ops, [](const IrOp& op) { return ir_op_to_json(op); });
}

std::string layout_cells_to_json(const std::vector<LayoutIrCell>& cells) {
  return json_array(cells, [](const LayoutIrCell& cell) { return layout_cell_to_json(cell); });
}

} // namespace mkpro
