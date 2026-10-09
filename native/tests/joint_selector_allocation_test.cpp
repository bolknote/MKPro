#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/layout_search_artifact.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;

void require(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}
void op(std::vector<MachineItem>& items, int code) {
  items.push_back(MachineItem::op(code, "selector allocation fixture"));
}
std::vector<std::string> run(const std::vector<MachineItem>& items,
                             const std::vector<PreloadReport>& preloads,
                             const std::string& input = {}) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "fixture must resolve");
  std::vector<int> bytes;
  for (const auto& step : resolved.steps) bytes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(bytes).diagnostics.empty(), "fixture must fit stock ROM");
  for (const char* name : {"X", "Y", "Z", "T", "X1", "1", "2", "3", "4"})
    calc.set_register(name, "3.25");
  for (const auto& preload : preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(5000, 8).stopped, "fixture must stop");
  if (!input.empty()) {
    calc.press_sequence({"5", "ПП"});
    require(calc.run_until_stable(5000, 8).stopped, "first input phase must single-step");
    for (char digit : input)
      if (digit != '-') calc.press(std::string(1, digit));
    if (input.front() == '-') calc.press("/-/");
    calc.press("С/П");
    require(calc.run_until_stable(5000, 8).stopped, "numeric resume must return and stop");
  }
  std::vector<std::string> result;
  for (const char* name : {"X", "Y", "Z", "T", "X1", "1", "2", "3", "4"})
    result.push_back(calc.read_register(name));
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}

void released_address_home() {
  std::vector<MachineItem> items;
  for (int value : {2, 3}) {
    op(items, value);
    op(items, 0x53);
    items.push_back(MachineItem::address(std::string("twice")));
    op(items, 0x40 + value - 1);
  }
  op(items, 0x61); op(items, 0x62); op(items, 0x10);
  op(items, 0x50); items.back().stop_disposition = StopDisposition::Terminal;
  items.push_back(MachineItem::label("twice"));
  op(items, 2); op(items, 0x12); op(items, 0x52);
  const std::vector<PreloadReport> preloads{{.register_name = "7", .value = "99"}};
  auto flow = build_post_layout_control_flow(items);
  NaturalTargetComponentLayoutOptions allocation;
  allocation.reallocate_released_address_only_selectors = true;
  const auto after = optimize_natural_target_component_layout(items, preloads, flow, allocation);
  require(after.applied > 0 && after.removed_cells == 2 && after.plan.proved,
          "an unused address home must be freely assigned; baseline saves two cells");
  require(after.preloads.front().value != "99", "the released home must actually be rebound");
  auto eager = allocation;
  eager.eager_symbolic_flow_analysis = true;
  const auto eager_after = optimize_natural_target_component_layout(items, preloads, flow, eager);
  require(eager_after.applied == after.applied && eager_after.removed_cells == after.removed_cells &&
              same_layout_search_artifact(
                  LayoutSearchArtifact{after.items, after.preloads, {}},
                  LayoutSearchArtifact{eager_after.items, eager_after.preloads, {}}),
          "demand-driven X2 analysis must not change the candidate frontier or final artifact");
  require(run(items, preloads) == run(after.items, after.preloads),
          "global address-home assignment must preserve ROM stack, X1, X2 and data");

  auto observed = items;
  observed.insert(observed.begin(), {MachineItem::op(0x67, "R7"),
                                     MachineItem::op(0x44, "store R4")});
  require(optimize_natural_target_component_layout(observed, preloads,
      build_post_layout_control_flow(observed), allocation).applied == 0,
      "an observed preload is not a free address home");
  auto written = items;
  written.insert(written.begin(), MachineItem::op(0x47, "store R7"));
  require(optimize_natural_target_component_layout(written, preloads,
      build_post_layout_control_flow(written), allocation).applied == 0,
      "a written register is not an immutable address home");
}

void continuous_call_transport() {
  std::vector<MachineItem> direct;
  op(direct, 0);
  op(direct, 0x50);
  direct.back().stop_disposition = StopDisposition::Resumable;
  direct.back().manual_interaction = {7, -1, ManualInteractionAnchorKind::PromptStop};
  op(direct, 0x43);
  direct.back().manual_interaction = {7, 0, ManualInteractionAnchorKind::SingleStepCommand};
  const std::size_t call = direct.size();
  op(direct, 0x53);
  direct.back().manual_interaction = {7, 1, ManualInteractionAnchorKind::ContinuousResume};
  direct.push_back(MachineItem::address(std::string("twice")));
  op(direct, 0x41); op(direct, 0x62); op(direct, 0x61); op(direct, 0x10);
  op(direct, 0x50); direct.back().stop_disposition = StopDisposition::Terminal;
  direct.push_back(MachineItem::label("twice"));
  op(direct, 2); op(direct, 0x12); op(direct, 0x52);

  std::vector<MachineItem> indirect;
  std::vector<std::optional<std::size_t>> mapping(direct.size());
  for (std::size_t i = 0; i < direct.size(); ++i) {
    if (i == call + 1) continue;
    mapping[i] = indirect.size();
    auto item = direct[i];
    if (i == call) {
      item.opcode = 0xac;
      item.indirect_flow_targets = std::vector<IrTarget>{std::string("twice")};
    }
    indirect.push_back(std::move(item));
  }
  const PostLayoutExecutionRelocationOptions options{.direct_to_indirect_flow_items = {call}};
  const auto prove = [&](const auto& before, const auto& after) {
    const auto old_flow = build_post_layout_control_flow(before);
    const auto new_flow = build_post_layout_control_flow(after);
    return prove_post_layout_execution_relocation(
        before, after, old_flow, new_flow, mapping, options);
  };
  const auto proof = prove(direct, indirect);
  require(proof.proved, proof.reasons.empty() ? "typed continuous call must transport" :
          proof.reasons.front());
  const auto resolved = resolve_machine_items(indirect);
  require(resolved.diagnostics.empty(), "indirect resume fixture must resolve");
  const auto target = build_post_layout_control_flow(indirect).indirect_flow_targets.at(call);
  require(target.size() == 1, "resume call needs one exact target");
  const std::vector<PreloadReport> preloads{
      {.register_name = "c", .value = std::to_string(target.front().address)}};
  for (const std::string input : {"0", "13", "-7", "3.25"})
    require(run(direct, preloads, input) == run(indirect, preloads, input),
            "typed resume transport must preserve input, return continuation and hidden stack");

  auto wrong_phase = indirect;
  wrong_phase[call].manual_interaction->phase = 2;
  require(!prove(direct, wrong_phase).proved, "a changed input phase must reject");
  auto stepped_before = direct;
  auto stepped_after = indirect;
  stepped_before[call].manual_interaction->kind = ManualInteractionAnchorKind::SingleStepCommand;
  stepped_after[call].manual_interaction->kind = ManualInteractionAnchorKind::SingleStepCommand;
  require(!prove(stepped_before, stepped_after).proved, "single-step input must remain protected");
  auto untyped = indirect;
  untyped[call].manual_interaction.reset();
  require(!prove(direct, untyped).proved, "erasing a typed observation must reject");
  auto raw = indirect;
  raw[call].raw = true;
  require(!prove(direct, raw).proved, "raw transport must reject");
}
} // namespace

int main() {
  try {
    released_address_home();
    continuous_call_transport();
    std::cout << "joint selector allocation compiler and ROM contracts passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
