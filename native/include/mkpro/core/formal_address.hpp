#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace mkpro {

enum class FormalAddressKind {
  Official,
  ShortSide,
  LongSide,
  Dark,
  SuperDark,
};

struct FormalAddressInfo {
  int opcode = 0;
  std::string label;
  int ordinal = 0;
  int actual = 0;
  FormalAddressKind kind = FormalAddressKind::Official;
  bool one_command = false;
  std::optional<int> extra;
};

int formal_address_ordinal(int opcode);
int official_address_to_opcode(int address);
std::string format_formal_address_opcode(int opcode);
std::string format_official_address(int address);
std::optional<int> parse_formal_address_opcode(std::string_view text);
FormalAddressInfo formal_address_info(int opcode);
std::string formal_address_kind_name(FormalAddressKind kind);

}  // namespace mkpro
