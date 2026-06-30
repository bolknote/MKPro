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

bool has_proof(const CompileResult& result, const std::string& id) {
  return std::any_of(result.proofs.begin(), result.proofs.end(),
                     [&](const ProofReport& proof) { return proof.id == id; });
}

bool has_dark_selector_preload(const CompileResult& result) {
  return std::any_of(result.preloads.begin(), result.preloads.end(),
                     [](const PreloadReport& preload) {
                       return preload.value.find_first_of("ABCDEFabcdef") != std::string::npos;
                     });
}

bool has_indirect_branch(const CompileResult& result) {
  return std::any_of(result.steps.begin(), result.steps.end(), [](const ResolvedStep& step) {
    return step.opcode >= 0x80 && step.opcode <= 0x8e;
  });
}

struct Scenario {
  std::map<std::string, std::string> registers;
  std::vector<std::string> keys;
};

struct Observation {
  std::string display;
  bool stopped = false;
  std::map<std::string, std::string> registers;
};

const std::vector<std::string> kDataRegisters = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e",
};

Observation observe(const std::vector<int>& codes, const Scenario& scenario,
                    const std::map<std::string, std::string>& preload_registers,
                    const std::set<std::string>& excluded) {
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), "indirect-flow equivalence program should load");

  for (const auto& [reg, value] : scenario.registers)
    calc.set_register(reg, value);
  for (const auto& [reg, value] : preload_registers)
    calc.set_register(reg, mk61_hex_literal(value));

  calc.press_sequence(scenario.keys);
  const emulator::RunResult run = calc.run_until_stable(600, 5);

  std::map<std::string, std::string> registers;
  for (const std::string& reg : kDataRegisters) {
    if (!excluded.contains(reg))
      registers[reg] = trim_ascii(calc.read_register(reg));
  }
  return Observation{
      .display = trim_ascii(calc.display_text()),
      .stopped = run.stopped,
      .registers = std::move(registers),
  };
}

std::string preload_key(const PreloadReport& preload) {
  return preload.register_name + "=" + preload.value;
}

void require_same_observation(const Observation& actual, const Observation& expected,
                              const std::string& context) {
  require(actual.stopped == expected.stopped, context + " stopped state should match");
  require(actual.display == expected.display,
          context + " display expected " + expected.display + ", got " + actual.display);
  require(actual.registers == expected.registers, context + " data registers should match");
}

} // namespace

void emulator_indirect_flow_equivalence_matches_typescript_contract() {
  const std::filesystem::path root = std::filesystem::current_path();
  const std::string source = read_text(root / "examples" / "human.mkpro");

  // The aggressive post-layout indirect-flow rescue is now enabled by default
  // and is selected automatically by candidate search (guarded by local static
  // proof obligations in compile_source). To still exercise the
  // shrink-and-preserve-behavior contract we compare the default compile (which
  // now picks the aggressive form) against a non-aggressive reference produced
  // by disabling candidate search entirely.
  CompileOptions baseline_options;
  baseline_options.disable_candidate_search = true;
  const CompileResult before = compile_source(source, baseline_options);
  const CompileResult after = compile_source(source);

  require(before.implemented, "baseline human.mkpro should compile");
  require(after.implemented, "aggressive indirect-flow human.mkpro should compile");
  require(before.diagnostics.empty(), "baseline human.mkpro should not report diagnostics");
  require(after.diagnostics.empty(), "aggressive indirect-flow human.mkpro should not report diagnostics");
  require(after.steps.size() < before.steps.size(),
          "aggressive indirect-flow should shrink human.mkpro");
  require(has_dark_selector_preload(after),
          "aggressive indirect-flow should add a dark-entry selector preload");
  require(has_indirect_branch(after),
          "aggressive indirect-flow should emit an indirect branch");
  require(has_optimization(after, "dark-entry-layout"),
          "aggressive indirect-flow should report dark-entry-layout");
  require(has_proof(after, "indirect-flow-targets"),
          "aggressive indirect-flow should report its static target proof");

  const std::set<std::string> before_preloads = [&] {
    std::set<std::string> keys;
    for (const PreloadReport& preload : before.preloads)
      keys.insert(preload_key(preload));
    return keys;
  }();

  std::map<std::string, std::string> preload_registers;
  for (const PreloadReport& preload : after.preloads) {
    if (!before_preloads.contains(preload_key(preload)))
      preload_registers[preload.register_name] = preload.value;
  }
  std::set<std::string> excluded;
  for (const auto& [reg, _] : preload_registers)
    excluded.insert(reg);

  const std::vector<Scenario> scenarios = {
      Scenario{.keys = {"В/О", "С/П"}},
      Scenario{.registers = {{"1", "0"}, {"2", "5"}},
               .keys = {"В/О", "С/П", "Сx", "2", "С/П", "С/П"}},
      Scenario{.registers = {{"1", "3"}, {"2", "5"}},
               .keys = {"В/О", "С/П", "Сx", "8", "С/П", "С/П"}},
      Scenario{.registers = {{"1", "7"}, {"2", "9"}},
               .keys = {"В/О", "С/П", "Сx", "6", "С/П"}},
  };

  const std::vector<int> before_codes = step_opcodes(before.steps);
  const std::vector<int> after_codes = step_opcodes(after.steps);
  for (std::size_t index = 0; index < scenarios.size(); ++index) {
    const Observation original = observe(before_codes, scenarios.at(index), {}, excluded);
    const Observation rewritten =
        observe(after_codes, scenarios.at(index), preload_registers, excluded);
    require_same_observation(rewritten, original,
                             "indirect-flow scenario " + std::to_string(index));
  }
}

} // namespace mkpro::tests
