#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

// One emulator- or ROM-proved raw selector value.  The raw spelling is opaque
// to this post-layout pass: it must not be normalized through host floating
// point before the proof is constructed.
struct RawZeroReturnSelectorFact {
  std::string raw_value;
  int actual_flow_target = -1;
  bool conditional_preserves_selector = false;
  bool conditional_preserves_x_y_z_t = false;
  bool conditional_preserves_x2 = false;
};

// Caller-supplied proof that every runtime value reaching the new indirect
// conditional is covered by an oracle fact and selects physical address 00.
struct RawZeroReturnSelectorProof {
  std::string selector_register;
  std::vector<RawZeroReturnSelectorFact> facts;
  bool runtime_value_set_is_exhaustive = false;
  bool selector_is_unwritten_until_use = false;
};

// Caller-supplied interprocedural/liveness proof.  These obligations cannot be
// reconstructed from a local MachineItem window: the physical stop is reached
// only after returning through one or more caller continuations.
struct TerminalContinuationLivenessProof {
  std::string terminal_slot_register;
  bool fractional_predicate_is_terminal_payload = false;
  bool previous_slot_value_is_dead_on_report_path = false;
  bool stored_payload_is_live_until_terminal_stop = false;
  bool every_report_continuation_reaches_terminal_stop = false;
  bool no_prior_observable_event_error_or_divergence = false;
  bool continuation_stack_and_x2_effects_are_unobservable = false;
};

// Exact post-layout certificate.  The continuation body must already have
// been relocated immediately after the provisional dot/STOP tail.  This pass
// deliberately does not guess a movable block or repair unknown selector
// charges; it only consumes an explicit relocation proof and rechecks the
// locally observable layout facts.
struct TerminalReportTailRelocationProof {
  int expected_input_cells = -1;
  int expected_direct_condition_address = -1;
  int expected_continuation_entry_address = -1;

  std::size_t mask_item_index = 0;
  std::size_t fraction_item_index = 0;
  std::size_t direct_condition_item_index = 0;
  std::size_t direct_address_item_index = 0;
  std::size_t dot_item_index = 0;
  std::size_t stop_item_index = 0;
  std::size_t continuation_entry_item_index = 0;
  std::size_t direct_return_item_index = 0;
  std::size_t zero_return_item_index = 0;

  bool continuation_is_relocated_immediately_after_tail = false;
  bool all_direct_references_are_relocated_or_symbolic = false;
  bool all_indirect_targets_and_selector_charges_are_rebound = false;
  bool removed_cells_have_no_external_entries = false;
  bool zero_and_direct_returns_have_equivalent_stack_contracts = false;
};

struct TerminalReportTailOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
};

struct TerminalReportTailVerification {
  bool proved = false;
  bool final_artifact_proved = false;
  int input_cells = 0;
  int output_cells = 0;
  int direct_condition_address = -1;
  int continuation_entry_address = -1;
  int zero_return_address = -1;
  std::string selector_register;
  std::string terminal_slot_register;
  std::vector<std::string> raw_selector_values;
  std::vector<std::string> reasons;
};

struct TerminalReportTailResult {
  std::vector<MachineItem> items;
  TerminalReportTailVerification verification;
  int applied = 0;
  int removed_cells = 0;
};

// Verify the exact four-cell direct tail
//
//   F x!=0 <return>; .; STOP
//
// after KAND; Kfrac, plus the caller-supplied raw-selector, continuation, and
// relocation obligations.  A local mismatch or any missing proof bit fails
// closed.
bool raw_selector_targets_physical_zero(
    const std::string& selector_register, const std::string& raw_value,
    AddressSpaceModel model = AddressSpaceModel::Standard);

std::optional<int> raw_selector_physical_target(
    const std::string& selector_register, const std::string& raw_value,
    AddressSpaceModel model = AddressSpaceModel::Standard);

TerminalReportTailVerification
verify_terminal_report_tail(const std::vector<MachineItem>& items,
                            const RawZeroReturnSelectorProof& raw_selector,
                            const TerminalContinuationLivenessProof& continuation,
                            const TerminalReportTailRelocationProof& relocation,
                            const TerminalReportTailOptions& options = {});

// Rewrite the proved tail to
//
//   K x!=0 Rs; X->P Rt
//
// where the zero branch returns through physical 00 and the nonzero branch
// stores the terminal fractional payload before falling into the already
// relocated continuation body.  The final artifact is independently checked;
// failure returns the original items unchanged.
TerminalReportTailResult
rewrite_terminal_report_tail(const std::vector<MachineItem>& items,
                             const RawZeroReturnSelectorProof& raw_selector,
                             const TerminalContinuationLivenessProof& continuation,
                             const TerminalReportTailRelocationProof& relocation,
                             const TerminalReportTailOptions& options = {});

} // namespace mkpro::core
