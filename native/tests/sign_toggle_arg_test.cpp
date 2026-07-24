#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
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

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& entry) { return entry.name == name; });
}

std::string run_display(const CompileResult& result, const std::string& context) {
  require(result.implemented, context + " should compile");
  require(result.diagnostics.empty(), context + " should not report diagnostics");
  std::vector<int> codes;
  codes.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), context + " should load without diagnostics");
  for (const PreloadReport& preload : result.preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(3000, 6);
  require(run.stopped, context + " should halt");
  return trim_ascii(calc.display_text());
}

// Two invokes of a single-parameter rule with strictly alternating literal
// signs inside one linear block: the exact shape the sign-toggle pass targets.
constexpr const char* kToggleSource = R"mkpro(
program SignToggleProbe {
  state {
    total: packed = 0
    line: counter 0..9 = 3
  }

  fn apply_mark(mark_sign) {
    total += mark_sign * line
  }

  loop {
    apply_mark(-1)
    line = line + 2
    apply_mark(1)
    halt(total)
  }
}
)mkpro";

// Same rule invoked an odd number of times: repeated block executions would
// desynchronize the toggle, so the pass must leave the literals alone.
constexpr const char* kOddSource = R"mkpro(
program SignToggleOddProbe {
  state {
    total: packed = 0
    line: counter 0..9 = 3
  }

  fn apply_mark(mark_sign) {
    total += mark_sign * line
  }

  loop {
    apply_mark(-1)
    line = line + 2
    apply_mark(1)
    apply_mark(-1)
    halt(total)
  }
}
)mkpro";

} // namespace

void alternating_sign_toggle_arg_matches_literal_semantics() {
  CompileOptions options;
  options.budget = 999999;
  options.disable_candidate_search = true;

  CompileOptions toggle_options = options;
  toggle_options.alternating_sign_toggle_args = true;

  // The pass rewrites the eligible fixture and reports the optimization.
  const CompileResult toggled = compile_source(kToggleSource, toggle_options);
  require(has_optimization(toggled, "alternating-sign-toggle-arg"),
          "alternating literal args should be replaced by a sign toggle");

  // Baseline (literal arguments) and toggled variants must agree at runtime:
  // total = -3 + 5 = 2.
  const CompileResult baseline = compile_source(kToggleSource, options);
  require(!has_optimization(baseline, "alternating-sign-toggle-arg"),
          "pass should stay off without its option flag");
  const std::string baseline_display = run_display(baseline, "literal-arg baseline");
  const std::string toggled_display = run_display(toggled, "sign-toggle variant");
  require(!baseline_display.empty() && baseline_display.front() == '2',
          "literal-arg baseline should halt with 2, got " + baseline_display);
  require(toggled_display == baseline_display,
          "sign-toggle variant should match the literal baseline: toggled=" + toggled_display +
              " baseline=" + baseline_display);

  // An odd number of call sites cannot keep the toggle phase stable across
  // block re-entries, so the rewrite must not fire.
  const CompileResult odd = compile_source(kOddSource, toggle_options);
  require(odd.implemented, "odd-count fixture should compile");
  require(!has_optimization(odd, "alternating-sign-toggle-arg"),
          "odd invoke count should not be rewritten into a sign toggle");

}

} // namespace mkpro::tests
