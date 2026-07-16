#include "mkpro/core/packed_bcd_popcount.hpp"

#include "mkpro/core/parser.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <exception>
#include <optional>
#include <set>
#include <string>
#include <tuple>
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
                          const V2Statement& accumulator_init,
                          const V2Statement& counter_init, const V2Statement& loop);

std::string expression_key(const Expression& expression) {
  std::string result = expression.kind + "{" + expression.raw + "|" + expression.text + "|" +
                       expression.name + "|" + expression.base + "|" + expression.op + "|" +
                       expression.callee;
  if (expression.field.has_value())
    result += "|field=" + *expression.field;
  const auto append = [&](const char* label, const ExpressionPtr& child) {
    if (child != nullptr)
      result += std::string("|") + label + "=" + expression_key(*child);
  };
  append("index", expression.index);
  append("expr", expression.expr);
  append("left", expression.left);
  append("right", expression.right);
  for (const Expression& arg : expression.args)
    result += "|arg=" + expression_key(arg);
  return result + "}";
}

bool expression_same(const Expression& left, const Expression& right) {
  return expression_key(left) == expression_key(right);
}

std::optional<std::string> simple_expression_text(const Expression& expression) {
  if (expression.kind == "identifier")
    return expression.name;
  if (expression.kind == "number")
    return expression.raw.empty() ? expression.text : expression.raw;
  return std::nullopt;
}

bool call_named(const Expression& expression, const std::string& name, std::size_t arity) {
  return expression.kind == "call" && expression.callee == name &&
         expression.args.size() == arity;
}

bool binary_named(const Expression& expression, const std::string& op) {
  return expression.kind == "binary" && expression.op == op && expression.left != nullptr &&
         expression.right != nullptr;
}

const V2StateField* state_field(const V2Program& program, const std::string& name) {
  const auto found = std::find_if(program.state.begin(), program.state.end(),
                                  [&](const V2StateField& field) { return field.name == name; });
  return found == program.state.end() ? nullptr : &*found;
}

bool state_range_is(const V2Program& program, const std::string& name, int min, int max) {
  const V2StateField* field = state_field(program, name);
  return field != nullptr && field->min == min && field->max == max;
}

bool state_is_packed(const V2Program& program, const std::string& name) {
  const V2StateField* field = state_field(program, name);
  return field != nullptr && field->type == "packed";
}

bool state_bank_is(const V2Program& program, const std::string& name, int min, int max) {
  const V2StateField* field = state_field(program, name);
  return field != nullptr && field->type == "packed" && field->bank.has_value() &&
         field->bank->min == min && field->bank->max == max;
}

bool compare_loop_from_one(const V2Statement& loop, const std::string& counter) {
  if (loop.kind != "v2_while" || loop.negated || !loop.predicate.has_value())
    return false;
  const V2Predicate& predicate = *loop.predicate;
  const std::optional<Expression> left = parse_expression_safe(predicate.left, loop.line);
  const std::optional<Expression> right = parse_expression_safe(predicate.right, loop.line);
  return predicate.kind == "v2_compare" && predicate.op == ">=" && left.has_value() &&
         right.has_value() && expression_is_identifier(*left, counter) &&
         expression_is_number(*right, 1.0);
}

struct PackedHammingKernel {
  std::string rule;
  std::string parameter;
  std::string sample;
  std::string mask;
  std::string accumulator;
  std::string counter;
  std::string digit;
  std::string half;
  int digits = 0;
  int divisor = 0;
  int bias = 0;
  int line = 0;
};

std::optional<PackedHammingKernel> match_hamming_kernel(const V2Program& program,
                                                       const V2Rule& rule) {
  if (rule.params.size() != 1U || rule.body.size() != 5U)
    return std::nullopt;
  const std::optional<PackedBcdPopcountMatch> fold = match_packed_bcd_popcount(
      program, rule.body.at(0), rule.body.at(1), rule.body.at(2), rule.body.at(3));
  if (!fold.has_value() || fold->source != rule.params.front())
    return std::nullopt;

  const V2Statement& ret = rule.body.back();
  if (ret.kind != "v2_return" || !ret.expr.has_value())
    return std::nullopt;
  const std::optional<Expression> return_expression = parse_expression_safe(*ret.expr, ret.line);
  if (!return_expression.has_value() || !call_named(*return_expression, "int", 1U) ||
      !binary_named(return_expression->args.front(), "/")) {
    return std::nullopt;
  }
  const Expression& quotient = return_expression->args.front();
  const Expression* numerator = quotient.left.get();
  double bias_value = 0.0;
  if (!expression_is_identifier(*numerator, fold->accumulator)) {
    if (!binary_named(*numerator, "+"))
      return std::nullopt;
    const Expression* bias_expression = nullptr;
    if (expression_is_identifier(*numerator->left, fold->accumulator))
      bias_expression = numerator->right.get();
    else if (expression_is_identifier(*numerator->right, fold->accumulator))
      bias_expression = numerator->left.get();
    if (bias_expression == nullptr)
      return std::nullopt;
    const std::optional<double> matched_bias = number_value(*bias_expression);
    if (!matched_bias.has_value())
      return std::nullopt;
    bias_value = *matched_bias;
  }
  const std::optional<double> divisor_value = number_value(*quotient.right);
  const double bit_count = 3.0 * fold->digits;
  const double effective_divisor = divisor_value.value_or(0.0) - bias_value;
  // The final K[x] lowering is a single bit only when the quotient cannot
  // reach two.  Requiring the effective threshold (divisor - bias) to be a
  // strict majority of all reachable bits proves that bound as well as the
  // original packed-Hamming threshold interpretation.
  if (!divisor_value.has_value() || std::floor(*divisor_value) != *divisor_value ||
      std::floor(bias_value) != bias_value || bias_value < 0.0 || bias_value > bit_count ||
      effective_divisor <= bit_count / 2.0 || effective_divisor > bit_count) {
    return std::nullopt;
  }

  const std::optional<Expression> source_expression =
      parse_expression_safe(*rule.body.front().expr, rule.body.front().line);
  if (!source_expression.has_value() || !call_named(*source_expression, "frac", 1U))
    return std::nullopt;
  const Expression& masked = source_expression->args.front();
  if (!call_named(masked, "bit_and", 2U))
    return std::nullopt;
  const Expression* xor_expression = nullptr;
  const Expression* mask_expression = nullptr;
  if (call_named(masked.args.at(0), "bit_xor", 2U)) {
    xor_expression = &masked.args.at(0);
    mask_expression = &masked.args.at(1);
  } else if (call_named(masked.args.at(1), "bit_xor", 2U)) {
    xor_expression = &masked.args.at(1);
    mask_expression = &masked.args.at(0);
  }
  if (xor_expression == nullptr || !expression_is_seven_mask(program, *mask_expression,
                                                              fold->digits)) {
    return std::nullopt;
  }
  const Expression* sample = nullptr;
  if (expression_is_identifier(xor_expression->args.at(0), rule.params.front()))
    sample = &xor_expression->args.at(1);
  else if (expression_is_identifier(xor_expression->args.at(1), rule.params.front()))
    sample = &xor_expression->args.at(0);
  const std::optional<std::string> sample_text =
      sample == nullptr ? std::nullopt : simple_expression_text(*sample);
  const std::optional<std::string> mask_text = simple_expression_text(*mask_expression);
  if (!sample_text.has_value() || !mask_text.has_value() ||
      !state_is_packed(program, *sample_text)) {
    return std::nullopt;
  }

  return PackedHammingKernel{
      .rule = rule.name,
      .parameter = rule.params.front(),
      .sample = *sample_text,
      .mask = *mask_text,
      .accumulator = fold->accumulator,
      .counter = fold->counter,
      .digit = fold->digit,
      .half = fold->half,
      .digits = fold->digits,
      .divisor = static_cast<int>(*divisor_value),
      .bias = static_cast<int>(bias_value),
      .line = fold->line,
  };
}

bool indexed_bank_expression(const Expression& expression, std::string& base,
                             std::string& index) {
  if (expression.kind != "indexed" || expression.index == nullptr ||
      expression.index->kind != "identifier") {
    return false;
  }
  base = expression.base;
  index = expression.index->name;
  return true;
}

std::string indexed_text(const std::string& base, const std::string& index) {
  return base + "[" + index + "]";
}

bool match_horner_term(const Expression& expression, const std::string& target,
                       const PackedHammingKernel& kernel, std::string& bank,
                       std::string& index) {
  if (!binary_named(expression, "+"))
    return false;
  const Expression* product = expression.left.get();
  const Expression* call = expression.right.get();
  if (!binary_named(*product, "*") || !call_named(*call, kernel.rule, 1U)) {
    product = expression.right.get();
    call = expression.left.get();
  }
  if (!binary_named(*product, "*") || !call_named(*call, kernel.rule, 1U))
    return false;
  const bool product_ok =
      (expression_is_identifier(*product->left, target) &&
       expression_is_number(*product->right, 2.0)) ||
      (expression_is_identifier(*product->right, target) &&
       expression_is_number(*product->left, 2.0));
  return product_ok && indexed_bank_expression(call->args.front(), bank, index);
}

bool rewrite_horner_block(V2Program& program, std::vector<V2Statement>& statements,
                          const PackedHammingKernel& kernel,
                          std::vector<OptimizationReport>& optimizations) {
  bool rewritten = false;
  for (std::size_t index = 0; index < statements.size(); ++index) {
    if (index + 2U < statements.size()) {
      const std::optional<std::string> target = identifier_target(statements.at(index));
      const std::optional<std::string> counter = identifier_target(statements.at(index + 1U));
      const std::optional<int> iterations = integer_assignment(statements.at(index + 1U));
      const V2Statement& loop = statements.at(index + 2U);
      if (target.has_value() && counter.has_value() && iterations.has_value() &&
          integer_assignment(statements.at(index)) == 0 && *iterations >= 1 &&
          *iterations <= 3 && compare_loop_from_one(loop, *counter) && loop.body.size() == 2U &&
          matches_unit_decrement(loop.body.back(), *counter) &&
          loop.body.front().kind == "v2_assign" &&
          identifier_target(loop.body.front()) == target && loop.body.front().expr.has_value()) {
        const std::optional<Expression> recurrence =
            parse_expression_safe(*loop.body.front().expr, loop.body.front().line);
        std::string bank;
        std::string call_index;
        if (recurrence.has_value() &&
            match_horner_term(*recurrence, *target, kernel, bank, call_index) &&
            call_index == *counter && state_bank_is(program, bank, 1, *iterations)) {
          V2Statement packed{
              .kind = kPackedBcdHornerThresholdStatement,
              .target = *target,
              .args = {*target,
                       *counter,
                       indexed_text(bank, *counter),
                       kernel.sample,
                       std::to_string(kernel.digits),
                       std::to_string(kernel.divisor),
                       "1111111.1",
                       kernel.counter,
                       std::to_string(*iterations),
                       kernel.accumulator,
                       "0",
                       std::to_string(kernel.bias)},
              .line = loop.line,
          };
          statements.at(index) = std::move(packed);
          statements.erase(statements.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                           statements.begin() + static_cast<std::ptrdiff_t>(index + 3U));
          optimizations.push_back(OptimizationReport{
              .name = "packed-bcd-horner-threshold-loop",
              .detail = "Lowered a " + std::to_string(*iterations) +
                        "-term indexed Horner recurrence through a proved packed-BCD "
                        "popcount threshold at line " + std::to_string(loop.line) + ".",
          });
          rewritten = true;
          continue;
        }
      }
    }
    V2Statement& statement = statements.at(index);
    rewritten = rewrite_horner_block(program, statement.body, kernel, optimizations) || rewritten;
    rewritten = rewrite_horner_block(program, statement.then_body, kernel, optimizations) ||
                rewritten;
    rewritten = rewrite_horner_block(program, statement.else_body, kernel, optimizations) ||
                rewritten;
  }
  return rewritten;
}

struct BitPlaneMask {
  Expression value;
  Expression selector;
  Expression full;
};

std::optional<BitPlaneMask> match_bit_plane_mask(const Expression& expression) {
  if (!binary_named(expression, "*"))
    return std::nullopt;
  const Expression* quotient = expression.left.get();
  const Expression* full = expression.right.get();
  if (!binary_named(*quotient, "/")) {
    quotient = expression.right.get();
    full = expression.left.get();
  }
  if (!binary_named(*quotient, "/") || !call_named(*quotient->left, "bit_and", 2U))
    return std::nullopt;
  const Expression& bit_and = *quotient->left;
  const Expression* value = &bit_and.args.at(0);
  const Expression* selector = &bit_and.args.at(1);
  if (!expression_same(*selector, *quotient->right)) {
    value = &bit_and.args.at(1);
    selector = &bit_and.args.at(0);
  }
  if (!expression_same(*selector, *quotient->right))
    return std::nullopt;
  return BitPlaneMask{.value = *value, .selector = *selector, .full = *full};
}

bool unordered_binary_parts(const Expression& expression, const std::string& op,
                            const Expression& expected, const Expression*& other) {
  if (!binary_named(expression, op))
    return false;
  if (expression_same(*expression.left, expected)) {
    other = expression.right.get();
    return true;
  }
  if (expression_same(*expression.right, expected)) {
    other = expression.left.get();
    return true;
  }
  return false;
}

bool random_octet_mask(const Expression& expression) {
  if (!binary_named(expression, "+"))
    return false;
  return (expression_is_number(*expression.left, 8.0) &&
          call_named(*expression.right, "random", 0U)) ||
         (expression_is_number(*expression.right, 8.0) &&
          call_named(*expression.left, "random", 0U));
}

struct OneHotUpdateMatch {
  std::string bank;
  std::string index;
  std::string selector;
  std::string sample;
  std::string desired;
  std::string observed;
  std::string full;
  int iterations = 0;
};

std::optional<OneHotUpdateMatch> match_one_hot_update(const V2Program& program,
                                                       const V2Statement& selector_init,
                                                       const V2Statement& index_init,
                                                       const V2Statement& loop,
                                                       const PackedHammingKernel& kernel) {
  const std::optional<std::string> selector = identifier_target(selector_init);
  const std::optional<std::string> index = identifier_target(index_init);
  const std::optional<int> iterations = integer_assignment(index_init);
  if (!selector.has_value() || !index.has_value() || !iterations.has_value() ||
      *iterations < 1 || *iterations > 3 ||
      integer_assignment(selector_init) != (1 << (*iterations - 1)) ||
      *selector != kernel.accumulator || !compare_loop_from_one(loop, *index) ||
      loop.body.size() != 3U || !matches_unit_decrement(loop.body.back(), *index)) {
    return std::nullopt;
  }
  const V2Statement& selector_update = loop.body.at(1);
  if (selector_update.kind != "v2_update" || identifier_target(selector_update) != selector ||
      selector_update.op != "/=" || !selector_update.expr.has_value())
    return std::nullopt;
  const std::optional<Expression> selector_divisor =
      parse_expression_safe(*selector_update.expr, selector_update.line);
  if (!selector_divisor.has_value() || !expression_is_number(*selector_divisor, 2.0))
    return std::nullopt;

  const V2Statement& update = loop.body.front();
  if (update.kind != "v2_assign" || !update.target.has_value() || !update.expr.has_value())
    return std::nullopt;
  const std::optional<Expression> target = parse_expression_safe(*update.target, update.line);
  const std::optional<Expression> rhs = parse_expression_safe(*update.expr, update.line);
  if (!target.has_value() || !rhs.has_value())
    return std::nullopt;
  std::string bank;
  std::string target_index;
  if (!indexed_bank_expression(*target, bank, target_index) || target_index != *index ||
      !state_bank_is(program, bank, 1, *iterations))
    return std::nullopt;

  const Expression* stochastic = nullptr;
  if (!unordered_binary_parts(*rhs, "", *target, stochastic)) {
    if (!call_named(*rhs, "bit_xor", 2U))
      return std::nullopt;
    if (expression_same(rhs->args.at(0), *target))
      stochastic = &rhs->args.at(1);
    else if (expression_same(rhs->args.at(1), *target))
      stochastic = &rhs->args.at(0);
    else
      return std::nullopt;
  }
  const Expression* gated = nullptr;
  const Expression* random_mask = nullptr;
  if (!call_named(*stochastic, "bit_and", 2U))
    return std::nullopt;
  if (random_octet_mask(stochastic->args.at(0))) {
    random_mask = &stochastic->args.at(0);
    gated = &stochastic->args.at(1);
  } else if (random_octet_mask(stochastic->args.at(1))) {
    random_mask = &stochastic->args.at(1);
    gated = &stochastic->args.at(0);
  }
  if (random_mask == nullptr || !call_named(*gated, "bit_and", 2U))
    return std::nullopt;

  const Expression* adjusted = nullptr;
  const Expression* mismatch_expression = nullptr;
  const std::optional<BitPlaneMask> first_mask = match_bit_plane_mask(gated->args.at(0));
  const std::optional<BitPlaneMask> second_mask = match_bit_plane_mask(gated->args.at(1));
  if (first_mask.has_value()) {
    mismatch_expression = &gated->args.at(0);
    adjusted = &gated->args.at(1);
  } else if (second_mask.has_value()) {
    mismatch_expression = &gated->args.at(1);
    adjusted = &gated->args.at(0);
  }
  if (mismatch_expression == nullptr || adjusted == nullptr)
    return std::nullopt;
  const std::optional<BitPlaneMask> mismatch = match_bit_plane_mask(*mismatch_expression);
  if (!mismatch.has_value() || !call_named(mismatch->value, "bit_xor", 2U) ||
      !expression_is_identifier(mismatch->selector, *selector))
    return std::nullopt;

  const Expression* desired_expression = nullptr;
  const Expression* base = nullptr;
  if (!call_named(*adjusted, "bit_xor", 2U))
    return std::nullopt;
  const std::optional<BitPlaneMask> adjusted_first =
      match_bit_plane_mask(adjusted->args.at(0));
  const std::optional<BitPlaneMask> adjusted_second =
      match_bit_plane_mask(adjusted->args.at(1));
  if (adjusted_first.has_value()) {
    desired_expression = &adjusted->args.at(0);
    base = &adjusted->args.at(1);
  } else if (adjusted_second.has_value()) {
    desired_expression = &adjusted->args.at(1);
    base = &adjusted->args.at(0);
  }
  if (desired_expression == nullptr || !call_named(*base, "bit_xor", 2U))
    return std::nullopt;
  const std::optional<BitPlaneMask> desired = match_bit_plane_mask(*desired_expression);
  if (!desired.has_value() || !expression_is_identifier(desired->selector, *selector) ||
      !expression_same(desired->full, mismatch->full))
    return std::nullopt;

  const Expression bank_expression = *target;
  const Expression* sample = nullptr;
  if (expression_same(base->args.at(0), bank_expression))
    sample = &base->args.at(1);
  else if (expression_same(base->args.at(1), bank_expression))
    sample = &base->args.at(0);
  const std::optional<std::string> sample_text =
      sample == nullptr ? std::nullopt : simple_expression_text(*sample);
  const std::optional<std::string> desired_text = simple_expression_text(desired->value);
  const std::optional<std::string> full_text = simple_expression_text(desired->full);
  if (!sample_text.has_value() || !desired_text.has_value() || !full_text.has_value() ||
      *sample_text != kernel.sample || !expression_is_seven_mask(program, desired->full,
                                                                  kernel.digits)) {
    return std::nullopt;
  }

  const Expression& mismatch_xor = mismatch->value;
  const Expression* observed = nullptr;
  if (expression_same(mismatch_xor.args.at(0), desired->value))
    observed = &mismatch_xor.args.at(1);
  else if (expression_same(mismatch_xor.args.at(1), desired->value))
    observed = &mismatch_xor.args.at(0);
  const std::optional<std::string> observed_text =
      observed == nullptr ? std::nullopt : simple_expression_text(*observed);
  if (!observed_text.has_value())
    return std::nullopt;
  const int max_value = (1 << *iterations) - 1;
  if (!state_range_is(program, *desired_text, 0, max_value) ||
      !state_range_is(program, *observed_text, 0, max_value)) {
    return std::nullopt;
  }

  return OneHotUpdateMatch{
      .bank = bank,
      .index = *index,
      .selector = *selector,
      .sample = *sample_text,
      .desired = *desired_text,
      .observed = *observed_text,
      .full = *full_text,
      .iterations = *iterations,
  };
}

std::optional<std::string> match_binary_score(const V2Statement& statement,
                                              const OneHotUpdateMatch& update) {
  if (statement.kind != "v2_update" || statement.op != "+=" ||
      !statement.expr.has_value())
    return std::nullopt;
  const std::optional<std::string> target = identifier_target(statement);
  const std::optional<Expression> expression =
      parse_expression_safe(*statement.expr, statement.line);
  if (!target.has_value() || !expression.has_value() || !binary_named(*expression, "-") ||
      !expression_is_number(*expression->right, (1 << update.iterations) - 1) ||
      !binary_named(*expression->left, "*")) {
    return std::nullopt;
  }
  const Expression& product = *expression->left;
  const Expression* magnitude = nullptr;
  if (expression_is_number(*product.left, 1 << update.iterations))
    magnitude = product.right.get();
  else if (expression_is_number(*product.right, 1 << update.iterations))
    magnitude = product.left.get();
  if (magnitude == nullptr || !call_named(*magnitude, "abs", 1U) ||
      !call_named(magnitude->args.front(), "sign", 1U) ||
      !binary_named(magnitude->args.front().args.front(), "-")) {
    return std::nullopt;
  }
  const Expression& difference = magnitude->args.front().args.front();
  const bool same_order = expression_is_identifier(*difference.left, update.desired) &&
                          expression_is_identifier(*difference.right, update.observed);
  const bool reverse_order = expression_is_identifier(*difference.right, update.desired) &&
                             expression_is_identifier(*difference.left, update.observed);
  return same_order || reverse_order ? target : std::nullopt;
}

bool packed_expression_mentions_identifier(const Expression& expression,
                                           const std::string& identifier) {
  if (expression.kind == "identifier" && expression.name == identifier)
    return true;
  for (const ExpressionPtr& child :
       {expression.index, expression.expr, expression.left, expression.right}) {
    if (child != nullptr && packed_expression_mentions_identifier(*child, identifier))
      return true;
  }
  return std::any_of(expression.args.begin(), expression.args.end(),
                     [&](const Expression& argument) {
                       return packed_expression_mentions_identifier(argument, identifier);
                     });
}

bool packed_text_contains_identifier_token(const std::string& text,
                                           const std::string& identifier) {
  const auto identifier_character = [](char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '_';
  };
  for (std::size_t offset = text.find(identifier); offset != std::string::npos;
       offset = text.find(identifier, offset + 1U)) {
    const bool left_boundary = offset == 0U || !identifier_character(text.at(offset - 1U));
    const std::size_t end = offset + identifier.size();
    const bool right_boundary = end == text.size() || !identifier_character(text.at(end));
    if (left_boundary && right_boundary)
      return true;
  }
  return false;
}

bool packed_text_mentions_identifier(const std::string& text, int line,
                                     const std::string& identifier) {
  if (text.empty())
    return false;
  const std::optional<Expression> expression = parse_expression_safe(text, line);
  return expression.has_value()
             ? packed_expression_mentions_identifier(*expression, identifier)
             : packed_text_contains_identifier_token(text, identifier);
}

bool packed_statement_mentions_identifier(
    const V2Statement& statement, const std::string& identifier,
    const std::vector<const V2Statement*>& ignored) {
  if (std::find(ignored.begin(), ignored.end(), &statement) != ignored.end())
    return false;
  const auto text_mentions = [&](const std::string& text) {
    return packed_text_mentions_identifier(text, statement.line, identifier);
  };
  if ((statement.target.has_value() && text_mentions(*statement.target)) ||
      (statement.expr.has_value() && text_mentions(*statement.expr)) ||
      std::any_of(statement.args.begin(), statement.args.end(), text_mentions)) {
    return true;
  }
  if (statement.predicate.has_value()) {
    const V2Predicate& predicate = *statement.predicate;
    if (text_mentions(predicate.left) || text_mentions(predicate.right) ||
        text_mentions(predicate.collection) || text_mentions(predicate.item)) {
      return true;
    }
  }
  if (statement.items.has_value()) {
    for (const DisplayItem& item : *statement.items) {
      if (item.expr.has_value() &&
          packed_expression_mentions_identifier(*item.expr, identifier)) {
        return true;
      }
    }
  }
  for (const V2RawInput& input : statement.inputs) {
    if (packed_text_mentions_identifier(input.expr, input.line, identifier))
      return true;
  }
  for (const V2RawOutput& output : statement.outputs) {
    if (packed_text_mentions_identifier(output.target, output.line, identifier))
      return true;
  }
  if (std::find(statement.clobbers.begin(), statement.clobbers.end(), identifier) !=
          statement.clobbers.end() ||
      std::find(statement.preserves.begin(), statement.preserves.end(), identifier) !=
          statement.preserves.end()) {
    return true;
  }
  for (const RawBlockLine& raw_line : statement.lines) {
    if (packed_text_contains_identifier_token(raw_line.text, identifier))
      return true;
  }
  const auto block_mentions = [&](const std::vector<V2Statement>& block) {
    return std::any_of(block.begin(), block.end(), [&](const V2Statement& child) {
      return packed_statement_mentions_identifier(child, identifier, ignored);
    });
  };
  if (block_mentions(statement.body) || block_mentions(statement.then_body) ||
      block_mentions(statement.else_body)) {
    return true;
  }
  for (const V2MatchCase& match_case : statement.cases) {
    if (std::any_of(match_case.values.begin(), match_case.values.end(), text_mentions) ||
        (match_case.action != nullptr &&
         packed_statement_mentions_identifier(*match_case.action, identifier, ignored))) {
      return true;
    }
  }
  return statement.otherwise != nullptr &&
         packed_statement_mentions_identifier(*statement.otherwise, identifier, ignored);
}

bool packed_program_mentions_identifier_outside(
    const V2Program& program, const std::string& identifier,
    const std::vector<const V2Statement*>& ignored) {
  for (const V2Const& constant : program.consts) {
    if (packed_text_mentions_identifier(constant.expr, constant.line, identifier))
      return true;
  }
  for (const V2StateField& field : program.state) {
    if ((field.initial.has_value() &&
         packed_text_mentions_identifier(*field.initial, field.line, identifier)) ||
        (field.initial_stack.has_value() &&
         packed_text_contains_identifier_token(*field.initial_stack, identifier))) {
      return true;
    }
  }
  const auto block_mentions = [&](const std::vector<V2Statement>& block) {
    return std::any_of(block.begin(), block.end(), [&](const V2Statement& statement) {
      return packed_statement_mentions_identifier(statement, identifier, ignored);
    });
  };
  if (block_mentions(program.body))
    return true;
  return std::any_of(program.rules.begin(), program.rules.end(), [&](const V2Rule& rule) {
    return block_mentions(rule.body);
  });
}

std::optional<std::size_t> match_dead_entered_value_for_packed_register(
    const V2Program& program, const std::vector<V2Statement>& statements,
    std::size_t update_index, bool history, const PackedHammingKernel& kernel,
    const OneHotUpdateMatch& update) {
  if (update_index < 3U || kernel.digit == update.desired ||
      !state_range_is(program, kernel.digit, 0, (1 << update.iterations) - 1)) {
    return std::nullopt;
  }
  const std::size_t score_index = update_index - 1U;
  const std::size_t entered_index = score_index - 2U;
  const std::size_t display_index = score_index - 1U;
  const V2Statement& entered = statements.at(entered_index);
  const std::optional<std::string> target = identifier_target(entered);
  const std::optional<Expression> input =
      entered.expr.has_value() ? parse_expression_safe(*entered.expr, entered.line) : std::nullopt;
  if (target != update.desired || !input.has_value() ||
      (!call_named(*input, "entered", 0U) && !call_named(*input, "entered", 2U)) ||
      statements.at(display_index).kind != "v2_show" ||
      packed_statement_mentions_identifier(statements.at(display_index), kernel.digit, {})) {
    return std::nullopt;
  }

  std::vector<const V2Statement*> consumed = {
      &entered,
      &statements.at(score_index),
      &statements.at(update_index),
      &statements.at(update_index + 1U),
      &statements.at(update_index + 2U),
  };
  if (history)
    consumed.push_back(&statements.at(update_index + 3U));
  if (packed_program_mentions_identifier_outside(program, update.desired, consumed))
    return std::nullopt;
  return entered_index;
}

enum class HistoryPrependKind { MaskedFraction, DirectFraction };

std::optional<HistoryPrependKind>
match_history_prepend(const V2Program&, const V2Statement& statement,
                      const OneHotUpdateMatch& update) {
  if (statement.kind != "v2_assign" || !statement.target.has_value() ||
      !statement.expr.has_value())
    return std::nullopt;
  const std::optional<Expression> target = parse_expression_safe(*statement.target, statement.line);
  const std::optional<Expression> expression = parse_expression_safe(*statement.expr, statement.line);
  if (!target.has_value() || !expression.has_value() || target->kind != "identifier" ||
      target->name != update.sample || !call_named(*expression, "bit_or", 2U))
    return std::nullopt;
  const Expression* shifted = nullptr;
  const Expression* encoded = nullptr;
  for (int order = 0; order < 2; ++order) {
    const Expression& first = expression->args.at(static_cast<std::size_t>(order));
    const Expression& second = expression->args.at(static_cast<std::size_t>(1 - order));
    if (binary_named(first, "+") && binary_named(second, "+")) {
      shifted = &first;
      encoded = &second;
      break;
    }
  }
  if (shifted == nullptr || encoded == nullptr)
    return std::nullopt;
  const Expression* fractional_history = nullptr;
  if (expression_is_number(*shifted->left, 2.0))
    fractional_history = shifted->right.get();
  else if (expression_is_number(*shifted->right, 2.0))
    fractional_history = shifted->left.get();
  if (fractional_history == nullptr)
    return std::nullopt;

  std::optional<HistoryPrependKind> kind;
  if (call_named(*fractional_history, "frac", 1U) &&
      expression_is_identifier(fractional_history->args.front(), update.sample)) {
    kind = HistoryPrependKind::DirectFraction;
  } else if (call_named(*fractional_history, "bit_and", 2U)) {
    const Expression* frac_mask = nullptr;
    if (expression_is_identifier(fractional_history->args.at(0), update.sample))
      frac_mask = &fractional_history->args.at(1);
    else if (expression_is_identifier(fractional_history->args.at(1), update.sample))
      frac_mask = &fractional_history->args.at(0);
    const std::optional<Expression> full = parse_expression_safe(update.full, statement.line);
    if (frac_mask != nullptr && full.has_value() && call_named(*frac_mask, "frac", 1U) &&
        expression_same(frac_mask->args.front(), *full)) {
      kind = HistoryPrependKind::MaskedFraction;
    }
  }
  if (!kind.has_value())
    return std::nullopt;

  const bool encoded_desired =
      (expression_is_number(*encoded->left, 10.0) &&
       expression_is_identifier(*encoded->right, update.desired)) ||
      (expression_is_number(*encoded->right, 10.0) &&
       expression_is_identifier(*encoded->left, update.desired));
  return encoded_desired ? kind : std::nullopt;
}

bool rewrite_one_hot_block(V2Program& program, std::vector<V2Statement>& statements,
                           const PackedHammingKernel& kernel,
                           std::vector<OptimizationReport>& optimizations) {
  bool rewritten = false;
  for (std::size_t index = 0; index < statements.size(); ++index) {
    if (index + 2U < statements.size()) {
      const std::optional<OneHotUpdateMatch> update = match_one_hot_update(
          program, statements.at(index), statements.at(index + 1U), statements.at(index + 2U),
          kernel);
      if (update.has_value()) {
        const std::optional<std::string> score =
            index > 0 ? match_binary_score(statements.at(index - 1U), *update) : std::nullopt;
        const std::optional<HistoryPrependKind> history_kind =
            index + 3U < statements.size()
                ? match_history_prepend(program, statements.at(index + 3U), *update)
                : std::nullopt;
        const bool history = history_kind.has_value();
        const std::optional<std::size_t> coalesced_entered =
            score.has_value()
                ? match_dead_entered_value_for_packed_register(program, statements, index,
                                                               history, kernel, *update)
                : std::nullopt;
        if (history) {
          const auto field = std::find_if(
              program.state.begin(), program.state.end(), [&](const V2StateField& item) {
                return item.name == kernel.accumulator;
              });
          if (field != program.state.end())
            field->initial = "10";
        }
        V2Statement prepare{
            .kind = kPackedBcdBinaryPrepareStatement,
            .target = kernel.half,
            .args = {kernel.digit, kernel.half,
                     coalesced_entered.has_value() ? kernel.digit : update->desired,
                     update->observed,
                     kernel.accumulator, update->sample, update->full, history ? "1" : "0"},
            .line = statements.at(index).line,
        };
        if (history_kind == HistoryPrependKind::DirectFraction)
          prepare.args.back() = "2";
        V2Statement packed_update{
            .kind = kPackedBcdOneHotUpdateStatement,
            .target = indexed_text(update->bank, update->index),
            .args = {indexed_text(update->bank, update->index),
                     update->index,
                     update->sample,
                     kernel.digit,
                     kernel.half,
                     update->full,
                     kernel.accumulator,
                     kernel.counter,
                     "1111111.1",
                     std::to_string(update->iterations)},
            .line = statements.at(index + 2U).line,
        };
        if (score.has_value()) {
          const std::size_t begin = index - 1U;
          if (coalesced_entered.has_value())
            statements.at(*coalesced_entered).target = kernel.digit;
          std::optional<std::size_t> rotated_horner;
          const bool top_level_loop_body =
              program.body.size() == 1U && program.body.front().kind == "v2_loop" &&
              &statements == &program.body.front().body;
          if (history && top_level_loop_body) {
            for (std::size_t candidate = 0; candidate < begin; ++candidate) {
              V2Statement& possible = statements.at(candidate);
              if (possible.kind == kPackedBcdHornerThresholdStatement &&
                  possible.args.size() == 12U && possible.args.at(0) == update->observed &&
                  possible.args.at(1) == update->index) {
                possible.args.at(10) = "1";
                rotated_horner = candidate;
                break;
              }
            }
          }
          V2Statement score_update{
              .kind = kPackedBcdNonzeroScoreStatement,
              .target = *score,
              .args = {*score, kernel.half, std::to_string(1 << update->iterations),
                       std::to_string((1 << update->iterations) - 1)},
              .line = statements.at(begin).line,
          };
          statements.at(begin) = std::move(prepare);
          statements.at(index) = std::move(score_update);
          statements.at(index + 1U) = std::move(packed_update);
          statements.erase(statements.begin() + static_cast<std::ptrdiff_t>(index + 2U));
          if (history) {
            if (rotated_horner.has_value()) {
              statements.at(index + 2U) = V2Statement{
                  .kind = kPackedBcdLoopResetStatement,
                  .target = update->observed,
                  .args = {update->observed, update->index},
                  .line = statements.at(index + 2U).line,
              };
            } else {
              statements.erase(statements.begin() +
                               static_cast<std::ptrdiff_t>(index + 2U));
            }
          }
          index = begin + 2U;
          optimizations.push_back(OptimizationReport{
              .name = "packed-bcd-nonzero-score-reuse",
              .detail = "Reused a proved packed XOR nonzero mask for a binary score update at "
                        "line " + std::to_string(statements.at(begin + 1U).line) + ".",
          });
          if (coalesced_entered.has_value()) {
            optimizations.push_back(OptimizationReport{
                .name = "packed-bcd-dead-input-register-coalescing",
                .detail = "Coalesced an entered value with its immediately replacing packed "
                          "work value after proving that no other reads remain.",
            });
          }
        } else {
          statements.at(index) = std::move(prepare);
          statements.at(index + 1U) = std::move(packed_update);
          statements.erase(statements.begin() + static_cast<std::ptrdiff_t>(index + 2U));
          if (history)
            statements.erase(statements.begin() + static_cast<std::ptrdiff_t>(index + 2U));
        }
        optimizations.push_back(OptimizationReport{
            .name = "packed-bcd-one-hot-bitplane-loop",
            .detail = "Lowered a " + std::to_string(update->iterations) +
                      "-step indexed one-hot update through a shared proved BCD bit-plane "
                      "split at line " + std::to_string(packed_update.line) + ".",
        });
        if (history) {
          optimizations.push_back(OptimizationReport{
              .name = "packed-bcd-history-prepend",
              .detail = "Lowered a proved packed octal history prepend without decimal "
                        "multiply/divide scaling.",
          });
        }
        rewritten = true;
        continue;
      }
    }
    V2Statement& statement = statements.at(index);
    rewritten = rewrite_one_hot_block(program, statement.body, kernel, optimizations) || rewritten;
    rewritten = rewrite_one_hot_block(program, statement.then_body, kernel, optimizations) ||
                rewritten;
    rewritten = rewrite_one_hot_block(program, statement.else_body, kernel, optimizations) ||
                rewritten;
  }
  return rewritten;
}

int rewrite_proved_packed_kernels(V2Program& program,
                                  std::vector<OptimizationReport>& optimizations) {
  int applied = 0;
  for (std::size_t index = 0; index < program.rules.size();) {
    const std::optional<PackedHammingKernel> kernel =
        match_hamming_kernel(program, program.rules.at(index));
    if (!kernel.has_value() ||
        !rewrite_horner_block(program, program.body, *kernel, optimizations)) {
      ++index;
      continue;
    }
    (void)rewrite_one_hot_block(program, program.body, *kernel, optimizations);
    program.rules.erase(program.rules.begin() + static_cast<std::ptrdiff_t>(index));
    optimizations.push_back(OptimizationReport{
        .name = "packed-bcd-popcount-fold",
        .detail = "Replaced a sequential " + std::to_string(kernel->digits) +
                  "-digit BCD popcount function by a shared proved bit-plane split and "
                  "horizontal fold.",
    });
    ++applied;
  }
  return applied;
}

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
  int applied = rewrite_proved_packed_kernels(program, optimizations);
  applied += rewrite_block(program, program.body, optimizations);
  for (V2Rule& rule : program.rules)
    applied += rewrite_block(program, rule.body, optimizations);
  return applied;
}

} // namespace mkpro::core
