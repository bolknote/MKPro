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

void inline_floor_packed_row_matches_typescript_contract() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.inline_floor_packed_row_expressions = true;

  const CompileResult result = compile_source(R"mkpro(
program InlineFloorPackedRow {
  state {
    floor: counter 1..9 = 2
  }

  loop {
    show(floor, ".", bit_not(5 / 9))
  }
}
)mkpro",
                                              options);

  require(result.implemented, "inline floor packed-row expression should compile");
  require(result.diagnostics.empty(),
          "inline floor packed-row expression should not report diagnostics: " +
              diagnostics_text(result));
  require(has_optimization(result, "floor-packed-row-expression-display"),
          "inline floor packed-row expression should reach the TS display lowering");
  require(!has_optimization(result, "display-expression-materialization"),
          "inline floor packed-row expression should skip display-expression materialization");
  require(!has_register_prefix(result, "__display_expr_"),
          "inline floor packed-row expression should not allocate display-expression scratch");
  require(result.listing.find("display packed row expression merge") != std::string::npos,
          "inline floor packed-row expression should splice the computed row");

  const CompileResult indexed = compile_source(R"mkpro(
program IndexedFloorPackedRow {
  state {
    floor: counter 1..9 = 2
    rows: packed[1..9] = bit_not(5 / 9)
  }

  loop {
    show(floor, ".", rows[floor])
  }
}
)mkpro",
                                               options);

  require(indexed.implemented, "indexed inline floor packed-row expression should compile");
  require(indexed.diagnostics.empty(),
          "indexed inline floor packed-row expression should not report diagnostics: " +
              diagnostics_text(indexed));
  require(has_optimization(indexed, "indexed-packed-row-table"),
          "indexed inline floor packed-row expression should report the TS row-table strategy");
  require(has_optimization(indexed, "floor-packed-row-expression-display"),
          "indexed inline floor packed-row expression should still use packed-row splicing");
  require(
      !has_optimization(indexed, "display-expression-materialization"),
      "indexed inline floor packed-row expression should skip display-expression materialization");
  require(indexed.listing.find("indexed recall rows") != std::string::npos,
          "indexed inline floor packed-row expression should read the row through indexed memory");
}

} // namespace mkpro::tests
