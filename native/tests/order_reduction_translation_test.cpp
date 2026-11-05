#include "mkpro/compiler.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/order_reduction_translation.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;
struct Fixture {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  std::size_t first = 0, last = 0;
};
Fixture fixture(const std::string& increment, int observation = 0, bool multiply = false, int initial_seed = 98) {
  MachineEmitter e;
  e.emit_op(0x60);
  const auto first = e.items.size(); e.emit_number(std::to_string(initial_seed));
  const auto last = e.items.size() - 1U;
  e.emit_op(0x61); e.emit_op(multiply ? 0x12 : 0x10); e.emit_op(0x48);
  e.emit_op(0x6b); e.emit_op(0x36); e.emit_op(0x49); e.emit_op(0x11);
  if (observation == 1) e.emit_op(0x68);
  else if (observation == 2) e.emit_op(0x0f);
  else if (observation == 3) { e.emit_op(0x68); e.emit_op(0x0a); }
  else if (observation == 4) {
    e.emit_op(0x0d); e.emit_op(0x43); e.emit_op(0xd3);
    e.items.back().indirect_memory_targets = std::vector<int>{3};
  }
  else { e.emit_op(0x62); e.emit_op(0x60); }
  e.emit_stop(StopDisposition::Terminal);
  e.items.back().roles.push_back(kTypedDisplayObservationRole);
  return {e.items, {{.register_name = "0", .value = "0"},
      {.register_name = "1", .value = increment}, {.register_name = "2", .value = "0"},
      {.register_name = "b", .value = "-1"}}, first, last};
}
OrderReductionTranslationProof prove(const Fixture& f, std::size_t budget = 100000) {
  const auto flow = build_post_layout_control_flow(f.items);
  require(flow.proved, "translation fixture must have a complete CFG");
  const auto lift = analyze_post_layout_automatic_entry_lift(f.items, flow, 100000);
  require(lift.has_value(), "translation fixture must have proved entry mode");
  std::vector<std::size_t> queries;
  for (std::size_t at = 0; at < f.items.size(); ++at)
    if (f.items[at].kind == MachineItemKind::Op) queries.push_back(at);
  const auto bounds = prove_post_layout_bounded_stack_numeric_values(
      f.items, flow, *lift, queries, f.preloads);
  require(!bounds.exhausted, "fixture bounds must converge");
  return prove_order_reduction_translation(f.items, flow, *lift, bounds, f.first, f.last, budget);
}
std::vector<std::string> observe(const Fixture& fixture, int seed) {
  auto items = fixture.items;
  items[fixture.first].opcode = seed / 10; items[fixture.last].opcode = seed % 10;
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "translated fixture resolves");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "fixture fits the real stock ROM");
  for (const auto& p : fixture.preloads) calc.set_register(p.register_name, p.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(6000, 8).stopped, "fixture reaches its terminal display");
  return {calc.read_register("X"), calc.read_register("Y")};
}
void translation_contracts() {
  for (const char* increment : {"0", "0.0000001", "0.1234567", "0.9999999", "1.0000001", "-0.0000001"}) {
    const auto f = fixture(increment);
    const auto proof = prove(f);
    std::string reasons;
    for (const auto& reason : proof.reasons) reasons += reason + "; ";
    require(proof.proved && proof.minimum_seed <= 52 && proof.maximum_seed >= 52,
            "closed order-only reduction must admit translation: " + reasons);
    for (const int seed : {52, 73, 98})
      require(observe(f, 98) == observe(f, seed), "translation preserves public X:Y and exact tie subtraction");
  }
  for (const auto& f : {fixture("2.01"), fixture("0.1", 1), fixture("0.1", 2),
                         fixture("0.1", 3), fixture("0.5", 0, true)})
    require(!prove(f).proved, "exponent crossing, visible score, X1, X2 and non-order consumers reject");
  auto raw = fixture("0.1"); raw.items[raw.first].raw = true;
  require(!prove(raw).proved, "raw literal cannot acquire a decimal translation proof");
  require(!prove(fixture("0.1"), 1).proved, "exhaustion cannot export a partial proof");
  auto zero = fixture("0.1"); zero.preloads.back().value = "0";
  require(!prove(zero).proved, "zero-sensitive Kmax sentinel must not acquire ordinary ordering");
  const auto self = fixture("0.1", 4);
  const auto self_proof = prove(self);
  require(self_proof.proved && self_proof.minimum_seed <= 52 && self_proof.maximum_seed >= 52,
          "an equal private self-selector must preserve exact zero-underflow writeback");
  require(observe(self, 98) == observe(self, 52), "zero-underflow error word preserves the ROM display pair");
  const auto other_seed = fixture("0.2345678", 0, false, 37);
  const auto other_proof = prove(other_seed);
  require(other_proof.proved && other_proof.minimum_seed <= 52 && other_proof.maximum_seed >= 52 &&
          observe(other_seed, 37) == observe(other_seed, 52), "translation is not tied to a particular original seed");
  auto without_maximum = fixture("0.1");
  for (auto& item : without_maximum.items) if (item.opcode == 0x6b) item.opcode = 0x68;
  std::erase_if(without_maximum.items, [](const auto& item) { return item.opcode == 0x36; });
  require(!prove(without_maximum).proved, "a program without Kmax cannot enter the order-only maximum lane");
}
Fixture tied_choice(const std::string& first_increment, const std::string& second_increment) {
  MachineEmitter e;
  e.emit_jump(0x53, "first candidate", "score"); e.emit_op(0x48);
  e.emit_op(0x62); e.emit_op(0x41);
  e.emit_jump(0x53, "second candidate", "score");
  e.emit_op(0x68); e.emit_op(0x36); e.emit_op(0x48); e.emit_op(0x11);
  e.emit_jump(0x5e, "same maximum", "first");
  e.emit_number("2"); e.emit_jump(0x51, "selected", "done");
  e.emit_label("first"); e.emit_number("1");
  e.emit_label("done"); e.emit_stop(StopDisposition::Terminal);
  e.items.back().roles.push_back(kTypedDisplayObservationRole);
  e.emit_label("score"); e.emit_op(0x60);
  const auto first = e.items.size(); e.emit_number("98");
  const auto last = e.items.size() - 1U;
  e.emit_op(0x61); e.emit_op(0x10); e.emit_op(0x52);
  return {e.items, {{.register_name = "0", .value = "0"},
      {.register_name = "1", .value = first_increment},
      {.register_name = "2", .value = second_increment}}, first, last};
}
void rounding_and_ties() {
  for (const auto& pair : std::vector<std::pair<std::string, std::string>>{
      {"0.1234567", "0.1234567"}, {"0.1234567", "0.1234568"},
      {"0.1234568", "0.1234567"}, {"0.9999994", "0.9999995"},
      {"0.0000001", "0.0000002"}, {"-0.0000001", "0.0000001"}}) {
    const auto f = tied_choice(pair.first, pair.second);
    const auto proof = prove(f);
    std::string reasons;
    for (const auto& reason : proof.reasons) reasons += reason + "; ";
    require(proof.proved && proof.minimum_seed <= 52 && proof.maximum_seed >= 52,
            "called reduction must prove tie-preserving translation: " + reasons);
    require(observe(f, 98) == observe(f, 52),
            "ROM winner and tie direction must survive one-ULP and rounding-boundary inputs");
  }
}
void input_contract() {
  const std::string source = R"(program DecimalInput {
    state { value: packed }
    show(0)
    value = entered()
    halt(frac(value))
  })";
  CompileOptions options; options.analysis = true; options.disable_candidate_search = true;
  options.ordinary_decimal_keyboard_input = true;
  const auto compiled = compile_source(source, options);
  std::string diagnostics;
  for (const auto& d : compiled.diagnostics) diagnostics += d.message + "; ";
  require(compiled.implemented && compiled.diagnostics.empty(), "typed input fixture compiles: " + diagnostics);
  const auto declared = [](const CompileResult& result) {
    return std::any_of(result.optimizations.begin(), result.optimizations.end(), [](const auto& report) {
      return report.name == "ordinary-decimal-keyboard-input-contract";
    });
  };
  require(declared(compiled), "the opt-in runtime assumption must be explicit in JSON/explain");
  options.ordinary_decimal_keyboard_input = false;
  const auto unrestricted = compile_source(source, options);
  require(unrestricted.implemented && unrestricted.diagnostics.empty() && !declared(unrestricted),
          "compiled-result caches must distinguish ordinary and unrestricted input contracts");
  const auto flow = build_post_layout_control_flow(compiled.items);
  require(flow.proved, "typed input fixture CFG");
  const auto lift = analyze_post_layout_automatic_entry_lift(compiled.items, flow, 100000);
  require(lift.has_value(), "typed input entry mode");
  std::vector<std::size_t> stops;
  for (std::size_t at = 0; at < compiled.items.size(); ++at)
    if (compiled.items[at].opcode == 0x50 && compiled.items[at].stop_disposition == StopDisposition::Terminal)
      stops.push_back(at);
  require(!stops.empty(), "typed terminal exists");
  const auto decimal = prove_post_layout_stack_numeric_values(compiled.items, flow, *lift, stops,
      &compiled.preloads, 100000, NumericRangeInputPolicy::ValidatedOrdinaryDecimalInput);
  const auto unknown = prove_post_layout_stack_numeric_values(compiled.items, flow, *lift, stops,
      &compiled.preloads, 100000, NumericRangeInputPolicy::ValidatedRegisterTransport);
  require(!decimal.exhausted && !unknown.exhausted, "input contract analyses converge");
  require(decimal.x_before.contains(stops.back()) && decimal.x_before.at(stops.back()).lower == -1 &&
          decimal.x_before.at(stops.back()).upper == 1, "explicit decimal input recovers only the fractional enclosure");
  require(!unknown.x_before.contains(stops.back()), "unrestricted entered() must not silently become decimal");
}
void selector_class_contract() {
  for (const int bitwise : {0x38, 0x37}) {
    MachineEmitter e;
    e.emit_op(0x63); e.emit_op(0x64); e.emit_op(bitwise); e.emit_op(0x40);
    e.emit_op(0x65); e.emit_op(0xb0);
    e.items.back().indirect_memory_targets = std::vector<int>{0, 4, 5, 6, 7};
    e.emit_op(0x60);
    const auto query = e.items.size(); e.emit_stop(StopDisposition::Terminal);
    const auto flow = build_post_layout_control_flow(e.items);
    require(flow.proved, "complete conservative alias set");
    const auto lift = analyze_post_layout_automatic_entry_lift(e.items, flow, 100000);
    require(lift.has_value(), "selector entry mode");
    const std::vector<PreloadReport> preloads{{.register_name = "5", .value = "12345"}};
    const auto proof = prove_post_layout_bounded_stack_numeric_values(e.items, flow, *lift, {query}, preloads,
        100000, NumericRangeInputPolicy::ValidatedRegisterTransport);
    require(!proof.exhausted, "selector class proof converges");
    const auto result = proof.x_before.find(query);
    const bool seven = result != proof.x_before.end() && result->second == OrdinaryNumericRange{7, 7, true};
    require(seven == (bitwise == 0x38),
            "only the certified OR word class can remove a spurious self-store alias");
  }
}
void read_only_word_contract() {
  std::string alias_selector;
  for (int value = 1; value <= 99; ++value) {
    const auto decoded = evaluate_indirect_address("7", std::to_string(value), IndirectOperationKind::Memory);
    if (decoded && decoded->memory_target == 14) { alias_selector = std::to_string(value); break; }
  }
  require(!alias_selector.empty(), "fixture needs a concrete R7 selector for RE");
  for (const int writer : {0, 1, 2}) {
    MachineEmitter e;
    e.emit_op(0xae); e.items.back().indirect_flow_targets = std::vector<IrTarget>{std::string("callee")};
    e.emit_op(0x6e);
    const auto query = e.items.size(); e.emit_stop(StopDisposition::Terminal);
    while (e.items.size() < 76U) e.emit_op(0x54);
    e.emit_label("callee");
    if (writer != 0) {
      e.emit_op(0x61); e.emit_op(writer == 1 ? 0x4e : 0xb7);
      if (writer == 2) e.items.back().indirect_memory_targets = std::vector<int>{14};
    }
    e.emit_op(0x52);
    const std::vector<PreloadReport> preloads{{.register_name = "e", .value = "4.1200076E-1"},
        {.register_name = "7", .value = alias_selector}};
    const auto flow = build_post_layout_control_flow(e.items);
    require(flow.proved, "read-only preload fixture CFG");
    const auto lift = analyze_post_layout_automatic_entry_lift(e.items, flow, 100000);
    require(lift.has_value(), "read-only preload entry mode");
    const auto proof = prove_post_layout_bounded_stack_numeric_values(e.items, flow, *lift, {query}, preloads);
    require(!proof.exhausted, "read-only preload proof converges");
    bool constant = false;
    for (const auto& [state, values] : proof.stack_before_state)
      if (flow.execution_states[state].item_index == query && values[0].enclosure)
        constant = values[0].enclosure->lower == 412000 && values[0].enclosure->upper == 412001;
    require(constant == (writer == 0), "direct and aliased writers invalidate an immutable preload certificate");
    const auto word = evaluate_indirect_address("e", preloads.front().value, IndirectOperationKind::Flow);
    const auto decimal = evaluate_indirect_address("e", "0.41200076", IndirectOperationKind::Flow);
    require(word && indirect_writeback_preserves_literal_word(*word, preloads.front().value) &&
            decimal && !indirect_writeback_preserves_literal_word(*decimal, "0.41200076"),
            "equal decimal numbers are not interchangeable selector memory words");
    const auto resolved = resolve_machine_items(e.items);
    require(resolved.diagnostics.empty(), "word fixture resolves");
    std::vector<int> code;
    for (const auto& step : resolved.steps) code.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(code).diagnostics.empty(), "word fixture fits stock ROM");
    for (const auto& p : preloads) calc.set_register(p.register_name, p.value);
    calc.set_register("1", "1.25");
    const auto before = calc.read_register("e");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(6000, 8).stopped, "word fixture stops");
    require((calc.read_register("e") == before) == (writer == 0),
            "ROM confirms complete scientific-word preservation and both real writer aliases");
  }
}
void negative_divisor_enclosure() {
  MachineEmitter e;
  e.emit_op(0x63); e.emit_jump(0x57, "unknown choice", "other");
  e.emit_number("-3"); e.emit_op(0x41); e.emit_jump(0x51, "joined divisor", "divide");
  e.emit_label("other"); e.emit_number("-2"); e.emit_op(0x41);
  e.emit_label("divide"); e.emit_op(0x60); e.emit_op(0x61); e.emit_op(0x13);
  const auto query = e.items.size(); e.emit_stop(StopDisposition::Terminal);
  const std::vector<PreloadReport> preloads{{.register_name = "0", .value = "6"}};
  const auto flow = build_post_layout_control_flow(e.items);
  require(flow.proved, "negative divisor CFG");
  const auto lift = analyze_post_layout_automatic_entry_lift(e.items, flow, 100000);
  require(lift.has_value(), "negative divisor entry mode");
  const auto proof = prove_post_layout_bounded_stack_numeric_values(e.items, flow, *lift, {query}, preloads,
      100000, NumericRangeInputPolicy::ValidatedRegisterTransport);
  require(!proof.exhausted && !proof.stack_before_state.empty(), "negative divisor interval converges");
  for (const auto& [state, values] : proof.stack_before_state) {
    (void)state;
    require(values[0].enclosure && values[0].enclosure->lower > -4000000 &&
            values[0].enclosure->upper < -1000000,
            "negative denominators must not mutate the numerator across endpoint alternatives");
  }
}
void pending_decimal_size_baseline() {
  std::ifstream input(std::string(MKPRO_ORDER_REDUCTION_SOURCE_ROOT) +
      "/examples/pending-optimizer/tic-tac-toe-4x4.mkpro");
  require(input.good(), "pending source must exist");
  const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  CompileOptions options;
  // Match the existing over-budget planning measurement. The compiler budget
  // is not the size assertion: enabling intermediate artifacts must not be
  // confused with raising the delivered regression threshold below.
  options.analysis = true; options.budget = 999999; options.fast_candidate_search = true;
  options.ordinary_decimal_keyboard_input = true;
  const auto result = compile_source(source, options);
  std::string diagnostics;
  for (const auto& d : result.diagnostics)
    if (d.severity == DiagnosticSeverity::Error) diagnostics += d.message + "; ";
  require(result.implemented && diagnostics.empty(), "decimal-contract pending port must compile: " + diagnostics);
  require(result.feature_profile == FeatureProfile::Standard && result.steps.size() <= 120U,
          "unchanged source decimal-contract size baseline must remain at most 120 stock-profile cells; got " +
              std::to_string(result.steps.size()));
  for (const auto& [name, reg] : result.registers) {
    (void)name;
    // Opcode aliases such as 4F mean R0, not Rf, on the stock machine.
    require(register_index(register_from_text(reg)) != 15,
            "stock-profile reduction must not allocate Rf");
  }
  std::cout << "ordinary-decimal pending size baseline: " << result.steps.size()
            << " cells, stock register profile, unchanged MK-Pro source\n";
}
} // namespace
int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--pending-decimal-baseline") {
      pending_decimal_size_baseline(); return 0;
    }
    translation_contracts(); rounding_and_ties(); input_contract(); selector_class_contract(); read_only_word_contract();
    negative_divisor_enclosure();
    std::cout << "order-only reduction translation compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
