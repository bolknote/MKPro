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

std::vector<int> opcodes(const std::vector<ResolvedStep>& steps) {
  std::vector<int> result;
  for (const auto& step : steps)
    result.push_back(step.opcode);
  return result;
}

std::string number_literal(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
                return std::isspace(ch) != 0;
              }), value.end());
  std::replace(value.begin(), value.end(), ',', '.');
  return value;
}

void start(emulator::MK61& calculator, const std::string& context) {
  calculator.press_sequence({"В/О", "С/П"});
  require(calculator.run_until_stable(10000, 6).stopped, context + " should stop");
}

struct RowFacts {
  std::string packed;
  int digit = 0;
};

// Evaluate the source's elementary operations directly on ROM, independently
// of compiler layout, helper sharing, preloads and indirect branch selectors.
// Host floating point is not an oracle for MK-61 sine or packed BCD bit_and.
RowFacts row_facts(const std::string& seed, int floor, int row, int room) {
  emulator::MK61 calculator({.angle_mode = "rad"});
  require(calculator.load_program({
              0x60, 0x61, 0x1c, 0x13, 0x62, 0x12, 0x64, 0x37, 0x47,
              0x63, 0x15, 0x13, 0x35, 0x65, 0x12, 0x34, 0x46, 0x50,
          }).diagnostics.empty(), "labyrinth row oracle should load");
  calculator.set_register("0", seed);
  calculator.set_register("1", std::to_string(floor));
  calculator.set_register("2", std::to_string(row));
  calculator.set_register("3", std::to_string(room));
  calculator.set_register("4", "9.5555555");
  calculator.set_register("5", "10");
  start(calculator, "labyrinth row oracle");
  return {calculator.read_register("7"),
          static_cast<int>(std::stod(number_literal(calculator.read_register("6"))))};
}

std::string row_display(int row, const std::string& packed) {
  emulator::MK61 calculator({.angle_mode = "rad"});
  require(calculator.load_program({0x60, 0x61, 0x14, 0x25, 0x0c, 0x50}).diagnostics.empty(),
          "labyrinth display oracle should load");
  calculator.set_register("0", std::to_string(row));
  calculator.set_register("1", number_literal(packed));
  start(calculator, "labyrinth display oracle");
  return calculator.display_text();
}

struct Scenario {
  const char* name;
  int room;
  int energy;
  int command;
  int next_row;
  int next_room;
  bool victory = false;
};

void check_scenario(const CompileResult& compiled, const Scenario& scenario,
                    const std::string& mode) {
  const std::string context = "labyrinth777 " + mode + " " + scenario.name;
  constexpr int floor = 4;
  constexpr int row = 4;
  const std::string seed = "0.1234567";
  const RowFacts before = row_facts(seed, floor, row, scenario.room);
  const int energy = scenario.energy + 1 - before.digit;
  const bool drained = energy < 0;
  const int next_row = drained ? row : scenario.next_row;
  const int next_room = drained ? scenario.room : scenario.next_room;
  const RowFacts after = row_facts(seed, floor, next_row, next_room);

  emulator::MK61 calculator({.angle_mode = "rad"});
  require(calculator.load_program(opcodes(compiled.setup_program->steps)).diagnostics.empty(),
          context + " setup should fit calculator memory");
  start(calculator, context + " setup");
  const auto set = [&](const std::string& name, const std::string& value) {
    calculator.set_register(compiled.registers.at(name), value);
  };
  const auto read = [&](const std::string& name) {
    return calculator.read_register(compiled.registers.at(name));
  };
  set("seed", seed);
  set("floor", std::to_string(floor));
  set("row", std::to_string(row));
  set("room", std::to_string(scenario.room));
  set("energy", std::to_string(scenario.energy));
  set("exit_view", scenario.victory ? number_literal(after.packed) : "9.9999999");
  set("row_view", "0");
  require(calculator.load_program(opcodes(compiled.steps)).diagnostics.empty(),
          context + " main program should fit calculator memory");
  start(calculator, context + " first prompt");
  const std::string prompt_pc = calculator.program_counter();
  require(calculator.display_text() == row_display(row, "0"),
          context + " should preserve the initial packed-row display");

  calculator.input_number(std::to_string(scenario.command), true);
  calculator.press("С/П");
  require(calculator.run_until_stable(10000, 6).stopped, context + " turn should stop");
  require(read("floor") == std::to_string(floor) + "," &&
              read("row") == std::to_string(next_row) + "," &&
              read("room") == std::to_string(next_room) + ",",
          context + " should preserve the source coordinate updates");
  require(read("energy") == std::to_string(energy) + ",",
          context + " should charge the room being left before moving");
  require(read("row_view") == (drained ? before.packed : after.packed),
          context + " should regenerate the correct row");
  if (drained || scenario.victory) {
    require(calculator.program_counter() != prompt_pc,
            context + " should reach a terminal stop rather than another prompt");
    require(calculator.display_text() == (drained ? std::to_string(energy) + "," : "0,"),
            context + " should display the loss energy or victory zero");
    return;
  }
  require(calculator.program_counter() == prompt_pc &&
              calculator.display_text() == row_display(next_row, after.packed),
          context + " should return to the packed-row input prompt");

  // A second turn exercises the return to the input screen, not just boot.
  calculator.input_number("1", true);
  calculator.press("С/П");
  require(calculator.run_until_stable(10000, 6).stopped,
          context + " inspection continuation should stop");
  require(calculator.program_counter() == prompt_pc &&
              calculator.display_text() == row_display(next_row, after.packed) &&
              read("energy") == std::to_string(energy + 1 - after.digit) + "," &&
              read("floor") == std::to_string(floor) + "," &&
              read("row") == std::to_string(next_row) + "," &&
              read("room") == std::to_string(next_room) + ",",
          context + " should accept another command without losing state");
}

} // namespace

void emulator_labyrinth777_preserves_gameplay_contract() {
  std::ifstream input("examples/labyrinth777.mkpro");
  require(static_cast<bool>(input), "labyrinth777 source should be readable");
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  const std::vector<Scenario> scenarios{
      {"right", 4, 4, 6, 4, 5},
      {"left", 4, 4, 4, 4, 3},
      {"next-row", 4, 4, 8, 5, 4},
      {"previous-row", 4, 4, 2, 3, 4},
      {"inspect", 4, 4, 1, 4, 4},
      {"no-jumper", 4, 4, 5, 4, 4},
      {"zero-energy-is-not-defeat", 1, 7, 6, 4, 2},
      {"drained-before-movement", 1, 0, 6, 4, 2},
      {"victory", 4, 4, 1, 4, 4, true},
  };
  for (const bool fast : {false, true}) {
    CompileOptions options;
    options.fast_candidate_search = fast;
    const CompileResult compiled = compile_source(source, options);
    require(compiled.implemented && compiled.diagnostics.empty() &&
                compiled.steps.size() <= 105U && compiled.setup_program.has_value(),
            "labyrinth777 should compile and initialize within stock MK-61 memory");
    for (const Scenario& scenario : scenarios)
      check_scenario(compiled, scenario, fast ? "fast" : "normal");
  }
}

} // namespace mkpro::tests
