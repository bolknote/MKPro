#include "mkpro/core/natural_target_component_layout.hpp"

#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/emit/lowering/proc_raw_setup.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

MachineItem op(int opcode) {
  return MachineItem::op(opcode, opcode_by_code(opcode).name);
}

MachineItem stop() {
  MachineItem item = op(0x50);
  item.stop_disposition = StopDisposition::Terminal;
  return item;
}

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const MachineItem& item) {
    return item.kind != MachineItemKind::Label;
  }));
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
  throw std::runtime_error("missing synthetic address " + std::to_string(wanted));
}

struct Fixture {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  std::size_t visible_stop = 0;
  int selector_target = 34;
  std::string old_fractional_selector;
};

Fixture fixture(int calls, int padding_components, bool fixed_call_selector,
                int selector_target = 34) {
  require(calls >= 1 && padding_components >= 1,
          "synthetic fixture dimensions should be positive");
  Fixture result;
  result.selector_target = selector_target;
  const std::string helper = "opaque_leaf_" + std::to_string(calls);
  const std::string side_branch = "unrelated_side_" + std::to_string(calls);
  const std::string side_sink = "unrelated_sink_" + std::to_string(calls);

  result.items.push_back(MachineItem::label("opaque_entry_" + std::to_string(calls)));
  for (int call = 0; call < calls; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper));
  }
  result.items.push_back(op(0x6b)); // unrelated selector also has a data projection
  result.items.push_back(op(0x35));
  result.items.push_back(op(0x10));
  if (fixed_call_selector) {
    result.items.push_back(op(0x68)); // chosen call selector remains ordinary data
    result.items.push_back(op(0x35));
    result.items.push_back(op(0x10));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(helper));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label(side_branch));
  result.items.push_back(op(0x8b));
  result.items.back().indirect_flow_targets = std::vector<IrTarget>{side_sink};
  result.items.push_back(MachineItem::label(side_sink));
  result.items.push_back(stop());

  const int converted_main = calls + (fixed_call_selector ? 7 : 4);
  const int padding_cells = result.selector_target - converted_main - 2;
  require(padding_cells >= padding_components,
          "synthetic target should leave room for every padding component");
  int remaining = padding_cells;
  for (int component = 0; component < padding_components; ++component) {
    result.items.push_back(MachineItem::label(
        "opaque_padding_" + std::to_string(calls) + "_" + std::to_string(component)));
    const int components_left = padding_components - component;
    const int length = remaining / components_left;
    for (int cell = 1; cell < length; ++cell)
      result.items.push_back(op(0x0d));
    result.items.push_back(stop());
    remaining -= length;
  }
  require(remaining == 0, "synthetic padding partition should be exact");

  const int old_sink = 2 * calls + (fixed_call_selector ? 10 : 7);
  result.old_fractional_selector = std::to_string(old_sink) + ".375";
  result.preloads.push_back(PreloadReport{
      .register_name = "b",
      .value = result.old_fractional_selector,
  });
  if (fixed_call_selector) {
    result.preloads.push_back(PreloadReport{
        .register_name = "8",
        .value = std::to_string(result.selector_target),
    });
  }
  return result;
}

Fixture multi_anchor_fixture() {
  Fixture result;
  const std::string helper_a = "unrelated_multi_helper_a";
  const std::string helper_b = "unrelated_multi_helper_b";

  result.items.push_back(MachineItem::label("unrelated_multi_entry"));
  for (int call = 0; call < 2; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_a));
  }
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_b));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(helper_a));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label(helper_b));
  result.items.push_back(op(0x23));
  result.items.push_back(op(0x24));
  result.items.push_back(op(0x52));

  const auto append_padding = [&](const std::string& name, int length) {
    result.items.push_back(MachineItem::label(name));
    for (int cell = 1; cell < length; ++cell)
      result.items.push_back(op(0x0d));
    result.items.push_back(stop());
  };
  append_padding("unrelated_multi_padding_14", 14);
  append_padding("unrelated_multi_padding_8", 8);

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "20"});
  result.preloads.push_back(PreloadReport{.register_name = "9", .value = "30"});
  return result;
}

Fixture paid_alignment_fixture() {
  Fixture result;
  const std::string helper = "unrelated_paid_alignment_helper";

  result.items.push_back(MachineItem::label("unrelated_paid_alignment_entry"));
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(helper));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label("unrelated_paid_alignment_padding"));
  for (int cell = 1; cell < 15; ++cell)
    result.items.push_back(op(0x0d));
  result.items.push_back(stop());

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "20"});
  return result;
}

Fixture overlapping_fixed_targets_fixture() {
  Fixture result;
  const std::string helper_a = "unrelated_overlap_helper_a";
  const std::string helper_b = "unrelated_overlap_helper_b";

  result.items.push_back(MachineItem::label("unrelated_overlap_entry"));
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_a));
  }
  for (int call = 0; call < 4; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_b));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  const auto append_helper = [&](const std::string& name, int first_opcode) {
    result.items.push_back(MachineItem::label(name));
    result.items.push_back(op(first_opcode));
    for (int cell = 0; cell < 8; ++cell)
      result.items.push_back(op(0x54));
    result.items.push_back(op(0x52));
  };
  append_helper(helper_a, 0x22);
  append_helper(helper_b, 0x23);

  const auto append_padding = [&](const std::string& name, int length) {
    result.items.push_back(MachineItem::label(name));
    for (int cell = 1; cell < length; ++cell)
      result.items.push_back(op(0x54));
    result.items.push_back(stop());
  };
  append_padding("unrelated_overlap_padding_12", 12);
  append_padding("unrelated_overlap_padding_2", 2);

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "20"});
  result.preloads.push_back(PreloadReport{.register_name = "9", .value = "22"});
  return result;
}

Fixture split_overlapping_fixed_targets_fixture() {
  Fixture result;
  const std::string helper_a = "unrelated_split_helper_a";
  const std::string helper_b = "unrelated_split_helper_b";

  result.items.push_back(MachineItem::label("unrelated_split_entry"));
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_a));
  }
  for (int call = 0; call < 4; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_b));
  }
  for (int cell = 0; cell < 14; ++cell)
    result.items.push_back(op(0x54));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  const auto append_helper = [&](const std::string& name, int first_opcode, int length) {
    result.items.push_back(MachineItem::label(name));
    result.items.push_back(op(first_opcode));
    for (int cell = 2; cell < length; ++cell)
      result.items.push_back(op(0x54));
    result.items.push_back(op(0x52));
  };
  append_helper(helper_a, 0x22, 10);
  append_helper(helper_b, 0x23, 7);

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "29"});
  result.preloads.push_back(PreloadReport{.register_name = "9", .value = "34"});
  return result;
}

Fixture split_internal_fixed_target_fixture() {
  Fixture result;
  const std::string helper_a = "unrelated_internal_split_helper_a";
  const std::string helper_b = "unrelated_internal_split_helper_b";

  result.items.push_back(MachineItem::label("unrelated_internal_split_entry"));
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_a));
  }
  for (int call = 0; call < 4; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_b));
  }
  for (int cell = 0; cell < 12; ++cell)
    result.items.push_back(op(0x54));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(op(0x54));
  result.items.push_back(op(0x54));
  result.items.push_back(MachineItem::label(helper_a));
  result.items.push_back(op(0x22));
  for (int cell = 2; cell < 10; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label(helper_b));
  result.items.push_back(op(0x23));
  for (int cell = 2; cell < 7; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(op(0x52));

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "29"});
  result.preloads.push_back(PreloadReport{.register_name = "9", .value = "34"});
  return result;
}

Fixture shared_call_and_tail_jump_fixture() {
  Fixture result;
  const std::string helper = "unrelated_shared_flow_helper";

  result.items.push_back(MachineItem::label("unrelated_shared_flow_entry"));
  for (int call = 0; call < 2; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label("unrelated_shared_flow_tail"));
  result.items.push_back(op(0x51));
  result.items.push_back(MachineItem::address(helper));

  result.items.push_back(MachineItem::label(helper));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label("unrelated_shared_flow_padding"));
  for (int cell = 1; cell < 16; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(stop());

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "20"});
  return result;
}

Fixture shared_conditional_fixture() {
  Fixture result;
  const std::string guard_sink = "unrelated_shared_conditional_guard_sink";
  const std::string sink = "unrelated_shared_conditional_sink";

  result.items.push_back(MachineItem::label("unrelated_shared_conditional_entry"));
  result.items.push_back(op(0x59));
  result.items.push_back(MachineItem::address(guard_sink));
  result.items.push_back(op(0x5e));
  result.items.push_back(MachineItem::address(sink));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(guard_sink));
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(sink));
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label("unrelated_shared_conditional_padding"));
  for (int cell = 1; cell < 16; ++cell)
    result.items.push_back(op(0x0d));
  result.items.push_back(stop());

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "20"});
  return result;
}

Fixture address_selector_rebind_fixture() {
  Fixture result;
  const std::string helper = "unrelated_rebound_address_helper";
  const std::string sink = "unrelated_rebound_address_sink";

  result.items.push_back(MachineItem::label("unrelated_rebound_address_entry"));
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper));
  }
  MachineItem old_indirect_jump = op(0x8e);
  old_indirect_jump.indirect_flow_targets = std::vector<IrTarget>{sink};
  result.items.push_back(std::move(old_indirect_jump));

  result.items.push_back(MachineItem::label(helper));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label(sink));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());
  result.preloads.push_back(PreloadReport{.register_name = "e", .value = "9"});
  return result;
}

core::AuthoritativePostLayoutControlFlow flow(const Fixture& fixture_value) {
  return core::build_post_layout_control_flow(fixture_value.items);
}

std::string trim_ascii(std::string text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    text.erase(text.begin());
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    text.pop_back();
  return text;
}

std::string canonical_register(std::string text) {
  text = trim_ascii(std::move(text));
  const std::size_t sign = !text.empty() && text.front() == '-' ? 1U : 0U;
  while (text.size() > sign + 1U && text.at(sign) == '0' &&
         std::isdigit(static_cast<unsigned char>(text.at(sign + 1U))) != 0) {
    text.erase(sign, 1U);
  }
  return text;
}

std::map<std::string, std::string> preload_map(const std::vector<PreloadReport>& preloads) {
  std::map<std::string, std::string> result;
  for (const PreloadReport& preload : preloads)
    result.emplace(preload.register_name, preload.value);
  return result;
}

struct Observation {
  bool stopped = false;
  std::map<std::string, std::string> state;
};

Observation observe(const std::vector<MachineItem>& items,
                    const std::vector<PreloadReport>& preloads,
                    bool generated_setup = false) {
  const ResolvedProgram resolved = resolve_machine_items(items, {});
  require(resolved.diagnostics.empty(), "synthetic layout should resolve to official opcodes");
  std::vector<int> codes;
  for (const ResolvedStep& step : resolved.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  if (generated_setup) {
    const SetupProgramReport setup =
        core::emit::lowering::compile_setup_program_with_preloads(
            {}, {}, preloads, CompileOptions{});
    std::vector<int> setup_codes;
    for (const ResolvedStep& step : setup.steps)
      setup_codes.push_back(step.opcode);
    require(calc.load_program(setup_codes).diagnostics.empty(),
            "generated synthetic setup should load in the emulator");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(1000, 6).stopped,
            "generated synthetic setup should stop");
  }
  require(calc.load_program(codes).diagnostics.empty(),
          "synthetic layout should load in the emulator");
  calc.set_register("x", "2");
  calc.set_register("y", "0");
  calc.set_register("z", "0");
  calc.set_register("t", "0");
  calc.set_register("x1", "0");
  for (const PreloadReport& preload : preloads) {
    if (generated_setup)
      continue;
    calc.set_register(preload.register_name, preload.value);
  }
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(1000, 6);
  Observation result{.stopped = run.stopped};
  for (const std::string& slot : {"x", "y", "z", "t", "x1", "8", "9"})
    result.state.emplace(slot, canonical_register(calc.read_register(slot)));
  return result;
}

bool reason_contains(const core::NaturalTargetComponentLayoutPlan& plan,
                     const std::string& fragment) {
  return std::any_of(plan.reasons.begin(), plan.reasons.end(), [&](const std::string& reason) {
    return reason.find(fragment) != std::string::npos;
  });
}

} // namespace

void natural_target_component_layout_is_generic_and_proof_gated() {
  {
    const Fixture input = shared_conditional_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const int converted_conditionals = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.original_opcode == 0x5e;
        }));
    std::string rejection;
    for (const std::string& reason : rewritten.plan.reasons) {
      if (!rejection.empty())
        rejection += " | ";
      rejection += reason;
    }
    require(rewritten.plan.proved && rewritten.applied == 1 &&
                rewritten.removed_cells == 1 && converted_conditionals == 1 &&
                rewritten.items.at(item_at_address(rewritten.items, 2)).opcode == 0xe8 &&
                rewritten.items.at(item_at_address(rewritten.items, 20)).opcode == 0x50,
            "compatible conditionals should share one proved natural-target selector: applied=" +
                std::to_string(rewritten.applied) +
                ", removed=" + std::to_string(rewritten.removed_cells) +
                ", reasons=" + rejection);
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "conditional flow conversion must preserve observable machine state");
  }

  {
    const Fixture input = shared_call_and_tail_jump_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const int converted_calls = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.original_opcode == 0x53;
        }));
    const int converted_jumps = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.original_opcode == 0x51;
        }));
    require(rewritten.plan.proved && rewritten.applied == 3 &&
                rewritten.removed_cells == 3 && converted_calls == 2 &&
                converted_jumps == 1 &&
                rewritten.items.at(item_at_address(rewritten.items, 0)).opcode == 0xa8 &&
                rewritten.items.at(item_at_address(rewritten.items, 1)).opcode == 0xa8 &&
                rewritten.items.at(item_at_address(rewritten.items, 3)).opcode == 0x88 &&
                rewritten.items.at(item_at_address(rewritten.items, 20)).opcode == 0x22,
            "calls and a tail jump should share one proved natural-target selector");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "mixed direct-flow conversion must preserve observable machine state");
  }

  {
    const Fixture input = split_internal_fixed_target_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.plan.proved && rewritten.applied == 7 &&
                rewritten.removed_cells == 5 &&
                rewritten.plan.transparent_trampolines == 0 &&
                rewritten.plan.transparent_split_bridges == 1 &&
                rewritten.items.at(item_at_address(rewritten.items, 32)).opcode == 0x51 &&
                rewritten.items.at(item_at_address(rewritten.items, 33)).kind ==
                    MachineItemKind::Address &&
                rewritten.items.at(item_at_address(rewritten.items, 34)).kind ==
                    MachineItemKind::Op,
            "a fixed target inside a fallthrough component should admit a proved split");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "an internal-target split must preserve observable machine state");
  }

  {
    const Fixture input = split_overlapping_fixed_targets_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.plan.proved && rewritten.applied == 7 &&
                rewritten.removed_cells == 5 &&
                rewritten.plan.transparent_trampolines == 0 &&
                rewritten.plan.transparent_split_bridges == 1 &&
                rewritten.items.at(item_at_address(rewritten.items, 32)).opcode == 0x51 &&
                rewritten.items.at(item_at_address(rewritten.items, 33)).kind ==
                    MachineItemKind::Address &&
                rewritten.items.at(item_at_address(rewritten.items, 34)).kind ==
                    MachineItemKind::Op,
            "a helper suffix should fill the gap before two overlapping fixed targets");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "transparent helper split must preserve observable machine state");
  }

  {
    const Fixture input = overlapping_fixed_targets_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.plan.proved && rewritten.applied == 7 &&
                rewritten.removed_cells == 5 &&
                rewritten.plan.transparent_trampolines == 1 &&
                rewritten.items.at(item_at_address(rewritten.items, 20)).opcode == 0x51 &&
                rewritten.items.at(item_at_address(rewritten.items, 21)).kind ==
                    MachineItemKind::Address &&
                rewritten.items.at(item_at_address(rewritten.items, 22)).kind ==
                    MachineItemKind::Op,
            "overlapping fixed targets should share a profitable proved jump trampoline");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "transparent natural-target trampoline must preserve observable machine state");
  }

  {
    const Fixture input = address_selector_rebind_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const std::map<std::string, std::string> final_preloads =
        preload_map(rewritten.preloads);
    require(rewritten.plan.proved && rewritten.applied == 3 &&
                rewritten.removed_cells == 2 &&
                rewritten.plan.rebound_indirect_flows == 1 &&
                final_preloads.at("e") == "5" &&
                rewritten.items.at(item_at_address(rewritten.items, 3)).opcode == 0x51 &&
                rewritten.items.at(item_at_address(rewritten.items, 4)).kind ==
                    MachineItemKind::Address &&
                rewritten.items.at(item_at_address(rewritten.items, 5)).opcode == 0x22,
            "a stable address-only selector should move from one flow to three calls");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "address-only selector reassignment must preserve observable machine state");

    Fixture data_visible = address_selector_rebind_fixture();
    const auto old_jump = std::find_if(
        data_visible.items.begin(), data_visible.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Op && item.opcode == 0x8e;
        });
    require(old_jump != data_visible.items.end(),
            "address-only fixture should expose its old indirect jump");
    data_visible.items.insert(old_jump, op(0x6e));
    data_visible.preloads.front().value = "10";
    const auto rejected = core::optimize_natural_target_component_layout(
        data_visible.items, data_visible.preloads, flow(data_visible));
    require(rejected.plan.rebound_indirect_flows == 0,
            "a selector with an ordinary data recall must not be reassigned");
  }

  {
    const Fixture input = paid_alignment_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.plan.proved && rewritten.applied == 3 &&
                rewritten.removed_cells == 2 &&
                cell_count(rewritten.items) == cell_count(input.items) - 2 &&
                rewritten.items.at(item_at_address(rewritten.items, 19)).opcode == 0x54 &&
                rewritten.items.at(item_at_address(rewritten.items, 20)).opcode == 0x22,
            "a profitable natural target may spend one unreachable alignment cell");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "paid natural-target alignment must preserve observable machine state");
  }

  {
    const Fixture input = multi_anchor_fixture();
    const core::AuthoritativePostLayoutControlFlow input_flow = flow(input);
    require(input_flow.proved, "multi-anchor synthetic fixture should have an exact CFG");
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, input_flow);
    const int calls_through_8 = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.selector_register == "8" && flow.target_address == 20;
        }));
    const int calls_through_9 = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.selector_register == "9" && flow.target_address == 30;
        }));
    require(rewritten.plan.proved && rewritten.applied == 5 &&
                rewritten.removed_cells == 5 && calls_through_8 == 2 &&
                calls_through_9 == 3 &&
                rewritten.items.at(item_at_address(rewritten.items, 20)).opcode == 0x22 &&
                rewritten.items.at(item_at_address(rewritten.items, 30)).opcode == 0x23,
            "independent constants should place and prove two unrelated helpers jointly");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "joint natural-target layout must preserve the observable machine state");
  }

  for (const auto [calls, components] :
       {std::pair{2, 3}, std::pair{3, 5}, std::pair{5, 6}}) {
    const Fixture input = fixture(calls, components, true);
    const core::AuthoritativePostLayoutControlFlow input_flow = flow(input);
    require(input_flow.proved, "unrelated synthetic fixture should have an exact CFG");
    const core::NaturalTargetComponentLayoutResult rewritten =
        core::optimize_natural_target_component_layout(
            input.items, input.preloads, input_flow);
    require(rewritten.plan.proved && rewritten.removed_cells == calls &&
                rewritten.applied == calls &&
                rewritten.plan.selector_origin ==
                    core::NaturalTargetSelectorOrigin::ExistingPreload &&
                rewritten.plan.selector_register == "8" &&
                rewritten.plan.natural_target == input.selector_target &&
                rewritten.plan.flows.size() == static_cast<std::size_t>(calls) &&
                cell_count(rewritten.items) == cell_count(input.items) - calls,
            "arbitrary call count and component count should use the same exact planner: calls=" +
                std::to_string(calls) + " applied=" + std::to_string(rewritten.applied) +
                " removed=" + std::to_string(rewritten.removed_cells) +
                " reason=" + (rewritten.plan.reasons.empty()
                                    ? std::string("none")
                                    : rewritten.plan.reasons.front()));
    require(rewritten.plan.control_flow_equivalent &&
                rewritten.plan.call_return_equivalent &&
                rewritten.plan.stack_and_x2_equivalent &&
                rewritten.plan.indirect_memory_equivalent &&
                rewritten.plan.data_projection_equivalent &&
                rewritten.plan.final_control_flow.proved,
            "all independent proof obligations should be retained in the result");
    require(rewritten.plan.runtime_selectors.size() ==
                static_cast<std::size_t>(calls + 1) &&
                std::all_of(
                    rewritten.plan.runtime_selectors.begin(),
                    rewritten.plan.runtime_selectors.end(),
                    [](const core::NaturalTargetRuntimeSelectorProof& proof) {
                      return proof.stable_mutation_class && proof.selector_unwritten &&
                             proof.typed_target_matches_runtime_decode &&
                             proof.decoded_target == proof.final_target_address;
                    }) &&
                static_cast<int>(std::count_if(
                    rewritten.plan.runtime_selectors.begin(),
                    rewritten.plan.runtime_selectors.end(),
                    [&](const core::NaturalTargetRuntimeSelectorProof& proof) {
                      return proof.register_name == "8" &&
                             proof.delivered_preload ==
                                 std::to_string(input.selector_target) &&
                             proof.decoded_target == input.selector_target;
                    })) == calls,
            "every typed selector must independently decode from its delivered preload");
    const std::map<std::string, std::string> final_preloads =
        preload_map(rewritten.preloads);
    require(final_preloads.at("8") == std::to_string(input.selector_target),
            "stable natural selector must retain its exact ordinary data value");
    require(final_preloads.at("b") != input.old_fractional_selector &&
                final_preloads.at("b").ends_with(".375") &&
                std::any_of(rewritten.plan.preloads.begin(), rewritten.plan.preloads.end(),
                            [](const core::NaturalTargetPreloadRewrite& preload) {
                              return preload.register_name == "b" &&
                                     preload.fractional_projection_only;
                            }),
            "relocated indirect flow should rebind only the proved-dead integer projection");

    const std::size_t helper_item = item_at_address(rewritten.items, input.selector_target);
    require(rewritten.items.at(helper_item).opcode == 0x22,
            "callee identity should land exactly on the selector's natural address");
    require(static_cast<int>(std::count_if(
                rewritten.items.begin(), rewritten.items.end(), [](const MachineItem& item) {
                  return item.kind == MachineItemKind::Op && item.opcode == 0xa8;
                })) == calls,
            "every equivalent direct call should become one stable indirect call");
  }

  {
    const Fixture input = fixture(2, 4, true);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "emulator must preserve X/Y/Z/T, hidden X2, and the data use of R8: before=" +
                before.state.at("x") + "/" + before.state.at("x1") + "/" +
                before.state.at("8") + " after=" + after.state.at("x") + "/" +
                after.state.at("x1") + "/" + after.state.at("8"));
  }

  {
    Fixture input = fixture(2, 5, true, 76);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "8")
        preload.value = "0.41200076";
    }
    const auto selector_recall =
        std::find_if(input.items.begin(), input.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Op && item.opcode == 0x68;
        });
    require(selector_recall != input.items.end(),
            "fractional-selector fixture should contain an ordinary data recall");
    const std::size_t recall_index =
        static_cast<std::size_t>(std::distance(input.items.begin(), selector_recall));
    require(recall_index + 1U < input.items.size() &&
                input.items.at(recall_index + 1U).opcode == 0x35,
            "fractional-selector fixture should expose the expected projection use");
    input.items.at(recall_index + 1U) = op(0x12);

    const core::AuthoritativePostLayoutControlFlow input_flow = flow(input);
    require(input_flow.proved,
            "fractional-selector fixture should have an exact original CFG");
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, input_flow);
    const std::map<std::string, std::string> final_preloads =
        preload_map(rewritten.preloads);
    require(rewritten.plan.proved && rewritten.applied == 2 &&
                rewritten.plan.selector_register == "8" &&
                rewritten.plan.natural_target == 76 &&
                final_preloads.at("8") == "4.1200076E-1",
            "a hidden fractional constant should retain its numeric value while its canonical "
            "BCD entry supplies a proved natural call target: " +
                (rewritten.plan.reasons.empty() ? std::string("no reason")
                                                : rewritten.plan.reasons.front()));
    const Observation before = observe(input.items, input.preloads, true);
    const Observation after = observe(rewritten.items, rewritten.preloads, true);
    require(before.stopped && after.stopped && before.state == after.state,
            "fractional exponent selector must preserve its numeric data projection: before=" +
                before.state.at("x") + "/R8=" + before.state.at("8") + " after=" +
                after.state.at("x") + "/R8=" + after.state.at("8"));
  }

  {
    Fixture input = fixture(2, 3, true);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "8")
        preload.register_name = "6";
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0 &&
                reason_contains(rejected.plan, "no stable selector"),
            "pre-increment and pre-decrement selector classes must fail closed");
  }

  {
    const Fixture input = fixture(2, 3, true);
    core::AuthoritativePostLayoutControlFlow forged = flow(input);
    forged.proved = false;
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, forged);
    require(!rejected.plan.proved &&
                reason_contains(rejected.plan, "not authoritative"),
            "an unproved input CFG must never authorize layout mutation");
  }

  {
    Fixture input = fixture(2, 3, true);
    const int helper_address = 2 * 2 + 7;
    for (MachineItem& item : input.items) {
      if (item.kind == MachineItemKind::Address) {
        item.formal_opcode = official_address_to_opcode(helper_address);
        break;
      }
    }
    const core::AuthoritativePostLayoutControlFlow input_flow = flow(input);
    require(input_flow.proved,
            "ordinary formal-address fixture should have a valid original CFG");
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, input_flow);
    require(rewritten.plan.proved && rewritten.applied == 2 &&
                rewritten.removed_cells == 2,
            "wrapped ordinary formal address operands should follow their proved label identity");
  }

  {
    Fixture input = fixture(2, 3, true);
    const auto helper = std::find_if(input.items.begin(), input.items.end(),
                                     [](const MachineItem& item) {
                                       return item.kind == MachineItemKind::Label &&
                                              item.name.starts_with("opaque_leaf_");
                                     });
    require(helper != input.items.end(), "overflow fixture should contain its helper label");
    const std::size_t helper_index =
        static_cast<std::size_t>(std::distance(input.items.begin(), helper));
    require(helper_index + 2U < input.items.size(),
            "overflow fixture helper should contain a command and return");
    std::vector<MachineItem> moved_helper(
        input.items.begin() + static_cast<std::ptrdiff_t>(helper_index),
        input.items.begin() + static_cast<std::ptrdiff_t>(helper_index + 3U));
    input.items.erase(input.items.begin() + static_cast<std::ptrdiff_t>(helper_index),
                      input.items.begin() + static_cast<std::ptrdiff_t>(helper_index + 3U));
    const std::vector<MachineItem> old_helper_replacement{
        MachineItem::label("old_helper_replacement"), op(0x0d), stop()};
    input.items.insert(input.items.begin() + static_cast<std::ptrdiff_t>(helper_index),
                       old_helper_replacement.begin(), old_helper_replacement.end());
    std::vector<MachineItem> overflow_padding;
    overflow_padding.push_back(MachineItem::label("overflow_padding"));
    for (int cell = 0; cell < 69; ++cell)
      overflow_padding.push_back(op(0x0d));
    overflow_padding.push_back(stop());
    input.items.insert(input.items.end(), overflow_padding.begin(), overflow_padding.end());
    input.items.insert(input.items.end(), moved_helper.begin(), moved_helper.end());
    const int moved_helper_address = cell_count(input.items) - 2;
    for (MachineItem& item : input.items) {
      if (item.kind == MachineItemKind::Address &&
          std::holds_alternative<std::string>(item.target) &&
          std::get<std::string>(item.target).starts_with("opaque_leaf_")) {
        item.target = moved_helper_address;
        item.formal_opcode = 0x85;
      }
    }
    const core::AuthoritativePostLayoutControlFlow input_flow = flow(input);
    require(input_flow.proved,
            "wrapped over-window fixture should still have a physical input CFG");
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, input_flow);
    require(rewritten.plan.proved && rewritten.applied == 2 &&
                rewritten.removed_cells == 2 && rewritten.plan.natural_target == 34,
            "over-window calls should follow normalized label identities into proved layout: " +
                (rewritten.plan.reasons.empty() ? std::string("no reason")
                                                : rewritten.plan.reasons.front()) +
                "; applied=" + std::to_string(rewritten.applied));
  }

  {
    Fixture input = fixture(2, 3, true);
    for (MachineItem& item : input.items) {
      if (item.kind == MachineItemKind::Address) {
        item.roles.push_back("opaque-extra-contract");
        break;
      }
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0,
            "an address operand with an opaque secondary contract must fail closed");
  }

  {
    Fixture input = fixture(2, 3, true);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "8") {
        preload.setup_expression = true;
        preload.setup_expression_text = "opaque_runtime_expression()";
        preload.setup_source_line = 71;
      }
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0,
            "a computed setup value must not be trusted as a literal call selector");
  }

  {
    Fixture input = fixture(2, 3, true);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "b") {
        preload.setup_expression = true;
        preload.setup_expression_text = "another_opaque_expression()";
      }
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0,
            "a computed setup selector must not be rebound by replacing only report.value");
  }

  {
    Fixture input = fixture(2, 3, true);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "8")
        preload.setup_target_name = "opaque_generated_setup_target";
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0,
            "a preload owned by generated setup must fail closed conservatively");
  }

  {
    Fixture input = fixture(1, 3, true);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "b")
        preload.value = "12.12345678";
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0,
            "fractional projection must fail closed when relocating a selector changes the "
            "number of integer digits and therefore MK-61 mantissa precision");
  }

  {
    Fixture input = fixture(2, 5, true, 89);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "8")
        preload.value = "123456789";
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0,
            "runtime selector proof must reject decimal preload text whose MK-61 mantissa "
            "canonicalization changes the indirect target");
  }
}

} // namespace mkpro::tests

#ifdef MKPRO_STANDALONE_NATURAL_TARGET_COMPONENT_LAYOUT_TEST
int main() {
  try {
    mkpro::tests::natural_target_component_layout_is_generic_and_proof_gated();
    std::cout << "[PASS] natural_target_component_layout_is_generic_and_proof_gated\n";
  } catch (const std::exception& error) {
    std::cerr << "[FAIL] " << error.what() << '\n';
    return 1;
  }
  return 0;
}
#endif
