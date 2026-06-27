#pragma once

#include <span>
#include <string_view>

namespace mkpro::emulator {

enum class RomChip {
  Ik1302,
  Ik1303,
  Ik1306,
  Vh205,
};

struct ChipRom {
  std::string_view name;
  std::span<const int> commands;
  std::span<const int> sync_programs;
  std::span<const int> microcommands;
};

std::span<const ChipRom> rom_chips();
const ChipRom& rom_chip(RomChip chip);

}  // namespace mkpro::emulator
