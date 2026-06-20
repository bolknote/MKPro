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

bool has_step_comment(const CompileResult& result, const std::string& comment) {
  return std::any_of(result.steps.begin(), result.steps.end(),
                     [&](const ResolvedStep& step) { return step.comment == comment; });
}

} // namespace

void residual_elseif_matches_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program ResidualElseIfProbe {
  state {
    pos: packed = 2
    food: counter 0..99 = 0
    dynamite: counter 0..9 = 0
    treasure: counter 0..9 = 0
  }
  loop {
    if int(pos) == 1 {
      food += 9
      halt(food)
    }
    else {
      if int(pos) == 2 {
        dynamite += 4
        halt(dynamite)
      }
      else {
        treasure++
        halt(treasure)
      }
    }
  }
}
)mkpro");

  require(result.implemented, "residual else-if probe should compile");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "residual else-if probe should not report errors: " + diagnostic.message);
  }
  require(has_optimization(result, "residual-elseif-compare"),
          "residual else-if probe should report residual-elseif-compare");
  require(has_step_comment(result, "residual else-if compare"),
          "residual else-if probe should emit residual else-if compare step");
}

} // namespace mkpro::tests
