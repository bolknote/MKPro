#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string compact(std::string value) {
  std::string out;
  for (const char ch : value) {
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
      out.push_back(ch);
  }
  return out;
}

std::vector<int> nested_call_program(int depth) {
  std::vector<int> codes = {0x53, 0x03, 0x50};
  std::vector<int> subroutine_addresses;
  int address = static_cast<int>(codes.size());
  for (int index = 0; index < depth; ++index) {
    subroutine_addresses.push_back(address);
    address += 3;
  }
  for (int index = 0; index < depth; ++index) {
    if (index == depth - 1) {
      codes.push_back(0x01);
      codes.push_back(0x40);
      codes.push_back(0x52);
    } else {
      codes.push_back(0x53);
      codes.push_back(subroutine_addresses[static_cast<std::size_t>(index + 1)]);
      codes.push_back(0x52);
    }
  }
  return codes;
}

struct NestedRun {
  bool stopped = false;
  std::string pc;
  std::string r0;
};

NestedRun run_nested_call_program(int depth, bool extended) {
  emulator::MK61 calc(emulator::MK61Options{.extended = extended});
  calc.load_program(nested_call_program(depth));
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(depth <= 5 ? 80 : 120, 6);
  return NestedRun{.stopped = run.stopped,
                   .pc = calc.program_counter(),
                   .r0 = compact(calc.read_register("0"))};
}

} // namespace

void emulator_vo_return_matches_typescript_contract() {
  {
    emulator::MK61 calc;
    calc.load_program({0x50, 0x01, 0x40, 0x52, 0x50});
    calc.press_sequence({"В/О", "С/П"});
    calc.run_until_stable(50, 3);
    calc.press_sequence({"С/П"});
    calc.run_until_stable(50, 3);
    require(calc.program_counter() == "00", "В/О with empty return stack should jump to 00");
    require(compact(calc.read_register("0")) == "1,",
            "empty-stack В/О program should store marker in R0 before returning to head");
  }

  for (const bool extended : {false, true}) {
    const NestedRun depth5 = run_nested_call_program(5, extended);
    require(depth5.stopped, "MK-61 should return through five nested ПП frames");
    require(depth5.pc == "03", "five nested ПП frames should return to caller stop at PC 03");
    require(depth5.r0 == "1,", "five nested ПП frames should execute the leaf store");

    const NestedRun depth6 = run_nested_call_program(6, extended);
    require(!depth6.stopped, "MK-61 should not return through six nested ПП frames");
  }
}

} // namespace mkpro::tests
