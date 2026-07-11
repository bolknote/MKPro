#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/mk61_trig.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mkpro::core {

enum class PackedDeltaFactorMaterialization {
  // Keep the existing recall/sign/store factor toggle and replace only the
  // three-cell decimal selector charge.  This costs two additional cells.
  PreserveIndependentToggle,

  // Derive the required +/-1 factor from the signed raw unary result with
  // K sign, then store it in the existing factor register.  This replaces the
  // whole six-cell prefix with seven cells and costs one additional cell.
  DeriveWithKSign,
};

struct RawPhaseSelectorFact {
  std::string input_seed;
  std::string toggled_seed;
  std::string raw_unary_result;
  int actual_flow_target = -1;
  int required_factor = 0;
};

// Trusted raw-unary facts plus the global phase proof.  The verifier rechecks
// every fact through evaluate_raw_bcd_unary_indirect_selector; raw spellings
// are never normalized through host floating point.
struct RawPhaseSelectorProof {
  std::string seed_register;
  std::string selector_register;
  std::string initial_seed;
  mk61_trig::AngleMode angle_mode = mk61_trig::AngleMode::Deg;
  mk61_trig::Function unary_operation = mk61_trig::Function::Tg;
  std::vector<RawPhaseSelectorFact> facts;
  bool runtime_seed_set_is_exhaustive = false;
  bool phase_is_strict_two_cycle = false;
  bool angle_mode_is_invariant_at_phase_entries = false;
  bool seed_preload_is_delivered = false;
  bool seed_register_is_free_before_materialization = false;
  bool seed_and_selector_are_preserved_between_phase_entries = false;

  // The seed may have explicit program-side preload stores even though the
  // register is otherwise free.  Every machine-code use of the seed register
  // must be one of these exact X->P seed items; an empty list describes an
  // externally delivered preload.  seed_preload_is_delivered proves that the
  // listed stores (or the external preload) write initial_seed on every entry
  // path that reaches the materializer.
  std::vector<std::size_t> seed_preload_store_item_indices;
};

struct PackedDeltaFactorProof {
  PackedDeltaFactorMaterialization materialization =
      PackedDeltaFactorMaterialization::DeriveWithKSign;
  // Unlike the raw seed and indirect selector, the packed factor is accessed
  // directly and may therefore live in any ordinary R0..Re register.  The
  // verifier rejects every indirect machine-code use of this register.
  std::string factor_register;
  bool required_factor_domain_is_exact_unit_signs = false;
  bool factor_phase_matches_raw_seed_phase = false;
  bool every_update_consumer_reads_factor_register = false;
  bool factor_is_unwritten_until_update_consumers = false;
  bool ksign_of_raw_unary_result_is_required_factor = false;
};

// Exact six-cell input block and its already-proved post-layout relocation
// environment:
//
//   P->X factor; /-/; X->P factor; digit-hi; digit-lo; X->P selector
//
// The decimal digits must select selector_target_item_index in the input
// artifact.  The final verifier independently checks that growth moves this
// same item to expected_output_target_address, which must also be the raw
// unary oracle target.
struct PhaseSelectorRelocationProof {
  int expected_input_cells = -1;
  int expected_output_cells = -1;
  int expected_materializer_address = -1;
  int expected_input_target_address = -1;
  int expected_output_target_address = -1;

  std::size_t factor_recall_item_index = 0;
  std::size_t factor_sign_item_index = 0;
  std::size_t factor_store_item_index = 0;
  std::size_t selector_high_digit_item_index = 0;
  std::size_t selector_low_digit_item_index = 0;
  std::size_t selector_store_item_index = 0;
  std::size_t continuation_entry_item_index = 0;
  std::size_t selector_target_item_index = 0;

  bool all_direct_references_are_relocated_or_symbolic = false;
  bool all_other_indirect_targets_and_charges_are_rebound = false;
  bool materializer_cells_have_no_external_entries = false;
  bool continuation_fallthrough_is_proved = false;
  bool selector_consumers_target_proved_entry = false;
  bool replacement_stack_effect_is_proved_compatible = false;
};

struct PhaseSelectorMaterializerOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
};

struct PhaseSelectorMaterializerVerification {
  bool proved = false;
  bool final_artifact_proved = false;
  int input_cells = 0;
  int output_cells = 0;
  int cell_delta = 0;
  int required_new_stable_registers = 1;
  int materializer_address = -1;
  int input_target_address = -1;
  int output_target_address = -1;
  std::string seed_register;
  std::string selector_register;
  std::string factor_register;
  std::string seed_preload_value;
  std::vector<std::string> reasons;
};

struct PhaseSelectorMaterializerResult {
  std::vector<MachineItem> items;
  PhaseSelectorMaterializerVerification verification;
  int applied = 0;
};

PhaseSelectorMaterializerVerification verify_phase_selector_materializer(
    const std::vector<MachineItem>& items, const RawPhaseSelectorProof& raw_selector,
    const PackedDeltaFactorProof& factor, const PhaseSelectorRelocationProof& relocation,
    const PhaseSelectorMaterializerOptions& options = {});

PhaseSelectorMaterializerResult rewrite_phase_selector_materializer(
    const std::vector<MachineItem>& items, const RawPhaseSelectorProof& raw_selector,
    const PackedDeltaFactorProof& factor, const PhaseSelectorRelocationProof& relocation,
    const PhaseSelectorMaterializerOptions& options = {});

} // namespace mkpro::core
