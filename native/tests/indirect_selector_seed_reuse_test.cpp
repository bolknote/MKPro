#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/helper_invariant_recall_hoist.hpp"
#include "mkpro/core/passes/indirect_selector_seed_reuse.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/shared_helper_dual_mode_layout.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>

namespace mkpro {
bool refresh_proved_layout_annotations_for_testing(CompileResult&, const CompileOptions&);
std::optional<std::map<std::size_t, int>> fixed_direct_address_targets_for_testing(
    const std::vector<MachineItem>&, const CompileOptions&);
}

namespace mkpro::tests {
namespace {

MachineItem op(int opcode) {
  return MachineItem::op(opcode, opcode_by_code(opcode).name);
}

MachineItem stop() {
  MachineItem result = op(0x50);
  result.stop_disposition = StopDisposition::Terminal;
  return result;
}

std::vector<MachineItem> probe(int reg) {
  MachineItem access = op(0xd0 + reg);
  access.indirect_memory_targets = std::vector<int>{7};
  // A called producer and a called consumer exercise actual return contexts.
  // Four unrelated loads erase all stack/X2 differences introduced by the
  // removed seed literal before the selector is observed as memory addressing.
  return {
      op(0x53), MachineItem::address("producer"),
      op(0x01), op(0x4e), op(0x08), op(0x40 + reg),
      op(0x53), MachineItem::address("consumer"), stop(),
      MachineItem::label("producer"), op(0x68), op(0x69), op(0x38), op(0x4b), op(0x52),
      MachineItem::label("consumer"), op(0x68), op(0x69), op(0x68), op(0x69),
      access, op(0x60 + reg), op(0x52),
  };
}

core::passes::PassResult optimize(const std::vector<MachineItem>& items, bool allow_neutral = false) {
  CompileOptions options;
  options.allow_size_neutral_selector_seed_reuse = allow_neutral;
  return core::passes::indirect_selector_seed_reuse(
      raise_machine_to_ir(items), core::passes::PassContext{options});
}

std::vector<int> codes(const std::vector<IrOp>& ops) {
  const auto resolved = resolve_machine_items(lower_ir_to_machine(ops), CompileOptions{});
  require(resolved.diagnostics.empty(), "seed fixture must assemble through the real emitter");
  std::vector<int> result;
  for (const auto& step : resolved.steps)
    result.push_back(step.opcode);
  return result;
}

std::string compact(const std::string& value) {
  std::string result;
  for (const char ch : value) {
    if (ch != ' ' && ch != '\t' && ch != '\n')
      result += ch;
  }
  return result;
}

std::vector<std::string> run(const std::vector<int>& program,
                             const std::string& a, const std::string& b) {
  emulator::MK61 calc;
  calc.set_register("7", "1234567");
  calc.set_register("8", a);
  calc.set_register("9", b);
  calc.set_register("0", "51");
  calc.set_register("1", "62");
  calc.set_register("2", "73");
  calc.set_register("3", "84");
  require(calc.load_program(program).diagnostics.empty(), "seed probe should load");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(2000, 6).stopped, "seed probe should stop");
  std::vector<std::string> result{compact(calc.display_text())};
  for (const std::string name : {"x", "y", "z", "t", "x1", "0", "1", "2", "3",
                                 "7", "8", "9", "b", "e"})
    result.push_back(compact(calc.read_register(name)));
  return result;
}

void reject(const std::vector<MachineItem>& items, const std::string& why) {
  const auto result = optimize(items);
  require(result.applied == 0, "selector seed must reject " + why);
  require(codes(result.ops) == codes(raise_machine_to_ir(items)),
          "rejected selector seed must leave code unchanged: " + why);
}

} // namespace

void indirect_selector_seed_reuse_preserves_observations() {
  {
    auto loop = op(0x8b);
    loop.indirect_flow_targets = std::vector<IrTarget>{std::string("main")};
    auto call = op(0xac);
    call.indirect_flow_targets = std::vector<IrTarget>{std::string("worker")};
    auto prompt = stop();
    prompt.stop_disposition = StopDisposition::Resumable;
    std::vector<MachineItem> items{
        loop, MachineItem::label("main"), call, op(0x6c), prompt, loop,
        MachineItem::label("worker"), op(0x61), op(0x62), op(0x10), op(0x52), op(0x54),
    };
    std::vector<PreloadReport> preloads{
        {.register_name = "b", .value = "1"}, {.register_name = "c", .value = "5"},
    };
    core::PostLayoutControlFlowOptions control_options;
    control_options.empty_return_target = 1;
    const auto control = core::build_post_layout_control_flow(items, control_options);
    require(control.proved, "selector-release composition needs a proved original CFG");
    core::NaturalTargetComponentLayoutOptions layout_options;
    layout_options.allow_size_neutral_absolute_layout = true;
    layout_options.require_size_neutral_absolute_layout = true;
    layout_options.required_absolute_targets = {{.target_item = 7, .target_address = 6}};
    const auto variants = core::reassign_stable_indirect_selector_families(
        items, preloads, control, layout_options);
    require(!variants.empty(),
            "releasing the complete loop family must let a data-fixed helper move");
    const auto play = [&](const std::vector<MachineItem>& program,
                          const std::vector<PreloadReport>& setup) {
      emulator::MK61 calc;
      for (const auto& preload : setup)
        calc.set_register(preload.register_name, preload.value);
      calc.set_register("1", "2");
      calc.set_register("2", "3");
      require(calc.load_program(codes(raise_machine_to_ir(program))).diagnostics.empty(),
              "released-selector composition must load");
      calc.press_sequence({"В/О", "С/П"});
      std::vector<std::string> states;
      for (const std::string input : {"", "44", "-7"}) {
        if (!input.empty()) {
          calc.input_number(input);
          calc.press("С/П");
        }
        require(calc.run_until_stable(1500, 6).stopped,
                "released-selector composition must preserve every prompt");
        states.push_back(compact(calc.display_text()));
        for (const std::string reg : {"x", "y", "z", "t", "x1", "0", "1", "2", "3", "c"}) {
          const auto value = calc.read_register(reg);
          // C is the numeric data constant 5, not an observation of the
          // debugger's leading-zero padding after indirect flow decoding.
          states.push_back(reg == "c" ? std::to_string(std::stod(value)) : compact(value));
        }
      }
      return states;
    };
    const auto expected = play(items, preloads);
    for (const auto& variant : variants) {
      require(variant.control_flow.proved && variant.control_flow.empty_return_target.has_value() &&
                  variant.control_flow.empty_return_target->address == 1,
              "selector release must preserve the physical empty-return continuation");
      require(std::any_of(variant.optimizations.begin(), variant.optimizations.end(),
                          [](const auto& applied) { return applied.name == "empty-return-selector-release"; }),
              "the alternative must explain the neutral selector-release stage");
      require(codes(raise_machine_to_ir(variant.items)).size() ==
                  codes(raise_machine_to_ir(items)).size(),
              "selector release must not claim a size saving before downstream fusion");
      const auto actual = play(variant.items, variant.preloads);
      require(actual.size() == expected.size(), "selector-release observation arity must match");
      for (std::size_t index = 0; index < expected.size(); ++index)
        require(actual.at(index) == expected.at(index),
                "selector-release observation " + std::to_string(index) + ": expected " +
                    expected.at(index) + ", got " + actual.at(index));
    }
    auto written = items;
    written.at(7) = op(0x4b);
    require(core::reassign_stable_indirect_selector_families(
                written, preloads, core::build_post_layout_control_flow(written, control_options),
                layout_options).empty(),
            "a runtime-written selector must not become a borrowed call address");
    auto observable = items;
    observable.at(3) = op(0x6b);
    require(core::reassign_stable_indirect_selector_families(
                observable, preloads, core::build_post_layout_control_flow(observable, control_options),
                layout_options).empty(),
            "retuning must reject an observable numeric use of the freed selector");
    auto incomplete = items;
    incomplete.at(0).indirect_flow_targets.reset();
    require(core::reassign_stable_indirect_selector_families(
                incomplete, preloads, core::build_post_layout_control_flow(incomplete, control_options),
                layout_options).empty(),
            "a partially known loop family must never be treated as fully released");
    auto partial = items;
    partial.at(5) = op(0x7b);
    partial.at(5).indirect_flow_targets = std::vector<IrTarget>{std::string("main")};
    const auto partial_control = core::build_post_layout_control_flow(partial, control_options);
    require(partial_control.proved, "partial-family rejection fixture must have a complete CFG");
    const auto partial_variants = core::reassign_stable_indirect_selector_families(
        partial, preloads, partial_control, layout_options);
    for (const auto& variant : partial_variants)
      require(std::none_of(variant.optimizations.begin(), variant.optimizations.end(),
                           [](const auto& applied) { return applied.name == "empty-return-selector-release"; }),
              "a surviving conditional use must prevent borrowing the loop selector");
  }
  {
    const std::vector<MachineItem> original = {
        op(0x52), op(0x61), op(0x53), MachineItem::address("worker"), stop(),
        op(0x52), MachineItem::label("worker"), op(0x62), op(0x10), op(0x52),
    };
    core::PostLayoutControlFlowOptions flow_options;
    flow_options.empty_return_target = 1;
    const auto flow = core::build_post_layout_control_flow(original, flow_options);
    require(flow.proved, "empty-return layout fixture must have a closed hardware CFG");
    core::NaturalTargetComponentLayoutOptions options;
    options.allow_size_neutral_absolute_layout = true;
    options.require_size_neutral_absolute_layout = true;
    options.required_absolute_targets = {{.target_item = 7, .target_address = 5}};
    const auto moved = core::optimize_natural_target_component_layout(original, {}, flow, options);
    const auto moved_flow = core::build_post_layout_control_flow(moved.items, flow_options);
    require(moved.applied > 0 && moved.plan.final_artifact_proved &&
                moved_flow.proved && moved_flow.empty_return_target.has_value() &&
                moved_flow.empty_return_target->address == 1,
            "ordinary helper movement must retain the physical empty-return continuation");
    require(run(codes(raise_machine_to_ir(original)), "0", "0") ==
                run(codes(raise_machine_to_ir(moved.items)), "0", "0"),
            "a cold-start return must still reach the same main program after layout");
    options.required_absolute_targets.front().target_address = 1;
    const auto rejected = core::optimize_natural_target_component_layout(original, {}, flow, options);
    require(rejected.applied == 0,
            "layout must not move the main continuation and pretend hardware returns to its new address");
  }
  {
    std::vector<MachineItem> pending(130, op(0x54));
    pending.at(0) = op(0x53);
    pending.at(1) = MachineItem::address(123);
    pending.at(2) = stop();
    pending.at(123) = op(0x52);
    for (const bool analysis : {false, true}) {
      CompileOptions options;
      options.analysis = analysis;
      const auto targets = fixed_direct_address_targets_for_testing(pending, options);
      require(targets.has_value() && targets->at(1) == 123,
              "an over-window logical address must never be decoded from its listing placeholder");
    }
    auto official = pending;
    official.at(1).target = 70;
    const auto ordinary = fixed_direct_address_targets_for_testing(official, CompileOptions{});
    require(ordinary.has_value() && ordinary->at(1) == 70,
            "ordinary numeric targets must retain their physical command identity");
    auto side = pending;
    side.at(1).formal_opcode = 0xeb;
    const auto formal = fixed_direct_address_targets_for_testing(side, CompileOptions{});
    require(formal.has_value() && formal->at(1) == 39,
            "an explicitly encoded side-space address must still use hardware decoding");
    auto outside = pending;
    outside.at(1).target = 140;
    require(!fixed_direct_address_targets_for_testing(outside, CompileOptions{}).has_value(),
            "a logical address outside the artifact must fail the direct-target proof");
  }
  {
    MachineItem address = MachineItem::address("worker");
    address.formal_opcode = 0x04;
    const std::vector<MachineItem> original = {
        op(0x53), address, stop(), op(0x54), MachineItem::label("worker"),
        op(0x61), op(0x62), op(0x10), op(0x52),
    };
    auto reduced = original;
    reduced.erase(reduced.begin() + 3);
    const auto rebound = core::normalize_natural_target_overflow_formals(reduced);
    require(rebound.has_value() && !rebound->at(1).formal_opcode.has_value() &&
                std::get<std::string>(rebound->at(1).target) == "worker",
            "an ordinary cached address must follow its surviving symbolic command identity");
    require(run(codes(raise_machine_to_ir(original)), "0", "0") ==
                run(codes(raise_machine_to_ir(*rebound)), "0", "0"),
            "erasing a preceding cell must preserve calls, returns and all observed stack registers");

    auto physical = reduced;
    physical.at(1).target = 4;
    const auto numeric = core::normalize_natural_target_overflow_formals(physical);
    require(numeric.has_value() && numeric->at(1).formal_opcode == 0x04 &&
                std::get<int>(numeric->at(1).target) == 4,
            "an explicit numeric target must not silently become relocatable");
    auto side = reduced;
    side.at(1).formal_opcode = 0xeb;
    const auto side_result = core::normalize_natural_target_overflow_formals(side);
    require(side_result.has_value() && side_result->at(1).formal_opcode == 0xeb,
            "normalization must preserve side-space return/continuation semantics");
    auto opaque = reduced;
    opaque.at(1).roles = {"exec"};
    const auto opaque_result = core::normalize_natural_target_overflow_formals(opaque);
    require(opaque_result.has_value() && opaque_result->at(1).formal_opcode == 0x04,
            "an executable address overlay must retain its exact byte contract");
    auto raw = reduced;
    raw.at(1).raw = true;
    const auto raw_result = core::normalize_natural_target_overflow_formals(raw);
    require(raw_result.has_value() && raw_result->at(1).formal_opcode == 0x04,
            "raw address operands must not be rewritten by label-cache normalization");
  }
  {
    CompileOptions options;
    CompileResult artifact;
    MachineItem call = op(0xab);
    call.indirect_flow_targets = std::vector<IrTarget>{std::string("work")};
    call.comment = "ordinary call; preloaded Rc=88888858 indirect-target=58 indirect flow";
    artifact.items = {call, stop(), MachineItem::label("work"),
                      op(0x61), op(0x62), op(0x10), op(0x52)};
    artifact.preloads = {{.register_name = "b", .value = "2"}};
    const auto before = codes(raise_machine_to_ir(artifact.items));
    require(refresh_proved_layout_annotations_for_testing(artifact, options),
            "a complete selector-family relocation must refresh legacy proof annotations");
    require(artifact.items.front().comment->find("preloaded Rb=2 indirect-target=2") !=
                std::string::npos &&
                artifact.items.front().comment->find("preloaded Rc=") == std::string::npos,
            "annotations must use the delivered register, value and command identity");
    require(codes(raise_machine_to_ir(artifact.items)) == before,
            "refreshing proof annotations must never change machine code");
    artifact.preloads.front().value = "3";
    const auto preserved = artifact.items.front().comment;
    require(!refresh_proved_layout_annotations_for_testing(artifact, options) &&
                artifact.items.front().comment == preserved,
            "an inconsistent delivered selector must fail transactionally, not acquire a proof");

    MachineItem high = op(0);
    MachineItem low = op(4);
    high.roles = {"late-decimal-selector-high:body"};
    low.roles = {"late-decimal-selector-low:body"};
    high.comment = low.comment = "callee-hole selector-value=39 indirect-target=39";
    CompileResult charge;
    charge.items = {high, low, op(0x4e), stop(), MachineItem::label("body"),
                    op(0x61), stop()};
    require(refresh_proved_layout_annotations_for_testing(charge, options) &&
                charge.items.front().comment == "callee-hole selector-value=4 indirect-target=4",
            "a moved late-bound charge must publish its actual literal and symbolic leaf");
    charge.items.at(1).opcode = 5;
    const auto old_comment = charge.items.front().comment;
    require(!refresh_proved_layout_annotations_for_testing(charge, options) &&
                charge.items.front().comment == old_comment,
            "an incorrect literal must not be repaired by rewriting its proof text");
  }
  {
    emulator::MK61 calc;
    calc.set_register("1", "62");
    calc.set_register("2", "73");
    calc.load_program({0x61, 0x62, 0x12, 0x0f, 0x50});
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(500, 6).stopped, "F Bx hardware fact must stop");
    require(compact(calc.read_register("x")) == "73," &&
                compact(calc.read_register("y")) == "4526," &&
                compact(calc.read_register("x1")) == "73,",
            "F Bx must restore last-X and lift the previous result, not pop the stack");
    core::StackValueEqualityState unequal_last_x{{true, true, true, true}, true, false};
    require(core::transfer_decimal_digit_equality(unequal_last_x, false) ==
                core::StackValueEqualityTransfer::Continue && !unequal_last_x.x1_equal,
            "a fresh digit must not erase physical X1 differences");
    core::transfer_stack_value_equality(unequal_last_x, 0x0f,
                                        core::StackValueEqualityStepKind::Plain);
    require(!unequal_last_x.stack_equal.at(0) && unequal_last_x.stack_equal.at(1),
            "last-X differences must become visible through F Bx");
    core::StackValueEqualityState rotates{{false, true, true, true}, true, true};
    core::transfer_stack_value_equality(rotates, 0x25, core::StackValueEqualityStepKind::Plain);
    require(!rotates.stack_equal.at(3) && !rotates.x1_equal,
            "F reverse must keep the old X in T and physical X1");
  }
  for (int reg = 0; reg < 4; ++reg) {
    const auto original = probe(reg);
    const auto result = optimize(original);
    require(result.applied == 1, "seed reuse should work in every predecrement register");
    const auto before = codes(raise_machine_to_ir(original));
    const auto after = codes(result.ops);
    require(before.size() == after.size() + 1U, "seed reuse should save exactly one cell");
    require(core::build_post_layout_control_flow(lower_ir_to_machine(result.ops)).proved,
            "rewritten selector seed must retain a complete CFG");
    require(optimize(lower_ir_to_machine(result.ops)).applied == 0,
            "seed reuse should be idempotent");
    for (const auto& values : std::array<std::array<std::string, 2>, 5>{{
             {{"0", "0"}}, {{"12345678", "87654321"}}, {{"-0.000001", "9E20"}},
             {{"88888888", "77777777"}}, {{"0.99999999", "1E-99"}},
         }}) {
      require(run(before, values.at(0), values.at(1)) ==
                  run(after, values.at(0), values.at(1)),
              "seed reuse must preserve display, X/Y/Z/T/X1, registers and return continuation");
    }
    // X2 is an internal copy, not a register exposed by the emulator API.
    // Observe it through the real dot instruction before the terminal stop.
    auto observes_x2 = original;
    observes_x2.insert(observes_x2.begin() + 8, op(0x0a));
    const auto observed_result = optimize(observes_x2);
    require(observed_result.applied == 1, "X2 must converge before its dot observer");
    require(run(codes(raise_machine_to_ir(observes_x2)), "12345678", "87654321") ==
                run(codes(observed_result.ops), "12345678", "87654321"),
            "real dot observation must confirm hidden X2 equivalence");
    auto store_probe = original;
    store_probe.at(20).opcode = 0xb0 + reg;
    store_probe.at(20).mnemonic = opcode_by_code(0xb0 + reg).name;
    store_probe.insert(store_probe.begin() + 21, op(0x0a));
    const auto store_result = optimize(store_probe);
    require(store_result.applied == 1, "indirect-store seed should retain its X2 proof");
    require(run(codes(raise_machine_to_ir(store_probe)), "88888888", "77777777") ==
                run(codes(store_result.ops), "88888888", "77777777"),
            "indirect store must preserve stack/X2 even for hex fractional tails");
  }
  {
    // A helper's complete memory-target set can cover several call contexts.
    // The first-use proof knows the exact 8 -> 7 transition in this context.
    auto items = probe(0);
    items.at(20).indirect_memory_targets = std::vector<int>{4, 5, 6, 7};
    require(optimize(items).applied == 1, "context-specific seed must refine a bank target set");
  }
  {
    auto terminal = probe(0);
    terminal.erase(terminal.begin() + 18, terminal.begin() + 20);
    terminal.at(18).opcode = 0xb0;
    terminal.at(18).mnemonic = opcode_by_code(0xb0).name;
    terminal.at(19) = op(0x0a);
    const auto result = optimize(terminal);
    require(result.applied == 1, "typed terminal result may discard dead Z/T values");
    const auto before = run(codes(raise_machine_to_ir(terminal)), "12345678", "87654321");
    const auto after = run(codes(result.ops), "12345678", "87654321");
    for (const std::size_t index : {0U, 1U, 2U, 5U, 6U, 10U})
      require(before.at(index) == after.at(index), "terminal observable state must agree");
    require(before.at(3) != after.at(3) || before.at(4) != after.at(4),
            "terminal fixture must actually exercise dead deep-stack differences");
    terminal.at(8).stop_disposition = StopDisposition::Resumable;
    terminal.insert(terminal.begin() + 9, op(0x14));
    terminal.insert(terminal.begin() + 10, stop());
    reject(terminal, "dead-stack assumptions at a resumable stop");
  }
  {
    auto items = probe(0);
    // Two independently reached OR producers. Cloning the selector store is
    // locally neutral and must remain opt-in until full layout repays it.
    std::vector<MachineItem> branched = {
        op(0x68), op(0x5e), MachineItem::address("other"),
        op(0x53), MachineItem::address("producer"),
        op(0x51), MachineItem::address("join"),
        MachineItem::label("other"), op(0x53), MachineItem::address("second"),
        MachineItem::label("join"),
    };
    branched.insert(branched.end(), items.begin() + 2, items.end());
    branched.push_back(MachineItem::label("second"));
    branched.insert(branched.end(), items.begin() + 10, items.begin() + 15);
    reject(branched, "a neutral rewrite without a whole-result comparison");
    const auto result = optimize(branched, true);
    require(result.applied == 1, "neutral candidate should cover both incoming producers");
    const auto before = codes(raise_machine_to_ir(branched));
    const auto after = codes(result.ops);
    require(before.size() == after.size(), "two-producer seed must not grow the IR");
    for (const std::string value : {"0", "12345678"})
      require(run(before, value, "87654321") == run(after, value, "87654321"),
              "neutral seed candidate must preserve both control paths");
  }
  {
    auto items = probe(0);
    items.insert(items.begin() + 4, op(0x60));
    reject(items, "an ordinary read before initialization");
  }
  {
    const auto base = probe(0);
    std::vector<MachineItem> items = {
        op(0x68), op(0x5e), MachineItem::address("other"),
        op(0x68), op(0x53), MachineItem::address("mask"), op(0x38), op(0x4b),
        op(0x51), MachineItem::address("join"),
        MachineItem::label("other"), op(0x68), op(0x53), MachineItem::address("mask"),
        op(0x38), op(0x4b), MachineItem::label("join"),
    };
    items.insert(items.end(), base.begin() + 2, base.begin() + 9);
    items.insert(items.end(), base.begin() + 15, base.end());
    items.insert(items.end(), {MachineItem::label("mask"), op(0x69), op(0x52)});
    const auto seed = optimize(items, true);
    require(seed.applied == 1, "two caller continuations should reuse the selector seed");
    const auto seeded = lower_ir_to_machine(seed.ops);
    const core::HelperInvariantRecallHoistOptions hoist_options{
        .allow_before_call_commutative_tail = true,
    };
    const auto hoisted = core::optimize_helper_invariant_recall_hoist(seeded, hoist_options);
    require(hoisted.applied == 1 && hoisted.proof.final_artifact_proved,
            "neutral seed should compose with common-operand hoisting before layout");
    require(hoisted.proof.insertion == core::HelperInvariantRecallInsertion::HelperRoot,
            "a commuted tail with live physical X1 must lose to the exact root placement");
    const auto before = codes(raise_machine_to_ir(items));
    const auto after = codes(raise_machine_to_ir(hoisted.items));
    require(before.size() == after.size() + 1U,
            "complete seed/operand composition must repay one cell");
    require(core::build_post_layout_control_flow(hoisted.items).proved,
            "composed seed/operand rewrite must preserve the final CFG");
    for (const std::string value : {"0", "12345678"}) {
      const auto expected = run(before, value, "87654321");
      const auto actual = run(after, value, "87654321");
      for (std::size_t index = 0; index < expected.size(); ++index)
        require(expected.at(index) == actual.at(index),
                "composed seed/operand state " + std::to_string(index) + " for " + value +
                    ": expected " + expected.at(index) + ", got " + actual.at(index));
    }
    items.erase(items.begin() + 11);
    const auto incomplete = optimize(items, true);
    require(incomplete.applied == 1, "seed proof should remain independent of operand hoisting");
    require(core::optimize_helper_invariant_recall_hoist(
                lower_ir_to_machine(incomplete.ops), hoist_options).applied == 0,
            "operand hoisting must reject a call without the common operand");
    items.insert(items.begin() + 13, op(0x68));
    const auto mixed = optimize(items, true);
    require(mixed.applied == 1, "both mixed operand placements still produce the same seed class");
    require(core::optimize_helper_invariant_recall_hoist(
                lower_ir_to_machine(mixed.ops), hoist_options).applied == 0,
            "mixed placements cannot both commute a join while preserving live physical X1");
  }
  {
    const auto base = probe(0);
    MachineItem prompt = op(0x50);
    prompt.stop_disposition = StopDisposition::Resumable;
    prompt.manual_interaction = ManualInteractionAnchor{
        .protocol_id = 17, .phase = -1, .kind = ManualInteractionAnchorKind::PromptStop};
    MachineItem input = op(0x48);
    input.manual_interaction = ManualInteractionAnchor{
        .protocol_id = 17, .phase = 0, .kind = ManualInteractionAnchorKind::ContinuousResume};
    std::vector<MachineItem> retry = {
        MachineItem::label("retry"), prompt, input,
        op(0x53), MachineItem::address("producer"), op(0x68),
        op(0x5e), MachineItem::address("accepted"),
        op(0x51), MachineItem::address("retry"), MachineItem::label("accepted"),
    };
    retry.insert(retry.end(), base.begin() + 2, base.end());
    const auto result = optimize(retry);
    require(result.applied == 1, "dead speculative seed may cross a typed retry prompt");
    const auto play = [&](const std::vector<int>& program) {
      emulator::MK61 calc;
      calc.set_register("0", "51");
      calc.set_register("7", "1234567");
      calc.set_register("9", "87654321");
      calc.load_program(program);
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(2000, 6).stopped, "retry initial prompt must stop");
      std::vector<std::string> observations;
      for (const std::string value : {"0", "1"}) {
        calc.input_number(value);
        calc.press("С/П");
        require(calc.run_until_stable(2000, 6).stopped, "retry continuation must stop");
        observations.push_back(compact(calc.display_text()));
        for (const std::string name : {"x", "y", "z", "t", "x1"})
          observations.push_back(compact(calc.read_register(name)));
      }
      observations.push_back(compact(calc.read_register("0")));
      return observations;
    };
    require(play(codes(raise_machine_to_ir(retry))) == play(codes(result.ops)),
            "retry UI and the eventual initialized register must remain identical");
    retry.insert(retry.begin() + 3, op(0x60));
    reject(retry, "a scratch value read after resuming a retry prompt");
  }
  {
    auto items = probe(0);
    items.insert(items.begin() + 16, op(0x60));
    reject(items, "an ordinary read before selector truncation");
  }
  {
    auto items = probe(0);
    items.insert(items.begin() + 16, stop());
    reject(items, "a visible stop before convergence");
  }
  {
    auto items = probe(0);
    items.insert(items.begin() + 16, op(0x10));
    reject(items, "arithmetic observing a removed stack lift");
  }
  {
    auto items = probe(0);
    items.at(12).opcode = 0x37;
    reject(items, "an uncertified producer opcode");
  }
  {
    auto items = probe(0);
    items.at(20).indirect_memory_targets.reset();
    reject(items, "an unknown indirect target");
  }
  {
    auto items = probe(0);
    items.at(20).indirect_memory_targets = std::vector<int>{0, 7};
    reject(items, "an aliased indirect read");
  }
  {
    auto items = probe(0);
    items.at(4).raw = true;
    reject(items, "a raw literal");
  }
  {
    auto items = probe(0);
    items.at(1).target = 9;
    reject(items, "frozen physical control flow");
  }
  {
    auto items = probe(0);
    items.at(4).manual_interaction = ManualInteractionAnchor{
        .protocol_id = 3, .phase = 0,
        .kind = ManualInteractionAnchorKind::ContinuousResume};
    reject(items, "a manual entry that bypasses the producer");
  }
  {
    // Different fractional tails, including hex nibbles, must all disappear
    // before the first R0 indirect access. Exercise the same fact independently
    // of the optimizer and of its flow/stack proof.
    for (const std::string value : {"8.0000001", "8.9999999", "8.ABCDAB1"}) {
      emulator::MK61 calc;
      calc.set_register("0", value);
      calc.set_register("7", "1234");
      calc.load_program({0xd0, 0x50});
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(500, 6).stopped, "fractional selector fact should stop");
      require(compact(calc.read_register("0")) == "00000007,",
              "predecrement must discard the full fractional tail");
      require(compact(calc.display_text()) == "1234,",
              "fractional seed must select the same memory register");
    }
  }
  {
    // Two equal-width components can exchange physical positions, but only one
    // selector's numerical value is free: C is also ordinary observable data.
    auto conditional = op(0xec);
    conditional.indirect_flow_targets = std::vector<IrTarget>{std::string("a")};
    auto call = op(0xab);
    call.indirect_flow_targets = std::vector<IrTarget>{std::string("b")};
    std::vector<MachineItem> items = {
        op(0x53), MachineItem::address("worker"), op(0x6c), op(0x10), stop(),
        MachineItem::label("a"), op(0x61), op(0x62), op(0x10), op(0x40), op(0x52),
        MachineItem::label("b"), op(0x63), op(0x64), op(0x10), op(0x40), op(0x52),
        MachineItem::label("worker"), op(0x65), conditional, call, op(0x52),
    };
    std::vector<PreloadReport> preloads{
        {.register_name = "c", .value = "5"}, {.register_name = "b", .value = "10"},
    };
    const auto variants = core::reassign_stable_indirect_selector_families(
        items, preloads, core::build_post_layout_control_flow(items));
    require(!variants.empty(), "fixed call/conditional selector families should be reassignable");
    const auto play = [&](const std::vector<MachineItem>& program,
                          const std::vector<PreloadReport>& setup, const std::string& branch,
                          bool resume = false) {
      emulator::MK61 calc;
      for (const auto& preload : setup)
        calc.set_register(preload.register_name, preload.value);
      calc.set_register("1", "2");
      calc.set_register("2", "3");
      calc.set_register("3", "5");
      calc.set_register("4", "7");
      calc.set_register("5", branch);
      require(calc.load_program(codes(raise_machine_to_ir(program))).diagnostics.empty(),
              "selector-family fixture should load");
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(2000, 6).stopped, "selector-family fixture should return");
      std::vector<std::string> observed;
      const auto snapshot = [&] {
        observed.push_back(compact(calc.display_text()));
        for (const std::string name : {"x", "y", "z", "t", "x1", "0", "1", "2", "3", "4", "5"})
          observed.push_back(compact(calc.read_register(name)));
        // This fixture observes C numerically, not as raw leading-zero padding.
        observed.push_back(std::to_string(std::stod(calc.read_register("c"))));
      };
      snapshot();
      if (resume) {
        calc.input_number("44");
        calc.press("С/П");
        require(calc.run_until_stable(2000, 6).stopped, "manual selector continuation must stop");
        snapshot();
      }
      return observed;
    };
    for (const auto& variant : variants) {
      require(variant.applied == 1 && variant.control_flow.proved,
              "selector-family alternative must have a final authoritative CFG");
      require(codes(raise_machine_to_ir(variant.items)).size() <=
                  codes(raise_machine_to_ir(items)).size(),
              "selector-family allocation must not grow its transaction seed");
      for (const std::string branch : {"0", "1"}) {
        const auto expected = play(items, preloads, branch);
        const auto actual = play(variant.items, variant.preloads, branch);
        std::string artifact;
        for (int byte : codes(raise_machine_to_ir(variant.items)))
          artifact += " " + std::to_string(byte);
        for (const auto& preload : variant.preloads)
          artifact += " R" + preload.register_name + "=" + preload.value;
        for (std::size_t index = 0; index < expected.size(); ++index)
          require(expected.at(index) == actual.at(index),
                  "selector-family state " + std::to_string(index) + " for " + branch +
                      ": expected " + expected.at(index) + ", got " + actual.at(index) + artifact);
      }
    }
    const auto rejects_family = [&](const std::vector<MachineItem>& bad, const std::string& why) {
      require(core::reassign_stable_indirect_selector_families(
                  bad, preloads, core::build_post_layout_control_flow(bad)).empty(),
              "selector-family exchange must reject " + why);
    };
    auto written = items;
    written.at(9) = op(0x4b);
    rejects_family(written, "a runtime-written selector");
    auto aliased = items;
    aliased.at(9) = op(0xb0);
    rejects_family(aliased, "an unknown indirect-store alias");
    auto observable = items;
    observable.at(12) = op(0x6b);
    rejects_family(observable, "an unproved data projection of the flexible selector");
    auto incomplete = items;
    incomplete.at(20).opcode = 0xac;
    incomplete.at(20).mnemonic = opcode_by_code(0xac).name;
    rejects_family(incomplete, "a selector with more than one target identity");
    auto manual = items;
    manual.at(4).stop_disposition = StopDisposition::Resumable;
    manual.at(4).manual_interaction = ManualInteractionAnchor{
        .protocol_id = 23, .phase = -1, .kind = ManualInteractionAnchorKind::PromptStop};
    auto resumed_call = call;
    resumed_call.manual_interaction = ManualInteractionAnchor{
        .protocol_id = 23, .phase = 0, .kind = ManualInteractionAnchorKind::ContinuousResume};
    manual.insert(manual.begin() + 5, {resumed_call, op(0x6c), op(0x10), stop()});
    auto manual_preloads = preloads;
    manual_preloads.at(0).value = "9";
    manual_preloads.at(1).value = "14";
    const auto manual_variants = core::reassign_stable_indirect_selector_families(
        manual, manual_preloads, core::build_post_layout_control_flow(manual));
    require(!manual_variants.empty(), "a preserved manual-entry identity must not pin its flow register");
    for (const auto& variant : manual_variants)
      for (const std::string branch : {"0", "1"})
        require(play(manual, manual_preloads, branch, true) ==
                    play(variant.items, variant.preloads, branch, true),
                "family reassignment must preserve prompt, operator input, continuation and return stack");
  }
}

} // namespace mkpro::tests
