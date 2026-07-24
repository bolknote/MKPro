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
    std::vector<int> program(105, 0x50);
    program.at(0) = 0x52;
    program.at(1) = 0x52;
    program.at(6) = 0x52;
    program.at(24) = 0x52;
    program.at(64) = 0x52;

    emulator::MK61 calc;
    calc.load_program(program);
    calc.input_number("1E2", true);
    calc.press("В↑");
    calc.input_number("2.3230563E99");
    calc.press("В↑");
    calc.input_number("1E99");
    calc.press_sequence({"*", "*"});
    require(calc.program_counter() == "20" && calc.display_text().find("ГГ") != std::string::npos,
            "3GG0G should select PC 20 after the exact three-factor setup");

    calc.press_sequence({"F", ",", "В/О", "С/П"});
    const emulator::RunResult run = calc.run_until_stable(1000, 5);
    require(run.stopped && calc.program_counter() == "35",
            "3GG0G should preload returns 01, 24, 24, 06, 64 and then dirty target 34");

    emulator::MK61 direct;
    direct.load_program(program);
    direct.input_number("1E2", true);
    direct.press("В↑");
    direct.input_number("2.3230563E99");
    direct.press("В↑");
    direct.input_number("1E99");
    direct.press_sequence({"*", "*"});
    direct.press_sequence({"F", ",", "С/П"});
    const emulator::RunResult direct_run =
        direct.run_until_stable(1000, 5);
    require(direct_run.stopped && direct.program_counter() == "21",
            "3GG0G should permit direct С/П execution from its selected PC 20");

    std::vector<int> call_program = program;
    call_program.at(34) = 0x53;
    call_program.at(35) = 0x50;
    call_program.at(36) = 0x50;
    call_program.at(37) = 0x52;
    call_program.at(50) = 0x07;
    call_program.at(51) = 0x52;
    emulator::MK61 call_loop;
    call_loop.load_program(call_program);
    call_loop.input_number("1E2", true);
    call_loop.press("В↑");
    call_loop.input_number("2.3230563E99");
    call_loop.press("В↑");
    call_loop.input_number("1E99");
    call_loop.press_sequence({"*", "*"});
    call_loop.press_sequence({"F", ",", "В/О", "С/П"});
    const emulator::RunResult first_call =
        call_loop.run_until_stable(1000, 5);
    require(first_call.stopped && call_loop.program_counter() == "37",
            "a leaf call at dirty target 34 should return to its local stop");
    call_loop.press("С/П");
    const emulator::RunResult repeated_call =
        call_loop.run_until_stable(1000, 5);
    require(repeated_call.stopped && call_loop.program_counter() == "37",
            "the restored uniform dirty stack should route a later В/О back "
            "to target 34 across another leaf call");
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
