#include "mkpro/core/format.hpp"

#include "test_support.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mkpro::tests {

void format_primitives_match_typescript_contract() {
  const std::vector<ResolvedStep> steps = {
      {.address = 0, .hex = "50", .mnemonic = "С/П"},
      {.address = 1, .hex = "41", .mnemonic = "X->П 1"},
      {.address = 2, .hex = "50", .mnemonic = "С/П"},
      {.address = 3, .hex = "61", .mnemonic = "П->X 1"},
      {.address = 4, .hex = "10", .mnemonic = "+"},
      {.address = 5, .hex = "50", .mnemonic = "С/П"},
      {.address = 6, .hex = "51", .mnemonic = "БП"},
      {.address = 7, .hex = "00", .mnemonic = "0"},
      {.address = 8, .hex = "21", .mnemonic = "F sqrt"},
  };

  require(format_hex_steps(steps) == "00: 50 41 50 61 10 50 51 00\n08: 21",
          "hex formatter should use 8 columns and formal addresses");
  require(format_mk61s_steps(steps) == "0000 504150611050510021",
          "mk61s formatter should emit compact hout-compatible rows");
  require(format_listing_steps({steps.begin(), steps.begin() + 3}) ==
              " Step | Code | Command                 | Comment\n"
              "------+------+-------------------------+----------------\n"
              "   00 |  50  | С/П                     |\n"
              "   01 |  41  | X→П 1                   |\n"
              "   02 |  50  | С/П                     |",
          "listing formatter should use TS table layout");
  require(format_program_tokens({steps.begin(), steps.begin() + 3}) == "50\n41\n50",
          "program token formatter should emit one hex byte per line");

  std::vector<ResolvedStep> mk61s_steps;
  for (int index = 0; index < 25; ++index)
    mk61s_steps.push_back({.address = index, .hex = index == 24 ? "18" : "50", .mnemonic = "noop"});
  require(format_mk61s_steps(mk61s_steps) ==
              "0000 505050505050505050505050505050505050505050505050\n"
              "0024 18",
          "mk61s formatter should wrap after 24 bytes and keep a short tail");

  CompileResult mk61s_result;
  mk61s_result.steps = {steps.begin(), steps.begin() + 3};
  mk61s_result.setup_program = SetupProgramReport{
      .steps = {steps.begin() + 3, steps.begin() + 6},
      .reason = "test setup",
      .optimizations = {},
  };
  require(format_mk61s_result(mk61s_result) ==
              "hin 0000 611050\n"
              "run\n"
              "hin 0000 504150",
          "mk61s result formatter should emit setup load, setup run, then program load");
  mk61s_result.expected_mode = "grd";
  require(format_mk61s_result(mk61s_result) ==
              "kbd 9\n"
              "hin 0000 611050\n"
              "run\n"
              "hin 0000 504150",
          "mk61s result formatter should set expected angle mode before hin rows");

  CompileResult mk61s_no_setup_result;
  mk61s_no_setup_result.expected_mode = "rad";
  mk61s_no_setup_result.steps = {steps.begin(), steps.begin() + 3};
  require(format_mk61s_result(mk61s_no_setup_result) ==
              "kbd E\n"
              "0000 504150",
          "mk61s result formatter should set expected angle mode before program rows");

  const std::vector<ResolvedStep> dot_steps = {
      {.address = 0, .opcode = 0x5e, .hex = "5E", .mnemonic = "F x=0", .comment = "if zero"},
      {.address = 1, .opcode = 0x05, .hex = "05", .mnemonic = "05"},
      {.address = 2, .opcode = 0xaf, .hex = "AF", .mnemonic = "К ПП 0 alias"},
      {.address = 3, .opcode = 0x52, .hex = "52", .mnemonic = "В/О"},
      {.address = 4, .opcode = 0x8f, .hex = "8F", .mnemonic = "К БП 0 alias"},
      {.address = 5, .opcode = 0x50, .hex = "50", .mnemonic = "С/П", .comment = "halt"},
  };
  const std::string dot = format_dot_steps(dot_steps);
  require(dot.find("digraph mk61_cfg") != std::string::npos,
          "dot formatter should emit a Graphviz graph");
  require(dot.find("n0 [label=\"[00..01] branch\\l  F x=0 05\\l\"") != std::string::npos,
          "dot formatter should emit a direct conditional branch node with its address operand");
  require(dot.find("n0 -> n5") != std::string::npos &&
              dot.find("label=\"yes\"") != std::string::npos,
          "dot formatter should emit the conditional target edge");
  require(dot.find("n0 -> n2") != std::string::npos &&
              dot.find("label=\"no\"") != std::string::npos,
          "dot formatter should emit the conditional fallthrough edge");
  require(dot.find("К ПП 0 alias") != std::string::npos &&
              dot.find("[?] call") != std::string::npos && dot.find("R0") != std::string::npos,
          "dot formatter should cover undocumented indirect call aliases");
  require(dot.find("return stack") == std::string::npos,
          "dot formatter should not invent a static return-stack target");
  require(dot.find("n4 [label=\"[04] entryless") != std::string::npos,
          "dot formatter should keep decoded code without static entries visible");
  require(dot.find("[?] jump") != std::string::npos,
          "dot formatter should cover undocumented indirect jump aliases");

  const std::vector<ResolvedStep> known_indirect_steps = {
      {.address = 0,
       .opcode = 0x87,
       .hex = "87",
       .mnemonic = "К БП 7",
       .comment = "preloaded R7=B2 indirect-target=0 indirect flow"},
  };
  const std::vector<PreloadReport> known_indirect_preloads = {
      {.register_name = "7", .value = "B2", .counts_against_program = false},
  };
  const std::string known_dot =
      format_dot_steps(known_indirect_steps, known_indirect_preloads);
  require(known_dot.find("К БП 7") != std::string::npos &&
              known_dot.find("n0 -> n0") != std::string::npos &&
              known_dot.find("[?]") == std::string::npos,
          "dot formatter should resolve proved preloaded indirect branch targets");

  const std::vector<PreloadReport> formal_preloads = {
      {.register_name = "7", .value = "C5", .counts_against_program = false},
      {.register_name = "8", .value = "FF", .counts_against_program = false},
      {.register_name = "9", .value = "1E-3", .counts_against_program = false},
      {.register_name = "a", .value = "1|-00", .counts_against_program = false},
  };
  const std::optional<std::string> setup_block = format_setup_block(formal_preloads);
  require(setup_block.has_value(), "setup block formatter should render literal preloads");
  require(*setup_block == "`R7=С5; R8=__; R9=1E-3`",
          "setup block formatter should use the calculator display alphabet for formal preloads");

  const std::vector<ResolvedStep> number_steps = {
      {.address = 0, .opcode = 1, .hex = "01", .mnemonic = "1", .comment = "literal"},
      {.address = 1, .opcode = 2, .hex = "02", .mnemonic = "2"},
      {.address = 2, .opcode = 0x0a, .hex = "0A", .mnemonic = "."},
      {.address = 3, .opcode = 3, .hex = "03", .mnemonic = "3"},
      {.address = 4, .opcode = 0x0c, .hex = "0C", .mnemonic = "ВП"},
      {.address = 5, .opcode = 4, .hex = "04", .mnemonic = "4"},
      {.address = 6, .opcode = 0x0b, .hex = "0B", .mnemonic = "/-/"},
      {.address = 7, .opcode = 0x0b, .hex = "0B", .mnemonic = "/-/"},
      {.address = 8, .opcode = 0x50, .hex = "50", .mnemonic = "С/П", .comment = "halt"},
  };
  const std::string number_listing = format_listing_steps(number_steps);
  require(number_listing.find("01 ... 0B") != std::string::npos,
          "listing formatter should collapse long number entry code");
  require(number_listing.find("-12.3E-4") != std::string::npos,
          "listing formatter should collapse long number entry mnemonic");
  require(number_listing.find("literal") != std::string::npos,
          "listing formatter should preserve first number-entry comment");
  require(number_listing.find("halt") != std::string::npos,
          "listing formatter should preserve following comments");

  require(to_keycaps("*") == "×", "multiplication keycap");
  require(to_keycaps("/") == "÷", "division keycap");
  require(to_keycaps("-") == "−", "minus keycap");
  require(to_keycaps("<->") == "↔", "swap keycap");
  require(to_keycaps("F pi") == "F π", "pi keycap");
  require(to_keycaps("F sqrt") == "F √", "sqrt keycap");
  require(to_keycaps("F x^-1") == "F x⁻¹", "inverse superscript keycap");
  require(to_keycaps("X->П 0") == "X→П 0", "store arrow keycap");
  require(to_keycaps("F x!=0") == "F x≠0", "not-equal branch keycap");
  require(to_keycaps("F x>=0") == "F x≥0", "greater-or-equal branch keycap");
}

} // namespace mkpro::tests
