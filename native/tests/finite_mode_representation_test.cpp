#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using mkpro::tests::require;

void append(std::vector<MachineItem>& items, std::initializer_list<int> codes) {
  for (const auto code : codes)
    items.push_back(MachineItem::op(code, "fixture"));
}

void finish(std::vector<MachineItem>& items) {
  // Kill every differing stack slot and then physical last-X. Do not let
  // a correct numeric result stand in for a complete continuation proof.
  append(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
}

std::vector<MachineItem> fixture() {
  std::vector<MachineItem> items;
  append(items, {0x4e, 1, 0x48, 0x68, 2, 0x12, 0x49,
                0x68, 2, 0x12, 0x4a});
  return items;
}

core::passes::PassResult optimize(const std::vector<MachineItem>& items, bool enabled = true) {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = enabled;
  return core::passes::finite_mode_representation(raise_machine_to_ir(items), {options});
}

std::size_t cells(const std::vector<MachineItem>& items) {
  return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [](const auto& item) {
    return item.kind != MachineItemKind::Label;
  }));
}

std::vector<int> codes(const std::vector<MachineItem>& items) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "fixture must resolve symbolic addresses");
  std::vector<int> result;
  for (const auto& step : resolved.steps)
    result.push_back(step.opcode);
  return result;
}

std::vector<std::string> observe(const std::vector<MachineItem>& items,
                                  const std::string& input, bool inspect_carrier) {
  emulator::MK61 calc;
  require(calc.load_program(codes(items)).diagnostics.empty(), "ROM fixture must fit memory");
  constexpr std::array<const char*, 15> registers{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  for (std::size_t i = 0; i < registers.size(); ++i)
    calc.set_register(registers[i], std::to_string(i + 2));
  calc.set_register("b", input);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(2500, 8).stopped, "ROM fixture must reach its stop");
  std::vector<std::string> observations{calc.display_text(true)};
  for (std::size_t i = 0; i < registers.size(); ++i)
    if (i != 8U || inspect_carrier)
      observations.push_back(calc.read_register(registers[i]));
  return observations;
}

void equivalent(const std::vector<MachineItem>& original,
                const core::passes::PassResult& result, bool inspect_carrier = false) {
  require(result.applied == 1, "eligible projected lifetime must be rewritten");
  const auto rewritten = lower_ir_to_machine(result.ops);
  require(cells(rewritten) < cells(original), "projected lifetime must save cells");
  require(core::build_post_layout_control_flow(rewritten).proved,
          "relocated final CFG must remain authoritative");
  for (const auto* input : {"0", "-2", "7", "12345678"})
    require(observe(original, input, inspect_carrier) ==
                observe(rewritten, input, inspect_carrier),
            std::string("ROM observations changed for input ") + input);
}

void rejected(const std::vector<MachineItem>& items, const std::string& reason) {
  const auto result = optimize(items);
  require(result.applied == 0, reason);
  const auto output = lower_ir_to_machine(result.ops);
  require(output.size() == items.size(), "rejection must retain every original item");
  for (std::size_t i = 0; i < items.size(); ++i)
    require(machine_items_equal(items[i], output[i]), "rejection must be transactional");
}

void run() {
  // Enumeration exercises the ordinary-decimal certificate independently of
  // the def-use proof. Every accepted codec must agree with ROM arithmetic;
  // unrepresentable/nonterminating results are allowed only as rejections.
  for (const auto* value : {"-0", "0", "1", "-1", "2.75", "-2.75", "99999999",
                            "0.0000001", "12345678", "7.7777777"}) {
    for (const int operation : {0x0b, 0x32, 0x34, 0x35, 0x10, 0x11, 0x12, 0x13}) {
      for (const auto* operand : {"3", "10"}) {
        const bool binary = operation >= 0x10 && operation <= 0x13;
        if (!binary && std::string(operand) == "10")
          continue;
        MachineEmitter emitter;
        emitter.emit_op(0x4e);
        emitter.emit_number(value);
        emitter.emit_op(0x48);
        for (const int target : {0x49, 0x4a}) {
          emitter.emit_op(0x68);
          if (binary)
            emitter.emit_number(operand);
          emitter.emit_op(operation);
          emitter.emit_op(target);
        }
        finish(emitter.items);
        const auto result = optimize(emitter.items);
        const bool counterexample =
            (std::string(value) == "-0" && operation == 0x32) ||
            (std::string(value) == "-1" && operation == 0x35) ||
            ((std::string(value) == "0.0000001" || std::string(value) == "7.7777777") &&
             std::string(operand) == "10" && (operation == 0x11 || operation == 0x13));
        if (counterexample)
          require(result.applied == 0, "rounding/signed-zero/entry counterexample must reject");
        if (result.applied > 0)
          equivalent(emitter.items, result);
      }
    }
  }
  {
    auto items = fixture();
    finish(items);
    const auto result = optimize(items);
    require(cells(lower_ir_to_machine(result.ops)) + 4U == cells(items),
            "two projected reads must remove four cells");
    equivalent(items, result);
    require(optimize(items, false).applied == 0, "ordinary candidate remains unchanged");
  }
  {
    auto items = fixture();
    // The same physical register later carries unrelated, nonconstant data.
    // No register rename, preload or game-specific shape is involved.
    append(items, {0x6b, 0x48, 0x68, 0x46});
    finish(items);
    equivalent(items, optimize(items), true);
  }
  {
    std::vector<MachineItem> items;
    append(items, {0x4e, 1, 0x48, 0x53});
    items.push_back(MachineItem::address(std::string("projection")));
    finish(items);
    items.push_back(MachineItem::label("projection"));
    append(items, {0x68, 2, 0x12, 0x49, 0x52});
    equivalent(items, optimize(items));
  }
  {
    std::vector<MachineItem> items;
    append(items, {0x6b, 0x57});
    items.push_back(MachineItem::address(std::string("other")));
    append(items, {0x4e, 1, 0x48, 0x51});
    items.push_back(MachineItem::address(std::string("join")));
    items.push_back(MachineItem::label("other"));
    append(items, {0x4e, 3, 0x48});
    items.push_back(MachineItem::label("join"));
    append(items, {0x68, 2, 0x12, 0x49, 0x68, 2, 0x12, 0x4a});
    finish(items);
    equivalent(items, optimize(items));
  }
  {
    auto items = fixture();
    append(items, {0x68, 0x46});
    finish(items);
    rejected(items, "an ordinary read in the same lifetime prevents re-encoding");
  }
  {
    auto items = fixture();
    append(items, {0x0f, 0x46});
    finish(items);
    rejected(items, "physical last-X is observable before convergence");
  }
  {
    auto items = fixture();
    append(items, {0x0c, 2, 0x46});
    finish(items);
    rejected(items, "exponent entry cannot consume an unproved hidden context");
  }
  {
    auto items = fixture();
    finish(items);
    items.back().stop_disposition = StopDisposition::Resumable;
    append(items, {0x68, 0x46});
    finish(items);
    rejected(items, "representation cannot leak through an independent resume");
  }
  {
    auto items = fixture();
    finish(items);
    items[0].raw = true;
    rejected(items, "raw access has no private-register proof");
  }
  {
    auto items = fixture();
    items[0].opcode = 0x0d;
    finish(items);
    rejected(items, "Cx is not a proved automatic-lift numeric-entry boundary");
  }
  {
    auto items = fixture();
    append(items, {0xd7});
    items.back().indirect_memory_targets = std::vector<int>{8};
    finish(items);
    rejected(items, "indirect alias observes the encoded carrier");
  }
  {
    std::vector<MachineItem> items;
    append(items, {0x4e, 1, 0x48, 0x6b, 0x57});
    items.push_back(MachineItem::address(std::string("join")));
    append(items, {0x6c, 0x48});
    items.push_back(MachineItem::label("join"));
    append(items, {0x68, 2, 0x12, 0x49, 0x68, 2, 0x12, 0x4a});
    finish(items);
    rejected(items, "dynamic reaching definition poisons the entire merged web");
  }
}
} // namespace

int main() {
  try {
    run();
    std::cout << "finite-mode representation: compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
