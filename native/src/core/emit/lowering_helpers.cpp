#include "mkpro/core/emit/lowering_helpers.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mkpro::core::emit {

namespace {

std::optional<char32_t> next_utf8_codepoint(std::string_view text, std::size_t& offset) {
  if (offset >= text.size())
    return std::nullopt;

  const unsigned char first = static_cast<unsigned char>(text[offset++]);
  if (first < 0x80)
    return first;

  int extra_bytes = 0;
  char32_t codepoint = 0;
  if ((first & 0xe0U) == 0xc0U) {
    extra_bytes = 1;
    codepoint = first & 0x1fU;
  } else if ((first & 0xf0U) == 0xe0U) {
    extra_bytes = 2;
    codepoint = first & 0x0fU;
  } else if ((first & 0xf8U) == 0xf0U) {
    extra_bytes = 3;
    codepoint = first & 0x07U;
  } else {
    return std::nullopt;
  }

  for (int index = 0; index < extra_bytes; ++index) {
    if (offset >= text.size())
      return std::nullopt;
    const unsigned char next = static_cast<unsigned char>(text[offset++]);
    if ((next & 0xc0U) != 0x80U)
      return std::nullopt;
    codepoint = (codepoint << 6U) | (next & 0x3fU);
  }
  return codepoint;
}

bool is_error_literal_cells(const std::vector<int>& cells) {
  return cells.size() == 5U && cells[0] == 14 && cells[1] == 13 && cells[2] == 13 &&
         cells[3] == 0 && cells[4] == 13;
}

std::optional<std::string> display_literal_inversion_digits(const std::vector<int>& cells) {
  if (cells.empty() || cells[0] != 8)
    return std::nullopt;
  std::string digits = "1";
  for (std::size_t index = 1; index < cells.size(); ++index) {
    const int cell = cells[index];
    if (cell < 6 || cell > 15)
      return std::nullopt;
    digits += static_cast<char>('0' + (15 - cell));
  }
  return digits;
}

std::optional<std::pair<int, int>> decimal_xor_pair(int value) {
  for (int left = 0; left <= 9; ++left) {
    for (int right = 0; right <= 9; ++right) {
      if ((left ^ right) == value)
        return std::pair<int, int>{left, right};
    }
  }
  return std::nullopt;
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool expression_equals(const Expression& left, const Expression& right) {
  return expression_to_json(left) == expression_to_json(right);
}

bool expression_pure_for_remainder_substitution(const Expression& expression) {
  if (expression.kind == "call" && lower_ascii(expression.callee) == "read")
    return false;
  if (expression.index != nullptr && !expression_pure_for_remainder_substitution(*expression.index))
    return false;
  if (expression.expr != nullptr && !expression_pure_for_remainder_substitution(*expression.expr))
    return false;
  if (expression.left != nullptr && !expression_pure_for_remainder_substitution(*expression.left))
    return false;
  if (expression.right != nullptr && !expression_pure_for_remainder_substitution(*expression.right))
    return false;
  return std::all_of(expression.args.begin(), expression.args.end(), [](const Expression& arg) {
    return expression_pure_for_remainder_substitution(arg);
  });
}

std::optional<double> numeric_literal_value(const Expression& expression) {
  if (expression.kind == "unary" && expression.op == "-" && expression.expr != nullptr) {
    const std::optional<double> value = numeric_literal_value(*expression.expr);
    return value.has_value() ? std::optional<double>{-*value} : std::nullopt;
  }
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

std::optional<RemainderByConstantMatch> match_int_divide_by_constant(const Expression& expression) {
  if (expression.kind != "call" || lower_ascii(expression.callee) != "int" ||
      expression.args.size() != 1U) {
    return std::nullopt;
  }
  const Expression& divided = expression.args.front();
  if (divided.kind != "binary" || divided.op != "/" || divided.left == nullptr ||
      divided.right == nullptr || !numeric_literal_value(*divided.right).has_value()) {
    return std::nullopt;
  }
  return RemainderByConstantMatch{.value = *divided.left, .divisor = *divided.right};
}

} // namespace

std::string coord_list_item_name(const std::string& list_name, int index) {
  return std::string(k_coord_list_item_prefix) + list_name + "_" + std::to_string(index);
}

std::optional<CoordListItemInfo> coord_list_item_info(std::string_view name) {
  if (!name.starts_with(k_coord_list_item_prefix))
    return std::nullopt;
  static const std::regex pattern(R"(^__coord_list_(.+)_(\d+)$)");
  std::cmatch match;
  if (!std::regex_match(name.begin(), name.end(), match, pattern))
    return std::nullopt;
  return CoordListItemInfo{
      .list_name = match[1].str(),
      .index = std::stoi(match[2].str()),
  };
}

std::string segmented_bitplane_name(const std::string& collection, int index) {
  return std::string(k_segmented_bitplane_prefix) + collection + "_" + std::to_string(index);
}

std::vector<std::string> segmented_bitplane_names(const std::string& collection) {
  std::vector<std::string> names;
  names.reserve(4);
  for (int index = 0; index < 4; ++index)
    names.push_back(segmented_bitplane_name(collection, index));
  return names;
}

std::string spatial_count_total_scratch_name() {
  return std::string(k_spatial_count_scratch_prefix) + "total";
}

std::string spatial_count_line_scratch_name() {
  return std::string(k_spatial_count_scratch_prefix) + "line";
}

std::string spatial_count_offset_scratch_name() {
  return std::string(k_spatial_count_scratch_prefix) + "offset";
}

std::string spatial_count_counter_scratch_name() {
  return std::string(k_spatial_count_scratch_prefix) + "counter";
}

bool is_unsigned_decimal_digits(std::string_view text) {
  return !text.empty() &&
         std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isdigit(ch); });
}

std::optional<std::string> normalize_display_literal_text(std::string_view text) {
  std::string normalized;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const std::optional<char32_t> codepoint = next_utf8_codepoint(text, offset);
    if (!codepoint.has_value())
      return std::nullopt;

    switch (*codepoint) {
    case U'\u2013':
    case U'\u2014':
      normalized.push_back('-');
      break;
    case U'\u041e':
    case U'O':
      normalized.push_back('0');
      break;
    case U'\u041b':
    case U'\u0412':
    case U'B':
      normalized.push_back('L');
      break;
    case U'\u0421':
    case U'C':
      normalized.push_back('C');
      break;
    case U'\u0413':
    case U'D':
    case U'G':
      normalized.push_back('D');
      break;
    case U'\u0415':
    case U'E':
      normalized.push_back('E');
      break;
    default:
      if (*codepoint < 0x80U) {
        normalized.push_back(static_cast<char>(*codepoint));
      } else {
        return std::nullopt;
      }
      break;
    }
  }
  return normalized;
}

std::optional<std::vector<int>> display_literal_cells(std::string_view text) {
  const std::optional<std::string> normalized = normalize_display_literal_text(text);
  if (!normalized.has_value())
    return std::nullopt;

  std::vector<int> cells;
  for (char raw_ch : *normalized) {
    const unsigned char ch = static_cast<unsigned char>(raw_ch);
    if (ch == '.' || ch == ',')
      continue;
    if (std::isspace(ch) != 0 || ch == '_') {
      cells.push_back(15);
      continue;
    }
    if (std::isdigit(ch) != 0) {
      cells.push_back(ch - '0');
      continue;
    }
    if (ch == '-') {
      cells.push_back(10);
    } else if (ch == 'L') {
      cells.push_back(11);
    } else if (ch == 'C') {
      cells.push_back(12);
    } else if (ch == 'D') {
      cells.push_back(13);
    } else if (ch == 'E') {
      cells.push_back(14);
    } else {
      return std::nullopt;
    }
  }
  return cells;
}

std::optional<DisplayLiteralProgram>
display_literal_program_from_cells(const std::optional<std::vector<int>>& cells, bool negative) {
  if (!cells.has_value() || cells->empty() || cells->size() > 8U)
    return std::nullopt;
  if (cells->at(0) != 8)
    return std::nullopt;

  if (const std::optional<std::string> inverted = display_literal_inversion_digits(*cells)) {
    return DisplayLiteralProgram{
        .kind = "kinv",
        .digits = *inverted,
        .negative = negative,
    };
  }

  std::string left;
  std::string right;
  for (std::size_t index = 0; index < cells->size(); ++index) {
    std::pair<int, int> pair = {1, 9};
    if (index != 0) {
      const std::optional<std::pair<int, int>> resolved = decimal_xor_pair(cells->at(index));
      if (!resolved.has_value())
        return std::nullopt;
      pair = *resolved;
    }
    left += static_cast<char>('0' + pair.first);
    right += static_cast<char>('0' + pair.second);
  }
  return DisplayLiteralProgram{
      .kind = "xor",
      .left = std::move(left),
      .right = std::move(right),
      .negative = negative,
  };
}

std::optional<DisplayLiteralProgram> display_literal_program(std::string_view text) {
  const std::optional<std::string> normalized = normalize_display_literal_text(text);
  if (!normalized.has_value())
    return std::nullopt;
  const std::optional<std::vector<int>> error_cells = display_literal_cells(*normalized);
  if (error_cells.has_value() && is_error_literal_cells(*error_cells))
    return DisplayLiteralProgram{.kind = "error"};

  const bool negative = normalized->size() > 1U && normalized->front() == '-';
  const std::string body = negative ? normalized->substr(1) : *normalized;
  return display_literal_program_from_cells(display_literal_cells(body), negative);
}

std::optional<std::string> decimal_display_literal_number(std::string_view text) {
  const std::optional<std::string> normalized = normalize_display_literal_text(text);
  if (!normalized.has_value())
    return std::nullopt;
  static const std::regex decimal_regex(R"(^-?(?:0|[1-9][0-9]{0,7})$)");
  if (!std::regex_match(*normalized, decimal_regex))
    return std::nullopt;
  return normalized;
}

std::optional<LeadingZeroHexProductPlan>
leading_zero_hex_product_display_program(std::string_view text) {
  struct Row {
    std::string_view source_literal;
    int factor = 0;
    std::string_view output;
  };
  static constexpr std::array<Row, 28> kRows{{
      {"-", 10, "00"},  {"-", 12, "04"},  {"-", 14, "08"},  {"-", 16, "000"}, {"-", 17, "010"},
      {"-", 18, "020"}, {"-", 19, "030"}, {"-", 35, "030"}, {"-", 36, "040"}, {"-", 37, "050"},
      {"L", 15, "021"}, {"L", 16, "032"}, {"L", 17, "043"}, {"L", 18, "054"}, {"L", 29, "015"},
      {"C", 15, "052"}, {"C", 26, "024"}, {"C", 27, "020"}, {"C", 28, "032"}, {"C", 29, "044"},
      {"D", 25, "053"}, {"D", 26, "050"}, {"D", 37, "033"}, {"D", 38, "030"}, {"D", 39, "043"},
      {"E", 35, "042"}, {"E", 36, "040"}, {"E", 37, "054"},
  }};

  const std::optional<std::string> normalized = normalize_display_literal_text(text);
  if (!normalized.has_value())
    return std::nullopt;
  const auto it = std::find_if(kRows.begin(), kRows.end(),
                               [&](const Row& row) { return row.output == *normalized; });
  if (it == kRows.end())
    return std::nullopt;
  return LeadingZeroHexProductPlan{
      .source_literal = std::string(it->source_literal),
      .factor = std::to_string(it->factor),
  };
}

std::optional<ZeroDigitTailDisplayProgram> zero_digit_tail_display_program(std::string_view text) {
  const std::optional<std::vector<int>> cells = display_literal_cells(text);
  if (!cells.has_value() || cells->size() != 2U)
    return std::nullopt;
  const int sign_digit = cells->at(0);
  const int tail = cells->at(1);
  if (sign_digit < 2 || sign_digit > 9 || tail != 14)
    return std::nullopt;
  return ZeroDigitTailDisplayProgram{.input = sign_digit - 1};
}

std::optional<SignDigitLiteralDisplayProgram>
sign_digit_literal_display_program(std::string_view text) {
  const std::optional<std::vector<int>> cells = display_literal_cells(text);
  if (!cells.has_value() || cells->size() != 9U)
    return std::nullopt;

  const int sign_digit = cells->at(0);
  if (sign_digit < 2 || sign_digit > 9)
    return std::nullopt;

  const int first = cells->at(1);
  if (!((first >= 0 && first <= 9) || first == 14))
    return std::nullopt;

  std::string lower;
  lower.reserve(7);
  for (std::size_t index = 2; index < cells->size(); ++index) {
    const int cell = cells->at(index);
    if (cell < 0 || cell > 9)
      return std::nullopt;
    lower.push_back(static_cast<char>('0' + cell));
  }

  int target_lower = 0;
  const char* begin = lower.data();
  const char* end = begin + lower.size();
  const std::from_chars_result parsed = std::from_chars(begin, end, target_lower);
  if (parsed.ec != std::errc{} || parsed.ptr != end)
    return std::nullopt;

  const int indirect_steps = sign_digit - 1;
  const int start_lower = target_lower - indirect_steps;
  if (start_lower < 0 || start_lower > 9999999)
    return std::nullopt;

  std::ostringstream padded;
  padded << "1" << std::setw(7) << std::setfill('0') << start_lower;
  return SignDigitLiteralDisplayProgram{
      .sign_digit = sign_digit,
      .first = first == 14 ? "E" : std::string(1, static_cast<char>('0' + first)),
      .start = padded.str(),
      .indirect_steps = indirect_steps,
  };
}

std::optional<std::vector<int>> display_literal_mantissa_cells(std::string_view text) {
  const std::optional<std::string> normalized = normalize_display_literal_text(text);
  if (!normalized.has_value())
    return std::nullopt;
  if (normalized->find('.') != std::string::npos || normalized->find(',') != std::string::npos)
    return std::nullopt;
  return display_literal_cells(*normalized);
}

std::string display_cells_literal(const std::vector<int>& cells) {
  std::string result;
  for (int cell : cells) {
    if (cell >= 0 && cell <= 9) {
      result += static_cast<char>('0' + cell);
    } else if (cell == 10) {
      result += '-';
    } else if (cell == 11) {
      result += 'L';
    } else if (cell == 12) {
      result += 'C';
    } else if (cell == 13) {
      result += 'D';
    } else if (cell == 14) {
      result += 'E';
    } else if (cell == 15) {
      result += '_';
    }
  }
  return result;
}

std::string normalize_display_template_literal(std::string_view text) {
  std::string normalized;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const std::optional<char32_t> codepoint = next_utf8_codepoint(text, offset);
    if (!codepoint.has_value())
      return std::string(text);
    if (*codepoint < 0x80U && std::isspace(static_cast<unsigned char>(*codepoint)) != 0) {
      continue;
    }
    if (*codepoint < 0x80U) {
      normalized.push_back(static_cast<char>(*codepoint));
    } else {
      return std::string(text);
    }
  }
  return normalized;
}

std::optional<int> display_literal_point_exponent(std::string_view text) {
  const std::optional<std::string> normalized = normalize_display_literal_text(text);
  if (!normalized.has_value())
    return std::nullopt;
  const std::size_t point = normalized->find_first_of(".,");
  if (point == std::string::npos)
    return std::nullopt;
  const std::optional<std::vector<int>> prefix_cells =
      display_literal_cells(normalized->substr(0, point));
  if (!prefix_cells.has_value() || prefix_cells->empty())
    return std::nullopt;
  return static_cast<int>(prefix_cells->size()) - 1;
}

std::optional<FirstSpliceDisplayLiteralProgram>
first_splice_display_literal_program(std::string_view text) {
  const std::optional<std::vector<int>> cells = display_literal_cells(text);
  if (!cells.has_value() || cells->empty() || cells->size() > 8U)
    return std::nullopt;
  const int first = cells->front();
  if (first == 15)
    return std::nullopt;
  std::vector<int> body_cells;
  body_cells.reserve(cells->size());
  body_cells.push_back(8);
  body_cells.insert(body_cells.end(), cells->begin() + 1, cells->end());
  std::optional<DisplayLiteralProgram> body = display_literal_program_from_cells(body_cells, false);
  if (!body.has_value() || body->kind == "error")
    return std::nullopt;
  return FirstSpliceDisplayLiteralProgram{
      .first = first,
      .second = cells->size() > 1U ? std::optional<int>{cells->at(1)} : std::nullopt,
      .body = std::move(*body),
      .exponent =
          display_literal_point_exponent(text).value_or(static_cast<int>(cells->size()) - 1),
      .negative = false,
  };
}

Expression number_expression(std::string raw) {
  Expression expression;
  expression.kind = "number";
  expression.raw = std::move(raw);
  return expression;
}

Expression identifier_expression(std::string name) {
  Expression expression;
  expression.kind = "identifier";
  expression.name = std::move(name);
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

Expression unary_expression(std::string op, Expression expr) {
  Expression expression;
  expression.kind = "unary";
  expression.op = std::move(op);
  expression.expr = std::make_shared<Expression>(std::move(expr));
  return expression;
}

Expression call_expression(std::string callee, std::vector<Expression> args) {
  Expression expression;
  expression.kind = "call";
  expression.callee = std::move(callee);
  expression.args = std::move(args);
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

Expression pow10_expression(Expression expression) {
  return call_expression("pow10", {std::move(expression)});
}

Expression int_expression(Expression expression) {
  return call_expression("int", {std::move(expression)});
}

Expression frac_expression(Expression expression) {
  return call_expression("frac", {std::move(expression)});
}

Expression sign_expression(Expression expression) {
  return call_expression("sign", {std::move(expression)});
}

Expression abs_expression(Expression expression) {
  return call_expression("abs", {std::move(expression)});
}

Expression max_expression(Expression left, Expression right) {
  return call_expression("max", {std::move(left), std::move(right)});
}

Expression min_expression(const Expression& left, const Expression& right) {
  return unary_expression(
      "-", max_expression(unary_expression("-", left), unary_expression("-", right)));
}

Expression safe_max_expression(const Expression& left, const Expression& right) {
  Expression span = abs_expression(subtract_expression(left, right));
  return divide_expression(add_expression(add_expression(left, right), std::move(span)),
                           number_expression("2"));
}

Expression safe_min_expression(const Expression& left, const Expression& right) {
  Expression span = abs_expression(subtract_expression(left, right));
  return divide_expression(subtract_expression(add_expression(left, right), std::move(span)),
                           number_expression("2"));
}

Expression one_minus_expression(Expression expression) {
  return subtract_expression(number_expression("1"), std::move(expression));
}

std::string board_width_literal(int width) {
  return std::to_string(width);
}

std::string cell_mask_row_constant_literal(int width) {
  if (width == 4)
    return "0.22600029";
  throw std::runtime_error("cell_mask is only hardware-verified for board width(s) 4; width " +
                           std::to_string(width) + " needs a verified fractional constant");
}

double cell_mask_row_constant(int width) {
  if (width == 4)
    return 0.22600029;
  (void)cell_mask_row_constant_literal(width);
  return 0.0;
}

Expression grid_norm_expression(Expression expression, int width) {
  Expression rem = multiply_expression(
      frac_expression(divide_expression(int_expression(std::move(expression)),
                                        number_expression(board_width_literal(width)))),
      number_expression(board_width_literal(width)));
  Expression correction = multiply_expression(
      number_expression(board_width_literal(width)),
      one_minus_expression(sign_expression(max_expression(rem, number_expression("0")))));
  return add_expression(std::move(rem), std::move(correction));
}

Expression positive_grid_norm_expression(Expression expression, int width) {
  Expression rem = multiply_expression(
      frac_expression(divide_expression(int_expression(std::move(expression)),
                                        number_expression(board_width_literal(width)))),
      number_expression(board_width_literal(width)));
  Expression correction = multiply_expression(number_expression(board_width_literal(width)),
                                              one_minus_expression(sign_expression(rem)));
  return add_expression(std::move(rem), std::move(correction));
}

Expression bit_mask_expression(Expression index) {
  Expression nibble = int_expression(divide_expression(index, number_expression("4")));
  Expression offset =
      subtract_expression(index, multiply_expression(nibble, number_expression("4")));
  Expression bit_value = int_expression(add_expression(
      call_expression("pow", {number_expression("2"), offset}), number_expression("0.5")));
  return add_expression(number_expression("8"),
                        divide_expression(bit_value, pow10_expression(add_expression(
                                                         nibble, number_expression("1")))));
}

Expression bit_membership_expression(Expression mask, Expression index) {
  return sign_expression(frac_expression(
      call_expression("bit_and", {std::move(mask), bit_mask_expression(std::move(index))})));
}

Expression cell_mask_expression(Expression x, Expression y, int width) {
  return add_expression(
      pow10_expression(std::move(x)),
      int_expression(pow10_expression(multiply_expression(
          std::move(y), number_expression(cell_mask_row_constant_literal(width))))));
}

Expression offset_expression(Expression expression, int offset) {
  if (offset == 0)
    return expression;
  if (offset > 0)
    return add_expression(std::move(expression), number_expression(std::to_string(offset)));
  return subtract_expression(std::move(expression), number_expression(std::to_string(-offset)));
}

Expression board_cell_expression(Expression x, Expression y) {
  return add_expression(std::move(x), multiply_expression(number_expression("10"), std::move(y)));
}

Expression spatial_bit_index_expression_for_board(const V2Board* board, Expression cell) {
  if (board != nullptr && board->height == 1)
    return offset_expression(std::move(cell), -board->x_min);
  if (board != nullptr && board->width == 1)
    return offset_expression(std::move(cell), -board->y_min);
  return cell;
}

Expression spatial_hit_expression(Expression mask, Expression index) {
  return call_expression(std::string(k_spatial_hit_callee), {std::move(mask), std::move(index)});
}

std::optional<RemainderByConstantMatch> match_remainder_by_constant(const Expression& expression) {
  if (expression.kind != "binary" || expression.op != "-" || expression.left == nullptr ||
      expression.right == nullptr ||
      !expression_pure_for_remainder_substitution(*expression.left)) {
    return std::nullopt;
  }

  const Expression& product = *expression.right;
  if (product.kind != "binary" || product.op != "*" || product.left == nullptr ||
      product.right == nullptr) {
    return std::nullopt;
  }

  const std::optional<RemainderByConstantMatch> left_int_divide =
      match_int_divide_by_constant(*product.left);
  if (left_int_divide.has_value() && expression_equals(left_int_divide->value, *expression.left) &&
      expression_equals(left_int_divide->divisor, *product.right)) {
    return left_int_divide;
  }

  const std::optional<RemainderByConstantMatch> right_int_divide =
      match_int_divide_by_constant(*product.right);
  if (right_int_divide.has_value() &&
      expression_equals(right_int_divide->value, *expression.left) &&
      expression_equals(right_int_divide->divisor, *product.left)) {
    return right_int_divide;
  }

  return std::nullopt;
}

} // namespace mkpro::core::emit
