#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string trim_ascii_text(std::string value) {
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

bool has_proof(const CompileResult& result, const std::string& id) {
  return std::any_of(result.proofs.begin(), result.proofs.end(), [&](const ProofReport& entry) {
    return entry.id == id && entry.status == "proved";
  });
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
  const emulator::RunResult run = calc.run_until_stable(5000, 6);
  require(run.stopped, context + " should halt");
  return trim_ascii_text(calc.display_text());
}

// Two straight-line walks identical except for the leaf they call: the exact
// shape the callee-hole skeleton extraction targets. Register recalls (not
// digit literals) feed the walk so the regions stay X2-boundary clean.
constexpr const char* kCalleeHoleSource = R"mkpro(
program CalleeHoleProbe {
  state {
    total: packed = 0
    line: packed = 0
    q1: packed = 3
    q2: packed = 5
    q3: packed = 7
    q4: packed = 9
  }

  fn leaf_add() {
    total += line
  }

  fn leaf_double_sub() {
    total -= line * 2
  }

  loop {
    line = q1
    leaf_add()
    line = q2
    leaf_add()
    line = q3
    leaf_add()
    line = q4
    leaf_add()
    line = q1
    leaf_double_sub()
    line = q2
    leaf_double_sub()
    line = q3
    leaf_double_sub()
    line = q4
    leaf_double_sub()
    halt(total)
  }
}
)mkpro";

} // namespace

void callee_hole_helper_matches_direct_call_semantics() {
  CompileOptions options;
  options.budget = 999999;
  options.disable_candidate_search = true;
  options.hoist_procs = true;

  CompileOptions hole_options = options;
  hole_options.callee_hole_straight_line_helper = true;

  const CompileResult baseline = compile_source(kCalleeHoleSource, options);
  require(!has_optimization(baseline, "callee-hole-straight-line-helper"),
          "pass should stay off without its option flag");

  const CompileResult hole = compile_source(kCalleeHoleSource, hole_options);
  require(has_optimization(hole, "callee-hole-straight-line-helper"),
          "walks differing only in their leaf call should merge into a skeleton");
  require(has_proof(hole, "callee-hole-indirect-call-targets"),
          "callee-hole dispatch should discharge its leaf-address proof");
  require(hole.steps.size() < baseline.steps.size(),
          "callee-hole skeleton should shrink the program: hole=" +
              std::to_string(hole.steps.size()) +
              " baseline=" + std::to_string(baseline.steps.size()));

  // total = (3+5+7+9) - 2*(3+5+7+9) = -24 per iteration.
  const std::string baseline_display = run_display(baseline, "direct-call baseline");
  const std::string hole_display = run_display(hole, "callee-hole variant");
  require(baseline_display.find("24") != std::string::npos &&
              baseline_display.front() == '-',
          "direct-call baseline should halt with -24, got " + baseline_display);
  require(hole_display == baseline_display,
          "callee-hole variant should match the direct-call baseline: hole=" + hole_display +
              " baseline=" + baseline_display);
}

} // namespace mkpro::tests
