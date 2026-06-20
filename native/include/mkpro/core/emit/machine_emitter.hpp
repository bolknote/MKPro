#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/result.hpp"
#include "mkpro/core/types.hpp"

#include <memory>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mkpro {

struct Expression;

struct MachineEmitterLabelMetadata {
  std::optional<std::string> procedure_boundary;
  std::optional<std::string> procedure_name;
  bool hidden = false;
};

struct FormattedCoordReportBodyFact {
  std::string cell;
  int cell_width = 0;
  std::string bearing;
  int bearing_width = 0;
};

class MachineEmitter {
public:
  std::vector<MachineItem> items;
  std::optional<std::string> current_x_variable;
  std::shared_ptr<Expression> current_x_expression;
  std::optional<FormattedCoordReportBodyFact> current_x_formatted_coord_report_body;
  std::set<std::string> current_x_aliases;
  bool current_x_known_zero = false;
  bool coord_list_counter_known_one = false;
  std::set<std::string> zero_address_labels;
  std::map<std::string, std::optional<std::string>> label_edge_x;
  bool machine_entry_open = false;

  void emit_number(std::string raw);
  void emit_jump(int opcode, std::string mnemonic, IrTarget target,
                 std::optional<std::string> comment = std::nullopt,
                 std::optional<int> source_line = std::nullopt);
  void emit_address(IrTarget target, std::optional<std::string> comment = std::nullopt,
                    std::optional<int> source_line = std::nullopt);
  void emit_formal_address(int opcode, std::optional<std::string> comment = std::nullopt,
                           std::optional<int> source_line = std::nullopt);
  void emit_op(int opcode, std::optional<std::string> mnemonic = std::nullopt,
               std::optional<std::string> comment = std::nullopt,
               std::optional<int> source_line = std::nullopt, bool raw = false);
  void record_label_edge(const std::string& label, std::optional<std::string> fact);
  void emit_label(std::string name, MachineEmitterLabelMetadata metadata = {});
  std::string fresh_label(std::string prefix);

private:
  int label_counter_ = 0;
};

struct ResolvedProgram {
  std::vector<ResolvedStep> steps;
  std::vector<LabelAddress> labels;
  std::vector<Diagnostic> diagnostics;
};

ResolvedStep build_resolved_step(int address, int opcode, std::string mnemonic,
                                 std::optional<std::string> comment = std::nullopt);
ResolvedProgram resolve_machine_items(const std::vector<MachineItem>& items,
                                      const CompileOptions& options = {});

} // namespace mkpro
