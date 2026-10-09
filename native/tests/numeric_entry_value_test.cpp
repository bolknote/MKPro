#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"
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

void append(std::vector<MachineItem>& items, std::initializer_list<int> codes) {
  for (const int code : codes)
    items.push_back(MachineItem::op(code, "numeric entry contract"));
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
  require(resolved.diagnostics.empty(), "entry fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps)
    codes.push_back(step.opcode);
  return codes;
}
PostLayoutNumericRangeProof ranges(
    const std::vector<MachineItem>& items, const std::vector<std::size_t>& requested,
    const std::vector<PreloadReport>& preloads = {}, bool unproved_lift = false) {
  const auto flow = build_post_layout_control_flow(items);
  require(flow.proved, "entry fixture needs an authoritative CFG");
  auto lift = analyze_post_layout_automatic_entry_lift(items, flow, 100000);
  require(lift.has_value(), "entry fixture needs entry-lift analysis");
  if (unproved_lift)
    std::fill(lift->begin(), lift->end(), false);
  return prove_post_layout_numeric_ranges(
      items, flow, *lift, requested, &preloads, 100000,
      NumericRangeInputPolicy::ValidatedRegisterTransport);
}

void exact_x_is_not_a_stack_lift_certificate() {
  for (const int prefix : {0x0d, 0x0e}) {
    std::vector<MachineItem> items;
    append(items, {prefix, 4});
    const auto x = items.size();
    append(items, {0x48, 0x14});
    const auto y = items.size();
    stop(items, StopDisposition::Terminal);
    const auto proof = ranges(items, {x, y}, {}, true);
    require(proof.x_before.at(x) == OrdinaryNumericRange{4, 4, true},
            "fresh integer X does not require a proved stack lift");
    require(!proof.x_before.contains(y),
            "an exact X certificate must not invent the previous Y");

    emulator::MK61 calc;
    require(calc.load_program(bytecode(items)).diagnostics.empty(), "entry ROM load");
    calc.set_register("X", "11");
    calc.set_register("Y", "13");
    calc.set_register("Z", "17");
    calc.set_register("T", "19");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(2000, 8).stopped, "entry ROM must stop");
    require(calc.read_register("8").starts_with("4,") &&
                calc.read_register("X").starts_with(prefix == 0x0d ? "13," : "11,"),
            "clear and duplicate preserve different stack histories despite identical entered X");
  }

  std::vector<MachineItem> items;
  append(items, {0x0e, 1, 2, 3});
  const auto query = items.size();
  stop(items, StopDisposition::Terminal);
  require(ranges(items, {query}, {}, true).x_before.at(query) ==
              OrdinaryNumericRange{123, 123, true},
          "an uninterrupted number entry is a whole number, not its final digit");

  for (const auto& suffix : {
      std::vector<int>{1, 0x0a, 2}, std::vector<int>{1, 0x0c, 2},
      std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9}}) {
    items.clear();
    append(items, {0x0e});
    for (const int code : suffix)
      append(items, {code});
    const auto at = items.size();
    stop(items, StopDisposition::Terminal);
    require(ranges(items, {at}, {}, true).x_before.empty(),
            "fraction, exponent and overlong entry need their own proof");
  }
}


void integer_sign_closes_entry_without_claiming_stack_lift() {
  struct Case {
    std::vector<int> suffix;
    const char* value;
    bool exact;
  };
  const std::vector<Case> cases{
      {{1, 0x0b}, "-1", true},
      {{1, 0x0b, 0x0b}, "1", true},
      {{1, 0x0b, 2}, "2", true},
      {{1, 0x0b, 0x0b, 2}, "2", true},
      {{1, 0x0b, 2, 3}, "23", true},
      {{1, 2, 3, 4, 5, 6, 7, 8, 0x0b}, "-12345678", true},
      {{1, 2, 3, 4, 5, 6, 7, 8, 0x0b, 9}, "9", true},
      {{0, 0x0b}, "0", false},
      {{0, 0x0b, 1}, "1", true},
      {{0, 0, 1, 0x0b, 2}, "2", true},
  };
  for (const int prefix : {0x0d, 0x0e, 0x68}) {
    for (const auto& test : cases) {
      std::vector<MachineItem> items;
      append(items, {prefix});
      for (const int code : test.suffix)
        append(items, {code});
      const auto at = items.size();
      stop(items, StopDisposition::Terminal);
      const auto proof = ranges(items, {at}, {}, true);
      if (test.exact) {
        const auto expected = std::stoll(test.value);
        require(proof.x_before.contains(at) &&
                    proof.x_before.at(at) == OrdinaryNumericRange{expected, expected, true},
                "integer sign must close entry and the next digit must start a new value");
      } else {
        require(!proof.x_before.contains(at),
                "typed signed zero must not become an ordinary-word certificate");
      }

      emulator::MK61 calc;
      require(calc.load_program(bytecode(items)).diagnostics.empty(), "signed-entry ROM load");
      calc.set_register("X", "11");
      calc.set_register("Y", "13");
      calc.set_register("Z", "17");
      calc.set_register("T", "19");
      calc.set_register("8", "23");
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(2000, 8).stopped &&
                  calc.read_register("X").starts_with(std::string(test.value) + ","),
              "ROM must confirm integer sign and fresh digits after sign");
    }
  }


  std::vector<MachineItem> roundtrip;
  append(roundtrip, {0x0e, 3, 0x0b, 0x48, 0x0d, 0x68});
  const auto recalled = roundtrip.size();
  stop(roundtrip, StopDisposition::Terminal);
  require(ranges(roundtrip, {recalled}, {}, true).x_before.at(recalled) ==
              OrdinaryNumericRange{-3, -3, true},
          "a signed integer must survive store, clear and recall");
  const auto roundtrip_flow = build_post_layout_control_flow(roundtrip);
  const auto roundtrip_values =
      analyze_stable_register_value_flow(roundtrip, {}, roundtrip_flow);
  require(roundtrip_values.proved &&
              roundtrip_values.before_item.at(recalled).at(1) ==
                  std::optional<std::string>{"-3"},
          "the stable-register certificate must retain the signed machine value");

  for (const auto& suffix : {
      std::vector<int>{1, 0x0a, 2, 0x0b},
      std::vector<int>{1, 0x0c, 2, 0x0b},
      std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 0x0b}}) {
    std::vector<MachineItem> items;
    append(items, {0x0e});
    for (const int code : suffix)
      append(items, {code});
    const auto at = items.size();
    stop(items, StopDisposition::Terminal);
    require(ranges(items, {at}, {}, true).x_before.empty(),
            "sign must not legalize unsupported fractional, exponent or overlong entry");
  }

  std::vector<MachineItem> resumed;
  append(resumed, {0x0d, 1});
  stop(resumed, StopDisposition::Resumable);
  append(resumed, {0x0b, 2});
  const auto at = resumed.size();
  stop(resumed, StopDisposition::Terminal);
  require(ranges(resumed, {at}, {}, true).x_before.empty(),
          "sign alone must not certify an ambiguous manual entry as a fresh integer");
}

void exact_caller_contexts_and_input_boundaries() {
  std::vector<MachineItem> items;
  append(items, {0x0d, 3, 0x0b});
  target(items, 0x53, "identity");
  const auto first = items.size();
  append(items, {0x48, 0x0d, 7, 0x0b});
  target(items, 0x53, "identity");
  const auto second = items.size();
  append(items, {0x49});
  stop(items, StopDisposition::Terminal);
  items.push_back(MachineItem::label("identity"));
  const auto helper = items.size();
  append(items, {0x52});
  const auto flow = build_post_layout_control_flow(items);
  const auto stable = analyze_stable_register_value_flow(items, {}, flow);
  require(stable.proved && stable.x_before_state.size() == flow.execution_states.size(),
          "exact X facts are indexed by authoritative execution state");
  std::set<std::string> helper_values;
  for (std::size_t state = 0; state < flow.execution_states.size(); ++state)
    if (flow.execution_states[state].item_index == helper) {
      require(stable.x_before_state[state].has_value(), "each caller has an exact helper input");
      helper_values.insert(*stable.x_before_state[state]);
    }
  require(helper_values == std::set<std::string>{"-3", "-7"},
          "distinct return contexts must not be merged into one exact X");
  const auto proof = ranges(items, {first, second, helper}, {}, true);
  require(proof.x_before.at(first) == OrdinaryNumericRange{-3, -3, true} &&
              proof.x_before.at(second) == OrdinaryNumericRange{-7, -7, true} &&
              proof.x_before.at(helper) == OrdinaryNumericRange{-7, -3, true},
          "joining a public query must not contaminate its individual caller results");

  emulator::MK61 calc;
  require(calc.load_program(bytecode(items)).diagnostics.empty(), "caller ROM load");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(2000, 8).stopped &&
              calc.read_register("8").starts_with("-3,") &&
              calc.read_register("9").starts_with("-7,"),
          "ROM must confirm the context-specific returns");

  for (const bool clear : {false, true}) {
    items.clear();
    append(items, {0x0d, 3});
    stop(items, StopDisposition::Resumable);
    const auto input = items.size();
    if (clear)
      append(items, {0x0d});
    append(items, {4});
    const auto after = items.size();
    stop(items, StopDisposition::Terminal);
    const auto resumed = ranges(items, {input, after}, {}, true);
    require(!resumed.x_before.contains(input),
            "resume input must not inherit the displayed literal");
    require(resumed.x_before.contains(after) == clear,
            "input entry remains ambiguous until an explicit entry-ending command");
    if (clear)
      require(resumed.x_before.at(after) == OrdinaryNumericRange{4, 4, true},
              "a new integer entry after clear restores an exact X");
  }

  for (const char* word : {"A-2", "-0", "1e200"}) {
    items.clear();
    append(items, {0x68});
    const auto at = items.size();
    stop(items, StopDisposition::Terminal);
    const std::vector<PreloadReport> preloads{{.register_name = "8", .value = word}};
    require(ranges(items, {at}, preloads, true).x_before.empty(),
            "exact machine words are not automatically ordinary-number certificates");
  }
}

enum class Score { Known, UnknownArm, ZeroArm, SignedSquare };
std::vector<MachineItem> composite_fixture(int reg, Score score = Score::Known) {
  std::vector<MachineItem> items;
  append(items, {0x45, 1, 0x40 + reg});
  for (int product = 0; product < 3; ++product)
    append(items, {0x6b, 0x60 + reg, 1, 0, 0x13, 0x12, 0x4a});
  target(items, 0x53, "score");
  append(items, {0x60 + reg, 0x36, 0x40 + reg, 0x60 + reg, 0x44});
  // Reestablish all observable stack slots and last-X after the carrier epoch.
  append(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10});
  stop(items, StopDisposition::Terminal);
  items.push_back(MachineItem::label("score"));
  append(items, {0x6b});
  target(items, 0x57, "high");
  append(items, {0x0e, score == Score::UnknownArm ? 0x6c : score == Score::ZeroArm ? 0 : 3});
  if (score == Score::SignedSquare)
    append(items, {0x0b, 0x22});
  append(items, {0x52});
  items.push_back(MachineItem::label("high"));
  append(items, {0x0e, 7});
  if (score == Score::SignedSquare)
    append(items, {0x0b, 0x22});
  append(items, {0x52});
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
std::vector<std::string> observe(const std::vector<MachineItem>& items,
                                 const std::string& input) {
  emulator::MK61 calc;
  require(calc.load_program(bytecode(items)).diagnostics.empty(), "composite ROM load");
  constexpr std::array<const char*, 15> names{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  for (std::size_t i = 0; i < names.size(); ++i)
    calc.set_register(names[i], std::to_string(i + 2U));
  calc.set_register("b", input);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(15000, 8).stopped, "composite fixture must return and stop");
  std::vector<std::string> values{calc.display_text(true)};
  calc.press(".");
  values.push_back(calc.display_text(true));
  for (const char* name : {"X", "Y", "Z", "T", "X1"})
    values.push_back(calc.read_register(name));
  for (const char* name : names)
    values.push_back(calc.read_register(name));
  return values;
}
void composition_compiler_rom_and_size_baseline() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  for (const int reg : {2, 8}) {
    const auto before = composite_fixture(reg);
    const auto candidates = passes::finite_mode_representation_candidates(
        raise_machine_to_ir(before), {options}, {.maximum_candidates = 32});
    const auto found = std::find_if(candidates.begin(), candidates.end(), mixed);
    require(found != candidates.end(),
            "exact entry values must unlock a mixed finite/dynamic representation");
    const auto after = lower_ir_to_machine(found->ops);
    require(cells(before) == 47 && cells(after) == 40,
            "composed entry/carrier baseline must shrink 47 to 40 cells");
    require(found->preloads.empty() && build_post_layout_control_flow(after).proved,
            "composed rewrite needs no extra setup and must retain a valid final CFG");
    for (const char* input : {"-7", "0", "13"})
      require(observe(before, input) == observe(after, input),
              "composition must preserve registers, stack, X1, decimal-entry state and returns");
    for (const auto score : {Score::UnknownArm, Score::ZeroArm}) {
      const auto rejected = passes::finite_mode_representation_candidates(
          raise_machine_to_ir(composite_fixture(reg, score)), {options},
          {.maximum_candidates = 32});
      require(std::none_of(rejected.begin(), rejected.end(), mixed),
              "one unknown or zero score arm must block mixed-epoch absorption");
    }
  }

  const LayoutSearchArtifact root{.items = composite_fixture(8)};
  const auto edges = expand_joint_representation_transactions(root, {}, false);
  require(std::any_of(edges.begin(), edges.end(), [](const auto& edge) {
    return cells(edge.node.items) == 40 &&
        std::any_of(edge.node.optimizations.begin(), edge.node.optimizations.end(),
            [](const auto& report) {
              return report.name == "mixed-epoch-coefficient-state-carrier";
            });
  }), "the production joint search must consume the composed proof");
}

void signed_score_composition() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  for (const int reg : {2, 8}) {
    const auto before = composite_fixture(reg, Score::SignedSquare);
    const auto candidates = passes::finite_mode_representation_candidates(
        raise_machine_to_ir(before), {options}, {.maximum_candidates = 32});
    const auto found = std::find_if(candidates.begin(), candidates.end(), mixed);
    require(found != candidates.end(), "signed entry must unlock positive score proof");
    const auto after = lower_ir_to_machine(found->ops);
    require(cells(before) == 51 && cells(after) == 44,
            "signed-entry/carrier baseline must shrink 51 to 44 cells");
    require(found->preloads.empty() && build_post_layout_control_flow(after).proved,
            "signed-score composition needs no setup and must retain a valid final CFG");
    for (const char* input : {"-7", "0", "13"})
      require(observe(before, input) == observe(after, input),
              "signed-score composition must retain every observable");
  }
  const LayoutSearchArtifact root{.items = composite_fixture(8, Score::SignedSquare)};
  const auto edges = expand_joint_representation_transactions(root, {}, false);
  require(std::any_of(edges.begin(), edges.end(), [](const auto& edge) {
    return cells(edge.node.items) == 44 &&
        std::any_of(edge.node.optimizations.begin(), edge.node.optimizations.end(),
            [](const auto& report) {
              return report.name == "mixed-epoch-coefficient-state-carrier";
            });
  }), "the production joint search must consume signed-entry score bounds");

}


std::vector<MachineItem> masked_selector_fixture(bool guarded = true, bool live_x1 = false,
                                                bool bypass_filter = false) {
  std::vector<MachineItem> items;
  append(items, {0x54, 0x60});
  if (bypass_filter)
    target(items, 0x5e, "mask");
  append(items, guarded ? std::initializer_list<int>{0x64, 0x37}
                        : std::initializer_list<int>{0x54, 0x54});
  items.push_back(MachineItem::label("mask"));
  append(items, {0x6a, 0x37, live_x1 ? 0x0f : 0x35, 0x41, 0xaa});
  items.back().indirect_flow_targets = std::vector<IrTarget>{"leaf"};
  append(items, {0x42});
  // The filler is executed and preserves the observed fractional result.
  while (cells(items) < 33) append(items, {0x31});
  stop(items, StopDisposition::Terminal);
  items.push_back(MachineItem::label("leaf"));
  append(items, {0x52});
  return items;
}

void mask_bits_selector_layout_and_rom() {
  const std::vector<PreloadReport> preloads{
      {.register_name = "4", .value = "88888800"},
      {.register_name = "a", .value = "88888834"}};
  const auto before = masked_selector_fixture();
  const auto flow = build_post_layout_control_flow(before);
  require(flow.proved, "masked selector fixture needs authoritative calls");
  const auto plan = plan_preloaded_indirect_flow_cell_erasure(
      before, preloads, flow, 0, 0);
  require(plan.proved && plan.preloads.at(1).value == "88888833",
          "dead mask bits must allow retargeting a data/control word");
  auto after = before;
  after.erase(after.begin());
  require(cells(before) == 35 && cells(after) == 34,
          "masked selector erasure baseline must shrink 35 to 34 cells");
  require(build_post_layout_control_flow(after).proved,
          "retargeted masked selector must retain a complete final CFG");
  const auto run = [&](const std::vector<MachineItem>& program,
                       const std::vector<PreloadReport>& setup, const std::string& input) {
    emulator::MK61 calc;
    require(calc.command_limit() == 105, "mask contract uses stock MK-61 ROM");
    std::string raw_word;
    if (input == "raw-bcd") {
      require(calc.load_program({0x61, 0x62, 0x38, 0x40, 0x50}).diagnostics.empty(),
              "raw mask operand preparation");
      calc.set_register("1", "66666666");
      calc.set_register("2", "88888888");
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(1000, 8).stopped, "raw operand preparation must stop");
      raw_word = calc.read_register("0");
      require(raw_word.find('E') != std::string::npos ||
                  raw_word.find("Е") != std::string::npos,
              "ROM OR must prepare a nondecimal mantissa");
    }
    require(calc.load_program(bytecode(program)).diagnostics.empty(), "masked selector ROM load");
    for (const auto& preload : setup) calc.set_register(preload.register_name, preload.value);
    if (input == "raw-bcd")
      require(calc.read_register("0") == raw_word, "program load must preserve the prepared BCD word");
    else
      calc.set_register("0", input);
    for (const char* reg : {"X", "Y", "Z", "T", "X1"}) calc.set_register(reg, "12345678");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(3000, 8).stopped, "masked selector must call, return and stop");
    std::vector<std::string> observed;
    for (const char* reg : {"X", "Y", "Z", "T", "X1", "0", "1", "2", "4"})
      observed.push_back(calc.read_register(reg));
    calc.press(".");
    observed.push_back(calc.display_text(true));
    return observed;
  };
  for (const char* input : {"0", "44444.4", "-44444.4", "123456.7", "88888888",
                            "99999999", "1e99", "-1e99", "1e-99", "raw-bcd"})
    require(run(before, preloads, input) == run(after, plan.preloads, input),
            "mask rebind must preserve data, stack, X1, X2 and return behavior");

  for (const auto& unsafe : {masked_selector_fixture(false),
                             masked_selector_fixture(true, true),
                             masked_selector_fixture(true, false, true)}) {
    const auto graph = build_post_layout_control_flow(unsafe);
    require(graph.proved, "negative masked fixture must have valid control flow");
    require(!plan_preloaded_indirect_flow_cell_erasure(unsafe, preloads, graph, 0, 0).proved,
            "unknown operand, live X1 or an unfiltered incoming edge must block mask retuning");
    auto wrong = unsafe;
    wrong.erase(wrong.begin());
    require(run(unsafe, preloads, "99999999") != run(wrong, plan.preloads, "99999999"),
            "each rejected rewrite must have an observable ROM counterexample");
  }
  auto generated = preloads;
  generated[0].setup_target_name = "leaf";
  require(!plan_preloaded_indirect_flow_cell_erasure(before, generated, flow, 0, 0).proved,
          "a generated setup word must not masquerade as an exact mask");

  const auto bounded = prove_post_layout_mantissa_bits(before, preloads, flow, {1}, 0);
  require(bounded.exhausted && bounded.x_possible_bits.empty(),
          "bit-analysis budget exhaustion must not export partial facts");

  // Losing the last two nibbles is a data-flow property, not a mask-name or
  // target-number special case. The bit invariant survives an unbounded loop.
  std::vector<MachineItem> loop;
  append(loop, {0x60, 0x64, 0x37, 0x41});
  loop.push_back(MachineItem::label("again"));
  append(loop, {0x61});
  const auto read = loop.size();
  append(loop, {0x6a, 0x37, 0x41});
  target(loop, 0x51, "again");
  const auto loop_flow = build_post_layout_control_flow(loop);
  require(loop_flow.proved, "bit lattice loop CFG");
  const auto bits = prove_post_layout_mantissa_bits(loop, preloads, loop_flow, {read});
  require(!bits.exhausted && bits.x_possible_bits.contains(read) &&
              (bits.x_possible_bits.at(read) & 0xffU) == 0,
          "the finite may-bit lattice must retain an inductive mask across a loop");

  auto alias = before;
  alias[1].opcode = 0xdd; // Unknown indirect alias may expose the changed word.
  require(!plan_preloaded_indirect_flow_cell_erasure(
      alias, preloads, build_post_layout_control_flow(alias), 0, 0).proved,
      "unknown indirect aliases must fail closed");
  alias[1].indirect_memory_targets = std::vector<int>{10};
  const auto alias_flow = build_post_layout_control_flow(alias);
  require(alias_flow.proved &&
              !plan_preloaded_indirect_flow_cell_erasure(alias, preloads, alias_flow, 0, 0).proved,
          "a known indirect alias of the mask is also a data observation");

  auto coupled = before;
  for (auto& item : coupled)
    if (item.kind == MachineItemKind::Op && item.opcode == 0x64) item.opcode = 0x6c;
  auto mutable_filter = preloads;
  mutable_filter[0].register_name = "c";
  require(!plan_preloaded_indirect_flow_cell_erasure(
      coupled, mutable_filter, build_post_layout_control_flow(coupled), 0, 0).proved,
      "retuning cannot borrow dead bits from another layout-allocatable preload");
}


void digit_shape_join_contracts() {
  for (const bool raw_branch : {false, true}) {
    std::vector<MachineItem> items;
    append(items, {0x60});
    target(items, 0x57, "mask-right");
    append(items, {0x61});
    target(items, 0x51, "mask-joined");
    items.push_back(MachineItem::label("mask-right"));
    append(items, {0x62});
    if (raw_branch) append(items, {0x63, 0x38});
    items.push_back(MachineItem::label("mask-joined"));
    append(items, {0x44, 0x60, 0x64, 0x37});
    const auto masked = items.size();
    append(items, {0x35});
    const auto fractional = items.size();
    stop(items, StopDisposition::Terminal);
    const std::vector<PreloadReport> preloads{
        {.register_name = "1", .value = "88888888"},
        {.register_name = "2", .value = "77777777"},
        {.register_name = "3", .value = "88888888"}};
    const auto proof = ranges(items, {masked, fractional}, preloads);
    if (raw_branch) {
      require(proof.x_before.empty(),
              "a hexadecimal mask path must not acquire an ordinary-number certificate");
    } else {
      require(proof.x_before.at(masked) == OrdinaryNumericRange{8, 9, false} &&
                  proof.x_before.at(fractional) == OrdinaryNumericRange{0, 1, false},
              "digit alternatives 7/8 stay decimal although their may-bit union is F");
      const auto graph = build_post_layout_control_flow(items);
      const auto lift = analyze_post_layout_automatic_entry_lift(items, graph, 100000);
      const auto limited = prove_post_layout_numeric_ranges(
          items, graph, *lift, {masked}, &preloads, 1,
          NumericRangeInputPolicy::ValidatedRegisterTransport);
      require(limited.exhausted && limited.x_before.empty(),
              "the combined numeric/digit fixed point must not publish partial proofs");
    }
  }

  for (const bool bypass : {false, true}) {
    std::vector<MachineItem> items;
    append(items, {0x60, 0x42});
    if (bypass) target(items, 0x57, "mask-done");
    items.push_back(MachineItem::label("mask-loop"));
    append(items, {0x62, 0x61, 0x37, 0x42, 0x60});
    target(items, 0x57, "mask-loop");
    items.push_back(MachineItem::label("mask-done"));
    append(items, {0x62});
    const auto masked = items.size();
    append(items, {0x35});
    const auto fractional = items.size();
    stop(items, StopDisposition::Terminal);
    const std::vector<PreloadReport> preloads{
        {.register_name = "1", .value = "77777777"}};
    const auto proof = ranges(items, {masked, fractional}, preloads);
    require(!proof.exhausted && proof.x_before.contains(masked) != bypass &&
                proof.x_before.contains(fractional) != bypass,
            "an unbounded filtered loop must prove its invariant, but an unfiltered entry cannot");
  }

  for (const bool overwrite : {false, true}) {
    std::vector<MachineItem> items;
    append(items, {0x60, 0xb7, 0x60, 0x61, 0x37});
    items[1].indirect_memory_targets = std::vector<int>{overwrite ? 1 : 2};
    const auto masked = items.size();
    stop(items, StopDisposition::Terminal);
    const std::vector<PreloadReport> preloads{
        {.register_name = "1", .value = "88888888"},
        {.register_name = "7", .value = overwrite ? "1" : "2"}};
    const auto proof = ranges(items, {masked}, preloads);
    require(proof.x_before.contains(masked) != overwrite,
            "digit facts must respect indirect stores that alias the mask");
  }
}

void digit_shape_nibble_compiler_and_rom() {
  const auto word = [](int digit) { return "8" + std::string(7, static_cast<char>('0' + digit)); };
  const auto boundary = [](int digit) {
    return digit == 0 || digit == 7 || digit == 8 || digit == 9 || digit == 10 || digit == 15;
  };
  for (int a = 0; a < 16; ++a)
    for (int b = 0; b < 16; ++b)
      for (const int operation : {0x37, 0x38, 0x39}) {
        // Two ordinary decimal operands construct every hexadecimal payload
        // through the ROM's OR, rather than injecting an invented raw spelling.
        const std::vector<PreloadReport> preloads{
            {.register_name = "1", .value = word(a & 7)},
            {.register_name = "2", .value = word(a & 8)},
            {.register_name = "3", .value = word(b & 7)},
            {.register_name = "4", .value = word(b & 8)}};
        std::vector<MachineItem> items;
        append(items, {0x61, 0x62, 0x38, 0x40, 0x63, 0x64, 0x38, 0x45,
                       0x60, 0x65, operation});
        const auto value = items.size();
        append(items, {0x46, 0x34});
        const auto integer = items.size();
        stop(items, StopDisposition::Terminal);
        const auto proof = ranges(items, {value, integer}, preloads);
        const int digit = operation == 0x37 ? a & b : operation == 0x38 ? a | b : a ^ b;
        require(!proof.exhausted &&
                    proof.x_before.at(integer) == OrdinaryNumericRange{8, 8, true},
                "integer extraction from a proved positive order-zero 8.x word is eight");
        if (digit <= 9) {
          const auto expected = bound_ordinary_decimal(
              "8." + std::string(7, static_cast<char>('0' + digit)));
          require(proof.x_before.contains(value) && proof.x_before.at(value) == *expected,
                  "all 16x16 payload combinations must retain exact decimal enclosures");
        } else {
          require(!proof.x_before.contains(value),
                  "hexadecimal payloads remain nonnumeric even when their bitwise format is known");
        }
        if (!boundary(a) || !boundary(b)) continue;
        emulator::MK61 calc;
        require(calc.load_program(bytecode(items)).diagnostics.empty(), "digit-shape ROM load");
        for (const auto& preload : preloads)
          calc.set_register(preload.register_name, preload.value);
        calc.press_sequence({"В/О", "С/П"});
        require(calc.run_until_stable(2000, 8).stopped, "digit-shape ROM must stop");
        require(calc.read_register("X").starts_with("8,"),
                "ROM integer extraction must agree for ordinary and raw payloads");
        if (digit <= 9)
          require(calc.read_register("6").starts_with(
                      digit == 0 ? "8," : "8," + std::string(7, static_cast<char>('0' + digit))),
                  "ROM logical mantissas must agree with the digit-set transfer table");
      }

  std::vector<MachineItem> inverse;
  append(inverse, {0x60, 0x3a});
  const auto raw = inverse.size();
  append(inverse, {0x34});
  const auto integer = inverse.size();
  stop(inverse, StopDisposition::Terminal);
  const auto proof = ranges(inverse, {raw, integer});
  require(!proof.x_before.contains(raw) &&
              proof.x_before.at(integer) == OrdinaryNumericRange{8, 8, true},
          "NOT certifies a word format, not decimal digits of an unknown input");
}

} // namespace

int main() {
  try {
    exact_x_is_not_a_stack_lift_certificate();
    integer_sign_closes_entry_without_claiming_stack_lift();
    exact_caller_contexts_and_input_boundaries();
    composition_compiler_rom_and_size_baseline();
    signed_score_composition();
    mask_bits_selector_layout_and_rom();
    digit_shape_join_contracts();
    digit_shape_nibble_compiler_and_rom();
    std::cout << "numeric entry value compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

