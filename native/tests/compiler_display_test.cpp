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
  require(std::any_of(result.preloads.begin(), result.preloads.end(),
                      [](const PreloadReport& preload) { return preload.value == "8,-00--_"; }),
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
                        return step.opcode == 0x50 &&
                               step.comment.has_value() &&
                               step.comment->starts_with("show __inline_show_");
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
                        return step.opcode == 0x50 && step.comment.has_value() &&
                               step.comment->starts_with("show __inline_show_");
                      }),
          "sign-digit literal display should emit the calculator stop");

  CompileOptions display_preload_options;
  display_preload_options.general_constant_preloads = true;
  const CompileResult text_literal_preload = compile_source(R"mkpro(
program LiteralAlphabetScreen {
  loop {
    show("---")
    show("8-LСГ90")
    show("700-----8")
  }
}
)mkpro",
                                                            display_preload_options);
  require(text_literal_preload.implemented,
          "native compiler should lower preloaded game-style text literals");
  require(text_literal_preload.diagnostics.empty(),
          "preloaded text literal display compile should not report diagnostics");
  require(has_optimization(text_literal_preload, "screen-text-literal-preload"),
          "preloaded text literal display should report the TS strategy name");
  require(has_optimization(text_literal_preload, "setup-display-literal-minus-source-reuse"),
          "setup display literal preload should report minus-source reuse");
  require(has_optimization(text_literal_preload, "setup-display-literal-first-digit-reuse"),
          "setup display literal preload should report first-digit reuse");
  require(text_literal_preload.setup_program.has_value(),
          "preloaded text literal display should expose a setup program");
  require(std::any_of(
              text_literal_preload.steps.begin(), text_literal_preload.steps.end(),
              [](const ResolvedStep& step) {
                return step.comment.has_value() &&
                       step.comment->starts_with("display __inline_show_") &&
                       step.comment->ends_with(" literal");
              }),
          "preloaded text literal display should recall the prepared literal");
  require(std::any_of(text_literal_preload.setup_program->steps.begin(),
                      text_literal_preload.setup_program->steps.end(),
                      [](const ResolvedStep& step) {
                        return step.comment.has_value() &&
                               step.comment->find("first digit reuse") != std::string::npos;
                      }),
          "setup display literal preload should use first-digit reuse instructions");

  const CompileResult variable_mask = compile_source(R"mkpro(
program VariableLeadingSpaceLow {
  state {
    room: counter 1..20 = 1
    arrows: counter 0..5 = 5
    clue: counter 0..9 = 3
  }

  loop {
    show(room, " ", arrows, " ", clue)
  }
}
)mkpro");
  require(variable_mask.implemented, "native compiler should lower variable-leading display masks");
  require(variable_mask.diagnostics.empty(),
          "variable-leading display mask compile should not report diagnostics");
  require(has_optimization(variable_mask, "display-byte-variable-mask-lowering"),
          "variable-leading display mask should report the TS strategy name");
  bool saw_low_mask = false;
  bool saw_high_mask = false;
  for (const PreloadReport& preload : variable_mask.preloads) {
    saw_low_mask = saw_low_mask || preload.value == "8_0_0";
    saw_high_mask = saw_high_mask || preload.value == "80_0_0";
  }
  require(saw_low_mask, "variable-leading display should preload the low-width mask");
  require(saw_high_mask, "variable-leading display should preload the high-width mask");
  require(std::any_of(
              variable_mask.steps.begin(), variable_mask.steps.end(),
              [](const ResolvedStep& step) { return step.comment == "display mask high leader"; }),
          "variable-leading display should emit the high-width leader branch");
  require(std::any_of(
              variable_mask.steps.begin(), variable_mask.steps.end(),
              [](const ResolvedStep& step) { return step.comment == "display mask low branch"; }),
          "variable-leading display should emit the width dispatch branch");

  const CompileResult text_display = compile_source(R"mkpro(
program GenericBeerTextDisplay {
  state {
    level: counter 0..99 = stack.X
  }

  loop {
    show("BEEr ", level:02)
  }
}
)mkpro");
  require(text_display.implemented, "native compiler should lower generic text displays");
  require(text_display.diagnostics.empty(),
          "generic text display compile should not report diagnostics");
  require(has_optimization(text_display, "screen-text-lowering"),
          "generic text display should report the TS strategy name");
  require(text_display.registers.find("__text_tens_scratch") == text_display.registers.end(),
          "generic text display should not allocate the old tens scratch variable");
  require(text_display.registers.find("__text_ones_scratch") == text_display.registers.end(),
          "generic text display should not allocate the old ones scratch variable");
  require(text_display.registers.find("__text_digit_offset") == text_display.registers.end(),
          "generic text display should not allocate the old digit-offset scratch variable");
  require(std::any_of(text_display.steps.begin(), text_display.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x60 && step.comment == "text display verse";
                      }),
          "generic text display should start from the chosen source register");
  require(std::any_of(text_display.steps.begin(), text_display.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x41 && step.comment == "text tens scratch";
                      }),
          "generic text display should use the TS fixed tens scratch register");
  require(std::any_of(text_display.steps.begin(), text_display.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0xd8 && step.comment == "text display prefix";
                      }),
          "generic text display should use the TS fixed prefix register");
  require(std::any_of(text_display.steps.begin(), text_display.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x50 && step.comment == "show text";
                      }),
          "generic text display should emit the visible text stop");

  const CompileResult dynamic_line = compile_source(R"mkpro(
program DynamicLineReportShow {
  state {
    line: counter 0..9 = 3
  }

  loop {
    show("8.-0", line)
  }
}
)mkpro");
  require(dynamic_line.implemented, "native compiler should lower dynamic line reports");
  require(dynamic_line.diagnostics.empty(),
          "dynamic line report compile should not report diagnostics");
  require(has_optimization(dynamic_line, "screen-dynamic-line-report-lowering"),
          "dynamic line report should report the TS strategy name");
  require(std::any_of(dynamic_line.steps.begin(), dynamic_line.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.comment == "display dynamic line report source";
                      }),
          "dynamic line report should load its one-digit source");
  require(std::any_of(dynamic_line.steps.begin(), dynamic_line.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x14 &&
                               step.comment == "display dynamic line report operand order";
                      }),
          "dynamic line report should splice the right video value");
  require(std::any_of(dynamic_line.steps.begin(), dynamic_line.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x39 &&
                               step.comment == "display dynamic line report video bytes";
                      }),
          "dynamic line report should combine the video bytes with K xor");
  require(std::any_of(dynamic_line.steps.begin(), dynamic_line.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x50 && step.comment == "show dynamic line report";
                      }),
          "dynamic line report should emit the visible report stop");

  const CompileResult dynamic_line_width = compile_source(R"mkpro(
program DynamicLineReportWidth {
  state {
    line: counter 0..9 = 4
  }

  loop {
    show("8.-0", line:1)
  }
}
)mkpro");
  require(dynamic_line_width.implemented,
          "native compiler should lower explicitly one-wide dynamic line reports");
  require(dynamic_line_width.diagnostics.empty(),
          "explicit-width dynamic line report compile should not report diagnostics");
  require(has_optimization(dynamic_line_width, "screen-dynamic-line-report-lowering"),
          "explicit-width dynamic line report should report the TS strategy name");

  const CompileResult literal_helper = compile_source(R"mkpro(
program RepeatedLiteralScreenHelper {
  loop {
    show("8-LСГ90")
    show("8-LСГ90")
    show("8-LСГ90")
    show("8-LСГ90")
  }
}
)mkpro");
  require(literal_helper.implemented, "native compiler should lower repeated literal screens");
  require(literal_helper.diagnostics.empty(),
          "repeated literal screen compile should not report diagnostics");
  require(has_optimization(literal_helper, "screen-video-literal-helper-call"),
          "repeated literal screen should report the TS helper-call strategy name");
  require(has_optimization(literal_helper, "screen-video-literal-helper"),
          "repeated literal screen should emit the shared literal helper body");
  require(std::any_of(literal_helper.steps.begin(), literal_helper.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x53 && step.comment == "show literal helper";
                      }),
          "repeated literal screen should call the shared literal helper");
  require(std::any_of(literal_helper.steps.begin(), literal_helper.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x52 && step.comment == "display literal return";
                      }),
          "repeated literal screen should return from the shared helper");
}

} // namespace mkpro::tests
