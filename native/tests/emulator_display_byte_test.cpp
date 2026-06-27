#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <string>

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

} // namespace

void emulator_display_byte_facts_match_typescript_contract() {
  {
    emulator::MK61 calc;
    calc.load_program({0x5f, 0x50});
    calc.set_register("x", "12345678");
    calc.press_sequence({"В/О", "С/П"});
    const emulator::RunResult run = calc.run_until_stable(200, 5);
    require(run.stopped, "opcode 5F program should stop");
    require(compact(calc.read_register("x")) == "12345678,",
            "opcode 5F should leave X untouched");

    emulator::MK61 baseline;
    baseline.load_program({0x50});
    baseline.set_register("x", "12345678");
    baseline.press_sequence({"В/О", "С/П"});
    baseline.run_until_stable(200, 5);
    require(calc.display_text() != baseline.display_text(),
            "opcode 5F should visibly mutate the display indicator");
  }

  {
    emulator::MK61 calc;
    calc.load_program({0x35, 0x50});
    calc.set_register("x", "3.14159");
    calc.press_sequence({"В/О", "С/П"});
    const emulator::RunResult run = calc.run_until_stable(200, 5);
    require(run.stopped, "К {x} program should stop");
    require(compact(calc.read_register("x")).find("4159") != std::string::npos,
            "К {x} should extract the fractional part of X");
  }
}

} // namespace mkpro::tests
