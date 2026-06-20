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

void compiler_display_lowering_matches_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program FormattedCoordReport {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    bearing: counter 0..9 = 0
  }

  loop {
    cell = read()
    bearing = 3
    show("--", cell:02, "--", bearing)
  }
}
)mkpro");

  require(result.implemented, "native compiler should lower formatted coord reports");
  require(result.diagnostics.empty(), "formatted coord report should not report diagnostics");
  require(result.registers.find("__coord_list_dx") != result.registers.end(),
          "formatted coord report should reserve the TS mask scratch register");
  for (const auto& [name, unused_register] : result.registers) {
    (void)unused_register;
    require(name.find("__display_mask_body_") != 0,
            "formatted coord report should not allocate generic display-mask body scratch");
  }
  require(result.listing.find("setup display literal") != std::string::npos,
          "formatted coord report should preload its verified video mask");
  require(result.listing.find("display formatted mask") != std::string::npos,
          "formatted coord report should recall the preloaded mask");
  require(result.listing.find("show formatted coord report") != std::string::npos,
          "formatted coord report should use the specialized display stop");

  const CompileResult leading_zero = compile_source(R"mkpro(
program LeadingZeroLiteralDisplay {
  loop {
    show("020")
  }
}
)mkpro");
  require(leading_zero.implemented, "native compiler should lower leading-zero display literals");
  require(leading_zero.diagnostics.empty(),
          "leading-zero literal display compile should not report diagnostics");
  require(has_optimization(leading_zero, "screen-leading-zero-hex-lowering"),
          "leading-zero literal display should report the TS strategy name");
  require(has_optimization(leading_zero, "hex-mantissa-arithmetic"),
          "leading-zero literal display should report the shared hex mantissa capability");
  require(has_optimization(leading_zero, "screen-video-literal-lowering"),
          "leading-zero literal display should report the outer literal-screen lowering strategy");
  require(std::any_of(leading_zero.preloads.begin(), leading_zero.preloads.end(),
                      [](const PreloadReport& preload) { return preload.value == "-"; }),
          "leading-zero literal display should preload the TS source literal");
  require(leading_zero.setup_program.has_value(),
          "leading-zero literal display should expose the generated setup program");
  require(std::any_of(leading_zero.steps.begin(), leading_zero.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x34 &&
                               step.comment == "display leading-zero hex source";
                      }),
          "leading-zero literal display should extract the source mantissa integer part");
  require(std::any_of(leading_zero.steps.begin(), leading_zero.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x12 &&
                               step.comment == "display leading-zero hex product";
                      }),
          "leading-zero literal display should multiply by the planned factor");
  require(std::any_of(leading_zero.setup_program->steps.begin(),
                      leading_zero.setup_program->steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x3a && step.comment.has_value() &&
                               step.comment->find("first digit") != std::string::npos;
                      }),
          "leading-zero literal setup should build the source display literal first digit");
}

} // namespace mkpro::tests
