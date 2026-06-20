#include "mkpro/core/super_dark_layout.hpp"

#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace mkpro {

namespace {

std::string upper_hex(int value) {
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << value;
  return out.str();
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string upper_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return value;
}

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

std::string compact_selector(std::string value) {
  value = upper_ascii(trim_ascii(std::move(value)));
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) { return std::isspace(ch) != 0; }),
              value.end());
  return value;
}

bool contains_role(const LayoutIrCell& cell, const std::string& role) {
  return std::find(cell.roles.begin(), cell.roles.end(), role) != cell.roles.end();
}

bool tactic_marks_super_dark(const std::string& tactic) {
  const std::string lowered = lower_ascii(tactic);
  return lowered.find("super-dark") != std::string::npos ||
         lowered.find("super dark") != std::string::npos;
}

std::string register_for_indirect_jump_opcode(int opcode) {
  static const std::vector<std::string> registers = {
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  const int index = opcode - 0x80;
  if (index < 0 || index >= static_cast<int>(registers.size()))
    return "?";
  return registers.at(static_cast<std::size_t>(index));
}

std::optional<std::string> selector_value_for_register(
    const std::map<std::string, std::string>& selector_values, const std::string& reg) {
  const std::string upper = upper_ascii(reg);
  const std::vector<std::string> aliases = {reg, upper, "R" + reg, "R" + upper, "r" + reg};
  for (const std::string& alias : aliases) {
    const auto found = selector_values.find(alias);
    if (found != selector_values.end())
      return found->second;
  }
  return std::nullopt;
}

bool is_single_super_dark_formal(const std::string& normalized) {
  return normalized.size() == 2 && normalized.at(0) == 'F' && normalized.at(1) >= 'A' &&
         normalized.at(1) <= 'F';
}

bool is_super_dark_selector_value(const std::optional<std::string>& value) {
  if (!value.has_value())
    return false;
  const std::string normalized = compact_selector(*value);
  if (is_single_super_dark_formal(normalized))
    return true;
  return normalized == "FA..FF" || normalized == "FA-FF" || normalized == "FA…FF" ||
         normalized == "SUPER-DARK" || normalized == "SUPER_DARK";
}

std::vector<SuperDarkDispatchCell> collect_super_dark_dispatch_cells(
    const std::vector<LayoutIrCell>& layout,
    const std::map<std::string, std::string>& selector_values) {
  std::vector<SuperDarkDispatchCell> cells;
  for (const LayoutIrCell& cell : layout) {
    if (cell.opcode < 0x87 || cell.opcode > 0x8e)
      continue;
    if (!tactic_marks_super_dark(cell.tactic))
      continue;
    const std::string reg = register_for_indirect_jump_opcode(cell.opcode);
    cells.push_back(SuperDarkDispatchCell{
        .address = cell.address,
        .opcode = cell.opcode,
        .register_name = reg,
        .tactic = cell.tactic,
        .selector_value = selector_value_for_register(selector_values, reg),
    });
  }
  return cells;
}

std::vector<int> required_super_dark_offsets(
    const std::vector<SuperDarkDispatchCell>& dispatch_cells) {
  std::set<int> offsets;
  for (const SuperDarkDispatchCell& cell : dispatch_cells) {
    if (!cell.selector_value.has_value())
      continue;
    const std::string normalized = compact_selector(*cell.selector_value);
    if (is_single_super_dark_formal(normalized)) {
      offsets.insert(static_cast<int>(normalized.at(1) - 'A'));
      continue;
    }
    if (is_super_dark_selector_value(cell.selector_value)) {
      for (int offset = 0; offset <= 5; ++offset)
        offsets.insert(offset);
    }
  }
  return {offsets.begin(), offsets.end()};
}

} // namespace

SuperDarkLayoutProof verify_super_dark_suffix_layout(
    const std::vector<LayoutIrCell>& layout, const SuperDarkLayoutOptions& options) {
  std::map<int, LayoutIrCell> by_address;
  for (const LayoutIrCell& cell : layout)
    by_address[cell.address] = cell;

  SuperDarkLayoutProof proof;
  proof.dispatch_cells =
      collect_super_dark_dispatch_cells(layout, options.selector_values);

  std::vector<SuperDarkDispatchCell> proved_dispatch_cells;
  std::copy_if(proof.dispatch_cells.begin(), proof.dispatch_cells.end(),
               std::back_inserter(proved_dispatch_cells),
               [](const SuperDarkDispatchCell& cell) {
                 return is_super_dark_selector_value(cell.selector_value);
               });
  const std::vector<int> required_offsets = required_super_dark_offsets(proved_dispatch_cells);

  if (proof.dispatch_cells.empty()) {
    proof.reasons.push_back("no super-dark К БП R dispatch cell is marked in the layout");
  } else if (proved_dispatch_cells.empty()) {
    proof.reasons.push_back(
        "no super-dark dispatch register has a proved FA..FF selector value");
  }

  for (const int offset : required_offsets) {
    const int formal = 0xfa + offset;
    const int entry_address = 48 + offset;
    const int continuation_address = 1 + offset;
    const auto entry = by_address.find(entry_address);
    const auto continuation = by_address.find(continuation_address);

    if (entry == by_address.end()) {
      proof.reasons.push_back(upper_hex(formal) + " has no physical entry cell at " +
                              std::to_string(entry_address));
      continue;
    }
    if (continuation == by_address.end()) {
      proof.reasons.push_back(upper_hex(formal) + " has no continuation cell at " +
                              std::to_string(continuation_address));
      continue;
    }
    if (!contains_role(entry->second, "exec")) {
      proof.reasons.push_back(upper_hex(formal) + " entry " +
                              std::to_string(entry_address) + " is not executable");
      continue;
    }
    if (opcode_by_code(entry->second.opcode).takes_address) {
      proof.reasons.push_back(upper_hex(formal) + " entry " +
                              std::to_string(entry_address) +
                              " is a two-cell address-taking command");
      continue;
    }
    if (!contains_role(continuation->second, "exec")) {
      proof.reasons.push_back(upper_hex(formal) + " continuation " +
                              std::to_string(continuation_address) + " is not executable");
      continue;
    }

    proof.pairs.push_back(SuperDarkLayoutPair{
        .formal = formal,
        .entry_address = entry_address,
        .continuation_address = continuation_address,
        .entry_opcode = entry->second.opcode,
        .continuation_opcode = continuation->second.opcode,
    });
  }

  if (proof.pairs.size() != required_offsets.size() && proof.reasons.empty()) {
    proof.reasons.push_back(
        "FA..FF did not produce the required super-dark entry/continuation pairs");
  }

  proof.proved = !proved_dispatch_cells.empty() &&
                 proof.pairs.size() == required_offsets.size() && proof.reasons.empty();
  return proof;
}

} // namespace mkpro
