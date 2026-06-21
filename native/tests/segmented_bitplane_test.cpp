#include "mkpro/compiler.hpp"
#include "mkpro/core/parser.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>

namespace mkpro::tests {

namespace {

constexpr std::string_view kSegmentedProgram = R"mkpro(
program SegmentedSmoke {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    marks: cells(field) = 0
    answer: counter 0..9 = 0
  }

  loop {
    cell = read()
    marks += cell
    if cell in marks {
      marks -= cell
      answer = 1
    }
    halt(answer)
  }
}
)mkpro";

constexpr std::string_view kSegmentedLineCountProgram = R"mkpro(
program SegmentedLineCount {
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
)mkpro";

constexpr std::string_view kSegmentedFoxHuntProgram = R"mkpro(
program SegmentedRandomSetup {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    foxes: cells(field) = random()
    remaining_foxes: counter 1..100 = stack.Y
    answer: counter 0..9 = 0
  }

  loop {
    cell = read()
    if cell in foxes {
      foxes -= cell
      remaining_foxes--
      answer = -remaining_foxes
    }
    else {
      answer = line_count(foxes, cell)
    }
    halt(answer)
  }
}
)mkpro";

CompileResult compile_segmented(std::string_view source) {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.segmented_bitplanes = true;
  return compile_source(std::string(source), options);
}

CompileResult compile_segmented_scan(std::string_view source) {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.segmented_bitplanes = true;
  options.segmented_line_count_scan = true;
  return compile_source(std::string(source), options);
}

bool contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

bool has_optimization(const CompileResult& result, std::string_view name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [name](const OptimizationReport& item) { return item.name == name; });
}

} // namespace

void segmented_bitplanes_match_typescript_contract() {
  const ProgramAst ast =
      parse_program(std::string(kSegmentedProgram), ParseOptions{.segmented_bitplanes = true});
  require(ast.v2.has_value(), "segmented source should parse as a V2 program");
  require(ast.v2->boards.size() == 1, "segmented test should declare one board");
  require(ast.v2->boards.at(0).width == 10 && ast.v2->boards.at(0).height == 10,
          "segmented bitplane board should stay zero-origin 10x10");

  const CompileResult result = compile_segmented(kSegmentedProgram);
  require(result.implemented, "native compiler should lower segmented bitplane cells");
  require(result.diagnostics.empty(), "segmented bitplane compile should not report diagnostics");

  require(result.registers.find("marks") == result.registers.end(),
          "logical segmented cells collection should not allocate one native register");
  require(result.registers.find("__seg_bitplane_index") != result.registers.end(),
          "segmented bitplane lowering should allocate the local index scratch");
  require(result.registers.at("__seg_bitplane_selector") == "7",
          "segmented bitplane selector should use R7 like the TS compiler");
  require(result.registers.at("__seg_bitplane_marks_0") == "0",
          "segmented bitplane plane 0 should use R0");
  require(result.registers.at("__seg_bitplane_marks_1") == "1",
          "segmented bitplane plane 1 should use R1");
  require(result.registers.at("__seg_bitplane_marks_2") == "b",
          "segmented bitplane plane 2 should use Rb");
  require(result.registers.at("__seg_bitplane_marks_3") == "e",
          "segmented bitplane plane 3 should use Re");

  require(contains(result.listing, "segmented bitplane hit"),
          "segmented membership should dispatch through bitplane hit lowering");
  require(contains(result.listing, "segmented bitplane store selected plane"),
          "segmented cells update should store through the selected plane");
  require(contains(result.listing, "segmented bitplane clear"),
          "segmented cells -= should clear through the selected plane");
  require(has_optimization(result, "segmented-bitplane-update-indirect"),
          "segmented cells update should use the indirect selected-plane update");
  require(has_optimization(result, "segmented-bitplane-hit-update-indirect"),
          "segmented membership followed by clear should fuse through the selected plane");
  require(!contains(result.listing, "bit_clear"),
          "segmented cells should not lower through generic bit_clear syntax");

  const CompileResult clear = compile_segmented(R"mkpro(
program SegmentedClear {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    marks: cells(field) = random()
    answer: counter 0..9 = 0
  }

  loop {
    cell = read()
    if cell in marks {
      marks -= cell
      answer = 1
    }
    halt(answer)
  }
}
)mkpro");
  require(clear.implemented, "native compiler should lower segmented hit-and-clear");
  require(clear.diagnostics.empty(), "segmented hit-and-clear should not report diagnostics");
  require(has_optimization(clear, "segmented-bitplane-hit-update-indirect"),
          "segmented hit-and-clear should fuse through one indirect selected bitplane");
  require(clear.registers.at("__seg_bitplane_selector") == "7",
          "segmented hit-and-clear should keep selector in R7");

  const CompileResult direct = compile_segmented(R"mkpro(
program SegmentedDirectDispatch {
  field: board(0..9, 0..9)

  state {
    reserved: packed[7..7] = 0
    cell: coord(field)
    marks: cells(field) = 0
    answer: counter 0..9 = 0
  }

  loop {
    cell = read()
    marks += cell
    if cell in marks {
      marks -= cell
      answer = 1
    }
    answer += reserved[7]
    halt(answer)
  }
}
)mkpro");
  require(direct.implemented, "native compiler should lower direct segmented bitplane dispatch");
  require(direct.diagnostics.empty(), "direct segmented bitplane dispatch should not diagnose");
  require(direct.registers.at("__seg_bitplane_selector") != "7",
          "reserved R7 should force direct four-plane segmented dispatch");
  require(has_optimization(direct, "segmented-bitplane-update"),
          "direct segmented cells update should report the TS four-plane update strategy");
  require(has_optimization(direct, "segmented-bitplane-hit-update"),
          "direct segmented hit-and-clear should report the TS four-plane hit/update strategy");
  require(!has_optimization(direct, "segmented-bitplane-update-indirect"),
          "direct segmented update should not use the indirect selected-plane strategy");
  require(!has_optimization(direct, "segmented-bitplane-hit-update-indirect"),
          "direct segmented hit-and-clear should not use the indirect selected-plane strategy");

  const CompileResult baseline = compile_segmented(kSegmentedLineCountProgram);
  const CompileResult scan = compile_segmented_scan(kSegmentedLineCountProgram);
  require(baseline.implemented, "baseline segmented line_count program should compile");
  require(scan.implemented, "segmented line_count scan variant should compile");
  require(scan.diagnostics.empty(), "segmented line_count scan should not report diagnostics");
  require(has_optimization(scan, "segmented-bitplane-line-count-scan"),
          "segmented line_count scan should report the TS optimization name");
  require(scan.steps.size() < baseline.steps.size(),
          "segmented line_count scan should be smaller than the baseline segmented variant");

  const CompileResult condition_helper = compile_segmented_scan(R"mkpro(
program SegmentedConditionHelper {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    foxes: cells(field) = random()
    hit_value: packed = -20
    bearing: counter 0..9 = 0
  }

  loop {
    cell = read()
    if cell in foxes {
      show(hit_value)
    }
    bearing = line_count(foxes, cell)
    halt(bearing)
  }
}
)mkpro");
  require(condition_helper.implemented,
          "segmented bitplane condition-helper program should compile");
  require(condition_helper.diagnostics.empty(),
          "segmented bitplane condition-helper program should not report diagnostics");
  require(has_optimization(condition_helper, "segmented-bitplane-condition-helper"),
          "segmented membership before line_count should report the TS condition helper");
  require(has_optimization(condition_helper, "segmented-bitplane-hit-helper-call"),
          "segmented membership before line_count should route through the shared hit helper");
  require(has_optimization(condition_helper, "segmented-bitplane-line-count-scan"),
          "segmented membership helper program should still use the line_count scan");
  require(!has_optimization(condition_helper, "spatial-hit-condition-helper"),
          "segmented membership should not report ordinary spatial-hit condition helper");

  const CompileResult fox_hunt = compile_segmented(kSegmentedFoxHuntProgram);
  require(fox_hunt.implemented,
          "native compiler should lower counted segmented random setup program");
  require(fox_hunt.diagnostics.empty(),
          "counted segmented random setup should not report diagnostics");
  require(fox_hunt.setup_program.has_value(),
          "counted segmented random setup should expose a generated setup program");
  require(has_optimization(fox_hunt, "setup-segmented-bitplane-random-unique"),
          "counted segmented random setup should report the TS setup optimization name");
  require(has_optimization(fox_hunt, "spatial-count-fl-loop"),
          "segmented fox hunt should keep the spatial-count loop optimization");
  require(contains(fox_hunt.setup_listing, "segmented bitplane random collision probe"),
          "segmented setup listing should include the collision probe");
  require(contains(fox_hunt.setup_listing, "segmented bitplane setup complete display"),
          "segmented setup listing should include the completion display");
  require(fox_hunt.registers.at("__seg_bitplane_selector") == "7",
          "counted segmented setup should keep selector in R7");
  require(fox_hunt.registers.at("__seg_bitplane_foxes_0") == "0",
          "counted segmented setup plane 0 should use R0");
  require(fox_hunt.registers.at("__seg_bitplane_foxes_1") == "1",
          "counted segmented setup plane 1 should use R1");
  require(fox_hunt.registers.at("__seg_bitplane_foxes_2") == "b",
          "counted segmented setup plane 2 should use Rb");
  require(fox_hunt.registers.at("__seg_bitplane_foxes_3") == "e",
          "counted segmented setup plane 3 should use Re");
}

} // namespace mkpro::tests
