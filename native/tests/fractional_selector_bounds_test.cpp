#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/emulator/mk61.hpp"

#include <array>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace mkpro::tests {

void fractional_selector_bounds_do_not_require_zero_target() {
  const auto require = [](bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
  };
  const auto op = [](int code) {
    return MachineItem::op(code, opcode_by_code(code).name);
  };
  std::vector<MachineItem> items;
  auto fixed_call = op(0xac);
  fixed_call.indirect_flow_targets = std::vector<IrTarget>{"helper2"};
  items.push_back(fixed_call);
  auto fractional_call = op(0xab);
  fractional_call.indirect_flow_targets = std::vector<IrTarget>{"helper3"};
  items.push_back(fractional_call);
  for (int helper : {0, 1, 4, 5, 6}) {
    items.push_back(op(0x53));
    items.push_back(MachineItem::address("helper" + std::to_string(helper)));
  }
  auto stop = op(0x50);
  stop.stop_disposition = StopDisposition::Terminal;
  items.push_back(stop);
  constexpr std::array<int, 7> lengths{8, 18, 14, 17, 10, 4, 19};
  std::array<std::size_t, 7> heads{};
  for (std::size_t helper = 0; helper < lengths.size(); ++helper) {
    items.push_back(MachineItem::label("helper" + std::to_string(helper)));
    heads[helper] = items.size();
    int used = 0;
    if (helper == 3) {
      // The integer projection is 4 throughout the certified fractional
      // family. The final exact addition overwrites the discarded fractional
      // intermediate before the helper returns.
      items.push_back(op(3));
      auto recall = op(0x6b);
      recall.roles.push_back("retunable-natural-fractional-selector:0.226000");
      items.push_back(recall);
      for (int code : {0x12, 0x15, 0x34, 0x0d, 0x00, 0x10}) items.push_back(op(code));
      used = 8;
    }
    while (++used < lengths[helper]) items.push_back(op(0x54));
    items.push_back(op(0x52));
  }
  PreloadReport fractional;
  fractional.register_name = "b";
  fractional.value = "2.2600053E-1";
  fractional.retunable_natural_fractional_prefix = "0.226000";
  PreloadReport fixed;
  fixed.register_name = "c";
  fixed.value = "40";
  const auto flow = core::build_post_layout_control_flow(items);
  require(flow.proved, "fractional-bound fixture lacks authoritative flow");
  require(!core::rebind_stable_preloaded_indirect_flow_selector(
              items, fractional, flow, 53, 0),
          "the zero probe must be unrepresentable in this fractional family");
  require(core::rebind_stable_preloaded_indirect_flow_selector(
              items, fractional, flow, 53, 1).has_value(),
          "a failed zero probe must not imply a fixed selector");

  core::NaturalTargetComponentLayoutOptions options;
  options.allow_size_neutral_absolute_layout = true;
  options.require_size_neutral_absolute_layout = true;
  options.required_absolute_targets.push_back({heads[2], 40});
  options.deferred_selector_reconciliations.push_back({0, heads[2], 40});
  const auto layout = core::optimize_natural_target_component_layout(
      items, {fractional, fixed}, flow, options);
  require(layout.applied > 0 && layout.plan.proved &&
              layout.plan.final_artifact_proved && layout.plan.bounded_targets_proved &&
              layout.plan.deferred_selector_reconciliations_proved,
          "fractional selector reconciliation did not preserve all proofs");
  require(layout.plan.bounded_targets == 1,
          "a retunable fractional selector was omitted after its zero probe failed");

  auto generated = fractional;
  generated.setup_expression = true;
  require(!core::rebind_stable_preloaded_indirect_flow_selector(
              items, generated, flow, 53, 1),
          "a generated setup value must not acquire literal rebinding rights");
  auto uncertified = fractional;
  uncertified.retunable_natural_fractional_prefix.reset();
  require(!core::rebind_stable_preloaded_indirect_flow_selector(
              items, uncertified, flow, 53, 1),
          "ordinary data reads must retain their projection certificate");

  const auto codes = [](const std::vector<MachineItem>& program) {
    std::map<std::string, int> labels;
    int address = 0;
    for (const auto& item : program) {
      if (item.kind == MachineItemKind::Label) labels[item.name] = address;
      else ++address;
    }
    std::vector<int> output;
    for (const auto& item : program) {
      if (item.kind == MachineItemKind::Label) continue;
      if (item.kind == MachineItemKind::Op) { output.push_back(item.opcode); continue; }
      const int target = std::holds_alternative<int>(item.target)
                             ? std::get<int>(item.target)
                             : labels.at(std::get<std::string>(item.target));
      output.push_back(item.formal_opcode.value_or(address_to_opcode(target)));
    }
    return output;
  };
  emulator::MK61 original, optimized;
  require(original.load_program(codes(items)).diagnostics.empty() &&
              optimized.load_program(codes(layout.items)).diagnostics.empty(),
          "fractional-bound fixture did not load into the emulator");
  original.set_register("b", fractional.value).set_register("c", "39");
  for (const auto& preload : layout.preloads)
    optimized.set_register(preload.register_name, preload.value);
  // Loading different selector literals must not leave different setup-time
  // stack contents as inputs to the program being compared.
  for (const char* reg : {"T", "Z", "Y", "X1", "X"}) {
    original.set_register(reg, "0");
    optimized.set_register(reg, "0");
  }
  original.press_sequence({"В/О", "С/П"});
  optimized.press_sequence({"В/О", "С/П"});
  require(original.run_until_stable(4000, 8).stopped &&
              optimized.run_until_stable(4000, 8).stopped,
          "fractional-bound fixture did not stop");
  require(original.display_text() == optimized.display_text() &&
              original.program_counter() == optimized.program_counter(),
          "fractional-bound layout changed the observable stop");
  for (const char* reg : {"X", "Y", "Z", "T", "X1"}) {
    const auto before = original.read_register(reg);
    const auto after = optimized.read_register(reg);
    if (before != after)
      throw std::runtime_error(std::string("fractional-bound stack ") + reg +
                               " differs: " + before + " vs " + after);
  }
  original.press(".");
  optimized.press(".");
  require(original.display_text() == optimized.display_text(),
          "fractional-bound layout changed the restored X2 value");
}

} // namespace mkpro::tests
