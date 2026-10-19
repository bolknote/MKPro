#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/pre_shift_stack_lift.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;

void require(bool value, const std::string& reason) {
  if (!value) throw std::runtime_error(reason);
}

void append(std::vector<MachineItem>& items, std::initializer_list<int> codes) {
  for (const int code : codes)
    items.push_back(MachineItem::op(code, "continuation fixture"));
}

enum class Variant { Manual, SharedReturn, RecalledSign, Untyped, Raw, LiveTail, ResumedTail };

std::vector<MachineItem> fixture(Variant variant) {
  std::vector<MachineItem> items;
  append(items, {0x61, 0x53});
  items.push_back(MachineItem::address(std::string("leaf")));
  append(items, {0x62, 0x63});
  if (variant == Variant::RecalledSign) {
    append(items, {0x0b, 0x64, 0x65, 0x66});
  } else {
    if (variant == Variant::LiveTail)
      append(items, {0x25, 0x25, 0x25, 0x49});
    append(items, {0x50});
    items.back().stop_disposition = StopDisposition::Resumable;
    if (variant != Variant::Untyped)
      items.back().roles.push_back(kTypedDisplayObservationRole);
    items.back().raw = variant == Variant::Raw;
    items.back().manual_interaction =
        ManualInteractionAnchor{73, -1, ManualInteractionAnchorKind::PromptStop};
    append(items, {0x44});
    items.back().manual_interaction =
        ManualInteractionAnchor{73, 0, ManualInteractionAnchorKind::SingleStepCommand};
    append(items, {0x45});
    items.back().manual_interaction =
        ManualInteractionAnchor{73, 1, ManualInteractionAnchorKind::ContinuousResume};
    if (variant == Variant::ResumedTail)
      append(items, {0x25, 0x25, 0x25, 0x49});
    append(items, {0x66, 0x67, 0x68, 0x69, 0x66, 0x10});
  }
  append(items, {0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  items.push_back(MachineItem::label("leaf"));
  if (variant == Variant::SharedReturn) {
    append(items, {0x57});
    items.push_back(MachineItem::address(std::string("shared_return")));
  }
  append(items, {0x0e, 0x4a});
  if (variant == Variant::SharedReturn)
    items.push_back(MachineItem::label("shared_return"));
  append(items, {0x52});
  return items;
}

int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
      [](const auto& item) { return item.kind != MachineItemKind::Label; }));
}

bool continuation_proves(const std::vector<MachineItem>& items) {
  const auto flow = build_post_layout_control_flow(items);
  StackValueEqualityState state;
  state.stack_equal = {true, false, false, false};
  state.x1_equal = true;
  state.x2_equal = false;
  int address = 0;
  for (const auto& item : items) {
    if (item.kind == MachineItemKind::Label) continue;
    if (item.opcode == 0x0e)
      return prove_post_layout_stack_entry_equality(items, flow, address + 1, state);
    ++address;
  }
  throw std::runtime_error("fixture lift is missing");
}

std::vector<MachineItem> without_lift(std::vector<MachineItem> items) {
  const auto lift = std::find_if(items.begin(), items.end(), [](const auto& item) {
    return item.kind == MachineItemKind::Op && item.opcode == 0x0e;
  });
  require(lift != items.end(), "fixture needs one lift");
  items.erase(lift);
  return items;
}

std::vector<std::string> observe(const std::vector<MachineItem>& items, Variant variant,
                                 const std::string& first, const std::string& second,
                                 const std::string& seed) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps) codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "fixture must fit stock ROM");
  constexpr std::array<const char*, 15> registers{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  for (std::size_t i = 0; i < registers.size(); ++i)
    calc.set_register(registers[i], std::to_string(11U + i));
  calc.set_register("1", seed);
  calc.set_register("X", "101");
  calc.set_register("Y", "103");
  calc.set_register("Z", "107");
  calc.set_register("T", "109");
  calc.set_register("X1", "113");
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(4000, 6).stopped, "fixture must reach first stop");
  std::vector<std::string> result;
  if (variant != Variant::RecalledSign) {
    // At the source-typed prompt only X/Y are displayed. X1 and all memory
    // remain equal as well; Z/T must still converge after the input protocol.
    for (const char* reg : {"X", "Y", "X1"})
      result.push_back(calc.read_register(reg));
    for (const char* reg : registers)
      result.push_back(calc.read_register(reg));
    for (const char c : first) calc.press(std::string(1, c));
    calc.press("\u041f\u041f");
    for (const char* reg : {"X", "Y", "X1"})
      result.push_back(calc.read_register(reg));
    for (const char c : second) calc.press(std::string(1, c));
    calc.press("\u0421/\u041f");
    require(calc.run_until_stable(4000, 6).stopped, "fixture must resume and stop");
  }
  for (const char* reg : {"X", "Y", "Z", "T", "X1"})
    result.push_back(calc.read_register(reg));
  for (const char* reg : registers)
    result.push_back(calc.read_register(reg));
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}

void positive_compiler_and_rom() {
  for (const auto variant : {Variant::Manual, Variant::SharedReturn, Variant::RecalledSign}) {
    const auto before = fixture(variant);
    require(continuation_proves(before), "all exact continuations must prove");
    CompileOptions options;
    const auto result = passes::pre_shift_stack_lift(raise_machine_to_ir(before), {options});
    const auto after = lower_ir_to_machine(result.ops);
    require(result.applied == 1 && cells(after) + 1 == cells(before),
            "compiler must remove exactly one stored-return lift");
    require(std::none_of(after.begin(), after.end(), [](const auto& item) {
      return item.kind == MachineItemKind::Op && item.opcode == 0x0e;
    }), "only the proved lift must disappear");
    const int expected = variant == Variant::Manual ? 18 :
                         variant == Variant::SharedReturn ? 20 : 13;
    require(cells(before) == expected && cells(after) == expected - 1,
            "synthetic size baselines must stay explicit");
    require(build_post_layout_control_flow(after).proved, "final CFG must prove");
    require(passes::pre_shift_stack_lift(result.ops, {options}).applied == 0,
            "continuation deletion must be idempotent");
    for (const char* seed : {"0", "12", "-3.125"})
      for (const auto& input : {
          std::pair{std::string("3"), std::string("7")},
          std::pair{std::string("12.5"), std::string(".125")},
          std::pair{std::string(""), std::string("")}})
        require(observe(before, variant, input.first, input.second, seed) ==
                    observe(after, variant, input.first, input.second, seed),
                "ROM must preserve input phases, displays, memory, X1/X2 and resumed stack");
  }
}

void negative_contracts() {
  for (const auto variant : {Variant::Untyped, Variant::Raw, Variant::LiveTail,
                             Variant::ResumedTail})
    require(!continuation_proves(fixture(variant)),
            "untyped, opaque or subsequently observed differences must reject");
  const auto live = fixture(Variant::ResumedTail);
  require(observe(live, Variant::ResumedTail, "", "", "12") !=
              observe(without_lift(live), Variant::ResumedTail, "", "", "12"),
          "ROM must witness the resumed live-T counterexample");

  auto invalid = fixture(Variant::Manual);
  for (auto& item : invalid)
    if (item.manual_interaction &&
        item.manual_interaction->kind == ManualInteractionAnchorKind::SingleStepCommand)
      item.opcode = 0x64;
  require(!continuation_proves(invalid), "malformed single-step protocol must reject");

  std::vector<MachineItem> terminal;
  append(terminal, {0x41, 0x50});
  terminal.back().stop_disposition = StopDisposition::Terminal;
  terminal.back().roles.push_back(kTypedDisplayObservationRole);
  StackValueEqualityState equal_front;
  equal_front.stack_equal = {true, true, true, false};
  equal_front.x1_equal = true;
  equal_front.x2_equal = true;
  const auto check_terminal = [&](const auto& items, const auto& equality) {
    return prove_post_layout_stack_entry_equality(
        items, build_post_layout_control_flow(items), 1, equality);
  };
  require(check_terminal(terminal, equal_front), "typed halt observes only its declared front");
  for (int observer = 0; observer < 4; ++observer) {
    auto difference = equal_front;
    if (observer == 0) difference.stack_equal[0] = false;
    if (observer == 1) difference.stack_equal[1] = false;
    if (observer == 2) difference.x1_equal = false;
    if (observer == 3) difference.x2_equal = false;
    require(check_terminal(terminal, difference) == (observer >= 2),
            "typed halt observes X/Y, not unused X1/X2");
  }
  auto untyped_terminal = terminal;
  untyped_terminal.back().roles.clear();
  require(!check_terminal(untyped_terminal, equal_front),
          "an untyped terminal remains a full-state barrier");
  terminal.back().raw = true;
  require(!check_terminal(terminal, equal_front), "raw typed halt must not bypass opacity");

  auto protected_return = raise_machine_to_ir(fixture(Variant::SharedReturn));
  for (auto& item : protected_return)
    if (item.kind == IrKind::Label && item.name == "shared_return")
      item.meta.roles.push_back("protected-return-label");
  CompileOptions options;
  const auto protected_result = passes::pre_shift_stack_lift(protected_return, {options});
  require(std::none_of(protected_result.optimizations.begin(), protected_result.optimizations.end(),
      [](const auto& report) { return report.name == "matched-return-stack-lift"; }),
      "transparent-label handling must not cross protected labels");
}

std::array<std::string, 2> arithmetic_observation(int opcode, const std::string& input,
                                                 int context, bool digit) {
  std::vector<int> code{0x61, 0x62};
  if (context == 1) code.insert(code.end(), {0x0d, 2});
  if (context == 2) code.push_back(0x0e);
  if (context == 3) code.push_back(0x43);
  code.push_back(opcode);
  if (digit) code.push_back(7);
  code.push_back(0x50);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "entry fact must fit ROM");
  calc.set_register("1", "3.125");
  calc.set_register("2", input);
  calc.set_register("X", "101");
  calc.set_register("Y", "103");
  calc.set_register("Z", "107");
  calc.set_register("T", "109");
  calc.set_register("X1", "113");
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(4000, 6).stopped, "entry fact must stop");
  return {calc.read_register("X"), calc.read_register("Y")};
}

void arithmetic_entry_rom_facts() {
  for (const int opcode : {0x10, 0x11, 0x12, 0x13, 0x15, 0x22, 0x31, 0x34, 0x35}) {
    require(selector_charge_entry_closer_opcode(opcode), "ROM opcode must enable fresh entry");
    for (const char* input : {"-3.125", "0.125", "7.125"})
      for (int context = 0; context < 4; ++context) {
        const auto before = arithmetic_observation(opcode, input, context, false);
        const auto after = arithmetic_observation(opcode, input, context, true);
        require(after[0] == "7," && after[1] == before[0],
                "a new digit must lift the exact arithmetic result");
      }
  }
  for (const int opcode : {0x0a, 0x0b, 0x0c, 0x0e, 0x0f, 0x25, 0x52, 0x54})
    require(!selector_charge_entry_closer_opcode(opcode),
            "editing, entry suppression and context-preserving flow are not unconditional closers");
}

std::array<std::string, 5> conditional_entry_observation(
    int opcode, const std::string& input, int context, bool digit) {
  std::vector<int> code{0x61, 0x62};
  if (context == 1) code.insert(code.end(), {0x0d, 2});
  if (context == 2) code.push_back(0x0e);
  if (context == 3) code.push_back(0x43);
  if (context == 4) code.insert(code.end(), {2, 0x0c, 3});
  if (context == 5) code.push_back(0x0d);
  // Both outcomes lead directly to the probe. No intervening BP/store may
  // manufacture the entry fact that this test attributes to the conditional.
  const int next = static_cast<int>(code.size()) + 2;
  code.insert(code.end(), {opcode, (next / 10) * 16 + next % 10});
  if (digit) code.push_back(7);
  code.push_back(0x50);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "conditional entry ROM load");
  calc.set_register("X", "101").set_register("Y", "103")
      .set_register("Z", "107").set_register("T", "109");
  for (const char* reg : {"0", "1", "2", "3"})
    calc.set_register(reg, input);
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(4000, 8).stopped, "conditional entry ROM must stop");
  return {calc.read_register("X"), calc.read_register("Y"), calc.read_register("Z"),
          calc.read_register("T"), calc.read_register("X1")};
}

void conditional_entry_rom_facts() {
  for (const int opcode : {0x57, 0x59, 0x5c, 0x5e, 0x5d, 0x5b, 0x58, 0x5a}) {
    require(selector_charge_entry_closer_opcode(opcode),
            "direct predicates and FL counters must certify fresh entry");
    // Zero, signs, and the FL 1/2 boundary exercise both hardware decisions.
    for (const char* input : {"0", "1", "2", "-3.125"})
      for (int context = 0; context < 6; ++context) {
        const auto before = conditional_entry_observation(opcode, input, context, false);
        const auto after = conditional_entry_observation(opcode, input, context, true);
        require(after[0] == "7," && after[1] == before[0] &&
                    after[2] == before[1] && after[3] == before[2] &&
                    after[4] == before[4],
                "either conditional edge must close entry and lift all four stack slots");
      }
  }
}

} // namespace

int main() {
  try {
    positive_compiler_and_rom();
    negative_contracts();
    arithmetic_entry_rom_facts();
    conditional_entry_rom_facts();
    std::cout << "Typed continuation compiler/ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
