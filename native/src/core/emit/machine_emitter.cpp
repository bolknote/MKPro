#include "mkpro/core/emit/machine_emitter.hpp"

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace mkpro {

namespace {

std::string trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  return value;
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::pair<std::string, std::optional<std::string>> split_exponent(std::string value) {
  const std::size_t pos = value.find('e');
  if (pos == std::string::npos)
    return {std::move(value), std::nullopt};
  return {value.substr(0, pos), value.substr(pos + 1U)};
}

bool represents_zero(const std::string& raw) {
  char* end = nullptr;
  const double value = std::strtod(raw.c_str(), &end);
  return end != raw.c_str() && value == 0.0;
}

void append_diagnostic(std::vector<Diagnostic>& diagnostics, DiagnosticSeverity severity,
                       std::string code, std::string message) {
  diagnostics.push_back(Diagnostic{
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
  });
}

AddressSpaceModel address_space_model_for_options(const CompileOptions& options) {
  return address_space_model_for_feature_profile(options.feature_profile);
}

int safe_address_to_opcode(int address, std::vector<Diagnostic>& diagnostics,
                           const CompileOptions& options) {
  try {
    return official_address_to_opcode(address, address_space_model_for_options(options));
  } catch (const std::exception& error) {
    if (options.analysis && address >= 0)
      return address & 0xff;
    append_diagnostic(diagnostics, DiagnosticSeverity::Error, "address-out-of-range", error.what());
    return -1;
  }
}

std::string safe_format_address(int address,
                                AddressSpaceModel model = AddressSpaceModel::Standard) {
  try {
    return format_address(address, model);
  } catch (const std::exception&) {
    std::ostringstream out;
    out << ">" << std::uppercase << std::hex << address;
    return out.str();
  }
}

bool all_items_are_labels(const std::vector<MachineItem>& items) {
  return std::all_of(items.begin(), items.end(),
                     [](const MachineItem& item) { return item.kind == MachineItemKind::Label; });
}

} // namespace

void MachineEmitter::emit_number(std::string raw) {
  current_x_variable.reset();
  current_x_expression.reset();
  current_x_formatted_coord_report_body.reset();
  current_x_aliases.clear();
  current_x_known_zero = false;
  if (machine_entry_open)
    emit_op(0x0e, "В↑", "separate adjacent number entry");

  const std::string normalized = lower_ascii(trim(raw));
  const bool negative = starts_with(normalized, "-");
  const std::string unsigned_value = negative ? normalized.substr(1) : normalized;
  const auto [mantissa, exponent] = split_exponent(unsigned_value);

  for (char ch : mantissa.empty() ? std::string("0") : mantissa) {
    if (ch == '.') {
      emit_op(0x0a, ".");
    } else if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      emit_op(ch - '0', std::string(1, ch));
    }
  }

  if (exponent.has_value()) {
    emit_op(0x0c, "ВП", "exponent");
    const bool exponent_negative = starts_with(*exponent, "-");
    const bool exponent_positive = starts_with(*exponent, "+");
    const std::string exponent_digits =
        (exponent_negative || exponent_positive) ? exponent->substr(1) : *exponent;
    for (char ch : exponent_digits) {
      if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
        emit_op(ch - '0', std::string(1, ch));
      }
    }
    if (exponent_negative)
      emit_op(0x0b, "/-/", "negative exponent");
  }
  if (negative)
    emit_op(0x0b, "/-/", "negative number");
  current_x_known_zero = represents_zero(normalized);
}

void MachineEmitter::emit_jump(int opcode, std::string mnemonic, IrTarget target,
                               std::optional<std::string> comment, std::optional<int> source_line) {
  if (opcode != 0x53) {
    const auto* label = std::get_if<std::string>(&target);
    if (label != nullptr)
      record_label_edge(*label, current_x_variable);
  }
  const std::optional<std::string> address_comment =
      comment.has_value() ? comment : std::optional<std::string>{mnemonic};
  emit_op(opcode, std::move(mnemonic), comment, source_line);
  emit_address(std::move(target), address_comment, source_line);
}

void MachineEmitter::emit_address(IrTarget target, std::optional<std::string> comment,
                                  std::optional<int> source_line) {
  MachineItem item = MachineItem::address(std::move(target));
  item.comment = std::move(comment);
  item.source_line = source_line;
  items.push_back(std::move(item));
}

void MachineEmitter::emit_formal_address(int opcode, std::optional<std::string> comment,
                                         std::optional<int> source_line) {
  current_x_expression.reset();
  current_x_formatted_coord_report_body.reset();
  const FormalAddressInfo info = formal_address_info(opcode, address_space_model);
  MachineItem item = MachineItem::address(info.ordinal);
  item.formal_opcode = opcode;
  if (comment.has_value()) {
    item.comment =
        *comment + "; formal " + info.label + "->" + format_address(info.actual, address_space_model);
  }
  item.source_line = source_line;
  items.push_back(std::move(item));
}

void MachineEmitter::emit_op(int opcode, std::optional<std::string> mnemonic,
                             std::optional<std::string> comment, std::optional<int> source_line,
                             bool raw) {
  MachineItem item = MachineItem::op(opcode, mnemonic.value_or(opcode_by_code(opcode).name));
  item.comment = std::move(comment);
  item.source_line = source_line;
  item.raw = raw;
  item.manual_interaction = std::move(next_op_manual_interaction);
  next_op_manual_interaction.reset();
  items.push_back(std::move(item));

  if (opcode >= 0x80 && opcode <= 0xfe)
    coord_list_counter_known_one = false;
  current_x_variable.reset();
  current_x_expression.reset();
  current_x_formatted_coord_report_body.reset();
  current_x_aliases.clear();
  current_x_known_zero = false;
  machine_entry_open = opcode <= 0x0c;
}

void MachineEmitter::emit_stop(StopDisposition disposition,
                               std::optional<std::string> mnemonic,
                               std::optional<std::string> comment,
                               std::optional<int> source_line, bool raw) {
  emit_op(0x50, std::move(mnemonic), std::move(comment), source_line, raw);
  items.back().stop_disposition = disposition;
}

void MachineEmitter::record_label_edge(const std::string& label, std::optional<std::string> fact) {
  const auto it = label_edge_x.find(label);
  if (it == label_edge_x.end()) {
    label_edge_x.emplace(label, std::move(fact));
    return;
  }
  if (it->second != fact)
    it->second.reset();
}

void MachineEmitter::emit_label(std::string name, MachineEmitterLabelMetadata metadata) {
  if (all_items_are_labels(items))
    zero_address_labels.insert(name);
  coord_list_counter_known_one = false;
  current_x_expression.reset();
  current_x_formatted_coord_report_body.reset();

  MachineItem item = MachineItem::label(name);
  item.procedure_boundary = std::move(metadata.procedure_boundary);
  item.procedure_name = std::move(metadata.procedure_name);
  item.hidden = metadata.hidden;
  items.push_back(std::move(item));

  const auto edge_it = label_edge_x.find(name);
  if (edge_it != label_edge_x.end()) {
    if (current_x_variable != edge_it->second)
      current_x_variable.reset();
    current_x_known_zero = false;
  }
  current_x_aliases.clear();
  if (current_x_variable.has_value())
    current_x_aliases.insert(*current_x_variable);
}

std::string MachineEmitter::fresh_label(std::string prefix) {
  const std::string label = "__" + prefix + "_" + std::to_string(label_counter_);
  ++label_counter_;
  return label;
}

ResolvedStep build_resolved_step(int address, int opcode, std::string mnemonic,
                                 std::optional<std::string> comment) {
  ResolvedStep step;
  step.address = address;
  step.opcode = opcode;
  step.hex = opcode_by_code(opcode).hex;
  step.mnemonic = std::move(mnemonic);
  step.comment = std::move(comment);
  return step;
}

ResolvedProgram resolve_machine_items(const std::vector<MachineItem>& items,
                                      const CompileOptions& options) {
  std::map<std::string, int> label_addresses;
  std::set<std::string> hidden_labels;
  int address = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label) {
      label_addresses[item.name] = address;
      if (item.hidden)
        hidden_labels.insert(item.name);
    } else {
      ++address;
    }
  }

  ResolvedProgram result;
  address = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      continue;
    if (!options.analysis && address > 0xff) {
      append_diagnostic(result.diagnostics, DiagnosticSeverity::Error, "address-out-of-range",
                        "Program address " + std::to_string(address) +
                            " exceeds formal MK-61 address range.");
      break;
    }
    if (item.kind == MachineItemKind::Op) {
      result.steps.push_back(
          build_resolved_step(address, item.opcode, item.mnemonic, item.comment));
      ++address;
      continue;
    }

    int target_address = 0;
    if (const auto* numeric = std::get_if<int>(&item.target)) {
      target_address = *numeric;
    } else {
      const auto label_it = label_addresses.find(std::get<std::string>(item.target));
      if (label_it == label_addresses.end()) {
        append_diagnostic(result.diagnostics, DiagnosticSeverity::Error, "unknown-label",
                          "Unknown label '" + std::get<std::string>(item.target) + "'");
        continue;
      }
      target_address = label_it->second;
    }

    const int opcode = item.formal_opcode.has_value()
                           ? *item.formal_opcode
                           : safe_address_to_opcode(target_address, result.diagnostics, options);
    if (opcode < 0) {
      ++address;
      continue;
    }
    const std::string mnemonic = item.formal_opcode.has_value()
                                     ? format_formal_address_opcode(*item.formal_opcode)
                                     : safe_format_address(target_address,
                                                           address_space_model_for_options(options));
    result.steps.push_back(build_resolved_step(address, opcode, mnemonic, item.comment));
    ++address;
  }

  for (const auto& [label, label_address] : label_addresses) {
    if (hidden_labels.contains(label))
      continue;
    result.labels.push_back(LabelAddress{.name = label, .address = label_address});
  }
  std::sort(result.labels.begin(), result.labels.end(),
            [](const LabelAddress& a, const LabelAddress& b) {
              if (a.address != b.address)
                return a.address < b.address;
              return a.name < b.name;
            });
  return result;
}

} // namespace mkpro
