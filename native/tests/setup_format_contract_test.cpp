#include "mkpro/compiler.hpp"
#include "mkpro/core/format.hpp"

#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read fixture: " + path.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

} // namespace

void setup_formatting_matches_typescript_contract() {
  // Traceability: tests/compiler/setup-format.test.ts
  const std::filesystem::path root = std::filesystem::current_path();

  {
    const CompileResult game = compile_source(read_text(root / "examples" / "game-100-pig.mkpro"));
    const std::optional<std::string> setup_block = format_setup_block(game.preloads);
    require(setup_block.has_value(), "setup-format should render setup registers for game-100-pig");
    require(*setup_block ==
                "`R5=0; R0=0; R7=0; R4=0; R6=0; Ra=20; Rb=100; Rc=1E3; Rd=1E7; "
                "Re=8,-00-000; R8=L3; R9=С4`",
            "setup-format should render the same deterministic setup block; actual " +
                *setup_block);
  }

  {
    const std::vector<PreloadReport> formal_preloads{
        {.register_name = "7", .value = "C5", .counts_against_program = false},
        {.register_name = "8", .value = "FF", .counts_against_program = false},
        {.register_name = "9", .value = "1E-3", .counts_against_program = false},
        {.register_name = "a", .value = "1|-00", .counts_against_program = false},
    };
    const std::optional<std::string> setup_block = format_setup_block(formal_preloads);
    require(setup_block.has_value(), "format setup block should accept formal preloads");
    require(*setup_block == "`R7=С5; R8=__; R9=1E-3`",
            "format setup block should keep non-decimal literals compact");
  }

  {
    CompileOptions analysis_options;
    analysis_options.analysis = true;
    const CompileResult wumpus = compile_source(read_text(root / "examples" / "wumpus.mkpro"),
                                               analysis_options);
    require(wumpus.listing.find("# Setup Listing") != std::string::npos,
            "setup-format should include setup listing section");
    require(wumpus.listing.find("setup wumpus") != std::string::npos,
            "setup-format should include a named setup program in listing");
    require(wumpus.listing.find("# Main Listing") != std::string::npos,
            "setup-format should include main listing section");
  }

  {
    CompileOptions analysis_options;
    analysis_options.analysis = true;
    analysis_options.budget = 999;
    const CompileResult rambo = compile_source(read_text(root / "examples" / "rambo-iii.mkpro"),
                                              analysis_options);
    require(rambo.listing.find("Setup Block:") != std::string::npos,
            "setup-format should include setup-block values for high-value constants");
    require(rambo.listing.find("   00 |   -  | 25") != std::string::npos,
            "setup-format should show compact setup load for decimal constant");
    require(rambo.listing.find("   02 |   -  | 0.5") != std::string::npos,
            "setup-format should show compact setup load for 0.5");
    require(rambo.listing.find("   05 |  41  | X→П 1") != std::string::npos,
            "setup-format should show setup program register moves");
    require(rambo.listing.find("# Setup Listing") != std::string::npos,
            "setup-format should show setup listing for over-budget programs");
  }

  {
    const std::vector<ResolvedStep> steps = {
        {.address = 0, .opcode = 0x01, .hex = "01", .mnemonic = "1"},
        {.address = 1, .opcode = 0x05, .hex = "05", .mnemonic = "5"},
        {.address = 2, .opcode = 0x54, .hex = "54", .mnemonic = "ВП", .comment = "halt"},
    };
    const std::string listing = format_listing_steps(steps);
    require(listing.find("01 05 54") != std::string::npos,
            "formatListing should keep short merged number-entry token groups explicit");
    require(listing.find("15E") != std::string::npos,
            "formatListing should merge adjacent number-entry commands");
    require(listing.find("halt") != std::string::npos,
            "formatListing should preserve trailing instruction comment");
  }

  {
    CompileOptions setup_options;
    setup_options.analysis = true;
    const CompileResult bottles = compile_source(read_text(root / "examples" / "99-bottles.mkpro"),
                                                setup_options);
    require(bottles.listing.find("# Setup Listing") != std::string::npos,
            "startup stack-input setup should include setup listing");
    require(bottles.listing.find("enter any value 0..99 in X") != std::string::npos,
            "startup stack-input setup should preserve user prompt text");
    require(bottles.listing.find("X→П 0") != std::string::npos,
            "startup stack-input setup should include an R0 preload transfer");
    require(!bottles.setup_listing.empty(),
            "startup stack-input setup should expose the extracted setup listing text");
  }
}

} // namespace mkpro::tests
