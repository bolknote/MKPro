#include "mkpro/compiler.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
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
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  return value;
}

std::optional<int> compact_register_opcode(std::string_view mnemonic) {
  const auto with_register =
      [&](std::string_view prefix, int base) -> std::optional<int> {
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

int parse_opcode(const std::string& mnemonic, const std::string& context) {
  if (const OpcodeInfo* opcode = find_opcode_name(mnemonic))
    return opcode->code;
  if (const std::optional<int> opcode = compact_register_opcode(mnemonic))
    return *opcode;
  throw std::runtime_error(context + ": unknown MK-61 mnemonic " + mnemonic);
}

std::vector<int> parse_reference_listing(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read Nekromant listing: " + path.string());
  std::vector<int> opcodes;
  std::string line;
  int source_line = 0;
  while (std::getline(input, line)) {
    ++source_line;
    if (trim_ascii(line).empty())
      continue;
    const std::size_t separator = line.find('\t');
    require(separator != std::string::npos,
            "Nekromant listing line should contain address and mnemonic");
    const std::string address = trim_ascii(line.substr(0, separator));
    const std::string mnemonic = trim_ascii(line.substr(separator + 1U));
    require(address == format_address(static_cast<int>(opcodes.size())),
            "Nekromant listing should be contiguous at source line " +
                std::to_string(source_line));
    opcodes.push_back(
        parse_opcode(mnemonic,
                     path.string() + ":" + std::to_string(source_line)));
  }
  require(opcodes.size() == 105U,
          "Nekromant reference should occupy addresses 00..A4");
  return opcodes;
}

std::filesystem::path fixture_root() {
  const std::filesystem::path current = std::filesystem::current_path();
  if (std::filesystem::exists(current / "games" / "adventure" / "nekromant.txt"))
    return current;
  if (std::filesystem::exists(current.parent_path() / "games" / "adventure" /
                              "nekromant.txt"))
    return current.parent_path();
  throw std::runtime_error("cannot locate Nekromant fixtures from " +
                           current.string());
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read fixture: " + path.string());
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

std::string compact(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) {
                               return std::isspace(ch) != 0;
                             }),
              value.end());
  std::replace(value.begin(), value.end(), ',', '.');
  return value;
}

int register_integer(emulator::MK61& calc, std::string_view name) {
  return static_cast<int>(
      std::llround(std::stod(compact(calc.read_register(std::string(name))))));
}

void run_to_stop(emulator::MK61& calc, std::string_view context) {
  const emulator::RunResult run = calc.run_until_stable(20000, 8);
  require(run.stopped, std::string(context) + " should reach a stop");
}

void preload_reference_game(emulator::MK61& calc) {
  emulator::MK61 setup;
  const emulator::ProgramLoadResult setup_loaded =
      setup.load_program({0x05, 0x05, 0x3a, 0x35, 0x0c,
                          0x01, 0x34, 0x44, 0x50});
  require(setup_loaded.diagnostics.empty(),
          "Nekromant R4 setup sequence should load");
  setup.press_sequence({"В/О", "С/П"});
  run_to_stop(setup, "Nekromant R4 setup sequence");
  calc.set_register("4", setup.read_register("4"));
  calc.set_register("5", "0.2");
  calc.set_register("6", "25");
  calc.set_register("7", "74");
  calc.set_register("8", "83");
  calc.set_register("9", "92");
  calc.set_register("b", "53");
  calc.set_register("c", "96");
  calc.set_register("e", "1000");
}

emulator::MK61 boot_reference(const std::vector<int>& opcodes) {
  emulator::MK61 calc({.extended = true});
  const emulator::ProgramLoadResult loaded = calc.load_program(opcodes);
  require(loaded.diagnostics.empty(),
          "Nekromant reference should load without truncation");
  preload_reference_game(calc);
  calc.press_sequence({"В/О", "С/П"});
  run_to_stop(calc, "Nekromant cold start");
  require(calc.program_counter() == "47",
          "cold start should stop after the spell prompt at address 46");
  require(register_integer(calc, "1") == 25 &&
              register_integer(calc, "2") == 3,
          "cold start should expose 25 minutes and three contact chances");
  return calc;
}

void enter_spell_and_resume(emulator::MK61& calc, const std::string& spell,
                            std::string_view context) {
  calc.input_number(spell, true);
  calc.press("С/П");
  run_to_stop(calc, context);
}

} // namespace

void emulator_nekromant_reference_and_source_contract() {
  const std::filesystem::path root = fixture_root();
  const std::vector<int> reference =
      parse_reference_listing(root / "games" / "adventure" / "nekromant.txt");

  emulator::MK61 contact = boot_reference(reference);
  contact.set_register("0", "0.4");
  contact.set_register("a", "0.4");
  enter_spell_and_resume(contact, "0", "Nekromant contact");
  require(contact.program_counter() == "68",
          "a monster reaching the player should stop at the contact marker");
  const std::string contact_display = compact(contact.display_text());
  require(contact_display == "-850.",
          "contact should expose the emulator's exact dark-display encoding "
          "of the documented --. marker");
  contact.press("С/П");
  run_to_stop(contact, "Nekromant amulet resume");
  require(register_integer(contact, "2") == 2 &&
              contact.program_counter() == "47",
          "resuming after first contact should spend one amulet and start a new turn");

  emulator::MK61 victory = boot_reference(reference);
  victory.set_register("0", "0");
  victory.set_register("a", "0");
  victory.set_register("1", "1");
  enter_spell_and_resume(victory, "0", "Nekromant final minute");
  require(victory.program_counter() == "68" &&
              register_integer(victory, "e") == 1000 &&
              compact(victory.display_text()).find("1000") !=
                  std::string::npos,
          "surviving the last minute should stop with the documented 1000 fee");

  const std::filesystem::path source =
      root / "examples" / "pending-optimizer" / "nekromant.mkpro";
  const std::string text = read_text(source);
  require(text.find("program Nekromant") != std::string::npos &&
              text.find("preview(minutes)") != std::string::npos,
          "the port should state the game and its Y-register countdown UI");
  require(text.find("raw {") == std::string::npos &&
              text.find("code {") == std::string::npos,
          "the MK-Pro port must remain high-level rather than embed the listing");

  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.disable_candidate_search = true;
  const CompileResult compiled = compile_source(text, options);
  std::string errors;
  for (const Diagnostic& diagnostic : compiled.diagnostics) {
    if (diagnostic.severity != DiagnosticSeverity::Error)
      continue;
    if (!errors.empty())
      errors += "; ";
    errors += diagnostic.code + ": " + diagnostic.message;
  }
  require(compiled.implemented && errors.empty(),
          "the high-level Nekromant source should compile without error "
          "diagnostics: " +
              errors);
  require(compiled.reference.has_value() &&
              compiled.reference->reference_steps == 105,
          "the port should retain the original 105-cell reference contract");
  require(std::any_of(
              compiled.items.begin(), compiled.items.end(),
              [](const MachineItem& item) {
                return item.kind == MachineItemKind::Op &&
                       item.opcode == 0x50 &&
                       item.stop_disposition == StopDisposition::Resumable;
              }),
          "the high-level source should preserve resumable game prompts");
}

} // namespace mkpro::tests
