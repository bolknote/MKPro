#include "mkpro/core/late_bound_decimal_selector.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core {

namespace {

constexpr std::string_view kHighRolePrefix = "late-decimal-selector-high:";
constexpr std::string_view kLowRolePrefix = "late-decimal-selector-low:";

struct Marker {
  LateBoundDecimalSelectorPart part = LateBoundDecimalSelectorPart::High;
  std::string target_label;
  std::size_t item_index = 0;
  int cell_address = 0;
};

struct Pair {
  Marker high;
  Marker low;
  int target_address = 0;
};

void append_error(std::vector<Diagnostic>& diagnostics, std::string code, std::string message) {
  diagnostics.push_back(Diagnostic{
      .severity = DiagnosticSeverity::Error,
      .code = std::move(code),
      .message = std::move(message),
  });
}

bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::optional<std::pair<LateBoundDecimalSelectorPart, std::string>>
parse_marker_role(std::string_view role, bool& recognized) {
  recognized = false;
  std::string_view target;
  LateBoundDecimalSelectorPart part = LateBoundDecimalSelectorPart::High;
  if (starts_with(role, kHighRolePrefix)) {
    recognized = true;
    target = role.substr(kHighRolePrefix.size());
    part = LateBoundDecimalSelectorPart::High;
  } else if (starts_with(role, kLowRolePrefix)) {
    recognized = true;
    target = role.substr(kLowRolePrefix.size());
    part = LateBoundDecimalSelectorPart::Low;
  } else {
    return std::nullopt;
  }
  if (target.empty())
    return std::nullopt;
  return std::pair{part, std::string(target)};
}

bool is_strict_placeholder(const MachineItem& item,
                           const LateBoundDecimalSelectorOptions& options) {
  return item.kind == MachineItemKind::Op && !item.raw &&
         item.opcode == options.placeholder_opcode && item.mnemonic == options.placeholder_mnemonic;
}

bool is_non_raw_decimal_digit(const MachineItem& item) {
  return item.kind == MachineItemKind::Op && !item.raw && item.opcode >= 0x00 &&
         item.opcode <= 0x09 && item.mnemonic == std::to_string(item.opcode);
}

} // namespace

std::string make_late_bound_decimal_selector_role(LateBoundDecimalSelectorPart part,
                                                  std::string_view target_label) {
  if (target_label.empty())
    throw std::invalid_argument("late-bound decimal selector target label must not be empty");
  const std::string_view prefix =
      part == LateBoundDecimalSelectorPart::High ? kHighRolePrefix : kLowRolePrefix;
  return std::string(prefix) + std::string(target_label);
}

std::optional<std::vector<std::string>>
late_bound_decimal_selector_target_labels(const std::vector<MachineItem>& items) {
  std::set<std::string> labels;
  for (const MachineItem& item : items) {
    for (const std::string& role : item.roles) {
      bool recognized = false;
      const std::optional<std::pair<LateBoundDecimalSelectorPart, std::string>> marker =
          parse_marker_role(role, recognized);
      if (!recognized)
        continue;
      if (!marker.has_value())
        return std::nullopt;
      labels.insert(marker->second);
    }
  }
  return std::vector<std::string>(labels.begin(), labels.end());
}

LateBoundDecimalSelectorResult
bind_late_bound_decimal_selectors(const std::vector<MachineItem>& items,
                                  const LateBoundDecimalSelectorOptions& options) {
  LateBoundDecimalSelectorResult result;
  result.items = items;

  if (options.minimum_target_address < 0 || options.maximum_target_address > 99 ||
      options.minimum_target_address > options.maximum_target_address ||
      options.placeholder_opcode < 0x00 || options.placeholder_opcode > 0x09 ||
      options.placeholder_mnemonic.empty()) {
    append_error(result.diagnostics, "late-decimal-selector-invalid-options",
                 "Late-bound decimal selector options must describe a decimal digit placeholder "
                 "and an address interval contained in 00..99.");
    return result;
  }

  std::map<std::string, std::vector<int>> label_addresses;
  std::vector<std::optional<int>> cell_address_by_item(items.size());
  int cell_address = 0;
  for (std::size_t index = 0; index < items.size(); ++index) {
    const MachineItem& item = items.at(index);
    if (item.kind == MachineItemKind::Label) {
      label_addresses[item.name].push_back(cell_address);
    } else {
      cell_address_by_item.at(index) = cell_address;
      ++cell_address;
    }
  }

  std::vector<std::optional<Marker>> marker_by_item(items.size());
  for (std::size_t index = 0; index < items.size(); ++index) {
    const MachineItem& item = items.at(index);
    std::vector<std::pair<LateBoundDecimalSelectorPart, std::string>> item_markers;
    for (const std::string& role : item.roles) {
      bool recognized = false;
      std::optional<std::pair<LateBoundDecimalSelectorPart, std::string>> marker =
          parse_marker_role(role, recognized);
      if (!recognized)
        continue;
      if (!marker.has_value()) {
        append_error(result.diagnostics, "late-decimal-selector-malformed-marker",
                     "Late-bound decimal selector marker at item " + std::to_string(index) +
                         " has an empty target label.");
        continue;
      }
      item_markers.push_back(std::move(*marker));
    }
    if (item_markers.empty())
      continue;
    if (item_markers.size() != 1U) {
      append_error(result.diagnostics, "late-decimal-selector-multiple-markers",
                   "Late-bound decimal selector item " + std::to_string(index) +
                       " must carry exactly one high or low marker role.");
      continue;
    }
    if (!is_strict_placeholder(item, options)) {
      append_error(result.diagnostics, "late-decimal-selector-not-placeholder",
                   "Late-bound decimal selector item " + std::to_string(index) +
                       " is not the configured non-raw decimal placeholder.");
      continue;
    }
    if (!cell_address_by_item.at(index).has_value()) {
      append_error(result.diagnostics, "late-decimal-selector-not-placeholder",
                   "Late-bound decimal selector marker cannot be attached to a label.");
      continue;
    }
    marker_by_item.at(index) = Marker{
        .part = item_markers.front().first,
        .target_label = std::move(item_markers.front().second),
        .item_index = index,
        .cell_address = *cell_address_by_item.at(index),
    };
  }

  std::vector<Pair> pairs;
  for (std::size_t index = 0; index < marker_by_item.size(); ++index) {
    if (!marker_by_item.at(index).has_value())
      continue;
    const Marker& high = *marker_by_item.at(index);
    if (high.part != LateBoundDecimalSelectorPart::High) {
      append_error(result.diagnostics, "late-decimal-selector-unexpected-low",
                   "Late-bound decimal selector low placeholder at item " + std::to_string(index) +
                       " is not preceded by its adjacent high digit.");
      continue;
    }
    if (index + 1U >= marker_by_item.size() || !marker_by_item.at(index + 1U).has_value()) {
      append_error(result.diagnostics, "late-decimal-selector-nonadjacent-pair",
                   "Selector placeholders for target label '" + high.target_label +
                       "' must be adjacent, with high before low.");
      continue;
    }
    const Marker& low = *marker_by_item.at(index + 1U);
    if (low.part != LateBoundDecimalSelectorPart::Low) {
      append_error(result.diagnostics, "late-decimal-selector-incomplete-pair",
                   "Late-bound decimal selector high placeholder for target label '" +
                       high.target_label + "' is not followed by a low placeholder.");
      continue;
    }
    ++index;
    if (low.target_label != high.target_label) {
      append_error(
          result.diagnostics, "late-decimal-selector-mismatched-target",
          "Adjacent late-bound decimal selector digits refer to different target labels '" +
              high.target_label + "' and '" + low.target_label + "'.");
      continue;
    }
    const std::string& target_label = high.target_label;
    const auto label_it = label_addresses.find(target_label);
    if (label_it == label_addresses.end()) {
      append_error(result.diagnostics, "late-decimal-selector-missing-label",
                   "Late-bound decimal selector target label '" + target_label +
                       "' does not exist in the final layout.");
      continue;
    }
    if (label_it->second.size() != 1U) {
      append_error(result.diagnostics, "late-decimal-selector-duplicate-label",
                   "Late-bound decimal selector target label '" + target_label +
                       "' is not unique in the final layout.");
      continue;
    }
    const int target_address = label_it->second.front();
    if (target_address < options.minimum_target_address ||
        target_address > options.maximum_target_address) {
      append_error(result.diagnostics, "late-decimal-selector-target-out-of-range",
                   "Late-bound decimal selector target label '" + target_label + "' resolves to " +
                       std::to_string(target_address) +
                       ", outside the configured two-digit interval " +
                       std::to_string(options.minimum_target_address) + ".." +
                       std::to_string(options.maximum_target_address) + ".");
      continue;
    }
    pairs.push_back(Pair{.high = high, .low = low, .target_address = target_address});
  }

  if (!result.diagnostics.empty())
    return result;

  for (const Pair& pair : pairs) {
    const int high_digit = pair.target_address / 10;
    const int low_digit = pair.target_address % 10;
    MachineItem& high = result.items.at(pair.high.item_index);
    MachineItem& low = result.items.at(pair.low.item_index);
    high.opcode = high_digit;
    high.mnemonic = std::to_string(high_digit);
    low.opcode = low_digit;
    low.mnemonic = std::to_string(low_digit);
    result.proofs.push_back(LateBoundDecimalSelectorProof{
        .target_label = pair.high.target_label,
        .target_address = pair.target_address,
        .high_item_index = pair.high.item_index,
        .low_item_index = pair.low.item_index,
        .high_cell_address = pair.high.cell_address,
        .low_cell_address = pair.low.cell_address,
        .high_digit = high_digit,
        .low_digit = low_digit,
    });
  }
  result.applied = static_cast<int>(pairs.size());
  return result;
}

LateBoundDecimalSelectorResult
rebind_late_bound_decimal_selectors(const std::vector<MachineItem>& items,
                                    const LateBoundDecimalSelectorOptions& options) {
  std::vector<MachineItem> placeholders = items;
  LateBoundDecimalSelectorResult rejected;
  rejected.items = items;

  for (std::size_t index = 0; index < placeholders.size(); ++index) {
    MachineItem& item = placeholders.at(index);
    bool marked = false;
    for (const std::string& role : item.roles) {
      bool recognized = false;
      (void)parse_marker_role(role, recognized);
      marked = marked || recognized;
    }
    if (!marked)
      continue;
    if (!is_non_raw_decimal_digit(item)) {
      append_error(rejected.diagnostics, "late-decimal-selector-not-bound-digit",
                   "Late-bound decimal selector item " + std::to_string(index) +
                       " is not a non-raw decimal digit command eligible for rebinding.");
      continue;
    }
    item.opcode = options.placeholder_opcode;
    item.mnemonic = options.placeholder_mnemonic;
  }

  if (!rejected.diagnostics.empty())
    return rejected;

  LateBoundDecimalSelectorResult result =
      bind_late_bound_decimal_selectors(placeholders, options);
  if (!result.diagnostics.empty()) {
    result.items = items;
    result.applied = 0;
    result.proofs.clear();
  }
  return result;
}

} // namespace mkpro::core
