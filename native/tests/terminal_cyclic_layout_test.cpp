#include "mkpro/core/terminal_cyclic_layout.hpp"

#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
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

MachineItem stop(StopDisposition disposition) {
  MachineItem item = MachineItem::op(0x50, "С/П");
  item.stop_disposition = disposition;
  return item;
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
  throw std::runtime_error("fixture address is absent: " + std::to_string(wanted));
}

void pad_to(std::vector<MachineItem>& items, int address) {
  while (cell_count(items) < address)
    items.push_back(stop(StopDisposition::Resumable));
}

std::vector<MachineItem> fixture(std::string_view suffix = "a") {
  const std::string helper = "leaf_" + std::string(suffix);
  const std::string update = "update_" + std::string(suffix);
  const std::string continuation = "continuation_" + std::string(suffix);
  const std::string direct_return = "direct_return_" + std::string(suffix);

  std::vector<MachineItem> items;
  items.push_back(MachineItem::op(0x52, "В/О")); // 00
  items.push_back(MachineItem::label("entry_" + std::string(suffix)));
  items.push_back(MachineItem::op(0x53, "ПП"));     // 01
  items.push_back(MachineItem::address(helper));    // 02
  items.push_back(MachineItem::op(0x53, "ПП"));     // 03
  items.push_back(MachineItem::address(update));    // 04
  items.push_back(MachineItem::op(0x63, "П->X 3")); // 05
  items.push_back(stop(StopDisposition::Terminal)); // 06
  pad_to(items, 10);
  items.push_back(MachineItem::op(0x8c, "К БП c")); // 10
  pad_to(items, 20);
  items.at(item_at_address(items, 15)) = MachineItem::op(0xd7, "К П->X 7");

  MachineItem update_label = MachineItem::label(update);
  update_label.procedure_boundary = "start";
  update_label.procedure_name = update;
  items.push_back(std::move(update_label));
  items.push_back(MachineItem::op(0x37, "К AND"));      // 20
  items.push_back(MachineItem::op(0x35, "К frac"));     // 21
  items.push_back(MachineItem::op(0x57, "F x!=0"));     // 22
  items.push_back(MachineItem::address(direct_return)); // 23
  items.push_back(MachineItem::op(0x0a, "."));          // 24
  items.push_back(stop(StopDisposition::Terminal));     // 25
  items.push_back(MachineItem::label(continuation));
  items.push_back(MachineItem::op(0x01, "1")); // 26
  items.push_back(MachineItem::label(direct_return));
  items.push_back(MachineItem::op(0x52, "В/О")); // 27
  MachineItem update_end = MachineItem::label("end_" + update);
  update_end.procedure_boundary = "end";
  update_end.procedure_name = update;
  items.push_back(std::move(update_end));
  pad_to(items, 40);

  MachineItem helper_label = MachineItem::label(helper);
  helper_label.procedure_boundary = "start";
  helper_label.procedure_name = helper;
  items.push_back(std::move(helper_label));
  items.push_back(MachineItem::op(0x22, "F x^2")); // 40
  items.push_back(MachineItem::op(0x31, "F |x|")); // 41
  items.push_back(MachineItem::op(0x52, "В/О"));   // 42
  MachineItem helper_end = MachineItem::label("end_" + helper);
  helper_end.procedure_boundary = "end";
  helper_end.procedure_name = helper;
  items.push_back(std::move(helper_end));
  pad_to(items, 108);
  return items;
}

core::TerminalCyclicControlFlow complete_flow(const std::vector<MachineItem>& items) {
  core::TerminalCyclicControlFlow flow;
  flow.external_entries = {{.pc = 1}};
  flow.indirect_flow_targets[item_at_address(items, 10)] = {0};
  const std::size_t memory_item = item_at_address(items, 15);
  if (items.at(memory_item).kind == MachineItemKind::Op &&
      ((items.at(memory_item).opcode & 0xf0) == 0xb0 ||
       (items.at(memory_item).opcode & 0xf0) == 0xd0)) {
    flow.indirect_memory_targets[memory_item] = {4};
  }
  return flow;
}

std::vector<PreloadReport> preloads() {
  return {PreloadReport{.register_name = "c", .value = "0.41200076"}};
}

std::vector<MachineItem> payload_oracle_fixture() {
  return {
      MachineItem::op(0x52, "В/О"),
      MachineItem::op(0x53, "ПП"),
      MachineItem::address(std::string("oracle_update")),
      MachineItem::op(0x63, "П->X 3"),
      stop(StopDisposition::Terminal),
      MachineItem::label("oracle_update"),
      MachineItem::op(0x37, "К AND"),
      MachineItem::op(0x35, "К frac"),
      MachineItem::op(0x57, "F x!=0"),
      MachineItem::address(std::string("oracle_return")),
      MachineItem::op(0x0a, "."),
      stop(StopDisposition::Terminal),
      MachineItem::op(0x0d, "Cx"),
      MachineItem::op(0x20, "F pi"),
      MachineItem::label("oracle_return"),
      MachineItem::op(0x52, "В/О"),
  };
}

core::TerminalCyclicControlFlow payload_oracle_flow(const std::vector<MachineItem>&) {
  return core::TerminalCyclicControlFlow{
      .external_entries = {{.pc = 1}},
  };
}

std::string compact(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) { return std::isspace(ch) != 0; }),
              value.end());
  return value;
}

struct OracleOutcome {
  bool stopped = false;
  std::string x;
  std::string x2;
  std::string selector;
};

OracleOutcome run_payload_oracle(const std::vector<MachineItem>& items, const std::string& y) {
  const ResolvedProgram resolved = resolve_machine_items(items, {});
  require(resolved.diagnostics.empty(), "payload theorem artifact should resolve");
  std::vector<int> codes;
  for (const ResolvedStep& step : resolved.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  require(calc.load_program(codes).diagnostics.empty(), "payload theorem artifact should load");
  calc.set_register("a", "ГE-2");
  calc.set_register("3", "777");
  calc.set_register("x", "88888834");
  calc.set_register("x1", "123");
  calc.set_register("y", y);
  calc.set_register("z", "333");
  calc.set_register("t", "444");
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult stable = calc.run_until_stable(4000, 8);
  return OracleOutcome{
      .stopped = stable.stopped,
      .x = compact(calc.read_register("x")),
      .x2 = compact(calc.read_register("x1")),
      .selector = compact(calc.read_register("a")),
  };
}

bool contains_reason(const core::TerminalCyclicLayoutPlan& plan, std::string_view needle) {
  return std::any_of(plan.reasons.begin(), plan.reasons.end(), [&](const std::string& reason) {
    return reason.find(needle) != std::string::npos;
  });
}

} // namespace

void terminal_cyclic_layout_derives_complete_proofs_transactionally() {
  {
    const std::vector<MachineItem> oracle_input = payload_oracle_fixture();
    const std::vector<PreloadReport> oracle_preloads = {
        PreloadReport{.register_name = "a", .value = "ГE-2"}};
    const auto oracle_rewrite = core::optimize_terminal_cyclic_layout(
        oracle_input, oracle_preloads, payload_oracle_flow(oracle_input));
    require(oracle_rewrite.applied == 1 && oracle_rewrite.plan.terminal_proved &&
                !oracle_rewrite.plan.cyclic_proved &&
                cell_count(oracle_rewrite.items) == cell_count(oracle_input) - 2,
            "raw payload theorem fixture should independently prove terminal -2");
    const OracleOutcome baseline_return = run_payload_oracle(oracle_input, "44444.4");
    const OracleOutcome rewritten_return = run_payload_oracle(oracle_rewrite.items, "44444.4");
    require(baseline_return.stopped && rewritten_return.stopped &&
                baseline_return.x == rewritten_return.x &&
                baseline_return.x2 == rewritten_return.x2 &&
                baseline_return.selector == rewritten_return.selector,
            "raw zero-return branch must preserve visible X, hidden X2, and selector state");
    const OracleOutcome baseline_report = run_payload_oracle(oracle_input, "88888.4");
    const OracleOutcome rewritten_report = run_payload_oracle(oracle_rewrite.items, "88888.4");
    require(baseline_report.stopped && rewritten_report.stopped &&
                baseline_report.x == rewritten_report.x &&
                baseline_report.selector == rewritten_report.selector,
            "report branch must store and recover the exact raw X payload");
    require(baseline_report.x2 != rewritten_report.x2,
            "oracle should expose why hidden X2 differences require typed no-resume halt proof: " +
                baseline_report.x + "/" + baseline_report.x2 + " vs " + rewritten_report.x + "/" +
                rewritten_report.x2);
  }

  const std::vector<MachineItem> input = fixture();
  const core::TerminalCyclicControlFlow flow = complete_flow(input);
  const core::TerminalCyclicLayoutPlan plan =
      core::verify_terminal_cyclic_layout(input, preloads(), flow);
  require(plan.terminal_proved && plan.cyclic_proved && plan.final_artifact_proved &&
              plan.input_cells == 108 && plan.terminal_output_cells == 106 &&
              plan.output_cells == 105 && plan.removed_cells == 3,
          "trusted plan should derive the complete 108 -> 106 -> 105 proof chain");
  require(plan.raw_selector.selector_register == "c" &&
              plan.raw_selector.runtime_value_set_is_exhaustive &&
              plan.raw_selector.selector_is_unwritten_until_use &&
              plan.raw_selector.facts.size() == 1U &&
              plan.raw_selector.facts.front().actual_flow_target == 0 &&
              plan.raw_selector.facts.front().conditional_preserves_selector &&
              plan.raw_selector.facts.front().conditional_preserves_x_y_z_t &&
              plan.raw_selector.facts.front().conditional_preserves_x2,
          "raw selector booleans should be consequences of preload and opcode analysis");
  require(plan.continuation.terminal_slot_register == "3" &&
              plan.continuation.fractional_predicate_is_terminal_payload &&
              plan.continuation.previous_slot_value_is_dead_on_report_path &&
              plan.continuation.stored_payload_is_live_until_terminal_stop &&
              plan.continuation.every_report_continuation_reaches_terminal_stop &&
              plan.continuation.no_prior_observable_event_error_or_divergence &&
              plan.continuation.continuation_stack_and_x2_effects_are_unobservable,
          "liveness booleans should be derived from all continuation paths");
  require(plan.relocation.continuation_is_relocated_immediately_after_tail &&
              plan.relocation.all_direct_references_are_relocated_or_symbolic &&
              plan.relocation.all_indirect_targets_and_selector_charges_are_rebound &&
              plan.relocation.removed_cells_have_no_external_entries &&
              plan.relocation.zero_and_direct_returns_have_equivalent_stack_contracts,
          "relocation booleans should be derived from item and address identity ledgers");

  const core::TerminalCyclicLayoutResult rewritten =
      core::optimize_terminal_cyclic_layout(input, preloads(), flow);
  require(rewritten.applied == 2 && rewritten.removed_cells == 3 &&
              cell_count(rewritten.items) == 105 && rewritten.plan.final_artifact_proved,
          "terminal and cyclic rewrites should commit only after both final checks");
  require(rewritten.plan.final_indirect_flow_targets.size() == 2U &&
              std::all_of(rewritten.plan.final_indirect_flow_targets.begin(),
                          rewritten.plan.final_indirect_flow_targets.end(),
                          [](const auto& entry) { return entry.second == std::vector<int>{0}; }),
          "final proof should contain the old flow and derived terminal flow target sets");
  require(rewritten.plan.final_external_entries == std::vector<core::ExternalEntryState>{{.pc = 1}},
          "external entry identity should survive both relocations");
  require(rewritten.plan.final_indirect_memory_targets.size() == 1U &&
              rewritten.plan.final_indirect_memory_targets.begin()->second == std::vector<int>{4},
          "the total indirect-memory map should be reindexed and rechecked in the final gate");
  const MachineItem& a4 = rewritten.items.at(item_at_address(rewritten.items, 104));
  require(a4.kind == MachineItemKind::Op && a4.opcode == 0x31 &&
              std::find(a4.roles.begin(), a4.roles.end(), "cyclic-end-return:A4-to-00") !=
                  a4.roles.end(),
          "cyclic helper body should end at A4 and wrap to the proved return at 00");

  {
    const std::vector<MachineItem> alpha = fixture("renamed");
    const auto alpha_result =
        core::optimize_terminal_cyclic_layout(alpha, preloads(), complete_flow(alpha));
    require(alpha_result.applied == 2 && cell_count(alpha_result.items) == 105,
            "alpha-renamed labels must not affect structural discovery");
  }
  {
    std::vector<MachineItem> side_space = input;
    side_space.at(item_at_address(side_space, 4)).formal_opcode = 0xd2;
    const auto side_result =
        core::optimize_terminal_cyclic_layout(side_space, preloads(), complete_flow(side_space));
    require(side_result.applied == 2 && cell_count(side_result.items) == 105,
            "modeled non-super-dark formal calls whose target identity stays fixed should pass");
  }
  {
    std::vector<MachineItem> metadata_free = input;
    for (MachineItem& item : metadata_free) {
      item.mnemonic.clear();
      item.comment.reset();
      item.roles.clear();
    }
    const auto metadata_result =
        core::optimize_terminal_cyclic_layout(metadata_free, preloads(), flow);
    require(metadata_result.applied == 2 && cell_count(metadata_result.items) == 105,
            "mnemonics, comments, and roles must not participate in the proof");
  }
  {
    core::TerminalCyclicControlFlow incomplete = flow;
    incomplete.indirect_flow_targets.clear();
    const auto rejected = core::optimize_terminal_cyclic_layout(input, preloads(), incomplete);
    require(rejected.applied == 0 && cell_count(rejected.items) == 108 &&
                contains_reason(rejected.plan, "missing"),
            "unknown indirect flow must reject the transaction");
  }
  {
    core::TerminalCyclicControlFlow bad_entry = flow;
    bad_entry.external_entries = {{.pc = 24}};
    const auto rejected = core::optimize_terminal_cyclic_layout(input, preloads(), bad_entry);
    require(rejected.applied == 0 && cell_count(rejected.items) == 108 &&
                contains_reason(rejected.plan, "interior"),
            "entry into the dot rewritten as a store must fail closed");
  }
  {
    core::TerminalCyclicControlFlow nonempty_stack = flow;
    nonempty_stack.external_entries = {{.pc = 3, .return_stack = {7}}};
    const auto accepted = core::optimize_terminal_cyclic_layout(input, preloads(), nonempty_stack);
    require(accepted.applied == 2 && cell_count(accepted.items) == 105 &&
                accepted.plan.final_external_entries == nonempty_stack.external_entries,
            "typed nonempty hardware return-stack entries must be explored and preserved");
  }
  {
    core::TerminalCyclicControlFlow too_deep = flow;
    too_deep.external_entries = {{.pc = 3, .return_stack = {7, 8, 9, 11, 12, 13}}};
    const auto rejected = core::optimize_terminal_cyclic_layout(input, preloads(), too_deep);
    require(rejected.applied == 0 && contains_reason(rejected.plan, "five-level"),
            "entry protocol cannot exceed the five-level MK-61 return stack");
  }
  {
    core::TerminalCyclicLayoutOptions impossible_depth;
    impossible_depth.maximum_return_depth = 6;
    const auto rejected =
        core::optimize_terminal_cyclic_layout(input, preloads(), flow, impossible_depth);
    require(rejected.applied == 0 && contains_reason(rejected.plan, "five-level"),
            "the verifier bound itself cannot exceed the hardware return-stack depth");
  }
  {
    std::vector<MachineItem> resumable_sink = input;
    resumable_sink.at(item_at_address(resumable_sink, 6)).stop_disposition =
        StopDisposition::Resumable;
    const auto rejected = core::optimize_terminal_cyclic_layout(resumable_sink, preloads(),
                                                                complete_flow(resumable_sink));
    require(rejected.applied == 0 && contains_reason(rejected.plan, "resumable STOP"),
            "an ordinary resumable STOP cannot hide continuation stack/X2 differences");
  }
  {
    std::vector<MachineItem> untyped_candidate = input;
    untyped_candidate.at(item_at_address(untyped_candidate, 25)).stop_disposition =
        StopDisposition::Unknown;
    const auto rejected = core::optimize_terminal_cyclic_layout(untyped_candidate, preloads(),
                                                                complete_flow(untyped_candidate));
    require(rejected.applied == 0 && contains_reason(rejected.plan, "halt provenance"),
            "the provisional STOP must retain compiler-owned source halt provenance");
  }
  {
    std::vector<MachineItem> overwritten = input;
    overwritten.at(item_at_address(overwritten, 15)) = MachineItem::op(0x4c, "X->П c");
    const auto rejected =
        core::optimize_terminal_cyclic_layout(overwritten, preloads(), complete_flow(overwritten));
    require(rejected.applied == 0 && cell_count(rejected.items) == 108,
            "a selector write on any proved entry graph must reject raw exhaustiveness");
  }
  {
    std::vector<MachineItem> clobbered = input;
    clobbered.at(item_at_address(clobbered, 26)) = MachineItem::op(0x43, "X->П 3");
    const auto rejected =
        core::optimize_terminal_cyclic_layout(clobbered, preloads(), complete_flow(clobbered));
    require(rejected.applied == 0 && cell_count(rejected.items) == 108 &&
                contains_reason(rejected.plan, "payload register"),
            "a continuation clobber before the sink recall must fail liveness");
  }
  {
    std::vector<MachineItem> observed = input;
    observed.at(item_at_address(observed, 26)) = MachineItem::op(0xd7, "К П->X 7");
    core::TerminalCyclicControlFlow observed_flow = complete_flow(observed);
    observed_flow.indirect_memory_targets[item_at_address(observed, 26)] = {3};
    const auto rejected =
        core::optimize_terminal_cyclic_layout(observed, preloads(), observed_flow);
    require(rejected.applied == 0 && cell_count(rejected.items) == 108 &&
                contains_reason(rejected.plan, "payload register"),
            "an indirect D* recall of the terminal slot must count as a live observation");
  }
  {
    std::vector<MachineItem> domain_error = input;
    domain_error.at(item_at_address(domain_error, 26)) = MachineItem::op(0x21, "F sqrt");
    const auto rejected = core::optimize_terminal_cyclic_layout(domain_error, preloads(),
                                                                complete_flow(domain_error));
    require(rejected.applied == 0 && cell_count(rejected.items) == 108 &&
                contains_reason(rejected.plan, "value-domain proof"),
            "documented /, log, sqrt class operations need more than OpcodeRisk metadata");
  }
  {
    std::vector<MachineItem> undocumented = input;
    undocumented.at(item_at_address(undocumented, 26)) = MachineItem::op(0x3e, "Y->X");
    const auto rejected = core::optimize_terminal_cyclic_layout(undocumented, preloads(),
                                                                complete_flow(undocumented));
    require(rejected.applied == 0 && contains_reason(rejected.plan, "non-documented"),
            "an undocumented continuation opcode needs an exact semantic whitelist");
  }
  {
    std::vector<MachineItem> super_dark = input;
    super_dark.at(item_at_address(super_dark, 2)).formal_opcode = 0xfa;
    const auto rejected =
        core::optimize_terminal_cyclic_layout(super_dark, preloads(), complete_flow(super_dark));
    require(rejected.applied == 0 && contains_reason(rejected.plan, "super-dark"),
            "one-command super-dark operands need a dedicated hardware CFG model");
  }
  {
    std::vector<MachineItem> entered_removed_stop = input;
    entered_removed_stop.at(item_at_address(entered_removed_stop, 15)) =
        MachineItem::op(0x51, "БП");
    entered_removed_stop.at(item_at_address(entered_removed_stop, 16)) =
        MachineItem::address(std::string("removed_stop"));
    entered_removed_stop.insert(
        entered_removed_stop.begin() +
            static_cast<std::ptrdiff_t>(item_at_address(entered_removed_stop, 25)),
        MachineItem::label("removed_stop"));
    const auto rejected = core::optimize_terminal_cyclic_layout(
        entered_removed_stop, preloads(), complete_flow(entered_removed_stop));
    require(rejected.applied == 0 && cell_count(rejected.items) == 108 &&
                contains_reason(rejected.plan, "interior"),
            "a symbolic reference into the removed STOP must not be called relocatable");
  }
  {
    core::TerminalCyclicControlFlow indirect_interior = flow;
    indirect_interior.indirect_flow_targets[item_at_address(input, 10)] = {24};
    const auto rejected =
        core::optimize_terminal_cyclic_layout(input, preloads(), indirect_interior);
    require(rejected.applied == 0 && contains_reason(rejected.plan, "interior"),
            "an indirect target into the rewritten dot/store interior must fail closed");
  }
  {
    std::vector<MachineItem> no_zero_return = input;
    no_zero_return.at(item_at_address(no_zero_return, 0)) = stop(StopDisposition::Resumable);
    const auto rejected = core::optimize_terminal_cyclic_layout(no_zero_return, preloads(),
                                                                complete_flow(no_zero_return));
    require(rejected.applied == 0 && cell_count(rejected.items) == 108,
            "missing physical-00 return must reject both tricks");
  }
  {
    std::vector<MachineItem> non_call = input;
    non_call.at(item_at_address(non_call, 1)).opcode = 0x51;
    non_call.at(item_at_address(non_call, 1)).mnemonic = "БП";
    core::TerminalCyclicControlFlow entry_after_jump = complete_flow(non_call);
    entry_after_jump.external_entries = {{.pc = 3}};
    const auto terminal_only =
        core::optimize_terminal_cyclic_layout(non_call, preloads(), entry_after_jump);
    require(terminal_only.applied == 1 && terminal_only.removed_cells == 2 &&
                cell_count(terminal_only.items) == 106 && terminal_only.plan.terminal_proved &&
                !terminal_only.plan.cyclic_proved,
            "cyclic failure must not fabricate its certificate or undo an independent terminal "
            "commit");
  }
}

} // namespace mkpro::tests
