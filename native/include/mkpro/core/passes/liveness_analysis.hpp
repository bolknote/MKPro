#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

struct LivenessInfo {
  std::vector<RegisterValueSet> live_in;
  std::vector<RegisterValueSet> live_out;
};

struct LivenessOptions {
  bool unknown_indirect_flow_to_all = false;
};

LivenessInfo compute_liveness(const std::vector<IrOp>& ops, LivenessOptions options = {});

} // namespace mkpro::core::passes
