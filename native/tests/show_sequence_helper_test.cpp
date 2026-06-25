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

int count_steps_with_opcode_and_comment(const CompileResult& result, int opcode,
                                        const std::string& comment) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [&](const ResolvedStep& step) {
        return step.opcode == opcode && step.comment.has_value() && *step.comment == comment;
      }));
}

int count_steps_with_comment(const CompileResult& result, const std::string& comment) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [&](const ResolvedStep& step) {
        return step.comment.has_value() && step.comment->starts_with(comment);
      }));
}

} // namespace

void show_sequence_helpers_match_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program RepeatedChallengePrompt {
  state {
    warning_value: packed = 7
    memory_value: packed = 3
    answer: packed = 0
    selector: counter 0..9 = 0
  }

  loop {
    show(warning_value)
    show(memory_value)
    answer = read()
    show(warning_value)
    show(memory_value)
    answer = read()
    show(warning_value)
    show(memory_value)
    answer = read()
    show(warning_value)
    show(memory_value)
    answer = read()
    show(warning_value)
    show(memory_value)
    answer = read()
    show(warning_value)
    show(memory_value)
    answer = read()
    halt(answer)
  }
}
)mkpro");

  require(result.implemented, "native compiler should lower repeated show-show-read sequences");
  require(!has_error_diagnostic(result),
          "show-show-read compile should not report error diagnostics");
  require(count_optimization(result, "show-sequence-helper") == 1,
          "repeated show-show-read sequences should emit one shared helper body");
  require(count_optimization(result, "show-sequence-helper-call") == 6,
          "each repeated show-show-read site should call the shared helper");
  require(count_steps_with_comment(result, "show sequence helper") == 6,
          "each repeated show-show-read site should emit a helper call");
  require(count_steps_with_opcode_and_comment(result, 0x52, "show sequence return") == 1,
          "shared show sequence helper should return once");
}

} // namespace mkpro::tests
