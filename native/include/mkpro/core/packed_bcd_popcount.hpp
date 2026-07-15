#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/result.hpp"

#include <vector>

namespace mkpro::core {

int fold_proved_packed_bcd_popcount_loops(
    V2Program& program, std::vector<OptimizationReport>& optimizations);

} // namespace mkpro::core
