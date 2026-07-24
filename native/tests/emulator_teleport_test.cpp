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

std::vector<int> teleport_opcodes(const CompileResult& compiled) {
  std::vector<int> opcodes;
  opcodes.reserve(compiled.steps.size());
  for (const ResolvedStep& step : compiled.steps)
    opcodes.push_back(step.opcode);
  return opcodes;
}

int teleport_register_integer(emulator::MK61& calculator, const std::string& reg) {
  std::string value = calculator.read_register(reg);
  value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
                return std::isspace(ch) != 0;
              }),
              value.end());
  std::replace(value.begin(), value.end(), ',', '.');
  return static_cast<int>(std::stod(value));
}

std::size_t teleport_count_opcode(const std::vector<ResolvedStep>& steps, int opcode) {
  return static_cast<std::size_t>(
      std::count_if(steps.begin(), steps.end(),
                    [opcode](const ResolvedStep& step) { return step.opcode == opcode; }));
}

} // namespace

void emulator_teleport_dependent_floor_update_preserves_value() {
  std::ifstream input("examples/teleport.mkpro");
  require(static_cast<bool>(input), "teleport source should be available from the repository root");
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  const CompileResult compiled = compile_source(source);
  require(compiled.implemented && compiled.diagnostics.empty(),
          "optimized teleport source should compile without diagnostics");
  require(compiled.steps.size() == 96U,
          "dependent commutative scheduling should keep teleport at 96 program cells");
  require(teleport_count_opcode(compiled.steps, 0x36) == 1U,
          "teleport main program should retain its ordinary K max command");
  require(compiled.setup_program.has_value(),
          "teleport should provide executable setup for its random floors");
  require(teleport_count_opcode(compiled.setup_program->steps, 0x36) == 1U,
          "teleport random-floor setup should retain its ordinary K max command");

  emulator::MK61 calculator({.angle_mode = "rad"});
  const emulator::ProgramLoadResult loaded = calculator.load_program(teleport_opcodes(compiled));
  require(loaded.diagnostics.empty(), "teleport should load without truncation");

  calculator.set_register(compiled.registers.at("charges"), "5");
  calculator.set_register(compiled.registers.at("vault_uses"), "9");
  calculator.set_register(compiled.registers.at("floor"), "1");
  calculator.set_register(compiled.registers.at("room"), "7");
  calculator.set_register(compiled.registers.at("command"), "0");
  calculator.set_register(compiled.registers.at("empty_row"), "0");
  calculator.set_register(compiled.registers.at("row"), "0");
  calculator.set_register(compiled.registers.at("floors_1"), "8.0000001");
  calculator.set_register(compiled.registers.at("floors_2"), "0");
  calculator.set_register(compiled.registers.at("floors_3"), "0");
  calculator.set_register(compiled.registers.at("floors_4"), "0");
  calculator.set_register("e", "10");
  calculator.set_register("a", "L2");
  calculator.set_register("b", "80");
  calculator.set_register("c", "32");

  calculator.press_sequence({"В/О", "С/П"});
  require(calculator.run_until_stable(4000, 6).stopped,
          "teleport should stop to display the initial floor");
  calculator.input_number("8", true);
  calculator.press("С/П");
  require(calculator.run_until_stable(10000, 6).stopped,
          "teleport should finish an upward movement and display the next turn");
  require(teleport_register_integer(calculator, compiled.registers.at("floor")) == 2,
          "teleport should move from floor 1 to floor 2 when the current room has a ladder");
}

} // namespace mkpro::tests
