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

} // namespace

void small_set_condition_lowering_matches_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program SmallSetConditions {
  state {
    room: counter 0..20 = 10
    first: counter 0..20 = 9
    second: counter 0..20 = 12
    result: counter 0..9 = 0
  }

  loop {
    result = 0
    if near_any(room, 1, first, second) >= 0 {
      result = 1
    }
    if eq_any(room, first, second) == 0 {
      result = result + 2
    }
    halt(result)
  }
}
)mkpro");

  require(result.implemented, "native compiler should lower small-set conditions");
  require(result.diagnostics.empty(), "small-set condition compile should not report diagnostics");
  require(has_optimization(result, "small-set-condition-lowering"),
          "small-set conditions should report the TS condition strategy name");
  require(count_optimization(result, "small-set-condition-lowering") == 2,
          "near_any and eq_any conditions should both use direct small-set lowering");
  require(result.listing.find("near_any any hit") != std::string::npos,
          "near_any condition should branch directly on a candidate hit");
  require(result.listing.find("eq_any any hit") != std::string::npos,
          "eq_any condition should branch directly on a candidate hit");
  require(result.listing.find("max()") == std::string::npos,
          "near_any condition should not materialize the full max() expression");
}

} // namespace mkpro::tests
