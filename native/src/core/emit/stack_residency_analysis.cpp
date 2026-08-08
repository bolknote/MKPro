#include "mkpro/core/emit/stack_residency_analysis.hpp"

#include "mkpro/core/emit/lowering_helpers.hpp"
#include "mkpro/core/parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace mkpro::core::emit {

namespace {

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool expression_pure_for_substitution(const Expression& expression) {
  if (expression.kind == "call") {
    const std::string callee = lower_ascii(expression.callee);
    if (callee == "random" || callee == "entered" || callee == "read")
      return false;
  }
  if (expression.index != nullptr && !expression_pure_for_substitution(*expression.index))
    return false;
  if (expression.expr != nullptr && !expression_pure_for_substitution(*expression.expr))
    return false;
  if (expression.left != nullptr && !expression_pure_for_substitution(*expression.left))
    return false;
  if (expression.right != nullptr && !expression_pure_for_substitution(*expression.right))
    return false;
  return std::all_of(expression.args.begin(), expression.args.end(),
                     [](const Expression& arg) { return expression_pure_for_substitution(arg); });
}

bool stack_temp_source_is_safe(const Expression& expression) {
  if (!expression_pure_for_substitution(expression))
    return false;
  static const std::set<std::string> kPureUnaryBuiltins = {
      "abs",      "acos",     "asin", "atg",  "cos",      "exp",
      "frac",     "from_min", "from_sec", "int", "inv",  "lg",
      "ln",       "sign",     "sin",  "sqr",  "sqrt",     "tg",
      "to_min",   "to_sec",
  };
  std::function<bool(const Expression&)> calls_are_safe = [&](const Expression& current) {
    if (current.kind == "call" &&
        (current.args.size() != 1U ||
         !kPureUnaryBuiltins.contains(lower_ascii(current.callee)))) {
      return false;
    }
    if (current.index != nullptr && !calls_are_safe(*current.index))
      return false;
    if (current.expr != nullptr && !calls_are_safe(*current.expr))
      return false;
    if (current.left != nullptr && !calls_are_safe(*current.left))
      return false;
    if (current.right != nullptr && !calls_are_safe(*current.right))
      return false;
    return std::all_of(current.args.begin(), current.args.end(), calls_are_safe);
  };
  return calls_are_safe(expression);
}

std::optional<Expression> parse_expression_or_none(const std::optional<std::string>& text,
                                                   int line) {
  if (!text.has_value())
    return std::nullopt;
  try {
    return parse_expression(*text, line);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<Expression> parse_expression_or_none(const std::string& text, int line) {
  try {
    return parse_expression(text, line);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool expression_text_references_identifier(const std::string& text, const std::string& name,
                                           int line) {
  const std::optional<Expression> expression = parse_expression_or_none(text, line);
  return expression.has_value() && expression_references_identifier(*expression, name);
}

bool expression_text_references_identifier(const std::optional<std::string>& text,
                                           const std::string& name, int line) {
  if (!text.has_value())
    return false;
  return expression_text_references_identifier(*text, name, line);
}

bool target_text_reads_identifier(const std::optional<std::string>& text,
                                  const std::string& name, int line) {
  if (!text.has_value())
    return false;
  const std::optional<Expression> expression = parse_expression_or_none(*text, line);
  if (!expression.has_value() || expression->kind == "identifier")
    return false;
  return expression_references_identifier(*expression, name);
}

bool statement_writes_identifier(const V2Statement& statement, const std::string& name) {
  if ((statement.kind != "v2_assign" && statement.kind != "v2_update") ||
      !statement.target.has_value())
    return false;
  const std::optional<Expression> target = parse_expression_or_none(*statement.target,
                                                                    statement.line);
  return target.has_value() && target->kind == "identifier" && target->name == name;
}

bool statements_read_identifier(const std::vector<V2Statement>& statements,
                                const std::string& name);

bool statements_read_identifier_before_write(const std::vector<V2Statement>& statements,
                                             const std::string& name);

bool statement_reads_identifier(const V2Statement& statement, const std::string& name) {
  if (statement.kind == "v2_update" && statement.target.has_value()) {
    const std::optional<Expression> target =
        parse_expression_or_none(*statement.target, statement.line);
    if (target.has_value() && target->kind == "identifier" && target->name == name)
      return true;
  }
  if (target_text_reads_identifier(statement.target, name, statement.line) ||
      expression_text_references_identifier(statement.expr, name, statement.line)) {
    return true;
  }
  if (statement.predicate.has_value()) {
    const V2Predicate& predicate = *statement.predicate;
    if (expression_text_references_identifier(predicate.left, name, statement.line) ||
        expression_text_references_identifier(predicate.right, name, statement.line) ||
        expression_text_references_identifier(predicate.collection, name, statement.line) ||
        expression_text_references_identifier(predicate.item, name, statement.line)) {
      return true;
    }
  }
  for (const std::string& arg : statement.args) {
    if (expression_text_references_identifier(arg, name, statement.line))
      return true;
  }
  if (statement.items.has_value()) {
    for (const DisplayItem& item : *statement.items) {
      if (item.expr.has_value() && expression_references_identifier(*item.expr, name))
        return true;
    }
  }
  for (const V2RawInput& input : statement.inputs) {
    if (expression_text_references_identifier(input.expr, name, input.line))
      return true;
  }
  for (const V2MatchCase& match_case : statement.cases) {
    for (const std::string& value : match_case.values) {
      if (expression_text_references_identifier(value, name, match_case.line))
        return true;
    }
    if (match_case.action != nullptr && statement_reads_identifier(*match_case.action, name))
      return true;
  }
  if (statement.otherwise != nullptr && statement_reads_identifier(*statement.otherwise, name))
    return true;
  return statements_read_identifier(statement.body, name) ||
         statements_read_identifier(statement.then_body, name) ||
         statements_read_identifier(statement.else_body, name);
}

bool statements_read_identifier(const std::vector<V2Statement>& statements,
                                const std::string& name) {
  return std::any_of(statements.begin(), statements.end(), [&](const V2Statement& statement) {
    return statement_reads_identifier(statement, name);
  });
}

bool condition_references_protected(const V2Predicate& predicate,
                                    const std::set<std::string>& protected_temps, int line) {
  for (const std::string& temp : protected_temps) {
    if (expression_text_references_identifier(predicate.left, temp, line) ||
        expression_text_references_identifier(predicate.right, temp, line) ||
        expression_text_references_identifier(predicate.collection, temp, line) ||
        expression_text_references_identifier(predicate.item, temp, line)) {
      return true;
    }
  }
  return false;
}

bool statements_preserve_stack_residency_block(const std::vector<V2Statement>& statements,
                                               const std::set<std::string>& protected_temps) {
  return std::all_of(statements.begin(), statements.end(), [&](const V2Statement& statement) {
    return statement_preserves_stack_residency(statement, protected_temps);
  });
}

bool is_stack_resident_consumer(const V2Statement& statement) {
  if (statement.kind == "v2_assign" || statement.kind == "v2_stop" ||
      statement.kind == "v2_preview" || statement.kind == "v2_return") {
    return true;
  }
  return statement.kind == "v2_update" && statement.target.has_value() &&
         statement.expr.has_value() && statement.op.has_value() &&
         (*statement.op == "+=" || *statement.op == "-=");
}

std::optional<std::string> consumer_overwrite_target(const V2Statement& consumer) {
  if (consumer.kind == "v2_assign")
    return consumer.target;
  return std::nullopt;
}

bool stack_temp_value_dead_after_consumer(const std::string& temp,
                                          const std::optional<std::string>& overwritten,
                                          const std::vector<V2Statement>& tail) {
  return (overwritten.has_value() && *overwritten == temp) ||
         !statements_read_identifier_before_write(tail, temp);
}

bool statements_read_identifier_before_write(const std::vector<V2Statement>& statements,
                                             const std::string& name) {
  for (const V2Statement& statement : statements) {
    if (statement_reads_identifier(statement, name))
      return true;
    if (statement_writes_identifier(statement, name))
      return false;
  }
  return false;
}

bool stack_temp_other_operand_is_safe(const Expression& expression) {
  return expression.kind != "call" && expression_pure_for_substitution(expression);
}

bool validate_stack_resident_expression(const Expression& expression,
                                        const std::vector<std::string>& temps) {
  if (expression.kind == "number" || expression.kind == "string")
    return true;
  if (expression.kind == "identifier") {
    return std::find(temps.begin(), temps.end(), expression.name) != temps.end();
  }
  if (expression.kind == "indexed")
    return false;
  if (expression.kind == "unary" && expression.expr != nullptr)
    return validate_stack_resident_expression(*expression.expr, temps);
  if (expression.kind == "binary" && expression.left != nullptr && expression.right != nullptr) {
    const bool left_refs = std::any_of(temps.begin(), temps.end(), [&](const std::string& temp) {
      return count_identifier_reads(*expression.left, temp) > 0;
    });
    const bool right_refs = std::any_of(temps.begin(), temps.end(), [&](const std::string& temp) {
      return count_identifier_reads(*expression.right, temp) > 0;
    });
    if (left_refs && right_refs) {
      return validate_stack_resident_expression(*expression.left, temps) &&
             validate_stack_resident_expression(*expression.right, temps);
    }
    if (left_refs) {
      return validate_stack_resident_expression(*expression.left, temps) &&
             stack_temp_other_operand_is_safe(*expression.right);
    }
    if (right_refs) {
      return validate_stack_resident_expression(*expression.right, temps) &&
             stack_temp_other_operand_is_safe(*expression.left) &&
             (expression.op == "+" || expression.op == "*");
    }
    return stack_temp_other_operand_is_safe(*expression.left) &&
           stack_temp_other_operand_is_safe(*expression.right);
  }
  if (expression.kind == "call") {
    if (expression.args.empty())
      return false;
    return std::all_of(expression.args.begin(), expression.args.end(), [&](const Expression& arg) {
      return validate_stack_resident_expression(arg, temps);
    });
  }
  return false;
}

Expression sum_expressions_for_stack_analysis(const std::vector<Expression>& expressions) {
  if (expressions.empty())
    return number_expression("0");
  Expression sum = expressions.front();
  for (std::size_t index = 1; index < expressions.size(); ++index)
    sum = add_expression(std::move(sum), expressions.at(index));
  return sum;
}

bool expression_duplicates_single_stack_temp(const Expression& expression,
                                             const std::vector<std::string>& temps) {
  if (expression.kind == "call" && lower_ascii(expression.callee) == "sum")
    return expression_duplicates_single_stack_temp(
        sum_expressions_for_stack_analysis(expression.args), temps);
  if (temps.size() != 1U || expression.kind != "binary" ||
      (expression.op != "+" && expression.op != "-" && expression.op != "*" &&
       expression.op != "/") ||
      expression.left == nullptr || expression.right == nullptr) {
    return false;
  }
  if (expression_to_json(*expression.left) != expression_to_json(*expression.right))
    return false;
  return count_identifier_reads(*expression.left, temps.front()) == 1 &&
         validate_stack_resident_expression(*expression.left, temps);
}

bool collect_repeated_sum_stack_temp(const Expression& expression, const std::string& temp,
                                     int& count) {
  if (expression.kind == "identifier" && expression.name == temp) {
    ++count;
    return true;
  }
  if (expression.kind == "call" && lower_ascii(expression.callee) == "sum") {
    return std::all_of(expression.args.begin(), expression.args.end(), [&](const Expression& arg) {
      return collect_repeated_sum_stack_temp(arg, temp, count);
    });
  }
  if (expression.kind == "binary" && expression.op == "+" && expression.left != nullptr &&
      expression.right != nullptr) {
    return collect_repeated_sum_stack_temp(*expression.left, temp, count) &&
           collect_repeated_sum_stack_temp(*expression.right, temp, count);
  }
  return false;
}

bool expression_repeats_single_stack_temp_sum(const Expression& expression,
                                              const std::vector<std::string>& temps) {
  if (temps.size() != 1U)
    return false;
  int count = 0;
  return collect_repeated_sum_stack_temp(expression, temps.front(), count) && count >= 2 &&
         count <= 4;
}

bool two_temp_shared_rhs_shape(const Expression& expression,
                               const std::vector<std::string>& temps) {
  if (temps.size() != 2U || expression.kind != "binary" || expression.op != "-" ||
      expression.left == nullptr || expression.right == nullptr ||
      expression.left->kind != "binary" || expression.left->op != "-" ||
      expression.left->left == nullptr || expression.left->right == nullptr ||
      expression.left->left->kind != "identifier" ||
      expression.left->right->kind != "identifier" ||
      expression.left->left->name != temps.at(0) ||
      expression.left->right->name != temps.at(1)) {
    return false;
  }
  return count_identifier_reads(*expression.right, temps.at(0)) == 0 &&
         count_identifier_reads(*expression.right, temps.at(1)) == 1 &&
         validate_stack_resident_expression(*expression.right, {temps.at(1)});
}

bool assign_temp_is_safe(const V2Statement& statement,
                         const std::vector<std::string>& targets) {
  if (statement.kind != "v2_assign" || !statement.target.has_value() ||
      !statement.expr.has_value()) {
    return false;
  }
  if (std::find(targets.begin(), targets.end(), *statement.target) != targets.end())
    return false;
  const std::optional<Expression> expression =
      parse_expression_or_none(*statement.expr, statement.line);
  if (!expression.has_value() || !stack_temp_source_is_safe(*expression))
    return false;
  if (expression_references_identifier(*expression, *statement.target))
    return false;
  if (targets.empty())
    return true;
  const bool references_prior = std::any_of(
      targets.begin(), targets.end(), [&](const std::string& target) {
        return expression_references_identifier(*expression, target);
      });
  return !references_prior || can_lower_stack_resident_expression(*expression, targets);
}

int count_indexed_consumer(const std::vector<V2Statement>& statements, std::size_t start) {
  if (start + 1U >= statements.size())
    return 0;
  const V2Statement& temp = statements.at(start);
  const V2Statement& consumer = statements.at(start + 1U);
  if (temp.kind != "v2_assign" || !temp.target.has_value() || consumer.kind != "v2_assign")
    return 0;
  if (find_stack_resident_fusion_site(statements, start).has_value())
    return 0;
  const std::optional<Expression> source = parse_expression_or_none(temp.expr, temp.line);
  const std::optional<Expression> consumer_expr =
      parse_expression_or_none(consumer.expr, consumer.line);
  if (!source.has_value() || !consumer_expr.has_value() || !stack_temp_source_is_safe(*source))
    return 0;
  if (expression_references_identifier(*source, *temp.target))
    return 0;
  const std::vector<V2Statement> tail(statements.begin() + static_cast<std::ptrdiff_t>(start + 2U),
                                      statements.end());
  if (!stack_temp_value_dead_after_consumer(*temp.target, std::nullopt, tail))
    return 0;
  if (count_identifier_reads(*consumer_expr, *temp.target) != 1)
    return 0;
  return can_lower_stack_resident_expression(*consumer_expr, {*temp.target}) ? 1 : 0;
}

int count_straight_single_use_pair(const std::vector<V2Statement>& statements, std::size_t start) {
  if (start + 1U >= statements.size())
    return 0;
  const V2Statement& temp = statements.at(start);
  const V2Statement& consumer = statements.at(start + 1U);
  if (temp.kind != "v2_assign" || !temp.target.has_value() || !is_stack_resident_consumer(consumer))
    return 0;
  if (find_stack_resident_fusion_site(statements, start).has_value())
    return 0;
  const std::optional<Expression> source = parse_expression_or_none(temp.expr, temp.line);
  const std::optional<Expression> consumer_expr =
      parse_expression_or_none(consumer.expr, consumer.line);
  if (!source.has_value() || !consumer_expr.has_value() || !stack_temp_source_is_safe(*source))
    return 0;
  if (expression_references_identifier(*source, *temp.target))
    return 0;
  const std::vector<V2Statement> tail(statements.begin() + static_cast<std::ptrdiff_t>(start + 2U),
                                      statements.end());
  if (!stack_temp_value_dead_after_consumer(*temp.target, consumer_overwrite_target(consumer),
                                            tail))
    return 0;
  if (count_identifier_reads(*consumer_expr, *temp.target) != 1)
    return 0;
  return can_lower_stack_resident_expression(*consumer_expr, {*temp.target}) ? 1 : 0;
}

int peak_live_assign_temps_in_block(const std::vector<V2Statement>& statements) {
  std::set<std::string> defs;
  std::set<std::string> used_defs;
  std::function<void(const V2Statement&)> index_statement = [&](const V2Statement& statement) {
    if (statement.kind == "v2_assign" && statement.target.has_value())
      defs.insert(*statement.target);
    for (const std::string& target : defs) {
      if (statement_reads_identifier(statement, target))
        used_defs.insert(target);
    }
    for (const V2Statement& child : statement.body)
      index_statement(child);
    for (const V2Statement& child : statement.then_body)
      index_statement(child);
    for (const V2Statement& child : statement.else_body)
      index_statement(child);
    for (const V2MatchCase& match_case : statement.cases) {
      if (match_case.action != nullptr)
        index_statement(*match_case.action);
    }
    if (statement.otherwise != nullptr)
      index_statement(*statement.otherwise);
  };
  for (const V2Statement& statement : statements)
    index_statement(statement);
  return defs.empty() ? 0 : static_cast<int>(used_defs.size());
}

std::optional<std::string> arithmetic_update_operator(const V2Statement& statement) {
  if (!statement.op.has_value())
    return std::nullopt;
  if (*statement.op == "+=")
    return "+";
  if (*statement.op == "-=")
    return "-";
  if (*statement.op == "*=")
    return "*";
  if (*statement.op == "/=")
    return "/";
  return std::nullopt;
}

std::optional<ForwardableValueDefinition>
forwardable_value_definition(const V2Statement& statement, std::size_t statement_index) {
  if ((statement.kind != "v2_assign" && statement.kind != "v2_update") ||
      !statement.target.has_value() || !statement.expr.has_value()) {
    return std::nullopt;
  }
  const std::optional<Expression> target =
      parse_expression_or_none(*statement.target, statement.line);
  if (!target.has_value() || target->kind != "identifier")
    return std::nullopt;

  std::optional<Expression> value;
  if (statement.kind == "v2_assign") {
    value = parse_expression_or_none(statement.expr, statement.line);
  } else {
    const std::optional<std::string> arithmetic = arithmetic_update_operator(statement);
    if (!arithmetic.has_value())
      return std::nullopt;
    value = parse_expression_or_none("(" + target->name + ") " + *arithmetic + " (" +
                                         *statement.expr + ")",
                                     statement.line);
  }
  if (!value.has_value() || !stack_temp_source_is_safe(*value))
    return std::nullopt;
  return ForwardableValueDefinition{
      .statement = statement,
      .target = target->name,
      .value = std::move(*value),
      .statement_index = statement_index,
  };
}

void collect_expression_identifiers(const Expression& expression,
                                    std::set<std::string>& identifiers) {
  if (expression.kind == "identifier")
    identifiers.insert(expression.name);
  if (expression.index != nullptr)
    collect_expression_identifiers(*expression.index, identifiers);
  if (expression.expr != nullptr)
    collect_expression_identifiers(*expression.expr, identifiers);
  if (expression.left != nullptr)
    collect_expression_identifiers(*expression.left, identifiers);
  if (expression.right != nullptr)
    collect_expression_identifiers(*expression.right, identifiers);
  for (const Expression& arg : expression.args)
    collect_expression_identifiers(arg, identifiers);
}

} // namespace

bool expression_references_identifier(const Expression& expression, const std::string& name) {
  return count_identifier_reads(expression, name) > 0;
}

int count_identifier_reads(const Expression& expression, const std::string& name) {
  int count = expression.kind == "identifier" && expression.name == name ? 1 : 0;
  if (expression.index != nullptr)
    count += count_identifier_reads(*expression.index, name);
  if (expression.expr != nullptr)
    count += count_identifier_reads(*expression.expr, name);
  if (expression.left != nullptr)
    count += count_identifier_reads(*expression.left, name);
  if (expression.right != nullptr)
    count += count_identifier_reads(*expression.right, name);
  for (const Expression& arg : expression.args)
    count += count_identifier_reads(arg, name);
  return count;
}

std::optional<ForwardableValueRegion> analyze_forwardable_value_region(
    const std::vector<V2Statement>& statements, std::size_t definition_begin,
    std::size_t consumer_begin, std::size_t consumer_end) {
  if (definition_begin >= consumer_begin || consumer_begin >= consumer_end ||
      consumer_end > statements.size()) {
    return std::nullopt;
  }

  std::vector<ForwardableValueDefinition> definitions;
  std::set<std::string> targets;
  for (std::size_t index = definition_begin; index < consumer_begin; ++index) {
    const std::optional<ForwardableValueDefinition> definition =
        forwardable_value_definition(statements.at(index), index);
    if (!definition.has_value() || !targets.insert(definition->target).second)
      return std::nullopt;
    definitions.push_back(*definition);
  }

  for (const ForwardableValueDefinition& definition : definitions) {
    for (const std::string& target : targets) {
      if (target != definition.target &&
          expression_references_identifier(definition.value, target)) {
        return std::nullopt;
      }
    }
  }

  for (std::size_t index = consumer_begin; index < consumer_end; ++index) {
    const V2Statement& statement = statements.at(index);
    if ((statement.kind != "v2_assign" && statement.kind != "v2_update") ||
        !statement.expr.has_value()) {
      return std::nullopt;
    }
    const std::optional<Expression> expression =
        parse_expression_or_none(statement.expr, statement.line);
    if (!expression.has_value() || !stack_temp_source_is_safe(*expression))
      return std::nullopt;
  }

  for (const ForwardableValueDefinition& definition : definitions) {
    bool read_before_overwrite = false;
    std::optional<std::size_t> first_overwrite;
    for (std::size_t index = consumer_begin; index < consumer_end; ++index) {
      const V2Statement& statement = statements.at(index);
      if (statement_reads_identifier(statement, definition.target))
        read_before_overwrite = true;
      if (statement_writes_identifier(statement, definition.target)) {
        first_overwrite = index;
        break;
      }
    }
    if (!read_before_overwrite || !first_overwrite.has_value())
      return std::nullopt;

    std::set<std::string> source_identifiers;
    collect_expression_identifiers(definition.value, source_identifiers);
    source_identifiers.erase(definition.target);
    for (const std::string& source : source_identifiers) {
      for (std::size_t index = consumer_begin; index <= *first_overwrite; ++index) {
        if (statement_writes_identifier(statements.at(index), source))
          return std::nullopt;
      }
    }
  }

  return ForwardableValueRegion{
      .definitions = std::move(definitions),
      .consumer_begin = consumer_begin,
      .consumer_end = consumer_end,
  };
}

std::optional<ForwardableValueRegion> find_forwardable_value_region(
    const std::vector<V2Statement>& statements, std::size_t definition_begin) {
  if (definition_begin >= statements.size())
    return std::nullopt;

  std::vector<ForwardableValueDefinition> definitions;
  std::set<std::string> targets;
  std::size_t consumer_begin = definition_begin;
  for (; consumer_begin < statements.size(); ++consumer_begin) {
    const V2Statement& statement = statements.at(consumer_begin);
    const bool reads_prior = std::any_of(
        targets.begin(), targets.end(), [&](const std::string& target) {
          return statement_reads_identifier(statement, target);
        });
    if (reads_prior)
      break;

    const std::optional<ForwardableValueDefinition> definition =
        forwardable_value_definition(statement, consumer_begin);
    if (!definition.has_value() || !targets.insert(definition->target).second)
      break;
    definitions.push_back(*definition);
  }
  if (definitions.empty() || consumer_begin >= statements.size())
    return std::nullopt;

  // Definitions in one delayed group may read their own old value, but not a
  // sibling's value: otherwise moving their materialization independently
  // would change the source-level evaluation order.
  for (const ForwardableValueDefinition& definition : definitions) {
    for (const std::string& target : targets) {
      if (target != definition.target &&
          expression_references_identifier(definition.value, target)) {
        return std::nullopt;
      }
    }
  }

  std::size_t consumer_end = consumer_begin;
  for (const ForwardableValueDefinition& definition : definitions) {
    std::set<std::string> dependencies;
    collect_expression_identifiers(definition.value, dependencies);
    dependencies.erase(definition.target);

    bool found_read = false;
    bool found_overwrite = false;
    for (std::size_t index = consumer_begin; index < statements.size(); ++index) {
      const V2Statement& statement = statements.at(index);
      if ((statement.kind != "v2_assign" && statement.kind != "v2_update") ||
          !statement.expr.has_value()) {
        return std::nullopt;
      }
      const std::optional<Expression> expression =
          parse_expression_or_none(statement.expr, statement.line);
      if (!expression.has_value() || !stack_temp_source_is_safe(*expression))
        return std::nullopt;

      // The RHS is evaluated before its target is written. Continue through
      // every pure reader until the first overwrite: lowering may fuse several
      // source reads into one actual recall, so the store decision belongs to
      // the emitted def-use stream rather than the source read count.
      if (!found_read && statement_reads_identifier(statement, definition.target))
        found_read = true;
      if (statement_writes_identifier(statement, definition.target)) {
        if (!found_read)
          return std::nullopt;
        found_overwrite = true;
        consumer_end = std::max(consumer_end, index + 1U);
        break;
      }
      if (!found_read) {
        for (const std::string& dependency : dependencies) {
          if (statement_writes_identifier(statement, dependency))
            return std::nullopt;
        }
      }
    }
    if (!found_read || !found_overwrite)
      return std::nullopt;
  }

  return ForwardableValueRegion{
      .definitions = std::move(definitions),
      .consumer_begin = consumer_begin,
      .consumer_end = consumer_end,
  };
}

bool can_lower_stack_resident_expression(const Expression& expression,
                                         const std::vector<std::string>& temps) {
  if (two_temp_shared_rhs_shape(expression, temps))
    return true;
  if (expression.kind == "binary" && expression.left != nullptr && expression.right != nullptr) {
    const bool left_refs = std::any_of(temps.begin(), temps.end(), [&](const std::string& temp) {
      return count_identifier_reads(*expression.left, temp) > 0;
    });
    const bool right_refs = std::any_of(temps.begin(), temps.end(), [&](const std::string& temp) {
      return count_identifier_reads(*expression.right, temp) > 0;
    });
    if (left_refs && !right_refs &&
        can_lower_stack_resident_expression(*expression.left, temps) &&
        stack_temp_other_operand_is_safe(*expression.right)) {
      return true;
    }
    if (!left_refs && right_refs &&
        can_lower_stack_resident_expression(*expression.right, temps) &&
        stack_temp_other_operand_is_safe(*expression.left) &&
        (expression.op == "+" || expression.op == "*")) {
      return true;
    }
  }
  if (expression_duplicates_single_stack_temp(expression, temps))
    return true;
  if (expression_repeats_single_stack_temp_sum(expression, temps))
    return true;
  for (const std::string& temp : temps) {
    if (count_identifier_reads(expression, temp) != 1)
      return false;
  }
  return validate_stack_resident_expression(expression, temps);
}

std::vector<StackResidentRestoreOp> stack_resident_restore_ops(std::size_t temp_index,
                                                               std::size_t temp_count) {
  const std::size_t depth_from_x = temp_count - 1U - temp_index;
  if (depth_from_x == 0U)
    return {};
  if (depth_from_x == 1U)
    return {StackResidentRestoreOp::Swap};
  if (depth_from_x == 2U)
    return {StackResidentRestoreOp::Reverse, StackResidentRestoreOp::Swap};
  return {StackResidentRestoreOp::Reverse, StackResidentRestoreOp::Reverse,
          StackResidentRestoreOp::Swap};
}

bool statement_preserves_stack_residency(const V2Statement& statement,
                                         const std::set<std::string>& protected_temps) {
  for (const std::string& temp : protected_temps) {
    if (statement_writes_identifier(statement, temp) || statement_reads_identifier(statement, temp))
      return false;
  }
  if (statement.kind == "v2_if") {
    if (statement.predicate.has_value() &&
        condition_references_protected(*statement.predicate, protected_temps, statement.line)) {
      return false;
    }
    return statements_preserve_stack_residency_block(statement.then_body, protected_temps) &&
           statements_preserve_stack_residency_block(statement.else_body, protected_temps);
  }
  if (statement.kind == "v2_loop")
    return statements_preserve_stack_residency_block(statement.body, protected_temps);
  if (statement.kind == "v2_while") {
    if (statement.predicate.has_value() &&
        condition_references_protected(*statement.predicate, protected_temps, statement.line)) {
      return false;
    }
    return statements_preserve_stack_residency_block(statement.body, protected_temps);
  }
  if (statement.kind == "v2_match") {
    if (statement.expr.has_value()) {
      for (const std::string& temp : protected_temps) {
        if (expression_text_references_identifier(*statement.expr, temp, statement.line))
          return false;
      }
    }
    for (const V2MatchCase& match_case : statement.cases) {
      for (const std::string& temp : protected_temps) {
        for (const std::string& value : match_case.values) {
          if (expression_text_references_identifier(value, temp, match_case.line))
            return false;
        }
      }
      if (match_case.action != nullptr &&
          !statement_preserves_stack_residency(*match_case.action, protected_temps)) {
        return false;
      }
    }
    return statement.otherwise == nullptr ||
           statement_preserves_stack_residency(*statement.otherwise, protected_temps);
  }
  return false;
}

std::optional<StackResidentFusionSite>
find_stack_resident_fusion_site(const std::vector<V2Statement>& statements, std::size_t start) {
  std::vector<StackResidentTempSegment> segments;
  std::vector<std::string> targets;
  std::size_t index = start;

  while (index < statements.size() && segments.size() < 4U) {
    const V2Statement& statement = statements.at(index);
    if (!assign_temp_is_safe(statement, targets) || !statement.target.has_value())
      break;
    targets.push_back(*statement.target);
    ++index;
    const std::size_t preserve_start = index;
    while (index < statements.size() &&
           statement_preserves_stack_residency(
               statements.at(index), std::set<std::string>(targets.begin(), targets.end()))) {
      ++index;
    }
    segments.push_back(StackResidentTempSegment{
        .assign = statement,
        .preserve_after = std::vector<V2Statement>(
            statements.begin() + static_cast<std::ptrdiff_t>(preserve_start),
            statements.begin() + static_cast<std::ptrdiff_t>(index)),
    });
    if (index >= statements.size())
      return std::nullopt;
    if (statements.at(index).kind == "v2_assign" && segments.size() < 4U)
      continue;
    break;
  }

  if (segments.empty() || index >= statements.size())
    return std::nullopt;
  const V2Statement& consumer = statements.at(index);
  if (!is_stack_resident_consumer(consumer))
    return std::nullopt;
  const std::optional<Expression> consumer_expr =
      parse_expression_or_none(consumer.expr, consumer.line);
  if (!consumer_expr.has_value())
    return std::nullopt;

  std::vector<std::string> temp_names;
  temp_names.reserve(segments.size());
  for (const StackResidentTempSegment& segment : segments) {
    if (!segment.assign.target.has_value())
      return std::nullopt;
    temp_names.push_back(*segment.assign.target);
  }
  const std::vector<V2Statement> tail(statements.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                                      statements.end());
  const std::optional<std::string> overwrite = consumer_overwrite_target(consumer);
  for (const std::string& name : temp_names) {
    if (!stack_temp_value_dead_after_consumer(name, overwrite, tail))
      return std::nullopt;
  }
  if (consumer.kind == "v2_update") {
    for (const std::string& name : temp_names) {
      if (expression_text_references_identifier(consumer.target, name, consumer.line))
        return std::nullopt;
    }
  }
  if (!can_lower_stack_resident_expression(*consumer_expr, temp_names))
    return std::nullopt;

  const bool crosses_control_flow =
      std::any_of(segments.begin(), segments.end(), [](const StackResidentTempSegment& segment) {
        return !segment.preserve_after.empty();
      });
  // Plain assignments already have a dedicated single-use X scheduler. Updates
  // do not, so let the general stack-resident lowering handle an adjacent pure
  // temporary consumed by += or -= as well.
  if (segments.size() == 1U && !crosses_control_flow && consumer.kind == "v2_assign")
    return std::nullopt;

  return StackResidentFusionSite{
      .temps = std::move(segments),
      .consumer = consumer,
      .consumer_index = index,
      .crosses_control_flow = crosses_control_flow,
  };
}

StackResidencySummary
summarize_stack_residency_candidates_in_block(const std::vector<V2Statement>& statements) {
  StackResidencySummary summary;
  summary.max_live_temps = peak_live_assign_temps_in_block(statements);

  std::function<void(const std::vector<V2Statement>&)> visit =
      [&](const std::vector<V2Statement>& body) {
        for (std::size_t index = 0; index < body.size(); ++index) {
          const std::optional<StackResidentFusionSite> site =
              find_stack_resident_fusion_site(body, index);
          if (site.has_value()) {
            summary.fusion_sites += 1;
            if (site->crosses_control_flow)
              summary.control_flow_fusions += 1;
            index = site->consumer_index;
            continue;
          }
          summary.indexed_consumers += count_indexed_consumer(body, index);
          summary.single_use_pairs += count_straight_single_use_pair(body, index);
          const V2Statement& statement = body.at(index);
          visit(statement.body);
          visit(statement.then_body);
          visit(statement.else_body);
          for (const V2MatchCase& match_case : statement.cases) {
            if (match_case.action != nullptr)
              visit({*match_case.action});
          }
          if (statement.otherwise != nullptr)
            visit({*statement.otherwise});
        }
      };
  visit(statements);
  return summary;
}

} // namespace mkpro::core::emit
