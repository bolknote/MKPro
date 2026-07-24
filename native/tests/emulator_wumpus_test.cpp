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

std::vector<int> wumpus_opcodes(const CompileResult& compiled) {
  std::vector<int> opcodes;
  opcodes.reserve(compiled.steps.size());
  for (const ResolvedStep& step : compiled.steps)
    opcodes.push_back(step.opcode);
  return opcodes;
}

std::string compact_wumpus_display(std::string text) {
  text.erase(std::remove_if(text.begin(), text.end(),
                            [](unsigned char ch) { return std::isspace(ch) != 0; }),
             text.end());
  return text;
}

} // namespace

void emulator_wumpus_winning_shot_reaches_terminal_display() {
  std::ifstream input("examples/wumpus.mkpro");
  require(static_cast<bool>(input), "wumpus source should be available from the repository root");
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  const CompileResult compiled = compile_source(source);
  require(compiled.implemented && compiled.diagnostics.empty(),
          "optimized wumpus source should compile without diagnostics");
  require(compiled.steps.size() == 105U,
          "wumpus should exactly fill the standard MK-61 program window");

  emulator::MK61 calculator({.angle_mode = "rad"});
  const emulator::ProgramLoadResult loaded = calculator.load_program(wumpus_opcodes(compiled));
  require(loaded.diagnostics.empty(), "wumpus should load without truncation");

  calculator.set_register(compiled.registers.at("room"), "1");
  calculator.set_register(compiled.registers.at("target"), "0");
  calculator.set_register(compiled.registers.at("wumpus"), "7");
  calculator.set_register(compiled.registers.at("hazard_pit_1"), "14");
  calculator.set_register(compiled.registers.at("hazard_pit_2"), "15");
  calculator.set_register(compiled.registers.at("hazard_bat_1"), "16");
  calculator.set_register(compiled.registers.at("hazard_bat_2"), "17");
  calculator.set_register(compiled.registers.at("arrows"), "5");
  calculator.set_register(compiled.registers.at("clue"), "0");
  calculator.set_register("b", "10");
  calculator.set_register("e", "20");
  calculator.set_register("d", "777");
  calculator.set_register("c", "0.8");
  calculator.set_register("9", "99");
  calculator.set_register("a", "67");

  calculator.press_sequence({"В/О", "С/П"});
  require(calculator.run_until_stable(10000, 6).stopped,
          "wumpus should stop at the room-and-arrows display");
  calculator.press("С/П");
  require(calculator.run_until_stable(10000, 6).stopped,
          "wumpus should stop at the clue display");
  calculator.input_number("7", true);
  calculator.press_sequence({"/-/", "С/П"});
  require(calculator.run_until_stable(10000, 6).stopped,
          "wumpus should stop after the winning shot");
  const std::string final_display = compact_wumpus_display(calculator.display_text());
  require(final_display.find("777") != std::string::npos,
          "shooting the Wumpus in room 7 should produce the 777 victory display, got " +
              final_display + " at " + calculator.program_counter());
}

} // namespace mkpro::tests
