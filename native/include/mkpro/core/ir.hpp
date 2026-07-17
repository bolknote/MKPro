#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mkpro {

using IrTarget = std::variant<int, std::string>;
using CellRole = std::string;

// Compiler-owned proof role for a decimal constant whose final two mantissa
// digits may be selected by final layout.  The suffix after this prefix is the
// stable decimal prefix before those two digits.  Source syntax cannot forge
// roles, so consumers may use this only after checking every data recall of
// the carrier and independently decoding the rebuilt preload.
inline constexpr const char* kRetunableNaturalFractionalSelectorRolePrefix =
    "retunable-natural-fractional-selector:";

// Compiler-owned interaction semantics.  Comments and mnemonic text are
// deliberately not part of this channel: post-layout proofs must be able to
// distinguish a resumable prompt from source-level halt() after comments have
// been stripped.
enum class StopDisposition {
  Unknown,
  Resumable,
  Terminal,
};

enum class ManualInteractionAnchorKind {
  PromptStop,
  SingleStepCommand,
  ContinuousResume,
};

struct ManualInteractionAnchor {
  int protocol_id = -1;
  int phase = -1;
  ManualInteractionAnchorKind kind = ManualInteractionAnchorKind::PromptStop;
  bool operator==(const ManualInteractionAnchor&) const = default;
};

struct EnteredInputDomain {
  std::optional<int> minimum;
  std::optional<int> maximum;

  bool known() const { return minimum.has_value() && maximum.has_value(); }
  bool operator==(const EnteredInputDomain&) const = default;
};

struct ManualInteractionPhaseFact {
  int phase = -1;
  std::string target;
  int source_line = 0;
  EnteredInputDomain admitted_domain;
  bool operator==(const ManualInteractionPhaseFact&) const = default;
};

// Source-derived fact for the general `A PP B PP ... C R/S` entered() UI.
// It contains no program-, helper-, retry-, or register-layout recognition.
struct ManualInteractionProtocolFact {
  int protocol_id = -1;
  int prompt_source_line = 0;
  std::vector<ManualInteractionPhaseFact> phases;
  bool operator==(const ManualInteractionProtocolFact&) const = default;
};

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
  std::vector<CellRole> roles;
  StopDisposition stop_disposition = StopDisposition::Unknown;
  std::optional<ManualInteractionAnchor> manual_interaction;
  // Complete runtime target sets for an indirect command. Presence is an
  // authoritative proof claim; consumers reject a missing or empty set.
  // Flow integers are current physical command addresses and strings are
  // opaque exact labels. Memory integers are post-mutation R0..Re indices.
  std::optional<std::vector<IrTarget>> indirect_flow_targets;
  std::optional<std::vector<int>> indirect_memory_targets;
  // Opaque typed provenance for semantic helper calls. Optimizers may copy or
  // union these identities, but never infer meaning from their numeric value.
  std::vector<std::uint64_t> semantic_call_origins;
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
  StopDisposition stop_disposition = StopDisposition::Unknown;
  std::optional<ManualInteractionAnchor> manual_interaction;
  std::optional<std::vector<IrTarget>> indirect_flow_targets;
  std::optional<std::vector<int>> indirect_memory_targets;
  std::vector<std::uint64_t> semantic_call_origins;
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
