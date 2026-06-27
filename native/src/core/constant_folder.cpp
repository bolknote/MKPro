#include "mkpro/core/constant_folder.hpp"

#include "mkpro/core/parser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core {

namespace {

constexpr int kMaxFoldedSignificantDigits = 8;
constexpr int kMaxFoldedDecimalScale = 12;
constexpr int kMaxLiteralExponent = 18;
constexpr __int128 kMaxDecimalMagnitude =
    (((static_cast<__int128>(1) << 120) - 1) / 10) * 10;

struct DecimalValue {
  __int128 num = 0;
  int scale = 0;
};

struct LinearTerm {
  Expression expr;
  DecimalValue coeff;
};

struct SignedExpressionTerm {
  Expression expr;
  bool negative = false;
};

struct LinearForm {
  DecimalValue constant;
  std::vector<std::pair<std::string, LinearTerm>> terms;
};

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string expression_to_source(const Expression& expression);

int expression_binary_precedence(const std::string& op) {
  return op == "*" || op == "/" ? 2 : 1;
}

int expression_precedence(const Expression& expression) {
  if (expression.kind == "number" || expression.kind == "string" ||
      expression.kind == "identifier" || expression.kind == "call" ||
      expression.kind == "indexed") {
    return 4;
  }
  if (expression.kind == "unary")
    return 3;
  if (expression.kind == "binary")
    return expression_binary_precedence(expression.op);
  return 0;
}

std::string wrap_expression_source(const Expression& expression, int parent_precedence) {
  const std::string text = expression_to_source(expression);
  return expression_precedence(expression) < parent_precedence ? "(" + text + ")" : text;
}

std::string expression_to_source(const Expression& expression) {
  if (expression.kind == "number")
    return expression.raw.empty() ? expression.text : expression.raw;
  if (expression.kind == "identifier")
    return expression.name;
  if (expression.kind == "string") {
    std::string escaped = "\"";
    for (const char ch : expression.text) {
      if (ch == '"' || ch == '\\')
        escaped.push_back('\\');
      escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
  }
  if (expression.kind == "indexed" && expression.index != nullptr) {
    std::string text = expression.base + "[" + expression_to_source(*expression.index) + "]";
    if (expression.field.has_value())
      text += "." + *expression.field;
    return text;
  }
  if (expression.kind == "unary" && expression.expr != nullptr)
    return expression.op + wrap_expression_source(*expression.expr, 3);
  if (expression.kind == "binary" && expression.left != nullptr && expression.right != nullptr) {
    const int precedence = expression_binary_precedence(expression.op);
    const int right_precedence =
        precedence + (expression.op == "-" || expression.op == "/" ? 1 : 0);
    return wrap_expression_source(*expression.left, precedence) + " " + expression.op + " " +
           wrap_expression_source(*expression.right, right_precedence);
  }
  if (expression.kind == "call") {
    std::string text = expression.callee + "(";
    for (std::size_t index = 0; index < expression.args.size(); ++index) {
      if (index != 0)
        text += ", ";
      text += expression_to_source(expression.args.at(index));
    }
    text += ")";
    return text;
  }
  throw std::runtime_error("Cannot serialize expression kind '" + expression.kind + "'");
}

Expression number_expression(std::string raw) {
  Expression expression;
  expression.kind = "number";
  expression.raw = std::move(raw);
  return expression;
}

Expression unary_expression(std::string op, Expression expr) {
  Expression expression;
  expression.kind = "unary";
  expression.op = std::move(op);
  expression.expr = std::make_shared<Expression>(std::move(expr));
  return expression;
}

Expression binary_expression(Expression left, std::string op, Expression right) {
  Expression expression;
  expression.kind = "binary";
  expression.op = std::move(op);
  expression.left = std::make_shared<Expression>(std::move(left));
  expression.right = std::make_shared<Expression>(std::move(right));
  return expression;
}

Expression add_expression(Expression left, Expression right) {
  return binary_expression(std::move(left), "+", std::move(right));
}

Expression subtract_expression(Expression left, Expression right) {
  return binary_expression(std::move(left), "-", std::move(right));
}

Expression multiply_expression(Expression left, Expression right) {
  return binary_expression(std::move(left), "*", std::move(right));
}

Expression divide_expression(Expression left, Expression right) {
  return binary_expression(std::move(left), "/", std::move(right));
}

__int128 abs_int128(__int128 value) {
  return value < 0 ? -value : value;
}

bool checked_mul(__int128 left, __int128 right, __int128& out) {
  if (left == 0 || right == 0) {
    out = 0;
    return true;
  }
  if (abs_int128(left) > kMaxDecimalMagnitude / abs_int128(right))
    return false;
  out = left * right;
  return true;
}

std::optional<__int128> pow10_int(int exponent) {
  if (exponent < 0)
    return std::nullopt;
  __int128 result = 1;
  for (int index = 0; index < exponent; ++index) {
    __int128 next = 0;
    if (!checked_mul(result, 10, next))
      return std::nullopt;
    result = next;
  }
  return result;
}

std::string int128_to_string(__int128 value) {
  if (value == 0)
    return "0";
  const bool negative = value < 0;
  __int128 remaining = negative ? -value : value;
  std::string digits;
  while (remaining > 0) {
    const int digit = static_cast<int>(remaining % 10);
    digits.push_back(static_cast<char>('0' + digit));
    remaining /= 10;
  }
  if (negative)
    digits.push_back('-');
  std::reverse(digits.begin(), digits.end());
  return digits;
}

DecimalValue normalize_decimal(DecimalValue value) {
  if (value.num == 0)
    return DecimalValue{.num = 0, .scale = 0};
  while (value.scale > 0 && value.num % 10 == 0) {
    value.num /= 10;
    value.scale -= 1;
  }
  return value;
}

DecimalValue decimal(int value) {
  return normalize_decimal(DecimalValue{.num = value, .scale = 0});
}

DecimalValue negate_decimal(DecimalValue value) {
  value.num = -value.num;
  return normalize_decimal(value);
}

DecimalValue abs_decimal(DecimalValue value) {
  value.num = abs_int128(value.num);
  return normalize_decimal(value);
}

int decimal_sign(DecimalValue value) {
  value = normalize_decimal(value);
  if (value.num < 0)
    return -1;
  if (value.num > 0)
    return 1;
  return 0;
}

bool is_decimal_zero(DecimalValue value) {
  return normalize_decimal(value).num == 0;
}

bool is_decimal_one(DecimalValue value) {
  value = normalize_decimal(value);
  return value.num == 1 && value.scale == 0;
}

std::optional<DecimalValue> parse_decimal_literal(const std::string& raw) {
  static const std::regex pattern(R"(^\s*(-)?(\d+)(?:\.(\d+))?(?:e([+-]?\d+))?\s*$)",
                                  std::regex::icase);
  std::smatch match;
  if (!std::regex_match(raw, match, pattern))
    return std::nullopt;

  const int exponent = match[4].matched ? std::stoi(match[4].str()) : 0;
  if (std::abs(exponent) > kMaxLiteralExponent)
    return std::nullopt;

  std::string digits = match[2].str() + (match[3].matched ? match[3].str() : std::string{});
  digits.erase(0, std::min(digits.find_first_not_of('0'), digits.size()));
  int scale = static_cast<int>(match[3].matched ? match[3].str().size() : 0) - exponent;
  if (scale < 0) {
    digits.append(static_cast<std::size_t>(-scale), '0');
    scale = 0;
  }

  __int128 number = 0;
  for (const char ch : digits.empty() ? std::string{"0"} : digits) {
    __int128 next = 0;
    if (!checked_mul(number, 10, next))
      return std::nullopt;
    next += static_cast<int>(ch - '0');
    if (abs_int128(next) > kMaxDecimalMagnitude)
      return std::nullopt;
    number = next;
  }
  if (match[1].matched)
    number = -number;
  return normalize_decimal(DecimalValue{.num = number, .scale = scale});
}

std::optional<DecimalValue> add_decimal(DecimalValue left, DecimalValue right) {
  const int scale = std::max(left.scale, right.scale);
  const std::optional<__int128> left_multiplier = pow10_int(scale - left.scale);
  const std::optional<__int128> right_multiplier = pow10_int(scale - right.scale);
  if (!left_multiplier.has_value() || !right_multiplier.has_value())
    return std::nullopt;

  __int128 left_num = 0;
  __int128 right_num = 0;
  if (!checked_mul(left.num, *left_multiplier, left_num) ||
      !checked_mul(right.num, *right_multiplier, right_num)) {
    return std::nullopt;
  }
  const __int128 result = left_num + right_num;
  if (abs_int128(result) > kMaxDecimalMagnitude)
    return std::nullopt;
  return normalize_decimal(DecimalValue{.num = result, .scale = scale});
}

std::optional<DecimalValue> subtract_decimal(DecimalValue left, DecimalValue right) {
  return add_decimal(left, negate_decimal(right));
}

std::optional<DecimalValue> multiply_decimal(DecimalValue left, DecimalValue right) {
  __int128 num = 0;
  if (!checked_mul(left.num, right.num, num))
    return std::nullopt;
  return normalize_decimal(DecimalValue{.num = num, .scale = left.scale + right.scale});
}

__int128 gcd_int128(__int128 left, __int128 right) {
  left = abs_int128(left);
  right = abs_int128(right);
  while (right != 0) {
    const __int128 next = left % right;
    left = right;
    right = next;
  }
  return left;
}

std::optional<DecimalValue> divide_decimal(DecimalValue left, DecimalValue right) {
  if (right.num == 0)
    return std::nullopt;
  const std::optional<__int128> right_scale = pow10_int(right.scale);
  const std::optional<__int128> left_scale = pow10_int(left.scale);
  if (!right_scale.has_value() || !left_scale.has_value())
    return std::nullopt;

  __int128 numerator = 0;
  __int128 denominator = 0;
  if (!checked_mul(left.num, *right_scale, numerator) ||
      !checked_mul(right.num, *left_scale, denominator)) {
    return std::nullopt;
  }
  if (denominator < 0) {
    numerator = -numerator;
    denominator = -denominator;
  }

  const __int128 divisor = gcd_int128(numerator, denominator);
  numerator /= divisor;
  denominator /= divisor;

  int twos = 0;
  while (denominator % 2 == 0) {
    denominator /= 2;
    ++twos;
  }
  int fives = 0;
  while (denominator % 5 == 0) {
    denominator /= 5;
    ++fives;
  }
  if (denominator != 1)
    return std::nullopt;

  const int scale = std::max(twos, fives);
  if (scale > kMaxFoldedDecimalScale)
    return std::nullopt;
  const std::optional<__int128> two_pad = pow10_int(0);
  (void)two_pad;

  __int128 padded = numerator;
  for (int index = 0; index < scale - twos; ++index) {
    if (!checked_mul(padded, 2, padded))
      return std::nullopt;
  }
  for (int index = 0; index < scale - fives; ++index) {
    if (!checked_mul(padded, 5, padded))
      return std::nullopt;
  }
  return normalize_decimal(DecimalValue{.num = padded, .scale = scale});
}

std::optional<DecimalValue> decimal_from_expression(const Expression& expression) {
  if (expression.kind != "number")
    return std::nullopt;
  return parse_decimal_literal(expression.raw.empty() ? expression.text : expression.raw);
}

std::string decimal_to_raw(DecimalValue value) {
  value = normalize_decimal(value);
  if (value.num == 0)
    return "0";
  const std::string sign = value.num < 0 ? "-" : "";
  std::string digits = int128_to_string(abs_int128(value.num));
  if (value.scale == 0)
    return sign + digits;
  if (digits.size() <= static_cast<std::size_t>(value.scale)) {
    return sign + "0." + std::string(static_cast<std::size_t>(value.scale) - digits.size(), '0') +
           digits;
  }
  const std::size_t split = digits.size() - static_cast<std::size_t>(value.scale);
  return sign + digits.substr(0, split) + "." + digits.substr(split);
}

int significant_digit_count(const std::string& raw) {
  std::string digits;
  bool in_exponent = false;
  for (char ch : raw) {
    if (ch == 'e' || ch == 'E') {
      in_exponent = true;
      continue;
    }
    if (in_exponent)
      continue;
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0)
      digits.push_back(ch);
  }
  digits.erase(0, std::min(digits.find_first_not_of('0'), digits.size()));
  return digits.empty() ? 1 : static_cast<int>(digits.size());
}

std::optional<Expression> decimal_number_expression(DecimalValue value) {
  value = normalize_decimal(value);
  if (value.scale > kMaxFoldedDecimalScale)
    return std::nullopt;
  const std::string raw = decimal_to_raw(value);
  if (significant_digit_count(raw) > kMaxFoldedSignificantDigits)
    return std::nullopt;
  return number_expression(raw);
}

std::vector<int> decimal_mantissa_digits(DecimalValue value) {
  value = normalize_decimal(value);
  std::vector<int> result(8, 0);
  if (value.num == 0)
    return result;
  const std::string digits = int128_to_string(abs_int128(value.num));
  for (std::size_t index = 0; index < result.size() && index < digits.size(); ++index)
    result[index] = digits[index] - '0';
  return result;
}

DecimalValue decimal_from_digits(const std::vector<int>& digits, int scale) {
  __int128 num = 0;
  for (const int digit : digits)
    num = num * 10 + digit;
  return normalize_decimal(DecimalValue{.num = num, .scale = scale});
}

int bitwise_nibble(int left, int right, const std::string& op) {
  if (op == "and")
    return left & right;
  if (op == "or")
    return left | right;
  return left ^ right;
}

std::optional<DecimalValue> fold_bitwise(DecimalValue left, DecimalValue right,
                                         const std::string& op) {
  const std::vector<int> lhs = decimal_mantissa_digits(left);
  const std::vector<int> rhs = decimal_mantissa_digits(right);
  std::vector<int> digits = {8};
  digits.reserve(8);
  for (std::size_t index = 1; index < 8; ++index) {
    const int digit = bitwise_nibble(lhs.at(index), rhs.at(index), op);
    if (digit > 9)
      return std::nullopt;
    digits.push_back(digit);
  }
  return decimal_from_digits(digits, 7);
}

std::optional<DecimalValue> fold_bitwise_not(DecimalValue value) {
  const std::vector<int> source = decimal_mantissa_digits(value);
  std::vector<int> digits = {8};
  digits.reserve(8);
  for (std::size_t index = 1; index < 8; ++index) {
    const int digit = (~source.at(index)) & 0x0f;
    if (digit > 9)
      return std::nullopt;
    digits.push_back(digit);
  }
  return decimal_from_digits(digits, 7);
}

std::optional<double> numeric_literal_value(const Expression& expression) {
  if (expression.kind != "number")
    return std::nullopt;
  const std::string raw = expression.raw.empty() ? expression.text : expression.raw;
  try {
    std::size_t parsed = 0;
    const double value = std::stod(raw, &parsed);
    if (parsed != raw.size() || !std::isfinite(value))
      return std::nullopt;
    return value;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool is_numeric_value(const Expression& expression, double value) {
  const std::optional<double> parsed = numeric_literal_value(expression);
  return parsed.has_value() && *parsed == value;
}

int estimate_number_cost(const std::string& raw) {
  std::string normalized;
  normalized.reserve(raw.size());
  for (const char ch : raw) {
    if (std::isspace(static_cast<unsigned char>(ch)) == 0)
      normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  const bool negative = !normalized.empty() && normalized.front() == '-';
  const std::string unsigned_text = negative ? normalized.substr(1) : normalized;
  const std::size_t exponent_index = unsigned_text.find('e');
  const std::string mantissa =
      exponent_index == std::string::npos ? unsigned_text : unsigned_text.substr(0, exponent_index);
  const std::string exponent = exponent_index == std::string::npos
                                   ? std::string{}
                                   : unsigned_text.substr(exponent_index + 1);
  int cost = negative ? 1 : 0;
  for (const char ch : mantissa) {
    if (ch == '.' || std::isdigit(static_cast<unsigned char>(ch)) != 0)
      ++cost;
  }
  if (exponent_index != std::string::npos) {
    ++cost;
    if (!exponent.empty() && exponent.front() == '-')
      ++cost;
    for (const char ch : exponent) {
      if (ch != '+' && ch != '-' && std::isdigit(static_cast<unsigned char>(ch)) != 0)
        ++cost;
    }
  }
  return cost;
}

int estimate_expression_cost(const Expression& expression);

int estimate_call_cost(const Expression& expression) {
  const std::string name = lower_ascii(expression.callee);
  if (name == "random") {
    if (expression.args.size() == 1U)
      return estimate_expression_cost(expression.args.front()) + 2;
    if (expression.args.size() == 2U)
      return estimate_expression_cost(expression.args.at(0)) +
             estimate_expression_cost(expression.args.at(1)) + 2;
    return 1;
  }
  if (name == "pi")
    return 1;
  if (name == "e")
    return 2;
  if (name == "pow") {
    if (expression.args.size() >= 2U && is_numeric_value(expression.args.at(1), 2))
      return estimate_expression_cost(expression.args.at(0)) + 1;
    if (expression.args.size() >= 2U && is_numeric_value(expression.args.at(0), 10))
      return estimate_expression_cost(expression.args.at(1)) + 1;
  }
  if (name == "min" || name == "max" || name == "safe_min" || name == "safe_max" ||
      name == "bit_and" || name == "bit_or" || name == "bit_xor" || name == "pow") {
    return (expression.args.empty() ? 0 : estimate_expression_cost(expression.args.at(0))) +
           (expression.args.size() < 2U ? 0 : estimate_expression_cost(expression.args.at(1))) + 1;
  }
  return (expression.args.empty() ? 0 : estimate_expression_cost(expression.args.front())) + 1;
}

int estimate_expression_cost(const Expression& expression) {
  if (expression.kind == "string")
    return 0;
  if (expression.kind == "number")
    return estimate_number_cost(expression.raw.empty() ? expression.text : expression.raw);
  if (expression.kind == "identifier")
    return 1;
  if (expression.kind == "indexed")
    return (expression.index == nullptr ? 0 : estimate_expression_cost(*expression.index)) + 1;
  if (expression.kind == "unary")
    return (expression.expr == nullptr ? 0 : estimate_expression_cost(*expression.expr)) + 1;
  if (expression.kind == "binary")
    return (expression.left == nullptr ? 0 : estimate_expression_cost(*expression.left)) +
           (expression.right == nullptr ? 0 : estimate_expression_cost(*expression.right)) + 1;
  if (expression.kind == "call")
    return estimate_call_cost(expression);
  return std::numeric_limits<int>::max() / 4;
}

int estimate_binary_cost(const Expression& left, const Expression& right) {
  return estimate_expression_cost(left) + estimate_expression_cost(right) + 1;
}

bool expression_pure_for_folding(const Expression& expression) {
  if (expression.kind == "call" && lower_ascii(expression.callee) == "random")
    return false;
  if (expression.index != nullptr && !expression_pure_for_folding(*expression.index))
    return false;
  if (expression.expr != nullptr && !expression_pure_for_folding(*expression.expr))
    return false;
  if (expression.left != nullptr && !expression_pure_for_folding(*expression.left))
    return false;
  if (expression.right != nullptr && !expression_pure_for_folding(*expression.right))
    return false;
  return std::all_of(expression.args.begin(), expression.args.end(),
                     [](const Expression& arg) { return expression_pure_for_folding(arg); });
}

bool expression_safe_to_drop(const Expression& expression) {
  if (expression.kind == "number" || expression.kind == "string" || expression.kind == "identifier")
    return true;
  if (expression.kind == "indexed")
    return expression.index != nullptr && expression_safe_to_drop(*expression.index);
  if (expression.kind == "unary")
    return expression.expr != nullptr && expression_safe_to_drop(*expression.expr);
  if (expression.kind == "binary")
    return expression.op != "/" && expression.left != nullptr && expression.right != nullptr &&
           expression_safe_to_drop(*expression.left) && expression_safe_to_drop(*expression.right);
  return false;
}

std::string expression_key(const Expression& expression) {
  if (expression.kind == "string")
    return "string:" + expression.text;
  if (expression.kind == "number")
    return "number:" + lower_ascii(expression.raw.empty() ? expression.text : expression.raw);
  if (expression.kind == "identifier")
    return "identifier:" + expression.name;
  if (expression.kind == "indexed") {
    return "indexed:" + expression.base + ":" + expression.field.value_or("") + ":" +
           (expression.index == nullptr ? "" : expression_key(*expression.index));
  }
  if (expression.kind == "unary")
    return "unary:" + expression.op + ":" +
           (expression.expr == nullptr ? "" : expression_key(*expression.expr));
  if (expression.kind == "binary") {
    return "binary:" + expression.op + ":" +
           (expression.left == nullptr ? "" : expression_key(*expression.left)) + ":" +
           (expression.right == nullptr ? "" : expression_key(*expression.right));
  }
  if (expression.kind == "call") {
    std::string key = "call:" + expression.callee + ":";
    for (std::size_t index = 0; index < expression.args.size(); ++index) {
      if (index != 0)
        key += ",";
      key += expression_key(expression.args.at(index));
    }
    return key;
  }
  return expression.kind;
}

std::optional<Expression> negate_number_expression(const Expression& expression) {
  const std::optional<DecimalValue> value = decimal_from_expression(expression);
  if (!value.has_value())
    return std::nullopt;
  return decimal_number_expression(negate_decimal(*value));
}

Expression multiply_expressions(Expression left, Expression right) {
  if (is_numeric_value(left, 1))
    return right;
  if (is_numeric_value(right, 1))
    return left;
  return multiply_expression(std::move(left), std::move(right));
}

Expression add_expressions(Expression left, Expression right);

Expression subtract_expressions(Expression left, Expression right) {
  if (is_numeric_value(right, 0))
    return left;
  if (is_numeric_value(left, 0))
    return unary_expression("-", std::move(right));
  const std::optional<double> right_value = numeric_literal_value(right);
  if (right_value.has_value() && *right_value < 0 && right.kind == "number") {
    if (std::optional<Expression> positive = negate_number_expression(right))
      return add_expression(std::move(left), std::move(*positive));
  }
  return subtract_expression(std::move(left), std::move(right));
}

Expression add_expressions(Expression left, Expression right) {
  if (is_numeric_value(left, 0))
    return right;
  if (is_numeric_value(right, 0))
    return left;
  const std::optional<double> right_value = numeric_literal_value(right);
  if (right_value.has_value() && *right_value < 0 && right.kind == "number") {
    if (std::optional<Expression> positive = negate_number_expression(right))
      return subtract_expressions(std::move(left), std::move(*positive));
  }
  return add_expression(std::move(left), std::move(right));
}

bool expression_begins_with_number_entry(const Expression& expression) {
  if (expression.kind == "number")
    return true;
  if (expression.kind == "unary")
    return expression.expr != nullptr &&
           (expression.expr->kind == "number" || expression_begins_with_number_entry(*expression.expr));
  if (expression.kind == "binary")
    return expression.left != nullptr && expression_begins_with_number_entry(*expression.left);
  if (expression.kind == "call")
    return !expression.args.empty() && expression_begins_with_number_entry(expression.args.front());
  return false;
}

std::optional<std::pair<Expression, bool>> signed_term_expression(const LinearTerm& term) {
  const DecimalValue coeff = normalize_decimal(term.coeff);
  if (is_decimal_zero(coeff))
    return std::nullopt;
  const bool negative = coeff.num < 0;
  const DecimalValue magnitude = abs_decimal(coeff);
  std::optional<Expression> coefficient = decimal_number_expression(magnitude);
  if (!coefficient.has_value())
    return std::nullopt;
  return std::pair<Expression, bool>{
      is_decimal_one(magnitude) ? term.expr : multiply_expressions(*coefficient, term.expr),
      negative,
  };
}

std::optional<Expression> build_linear_expression(const LinearForm& form) {
  std::vector<std::pair<Expression, bool>> signed_terms;
  for (const auto& [_, term] : form.terms) {
    if (std::optional<std::pair<Expression, bool>> signed_term = signed_term_expression(term))
      signed_terms.push_back(std::move(*signed_term));
  }

  const DecimalValue constant = normalize_decimal(form.constant);
  const int constant_sign = decimal_sign(constant);
  if (signed_terms.empty())
    return decimal_number_expression(constant);

  const bool has_positive_term =
      std::any_of(signed_terms.begin(), signed_terms.end(),
                  [](const std::pair<Expression, bool>& term) { return !term.second; });
  const bool first_term_leads_with_literal =
      !signed_terms.empty() &&
      expression_begins_with_number_entry(signed_terms.front().second
                                              ? unary_expression("-", signed_terms.front().first)
                                              : signed_terms.front().first);
  const bool start_with_constant =
      (constant_sign > 0 || (constant_sign < 0 && !has_positive_term)) &&
      !first_term_leads_with_literal;

  std::optional<Expression> result;
  if (start_with_constant && constant_sign != 0) {
    result = decimal_number_expression(constant);
    if (!result.has_value())
      return std::nullopt;
  }

  for (const auto& term : signed_terms) {
    if (!result.has_value()) {
      result = term.second ? unary_expression("-", term.first) : term.first;
    } else {
      result =
          term.second ? subtract_expressions(std::move(*result), term.first)
                      : add_expressions(std::move(*result), term.first);
    }
  }

  if (!result.has_value())
    return decimal_number_expression(constant);
  if (!start_with_constant && constant_sign != 0) {
    std::optional<Expression> constant_expr = decimal_number_expression(abs_decimal(constant));
    if (!constant_expr.has_value())
      return std::nullopt;
    result = constant_sign < 0 ? subtract_expressions(std::move(*result), std::move(*constant_expr))
                               : add_expressions(std::move(*result), std::move(*constant_expr));
  }
  return result;
}

LinearForm single_term(Expression expression) {
  const std::string key = expression_key(expression);
  return LinearForm{.constant = decimal(0),
                    .terms = {{key, LinearTerm{.expr = std::move(expression), .coeff = decimal(1)}}}};
}

bool add_linear_term(LinearForm& form, const std::string& key, Expression expr,
                     DecimalValue coeff) {
  DecimalValue next = coeff;
  const auto it = std::find_if(form.terms.begin(), form.terms.end(),
                               [&](const auto& entry) { return entry.first == key; });
  if (it != form.terms.end()) {
    std::optional<DecimalValue> sum = add_decimal(it->second.coeff, coeff);
    if (!sum.has_value())
      return false;
    next = *sum;
  }
  if (is_decimal_zero(next)) {
    if (it != form.terms.end())
      form.terms.erase(it);
    return true;
  }
  if (it == form.terms.end())
    form.terms.emplace_back(key, LinearTerm{.expr = std::move(expr), .coeff = next});
  else
    it->second.coeff = next;
  return true;
}

std::optional<LinearForm> add_linear_forms(LinearForm left, const LinearForm& right) {
  std::optional<DecimalValue> constant = add_decimal(left.constant, right.constant);
  if (!constant.has_value())
    return std::nullopt;
  left.constant = *constant;
  for (const auto& [key, term] : right.terms) {
    if (!add_linear_term(left, key, term.expr, term.coeff))
      return std::nullopt;
  }
  return left;
}

std::optional<LinearForm> scale_linear_form(const LinearForm& form, DecimalValue factor) {
  factor = normalize_decimal(factor);
  if (is_decimal_zero(factor))
    return LinearForm{.constant = decimal(0), .terms = {}};
  std::vector<std::pair<std::string, LinearTerm>> terms;
  for (const auto& [key, term] : form.terms) {
    std::optional<DecimalValue> coeff = multiply_decimal(term.coeff, factor);
    if (!coeff.has_value())
      return std::nullopt;
    terms.emplace_back(key, LinearTerm{.expr = term.expr, .coeff = *coeff});
  }
  std::optional<DecimalValue> constant = multiply_decimal(form.constant, factor);
  if (!constant.has_value())
    return std::nullopt;
  return LinearForm{.constant = *constant, .terms = std::move(terms)};
}

std::optional<LinearForm> linearize_expression(const Expression& expression) {
  if (!expression_pure_for_folding(expression))
    return std::nullopt;
  if (expression.kind == "string")
    return std::nullopt;
  if (expression.kind == "number") {
    std::optional<DecimalValue> value = decimal_from_expression(expression);
    return value.has_value() ? std::optional<LinearForm>{LinearForm{.constant = *value, .terms = {}}}
                             : std::optional<LinearForm>{single_term(expression)};
  }
  if (expression.kind == "identifier" || expression.kind == "indexed" || expression.kind == "call")
    return single_term(expression);
  if (expression.kind == "unary" && expression.expr != nullptr) {
    std::optional<LinearForm> inner = linearize_expression(*expression.expr);
    return inner.has_value() ? scale_linear_form(*inner, decimal(-1)) : std::nullopt;
  }
  if (expression.kind == "binary" && expression.left != nullptr && expression.right != nullptr) {
    if (expression.op == "+") {
      std::optional<LinearForm> left = linearize_expression(*expression.left);
      std::optional<LinearForm> right = linearize_expression(*expression.right);
      return left.has_value() && right.has_value() ? add_linear_forms(std::move(*left), *right)
                                                   : std::nullopt;
    }
    if (expression.op == "-") {
      std::optional<LinearForm> left = linearize_expression(*expression.left);
      std::optional<LinearForm> right = linearize_expression(*expression.right);
      if (!left.has_value() || !right.has_value())
        return std::nullopt;
      std::optional<LinearForm> negated = scale_linear_form(*right, decimal(-1));
      return negated.has_value() ? add_linear_forms(std::move(*left), *negated) : std::nullopt;
    }
    if (expression.op == "*") {
      if (std::optional<DecimalValue> left_factor = decimal_from_expression(*expression.left)) {
        std::optional<LinearForm> right = linearize_expression(*expression.right);
        return right.has_value() ? scale_linear_form(*right, *left_factor) : std::nullopt;
      }
      if (std::optional<DecimalValue> right_factor = decimal_from_expression(*expression.right)) {
        std::optional<LinearForm> left = linearize_expression(*expression.left);
        return left.has_value() ? scale_linear_form(*left, *right_factor) : std::nullopt;
      }
      return single_term(expression);
    }
    if (expression.op == "/") {
      std::optional<DecimalValue> divisor = decimal_from_expression(*expression.right);
      if (!divisor.has_value())
        return single_term(expression);
      std::optional<DecimalValue> reciprocal = divide_decimal(decimal(1), *divisor);
      if (!reciprocal.has_value())
        return std::nullopt;
      std::optional<LinearForm> left = linearize_expression(*expression.left);
      return left.has_value() ? scale_linear_form(*left, *reciprocal) : std::nullopt;
    }
  }
  return std::nullopt;
}

std::optional<Expression> fold_linear_expression(const Expression& expression) {
  if (!expression_pure_for_folding(expression))
    return std::nullopt;
  std::optional<LinearForm> form = linearize_expression(expression);
  if (!form.has_value())
    return std::nullopt;
  std::optional<Expression> rebuilt = build_linear_expression(*form);
  if (!rebuilt.has_value() || expression_key(*rebuilt) == expression_key(expression))
    return std::nullopt;
  return rebuilt;
}

void collect_signed_add_sub_terms(const Expression& expression, bool negative,
                                  std::vector<SignedExpressionTerm>& terms) {
  if (expression.kind == "unary" && expression.op == "-" && expression.expr != nullptr) {
    collect_signed_add_sub_terms(*expression.expr, !negative, terms);
    return;
  }
  if (expression.kind == "binary" && expression.left != nullptr && expression.right != nullptr &&
      (expression.op == "+" || expression.op == "-")) {
    collect_signed_add_sub_terms(*expression.left, negative, terms);
    collect_signed_add_sub_terms(*expression.right, expression.op == "-" ? !negative : negative,
                                 terms);
    return;
  }
  terms.push_back(SignedExpressionTerm{.expr = expression, .negative = negative});
}

std::optional<Expression> build_signed_add_sub_expression(
    const std::vector<SignedExpressionTerm>& terms) {
  std::optional<Expression> result;
  for (const SignedExpressionTerm& term : terms) {
    if (!result.has_value()) {
      result = term.negative ? unary_expression("-", term.expr) : term.expr;
      continue;
    }
    result = term.negative ? subtract_expressions(std::move(*result), term.expr)
                           : add_expressions(std::move(*result), term.expr);
  }
  return result;
}

std::optional<Expression> fold_signed_add_sub_expression(const Expression& expression) {
  if (!expression_pure_for_folding(expression))
    return std::nullopt;
  std::vector<SignedExpressionTerm> terms;
  collect_signed_add_sub_terms(expression, false, terms);
  if (terms.size() < 2)
    return std::nullopt;
  std::optional<Expression> rebuilt = build_signed_add_sub_expression(terms);
  if (!rebuilt.has_value() || expression_key(*rebuilt) == expression_key(expression))
    return std::nullopt;
  return rebuilt;
}

std::optional<DecimalValue> truncate_decimal(DecimalValue value) {
  value = normalize_decimal(value);
  if (value.scale == 0)
    return value;
  const std::optional<__int128> divisor = pow10_int(value.scale);
  if (!divisor.has_value())
    return std::nullopt;
  return normalize_decimal(DecimalValue{.num = value.num / *divisor, .scale = 0});
}

std::optional<DecimalValue> fractional_decimal(DecimalValue value) {
  std::optional<DecimalValue> integer = truncate_decimal(value);
  return integer.has_value() ? subtract_decimal(value, *integer) : std::nullopt;
}

std::optional<DecimalValue> pow10_decimal(DecimalValue exponent) {
  exponent = normalize_decimal(exponent);
  if (exponent.scale != 0 || exponent.num < -kMaxLiteralExponent ||
      exponent.num > kMaxLiteralExponent) {
    return std::nullopt;
  }
  const int power = static_cast<int>(exponent.num);
  if (power >= 0) {
    std::optional<__int128> num = pow10_int(power);
    return num.has_value() ? std::optional<DecimalValue>{normalize_decimal(
                                  DecimalValue{.num = *num, .scale = 0})}
                           : std::nullopt;
  }
  return normalize_decimal(DecimalValue{.num = 1, .scale = -power});
}

std::optional<DecimalValue> pow_decimal_integer(DecimalValue base, DecimalValue exponent) {
  exponent = normalize_decimal(exponent);
  if (decimal_sign(base) <= 0 || exponent.scale != 0 ||
      exponent.num < -kMaxLiteralExponent || exponent.num > kMaxLiteralExponent) {
    return std::nullopt;
  }
  const int magnitude = static_cast<int>(abs_int128(exponent.num));
  DecimalValue result = decimal(1);
  for (int index = 0; index < magnitude; ++index) {
    std::optional<DecimalValue> next = multiply_decimal(result, base);
    if (!next.has_value() || next->scale > kMaxFoldedDecimalScale)
      return std::nullopt;
    result = *next;
  }
  return exponent.num >= 0 ? std::optional<DecimalValue>{result}
                           : divide_decimal(decimal(1), result);
}

int compare_decimal(DecimalValue left, DecimalValue right) {
  const int scale = std::max(left.scale, right.scale);
  const std::optional<__int128> left_multiplier = pow10_int(scale - left.scale);
  const std::optional<__int128> right_multiplier = pow10_int(scale - right.scale);
  if (!left_multiplier.has_value() || !right_multiplier.has_value())
    return 0;
  const __int128 lhs = left.num * *left_multiplier;
  const __int128 rhs = right.num * *right_multiplier;
  if (lhs < rhs)
    return -1;
  if (lhs > rhs)
    return 1;
  return 0;
}

std::optional<Expression> fold_pure_constant_call(const std::string& callee,
                                                  const std::vector<Expression>& args,
                                                  bool grd_angle_mode,
                                                  int& grd_assumptions) {
  const std::string name = lower_ascii(callee);
  std::vector<DecimalValue> values;
  values.reserve(args.size());
  for (const Expression& arg : args) {
    std::optional<DecimalValue> value = decimal_from_expression(arg);
    if (!value.has_value())
      return std::nullopt;
    values.push_back(*value);
  }

  if (grd_angle_mode && values.size() == 1U) {
    if (name == "acos" && is_decimal_zero(values.front())) {
      ++grd_assumptions;
      return number_expression("100");
    }
    if (name == "cos" && compare_decimal(values.front(), decimal(100)) == 0) {
      ++grd_assumptions;
      return number_expression("0");
    }
  }

  std::optional<DecimalValue> result;
  if (name == "abs" && values.size() == 1U)
    result = abs_decimal(values.front());
  else if (name == "sign" && values.size() == 1U)
    result = decimal(decimal_sign(values.front()));
  else if (name == "int" && values.size() == 1U)
    result = truncate_decimal(values.front());
  else if (name == "frac" && values.size() == 1U)
    result = fractional_decimal(values.front());
  else if (name == "sqr" && values.size() == 1U)
    result = multiply_decimal(values.front(), values.front());
  else if (name == "inv" && values.size() == 1U)
    result = divide_decimal(decimal(1), values.front());
  else if (name == "pow10" && values.size() == 1U)
    result = pow10_decimal(values.front());
  else if (name == "pow" && values.size() == 2U)
    result = pow_decimal_integer(values.at(0), values.at(1));
  else if (name == "max" && values.size() == 2U)
    result = (is_decimal_zero(values.at(0)) || is_decimal_zero(values.at(1)))
                 ? decimal(0)
                 : (compare_decimal(values.at(0), values.at(1)) >= 0 ? values.at(0)
                                                                      : values.at(1));
  else if (name == "min" && values.size() == 2U)
    result = (is_decimal_zero(values.at(0)) || is_decimal_zero(values.at(1)))
                 ? decimal(0)
                 : (compare_decimal(values.at(0), values.at(1)) <= 0 ? values.at(0)
                                                                      : values.at(1));
  else if (name == "safe_max" && values.size() == 2U)
    result = compare_decimal(values.at(0), values.at(1)) >= 0 ? values.at(0) : values.at(1);
  else if (name == "safe_min" && values.size() == 2U)
    result = compare_decimal(values.at(0), values.at(1)) <= 0 ? values.at(0) : values.at(1);
  else if (name == "bit_and" && values.size() == 2U)
    result = fold_bitwise(values.at(0), values.at(1), "and");
  else if (name == "bit_or" && values.size() == 2U)
    result = fold_bitwise(values.at(0), values.at(1), "or");
  else if (name == "bit_xor" && values.size() == 2U)
    result = fold_bitwise(values.at(0), values.at(1), "xor");
  else if (name == "bit_not" && values.size() == 1U)
    result = fold_bitwise_not(values.front());

  return result.has_value() ? decimal_number_expression(*result) : std::nullopt;
}

class ConstantFolder {
 public:
  ConstantFolder(bool grd_angle_mode, const std::map<std::string, Expression>& constants)
      : grd_angle_mode_(grd_angle_mode), constants_(constants) {}

  ConstantFoldResult fold_program(V2Program& program) {
    for (V2StateField& field : program.state) {
      if (field.initial.has_value())
        fold_expression_text(*field.initial, field.line);
    }
    for (V2Statement& statement : program.body)
      fold_statement(statement);
    for (V2Rule& rule : program.rules) {
      for (V2Statement& statement : rule.body)
        fold_statement(statement);
    }
    return result_;
  }

 private:
  bool grd_angle_mode_ = false;
  const std::map<std::string, Expression>& constants_;
  ConstantFoldResult result_;

  Expression folded(Expression expression) {
    ++result_.applied;
    return expression;
  }

  Expression fold_expression(Expression expression) {
    if (expression.kind == "number" || expression.kind == "string") {
      return expression;
    }
    if (expression.kind == "identifier") {
      const auto constant = constants_.find(expression.name);
      return constant == constants_.end() ? expression : constant->second;
    }
    if (expression.kind == "indexed" && expression.index != nullptr) {
      Expression index = fold_expression(*expression.index);
      expression.index = std::make_shared<Expression>(std::move(index));
      return expression;
    }
    if (expression.kind == "unary" && expression.expr != nullptr) {
      Expression inner = fold_expression(*expression.expr);
      if (inner.kind == "number") {
        if (std::optional<Expression> negated = negate_number_expression(inner))
          return folded(*negated);
      }
      if (inner.kind == "unary" && inner.op == "-" && inner.expr != nullptr)
        return folded(*inner.expr);
      expression.expr = std::make_shared<Expression>(std::move(inner));
      return expression;
    }
    if (expression.kind == "call") {
      for (Expression& arg : expression.args)
        arg = fold_expression(arg);
      int grd_assumptions = 0;
      if (std::optional<Expression> constant =
              fold_pure_constant_call(expression.callee, expression.args, grd_angle_mode_,
                                      grd_assumptions)) {
        result_.grd_angle_assumptions += grd_assumptions;
        return folded(*constant);
      }
      return expression;
    }
    if (expression.kind == "binary" && expression.left != nullptr && expression.right != nullptr) {
      Expression left = fold_expression(*expression.left);
      Expression right = fold_expression(*expression.right);
      if (std::optional<Expression> folded_binary = fold_binary(expression.op, left, right))
        return folded(*folded_binary);
      expression.left = std::make_shared<Expression>(std::move(left));
      expression.right = std::make_shared<Expression>(std::move(right));
      return expression;
    }
    return expression;
  }

  std::optional<Expression> fold_binary(const std::string& op, const Expression& left,
                                        const Expression& right) {
    if (std::optional<Expression> numeric = fold_numeric_binary(op, left, right);
        numeric.has_value() && estimate_expression_cost(*numeric) <= estimate_binary_cost(left, right)) {
      return numeric;
    }

    if (op == "+") {
      if (is_numeric_value(left, 0))
        return right;
      if (is_numeric_value(right, 0))
        return left;
      Expression expression = add_expression(left, right);
      if (std::optional<Expression> signed_expr = fold_signed_add_sub_expression(expression);
          signed_expr.has_value() &&
          estimate_expression_cost(*signed_expr) < estimate_binary_cost(left, right)) {
        return signed_expr;
      }
      if (std::optional<Expression> linear = fold_linear_expression(expression);
          linear.has_value() &&
          estimate_expression_cost(*linear) <= estimate_binary_cost(left, right)) {
        return linear;
      }
      return std::nullopt;
    }
    if (op == "-") {
      if (is_numeric_value(right, 0))
        return left;
      if (is_numeric_value(left, 0))
        return unary_expression("-", right);
      Expression expression = subtract_expression(left, right);
      if (std::optional<Expression> signed_expr = fold_signed_add_sub_expression(expression);
          signed_expr.has_value() &&
          estimate_expression_cost(*signed_expr) < estimate_binary_cost(left, right)) {
        return signed_expr;
      }
      if (std::optional<Expression> linear = fold_linear_expression(expression);
          linear.has_value() &&
          estimate_expression_cost(*linear) <= estimate_binary_cost(left, right)) {
        return linear;
      }
      return std::nullopt;
    }
    if (op == "*") {
      if (is_numeric_value(left, 1))
        return right;
      if (is_numeric_value(right, 1))
        return left;
      if (is_numeric_value(left, 0) && expression_safe_to_drop(right))
        return number_expression("0");
      if (is_numeric_value(right, 0) && expression_safe_to_drop(left))
        return number_expression("0");
      Expression expression = multiply_expression(left, right);
      if (std::optional<Expression> linear = fold_linear_expression(expression);
          linear.has_value() &&
          estimate_expression_cost(*linear) <= estimate_binary_cost(left, right)) {
        return linear;
      }
      return std::nullopt;
    }
    if (op == "/") {
      if (is_numeric_value(right, 1))
        return left;
      Expression expression = divide_expression(left, right);
      if (std::optional<Expression> linear = fold_linear_expression(expression);
          linear.has_value() &&
          estimate_expression_cost(*linear) <= estimate_binary_cost(left, right)) {
        return linear;
      }
      return std::nullopt;
    }
    return std::nullopt;
  }

  std::optional<Expression> fold_numeric_binary(const std::string& op, const Expression& left,
                                                const Expression& right) {
    std::optional<DecimalValue> lhs = decimal_from_expression(left);
    std::optional<DecimalValue> rhs = decimal_from_expression(right);
    if (!lhs.has_value() || !rhs.has_value())
      return std::nullopt;
    std::optional<DecimalValue> result;
    if (op == "+")
      result = add_decimal(*lhs, *rhs);
    else if (op == "-")
      result = subtract_decimal(*lhs, *rhs);
    else if (op == "*")
      result = multiply_decimal(*lhs, *rhs);
    else if (op == "/")
      result = divide_decimal(*lhs, *rhs);
    return result.has_value() ? decimal_number_expression(*result) : std::nullopt;
  }

  void fold_expression_text(std::optional<std::string>& text, int line) {
    if (!text.has_value())
      return;
    fold_expression_text(*text, line);
  }

  void fold_expression_text(std::string& text, int line) {
    if (text.empty())
      return;
    try {
      Expression parsed = parse_expression(text, line);
      Expression next = fold_expression(std::move(parsed));
      text = expression_to_source(next);
    } catch (const std::exception&) {
      return;
    }
  }

  void fold_display_items(std::vector<DisplayItem>& items) {
    for (DisplayItem& item : items) {
      if (!item.expr.has_value())
        continue;
      item.expr = fold_expression(std::move(*item.expr));
      item.name = expression_to_source(*item.expr);
    }
  }

  void fold_statement(V2Statement& statement) {
    fold_expression_text(statement.target, statement.line);
    fold_expression_text(statement.expr, statement.line);
    for (std::string& arg : statement.args)
      fold_expression_text(arg, statement.line);
    if (statement.predicate.has_value()) {
      fold_expression_text(statement.predicate->left, statement.line);
      fold_expression_text(statement.predicate->right, statement.line);
      fold_expression_text(statement.predicate->collection, statement.line);
      fold_expression_text(statement.predicate->item, statement.line);
    }
    if (statement.items.has_value())
      fold_display_items(*statement.items);
    for (V2RawInput& input : statement.inputs)
      fold_expression_text(input.expr, input.line);
    for (V2Statement& child : statement.body)
      fold_statement(child);
    for (V2Statement& child : statement.then_body)
      fold_statement(child);
    for (V2Statement& child : statement.else_body)
      fold_statement(child);
    for (V2MatchCase& match_case : statement.cases) {
      for (std::string& value : match_case.values)
        fold_expression_text(value, match_case.line);
      if (match_case.action != nullptr)
        fold_statement(*match_case.action);
    }
    if (statement.otherwise != nullptr)
      fold_statement(*statement.otherwise);
  }
};

}  // namespace

ConstantFoldResult fold_program_constants(V2Program& program,
                                          const std::map<std::string, Expression>& constants) {
  return ConstantFolder(program.angle_mode.has_value() && program.angle_mode->mode == "grd",
                        constants)
      .fold_program(program);
}

}  // namespace mkpro::core
