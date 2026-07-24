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

std::vector<int> jack_pot_opcodes(const CompileResult& compiled) {
  std::vector<int> opcodes;
  opcodes.reserve(compiled.steps.size());
  for (const ResolvedStep& step : compiled.steps)
    opcodes.push_back(step.opcode);
  return opcodes;
}

int jack_pot_register_integer(emulator::MK61& calculator, const std::string& reg) {
  std::string value = calculator.read_register(reg);
  value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
                return std::isspace(ch) != 0;
              }),
              value.end());
  std::replace(value.begin(), value.end(), ',', '.');
  return static_cast<int>(std::stod(value));
}

bool is_numeric_preload(const std::string& value) {
  return value.find_first_not_of("0123456789.,+-") == std::string::npos;
}

} // namespace

void emulator_jack_pot_random_digits_keep_advancing() {
  std::ifstream input("examples/jack-pot.mkpro");
  require(static_cast<bool>(input), "jack-pot source should be available from the repository root");
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  const CompileResult compiled = compile_source(source);
  require(compiled.implemented && compiled.diagnostics.empty(),
          "optimized jack-pot source should compile without diagnostics");
  require(compiled.steps.size() == 94U,
          "reclaimed constant preloads should keep jack-pot at 94 program cells");
  require(std::none_of(compiled.steps.begin(), compiled.steps.end(),
                       [](const ResolvedStep& step) { return step.opcode == 0x34; }),
          "jack-pot random integer lowering must not use K [x], which freezes the generator");

  emulator::MK61 calculator({.angle_mode = "deg"});
  const emulator::ProgramLoadResult loaded = calculator.load_program(jack_pot_opcodes(compiled));
  require(loaded.diagnostics.empty(), "jack-pot should load without truncation");
  for (const PreloadReport& preload : compiled.preloads) {
    if (is_numeric_preload(preload.value))
      calculator.set_register(preload.register_name, preload.value);
  }

  calculator.press_sequence({"В/О", "С/П"});
  require(calculator.run_until_stable(2000, 5).stopped,
          "jack-pot should stop at its initial splash");
  calculator.press("С/П");
  require(calculator.run_until_stable(100000, 8).stopped,
          "jack-pot should complete a spin instead of looping in random integer conversion");

  const int reels = jack_pot_register_integer(calculator, compiled.registers.at("reels"));
  require(reels >= 111 && reels <= 999 && reels / 100 != 0 && (reels / 10) % 10 != 0 &&
              reels % 10 != 0,
          "jack-pot spin should produce three nonzero decimal digits");
}

} // namespace mkpro::tests
