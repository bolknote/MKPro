#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace mkpro::tests {

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot read fixture: " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

bool has_optimization(const CompileResult& result, std::string_view name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

int optimization_count(const CompileResult& result, const std::string& name) {
  return static_cast<int>(std::count_if(result.optimizations.begin(), result.optimizations.end(),
                                       [&](const OptimizationReport& item) { return item.name == name; }));
}

} // namespace

void show_optimization_strategies_match_typescript_contract() {
  // Traceability: tests/compiler/show-optimization.test.ts
  CompileOptions analysis_options;
  analysis_options.analysis = true;
  analysis_options.budget = 999;

  {
    const CompileResult result = compile_source(R"mkpro(
program CurrentXMiddleDisplayField {
  state {
    a: counter 0..9 = 1
    b: counter 0..9 = 2
    c: counter 0..9 = 3
  }

  loop {
    b = 7
    show(a, b, c)
  }
}
)mkpro",
                                              analysis_options);

    require(result.implemented, "current-x middle reuse should compile");
    require(result.diagnostics.empty(), "current-x middle display should not report diagnostics");
    require(has_optimization(result, "display-current-x-middle-reuse"),
            "current-x middle reuse should report the TS strategy name");
  }

  {
    const CompileResult result = compile_source(R"mkpro(
program DynamicLineReportBranch {
  state {
    target: counter 0..9 = 0
    line: counter 0..9 = 4
  }

  loop {
    target = random(0, 10)
    if target == 0 {
      halt(0)
    }
    else {
      halt("8.-0", line)
    }
  }
}
)mkpro",
                                               analysis_options);
    require(result.implemented, "dynamic branch terminal select should compile");
    require(result.diagnostics.empty(), "dynamic branch terminal select should not report diagnostics");
    require(!has_optimization(result, "arithmetic-if-terminal-select"),
            "dynamic terminal branch should not use arithmetic terminal-select");
    require(has_optimization(result, "screen-dynamic-line-report-lowering"),
            "dynamic terminal line report should use dynamic line report lowering");
  }

  {
    const CompileResult result = compile_source(R"mkpro(
program FormattedCoordReportRun {
  field: board(0..9, 0..9)

  state {
    cell: coord(field) = 58
    foxes: coord_list(field, 1) = 0
    bearing: counter 0..9 = 0
  }

  loop {
    bearing = line_count(foxes, cell)
    show("--", cell:02, "--", bearing)
  }
}
)mkpro",
                                              analysis_options);

    require(result.implemented, "formatted coord report lowering should compile");
    require(result.diagnostics.empty(), "formatted coord report lowering should not report diagnostics");
    require(has_optimization(result, "formatted-coord-report-lowering"),
            "formatted coord report should use the specialized lowering");
    require(has_optimization(result, "coord-list-line-count-formatted-report-body"),
            "formatted coord report should fuse with line_count body");
    require(has_optimization(result, "formatted-coord-report-packed-body"),
            "formatted coord report should reuse packed coord report body");
  }

  {
    const CompileResult result = compile_source(R"mkpro(
program UnverifiedCoordReport {
  field: board(0..9, 0..9)

  state {
    cell: coord(field) = 58
    foxes: coord_list(field, 1) = 0
    bearing: counter 0..99 = 0
  }

  loop {
    bearing = line_count(foxes, cell)
    show("--", cell:02, "--", bearing:02)
  }
}
)mkpro",
                                              analysis_options);

    require(result.implemented, "unverified formatted coord report case should compile");
    require(result.diagnostics.empty(),
            "unverified formatted coord report case should not report diagnostics");
    require(!has_optimization(result, "formatted-coord-report-lowering"),
            "unverified formatted coord report layout should not apply video-mask lowering");
  }

  {
    const CompileResult result = compile_source(R"mkpro(
program FusedFoxScan {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    foxes: coord_list(field, 2) = 0
    bearing: counter 0..9 = 0
  }

  loop {
    cell = read()
    if cell in foxes {
      show("-20")
    }
    bearing = line_count(foxes, cell)
    show("--", cell:02, "--", bearing)
  }
}
)mkpro",
                                              analysis_options);

    require(result.implemented, "fused coord_list and line_count case should compile");
    require(result.diagnostics.empty(), "fused coord_list/line_count case should not report diagnostics");
    require(
        has_optimization(result, "coord-list-fused-formatted-report-body"),
        "coord_list should fuse the formatted report body with membership scan");
    require(
        has_optimization(result, "formatted-coord-report-packed-body"),
        "fused coord_list report should preserve packed report body optimization");
    require(
        has_optimization(result, "coord-list-fused-hit-line-count") ||
            has_optimization(result, "coord-list-scaled-fused-hit-line-count"),
        "coord_list should fuse hit check and line_count scan");
    require(!has_optimization(result, "coord-list-indirect-membership"),
            "fused coord_list scan should not use indirect membership");
  }

  {
    CompileOptions inline_floor_options = analysis_options;
    inline_floor_options.inline_floor_packed_row_expressions = true;

    const CompileResult result = compile_source(R"mkpro(
program IndexedFloorPackedRowRegisterRescue {
  state {
    floor: counter 1..9 = 2
    rows: packed[1..9] = bit_not(5 / 9)
    a: packed = 1
    b: packed = 2
    c: packed = 3
    d: packed = 4
    e: packed = 5
  }

  loop {
    a = a + 1
    b = b + a
    c = c + b
    d = d + c
    e = e + d
    show(floor, ".", rows[floor])
    halt(a + b + c + d + e)
  }
}
)mkpro",
                                               inline_floor_options);

    require(result.implemented, "inline floor packed-row expression fallback should compile");
    require(result.diagnostics.empty(),
            "inline floor packed-row expression fallback should not report diagnostics");
    require(has_optimization(result, "inline-floor-packed-row-expression"),
            "register-pressure fallback should choose inline floor packed-row expression");
    require(!has_optimization(result, "display-expression-materialization"),
            "inline floor packed-row expression fallback should avoid materialization");
  }

  {
    const CompileResult result = compile_source(R"mkpro(
program EmptyLiteralScreen {
  loop {
    show("")
    show()
    show()
  }
}
)mkpro",
                                              analysis_options);

    require(result.implemented, "empty literal screen sequence should compile");
    require(result.diagnostics.empty(), "empty literal screen sequence should not report diagnostics");
    require(optimization_count(result, "screen-empty-literal-lowering") == 3,
            "empty literal screen sequence should apply one empty-literal optimization per show");
  }
}

} // namespace mkpro::tests
