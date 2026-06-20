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

} // namespace

void counted_loop_unroll_matches_typescript_contract() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.unroll_counted_loops = true;

  const CompileResult result = compile_source(R"mkpro(
program CountedLoopUnroll {
  state {
    i: counter 0..9 = 0
    total: counter 0..99 = 0
  }

  loop {
    total = 0
    i = 0
    while i < 3 {
      i++
      total += i
    }
    halt(total)
  }
}
)mkpro",
                                              options);

  require(result.implemented, "counted loop unroll should compile");
  require(result.diagnostics.empty(), "counted loop unroll should not report diagnostics");
  require(has_optimization(result, "counted-loop-unroll"),
          "counted loop unroll should report the TS optimization");
  require(result.listing.find("while loop back") == std::string::npos,
          "unrolled counted loop should not emit the generic while back edge");
}

} // namespace mkpro::tests
