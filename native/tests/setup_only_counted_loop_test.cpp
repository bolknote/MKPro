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
  return std::any_of(result.steps.begin(), result.steps.end(), [&](const ResolvedStep& step) {
    return step.comment.has_value() && *step.comment == comment;
  });
}

bool has_fl_mnemonic(const CompileResult& result) {
  return std::any_of(result.steps.begin(), result.steps.end(),
                     [](const ResolvedStep& step) { return step.mnemonic.rfind("F L", 0) == 0; });
}

} // namespace

void setup_only_counted_loop_matches_typescript_contract() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.setup_only_counted_loop_init = true;

  const CompileResult result = compile_source(R"mkpro(
program SetupCountedLoop {
  state {
    time: counter 0..3 = 3
  }

  while time >= 1 {
    show(time)
    time--
  }
  halt(0)
}
)mkpro",
                                              options);

  require(result.implemented, "setup-only counted loop should compile");
  require(result.diagnostics.empty(), "setup-only counted loop should not report diagnostics");
  require(has_optimization(result, "setup-only-counted-loop-init"),
          "setup-only counted loop should report the TS optimization");
  require(!has_optimization(result, "state-init-counted-loop"),
          "setup-only counted loop should bypass the state-init normalization fallback");
  require(!has_step_comment(result, "set time"),
          "setup-only counted loop should use the setup initializer instead of storing time");
  require(has_fl_mnemonic(result), "setup-only counted loop should emit an F Lx loop opcode");
}

} // namespace mkpro::tests
