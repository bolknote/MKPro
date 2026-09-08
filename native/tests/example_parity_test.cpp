#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read fixture: " + path.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string rstrip_newlines(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
    value.pop_back();
  return value;
}

void require_alaram_movement_contract(const CompileResult& result) {
  require(result.setup_program.has_value(), "alaram must provide its generated setup");
  emulator::MK61 calc({.angle_mode = "grad"});
  const auto load = [&](const std::vector<ResolvedStep>& steps) {
    std::vector<int> codes;
    for (const ResolvedStep& step : steps)
      codes.push_back(step.opcode);
    require(calc.load_program(codes).diagnostics.empty(),
            "alaram contract programs must fit calculator memory");
  };
  load(result.setup_program->steps);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(5000, 6).stopped, "alaram setup must complete");
  load(result.steps);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(5000, 6).stopped, "alaram must show its first prompt");
  const std::string prompt = calc.display_text();
  const std::vector<std::string> commands{"8", "8", "2"};
  const std::vector<std::string> altitudes{"100,", "200,", "100,"};
  const std::vector<std::string> ranges{"-299,", "-298,", "-297,"};
  for (std::size_t index = 0; index < commands.size(); ++index) {
    calc.press_sequence({commands.at(index), "С/П"});
    require(calc.run_until_stable(5000, 6).stopped && calc.display_text() == prompt,
            "alaram must continue after two climbs and one safe descent");
    require(calc.read_register(result.registers.at("altitude")) == altitudes.at(index),
            "alaram altitude must survive the random intruder-mark update");
    require(calc.read_register(result.registers.at("range")) == ranges.at(index),
            "alaram must advance range on every successful movement");
  }
}

} // namespace

void supported_examples_match_native_oracles() {
  const std::filesystem::path root = std::filesystem::current_path();
  const std::vector<std::string> supported_examples = {
      "99-bottles",        "alaram",         "basic",
      "cave-highlevel-baseline",             "cave-sketch",
      "cave-treasure",     "clock",          "dangerous-loading",
      "dungeon",           "e-94-digits",    "fox-hunt-100",
      "fox-hunt-mk61",     "functions-demo", "game-100-pig",
      "giants-country",    "human",          "jack-pot",
      "labyrinth777",      "lunar",          "minesweeper-9x7",
      "minesweeper-9x9",   "raja-yoga",      "river-battle",
      "rambo-iii",         "sea-battle",     "teleport",
      "tic-tac-toe",       "tiny-game",      "treasure-hunter-2",
      "wumpus",
  };

  const bool progress = std::getenv("MKPRO_NATIVE_EXAMPLE_PROGRESS") != nullptr;
  for (std::size_t index = 0; index < supported_examples.size(); ++index) {
    const std::string& name = supported_examples.at(index);
    if (progress) {
      std::cerr << "[example-parity] " << (index + 1U) << "/" << supported_examples.size()
                << " " << name << std::endl;
    }
    const std::string source = read_text(root / "examples" / (name + ".mkpro"));
    const std::string oracle_hex =
        rstrip_newlines(read_text(root / "native" / "oracles" / "examples" / name / "hex.txt"));
    const CompileResult result = compile_source(source);
    require(result.implemented, "native compiler should implement example: " + name);
    require(result.diagnostics.empty(), "native example diagnostics should be empty: " + name);
    if (name == "alaram")
      require_alaram_movement_contract(result);
    require(result.hex == oracle_hex, "native example hex mismatch: " + name);
  }
}

} // namespace mkpro::tests
