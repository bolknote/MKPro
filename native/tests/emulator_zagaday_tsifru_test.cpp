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
      // Ra = the dark 5.40000A8-02 value:
      // 55400003 B-up 80000008 K-OR K-frac X->R0 K-R->X0 K-R->X0 R0 VP 1 /-/
      0x05,
      0x05,
      0x04,
      0x00,
      0x00,
      0x00,
      0x00,
      0x03,
      0x0e,
      0x08,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x08,
      0x38,
      0x35,
      0x40,
      0xd0,
      0xd0,
      0x60,
      0x0c,
      0x01,
      0x0b,
      0x4a,
      // R4 = -7.7777777. The sign is the prediction-loop sentinel.
      0x07,
      0x0a,
      0x07,
      0x07,
      0x07,
      0x07,
      0x07,
      0x07,
      0x07,
      0x0b,
      0x44,
      // R9 = 9.9999999-02: bit mask, approximate 1/10, and target 99.
      0x09,
      0x0a,
      0x09,
      0x09,
      0x09,
      0x09,
      0x09,
      0x09,
      0x09,
      0x0c,
      0x00,
      0x02,
      0x0b,
      0x49,
      // Rb = 11: duplicated player digit and return target 11.
      0x01,
      0x01,
      0x4b,
      // Reproducible cold start: clear weights, history, and previous prediction.
      0x0d,
      0x41,
      0x42,
      0x43,
      0x48,
      0x4e,
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

std::vector<int> step_opcodes(const std::vector<ResolvedStep>& steps) {
  std::vector<int> opcodes;
  opcodes.reserve(steps.size());
  for (const ResolvedStep& step : steps)
    opcodes.push_back(step.opcode);
  return opcodes;
}

std::vector<int> step_opcodes(const CompileResult& result) {
  return step_opcodes(result.steps);
}

} // namespace

void emulator_zagaday_tsifru_corrected_revision_preserves_history_and_ui() {
  const std::filesystem::path root = fixture_root();
  const std::vector<int> listing =
      parse_final_listing(root / "games" / "logic" / "zagaday-tsifru-hs-on.txt");

  require(listing.at(0) == 0x52 && listing.at(1) == 0x00 && listing.at(2) == 0x4c,
          "corrected listing should start with V/O, 0, X->Rc");
  require(listing.at(23) == 0x13 && listing.at(28) == 0x12,
          "R9 should replace multiplication by ten, then Ra should multiply by the threshold");
  require(listing.at(37) == 0x6e && listing.at(38) == 0x6c && listing.at(39) == 0x50,
          "the stop should expose score in X and the preceding prediction in Y");
  require(listing.at(47) == 0x67 && listing.at(48) == 0x4e,
          "the calculated R7 prediction must be copied into Re before comparison");
  require(listing.at(77) == 0x1c && std::count(listing.begin(), listing.end(), 0x1c) == 1 &&
              std::count(listing.begin(), listing.end(), 0x3b) == 0,
          "corrected listing should retain deterministic F sin learning and no K random");
  require(listing.at(87) == 0x68 && listing.at(88) == 0x64 && listing.at(89) == 0x35 &&
              listing.at(90) == 0x37 && listing.at(91) == 0x02 && listing.at(92) == 0x10 &&
              listing.at(93) == 0x60 && listing.at(94) == 0x38 && listing.at(95) == 0x48,
          "addresses 87..95 should mask, shift, and merge history without 1.9999996");
  require(listing.at(96) == 0x80 && listing.at(97) == 0x00 && listing.at(98) == 0x00 &&
              listing.at(99) == 0x69 && listing.at(104) == 0x13,
          "addresses 97 and 98 should be free before the 99..A4 helper tail");
  const std::vector<int> r6_operations{0x46, 0x66, 0x76, 0x86, 0x96, 0xa6, 0xb6, 0xc6, 0xd6, 0xe6};
  require(std::none_of(listing.begin(), listing.end(),
                       [&](int opcode) {
                         return std::find(r6_operations.begin(), r6_operations.end(), opcode) !=
                                r6_operations.end();
                       }),
          "R6 should remain completely free");

  emulator::MK61 calculator({.extended = true, .angle_mode = "grad"});
  const emulator::ProgramLoadResult setup_loaded = calculator.load_program(setup_program());
  require(setup_loaded.diagnostics.empty(), "Zagaday Tsifru setup should load");
  calculator.press_sequence({"В/О", "С/П"});
  require_stop(calculator, "corrected setup", 20000);
  const std::string dark_ra = trim_ascii(calculator.read_register("a"));
  const std::string mask_r9 = trim_ascii(calculator.read_register("9"));
  require(trim_ascii(calculator.read_register("4")) == "-7,7777777" &&
              dark_ra.starts_with("5,40000-8") && dark_ra.ends_with("-2") &&
              mask_r9.starts_with("9,9999999") && mask_r9.ends_with("-2") &&
              trim_ascii(calculator.read_register("b")).ends_with("11,"),
          "corrected setup should preserve signed R4 and the exact dark Ra/R9/Rb values");
  calculator.set_register("6", "6.1234567");
  const std::string free_r6 = trim_ascii(calculator.read_register("6"));

  const emulator::ProgramLoadResult loaded = calculator.load_program(listing);
  require(loaded.diagnostics.empty(), "105-cell corrected listing should load without truncation");
  calculator.press_sequence({"В/О", "С/П"});
  require_stop(calculator, "initial score and preceding prediction");
  require(trim_ascii(calculator.display_text()) == "0," &&
              trim_ascii(calculator.read_register("y")) == "0," &&
              calculator.program_counter() == "40",
          "corrected listing should stop with score in X and preceding prediction in Y");

  for (int turn = 1; turn <= 9; ++turn) {
    calculator.input_number("0", true);
    calculator.press("С/П");
    require_stop(calculator, "all-zero turn " + std::to_string(turn));
    const std::string expected_score = std::to_string(-7 * turn) + ",";
    require(trim_ascii(calculator.display_text()) == expected_score &&
                trim_ascii(calculator.read_register("c")) == expected_score &&
                trim_ascii(calculator.read_register("y")) == "0," &&
                trim_ascii(calculator.read_register("e")) == "0," &&
                trim_ascii(calculator.read_register("8")) == "8,",
            "an all-zero history must stay exactly zero and score each matching prediction");
  }

  const std::vector<std::string> shifted_histories{"8,1",     "8,21",     "8,321",    "8,4321",
                                                   "8,54321", "8,654321", "8,7654321"};
  for (int digit = 1; digit <= 7; ++digit) {
    calculator.input_number(std::to_string(digit), true);
    calculator.press("С/П");
    require_stop(calculator, "mixed-history turn " + std::to_string(digit));
    require(trim_ascii(calculator.read_register("8")) ==
                shifted_histories.at(static_cast<std::size_t>(digit - 1)),
            "new history digits should be inserted from the left");
    require(trim_ascii(calculator.display_text()) == trim_ascii(calculator.read_register("c")) &&
                trim_ascii(calculator.read_register("y")) ==
                    trim_ascii(calculator.read_register("e")),
            "each stop should expose score in X and the used prediction in Y");
  }
  const std::vector<std::pair<std::string, std::string>> aliased_inputs{
      {"8", "8,0765432"}, {"9", "8,1076543"}};
  for (const auto& [input, expected_history] : aliased_inputs) {
    calculator.input_number(input, true);
    calculator.press("С/П");
    require_stop(calculator, "aliased input " + input);
    require(trim_ascii(calculator.read_register("8")) == expected_history,
            "inputs 8 and 9 should be treated as 0 and 1 respectively");
  }
  require(trim_ascii(calculator.read_register("6")) == free_r6,
          "gameplay must leave the free R6 register untouched");

  const std::string semantic_source = read_text(root / "examples" / "zagaday-tsifru.mkpro");
  const CompileResult semantic = compile_source(semantic_source);
  require(semantic.implemented && semantic.diagnostics.empty() && semantic.steps.size() == 105U,
          "corrected Zagaday Tsifru semantic source should compile into the MK-61 window");
  const std::vector<int> semantic_opcodes = step_opcodes(semantic);
  require(std::count(semantic_opcodes.begin(), semantic_opcodes.end(), 0x1c) == 1 &&
              std::count(semantic_opcodes.begin(), semantic_opcodes.end(), 0x3b) == 0 &&
              semantic_source.find("HISTORY_SHIFT") == std::string::npos &&
              semantic_source.find("history = bit_or(history + 2, 10 + player)") !=
                  std::string::npos,
          "semantic source should retain deterministic learning and the corrected history shift");

  require(semantic.setup_program.has_value(), "semantic source should provide a setup program");
  const auto history_store =
      std::find_if(semantic.steps.begin(), semantic.steps.end(), [](const ResolvedStep& step) {
        return step.comment == "set history" && step.opcode >= 0x40 && step.opcode <= 0x4e;
      });
  require(history_store != semantic.steps.end(),
          "semantic listing should expose its allocated history register");
  const int history_register_index = history_store->opcode & 0x0f;
  const std::string history_register =
      history_register_index < 10
          ? std::to_string(history_register_index)
          : std::string(1, static_cast<char>('a' + history_register_index - 10));
  emulator::MK61 semantic_calculator({.extended = true, .angle_mode = "grad"});
  const emulator::ProgramLoadResult semantic_setup =
      semantic_calculator.load_program(step_opcodes(semantic.setup_program->steps));
  require(semantic_setup.diagnostics.empty(), "semantic setup should load");
  semantic_calculator.press_sequence({"В/О", "С/П"});
  require_stop(semantic_calculator, "semantic setup", 20000);
  const emulator::ProgramLoadResult semantic_main =
      semantic_calculator.load_program(semantic_opcodes);
  require(semantic_main.diagnostics.empty(), "105-cell semantic program should load");
  semantic_calculator.press_sequence({"В/О", "С/П"});
  require_stop(semantic_calculator, "semantic initial score");
  for (int turn = 1; turn <= 2; ++turn) {
    semantic_calculator.input_number("0", true);
    semantic_calculator.press("С/П");
    require_stop(semantic_calculator, "semantic zero prediction " + std::to_string(turn));
    require(trim_ascii(semantic_calculator.display_text()) == "0,",
            "semantic zero history should predict zero");
    semantic_calculator.press("С/П");
    require_stop(semantic_calculator, "semantic zero score " + std::to_string(turn));
    require(trim_ascii(semantic_calculator.read_register(history_register)) == "8,",
            "semantic history must remain exactly zero after a zero input");
  }
  for (int digit = 1; digit <= 7; ++digit) {
    semantic_calculator.input_number(std::to_string(digit), true);
    semantic_calculator.press("С/П");
    require_stop(semantic_calculator, "semantic mixed prediction " + std::to_string(digit));
    semantic_calculator.press("С/П");
    require_stop(semantic_calculator, "semantic mixed score " + std::to_string(digit));
    const std::string actual_history =
        trim_ascii(semantic_calculator.read_register(history_register));
    const std::string& expected_history = shifted_histories.at(static_cast<std::size_t>(digit - 1));
    require(actual_history == expected_history,
            "semantic history should insert mixed digits from the left: expected=" +
                expected_history + ", actual=" + actual_history);
  }

  for (int ones = 0; ones <= 21; ++ones) {
    const int published = static_cast<int>(std::floor((ones + 8) * 0.054000098));
    require((ones + 8) / 19 == published,
            "semantic threshold should equal the published factor for every reachable popcount");
  }
}

} // namespace mkpro::tests
