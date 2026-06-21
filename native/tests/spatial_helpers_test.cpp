#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string_view>

namespace mkpro::tests {

namespace {

bool has_optimization(const CompileResult& result, std::string_view name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [name](const OptimizationReport& item) { return item.name == name; });
}

void require_clean_compile(const CompileResult& result, const std::string& name) {
  require(result.implemented, name + " should compile");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            name + " should not report errors: " + diagnostic.message);
  }
}

} // namespace

void spatial_helpers_match_typescript_contract() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;

  const CompileResult spatial_sum = compile_source(R"mkpro(
program SpatialSumLoopHelperReports {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    marks: cells(field) = 0
    answer: counter 0..9 = 0
  }

  loop {
    cell = read()
    answer = line_count(marks, cell)
    halt(answer)
  }
}
)mkpro",
                                                  options);
  require_clean_compile(spatial_sum, "ordinary spatial sum-loop helper");
  require(has_optimization(spatial_sum, "spatial-sum-loop-helper-call"),
          "ordinary line_count should report the TS spatial sum-loop helper call");
  require(has_optimization(spatial_sum, "spatial-sum-loop-helper"),
          "ordinary line_count should emit the TS spatial sum-loop helper");
  require(has_optimization(spatial_sum, "spatial-hit-inline"),
          "ordinary sum-loop helper should report inline spatial hit tests");

  CompileOptions segmented_options = options;
  segmented_options.segmented_bitplanes = true;
  const CompileResult segmented_sum = compile_source(R"mkpro(
program SegmentedSpatialSumLoopHelperReports {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    marks: cells(field) = 0
    answer: counter 0..9 = 0
  }

  loop {
    cell = read()
    answer = line_count(marks, cell)
    halt(answer)
  }
}
)mkpro",
                                                     segmented_options);
  require_clean_compile(segmented_sum, "segmented spatial sum-loop helper");
  require(has_optimization(segmented_sum, "spatial-sum-loop-helper-call"),
          "segmented line_count should report the TS spatial sum-loop helper call");
  require(has_optimization(segmented_sum, "spatial-sum-loop-helper"),
          "segmented line_count should emit the TS spatial sum-loop helper");
  require(has_optimization(segmented_sum, "segmented-bitplane-sum-helper-inline-hit"),
          "segmented sum-loop helper should report inline segmented bitplane hits");
  require(!has_optimization(segmented_sum, "spatial-hit-inline"),
          "segmented sum-loop helper should not report ordinary spatial hit inlining");
}

} // namespace mkpro::tests
