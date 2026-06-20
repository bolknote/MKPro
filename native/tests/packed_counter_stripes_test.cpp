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

bool has_register_prefix(const CompileResult& result, const std::string& prefix) {
  return std::any_of(result.registers.begin(), result.registers.end(),
                     [&](const auto& entry) { return entry.first.rfind(prefix, 0) == 0; });
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

void packed_counter_stripes_match_typescript_contract() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;

  const CompileResult result = compile_source(R"mkpro(
program DotCounterDisplay {
  state {
    floor: counter 1..9 = 9
    room: counter 0..6 = 0
  }

  loop {
    if room == 0 {
      room++
    }
    if floor == 9 {
      floor--
    }
    show(floor, ".", room)
  }
}
)mkpro",
                                              options);

  require(result.implemented, "packed counter stripe display should compile");
  require(result.diagnostics.empty(),
          "packed counter stripe display should not report diagnostics: " +
              diagnostics_text(result));
  require(has_optimization(result, "packed-counter-stripes"),
          "literal-separated small counters should be packed into one decimal register");
  require(!has_optimization(result, "fl-unit-decrement"),
          "packed counter stripes should avoid the separate floor decrement strategy");
  require(result.registers.contains("__packed_counter_0"),
          "packed counter stripe should allocate __packed_counter_0");
  require(!result.registers.contains("floor"), "packed counter stripe should remove floor state");
  require(!result.registers.contains("room"), "packed counter stripe should remove room state");
  require(!has_register_prefix(result, "__display_expr_"),
          "packed counter stripe display should not allocate display-expression scratch");
  require(result.listing.find("__packed_counter_0") != std::string::npos,
          "listing should reference the packed decimal register");
}

} // namespace mkpro::tests
