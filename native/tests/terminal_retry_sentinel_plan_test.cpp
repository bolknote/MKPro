#include "mkpro/compiler.hpp"
#include "mkpro/core/terminal_retry_sentinel_plan.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::tests {

namespace {

std::filesystem::path fixture_root() {
  const std::filesystem::path current = std::filesystem::current_path();
  if (std::filesystem::exists(current / "examples" / "pending-optimizer" /
                              "tic-tac-toe-4x4.mkpro")) {
    return current;
  }
  if (std::filesystem::exists(current.parent_path() / "examples" / "pending-optimizer" /
                              "tic-tac-toe-4x4.mkpro")) {
    return current.parent_path();
  }
  throw std::runtime_error("cannot locate terminal-retry fixture root");
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  require(static_cast<bool>(input), "terminal-retry source fixture should be readable");
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::size_t item_at_address(const std::vector<MachineItem>& items, int wanted) {
  int address = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (items.at(item_index).kind == MachineItemKind::Label)
      continue;
    if (address == wanted)
      return item_index;
    ++address;
  }
  throw std::runtime_error("missing machine address " + std::to_string(wanted));
}

std::vector<int> addresses(const std::vector<core::TerminalRetryRegisterAccessRef>& accesses) {
  std::vector<int> result;
  for (const auto& access : accesses)
    result.push_back(access.cell.address);
  return result;
}

bool contains_reason(const core::TerminalRetrySentinelPlan& plan, std::string_view needle) {
  return std::any_of(plan.reasons.begin(), plan.reasons.end(), [&](const std::string& reason) {
    return reason.find(needle) != std::string::npos;
  });
}

CompileResult compile_checkpoint() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.disable_candidate_search = true;
  options.canonicalize_packed_line_bank_walks = true;
  options.packed_line_family_mutating_selector_update_check_tail = true;
  options.stack_resident_temps = true;
  options.joint_packed_line_family_walk = true;
  options.dual_use_constant_indirect_flow = true;
  options.tail_branch_inversion = true;
  options.proc_layout_strategy = "reverse";
  return compile_source(read_text(fixture_root() / "examples" / "pending-optimizer" /
                                  "tic-tac-toe-4x4.mkpro"),
                        options);
}

core::TerminalRetrySentinelDiscoveryOptions discovery_options(
    const std::vector<MachineItem>& items) {
  core::TerminalRetrySentinelDiscoveryOptions options;
  options.manual_pp_then_run_proved = true;
  options.admitted_low_inputs = {1, 2, 3, 4};
  options.proved_indirect_flow_targets[item_at_address(items, 13)] = {0};
  return options;
}

std::string compact(std::string value) {
  value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
  return value;
}

void require_constant_carrier_emulator_oracle() {
  std::vector<int> program(40, 0x50);
  program.at(0) = 0xab; // K PP Rb
  program.at(1) = 0x40; // store the returned marker
  program.at(2) = 0x50;
  program.at(34) = 0x07;
  program.at(35) = 0x52;

  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  const emulator::ProgramLoadResult loaded = calc.load_program(program);
  require(loaded.diagnostics.empty(), "constant-carrier oracle should load");
  calc.set_register("b", "88888834");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(800, 6).stopped,
          "constant-carrier oracle should return and stop");
  require(compact(calc.read_register("0")) == "7,",
          "stable Rb=88888834 should call physical helper entry 34");
}

} // namespace

void terminal_retry_sentinel_plan_discovers_only_local_proved_prefix() {
  const CompileResult compiled = compile_checkpoint();
  require(compiled.implemented && compiled.diagnostics.empty() && compiled.steps.size() == 120U,
          "post-recall-hoist checkpoint should contain 120 cells");

  const core::TerminalRetrySentinelDiscoveryOptions options =
      discovery_options(compiled.items);
  const core::TerminalRetrySentinelPlan plan = core::discover_terminal_retry_sentinel_plan(
      compiled.items, compiled.preloads, compiled.registers, options);
  require(plan.proved && plan.reasons.empty(),
          "local manual-entry/retry prefix should produce a closed typed plan");
  require(plan.input_cells == 120 && plan.source_prefix_cells == 14 &&
              plan.replacement_prefix_cells == 7 && plan.projected_delta == -7 &&
              plan.projected_cells == 113,
          "local prefix plan should project the exact 120 -> 113 checkpoint");

  require(plan.header_recall.address == 0 && plan.prompt_stop.address == 1 &&
              plan.first_input_store.address == 2 && plan.second_input_store.address == 3 &&
              plan.local_helper_call.call.address == 4 &&
              plan.local_helper_call.operand.address == 5 && plan.occupied_join.address == 6 &&
              plan.occupied_store.address == 7 && plan.changed_subtract.address == 8 &&
              plan.changed_branch.call.address == 9 &&
              plan.changed_branch.operand.address == 10 && plan.sentinel_recall.address == 11 &&
              plan.sentinel_store.address == 12 && plan.retry_flow.address == 13 &&
              plan.success_entry.address == 14,
          "plan should expose every local source index without inspecting later algorithms");
  require(plan.complete_helper_calls.size() == 3U &&
              plan.complete_helper_calls.at(0).call.address == 4 &&
              plan.complete_helper_calls.at(1).call.address == 21 &&
              plan.complete_helper_calls.at(2).call.address == 51,
          "plan should publish the complete same-label helper call set without classifying calls");

  require(plan.display_register == 10 && plan.first_input_register == 1 &&
              plan.second_input_register == 2 && plan.occupied_register == 9 &&
              plan.low_input_slot_register == 3 && plan.sentinel_register == 11 &&
              plan.immutable_constant_register == 0 &&
              plan.direct_only_dynamic_register == 8 &&
              plan.free_stable_raw_seed_register == 8 && plan.retry_flow_register == 12,
          "register discovery should recover display/input/slot/sentinel and R0/R8 remaps");
  require(plan.removed_sentinel_preload_value == "-99999999" &&
              plan.rehomed_constant_flow_target == 34 && plan.register_remaps.size() == 2U,
          "sentinel deletion should rehome the natural target-34 constant before freeing R8");
  require(plan.register_remaps.at(0).from_register == 0 &&
              plan.register_remaps.at(0).to_register == 11 &&
              plan.register_remaps.at(0).moves_preload &&
              addresses(plan.register_remaps.at(0).accesses) == std::vector<int>({93}),
          "immutable R0 preload/access should move to the freed stable Rb carrier");
  require(plan.register_remaps.at(1).from_register == 8 &&
              plan.register_remaps.at(1).to_register == 0 &&
              !plan.register_remaps.at(1).moves_preload &&
              addresses(plan.register_remaps.at(1).accesses) ==
                  std::vector<int>({30, 32, 44, 56, 89}),
          "direct-only R8 state should move to the vacated low R0 carrier");
  require(plan.projected_logical_registers.at("best_score") == "0",
          "logical register ledger should project the direct-only R8 owner onto R0");

  const std::array<int, 7> expected_prefix{0x52, 0x62, 0x63, 0x50, 0x43, 0xab, 0xe3};
  require(plan.replacement_prefix.size() == expected_prefix.size(),
          "typed replacement prefix should contain seven cells");
  for (std::size_t index = 0; index < expected_prefix.size(); ++index)
    require(plan.replacement_prefix.at(index).opcode == expected_prefix.at(index),
            "typed replacement prefix opcode mismatch at " + std::to_string(index));
  require(plan.bare_return_zero_proved && plan.manual_pp_then_run_proved &&
              plan.target_prompt_stop_address == 3 &&
              plan.target_first_input_store_address == 4 &&
              plan.target_helper_call_address == 5 && plan.target_helper_entry_address == 34 &&
              plan.target_retry_conditional_address == 6 &&
              plan.admitted_low_inputs == std::vector<int>({1, 2, 3, 4}),
          "downstream return-stack facts should be explicit and locally derived");
  require(plan.address_reindex.front() == std::pair<int, int>{14, 7} &&
              plan.address_reindex.back() == std::pair<int, int>{119, 112},
          "plan should provide the complete post-prefix address reindex ledger");

  // Names, mnemonics, comments, and roles are deliberately absent from the
  // match. Symbolic labels remain only as CFG identities.
  std::vector<MachineItem> metadata_free = compiled.items;
  for (MachineItem& item : metadata_free) {
    item.mnemonic.clear();
    item.comment.reset();
    item.roles.clear();
  }
  const core::TerminalRetrySentinelPlan metadata_free_plan =
      core::discover_terminal_retry_sentinel_plan(metadata_free, compiled.preloads, {}, options);
  require(metadata_free_plan.proved && metadata_free_plan.free_stable_raw_seed_register == 8,
          "plan discovery should be independent of comments, roles, and logical names");

  {
    core::TerminalRetrySentinelDiscoveryOptions subset = options;
    subset.admitted_low_inputs = {4, 2};
    const auto proved_subset = core::discover_terminal_retry_sentinel_plan(
        compiled.items, compiled.preloads, compiled.registers, subset);
    require(proved_subset.proved &&
                proved_subset.admitted_low_inputs == std::vector<int>({2, 4}),
            "manual retry discovery should accept any oracle-safe proved input subset");
  }

  {
    core::TerminalRetrySentinelDiscoveryOptions escaped_domain = options;
    escaped_domain.admitted_low_inputs = {1, 5};
    const auto rejected = core::discover_terminal_retry_sentinel_plan(
        compiled.items, compiled.preloads, compiled.registers, escaped_domain);
    require(!rejected.proved && contains_reason(rejected, "manual return header"),
            "an input whose indirect target escapes the manual header must fail closed");
  }

  {
    core::TerminalRetrySentinelDiscoveryOptions unproved = options;
    unproved.manual_pp_then_run_proved = false;
    const auto rejected = core::discover_terminal_retry_sentinel_plan(
        compiled.items, compiled.preloads, compiled.registers, unproved);
    require(!rejected.proved && contains_reason(rejected, "manual PP-then-run"),
            "unknown manual-entry protocol must fail closed");
  }

  {
    core::TerminalRetrySentinelDiscoveryOptions unknown_flow = options;
    unknown_flow.proved_indirect_flow_targets.clear();
    const auto rejected = core::discover_terminal_retry_sentinel_plan(
        compiled.items, compiled.preloads, compiled.registers, unknown_flow);
    require(!rejected.proved && contains_reason(rejected, "sole-target header"),
            "unknown retry-flow target must fail closed");
  }

  {
    std::vector<MachineItem> extra_sentinel_use = compiled.items;
    extra_sentinel_use.at(item_at_address(extra_sentinel_use, 100)) =
        MachineItem::op(0x6b, "recall sentinel again");
    const auto rejected = core::discover_terminal_retry_sentinel_plan(
        extra_sentinel_use, compiled.preloads, compiled.registers,
        discovery_options(extra_sentinel_use));
    require(!rejected.proved && contains_reason(rejected, "extra use"),
            "sentinel carrier with any non-arm use must fail closed");
  }

  {
    std::vector<MachineItem> extra_entry = compiled.items;
    const std::size_t internal = item_at_address(extra_entry, 8);
    extra_entry.insert(extra_entry.begin() + static_cast<std::ptrdiff_t>(internal),
                       MachineItem::label("unrelated_internal_entry"));
    const auto rejected = core::discover_terminal_retry_sentinel_plan(
        extra_entry, compiled.preloads, compiled.registers, discovery_options(extra_entry));
    require(!rejected.proved && contains_reason(rejected, "internal label"),
            "an extra CFG entry inside the removable prefix must fail closed");
  }

  {
    std::vector<MachineItem> indirect_dynamic = compiled.items;
    indirect_dynamic.at(item_at_address(indirect_dynamic, 100)) =
        MachineItem::op(0xa8, "indirect use of dynamic carrier");
    const auto rejected = core::discover_terminal_retry_sentinel_plan(
        indirect_dynamic, compiled.preloads, compiled.registers,
        discovery_options(indirect_dynamic));
    require(!rejected.proved && contains_reason(rejected, "direct-only stable dynamic"),
            "indirect access must reject R8-to-R0 remapping");
  }

  {
    std::vector<MachineItem> live_low_slot = compiled.items;
    live_low_slot.at(item_at_address(live_low_slot, 14)) =
        MachineItem::op(0x63, "read low slot before dominating store");
    const auto rejected = core::discover_terminal_retry_sentinel_plan(
        live_low_slot, compiled.preloads, compiled.registers,
        discovery_options(live_low_slot));
    require(!rejected.proved && contains_reason(rejected, "dead low input/slot"),
            "a low register read before its first store cannot become the input slot");
  }

  require_constant_carrier_emulator_oracle();
}

} // namespace mkpro::tests
