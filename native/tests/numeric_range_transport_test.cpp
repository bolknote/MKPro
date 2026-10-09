#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

void append(std::vector<MachineItem>& items, std::initializer_list<int> codes) {
  for (const int code : codes)
    items.push_back(MachineItem::op(code, "numeric range contract"));
}
void target(std::vector<MachineItem>& items, int code, const std::string& label) {
  append(items, {code});
  items.push_back(MachineItem::address(label));
}
void stop(std::vector<MachineItem>& items, StopDisposition disposition) {
  append(items, {0x50});
  items.back().stop_disposition = disposition;
}
std::vector<int> bytecode(const std::vector<MachineItem>& items) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "range fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps)
    codes.push_back(step.opcode);
  return codes;
}
PostLayoutNumericRangeProof ranges(
    const std::vector<MachineItem>& items, const std::vector<std::size_t>& requested,
    const std::vector<PreloadReport>& preloads,
    NumericRangeInputPolicy policy = NumericRangeInputPolicy::ValidatedRegisterTransport) {
  const auto flow = build_post_layout_control_flow(items);
  require(flow.proved, "range fixture needs an authoritative CFG");
  const auto lift = analyze_post_layout_automatic_entry_lift(items, flow, 100000);
  require(lift.has_value(), "range fixture needs entry-lift facts");
  return prove_post_layout_numeric_ranges(items, flow, *lift, requested, &preloads,
                                          100000, policy);
}
double numeric(std::string value) {
  std::replace(value.begin(), value.end(), ',', '.');
  std::istringstream input(value);
  double mantissa = 0;
  require(static_cast<bool>(input >> mantissa), "ROM value must have a decimal mantissa");
  int exponent = 0;
  if (input >> exponent)
    mantissa *= std::pow(10.0, exponent);
  return mantissa;
}

void scientific_and_fractional_contracts() {
  for (const auto* word : {"4.1200076E-1", "0.41200076", "1e-99", "+1E-1"})
    require(bound_ordinary_decimal(word) == OrdinaryNumericRange{0, 1, false},
            "scientific runtime words need an outward ordinary enclosure");
  require(bound_ordinary_decimal("-1.25e+1") == OrdinaryNumericRange{-13, -12, false} &&
              bound_ordinary_decimal("5E2") == OrdinaryNumericRange{500, 500, true},
          "scientific sign and scale are part of the value");
  for (const auto* word : {"1e200", "1e100", "1e-100", "1E", "1E-", "1E1.0",
                           "-0E2", "A-2", "1.00000001", "1E8"})
    require(!bound_ordinary_decimal(word), "raw, oversized and malformed words must reject");

  struct Arithmetic {
    const char* y;
    const char* x;
    int opcode;
    OrdinaryNumericRange expected;
  };
  for (const auto& row : {
      Arithmetic{"1.25", "2.5", 0x10, {3, 5, false}},
      Arithmetic{"1.25", "2.5", 0x11, {-2, 0, false}},
      Arithmetic{"-1.25", "2.5", 0x12, {-6, -2, false}},
      Arithmetic{"1", "3", 0x13, {0, 1, false}},
      Arithmetic{"-7", "2", 0x13, {-4, -3, false}},
      Arithmetic{"1", "0.5", 0x13, {2, 2, true}},
      Arithmetic{"6", "2", 0x13, {3, 3, true}}}) {
    std::vector<MachineItem> items;
    append(items, {0x68, 0x69, row.opcode});
    const auto query = items.size();
    stop(items, StopDisposition::Terminal);
    const std::vector<PreloadReport> preloads{
        {.register_name = "8", .value = row.y},
        {.register_name = "9", .value = row.x}};
    const auto proof = ranges(items, {query}, preloads);
    require(proof.x_before.at(query) == row.expected, "fractional interval transfer mismatch");
    emulator::MK61 calc;
    require(calc.load_program(bytecode(items)).diagnostics.empty(), "fractional ROM load");
    for (const auto& preload : preloads)
      calc.set_register(preload.register_name, preload.value);
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(1000, 8).stopped, "fractional ROM must stop");
    const double actual = numeric(calc.read_register("X"));
    require(actual >= static_cast<double>(row.expected.lower) &&
                actual <= static_cast<double>(row.expected.upper),
            "integer enclosure must contain the actual rounded ROM result: " +
                std::string(row.y) + " opcode=" + std::to_string(row.opcode) + " " +
                row.x + " -> " + calc.read_register("X"));
  }
  for (const auto* denominator : {"0", "A-2"}) {
    std::vector<MachineItem> items;
    append(items, {0x68, 0x69, 0x13});
    const auto query = items.size();
    stop(items, StopDisposition::Terminal);
    const std::vector<PreloadReport> preloads{
        {.register_name = "8", .value = "1"},
        {.register_name = "9", .value = denominator}};
    require(ranges(items, {query}, preloads).x_before.empty(),
            "division with an enclosure containing zero or raw data must stay unknown");
  }

  // Unlike a known fractional divisor, this joined value really may be zero.
  std::vector<MachineItem> uncertain;
  append(uncertain, {0x6b});
  target(uncertain, 0x57, "zero");
  append(uncertain, {0x68, 0x49});
  target(uncertain, 0x51, "divide");
  uncertain.push_back(MachineItem::label("zero"));
  append(uncertain, {0x6a, 0x49});
  uncertain.push_back(MachineItem::label("divide"));
  append(uncertain, {0x6c, 0x69, 0x13});
  const auto query = uncertain.size();
  stop(uncertain, StopDisposition::Terminal);
  const std::vector<PreloadReport> preloads{
      {.register_name = "8", .value = "0.5"},
      {.register_name = "a", .value = "0"},
      {.register_name = "c", .value = "1"}};
  require(ranges(uncertain, {query}, preloads).x_before.empty(),
          "a joined denominator containing a real zero must remain unproved");

}

void fl_rom_and_range_contracts() {
  constexpr std::array<int, 4> loops{0x5d, 0x5b, 0x58, 0x5a};
  struct Row { const char* value; double next; bool branch; bool admitted; };
  for (int reg = 0; reg < 4; ++reg)
    for (const auto& row : {
        Row{"-2", -99999991, true, false}, Row{"0", -99999999, true, false},
        Row{"0.5", -99999999, true, false}, Row{"1.5", 1.5, false, false},
        Row{"2.5", 1, true, false}, Row{"1", 1, false, true},
        Row{"2", 1, true, true}, Row{"4", 3, true, true}}) {
      const int opcode = loops[static_cast<std::size_t>(reg)];
      emulator::MK61 calc;
      require(calc.load_program({opcode, 6, 2, 0x50, 0x54, 0x54, 3, 0x50})
                  .diagnostics.empty(), "FL ROM load");
      calc.set_register(std::to_string(reg), row.value);
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(1000, 8).stopped, "FL ROM must stop");
      require(numeric(calc.read_register(std::to_string(reg))) == row.next &&
                  numeric(calc.read_register("X")) == (row.branch ? 3 : 2),
              "FL is not ordinary subtraction outside positive integral counters");

      std::vector<MachineItem> items;
      target(items, opcode, "taken");
      append(items, {0x60 + reg});
      const auto fallthrough = items.size();
      stop(items, StopDisposition::Terminal);
      items.push_back(MachineItem::label("taken"));
      append(items, {0x60 + reg});
      const auto taken = items.size();
      stop(items, StopDisposition::Terminal);
      const std::vector<PreloadReport> preloads{
          {.register_name = std::to_string(reg), .value = row.value}};
      const auto proof = ranges(items, {fallthrough, taken}, preloads);
      if (row.admitted) {
        const auto query = row.branch ? taken : fallthrough;
        const auto other = row.branch ? fallthrough : taken;
        const auto expected = static_cast<std::int64_t>(row.next);
        require(proof.x_before.at(query) == OrdinaryNumericRange{expected, expected, true},
                "FL edge must refine and transform its counter");
        require(!proof.x_before.contains(other), "infeasible FL edge must not export a fact");
      } else {
        require(proof.x_before.empty(), "nonintegral/nonpositive FL domain must reject");
      }
    }

  // The body has a real backedge, not a bounded unrolling assumption.
  for (int reg = 0; reg < 4; ++reg) {
    std::vector<MachineItem> items;
    append(items, {0x45, 4, 0x40 + reg});
    items.push_back(MachineItem::label("body"));
    append(items, {0x60 + reg});
    const auto query = items.size();
    append(items, {0x49});
    target(items, loops[static_cast<std::size_t>(reg)], "body");
    append(items, {0x60 + reg});
    const auto final = items.size();
    stop(items, StopDisposition::Terminal);
    const auto proof = ranges(items, {query, final}, {});
    require(!proof.exhausted && proof.x_before.at(query) == OrdinaryNumericRange{1, 4, true} &&
                proof.x_before.at(final) == OrdinaryNumericRange{1, 1, true},
            "FL range must converge around a real positive countdown");
  }
}

void input_context_contracts() {
  for (const bool error : {false, true})
    for (const bool typed : {false, true}) {
      if (error && typed)
        continue;
      std::vector<MachineItem> items;
      append(items, {0x45, 3, 0x49});
      target(items, 0x53, "input");
      append(items, {0x69});
      const auto first = items.size();
      append(items, {0x48, 6, 0x49});
      target(items, 0x53, "input");
      append(items, {0x69});
      const auto second = items.size();
      append(items, {0x4a});
      stop(items, StopDisposition::Terminal);
      items.push_back(MachineItem::label("input"));
      append(items, {error ? 0x29 : 0x50});
      items.back().stop_disposition = StopDisposition::Resumable;
      if (typed)
        items.back().manual_interaction =
            ManualInteractionAnchor{27, -1, ManualInteractionAnchorKind::PromptStop};
      if (error)
        append(items, {0x49}); // This write is skipped on error continuation.
      const auto entered = items.size();
      append(items, {0x41});
      if (typed) {
        items.back().manual_interaction =
            ManualInteractionAnchor{27, 0, ManualInteractionAnchorKind::SingleStepCommand};
        append(items, {0x42});
        items.back().manual_interaction =
            ManualInteractionAnchor{27, 1, ManualInteractionAnchorKind::ContinuousResume};
      }
      append(items, {0x52});
      const std::vector<PreloadReport> preloads{{.register_name = "9", .value = "81"}};
      const auto proof = ranges(items, {first, second, entered}, preloads);
      require(proof.x_before.at(first) == OrdinaryNumericRange{3, 3, true} &&
                  proof.x_before.at(second) == OrdinaryNumericRange{6, 6, true} &&
                  !proof.x_before.contains(entered),
              "input must inherit exact predecessor registers, never setup values or input X");
      require(ranges(items, {first, second}, preloads, NumericRangeInputPolicy::Forget)
                  .x_before.empty(), "input transport is explicit, not an implicit assumption");
      const auto graph = build_post_layout_control_flow(items);
      const auto lift = analyze_post_layout_automatic_entry_lift(items, graph, 100000);
      require(lift.has_value(), "input fixture lift");
      auto damaged = graph;
      damaged.execution_edges.clear();
      require(!prove_post_layout_input_boundaries(items, damaged).proved &&
                  prove_post_layout_numeric_ranges(items, damaged, *lift, {first}, &preloads,
                      100000, NumericRangeInputPolicy::ValidatedRegisterTransport).x_before.empty(),
              "missing labelled input provenance must fail closed");
      damaged = graph;
      for (auto& edges : damaged.execution_edges)
        for (auto& edge : edges)
          if (edge.kind == PostLayoutExecutionEdgeKind::Resume)
            edge.kind = PostLayoutExecutionEdgeKind::Fallthrough;
      require(!prove_post_layout_input_boundaries(items, damaged).proved,
              "an ordinary edge must not impersonate a resume");

      emulator::MK61 calc;
      require(calc.load_program(bytecode(items)).diagnostics.empty(), "input ROM load");
      calc.set_register("9", "81");
      calc.press_sequence({"В/О", "С/П"});
      for (int call = 0; call < 2; ++call) {
        require(calc.run_until_stable(2000, 8).stopped, "input ROM prompt");
        calc.input_number("7");
        if (typed) {
          calc.press("ПП");
          calc.input_number("8");
        }
        calc.press("С/П");
      }
      require(calc.run_until_stable(2000, 8).stopped &&
                  numeric(calc.read_register("8")) == 3 &&
                  numeric(calc.read_register("a")) == 6,
              "ROM must confirm caller-specific register transport across input");
    }
}

void scientific_selector_spelling() {
  std::vector<MachineItem> items;
  append(items, {0x45});
  items.push_back(MachineItem::label("body"));
  append(items, {0x68});
  const auto query = items.size();
  append(items, {0x49, 0xa8});
  items.back().indirect_flow_targets = std::vector<IrTarget>{"leaf"};
  stop(items, StopDisposition::Resumable);
  target(items, 0x51, "body");
  for (int address = 7; address < 20; ++address)
    append(items, {0x54});
  items.push_back(MachineItem::label("leaf"));
  append(items, {0x52});
  const std::vector<PreloadReport> preloads{
      {.register_name = "8", .value = "1.0000020E-1"}};
  const auto proof = ranges(items, {query}, preloads);
  require(proof.x_before.at(query) == OrdinaryNumericRange{0, 1, false},
          "selector write-back must not lose an unchanged word because E became e");
  emulator::MK61 calc;
  require(calc.load_program(bytecode(items)).diagnostics.empty(), "scientific selector ROM load");
  calc.set_register("8", preloads.front().value);
  calc.press_sequence({"В/О", "С/П"});
  for (int iteration = 0; iteration < 3; ++iteration) {
    require(calc.run_until_stable(2000, 8).stopped,
            "scientific selector must return to its exact caller");
    require(calc.read_register("8") == calc.read_register("9"),
            "scientific selector write-back must preserve the actual machine word");
    if (iteration < 2)
      calc.press("С/П");
  }
}

void selector_edges_and_weak_stores() {
  std::vector<MachineItem> items;
  append(items, {0x45, 1, 4, 0x47});
  for (int call = 0; call < 2; ++call) {
    append(items, {0xa7});
    items.back().indirect_flow_targets = std::vector<IrTarget>{"leaf", "other"};
  }
  stop(items, StopDisposition::Terminal);
  append(items, {0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54});
  items.push_back(MachineItem::label("leaf"));
  append(items, {0x52});
  items.push_back(MachineItem::label("other"));
  append(items, {0x52});
  const auto flow = build_post_layout_control_flow(items);
  const auto product = build_selector_consistent_execution_graph(items, flow);
  require(product.proved && product.pruned_edges != 0 &&
              product.execution_edges.size() == product.original_states.size(),
          "selector refinement must retain a labelled product graph");
  for (std::size_t state = 0; state < product.original_states.size(); ++state)
    for (const auto& edge : product.execution_edges[state]) {
      auto original_edge = edge;
      original_edge.target_state = product.original_states.at(edge.target_state);
      const auto& original = flow.execution_edges.at(product.original_states[state]);
      require(std::find(original.begin(), original.end(), original_edge) != original.end(),
              "product edges must retain original kind and formal selector counter");
    }
  auto refined = flow;
  refined.execution_states.clear();
  for (const auto original : product.original_states)
    refined.execution_states.push_back(flow.execution_states[original]);
  refined.execution_successors = product.successors;
  refined.execution_edges = product.execution_edges;
  require(prove_post_layout_input_boundaries(items, refined).proved,
          "shared root validation must accept the refined labelled graph");

  items.clear();
  append(items, {0x45, 3, 0xb0});
  items.back().indirect_memory_targets = std::vector<int>{8, 9};
  append(items, {0x68});
  const auto first = items.size();
  append(items, {0x4a, 0x69});
  const auto second = items.size();
  stop(items, StopDisposition::Terminal);
  const std::vector<PreloadReport> preloads{
      {.register_name = "0", .value = "9"},
      {.register_name = "8", .value = "10"},
      {.register_name = "9", .value = "20"}};
  const auto proof = ranges(items, {first, second}, preloads);
  require(proof.x_before.at(first) == OrdinaryNumericRange{3, 10, true} &&
              proof.x_before.at(second) == OrdinaryNumericRange{3, 20, true},
          "a may-alias store must join the old value and the written value");
}
} // namespace

int main() {
  try {
    scientific_and_fractional_contracts();
    fl_rom_and_range_contracts();
    input_context_contracts();
    scientific_selector_spelling();
    selector_edges_and_weak_stores();
    std::cout << "numeric range transport and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
