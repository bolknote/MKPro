#pragma once

#include "mkpro/core/ast.hpp"

#include <set>
#include <string>

namespace mkpro::core {

std::set<std::string> inline_statement_rule_names(const V2Program& program);
int estimate_expression_cost(const Expression& expression);

}  // namespace mkpro::core
