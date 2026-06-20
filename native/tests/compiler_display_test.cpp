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

  const CompileResult zero_tail = compile_source(R"mkpro(
program ZeroDigitTailScreen {
  loop {
    show("2Е")
  }
}
)mkpro");
  require(zero_tail.implemented, "native compiler should lower zero-digit tail display literals");
  require(zero_tail.diagnostics.empty(),
          "zero-digit tail display compile should not report diagnostics");
  require(has_optimization(zero_tail, "screen-zero-digit-tail-lowering"),
          "zero-digit tail display should report the TS strategy name");
  require(has_optimization(zero_tail, "screen-video-literal-lowering"),
          "zero-digit tail display should report the outer literal-screen lowering strategy");
  require(std::any_of(zero_tail.steps.begin(), zero_tail.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x54 &&
                               step.comment == "display zero-digit tail seed";
                      }),
          "zero-digit tail display should seed the 0C tail trick");
  require(std::any_of(zero_tail.steps.begin(), zero_tail.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x6c &&
                               step.comment == "display zero-digit tail hidden tail";
                      }),
          "zero-digit tail display should read the hidden tail from Rc");
  require(std::any_of(zero_tail.steps.begin(), zero_tail.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x50 && step.comment == "show literal";
                      }),
          "zero-digit tail display should emit the calculator stop");

  const CompileResult sign_digit = compile_source(R"mkpro(
program SignDigitLiteralScreen {
  loop {
    show("3Е0000021")
  }
}
)mkpro");
  require(sign_digit.implemented, "native compiler should lower sign-digit display literals");
  require(sign_digit.diagnostics.empty(),
          "sign-digit literal display compile should not report diagnostics");
  require(has_optimization(sign_digit, "screen-sign-digit-literal-lowering"),
          "sign-digit literal display should report the TS strategy name");
  require(has_optimization(sign_digit, "screen-video-literal-lowering"),
          "sign-digit literal display should report the outer literal-screen lowering strategy");
  require(std::any_of(sign_digit.steps.begin(), sign_digit.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x3a && step.comment == "display sign-digit E source";
                      }),
          "sign-digit literal display should build the E source");
  require(std::any_of(sign_digit.steps.begin(), sign_digit.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode >= 0xd0 && step.opcode <= 0xd6 &&
                               step.comment == "display sign-digit indirect normalize";
                      }),
          "sign-digit literal display should normalize through indirect memory");
  require(std::any_of(sign_digit.steps.begin(), sign_digit.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x50 && step.comment == "show literal";
                      }),
          "sign-digit literal display should emit the calculator stop");
}

} // namespace mkpro::tests
