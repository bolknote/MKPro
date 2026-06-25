#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  require(input.good(), "should read " + path.string());
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string replace_once(std::string text, const std::string& from, const std::string& to) {
  const std::size_t pos = text.find(from);
  require(pos != std::string::npos, "interprocedural source fixture should contain " + from);
  text.replace(pos, from.size(), to);
  return text;
}

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

std::string mk61_hex_literal(const std::string& text) {
  std::string out;
  for (char ch : text) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))) {
      case 'A':
        out.push_back('-');
        break;
      case 'B':
        out.push_back('L');
        break;
      case 'C':
        out += "С";
        break;
      case 'D':
        out += "Г";
        break;
      case 'E':
        out += "Е";
        break;
      case 'F':
        out.push_back('_');
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

std::vector<int> step_opcodes(const std::vector<ResolvedStep>& steps) {
  std::vector<int> codes;
  codes.reserve(steps.size());
  for (const ResolvedStep& step : steps)
    codes.push_back(step.opcode);
  return codes;
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& optimization) {
                       return optimization.name == name;
                     });
}

struct Observation {
  std::string display;
  bool stopped = false;
  std::map<std::string, std::string> registers;
};

Observation observe(const std::vector<int>& codes, const std::vector<std::string>& keys,
                    const std::vector<PreloadReport>& preloads,
                    const std::set<std::string>& registers_to_compare) {
  emulator::MK61 calc;
  for (const PreloadReport& preload : preloads)
    calc.set_register(preload.register_name, mk61_hex_literal(preload.value));

  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), "interprocedural equivalence program should load");
  calc.press_sequence(keys);
  const emulator::RunResult run = calc.run_until_stable(800, 5);

  std::map<std::string, std::string> registers;
  for (const std::string& reg : registers_to_compare)
    registers[reg] = trim_ascii(calc.read_register(reg));

  return Observation{
      .display = trim_ascii(calc.display_text()),
      .stopped = run.stopped,
      .registers = std::move(registers),
  };
}

void require_same_observation(const Observation& actual, const Observation& expected,
                              const std::string& context) {
  require(actual.stopped == expected.stopped, context + " stopped state should match");
  require(actual.display == expected.display,
          context + " display expected " + expected.display + ", got " + actual.display);
  require(actual.registers == expected.registers, context + " registers should match");
}

} // namespace

void emulator_interprocedural_equivalence_matches_typescript_contract() {
  const std::filesystem::path root = std::filesystem::current_path();
  const std::string source = replace_once(
      read_text(root / "examples" / "game-100-pig.mkpro"),
      "die = random(die_faces)", "die = 3");

  CompileOptions before_options;
  before_options.disable_interprocedural_opts = true;
  // Match the post-layout branch-order variant selected by the TS candidate
  // search for this fixture; this test isolates interprocedural value flow.
  before_options.aggressive_post_layout_indirect_flow = true;
  before_options.invert_branch_order = true;
  const CompileResult before = compile_source(source, before_options);

  CompileOptions after_options;
  after_options.aggressive_post_layout_indirect_flow = true;
  after_options.invert_branch_order = true;
  const CompileResult after = compile_source(source, after_options);

  require(before.implemented, "interprocedural baseline should compile");
  require(after.implemented, "interprocedural optimized program should compile");
  require(before.diagnostics.empty(), "interprocedural baseline should not report diagnostics");
  require(after.diagnostics.empty(), "interprocedural optimized program should not report diagnostics");
  require(has_optimization(after, "interprocedural-value-propagation"),
          "interprocedural value propagation should report the TS optimization name");
  require(!has_optimization(before, "interprocedural-value-propagation"),
          "disabled interprocedural run should not report value propagation");
  require(after.steps.size() < before.steps.size(),
          "interprocedural value propagation should shrink game-100-pig");
  require(after.registers == before.registers,
          "interprocedural value propagation should keep field-to-register allocation");

  std::set<std::string> game_registers;
  for (const auto& [_name, reg] : after.registers)
    game_registers.insert(reg);

  const std::vector<std::vector<std::string>> scripts = {
      {"В/О", "С/П"},
      {"В/О", "С/П", "0", "С/П"},
      {"В/О", "С/П", "2", "С/П", "0", "С/П"},
      {"В/О", "С/П", "2", "С/П", "2", "С/П", "0", "С/П"},
      {"В/О", "С/П", "5", "С/П", "5", "С/П", "5", "С/П"},
  };

  const std::vector<int> before_codes = step_opcodes(before.steps);
  const std::vector<int> after_codes = step_opcodes(after.steps);
  for (std::size_t index = 0; index < scripts.size(); ++index) {
    const Observation original =
        observe(before_codes, scripts.at(index), before.preloads, game_registers);
    const Observation optimized =
        observe(after_codes, scripts.at(index), after.preloads, game_registers);
    require_same_observation(optimized, original,
                             "interprocedural script " + std::to_string(index));
  }
}

} // namespace mkpro::tests
