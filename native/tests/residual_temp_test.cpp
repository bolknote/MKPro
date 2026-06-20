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

bool has_early_set_step(const CompileResult& result) {
  return std::any_of(result.steps.begin(), result.steps.end(), [](const ResolvedStep& step) {
    return step.comment == "set step" && step.address < 10;
  });
}

} // namespace

void residual_temp_matches_typescript_contract() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999999;
  options.dead_source_residual_temp_reuse = true;

  const CompileResult result = compile_source(R"mkpro(
program ResidualTemp {
  state {
    command: packed = 0
    step: packed = 0
  }

  loop {
    command = read()
    step = command - 5

    unless step {
      halt(0)
    }
    else {
      step = int(1 / step)

      unless step {
        step = sign(command - 5)
        halt(step)
      }
      else {
        halt(step)
      }
    }
  }
}
)mkpro",
                                      options);

  require(result.implemented, "residual temp program should compile");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "residual temp program should not report errors: " + diagnostic.message);
  }
  require(has_optimization(result, "dead-source-residual-temp-reuse"),
          "residual temp program should report dead-source-residual-temp-reuse");
  require(has_step_comment(result, "set command"),
          "residual temp program should store the residual in command");
  require(!has_early_set_step(result),
          "residual temp program should not materialize step before address 10");
}

} // namespace mkpro::tests
