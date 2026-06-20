#include "mkpro/core/state_banks.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace mkpro::core {

namespace {

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

}  // namespace

std::string bank_member_key(const std::string& base, const std::optional<std::string>& field) {
  return field.has_value() ? base + "." + *field : base;
}

std::string bank_selector_variable_name(const std::string& base,
                                        const std::optional<std::string>& field) {
  return "__bank_selector_" + (field.has_value() ? base + "_" + *field : base);
}

std::string state_bank_key(const V2StateField& field) {
  if (!field.bank.has_value())
    return field.name;
  return bank_member_key(field.bank->name, field.bank->member);
}

std::string state_bank_element_name(const V2StateField& field, int index) {
  return field.name + "_" + std::to_string(index);
}

std::optional<int> numeric_index_value(const Expression& expression) {
  if (expression.kind == "number") {
    const std::string raw = expression.raw.empty() ? expression.text : expression.raw;
    if (raw.empty())
      return std::nullopt;
    char* end = nullptr;
    const double value = std::strtod(raw.c_str(), &end);
    if (end == raw.c_str() || *end != '\0' || !std::isfinite(value))
      return std::nullopt;
    const double rounded = std::round(value);
    if (std::fabs(value - rounded) >= 1e-12)
      return std::nullopt;
    return static_cast<int>(rounded);
  }
  if (expression.kind == "unary" && expression.op == "-" && expression.expr != nullptr) {
    const std::optional<int> value = numeric_index_value(*expression.expr);
    return value.has_value() ? std::optional<int>{-*value} : std::nullopt;
  }
  return std::nullopt;
}

std::optional<AffineIndexIdentifierOffset> affine_index_identifier_offset(
    const Expression& expression) {
  if (expression.kind == "identifier")
    return AffineIndexIdentifierOffset{.name = expression.name};
  if (expression.kind == "call" && lower_ascii(expression.callee) == "int" &&
      expression.args.size() == 1 && expression.args.front().kind == "identifier") {
    return AffineIndexIdentifierOffset{
        .name = expression.args.front().name,
        .integer_part = true,
    };
  }
  if (expression.kind != "binary" || expression.left == nullptr || expression.right == nullptr)
    return std::nullopt;

  const std::optional<AffineIndexIdentifierOffset> left =
      affine_index_identifier_offset(*expression.left);
  const std::optional<int> right_constant = numeric_index_value(*expression.right);
  if (left.has_value() && right_constant.has_value()) {
    if (expression.op == "+")
      return AffineIndexIdentifierOffset{
          .name = left->name,
          .offset = left->offset + *right_constant,
          .integer_part = left->integer_part,
      };
    if (expression.op == "-")
      return AffineIndexIdentifierOffset{
          .name = left->name,
          .offset = left->offset - *right_constant,
          .integer_part = left->integer_part,
      };
  }

  const std::optional<int> left_constant = numeric_index_value(*expression.left);
  const std::optional<AffineIndexIdentifierOffset> right =
      affine_index_identifier_offset(*expression.right);
  if (expression.op == "+" && left_constant.has_value() && right.has_value()) {
    return AffineIndexIdentifierOffset{
        .name = right->name,
        .offset = right->offset + *left_constant,
        .integer_part = right->integer_part,
    };
  }

  return std::nullopt;
}

}  // namespace mkpro::core
