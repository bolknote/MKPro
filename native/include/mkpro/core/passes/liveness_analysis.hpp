#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

struct LivenessInfo {
  std::vector<RegisterValueSet> live_in;
  std::vector<RegisterValueSet> live_out;
};

LivenessInfo compute_liveness(const std::vector<IrOp>& ops);

} // namespace mkpro::core::passes
