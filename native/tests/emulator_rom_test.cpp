#include "mkpro/emulator/rom.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string hex_byte(int value) {
  const char* digits = "0123456789ABCDEF";
  std::string out;
  out.push_back(digits[(value >> 4) & 0x0f]);
  out.push_back(digits[value & 0x0f]);
  return out;
}

std::vector<std::string> hex_bytes(const std::vector<int>& values) {
  std::vector<std::string> out;
  for (const int value : values)
    out.push_back(hex_byte(value));
  return out;
}

}  // namespace

void emulator_rom_tables_match_typescript_contract() {
  const auto chips = emulator::rom_chips();
  require(chips.size() == 4, "native ROM table should expose all rom.cjs chips");
  for (const auto& chip : chips) {
    require(chip.commands.size() == 256, std::string(chip.name) + " command ROM length");
    require(chip.sync_programs.size() == 1152,
            std::string(chip.name) + " sync program ROM length");
    require(chip.microcommands.size() == 68, std::string(chip.name) + " microcommand ROM length");
  }

  const std::array<emulator::RomChip, 3> mk61_chips = {
      emulator::RomChip::Ik1302,
      emulator::RomChip::Ik1303,
      emulator::RomChip::Ik1306,
  };
  std::vector<int> differing_opcodes;
  for (int opcode = 0; opcode <= 0xff; ++opcode) {
    std::set<int> words;
    for (const auto chip : mk61_chips)
      words.insert(emulator::rom_chip(chip).commands[static_cast<std::size_t>(opcode)]);
    if (words.size() > 1U)
      differing_opcodes.push_back(opcode);
  }
  require(differing_opcodes.size() == 256,
          "each user opcode should differ across at least one MK-61 command ROM");

  std::vector<int> zero_on_dispatcher;
  const auto& dispatcher = emulator::rom_chip(emulator::RomChip::Ik1302);
  const auto& later = emulator::rom_chip(emulator::RomChip::Ik1303);
  for (int opcode = 0; opcode <= 0xff; ++opcode) {
    if (dispatcher.commands[static_cast<std::size_t>(opcode)] == 0)
      zero_on_dispatcher.push_back(opcode);
  }
  require(hex_bytes(zero_on_dispatcher) ==
              std::vector<std::string>{"0B", "0C", "0D", "0E", "3B", "3C", "3D", "3E"},
          "dispatcher-zero opcode list should match TypeScript ROM discoveries");
  for (const int opcode : zero_on_dispatcher) {
    require(later.commands[static_cast<std::size_t>(opcode)] != 0,
            "dispatcher-zero opcode should have later-chip microcode: " + hex_byte(opcode));
  }
}

}  // namespace mkpro::tests
