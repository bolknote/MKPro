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

void append(std::vector<MachineItem>& items, std::initializer_list<int> opcodes) {
  for (const int opcode : opcodes)
    items.push_back(MachineItem::op(opcode, "state-machine fixture"));
}

void branch(std::vector<MachineItem>& items, int opcode, const std::string& target) {
  append(items, {opcode});
  items.push_back(MachineItem::address(target));
}

void finish(std::vector<MachineItem>& items) {
  append(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
}

std::size_t cells(const std::vector<MachineItem>& items) {
  return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [](const auto& item) {
    return item.kind != MachineItemKind::Label;
  }));
}

core::passes::PassResult optimize(const std::vector<MachineItem>& items) {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  return core::passes::finite_mode_representation(raise_machine_to_ir(items), {options});
}

std::vector<int> codes(const std::vector<MachineItem>& items) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "state-machine fixture must resolve");
  std::vector<int> result;
  for (const auto& step : resolved.steps)
    result.push_back(step.opcode);
  return result;
}

std::vector<std::string> observe(const std::vector<MachineItem>& items,
                               const std::string& input, bool inspect_carrier) {
  emulator::MK61 calc;
  require(calc.load_program(codes(items)).diagnostics.empty(), "state fixture must fit ROM memory");
  constexpr std::array<const char*, 15> registers{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  for (std::size_t i = 0; i < registers.size(); ++i)
    calc.set_register(registers[i], std::to_string(i + 2));
  calc.set_register("7", "2");
  calc.set_register("b", input);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(5000, 8).stopped, "state fixture must stop");
  std::vector<std::string> result{calc.display_text(true)};
  calc.press(".");
  result.push_back(calc.display_text(true));
  for (const char* reg : {"x", "y", "z", "t", "x1"})
    result.push_back(calc.read_register(reg));
  for (std::size_t i = 0; i < registers.size(); ++i)
    if (i != 8U || inspect_carrier)
      result.push_back(calc.read_register(registers[i]));
  return result;
}

enum class Variant {
  Ordinary, TwoSeeds, DynamicOverwrite, UnknownSeed, OpenRecurrence,
  BareRead, LastX, ExponentEntry, MemoryAlias, CounterAlias, Resume,
};

std::vector<MachineItem> cycle(Variant variant = Variant::Ordinary) {
  std::vector<MachineItem> items;
  const int reg = variant == Variant::CounterAlias ? 0 : 8;
  append(items, {0x6b, 0x4e});
  if (variant == Variant::TwoSeeds) {
    branch(items, 0x57, "negative_seed");
    append(items, {0x4e, 2, 0x40 + reg});
    branch(items, 0x51, "initialized");
    items.push_back(MachineItem::label("negative_seed"));
    append(items, {0x4e, 2, 0x0b, 0x40 + reg});
    items.push_back(MachineItem::label("initialized"));
  } else {
    append(items, {variant == Variant::UnknownSeed ? 0x6b : 2, 0x40 + reg});
  }
  append(items, {3, 0x40});
  items.push_back(MachineItem::label("cycle"));
  append(items, {0x60 + reg, 1, 0, 0x12, 0x49});
  if (variant == Variant::LastX)
    append(items, {0x0f, 0x46});
  if (variant == Variant::ExponentEntry)
    append(items, {0x0c, 2, 0x46});
  append(items, {0x60 + reg, 1, 0, 0x12, 0x4a, 0x60 + reg, 0x32, 0x46});
  if (variant == Variant::BareRead)
    append(items, {0x60 + reg, 0x45});
  if (variant == Variant::MemoryAlias) {
    append(items, {0xd7});
    items.back().indirect_memory_targets = std::vector<int>{reg};
  }
  append(items, {0x60 + reg});
  if (variant == Variant::OpenRecurrence)
    append(items, {1, 0x10});
  else
    append(items, {0x0b});
  append(items, {0x40 + reg});
  if (variant == Variant::Resume) {
    append(items, {0x50});
    items.back().stop_disposition = StopDisposition::Resumable;
  }
  branch(items, 0x5d, "cycle");
  if (variant == Variant::DynamicOverwrite)
    append(items, {0x6b, 0x48, 0x68, 0x45});
  finish(items);
  return items;
}

std::vector<MachineItem> indirect_argument() {
  std::vector<MachineItem> items;
  branch(items, 0x51, "main");
  items.push_back(MachineItem::label("worker"));
  append(items, {0x48, 0x68, 1, 0, 0x12, 0x49,
                0x68, 1, 0, 0x12, 0x4a, 0x68, 0x32, 0x46, 0x52});
  items.push_back(MachineItem::label("main"));
  append(items, {0x6b});
  branch(items, 0x57, "other");
  append(items, {0x4e, 2, 0xa7});
  items.back().indirect_flow_targets = std::vector<IrTarget>{"worker"};
  branch(items, 0x51, "done");
  items.push_back(MachineItem::label("other"));
  append(items, {0x4e, 2, 0x0b, 0xa7});
  items.back().indirect_flow_targets = std::vector<IrTarget>{"worker"};
  items.push_back(MachineItem::label("done"));
  finish(items);
  return items;
}

void accepted(const std::vector<MachineItem>& original, std::size_t saving,
              bool state_machine, bool inspect_carrier = false) {
  const auto optimized = optimize(original);
  require(optimized.applied == 1, "closed state web should be optimized");
  const auto rewritten = lower_ir_to_machine(optimized.ops);
  require(cells(original) == cells(rewritten) + saving,
          "state-machine size baseline: " + std::to_string(cells(original)) + " -> " +
              std::to_string(cells(rewritten)) + ", expected saving " + std::to_string(saving));
  require(core::build_post_layout_control_flow(rewritten).proved, "rewritten CFG must be proved");
  if (state_machine)
    require(std::any_of(optimized.optimizations.begin(), optimized.optimizations.end(),
                       [](const auto& report) { return report.name == "finite-mode-state-machine"; }),
            "cycle must be handled by the transition-aware pass");
  for (const char* input : {"0", "-2", "7", "12345678"})
    require(observe(original, input, inspect_carrier) == observe(rewritten, input, inspect_carrier),
            "state encoding changed ROM stack, hidden state or memory for input " + std::string(input));
  std::cout << (state_machine ? "state machine: " : "indirect argument: ")
            << cells(original) << " -> " << cells(rewritten) << " cells\n";
}

void rejected(const std::vector<MachineItem>& original, const std::string& context) {
  const auto result = optimize(original);
  require(result.applied == 0, context + " must reject encoding changes");
  const auto rewritten = lower_ir_to_machine(result.ops);
  require(original.size() == rewritten.size(), context + " changed rejected item count");
  for (std::size_t i = 0; i < original.size(); ++i)
    require(machine_items_equal(original[i], rewritten[i]), context + " changed rejected metadata");
}

void run() {
  accepted(cycle(), 5, true);
  accepted(cycle(Variant::TwoSeeds), 4, true);
  accepted(cycle(Variant::DynamicOverwrite), 5, true, true);
  accepted(indirect_argument(), 4, false);
  for (const auto& [variant, name] : {
           std::pair{Variant::UnknownSeed, "unknown entry value"},
           {Variant::OpenRecurrence, "unbounded recurrence"},
           {Variant::BareRead, "bare value observer"},
           {Variant::LastX, "observed physical last-X"},
           {Variant::ExponentEntry, "observed exponent-entry shadow"},
           {Variant::MemoryAlias, "indirect memory alias"},
           {Variant::CounterAlias, "destructive counter alias"},
           {Variant::Resume, "independent resume entry"}})
    rejected(cycle(variant), name);
  const auto original = cycle();
  const auto first = lower_ir_to_machine(optimize(original).ops);
  const auto second = lower_ir_to_machine(optimize(original).ops);
  require(first.size() == second.size(), "state synthesis must be deterministic");
  for (std::size_t i = 0; i < first.size(); ++i)
    require(machine_items_equal(first[i], second[i]), "state synthesis changed on repeat");
}
} // namespace

int main() {
  try {
    run();
    std::cout << "finite-mode transitions: compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
