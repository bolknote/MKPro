#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/parser.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>

namespace mkpro::core {

// Most scalar builtins replace one operand. An explicitly seeded random draw
// stages its seed in Y as well as X and therefore destroys pending operands.
// Propagate that ABI effect through expressions and user-function calls before
// choosing stack-only evaluation. This is not a purity or reorderability test.
class BuiltinStackEffects {
 public:
  explicit BuiltinStackEffects(const std::map<std::string, const V2Rule*>* rules = nullptr,
                               bool conservative_user_calls = false)
      : rules_(rules), conservative_user_calls_(conservative_user_calls) {}

  bool clobbers_pending_operands(const Expression& expression) {
    if (expression.kind == "call") {
      std::string callee = expression.callee;
      std::transform(callee.begin(), callee.end(), callee.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      if ((callee == "random" && expression.args.size() == 3U) ||
          rule_clobbers(expression.callee))
        return true;
    }
    for (const ExpressionPtr* child :
         {&expression.index, &expression.expr, &expression.left, &expression.right}) {
      if (*child != nullptr && clobbers_pending_operands(**child))
        return true;
    }
    return std::any_of(expression.args.begin(), expression.args.end(),
                       [&](const Expression& arg) { return clobbers_pending_operands(arg); });
  }

 private:
  bool expression_text_clobbers(const std::string& text, int line) {
    return !text.empty() && clobbers_pending_operands(parse_expression(text, line));
  }

  bool rule_clobbers(const std::string& name) {
    if (rules_ == nullptr || !rules_->contains(name))
      return false;
    // Calls inside guarded expression regions cannot be lifted to statement
    // temporaries. Without a stack-preservation proof, isolate their operands.
    if (conservative_user_calls_)
      return true;
    if (!visited_.insert(name).second)
      return false;
    return statements_clobber(rules_->at(name)->body);
  }

  bool statements_clobber(const std::vector<V2Statement>& statements) {
    return std::any_of(statements.begin(), statements.end(),
                       [&](const V2Statement& statement) { return statement_clobbers(statement); });
  }

  bool statement_clobbers(const V2Statement& statement) {
    if (statement.expr.has_value() && expression_text_clobbers(*statement.expr, statement.line))
      return true;
    if (statement.name.has_value() && rule_clobbers(*statement.name))
      return true;
    for (const std::string& arg : statement.args) {
      if (expression_text_clobbers(arg, statement.line))
        return true;
    }
    if (statement.predicate.has_value()) {
      const V2Predicate& predicate = *statement.predicate;
      for (const std::string* text :
           {&predicate.left, &predicate.right, &predicate.collection, &predicate.item}) {
        if (expression_text_clobbers(*text, statement.line))
          return true;
      }
    }
    if (statement.items.has_value()) {
      for (const DisplayItem& item : *statement.items) {
        if (item.expr.has_value() && clobbers_pending_operands(*item.expr))
          return true;
      }
    }
    if (statements_clobber(statement.body) || statements_clobber(statement.then_body) ||
        statements_clobber(statement.else_body))
      return true;
    for (const V2MatchCase& branch : statement.cases) {
      if (branch.action != nullptr && statement_clobbers(*branch.action))
        return true;
    }
    return statement.otherwise != nullptr && statement_clobbers(*statement.otherwise);
  }

  const std::map<std::string, const V2Rule*>* rules_;
  bool conservative_user_calls_;
  std::set<std::string> visited_;
};

} // namespace mkpro::core
