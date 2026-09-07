#include "mkpro/core/opcodes.hpp"
#include "mkpro/compiler.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
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

std::string mk61_hex_literal(const std::string& text) {
  std::string result;
  for (char ch : text) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))) {
    case 'A':
      result.push_back('-');
      break;
    case 'B':
      result.push_back('L');
      break;
    case 'C':
      result += "С";
      break;
    case 'D':
      result += "Г";
      break;
    case 'E':
      result += "Е";
      break;
    case 'F':
      result.push_back('_');
      break;
    default:
      result.push_back(ch);
      break;
    }
  }
  return result;
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

std::vector<int> step_opcodes(const std::vector<ResolvedStep>& steps) {
  std::vector<int> codes;
  codes.reserve(steps.size());
  for (const ResolvedStep& step : steps)
    codes.push_back(step.opcode);
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
  if (std::filesystem::exists(current / "games" / "logic" / "tic-tac-toe-4x4.txt"))
    return current;
  if (std::filesystem::exists(current.parent_path() / "games" / "logic" /
                              "tic-tac-toe-4x4.txt"))
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

constexpr std::string_view kManualUiProbe = R"mkpro(
program ManualUiProbe {
  state {
    last_pair: packed = 0
    x: packed = 0
    y: packed = 0
    best_score: packed = 0
  }

  loop {
    preview(y)
    show(x)
    x = entered()
    y = entered()
    y = grid_norm(y)
    x = grid_norm(x)
    best_score = x * 10 + y
    if last_pair == best_score {
      x = -99999999
    }
    else {
      last_pair = best_score
    }
  }
}
)mkpro";

struct ManualProbeLayout {
  std::size_t prompt_item = 0;
  std::size_t first_phase_item = 0;
  std::size_t second_phase_item = 0;
  int prompt_address = -1;
  int first_phase_address = -1;
  int second_phase_address = -1;
};

int machine_item_address(const std::vector<MachineItem>& items, std::size_t target) {
  return static_cast<int>(
      std::count_if(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(target),
                    [](const MachineItem& item) { return item.kind != MachineItemKind::Label; }));
}

ManualProbeLayout require_two_phase_probe_layout(const CompileResult& compiled) {
  require(compiled.interaction_protocols.size() == 1U,
          "manual UI probe should expose exactly one input protocol");
  const ManualInteractionProtocolFact& protocol = compiled.interaction_protocols.front();
  require(protocol.phases.size() == 2U && protocol.phases.at(0).target == "x" &&
              protocol.phases.at(1).target == "y" &&
              !protocol.phases.at(0).admitted_domain.known() &&
              !protocol.phases.at(1).admitted_domain.known(),
          "manual UI probe should retain two unbounded logical input phases");

  std::optional<std::size_t> prompt;
  std::optional<std::size_t> first;
  std::optional<std::size_t> second;
  for (std::size_t item_index = 0; item_index < compiled.items.size(); ++item_index) {
    const MachineItem& item = compiled.items.at(item_index);
    if (!item.manual_interaction.has_value() ||
        item.manual_interaction->protocol_id != protocol.protocol_id) {
      continue;
    }
    const ManualInteractionAnchor& anchor = *item.manual_interaction;
    if (anchor.kind == ManualInteractionAnchorKind::PromptStop) {
      require(!prompt.has_value(), "manual UI probe should have one typed prompt");
      prompt = item_index;
    } else if (anchor.phase == 0) {
      require(!first.has_value(), "manual UI probe should have one first input phase");
      first = item_index;
    } else if (anchor.phase == 1) {
      require(!second.has_value(), "manual UI probe should have one second input phase");
      second = item_index;
    }
  }
  require(prompt.has_value() && first.has_value() && second.has_value(),
          "manual UI probe should retain prompt and both phase identities after lowering");

  const MachineItem& prompt_item = compiled.items.at(*prompt);
  const MachineItem& first_item = compiled.items.at(*first);
  const MachineItem& second_item = compiled.items.at(*second);
  require(prompt_item.kind == MachineItemKind::Op && prompt_item.opcode == 0x50 &&
              prompt_item.stop_disposition == StopDisposition::Resumable &&
              prompt_item.manual_interaction->phase == -1,
          "manual UI prompt should remain a typed resumable STOP");
  require(first_item.kind == MachineItemKind::Op &&
              first_item.manual_interaction->kind == ManualInteractionAnchorKind::SingleStepCommand,
          "the PP phase should remain one typed single-step command");
  require(second_item.kind == MachineItemKind::Op &&
              second_item.manual_interaction->kind == ManualInteractionAnchorKind::ContinuousResume,
          "the final C/P phase may begin at any proved current-X consumer");

  const ManualProbeLayout layout{
      .prompt_item = *prompt,
      .first_phase_item = *first,
      .second_phase_item = *second,
      .prompt_address = machine_item_address(compiled.items, *prompt),
      .first_phase_address = machine_item_address(compiled.items, *first),
      .second_phase_address = machine_item_address(compiled.items, *second),
  };
  require(layout.first_phase_address == layout.prompt_address + 1 &&
              layout.second_phase_address == layout.first_phase_address + 1,
          "manual UI prompt and phases should occupy consecutive command cells");

  const core::AuthoritativePostLayoutControlFlow control =
      core::build_post_layout_control_flow(compiled.items);
  require(control.proved && control.reasons.empty(),
          "manual UI probe should retain authoritative post-layout flow");
  const auto has_phase_entry = [&](core::ExternalEntryKind kind, int phase) {
    return std::any_of(control.external_entries.begin(), control.external_entries.end(),
                       [&](const core::PostLayoutExternalEntryState& entry) {
                         return entry.kind == kind && entry.manual_interaction.has_value() &&
                                entry.manual_interaction->protocol_id == protocol.protocol_id &&
                                entry.manual_interaction->phase == phase;
                       });
  };
  require(has_phase_entry(core::ExternalEntryKind::ManualSingleStep, 0) &&
              has_phase_entry(core::ExternalEntryKind::ManualContinuous, 1),
          "manual UI probe should admit the exact PP and C/P external entries");
  return layout;
}

void require_manual_probe_observation(emulator::MK61& calc, int expected_x, int expected_y,
                                      std::string_view context) {
  require(display_integer(calc) == expected_x && read_integer(calc, "x") == expected_x &&
              read_integer(calc, "y") == expected_y,
          std::string(context) + " should expose X:Y=" + std::to_string(expected_x) + ":" +
              std::to_string(expected_y));
}

void enter_manual_probe_pair(emulator::MK61& calc, const ManualProbeLayout& layout,
                             const std::string& x, const std::string& y, int expected_x,
                             int expected_y, std::string_view context) {
  input_ui_number(calc, x);
  calc.press("ПП");
  run_to_stop(calc, std::string(context) + " X phase");
  require(calc.program_counter() == format_address(layout.second_phase_address),
          std::string(context) + " PP should execute only the first input command");

  input_ui_number(calc, y);
  calc.press("С/П");
  run_to_stop(calc, std::string(context) + " Y phase");
  require(calc.program_counter() == format_address(layout.first_phase_address),
          std::string(context) + " C/P should return to the same prompt protocol");
  require_manual_probe_observation(calc, expected_x, expected_y, context);
}

} // namespace

void tic_tac_toe_4x4_reference_transcript_matches_original_listing() {
  const std::filesystem::path root = fixture_root();
  const std::vector<int> reference =
      parse_reference_listing(root / "games" / "logic" / "tic-tac-toe-4x4.txt");

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
      parse_reference_listing(root / "games" / "logic" / "tic-tac-toe-4x4.txt");

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

  emulator::MK61 retry_y_alias = boot_reference_game(reference);
  enter_x(retry_y_alias, "3", "canonical occupied-Y-alias setup X phase");
  enter_y_and_run(retry_y_alias, "1", "canonical occupied-Y-alias setup Y phase");
  enter_x(retry_y_alias, "-5.9", "occupied signed/fractional alias X phase");
  enter_y_and_run(retry_y_alias, "5.9", "occupied signed/fractional alias Y phase");
  require(read_integer(retry_y_alias, "x") == -99999999 && read_integer(retry_y_alias, "y") == 1,
          "occupied signed/fractional aliases should expose the retry sentinel with normalized Y");
}

void tic_tac_toe_4x4_manual_ui_contract_probe_matches_emulator() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.disable_candidate_search = true;
  const CompileResult compiled = compile_source(std::string(kManualUiProbe), options);
  require(compiled.implemented && compiled.diagnostics.empty(),
          "generic manual UI contract probe should compile without diagnostics");
  require(!compiled.steps.empty() && compiled.steps.size() <= 105U,
          "generic manual UI contract probe should remain loadable in at most 105 cells");
  const ManualProbeLayout layout = require_two_phase_probe_layout(compiled);

  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(compiled.steps));
  require(loaded.diagnostics.empty(),
          "generic manual UI contract probe should load without truncation");
  for (const PreloadReport& preload : compiled.preloads)
    calc.set_register(preload.register_name, mk61_hex_literal(preload.value));

  calc.press_sequence({"В/О", "С/П"});
  run_to_stop(calc, "generic manual UI cold prompt");
  require(calc.program_counter() == format_address(layout.first_phase_address),
          "cold prompt should wait at the first PP input phase");
  require_manual_probe_observation(calc, 0, 0, "cold prompt");

  enter_manual_probe_pair(calc, layout, "-1.9", "5.9", 3, 1, "signed fractional pair");
  enter_manual_probe_pair(calc, layout, "7.8", "1.2", -99999999, 1, "normalized-alias retry");
  enter_manual_probe_pair(calc, layout, "5.9", "-2.9", 1, 2, "accepted pair after retry");
  enter_manual_probe_pair(calc, layout, "0", "-4.9", 4, 4,
                          "zero and negative-multiple boundary pair");
  enter_manual_probe_pair(calc, layout, "4.8", "0", -99999999, 4,
                          "zero boundary normalized-alias retry");
}

void tic_tac_toe_4x4_source_manual_ui_contract_is_explicit() {
  const std::filesystem::path source =
      fixture_root() / "examples" / "pending-optimizer" / "tic-tac-toe-4x4.mkpro";
  const std::string text = read_text(source);
  require(text.find("y = entered()") != std::string::npos &&
              text.find("second interactive") != std::string::npos,
          "the high-level port must keep the second Y input phase explicit even when "
          "lowering forwards its current X without a physical register store");
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.disable_candidate_search = true;
  options.packed_score_accumulator_helpers = true;
  options.proc_layout_strategy = "reverse";
  const CompileResult compiled = compile_source(text, options);
  require(compiled.implemented && compiled.diagnostics.empty(),
          "full source UI contract should compile through generic lowering");
  // The playable source's size has its own baseline. This contract checks the
  // original input protocol even when correcting game semantics changes size;
  // frozen structural fixtures retain the older optimization-specific bounds.
  require(std::any_of(compiled.preloads.begin(), compiled.preloads.end(),
                      [](const PreloadReport& preload) {
                        return preload.retunable_natural_fractional_prefix == "0.226000";
                      }),
          "cell-mask lowering should preserve its retunable fractional-selector preload proof");

  const auto packed_report = [](const std::string& value,
                                const std::string& mask) {
    emulator::MK61 calc;
    const emulator::ProgramLoadResult loaded =
        calc.load_program({0x64, 0x6c, 0x37, 0x35, 0x50});
    require(loaded.diagnostics.empty(),
            "packed-mask selector probe should load without diagnostics");
    calc.set_register("4", value);
    calc.set_register("c", mask);
    calc.press_sequence({"В/О", "С/П"});
    run_to_stop(calc, "packed-mask selector probe");
    return compact(calc.display_text());
  };
  for (const std::string_view packed_line : {
           "44444.4", "84444.4", "48444.4", "44844.4", "44484.4",
           "44448.4", "88888.8", "00000.0",
       }) {
    require(packed_report(std::string(packed_line), "88888834") ==
                packed_report(std::string(packed_line), "88888819"),
            "retuning the address suffix of a six-digit packed report mask must preserve "
            "the emulator-visible fractional report");
  }
  require(compiled.interaction_protocols.size() == 1U &&
              compiled.interaction_protocols.front().phases.size() == 2U &&
              !compiled.interaction_protocols.front().phases.at(0).admitted_domain.known() &&
              !compiled.interaction_protocols.front().phases.at(1).admitted_domain.known(),
          "full source should retain two unbounded logical input phases without pinning registers");

  const int protocol_id = compiled.interaction_protocols.front().protocol_id;
  const MachineItem* prompt = nullptr;
  const MachineItem* first = nullptr;
  const MachineItem* second = nullptr;
  int prompt_count = 0;
  int first_count = 0;
  int second_count = 0;
  for (std::size_t item_index = 0; item_index < compiled.items.size(); ++item_index) {
    const MachineItem& item = compiled.items.at(item_index);
    if (!item.manual_interaction.has_value() ||
        item.manual_interaction->protocol_id != protocol_id) {
      continue;
    }
    const ManualInteractionAnchor& anchor = *item.manual_interaction;
    if (anchor.kind == ManualInteractionAnchorKind::PromptStop) {
      prompt = &item;
      ++prompt_count;
    } else if (anchor.phase == 0) {
      first = &item;
      ++first_count;
    } else if (anchor.phase == 1) {
      second = &item;
      ++second_count;
    }
  }
  require(prompt_count == 1 && prompt != nullptr && prompt->kind == MachineItemKind::Op &&
              prompt->opcode == 0x50 && prompt->stop_disposition == StopDisposition::Resumable,
          "full source should retain one typed resumable UI prompt");
  require(first_count == 1 && first != nullptr && first->kind == MachineItemKind::Op &&
              first->manual_interaction->kind == ManualInteractionAnchorKind::SingleStepCommand,
          "full source should retain one typed PP single-step phase");
  require(second_count == 1 && second != nullptr && second->kind == MachineItemKind::Op &&
              second->manual_interaction->kind == ManualInteractionAnchorKind::ContinuousResume,
          "full source should retain one typed continuous C/P phase");

  const auto mask_assignment = text.find("    line = cell_mask(x, y)");
  require(mask_assignment != std::string::npos, "input phase must compute its cell mask");
  const auto guard_start = text.find("    if ", mask_assignment);
  const auto guard_end = text.find('{', guard_start);
  require(guard_start != std::string::npos && guard_end != std::string::npos,
          "input phase must retain its occupied-cell guard");
  const std::string update_and_guard = text.substr(mask_assignment, guard_end - mask_assignment);
  const auto original_listing = parse_reference_listing(fixture_root() / "games/logic/tic-tac-toe-4x4.txt");
  std::vector<int> original_update(original_listing.begin() + 39, original_listing.begin() + 52);
  original_update.push_back(0x50);
  struct Query { std::string x; std::string y; std::string occupied; int expected; };
  for (const auto& query : std::array<Query, 6>{{
           {"1", "1", "0", 0}, {"1", "1", "8.104", 1}, {"3", "3", "8.104", 1},
           {"4", "4", "8.104", 0}, {"1", "3.2", "8.1", 0}, {"2.8", "1", "8.1", 0},
       }}) {
    const std::string fixture =
        "program OccupancyContract {\n"
        "  grid: board(1..4, 1..4)\n"
        "  state {\n"
        "    x: packed = " + query.x + "\n"
        "    y: packed = " + query.y + "\n"
        "    occupied: packed\n"
        "    previous_occupied: packed\n"
        "    line: packed\n"
        "  }\n"
        "  occupied = entered()\n"
        + update_and_guard +
        "{\n show(1)\n }\n else {\n show(0)\n }\n halt(occupied)\n}\n";
    const auto membership = compile_source(fixture, options);
    require(membership.implemented && membership.diagnostics.empty() && membership.steps.size() <= 105U,
            "actual occupied-cell guard must compile within real emulator memory");
    emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
    require(calc.load_program(step_opcodes(membership.steps)).diagnostics.empty(),
            "actual occupied-cell guard must load completely");
    for (const auto& preload : membership.preloads)
      calc.set_register(preload.register_name, preload.value);
    calc.press("В/О").set_register("X", query.occupied).press("С/П");
    run_to_stop(calc, "actual occupied-cell guard");
    require(display_integer(calc) == query.expected,
            "occupied-cell guard must compare old and updated values, including partial masks");
    emulator::MK61 original({.extended = true, .angle_mode = "deg"});
    original.load_program(original_update);
    original.set_register("9", query.occupied).set_register("1", query.x)
        .set_register("2", query.y).set_register("c", "2.2600029E-1");
    original.press_sequence({"В/О", "С/П"}); run_to_stop(original, "original occupied-cell update");
    require((compact(original.display_text()) == "0,") == (query.expected == 1),
            "occupied response must follow the original before/after comparison");
    calc.press("С/П"); run_to_stop(calc, "occupied-cell committed update");
    require(compact(calc.display_text()) == compact(original.read_register("9")),
            "occupied-cell update must match the original raw packed word on every branch");
  }
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

namespace mkpro::tests {
namespace {

std::string response_contract_rule(const std::string& source, const std::string& name) {
  const auto start = source.find("fn " + name + "(");
  require(start != std::string::npos, "response contract is missing rule " + name);
  const auto body = source.find('{', start);
  require(body != std::string::npos, "response contract rule has no body: " + name);
  unsigned depth = 0;
  for (std::size_t end = body; end < source.size(); ++end) {
    if (source[end] == '{') ++depth;
    if (source[end] == '}' && --depth == 0)
      return source.substr(start, end - start + 1U) + "\n";
  }
  throw std::runtime_error("unclosed response contract rule " + name);
}

std::string response_contract_ui(const std::string& source) {
  const auto loop = source.find("  loop {");
  require(loop != std::string::npos, "response contract needs the source UI loop");
  const auto first_input = source.find("    x = entered()", loop);
  require(first_input != std::string::npos, "response contract needs the explicit X input phase");
  return source.substr(loop + std::string("  loop {").size(),
                       first_input - loop - std::string("  loop {").size());
}

emulator::MK61 compile_response_contract(const std::string& source,
                                        const std::array<std::string, 4>& banks,
                                        int x, int y, bool occupied) {
  std::string fields;
  for (const auto& bank : banks) {
    if (!fields.empty()) fields += ", ";
    fields += bank;
  }
  std::string fixture =
      "program ResponseContract {\n"
      "  grid: board(1..4, 1..4)\n"
      "  state {\n"
      "    expected_mode_only(\"deg\")\n"
      "    lines: packed[4..7] = [" + fields + "]\n"
      "    x: packed = " + std::to_string(x) + "\n"
      "    y: packed = " + std::to_string(y) + "\n"
      "    display_x: packed = 0\n"
      "    best_score: packed\n"
      "    line: packed\n"
      "    slot: packed\n"
      "    report: packed\n"
      "  }\n";
  fixture += occupied ? "  occupied_cell()\n" : "  mark_lines_and_check(1)\n";
  fixture += response_contract_ui(source);
  // Keep the actual coordinate observable after the response stop. It must
  // not have been overwritten by either the retry sentinel or the win report.
  fixture += "  halt(x)\n";
  for (const std::string& name : {"mark_one", "mark_lines_and_check", "normalize", "occupied_cell"})
    fixture += response_contract_rule(source, name);
  fixture += "}\n";

  CompileOptions options;
  options.analysis = true;
  options.budget = 105;
  options.disable_candidate_search = true;
  options.hoist_procs = true;
  options.x_param_value_functions = true;
  const auto result = compile_source(fixture, options);
  std::string diagnostics;
  for (const auto& diagnostic : result.diagnostics)
    diagnostics += diagnostic.message + "; ";
  require(result.implemented && result.diagnostics.empty(),
          std::string(occupied ? "occupied" : "winning") +
              " source response closure must compile: " + diagnostics);
  require(result.steps.size() <= 105U,
          "source response closure must fit real emulator memory: " +
              std::to_string(result.steps.size()));
  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  require(calc.load_program(step_opcodes(result.steps)).diagnostics.empty(),
          "source response closure must load completely");
  for (const auto& preload : result.preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  run_to_stop(calc, "source response closure");
  return calc;
}

} // namespace

void tic_tac_toe_4x4_source_responses_match_original_listing() {
  const auto root = fixture_root();
  const auto reference = parse_reference_listing(root / "games/logic/tic-tac-toe-4x4.txt");
  const auto source = read_text(root / "examples/pending-optimizer/tic-tac-toe-4x4.mkpro");

  const std::array<std::array<std::string, 4>, 2> positions = {{
      {"43543.4", "43444.4", "45532.4", "44227.4"},
      {"44444.4", "44444.4", "44474.4", "44447.4"},
  }};
  for (std::size_t position = 0; position < positions.size(); ++position) {
    auto original = boot_reference_game(reference);
    for (std::size_t bank = 0; bank < 4U; ++bank)
      original.set_register(std::to_string(bank + 4U), positions[position][bank]);
    original.set_register("9", position == 0 ? mk61_hex_literal("8.E33") : "0");
    original.set_register("a", "-ГE-2").set_register("0", "1");
    original.press_sequence({"БП", "0", "7", "С/П"});
    run_to_stop(original, "original winning response");
    require(original.program_counter() == "04", "original win must reach its ordinary UI stop");
    require(compact(original.display_text()).starts_with("8,"),
            "the reference position must produce a winning report");
    const int x = read_integer(original, "1");
    const int y = read_integer(original, "2");
    auto translated = compile_response_contract(source, positions[position], x, y, false);
    require(compact(translated.display_text()) == compact(original.display_text()),
            "winning response must be the original fractional report, not the full bit mask: " +
                compact(translated.display_text()) + " != " + compact(original.display_text()));
    require(compact(translated.read_register("Y")) == compact(original.read_register("Y")),
            "winning response must preserve the original Y coordinate");
    for (std::size_t bank = 4; bank <= 7; ++bank) {
      require(compact(translated.read_register(std::to_string(bank))) ==
                  compact(original.read_register(std::to_string(bank))),
              "winning response must finish every line bank, including R" + std::to_string(bank));
    }
    translated.press("С/П");
    run_to_stop(translated, "coordinate after winning response");
    require(display_integer(translated) == x, "winning report must not overwrite the X coordinate");
  }

  auto original = boot_reference_game(reference);
  enter_x(original, "1", "first reference X");
  enter_y_and_run(original, "1", "first reference Y");
  enter_x(original, "1", "occupied reference X");
  enter_y_and_run(original, "1", "occupied reference Y");
  const std::array<std::string, 4> neutral = {"44444.4", "44444.4", "44444.4", "44444.4"};
  const int x = read_integer(original, "1");
  auto translated = compile_response_contract(source, neutral, x, read_integer(original, "2"), true);
  require(compact(translated.display_text()) == compact(original.display_text()),
          "occupied response must preserve the reference sentinel");
  require(compact(translated.read_register("Y")) == compact(original.read_register("Y")),
          "occupied response must preserve the reference Y coordinate");
  translated.press("С/П");
  run_to_stop(translated, "coordinate after occupied response");
  require(display_integer(translated) == x, "occupied sentinel must not overwrite the X coordinate");
}

void tic_tac_toe_4x4_source_scores_match_original_listing() {
  const auto root = fixture_root();
  const auto reference = parse_reference_listing(root / "games/logic/tic-tac-toe-4x4.txt");
  const auto source = read_text(root / "examples/pending-optimizer/tic-tac-toe-4x4.mkpro");
  const std::array<std::array<std::string, 4>, 3> positions = {{
      {"44444.4", "44444.4", "44444.4", "44444.4"},
      {"44444.4", "44444.4", "44543.4", "44543.4"},
      {"43543.4", "43444.4", "45532.4", "44227.4"},
  }};
  for (const auto& banks : positions) {
    std::string fields;
    for (const auto& bank : banks) {
      if (!fields.empty()) fields += ", ";
      fields += bank;
    }
    for (int x = 1; x <= 4; ++x) {
      for (int y = 1; y <= 4; ++y) {
        auto original = boot_reference_game(reference);
        for (std::size_t bank = 0; bank < 4U; ++bank)
          original.set_register(std::to_string(bank + 4U), banks[bank]);
        original.set_register("1", std::to_string(x)).set_register("2", std::to_string(y));
        // Enter the original shared traversal with its original score seed.
        // Its empty return reaches 01; the two UI recalls move the score to Z.
        original.input_number("98", true).press_sequence({"БП", "5", "8", "С/П"});
        run_to_stop(original, "original candidate score");
        require(original.program_counter() == "04", "reference score must return through the UI");

        const std::string fixture =
            "program ScoreContract {\n"
            "  grid: board(1..4, 1..4)\n"
            "  state {\n"
            "    expected_mode_only(\"deg\")\n"
            "    lines: packed[4..7] = [" + fields + "]\n"
            "    x: packed = " + std::to_string(x) + "\n"
            "    y: packed = " + std::to_string(y) + "\n"
            "    score: packed\n"
            "    line: packed\n"
            "  }\n"
            "  candidate_score()\n"
            "  halt(score)\n" +
            response_contract_rule(source, "candidate_score") +
            response_contract_rule(source, "normalize") + "}\n";
        CompileOptions options;
        options.analysis = true;
        options.budget = 105;
        options.disable_candidate_search = true;
        options.hoist_procs = true;
        options.x_param_value_functions = true;
        const auto result = compile_source(fixture, options);
        std::string diagnostics;
        for (const auto& diagnostic : result.diagnostics)
          diagnostics += diagnostic.message + "; ";
        require(result.implemented && result.diagnostics.empty(),
                "actual source score closure must compile: " + diagnostics);
        require(result.steps.size() <= 105U, "source score closure must fit the real emulator");
        emulator::MK61 translated({.extended = true, .angle_mode = "deg"});
        require(translated.load_program(step_opcodes(result.steps)).diagnostics.empty(),
                "source score closure must load completely");
        for (const auto& preload : result.preloads)
          translated.set_register(preload.register_name, preload.value);
        translated.press_sequence({"В/О", "С/П"});
        run_to_stop(translated, "source candidate score");
        require(compact(translated.display_text()) == compact(original.read_register("Z")),
                "candidate score must preserve the original seed and arithmetic order at " +
                    std::to_string(x) + ":" + std::to_string(y) + ": " +
                    compact(translated.display_text()) + " != " +
                    compact(original.read_register("Z")));
      }
    }
  }
}

void tic_tac_toe_4x4_source_moves_match_original_listing() {
  const auto root = fixture_root();
  const auto reference = parse_reference_listing(root / "games/logic/tic-tac-toe-4x4.txt");
  const auto source = read_text(root / "examples/pending-optimizer/tic-tac-toe-4x4.mkpro");
  struct Position {
    std::array<std::string, 4> banks;
    std::string occupied;
  };
  const std::array<Position, 3> positions = {{
      {{"44444.4", "44444.4", "44444.4", "44444.4"}, "0"},
      {{"44444.4", "44444.4", "44543.4", "44543.4"}, "8.104"},
      {{"43543.4", "43444.4", "45532.4", "44227.4"}, "8.E33"},
  }};
  for (const auto& position : positions) {
    auto original = boot_reference_game(reference);
    std::string fields;
    for (std::size_t bank = 0; bank < 4U; ++bank) {
      original.set_register(std::to_string(bank + 4U), position.banks[bank]);
      if (!fields.empty()) fields += ", ";
      fields += position.banks[bank];
    }
    const auto occupied = mk61_hex_literal(position.occupied);
    original.set_register("9", occupied).set_register("a", "-ГE-2").set_register("0", "1");
    original.press_sequence({"БП", "0", "7", "С/П"});
    run_to_stop(original, "original move selection");

    const std::string fixture =
        "program MoveContract {\n"
        "  grid: board(1..4, 1..4)\n"
        "  state {\n"
        "    expected_mode_only(\"deg\")\n"
        "    lines: packed[4..7] = [" + fields + "]\n"
        "    occupied: packed\n"
        "    x: packed\n"
        "    y: packed\n"
        "    best_y: packed = 0\n"
        "    best_score: packed = -1\n"
        "    score: packed\n"
        "    line: packed\n"
        "    slot: packed\n"
        "  }\n"
        "  occupied = entered()\n"
        "  choose_calculator_move()\n"
        "  preview(y)\n"
        "  halt(x)\n" +
        response_contract_rule(source, "choose_calculator_move") +
        response_contract_rule(source, "candidate_score") +
        response_contract_rule(source, "normalize") +
        response_contract_rule(source, "draw") + "}\n";
    CompileOptions options;
    options.analysis = true;
    options.budget = 105;
    options.disable_candidate_search = true;
    options.hoist_procs = true;
    options.x_param_value_functions = true;
    options.packed_score_accumulator_helpers = true;
    const auto result = compile_source(fixture, options);
    std::string diagnostics;
    for (const auto& diagnostic : result.diagnostics) diagnostics += diagnostic.message + "; ";
    require(result.implemented && result.diagnostics.empty(),
            "actual move-selection closure must compile: " + diagnostics);
    require(result.steps.size() <= 105U,
            "actual move-selection closure must fit the real emulator: " +
                std::to_string(result.steps.size()));
    emulator::MK61 translated({.extended = true, .angle_mode = "deg"});
    require(translated.load_program(step_opcodes(result.steps)).diagnostics.empty(),
            "actual move-selection closure must load completely");
    for (const auto& preload : result.preloads)
      translated.set_register(preload.register_name, preload.value);
    translated.press("В/О").set_register("X", occupied).press("С/П");
    run_to_stop(translated, "source move selection");
    require(display_integer(translated) == read_integer(original, "1") &&
                read_integer(translated, "Y") == read_integer(original, "2"),
            "source move selection and tie breaking must match the original for " + position.occupied + ": " +
                compact(translated.display_text()) + ":" + compact(translated.read_register("Y")) +
                " != " + compact(original.read_register("1")) + ":" +
                compact(original.read_register("2")));
  }
}

} // namespace mkpro::tests
