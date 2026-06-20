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
  require(format_listing_steps({steps.begin(), steps.begin() + 3}) ==
              " Step | Code | Command                 | Comment\n"
              "------+------+-------------------------+----------------\n"
              "   00 |  50  | С/П                     |\n"
              "   01 |  41  | X→П 1                   |\n"
              "   02 |  50  | С/П                     |",
          "listing formatter should use TS table layout");
  require(format_program_tokens({steps.begin(), steps.begin() + 3}) == "50\n41\n50",
          "program token formatter should emit one hex byte per line");

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
