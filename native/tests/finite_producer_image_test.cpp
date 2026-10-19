#include "mkpro/compiler.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

enum class Observer { None, LastX, EntryShadow };

void append(std::vector<MachineItem>& items, std::initializer_list<int> codes) {
  for (const int code : codes)
    items.push_back(MachineItem::op(code, "finite producer fixture"));
}

std::vector<MachineItem> fixture(Observer observer = Observer::None,
                               bool raw = false, bool repeated = false,
                               bool negative = false) {
  std::vector<MachineItem> items{MachineItem::label("again")};
  if (negative) {
    append(items, {0x69, 0x69, 0x61, 0x38, 0x49, 0x11, 0x57});
    items.push_back(MachineItem::address(std::string("done")));
  } else {
    append(items, {0x61, 0x62, raw ? 0x54 : 0x38});
  }
  append(items, {0x32, 0x48});
  if (observer == Observer::LastX) append(items, {0x0f, 0x47});
  if (observer == Observer::EntryShadow) append(items, {0x0a, 0x47});
  append(items, {0x63, 0x15, 0x68, 1, 0, 0x13, 0x12, 0x44,
                 0x65, 0x15, 0x68, 1, 0, 0x13, 0x12, 0x46, 0, 0x48});
  if (repeated || negative) {
    append(items, {negative ? 0x51 : 0x5d});
    items.push_back(MachineItem::address(std::string("again")));
  }
  items.push_back(MachineItem::label("done"));
  append(items, {0x6a, 0x6b, 0x6c, 0x6d, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  return items;
}

std::vector<PreloadReport> preloads(bool raw = false) {
  std::vector<PreloadReport> result;
  for (int reg = 0; reg < 15; ++reg)
    result.push_back({.register_name = std::string(1, "0123456789abcde"[reg]),
                      .value = std::to_string(reg + 1)});
  result[0].value = "2";
  result[3].value = "2";
  result[5].value = "3";
  result[9].value = "0";
  if (raw) result[2].value = "0xEE";
  return result;
}

std::vector<passes::PassResult> candidates(const std::vector<MachineItem>& items,
                                          const std::vector<PreloadReport>& setup) {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  return passes::finite_mode_representation_candidates(
      raise_machine_to_ir(items), {options},
      {.maximum_candidates = 16, .runtime_preloads = &setup});
}

int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const auto& item) {
    return item.kind != MachineItemKind::Label;
  }));
}

std::vector<std::string> observe(const std::vector<MachineItem>& items,
                                 const std::vector<PreloadReport>& setup) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "finite producer fixture resolves");
  std::vector<int> bytes;
  for (const auto& step : resolved.steps) bytes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.command_limit() == 105, "finite producer must use the stock register bank");
  require(calc.load_program(bytes).diagnostics.empty(), "finite producer fixture fits stock ROM");
  for (const auto& value : setup) calc.set_register(value.register_name, value.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(6000, 8).stopped, "finite producer ROM execution stops");
  std::vector<std::string> state{calc.display_text(true)};
  for (const auto* name : {"X", "Y", "Z", "T", "X1"}) state.push_back(calc.read_register(name));
  for (const char name : std::string("0123456789abcde"))
    state.push_back(calc.read_register(std::string(1, name)));
  calc.press(".");
  state.push_back(calc.display_text(true));
  return state;
}

void profitable_image(bool repeated, bool negative) {
  const auto before = fixture(Observer::None, false, repeated, negative);
  const auto setup = preloads();
  const auto alternatives = candidates(before, setup);
  require(!alternatives.empty(), "proved computed inputs need a finite image candidate");
  const auto after = lower_ir_to_machine(alternatives.front().ops);
  require(cells(after) == cells(before) - 3, "two consumers repay a shared producer decoder");
  require(observe(before, setup) == observe(after, setup),
          "computed image preserves all words, registers, last-X, X2 and repeated epochs");
}

void sign_entry_contract() {
  const auto observation = [](bool close, int prefix, int value) {
    MachineEmitter e;
    if (prefix == 0) e.emit_op(0x61);
    if (prefix == 1) e.emit_number(std::to_string(value));
    if (prefix == 2) { e.emit_op(0x61); e.emit_op(0x0e); }
    if (prefix >= 3) {
      e.emit_number("8"); e.emit_op(0x0c); e.emit_number("1");
      if (prefix == 4) e.emit_op(0x0b);
    }
    e.emit_op(0x32);
    if (close) e.emit_op(0x4e);
    e.emit_number("9");
    e.emit_stop(StopDisposition::Terminal);
    const auto resolved = resolve_machine_items(e.items);
    require(resolved.diagnostics.empty(), "sign entry fixture resolves");
    std::vector<int> bytes;
    for (const auto& step : resolved.steps) bytes.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(bytes).diagnostics.empty(), "sign entry fixture fits ROM");
    for (const auto& [name, word] : std::vector<std::pair<std::string, std::string>>{
             {"X", "17"}, {"Y", "23"}, {"Z", "31"}, {"T", "43"},
             {"1", std::to_string(value)}}) calc.set_register(name, word);
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(1500, 8).stopped, "sign entry fixture stops");
    std::vector<std::string> state;
    for (const auto* name : {"X", "Y", "Z", "T", "X1"}) state.push_back(calc.read_register(name));
    calc.press("."); state.push_back(calc.display_text(true));
    return state;
  };
  require(selector_charge_entry_closer_opcode(0x32), "K sign closes numeric entry");
  for (int prefix = 0; prefix < 5; ++prefix)
    for (const int value : {-57, -1, 0, 1, 83})
      require(observation(false, prefix, value) == observation(true, prefix, value),
              "K sign independently enables the next literal lift");
}


void exact_entry_context_contracts() {
  for (const bool resume : {false, true}) {
    std::vector<MachineItem> items{
        MachineItem::label("startup_return"), MachineItem::op(0x52, "return")};
    if (resume) {
      items.push_back(MachineItem::op(0x50, "external resume"));
      items.back().stop_disposition = StopDisposition::Resumable;
    }
    append(items, {0x64, 0x53});
    items.push_back(MachineItem::address(std::string("startup_return")));
    const auto query = items.size();
    append(items, {0x64, 0x50});
    items.back().stop_disposition = StopDisposition::Terminal;
    const auto flow = build_post_layout_control_flow(items, {.empty_return_target = 1});
    require(flow.proved, "startup/callee fixture has exact return contexts");
    const auto entries = resolve_post_layout_external_entry_contexts(flow);
    require(entries.has_value(), "all external contexts must resolve");
    std::size_t mains = 0, called_returns = 0;
    for (std::size_t state = 0; state < flow.execution_states.size(); ++state) {
      const auto& execution = flow.execution_states[state];
      mains += entries->main[state] ? 1U : 0U;
      if (execution.address == 0 && !execution.return_stack.empty()) {
        ++called_returns;
        require(!entries->external[state], "a called startup return is not an external entry");
      }
    }
    require(mains == 1 && called_returns != 0, "fixture distinguishes both uses of cell 00");
    const auto lift = analyze_post_layout_automatic_entry_lift(items, flow, 100000);
    require(lift.has_value(), "entry facts resolve through the shared return");
    const std::vector<PreloadReport> setup{{.register_name = "4", .value = "7"}};
    const auto ranges = prove_post_layout_numeric_ranges(items, flow, *lift, {query}, &setup);
    require(!ranges.exhausted, "entry-context bounds must converge");
    if (resume) {
      require(!ranges.x_before.contains(query), "a genuine resume cannot inherit setup registers");
    } else {
      require(ranges.x_before.contains(query) &&
                  ranges.x_before.at(query) == OrdinaryNumericRange{7, 7, true},
              "an ordinary call through 00 must retain its caller's register bounds");
    }
    auto malformed = flow;
    malformed.external_entries.front().entry.item_index = items.size();
    require(!resolve_post_layout_external_entry_contexts(malformed),
            "missing exact roots must not produce a partial entry certificate");
  }

  PostLayoutExternalEntryState entry;
  entry.entry = {.item_index = 2, .address = 1};
  entry.formal_opcode = 1;
  entry.return_stack.push_back({.item_index = 8, .address = 6});
  entry.formal_return_stack.push_back(6);
  PostLayoutExecutionState execution;
  execution.item_index = 2;
  execution.address = 1;
  execution.formal_opcode = 1;
  execution.return_stack = {6};
  execution.formal_return_stack = {6};
  require(post_layout_execution_matches_external_entry(execution, entry),
          "identical encoded return contexts match");
  auto different = execution;
  different.formal_opcode.reset();
  require(!post_layout_execution_matches_external_entry(different, entry),
          "logical and physical entry counters are distinct");
  different = execution;
  different.formal_return_stack[0].reset();
  require(!post_layout_execution_matches_external_entry(different, entry),
          "physical return targets do not erase formal return provenance");
  different = execution;
  different.return_stack[0] = 7;
  require(!post_layout_execution_matches_external_entry(different, entry),
          "different return destinations do not match");
  different = execution;
  different.formal_return_stack.clear();
  require(!post_layout_execution_matches_external_entry(different, entry),
          "incomplete return counters fail closed");
}

void return_zero_context_contracts() {
  for (const auto observer : {Observer::None, Observer::LastX, Observer::EntryShadow}) {
    auto source = fixture(observer);
    const auto second = std::find_if(source.begin(), source.end(), [](const auto& item) {
      return item.kind == MachineItemKind::Op && item.opcode == 0x65;
    });
    require(second != source.end(), "second coefficient consumer exists");
    source.insert(second, {MachineItem::op(0x53, "call shared initial return"),
                          MachineItem::address(std::string("initial_return"))});
    source.insert(source.begin(), {MachineItem::label("initial_return"),
                                  MachineItem::op(0x52, "shared initial return")});
    const auto setup=preloads();
    CompileOptions options;options.defer_return_suffix_until_callee_hole=true;
    const auto alternatives=passes::finite_mode_representation_candidates(
        raise_machine_to_ir(source),{options},
        {.maximum_candidates=16,.runtime_preloads=&setup,
         .preserve_compiler_selector_charges=true});
    if(observer!=Observer::None) {
      require(alternatives.empty(), "return context cannot hide a real X1/X2 observation");
      continue;
    }
    require(!alternatives.empty(), "a call through the startup return must not reset the finite epoch");
    const auto changed=lower_ir_to_machine(alternatives.front().ops);
    require(cells(source)==31 && cells(changed)==28,"shared-return finite-image baseline is 31 -> 28");
    require(observe(source,setup)==observe(changed,setup),
            "shared startup/callee return preserves all registers, visible and hidden stack state");
  }
}

} // namespace

int main() {
  try {
    sign_entry_contract();
    return_zero_context_contracts();
    exact_entry_context_contracts();
    profitable_image(false, false);
    profitable_image(true, false);
    profitable_image(false, true);
    const auto setup = preloads();
    require(candidates(fixture(Observer::LastX), setup).empty(), "live last-X blocks recoding");
    require(candidates(fixture(Observer::EntryShadow), setup).empty(), "live X2 blocks recoding");
    require(candidates(fixture(Observer::None, true), preloads(true)).empty(),
            "an arbitrary raw word has no unit-sign certificate");
    std::cout << "finite producer image, entry, positive and negative ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
