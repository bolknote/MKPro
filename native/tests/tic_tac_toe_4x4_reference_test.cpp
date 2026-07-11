#include "mkpro/core/opcodes.hpp"
#include "mkpro/compiler.hpp"
#include "mkpro/core/compiler_static_proof_gate.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
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

void enter_x(emulator::MK61& calc, const std::string& x, std::string_view context) {
  calc.input_number(x, true);
  calc.press("ПП");
  run_to_stop(calc, context);
  require(calc.program_counter() == "05",
          std::string(context) + " should execute only X->П 3 and stop at PC 05");
  require(read_integer(calc, "3") == std::stoi(x),
          std::string(context) + " should store the entered X coordinate in R3");
}

void enter_y_and_run(emulator::MK61& calc, const std::string& y, std::string_view context) {
  calc.input_number(y, true);
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

std::string replace_identifier_tokens(
    const std::string& source, const std::map<std::string, std::string>& replacements) {
  std::string result;
  for (std::size_t cursor = 0; cursor < source.size();) {
    const unsigned char ch = static_cast<unsigned char>(source.at(cursor));
    if (std::isalpha(ch) == 0 && source.at(cursor) != '_') {
      result.push_back(source.at(cursor++));
      continue;
    }
    std::size_t end = cursor + 1U;
    while (end < source.size()) {
      const unsigned char next = static_cast<unsigned char>(source.at(end));
      if (std::isalnum(next) == 0 && source.at(end) != '_')
        break;
      ++end;
    }
    const std::string token = source.substr(cursor, end - cursor);
    const auto replacement = replacements.find(token);
    result += replacement == replacements.end() ? token : replacement->second;
    cursor = end;
  }
  return result;
}

bool has_optimization(const CompileResult& result, std::string_view name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

std::filesystem::path fixture_root() {
  const std::filesystem::path current = std::filesystem::current_path();
  if (std::filesystem::exists(current / "games" / "tic-tac-toe-4x4.txt"))
    return current;
  if (std::filesystem::exists(current.parent_path() / "games" / "tic-tac-toe-4x4.txt"))
    return current.parent_path();
  throw std::runtime_error("cannot locate the MK-Pro fixture root from " + current.string());
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
  require(calc.program_counter() == "04" && display_integer(calc) == 0,
          "cold start should show zero and stop at the first coordinate store");

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
              read_integer(calc, "3") == -99999999,
          "re-entering occupied cell 1:1 should show the retry sentinel");
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

void tic_tac_toe_4x4_source_uses_reference_angle_mode() {
  const std::filesystem::path source =
      fixture_root() / "examples" / "pending-optimizer" / "tic-tac-toe-4x4.mkpro";
  require(read_text(source).find("expected_mode_only(\"deg\")") != std::string::npos,
          "the executable 105-cell reference requires switch position Г (DEG), not ГРД");
}
void tic_tac_toe_4x4_generic_optimizer_checkpoint_is_name_independent() {
  const std::filesystem::path root = fixture_root();
  const std::string source =
      read_text(root / "examples" / "pending-optimizer" / "tic-tac-toe-4x4.mkpro");

  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.disable_candidate_search = true;
  options.canonicalize_packed_line_bank_walks = true;
  options.packed_line_family_mutating_selector_update_check_tail = true;
  options.stack_resident_temps = true;
  options.joint_packed_line_family_walk = true;
  const CompileResult compiled = compile_source(source, options);
  require(compiled.implemented && compiled.diagnostics.empty(),
          "forced reusable packed-line pipeline should compile the source");
  require(compiled.steps.size() == 121U,
          "generic checkpoint after removing the whole-program recognizer should be 121 cells, "
          "got " +
              std::to_string(compiled.steps.size()));
  require(has_optimization(compiled, "joint-packed-line-family-walk"),
          "checkpoint should expose the reusable joint packed-line walk");
  const std::optional<std::string> compiled_rejection =
      optimizer_static_proof_gate_rejection_reason_for_testing(options, compiled);
  require(!compiled_rejection.has_value(),
          "basic generic candidate should carry a valid final selector proof: " +
              compiled_rejection.value_or("accepted"));

  CompileOptions composed_options = options;
  composed_options.dual_use_constant_indirect_flow = true;
  composed_options.tail_branch_inversion = true;
  composed_options.proc_layout_strategy = "reverse";
  const CompileResult composed = compile_source(source, composed_options);
  require(composed.implemented && composed.steps.size() == 120U,
          "generic joint/dual-use/reverse composition should occupy 120 cells, got " +
              std::to_string(composed.steps.size()));
  const std::optional<std::string> composed_rejection =
      optimizer_static_proof_gate_rejection_reason_for_testing(composed_options, composed);
  require(!composed_rejection.has_value(),
          "composed generic candidate should carry a valid final selector proof: " +
              composed_rejection.value_or("accepted"));

  const std::string alpha = replace_identifier_tokens(
      source,
      {{"TicTacToe4x4", "QuartetReplyProbe"},
       {"grid", "arena"},
       {"occupied", "ledger_mask"},
       {"lines", "bands"},
       {"best_y", "answer_row"},
       {"best_score", "peak"},
       {"score", "merit"},
       {"line", "band_index"},
       {"slot", "cursor"},
       {"draw", "stalemate"},
       {"occupied_cell", "reject_taken"},
       {"mark_one", "touch_band"},
       {"mark_lines_and_check", "sweep_quartet"},
       {"choose_calculator_move", "select_reply"},
       {"candidate_score", "rate_candidate"},
       {"normalize", "fold_axis"},
       {"mark_sign", "impulse"},
       {"raw_line", "raw_axis"},
       {"report", "alarm"},
       {"x", "axis_p"},
       {"y", "axis_q"}});
  const CompileResult alpha_compiled = compile_source(alpha, options);
  require(alpha_compiled.implemented && alpha_compiled.steps.size() == 121U &&
              has_optimization(alpha_compiled, "joint-packed-line-family-walk"),
          "alpha-renamed source should keep the same checkpoint through reusable passes");
  const CompileResult alpha_composed = compile_source(alpha, composed_options);
  require(alpha_composed.implemented && alpha_composed.steps.size() == 120U &&
              optimizer_static_proof_gate_accepts_for_testing(composed_options, alpha_composed),
          "alpha-renamed source should keep the same proved composed layout");
}

} // namespace mkpro::tests
