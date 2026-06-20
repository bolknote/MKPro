#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>

namespace mkpro::tests {

namespace {

const std::string kProgramWithImplicitAssign = R"mkpro(
program Test {
  state {
    score: counter 0..9 = 0
  }

  loop {
    temp = score + 1
    score = temp
    show(score)
  }
}
)mkpro";

const std::string kProgramWithSilentRead = R"mkpro(
program Test {
  state {
    score: counter 0..9 = 0
  }

  loop {
    show(score)
    key = read()
    score = key
  }
}
)mkpro";

const std::string kFullyDeclaredProgram = R"mkpro(
program Test {
  state {
    score: counter 0..9 = 0
    temp: packed = 0
    key: packed = 0
  }

  loop {
    temp = score + 1
    score = temp
    show(score)
    key = read()
    score = key
  }
}
)mkpro";

bool has_diagnostic(const CompileResult& result, DiagnosticSeverity severity,
                    const std::string& text) {
  return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [&](const Diagnostic& diagnostic) {
                       return diagnostic.severity == severity &&
                              diagnostic.message.find(text) != std::string::npos;
                     });
}

int diagnostic_count(const CompileResult& result, DiagnosticSeverity severity) {
  return static_cast<int>(std::count_if(result.diagnostics.begin(), result.diagnostics.end(),
                                        [&](const Diagnostic& diagnostic) {
                                          return diagnostic.severity == severity;
                                        }));
}

} // namespace

void strict_allocation_matches_typescript_contract() {
  const CompileResult implicit_assign = compile_source(kProgramWithImplicitAssign);
  require(has_diagnostic(implicit_assign, DiagnosticSeverity::Warning,
                         "Implicit allocation for undeclared variable 'temp'"),
          "default allocation should warn for an undeclared assignment target");
  require(has_diagnostic(implicit_assign, DiagnosticSeverity::Warning, "state { ... }"),
          "implicit allocation warning should mention state { ... }");

  const CompileResult silent_read = compile_source(kProgramWithSilentRead);
  require(diagnostic_count(silent_read, DiagnosticSeverity::Warning) == 0,
          "read() scratch targets should stay silent by default");
  require(diagnostic_count(silent_read, DiagnosticSeverity::Error) == 0,
          "read() scratch targets should not error by default");

  CompileOptions strict_options;
  strict_options.budget = 999;
  strict_options.strict = true;

  const CompileResult strict_assign = compile_source(kProgramWithImplicitAssign, strict_options);
  require(!strict_assign.implemented, "strict allocation should reject undeclared assignments");
  require(has_diagnostic(strict_assign, DiagnosticSeverity::Error,
                         "Undeclared variable 'temp' (strict allocation)"),
          "strict allocation should report the undeclared assignment target");

  const CompileResult strict_read = compile_source(kProgramWithSilentRead, strict_options);
  require(!strict_read.implemented, "strict allocation should reject silent read targets");
  require(has_diagnostic(strict_read, DiagnosticSeverity::Error,
                         "Undeclared variable 'key' (strict allocation)"),
          "strict allocation should report the silent read target");

  const CompileResult declared = compile_source(kFullyDeclaredProgram, strict_options);
  require(declared.implemented, "fully declared strict program should compile");
  require(diagnostic_count(declared, DiagnosticSeverity::Error) == 0,
          "fully declared strict program should not report errors");
  require(!has_diagnostic(declared, DiagnosticSeverity::Warning, "Implicit allocation") &&
              !has_diagnostic(declared, DiagnosticSeverity::Error, "strict allocation"),
          "fully declared strict program should not report allocation diagnostics");
}

} // namespace mkpro::tests
