#include "mkpro/core/raw_bcd_unary_selector.hpp"

#include "mkpro/core/indirect_addressing.hpp"

#include <array>
#include <cctype>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

namespace mkpro::core {

namespace {

struct StructuralHexExponentSeed {
  bool negative = false;
  int digit = 0;
  int exponent = 0;
  std::string canonical;
};

struct DegTgRawFact {
  std::string_view magnitude;
  int flow_target = 0;
};

// ROM-derived raw BCD facts. These are intentionally not host tan() values:
// the significant-digit count and exponent participate in the later indirect
// microcode. The table is exhaustively checked against the emulator oracle in
// raw_bcd_unary_selector_test.cpp.
constexpr std::array<std::array<DegTgRawFact, 4>, 4> kDegTgFacts{{
    {{{"0", 0}, {"0", 0}, {"0", 0}, {"0", 0}}},
    {{{"1.7453309E-3", 9},
      {"1.7453292E-4", 29},
      {"1.7453292E-5", 32},
      {"1.7453292E-6", 53}}},
    {{{"3.4906726E-3", 26},
      {"3.4906585E-4", 58},
      {"3.4906584E-5", 65},
      {"3.4906584E-6", 6}}},
    {{{"5.2360355E-3", 55},
      {"5.235988E-4", 88},
      {"5.2359878E-5", 98},
      {"5.2359878E-6", 59}}},
}};

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

void replace_all(std::string& value, std::string_view from, std::string_view to) {
  std::size_t cursor = 0;
  while ((cursor = value.find(from, cursor)) != std::string::npos) {
    value.replace(cursor, from.size(), to);
    cursor += to.size();
  }
}

std::optional<StructuralHexExponentSeed> parse_structural_hex_exponent_seed(
    std::string_view raw_seed) {
  std::string text = trim_ascii(std::string(raw_seed));
  for (char& ch : text) {
    if (ch >= 'a' && ch <= 'z')
      ch = static_cast<char>(ch - ('a' - 'A'));
  }

  // Normalize the calculator's Cyrillic glyphs without interpreting arbitrary
  // decimal or multi-digit values as structural hex seeds.
  replace_all(text, "а", "A");
  replace_all(text, "А", "A");
  replace_all(text, "в", "B");
  replace_all(text, "В", "B");
  replace_all(text, "с", "C");
  replace_all(text, "С", "C");
  replace_all(text, "г", "D");
  replace_all(text, "Г", "D");
  replace_all(text, "д", "D");
  replace_all(text, "Д", "D");
  replace_all(text, "е", "E");
  replace_all(text, "Е", "E");

  bool negative = false;
  if (!text.empty() && text.front() == '-') {
    negative = true;
    text.erase(text.begin());
  }
  if (text.size() != 4U || text.at(1) != 'E' || text.at(2) != '-' ||
      text.at(0) < 'A' || text.at(0) > 'D' || text.at(3) < '1' || text.at(3) > '4') {
    return std::nullopt;
  }

  const int digit = 10 + (text.at(0) - 'A');
  const int exponent = -(text.at(3) - '0');
  const std::string digit_text = digit == 13 ? "Г" : std::string(1, text.at(0));
  return StructuralHexExponentSeed{
      .negative = negative,
      .digit = digit,
      .exponent = exponent,
      .canonical = (negative ? "-" : "") + digit_text + "E" + std::to_string(exponent),
  };
}

std::optional<std::string> normalize_stable_selector(std::string_view selector) {
  std::string normalized = trim_ascii(std::string(selector));
  if (normalized.size() == 2U && (normalized.front() == 'R' || normalized.front() == 'r'))
    normalized.erase(normalized.begin());
  if (normalized.size() != 1U)
    return std::nullopt;
  char& ch = normalized.front();
  if (ch >= 'A' && ch <= 'E')
    ch = static_cast<char>(ch - 'A' + 'a');
  if (!((ch >= '7' && ch <= '9') || (ch >= 'a' && ch <= 'e')))
    return std::nullopt;
  return normalized;
}

} // namespace

std::optional<RawBcdUnaryIndirectSelectorResult>
evaluate_raw_bcd_unary_indirect_selector(mk61_trig::AngleMode mode,
                                         mk61_trig::Function operation,
                                         std::string_view raw_seed,
                                         std::string_view selector,
                                         AddressSpaceModel model) {
  if (mode != mk61_trig::AngleMode::Deg || operation != mk61_trig::Function::Tg)
    return std::nullopt;

  const std::optional<StructuralHexExponentSeed> seed =
      parse_structural_hex_exponent_seed(raw_seed);
  const std::optional<std::string> stable_selector = normalize_stable_selector(selector);
  if (!seed.has_value() || !stable_selector.has_value())
    return std::nullopt;

  const std::size_t digit_index = static_cast<std::size_t>(seed->digit - 10);
  const std::size_t exponent_index = static_cast<std::size_t>(-seed->exponent - 1);
  if (digit_index >= kDegTgFacts.size() ||
      exponent_index >= kDegTgFacts.at(digit_index).size()) {
    return std::nullopt;
  }
  const DegTgRawFact& fact = kDegTgFacts.at(digit_index).at(exponent_index);

  // The table proves the raw unary-to-formal-target identity. Delegate only
  // the formal-address-space mapping to the ordinary indirect evaluator.
  std::optional<IndirectAddressEvaluation> evaluated;
  try {
    evaluated = evaluate_indirect_address(*stable_selector, std::to_string(fact.flow_target),
                                          IndirectOperationKind::Flow, model);
  } catch (const std::exception&) {
    return std::nullopt;
  }
  if (!evaluated.has_value() || !evaluated->flow_target.has_value() ||
      !evaluated->actual_flow_target.has_value() ||
      *evaluated->flow_target != fact.flow_target) {
    return std::nullopt;
  }

  const std::string raw_result =
      seed->negative && fact.magnitude != "0" ? "-" + std::string(fact.magnitude)
                                               : std::string(fact.magnitude);
  return RawBcdUnaryIndirectSelectorResult{
      .canonical_seed = seed->canonical,
      .raw_result = raw_result,
      .selector = *stable_selector,
      .formal_flow_target = fact.flow_target,
      .actual_flow_target = *evaluated->actual_flow_target,
  };
}

} // namespace mkpro::core
