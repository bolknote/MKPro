#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/result.hpp"

#include <vector>

namespace mkpro::core {

int propagate_values_interprocedurally(V2Program& program,
                                       std::vector<OptimizationReport>& optimizations);

} // namespace mkpro::core
