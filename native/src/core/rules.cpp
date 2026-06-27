#include "mkpro/core/rules.hpp"

#include "mkpro/core/parser.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <optional>

namespace mkpro::core {

namespace {

void collect_calls(const V2Statement& statement, std::set<std::string>& calls) {
  if (statement.kind == "v2_invoke" && statement.name.has_value())
    calls.insert(*statement.name);
  for (const V2Statement& child : statement.body)
    collect_calls(child, calls);
  for (const V2Statement& child : statement.then_body)
    collect_calls(child, calls);
  for (const V2Statement& child : statement.else_body)
    collect_calls(child, calls);
  for (const V2MatchCase& match_case : statement.cases) {
    if (match_case.action != nullptr)
      collect_calls(*match_case.action, calls);
  }
  if (statement.otherwise != nullptr)
    collect_calls(*statement.otherwise, calls);
}

bool statement_returns_value(const V2Statement& statement) {
  if (statement.kind == "v2_return")
    return true;
  for (const V2Statement& child : statement.body) {
    if (statement_returns_value(child))
      return true;
  }
  for (const V2Statement& child : statement.then_body) {
    if (statement_returns_value(child))
      return true;
  }
  for (const V2Statement& child : statement.else_body) {
    if (statement_returns_value(child))
      return true;
  }
  for (const V2MatchCase& match_case : statement.cases) {
    if (match_case.action != nullptr && statement_returns_value(*match_case.action))
      return true;
  }
  return statement.otherwise != nullptr && statement_returns_value(*statement.otherwise);
}

bool rule_returns_value(const V2Rule& rule) {
  return std::any_of(rule.body.begin(), rule.body.end(), [](const V2Statement& statement) {
    return statement_returns_value(statement);
  });
}

void count_calls(const V2Statement& statement, std::map<std::string, int>& counts) {
  if (statement.kind == "v2_invoke" && statement.name.has_value())
    counts[*statement.name] += 1;
  for (const V2Statement& child : statement.body)
    count_calls(child, counts);
  for (const V2Statement& child : statement.then_body)
    count_calls(child, counts);
  for (const V2Statement& child : statement.else_body)
    count_calls(child, counts);
  for (const V2MatchCase& match_case : statement.cases) {
    if (match_case.action != nullptr)
      count_calls(*match_case.action, counts);
  }
  if (statement.otherwise != nullptr)
    count_calls(*statement.otherwise, counts);
}

std::map<std::string, int> collect_call_counts(const V2Program& program) {
  std::map<std::string, int> counts;
  for (const V2Statement& statement : program.body)
    count_calls(statement, counts);
  for (const V2Rule& rule : program.rules) {
    for (const V2Statement& statement : rule.body)
      count_calls(statement, counts);
  }
  return counts;
}

std::map<std::string, std::set<std::string>> rule_call_graph(const V2Program& program) {
  std::map<std::string, std::set<std::string>> graph;
  for (const V2Rule& rule : program.rules) {
    std::set<std::string> calls;
    for (const V2Statement& statement : rule.body)
      collect_calls(statement, calls);
    graph[rule.name] = std::move(calls);
  }
  return graph;
}

bool rule_reaches(const std::map<std::string, std::set<std::string>>& graph,
                  const std::string& start, const std::string& target) {
  std::set<std::string> visiting;
  auto visit = [&](const std::string& name, const auto& self) -> bool {
    const auto calls_it = graph.find(name);
    if (calls_it == graph.end())
      return false;
    for (const std::string& callee : calls_it->second) {
      if (callee == target)
        return true;
      if (visiting.contains(callee))
        continue;
      visiting.insert(callee);
      if (self(callee, self))
        return true;
    }
    return false;
  };
  visiting.insert(start);
  return visit(start, visit);
}

constexpr int kInfiniteCost = std::numeric_limits<int>::max() / 4;

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
      cost += 1;
  }
  if (exponent_index != std::string::npos) {
    cost += 1;
    if (!exponent.empty() && exponent.front() == '-')
      cost += 1;
    for (const char ch : exponent) {
      if (ch != '+' && ch != '-' && std::isdigit(static_cast<unsigned char>(ch)) != 0)
        cost += 1;
    }
  }
  return cost;
}

bool numeric_literal_is_value(const Expression& expression, int value) {
  if (expression.kind != "number")
    return false;
  try {
    std::size_t parsed = 0;
    const double number = std::stod(expression.raw, &parsed);
    return parsed == expression.raw.size() && number == static_cast<double>(value);
  } catch (const std::exception&) {
    return false;
  }
}

int estimate_expression_cost_impl(const Expression& expression);

int estimate_call_cost(const Expression& expression) {
  std::string name = expression.callee;
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (name == "random") {
    if (expression.args.size() == 1U)
      return estimate_expression_cost_impl(expression.args.front()) + 2;
    if (expression.args.size() == 2U)
      return estimate_expression_cost_impl(expression.args.at(0)) +
             estimate_expression_cost_impl(expression.args.at(1)) + 2;
    return 1;
  }
  if (name == "pi")
    return 1;
  if (name == "e")
    return 2;
  if (name == "pow") {
    if (expression.args.size() >= 2U && numeric_literal_is_value(expression.args.at(1), 2))
      return estimate_expression_cost_impl(expression.args.at(0)) + 1;
    if (expression.args.size() >= 2U && numeric_literal_is_value(expression.args.at(0), 10))
      return estimate_expression_cost_impl(expression.args.at(1)) + 1;
  }
  if (name == "min" || name == "max" || name == "safe_min" || name == "safe_max" ||
      name == "bit_and" || name == "bit_or" || name == "bit_xor" || name == "pow") {
    return (expression.args.empty() ? 0 : estimate_expression_cost_impl(expression.args.at(0))) +
           (expression.args.size() < 2U ? 0 : estimate_expression_cost_impl(expression.args.at(1))) +
           1;
  }
  return (expression.args.empty() ? 0 : estimate_expression_cost_impl(expression.args.front())) + 1;
}

int estimate_expression_cost_impl(const Expression& expression) {
  if (expression.kind == "string")
    return 0;
  if (expression.kind == "number")
    return estimate_number_cost(expression.raw);
  if (expression.kind == "identifier")
    return 1;
  if (expression.kind == "indexed")
    return (expression.index == nullptr ? 0 : estimate_expression_cost_impl(*expression.index)) +
           2;
  if (expression.kind == "unary")
    return (expression.expr == nullptr ? 0 : estimate_expression_cost_impl(*expression.expr)) + 1;
  if (expression.kind == "binary") {
    return (expression.left == nullptr ? 0 : estimate_expression_cost_impl(*expression.left)) +
           (expression.right == nullptr ? 0 : estimate_expression_cost_impl(*expression.right)) +
           1;
  }
  if (expression.kind == "call")
    return estimate_call_cost(expression);
  return kInfiniteCost;
}

int estimate_expression_text_cost(const std::optional<std::string>& text, int line) {
  if (!text.has_value())
    return 0;
  try {
    return estimate_expression_cost_impl(parse_expression(*text, line));
  } catch (const std::exception&) {
    return kInfiniteCost;
  }
}

int estimate_predicate_cost(const V2Predicate& predicate, int line) {
  if (predicate.kind != "v2_compare")
    return kInfiniteCost;
  const int left = estimate_expression_text_cost(predicate.left, line);
  const int right = estimate_expression_text_cost(predicate.right, line);
  if (left >= kInfiniteCost || right >= kInfiniteCost)
    return kInfiniteCost;
  return left + right + 1;
}

int estimate_body_cost(const std::vector<V2Statement>& statements);

int estimate_statement_cost(const V2Statement& statement) {
  if (statement.kind == "v2_assign")
    return estimate_expression_text_cost(statement.expr, statement.line) + 1;
  if (statement.kind == "v2_update")
    return estimate_expression_text_cost(statement.expr, statement.line) + 2;
  if (statement.kind == "v2_preview")
    return estimate_expression_text_cost(statement.expr, statement.line);
  if (statement.kind == "v2_return")
    return estimate_expression_text_cost(statement.expr, statement.line) + 1;
  if (statement.kind == "v2_stop")
    return statement.target.has_value()
               ? estimate_expression_text_cost(statement.target, statement.line) + 1
               : 1;
  if (statement.kind == "v2_read")
    return 2;
  if (statement.kind == "v2_show")
    return estimate_expression_text_cost(statement.target, statement.line) + 1;
  if (statement.kind == "v2_invoke")
    return 2;
  if (statement.kind == "v2_raw")
    return static_cast<int>(statement.lines.size());
  if (statement.kind == "v2_block")
    return estimate_body_cost(statement.body);
  if (statement.kind == "v2_if" && statement.predicate.has_value()) {
    const int condition = estimate_predicate_cost(*statement.predicate, statement.line);
    const int then_cost = estimate_body_cost(statement.then_body);
    if (condition >= kInfiniteCost || then_cost >= kInfiniteCost)
      return kInfiniteCost;
    if (!statement.has_else_body && statement.else_body.empty())
      return condition + then_cost;
    const int else_cost = estimate_body_cost(statement.else_body);
    if (else_cost >= kInfiniteCost)
      return kInfiniteCost;
    return condition + then_cost + 2 + else_cost;
  }
  return kInfiniteCost;
}

int estimate_body_cost(const std::vector<V2Statement>& statements) {
  int total = 0;
  for (const V2Statement& statement : statements) {
    const int cost = estimate_statement_cost(statement);
    if (cost >= kInfiniteCost)
      return kInfiniteCost;
    total += cost;
  }
  return total;
}

bool statement_terminates_statically(const V2Program& program, const V2Statement& statement,
                                     std::set<std::string>& seen_rules);

bool statements_terminate_statically(const V2Program& program,
                                     const std::vector<V2Statement>& statements,
                                     std::set<std::string>& seen_rules) {
  if (statements.empty())
    return false;
  return statement_terminates_statically(program, statements.back(), seen_rules);
}

bool statement_terminates_statically(const V2Program& program, const V2Statement& statement,
                                     std::set<std::string>& seen_rules) {
  if (statement.kind == "v2_stop" || statement.kind == "v2_loop" || statement.kind == "v2_return") {
    return true;
  }
  if (statement.kind == "v2_block")
    return statements_terminate_statically(program, statement.body, seen_rules);
  if (statement.kind == "v2_if" && !statement.else_body.empty()) {
    std::set<std::string> then_seen = seen_rules;
    std::set<std::string> else_seen = seen_rules;
    return statements_terminate_statically(program, statement.then_body, then_seen) &&
           statements_terminate_statically(program, statement.else_body, else_seen);
  }
  if (statement.kind != "v2_invoke" || !statement.name.has_value())
    return false;
  const auto rule_it =
      std::find_if(program.rules.begin(), program.rules.end(),
                   [&](const V2Rule& rule) { return rule.name == *statement.name; });
  if (rule_it == program.rules.end() || seen_rules.contains(rule_it->name))
    return false;
  seen_rules.insert(rule_it->name);
  return statements_terminate_statically(program, rule_it->body, seen_rules);
}

bool statements_terminate_statically(const V2Program& program,
                                     const std::vector<V2Statement>& statements) {
  std::set<std::string> seen_rules;
  return statements_terminate_statically(program, statements, seen_rules);
}

bool straight_line_assignment_body(const std::vector<V2Statement>& statements) {
  return !statements.empty() &&
         std::all_of(statements.begin(), statements.end(), [](const V2Statement& statement) {
           return statement.kind == "v2_assign" || statement.kind == "v2_update";
         });
}

} // namespace

int estimate_expression_cost(const Expression& expression) {
  return estimate_expression_cost_impl(expression);
}

std::set<std::string> inline_statement_rule_names(const V2Program& program) {
  const std::map<std::string, std::set<std::string>> graph = rule_call_graph(program);
  const std::map<std::string, int> counts = collect_call_counts(program);
  std::set<std::string> names;
  for (const V2Rule& rule : program.rules) {
    const int uses = counts.contains(rule.name) ? counts.at(rule.name) : 0;
    if (uses == 0)
      continue;
    if (!rule.params.empty())
      continue;
    if (rule_returns_value(rule))
      continue;
    if (rule_reaches(graph, rule.name, rule.name))
      continue;
    const int body_cost = estimate_body_cost(rule.body);
    if (body_cost >= kInfiniteCost) {
      if (uses == 1)
        names.insert(rule.name);
      continue;
    }
    const bool terminal = statements_terminate_statically(program, rule.body);
    if (uses > 1 && !straight_line_assignment_body(rule.body))
      continue;
    if (uses > 1 && terminal)
      continue;
    const int subroutine_cost = body_cost + (terminal ? 0 : 1) + uses * 2;
    const int inline_cost = body_cost * uses;
    if (inline_cost < subroutine_cost)
      names.insert(rule.name);
  }
  return names;
}

} // namespace mkpro::core
