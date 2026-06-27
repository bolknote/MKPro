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

int count_steps_with_opcode_and_comment_prefix(const CompileResult& result, int opcode,
                                               const std::string& prefix) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [&](const ResolvedStep& step) {
        return step.opcode == opcode && step.comment.has_value() &&
               step.comment->rfind(prefix, 0) == 0;
      }));
}

// Pin the shared-helper / direct-call (ПП) structure this suite verifies by
// suppressing the default-on aggressive post-layout indirect-flow repacking.
CompileOptions pinned_options() {
  CompileOptions options;
  options.disable_aggressive_post_layout = true;
  return options;
}

} // namespace

void expression_helpers_match_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program RepeatedExpression {
  state {
    pos: packed = 23
    map: packed = 123456789
    a: packed = 0
    b: packed = 0
    c: packed = 0
  }
  loop {
    a = digit_at(map, pos - int(pos / 10) * 10)
    b = digit_at(map, pos - int(pos / 10) * 10)
    c = digit_at(map, pos - int(pos / 10) * 10)
    halt(a + b + c)
  }
}
)mkpro",
                                              pinned_options());

  require(result.implemented, "native compiler should lower repeated pure expressions");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "repeated expression compile should not report errors: " + diagnostic.message);
  }
  require(has_optimization(result, "expression-helper"),
          "repeated pure expression should emit the TS expression helper body");
  require(count_optimization(result, "expression-helper-call") >= 1,
          "repeated pure expression should report helper-call optimizations");
  require(count_steps_with_opcode_and_comment_prefix(result, 0x53, "expr ") >= 1,
          "repeated pure expression should call the shared expression helper");
  require(count_steps_with_opcode_and_comment_prefix(result, 0x52, "expression helper return") == 1,
          "repeated pure expression should emit exactly one helper return");
}

} // namespace mkpro::tests
