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

int count_steps_with_comment(const CompileResult& result, const std::string& comment) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [&](const ResolvedStep& step) {
        return step.comment.has_value() && *step.comment == comment;
      }));
}

int count_steps_with_opcode_and_comment(const CompileResult& result, int opcode,
                                        const std::string& comment) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [&](const ResolvedStep& step) {
        return step.opcode == opcode && step.comment.has_value() && *step.comment == comment;
      }));
}

} // namespace

void random_cell_helpers_match_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program RandomLenReuse {
  grid: board(1..20, 1..1)

  state {
    a: coord(grid) = 1
    b: coord(grid) = 1
    c: coord(grid) = 1
  }

  loop {
    a = random(grid)
    b = random(grid)
    c = random(grid)
    halt(a + b + c)
  }
}
)mkpro");

  require(result.implemented, "native compiler should lower repeated random(board) calls");
  require(result.diagnostics.empty(),
          "repeated random(board) compile should not report diagnostics");
  require(has_optimization(result, "random-cell-helper"),
          "repeated random(board) should emit the TS random cell helper body");
  require(count_optimization(result, "random-cell-helper-call") == 3,
          "three random(board) calls should report three helper-call optimizations");
  require(!has_optimization(result, "repeated-assignment-value-reuse"),
          "random(board) assignments must not reuse one random draw as a repeated pure value");
  require(count_steps_with_opcode_and_comment(result, 0x53, "random cell random(grid)") == 3,
          "three random(board) calls should call the shared helper");
  require(count_steps_with_comment(result, "random()") == 1,
          "shared random cell helper should keep one random draw in the helper body");
}

} // namespace mkpro::tests
