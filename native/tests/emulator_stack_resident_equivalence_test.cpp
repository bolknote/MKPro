#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

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
                     [&](const OptimizationReport& entry) { return entry.name == name; });
}

bool has_error_diagnostic(const CompileResult& result) {
  return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [](const Diagnostic& diagnostic) {
                       return diagnostic.severity == DiagnosticSeverity::Error;
                     });
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot read fixture: " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
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
  require(loaded.diagnostics.empty(), "stack-resident equivalence run should load without diagnostics");
  calc.press_sequence(keys);
  const emulator::RunResult run = calc.run_until_stable(800, 5);

  std::map<std::string, std::string> register_values;
  for (const std::string& register_name : registers_to_compare)
    register_values[register_name] = trim_ascii(calc.read_register(register_name));

  return Observation{
      .display = trim_ascii(calc.display_text()),
      .stopped = run.stopped,
      .registers = std::move(register_values),
  };
}

void require_same_observation(const Observation& actual, const Observation& expected,
                             const std::string& context) {
  require(actual.stopped == expected.stopped, context + " stopped state should match");
  require(actual.display == expected.display,
          context + " expected display " + expected.display + ", got " + actual.display);
  require(actual.registers == expected.registers, context + " registers should match");
}

CompileResult compile_variant(const std::string& source, bool stack_resident, bool canonicalize_arg_temps) {
  CompileOptions options;
  options.budget = 999999;
  options.stack_resident_temps = stack_resident;
  options.canonicalize_repeated_unary_update_args = canonicalize_arg_temps;
  options.disable_candidate_search = true;
  return compile_source(source, options);
}

} // namespace

void emulator_stack_resident_equivalence_matches_typescript_contract() {
  const std::string dual_stack_source = R"mkpro(
program DualStackEq {
  state {
    x: packed = 3
    y: packed = 4
    z: packed = 0
  }
  loop {
    a = x
    b = y
    z = a + b
    halt(z)
  }
}
)mkpro";

  {
    const CompileResult baseline = compile_variant(dual_stack_source, false, false);
    const CompileResult optimized = compile_variant(dual_stack_source, true, false);

    require(baseline.implemented, "baseline dual-stack variant should compile");
    require(optimized.implemented, "stack-resident dual-stack variant should compile");
    
    require(has_optimization(optimized, "stack-resident-temps"),
            "stack-resident variant should report stack-resident-temps");
    require(optimized.steps.size() <= baseline.steps.size(),
            "stack-resident dual-stack variant should not grow");

    const std::vector<int> baseline_codes = step_opcodes(baseline.steps);
    const std::vector<int> optimized_codes = step_opcodes(optimized.steps);
    const std::string baseline_z = baseline.registers.at("z");
    const std::string optimized_z = optimized.registers.at("z");
    require(baseline_z == optimized_z, "dual-stack stack-resident registers should match");

    const Observation baseline_observation = observe(baseline_codes, {"В/О", "С/П"}, baseline.preloads,
                                                    {baseline_z});
    const Observation optimized_observation = observe(optimized_codes, {"В/О", "С/П"},
                                                     optimized.preloads, {optimized_z});
    require_same_observation(optimized_observation, baseline_observation,
                            "dual-stack stack-resident");
  }

  const std::string control_flow_source = R"mkpro(
program CrossFlowEq {
  state {
    x: packed = 3
    y: packed = 4
    z: packed = 0
    gate: packed = 0
  }
  loop {
    a = x
    b = y
    if gate == 1 {
      loop {
      }
    }
    z = a + b
    halt(z)
  }
}
)mkpro";

  {
    const CompileResult baseline = compile_variant(control_flow_source, false, false);
    const CompileResult optimized = compile_variant(control_flow_source, true, false);

    require(baseline.implemented, "control-flow baseline should compile");
    require(optimized.implemented, "control-flow stack-resident variant should compile");
    require(has_optimization(optimized, "stack-resident-control-flow"),
            "control-flow stack variant should report stack-resident-control-flow");

    const std::vector<int> baseline_codes = step_opcodes(baseline.steps);
    const std::vector<int> optimized_codes = step_opcodes(optimized.steps);
    const std::string baseline_z = baseline.registers.at("z");
    const std::string optimized_z = optimized.registers.at("z");
    require(baseline_z == optimized_z, "control-flow stack-resident registers should match");

    const Observation baseline_observation = observe(baseline_codes, {"В/О", "С/П"},
                                                    baseline.preloads, {baseline_z});
    const Observation optimized_observation = observe(optimized_codes, {"В/О", "С/П"},
                                                     optimized.preloads, {optimized_z});
    require_same_observation(optimized_observation, baseline_observation,
                            "control-flow stack-resident");
  }

  const std::string repeated_unary_source = R"mkpro(
program RepeatedUnaryArgEq {
  state {
    a: packed = 2
    b: packed = 5
    c: packed = 100
    d: packed = 0
  }
  loop {
    c -= sqr(a)
    d = a + b
    c -= sqr(b)
    halt(c)
  }
}
)mkpro";

  {
    const CompileResult baseline = compile_variant(repeated_unary_source, false, false);
    const CompileResult optimized = compile_variant(repeated_unary_source, false, true);
    require(optimized.implemented, "canonicalized repeated-unary variant should compile");
    require(has_optimization(optimized, "repeated-unary-update-arg-temp"),
            "repeated-unary variant should report repeated-unary-update-arg-temp");
    const std::vector<int> baseline_codes = step_opcodes(baseline.steps);
    const std::vector<int> optimized_codes = step_opcodes(optimized.steps);
    const std::string baseline_c = baseline.registers.at("c");
    const std::string optimized_c = optimized.registers.at("c");

    const Observation before = observe(baseline_codes, {"В/О", "С/П"}, baseline.preloads, {baseline_c});
    const Observation after = observe(optimized_codes, {"В/О", "С/П"}, optimized.preloads,
                                    {optimized_c});
    require(after.display == before.display, "repeated unary canonicalization display should match");
    require(after.stopped == before.stopped, "repeated unary canonicalization stop state should match");
    require(after.registers.at(optimized_c) == before.registers.at(baseline_c),
            "repeated unary canonicalization registers should match");
  }

  const std::string impure_arg_source = R"mkpro(
program ImpureArgGuard {
  state {
    c: packed = 100
    seed: packed = 7
  }
  loop {
    c -= sqr(random(seed))
    c -= sqr(random(seed))
    halt(c)
  }
}
)mkpro";

  {
    const CompileResult canonicalized = compile_variant(impure_arg_source, false, true);
    require(!has_error_diagnostic(canonicalized),
            "impure-arg canonicalization should have no errors");
    require(!has_optimization(canonicalized, "repeated-unary-update-arg-temp"),
            "impure arg must not trigger repeated-unary-update-arg-temp");
    const bool has_scratch = std::any_of(canonicalized.registers.begin(),
                                         canonicalized.registers.end(), [](const auto& entry) {
                                           return entry.first.rfind("__mkpro_unary_arg_", 0) == 0;
                                         });
    require(!has_scratch, "impure-arg canonicalization should not allocate unary arg scratch");
  }

  const std::string multi_group_source = R"mkpro(
program MultiGroupUnaryEq {
  state {
    a: packed = 3
    b: packed = 5
    c: packed = 200
    d: packed = 50
  }
  loop {
    c -= sqr(a + 1)
    d -= abs(a - b)
    c -= sqr(b + 1)
    d -= abs(b - a)
    c = c + d
    halt(c)
  }
}
)mkpro";

  {
    const CompileResult baseline = compile_variant(multi_group_source, false, false);
    const CompileResult canonicalized = compile_variant(multi_group_source, false, true);
    require(!has_error_diagnostic(canonicalized),
            "multi-group canonicalized stack-resident should compile");
    require(has_optimization(canonicalized, "repeated-unary-update-arg-temp"),
            "multi-group canonicalization should report repeated-unary-update-arg-temp");

    std::size_t scratch_count = 0;
    for (const auto& [name, _value] : canonicalized.registers) {
      if (name.rfind("__mkpro_unary_arg_", 0) == 0)
        ++scratch_count;
    }
    require(scratch_count == 1, "multi-group canonicalization should use one unary arg scratch");

    const std::vector<int> baseline_codes = step_opcodes(baseline.steps);
    const std::vector<int> canonicalized_codes = step_opcodes(canonicalized.steps);
    const std::string baseline_c = baseline.registers.at("c");
    const std::string canonicalized_c = canonicalized.registers.at("c");
    const Observation before = observe(baseline_codes, {"В/О", "С/П"}, baseline.preloads, {baseline_c});
    const Observation after =
        observe(canonicalized_codes, {"В/О", "С/П"}, canonicalized.preloads, {canonicalized_c});
    require(after.stopped == before.stopped, "multi-group canonicalization stop state should match");
    require(after.display == before.display,
            "multi-group canonicalization display should match baseline");
    require(after.registers.at(canonicalized_c) == before.registers.at(baseline_c),
            "multi-group canonicalization register value should match");
  }

  const std::filesystem::path root = std::filesystem::current_path();
  const std::array<const char*, 4> pending_programs = {
      "examples/tic-tac-toe.mkpro",
      "examples/cave-treasure.mkpro",
      "examples/giants-country.mkpro",
      "examples/pending-optimizer/tic-tac-toe-4x4.mkpro",
  };
  for (const char* file : pending_programs) {
    CompileOptions options;
    options.stack_resident_temps = true;
    options.analysis = true;
    options.budget = 999999;
    const CompileResult result = compile_source(read_file(root / file), options);
    require(result.implemented, "stack-resident example should implement " + std::string(file));
    require(!has_error_diagnostic(result), "stack-resident example should not emit errors: " +
                                             std::string(file));
    require(!result.steps.empty(), "stack-resident example should emit steps: " + std::string(file));
  }
}

} // namespace mkpro::tests
