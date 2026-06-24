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

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

int count_optimization(const CompileResult& result, const std::string& name) {
  return static_cast<int>(
      std::count_if(result.optimizations.begin(), result.optimizations.end(),
                    [&](const OptimizationReport& item) { return item.name == name; }));
}

int count_display_byte_helper_call_operands(const CompileResult& result) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [](const ResolvedStep& step) {
        return step.comment.has_value() && step.comment->starts_with("show __inline_show_") &&
               step.mnemonic != "С/П";
      }));
}

int count_display_byte_helper_returns(const CompileResult& result) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [](const ResolvedStep& step) {
        return step.comment.has_value() &&
               step.comment->starts_with("display __inline_show_") &&
               step.comment->ends_with(" return");
      }));
}

} // namespace

void display_byte_helpers_match_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program RepeatedLiteralSeparatedScoreboard {
  state {
    selector: counter 0..9 = 0
    die: counter 1..6 = 1
    score: counter 0..99 = 0
    total: counter 0..999 = 0
    roll: counter 0..99 = 0
  }

  loop {
    match selector {
      1 => show(die, ".-", score:02, "-", total:03, "-", roll:02)
      2 => show(die, ".-", score:02, "-", total:03, "-", roll:02)
      otherwise => halt(0)
    }
  }
}
)mkpro");

  require(result.implemented,
          "native compiler should lower repeated literal-separated display bodies");
  require(!has_error_diagnostic(result),
          "repeated literal-separated display compile should not report error diagnostics");
  require(count_optimization(result, "display-byte-helper") == 1,
          "repeated literal-separated displays should emit one shared display-byte helper body");
  require(count_optimization(result, "display-byte-helper-call") == 2,
          "each repeated literal-separated display site should call the display-byte helper");
  require(has_optimization(result, "display-byte-x2-lowering") ||
              has_optimization(result, "display-byte-mask-lowering") ||
              has_optimization(result, "display-byte-variable-mask-lowering"),
          "shared helper body should use an existing display-byte builder strategy");
  require(count_display_byte_helper_call_operands(result) == 2,
          "each repeated literal-separated display site should branch to the helper");
  require(count_display_byte_helper_returns(result) == 1,
          "shared display-byte helper should have one return continuation");
}

} // namespace mkpro::tests
