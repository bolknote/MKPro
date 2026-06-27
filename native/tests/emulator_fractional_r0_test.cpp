#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <array>
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

std::vector<int> stop_program() {
  return std::vector<int>(105, 0x50);
}

} // namespace

void emulator_fractional_r0_matches_typescript_contract() {
  for (const std::string r0 : {"0.5", "0.1", "0.7"}) {
    emulator::MK61 calc;
    calc.load_program({0xd0, 0x50});
    calc.set_register("0", r0);
    calc.set_register("3", "42");
    calc.press_sequence({"В/О", "С/П"});
    const emulator::RunResult run = calc.run_until_stable(200, 5);
    require(run.stopped, "fractional R0 indirect recall should stop");
    require(compact(calc.read_register("x")) == "42,",
            "К П->X 0 with fractional R0 should recall R3");
    require(compact(calc.read_register("0")) == "-99999999,",
            "К П->X 0 with fractional R0 should leave the sentinel in R0");
  }

  {
    std::vector<int> program = stop_program();
    program[0] = 0x80;
    program[1] = 0x01;
    program[99] = 0x07;
    program[100] = 0x50;

    emulator::MK61 calc;
    calc.load_program(program);
    calc.set_register("0", "0.5");
    calc.press_sequence({"В/О", "С/П"});
    const emulator::RunResult run = calc.run_until_stable(300, 5);
    require(run.stopped, "К БП 0 with fractional R0 should stop after jumping to 99");
    require(compact(calc.display_text()).find("7") != std::string::npos,
            "К БП 0 with fractional R0 should jump to address 99");
    require(compact(calc.read_register("0")) == "-99999999,",
            "К БП 0 with fractional R0 should leave the sentinel in R0");
  }

  {
    std::vector<int> program = stop_program();
    program[0] = 0xa0;
    program[1] = 0x01;
    program[2] = 0x50;
    program[99] = 0x07;
    program[100] = 0x52;

    emulator::MK61 calc;
    calc.load_program(program);
    calc.set_register("0", "0.5");
    calc.press_sequence({"В/О", "С/П"});
    const emulator::RunResult run = calc.run_until_stable(500, 5);
    require(run.stopped, "К ПП 0 with fractional R0 should return and stop");
    require(compact(calc.display_text()).find("1") != std::string::npos,
            "К ПП 0 with fractional R0 should return to address 1");
    require(compact(calc.read_register("0")) == "-99999999,",
            "К ПП 0 with fractional R0 should leave the sentinel in R0");
  }

  struct ConditionalCase {
    int opcode;
    std::string fallthrough_x;
    std::string jump_x;
  };

  constexpr std::array<ConditionalCase, 4> cases = {{
      {0xe0, "0", "5"},
      {0x70, "5", "0"},
      {0x90, "5", "-5"},
      {0xc0, "-5", "5"},
  }};

  for (const ConditionalCase& test_case : cases) {
    std::vector<int> fallthrough_program = stop_program();
    fallthrough_program[0] = test_case.opcode;
    fallthrough_program[1] = 0x01;
    fallthrough_program[2] = 0x02;
    fallthrough_program[99] = 0x07;
    fallthrough_program[100] = 0x50;

    emulator::MK61 fallthrough;
    fallthrough.load_program(fallthrough_program);
    fallthrough.set_register("0", "0.5");
    fallthrough.set_register("x", test_case.fallthrough_x);
    fallthrough.press_sequence({"В/О", "С/П"});
    require(fallthrough.run_until_stable(300, 5).stopped,
            "fractional R0 indirect conditional fallthrough should stop");
    require(compact(fallthrough.display_text()).find("12") != std::string::npos,
            "fractional R0 indirect conditional should fall through on true branch");
    require(compact(fallthrough.read_register("0")) == "0,5",
            "true-branch fractional R0 indirect conditional should preserve R0");

    std::vector<int> jump_program = stop_program();
    jump_program[0] = test_case.opcode;
    jump_program[1] = 0x01;
    jump_program[2] = 0x02;
    jump_program[99] = 0x07;
    jump_program[100] = 0x50;

    emulator::MK61 jumping;
    jumping.load_program(jump_program);
    jumping.set_register("0", "0.5");
    jumping.set_register("x", test_case.jump_x);
    jumping.press_sequence({"В/О", "С/П"});
    require(jumping.run_until_stable(300, 5).stopped,
            "fractional R0 indirect conditional false branch should stop");
    require(compact(jumping.display_text()).find("7") != std::string::npos,
            "fractional R0 indirect conditional should jump to address 99 on false branch");
    require(compact(jumping.read_register("0")) == "-99999999,",
            "false-branch fractional R0 indirect conditional should leave sentinel in R0");
  }
}

} // namespace mkpro::tests
