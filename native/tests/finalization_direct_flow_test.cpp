#include "mkpro/core/finalization_direct_flow.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using tests::require;

std::vector<MachineItem> fixture(bool side) {
  std::vector<MachineItem> code;
  const auto op = [&](int byte) { code.push_back(MachineItem::op(byte, "erasure fixture")); };
  op(0x52); op(0x61); op(0x47); op(0x53);
  code.push_back(MachineItem::address(38));
  code.back().formal_opcode = side ? 0xea : 0x38;
  op(0x68); op(0x50);
  code.back().stop_disposition = StopDisposition::Terminal;
  while (code.size() < 38U) op(0x54);
  for (int i = 0; i < 8; ++i) op(0x31);
  op(0x48); op(0x31); op(1); op(0x49); op(0x52);
  while (code.size() < 56U) op(0x54);
  return code;
}

std::vector<MachineItem> erased(const std::vector<MachineItem>& code, int address) {
  auto output = code;
  output.erase(output.begin() + address);
  return output;
}

std::vector<std::string> observe(const std::vector<MachineItem>& code) {
  const auto resolved = resolve_machine_items(code);
  require(resolved.diagnostics.empty(), "formal erasure fixture resolves");
  std::vector<int> bytes;
  for (const auto& step : resolved.steps) bytes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(bytes).diagnostics.empty(), "formal erasure fixture fits stock ROM");
  for (const auto* name : {"X", "Y", "Z", "T", "X1"}) calc.set_register(name, "2.5");
  calc.set_register("1", "3"); calc.set_register("8", "0"); calc.set_register("9", "0");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(3000, 8).stopped, "formal erasure fixture returns and stops");
  std::vector<std::string> out;
  for (const auto* name : {"X", "Y", "Z", "T", "X1", "8", "9"})
    out.push_back(calc.read_register(name));
  calc.press("."); out.push_back(calc.display_text(true));
  return out;
}

void contracts() {
  const auto original = fixture(true);
  std::string reason;
  require(!core::retarget_direct_flows_after_cell_erasure(
      original, erased(original, 2), 2, AddressSpaceModel::Standard, &reason),
      "deleting before a side body requires joint placement, not counter canonicalization");
  require(!reason.empty(), "rejected formal erasure explains its obligation");
  require(!core::retarget_direct_flows_after_cell_erasure(
      original, erased(original, 40), 40, AddressSpaceModel::Standard),
      "erasing inside the formal fetch window cannot shift its discontinuity");
  const auto safe = core::retarget_direct_flows_after_cell_erasure(
      original, erased(original, 55), 55, AddressSpaceModel::Standard);
  require(safe && (*safe)[4].formal_opcode == std::optional<int>{0xea},
      "erasing after a complete side window retains the exact counter");
  require(observe(original) == observe(*safe),
      "safe erasure preserves stack, X1/X2, data and the hardware return path");

  auto unsafe = erased(original, 2);
  for (auto& item : unsafe) if (item.kind == MachineItemKind::Address) {
    if (auto* target = std::get_if<int>(&item.target); target && *target > 2) --*target;
    item.formal_opcode.reset();
  }
  require(observe(original) != observe(unsafe),
      "canonicalizing EA executes the forbidden tail and changes observable state in ROM");

  const auto ordinary = fixture(false);
  const auto moved = core::retarget_direct_flows_after_cell_erasure(
      ordinary, erased(ordinary, 2), 2, AddressSpaceModel::Standard);
  require(moved && !(*moved)[3].formal_opcode && (*moved)[3].target == IrTarget{37},
      "an official encoded operand remains a discardable address cache");
  require(observe(ordinary) == observe(*moved),
      "ordinary erasure still preserves the complete observable ROM state");

  auto expanded = fixture(false);
  expanded[4].target = 108;
  expanded[4].formal_opcode = 0xa8;
  const auto extended = core::retarget_direct_flows_after_cell_erasure(
      expanded, erased(expanded, 2), 2, AddressSpaceModel::Mk61SMiniExpanded);
  require(extended && (*extended)[3].target == IrTarget{107} && !(*extended)[3].formal_opcode,
      "A8 is an ordinary address only in the expanded 112-cell model");
  require(!core::retarget_direct_flows_after_cell_erasure(
      expanded, erased(expanded, 2), 2, AddressSpaceModel::Standard),
      "stock short-side addresses must not inherit expanded address semantics");
}
} // namespace

int main() {
  try {
    contracts();
    std::cout << "finalization direct-flow and formal-window ROM contracts passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

