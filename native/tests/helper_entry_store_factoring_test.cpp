#include "mkpro/core/passes/helper_entry_store_factoring.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/emulator/mk61.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mkpro;

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
MachineItem op(int code) {
  return MachineItem::op(code, opcode_by_code(code).name);
}
std::vector<MachineItem> fixture(int reg, bool prepare_y = false) {
  std::vector<MachineItem> items;
  for (int iteration = 0; iteration < 3; ++iteration) {
    items.push_back(op(iteration == 1 ? 0x60 + reg : 0x40 + reg));
    if (prepare_y) {
      items.push_back(op(0x69));
      items.push_back(op(0x14));
    }
    items.push_back(op(0x53));
    items.push_back(MachineItem::address("opaque_kernel"));
  }
  auto stop = op(0x50);
  stop.stop_disposition = StopDisposition::Terminal;
  items.push_back(stop);
  items.push_back(MachineItem::label("opaque_kernel"));
  items.push_back(op(0x31));
  items.push_back(op(0x62));
  items.push_back(op(0x10));
  items.push_back(op(0x52));
  return items;
}
core::passes::PassResult optimize(const std::vector<MachineItem>& items) {
  CompileOptions options;
  return core::passes::helper_entry_store_factoring(
      raise_machine_to_ir(items), core::passes::PassContext{options});
}
void compare_rom(const std::vector<MachineItem>& before,
                 const std::vector<MachineItem>& after) {
  for (const std::string input : {"0", "-3.5", "123.456", "0.000001"}) {
    emulator::MK61 reference;
    emulator::MK61 candidate;
    const auto run = [&](emulator::MK61& calculator,
                         const std::vector<MachineItem>& items) {
      const auto resolved = resolve_machine_items(items);
      require(resolved.diagnostics.empty(), "fixture resolution failed");
      std::vector<int> bytes;
      for (const auto& step : resolved.steps) bytes.push_back(step.opcode);
      calculator.load_program(bytes);
      calculator.set_register("2", "0.125");
      calculator.set_register("9", "7");
      calculator.input_number(input);
      calculator.press_sequence({"В/О", "С/П"});
      calculator.run_until_stable();
    };
    run(reference, before);
    run(candidate, after);
    require(reference.display_text() == candidate.display_text(), "display differs");
    for (const std::string name : {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e", "x", "y", "z", "t"})
      require(reference.read_register(name) == candidate.read_register(name), "register differs");
  }
}
} // namespace

int main() {
  try {
    for (int reg : {1, 4, 6}) {
      for (bool prepare : {false, true}) {
        const auto original = fixture(reg, prepare);
        const auto result = optimize(original);
        require(result.applied == 1, "complete argument family was not factored");
        const auto rewritten = lower_ir_to_machine(result.ops);
        require(rewritten.size() + 1U == original.size(), "factoring must save one cell");
        compare_rom(original, rewritten);
      }
    }
    auto mixed = fixture(1);
    mixed[3] = op(0x63);
    require(optimize(mixed).applied == 0, "different argument register must reject");
    auto observation = fixture(1);
    observation[11] = op(0x0f);
    require(optimize(observation).applied == 0, "observable hidden state must reject");
    auto crossing = fixture(1, true);
    crossing[1] = op(0x61);
    require(optimize(crossing).applied == 0, "read of a moved store must reject");
    auto single = fixture(1);
    single[6] = op(0x61);
    require(optimize(single).applied == 0, "one store cannot repay the entry store");
    auto raw = fixture(1);
    raw[0].raw = true;
    require(optimize(raw).applied == 0, "raw instruction must reject");
    std::cout << "helper-entry-store-factoring compiler and ROM tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
