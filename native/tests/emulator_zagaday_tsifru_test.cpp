#include "mkpro/compiler.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

std::filesystem::path fixture_root() {
  const std::filesystem::path current = std::filesystem::current_path();
  if (std::filesystem::exists(current / "games" / "logic" / "zagaday-tsifru-hs-on.txt"))
    return current;
  if (std::filesystem::exists(current.parent_path() / "games" / "logic" /
                              "zagaday-tsifru-hs-on.txt"))
    return current.parent_path();
  throw std::runtime_error("cannot locate Zagaday Tsifru fixtures from " + current.string());
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  require(input.good(), "should read " + path.string());
  std::ostringstream source;
  source << input.rdbuf();
  return source.str();
}

std::optional<int> compact_register_opcode(std::string_view mnemonic) {
  const auto with_register = [&](std::string_view prefix, int base) -> std::optional<int> {
    if (!mnemonic.starts_with(prefix))
      return std::nullopt;
    const std::string_view suffix = mnemonic.substr(prefix.size());
    if (suffix.empty())
      return std::nullopt;
    try {
      std::string register_name(suffix);
      if (register_name == "А")
        register_name = "a";
      else if (register_name == "В")
        register_name = "b";
      else if (register_name == "С")
        register_name = "c";
      else if (register_name == "Д")
        register_name = "d";
      else if (register_name == "Е")
        register_name = "e";
      else
        register_name = register_from_text(register_name);
      return base + register_index(register_name);
    } catch (const std::runtime_error&) {
      return std::nullopt;
    }
  };

  for (const auto& [prefix, base] : {
           std::pair<std::string_view, int>{"Пх", 0x60},
           {"хП", 0x40},
           {"КПП", 0xa0},
           {"КБП", 0x80},
           {"КПх", 0xd0},
           {"КхП", 0xb0},
           {"Кx=0", 0xe0},
           {"Кx≠0", 0x70},
           {"Кx≥0", 0x90},
           {"Кx<0", 0xc0},
       }) {
    if (const std::optional<int> opcode = with_register(prefix, base))
      return opcode;
  }
  return std::nullopt;
}

int parse_listing_opcode(const std::string& mnemonic, const std::string& context) {
  if (mnemonic == "↔")
    return 0x14;
  if (const OpcodeInfo* opcode = find_opcode_name(mnemonic))
    return opcode->code;
  if (const std::optional<int> opcode = compact_register_opcode(mnemonic))
    return *opcode;
  throw std::runtime_error(context + ": unknown MK-61 mnemonic " + mnemonic);
}

std::vector<int> parse_final_listing(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read Zagaday Tsifru listing: " + path.string());

  std::vector<int> codes;
  std::string line;
  int source_line = 0;
  while (std::getline(input, line)) {
    ++source_line;
    if (trim_ascii(line).empty())
      continue;
    const std::size_t separator = line.find('\t');
    require(separator != std::string::npos,
            "Zagaday Tsifru listing line should contain address and mnemonic");
    const std::string address = trim_ascii(line.substr(0, separator));
    const std::string mnemonic = trim_ascii(line.substr(separator + 1U));
    require(address == format_address(static_cast<int>(codes.size())),
            "Zagaday Tsifru listing should be contiguous at source line " +
                std::to_string(source_line));
    codes.push_back(
        parse_listing_opcode(mnemonic, path.string() + ":" + std::to_string(source_line)));
  }
  require(codes.size() == 105U, "final Zagaday Tsifru listing should occupy addresses 00..A4");
  return codes;
}

std::vector<int> setup_program() {
  return {
      // R6 = 1.9999996
      0x01,
      0x0a,
      0x09,
      0x09,
      0x09,
      0x09,
      0x09,
      0x09,
      0x06,
      0x46,
      // R7 = 5.4000098-02
      0x05,
      0x0a,
      0x04,
      0x00,
      0x00,
      0x00,
      0x00,
      0x09,
      0x08,
      0x0c,
      0x02,
      0x0b,
      0x47,
      // Rb = the dark A8 selector: 557 K INV K {x} VP 2
      0x05,
      0x05,
      0x07,
      0x3a,
      0x35,
      0x0c,
      0x02,
      0x4b,
      // Rc = 10
      0x01,
      0x00,
      0x4c,
      // Rd = 7.7777777
      0x07,
      0x0a,
      0x07,
      0x07,
      0x07,
      0x07,
      0x07,
      0x07,
      0x07,
      0x4d,
      0x50,
  };
}

void require_stop(emulator::MK61& calculator, const std::string& phase, int max_frames = 30000) {
  const emulator::RunResult run = calculator.run_until_stable(max_frames, 8);
  require(run.stopped, "final Zagaday Tsifru should stop at " + phase +
                           ", pc=" + calculator.program_counter() +
                           ", display=" + trim_ascii(calculator.display_text()) +
                           ", frames=" + std::to_string(run.frames));
}

std::vector<int> step_opcodes(const CompileResult& result) {
  std::vector<int> opcodes;
  opcodes.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    opcodes.push_back(step.opcode);
  return opcodes;
}

} // namespace

void emulator_zagaday_tsifru_final_revision_preserves_ui() {
  const std::filesystem::path root = fixture_root();
  const std::vector<int> listing =
      parse_final_listing(root / "games" / "logic" / "zagaday-tsifru-hs-on.txt");

  require(listing.at(0) == 0x52 && listing.at(1) == 0x00 && listing.at(2) == 0x45,
          "final listing should start with V/O, 0, X->R5");
  require(listing.at(54) == 0x44, "final listing address 54 must store the entered digit in R4");
  require(listing.at(81) == 0x1c && std::count(listing.begin(), listing.end(), 0x1c) == 1 &&
              std::count(listing.begin(), listing.end(), 0x3b) == 0,
          "final listing should replace K random with one F sin command");
  require(listing.at(95) == 0x00 && listing.at(96) == 0x00 && listing.at(97) == 0x00 &&
              listing.at(98) == 0x09 && listing.at(104) == 0x13,
          "final listing should preserve its zero padding and 98..A4 helper tail");

  emulator::MK61 calculator({.extended = true, .angle_mode = "grad"});
  const emulator::ProgramLoadResult setup_loaded = calculator.load_program(setup_program());
  require(setup_loaded.diagnostics.empty(), "Zagaday Tsifru setup should load");
  calculator.press_sequence({"В/О", "С/П"});
  require_stop(calculator, "setup", 10000);
  require(trim_ascii(calculator.read_register("6")) == "1,9999996" &&
              trim_ascii(calculator.read_register("b")) == "85700," &&
              trim_ascii(calculator.read_register("c")) == "10," &&
              trim_ascii(calculator.read_register("d")) == "7,7777777",
          "published setup should preload R6, dark Rb, Rc, and Rd exactly");

  const emulator::ProgramLoadResult loaded = calculator.load_program(listing);
  require(loaded.diagnostics.empty(), "105-cell final listing should load without truncation");
  calculator.press_sequence({"В/О", "С/П"});
  require_stop(calculator, "initial score");
  require(trim_ascii(calculator.display_text()) == "0,",
          "final listing should start with score zero");

  calculator.input_number("3", true);
  calculator.press("ПП");
  require_stop(calculator, "first positive prediction", 1000);
  require(trim_ascii(calculator.display_text()) == "0," && calculator.program_counter() == "43",
          "digit PP should reveal the first positive prediction and wait for C/P");
  calculator.press("С/П");
  require_stop(calculator, "score after a miss");
  require(trim_ascii(calculator.display_text()) == "1," &&
              trim_ascii(calculator.read_register("5")) == "1,",
          "a missed prediction should add one point");

  calculator.input_number("7", true);
  calculator.press("ПП");
  require_stop(calculator, "second positive prediction", 1000);
  require(trim_ascii(calculator.display_text()) == "7,",
          "the final listing should show prediction 7 without the old minus sign");
  calculator.press("С/П");
  require_stop(calculator, "score after a hit");
  require(trim_ascii(calculator.display_text()) == "-6," &&
              trim_ascii(calculator.read_register("5")) == "-6,",
          "a matched prediction should subtract seven points");

  const std::string semantic_source = read_text(root / "examples" / "zagaday-tsifru.mkpro");
  const CompileResult semantic = compile_source(semantic_source);
  require(semantic.implemented && semantic.diagnostics.empty() && semantic.steps.size() == 105U,
          "final Zagaday Tsifru semantic source should compile into the MK-61 window");
  const std::vector<int> semantic_opcodes = step_opcodes(semantic);
  require(std::count(semantic_opcodes.begin(), semantic_opcodes.end(), 0x1c) == 1 &&
              std::count(semantic_opcodes.begin(), semantic_opcodes.end(), 0x3b) == 0 &&
              semantic_source.find("history = bit_or(history + HISTORY_SHIFT, player)") !=
                  std::string::npos,
          "semantic source should retain deterministic sine learning and the new history shift");

  for (int ones = 0; ones <= 21; ++ones) {
    const int published = static_cast<int>(std::floor((ones + 8) * 0.054000098));
    require((ones + 8) / 19 == published,
            "semantic threshold should equal the published factor for every reachable popcount");
  }
}

} // namespace mkpro::tests
