#include "mkpro/core/layout_deadline_packing.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

std::optional<int> brute_force(
    const std::vector<DeadlinePackingComponent>& components,
    const std::vector<DeadlinePackingGap>& gaps, int trailing, int padding_limit) {
  std::optional<int> best;
  std::vector<int> assignment(components.size(), -1);
  const auto evaluate = [&] {
    int padding = 0;
    for (std::size_t group = 0; group <= gaps.size(); ++group) {
      int cursor = group == gaps.size() ? trailing : gaps[group].start;
      std::vector<std::size_t> order;
      for (std::size_t i = 0; i < components.size(); ++i)
        if (assignment[i] == (group == gaps.size() ? -1 : static_cast<int>(group)))
          order.push_back(i);
      // Exhaustive permutations are an independent oracle, not another EDF implementation.
      bool feasible = false;
      do {
        int address = cursor;
        bool valid = true;
        for (const auto i : order) {
          if (components[i].latest_start && address > *components[i].latest_start)
            valid = false;
          address += components[i].cells;
        }
        if (group < gaps.size() && address > gaps[group].start + gaps[group].cells)
          valid = false;
        if (valid) {
          feasible = true;
          if (group < gaps.size())
            padding += gaps[group].start + gaps[group].cells - address;
          break;
        }
      } while (std::next_permutation(order.begin(), order.end()));
      if (!feasible)
        return;
    }
    if (padding <= padding_limit && (!best || padding < *best))
      best = padding;
  };
  std::function<void(std::size_t)> assign = [&](std::size_t index) {
    if (index == components.size()) {
      evaluate();
      return;
    }
    for (int group = -1; group < static_cast<int>(gaps.size()); ++group) {
      assignment[index] = group;
      assign(index + 1U);
    }
  };
  assign(0);
  return best;
}

std::vector<MachineItem> fixture(int length, int offset, int short_length) {
  MachineEmitter emitter;
  emitter.emit_op(0x60);
  emitter.emit_jump(0x5e, "condition", "short");
  emitter.emit_jump(0x51, "branch", "long");
  emitter.emit_label("long");
  for (int i = 0; i < length - 1; ++i) {
    if (i == offset)
      emitter.emit_label("interior");
    emitter.emit_op(0x54);
  }
  emitter.emit_stop(StopDisposition::Terminal);
  emitter.emit_label("short");
  for (int i = 1; i < short_length; ++i)
    emitter.emit_op(0x54);
  emitter.emit_stop(StopDisposition::Terminal);
  return emitter.items;
}

std::vector<int> bytes(const std::vector<MachineItem>& items) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "deadline fixture must resolve");
  std::vector<int> result;
  for (const auto& step : resolved.steps)
    result.push_back(step.opcode);
  return result;
}

std::vector<std::string> observe(const std::vector<MachineItem>& items,
                                  const std::string& input) {
  emulator::MK61 calc;
  require(calc.load_program(bytes(items)).diagnostics.empty(), "fixture must fit ROM");
  for (const char* reg : {"X", "Y", "Z", "T", "X1"})
    calc.set_register(reg, "3.25");
  calc.set_register("0", input);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(2000, 8).stopped, "both relocated branches must stop");
  std::vector<std::string> state;
  for (const char* reg : {"X", "Y", "Z", "T", "X1", "0"})
    state.push_back(calc.read_register(reg));
  calc.press(".");
  state.push_back(calc.display_text(true));
  return state;
}

void run_tests() {
  // Same total fills do not imply the same remaining deadline capacity.
  for (int seed = 0; seed < 180; ++seed) {
    std::vector<DeadlinePackingComponent> components;
    for (int i = 0; i < 4; ++i)
      components.push_back({1 + ((seed / (i + 1) + 3 * i) % 5),
          ((seed + i) % 4 == 0) ? std::nullopt :
              std::optional<int>{(seed * (i + 2) + i) % 15}});
    const std::vector<DeadlinePackingGap> gaps{{0, 5}, {7, 4}};
    const auto expected = brute_force(components, gaps, 13, 9);
    const auto actual = pack_components_with_deadlines(components, gaps, 13, 9);
    require(!actual.exhausted && actual.proved == expected.has_value() &&
                (!expected || actual.padding == *expected),
            "Pareto packing must match exhaustive assignment/permutation search");
    const auto repeated = pack_components_with_deadlines(components, gaps, 13, 9);
    require(actual.assignment == repeated.assignment && actual.work == repeated.work,
            "deadline search must be deterministic");
  }
  const std::vector<DeadlinePackingComponent> components{{20, 5}, {1, 10}};
  const auto exact = pack_components_with_deadlines(components, {}, 1, 0);
  require(exact.proved && !exact.exhausted && exact.padding == 0,
          "shorter later-start-deadline component must precede the long one");
  require(pack_components_with_deadlines(components, {}, 1, 0, 0).exhausted,
          "zero frontier budget must report exhaustion, not infeasibility");
  require(pack_components_with_deadlines(components, {}, 1, 0, 20, 1).exhausted,
          "scalar work budget must bound dominance and extension work");
  require(!pack_components_with_deadlines({{20, 0}, {1, 0}}, {}, 1, 0).proved,
          "impossible deadlines must fail closed");
  require(!pack_components_with_deadlines({{0, 1}}, {}, 1, 0).proved,
          "malformed component sizes must fail closed");

  for (const int length : {13, 20, 29})
    for (const int offset : {3, 5, 7})
      for (const int short_length : {1, 2, 3}) {
        const auto input = fixture(length, offset, short_length);
        const auto flow = build_post_layout_control_flow(input);
        require(flow.proved, "synthetic input must have authoritative CFG");
        NaturalTargetComponentLayoutOptions options;
        options.required_bounded_target_labels = {"interior", "short"};
        options.maximum_bounded_target_address = 5 + offset + short_length;
        options.allow_size_neutral_bounded_layout = true;
        const auto output = optimize_natural_target_component_layout(input, {}, flow, options);
        require(output.plan.proved && output.plan.final_artifact_proved &&
                    output.plan.bounded_targets_proved &&
                    bytes(output.items).size() == bytes(input).size(),
                "bounded interior entries require completion-deadline scheduling");
        if (length == 20 && offset == 5 && short_length == 1)
          for (const std::string value : {"0", "1", "-2", "123.45"})
            require(observe(input, value) == observe(output.items, value),
                    "relocation must preserve both ROM branches, stack, X1 and X2");
        options.maximum_bounded_target_address = 4 + offset;
        const auto rejected = optimize_natural_target_component_layout(input, {}, flow, options);
        require(!rejected.plan.proved, "a target before its earliest possible position must reject");
      }
}
} // namespace

int main() {
  try {
    run_tests();
    std::cout << "[PASS] deadline-constrained component packing\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "[FAIL] " << error.what() << '\n';
    return 1;
  }
}

