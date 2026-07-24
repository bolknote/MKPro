#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string fox_hunt_hex_literal(const std::string& text) {
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

std::vector<int> fox_hunt_step_opcodes(const CompileResult& compiled) {
  std::vector<int> opcodes;
  opcodes.reserve(compiled.steps.size());
  for (const ResolvedStep& step : compiled.steps)
    opcodes.push_back(step.opcode);
  return opcodes;
}

} // namespace

void emulator_fox_hunt_100_optimized_loop_returns_to_input() {
  std::ifstream input("examples/fox-hunt-100.mkpro");
  require(static_cast<bool>(input),
          "fox-hunt-100 source should be available from the repository root");
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  const CompileResult compiled = compile_source(source);
  require(compiled.implemented && compiled.diagnostics.empty(),
          "optimized fox-hunt-100 source should compile without diagnostics");
  require(compiled.steps.size() == 102U,
          "reclaimed loop selector should keep fox-hunt-100 at 102 program cells");

  emulator::MK61 calculator({.angle_mode = "grad"});
  const emulator::ProgramLoadResult loaded =
      calculator.load_program(fox_hunt_step_opcodes(compiled));
  require(loaded.diagnostics.empty(), "fox-hunt-100 should load without truncation");
  for (const PreloadReport& preload : compiled.preloads) {
    if (preload.value == "random()" || preload.value.starts_with("stack."))
      continue;
    calculator.set_register(preload.register_name, fox_hunt_hex_literal(preload.value));
  }
  calculator.set_register(compiled.registers.at("foxes"), "0");
  calculator.set_register(compiled.registers.at("remaining_foxes"), "3");

  calculator.press_sequence({"В/О", "С/П"});
  const emulator::RunResult prompt = calculator.run_until_stable(2000, 5);
  require(prompt.stopped && calculator.program_counter() == "01",
          "fox-hunt-100 should wait for a cell at program address 00");

  calculator.input_number("11", true);
  calculator.press("С/П");
  const emulator::RunResult clue = calculator.run_until_stable(20000, 5);
  require(clue.stopped, "fox-hunt-100 should stop after displaying a bearing");

  calculator.press("С/П");
  const emulator::RunResult next_prompt = calculator.run_until_stable(2000, 5);
  require(next_prompt.stopped && calculator.program_counter() == "01",
          "optimized fox-hunt-100 loop-back should return to the input stop");
}

} // namespace mkpro::tests
