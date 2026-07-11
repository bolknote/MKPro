#include "mkpro/core/phase_selector_materializer.hpp"

#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#ifdef MKPRO_PHASE_SELECTOR_MATERIALIZER_STANDALONE_TEST
#include <iostream>
#endif
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::tests {

namespace {

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const MachineItem& item) {
    return item.kind != MachineItemKind::Label;
  }));
}

std::size_t label_item(const std::vector<MachineItem>& items, std::string_view name) {
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (items.at(index).kind == MachineItemKind::Label && items.at(index).name == name)
      return index;
  }
  throw std::runtime_error("missing fixture label " + std::string(name));
}

std::size_t next_cell(const std::vector<MachineItem>& items, std::size_t after) {
  for (++after; after < items.size(); ++after) {
    if (items.at(after).kind != MachineItemKind::Label)
      return after;
  }
  throw std::runtime_error("missing next fixture cell");
}

int item_address(const std::vector<MachineItem>& items, std::size_t wanted) {
  int address = 0;
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (index == wanted)
      return address;
    if (items.at(index).kind != MachineItemKind::Label)
      ++address;
  }
  throw std::runtime_error("missing fixture item index");
}

std::vector<MachineItem> materializer_fixture(int total_cells, int target_address) {
  std::vector<MachineItem> items;
  while (cell_count(items) < 55)
    items.push_back(MachineItem::op(0x50, "С/П"));

  items.push_back(MachineItem::label("phase_entry"));
  items.push_back(MachineItem::op(0x68, "П->X 8"));
  items.push_back(MachineItem::op(0x0b, "/-/"));
  items.push_back(MachineItem::op(0x48, "X->П 8"));
  items.push_back(MachineItem::op(target_address / 10, "selector high digit"));
  items.push_back(MachineItem::op(target_address % 10, "selector low digit"));
  items.push_back(MachineItem::op(0x4e, "X->П e"));
  items.push_back(MachineItem::label("phase_continuation"));
  items.push_back(MachineItem::op(0x8e, "К БП e"));

  while (cell_count(items) < target_address)
    items.push_back(MachineItem::op(0x50, "С/П"));
  items.push_back(MachineItem::label("selected_entry"));
  items.push_back(MachineItem::op(0x04, "4"));
  items.push_back(MachineItem::op(0x40, "X->П 0"));
  items.push_back(MachineItem::op(0x50, "С/П"));
  while (cell_count(items) < total_cells)
    items.push_back(MachineItem::op(0x50, "С/П"));
  require(cell_count(items) == total_cells, "phase fixture should have its requested size");
  return items;
}

core::RawPhaseSelectorProof valid_raw_proof() {
  return core::RawPhaseSelectorProof{
      .seed_register = "b",
      .selector_register = "e",
      .initial_seed = "ГE-2",
      .angle_mode = core::mk61_trig::AngleMode::Deg,
      .unary_operation = core::mk61_trig::Function::Tg,
      .facts = {{.input_seed = "ГE-2",
                 .toggled_seed = "-ГE-2",
                 .raw_unary_result = "-5.235988E-4",
                 .actual_flow_target = 88,
                 .required_factor = -1},
                {.input_seed = "-ГE-2",
                 .toggled_seed = "ГE-2",
                 .raw_unary_result = "5.235988E-4",
                 .actual_flow_target = 88,
                 .required_factor = 1}},
      .runtime_seed_set_is_exhaustive = true,
      .phase_is_strict_two_cycle = true,
      .angle_mode_is_invariant_at_phase_entries = true,
      .seed_preload_is_delivered = true,
      .seed_register_is_free_before_materialization = true,
      .seed_and_selector_are_preserved_between_phase_entries = true,
  };
}

core::PackedDeltaFactorProof factor_proof(core::PackedDeltaFactorMaterialization materialization) {
  return core::PackedDeltaFactorProof{
      .materialization = materialization,
      .factor_register = "8",
      .required_factor_domain_is_exact_unit_signs = true,
      .factor_phase_matches_raw_seed_phase = true,
      .every_update_consumer_reads_factor_register = true,
      .factor_is_unwritten_until_update_consumers = true,
      .ksign_of_raw_unary_result_is_required_factor = true,
  };
}

core::PhaseSelectorRelocationProof
relocation_proof(const std::vector<MachineItem>& items,
                 core::PackedDeltaFactorMaterialization materialization) {
  const std::size_t factor_recall = next_cell(items, label_item(items, "phase_entry"));
  const std::size_t factor_sign = next_cell(items, factor_recall);
  const std::size_t factor_store = next_cell(items, factor_sign);
  const std::size_t high = next_cell(items, factor_store);
  const std::size_t low = next_cell(items, high);
  const std::size_t selector_store = next_cell(items, low);
  const std::size_t continuation = next_cell(items, selector_store);
  const std::size_t target = next_cell(items, label_item(items, "selected_entry"));
  const int delta =
      materialization == core::PackedDeltaFactorMaterialization::DeriveWithKSign ? 1 : 2;
  return core::PhaseSelectorRelocationProof{
      .expected_input_cells = cell_count(items),
      .expected_output_cells = cell_count(items) + delta,
      .expected_materializer_address = item_address(items, factor_recall),
      .expected_input_target_address = item_address(items, target),
      .expected_output_target_address = item_address(items, target) + delta,
      .factor_recall_item_index = factor_recall,
      .factor_sign_item_index = factor_sign,
      .factor_store_item_index = factor_store,
      .selector_high_digit_item_index = high,
      .selector_low_digit_item_index = low,
      .selector_store_item_index = selector_store,
      .continuation_entry_item_index = continuation,
      .selector_target_item_index = target,
      .all_direct_references_are_relocated_or_symbolic = true,
      .all_other_indirect_targets_and_charges_are_rebound = true,
      .materializer_cells_have_no_external_entries = true,
      .continuation_fallthrough_is_proved = true,
      .selector_consumers_target_proved_entry = true,
      .replacement_stack_effect_is_proved_compatible = true,
  };
}

bool same_items(const std::vector<MachineItem>& left, const std::vector<MachineItem>& right) {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (!machine_items_equal(left.at(index), right.at(index)))
      return false;
  }
  return true;
}

bool contains_reason(const core::PhaseSelectorMaterializerVerification& verification,
                     std::string_view needle) {
  return std::any_of(
      verification.reasons.begin(), verification.reasons.end(),
      [&](const std::string& reason) { return reason.find(needle) != std::string::npos; });
}

std::string compact(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) { return std::isspace(ch) != 0; }),
              value.end());
  return value;
}

struct PhaseObservation {
  bool stopped = false;
  std::string factor;
  std::string seed;
  std::string selected_marker;
};

std::vector<PhaseObservation> run_two_phases(const std::vector<MachineItem>& items) {
  const ResolvedProgram resolved = resolve_machine_items(items, {});
  require(resolved.diagnostics.empty(), "phase selector fixture should resolve");
  std::vector<int> codes;
  for (const ResolvedStep& step : resolved.steps)
    codes.push_back(step.opcode);

  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), "executable phase selector fixture should fit memory");
  calc.set_register("8", "1");
  calc.set_register("b", "ГE-2");
  calc.set_register("e", "0");
  calc.set_register("0", "0");

  std::vector<PhaseObservation> observations;
  for (int phase = 0; phase < 2; ++phase) {
    calc.press_sequence({"БП", "5", "5", "С/П"});
    const emulator::RunResult stable = calc.run_until_stable(3000, 8);
    observations.push_back(PhaseObservation{
        .stopped = stable.stopped,
        .factor = compact(calc.read_register("8")),
        .seed = compact(calc.read_register("b")),
        .selected_marker = compact(calc.read_register("0")),
    });
  }
  return observations;
}

void require_rejected(const std::vector<MachineItem>& items, const core::RawPhaseSelectorProof& raw,
                      const core::PackedDeltaFactorProof& factor,
                      const core::PhaseSelectorRelocationProof& relocation,
                      std::string_view context) {
  const core::PhaseSelectorMaterializerResult result =
      core::rewrite_phase_selector_materializer(items, raw, factor, relocation);
  require(result.applied == 0 && !result.verification.proved && same_items(result.items, items),
          "failed phase proof should preserve input: " + std::string(context));
}

} // namespace

void phase_selector_materializer_requires_proved_raw_phase_and_factor() {
  using core::PackedDeltaFactorMaterialization;

  // Expected composition: a one-cell predecessor deletion moves the selected
  // entry from 88 to 87 and shrinks 120 to 119.  The seven-cell K-sign
  // materializer grows the six-cell prefix by one, restoring size 120 and
  // placing the same entry back at the oracle-proved address 88.
  const std::vector<MachineItem> expected = materializer_fixture(119, 87);
  const core::RawPhaseSelectorProof raw = valid_raw_proof();
  const core::PackedDeltaFactorProof derived =
      factor_proof(PackedDeltaFactorMaterialization::DeriveWithKSign);
  const core::PhaseSelectorRelocationProof expected_relocation =
      relocation_proof(expected, derived.materialization);
  const core::PhaseSelectorMaterializerVerification expected_verification =
      core::verify_phase_selector_materializer(expected, raw, derived, expected_relocation);
  require(expected_verification.proved && expected_verification.reasons.empty() &&
              expected_verification.input_cells == 119 &&
              expected_verification.output_cells == 120 && expected_verification.cell_delta == 1 &&
              expected_verification.materializer_address == 55 &&
              expected_verification.input_target_address == 87 &&
              expected_verification.output_target_address == 88 &&
              expected_verification.required_new_stable_registers == 1 &&
              expected_verification.seed_register == "b" &&
              expected_verification.selector_register == "e" &&
              expected_verification.factor_register == "8",
          "K-sign materializer should have the exact 119->120, 87->88 composition delta");
  const core::PhaseSelectorMaterializerResult expected_rewritten =
      core::rewrite_phase_selector_materializer(expected, raw, derived, expected_relocation);
  require(expected_rewritten.applied == 1 && expected_rewritten.verification.proved &&
              expected_rewritten.verification.final_artifact_proved &&
              cell_count(expected_rewritten.items) == 120,
          "proved K-sign phase materializer should rewrite the expected artifact");

  // The packed factor is direct-only state, so R0 is valid even though it is
  // not a stable indirect selector register.
  std::vector<MachineItem> r0_factor_items = expected;
  r0_factor_items.at(expected_relocation.factor_recall_item_index).opcode = 0x60;
  r0_factor_items.at(expected_relocation.factor_store_item_index).opcode = 0x40;
  core::PackedDeltaFactorProof r0_factor = derived;
  r0_factor.factor_register = "0";
  const auto r0_rewritten = core::rewrite_phase_selector_materializer(
      r0_factor_items, raw, r0_factor, expected_relocation);
  require(r0_rewritten.applied == 1 && r0_rewritten.verification.factor_register == "0",
          "direct-only packed factor should be allowed in R0");

  const std::size_t begin =
      next_cell(expected_rewritten.items, label_item(expected_rewritten.items, "phase_entry"));
  std::vector<int> derived_opcodes;
  std::size_t cursor = begin;
  for (int index = 0; index < 7; ++index) {
    derived_opcodes.push_back(expected_rewritten.items.at(cursor).opcode);
    if (index != 6)
      cursor = next_cell(expected_rewritten.items, cursor);
  }
  require(derived_opcodes == std::vector<int>({0x6b, 0x0b, 0x4b, 0x1e, 0x4e, 0x32, 0x48}),
          "K-sign materializer should emit seed toggle, Ftg selector, and +/-1 factor store");

  // The independent-factor strategy costs two cells.  On the same 119-cell,
  // target-87 input it would place the entry at 89, not the Ftg oracle target
  // 88, and must therefore fail closed.
  const core::PackedDeltaFactorProof independent =
      factor_proof(PackedDeltaFactorMaterialization::PreserveIndependentToggle);
  const core::PhaseSelectorRelocationProof wrong_independent_relocation =
      relocation_proof(expected, independent.materialization);
  const auto wrong_independent = core::rewrite_phase_selector_materializer(
      expected, raw, independent, wrong_independent_relocation);
  require(wrong_independent.applied == 0 &&
              contains_reason(wrong_independent.verification, "oracle address"),
          "eight-cell independent factor path should reject 87->89 against oracle target 88");

  // With two predecessor cells already removed, the independent-factor path
  // is valid: 118->120 and selected entry 86->88.
  const std::vector<MachineItem> two_cell_gap = materializer_fixture(118, 86);
  const core::PhaseSelectorRelocationProof independent_relocation =
      relocation_proof(two_cell_gap, independent.materialization);
  const core::PhaseSelectorMaterializerResult independent_rewritten =
      core::rewrite_phase_selector_materializer(two_cell_gap, raw, independent,
                                                independent_relocation);
  require(independent_rewritten.applied == 1 &&
              independent_rewritten.verification.cell_delta == 2 &&
              independent_rewritten.verification.output_cells == 120 &&
              independent_rewritten.verification.output_target_address == 88,
          "independent factor toggle should require an exact two-cell relocation gap");

  // Emulator equivalence on official-size mirrors: both strategies preserve
  // the selected semantic entry and the alternating -1,+1 packed factor.
  const std::vector<MachineItem> executable_derived = materializer_fixture(90, 87);
  const auto executable_derived_relocation =
      relocation_proof(executable_derived, derived.materialization);
  const auto executable_derived_rewritten = core::rewrite_phase_selector_materializer(
      executable_derived, raw, derived, executable_derived_relocation);
  require(executable_derived_rewritten.applied == 1,
          "executable K-sign phase mirror should rewrite");
  const std::vector<PhaseObservation> baseline_phases = run_two_phases(executable_derived);
  const std::vector<PhaseObservation> derived_phases =
      run_two_phases(executable_derived_rewritten.items);
  require(baseline_phases.size() == 2U && derived_phases.size() == 2U &&
              baseline_phases.at(0).stopped && baseline_phases.at(1).stopped &&
              derived_phases.at(0).stopped && derived_phases.at(1).stopped &&
              baseline_phases.at(0).factor == "-1," && baseline_phases.at(1).factor == "1," &&
              derived_phases.at(0).factor == baseline_phases.at(0).factor &&
              derived_phases.at(1).factor == baseline_phases.at(1).factor &&
              derived_phases.at(0).seed == "-Г,-2" && derived_phases.at(1).seed == "Г,-2" &&
              derived_phases.at(0).selected_marker == "4," &&
              derived_phases.at(1).selected_marker == "4,",
          "K-sign materializer should alternate raw phase and select the relocated entry twice");

  const std::vector<MachineItem> executable_independent = materializer_fixture(89, 86);
  const auto executable_independent_relocation =
      relocation_proof(executable_independent, independent.materialization);
  const auto executable_independent_rewritten = core::rewrite_phase_selector_materializer(
      executable_independent, raw, independent, executable_independent_relocation);
  require(executable_independent_rewritten.applied == 1,
          "executable independent-factor phase mirror should rewrite");
  const std::vector<PhaseObservation> independent_phases =
      run_two_phases(executable_independent_rewritten.items);
  require(independent_phases.at(0).factor == "-1," && independent_phases.at(1).factor == "1," &&
              independent_phases.at(0).selected_marker == "4," &&
              independent_phases.at(1).selected_marker == "4,",
          "independent factor toggle should preserve factor phase and raw selector target");

  {
    core::RawPhaseSelectorProof proof = raw;
    proof.seed_register_is_free_before_materialization = false;
    require_rejected(expected, proof, derived, expected_relocation, "seed register not free");
  }
  {
    core::RawPhaseSelectorProof proof = raw;
    proof.seed_register = "8";
    const auto rejected =
        core::rewrite_phase_selector_materializer(expected, proof, derived, expected_relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "distinct"),
            "aliased seed/factor registers should fail closed");
  }
  {
    std::vector<MachineItem> used_seed = expected;
    used_seed.front() = MachineItem::op(0x6b, "П->X b");
    const auto rejected =
        core::rewrite_phase_selector_materializer(used_seed, raw, derived, expected_relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "machine-code use"),
            "a supposedly free seed register with an existing use should fail closed");
  }
  {
    std::vector<MachineItem> preloaded_seed = expected;
    preloaded_seed.front() = MachineItem::op(0x4b, "X->П b");
    core::RawPhaseSelectorProof proof = raw;
    proof.seed_preload_store_item_indices = {0};
    const auto accepted = core::rewrite_phase_selector_materializer(preloaded_seed, proof, derived,
                                                                    expected_relocation);
    require(accepted.applied == 1,
            "an explicitly proved X->P seed preload should not make the seed register busy");
  }
  {
    core::RawPhaseSelectorProof proof = raw;
    proof.angle_mode_is_invariant_at_phase_entries = false;
    const auto rejected =
        core::rewrite_phase_selector_materializer(expected, proof, derived, expected_relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "angle mode"),
            "a mutable runtime angle mode should fail closed");
  }
  {
    core::RawPhaseSelectorProof proof = raw;
    proof.angle_mode = core::mk61_trig::AngleMode::Rad;
    const auto rejected =
        core::rewrite_phase_selector_materializer(expected, proof, derived, expected_relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "unary oracle"),
            "unsupported angle mode should fail closed");
  }
  {
    core::RawPhaseSelectorProof proof = raw;
    proof.facts.front().raw_unary_result = "5.235988E-4";
    const auto rejected =
        core::rewrite_phase_selector_materializer(expected, proof, derived, expected_relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "disagrees"),
            "wrong raw unary sign/result should fail closed");
  }
  {
    core::RawPhaseSelectorProof proof = raw;
    proof.phase_is_strict_two_cycle = false;
    require_rejected(expected, proof, derived, expected_relocation, "unproved phase cycle");
  }
  {
    core::PackedDeltaFactorProof proof = derived;
    proof.ksign_of_raw_unary_result_is_required_factor = false;
    const auto rejected =
        core::rewrite_phase_selector_materializer(expected, raw, proof, expected_relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "K sign"),
            "K-sign materialization without an arithmetic proof should fail closed");
  }
  {
    core::PackedDeltaFactorProof proof = derived;
    proof.required_factor_domain_is_exact_unit_signs = false;
    require_rejected(expected, raw, proof, expected_relocation, "non-unit factor domain");
  }
  {
    std::vector<MachineItem> indirect_factor = r0_factor_items;
    indirect_factor.front() = MachineItem::op(0x80, "K BP 0");
    const auto rejected = core::rewrite_phase_selector_materializer(indirect_factor, raw, r0_factor,
                                                                    expected_relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "indirect or alias"),
            "an indirect use of a direct-only R0 factor should fail closed");
  }
  {
    std::vector<MachineItem> malformed = expected;
    malformed.at(expected_relocation.selector_low_digit_item_index).opcode = 6;
    const auto rejected =
        core::rewrite_phase_selector_materializer(malformed, raw, derived, expected_relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "does not name"),
            "decimal selector charge that misses the entry should fail closed");
  }
  {
    core::PhaseSelectorRelocationProof stale = expected_relocation;
    ++stale.expected_output_target_address;
    require_rejected(expected, raw, derived, stale, "stale output target proof");
  }
  {
    core::PhaseSelectorRelocationProof unsafe_stack = expected_relocation;
    unsafe_stack.replacement_stack_effect_is_proved_compatible = false;
    const auto rejected =
        core::rewrite_phase_selector_materializer(expected, raw, derived, unsafe_stack);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "stack and X2"),
            "an unproved stack/X2 effect should fail closed");
  }
  {
    std::vector<MachineItem> removed_target = expected;
    removed_target.at(expected_relocation.selector_high_digit_item_index).opcode = 5;
    removed_target.at(expected_relocation.selector_low_digit_item_index).opcode = 5;
    core::RawPhaseSelectorProof ge1 = raw;
    ge1.initial_seed = "ГE-1";
    ge1.facts = {{.input_seed = "ГE-1",
                  .toggled_seed = "-ГE-1",
                  .raw_unary_result = "-5.2360355E-3",
                  .actual_flow_target = 55,
                  .required_factor = -1},
                 {.input_seed = "-ГE-1",
                  .toggled_seed = "ГE-1",
                  .raw_unary_result = "5.2360355E-3",
                  .actual_flow_target = 55,
                  .required_factor = 1}};
    core::PhaseSelectorRelocationProof removed = expected_relocation;
    removed.selector_target_item_index = removed.factor_recall_item_index;
    removed.expected_input_target_address = 55;
    removed.expected_output_target_address = 55;
    const auto rejected =
        core::rewrite_phase_selector_materializer(removed_target, ge1, derived, removed);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "would be removed"),
            "a selector target inside the replaced input block should fail closed");
  }
  {
    std::vector<MachineItem> fixed = expected;
    fixed.push_back(MachineItem::op(0x51, "БП"));
    fixed.push_back(MachineItem::address(87));
    fixed.push_back(MachineItem::op(0x50, "С/П"));
    const core::PhaseSelectorRelocationProof fixed_relocation =
        relocation_proof(fixed, derived.materialization);
    const auto rejected =
        core::rewrite_phase_selector_materializer(fixed, raw, derived, fixed_relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "fixed address"),
            "a fixed downstream address should fail closed across materializer growth");
  }
}

} // namespace mkpro::tests

#ifdef MKPRO_PHASE_SELECTOR_MATERIALIZER_STANDALONE_TEST
int main() {
  try {
    mkpro::tests::phase_selector_materializer_requires_proved_raw_phase_and_factor();
    std::cout << "phase_selector_materializer_test: ok\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "phase_selector_materializer_test: " << error.what() << '\n';
    return 1;
  }
}
#endif
