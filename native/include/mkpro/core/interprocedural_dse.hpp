#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/result.hpp"

#include <vector>

namespace mkpro::core {

int eliminate_interprocedural_dead_stores(V2Program& program,
                                          std::vector<OptimizationReport>& optimizations);

} // namespace mkpro::core
