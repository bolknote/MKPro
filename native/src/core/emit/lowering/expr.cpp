#include "mkpro/core/emit/lowering/expr.hpp"

#include "mkpro/core/emit/lowering_helpers.hpp"
#include "mkpro/core/rules.hpp"
#include "mkpro/core/state_banks.hpp"
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

bool expression_is_deterministic(const Expression& expression) {
  if (expression.kind == "number" || expression.kind == "string" || expression.kind == "identifier")
    return true;
  if (expression.kind == "indexed")
    return expression.index == nullptr || expression_is_deterministic(*expression.index);
  if (expression.kind == "unary")
    return expression.expr != nullptr && expression_is_deterministic(*expression.expr);
  if (expression.kind == "binary")
    return expression.left != nullptr && expression.right != nullptr &&
           expression_is_deterministic(*expression.left) &&
           expression_is_deterministic(*expression.right);
  if (expression.kind == "call") {
    const std::string callee = lower_ascii(expression.callee);
    if (callee == "random" || callee == "entered")
      return false;
    return std::all_of(expression.args.begin(), expression.args.end(),
                       [](const Expression& arg) { return expression_is_deterministic(arg); });
  }
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

bool is_simple_stack_load(const Expression& expression) {
  return expression.kind == "identifier" || expression.kind == "number";
}

bool lower_commutative_with_current_x(ExpressionEmitApi& api, LoweringContext& context,
                                      const Expression& expression, int opcode) {
  if (!api.emitter.current_x_variable.has_value() || expression.left == nullptr ||
      expression.right == nullptr) {
    return false;
  }

  if ((expression.left->kind == "identifier" && current_x_holds_name(api, expression.left->name) &&
       is_simple_stack_load(*expression.right))) {
    if (!api.lower_expression_to_x(*expression.right))
      return false;
    api.emitter.emit_op(opcode, expression.op, "expr " + expression.op);
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    context.optimizations.push_back(OptimizationReport{
        .name = "stack-current-x-scheduling",
        .detail = "Reused current X for commutative " + expression.op + ".",
    });
    return true;
  }

  if ((expression.right->kind == "identifier" &&
       current_x_holds_name(api, expression.right->name) &&
       is_simple_stack_load(*expression.left))) {
    if (!api.lower_expression_to_x(*expression.left))
      return false;
    api.emitter.emit_op(opcode, expression.op, "expr " + expression.op);
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    context.optimizations.push_back(OptimizationReport{
        .name = "stack-current-x-scheduling",
        .detail = "Reused current X for commutative " + expression.op + ".",
    });
    return true;
  }

  return false;
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

int expression_precedence(const Expression& expression);

int binary_precedence(const std::string& op) {
  return op == "*" || op == "/" ? 2 : 1;
}

std::string expression_to_intent_text(const Expression& expression);

std::string wrap_expression_text(const Expression& expression, int parent_precedence) {
  const std::string text = expression_to_intent_text(expression);
  return expression_precedence(expression) < parent_precedence ? "(" + text + ")" : text;
}

int expression_precedence(const Expression& expression) {
  if (expression.kind == "unary")
    return 3;
  if (expression.kind == "binary")
    return binary_precedence(expression.op);
  return 4;
}

std::string expression_to_intent_text(const Expression& expression) {
  if (expression.kind == "number")
    return expression.raw;
  if (expression.kind == "string") {
    std::string text = "\"";
    for (const char ch : expression.text) {
      if (ch == '\\' || ch == '"')
        text.push_back('\\');
      text.push_back(ch);
    }
    text.push_back('"');
    return text;
  }
  if (expression.kind == "identifier")
    return expression.name;
  if (expression.kind == "indexed") {
    const std::string member = expression.field.has_value() ? "." + *expression.field : "";
    return expression.base + "[" +
           (expression.index == nullptr ? "" : expression_to_intent_text(*expression.index)) +
           "]" + member;
  }
  if (expression.kind == "unary")
    return "-" + (expression.expr == nullptr ? "" : wrap_expression_text(*expression.expr, 3));
  if (expression.kind == "binary") {
    const int precedence = binary_precedence(expression.op);
    const int right_precedence =
        precedence + (expression.op == "-" || expression.op == "/" ? 1 : 0);
    return (expression.left == nullptr ? "" : wrap_expression_text(*expression.left, precedence)) +
           " " + expression.op + " " +
           (expression.right == nullptr ? "" : wrap_expression_text(*expression.right,
                                                                     right_precedence));
  }
  if (expression.kind == "call") {
    std::string text = expression.callee + "(";
    for (std::size_t index = 0; index < expression.args.size(); ++index) {
      if (index > 0)
        text += ", ";
      text += expression_to_intent_text(expression.args.at(index));
    }
    text += ")";
    return text;
  }
  return "";
}

bool lower_commutative_call_with_current_x(
    ExpressionEmitApi& api, LoweringContext& context, const Expression& expression,
    const std::pair<int, std::string>& opcode,
    const std::map<std::string, std::pair<int, std::string>>& unary_opcodes) {
  const Expression& left = expression.args.at(0);
  const Expression& right = expression.args.at(1);
  if (is_simple_stack_load(right) && compile_current_x_derivation(api, left, unary_opcodes)) {
    if (!api.lower_expression_to_x(right))
      return false;
    api.emitter.emit_op(opcode.first, opcode.second, lower_ascii(expression.callee) + "()");
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    context.optimizations.push_back(OptimizationReport{
        .name = "stack-current-x-scheduling",
        .detail = "Reused " + expression_to_intent_text(left) +
                  " already derivable from X for " + expression.callee + "().",
    });
    return true;
  }
  if (is_simple_stack_load(left) && compile_current_x_derivation(api, right, unary_opcodes)) {
    if (!api.lower_expression_to_x(left))
      return false;
    api.emitter.emit_op(opcode.first, opcode.second, lower_ascii(expression.callee) + "()");
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    context.optimizations.push_back(OptimizationReport{
        .name = "stack-current-x-scheduling",
        .detail = "Reused " + expression_to_intent_text(right) +
                  " already derivable from X for " + expression.callee + "().",
    });
    return true;
  }
  return false;
}

std::optional<std::string> direct_integer_indexed_source(const Expression& expression) {
  if (expression.kind != "indexed" || expression.index == nullptr)
    return std::nullopt;
  const std::optional<mkpro::core::AffineIndexIdentifierOffset> affine =
      mkpro::core::affine_index_identifier_offset(*expression.index);
  if (!affine.has_value() || !affine->integer_part)
    return std::nullopt;
  return affine->name;
}

bool lower_commutative_call_with_destructive_selector_last(
    ExpressionEmitApi& api, LoweringContext& context, const Expression& expression,
    const std::pair<int, std::string>& opcode) {
  const Expression& left = expression.args.at(0);
  const Expression& right = expression.args.at(1);
  if (!expression_is_deterministic(left) || !expression_is_deterministic(right))
    return false;

  const std::optional<std::string> left_source = direct_integer_indexed_source(left);
  if (!left_source.has_value() || !api.expression_contains_identifier(right, *left_source))
    return false;

  if (!api.lower_expression_to_x(right))
    return false;
  if (!api.lower_expression_to_x(left))
    return false;
  api.emitter.emit_op(opcode.first, opcode.second, lower_ascii(expression.callee) + "()");
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
  context.optimizations.push_back(OptimizationReport{
      .name = "destructive-selector-operand-order",
      .detail = "Scheduled integer-indexed operand after the dependent operand so "
                "integer-part indirect addressing cannot destroy " +
                *left_source + " before the other operand uses it.",
  });
  return true;
}

std::optional<std::size_t> packed_grid_macro_arity_impl(const std::string& name) {
  static const std::map<std::string, std::size_t> arities = {
      {"grid_norm", 1},        {"grid_wrap", 1},    {"bit_mask", 1},   {"bit_has", 2},
      {"bit_set", 2},          {"bit_clear", 2},    {"bit_toggle", 2}, {"diag_left_index", 2},
      {"diag_right_index", 2}, {"cell_mask", 2},    {"cell_has", 3},   {"cell_set", 3},
      {"cell_clear", 3},       {"cell_toggle", 3},  {"cell_used", 3},  {"cell_mark", 3},
      {"digit_at", 2},         {"digit_add", 3},    {"digit_set", 3},  {"packed_add", 3},
      {"packed_digit", 2},     {"packed_score", 2},
  };
  const auto it = arities.find(name);
  return it == arities.end() ? std::nullopt : std::optional<std::size_t>{it->second};
}

Expression digit_place_expression(Expression index) {
  return pow10_expression(subtract_expression(std::move(index), number_expression("1")));
}

Expression packed_digit_expression(Expression value, Expression index) {
  return int_expression(multiply_expression(
      frac_expression(divide_expression(std::move(value), pow10_expression(std::move(index)))),
      number_expression("10")));
}

Expression digit_set_expression(Expression value, Expression index, Expression digit) {
  Expression place = digit_place_expression(index);
  return add_expression(
      subtract_expression(value, multiply_expression(packed_digit_expression(value, index), place)),
      multiply_expression(std::move(digit), std::move(place)));
}

std::optional<Expression> packed_grid_expression_macro_impl(const std::string& name,
                                                            const std::vector<Expression>& args) {
  if (name == "grid_norm" || name == "grid_wrap")
    return grid_norm_expression(args.at(0));
  if (name == "bit_mask")
    return bit_mask_expression(args.at(0));
  if (name == "bit_has")
    return bit_membership_expression(args.at(0), args.at(1));
  if (name == "bit_set")
    return call_expression("bit_or", {args.at(0), bit_mask_expression(args.at(1))});
  if (name == "bit_clear") {
    return call_expression(
        "bit_and", {args.at(0), call_expression("bit_not", {bit_mask_expression(args.at(1))})});
  }
  if (name == "bit_toggle")
    return call_expression("bit_xor", {args.at(0), bit_mask_expression(args.at(1))});
  if (name == "diag_left_index")
    return positive_grid_norm_expression(add_expression(args.at(0), args.at(1)));
  if (name == "diag_right_index") {
    return positive_grid_norm_expression(
        subtract_expression(subtract_expression(args.at(0), args.at(1)), number_expression("4")));
  }
  if (name == "cell_mask")
    return cell_mask_expression(args.at(0), args.at(1));
  if (name == "cell_has" || name == "cell_used") {
    return sign_expression(frac_expression(
        call_expression("bit_and", {args.at(0), cell_mask_expression(args.at(1), args.at(2))})));
  }
  if (name == "cell_set" || name == "cell_mark")
    return call_expression("bit_or", {args.at(0), cell_mask_expression(args.at(1), args.at(2))});
  if (name == "cell_clear") {
    return call_expression(
        "bit_and",
        {args.at(0), call_expression("bit_not", {cell_mask_expression(args.at(1), args.at(2))})});
  }
  if (name == "cell_toggle")
    return call_expression("bit_xor", {args.at(0), cell_mask_expression(args.at(1), args.at(2))});
  if (name == "digit_at")
    return packed_digit_expression(args.at(0), args.at(1));
  if (name == "digit_add") {
    return add_expression(args.at(0),
                          multiply_expression(args.at(2), digit_place_expression(args.at(1))));
  }
  if (name == "packed_add")
    return add_expression(multiply_expression(args.at(2), pow10_expression(args.at(1))),
                          args.at(0));
  if (name == "digit_set")
    return digit_set_expression(args.at(0), args.at(1), args.at(2));
  if (name == "packed_digit")
    return packed_digit_expression(args.at(0), args.at(1));
  if (name == "packed_score") {
    return call_expression(
        "sqr", {subtract_expression(
                   frac_expression(divide_expression(args.at(0), pow10_expression(args.at(1)))),
                   number_expression("0.41200076"))});
  }
  return std::nullopt;
}

} // namespace

std::optional<std::size_t> packed_grid_macro_arity(const std::string& name) {
  return packed_grid_macro_arity_impl(name);
}

std::optional<Expression> packed_grid_expression_macro(const std::string& name,
                                                       const std::vector<Expression>& args) {
  return packed_grid_expression_macro_impl(name, args);
}

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
    if (expression.expr->kind == "number") {
      std::string value =
          trim_ascii(expression.expr->raw.empty() ? expression.expr->text : expression.expr->raw);
      value = value.starts_with("-") ? value.substr(1) : "-" + value;
      api.emit_number_or_preload(value, std::nullopt, std::nullopt);
      return true;
    }
    if (const std::optional<double> folded =
            numeric_value_of_expression(expression, context.constants);
        folded.has_value()) {
      api.emitter.emit_number(format_number_literal(*folded));
      return true;
    }
    if (!api.lower_expression_to_x(*expression.expr))
      return false;
    api.emitter.emit_op(0x0b, "/-/", "unary minus");
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
                                  const Expression& expression, bool allow_constant_fold) {
  if (allow_constant_fold) {
    if (const std::optional<double> folded =
            numeric_value_of_expression(expression, context.constants);
        folded.has_value()) {
      api.emitter.emit_number(format_number_literal(*folded));
      return true;
    }
  }

  if (expression.left == nullptr || expression.right == nullptr)
    return false;

  const std::map<std::string, int> arithmetic_opcodes = {
      {"+", 0x10},
      {"-", 0x11},
      {"*", 0x12},
      {"/", 0x13},
  };

  if (expression.op == "+" && expression.left->kind == "call" && expression.right->kind == "call" &&
      (api.call_needs_binary_temp(*expression.left) ||
       api.call_needs_binary_temp(*expression.right))) {
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

  if ((expression.op == "+" || expression.op == "*") &&
      lower_commutative_with_current_x(api, context, expression,
                                       expression.op == "+" ? 0x10 : 0x12)) {
    return true;
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

  const auto duplicated_opcode_it = arithmetic_opcodes.find(expression.op);
  if (duplicated_opcode_it != arithmetic_opcodes.end() &&
      expression_equals(*expression.left, *expression.right) &&
      is_pure_expression(*expression.left) && estimate_expression_cost(*expression.left) > 1) {
    if (!api.lower_expression_to_x(*expression.left))
      return false;
    api.emitter.emit_op(0x0e, "В↑", "duplicate repeated operand through stack");
    api.emitter.emit_op(duplicated_opcode_it->second, expression.op, "expr " + expression.op);
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    context.optimizations.push_back(OptimizationReport{
        .name = "stack-current-x-scheduling",
        .detail = "Duplicated " + expression_to_intent_text(*expression.left) +
                  " through the stack (В↑) for " + expression.op +
                  " instead of recomputing it.",
    });
    return true;
  }

  if (const std::optional<RemainderByConstantMatch> remainder =
          match_remainder_by_constant(expression)) {
    if (!api.lower_expression_to_x(remainder->value))
      return false;
    if (!api.lower_expression_to_x(remainder->divisor))
      return false;
    api.emitter.emit_op(0x13, "/", "remainder quotient");
    api.emitter.emit_op(0x35, "К {x}", "remainder fractional part");
    if (!api.lower_expression_to_x(remainder->divisor))
      return false;
    api.emitter.emit_op(0x12, "*", "remainder scale");
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    context.optimizations.push_back(OptimizationReport{
        .name = "remainder-fraction-lowering",
        .detail = "Lowered integer remainder without recomputing the dividend.",
    });
    return true;
  }

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
    api.emitter.machine_entry_open = true;
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

  if (callee == "pi") {
    if (!expression.args.empty()) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = expression.callee + "() takes no arguments, got " +
                     std::to_string(expression.args.size()) + ".",
      });
      return false;
    }
    api.emitter.emit_op(0x20, "F pi", expression.callee + "()");
    return true;
  }

  if (callee == "e") {
    if (!expression.args.empty()) {
      context.diagnostics.push_back(Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .code = "native-unsupported",
          .message = expression.callee + "() takes no arguments, got " +
                     std::to_string(expression.args.size()) + ".",
      });
      return false;
    }
    api.emitter.emit_number("1");
    api.emitter.emit_op(0x16, "F e^x", expression.callee + "()");
    return true;
  }

  if (const std::optional<std::size_t> arity = packed_grid_macro_arity_impl(callee);
      arity.has_value() && expression.args.size() != *arity) {
    context.diagnostics.push_back(Diagnostic{
        .severity = DiagnosticSeverity::Error,
        .code = "native-unsupported",
        .message = expression.callee + "() expects " + std::to_string(*arity) + " arguments, got " +
                   std::to_string(expression.args.size()) + ".",
    });
    return false;
  }

  if (const std::optional<Expression> macro =
          packed_grid_expression_macro_impl(callee, expression.args)) {
    if (!api.lower_expression_to_x_no_constant_fold(*macro))
      return false;
    context.optimizations.push_back(OptimizationReport{
        .name = "packed-grid-primitive-lowering",
        .detail =
            "Lowered " + expression.callee + "() to reusable 4x4 grid/packed-line arithmetic.",
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
    api.emitter.emit_op(0x15, "F 10^x", "pow10()");
    api.emitter.emit_op(0x13, "/", "expr /");
    api.emitter.emit_op(0x35, "К {x}", "frac()");
    api.emit_number_or_preload("10", std::nullopt, std::nullopt);
    api.emitter.emit_op(0x12, "*", "expr *");
    api.emitter.emit_op(0x34, "К [x]", "int()");
    api.emitter.current_x_variable.reset();
    api.emitter.current_x_aliases.clear();
    context.optimizations.push_back(OptimizationReport{
        .name = "packed-grid-primitive-lowering",
        .detail =
            "Lowered " + expression.callee + "() to reusable 4x4 grid/packed-line arithmetic.",
    });
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
    if (!best_margin.has_value() || !api.lower_expression_to_x(*best_margin))
      return false;
    context.optimizations.push_back(OptimizationReport{
        .name = "small-set-primitive-lowering",
        .detail = "Lowered " + expression.callee + "() to coordinate-set arithmetic.",
    });
    return true;
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
    if (!api.lower_expression_to_x(product))
      return false;
    context.optimizations.push_back(OptimizationReport{
        .name = "small-set-primitive-lowering",
        .detail = "Lowered " + expression.callee + "() to coordinate-set arithmetic.",
    });
    return true;
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
    if (!api.lower_expression_to_x(min_expression(expression.args.at(0), expression.args.at(1))))
      return false;
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
    if (compile_current_x_derivation(api, expression, unary_opcodes)) {
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
    if (lower_commutative_call_with_destructive_selector_last(api, context, expression,
                                                              binary_it->second))
      return true;
    if (lower_commutative_call_with_current_x(api, context, expression, binary_it->second,
                                             unary_opcodes))
      return true;
    if (callee == "pow") {
      if (!api.lower_expression_to_x(expression.args.at(1)))
        return false;
      if (!api.lower_expression_to_x(expression.args.at(0)))
        return false;
      api.emitter.emit_op(binary_it->second.first, binary_it->second.second, callee + "()");
      api.emitter.current_x_variable.reset();
      api.emitter.current_x_aliases.clear();
      return true;
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
