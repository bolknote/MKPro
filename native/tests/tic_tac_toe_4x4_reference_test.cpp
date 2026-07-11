#include "mkpro/core/opcodes.hpp"
#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
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

std::optional<int> compact_register_opcode(std::string_view mnemonic) {
  const auto with_register = [&](std::string_view prefix, int base) -> std::optional<int> {
    if (!mnemonic.starts_with(prefix))
      return std::nullopt;
    const std::string_view suffix = mnemonic.substr(prefix.size());
    if (suffix.size() != 1U)
      return std::nullopt;
    try {
      return base + register_index(suffix);
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
  if (const OpcodeInfo* opcode = find_opcode_name(mnemonic))
    return opcode->code;
  if (const std::optional<int> opcode = compact_register_opcode(mnemonic))
    return *opcode;
  throw std::runtime_error(context + ": unknown MK-61 mnemonic " + mnemonic);
}

std::vector<int> parse_reference_listing(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read reference listing: " + path.string());

  std::vector<int> codes;
  std::string line;
  int source_line = 0;
  while (std::getline(input, line)) {
    ++source_line;
    if (trim_ascii(line).empty())
      continue;
    const std::size_t separator = line.find('\t');
    require(separator != std::string::npos,
            "reference listing line should contain an address and mnemonic");
    const std::string address = trim_ascii(line.substr(0, separator));
    const std::string mnemonic = trim_ascii(line.substr(separator + 1U));
    require(address == format_address(static_cast<int>(codes.size())),
            "reference listing should be contiguous at source line " + std::to_string(source_line) +
                ": expected " + format_address(static_cast<int>(codes.size())) + ", got " +
                address);
    codes.push_back(
        parse_listing_opcode(mnemonic, path.string() + ":" + std::to_string(source_line)));
  }
  require(codes.size() == 105U, "reference listing should occupy exactly 105 MK-61 cells");
  return codes;
}

void preload_reference_game(emulator::MK61& calc) {
  calc.set_register("a", "ГE-2");
  calc.set_register("b", "88888834");
  calc.set_register("c", "2.2600029E-1");
  calc.set_register("d", "4.1200076E-1");
  for (const std::string_view reg : {"4", "5", "6", "7"})
    calc.set_register(std::string(reg), "44444.4");
  calc.set_register("9", "0");
}

std::string compact(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) { return std::isspace(ch) != 0; }),
              value.end());
  return value;
}

int read_integer(emulator::MK61& calc, std::string_view reg) {
  return std::stoi(compact(calc.read_register(std::string(reg))));
}

int display_integer(emulator::MK61& calc) {
  return std::stoi(compact(calc.display_text()));
}

std::array<std::string, 5> board_signature(emulator::MK61& calc) {
  return {
      compact(calc.read_register("4")), compact(calc.read_register("5")),
      compact(calc.read_register("6")), compact(calc.read_register("7")),
      compact(calc.read_register("9")),
  };
}

void run_to_stop(emulator::MK61& calc, std::string_view context) {
  const emulator::RunResult run = calc.run_until_stable(12000, 8);
  require(run.stopped, std::string(context) + " should stop");
}

void input_ui_number(emulator::MK61& calc, std::string value) {
  const bool negative = !value.empty() && value.front() == '-';
  if (negative)
    value.erase(value.begin());
  calc.input_number(value, true);
  if (negative)
    calc.press("/-/");
}

void enter_x(emulator::MK61& calc, const std::string& x, std::string_view context) {
  input_ui_number(calc, x);
  calc.press("ПП");
  run_to_stop(calc, context);
  require(calc.program_counter() == "05",
          std::string(context) + " should execute only X->П 3 and stop at PC 05");
  require(read_integer(calc, "3") == std::stoi(x),
          std::string(context) + " should store the entered X coordinate in R3");
}

void enter_y_and_run(emulator::MK61& calc, const std::string& y, std::string_view context) {
  input_ui_number(calc, y);
  calc.press("С/П");
  run_to_stop(calc, context);
  require(calc.program_counter() == "04",
          std::string(context) + " should return to the next X-entry instruction");
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read source fixture: " + path.string());
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::filesystem::path fixture_root() {
  const std::filesystem::path current = std::filesystem::current_path();
  if (std::filesystem::exists(current / "games" / "tic-tac-toe-4x4.txt"))
    return current;
  if (std::filesystem::exists(current.parent_path() / "games" / "tic-tac-toe-4x4.txt"))
    return current.parent_path();
  throw std::runtime_error("cannot locate the MK-Pro fixture root from " + current.string());
}

struct UiObservation {
  std::string display;
  std::string x;
  std::string y;

  bool operator==(const UiObservation&) const = default;
};

emulator::MK61 boot_reference_game(const std::vector<int>& reference) {
  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  const emulator::ProgramLoadResult loaded = calc.load_program(reference);
  require(loaded.diagnostics.empty(), "reference UI probe should load without truncation");
  preload_reference_game(calc);
  calc.press_sequence({"В/О", "С/П"});
  run_to_stop(calc, "reference UI probe cold start");
  return calc;
}

UiObservation play_reference_first_move(const std::vector<int>& reference, const std::string& x,
                                        const std::string& y) {
  emulator::MK61 calc = boot_reference_game(reference);
  enter_x(calc, x, "reference UI probe X phase");
  enter_y_and_run(calc, y, "reference UI probe Y phase");
  return UiObservation{
      .display = compact(calc.display_text()),
      .x = compact(calc.read_register("x")),
      .y = compact(calc.read_register("y")),
  };
}

} // namespace

void tic_tac_toe_4x4_reference_transcript_matches_original_listing() {
  const std::filesystem::path root = fixture_root();
  const std::vector<int> reference =
      parse_reference_listing(root / "games" / "tic-tac-toe-4x4.txt");

  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  const emulator::ProgramLoadResult loaded = calc.load_program(reference);
  require(loaded.diagnostics.empty(), "105-cell reference listing should load without truncation");
  require(calc.read_program_codes(105) == reference,
          "emulator should preserve every parsed reference opcode through address A4");
  preload_reference_game(calc);

  require(compact(calc.read_register("a")) == "Г,-2",
          "reference Ra should contain the documented hexadecimal ГE-2 flag");
  require(read_integer(calc, "b") == 88888834,
          "reference Rb should contain the documented win mask");
  require(compact(calc.read_register("c")) == "2,2600029-1" &&
              compact(calc.read_register("d")) == "4,1200076-1",
          "reference Rc/Rd should contain the documented dual-use constants");
  require(board_signature(calc) ==
              std::array<std::string, 5>{"44444,4", "44444,4", "44444,4", "44444,4", "0,"},
          "reference board banks and occupied mask should have their documented initial values");

  calc.press_sequence({"В/О", "С/П"});
  run_to_stop(calc, "cold start");
  require(calc.program_counter() == "04" && display_integer(calc) == 0 &&
              read_integer(calc, "x") == 0 && read_integer(calc, "y") == 0,
          "cold start should expose stack pair X=0, Y=0 at the first coordinate store");

  enter_x(calc, "1", "first coordinate entry");
  enter_y_and_run(calc, "1", "first move");
  require(display_integer(calc) == 3 && read_integer(calc, "x") == 3 &&
              read_integer(calc, "y") == 3 && read_integer(calc, "1") == 3 &&
              read_integer(calc, "2") == 3,
          "after player 1:1 the reference calculator should answer at 3:3");
  const std::array<std::string, 5> after_first_move = board_signature(calc);
  require(after_first_move ==
              std::array<std::string, 5>{"44444,4", "44444,4", "44543,4", "44543,4", "8,104"},
          "first move should update the documented packed line and occupancy banks");

  enter_x(calc, "1", "occupied coordinate entry");
  enter_y_and_run(calc, "1", "occupied move");
  require(display_integer(calc) == -99999999 && read_integer(calc, "x") == -99999999 &&
              read_integer(calc, "y") == 1 && read_integer(calc, "3") == -99999999,
          "re-entering occupied cell 1:1 should expose retry pair X=-99999999, Y=1");
  require(board_signature(calc) == after_first_move,
          "occupied-cell retry should not change line or occupancy banks");

  enter_x(calc, "1", "retry coordinate entry");
  enter_y_and_run(calc, "2", "retry move");
  require(display_integer(calc) == 1 && read_integer(calc, "x") == 1 &&
              read_integer(calc, "y") == 3 && read_integer(calc, "1") == 1 &&
              read_integer(calc, "2") == 3,
          "after retry at player 1:2 the reference calculator should answer at 1:3");
  require(board_signature(calc) ==
              std::array<std::string, 5>{"44354,4", "45344,4", "44633,4", "44543,4", "8,704"},
          "second accepted move should update all affected packed banks");
}

void tic_tac_toe_4x4_reference_ui_normalizes_coordinates() {
  const std::filesystem::path root = fixture_root();
  const std::vector<int> reference =
      parse_reference_listing(root / "games" / "tic-tac-toe-4x4.txt");

  const UiObservation one_one = play_reference_first_move(reference, "1", "1");
  for (const std::string& alias : {std::string("5"), std::string("9"), std::string("1.9")}) {
    require(play_reference_first_move(reference, alias, "1") == one_one,
            "positive/fractional X alias " + alias + " should normalize to X=1");
  }

  const UiObservation three_one = play_reference_first_move(reference, "3", "1");
  for (const std::string& alias : {std::string("-1"), std::string("-5"),
                                   std::string("-5.9")}) {
    require(play_reference_first_move(reference, alias, "1") == three_one,
            "negative X alias " + alias + " should normalize to X=3");
  }
  require(play_reference_first_move(reference, "0", "1") ==
              play_reference_first_move(reference, "4", "1"),
          "zero X should normalize to X=4");
  require(play_reference_first_move(reference, "1", "-5") ==
              play_reference_first_move(reference, "1", "3"),
          "negative Y should use the same signed modulo normalization");

  emulator::MK61 retry = boot_reference_game(reference);
  enter_x(retry, "3", "canonical occupied-alias setup X phase");
  enter_y_and_run(retry, "1", "canonical occupied-alias setup Y phase");
  enter_x(retry, "-5", "occupied negative-alias X phase");
  enter_y_and_run(retry, "1", "occupied negative-alias Y phase");
  require(read_integer(retry, "x") == -99999999 && read_integer(retry, "y") == 1,
          "an occupied normalized alias should expose retry pair X=-99999999, Y=raw input Y");
}

void tic_tac_toe_4x4_source_uses_reference_angle_mode() {
  const std::filesystem::path source =
      fixture_root() / "examples" / "pending-optimizer" / "tic-tac-toe-4x4.mkpro";
  const std::string text = read_text(source);
  require(text.find("expected_mode_only(\"deg\")") != std::string::npos,
          "the executable 105-cell reference requires switch position Г (DEG), not ГРД");

  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.disable_candidate_search = true;
  const CompileResult compiled = compile_source(text, options);
  require(compiled.implemented && compiled.diagnostics.empty(),
          "entered() source contract should compile through the generic pipeline");
  require(compiled.interaction_protocols.size() == 1U &&
              compiled.interaction_protocols.front().phases.size() == 2U &&
              !compiled.interaction_protocols.front().phases.at(0).admitted_domain.known() &&
              !compiled.interaction_protocols.front().phases.at(1).admitted_domain.known(),
          "source should expose two generic manual-input phases with an unknown admitted domain");
}
} // namespace mkpro::tests
