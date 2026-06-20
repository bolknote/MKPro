#include "mkpro/core/v2_const.hpp"

#include "mkpro/core/parser.hpp"

#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mkpro::core {

namespace {

Diagnostic diagnostic(DiagnosticSeverity severity, std::string code, std::string message) {
  return Diagnostic{
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
  };
}

std::optional<std::string> assignment_root_identifier(const std::string& target) {
  static const std::regex identifier_regex(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*$)");
  static const std::regex indexed_regex(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\[)");
  std::smatch match;
  if (std::regex_search(target, match, identifier_regex))
    return match[1].str();
  if (std::regex_search(target, match, indexed_regex))
    return match[1].str();
  return std::nullopt;
}

void visit_v2_statement_children(const V2Statement& statement,
                                 const std::function<void(const std::vector<V2Statement>&)>& visit) {
  if (statement.kind == "v2_if") {
    visit(statement.then_body);
    visit(statement.else_body);
  } else if (statement.kind == "v2_while" || statement.kind == "v2_loop" ||
             statement.kind == "v2_block") {
    visit(statement.body);
  } else if (statement.kind == "v2_match") {
    for (const V2MatchCase& match_case : statement.cases) {
      if (match_case.action != nullptr)
        visit(std::vector<V2Statement>{*match_case.action});
    }
    if (statement.otherwise != nullptr)
      visit(std::vector<V2Statement>{*statement.otherwise});
  }
}

void assert_no_const_assignment(const V2Program& program,
                                const std::map<std::string, Expression>& constants,
                                std::vector<Diagnostic>& diagnostics) {
  auto visit = [&](const std::vector<V2Statement>& statements, const auto& self) -> void {
    for (const V2Statement& statement : statements) {
      if ((statement.kind == "v2_assign" || statement.kind == "v2_update") &&
          statement.target.has_value()) {
        const std::optional<std::string> root = assignment_root_identifier(*statement.target);
        if (root.has_value() && constants.contains(*root)) {
          diagnostics.push_back(diagnostic(DiagnosticSeverity::Error, "parse-error",
                                           "Cannot assign to const '" + *root + "'"));
        }
      }
      visit_v2_statement_children(statement, [&](const std::vector<V2Statement>& children) {
        self(children, self);
      });
    }
  };

  visit(program.body, visit);
  for (const V2Rule& rule : program.rules)
    visit(rule.body, visit);
}

}  // namespace

Expression number_expression(std::string raw) {
  Expression expression;
  expression.kind = "number";
  expression.raw = std::move(raw);
  return expression;
}

std::string format_number_literal(double value) {
  if (!std::isfinite(value))
    throw std::runtime_error("non-finite constant expression");
  const double rounded = std::round(value);
  if (std::fabs(value - rounded) < 1e-12 &&
      std::fabs(rounded) <= static_cast<double>(std::numeric_limits<long long>::max())) {
    return std::to_string(static_cast<long long>(rounded));
  }
  std::ostringstream out;
  out << std::setprecision(15) << value;
  std::string text = out.str();
  if (text.find('.') != std::string::npos) {
    while (!text.empty() && text.back() == '0')
      text.pop_back();
    if (!text.empty() && text.back() == '.')
      text.pop_back();
  }
  return text.empty() ? "0" : text;
}

std::optional<double> numeric_value_of_expression(
    const Expression& expression, const std::map<std::string, Expression>& constants) {
  if (expression.kind == "number") {
    const std::string raw = expression.raw.empty() ? expression.text : expression.raw;
    try {
      return std::stod(raw);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }
  if (expression.kind == "identifier") {
    const auto it = constants.find(expression.name);
    if (it == constants.end())
      return std::nullopt;
    return numeric_value_of_expression(it->second, constants);
  }
  if (expression.kind == "unary" && expression.op == "-" && expression.expr != nullptr) {
    const std::optional<double> value = numeric_value_of_expression(*expression.expr, constants);
    return value.has_value() ? std::optional<double>{-*value} : std::nullopt;
  }
  if (expression.kind == "binary" && expression.left != nullptr && expression.right != nullptr) {
    const std::optional<double> left = numeric_value_of_expression(*expression.left, constants);
    const std::optional<double> right = numeric_value_of_expression(*expression.right, constants);
    if (!left.has_value() || !right.has_value())
      return std::nullopt;
    if (expression.op == "+")
      return *left + *right;
    if (expression.op == "-")
      return *left - *right;
    if (expression.op == "*")
      return *left * *right;
    if (expression.op == "/") {
      if (*right == 0.0)
        return std::nullopt;
      return *left / *right;
    }
  }
  if (expression.kind == "call") {
    std::vector<double> args;
    args.reserve(expression.args.size());
    for (const Expression& arg : expression.args) {
      const std::optional<double> value = numeric_value_of_expression(arg, constants);
      if (!value.has_value())
        return std::nullopt;
      args.push_back(*value);
    }
    if (expression.callee == "sign" && args.size() == 1)
      return (args.at(0) > 0.0) - (args.at(0) < 0.0);
    if (expression.callee == "int" && args.size() == 1)
      return args.at(0) < 0.0 ? std::ceil(args.at(0)) : std::floor(args.at(0));
    if (expression.callee == "frac" && args.size() == 1) {
      const double integer = args.at(0) < 0.0 ? std::ceil(args.at(0)) : std::floor(args.at(0));
      return args.at(0) - integer;
    }
    if (expression.callee == "sqr" && args.size() == 1)
      return args.at(0) * args.at(0);
    if (expression.callee == "inv" && args.size() == 1 && args.at(0) != 0.0)
      return 1.0 / args.at(0);
    if (expression.callee == "pow10" && args.size() == 1)
      return std::pow(10.0, args.at(0));
    if (expression.callee == "pow" && args.size() == 2)
      return std::pow(args.at(0), args.at(1));
    if (expression.callee == "abs" && args.size() == 1)
      return std::fabs(args.at(0));
    if (expression.callee == "max" && args.size() == 2) {
      if (args.at(0) == 0.0 || args.at(1) == 0.0)
        return 0.0;
      return std::max(args.at(0), args.at(1));
    }
    if (expression.callee == "min" && args.size() == 2) {
      if (args.at(0) == 0.0 || args.at(1) == 0.0)
        return 0.0;
      return std::min(args.at(0), args.at(1));
    }
    if (expression.callee == "safe_max" && args.size() == 2)
      return std::max(args.at(0), args.at(1));
    if (expression.callee == "safe_min" && args.size() == 2)
      return std::min(args.at(0), args.at(1));
  }
  return std::nullopt;
}

void index_v2_constants(const V2Program& program, std::map<std::string, Expression>& constants,
                        std::vector<Diagnostic>& diagnostics) {
  std::set<std::string> state_names;
  for (const V2StateField& field : program.state)
    state_names.insert(field.name);

  for (const V2Const& constant : program.consts) {
    if (state_names.contains(constant.name)) {
      diagnostics.push_back(
          diagnostic(DiagnosticSeverity::Error, "parse-error",
                     "Const '" + constant.name + "' shadows state field '" + constant.name + "'"));
      continue;
    }
    if (constants.contains(constant.name)) {
      diagnostics.push_back(diagnostic(DiagnosticSeverity::Error, "parse-error",
                                       "Duplicate const '" + constant.name + "'"));
      continue;
    }

    Expression expression;
    try {
      expression = parse_expression(constant.expr, constant.line);
    } catch (const ParseError& error) {
      diagnostics.push_back(diagnostic(DiagnosticSeverity::Error, "parse-error", error.what()));
      continue;
    }

    const std::optional<double> value = numeric_value_of_expression(expression, constants);
    if (!value.has_value()) {
      diagnostics.push_back(diagnostic(
          DiagnosticSeverity::Error, "parse-error",
          "Const value must be a compile-time number expression, got '" + constant.expr + "'"));
      continue;
    }
    constants[constant.name] = number_expression(format_number_literal(*value));
  }

  assert_no_const_assignment(program, constants, diagnostics);
}

}  // namespace mkpro::core
