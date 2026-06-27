#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

bool contains_error_stop(const std::string& display) {
  return display.find("ЕГГ") != std::string::npos;
}

std::string run_trap(int opcode, const std::string& x) {
  emulator::MK61 calc;
  calc.load_program({opcode, 0x50});
  calc.set_register("x", x);
  calc.press("В/О");
  calc.press("С/П");
  calc.run_until_stable(200, 5);
  return calc.display_text();
}

}  // namespace

void emulator_mk61_execution_matches_typescript_contract() {
  {
    emulator::MK61 calc;
    const emulator::ProgramLoadResult loaded = calc.load_program({0x40, 0x50});
    require(loaded.diagnostics.empty(), "numeric program load should not report diagnostics");
    calc.set_register("x", "99");
    calc.press("В/О");
    calc.press("С/П");
    calc.run_until_stable(200, 4);
    require(calc.read_register("0") == "99,", "X->P 0 should store X in R0");

    emulator::MK61 alias;
    alias.set_register("x", "99");
    alias.load_program({0x4f, 0x50});
    alias.press("В/О");
    alias.press("С/П");
    alias.run_until_stable(200, 4);
    require(alias.read_register("0") == "99,", "X->P F alias should store X in R0 like TS");
  }

  {
    emulator::MK61 calc;
    calc.load_program({0x01, 0x02, 0x10, 0x50});
    calc.press("В/О");
    calc.press("С/П");
    const emulator::RunResult run = calc.run_until_stable(200, 5);
    require(run.stopped, "simple digit-entry program should stop");
    require(calc.display_text() == "12,", "digit-entry program should match JS emulator display");
  }

  {
    const std::vector<int> explicit_error_opcodes = {0x27, 0x28, 0x29, 0x2b,
                                                     0x2c, 0x2d, 0x2e, 0x3c};
    for (const int opcode : explicit_error_opcodes) {
      emulator::MK61 calc;
      calc.load_program({opcode, 0x50});
      calc.set_register("x", "5");
      calc.press("В/О");
      calc.press("С/П");
      const emulator::RunResult run = calc.run_until_stable(200, 5);
      require(run.stopped, "explicit trap opcode should stop");
      require(calc.program_counter() == "02", "explicit trap should stop at PC 02");
      require(contains_error_stop(calc.display_text()), "explicit trap should display error");
      require(calc.read_register("x") == "5,", "explicit trap should preserve X");
      require(calc.read_register("x1") == "5,", "explicit trap should copy X to X1");
    }
  }

  require(contains_error_stop(run_trap(0x23, "0")), "F 1/x should trap on zero");
  require(!contains_error_stop(run_trap(0x23, "5")), "F 1/x should not trap on non-zero");
  require(contains_error_stop(run_trap(0x21, "-4")), "F sqrt should trap on negative input");
  require(!contains_error_stop(run_trap(0x21, "4")), "F sqrt should not trap on positive input");
  require(contains_error_stop(run_trap(0x17, "0")), "F lg should trap on zero");
  require(contains_error_stop(run_trap(0x17, "-2")), "F lg should trap on negative input");
  require(!contains_error_stop(run_trap(0x17, "10")), "F lg should not trap on positive input");
  require(contains_error_stop(run_trap(0x19, "2")), "F arcsin should trap above one");
  require(!contains_error_stop(run_trap(0x19, "0.5")), "F arcsin should not trap inside domain");
  require(contains_error_stop(run_trap(0x15, "100")), "F 10^x should trap at 100");
  require(!contains_error_stop(run_trap(0x15, "2")), "F 10^x should not trap below 100");
  require(contains_error_stop(run_trap(0x2a, "1.6")),
          "K deg-to-min-sec should trap when fractional part is 0.6");
  require(contains_error_stop(run_trap(0x2a, "0.6")),
          "K deg-to-min-sec should trap on the 0.6 fractional boundary");
  require(!contains_error_stop(run_trap(0x2a, "1.5")),
          "K deg-to-min-sec should not trap below the fractional boundary");
  require(!contains_error_stop(run_trap(0x2a, "0.59")),
          "K deg-to-min-sec should not trap just below the fractional boundary");
}

}  // namespace mkpro::tests
