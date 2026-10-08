#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

enum class Charge { Literal, ThroughCall, Unknown, Raw, Negative, ThreeDigits, Contradiction };

std::vector<MachineItem> fixture(int selector, int selected, int extent,
                                 Charge charge = Charge::Literal) {
  std::vector<MachineItem> items;
  int address = 0;
  const auto emit = [&](int code) {
    items.push_back(MachineItem::op(code, "selector domain fixture"));
    ++address;
  };
  const auto pad = [&](int target) {
    require(address <= target, "fixture regions must not overlap");
    while (address < target)
      emit(0x54);
  };
  emit(0x40); // Establish a fresh numeric-entry context.
  if (charge == Charge::Unknown) {
    emit(0x64);
  } else {
    if (charge == Charge::ThreeDigits)
      emit(1);
    const int value = charge == Charge::Contradiction ? 55 : selected;
    emit(value / 10);
    emit(value % 10);
    if (charge == Charge::Raw)
      items.back().raw = true;
    if (charge == Charge::Negative)
      emit(0x0b);
  }
  if (charge == Charge::ThroughCall) {
    emit(0x53);
    items.push_back(MachineItem::address(std::string("receive")));
    ++address;
  } else {
    emit(0x40 + selector);
  }
  emit(0xa0 + selector);
  items.back().indirect_flow_targets = std::vector<IrTarget>{
      std::string("first"), std::string("second")};
  emit(0x6c);
  emit(0x10);
  emit(0x50);
  items.back().stop_disposition = StopDisposition::Terminal;
  pad(24);
  items.push_back(MachineItem::label("first"));
  emit(0x61);
  emit(0x52);
  pad(38);
  items.push_back(MachineItem::label("second"));
  emit(0x62);
  emit(0x52);
  pad(46);
  items.push_back(MachineItem::label("receive"));
  emit(0x40 + selector);
  emit(0x52);
  pad(extent);
  return items;
}

std::set<int> selected_callbacks(const std::vector<MachineItem>& items,
                                 const AuthoritativePostLayoutControlFlow& flow,
                                 const SelectorConsistentExecutionGraph& graph) {
  std::set<int> addresses;
  for (std::size_t i = 0; i < graph.original_states.size(); ++i) {
    const auto& source = flow.execution_states[graph.original_states[i]];
    if ((items[source.item_index].opcode & 0xf0) != 0xa0)
      continue;
    for (const auto next : graph.successors[i])
      addresses.insert(flow.execution_states[graph.original_states[next]].address);
  }
  return addresses;
}

void compiler_domains() {
  for (const auto model : {AddressSpaceModel::Standard,
                           AddressSpaceModel::Mk61SMiniExpanded}) {
    const int limit = official_program_step_limit(model);
    for (const int extent : {48, limit, limit + 1, limit + 31}) {
      for (const int selector : {7, 10, 14}) {
        for (const int selected : {24, 38}) {
          for (const auto charge : {Charge::Literal, Charge::ThroughCall}) {
            const auto items = fixture(selector, selected, extent, charge);
            const auto flow = build_post_layout_control_flow(
                items, {.address_space_model = model});
            require(flow.proved, "both address domains need exact call/return contexts");
            const auto graph = build_selector_consistent_execution_graph(items, flow);
            require(graph.proved && graph.pruned_edges > 0,
                    "a proved literal must refine physical and oversized logical calls");
            require(selected_callbacks(items, flow, graph) == std::set<int>{selected},
                    "refinement must retain the selected callee, not prune both alternatives");
            bool returned = false;
            for (std::size_t i = 0; i < graph.original_states.size(); ++i) {
              const auto original = graph.original_states[i];
              require(flow.execution_successors[original].empty() ||
                          !graph.successors[i].empty(),
                      "a reachable nonterminal instruction cannot become a terminal node");
              returned = returned ||
                  items[flow.execution_states[original].item_index].opcode == 0x50;
            }
            require(returned, "the selected callback must return to the observable stop");
            const auto repeated = build_selector_consistent_execution_graph(items, flow);
            require(graph.original_states == repeated.original_states &&
                        graph.successors == repeated.successors,
                    "logical-domain refinement must be deterministic");
          }
        }
      }
    }
  }
}

void negative_contracts() {
  for (const int extent : {48, 136}) {
    for (const auto charge : {Charge::Unknown, Charge::Raw, Charge::Negative,
                              Charge::ThreeDigits}) {
      const auto items = fixture(10, 24, extent, charge);
      const auto flow = build_post_layout_control_flow(items);
      const auto graph = build_selector_consistent_execution_graph(items, flow);
      require(graph.proved && selected_callbacks(items, flow, graph) == std::set<int>{24, 38},
              "unsupported numeric producers must retain both target alternatives");
    }
    const auto contradictory = fixture(10, 24, extent, Charge::Contradiction);
    const auto flow = build_post_layout_control_flow(contradictory);
    require(flow.proved, "the negative fixture needs a structurally complete CFG");
    const auto graph = build_selector_consistent_execution_graph(contradictory, flow);
    require(!graph.proved && graph.original_states.empty() && graph.successors.empty(),
            "inconsistent value/target claims must not publish a partial graph");
    const auto capped = build_selector_consistent_execution_graph(contradictory, flow, 1);
    require(!capped.proved && capped.original_states.empty() && capped.successors.empty(),
            "exhaustion must also discard the partial refinement");
  }
}

void rom_selected_target_and_return() {
  for (const int selector : {7, 10, 14}) {
    for (const int selected : {24, 38}) {
      for (const auto charge : {Charge::Literal, Charge::ThroughCall}) {
        const auto compact = fixture(selector, selected, 48, charge);
        const auto logical = fixture(selector, selected, 136, charge);
        const auto flow = build_post_layout_control_flow(logical);
        const auto graph = build_selector_consistent_execution_graph(logical, flow);
        require(graph.proved && selected_callbacks(logical, flow, graph) == std::set<int>{selected},
                "ROM target must agree with the oversized logical-domain proof");
        const auto resolved = resolve_machine_items(compact);
        require(resolved.diagnostics.empty(), "the executable prefix must resolve");
        std::vector<int> codes;
        for (const auto& step : resolved.steps)
          codes.push_back(step.opcode);
        emulator::MK61 calc;
        require(calc.load_program(codes).diagnostics.empty(), "the ROM prefix must fit");
        calc.set_register("1", "4").set_register("2", "90").set_register("c", "7");
        calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
        require(calc.run_until_stable(4000, 8).stopped, "the selected callback must return");
        require(std::stoi(calc.display_text()) == (selected == 24 ? 11 : 97),
                "ROM must execute the retained target and then its caller continuation");
      }
    }
  }
}
} // namespace

int main() {
  try {
    compiler_domains();
    negative_contracts();
    rom_selected_target_and_return();
    std::cout << "selector literal address domains: OK\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
