#include "mkpro/core/packed_bcd_popcount.hpp"

#include "mkpro/core/parser.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <exception>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core {

namespace {

std::optional<Expression> parse_expression_safe(const std::string& text, int line) {
  try {
    return parse_expression(normalize_v2_expression_text(text), line);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool expression_is_identifier(const Expression& expression, const std::string& name) {
  return expression.kind == "identifier" && expression.name == name;
}

std::optional<double> number_value(const Expression& expression) {
  if (expression.kind != "number")
    return std::nullopt;
  std::string text = expression.raw.empty() ? expression.text : expression.raw;
  std::replace(text.begin(), text.end(), ',', '.');
  try {
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value))
      return std::nullopt;
    return value;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool expression_is_number(const Expression& expression, double expected) {
  const std::optional<double> value = number_value(expression);
  return value.has_value() && std::fabs(*value - expected) < 1e-12;
}

std::optional<std::string> identifier_target(const V2Statement& statement) {
  if (!statement.target.has_value())
    return std::nullopt;
  const std::optional<Expression> target = parse_expression_safe(*statement.target, statement.line);
  if (!target.has_value() || target->kind != "identifier")
    return std::nullopt;
  return target->name;
}

std::optional<int> integer_assignment(const V2Statement& statement) {
  if (statement.kind != "v2_assign" || !statement.expr.has_value())
    return std::nullopt;
  const std::optional<Expression> expression =
      parse_expression_safe(*statement.expr, statement.line);
  if (!expression.has_value())
    return std::nullopt;
  const std::optional<double> value = number_value(*expression);
  if (!value.has_value() || std::floor(*value) != *value)
    return std::nullopt;
  return static_cast<int>(*value);
}

bool call_has_identifier_arg(const Expression& expression, const std::string& callee,
                             const std::string& name) {
  return expression.kind == "call" && expression.callee == callee &&
         expression.args.size() == 1U && expression_is_identifier(expression.args.front(), name);
}

bool expression_is_identifier_divided_by(const Expression& expression, const std::string& name,
                                         double divisor) {
  return expression.kind == "binary" && expression.op == "/" && expression.left != nullptr &&
         expression.right != nullptr && expression_is_identifier(*expression.left, name) &&
         expression_is_number(*expression.right, divisor);
}

bool call_is_int_of_identifier_divided_by(const Expression& expression, const std::string& name,
                                          double divisor) {
  return expression.kind == "call" && expression.callee == "int" &&
         expression.args.size() == 1U &&
         expression_is_identifier_divided_by(expression.args.front(), name, divisor);
}

std::optional<Expression> resolved_const_expression(const V2Program& program,
                                                    const Expression& expression) {
  if (expression.kind != "identifier")
    return expression;
  const auto found = std::find_if(program.consts.begin(), program.consts.end(),
                                  [&](const V2Const& item) { return item.name == expression.name; });
  if (found == program.consts.end())
    return expression;
  return parse_expression_safe(found->expr, found->line);
}

double repeated_seven_mask_value(int digits, bool integer_seven) {
  double value = integer_seven ? 7.0 : 0.0;
  double place = 0.1;
  for (int index = 0; index < digits; ++index) {
    value += 7.0 * place;
    place /= 10.0;
  }
  return value;
}

bool expression_is_seven_mask(const V2Program& program, const Expression& expression, int digits) {
  const std::optional<Expression> resolved = resolved_const_expression(program, expression);
  if (!resolved.has_value())
    return false;
  return expression_is_number(*resolved, repeated_seven_mask_value(digits, true)) ||
         expression_is_number(*resolved, repeated_seven_mask_value(digits, false));
}

bool assignment_masks_fractional_bcd_digits(const V2Program& program,
                                            const V2Statement& statement, int digits) {
  if (statement.kind != "v2_assign" || !statement.expr.has_value())
    return false;
  const std::optional<Expression> expression =
      parse_expression_safe(*statement.expr, statement.line);
  if (!expression.has_value() || expression->kind != "call" || expression->callee != "frac" ||
      expression->args.size() != 1U) {
    return false;
  }
  const Expression& masked = expression->args.front();
  if (masked.kind != "call" || masked.callee != "bit_and" || masked.args.size() != 2U)
    return false;
  return expression_is_seven_mask(program, masked.args.at(0), digits) ||
         expression_is_seven_mask(program, masked.args.at(1), digits);
}

bool matches_source_scale(const V2Statement& statement, const std::string& source) {
  if (identifier_target(statement) != source || !statement.expr.has_value())
    return false;
  const std::optional<Expression> expression =
      parse_expression_safe(*statement.expr, statement.line);
  if (!expression.has_value())
    return false;
  if (statement.kind == "v2_update" && statement.op == "*=")
    return expression_is_number(*expression, 10.0);
  if (statement.kind != "v2_assign" || expression->kind != "binary" ||
      expression->op != "*" || expression->left == nullptr || expression->right == nullptr) {
    return false;
  }
  return (expression_is_identifier(*expression->left, source) &&
          expression_is_number(*expression->right, 10.0)) ||
         (expression_is_number(*expression->left, 10.0) &&
          expression_is_identifier(*expression->right, source));
}

bool matches_digit_extract(const V2Statement& statement, const std::string& digit,
                           const std::string& source) {
  if (statement.kind != "v2_assign" || identifier_target(statement) != digit ||
      !statement.expr.has_value()) {
    return false;
  }
  const std::optional<Expression> expression =
      parse_expression_safe(*statement.expr, statement.line);
  return expression.has_value() && call_has_identifier_arg(*expression, "int", source);
}

bool matches_half_extract(const V2Statement& statement, const std::string& half,
                          const std::string& digit) {
  if (statement.kind != "v2_assign" || identifier_target(statement) != half ||
      !statement.expr.has_value()) {
    return false;
  }
  const std::optional<Expression> expression =
      parse_expression_safe(*statement.expr, statement.line);
  return expression.has_value() && call_is_int_of_identifier_divided_by(*expression, digit, 2.0);
}

bool matches_digit_popcount_update(const V2Statement& statement, const std::string& accumulator,
                                   const std::string& digit, const std::string& half) {
  if (statement.kind != "v2_update" || identifier_target(statement) != accumulator ||
      statement.op != "+=" || !statement.expr.has_value()) {
    return false;
  }
  const std::optional<Expression> expression =
      parse_expression_safe(*statement.expr, statement.line);
  if (!expression.has_value() || expression->kind != "binary" || expression->op != "-" ||
      expression->left == nullptr || expression->right == nullptr ||
      expression->left->kind != "binary" || expression->left->op != "-" ||
      expression->left->left == nullptr || expression->left->right == nullptr) {
    return false;
  }
  return expression_is_identifier(*expression->left->left, digit) &&
         expression_is_identifier(*expression->left->right, half) &&
         call_is_int_of_identifier_divided_by(*expression->right, half, 2.0);
}

bool matches_source_fraction(const V2Statement& statement, const std::string& source) {
  if (statement.kind != "v2_assign" || identifier_target(statement) != source ||
      !statement.expr.has_value()) {
    return false;
  }
  const std::optional<Expression> expression =
      parse_expression_safe(*statement.expr, statement.line);
  return expression.has_value() && call_has_identifier_arg(*expression, "frac", source);
}

bool matches_unit_decrement(const V2Statement& statement, const std::string& counter) {
  if (statement.kind != "v2_update" || identifier_target(statement) != counter ||
      statement.op != "-=" || !statement.expr.has_value()) {
    return false;
  }
  const std::optional<Expression> expression =
      parse_expression_safe(*statement.expr, statement.line);
  return expression.has_value() && expression_is_number(*expression, 1.0);
}

bool expression_reads_name(const Expression& expression, const std::string& name) {
  if (expression.kind == "identifier")
    return expression.name == name;
  if (expression.kind == "indexed" && expression.base == name)
    return true;
  if (expression.index != nullptr && expression_reads_name(*expression.index, name))
    return true;
  if (expression.expr != nullptr && expression_reads_name(*expression.expr, name))
    return true;
  if (expression.left != nullptr && expression_reads_name(*expression.left, name))
    return true;
  if (expression.right != nullptr && expression_reads_name(*expression.right, name))
    return true;
  return std::any_of(expression.args.begin(), expression.args.end(),
                     [&](const Expression& arg) { return expression_reads_name(arg, name); });
}

bool expression_text_reads_name(const std::string& text, int line, const std::string& name) {
  const std::optional<Expression> expression = parse_expression_safe(text, line);
  return !expression.has_value() || expression_reads_name(*expression, name);
}

bool statement_reads_name(const V2Statement& statement, const std::string& name,
                          const std::set<const V2Statement*>& excluded) {
  if (excluded.contains(&statement))
    return false;
  if (statement.kind == "v2_raw")
    return true;
  if (statement.expr.has_value() &&
      expression_text_reads_name(*statement.expr, statement.line, name)) {
    return true;
  }
  if (statement.target.has_value()) {
    const bool target_is_write = statement.kind == "v2_assign" || statement.kind == "v2_read";
    if (!target_is_write && expression_text_reads_name(*statement.target, statement.line, name))
      return true;
    if (target_is_write) {
      const std::optional<Expression> target =
          parse_expression_safe(*statement.target, statement.line);
      if (!target.has_value() || (target->kind == "indexed" && expression_reads_name(*target, name)))
        return true;
    }
  }
  for (const std::string& arg : statement.args) {
    if (expression_text_reads_name(arg, statement.line, name))
      return true;
  }
  if (statement.predicate.has_value()) {
    const V2Predicate& predicate = *statement.predicate;
    if ((!predicate.left.empty() &&
         expression_text_reads_name(predicate.left, statement.line, name)) ||
        (!predicate.right.empty() &&
         expression_text_reads_name(predicate.right, statement.line, name)) ||
        (!predicate.item.empty() &&
         expression_text_reads_name(predicate.item, statement.line, name)) ||
        predicate.collection == name) {
      return true;
    }
  }
  if (statement.items.has_value()) {
    for (const DisplayItem& item : *statement.items) {
      if (item.name == name ||
          (item.expr.has_value() && expression_reads_name(*item.expr, name))) {
        return true;
      }
    }
  }
  for (const V2RawInput& input : statement.inputs) {
    if (expression_text_reads_name(input.expr, input.line, name))
      return true;
  }
  auto block_reads = [&](const std::vector<V2Statement>& body) {
    return std::any_of(body.begin(), body.end(), [&](const V2Statement& child) {
      return statement_reads_name(child, name, excluded);
    });
  };
  if (block_reads(statement.body) || block_reads(statement.then_body) ||
      block_reads(statement.else_body)) {
    return true;
  }
  for (const V2MatchCase& match_case : statement.cases) {
    if (match_case.action != nullptr && statement_reads_name(*match_case.action, name, excluded))
      return true;
  }
  return statement.otherwise != nullptr &&
         statement_reads_name(*statement.otherwise, name, excluded);
}

bool program_reads_name_outside(const V2Program& program, const std::string& name,
                                const std::set<const V2Statement*>& excluded) {
  const auto block_reads = [&](const std::vector<V2Statement>& body) {
    return std::any_of(body.begin(), body.end(), [&](const V2Statement& statement) {
      return statement_reads_name(statement, name, excluded);
    });
  };
  if (block_reads(program.body))
    return true;
  return std::any_of(program.rules.begin(), program.rules.end(),
                     [&](const V2Rule& rule) { return block_reads(rule.body); });
}

bool horizontal_fold_is_decimal_exact(int digits) {
  const int pairs = (digits + 1) / 2;
  if (digits < 1 || digits > 7 || pairs > 4 || 3 * digits > 99)
    return false;

  long double fractional_spill = 0.0L;
  for (int distance = 1; distance < pairs; ++distance)
    fractional_spill += 6.0L * static_cast<long double>(pairs - distance) /
                        std::pow(100.0L, static_cast<long double>(distance));

  long double packed_max = 0.0L;
  long double coefficient = 0.0L;
  for (int index = 0; index < pairs; ++index) {
    const int power = 2 * index + 1;
    packed_max += 6.0L / std::pow(10.0L, static_cast<long double>(power));
    coefficient += std::pow(10.0L, static_cast<long double>(power));
  }

  // At most seven integer digits leave one fractional guard digit in the
  // MK-61's eight-digit mantissa. The proved spill is below 0.5, so rounding
  // that guard digit cannot carry into the two result digits.
  return fractional_spill < 0.5L && packed_max * coefficient < 10000000.0L;
}

struct PackedBcdPopcountMatch {
  std::string source;
  std::string accumulator;
  std::string counter;
  std::string digit;
  std::string half;
  int digits = 0;
  int line = 0;
};

std::optional<PackedBcdPopcountMatch>
match_packed_bcd_popcount(const V2Program& program, const V2Statement& source_init,
                          const V2Statement& accumulator_init, const V2Statement& counter_init,
                          const V2Statement& loop) {
  const std::optional<std::string> source = identifier_target(source_init);
  const std::optional<std::string> accumulator = identifier_target(accumulator_init);
  const std::optional<std::string> counter = identifier_target(counter_init);
  const std::optional<int> digits = integer_assignment(counter_init);
  if (!source.has_value() || !accumulator.has_value() || !counter.has_value() ||
      !digits.has_value() || !horizontal_fold_is_decimal_exact(*digits) ||
      integer_assignment(accumulator_init) != 0 ||
      !assignment_masks_fractional_bcd_digits(program, source_init, *digits) ||
      loop.kind != "v2_while" || loop.negated || !loop.predicate.has_value() ||
      loop.body.size() != 6U) {
    return std::nullopt;
  }

  const V2Predicate& predicate = *loop.predicate;
  const std::optional<Expression> predicate_left =
      parse_expression_safe(predicate.left, loop.line);
  const std::optional<Expression> predicate_right =
      parse_expression_safe(predicate.right, loop.line);
  if (predicate.kind != "v2_compare" || predicate.op != ">=" ||
      !predicate_left.has_value() || !predicate_right.has_value() ||
      !expression_is_identifier(*predicate_left, *counter) ||
      !expression_is_number(*predicate_right, 1.0)) {
    return std::nullopt;
  }

  const std::optional<std::string> digit = identifier_target(loop.body.at(1));
  const std::optional<std::string> half = identifier_target(loop.body.at(2));
  if (!digit.has_value() || !half.has_value())
    return std::nullopt;
  const std::set<std::string> names{*source, *accumulator, *counter, *digit, *half};
  if (names.size() != 5U || !matches_source_scale(loop.body.at(0), *source) ||
      !matches_digit_extract(loop.body.at(1), *digit, *source) ||
      !matches_half_extract(loop.body.at(2), *half, *digit) ||
      !matches_digit_popcount_update(loop.body.at(3), *accumulator, *digit, *half) ||
      !matches_source_fraction(loop.body.at(4), *source) ||
      !matches_unit_decrement(loop.body.at(5), *counter)) {
    return std::nullopt;
  }

  const std::set<const V2Statement*> excluded{
      &source_init,
      &accumulator_init,
      &counter_init,
      &loop,
  };
  for (const std::string* scratch : {&*source, &*counter, &*digit, &*half}) {
    if (program_reads_name_outside(program, *scratch, excluded))
      return std::nullopt;
  }

  return PackedBcdPopcountMatch{
      .source = *source,
      .accumulator = *accumulator,
      .counter = *counter,
      .digit = *digit,
      .half = *half,
      .digits = *digits,
      .line = loop.line,
  };
}

std::string repeated_fraction_digit(char digit, int count) {
  return "0." + std::string(static_cast<std::size_t>(count), digit);
}

std::string alternating_fraction_mask(int digits, bool odd) {
  std::string result = "0.";
  for (int index = 0; index < digits; ++index)
    result.push_back((index % 2 == 0) == odd ? '3' : '0');
  return result;
}

std::string packed_digit_popcount_expression(const std::string& source, int digits) {
  return "bit_and(" + source + ", " + repeated_fraction_digit('1', digits) + ") + " +
         "bit_and(" + source + ", " + repeated_fraction_digit('2', digits) + ") / 2 + " +
         "bit_and(" + source + ", " + repeated_fraction_digit('4', digits) + ") / 4";
}

std::string packed_horizontal_fold_expression(const std::string& packed, int digits) {
  std::string coefficient;
  for (int index = 0; index < (digits + 1) / 2; ++index)
    coefficient += "10";
  return "int(frac((bit_and(" + packed + ", " +
         alternating_fraction_mask(digits, true) + ") + bit_and(" + packed + ", " +
         alternating_fraction_mask(digits, false) + ") * 10) * " + coefficient +
         " / 100) * 100)";
}

int rewrite_block(V2Program& program, std::vector<V2Statement>& statements,
                  std::vector<OptimizationReport>& optimizations);

int rewrite_nested(V2Program& program, V2Statement& statement,
                   std::vector<OptimizationReport>& optimizations) {
  int applied = rewrite_block(program, statement.body, optimizations) +
                rewrite_block(program, statement.then_body, optimizations) +
                rewrite_block(program, statement.else_body, optimizations);
  for (V2MatchCase& match_case : statement.cases) {
    if (match_case.action != nullptr)
      applied += rewrite_nested(program, *match_case.action, optimizations);
  }
  if (statement.otherwise != nullptr)
    applied += rewrite_nested(program, *statement.otherwise, optimizations);
  return applied;
}

int rewrite_block(V2Program& program, std::vector<V2Statement>& statements,
                  std::vector<OptimizationReport>& optimizations) {
  int applied = 0;
  for (std::size_t index = 0; index < statements.size();) {
    if (index + 3U < statements.size()) {
      const std::optional<PackedBcdPopcountMatch> match = match_packed_bcd_popcount(
          program, statements.at(index), statements.at(index + 1U), statements.at(index + 2U),
          statements.at(index + 3U));
      if (match.has_value()) {
        statements.at(index + 1U) = V2Statement{
            .kind = "v2_assign",
            .target = match->digit,
            .expr = packed_digit_popcount_expression(match->source, match->digits),
            .line = match->line,
        };
        statements.at(index + 2U) = V2Statement{
            .kind = "v2_assign",
            .target = match->accumulator,
            .expr = packed_horizontal_fold_expression(match->digit, match->digits),
            .line = match->line,
        };
        statements.erase(statements.begin() + static_cast<std::ptrdiff_t>(index + 3U));
        optimizations.push_back(OptimizationReport{
            .name = "packed-bcd-popcount-fold",
            .detail = "Folded " + std::to_string(match->digits) +
                      " explicitly masked BCD digits through three bit planes and a proved "
                      "base-100 horizontal reduction at line " + std::to_string(match->line) +
                      "; all discarded scratch results have no reads outside the matched fold.",
        });
        ++applied;
        index += 3U;
        continue;
      }
    }
    applied += rewrite_nested(program, statements.at(index), optimizations);
    ++index;
  }
  return applied;
}

} // namespace

int fold_proved_packed_bcd_popcount_loops(
    V2Program& program, std::vector<OptimizationReport>& optimizations) {
  int applied = rewrite_block(program, program.body, optimizations);
  for (V2Rule& rule : program.rules)
    applied += rewrite_block(program, rule.body, optimizations);
  return applied;
}

} // namespace mkpro::core
