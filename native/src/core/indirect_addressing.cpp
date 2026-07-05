#include "mkpro/core/indirect_addressing.hpp"

#include "mkpro/core/int128.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace mkpro::core {

namespace {

constexpr std::array<int, 16> k_memory_target_with_zero_tens = {
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
    0x8, 0x9, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0,
};

constexpr std::array<int, 16> k_memory_target_with_nonzero_tens = {
    0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0, 0x0, 0x1,
    0x2,  0x3,  0x4,  0x5,  0x6,  0x7, 0x8, 0x9,
};

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string normalize_selector_value(std::string_view value) {
  std::string normalized = lower_ascii(trim_ascii(std::string(value)));
  if (normalized.starts_with("0x"))
    normalized.erase(0, 2);
  std::replace(normalized.begin(), normalized.end(), ',', '.');
  return normalized;
}

bool parses_finite_number(std::string_view value, double& parsed) {
  const std::string text(value);
  char* end = nullptr;
  parsed = std::strtod(text.c_str(), &end);
  return end != text.c_str() && *end == '\0' && std::isfinite(parsed);
}

bool is_positive_fractional(double value) {
  return value > 0 && value < 1;
}

bool is_positive_fractional(std::string_view value) {
  double parsed = 0.0;
  return parses_finite_number(normalize_selector_value(value), parsed) && is_positive_fractional(parsed);
}

int mutation_delta(IndirectSelectorMutation mutation) {
  if (mutation == IndirectSelectorMutation::PreDecrement)
    return -1;
  if (mutation == IndirectSelectorMutation::PreIncrement)
    return 1;
  return 0;
}

std::string int128_to_string(Int128 value) {
  if (value == 0)
    return "0";
  const bool negative = value < 0;
  if (negative)
    value = -value;
  std::string digits;
  while (value > 0) {
    digits.push_back(static_cast<char>('0' + static_cast<int>(value % 10)));
    value /= 10;
  }
  if (negative)
    digits.push_back('-');
  std::reverse(digits.begin(), digits.end());
  return digits;
}

std::string negative_integer_mantissa(long long value) {
  std::string digits = std::to_string(value);
  if (!digits.empty() && digits.front() == '-')
    digits.erase(digits.begin());
  if (digits.size() > 8)
    digits = digits.substr(digits.size() - 8);
  if (digits.size() < 8)
    digits = std::string(8 - digits.size(), '9') + digits;
  return digits;
}

std::string pad_left_8(int value) {
  std::ostringstream out;
  out << std::setw(8) << std::setfill('0') << value;
  return out.str();
}

std::optional<std::string> transform_decimal_selector_value(
    double value, IndirectSelectorMutation mutation) {
  if (!std::isfinite(value))
    return std::nullopt;
  const long double truncated = std::trunc(static_cast<long double>(value));
  if (truncated < static_cast<long double>(std::numeric_limits<long long>::min()) ||
      truncated > static_cast<long double>(std::numeric_limits<long long>::max())) {
    return std::nullopt;
  }
  const long long integer = static_cast<long long>(truncated);
  const int delta = mutation_delta(mutation);
  if (integer >= 0) {
    if (integer == 0 && delta < 0)
      return std::string("-99999999");
    return int128_to_string(static_cast<Int128>(integer) + static_cast<Int128>(delta));
  }

  const int transformed = std::stoi(negative_integer_mantissa(integer));
  if (delta == 0)
    return "-" + std::to_string(transformed);
  const int next = transformed + delta;
  if (next <= 0 || next >= 100000000)
    return std::string("0");
  return "-" + pad_left_8(next);
}

std::optional<std::string> stable_exponent_mantissa_selector(
    std::string_view normalized, IndirectSelectorMutation mutation) {
  if (mutation != IndirectSelectorMutation::Stable)
    return std::nullopt;
  static const std::regex pattern(R"(^([1-9])(?:\.([0-9]*))?e-[0-9]{1,2}$)",
                                  std::regex_constants::icase);
  std::cmatch match;
  const std::string text(normalized);
  if (!std::regex_match(text.c_str(), match, pattern))
    return std::nullopt;
  std::string result = match[1].str() + (match[2].matched ? match[2].str() : "");
  if (result.size() < 8)
    result += std::string(8 - result.size(), '0');
  if (result.size() > 8)
    result.resize(8);
  return result;
}

bool selector_value_text_is_valid(std::string_view normalized) {
  static const std::regex pattern(R"(^-?[0-9a-f]+(?:\.\d+)?$)",
                                  std::regex_constants::icase);
  return std::regex_match(std::string(normalized), pattern);
}

bool contains_hex_alpha(std::string_view value) {
  return std::any_of(value.begin(), value.end(), [](char ch) {
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return lower >= 'a' && lower <= 'f';
  });
}

std::optional<std::string> transform_selector_value(std::string_view value,
                                                    IndirectSelectorMutation mutation) {
  const std::string normalized = normalize_selector_value(value);
  if (const std::optional<std::string> exponent =
          stable_exponent_mantissa_selector(normalized, mutation)) {
    return exponent;
  }
  if (!selector_value_text_is_valid(normalized))
    return std::nullopt;
  if (mutation == IndirectSelectorMutation::Stable && !normalized.starts_with("-") &&
      contains_hex_alpha(normalized)) {
    return normalized;
  }
  double decimal = 0.0;
  if (!parses_finite_number(normalized, decimal))
    return std::nullopt;
  return transform_decimal_selector_value(decimal, mutation);
}

struct TailPair {
  int tens = 0;
  int ones = 0;
  bool hex = false;
};

std::optional<TailPair> transformed_tail_pair(std::string_view transformed) {
  const std::string normalized = lower_ascii(trim_ascii(std::string(transformed)));
  static const std::regex pattern(R"(^-?[0-9a-f]+$)", std::regex_constants::icase);
  if (!std::regex_match(normalized, pattern))
    return std::nullopt;
  const bool negative = normalized.starts_with("-");
  const std::string digits = negative ? normalized.substr(1) : normalized;
  if (digits.empty())
    return std::nullopt;
  const char fill = negative ? '9' : '0';
  const std::string pair = digits.size() == 1 ? std::string{fill, digits.front()}
                                              : digits.substr(digits.size() - 2);
  const int tens = std::stoi(pair.substr(0, 1), nullptr, 16);
  const int ones = std::stoi(pair.substr(1, 1), nullptr, 16);
  if (tens < 0 || tens > 0x0f || ones < 0 || ones > 0x0f)
    return std::nullopt;
  return TailPair{.tens = tens, .ones = ones, .hex = contains_hex_alpha(pair)};
}

int flow_target_from_transformed(std::string_view transformed) {
  const std::optional<TailPair> tail = transformed_tail_pair(transformed);
  if (!tail.has_value())
    return 0;
  return tail->hex ? tail->tens * 16 + tail->ones : tail->tens * 10 + tail->ones;
}

int formal_opcode_for_flow_target(std::string_view transformed, int flow_target,
                                  AddressSpaceModel model) {
  const std::string normalized = lower_ascii(trim_ascii(std::string(transformed)));
  static const std::regex integer_pattern(R"(^-?[0-9a-f]+$)", std::regex_constants::icase);
  if (std::regex_match(normalized, integer_pattern) && contains_hex_alpha(normalized))
    return flow_target;
  return official_address_to_opcode(flow_target, model);
}

IndirectAddressEvaluation r0_fractional_result(std::string selector,
                                                IndirectOperationKind operation) {
  IndirectAddressEvaluation result;
  result.selector = std::move(selector);
  result.mutation = IndirectSelectorMutation::PreDecrement;
  result.operation = operation;
  result.transformed = "99";
  result.result_value = "-99999999";
  if (operation == IndirectOperationKind::Flow) {
    const FormalAddressInfo info = formal_address_info(0x99);
    result.formal_address = info;
    result.flow_target = 99;
    result.actual_flow_target = info.actual;
  } else {
    result.memory_target = 3;
  }
  return result;
}

}  // namespace

IndirectSelectorMutation indirect_selector_mutation(int register_index_value) {
  if (register_index_value <= 3)
    return IndirectSelectorMutation::PreDecrement;
  if (register_index_value <= 6)
    return IndirectSelectorMutation::PreIncrement;
  return IndirectSelectorMutation::Stable;
}

IndirectSelectorMutation indirect_selector_mutation(std::string_view register_name) {
  return indirect_selector_mutation(register_index(register_name));
}

bool is_stable_indirect_selector(std::string_view register_name) {
  return indirect_selector_mutation(register_name) == IndirectSelectorMutation::Stable;
}

std::optional<IndirectAddressEvaluation> evaluate_indirect_address(
    std::string_view selector, double value, IndirectOperationKind operation,
    AddressSpaceModel model) {
  if (selector == "0" && is_positive_fractional(value))
    return r0_fractional_result(std::string(selector), operation);
  return evaluate_indirect_address(selector, std::to_string(value), operation, model);
}

std::optional<IndirectAddressEvaluation> evaluate_indirect_address(
    std::string_view selector, std::string_view value, IndirectOperationKind operation,
    AddressSpaceModel model) {
  const IndirectSelectorMutation mutation = indirect_selector_mutation(selector);
  if (selector == "0" && is_positive_fractional(value))
    return r0_fractional_result(std::string(selector), operation);

  const std::optional<std::string> transformed = transform_selector_value(value, mutation);
  if (!transformed.has_value())
    return std::nullopt;

  IndirectAddressEvaluation result;
  result.selector = std::string(selector);
  result.mutation = mutation;
  result.operation = operation;
  result.transformed = *transformed;
  result.result_value = *transformed;

  if (operation == IndirectOperationKind::Flow) {
    const int flow_target = flow_target_from_transformed(*transformed);
    const FormalAddressInfo info =
        formal_address_info(formal_opcode_for_flow_target(*transformed, flow_target, model), model);
    result.formal_address = info;
    result.flow_target = flow_target;
    result.actual_flow_target = info.actual;
    result.super_dark = super_dark_target(info.opcode, model);
    return result;
  }

  const std::optional<int> memory_target = memory_target_from_transformed(*transformed);
  if (!memory_target.has_value())
    return std::nullopt;
  result.memory_target = *memory_target;
  return result;
}

std::optional<int> memory_target_from_transformed(std::string_view transformed) {
  const std::optional<TailPair> tail = transformed_tail_pair(transformed);
  if (!tail.has_value())
    return std::nullopt;
  return tail->tens == 0 ? k_memory_target_with_zero_tens.at(static_cast<std::size_t>(tail->ones))
                         : k_memory_target_with_nonzero_tens.at(static_cast<std::size_t>(tail->ones));
}

std::optional<SuperDarkIndirectTarget> super_dark_target(int formal_target,
                                                        AddressSpaceModel model) {
  const FormalAddressInfo info = formal_address_info(formal_target, model);
  if (info.kind != FormalAddressKind::SuperDark || !info.extra.has_value())
    return std::nullopt;
  return SuperDarkIndirectTarget{
      .formal = info.opcode,
      .entry_address = info.actual,
      .continuation_address = *info.extra,
  };
}

std::string indirect_selector_mutation_name(IndirectSelectorMutation mutation) {
  switch (mutation) {
    case IndirectSelectorMutation::PreDecrement:
      return "pre-decrement";
    case IndirectSelectorMutation::PreIncrement:
      return "pre-increment";
    case IndirectSelectorMutation::Stable:
      return "stable";
  }
  return "stable";
}

}  // namespace mkpro::core
