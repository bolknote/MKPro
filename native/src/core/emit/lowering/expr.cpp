#include "mkpro/core/emit/lowering/expr.hpp"

#include "mkpro/core/emit/lowering_helpers.hpp"
#include "mkpro/core/v2_const.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace mkpro::core::emit {

namespace {

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string normalize_number_key(const std::string& value) {
  return lower_ascii(trim_ascii(value));
}

bool is_pure_expression(const Expression& expression) {
  if (expression.kind == "number" || expression.kind == "string" || expression.kind == "identifier")
    return true;
  if (expression.kind == "indexed")
    return expression.index == nullptr || is_pure_expression(*expression.index);
  if (expression.kind == "unary")
    return expression.expr != nullptr && is_pure_expression(*expression.expr);
  if (expression.kind == "binary")
    return expression.left != nullptr && expression.right != nullptr &&
           is_pure_expression(*expression.left) && is_pure_expression(*expression.right);
  return false;
}

bool expression_equals(const Expression& left, const Expression& right) {
  return expression_to_json(left) == expression_to_json(right);
}

bool is_numeric_value(const LoweringContext& context, const Expression& expression,
                      double expected) {
  const std::optional<double> value = numeric_value_of_expression(expression, context.constants);
  return value.has_value() && std::fabs(*value - expected) < 1e-12;
}

bool current_x_holds_name(ExpressionEmitApi& api, const std::string& name) {
  return api.emitter.current_x_variable == name || api.emitter.current_x_aliases.contains(name);
}

bool compile_current_x_derivation(
    ExpressionEmitApi& api, const Expression& expression,
    const std::map<std::string, std::pair<int, std::string>>& unary_opcodes) {
  if (expression.kind == "identifier")
    return current_x_holds_name(api, expression.name);

  if (expression.kind == "unary" && expression.op == "-" && expression.expr != nullptr) {
    if (!compile_current_x_derivation(api, *expression.expr, unary_opcodes))
      return false;
    api.emitter.emit_op(0x0b, "/-/", "current-X unary minus");
    return true;
  }

  if (expression.kind != "call" || expression.args.size() != 1)
    return false;
  const std::string fn = lower_ascii(expression.callee);
  const auto opcode_it = unary_opcodes.find(fn);
  if (opcode_it == unary_opcodes.end())
    return false;
  if (!compile_current_x_derivation(api, expression.args.front(), unary_opcodes))
    return false;
  api.emitter.emit_op(opcode_it->second.first, opcode_it->second.second, "current-X " + fn);
  return true;
}

} // namespace

std::optional<bool> lower_basic_expression_to_x(ExpressionEmitApi& api, LoweringContext& context,
                                                const Expression& expression) {
  if (expression.kind == "identifier") {
    const auto constant_it = context.constants.find(expression.name);
    if (constant_it != context.constants.end()) {
      if (!api.lower_expression_to_x(constant_it->second))
        return false;
      if (!api.emitter.items.empty() && !api.emitter.items.back().comment.has_value())
        api.emitter.items.back().comment = "const " + expression.name;
      return true;
    }
    if (api.emitter.current_x_variable == expression.name)
      return true;
    api.emit_recall(expression.name);
    return true;
  }

  if (expression.kind == "number") {
    const std::string value = expression.raw.empty() ? expression.text : expression.raw;
    const auto preload_it = context.preloaded_numbers.find(normalize_number_key(value));
    if (preload_it != context.preloaded_numbers.end()) {
      api.emit_recall(preload_it->second);
      api.emitter.items.back().comment = "preload const " + normalize_number_key(value);
      return true;
    }
    if (context.lunar_shape && value == "10") {
      api.emit_recall("__const_10");
      api.emitter.items.back().comment = "preload const 10";
      return true;
    }
    if (context.lunar_shape && value == "9.8") {
      api.emit_recall("__const_9_8");
      api.emitter.items.back().comment = "preload const 9.8";
      return true;
    }
    if (context.clock_shape && value == "10") {
      api.emit_recall("__const_10");
      api.emitter.items.back().comment = "preload const 10";
      return true;
    }
    if (context.clock_shape && value == "24") {
      api.emit_recall("__const_24");
      api.emitter.items.back().comment = "preload const 24";
      return true;
    }
    if (context.clock_shape && value == "60") {
      api.emit_recall("__const_60");
      api.emitter.items.back().comment = "preload const 60";
      return true;
    }
    api.emit_number_or_preload(value, std::nullopt, std::nullopt);
    return true;
  }

  if (expression.kind == "unary" && expression.op == "-" && expression.expr != nullptr) {
    if (const std::optional<double> folded =
            numeric_value_of_expression(expression, context.constants);
        folded.has_value()) {
      api.emitter.emit_number(format_number_literal(*folded));
      return true;
    }
    if (!api.lower_expression_to_x(*expression.expr))
      return false;
    api.emitter.emit_op(0x0b, "/-/", "expr unary -");
    return true;
  }

  if (expression.kind == "indexed") {
    if (api.x_holds_expression(expression)) {
      context.optimizations.push_back(OptimizationReport{
          .name = "current-x-indexed-reuse",
          .detail = "Reused indexed expression already in X.",
      });
      return true;
    }
    return std::nullopt;
  }

  return std::nullopt;
}

bool lower_binary_expression_to_x(ExpressionEmitApi& api, LoweringContext& context,
                                  const Expression& expression) {
  if (const std::optional<double> folded =
          numeric_value_of_expression(expression, context.constants);
      folded.has_value()) {
    api.emitter.emit_number(format_number_literal(*folded));
    return true;
  }

  if (expression.left == nullptr || expression.right == nullptr)
    return false;

  if (expression.op == "+" && expression.left->kind == "call" && expression.right->kind == "call") {
    if (!api.lower_call_to_x(*expression.left))
      return false;
    api.emit_store("__mkpro_call_1", "set __mkpro_call_1");
    if (!api.lower_call_to_x(*expression.right))
      return false;
    api.emit_recall("__mkpro_call_1");
    api.emitter.emit_op(0x10, "+", "expr +");
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    return true;
  }

  if (expression.op == "+") {
    if (expression.left->kind == "identifier" && expression.right->kind == "number") {
      if (!api.lower_expression_to_x(*expression.right))
        return false;
      if (!api.lower_expression_to_x(*expression.left))
        return false;
      api.emitter.emit_op(0x10, "+", "expr +");
      api.emitter.current_x_variable.reset();
      api.emitter.current_x_aliases.clear();
      return true;
    }
    const auto current = api.emitter.current_x_variable;
    if (current.has_value() && api.expression_contains_identifier(*expression.right, *current) &&
        expression.left->kind == "identifier") {
      api.emit_recall(expression.left->name);
      api.emitter.emit_op(0x10, "+", "expr +");
      api.emitter.current_x_variable.reset();
      api.emitter.current_x_aliases.clear();
      return true;
    }
    if (current.has_value() && api.expression_contains_identifier(*expression.left, *current) &&
        expression.right->kind == "identifier") {
      api.emit_recall(expression.right->name);
      api.emitter.emit_op(0x10, "+", "expr +");
      api.emitter.current_x_variable.reset();
      api.emitter.current_x_aliases.clear();
      return true;
    }
    if (expression.left->kind == "identifier" && expression.right->kind == "identifier") {
      api.emit_recall(expression.left->name);
      api.emit_recall(expression.right->name);
      api.emitter.emit_op(0x10, "+", "expr +");
      api.emitter.current_x_variable.reset();
      api.emitter.current_x_aliases.clear();
      return true;
    }
  }

  if (expression.op == "/" && is_numeric_value(context, *expression.left, 1.0)) {
    if (!api.lower_expression_to_x(*expression.right))
      return false;
    api.emitter.emit_op(0x23, "F 1/x", "reciprocal division");
    context.optimizations.push_back(OptimizationReport{
        .name = "reciprocal-division-lowering",
        .detail = "Lowered reciprocal division through F 1/x.",
    });
    return true;
  }

  if (expression.op == "*" && expression_equals(*expression.left, *expression.right) &&
      is_pure_expression(*expression.left)) {
    if (!api.lower_expression_to_x(*expression.left))
      return false;
    api.emitter.emit_op(0x22, "F x^2", "square repeated operand");
    context.optimizations.push_back(OptimizationReport{
        .name = "square-expression-lowering",
        .detail = "Lowered repeated multiplication through F x^2.",
    });
    return true;
  }

  const std::map<std::string, int> arithmetic_opcodes = {
      {"+", 0x10},
      {"-", 0x11},
      {"*", 0x12},
      {"/", 0x13},
  };
  const auto opcode_it = arithmetic_opcodes.find(expression.op);
  if (opcode_it != arithmetic_opcodes.end()) {
    if (!api.lower_expression_to_x(*expression.left))
      return false;
    if (!api.lower_expression_to_x(*expression.right))
      return false;
    api.emitter.emit_op(opcode_it->second, expression.op, "expr " + expression.op);
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    return true;
  }
  return false;
}

std::optional<bool> lower_calculator_builtin_call_to_x(ExpressionEmitApi& api,
                                                       LoweringContext& context,
                                                       const Expression& expression) {
  if (expression.kind != "call")
    return std::nullopt;

  const std::string callee = lower_ascii(expression.callee);
  if (callee == "read") {
    if (!expression.args.empty()) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = "read() expects no arguments",
      });
      return false;
    }
    api.emitter.emit_op(0x50, "С/П", "read()");
    return true;
  }

  if (callee == "entered") {
    if (!expression.args.empty()) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = "Function " + expression.callee + " expects no arguments",
      });
      return false;
    }
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    api.emitter.current_x_known_zero = false;
    context.optimizations.push_back(OptimizationReport{
        .name = "entered-current-x",
        .detail = "Consumed the current keyboard-entered X value without emitting another stop.",
    });
    return true;
  }

  if (callee == "digit_at") {
    if (expression.args.size() != 2) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = "Function " + expression.callee + " expects two arguments",
      });
      return false;
    }
    if (!api.lower_expression_to_x(expression.args.at(0)))
      return false;
    if (!api.lower_expression_to_x(expression.args.at(1)))
      return false;
    api.emitter.emit_op(0x15, "F 10^x", "digit_at place");
    api.emitter.emit_op(0x13, "/", "digit_at scaled value");
    api.emitter.emit_op(0x35, "К {x}", "digit_at fractional tail");
    api.emitter.emit_number("10");
    api.emitter.emit_op(0x12, "*", "digit_at digit scale");
    api.emitter.emit_op(0x34, "К [x]", "digit_at()");
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    return true;
  }

  if (callee == "near_any") {
    if (expression.args.size() < 3) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = "near_any() expects at least three arguments, got " +
                     std::to_string(expression.args.size()),
      });
      return false;
    }
    const Expression value = expression.args.at(0);
    const Expression radius = expression.args.at(1);
    std::optional<Expression> best_margin;
    for (std::size_t index = 2; index < expression.args.size(); ++index) {
      Expression distance =
          call_expression("abs", {binary_expression(value, "-", expression.args.at(index))});
      Expression margin = binary_expression(radius, "-", std::move(distance));
      if (best_margin.has_value()) {
        best_margin = call_expression("max", {*best_margin, std::move(margin)});
      } else {
        best_margin = std::move(margin);
      }
    }
    return best_margin.has_value() && api.lower_expression_to_x(*best_margin);
  }

  if (callee == "eq_any") {
    if (expression.args.size() < 2) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = "eq_any() expects at least two arguments, got " +
                     std::to_string(expression.args.size()),
      });
      return false;
    }
    const Expression value = expression.args.at(0);
    Expression product = binary_expression(value, "-", expression.args.at(1));
    for (std::size_t index = 2; index < expression.args.size(); ++index) {
      product = binary_expression(std::move(product), "*",
                                  binary_expression(value, "-", expression.args.at(index)));
    }
    return api.lower_expression_to_x(product);
  }

  if (callee == "min") {
    if (expression.args.size() != 2) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = "Function " + expression.callee + " expects two arguments",
      });
      return false;
    }
    const std::string scratch = "__min_scratch";
    if (!api.ensure_hidden_register(scratch))
      return false;
    if (!api.lower_expression_to_x(expression.args.at(0)))
      return false;
    api.emitter.emit_op(0x0b, "/-/", "min left negate");
    api.emit_store(scratch, "min left");
    if (!api.lower_expression_to_x(expression.args.at(1)))
      return false;
    api.emitter.emit_op(0x0b, "/-/", "min right negate");
    api.emit_recall(scratch);
    api.emitter.emit_op(0x36, "К max", "min negated max");
    api.emitter.emit_op(0x0b, "/-/", "min()");
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    context.optimizations.push_back(OptimizationReport{
        .name = "min-via-max-lowering",
        .detail = "Lowered " + expression.callee + "() through min-via-max().",
    });
    return true;
  }

  if (callee == "safe_max" || callee == "safe_min") {
    if (expression.args.size() != 2) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = "Function " + expression.callee + " expects two arguments",
      });
      return false;
    }
    const Expression& left = expression.args.at(0);
    const Expression& right = expression.args.at(1);
    if (!is_pure_expression(left) || !is_pure_expression(right)) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = expression.callee + "() requires duplicable operands; bind " +
                     (is_pure_expression(left) ? "the second" : "the first") +
                     " argument to a variable first.",
      });
      return false;
    }
    const Expression lowered =
        callee == "safe_max" ? safe_max_expression(left, right) : safe_min_expression(left, right);
    if (!api.lower_expression_to_x(lowered))
      return false;
    context.optimizations.push_back(OptimizationReport{
        .name = "quirk-free-minmax-lowering",
        .detail = "Lowered " + expression.callee +
                  "() through quirk-free arithmetic (avoids the К max zero-is-greatest "
                  "behaviour).",
    });
    return true;
  }

  const std::map<std::string, std::pair<int, std::string>> unary_opcodes = {
      {"abs", {0x31, "К |x|"}},     {"sign", {0x32, "К ЗН"}},     {"int", {0x34, "К [x]"}},
      {"frac", {0x35, "К {x}"}},    {"sqr", {0x22, "F x^2"}},     {"inv", {0x23, "F 1/x"}},
      {"sqrt", {0x21, "F sqrt"}},   {"lg", {0x17, "F lg"}},       {"ln", {0x18, "F ln"}},
      {"sin", {0x1c, "F sin"}},     {"cos", {0x1d, "F cos"}},     {"tg", {0x1e, "F tg"}},
      {"asin", {0x19, "F sin^-1"}}, {"acos", {0x1a, "F cos^-1"}}, {"atg", {0x1b, "F tg^-1"}},
      {"exp", {0x16, "F e^x"}},     {"pow10", {0x15, "F 10^x"}},  {"bit_not", {0x3a, "К ИНВ"}},
  };
  const auto unary_it = unary_opcodes.find(callee);
  if (unary_it != unary_opcodes.end()) {
    if (expression.args.size() != 1) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = "Function " + expression.callee + " expects one argument",
      });
      return false;
    }
    if (compile_current_x_derivation(api, expression.args.at(0), unary_opcodes)) {
      context.optimizations.push_back(OptimizationReport{
          .name = "current-x-unary-derivation",
          .detail = "Reused value already in X for " + expression.callee + "().",
      });
      return true;
    }
    if (!api.lower_expression_to_x(expression.args.at(0)))
      return false;
    api.emitter.emit_op(unary_it->second.first, unary_it->second.second, callee + "()");
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    if (callee == "pow10") {
      context.optimizations.push_back(OptimizationReport{
          .name = "pow10-opcode-lowering",
          .detail = "Lowered " + expression.callee + "() through F 10^x.",
      });
    }
    return true;
  }

  if (callee == "pow") {
    if (expression.args.size() != 2) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = "Function " + expression.callee + " expects two arguments",
      });
      return false;
    }
    if (is_numeric_value(context, expression.args.at(1), 2.0)) {
      if (!api.lower_expression_to_x(expression.args.at(0)))
        return false;
      api.emitter.emit_op(0x22, "F x^2", callee + "()");
      context.optimizations.push_back(OptimizationReport{
          .name = "pow-square-lowering",
          .detail = "Lowered " + expression.callee + "() through F x^2.",
      });
      return true;
    }
    if (is_numeric_value(context, expression.args.at(0), 10.0)) {
      if (!api.lower_expression_to_x(expression.args.at(1)))
        return false;
      api.emitter.emit_op(0x15, "F 10^x", callee + "()");
      context.optimizations.push_back(OptimizationReport{
          .name = "pow10-opcode-lowering",
          .detail = "Lowered " + expression.callee + "() through F 10^x.",
      });
      return true;
    }
  }

  const std::map<std::string, std::pair<int, std::string>> binary_opcodes = {
      {"pow", {0x24, "F x^y"}},  {"max", {0x36, "К max"}},   {"bit_and", {0x37, "К ∧"}},
      {"bit_or", {0x38, "К ∨"}}, {"bit_xor", {0x39, "К ⊕"}},
  };
  const auto binary_it = binary_opcodes.find(callee);
  if (binary_it != binary_opcodes.end()) {
    if (expression.args.size() != 2) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = "Function " + expression.callee + " expects two arguments",
      });
      return false;
    }
    if (!api.lower_expression_to_x(expression.args.at(0)))
      return false;
    if (!api.lower_expression_to_x(expression.args.at(1)))
      return false;
    api.emitter.emit_op(binary_it->second.first, binary_it->second.second, callee + "()");
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    return true;
  }

  return std::nullopt;
}

} // namespace mkpro::core::emit
