#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::vector<int> opcodes(const CompileResult& result) {
  std::vector<int> values;
  values.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    values.push_back(step.opcode);
  return values;
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

bool contains_opcode(const CompileResult& result, int opcode) {
  return std::any_of(result.steps.begin(), result.steps.end(),
                     [&](const ResolvedStep& step) { return step.opcode == opcode; });
}

} // namespace

void state_init_counted_loop_matches_typescript_contract() {
  const std::string state_init = R"mkpro(
program C {
  state {
    t: counter 0..25 = 25
    acc: counter 0..99 = 0
  }
  while t >= 1 {
    acc = acc + 1
    t--
  }
  halt(acc)
}
)mkpro";
  const std::string explicit_init = R"mkpro(
program C {
  state {
    t: counter 0..25
    acc: counter 0..99 = 0
  }
  t = 25
  while t >= 1 {
    acc = acc + 1
    t--
  }
  halt(acc)
}
)mkpro";
  const std::string reused_counter = R"mkpro(
program C {
  state {
    t: counter 0..25 = 25
    acc: counter 0..99 = 0
  }
  while t >= 1 {
    acc = acc + 1
    t--
  }
  halt(t)
}
)mkpro";

  const CompileResult state_result = compile_source(state_init);
  require(state_result.implemented, "state-init counted loop should compile");
  require(state_result.diagnostics.empty(),
          "state-init counted loop should not report diagnostics");

  const CompileResult explicit_result = compile_source(explicit_init);
  require(explicit_result.implemented, "explicit counted loop should compile");
  require(explicit_result.diagnostics.empty(), "explicit counted loop should not report diagnostics");

  require(opcodes(state_result) == opcodes(explicit_result),
          "state-init countdown should compile identically to explicit initialization");
  require(has_optimization(state_result, "state-init-counted-loop"),
          "state-init countdown should report the state-init-counted-loop optimization");
  require(contains_opcode(state_result, 0x58),
          "state-init countdown should recover the compact F Lx counted-loop opcode");
  require(!has_optimization(explicit_result, "state-init-counted-loop"),
          "explicit initialization should not report state-init-counted-loop");

  const CompileResult reused_result = compile_source(reused_counter);
  require(reused_result.implemented, "reused-counter countdown should compile");
  require(reused_result.diagnostics.empty(),
          "reused-counter countdown should not report diagnostics");
  require(!has_optimization(reused_result, "state-init-counted-loop"),
          "state-init countdown should not normalize when the counter is read after the loop");
}

} // namespace mkpro::tests
