#include "mkpro/core/phase_selector_materializer.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/raw_bcd_unary_selector.hpp"

#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core {

namespace {

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

std::optional<std::string> normalized_register(const std::string& text) {
  try {
    const std::string normalized = register_from_text(text);
    const int index = register_index(normalized);
    return index >= 0 && index <= 0x0e ? std::optional<std::string>(normalized) : std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<int> normalized_register_index(const std::string& text) {
  const std::optional<std::string> normalized = normalized_register(text);
  if (!normalized.has_value())
    return std::nullopt;
  try {
    return register_index(*normalized);
  } catch (const std::exception&) {
    return std::nullopt;
  }
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

std::optional<std::string> toggled_raw_sign(const std::string& raw) {
  if (raw.empty())
    return std::nullopt;
  if (raw.front() == '-') {
    if (raw.size() == 1U)
      return std::nullopt;
    return raw.substr(1U);
  }
  return "-" + raw;
}

int raw_result_sign(const std::string& raw) {
  if (raw.empty() || raw == "0" || raw == "-0")
    return 0;
  return raw.front() == '-' ? -1 : 1;
}

bool register_encoded_opcode_uses(int opcode, int register_index_value) {
  if ((opcode & 0x0f) != register_index_value)
    return false;
  const int family = opcode & 0xf0;
  return family == 0x40 || family == 0x60 || (family >= 0x70 && family <= 0xe0);
}

bool factor_register_has_no_indirect_or_alias_use(const std::vector<MachineItem>& items,
                                                  int factor_register_index) {
  return std::none_of(items.begin(), items.end(), [&](const MachineItem& item) {
    if (item.kind != MachineItemKind::Op)
      return false;
    if (factor_register_index == 0 && (item.opcode == 0x4f || item.opcode == 0x6f))
      return true;
    const int family = item.opcode & 0xf0;
    return family >= 0x70 && family <= 0xe0 && (item.opcode & 0x0f) == factor_register_index;
  });
}

bool seed_register_uses_are_only_proved_preloads(
    const std::vector<MachineItem>& items, int seed_register_index,
    const std::vector<std::size_t>& preload_store_indices, std::vector<std::string>& reasons) {
  const std::set<std::size_t> preload_set(preload_store_indices.begin(),
                                          preload_store_indices.end());
  if (preload_set.size() != preload_store_indices.size()) {
    reasons.push_back("raw seed preload proof contains a duplicate item index");
    return false;
  }
  for (const std::size_t item_index : preload_set) {
    if (!is_op(items, item_index, 0x40 + seed_register_index)) {
      reasons.push_back("raw seed preload proof does not name an X->P seed store");
      return false;
    }
  }
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Op ||
        !register_encoded_opcode_uses(item.opcode, seed_register_index)) {
      continue;
    }
    if (!preload_set.contains(item_index)) {
      reasons.push_back("raw seed register already has an unproved machine-code use");
      return false;
    }
  }
  return true;
}

bool indices_in_range(const std::vector<MachineItem>& items,
                      const PhaseSelectorRelocationProof& relocation,
                      std::vector<std::string>& reasons) {
  const std::vector<std::size_t> indices = {
      relocation.factor_recall_item_index,      relocation.factor_sign_item_index,
      relocation.factor_store_item_index,       relocation.selector_high_digit_item_index,
      relocation.selector_low_digit_item_index, relocation.selector_store_item_index,
      relocation.continuation_entry_item_index, relocation.selector_target_item_index,
  };
  if (std::any_of(indices.begin(), indices.end(),
                  [&](std::size_t item_index) { return item_index >= items.size(); })) {
    reasons.push_back("relocation proof contains an out-of-range item index");
    return false;
  }
  return true;
}

bool fixed_targets_survive_growth(const std::vector<MachineItem>& items, const ArtifactIndex& index,
                                  const PhaseSelectorRelocationProof& relocation,
                                  const PhaseSelectorMaterializerOptions& options,
                                  std::vector<std::string>& reasons) {
  const int growth_address = index.item_addresses.at(relocation.factor_recall_item_index);
  for (const MachineItem& item : items) {
    if (item.kind != MachineItemKind::Address)
      continue;
    const bool symbolic =
        !item.formal_opcode.has_value() && std::holds_alternative<std::string>(item.target);
    if (symbolic)
      continue;
    const std::optional<int> target =
        resolved_address_target(item, index, options.address_space_model);
    if (!target.has_value()) {
      reasons.push_back("remaining fixed address operand cannot be resolved for growth");
      return false;
    }
    if (*target > growth_address) {
      reasons.push_back(
          "remaining fixed address target after the materializer is not symbolically relocatable");
      return false;
    }
  }
  return true;
}

int materialized_cell_count(PackedDeltaFactorMaterialization materialization) {
  return materialization == PackedDeltaFactorMaterialization::DeriveWithKSign ? 7 : 8;
}

MachineItem materialized_op(int opcode, std::string comment, std::string role,
                            std::optional<int> source_line, bool raw = false) {
  MachineItem item = MachineItem::op(opcode, opcode_by_code(opcode).name);
  item.comment = std::move(comment);
  item.roles.push_back(std::move(role));
  item.source_line = source_line;
  item.raw = raw;
  return item;
}

bool verify_final_artifact(const std::vector<MachineItem>& items,
                           std::size_t materializer_begin_item_index,
                           std::size_t continuation_entry_item_index,
                           std::size_t selector_target_item_index, int seed_register_index,
                           int selector_register_index, int factor_register_index,
                           PackedDeltaFactorMaterialization materialization, int expected_cells,
                           int expected_materializer_address, int expected_target_address,
                           std::vector<std::string>& reasons) {
  const ArtifactIndex index = index_artifact(items);
  if (!index.duplicate_labels.empty()) {
    reasons.push_back("rewritten artifact contains duplicate labels");
    return false;
  }
  if (!address_operands_are_well_formed(items, index, reasons))
    return false;
  if (index.cells != expected_cells) {
    reasons.push_back("rewritten artifact has the wrong materialized cell count");
    return false;
  }
  if (index.item_addresses.at(materializer_begin_item_index) != expected_materializer_address) {
    reasons.push_back("rewritten materializer moved from its proved entry address");
    return false;
  }
  if (index.item_addresses.at(selector_target_item_index) != expected_target_address) {
    reasons.push_back("rewritten selector target does not match the raw unary oracle address");
    return false;
  }

  std::vector<int> expected;
  if (materialization == PackedDeltaFactorMaterialization::PreserveIndependentToggle) {
    expected = {0x60 + factor_register_index,
                0x0b,
                0x40 + factor_register_index,
                0x60 + seed_register_index,
                0x0b,
                0x40 + seed_register_index,
                0x1e,
                0x40 + selector_register_index};
  } else {
    expected = {0x60 + seed_register_index,     0x0b, 0x40 + seed_register_index,  0x1e,
                0x40 + selector_register_index, 0x32, 0x40 + factor_register_index};
  }
  std::size_t cursor = materializer_begin_item_index;
  for (std::size_t position = 0; position < expected.size(); ++position) {
    const int opcode = expected.at(position);
    if (!is_op(items, cursor, opcode)) {
      reasons.push_back("rewritten raw phase materializer has an unexpected opcode sequence");
      return false;
    }
    const std::optional<std::size_t> next = next_cell_item(items, cursor);
    if (position + 1U < expected.size()) {
      if (!next.has_value()) {
        reasons.push_back("rewritten raw phase materializer is truncated");
        return false;
      }
      cursor = *next;
    }
  }
  if (next_cell_item(items, cursor) != continuation_entry_item_index) {
    reasons.push_back("rewritten materializer does not fall through to the proved continuation");
    return false;
  }
  return true;
}

} // namespace

PhaseSelectorMaterializerVerification verify_phase_selector_materializer(
    const std::vector<MachineItem>& items, const RawPhaseSelectorProof& raw_selector,
    const PackedDeltaFactorProof& factor, const PhaseSelectorRelocationProof& relocation,
    const PhaseSelectorMaterializerOptions& options) {
  PhaseSelectorMaterializerVerification verification;
  const ArtifactIndex index = index_artifact(items);
  verification.input_cells = index.cells;
  verification.cell_delta = materialized_cell_count(factor.materialization) - 6;
  verification.output_cells = index.cells + verification.cell_delta;

  if (options.address_space_model != AddressSpaceModel::Standard) {
    verification.reasons.push_back(
        "raw phase selector is currently proved only for the standard MK-61 address space");
  }
  if (!index.duplicate_labels.empty())
    verification.reasons.push_back("artifact contains duplicate labels");
  if (!address_operands_are_well_formed(items, index, verification.reasons))
    return verification;

  const std::optional<std::string> seed = normalized_register(raw_selector.seed_register);
  const std::optional<std::string> selector = normalized_register(raw_selector.selector_register);
  const std::optional<std::string> factor_register = normalized_register(factor.factor_register);
  const std::optional<int> seed_index = normalized_register_index(raw_selector.seed_register);
  const std::optional<int> selector_index =
      normalized_register_index(raw_selector.selector_register);
  const std::optional<int> factor_index = normalized_register_index(factor.factor_register);
  if (!seed.has_value() || !seed_index.has_value()) {
    verification.reasons.push_back("raw phase proof names an invalid seed register");
  } else {
    verification.seed_register = *seed;
  }
  if (!selector.has_value() || !selector_index.has_value()) {
    verification.reasons.push_back("raw phase proof names an invalid selector register");
  } else {
    verification.selector_register = *selector;
  }
  if (!factor_register.has_value() || !factor_index.has_value()) {
    verification.reasons.push_back("factor proof names an invalid factor register");
  } else {
    verification.factor_register = *factor_register;
  }
  if (seed.has_value() && selector.has_value() && factor_register.has_value()) {
    if (*seed == *selector || *seed == *factor_register || *selector == *factor_register)
      verification.reasons.push_back("seed, selector, and factor registers must be distinct");
    try {
      if (!is_stable_indirect_selector(*seed) || !is_stable_indirect_selector(*selector)) {
        verification.reasons.push_back("seed and selector must use stable R7..Re registers");
      }
    } catch (const std::exception&) {
      verification.reasons.push_back("stable register proof cannot be established");
    }
  }

  require_claim(raw_selector.runtime_seed_set_is_exhaustive,
                "raw seed facts do not exhaust every phase entry", verification.reasons);
  require_claim(raw_selector.phase_is_strict_two_cycle,
                "raw seed phase is not proved to be a strict two-cycle", verification.reasons);
  require_claim(raw_selector.angle_mode_is_invariant_at_phase_entries,
                "raw unary angle mode is not proved invariant at every phase entry",
                verification.reasons);
  require_claim(raw_selector.seed_preload_is_delivered,
                "initial raw seed preload is not proved delivered", verification.reasons);
  require_claim(raw_selector.seed_register_is_free_before_materialization,
                "raw seed register is not proved free before materialization",
                verification.reasons);
  require_claim(raw_selector.seed_and_selector_are_preserved_between_phase_entries,
                "seed or selector can be clobbered between phase entries", verification.reasons);
  if (seed_index.has_value()) {
    seed_register_uses_are_only_proved_preloads(
        items, *seed_index, raw_selector.seed_preload_store_item_indices, verification.reasons);
  }

  require_claim(factor.required_factor_domain_is_exact_unit_signs,
                "packed delta factor is not proved to be exactly +/-1", verification.reasons);
  require_claim(factor.factor_phase_matches_raw_seed_phase,
                "packed delta factor phase does not match the raw seed phase",
                verification.reasons);
  require_claim(factor.every_update_consumer_reads_factor_register,
                "not every packed update consumes the proved factor register",
                verification.reasons);
  require_claim(factor.factor_is_unwritten_until_update_consumers,
                "packed delta factor can be overwritten before an update consumer",
                verification.reasons);
  if (factor.materialization == PackedDeltaFactorMaterialization::DeriveWithKSign) {
    require_claim(factor.ksign_of_raw_unary_result_is_required_factor,
                  "K sign of the raw unary result is not proved equal to the required factor",
                  verification.reasons);
  }
  if (factor_index.has_value() &&
      !factor_register_has_no_indirect_or_alias_use(items, *factor_index)) {
    verification.reasons.push_back(
        "packed delta factor register has an indirect or alias machine-code use");
  }

  if (raw_selector.facts.size() != 2U) {
    verification.reasons.push_back("raw phase proof must contain exactly two alternating facts");
  } else if (selector.has_value()) {
    std::set<std::string> inputs;
    std::set<int> factors;
    for (const RawPhaseSelectorFact& fact : raw_selector.facts) {
      const std::optional<std::string> expected_toggle = toggled_raw_sign(fact.input_seed);
      if (!expected_toggle.has_value() || *expected_toggle != fact.toggled_seed)
        verification.reasons.push_back("raw phase fact does not describe one sign toggle");
      inputs.insert(fact.input_seed);
      factors.insert(fact.required_factor);
      const std::optional<RawBcdUnaryIndirectSelectorResult> evaluated =
          evaluate_raw_bcd_unary_indirect_selector(raw_selector.angle_mode,
                                                   raw_selector.unary_operation, fact.toggled_seed,
                                                   *selector, options.address_space_model);
      if (!evaluated.has_value()) {
        verification.reasons.push_back("raw phase fact is unsupported by the unary oracle");
        continue;
      }
      if (evaluated->raw_result != fact.raw_unary_result ||
          evaluated->actual_flow_target != fact.actual_flow_target) {
        verification.reasons.push_back("raw phase fact disagrees with the unary oracle");
      }
      if (raw_result_sign(evaluated->raw_result) != fact.required_factor)
        verification.reasons.push_back("raw unary sign does not equal the required packed factor");
    }
    if (inputs.size() != 2U)
      verification.reasons.push_back("raw phase facts do not contain two distinct input states");
    for (const RawPhaseSelectorFact& fact : raw_selector.facts) {
      if (!inputs.contains(fact.toggled_seed))
        verification.reasons.push_back("raw phase facts do not close into a two-cycle");
    }
    if (factors != std::set<int>{-1, 1})
      verification.reasons.push_back("raw phase factor facts are not exactly {-1,+1}");
    if (!inputs.contains(raw_selector.initial_seed))
      verification.reasons.push_back("initial raw seed is outside the proved phase cycle");
  }
  verification.seed_preload_value = raw_selector.initial_seed;

  require_claim(relocation.all_direct_references_are_relocated_or_symbolic,
                "direct references are not proved safe across materializer growth",
                verification.reasons);
  require_claim(relocation.all_other_indirect_targets_and_charges_are_rebound,
                "other indirect targets or selector charges are not proved rebound",
                verification.reasons);
  require_claim(relocation.materializer_cells_have_no_external_entries,
                "an input materializer cell may have an external entry", verification.reasons);
  require_claim(relocation.continuation_fallthrough_is_proved,
                "materializer fallthrough continuation is not proved", verification.reasons);
  require_claim(relocation.selector_consumers_target_proved_entry,
                "selector consumers are not proved to target the selected entry",
                verification.reasons);
  require_claim(relocation.replacement_stack_effect_is_proved_compatible,
                "replacement stack and X2 effects are not proved compatible with continuation",
                verification.reasons);

  if (!indices_in_range(items, relocation, verification.reasons))
    return verification;
  verification.materializer_address = index.item_addresses.at(relocation.factor_recall_item_index);
  verification.input_target_address =
      index.item_addresses.at(relocation.selector_target_item_index);
  const bool target_after_materializer =
      relocation.selector_target_item_index > relocation.selector_store_item_index;
  verification.output_target_address =
      verification.input_target_address + (target_after_materializer ? verification.cell_delta : 0);

  if (relocation.expected_input_cells != verification.input_cells ||
      relocation.expected_output_cells != verification.output_cells) {
    verification.reasons.push_back("relocation proof was built for a different artifact size");
  }
  if (relocation.expected_materializer_address != verification.materializer_address)
    verification.reasons.push_back("relocation proof has a stale materializer address");
  if (relocation.expected_input_target_address != verification.input_target_address ||
      relocation.expected_output_target_address != verification.output_target_address) {
    verification.reasons.push_back("relocation proof has a stale selector target address");
  }

  if (factor_index.has_value()) {
    if (!is_op(items, relocation.factor_recall_item_index, 0x60 + *factor_index) ||
        !is_op(items, relocation.factor_sign_item_index, 0x0b) ||
        !is_op(items, relocation.factor_store_item_index, 0x40 + *factor_index)) {
      verification.reasons.push_back(
          "input factor prefix is not recall/sign/store of one register");
    }
  }
  if (!is_op(items, relocation.selector_high_digit_item_index,
             items.at(relocation.selector_high_digit_item_index).opcode) ||
      items.at(relocation.selector_high_digit_item_index).opcode < 0 ||
      items.at(relocation.selector_high_digit_item_index).opcode > 9 ||
      !is_op(items, relocation.selector_low_digit_item_index,
             items.at(relocation.selector_low_digit_item_index).opcode) ||
      items.at(relocation.selector_low_digit_item_index).opcode < 0 ||
      items.at(relocation.selector_low_digit_item_index).opcode > 9) {
    verification.reasons.push_back("input selector charge is not a two-digit decimal literal");
  }
  if (selector_index.has_value() &&
      !is_op(items, relocation.selector_store_item_index, 0x40 + *selector_index)) {
    verification.reasons.push_back("input selector charge does not store into the proved selector");
  }

  const std::vector<std::size_t> block = {
      relocation.factor_recall_item_index,      relocation.factor_sign_item_index,
      relocation.factor_store_item_index,       relocation.selector_high_digit_item_index,
      relocation.selector_low_digit_item_index, relocation.selector_store_item_index,
  };
  for (std::size_t position = 1; position < block.size(); ++position) {
    if (block.at(position) != block.at(position - 1U) + 1U)
      verification.reasons.push_back("input phase materializer contains an interior label or gap");
    if (next_cell_item(items, block.at(position - 1U)) != block.at(position))
      verification.reasons.push_back("input phase materializer cells are not consecutive");
  }
  if (next_cell_item(items, relocation.selector_store_item_index) !=
      relocation.continuation_entry_item_index) {
    verification.reasons.push_back("proved continuation is not immediately after selector charge");
  }

  const int high = items.at(relocation.selector_high_digit_item_index).opcode;
  const int low = items.at(relocation.selector_low_digit_item_index).opcode;
  if (high >= 0 && high <= 9 && low >= 0 && low <= 9 &&
      high * 10 + low != verification.input_target_address) {
    verification.reasons.push_back("decimal selector charge does not name its proved input target");
  }
  if (items.at(relocation.selector_target_item_index).kind != MachineItemKind::Op)
    verification.reasons.push_back("selector target is not an executable command cell");
  if (relocation.selector_target_item_index >= relocation.factor_recall_item_index &&
      relocation.selector_target_item_index <= relocation.selector_store_item_index) {
    verification.reasons.push_back("selector target would be removed with the input materializer");
  }
  for (const RawPhaseSelectorFact& fact : raw_selector.facts) {
    if (fact.actual_flow_target != verification.output_target_address) {
      verification.reasons.push_back(
          "materializer growth does not place the target at the raw unary oracle address");
    }
  }
  if (!fixed_targets_survive_growth(items, index, relocation, options, verification.reasons))
    return verification;

  verification.proved = verification.reasons.empty();
  return verification;
}

PhaseSelectorMaterializerResult rewrite_phase_selector_materializer(
    const std::vector<MachineItem>& items, const RawPhaseSelectorProof& raw_selector,
    const PackedDeltaFactorProof& factor, const PhaseSelectorRelocationProof& relocation,
    const PhaseSelectorMaterializerOptions& options) {
  PhaseSelectorMaterializerResult result{.items = items};
  result.verification =
      verify_phase_selector_materializer(items, raw_selector, factor, relocation, options);
  if (!result.verification.proved)
    return result;

  const int seed_index = *normalized_register_index(raw_selector.seed_register);
  const int selector_index = *normalized_register_index(raw_selector.selector_register);
  const int factor_index = *normalized_register_index(factor.factor_register);
  const std::string seed = *normalized_register(raw_selector.seed_register);
  const std::string selector = *normalized_register(raw_selector.selector_register);
  const std::string factor_register = *normalized_register(factor.factor_register);
  const std::optional<int> source_line = items.at(relocation.factor_recall_item_index).source_line;

  std::vector<MachineItem> materialized;
  if (factor.materialization == PackedDeltaFactorMaterialization::PreserveIndependentToggle) {
    materialized.push_back(items.at(relocation.factor_recall_item_index));
    materialized.push_back(items.at(relocation.factor_sign_item_index));
    materialized.push_back(items.at(relocation.factor_store_item_index));
  }
  materialized.push_back(materialized_op(0x60 + seed_index,
                                         "recall alternating raw phase seed R" + seed,
                                         "phase-selector-seed-recall:" + seed, source_line, true));
  materialized.push_back(materialized_op(0x0b, "toggle alternating raw phase seed sign",
                                         "phase-selector-seed-toggle:" + seed, source_line, true));
  materialized.push_back(materialized_op(0x40 + seed_index,
                                         "store alternating raw phase seed R" + seed,
                                         "phase-selector-seed-store:" + seed, source_line, true));
  materialized.push_back(
      materialized_op(0x1e, "materialize raw unary phase selector",
                      "phase-selector-raw-unary:" + selector + ":" +
                          std::to_string(result.verification.output_target_address),
                      source_line, true));
  materialized.push_back(
      materialized_op(0x40 + selector_index,
                      "store raw phase selector R" + selector +
                          " target=" + std::to_string(result.verification.output_target_address),
                      "phase-selector-store:" + selector + ":" +
                          std::to_string(result.verification.output_target_address),
                      source_line, true));
  if (factor.materialization == PackedDeltaFactorMaterialization::DeriveWithKSign) {
    materialized.push_back(materialized_op(0x32, "derive packed delta factor from raw unary sign",
                                           "phase-selector-factor-sign:" + factor_register,
                                           source_line, true));
    materialized.push_back(
        materialized_op(0x40 + factor_index, "store packed delta factor R" + factor_register,
                        "phase-selector-factor-store:" + factor_register, source_line, true));
  }

  std::vector<std::optional<std::size_t>> rewritten_indices(items.size());
  std::vector<MachineItem> rewritten;
  rewritten.reserve(items.size() + static_cast<std::size_t>(result.verification.cell_delta));
  std::optional<std::size_t> materializer_begin;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (item_index == relocation.factor_recall_item_index) {
      materializer_begin = rewritten.size();
      rewritten.insert(rewritten.end(), materialized.begin(), materialized.end());
      continue;
    }
    if (item_index >= relocation.factor_sign_item_index &&
        item_index <= relocation.selector_store_item_index) {
      continue;
    }
    rewritten_indices.at(item_index) = rewritten.size();
    rewritten.push_back(items.at(item_index));
  }

  const std::size_t continuation_entry =
      *rewritten_indices.at(relocation.continuation_entry_item_index);
  const std::size_t selector_target = *rewritten_indices.at(relocation.selector_target_item_index);
  std::vector<std::string> final_reasons;
  if (!materializer_begin.has_value() ||
      !verify_final_artifact(rewritten, *materializer_begin, continuation_entry, selector_target,
                             seed_index, selector_index, factor_index, factor.materialization,
                             result.verification.output_cells,
                             result.verification.materializer_address,
                             result.verification.output_target_address, final_reasons)) {
    result.verification.reasons.insert(result.verification.reasons.end(), final_reasons.begin(),
                                       final_reasons.end());
    result.verification.proved = false;
    return result;
  }

  result.items = std::move(rewritten);
  result.verification.final_artifact_proved = true;
  result.applied = 1;
  return result;
}

} // namespace mkpro::core
