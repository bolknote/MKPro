#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mkpro::core::mk61_trig {

enum class AngleMode : std::uint8_t {
  Rad = 10,
  Deg = 11,
  Grad = 12,
};

enum class Function : std::uint8_t {
  Sin = 0x1c,
  Cos = 0x1d,
  Tg = 0x1e,
};

std::string calculate_display(AngleMode mode, Function function, std::string_view literal);
std::string sin_display(AngleMode mode, std::string_view literal);
std::string cos_display(AngleMode mode, std::string_view literal);
std::string tg_display(AngleMode mode, std::string_view literal);

// Numeric counterpart of calculate_display: returns the ROM-faithful result as a
// value (the same number the machine would hold in X), for callers that reason
// in doubles rather than display strings (e.g. the address-formula solver).
double calculate(AngleMode mode, Function function, double value);

}  // namespace mkpro::core::mk61_trig
