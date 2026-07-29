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

void emulator_vo_empty_continuation_facts() {
  // Pin the exact continuation cell after an empty-stack В/О: the program
  // counter is set to 00, but the next executed command is the one at
  // physical 01. The cell at 00 is skipped, which is why a program may keep
  // a В/О pad at 00 and close its main loop with one-cell В/О commands.
  //
  // 00 `5`; 01 X→П2; 02 С/П; 03 `1`; 04 X→П0; 05 В/О
  // Resume after the first stop runs 03,04, then the empty-stack В/О. If the
  // continuation executed cell 00, R2 would be rewritten to 5 (X=5 from a
  // fresh `5` digit); executing from 01 stores the still-current X=1 instead.
  {
    emulator::MK61 calc;
    calc.load_program({0x05, 0x42, 0x50, 0x01, 0x40, 0x52});
    calc.press_sequence({"В/О", "С/П"});
    calc.run_until_stable(80, 6);
    require(compact(calc.read_register("2")) == "5,",
            "cold start should execute the cell at 00 before the first stop");
    calc.press_sequence({"С/П"});
    const emulator::RunResult run = calc.run_until_stable(120, 6);
    require(run.stopped, "empty-stack В/О should continue execution and reach the stop");
    require(compact(calc.read_register("0")) == "1,",
            "the store before the empty-stack В/О should execute");
    require(compact(calc.read_register("2")) == "1,",
            "empty-stack В/О should continue at physical 01, skipping the cell at 00");
  }

  // The same continuation with a В/О pad at 00: cold start passes through the
  // pad into 01, and every later empty-stack В/О lands at 01 as well.
  // 00 В/О; 01 `7`; 02 X→П3; 03 С/П; 04 `2`; 05 X→П1; 06 В/О
  {
    emulator::MK61 calc;
    calc.load_program({0x52, 0x07, 0x43, 0x50, 0x02, 0x41, 0x52});
    calc.press_sequence({"В/О", "С/П"});
    const emulator::RunResult first = calc.run_until_stable(120, 6);
    require(first.stopped, "cold start through a В/О pad should reach the stop");
    require(compact(calc.read_register("3")) == "7,",
            "cold start through a В/О pad should execute the loop head at 01");
    calc.press_sequence({"С/П"});
    const emulator::RunResult second = calc.run_until_stable(120, 6);
    require(second.stopped, "the В/О loop return should reach the stop again");
    require(compact(calc.read_register("1")) == "2,",
            "the body before the В/О loop return should execute");
    require(compact(calc.read_register("3")) == "7,",
            "the В/О loop return should re-enter the loop head at 01");
  }
}

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
