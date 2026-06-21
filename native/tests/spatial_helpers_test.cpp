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

  const CompileResult line_progression = compile_source(R"mkpro(
program SpatialLineProgressionHelperReports {
  field: board(1..4, 1..4)

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
  require_clean_compile(line_progression, "ordinary spatial line-progression helper");
  require(has_optimization(line_progression, "spatial-line-progression-helper-call"),
          "small-board line_count should call the TS spatial line-progression helper");
  require(has_optimization(line_progression, "spatial-line-progression-helper"),
          "small-board line_count should emit the TS spatial line-progression helper body");

  const CompileResult shared_line_count = compile_source(R"mkpro(
program SpatialLineCountSharedHelperReports {
  field: board(1..4, 1..4)

  state {
    cell: coord(field)
    a: cells(field) = 0
    b: cells(field) = 0
    left: counter 0..9 = 0
    right: counter 0..9 = 0
  }

  loop {
    cell = read()
    left = line_count(a, cell)
    right = line_count(b, cell)
    halt(left + right)
  }
}
)mkpro",
                                                         options);
  require_clean_compile(shared_line_count, "shared spatial line_count helper");
  require(has_optimization(shared_line_count, "spatial-line-count-helper-call"),
          "repeated line_count groups should call the TS shared line_count helper");
  require(has_optimization(shared_line_count, "spatial-line-count-helper"),
          "repeated line_count groups should emit the TS shared line_count helper body");

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
  require(has_optimization(segmented_sum, "segmented-bitplane-line-count-helper"),
          "segmented line_count should report the TS segmented bitplane line-count helper");
  require(has_optimization(segmented_sum, "segmented-bitplane-sum-helper-inline-hit"),
          "segmented sum-loop helper should report inline segmented bitplane hits");
  require(!has_optimization(segmented_sum, "spatial-hit-inline"),
          "segmented sum-loop helper should not report ordinary spatial hit inlining");

  const CompileResult expression_mask_count = compile_source(R"mkpro(
program SpatialCountHitHelperFallback {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    a: cells(field) = 0
    b: cells(field) = 0
    answer: counter 0..9 = 0
  }

  loop {
    cell = read()
    answer = neighbor_count(bit_or(a, b), cell)
    halt(answer)
  }
}
)mkpro",
                                                             options);
  require_clean_compile(expression_mask_count, "spatial count hit-helper fallback");
  require(has_optimization(expression_mask_count, "spatial-count-hit-helper"),
          "expression-mask neighbor_count should report the TS hit-helper fallback");
  require(expression_mask_count.listing.find("spatial hit test") != std::string::npos ||
              expression_mask_count.listing.find("bit membership to count") != std::string::npos,
          "expression-mask neighbor_count should lower through spatial hit membership tests");
}

} // namespace mkpro::tests
