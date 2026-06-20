#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mkpro {

using IrTarget = std::variant<int, std::string>;
using CellRole = std::string;

enum class MachineItemKind {
  Label,
  Op,
  Address,
};

struct MachineItem {
  MachineItemKind kind = MachineItemKind::Op;
  std::string name;
  std::optional<std::string> procedure_boundary;
  std::optional<std::string> procedure_name;
  bool hidden = false;
  int opcode = 0;
  std::string mnemonic;
  std::optional<std::string> comment;
  std::optional<int> source_line;
  bool raw = false;
  IrTarget target = 0;
  std::optional<int> formal_opcode;

  static MachineItem label(std::string name);
  static MachineItem op(int opcode, std::string mnemonic);
  static MachineItem address(IrTarget target);
};

struct LayoutIrCell {
  int address = 0;
  int opcode = 0;
  std::vector<CellRole> roles;
  std::string tactic;
};

enum class IrKind {
  Label,
  Store,
  Recall,
  IndirectStore,
  IndirectRecall,
  Jump,
  CondJump,
  Call,
  Loop,
  IndirectJump,
  IndirectCall,
  IndirectCondJump,
  Return,
  Stop,
  Plain,
  OrphanAddress,
};

struct IrMeta {
  std::string mnemonic;
  std::optional<std::string> comment;
  std::optional<int> source_line;
  bool raw = false;
  std::vector<CellRole> roles;
  std::optional<std::string> tactic;
};

struct IrTargetMeta {
  std::optional<std::string> comment;
  std::optional<int> source_line;
  std::vector<CellRole> roles;
  std::optional<int> formal_opcode;
};

struct IrOp {
  IrKind kind = IrKind::Plain;
  std::string name;
  std::optional<std::string> procedure_boundary;
  std::optional<std::string> procedure_name;
  bool hidden = false;
  std::string register_name;
  std::string condition;
  std::string counter;
  int opcode = 0;
  IrTarget target = 0;
  std::string semantic;
  IrMeta meta;
  IrTargetMeta target_meta;
};

struct LowerLayoutOptions {
  std::string default_tactic;
};

struct LabelAddress {
  std::string name;
  int address = 0;
};

struct LowerLayoutResult {
  std::vector<LayoutIrCell> cells;
  std::vector<LabelAddress> address_of_label;
};

std::vector<IrOp> raise_machine_to_ir(const std::vector<MachineItem>& items);
std::vector<MachineItem> lower_ir_to_machine(const std::vector<IrOp>& ops);

std::vector<IrOp> raise_layout_to_ir(const std::vector<LayoutIrCell>& cells);
LowerLayoutResult lower_ir_to_layout(const std::vector<IrOp>& ops,
                                     const LowerLayoutOptions& options = {});

bool machine_items_equal(const MachineItem& a, const MachineItem& b);

IrOp make_store(std::string register_name, IrMeta meta = {});
IrOp make_recall(std::string register_name, IrMeta meta = {});

std::string ir_kind_name(IrKind kind);
std::string machine_items_to_json(const std::vector<MachineItem>& items);
std::string ir_ops_to_json(const std::vector<IrOp>& ops);
std::string layout_cells_to_json(const std::vector<LayoutIrCell>& cells);

} // namespace mkpro
