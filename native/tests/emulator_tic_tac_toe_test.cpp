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
  for (const ResolvedStep& step : steps)
    result.push_back(step.opcode);
  return result;
}

std::string display(emulator::MK61& calc) {
  std::string text = calc.display_text();
  text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char ch) {
               return std::isspace(ch) != 0;
             }),
             text.end());
  return text;
}

struct Scenario {
  const char* name;
  std::vector<int> inputs;
  std::vector<int> outputs;
};

void play(const CompileResult& compiled, const Scenario& scenario,
          const std::string& mode) {
  const std::string context = mode + "/" + scenario.name;
  require(scenario.outputs.size() == scenario.inputs.size() + 1U,
          context + ": one expected display per input and initial prompt");
  emulator::MK61Options options;
  options.angle_mode = "grad";
  emulator::MK61 calc(options);
  require(compiled.setup_program.has_value(),
          context + ": expected-mode initializer should have executable setup");
  require(calc.load_program(opcodes(compiled.setup_program->steps)).diagnostics.empty(),
          context + ": setup should fit hardware");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(10000, 6).stopped, context + ": setup should stop");
  require(calc.load_program(opcodes(compiled.steps)).diagnostics.empty(),
          context + ": program should fit hardware");
  calc.press_sequence({"В/О", "С/П"});

  for (std::size_t turn = 0; turn < scenario.outputs.size(); ++turn) {
    require(calc.run_until_stable(10000, 6).stopped,
            context + ": game should stop at turn " + std::to_string(turn));
    const std::string actual = display(calc);
    const std::string expected = std::to_string(scenario.outputs[turn]) + ",";
    require(actual == expected,
            context + ": turn " + std::to_string(turn) + " expected " + expected +
                ", got " + actual);
    if (turn < scenario.inputs.size()) {
      // S/P resumes while the user's numeric entry is still open. Do not add
      // Enter: that would mask a missing compiler-generated entry boundary.
      // Cx is not part of this protocol either: it changes stack-lift state.
      calc.input_number(std::to_string(scenario.inputs[turn]));
      calc.press("С/П");
    }
  }
}

} // namespace

void emulator_tic_tac_toe_preserves_gameplay_contract() {
  std::ifstream input("examples/tic-tac-toe.mkpro");
  require(input.good(), "tic-tac-toe source should be readable");
  const std::string source{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
  // Independent source-level game expectations, not an opcode snapshot.
  const std::vector<Scenario> scenarios{
      {"outer-immediate", {1, 1}, {5, 8, 2000000}},
      {"outer-left", {1, 2, 7}, {5, 8, 9, 1000000}},
      {"outer-right", {1, 2, 6}, {5, 8, 9, 7000000}},
      {"side-immediate", {2, 1}, {5, 6, 4000000}},
      {"side-second", {2, 4, 1}, {5, 6, 1, 9000000}},
      {"side-draw", {2, 4, 9, 2}, {5, 6, 1, 8, 0}},
      {"side-last", {2, 4, 9, 3}, {5, 6, 1, 8, 2000000}},
      {"middle-immediate", {4, 1}, {5, 6, 4000000}},
      {"middle-second", {4, 4, 1}, {5, 6, 7, 3000000}},
      {"middle-draw", {4, 4, 3, 8}, {5, 6, 7, 2, 0}},
      {"middle-last", {4, 4, 3, 9}, {5, 6, 7, 2, 8000000}},
      {"inner-immediate", {3, 1}, {5, 6, 4000000}},
      {"inner-left", {3, 4, 3}, {5, 6, 9, 1000000}},
      {"inner-right", {3, 4, 1}, {5, 6, 9, 3000000}},
      {"outer-mirror", {9, 2, 7}, {5, 8, 9, 1000000}},
      {"side-mirror", {8, 4, 9, 2}, {5, 6, 1, 8, 0}},
      {"middle-mirror", {6, 4, 3, 8}, {5, 6, 7, 2, 0}},
      {"inner-mirror", {7, 4, 3}, {5, 6, 9, 1000000}},
      {"default-side", {5, 4, 9, 2}, {5, 6, 1, 8, 0}},
  };
  for (const bool fast : {false, true}) {
    CompileOptions options;
    options.fast_candidate_search = fast;
    const CompileResult compiled = compile_source(source, options);
    require(compiled.implemented && compiled.diagnostics.empty() &&
                compiled.steps.size() <= 105U,
            "tic-tac-toe should compile within the stock MK-61 program window");
    for (const Scenario& scenario : scenarios)
      play(compiled, scenario, fast ? "fast" : "normal");
  }
}

} // namespace mkpro::tests
