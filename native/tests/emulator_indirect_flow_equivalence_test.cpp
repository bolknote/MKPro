#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::tests {
namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  require(input.good(), "should read " + path.string());
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

std::string selector_literal(const std::string& text) {
  // Only generated two-digit formal addresses need display-glyph conversion.
  // Ordinary numeric setup values must not be interpreted as hexadecimal.
  if (text.size() != 2U || text.find_first_of("ABCDEF") == std::string::npos)
    return text;
  std::string result;
  for (char ch : text) {
    switch (ch) {
      case 'A': result += "-"; break;
      case 'B': result += "L"; break;
      case 'C': result += "С"; break;
      case 'D': result += "Г"; break;
      case 'E': result += "Е"; break;
      case 'F': result += "_"; break;
      default: result += ch; break;
    }
  }
  return result;
}

std::vector<int> step_opcodes(const std::vector<ResolvedStep>& steps) {
  std::vector<int> codes;
  for (const ResolvedStep& step : steps)
    codes.push_back(step.opcode);
  return codes;
}

bool has_proof(const CompileResult& result, const std::string& id) {
  return std::any_of(result.proofs.begin(), result.proofs.end(),
                     [&](const ProofReport& proof) {
                       return proof.id == id && proof.status == "proved";
                     });
}

bool has_indirect_branch(const CompileResult& result) {
  return std::any_of(result.steps.begin(), result.steps.end(), [](const ResolvedStep& step) {
    return step.opcode >= 0x80 && step.opcode <= 0x8e;
  });
}

struct Scenario {
  int score = 0;
  int food = 5;
  std::optional<int> key;
};

struct Observation {
  std::string display;
  std::map<std::string, std::string> values;
};

void require_scalar(emulator::MK61& calc, const std::string& reg, int expected,
                    const std::string& context) {
  const std::string value = trim_ascii(calc.read_register(reg));
  require(std::stod(value) == expected,
          context + ": expected " + std::to_string(expected) + ", got " + value);
}

Observation observe(const CompileResult& program, const Scenario& scenario, int gain,
                    const std::string& context) {
  emulator::MK61 calc({.angle_mode = "grad"});
  require(calc.load_program(step_opcodes(program.steps)).diagnostics.empty(),
          context + ": program should load");
  // Apply the complete setup of each artifact, not just newly borrowed selectors.
  for (const PreloadReport& preload : program.preloads) {
    require(!preload.setup_expression, context + ": fixture setup must be literal");
    calc.set_register(preload.register_name, selector_literal(preload.value));
  }
  calc.set_register(program.registers.at("score"), std::to_string(scenario.score));
  calc.set_register(program.registers.at("food"), std::to_string(scenario.food));
  for (const std::string seed : {"14", "13", "12", "11"})
    calc.input_number(seed, true).press("В↑");
  calc.input_number("0", true);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(600, 5).stopped, context + ": initial display should stop");
  require_scalar(calc, "X", scenario.score * 10 + scenario.food, context + " initial display");

  int score = scenario.score;
  int food = scenario.food;
  int display = score * 10 + food;
  if (scenario.key.has_value()) {
    calc.input_number(std::to_string(*scenario.key), true).press("С/П");
    require(calc.run_until_stable(600, 5).stopped, context + ": turn should stop");
    if (*scenario.key == 2)
      score += gain;
    else if (*scenario.key == 8)
      --food;
    display = (*scenario.key == 2 || *scenario.key == 8) ? score * 10 + food : 0;
  }
  require_scalar(calc, program.registers.at("score"), score, context + " score");
  require_scalar(calc, program.registers.at("food"), food, context + " food");
  require_scalar(calc, "X", display, context + " turn display");

  Observation result{.display = trim_ascii(calc.display_text())};
  result.values["score"] = trim_ascii(calc.read_register(program.registers.at("score")));
  result.values["food"] = trim_ascii(calc.read_register(program.registers.at("food")));
  for (const std::string reg : {"X", "Y", "Z", "T", "X1"})
    result.values[reg] = trim_ascii(calc.read_register(reg));
  calc.press(".");
  result.values["X2 probe"] = trim_ascii(calc.display_text());
  return result;
}

void require_same_observation(const Observation& actual, const Observation& expected,
                              const std::string& context) {
  require(actual.display == expected.display,
          context + " display expected " + expected.display + ", got " + actual.display);
  for (const auto& [name, value] : expected.values)
    require(actual.values.at(name) == value,
            context + " " + name + " expected " + value + ", got " + actual.values.at(name));
}

} // namespace

void emulator_indirect_flow_equivalence_matches_typescript_contract() {
  const std::string original =
      read_text(std::filesystem::current_path() / "examples" / "human.mkpro");
  for (int gain : {1, 2}) {
    std::string source = original;
    if (gain == 2) {
      const auto update = source.find("score++");
      require(update != std::string::npos, "counter fixture must contain its update");
      source.replace(update, 7, "score += 2");
    }
    CompileOptions baseline_options;
    baseline_options.disable_candidate_search = true;
    const CompileResult before = compile_source(source, baseline_options);
    const CompileResult after = compile_source(source);
    require(before.implemented && before.diagnostics.empty(),
            "counter reference should compile without diagnostics");
    require(after.implemented && after.diagnostics.empty(),
            "counter candidate should compile without diagnostics");
    require(after.steps.size() <= before.steps.size() && after.steps.size() <= 105U,
            "candidate search must not increase counter program size");

    // The former name-based CounterGame path ignored actual function bodies.
    // A program rename must not change code or register allocation.
    std::string renamed = source;
    const auto name = renamed.find("CounterGame");
    require(name != std::string::npos, "counter fixture must contain its program name");
    renamed.replace(name, 11, "GenericCounter");
    const CompileResult neutral = compile_source(renamed);
    require(neutral.implemented && neutral.diagnostics.empty(),
            "renamed counter should compile");
    require(step_opcodes(neutral.steps) == step_opcodes(after.steps) &&
                neutral.registers == after.registers,
            "counter lowering must not depend on the program name");

    if (gain == 2) {
      // This fixture has no discarded increment read of a borrowed selector.
      require(after.steps.size() < before.steps.size(),
              "safe indirect-flow fixture must still shrink");
      require(has_indirect_branch(after) && has_proof(after, "indirect-flow-targets"),
              "safe indirect-flow fixture must prove its delivered targets");
    }

    const std::vector<Scenario> scenarios{
        {},
        {.score = 0, .food = 5, .key = 2},
        {.score = 6, .food = 5, .key = 2},
        {.score = 7, .food = 5, .key = 2},
        {.score = 3, .food = 5, .key = 8},
        {.score = 7, .food = 9, .key = 6},
    };
    for (std::size_t index = 0; index < scenarios.size(); ++index) {
      const std::string context =
          "counter gain=" + std::to_string(gain) + " scenario=" + std::to_string(index);
      const Observation expected = observe(before, scenarios.at(index), gain, context + " baseline");
      const Observation actual = observe(after, scenarios.at(index), gain, context + " optimized");
      require_same_observation(actual, expected, context);
    }
  }
}

} // namespace mkpro::tests
