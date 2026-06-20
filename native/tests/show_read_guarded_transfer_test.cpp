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

std::string diagnostics_text(const CompileResult& result) {
  std::string text;
  for (const Diagnostic& diagnostic : result.diagnostics) {
    if (!text.empty())
      text += "; ";
    text += diagnostic.code + ": " + diagnostic.message;
  }
  return text;
}

} // namespace

void show_read_guarded_transfer_matches_typescript_contract() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.disable_candidate_search = true;
  options.domain_error_guards = true;
  options.show_read_guarded_transfer = true;

  const CompileResult result = compile_source(R"mkpro(
program GuardedReadTransfer {
  state {
    food: counter -9..9 = 3
    bag: counter -9..9 = 0
    pos: counter 0..9 = 1
    amount: counter 0..9 = 0
  }

  loop {
    show(pos)
    amount = int(read())
    food -= amount
    if food < 0 {
      halt("ЕГГОГ")
    }
    else {
      bag += amount
      if bag < 0 {
        halt("ЕГГОГ")
      }
      halt(bag)
    }
  }
}
)mkpro",
                                              options);

  require(result.implemented, "show-read guarded transfer should compile");
  require(result.diagnostics.empty(),
          "show-read guarded transfer should not report diagnostics: " + diagnostics_text(result));
  require(has_optimization(result, "show-read-guarded-transfer"),
          "show-read guarded transfer should report the TS optimization");
  require(!has_step_comment(result, "read()"),
          "show-read guarded transfer should consume the show stop instead of emitting read()");
  require(has_step_comment(result, "decrement input"),
          "show-read guarded transfer should decrement with the stack-resident input");
  require(has_step_comment(result, "restore decremented input"),
          "show-read guarded transfer should restore the input before incrementing");
  require(has_step_comment(result, "increment input"),
          "show-read guarded transfer should increment with the restored input");
  require(has_step_comment(result, "decrement/increment domain-error guard trap"),
          "show-read guarded transfer should use the shared domain-error trap tail");
}

} // namespace mkpro::tests
