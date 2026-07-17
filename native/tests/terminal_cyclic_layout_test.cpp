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
  items.back().indirect_flow_targets = std::vector<IrTarget>{0};
  pad_to(items, 20);
  items.at(item_at_address(items, 15)) = MachineItem::op(0xd7, "К П->X 7");
  items.at(item_at_address(items, 15)).indirect_memory_targets = std::vector<int>{4};

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

core::AuthoritativePostLayoutControlFlow complete_flow(const std::vector<MachineItem>& items,
                                                       int main_entry = 1) {
  core::PostLayoutControlFlowOptions options;
  options.main_entry = main_entry;
  return core::build_post_layout_control_flow(items, options);
}

std::vector<PreloadReport> preloads() {
  return {PreloadReport{.register_name = "c", .value = "0.41200076"}};
}

std::vector<MachineItem> relocated_main_fixture() {
  return {
      MachineItem::op(0x52, "В/О"),
      MachineItem::label("generic_tail"),
      MachineItem::op(0x37, "К AND"),
      MachineItem::op(0x35, "К frac"),
      MachineItem::op(0x57, "F x!=0"),
      MachineItem::address(std::string("generic_tail_return")),
      MachineItem::op(0x0a, "."),
      stop(StopDisposition::Terminal),
      MachineItem::op(0x01, "1"),
      MachineItem::label("generic_tail_return"),
      MachineItem::op(0x52, "В/О"),
      MachineItem::label("opaque_ui_entry"),
      MachineItem::op(0x53, "ПП"),
      MachineItem::address(std::string("generic_tail")),
      MachineItem::op(0x63, "П->X 3"),
      stop(StopDisposition::Terminal),
  };
}

core::AuthoritativePostLayoutControlFlow
relocated_main_flow(const std::vector<MachineItem>& items) {
  core::PostLayoutControlFlowOptions options;
  options.main_entry = std::string("opaque_ui_entry");
  return core::build_post_layout_control_flow(items, options);
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

core::AuthoritativePostLayoutControlFlow
payload_oracle_flow(const std::vector<MachineItem>& items) {
  return complete_flow(items);
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
    const auto terminal_stop = [] {
      MachineItem item = MachineItem::op(0x50, "С/П");
      item.stop_disposition = StopDisposition::Terminal;
      return item;
    };
    const std::vector<MachineItem> inverted_layout = {
        MachineItem::op(0x52, "В/О"),
        MachineItem::op(0x53, "ПП"),
        MachineItem::address(5),
        MachineItem::op(0x63, "П->X 3"),
        terminal_stop(),
        MachineItem::op(0x37, "К AND"),
        MachineItem::op(0x35, "К frac"),
        MachineItem::op(0x5e, "F x=0"),
        MachineItem::address(11),
        MachineItem::op(0x52, "В/О"),
        MachineItem::op(0x52, "В/О"),
        MachineItem::op(0x0a, "."),
        terminal_stop(),
    };
    core::PostLayoutControlFlowOptions flow_options;
    flow_options.main_entry = 1;
    const core::AuthoritativePostLayoutControlFlow inverted_flow =
        core::build_post_layout_control_flow(inverted_layout, flow_options);
    require(inverted_flow.proved,
            "inverted terminal fixture should have an authoritative input CFG");
    const std::vector<PreloadReport> inverted_preloads = {
        PreloadReport{.register_name = "a", .value = "ГE-2"}};
    const core::TerminalCyclicLayoutResult inverted =
        core::optimize_terminal_cyclic_layout(inverted_layout, inverted_preloads, inverted_flow);
    std::string inverted_reasons;
    for (const std::string& reason : inverted.plan.reasons)
      inverted_reasons += (inverted_reasons.empty() ? "" : "; ") + reason;
    require(inverted.applied == 1 && inverted.removed_cells == 2 &&
                inverted.plan.final_artifact_proved &&
                cell_count(inverted.items) == cell_count(inverted_layout) - 2,
            "generic branch inversion and terminal-block placement should expose a proved tail: " +
                inverted_reasons);

    std::vector<MachineItem> nonrecall_continuation = inverted_layout;
    nonrecall_continuation.front() = MachineItem::op(0x54, "К НОП");
    nonrecall_continuation.at(item_at_address(nonrecall_continuation, 3)) =
        MachineItem::op(0x01, "1");
    const core::AuthoritativePostLayoutControlFlow nonrecall_flow =
        core::build_post_layout_control_flow(nonrecall_continuation, flow_options);
    require(nonrecall_flow.proved,
            "return-alias fixture should have an authoritative non-recall continuation CFG");
    const std::vector<PreloadReport> nonzero_alias_preloads = {
        PreloadReport{.register_name = "e", .value = "10"}};
    const core::TerminalCyclicLayoutResult alias = core::optimize_terminal_cyclic_layout(
        nonrecall_continuation, nonzero_alias_preloads, nonrecall_flow,
        core::TerminalCyclicLayoutOptions{.enable_return_alias = true});
    std::string alias_reasons;
    for (const std::string& reason : alias.plan.reasons)
      alias_reasons += (alias_reasons.empty() ? "" : "; ") + reason;
    require(alias.applied == 1 && alias.removed_cells == 1 &&
                alias.plan.return_alias_proved && alias.plan.final_artifact_proved &&
                cell_count(alias.items) == cell_count(nonrecall_continuation) - 1,
            "equivalent nonzero return should shorten a direct conditional even when the "
            "full terminal continuation proof is unavailable: " + alias_reasons);
  }


  {
    const std::string helper = "semantic_zero_helper";
    const std::string update = "semantic_update";
    const std::string direct_return = "semantic_direct_return";
    std::vector<MachineItem> semantic;
    semantic.push_back(MachineItem::op(0x52, "V/O"));                    // 00
    semantic.push_back(MachineItem::label("semantic_main"));
    semantic.push_back(MachineItem::op(0x53, "PP"));                     // 01
    semantic.push_back(MachineItem::address(update));                     // 02
    semantic.push_back(MachineItem::op(0x67, "P->X 7"));                 // 03
    semantic.push_back(MachineItem::op(0x68, "P->X 8"));                 // 04
    semantic.push_back(MachineItem::op(0x69, "P->X 9"));                 // 05
    semantic.push_back(MachineItem::op(0x6a, "P->X a"));                 // 06
    semantic.push_back(MachineItem::op(0x15, "F 10^x"));                 // 07
    semantic.push_back(stop(StopDisposition::Terminal));                 // 08
    pad_to(semantic, 12);
    MachineItem helper_start = MachineItem::label(helper);
    helper_start.procedure_boundary = "start";
    helper_start.procedure_name = helper;
    semantic.push_back(std::move(helper_start));
    semantic.push_back(MachineItem::op(0x04, "4"));                      // 12
    semantic.push_back(MachineItem::op(0x13, "/"));                      // 13
    semantic.push_back(MachineItem::op(0x52, "V/O"));                    // 14
    MachineItem helper_end = MachineItem::label("end_" + helper);
    helper_end.procedure_boundary = "end";
    helper_end.procedure_name = helper;
    semantic.push_back(std::move(helper_end));
    pad_to(semantic, 20);
    semantic.push_back(MachineItem::label(update));
    semantic.push_back(MachineItem::op(0x12, "*"));                      // 20
    semantic.push_back(MachineItem::op(0x10, "+"));                      // 21
    semantic.push_back(MachineItem::op(0x37, "K AND"));                  // 22
    semantic.push_back(MachineItem::op(0x35, "K frac"));                 // 23
    semantic.push_back(MachineItem::op(0x57, "F x!=0"));                 // 24
    semantic.push_back(MachineItem::address(direct_return));              // 25
    semantic.push_back(MachineItem::op(0x0a, "."));                      // 26
    semantic.push_back(stop(StopDisposition::Terminal));                 // 27
    semantic.push_back(MachineItem::label(direct_return));
    semantic.push_back(MachineItem::op(0x52, "V/O"));                    // 28

    const std::optional<std::string> body_key =
        core::helper_semantic_alias_body_key(semantic, helper);
    require(body_key.has_value(), "semantic return fixture helper should certify its body");
    core::HelperSemanticContract contract;
    contract.entry_label = helper;
    contract.expression = core::helper_semantic_binary(
        core::HelperSemanticOp::Divide, core::helper_semantic_input(),
        core::helper_semantic_integer(4));
    contract.admitted_input =
        core::ExactIntegralDomain{.minimum = 0, .maximum = 0, .proved_integral = true};
    contract.input_decimal_derivation_exact = true;
    contract.input_zero_canonical_positive = true;
    contract.decimal_execution_exact = true;
    contract.hidden_x2_return_sync_proved = true;
    contract.x1_effect_proved = true;
    contract.x1_effect_key = "fixture";
    contract.certified_body_key = *body_key;
    const std::vector<core::HelperSemanticContract> contracts = {contract};
    const std::vector<PreloadReport> semantic_preloads = {
        PreloadReport{.register_name = "e", .value = "12"}};
    core::PostLayoutControlFlowOptions flow_options;
    flow_options.main_entry = 1;
    const core::AuthoritativePostLayoutControlFlow semantic_flow =
        core::build_post_layout_control_flow(semantic, flow_options);
    require(semantic_flow.proved, "semantic return fixture should have an authoritative CFG");
    const core::TerminalCyclicLayoutResult rewritten =
        core::optimize_terminal_cyclic_layout(
            semantic, semantic_preloads, semantic_flow,
            core::TerminalCyclicLayoutOptions{
                .enable_return_alias = true, .helper_semantic_contracts = &contracts});
    std::string reasons;
    for (const std::string& reason : rewritten.plan.reasons)
      reasons += (reasons.empty() ? "" : "; ") + reason;
    require(rewritten.applied == 1 && rewritten.removed_cells == 1 &&
                rewritten.plan.semantic_return_alias_proved &&
                rewritten.plan.final_artifact_proved &&
                cell_count(rewritten.items) == cell_count(semantic) - 1,
            "typed semantic return alias should remove one address cell: " + reasons);
  }

  {
    const std::vector<MachineItem> moving_entry = relocated_main_fixture();
    const core::AuthoritativePostLayoutControlFlow moving_flow =
        relocated_main_flow(moving_entry);
    require(moving_flow.proved &&
                moving_flow.external_entries.front().entry.address == 9,
            "synthetic terminal fixture should begin with an exact late main identity");
    const auto moved =
        core::optimize_terminal_cyclic_layout(moving_entry, preloads(), moving_flow);
    require(moved.applied == 1 && moved.removed_cells == 2 &&
                moved.plan.terminal_control_flow.proved &&
                moved.plan.terminal_control_flow.external_entries.front().entry.address == 7 &&
                moved.plan.terminal_control_flow.external_entries.front().entry.labels ==
                    std::vector<std::string>{"opaque_ui_entry"},
            "terminal relocation must rebind the exact main identity instead of preserving its "
            "stale physical address");
  }

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
  const core::AuthoritativePostLayoutControlFlow flow = complete_flow(input);
  require(flow.proved, "typed fixture CFG should be authoritative before layout optimization");
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
  require(rewritten.plan.final_control_flow.indirect_flow_targets.size() == 2U &&
              std::all_of(rewritten.plan.final_control_flow.indirect_flow_targets.begin(),
                          rewritten.plan.final_control_flow.indirect_flow_targets.end(),
                          [](const auto& entry) {
                            return entry.second.size() == 1U &&
                                   entry.second.front().address == 0;
                          }),
          "final proof should contain the old flow and derived terminal flow target sets");
  require(rewritten.plan.final_control_flow.external_entries.size() == 1U &&
              rewritten.plan.final_control_flow.external_entries.front().entry.address == 1 &&
              rewritten.plan.final_control_flow.external_entries.front().kind ==
                  core::ExternalEntryKind::Main,
          "exact external entry identity should survive both relocations");
  require(rewritten.plan.final_control_flow.indirect_memory_targets.size() == 1U &&
              rewritten.plan.final_control_flow.indirect_memory_targets.begin()->second ==
                  std::vector<int>{4},
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
    core::AuthoritativePostLayoutControlFlow incomplete = flow;
    incomplete.indirect_flow_targets.clear();
    const auto rejected = core::optimize_terminal_cyclic_layout(input, preloads(), incomplete);
    require(rejected.applied == 0 && cell_count(rejected.items) == 108 &&
                contains_reason(rejected.plan, "does not match"),
            "unknown indirect flow must reject the transaction");
  }
  {
    core::AuthoritativePostLayoutControlFlow bad_entry = complete_flow(input, 24);
    const auto rejected = core::optimize_terminal_cyclic_layout(input, preloads(), bad_entry);
    require(rejected.applied == 0 && cell_count(rejected.items) == 108 &&
                contains_reason(rejected.plan, "interior"),
            "entry into the dot rewritten as a store must fail closed");
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
    require(rejected.applied == 0 && contains_reason(rejected.plan, "authoritative"),
            "a resumable sink whose continuation cannot be proved must fail closed");
  }
  {
    std::vector<MachineItem> untyped_candidate = input;
    untyped_candidate.at(item_at_address(untyped_candidate, 25)).stop_disposition =
        StopDisposition::Unknown;
    const auto rejected = core::optimize_terminal_cyclic_layout(untyped_candidate, preloads(),
                                                                complete_flow(untyped_candidate));
    require(rejected.applied == 0 && contains_reason(rejected.plan, "unknown disposition"),
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
    observed.at(item_at_address(observed, 26)).indirect_memory_targets = std::vector<int>{3};
    core::AuthoritativePostLayoutControlFlow observed_flow = complete_flow(observed);
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
    std::vector<MachineItem> indirect_target = input;
    indirect_target.at(item_at_address(indirect_target, 10)).indirect_flow_targets =
        std::vector<IrTarget>{24};
    const core::AuthoritativePostLayoutControlFlow indirect_interior =
        complete_flow(indirect_target);
    const auto rejected = core::optimize_terminal_cyclic_layout(
        indirect_target, preloads(), indirect_interior);
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
    core::AuthoritativePostLayoutControlFlow entry_after_jump = complete_flow(non_call, 3);
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
