#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
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
    items.push_back(MachineItem::op(code, "quantized range contract"));
}
void target(std::vector<MachineItem>& items, int code, const std::string& label) {
  append(items, {code});
  items.push_back(MachineItem::address(label));
}
void stop(std::vector<MachineItem>& items) {
  append(items, {0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
}
std::vector<int> bytecode(const std::vector<MachineItem>& items) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "quantized fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps)
    codes.push_back(step.opcode);
  return codes;
}
PostLayoutNumericRangeProof ranges(
    const std::vector<MachineItem>& items, const std::vector<std::size_t>& requested,
    const std::vector<PreloadReport>& preloads = {}) {
  const auto flow = build_post_layout_control_flow(items);
  require(flow.proved, "quantized fixture needs an authoritative CFG");
  const auto lift = analyze_post_layout_automatic_entry_lift(items, flow, 100000);
  require(lift.has_value(), "quantized fixture needs entry-lift facts");
  return prove_post_layout_numeric_ranges(
      items, flow, *lift, requested, &preloads, 100000,
      NumericRangeInputPolicy::ValidatedRegisterTransport);
}
double number(std::string value) {
  std::replace(value.begin(), value.end(), ',', '.');
  std::istringstream stream(value);
  double mantissa = 0;
  int exponent = 0;
  require(static_cast<bool>(stream >> mantissa), "ROM must return an ordinary number: " + value);
  if (stream >> exponent)
    mantissa *= std::pow(10.0, exponent);
  return mantissa;
}
double execute(const std::vector<MachineItem>& items,
               const std::vector<PreloadReport>& preloads) {
  emulator::MK61 calc;
  require(calc.load_program(bytecode(items)).diagnostics.empty(), "quantized ROM load");
  for (const auto& p : preloads)
    calc.set_register(p.register_name, p.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(12000, 8).stopped, "quantized ROM must stop");
  return number(calc.read_register("X"));
}


void recall_entry_lift_contracts() {
  constexpr std::array<const char*, 15> names{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  const std::vector<std::vector<int>> prefixes{
      {}, {0x0d}, {0x0e}, {1, 0x0a, 2}, {1, 0x0c, 2}};
  const std::array<double, 5> old_x{11, 0, 11, 1.2, 100};
  const std::array<double, 5> old_y{13, 13, 11, 13, 13};
  for (std::size_t reg = 0; reg < names.size(); ++reg)
    for (std::size_t prefix = 0; prefix < prefixes.size(); ++prefix)
      for (const char* word : {"7", "-2.5", "0"}) {
        std::vector<MachineItem> items;
        for (const int code : prefixes[prefix])
          append(items, {code});
        append(items, {0x60 + static_cast<int>(reg), 3, 0x14});
        const auto query = items.size();
        stop(items);
        const std::vector<PreloadReport> preloads{
            {.register_name = names[reg], .value = word}};
        require(ranges(items, {query}, preloads).x_before.at(query) ==
                    *bound_ordinary_decimal(word),
                "direct recall must certify the next fresh digit's lift in every register");
        emulator::MK61 calc;
        require(calc.load_program(bytecode(items)).diagnostics.empty(), "recall ROM load");
        calc.set_register("X", "11");
        calc.set_register("Y", "13");
        calc.set_register("Z", "17");
        calc.set_register("T", "19");
        calc.set_register(names[reg], word);
        calc.press_sequence({"В/О", "С/П"});
        require(calc.run_until_stable(2000, 8).stopped, "recall ROM must stop");
        require(number(calc.read_register("X")) == std::stod(word) &&
                    number(calc.read_register("Y")) == 3 &&
                    number(calc.read_register("Z")) == old_x[prefix] &&
                    number(calc.read_register("T")) == old_y[prefix],
                "recall and new entry must lift twice, independent of prior entry mode");
      }

  // Input can poison entry state, but a subsequent ordinary recall closes it.
  std::vector<MachineItem> items;
  append(items, {0x0d, 0x50});
  items.back().stop_disposition = StopDisposition::Resumable;
  append(items, {0x68, 3, 0x14});
  const auto query = items.size();
  stop(items);
  const std::vector<PreloadReport> preloads{{.register_name = "8", .value = "7"}};
  require(ranges(items, {query}, preloads).x_before.at(query) ==
              OrdinaryNumericRange{7, 7, true},
          "recall after resumed input must establish a new entry context");
  emulator::MK61 calc;
  require(calc.load_program(bytecode(items)).diagnostics.empty(), "resume recall ROM load");
  calc.set_register("8", "7");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(2000, 8).stopped, "resume recall prompt");
  calc.input_number("99");
  calc.press("С/П");
  require(calc.run_until_stable(2000, 8).stopped &&
              number(calc.read_register("X")) == 7 &&
              number(calc.read_register("Y")) == 3 &&
              number(calc.read_register("Z")) == 99,
          "the new lift must preserve entered input beneath the recalled value");
}

void normalized_countdown_intervals() {
  for (const int width : {2, 4, 5, 8, 10})
    for (const int offset : {0, 4, 6}) {
      std::vector<MachineItem> items;
      append(items, {0x0d, 4, 0x40});
      items.push_back(MachineItem::label("body"));
      append(items, {0x60});
      if (offset != 0)
        append(items, {0x6c, 0x11});
      append(items, {0x34, 0x68, 0x13, 0x35, 1, 0x11, 0x35, 1, 0x10, 0x68, 0x12});
      const auto query = items.size();
      append(items, {0x49});
      target(items, 0x5d, "body");
      append(items, {0x69});
      stop(items);
      const std::vector<PreloadReport> preloads{
          {.register_name = "8", .value = std::to_string(width)},
          {.register_name = "c", .value = std::to_string(offset)}};
      const auto proof = ranges(items, {query}, preloads);
      require(!proof.exhausted && proof.x_before.contains(query),
              "a finite countdown must retain its exact decimal lattice: width=" +
                  std::to_string(width) + " offset=" + std::to_string(offset) +
                  " work=" + std::to_string(proof.work) + " exhausted=" + std::to_string(proof.exhausted));
      const auto bound = proof.x_before.at(query);
      require(bound.integral && bound.lower >= 1 && bound.upper <= width,
              "normalization must prove a positive integral result for any admitted width");
      const auto actual = execute(items, preloads);
      const int last = 1 - offset;
      const int expected = ((last - 1) % width + width) % width + 1;
      require(actual == expected && actual >= static_cast<double>(bound.lower) &&
                  actual <= static_cast<double>(bound.upper),
              "ROM must confirm signed remainder and one-based endpoint behavior");
    }
}

void joins_calls_and_aliases() {
  for (const bool alias_unknown : {false, true}) {
    std::vector<MachineItem> items;
    append(items, {0x6b});
    target(items, 0x57, "right");
    append(items, {0x68, 0x49});
    target(items, 0x51, "joined");
    items.push_back(MachineItem::label("right"));
    append(items, {0x6a, 0x49});
    items.push_back(MachineItem::label("joined"));
    if (alias_unknown) {
      append(items, {0x6b, 0xb0});
      items.back().indirect_memory_targets = std::vector<int>{9};
    }
    target(items, 0x53, "scale");
    const auto query = items.size();
    stop(items);
    items.push_back(MachineItem::label("scale"));
    append(items, {0x69, 0x6d, 0x12, 0x52});
    const std::vector<PreloadReport> preloads{
        {.register_name = "0", .value = "10"},
        {.register_name = "8", .value = "0.25"},
        {.register_name = "a", .value = "0.5"},
        {.register_name = "d", .value = "4"}};
    const auto proof = ranges(items, {query}, preloads);
    if (alias_unknown) {
      require(!proof.x_before.contains(query), "unknown alias must poison the lattice");
    } else {
      require(proof.x_before.at(query) == OrdinaryNumericRange{1, 2, true},
              "different denominators must join before the shared caller-specific transfer");
      for (const char* input : {"0", "7"}) {
        auto actual_preloads = preloads;
        actual_preloads.push_back({.register_name = "b", .value = input});
        const auto actual = execute(items, actual_preloads);
        require(actual == 1 || actual == 2, "both joined ROM paths must stay integral");
      }
    }
  }
}

void rounding_and_unsupported_domains() {
  struct Row {
    const char* numerator;
    const char* denominator;
    bool integral;
    double expected;
  };
  for (const auto& row : {
      Row{"7", "4", true, 7},
      Row{"-7", "4", true, -7},
      Row{"3", "8", true, 3},
      Row{"9999999", "5", true, 9999999},
      Row{"1", "3", false, 0.99999999},
      Row{"40000001", "4", false, 40000000}}) {
    std::vector<MachineItem> items;
    append(items, {0x68, 0x69, 0x13, 0x69, 0x12});
    const auto query = items.size();
    stop(items);
    const std::vector<PreloadReport> preloads{
        {.register_name = "8", .value = row.numerator},
        {.register_name = "9", .value = row.denominator}};
    const auto proof = ranges(items, {query}, preloads);
    require(proof.x_before.contains(query) &&
                proof.x_before.at(query).integral == row.integral,
            "finite exact decimal division is required, not algebraic cancellation");
    const auto actual = execute(items, preloads);
    require(std::abs(actual - row.expected) < 1e-10,
            "ROM must expose eight-digit rounding before multiplication: " +
                std::string(row.numerator) + "/" + row.denominator +
                " actual=" + std::to_string(actual));
    const auto bound = proof.x_before.at(query);
    require(actual >= static_cast<double>(bound.lower) &&
                actual <= static_cast<double>(bound.upper),
            "rounded ROM value must remain inside the exported enclosure");
  }

  for (const char* numerator : {"A-2", "-0", "1e200"}) {
    std::vector<MachineItem> items;
    append(items, {0x68, 4, 0x13, 0x35, 4, 0x12});
    const auto query = items.size();
    stop(items);
    const std::vector<PreloadReport> preloads{
        {.register_name = "8", .value = numerator}};
    require(ranges(items, {query}, preloads).x_before.empty(),
            "a lattice may not turn raw or unsupported words into ordinary values");
  }

  std::vector<MachineItem> items;
  append(items, {0x68, 0x69, 0x10, 0x69, 0x11});
  const auto query = items.size();
  stop(items);
  const std::vector<PreloadReport> tiny{
      {.register_name = "8", .value = "1e-8"}, {.register_name = "9", .value = "1"}};
  const auto proof = ranges(items, {query}, tiny);
  require(proof.x_before.at(query).lower <= 0 && proof.x_before.at(query).upper >= 0,
          "addition must not retain a decimal digit that the calculator rounded away");
  require(execute(items, tiny) == 0, "ROM must confirm the lost ninth significant digit");
}

std::vector<MachineItem> composite_fixture(int carrier, bool unknown_score = false) {
  std::vector<MachineItem> items;
  append(items, {0x45, 1, 0x40 + carrier});
  for (int product = 0; product < 3; ++product)
    append(items, {0x6b, 0x60 + carrier, 1, 0, 0x13, 0x12, 0x4a});
  target(items, 0x53, "score");
  append(items, {0x60 + carrier, 0x36, 0x40 + carrier, 0x60 + carrier, 0x44});
  append(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10});
  stop(items);
  items.push_back(MachineItem::label("score"));
  append(items, {0x6b});
  target(items, 0x57, "high");
  append(items, {0x0e, unknown_score ? 0x6c : 3});
  target(items, 0x51, "normalize");
  items.push_back(MachineItem::label("high"));
  append(items, {0x0e, 7});
  items.push_back(MachineItem::label("normalize"));
  append(items, {0x34, 4, 0x13, 0x35, 1, 0x11, 0x35, 1, 0x10, 4, 0x12, 2, 0x12, 0x52});
  return items;
}
bool mixed(const passes::PassResult& result) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
      [](const auto& report) { return report.name == "mixed-epoch-coefficient-state-carrier"; });
}
int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
      [](const auto& item) { return item.kind != MachineItemKind::Label; }));
}
std::vector<std::string> observe(
    const std::vector<MachineItem>& items, const char* input,
    const std::vector<PreloadReport>& preloads = {}) {
  emulator::MK61 calc;
  require(calc.load_program(bytecode(items)).diagnostics.empty(), "composed lattice ROM load");
  constexpr std::array<const char*, 15> names{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  for (std::size_t i = 0; i < names.size(); ++i)
    calc.set_register(names[i], std::to_string(i + 2U));
  calc.set_register("b", input);
  for (const auto& preload : preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(15000, 8).stopped, "composed lattice must return and stop");
  std::vector<std::string> values{calc.display_text(true)};
  calc.press(".");
  values.push_back(calc.display_text(true));
  for (const char* name : {"X", "Y", "Z", "T", "X1"})
    values.push_back(calc.read_register(name));
  for (const char* name : names)
    values.push_back(calc.read_register(name));
  return values;
}
void compiler_composition_and_size_baseline() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  for (const int carrier : {2, 8}) {
    const auto before = composite_fixture(carrier);
    const auto candidates = passes::finite_mode_representation_candidates(
        raise_machine_to_ir(before), {options}, {.maximum_candidates = 32});
    const auto found = std::find_if(candidates.begin(), candidates.end(), mixed);
    require(found != candidates.end(), "lattice bounds must unlock mixed-epoch carrier selection");
    const auto after = lower_ir_to_machine(found->ops);
    require(cells(before) == 61 && cells(after) == 54,
            "quantized composition size baseline must shrink 61 to 54 cells: " +
                std::to_string(cells(before)) + " -> " + std::to_string(cells(after)));
    require(found->preloads.empty() && build_post_layout_control_flow(after).proved,
            "lattice composition must not add setup and must retain the final CFG");
    for (const char* input : {"-7", "0", "13"})
      require(observe(before, input) == observe(after, input),
              "lattice composition must preserve registers, stack, X1, decimal state and returns");
    const auto unsafe = passes::finite_mode_representation_candidates(
        raise_machine_to_ir(composite_fixture(carrier, true)), {options},
        {.maximum_candidates = 32});
    require(std::none_of(unsafe.begin(), unsafe.end(), mixed),
            "an unknown score arm must not acquire an invented normalization domain");
  }
  const LayoutSearchArtifact root{.items = composite_fixture(8)};
  const auto edges = expand_joint_representation_transactions(root, {}, false);
  require(std::any_of(edges.begin(), edges.end(), [](const auto& edge) {
    return cells(edge.node.items) == 54 &&
        std::any_of(edge.node.optimizations.begin(), edge.node.optimizations.end(),
            [](const auto& report) {
              return report.name == "mixed-epoch-coefficient-state-carrier";
            });
  }), "the production joint search must consume quantized range proofs");
}
void recall_flow_entry_lift_contracts() {
  for (int chain = 1; chain <= 3; ++chain)
    for (const int reg : {0, 8, 14})
      for (const char* word : {"-2.5", "0", "7"}) {
        const std::string name(1, "0123456789abcde"[reg]);
        const std::vector<PreloadReport> preloads{
            {.register_name = name, .value = word}};
        std::vector<MachineItem> baseline;
        append(baseline, {0x60 + reg, 1, 0x14});
        stop(baseline);
        std::vector<MachineItem> items;
        append(items, {0x60 + reg});
        for (int jump = 0; jump < chain; ++jump) {
          const auto label = "next_" + std::to_string(jump);
          target(items, 0x51, label);
          items.push_back(MachineItem::label(label));
        }
        append(items, {1, 0x14});
        const auto query = items.size();
        stop(items);
        require(ranges(items, {query}, preloads).x_before.at(query) ==
                    *bound_ordinary_decimal(word),
                "recall must certify fresh-literal lift through every proved direct BP");
        require(observe(baseline, "13", preloads) == observe(items, "13", preloads),
                "ROM direct BP chains must preserve stack, registers, X1 and entry shadow");
      }

  const std::vector<PreloadReport> preloads{{.register_name = "8", .value = "7"}};
  std::vector<MachineItem> joined;
  append(joined, {0x6b});
  target(joined, 0x57, "clear");
  append(joined, {0x68});
  target(joined, 0x51, "digit");
  joined.push_back(MachineItem::label("clear"));
  append(joined, {0x0d});
  joined.push_back(MachineItem::label("digit"));
  append(joined, {1, 0x14});
  const auto query = joined.size();
  stop(joined);
  require(!ranges(joined, {query}, preloads).x_before.contains(query),
          "one unproved incoming arm must block the recall/lift certificate");

  std::vector<MachineItem> resumed;
  append(resumed, {0x68, 0x50});
  resumed.back().stop_disposition = StopDisposition::Resumable;
  target(resumed, 0x51, "resumed_digit");
  resumed.push_back(MachineItem::label("resumed_digit"));
  append(resumed, {1, 0x14});
  const auto resumed_query = resumed.size();
  stop(resumed);
  require(!ranges(resumed, {resumed_query}, preloads).x_before.contains(resumed_query),
          "external input must erase an earlier recall/lift certificate");
}

void power_of_ten_range_contracts() {
  struct Row {
    const char* exponent;
    OrdinaryNumericRange expected;
  };
  for (const auto& row : {
      Row{"0", {1, 1, true}},
      Row{"1", {10, 10, true}},
      Row{"0.25", {1, 10, false}},
      Row{"0.5", {1, 10, false}},
      Row{"0.99999999", {1, 10, false}},
      Row{"1.0000001", {10, 100, false}},
      Row{"-0.25", {0, 1, false}},
      Row{"-1.5", {0, 1, false}},
      Row{"6.5", {1000000, 10000000, false}},
      Row{"6.9999999", {1000000, 10000000, false}},
      Row{"-98.5", {0, 1, false}},
      Row{"-99", {0, 1, false}}}) {
    std::vector<MachineItem> items;
    append(items, {0x68, 0x15});
    const auto query = items.size();
    stop(items);
    const std::vector<PreloadReport> preloads{
        {.register_name = "8", .value = row.exponent}};
    const auto proof = ranges(items, {query}, preloads);
    require(proof.x_before.at(query) == row.expected,
            "10^x must enclose a bounded ordinary exponent without host transcendental math: " +
                std::string(row.exponent));
    const auto actual = execute(items, preloads);
    require(actual >= static_cast<double>(row.expected.lower) &&
                actual <= static_cast<double>(row.expected.upper),
            "ROM exponentiation must stay inside the outward enclosure");
  }

  for (int exponent = -8; exponent <= 7; ++exponent) {
    std::vector<MachineItem> items;
    append(items, {0x68, 0x15, 0x69, 0x13});
    const auto query = items.size();
    stop(items);
    const std::vector<PreloadReport> preloads{
        {.register_name = "8", .value = std::to_string(exponent)},
        {.register_name = "9", .value = "1e" + std::to_string(exponent)}};
    require(ranges(items, {query}, preloads).x_before.at(query) ==
                OrdinaryNumericRange{1, 1, true},
            "integer powers must retain their exact finite decimal lattice");
    require(execute(items, preloads) == 1,
            "ROM must confirm exact positive and negative integer powers");
  }

  // Fractional exponents do not preserve the exponent's input quantum.
  std::vector<MachineItem> fractional;
  append(fractional, {0x68, 0x15, 2, 0x12});
  const auto query = fractional.size();
  stop(fractional);
  const std::vector<PreloadReport> half{{.register_name = "8", .value = "0.5"}};
  const auto bound = ranges(fractional, {query}, half).x_before.at(query);
  require(!bound.integral && bound.lower == 2 && bound.upper == 20,
          "sqrt(10) times two must not acquire an invented half-integer lattice");
  const auto actual = execute(fractional, half);
  require(actual > 6 && actual < 7,
          "ROM must retain the nonintegral power result through multiplication");

  for (const char* input : {"7.1", "-99.1", "1e8", "1e200", "A-2", "-0"}) {
    std::vector<MachineItem> items;
    append(items, {0x68, 0x15});
    const auto at = items.size();
    stop(items);
    const std::vector<PreloadReport> preloads{{.register_name = "8", .value = input}};
    require(!ranges(items, {at}, preloads).x_before.contains(at),
            "unsupported exponent/result domains and raw words must stay unproved");
  }
  std::vector<MachineItem> unknown;
  append(unknown, {0x68, 0x15});
  const auto at = unknown.size();
  stop(unknown);
  require(ranges(unknown, {at}).x_before.empty(),
          "10^x must not manufacture a domain for an unknown input");
}

std::vector<MachineItem> exponential_carrier_fixture(int carrier) {
  std::vector<MachineItem> items;
  append(items, {0x45, 1, 0x40 + carrier});
  for (int product = 0; product < 3; ++product)
    append(items, {0x6b, 0x60 + carrier, 1, 0, 0x13, 0x12, 0x4a});
  target(items, 0x53, "score");
  append(items, {0x60 + carrier, 0x36, 0x40 + carrier, 0x60 + carrier, 0x44});
  append(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10});
  stop(items);
  items.push_back(MachineItem::label("score"));
  append(items, {0x6b});
  target(items, 0x57, "right");
  append(items, {0x6c});
  target(items, 0x51, "power");
  items.push_back(MachineItem::label("right"));
  append(items, {0x6d});
  items.push_back(MachineItem::label("power"));
  append(items, {1, 0x10, 0x15, 0x52});
  return items;
}

void exponential_carrier_composition() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  const std::vector<PreloadReport> preloads{
      {.register_name = "c", .value = "0.25"},
      {.register_name = "d", .value = "0.5"}};
  passes::FiniteModeCandidateOptions candidate_options;
  candidate_options.maximum_candidates = 32;
  candidate_options.runtime_preloads = &preloads;
  for (const int carrier : {2, 8}) {
    const auto before = exponential_carrier_fixture(carrier);
    const auto candidates = passes::finite_mode_representation_candidates(
        raise_machine_to_ir(before), {options}, candidate_options);
    const auto found = std::find_if(candidates.begin(), candidates.end(), mixed);
    require(found != candidates.end(),
            "a bounded fractional exponent must unlock mixed-epoch carrier selection");
    const auto after = lower_ir_to_machine(found->ops);
    require(cells(before) == 49 && cells(after) == 42,
            "exponential carrier compiler baseline must shrink 49 to 42 cells");
    require(found->preloads.empty() && build_post_layout_control_flow(after).proved,
            "exponential carrier must retain its CFG without inventing setup words");
    for (const char* input : {"-7", "0", "13"})
      require(observe(before, input, preloads) == observe(after, input, preloads),
              "exponential composition must preserve all registers, stack, X1, X2 and returns");

    for (const std::vector<PreloadReport>& unsafe_preloads : {
        std::vector<PreloadReport>{preloads.front()},
        std::vector<PreloadReport>{preloads.front(), {.register_name = "d", .value = "A-2"}},
        std::vector<PreloadReport>{preloads.front(), {.register_name = "d", .value = "-2"}}}) {
      auto unsafe_options = candidate_options;
      unsafe_options.runtime_preloads = &unsafe_preloads;
      const auto unsafe = passes::finite_mode_representation_candidates(
          raise_machine_to_ir(before), {options}, unsafe_options);
      require(std::none_of(unsafe.begin(), unsafe.end(), mixed),
              "unknown, raw or nonabsorbing exponent arms must block carrier replacement");
    }
  }
  const LayoutSearchArtifact root{
      .items = exponential_carrier_fixture(8), .preloads = preloads};
  const auto incomplete = expand_joint_representation_transactions(root, {}, false);
  require(std::none_of(incomplete.begin(), incomplete.end(), [](const auto& edge) {
    return std::any_of(edge.node.optimizations.begin(), edge.node.optimizations.end(),
        [](const auto& report) {
          return report.name == "mixed-epoch-coefficient-state-carrier";
        });
  }), "an incomplete preload environment must not authorize numeric facts");
  const auto edges = expand_joint_representation_transactions(root, {}, true);
  require(std::any_of(edges.begin(), edges.end(), [](const auto& edge) {
    return cells(edge.node.items) == 42 &&
        std::any_of(edge.node.optimizations.begin(), edge.node.optimizations.end(),
            [](const auto& report) {
              return report.name == "mixed-epoch-coefficient-state-carrier";
            });
  }), "the joint search must consume ordinary fractional-exponent range certificates");
}

} // namespace

int main() {
  try {
    recall_entry_lift_contracts();
    normalized_countdown_intervals();
    joins_calls_and_aliases();
    rounding_and_unsupported_domains();
    compiler_composition_and_size_baseline();
    recall_flow_entry_lift_contracts();
    power_of_ten_range_contracts();
    exponential_carrier_composition();
    std::cout << "quantized range compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

