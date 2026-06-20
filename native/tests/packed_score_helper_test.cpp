#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>

namespace mkpro::tests {

namespace {

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

int count_optimization(const CompileResult& result, const std::string& name) {
  return static_cast<int>(
      std::count_if(result.optimizations.begin(), result.optimizations.end(),
                    [&](const OptimizationReport& item) { return item.name == name; }));
}

int count_packed_score_helper_jumps(const CompileResult& result) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [](const ResolvedStep& step) {
        return step.opcode == 0x53 && step.comment == "packed_score helper";
      }));
}

} // namespace

void packed_score_helpers_match_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program PackedScoreStackHelper {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
    value: packed = 0
  }
  loop {
    value = packed_score(a, 1) + packed_score(b, 2) + packed_score(c, 3)
    halt(value)
  }
}
)mkpro");

  require(result.implemented, "native compiler should lower repeated packed_score calls");
  require(result.diagnostics.empty(),
          "repeated packed_score compile should not report diagnostics");
  require(has_optimization(result, "packed-score-stack-helper"),
          "repeated packed_score should report the TS helper body strategy name");
  require(count_optimization(result, "packed-score-stack-helper-call") == 3,
          "three packed_score calls should report three TS helper-call optimizations");
  require(count_packed_score_helper_jumps(result) == 3,
          "three packed_score calls should emit three helper calls");
}

} // namespace mkpro::tests
