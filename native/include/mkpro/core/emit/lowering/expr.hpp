#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/emit/lowering_context.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core::emit {

struct ExpressionEmitApi {
  MachineEmitter& emitter;
  std::function<void(const std::string&)> emit_recall;
  std::function<bool(const Expression&)> lower_expression_to_x;
  std::function<bool(const Expression&)> lower_expression_to_x_no_constant_fold;
  std::function<void(const std::string&, std::string)> emit_store;
  std::function<bool(const Expression&)> lower_call_to_x;
  std::function<bool(const Expression&)> call_needs_binary_temp;
  std::function<bool(const Expression&, const std::string&)> expression_contains_identifier;
  std::function<bool(const Expression&)> x_holds_expression;
  std::function<bool(const std::string&)> ensure_hidden_register;
  std::function<void(const std::string&, std::optional<std::string>, std::optional<int>,
                     std::optional<CellRole>)>
      emit_number_or_preload;
};

std::optional<bool> lower_basic_expression_to_x(ExpressionEmitApi& api, LoweringContext& context,
                                                const Expression& expression);
bool lower_binary_expression_to_x(ExpressionEmitApi& api, LoweringContext& context,
                                  const Expression& expression, bool allow_constant_fold = true);
std::optional<bool> lower_calculator_builtin_call_to_x(ExpressionEmitApi& api,
                                                       LoweringContext& context,
                                                       const Expression& expression);
std::optional<std::size_t> packed_grid_macro_arity(const std::string& name);
std::optional<Expression> packed_grid_expression_macro(const std::string& name,
                                                       const std::vector<Expression>& args);
std::optional<int> grid_norm_call_width(const std::vector<Expression>& args);
std::string grid_norm_use_count_key(int width);
void emit_grid_norm_body(ExpressionEmitApi& api, int width);

} // namespace mkpro::core::emit
