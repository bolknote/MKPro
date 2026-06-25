#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

const char* kSource = R"mkpro(program NearAnyHelperProbe {
  state {
    room: counter 0..20 = 0
    a: counter 0..20 = 0
    b: counter 0..20 = 0
    c: counter 0..20 = 0
    d: counter 0..20 = 0
    result: counter 0..9 = 0
  }

  loop {
    result = 0
    if near_any(room, 1, a, b) >= 0 {
      result = 1
    }
    if near_any(room, 1, c, d) >= 0 {
      result = result + 2
    }
    halt(result)
  }
})mkpro";

std::vector<int> step_opcodes(const std::vector<ResolvedStep>& steps) {
  std::vector<int> codes;
  codes.reserve(steps.size());
  for (const ResolvedStep& step : steps)
    codes.push_back(step.opcode);
  return codes;
}

std::string compact_display(std::string text) {
  text.erase(std::remove_if(text.begin(), text.end(),
                            [](unsigned char ch) { return std::isspace(ch) != 0; }),
             text.end());
  return text;
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& optimization) {
                       return optimization.name == name;
                     });
}

std::string run_probe(const CompileResult& result, const std::map<std::string, int>& values) {
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(result.steps));
  require(loaded.diagnostics.empty(), "near_any helper program should load without diagnostics");

  for (const PreloadReport& preload : result.preloads)
    calc.set_register(preload.register_name, preload.value);

  for (const auto& [name, value] : values) {
    const auto reg = result.registers.find(name);
    require(reg != result.registers.end(),
            "near_any probe should expose register for " + name);
    calc.set_register(reg->second, std::to_string(value));
  }

  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(400, 5);
  require(run.stopped, "near_any helper probe should stop");
  return compact_display(calc.display_text());
}

void require_probe(const CompileResult& result, std::map<std::string, int> values,
                   const std::string& expected, const std::string& context) {
  const std::string actual = run_probe(result, values);
  require(actual.find(expected) != std::string::npos,
          context + " should display " + expected + ", got " + actual);
}

} // namespace

void emulator_near_any_helper_matches_typescript_contract() {
  CompileOptions options;
  options.budget = 999;
  const CompileResult result = compile_source(kSource, options);
  require(result.implemented, "near_any helper probe should compile");
  require(result.diagnostics.empty(), "near_any helper probe should compile without diagnostics");
  require(has_optimization(result, "near-any-helper-lowering"),
          "near_any helper probe should report helper-call lowering");
  require(has_optimization(result, "near-any-helper"),
          "near_any helper probe should emit the shared helper body");

  const std::map<std::string, int> base = {
      {"room", 10}, {"a", 8}, {"b", 12}, {"c", 7}, {"d", 14}, {"result", 0},
  };

  require_probe(result, base, "0", "near_any no-hit probe");
  std::map<std::string, int> first = base;
  first["b"] = 11;
  require_probe(result, first, "1", "near_any first predicate hit");
  std::map<std::string, int> second = base;
  second["d"] = 9;
  require_probe(result, second, "2", "near_any second predicate hit");
  std::map<std::string, int> both = first;
  both["d"] = 9;
  require_probe(result, both, "3", "near_any both predicates hit");
}

} // namespace mkpro::tests
