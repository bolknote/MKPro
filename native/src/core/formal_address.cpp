#include "mkpro/core/formal_address.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace mkpro {

namespace {

std::string hex_byte(int value) {
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << value;
  return out.str();
}

void assert_byte(int value) {
  if (value < 0 || value > 0xff) {
    throw std::runtime_error("Formal MK-61 address byte " + std::to_string(value) +
                             " is out of range");
  }
}

char ascii_upper(char value) {
  return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
}

}  // namespace

int formal_address_ordinal(int opcode) {
  assert_byte(opcode);
  const int high = opcode >> 4;
  const int low = opcode & 0x0f;
  return high * 10 + low;
}

int official_address_to_opcode(int address) {
  if (address < 0 || address > 104) {
    throw std::runtime_error("Physical MK-61 program address " + std::to_string(address) +
                             " is outside 00..A4");
  }
  if (address <= 99) {
    const int tens = address / 10;
    const int ones = address % 10;
    return tens * 16 + ones;
  }
  return 0xa0 + (address - 100);
}

std::string format_formal_address_opcode(int opcode) {
  assert_byte(opcode);
  return hex_byte(opcode);
}

std::string format_official_address(int address) {
  return format_formal_address_opcode(official_address_to_opcode(address));
}

std::optional<int> parse_formal_address_opcode(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (char ch : text) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) continue;
    if (ch == '.' || ch == '-') {
      normalized.push_back('A');
    } else {
      normalized.push_back(ascii_upper(ch));
    }
  }
  if (normalized.size() != 2) return std::nullopt;
  if (!std::all_of(normalized.begin(), normalized.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F');
      })) {
    return std::nullopt;
  }
  return std::stoi(normalized, nullptr, 16);
}

FormalAddressInfo formal_address_info(int opcode) {
  assert_byte(opcode);
  const int ordinal = formal_address_ordinal(opcode);
  const std::string label = format_formal_address_opcode(opcode);

  if (ordinal <= 104) {
    return FormalAddressInfo{
        .opcode = opcode,
        .label = label,
        .ordinal = ordinal,
        .actual = ordinal,
        .kind = FormalAddressKind::Official,
        .one_command = false,
    };
  }

  if (ordinal <= 111) {
    return FormalAddressInfo{
        .opcode = opcode,
        .label = label,
        .ordinal = ordinal,
        .actual = ordinal - 105,
        .kind = FormalAddressKind::ShortSide,
        .one_command = false,
    };
  }

  if (ordinal <= 159) {
    return FormalAddressInfo{
        .opcode = opcode,
        .label = label,
        .ordinal = ordinal,
        .actual = ordinal - 112,
        .kind = ordinal >= 120 ? FormalAddressKind::Dark : FormalAddressKind::LongSide,
        .one_command = false,
    };
  }

  if (ordinal <= 165) {
    return FormalAddressInfo{
        .opcode = opcode,
        .label = label,
        .ordinal = ordinal,
        .actual = ordinal - 112,
        .kind = FormalAddressKind::SuperDark,
        .one_command = true,
        .extra = ordinal - 159,
    };
  }

  throw std::runtime_error("Formal MK-61 address " + label +
                           " maps past the known address space");
}

std::string formal_address_kind_name(FormalAddressKind kind) {
  switch (kind) {
    case FormalAddressKind::Official:
      return "official";
    case FormalAddressKind::ShortSide:
      return "short-side";
    case FormalAddressKind::LongSide:
      return "long-side";
    case FormalAddressKind::Dark:
      return "dark";
    case FormalAddressKind::SuperDark:
      return "super-dark";
  }
  return "official";
}

}  // namespace mkpro
