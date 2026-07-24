#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>

namespace mkpro::tests {

namespace {

bool has_error_diagnostic(const CompileResult& result) {
  return std::any_of(
      result.diagnostics.begin(), result.diagnostics.end(),
      [](const Diagnostic& item) { return item.severity == DiagnosticSeverity::Error; });
}

int count_optimization(const CompileResult& result, const std::string& name) {
  return static_cast<int>(
      std::count_if(result.optimizations.begin(), result.optimizations.end(),
                    [&](const OptimizationReport& item) { return item.name == name; }));
}

int count_steps_with_comment(const CompileResult& result, const std::string& comment) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [&](const ResolvedStep& step) {
        return step.comment.has_value() &&
               (step.comment->starts_with(comment + ";") || *step.comment == comment);
      }));
}

// Pin the shared-helper / direct-call (ПП) structure this suite verifies by
// suppressing the default-on aggressive post-layout indirect-flow repacking.
CompileOptions pinned_options() {
  CompileOptions options;
  options.disable_aggressive_post_layout = true;
  options.disable_return_stack_script = true;
  return options;
}

} // namespace

void packed_display_helpers_match_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program RepeatedPackedDisplay {
  state {
    selector: counter 0..9 = 0
    a: packed = 1
    b: packed = 2
    c: packed = 3
  }

  loop {
    match selector {
      1 => show(a, b, c)
      2 => show(a, b, c)
      3 => show(a, b, c)
      otherwise => halt(0)
    }
  }
}
)mkpro",
                                              pinned_options());

  require(result.implemented, "native compiler should lower repeated packed display bodies");
  require(!has_error_diagnostic(result),
          "repeated packed display compile should not report error diagnostics");
  require(count_optimization(result, "packed-display-helper") == 1,
          "repeated packed display bodies should emit one shared helper body");
  require(count_optimization(result, "packed-display-helper-call") == 3,
          "each repeated packed display site should call the shared helper");
  require(count_steps_with_comment(result, "show packed display helper") == 3,
          "each repeated packed display site should branch to the helper");
  const int helper_returns = count_steps_with_comment(result, "packed display return");
  require(helper_returns == 1 ||
              (helper_returns == 0 && count_optimization(result, "tail-call-lowering") > 0),
          "shared packed display helper should retain one return continuation or a proved "
          "tail continuation");
}

} // namespace mkpro::tests
