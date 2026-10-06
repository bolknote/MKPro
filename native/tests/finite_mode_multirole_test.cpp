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
#include <variant>
#include <vector>

namespace {
using namespace mkpro;
using mkpro::tests::require;

void append(std::vector<MachineItem>& items, std::initializer_list<int> codes) {
  for (const auto code : codes)
    items.push_back(MachineItem::op(code, "fixture"));
}

void branch(std::vector<MachineItem>& items, int opcode, const std::string& label) {
  append(items, {opcode});
  items.push_back(MachineItem::address(label));
}

void finish(std::vector<MachineItem>& items) {
  // Converge all stack slots, physical last-X and number-entry state before
  // the observable stop. Different encodings are not an excuse to ignore them.
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
  require(resolved.diagnostics.empty(), "multirole fixture must resolve symbolic addresses");
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
  require(calc.run_until_stable(3500, 8).stopped, "ROM fixture must reach its stop");
  std::vector<std::string> observations{calc.display_text(true)};
  // Probe number-entry state before diagnostic register reads can affect it.
  calc.press(".");
  observations.push_back(calc.display_text(true));
  for (std::size_t i = 0; i < registers.size(); ++i)
    if (i != 8U || inspect_carrier)
      observations.push_back(calc.read_register(registers[i]));
  return observations;
}

void equivalent(const std::vector<MachineItem>& original, std::size_t saving,
                const std::string& context, bool inspect_carrier = false) {
  const auto result = optimize(original);
  require(result.applied == 1, context + ": eligible lifetime must be rewritten");
  const auto rewritten = lower_ir_to_machine(result.ops);
  require(cells(rewritten) + saving == cells(original), context + ": size baseline changed");
  require(core::build_post_layout_control_flow(rewritten).proved,
          context + ": final CFG must remain authoritative");
  for (const auto* input : {"0", "-2", "7", "12345678"})
    require(observe(original, input, inspect_carrier) == observe(rewritten, input, inspect_carrier),
            context + ": ROM observations changed for input " + input);
}

void rejected(const std::vector<MachineItem>& items, const std::string& reason) {
  const auto result = optimize(items);
  require(result.applied == 0, reason);
  const auto output = lower_ir_to_machine(result.ops);
  require(output.size() == items.size(), "rejection must retain every original item");
  for (std::size_t i = 0; i < items.size(); ++i)
    require(machine_items_equal(items[i], output[i]), "rejection must be transactional");
}

std::vector<MachineItem> parameter_fixture(bool unknown = false, bool bare_read = false,
                                         bool expose_last_x = false,
                                         bool expose_entry = false, bool alias = false,
                                         bool overwrite = false, bool nested = false) {
  std::vector<MachineItem> items;
  append(items, {0x6b});
  branch(items, 0x57, "other");
  append(items, {0x4e, unknown ? 0x6b : 2});
  branch(items, 0x53, nested ? "wrapper" : "worker");
  branch(items, 0x51, "done");
  items.push_back(MachineItem::label("other"));
  append(items, {0x4e, 2, 0x0b});
  branch(items, 0x53, nested ? "wrapper" : "worker");
  items.push_back(MachineItem::label("done"));
  if (overwrite)
    append(items, {0x6b, 0x48, 0x68, 0x40});
  finish(items);
  if (nested) {
    items.push_back(MachineItem::label("wrapper"));
    branch(items, 0x53, "worker");
    append(items, {0x52});
  }
  items.push_back(MachineItem::label("worker"));
  append(items, {0x48, 0x68, 1, 0, 0x12, 0x49, 0x68, 1, 0, 0x12, 0x4a});
  if (expose_last_x)
    append(items, {0x0f, 0x46});
  if (expose_entry)
    append(items, {0x0c, 2, 0x46});
  if (alias) {
    append(items, {0xd7});
    items.back().indirect_memory_targets = std::vector<int>{8};
  }
  append(items, {0x68, bare_read ? 0x46 : 0x32});
  if (!bare_read)
    append(items, {0x46});
  append(items, {0x52});
  return items;
}

std::vector<MachineItem> selector_fixture(bool changed_target = false,
                                        bool mutable_selector = false,
                                        bool forward = false) {
  std::vector<MachineItem> items;
  if (!forward) {
    branch(items, 0x51, "main");
    items.push_back(MachineItem::label("selected"));
    append(items, {0x52});
    items.push_back(MachineItem::label("main"));
  }
  const int reg = mutable_selector ? 0 : 8;
  append(items, {0x4e});
  if (forward)
    append(items, {9, 9});
  else
    append(items, {2});
  append(items, {0x40 + reg});
  for (const int destination : {0x49, 0x4a}) {
    append(items, {0x60 + reg});
    if (changed_target)
      append(items, {2, 0x12});
    else
      append(items, {1, 0, 0, 0, 0x10});
    append(items, {destination});
  }
  append(items, {0xa0 + reg});
  items.back().indirect_flow_targets = std::vector<std::variant<int, std::string>>{"selected"};
  finish(items);
  if (forward) {
    while (cells(items) < 99U)
      append(items, {0x54});
    items.push_back(MachineItem::label("selected"));
    append(items, {0x52});
  }
  return items;
}

void run() {
  equivalent(parameter_fixture(), 4, "caller literals and heterogeneous consumers");
  equivalent(parameter_fixture(false, false, false, false, false, false, true), 4,
             "literal argument through nested calls");
  equivalent(parameter_fixture(false, false, false, false, false, true), 4,
             "later dynamic lifetime", true);
  equivalent(selector_fixture(), 7, "same carrier for arithmetic and indirect call");
  rejected(parameter_fixture(true), "unknown caller input must reject the connected web");
  rejected(parameter_fixture(false, true), "an unprojected read observes the original encoding");
  rejected(parameter_fixture(false, false, true), "observable last-X must reject decoder erasure");
  rejected(parameter_fixture(false, false, false, true), "exponent entry must retain hidden X2");
  rejected(parameter_fixture(false, false, false, false, true), "indirect memory alias must reject");
  rejected(selector_fixture(true), "a smaller arithmetic encoding cannot change the flow target");
  rejected(selector_fixture(false, true), "a mutating selector cannot use a stable codec proof");
  rejected(selector_fixture(false, false, true), "shrinking before a fixed target needs relocation");
}
} // namespace

int main() {
  try {
    run();
    std::cout << "finite-mode multirole representation: compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
