#include "mkpro/core/selector_writeback.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace mkpro::core {
namespace {

std::string spelling(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return ch == ',' ? '.' : static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::optional<double> numeric(const std::string& value) {
  char* end = nullptr;
  const double parsed = std::strtod(value.c_str(), &end);
  if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed))
    return std::nullopt;
  return parsed;
}

std::optional<std::string> numeric_preload_word(const std::string& value) {
  // Literal setup performs numeric entry and X->P, not a raw register-word
  // injection. A positive subunit decimal is stored with a negative exponent.
  // Keep the raw-word decoder unchanged for its other callers.
  const std::size_t dot = value.find('.');
  if (dot == std::string::npos ||
      !std::all_of(value.begin(), value.begin() + static_cast<std::string::difference_type>(dot),
                   [](char ch) { return ch == '0'; }) ||
      !std::all_of(value.begin() + static_cast<std::string::difference_type>(dot) + 1, value.end(),
                   [](char ch) { return ch >= '0' && ch <= '9'; }))
    return value;
  const std::string fraction = value.substr(dot + 1);
  const std::size_t first = fraction.find_first_not_of('0');
  if (first == std::string::npos)
    return value;
  std::string digits = fraction.substr(first);
  while (digits.size() > 1 && digits.back() == '0')
    digits.pop_back();
  if (digits.size() > 8 || first >= 99)
    return std::nullopt; // No unproved rounding or exponent overflow.
  std::string word(1, digits.front());
  if (digits.size() > 1)
    word += "." + digits.substr(1);
  return word + "e-" + std::to_string(first + 1);
}

bool complete_word_is_preserved(const std::string& before, const std::string& after) {
  if (before == after && before.find("e-") != std::string::npos)
    return true;
  std::string_view digits(before);
  if (digits.starts_with('-'))
    digits.remove_prefix(1);
  // Short integers are rewritten into a denormalized eight-digit word, even
  // though their numerical value is unchanged. Do not confuse that fact with
  // preservation of an arbitrary raw/bitwise observation.
  if (digits.size() != 8U ||
      !std::all_of(digits.begin(), digits.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
      })) {
    return false;
  }
  const auto old_number = numeric(before);
  const auto new_number = numeric(after);
  return old_number.has_value() && old_number == new_number;
}

bool recall_projection_is_preserved(const std::vector<MachineItem>& items,
                                    std::size_t recall, const std::string& before,
                                    const std::string& after) {
  std::size_t next = recall + 1;
  while (next < items.size() && items[next].kind == MachineItemKind::Label)
    ++next;
  if (next == items.size() || items[next].kind != MachineItemKind::Op ||
      items[next].raw || items[next].manual_interaction.has_value())
    return false;
  const auto old_number = numeric(before);
  const auto new_number = numeric(after);
  if (!old_number.has_value() || !new_number.has_value())
    return false;
  if (items[next].opcode == 0x35) {
    return *old_number - std::trunc(*old_number) ==
           *new_number - std::trunc(*new_number);
  }
  return items[next].opcode >= 0x10 && items[next].opcode <= 0x13 &&
         old_number == new_number;
}

} // namespace

bool selector_writeback_is_unobserved(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& control_flow,
    std::size_t command_item, const PreloadReport& preload,
    AddressSpaceModel model, std::string* failure) {
  const auto reject = [&](std::string reason) {
    if (failure != nullptr)
      *failure = std::move(reason);
    return false;
  };
  if (!control_flow.proved || command_item >= items.size() ||
      control_flow.execution_states.size() != control_flow.execution_successors.size())
    return reject("selector writeback lacks authoritative control flow");
  int reg;
  std::optional<IndirectAddressEvaluation> decoded;
  const auto setup_word = numeric_preload_word(spelling(preload.value));
  if (!setup_word.has_value())
    return reject("selector numeric setup requires unproved rounding");
  try {
    reg = register_index(preload.register_name);
    if (reg < 7 || reg > 14 || preload.setup_expression ||
        preload.setup_expression_text.has_value() || preload.setup_target_name.has_value())
      return reject("selector writeback requires a stable literal carrier");
    decoded = evaluate_indirect_address(preload.register_name, *setup_word,
                                        IndirectOperationKind::Flow, model);
    if (*setup_word != spelling(preload.value)) {
      const auto original_target = evaluate_indirect_address(
          preload.register_name, preload.value, IndirectOperationKind::Flow, model);
      // The surrounding layout currently binds the source spelling. Never
      // accept a normalized word that would redirect that already proved edge.
      if (!decoded.has_value() || !original_target.has_value() ||
          decoded->actual_flow_target != original_target->actual_flow_target)
        return reject("numeric setup changes the bound selector target");
    }
  } catch (const std::exception&) {
    return reject("selector writeback value cannot be decoded");
  }
  if (!decoded.has_value() || !decoded->result_value.has_value())
    return reject("selector writeback value is unknown");
  const std::string before = *setup_word;
  const std::string after = spelling(*decoded->result_value);
  if (complete_word_is_preserved(before, after))
    return true;

  // Address-only carriers and globally invariant data projections do not
  // become observable merely because control later reaches a resumable stop.
  // This also avoids traversing the CFG for the common integer selector case.
  bool all_projections_preserved = true;
  for (std::size_t index = 0; index < items.size(); ++index) {
    const auto& item = items[index];
    if (item.raw || (item.kind == MachineItemKind::Address &&
                     std::find(item.roles.begin(), item.roles.end(), "exec") != item.roles.end())) {
      all_projections_preserved = false;
      break;
    }
    if (item.kind == MachineItemKind::Op && item.opcode == 0x60 + reg &&
        (item.manual_interaction.has_value() ||
         !recall_projection_is_preserved(items, index, before, after))) {
      all_projections_preserved = false;
      break;
    }
    const auto memory = control_flow.indirect_memory_targets.find(index);
    if (memory != control_flow.indirect_memory_targets.end() &&
        (std::find(memory->second.begin(), memory->second.end(), reg) != memory->second.end() ||
         (item.opcode & 0x0f) == reg)) {
      all_projections_preserved = false;
      break;
    }
  }
  if (all_projections_preserved)
    return true;

  std::vector<std::size_t> pending;
  for (std::size_t state = 0; state < control_flow.execution_states.size(); ++state) {
    if (control_flow.execution_states[state].item_index == command_item) {
      const auto& next = control_flow.execution_successors[state];
      pending.insert(pending.end(), next.begin(), next.end());
    }
  }
  std::vector<bool> seen(control_flow.execution_states.size(), false);
  while (!pending.empty()) {
    const std::size_t state = pending.back();
    pending.pop_back();
    if (state >= seen.size())
      return reject("selector writeback has an unresolved successor");
    if (seen[state])
      continue;
    seen[state] = true;
    const auto& identity = control_flow.execution_states[state];
    if (identity.item_index >= items.size())
      return reject("selector writeback lost a command identity");
    const MachineItem& item = items[identity.item_index];
    if (item.kind != MachineItemKind::Op || item.raw || item.manual_interaction.has_value())
      return reject("selector writeback reaches an opaque or manual observation");
    if (item.opcode == 0x40 + reg)
      continue; // A complete overwrite kills the changed value on this path.
    if (item.opcode == 0x60 + reg &&
        !recall_projection_is_preserved(items, identity.item_index, before, after)) {
      return reject("selector writeback changes data read at logical address " +
                    std::to_string(identity.address));
    }
    const auto memory = control_flow.indirect_memory_targets.find(identity.item_index);
    if (memory != control_flow.indirect_memory_targets.end() &&
        (std::find(memory->second.begin(), memory->second.end(), reg) != memory->second.end() ||
         (item.opcode & 0x0f) == reg)) {
      return reject("selector writeback reaches an indirect data observation");
    }
    if (item.opcode == 0x50 && item.stop_disposition != StopDisposition::Terminal)
      return reject("selector writeback reaches a resumable observation");
    const auto& next = control_flow.execution_successors[state];
    pending.insert(pending.end(), next.begin(), next.end());
  }
  return true;
}

} // namespace mkpro::core
