#pragma once

#include <cstddef>
#include <string_view>

namespace mkpro::core::detail {

// Exactly -?[0-9]{1,8}(.[0-9]*)? with a literal decimal point and
// at most eight digits in total. This is entry syntax, not numeric equality:
// do not trim, accept exponents, collapse signed zero or admit raw BCD glyphs.
inline bool is_plain_decimal_mantissa(std::string_view text) {
  std::size_t at = !text.empty() && text.front() == '-' ? 1U : 0U;
  const auto first = at;
  while (at < text.size() && text[at] >= '0' && text[at] <= '9')
    ++at;
  std::size_t digits = at - first;
  if (digits == 0 || digits > 8U)
    return false;
  if (at < text.size() && text[at] == '.') {
    ++at;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
      ++at;
      if (++digits > 8U)
        return false;
    }
  }
  return at == text.size();
}

} // namespace mkpro::core::detail
