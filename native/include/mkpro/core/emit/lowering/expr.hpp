#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/emit/lowering_context.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"

#include <functional>
#include <optional>
#include <string>

namespace mkpro::core::emit {

struct ExpressionEmitApi {
  MachineEmitter& emitter;
  std::function<void(const std::string&)> emit_recall;
  std::function<bool(const Expression&)> lower_expression_to_x;
  std::function<void(const std::string&, std::string)> emit_store;
  std::function<bool(const Expression&)> lower_call_to_x;
  std::function<bool(const Expression&, const std::string&)> expression_contains_identifier;
  std::function<bool(const Expression&)> x_holds_expression;
  std::function<bool(const std::string&)> ensure_hidden_register;
  std::function<void(const std::string&, std::optional<std::string>, std::optional<int>)>
      emit_number_or_preload;
};

std::optional<bool> lower_basic_expression_to_x(ExpressionEmitApi& api, LoweringContext& context,
                                                const Expression& expression);
bool lower_binary_expression_to_x(ExpressionEmitApi& api, LoweringContext& context,
                                  const Expression& expression, bool allow_constant_fold = true);
std::optional<bool> lower_calculator_builtin_call_to_x(ExpressionEmitApi& api,
                                                       LoweringContext& context,
                                                       const Expression& expression);

} // namespace mkpro::core::emit
