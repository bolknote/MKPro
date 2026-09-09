#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/format.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/search_frontier.hpp"
#include "mkpro/core/selector_writeback.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/dead_code_after_halt.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <exception>

namespace mkpro::tests {

namespace {

template <class Action> void require_export_rejected(Action action) {
  bool rejected = false;
  try {
    action();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "virtual code must not be exported as a physical program");
}

} // namespace

void virtual_addresses_preserve_logical_control_flow() {
  CompileOptions analysis;
  analysis.analysis = true;
  for (const int target : {105, 111, 112, 125, 165, 255, 300}) {
    MachineEmitter emitter;
    emitter.emit_jump(0x51, "jump", std::string("tail"));
    for (int address = 2; address < target; ++address)
      emitter.emit_op(0x54, "nop");
    emitter.emit_label("tail");
    emitter.emit_op(0x07, "7");
    emitter.emit_stop(StopDisposition::Terminal, "stop");
    const auto resolved = resolve_machine_items(emitter.items, analysis);
    require(resolved.diagnostics.empty() &&
                resolved.steps.size() == static_cast<std::size_t>(target + 2),
            "analysis must not impose an eight-bit ceiling on logical targets");
    const auto& operand = resolved.steps.at(1);
    require(operand.opcode == -1 && operand.hex == "@" + std::to_string(target) &&
                operand.address_target == ResolvedAddress{LogicalCodeAddress{target}} &&
                resolved_step_target(operand) == target,
            "logical targets must remain distinct from every physical address byte");
    const auto dot = format_dot_steps(resolved.steps);
    require(dot.find("n0 -> n" + std::to_string(target) + " ") != std::string::npos,
            "analysis CFG must reach the logical tail, not a decoded byte alias");
    require(format_listing_steps(resolved.steps).find("@" + std::to_string(target)) !=
                std::string::npos,
            "human listing must expose an unencoded logical target");
    require(physical_program_image_rejection(resolved.steps).has_value(),
            "an oversized symbolic artifact is not executable bytecode");
    require_export_rejected([&] { (void)format_program_tokens(resolved.steps); });
    require_export_rejected([&] { (void)format_mk61s_steps(resolved.steps); });

    // Ordinary CFG reachability, not a source-specific recognizer, removes
    // the unreachable region while the target is still a symbolic label.
    const auto optimized = core::passes::dead_code_after_halt(
        raise_machine_to_ir(emitter.items), {.options = analysis});
    const auto linked = resolve_machine_items(lower_ir_to_machine(optimized.ops));
    require(optimized.applied == target - 2 && linked.diagnostics.empty() &&
                linked.steps.size() == 4 && linked.steps[1].opcode == 0x02 &&
                resolved_step_target(linked.steps[1]) == 2 &&
                !physical_program_image_rejection(linked.steps).has_value(),
            "logical optimization must become physical only after final relocation");
    std::vector<int> bytes;
    for (const auto& step : linked.steps)
      bytes.push_back(step.opcode);
    emulator::MK61 calculator;
    require(calculator.load_program(bytes).diagnostics.empty(), "physical code must load");
    calculator.press_sequence({"В/О", "С/П"});
    require(calculator.run_until_stable(12000, 8).stopped &&
                calculator.read_register("X") == "7,",
            "linked code must execute the intended branch on the MK-61 emulator");

    auto corrupt = linked.steps;
    corrupt[1].opcode = 0x03;
    require(!resolved_step_target(corrupt[1]).has_value() &&
                physical_program_image_rejection(corrupt).has_value(),
            "typed relocation metadata must not hide a mutated physical operand");
    const auto rejected = resolve_machine_items(emitter.items);
    require(std::any_of(rejected.diagnostics.begin(), rejected.diagnostics.end(),
                        [](const Diagnostic& diagnostic) {
                          return diagnostic.code == "address-out-of-range";
                        }),
            "ordinary physical compilation must still reject unresolved overflow");
  }

  for (const auto model : {AddressSpaceModel::Standard, AddressSpaceModel::Mk61SMiniExpanded}) {
    MachineEmitter emitter;
    emitter.address_space_model = model;
    for (const int opcode : {0xa5, 0xb2, 0xc5, 0xee, 0xff}) {
      emitter.emit_op(0x53, "call");
      emitter.emit_formal_address(opcode);
    }
    CompileOptions options;
    if (model == AddressSpaceModel::Mk61SMiniExpanded)
      options.optimizer_feature_profile_override = FeatureProfile::Mk61SMiniExpanded;
    const auto resolved = resolve_machine_items(emitter.items, options);
    require(resolved.diagnostics.empty(), "explicit formal entries must remain encodable");
    for (std::size_t index = 1; index < resolved.steps.size(); index += 2) {
      const auto& step = resolved.steps[index];
      require(step.address_target == ResolvedAddress{FormalCodeAddress{step.opcode}} &&
                  resolved_step_target(step, model) == formal_address_info(step.opcode, model).actual,
              "formal entries must preserve their entry byte and selected machine model");
      auto corrupt = step;
      corrupt.opcode ^= 1;
      require(!resolved_step_target(corrupt, model).has_value(),
              "formal metadata must not hide a changed entry byte");
    }

    MachineEmitter logical;
    logical.emit_jump(0x51, "jump", std::string("boundary"));
    for (int address = 2; address < 105; ++address)
      logical.emit_op(0x54, "nop");
    logical.emit_label("boundary");
    logical.emit_stop(StopDisposition::Terminal, "stop");
    options.analysis = true;
    const auto boundary = resolve_machine_items(logical.items, options);
    require(boundary.diagnostics.empty() && resolved_step_target(boundary.steps[1], model) == 105,
            "the same logical target must survive both physical memory profiles");
    require(boundary.steps[1].opcode ==
                (model == AddressSpaceModel::Standard ? -1 : 0xa5),
            "only the selected physical model determines whether target 105 can be encoded");
    require(physical_program_image_rejection(boundary.steps, model).has_value() ==
                (model == AddressSpaceModel::Standard),
            "expanded physical placement must not be confused with a stock side entry");
  }
}

void deferred_fractional_selectors_bind_after_logical_layout() {
  // Execute numeric setup itself: set_register("0.5") injects a different
  // raw word and cannot establish the compiler's literal-preload contract.
  for (const std::string value : {"0.5", "0.8", "0.14375", "14.375", "-850"}) {
    const bool preserved = value != "14.375" && value != "-850";
    const int target = value == "-850" ? 50 : preserved ? 0 : 14;
    std::vector<int> codes{0x52};
    for (const char ch : value) {
      if (ch != '-')
        codes.push_back(ch == '.' ? 0x0a : ch - '0');
    }
    if (value.front() == '-')
      codes.push_back(0x0b);
    const std::size_t store = codes.size();
    codes.insert(codes.end(), {0x4d, 0x50, 0xad, 0x6d, 0x50});
    if (target != 0) {
      while (codes.size() < static_cast<std::size_t>(target))
        codes.push_back(0x54);
      codes.push_back(0x52);
    }
    std::vector<MachineItem> items;
    for (int code : codes)
      items.push_back(MachineItem::op(code, "setup fact"));
    items[store + 2].indirect_flow_targets = std::vector<IrTarget>{target};
    items[store + 1].stop_disposition = StopDisposition::Resumable;
    items[store + 4].stop_disposition = StopDisposition::Terminal;
    const auto control = core::build_post_layout_control_flow(
        items, {.empty_return_target = 1});
    require(control.proved, "numeric setup fact must have authoritative control flow: " +
                value + ": " + (control.reasons.empty() ? "unknown" : control.reasons.front()));
    require(core::selector_writeback_is_unobserved(
                items, control, store + 2, {.register_name = "d", .value = value}) ==
                preserved,
            "selector writeback must use the actual numeric setup representation");
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(), "numeric setup fact must load");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(12000, 8).stopped, "numeric setup must reach its pause");
    const std::string before = calc.read_register("d");
    calc.press_sequence({"С/П"});
    require(calc.run_until_stable(12000, 8).stopped, "numeric selector must return");
    require((calc.read_register("d") == before) == preserved,
            "numeric setup preservation proof must match emulator writeback");
  }
  // A physically invalid incumbent may need a larger logical enabling state
  // before a later placement gets below the limit. Do not let an already
  // expanded incumbent occupy the only beam slot again.
  struct SearchNode {
    std::string key;
    int cells = 0;
  };
  const auto node_key = [](const SearchNode& node) { return node.key; };
  const auto smaller = [](const SearchNode& a, const SearchNode& b) {
    return a.cells < b.cells;
  };
  std::set<std::string> expanded_nodes{"root"};
  auto frontier = core::select_unexpanded_search_frontier(
      std::vector<SearchNode>{{"root", 106}, {"enable", 107}},
      expanded_nodes, 1, node_key, smaller);
  require(frontier.size() == 1 && frontier.front().key == "enable",
          "an expanded smaller layout must not starve its enabling successor");
  expanded_nodes.insert(frontier.front().key);
  frontier = core::select_unexpanded_search_frontier(
      std::vector<SearchNode>{{"enable", 107}, {"placed", 104}},
      expanded_nodes, 1, node_key, smaller);
  require(frontier.size() == 1 && frontier.front().cells == 104,
          "a larger logical intermediate must be able to reach a physical winner");
  frontier = core::select_unexpanded_search_frontier(
      std::vector<SearchNode>{{"z", 106}, {"a", 106}, {"a", 106}},
      expanded_nodes, 2, node_key, smaller);
  require(frontier.size() == 2 && frontier[0].key == "a" && frontier[1].key == "z",
          "equal-cost frontier identities must be deterministic and unique");
  require(core::select_unexpanded_search_frontier(
              std::vector<SearchNode>{{"root", 106}, {"enable", 107}},
              expanded_nodes, 1, node_key, smaller).empty(),
          "a frontier with no unexpanded successors must terminate");
  const auto op = [](int opcode) {
    return MachineItem::op(opcode, opcode_by_code(opcode).name);
  };
  const auto stop = [&] {
    auto item = op(0x50);
    item.stop_disposition = StopDisposition::Terminal;
    return item;
  };
  for (const auto [target, calls] :
       {std::pair{105, 10}, std::pair{111, 12}, std::pair{125, 24}}) {
    std::vector<MachineItem> items;
    items.push_back(op(0x68));
    items.push_back(op(0x35));
    items.push_back(op(0x10));
    for (int call = 0; call < calls; ++call) {
      items.push_back(op(0x53));
      items.push_back(MachineItem::address(std::string("opaque_leaf")));
    }
    items.push_back(stop());
    // Unrelated closed components, not removable by this layout transaction.
    // Their content is retained byte for byte while the target moves.
    while (static_cast<int>(items.size()) < target)
      items.push_back(stop());
    items.push_back(MachineItem::label("opaque_leaf"));
    items.push_back(op(0x0b));
    items.push_back(op(0x52));

    core::PostLayoutControlFlowOptions flow_options;
    flow_options.empty_return_target.reset();
    const auto flow = core::build_post_layout_control_flow(items, flow_options);
    require(flow.proved, "logical overflow must have exact input control flow");
    const std::vector<PreloadReport> preloads{
        {.register_name = "8", .value = "10.375"}};
    const auto rewritten = core::optimize_natural_target_component_layout(items, preloads, flow);
    require(rewritten.applied > 0 && rewritten.removed_cells == calls &&
                rewritten.plan.final_artifact_proved &&
                rewritten.plan.stack_and_x2_equivalent &&
                rewritten.plan.call_return_equivalent &&
                rewritten.plan.data_projection_equivalent,
            "a projected constant must bind a logical tail without first fixing its address: " +
                std::to_string(target) + " removed=" +
                std::to_string(rewritten.removed_cells));
    require(rewritten.plan.transparent_split_bridges == 0,
            "a proof-valid rebound target must beat a preferred target needing a paid bridge");
    require(rewritten.preloads.size() == 1U &&
                rewritten.preloads[0].value != "10.375" &&
                rewritten.preloads[0].value.ends_with(".375") &&
                rewritten.plan.flows.size() == static_cast<std::size_t>(calls),
            "the final physical target, not the discovery seed, determines the preload");
    for (const auto& proof : rewritten.plan.runtime_selectors) {
      require(proof.typed_target_matches_runtime_decode &&
                  proof.final_target_address <= 99 &&
                  proof.decoded_target == proof.final_target_address,
              "every converted call must decode to the independently proved physical target");
    }
    const auto linked = resolve_machine_items(rewritten.items);
    require(linked.diagnostics.empty() &&
                linked.steps.size() == static_cast<std::size_t>(target + 2 - calls) &&
                !physical_program_image_rejection(linked.steps).has_value(),
            "the complete relocated artifact must fit, not only its executable closure");
    std::vector<int> bytes;
    for (const auto& step : linked.steps)
      bytes.push_back(step.opcode);
    emulator::MK61 calculator;
    require(calculator.load_program(bytes).diagnostics.empty(), "relocated artifact must load");
    calculator.set_register("8", rewritten.preloads[0].value);
    calculator.set_register("X", "2");
    calculator.press_sequence({"В/О", "С/П"});
    const auto run = calculator.run_until_stable(12000, 8);
    require(run.stopped && calculator.read_register("X") == "2,375",
            "relocated calls must retain returns and the exact fractional data value: target=" +
                std::to_string(target) + " selector=" + rewritten.preloads[0].value +
                " X=" + calculator.read_register("X") +
                " stopped=" + std::to_string(run.stopped));

    // Full precision and a visible integer part are not projection proofs.
    auto raw_read = items;
    raw_read[1] = op(0x54);
    const auto raw_flow = core::build_post_layout_control_flow(raw_read, flow_options);
    const auto raw = core::optimize_natural_target_component_layout(raw_read, preloads, raw_flow);
    require(raw.preloads.size() == preloads.size() &&
                raw.preloads[0].register_name == preloads[0].register_name &&
                raw.preloads[0].value == preloads[0].value,
            "ordinary data reads must keep the exact original preload");

    auto generated = preloads;
    generated[0].setup_expression = true;
    generated[0].setup_expression_text = "10.375";
    const auto generated_layout =
        core::optimize_natural_target_component_layout(items, generated, flow);
    require(generated_layout.applied == 0,
            "a computed setup expression is not a literal preload rebinding proof");

    auto mutated = items;
    mutated[1] = op(0x48);
    const auto mutated_flow = core::build_post_layout_control_flow(mutated, flow_options);
    const auto written =
        core::optimize_natural_target_component_layout(mutated, preloads, mutated_flow);
    require(written.applied == 0, "a runtime-written register is not a stable carrier");

    // The same projection after a call is not dead: the hardware writes the
    // truncated integer back even for R7..RE. This was the counterexample to
    // treating the Stable mutation class as preservation of the whole word.
    auto observed_after_call = items;
    std::rotate(observed_after_call.begin(), observed_after_call.begin() + 3,
                observed_after_call.begin() + 3 + 2 * calls);
    const auto observed_flow =
        core::build_post_layout_control_flow(observed_after_call, flow_options);
    const auto observed = core::optimize_natural_target_component_layout(
        observed_after_call, preloads, observed_flow);
    require(observed.applied == 0,
            "a selector's fractional data must not be consumed after its new writeback");
  }

  for (const std::string value : {"14.375", "1.4375014E-1"}) {
    emulator::MK61 calculator;
    std::vector<int> bytes{0xa8, 0x68, 0x50};
    bytes.resize(14, 0x50);
    bytes.push_back(0x52);
    require(calculator.load_program(bytes).diagnostics.empty(), "writeback fact must load");
    calculator.set_register("8", value);
    const auto before = calculator.read_register("8");
    calculator.press_sequence({"В/О", "С/П"});
    require(calculator.run_until_stable(1000, 8).stopped, "writeback fact must return");
    const auto decoded = core::evaluate_indirect_address(
        "8", value, core::IndirectOperationKind::Flow);
    require(decoded.has_value() && decoded->actual_flow_target == 14,
            "both source encodings must decode the same target");
    if (value == "14.375") {
      require(calculator.read_register("8") == "00000014," && decoded->result_value == "14",
              "ordinary fractional selector data must model the destructive integer writeback");
    } else {
      require(calculator.read_register("8") == before &&
                  decoded->result_value == "1.4375014e-1" &&
                  decoded->transformed == "14375014",
              "negative-order mantissa addressing must distinguish decoder bits and data word");
    }
  }
}

} // namespace mkpro::tests
