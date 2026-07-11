#include "mkpro/core/terminal_report_tail.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mkpro::core {

namespace {

constexpr std::string_view kZeroReturnRole = "terminal-report-zero-return:";
constexpr std::string_view kContinuationStoreRole = "terminal-report-continuation-store:";

struct ArtifactIndex {
  std::vector<int> item_addresses;
  std::map<std::string, int> label_addresses;
  std::set<std::string> duplicate_labels;
  int cells = 0;
};

ArtifactIndex index_artifact(const std::vector<MachineItem>& items) {
  ArtifactIndex index;
  index.item_addresses.resize(items.size(), 0);
  int address = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    index.item_addresses.at(item_index) = address;
    if (item.kind == MachineItemKind::Label) {
      if (index.label_addresses.contains(item.name))
        index.duplicate_labels.insert(item.name);
      index.label_addresses[item.name] = address;
      continue;
    }
    ++address;
  }
  index.cells = address;
  return index;
}

std::optional<std::size_t> previous_cell_item(const std::vector<MachineItem>& items,
                                              std::size_t before) {
  while (before > 0) {
    --before;
    if (items.at(before).kind != MachineItemKind::Label)
      return before;
  }
  return std::nullopt;
}

std::optional<std::size_t> next_cell_item(const std::vector<MachineItem>& items,
                                          std::size_t after) {
  for (++after; after < items.size(); ++after) {
    if (items.at(after).kind != MachineItemKind::Label)
      return after;
  }
  return std::nullopt;
}

bool is_op(const std::vector<MachineItem>& items, std::size_t item_index, int opcode) {
  return item_index < items.size() && items.at(item_index).kind == MachineItemKind::Op &&
         items.at(item_index).opcode == opcode;
}

std::optional<int> normalized_register_index(const std::string& text) {
  try {
    const std::string normalized = register_from_text(text);
    const int index = register_index(normalized);
    if (index < 0 || index > 0x0e)
      return std::nullopt;
    return index;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<std::string> normalized_register(const std::string& text) {
  try {
    const std::string normalized = register_from_text(text);
    const int index = register_index(normalized);
    return index >= 0 && index <= 0x0e ? std::optional<std::string>(normalized) : std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<int> resolved_address_target(const MachineItem& item, const ArtifactIndex& index,
                                           AddressSpaceModel model) {
  if (item.kind != MachineItemKind::Address)
    return std::nullopt;
  if (item.formal_opcode.has_value()) {
    try {
      return formal_address_info(*item.formal_opcode, model).actual;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }
  if (const auto* numeric = std::get_if<int>(&item.target))
    return *numeric;
  const auto* label = std::get_if<std::string>(&item.target);
  if (label == nullptr)
    return std::nullopt;
  const auto found = index.label_addresses.find(*label);
  return found == index.label_addresses.end() ? std::nullopt : std::optional<int>(found->second);
}

void require_claim(bool claim, std::string reason, std::vector<std::string>& reasons) {
  if (!claim)
    reasons.push_back(std::move(reason));
}

void replace_all(std::string& value, std::string_view from, std::string_view to) {
  std::size_t cursor = 0;
  while ((cursor = value.find(from, cursor)) != std::string::npos) {
    value.replace(cursor, from.size(), to);
    cursor += to.size();
  }
}

std::optional<IndirectAddressEvaluation> evaluate_raw_selector_flow(std::string_view selector,
                                                                    std::string_view raw_value,
                                                                    AddressSpaceModel model) {
  if (const std::optional<IndirectAddressEvaluation> ordinary =
          evaluate_indirect_address(selector, raw_value, IndirectOperationKind::Flow, model)) {
    return ordinary;
  }

  // A raw register can contain a single hexadecimal mantissa digit in
  // exponent form (for example ГE-2), which the normalized decimal parser
  // intentionally does not accept. Preserve that BCD mantissa structurally,
  // then delegate the formal/physical mapping to the canonical evaluator.
  std::string text(raw_value);
  text.erase(text.begin(), std::find_if(text.begin(), text.end(),
                                        [](unsigned char ch) { return std::isspace(ch) == 0; }));
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    text.pop_back();
  replace_all(text, "а", "A");
  replace_all(text, "А", "A");
  replace_all(text, "в", "B");
  replace_all(text, "В", "B");
  replace_all(text, "с", "C");
  replace_all(text, "С", "C");
  replace_all(text, "г", "D");
  replace_all(text, "Г", "D");
  replace_all(text, "д", "D");
  replace_all(text, "Д", "D");
  replace_all(text, "е", "E");
  replace_all(text, "Е", "E");
  if (!text.empty() && text.front() == '-')
    text.erase(text.begin());
  for (char& ch : text) {
    if (ch >= 'a' && ch <= 'z')
      ch = static_cast<char>(ch - ('a' - 'A'));
  }
  if (text.size() < 4U || text.at(0) < 'A' || text.at(0) > 'F' || text.at(1) != 'E' ||
      text.at(2) != '-' || !std::all_of(text.begin() + 3, text.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
      })) {
    return std::nullopt;
  }
  const std::string structural_mantissa =
      std::string(1, static_cast<char>(text.at(0) - 'A' + 'a')) + "0000000";
  return evaluate_indirect_address(selector, structural_mantissa, IndirectOperationKind::Flow,
                                   model);
}

bool indices_in_range(const std::vector<MachineItem>& items,
                      const TerminalReportTailRelocationProof& relocation,
                      std::vector<std::string>& reasons) {
  const std::vector<std::size_t> indices = {
      relocation.mask_item_index,
      relocation.fraction_item_index,
      relocation.direct_condition_item_index,
      relocation.direct_address_item_index,
      relocation.dot_item_index,
      relocation.stop_item_index,
      relocation.continuation_entry_item_index,
      relocation.direct_return_item_index,
      relocation.zero_return_item_index,
  };
  if (std::any_of(indices.begin(), indices.end(),
                  [&](std::size_t item_index) { return item_index >= items.size(); })) {
    reasons.push_back("relocation proof contains an out-of-range item index");
    return false;
  }
  return true;
}

bool address_operands_are_well_formed(const std::vector<MachineItem>& items,
                                      const ArtifactIndex& index,
                                      std::vector<std::string>& reasons) {
  try {
    for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
      const MachineItem& item = items.at(item_index);
      if (item.kind == MachineItemKind::Op && opcode_by_code(item.opcode).takes_address) {
        const std::optional<std::size_t> operand = next_cell_item(items, item_index);
        if (!operand.has_value() || items.at(*operand).kind != MachineItemKind::Address) {
          reasons.push_back("address-taking opcode has no adjacent address operand");
          return false;
        }
        continue;
      }
      if (item.kind != MachineItemKind::Address)
        continue;
      const std::optional<std::size_t> flow = previous_cell_item(items, item_index);
      if (!flow.has_value() || items.at(*flow).kind != MachineItemKind::Op ||
          !opcode_by_code(items.at(*flow).opcode).takes_address) {
        reasons.push_back("artifact contains an orphan address operand");
        return false;
      }
      if (!item.formal_opcode.has_value()) {
        if (const auto* label = std::get_if<std::string>(&item.target);
            label != nullptr && !index.label_addresses.contains(*label)) {
          reasons.push_back("address operand references an unresolved label");
          return false;
        }
      }
    }
  } catch (const std::exception&) {
    reasons.push_back("artifact contains an invalid opcode in an address-bearing position");
    return false;
  }
  return true;
}

bool fixed_targets_survive_two_cell_removal(const std::vector<MachineItem>& items,
                                            const ArtifactIndex& index,
                                            const TerminalReportTailRelocationProof& relocation,
                                            const TerminalReportTailOptions& options,
                                            std::vector<std::string>& reasons) {
  const int removal_address = index.item_addresses.at(relocation.direct_condition_item_index);
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (item_index == relocation.direct_address_item_index)
      continue;
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Address)
      continue;
    const bool symbolic =
        !item.formal_opcode.has_value() && std::holds_alternative<std::string>(item.target);
    if (symbolic)
      continue;
    const std::optional<int> target =
        resolved_address_target(item, index, options.address_space_model);
    if (!target.has_value()) {
      reasons.push_back("remaining fixed address operand cannot be resolved for relocation");
      return false;
    }
    if (*target > removal_address) {
      reasons.push_back("remaining fixed address target after the rewritten tail is not "
                        "symbolically relocatable");
      return false;
    }
  }
  return true;
}

bool locally_verify_final_artifact(const std::vector<MachineItem>& items,
                                   std::size_t branch_item_index, std::size_t store_item_index,
                                   std::size_t continuation_entry_item_index,
                                   std::size_t zero_return_item_index,
                                   std::size_t direct_return_item_index, int selector_index,
                                   int terminal_slot_index, int expected_cells,
                                   std::vector<std::string>& reasons) {
  const ArtifactIndex index = index_artifact(items);
  if (!index.duplicate_labels.empty()) {
    reasons.push_back("rewritten artifact contains duplicate labels");
    return false;
  }
  if (!address_operands_are_well_formed(items, index, reasons))
    return false;
  if (index.cells != expected_cells) {
    reasons.push_back("rewritten artifact did not remove exactly two cells");
    return false;
  }
  if (!is_op(items, branch_item_index, 0x70 + selector_index)) {
    reasons.push_back("rewritten zero-return conditional has the wrong opcode");
    return false;
  }
  if (!is_op(items, store_item_index, 0x40 + terminal_slot_index)) {
    reasons.push_back("rewritten terminal-slot store has the wrong opcode");
    return false;
  }
  const std::optional<std::size_t> after_branch = next_cell_item(items, branch_item_index);
  const std::optional<std::size_t> after_store = next_cell_item(items, store_item_index);
  if (after_branch != store_item_index) {
    reasons.push_back("rewritten branch does not fall through directly to the terminal-slot store");
    return false;
  }
  if (after_store != continuation_entry_item_index) {
    reasons.push_back("rewritten store does not fall through directly to the proved continuation");
    return false;
  }
  if (!is_op(items, zero_return_item_index, 0x52) ||
      index.item_addresses.at(zero_return_item_index) != 0) {
    reasons.push_back("rewritten artifact has no bare return at physical 00");
    return false;
  }
  if (!is_op(items, direct_return_item_index, 0x52)) {
    reasons.push_back("rewritten continuation lost its proved bare return");
    return false;
  }
  return true;
}

} // namespace

TerminalReportTailVerification verify_terminal_report_tail(
    const std::vector<MachineItem>& items, const RawZeroReturnSelectorProof& raw_selector,
    const TerminalContinuationLivenessProof& continuation,
    const TerminalReportTailRelocationProof& relocation, const TerminalReportTailOptions& options) {
  TerminalReportTailVerification verification;
  const ArtifactIndex index = index_artifact(items);
  verification.input_cells = index.cells;
  verification.output_cells = index.cells >= 2 ? index.cells - 2 : 0;

  if (options.address_space_model != AddressSpaceModel::Standard) {
    verification.reasons.push_back(
        "raw zero-return tail is currently proved only for the standard MK-61 address space");
  }
  if (!index.duplicate_labels.empty())
    verification.reasons.push_back("artifact contains duplicate labels");
  if (!address_operands_are_well_formed(items, index, verification.reasons))
    return verification;

  const std::optional<std::string> selector = normalized_register(raw_selector.selector_register);
  const std::optional<int> selector_index =
      normalized_register_index(raw_selector.selector_register);
  if (!selector.has_value() || !selector_index.has_value()) {
    verification.reasons.push_back("raw selector proof names an invalid register");
  } else {
    verification.selector_register = *selector;
    try {
      if (!is_stable_indirect_selector(*selector))
        verification.reasons.push_back("raw zero-return selector is not a stable R7..Re register");
    } catch (const std::exception&) {
      verification.reasons.push_back("raw selector stability cannot be established");
    }
  }

  require_claim(raw_selector.runtime_value_set_is_exhaustive,
                "raw selector facts do not exhaust every runtime entry value",
                verification.reasons);
  require_claim(raw_selector.selector_is_unwritten_until_use,
                "raw selector can be overwritten before the conditional", verification.reasons);
  if (raw_selector.facts.empty()) {
    verification.reasons.push_back("raw selector proof contains no oracle facts");
  } else {
    std::set<std::string> raw_values;
    for (const RawZeroReturnSelectorFact& fact : raw_selector.facts) {
      if (fact.raw_value.empty()) {
        verification.reasons.push_back("raw selector oracle fact has an empty raw value");
        continue;
      }
      if (!raw_values.insert(fact.raw_value).second)
        verification.reasons.push_back("raw selector oracle facts contain a duplicate value");
      const std::optional<IndirectAddressEvaluation> evaluated =
          selector.has_value()
              ? evaluate_raw_selector_flow(*selector, fact.raw_value, options.address_space_model)
              : std::nullopt;
      if (!evaluated.has_value() || !evaluated->actual_flow_target.has_value() ||
          *evaluated->actual_flow_target != 0 || fact.actual_flow_target != 0 ||
          fact.actual_flow_target != *evaluated->actual_flow_target) {
        verification.reasons.push_back("raw selector oracle fact does not target physical 00");
      }
      if (!fact.conditional_preserves_selector) {
        verification.reasons.push_back(
            "raw selector oracle fact does not prove conditional preservation");
      }
      if (!fact.conditional_preserves_x_y_z_t) {
        verification.reasons.push_back(
            "raw selector oracle fact does not prove X/Y/Z/T preservation");
      }
      if (!fact.conditional_preserves_x2)
        verification.reasons.push_back("raw selector oracle fact does not prove X2 preservation");
      verification.raw_selector_values.push_back(fact.raw_value);
    }
  }

  const std::optional<std::string> terminal_slot =
      normalized_register(continuation.terminal_slot_register);
  const std::optional<int> terminal_slot_index =
      normalized_register_index(continuation.terminal_slot_register);
  if (!terminal_slot.has_value() || !terminal_slot_index.has_value()) {
    verification.reasons.push_back("continuation proof names an invalid terminal-slot register");
  } else {
    verification.terminal_slot_register = *terminal_slot;
  }
  require_claim(continuation.fractional_predicate_is_terminal_payload,
                "fractional predicate is not proved equal to the terminal payload",
                verification.reasons);
  require_claim(continuation.previous_slot_value_is_dead_on_report_path,
                "previous terminal-slot value is live on the report path", verification.reasons);
  require_claim(continuation.stored_payload_is_live_until_terminal_stop,
                "stored report payload is not live through the terminal stop",
                verification.reasons);
  require_claim(continuation.every_report_continuation_reaches_terminal_stop,
                "not every report continuation reaches the proved terminal stop",
                verification.reasons);
  require_claim(continuation.no_prior_observable_event_error_or_divergence,
                "report continuation can observe, error, or diverge before the terminal stop",
                verification.reasons);
  require_claim(continuation.continuation_stack_and_x2_effects_are_unobservable,
                "continuation can observe the rewritten stack or X2 state", verification.reasons);

  require_claim(relocation.continuation_is_relocated_immediately_after_tail,
                "continuation body has not been proved relocated after the provisional tail",
                verification.reasons);
  require_claim(relocation.all_direct_references_are_relocated_or_symbolic,
                "direct references are not proved safe across the two-cell relocation",
                verification.reasons);
  require_claim(relocation.all_indirect_targets_and_selector_charges_are_rebound,
                "indirect targets or selector charges are not proved rebound after relocation",
                verification.reasons);
  require_claim(relocation.removed_cells_have_no_external_entries,
                "a removed direct-address/dot/STOP cell may have an external entry",
                verification.reasons);
  require_claim(relocation.zero_and_direct_returns_have_equivalent_stack_contracts,
                "physical-00 and original direct returns lack an equivalent stack contract",
                verification.reasons);

  if (!indices_in_range(items, relocation, verification.reasons))
    return verification;
  if (relocation.expected_input_cells != index.cells)
    verification.reasons.push_back("relocation proof was built for a different input cell count");

  verification.direct_condition_address =
      index.item_addresses.at(relocation.direct_condition_item_index);
  verification.continuation_entry_address =
      index.item_addresses.at(relocation.continuation_entry_item_index);
  verification.zero_return_address = index.item_addresses.at(relocation.zero_return_item_index);
  if (relocation.expected_direct_condition_address != verification.direct_condition_address) {
    verification.reasons.push_back(
        "relocation proof was built for a different direct-condition address");
  }
  if (relocation.expected_continuation_entry_address != verification.continuation_entry_address) {
    verification.reasons.push_back(
        "relocation proof was built for a different continuation-entry address");
  }

  if (!is_op(items, relocation.mask_item_index, 0x37))
    verification.reasons.push_back("terminal report tail is not preceded by KAND");
  if (!is_op(items, relocation.fraction_item_index, 0x35))
    verification.reasons.push_back("terminal report tail is not preceded by Kfrac");
  if (!is_op(items, relocation.direct_condition_item_index, 0x57))
    verification.reasons.push_back("terminal report tail does not use direct F x!=0");
  if (items.at(relocation.direct_address_item_index).kind != MachineItemKind::Address)
    verification.reasons.push_back("direct F x!=0 has no address operand");
  if (!is_op(items, relocation.dot_item_index, 0x0a))
    verification.reasons.push_back("terminal report tail has no provisional dot");
  if (!is_op(items, relocation.stop_item_index, 0x50))
    verification.reasons.push_back("terminal report tail has no provisional STOP");
  if (!is_op(items, relocation.direct_return_item_index, 0x52))
    verification.reasons.push_back("original no-report target is not a bare return");
  if (!is_op(items, relocation.zero_return_item_index, 0x52) ||
      verification.zero_return_address != 0) {
    verification.reasons.push_back("physical address 00 is not the proved bare return");
  }

  const std::optional<std::size_t> after_mask = next_cell_item(items, relocation.mask_item_index);
  const std::optional<std::size_t> after_fraction =
      next_cell_item(items, relocation.fraction_item_index);
  const std::optional<std::size_t> after_condition =
      next_cell_item(items, relocation.direct_condition_item_index);
  const std::optional<std::size_t> after_address =
      next_cell_item(items, relocation.direct_address_item_index);
  const std::optional<std::size_t> after_dot = next_cell_item(items, relocation.dot_item_index);
  const std::optional<std::size_t> after_stop = next_cell_item(items, relocation.stop_item_index);
  if (after_mask != relocation.fraction_item_index ||
      after_fraction != relocation.direct_condition_item_index ||
      after_condition != relocation.direct_address_item_index ||
      after_address != relocation.dot_item_index || after_dot != relocation.stop_item_index) {
    verification.reasons.push_back(
        "KAND/Kfrac/direct-condition/address/dot/STOP cells are not physically consecutive");
  }
  if (after_stop != relocation.continuation_entry_item_index) {
    verification.reasons.push_back(
        "proved continuation entry is not the first cell after the provisional STOP");
  }
  const std::optional<std::size_t> before_condition =
      previous_cell_item(items, relocation.direct_condition_item_index);
  const std::optional<std::size_t> before_fraction =
      previous_cell_item(items, relocation.fraction_item_index);
  if (before_condition != relocation.fraction_item_index ||
      before_fraction != relocation.mask_item_index) {
    verification.reasons.push_back("proof indices do not describe one contiguous report tail");
  }
  if (index.item_addresses.at(relocation.direct_return_item_index) <
      verification.continuation_entry_address) {
    verification.reasons.push_back(
        "original direct return is not contained in the relocated continuation suffix");
  }
  const std::optional<int> direct_target = resolved_address_target(
      items.at(relocation.direct_address_item_index), index, options.address_space_model);
  if (!direct_target.has_value() ||
      *direct_target != index.item_addresses.at(relocation.direct_return_item_index)) {
    verification.reasons.push_back(
        "direct no-report operand does not target the proved continuation return");
  }
  if (!fixed_targets_survive_two_cell_removal(items, index, relocation, options,
                                              verification.reasons)) {
    return verification;
  }

  verification.proved = verification.reasons.empty();
  return verification;
}

TerminalReportTailResult rewrite_terminal_report_tail(
    const std::vector<MachineItem>& items, const RawZeroReturnSelectorProof& raw_selector,
    const TerminalContinuationLivenessProof& continuation,
    const TerminalReportTailRelocationProof& relocation, const TerminalReportTailOptions& options) {
  TerminalReportTailResult result{.items = items};
  result.verification =
      verify_terminal_report_tail(items, raw_selector, continuation, relocation, options);
  if (!result.verification.proved)
    return result;

  const std::string selector = *normalized_register(raw_selector.selector_register);
  const int selector_index = *normalized_register_index(selector);
  const std::string terminal_slot = *normalized_register(continuation.terminal_slot_register);
  const int terminal_slot_index = *normalized_register_index(terminal_slot);

  std::vector<std::optional<std::size_t>> rewritten_indices(items.size());
  std::vector<MachineItem> rewritten;
  rewritten.reserve(items.size() - 2U);
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (item_index == relocation.direct_address_item_index ||
        item_index == relocation.stop_item_index) {
      continue;
    }
    MachineItem item = items.at(item_index);
    if (item_index == relocation.direct_condition_item_index) {
      item.opcode = 0x70 + selector_index;
      item.mnemonic = opcode_by_code(item.opcode).name;
      item.comment = "terminal report zero-return through proved raw selector R" + selector +
                     " to physical 00";
      item.roles = {std::string(kZeroReturnRole) + selector + ":0"};
      item.raw = true;
      item.target = 0;
      item.formal_opcode.reset();
    } else if (item_index == relocation.dot_item_index) {
      item.opcode = 0x40 + terminal_slot_index;
      item.mnemonic = opcode_by_code(item.opcode).name;
      item.comment = "terminal report continuation payload store R" + terminal_slot;
      item.roles = {std::string(kContinuationStoreRole) + terminal_slot};
      item.raw = false;
      item.target = 0;
      item.formal_opcode.reset();
    }
    rewritten_indices.at(item_index) = rewritten.size();
    rewritten.push_back(std::move(item));
  }

  const std::size_t branch_item_index =
      *rewritten_indices.at(relocation.direct_condition_item_index);
  const std::size_t store_item_index = *rewritten_indices.at(relocation.dot_item_index);
  const std::size_t continuation_entry_item_index =
      *rewritten_indices.at(relocation.continuation_entry_item_index);
  const std::size_t zero_return_item_index =
      *rewritten_indices.at(relocation.zero_return_item_index);
  const std::size_t direct_return_item_index =
      *rewritten_indices.at(relocation.direct_return_item_index);

  std::vector<std::string> final_reasons;
  if (!locally_verify_final_artifact(rewritten, branch_item_index, store_item_index,
                                     continuation_entry_item_index, zero_return_item_index,
                                     direct_return_item_index, selector_index, terminal_slot_index,
                                     result.verification.output_cells, final_reasons)) {
    result.verification.reasons.insert(result.verification.reasons.end(), final_reasons.begin(),
                                       final_reasons.end());
    result.verification.proved = false;
    return result;
  }

  result.items = std::move(rewritten);
  result.verification.final_artifact_proved = true;
  result.applied = 1;
  result.removed_cells = 2;
  return result;
}

} // namespace mkpro::core
