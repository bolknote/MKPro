#include "mkpro/core/raw_bcd_word.hpp"

#include <cctype>
#include <string>

namespace mkpro::core {

std::optional<RawSingleHexWord> decode_raw_single_hex_word(std::string_view word) {
  while (!word.empty() && std::isspace(static_cast<unsigned char>(word.front())) != 0)
    word.remove_prefix(1);
  while (!word.empty() && std::isspace(static_cast<unsigned char>(word.back())) != 0)
    word.remove_suffix(1);
  if (word.starts_with("0x") || word.starts_with("0X"))
    word.remove_prefix(2);
  const bool negative = word.starts_with("-");
  if (negative)
    word.remove_prefix(1);
  const auto order = word.find_last_of("Ee");
  if (order == std::string_view::npos || order + 3U != word.size() ||
      word[order + 1U] != '-' || word[order + 2U] < '1' || word[order + 2U] > '4')
    return std::nullopt;
  const auto mantissa = word.substr(0, order);
  int digit = 0;
  if (mantissa == "A" || mantissa == "a") digit = 10;
  else if (mantissa == "B" || mantissa == "b") digit = 11;
  else if (mantissa == "C" || mantissa == "c" || mantissa == "\u0421") digit = 12;
  else if (mantissa == "D" || mantissa == "d" || mantissa == "\u0413") digit = 13;
  else return std::nullopt;
  const int exponent = -(word[order + 2U] - '0');
  const auto letter = static_cast<char>('A' + digit - 10);
  return RawSingleHexWord{
      (negative ? "-" : "") + std::string(1, letter) + "E" + std::to_string(exponent),
      digit, exponent, negative};
}

std::optional<RawPower10Product> evaluate_raw_right_power10_product(
    int left_exponent, std::string_view right_word) {
  const auto word = decode_raw_single_hex_word(right_word);
  if (!word || left_exponent < -8 || left_exponent > 8)
    return std::nullopt;
  return RawPower10Product{
      left_exponent + word->exponent + 1, word->negative, word->canonical_word};
}

} // namespace mkpro::core
