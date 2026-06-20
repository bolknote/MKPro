#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::vector<std::string> max_min_warnings(const std::string& source) {
  CompileOptions options;
  options.budget = 999;
  const CompileResult result = compile_source(source, options);
  std::vector<std::string> warnings;
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "max/min lint fixture should compile without errors: " + diagnostic.message);
    if (diagnostic.severity == DiagnosticSeverity::Warning &&
        diagnostic.message.find("К max") != std::string::npos)
      warnings.push_back(diagnostic.message);
  }
  return warnings;
}

} // namespace

void maxmin_zero_lint_matches_typescript_contract() {
  const std::string max_source = R"mkpro(
program Test {
  state {
    score: counter 0..9 = 0
    best: counter 0..99 = 1
  }

  loop {
    best = max(best, score)
    show(best)
  }
}
)mkpro";
  const std::vector<std::string> max_warnings = max_min_warnings(max_source);
  require(!max_warnings.empty(), "max() should warn when an operand can be exactly 0");
  require(max_warnings.front().find("safe_max()") != std::string::npos,
          "max() zero warning should recommend safe_max()");

  const std::string min_source = R"mkpro(
program Test {
  state {
    score: counter 0..9 = 5
    worst: counter 0..99 = 99
  }

  loop {
    worst = min(worst, score)
    show(worst)
  }
}
)mkpro";
  const std::vector<std::string> min_warnings = max_min_warnings(min_source);
  require(!min_warnings.empty(), "min() should warn the same way");
  require(min_warnings.front().find("safe_min()") != std::string::npos,
          "min() zero warning should recommend safe_min()");

  const std::string literal_zero_source = R"mkpro(
program Test {
  state {
    score: counter 1..9 = 1
  }

  loop {
    score = max(score, 0)
    show(score)
  }
}
)mkpro";
  require(max_min_warnings(literal_zero_source).empty(),
          "literal 0 operands should remain a silent intentional idiom");

  const std::string nonzero_range_source = R"mkpro(
program Test {
  state {
    a: counter 1..9 = 1
    b: counter 2..8 = 2
  }

  loop {
    a = max(a, b)
    show(a)
  }
}
)mkpro";
  require(max_min_warnings(nonzero_range_source).empty(),
          "ranges that exclude 0 should not warn");

  const std::string safe_max_source = R"mkpro(
program Test {
  state {
    score: counter 0..9 = 0
    best: counter 0..99 = 1
  }

  loop {
    best = safe_max(best, score)
    show(best)
  }
}
)mkpro";
  require(max_min_warnings(safe_max_source).empty(),
          "safe_max()/safe_min() calls should not trigger the К max lint");
}

} // namespace mkpro::tests
