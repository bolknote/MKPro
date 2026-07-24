#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::vector<int> clock_step_opcodes(const CompileResult& result) {
  std::vector<int> opcodes;
  opcodes.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    opcodes.push_back(step.opcode);
  return opcodes;
}

std::string clock_hex_literal(const std::string& text) {
  std::string result;
  for (char ch : text) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))) {
    case 'A':
      result.push_back('-');
      break;
    case 'B':
      result.push_back('L');
      break;
    case 'C':
      result += "С";
      break;
    case 'D':
      result += "Г";
      break;
    case 'E':
      result += "Е";
      break;
    case 'F':
      result.push_back('_');
      break;
    default:
      result.push_back(ch);
      break;
    }
  }
  return result;
}

int clock_register_integer(emulator::MK61& calculator, const std::string& reg) {
  std::string value = calculator.read_register(reg);
  value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
                return std::isspace(ch) != 0;
              }),
              value.end());
  std::replace(value.begin(), value.end(), ',', '.');
  return static_cast<int>(std::stod(value));
}

void require_clock_tick(const CompileResult& compiled, int initial_hour, int initial_minute,
                        int expected_hour, int expected_minute) {
  emulator::MK61 calculator({.angle_mode = "grad"});
  const emulator::ProgramLoadResult loaded =
      calculator.load_program(clock_step_opcodes(compiled));
  require(loaded.diagnostics.empty(), "clock program should load without truncation");
  for (const PreloadReport& preload : compiled.preloads) {
    if (preload.value.starts_with("stack."))
      continue;
    calculator.set_register(preload.register_name, clock_hex_literal(preload.value));
  }

  const std::string hour_reg = compiled.registers.at("hour");
  const std::string minute_reg = compiled.registers.at("minute");
  calculator.set_register(hour_reg, std::to_string(initial_hour));
  calculator.set_register(minute_reg, std::to_string(initial_minute));

  calculator.press_sequence({"В/О", "С/П"});
  const emulator::RunResult prompt = calculator.run_until_stable(2000, 5);
  require(prompt.stopped, "clock should stop at the initial HHMM display");

  calculator.press("С/П");
  const emulator::RunResult tick = calculator.run_until_stable(20000, 5);
  require(tick.stopped, "clock should stop after one complete nested delay tick");
  require(clock_register_integer(calculator, hour_reg) == expected_hour,
          "clock hour should advance according to the source-level rollover");
  require(clock_register_integer(calculator, minute_reg) == expected_minute,
          "clock minute should advance according to the source-level rollover");
}

} // namespace

void emulator_clock_optimized_source_preserves_tick() {
  std::ifstream input("examples/clock.mkpro");
  require(static_cast<bool>(input), "clock source should be available from the repository root");
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  const CompileResult compiled = compile_source(source);
  require(compiled.implemented && compiled.diagnostics.empty(),
          "optimized clock source should compile without diagnostics");
  require(compiled.steps.size() == 30U,
          "stack-carried nested delay should keep the clock at 30 program cells");

  require_clock_tick(compiled, 12, 34, 12, 35);
  require_clock_tick(compiled, 23, 59, 0, 0);
}

} // namespace mkpro::tests
