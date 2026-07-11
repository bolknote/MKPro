#include "mkpro/core/terminal_report_tail.hpp"

#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <functional>
#ifdef MKPRO_TERMINAL_REPORT_TAIL_STANDALONE_TEST
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

std::size_t item_at_address(const std::vector<MachineItem>& items, int wanted) {
  int address = 0;
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (items.at(index).kind == MachineItemKind::Label)
      continue;
    if (address == wanted)
      return index;
    ++address;
  }
  throw std::runtime_error("missing fixture address " + std::to_string(wanted));
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

std::vector<MachineItem> report_tail_fixture() {
  std::vector<MachineItem> items = {
      MachineItem::op(0x52, "В/О"),
      MachineItem::op(0x53, "ПП"),
      MachineItem::address(std::string("update_tail")),
      MachineItem::op(0x63, "П->X 3"),
      MachineItem::op(0x50, "С/П"),
      MachineItem::label("update_tail"),
      MachineItem::op(0x37, "К AND"),
      MachineItem::op(0x35, "К frac"),
      MachineItem::op(0x57, "F x!=0"),
      MachineItem::address(std::string("continuation_return")),
      MachineItem::label("provisional_terminal"),
      MachineItem::op(0x0a, "."),
      MachineItem::op(0x50, "С/П"),
      MachineItem::label("relocated_continuation"),
      MachineItem::op(0x15, "F 10^x"),
      MachineItem::op(0x13, "/"),
      MachineItem::op(0x35, "К frac"),
      MachineItem::op(0x6d, "П->X d"),
      MachineItem::op(0x11, "-"),
      MachineItem::op(0x22, "F x^2"),
      MachineItem::op(0x10, "+"),
      MachineItem::label("continuation_return"),
      MachineItem::op(0x52, "В/О"),
  };
  return items;
}

core::RawZeroReturnSelectorProof valid_raw_proof() {
  return core::RawZeroReturnSelectorProof{
      .selector_register = "a",
      .facts = {{.raw_value = "ГE-2",
                 .actual_flow_target = 0,
                 .conditional_preserves_selector = true,
                 .conditional_preserves_x_y_z_t = true,
                 .conditional_preserves_x2 = true},
                {.raw_value = "-ГE-2",
                 .actual_flow_target = 0,
                 .conditional_preserves_selector = true,
                 .conditional_preserves_x_y_z_t = true,
                 .conditional_preserves_x2 = true}},
      .runtime_value_set_is_exhaustive = true,
      .selector_is_unwritten_until_use = true,
  };
}

core::TerminalContinuationLivenessProof valid_liveness_proof() {
  return core::TerminalContinuationLivenessProof{
      .terminal_slot_register = "3",
      .fractional_predicate_is_terminal_payload = true,
      .previous_slot_value_is_dead_on_report_path = true,
      .stored_payload_is_live_until_terminal_stop = true,
      .every_report_continuation_reaches_terminal_stop = true,
      .no_prior_observable_event_error_or_divergence = true,
      .continuation_stack_and_x2_effects_are_unobservable = true,
  };
}

core::TerminalReportTailRelocationProof
valid_relocation_proof(const std::vector<MachineItem>& items) {
  const std::size_t mask = next_cell(items, label_item(items, "update_tail"));
  const std::size_t fraction = next_cell(items, mask);
  const std::size_t condition = next_cell(items, fraction);
  const std::size_t address = next_cell(items, condition);
  const std::size_t dot = next_cell(items, address);
  const std::size_t stop = next_cell(items, dot);
  const std::size_t continuation = next_cell(items, stop);
  const std::size_t direct_return = next_cell(items, label_item(items, "continuation_return"));
  const std::size_t zero_return = item_at_address(items, 0);
  return core::TerminalReportTailRelocationProof{
      .expected_input_cells = cell_count(items),
      .expected_direct_condition_address = item_address(items, condition),
      .expected_continuation_entry_address = item_address(items, continuation),
      .mask_item_index = mask,
      .fraction_item_index = fraction,
      .direct_condition_item_index = condition,
      .direct_address_item_index = address,
      .dot_item_index = dot,
      .stop_item_index = stop,
      .continuation_entry_item_index = continuation,
      .direct_return_item_index = direct_return,
      .zero_return_item_index = zero_return,
      .continuation_is_relocated_immediately_after_tail = true,
      .all_direct_references_are_relocated_or_symbolic = true,
      .all_indirect_targets_and_selector_charges_are_rebound = true,
      .removed_cells_have_no_external_entries = true,
      .zero_and_direct_returns_have_equivalent_stack_contracts = true,
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

bool contains_reason(const core::TerminalReportTailVerification& verification,
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

struct EmulatorOutcome {
  bool stopped = false;
  std::string pc;
  std::string x;
  std::string x2;
  std::string r3;
};

EmulatorOutcome run(const std::vector<MachineItem>& items, const std::string& line) {
  const ResolvedProgram resolved = resolve_machine_items(items, {});
  require(resolved.diagnostics.empty(), "terminal report-tail fixture should resolve");
  std::vector<int> codes;
  for (const ResolvedStep& step : resolved.steps)
    codes.push_back(step.opcode);

  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  calc.load_program(codes);
  calc.set_register("a", "ГE-2");
  calc.set_register("d", "4.1200076E-1");
  calc.set_register("3", "777");
  calc.set_register("x", "88888834");
  calc.set_register("x1", "123");
  calc.set_register("y", line);
  calc.set_register("z", "333");
  calc.set_register("t", "444");
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult stable = calc.run_until_stable(4000, 8);
  return EmulatorOutcome{
      .stopped = stable.stopped,
      .pc = calc.program_counter(),
      .x = compact(calc.read_register("x")),
      .x2 = compact(calc.read_register("x1")),
      .r3 = compact(calc.read_register("3")),
  };
}

void require_rejected(const std::vector<MachineItem>& items,
                      const core::RawZeroReturnSelectorProof& raw,
                      const core::TerminalContinuationLivenessProof& liveness,
                      const core::TerminalReportTailRelocationProof& relocation,
                      std::string_view reason) {
  const core::TerminalReportTailResult result =
      core::rewrite_terminal_report_tail(items, raw, liveness, relocation);
  require(result.applied == 0 && result.removed_cells == 0 && !result.verification.proved &&
              same_items(result.items, items),
          "failed proof should preserve the input artifact: " + std::string(reason));
}

} // namespace

void terminal_report_tail_rewrites_only_with_explicit_proofs() {
  const std::vector<MachineItem> baseline = report_tail_fixture();
  const core::RawZeroReturnSelectorProof raw = valid_raw_proof();
  const core::TerminalContinuationLivenessProof liveness = valid_liveness_proof();
  const core::TerminalReportTailRelocationProof relocation = valid_relocation_proof(baseline);

  const core::TerminalReportTailVerification verification =
      core::verify_terminal_report_tail(baseline, raw, liveness, relocation);
  require(verification.proved && verification.reasons.empty() && verification.input_cells == 19 &&
              verification.output_cells == 17 && verification.direct_condition_address == 7 &&
              verification.continuation_entry_address == 11 &&
              verification.zero_return_address == 0 && verification.selector_register == "a" &&
              verification.terminal_slot_register == "3" &&
              verification.raw_selector_values.size() == 2U,
          "complete raw/liveness/relocation evidence should prove the generic tail");

  const core::TerminalReportTailResult rewritten =
      core::rewrite_terminal_report_tail(baseline, raw, liveness, relocation);
  require(rewritten.applied == 1 && rewritten.removed_cells == 2 && rewritten.verification.proved &&
              rewritten.verification.final_artifact_proved &&
              cell_count(rewritten.items) == cell_count(baseline) - 2,
          "proved direct report tail should remove exactly two cells");
  const std::size_t rewritten_mask =
      next_cell(rewritten.items, label_item(rewritten.items, "update_tail"));
  const std::size_t rewritten_fraction = next_cell(rewritten.items, rewritten_mask);
  const std::size_t rewritten_condition = next_cell(rewritten.items, rewritten_fraction);
  const std::size_t rewritten_store = next_cell(rewritten.items, rewritten_condition);
  const std::size_t rewritten_continuation = next_cell(rewritten.items, rewritten_store);
  require(
      rewritten.items.at(rewritten_condition).opcode == 0x7a &&
          rewritten.items.at(rewritten_store).opcode == 0x43 &&
          rewritten_continuation ==
              next_cell(rewritten.items, label_item(rewritten.items, "relocated_continuation")) &&
          rewritten.items.at(rewritten_condition).roles ==
              std::vector<std::string>{"terminal-report-zero-return:a:0"} &&
          rewritten.items.at(rewritten_store).roles ==
              std::vector<std::string>{"terminal-report-continuation-store:3"},
      "rewrite should emit one-cell indirect zero return and one-cell payload store");

  const EmulatorOutcome baseline_zero = run(baseline, "44444.4");
  const EmulatorOutcome rewritten_zero = run(rewritten.items, "44444.4");
  require(baseline_zero.stopped && rewritten_zero.stopped && baseline_zero.x == "777," &&
              rewritten_zero.x == baseline_zero.x && rewritten_zero.r3 == "777,",
          "zero report should bypass the store and return through physical 00");
  const EmulatorOutcome baseline_report = run(baseline, "88888.4");
  const EmulatorOutcome rewritten_report = run(rewritten.items, "88888.4");
  require(baseline_report.stopped && rewritten_report.stopped && baseline_report.x == "8,888-1" &&
              rewritten_report.x == baseline_report.x && baseline_report.x2 == "8,8888" &&
              rewritten_report.r3 == "8,888-1",
          "nonzero report should store the fractional payload and reach the existing caller stop");

  {
    core::RawZeroReturnSelectorProof proof = raw;
    proof.runtime_value_set_is_exhaustive = false;
    require_rejected(baseline, proof, liveness, relocation, "non-exhaustive raw facts");
  }
  {
    core::RawZeroReturnSelectorProof proof = raw;
    proof.facts.front().actual_flow_target = 88;
    const auto rejected = core::rewrite_terminal_report_tail(baseline, proof, liveness, relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "physical 00"),
            "a raw selector targeting a different address should fail closed");
  }
  {
    core::RawZeroReturnSelectorProof proof = raw;
    proof.facts.front().raw_value = "88";
    proof.facts.front().actual_flow_target = 0;
    const auto rejected = core::rewrite_terminal_report_tail(baseline, proof, liveness, relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "physical 00"),
            "a forged raw-value/target certificate should fail the local indirect oracle");
  }
  {
    core::RawZeroReturnSelectorProof proof = raw;
    proof.facts.back().conditional_preserves_selector = false;
    require_rejected(baseline, proof, liveness, relocation, "selector mutation");
  }
  {
    core::RawZeroReturnSelectorProof proof = raw;
    proof.facts.front().conditional_preserves_x2 = false;
    const auto rejected = core::rewrite_terminal_report_tail(baseline, proof, liveness, relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "X2 preservation"),
            "a raw conditional without an X2 proof should fail closed");
  }
  {
    core::RawZeroReturnSelectorProof proof = raw;
    proof.selector_register = "3";
    const auto rejected = core::rewrite_terminal_report_tail(baseline, proof, liveness, relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "stable R7..Re"),
            "a mutating selector register should fail closed");
  }
  {
    core::RawZeroReturnSelectorProof proof = raw;
    proof.selector_is_unwritten_until_use = false;
    require_rejected(baseline, proof, liveness, relocation, "selector write before use");
  }
  {
    core::TerminalContinuationLivenessProof proof = liveness;
    proof.terminal_slot_register = "f";
    const auto rejected = core::rewrite_terminal_report_tail(baseline, raw, proof, relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "invalid"),
            "an unavailable terminal-slot register should fail closed");
  }
  {
    const core::TerminalReportTailOptions expanded{.address_space_model =
                                                       AddressSpaceModel::Mk61SMiniExpanded};
    const auto rejected =
        core::rewrite_terminal_report_tail(baseline, raw, liveness, relocation, expanded);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "standard MK-61"),
            "an unproved address-space profile should fail closed");
  }

  const std::vector<std::function<void(core::TerminalContinuationLivenessProof&)>>
      invalidate_liveness = {
          [](auto& proof) { proof.fractional_predicate_is_terminal_payload = false; },
          [](auto& proof) { proof.previous_slot_value_is_dead_on_report_path = false; },
          [](auto& proof) { proof.stored_payload_is_live_until_terminal_stop = false; },
          [](auto& proof) { proof.every_report_continuation_reaches_terminal_stop = false; },
          [](auto& proof) { proof.no_prior_observable_event_error_or_divergence = false; },
          [](auto& proof) { proof.continuation_stack_and_x2_effects_are_unobservable = false; },
      };
  for (std::size_t index = 0; index < invalidate_liveness.size(); ++index) {
    core::TerminalContinuationLivenessProof proof = liveness;
    invalidate_liveness.at(index)(proof);
    require_rejected(baseline, raw, proof, relocation,
                     "missing liveness obligation " + std::to_string(index));
  }

  const std::vector<std::function<void(core::TerminalReportTailRelocationProof&)>>
      invalidate_relocation = {
          [](auto& proof) { proof.continuation_is_relocated_immediately_after_tail = false; },
          [](auto& proof) { proof.all_direct_references_are_relocated_or_symbolic = false; },
          [](auto& proof) { proof.all_indirect_targets_and_selector_charges_are_rebound = false; },
          [](auto& proof) { proof.removed_cells_have_no_external_entries = false; },
          [](auto& proof) {
            proof.zero_and_direct_returns_have_equivalent_stack_contracts = false;
          },
      };
  for (std::size_t index = 0; index < invalidate_relocation.size(); ++index) {
    core::TerminalReportTailRelocationProof proof = relocation;
    invalidate_relocation.at(index)(proof);
    require_rejected(baseline, raw, liveness, proof,
                     "missing relocation obligation " + std::to_string(index));
  }

  {
    std::vector<MachineItem> malformed = baseline;
    malformed.at(relocation.direct_condition_item_index).opcode = 0x5e;
    require_rejected(malformed, raw, liveness, relocation, "wrong direct condition");
  }
  {
    std::vector<MachineItem> malformed = baseline;
    malformed.at(relocation.direct_address_item_index).target = std::string("update_tail");
    const auto rejected = core::rewrite_terminal_report_tail(malformed, raw, liveness, relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "does not target"),
            "a mismatched direct return target should fail closed");
  }
  {
    std::vector<MachineItem> malformed = baseline;
    malformed.at(relocation.zero_return_item_index).opcode = 0x50;
    require_rejected(malformed, raw, liveness, relocation, "no return at zero");
  }
  {
    core::TerminalReportTailRelocationProof stale = relocation;
    ++stale.expected_input_cells;
    require_rejected(baseline, raw, liveness, stale, "stale relocation certificate");
  }
  {
    std::vector<MachineItem> fixed = baseline;
    fixed.push_back(MachineItem::op(0x51, "БП"));
    fixed.push_back(MachineItem::address(18));
    fixed.push_back(MachineItem::op(0x50, "С/П"));
    const core::TerminalReportTailRelocationProof fixed_relocation = valid_relocation_proof(fixed);
    const auto rejected =
        core::rewrite_terminal_report_tail(fixed, raw, liveness, fixed_relocation);
    require(rejected.applied == 0 && contains_reason(rejected.verification, "fixed address"),
            "a remaining fixed target after the removed cells should fail closed");
  }
}

} // namespace mkpro::tests

#ifdef MKPRO_TERMINAL_REPORT_TAIL_STANDALONE_TEST
int main() {
  try {
    mkpro::tests::terminal_report_tail_rewrites_only_with_explicit_proofs();
    std::cout << "terminal_report_tail_test: ok\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "terminal_report_tail_test: " << error.what() << '\n';
    return 1;
  }
}
#endif
