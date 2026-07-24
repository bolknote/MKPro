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

std::vector<int> functions_demo_opcodes(const CompileResult& compiled) {
  std::vector<int> opcodes;
  opcodes.reserve(compiled.steps.size());
  for (const ResolvedStep& step : compiled.steps)
    opcodes.push_back(step.opcode);
  return opcodes;
}

int functions_demo_display_integer(const emulator::MK61& calculator) {
  std::string display = calculator.display_text();
  display.erase(std::remove_if(display.begin(), display.end(), [](unsigned char ch) {
                  return std::isspace(ch) != 0;
                }),
                display.end());
  std::replace(display.begin(), display.end(), ',', '.');
  return static_cast<int>(std::stod(display));
}

} // namespace

void emulator_functions_demo_inlined_calls_preserve_results() {
  std::ifstream input("examples/functions-demo.mkpro");
  require(static_cast<bool>(input),
          "functions-demo source should be available from the repository root");
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  const CompileResult compiled = compile_source(source);
  require(compiled.implemented && compiled.diagnostics.empty(),
          "optimized functions-demo source should compile without diagnostics");
  require(compiled.steps.size() == 13U,
          "fully inlined sum_of_squares should keep functions-demo at 13 cells");

  emulator::MK61 calculator({.angle_mode = "grad"});
  const emulator::ProgramLoadResult loaded =
      calculator.load_program(functions_demo_opcodes(compiled));
  require(loaded.diagnostics.empty(), "functions-demo should load without truncation");

  calculator.press_sequence({"В/О", "С/П"});
  require(calculator.run_until_stable(1000, 5).stopped,
          "functions-demo should stop for its first argument");
  calculator.input_number("3", true);
  calculator.press("С/П");
  require(calculator.run_until_stable(1000, 5).stopped,
          "functions-demo should stop for its second argument");
  calculator.input_number("4", true);
  calculator.press("С/П");
  require(calculator.run_until_stable(2000, 5).stopped,
          "functions-demo should stop with its result");
  require(functions_demo_display_integer(calculator) == 25,
          "inlined sum_of_squares(3, 4) should display 25");

  calculator.press("С/П");
  require(calculator.run_until_stable(1000, 5).stopped &&
              calculator.program_counter() == "01",
          "functions-demo should return to the first input stop");
}

} // namespace mkpro::tests
