#include "mkpro/core/emit/lowering/expr.hpp"

#include "test_support.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

Expression number_expr(std::string raw) {
  Expression expression;
  expression.kind = "number";
  expression.raw = std::move(raw);
  return expression;
}

Expression identifier_expr(std::string name) {
  Expression expression;
  expression.kind = "identifier";
  expression.name = std::move(name);
  return expression;
}

Expression indexed_expr(std::string base, std::optional<std::string> field, Expression index) {
  Expression expression;
  expression.kind = "indexed";
  expression.base = std::move(base);
  expression.field = std::move(field);
  expression.index = std::make_unique<Expression>(std::move(index));
  return expression;
}

Expression unary_minus_expr(Expression inner) {
  Expression expression;
  expression.kind = "unary";
  expression.op = "-";
  expression.expr = std::make_unique<Expression>(std::move(inner));
  return expression;
}

Expression binary_expr(Expression left, std::string op, Expression right) {
  Expression expression;
  expression.kind = "binary";
  expression.op = std::move(op);
  expression.left = std::make_unique<Expression>(std::move(left));
  expression.right = std::make_unique<Expression>(std::move(right));
  return expression;
}

Expression call_expr(std::string callee, std::vector<Expression> args = {}) {
  Expression expression;
  expression.kind = "call";
  expression.callee = std::move(callee);
  expression.args = std::move(args);
  return expression;
}

bool contains_identifier(const Expression& expression, const std::string& name) {
  if (expression.kind == "identifier")
    return expression.name == name;
  if (expression.expr != nullptr && contains_identifier(*expression.expr, name))
    return true;
  if (expression.left != nullptr && contains_identifier(*expression.left, name))
    return true;
  if (expression.right != nullptr && contains_identifier(*expression.right, name))
    return true;
  for (const Expression& arg : expression.args) {
    if (contains_identifier(arg, name))
      return true;
  }
  return false;
}

struct ExpressionHarness {
  LoweringContext context;
  std::function<bool(const Expression&)> lower;
  core::emit::ExpressionEmitApi api;

  ExpressionHarness()
      : api{.emitter = context.emitter,
            .emit_recall =
                [&](const std::string& name) {
                  context.emitter.emit_op(0x60, "П->X " + name, "recall " + name);
                  context.emitter.current_x_variable = name;
                  context.emitter.current_x_expression.reset();
                  context.emitter.current_x_aliases.clear();
                  context.emitter.current_x_aliases.insert(name);
                },
            .lower_expression_to_x =
                [&](const Expression& expression) { return lower(expression); },
            .emit_store =
                [&](const std::string& name, std::string comment) {
                  context.emitter.emit_op(0x40, "X->П " + name, std::move(comment));
                  context.emitter.current_x_variable = name;
                  context.emitter.current_x_expression.reset();
                  context.emitter.current_x_aliases.clear();
                  context.emitter.current_x_aliases.insert(name);
                },
            .lower_call_to_x = [&](const Expression&) { return false; },
            .expression_contains_identifier =
                [&](const Expression& expression, const std::string& name) {
                  return contains_identifier(expression, name);
                },
            .x_holds_expression =
                [&](const Expression& expression) {
                  return context.emitter.current_x_expression != nullptr &&
                         expression_to_json(*context.emitter.current_x_expression) ==
                             expression_to_json(expression);
                },
            .ensure_hidden_register = [&](const std::string&) { return true; },
            .emit_number_or_preload =
                [&](const std::string& value, std::optional<std::string> comment,
                    std::optional<int> source_line) {
                  context.emitter.emit_number(value);
                  if (comment.has_value() && !context.emitter.items.empty())
                    context.emitter.items.back().comment = std::move(*comment);
                  (void)source_line;
                }} {
    lower = [&](const Expression& expression) {
      const std::optional<bool> result =
          core::emit::lower_basic_expression_to_x(api, context, expression);
      if (result.has_value())
        return *result;
      if (expression.kind == "binary")
        return core::emit::lower_binary_expression_to_x(api, context, expression);
      if (expression.kind == "call") {
        const std::optional<bool> call_result =
            core::emit::lower_calculator_builtin_call_to_x(api, context, expression);
        return call_result.value_or(false);
      }
      return false;
    };
  }
};

} // namespace

void expression_lowering_helpers_match_typescript_contract() {
  {
    ExpressionHarness harness;
    require(harness.lower(number_expr("12")), "number expression should lower");
    require(harness.context.emitter.items.size() == 2,
            "number expression should emit digit entry steps");
    require(harness.context.emitter.items.at(0).opcode == 0x01,
            "number expression should emit first digit");
    require(harness.context.emitter.items.at(1).opcode == 0x02,
            "number expression should emit second digit");
  }

  {
    ExpressionHarness harness;
    harness.context.preloaded_numbers["10"] = "__const_10";
    require(harness.lower(number_expr("10")), "preloaded number should lower");
    require(harness.context.emitter.items.size() == 1, "preloaded number should use one recall");
    require(harness.context.emitter.items.back().comment == "preload const 10",
            "preloaded number should preserve preload comment");
  }

  {
    ExpressionHarness harness;
    harness.context.emitter.current_x_variable = "score";
    require(harness.lower(identifier_expr("score")), "current-X identifier should lower");
    require(harness.context.emitter.items.empty(), "current-X identifier should emit nothing");
  }

  {
    ExpressionHarness harness;
    Expression indexed = indexed_expr("bank", std::string("cell"), identifier_expr("i"));
    harness.context.emitter.current_x_expression = std::make_shared<Expression>(indexed);
    require(harness.lower(indexed), "current-X indexed expression should lower");
    require(harness.context.emitter.items.empty(),
            "current-X indexed expression should emit nothing");
    require(!harness.context.optimizations.empty() &&
                harness.context.optimizations.back().name == "current-x-indexed-reuse",
            "current-X indexed expression should report the TS optimization name");
  }

  {
    ExpressionHarness harness;
    require(harness.lower(identifier_expr("score")), "identifier should lower through recall");
    require(harness.context.emitter.items.size() == 1, "identifier recall should emit one op");
    require(harness.context.emitter.items.back().comment == "recall score",
            "identifier recall should preserve recall comment");
  }

  {
    ExpressionHarness harness;
    harness.context.constants["answer"] = number_expr("42");
    require(harness.lower(identifier_expr("answer")), "constant identifier should lower");
    require(harness.context.emitter.items.size() == 2,
            "constant number should emit digit entry steps");
    require(harness.context.emitter.items.back().comment == "const answer",
            "constant identifier should annotate lowered constant");
  }

  {
    ExpressionHarness harness;
    require(harness.lower(unary_minus_expr(identifier_expr("delta"))),
            "unary minus expression should lower");
    require(harness.context.emitter.items.size() == 2,
            "unary minus should recall operand and emit sign change");
    require(harness.context.emitter.items.back().opcode == 0x0b,
            "unary minus should emit sign-change opcode");
    require(harness.context.emitter.items.back().comment == "expr unary -",
            "unary minus should preserve comment");
  }

  {
    ExpressionHarness harness;
    require(harness.lower(binary_expr(number_expr("2"), "+", number_expr("3"))),
            "constant binary expression should lower");
    require(harness.context.emitter.items.size() == 1,
            "constant binary expression should fold to one digit");
    require(harness.context.emitter.items.back().opcode == 0x05,
            "constant binary expression should fold arithmetic value");
  }

  {
    ExpressionHarness harness;
    require(harness.lower(binary_expr(identifier_expr("score"), "+", identifier_expr("bonus"))),
            "identifier binary expression should lower");
    require(harness.context.emitter.items.size() == 3,
            "identifier addition should recall both operands and add");
    require(harness.context.emitter.items.at(0).comment == "recall score",
            "identifier addition should recall left operand");
    require(harness.context.emitter.items.at(1).comment == "recall bonus",
            "identifier addition should recall right operand");
    require(harness.context.emitter.items.at(2).opcode == 0x10,
            "identifier addition should emit plus opcode");
    require(!harness.context.emitter.current_x_variable.has_value(),
            "binary expression should clear current-X variable fact");
  }

  {
    ExpressionHarness harness;
    require(harness.lower(call_expr("read")), "read() should lower");
    require(harness.context.emitter.items.size() == 1, "read() should emit one stop");
    require(harness.context.emitter.items.back().opcode == 0x50, "read() should emit stop opcode");
    require(harness.context.emitter.items.back().comment == "read()",
            "read() should preserve comment");
  }

  {
    ExpressionHarness harness;
    std::vector<Expression> args;
    args.push_back(number_expr("9"));
    require(harness.lower(call_expr("sqrt", std::move(args))), "sqrt() should lower");
    require(harness.context.emitter.items.back().opcode == 0x21,
            "sqrt() should emit calculator sqrt opcode");
    require(harness.context.emitter.items.back().comment == "sqrt()",
            "sqrt() should preserve comment");
  }

  {
    ExpressionHarness harness;
    std::vector<Expression> args;
    args.push_back(identifier_expr("mask"));
    args.push_back(identifier_expr("cell"));
    require(harness.lower(call_expr("bit_and", std::move(args))), "bit_and() should lower");
    require(harness.context.emitter.items.size() == 3,
            "bit_and() should lower two args and an opcode");
    require(harness.context.emitter.items.back().opcode == 0x37,
            "bit_and() should emit K AND opcode");
    require(harness.context.emitter.items.back().comment == "bit_and()",
            "bit_and() should preserve comment");
  }

  {
    ExpressionHarness harness;
    std::vector<Expression> args;
    args.push_back(identifier_expr("value"));
    args.push_back(number_expr("2"));
    require(harness.lower(call_expr("digit_at", std::move(args))), "digit_at() should lower");
    require(harness.context.emitter.items.back().opcode == 0x34,
            "digit_at() should end with integer-part opcode");
    require(harness.context.emitter.items.back().comment == "digit_at()",
            "digit_at() should preserve comment");
  }

  {
    ExpressionHarness harness;
    std::vector<Expression> args;
    args.push_back(identifier_expr("left"));
    args.push_back(identifier_expr("right"));
    require(harness.lower(call_expr("min", std::move(args))), "min() should lower");
    require(harness.context.emitter.items.size() == 8,
            "min() should lower through negated max sequence");
    require(harness.context.emitter.items.at(1).comment == "min left negate",
            "min() should negate left argument");
    require(harness.context.emitter.items.at(2).comment == "min left",
            "min() should store left scratch");
    require(harness.context.emitter.items.at(4).comment == "min right negate",
            "min() should negate right argument");
    require(harness.context.emitter.items.at(6).comment == "min negated max",
            "min() should combine through max before final sign");
    require(harness.context.emitter.items.back().comment == "min()",
            "min() should preserve final comment");
  }

  {
    ExpressionHarness harness;
    std::vector<Expression> args;
    args.push_back(identifier_expr("value"));
    args.push_back(number_expr("2"));
    args.push_back(number_expr("5"));
    require(harness.lower(call_expr("eq_any", std::move(args))), "eq_any() should lower");
    require(harness.context.emitter.items.back().opcode == 0x12,
            "eq_any() should lower to a product of differences");
    require(harness.context.emitter.items.back().comment == "expr *",
            "eq_any() should preserve product comment");
    require(!harness.context.optimizations.empty() &&
                harness.context.optimizations.back().name == "small-set-primitive-lowering",
            "eq_any() should report the TS strategy name");
  }

  {
    ExpressionHarness harness;
    std::vector<Expression> args;
    args.push_back(identifier_expr("value"));
    args.push_back(number_expr("3"));
    args.push_back(number_expr("5"));
    require(harness.lower(call_expr("near_any", std::move(args))), "near_any() should lower");
    bool saw_abs = false;
    bool saw_max = false;
    for (const MachineItem& item : harness.context.emitter.items) {
      if (item.comment == "abs()")
        saw_abs = true;
      if (item.comment == "max()")
        saw_max = true;
    }
    require(saw_abs, "near_any() should lower distance through abs()");
    require(!saw_max, "single-candidate near_any() should not need max()");
    require(!harness.context.optimizations.empty() &&
                harness.context.optimizations.back().name == "small-set-primitive-lowering",
            "near_any() should report the TS strategy name");
  }
}

} // namespace mkpro::tests
