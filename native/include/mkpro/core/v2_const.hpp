#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/result.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

Expression number_expression(std::string raw);
std::string format_number_literal(double value);
std::optional<double> numeric_value_of_expression(
    const Expression& expression, const std::map<std::string, Expression>& constants);
void index_v2_constants(const V2Program& program, std::map<std::string, Expression>& constants,
                        std::vector<Diagnostic>& diagnostics);

}  // namespace mkpro::core
