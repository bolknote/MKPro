#pragma once

#include "mkpro/core/ast.hpp"

#include <map>
#include <string>

namespace mkpro::core {

enum class ConstantFoldMode {
  Full,
  DecimalPowersOnly,
};

struct ConstantFoldResult {
  int applied = 0;
  int grd_angle_assumptions = 0;
};

ConstantFoldResult fold_program_constants(V2Program& program,
                                          const std::map<std::string, Expression>& constants,
                                          ConstantFoldMode mode = ConstantFoldMode::Full);

}  // namespace mkpro::core
