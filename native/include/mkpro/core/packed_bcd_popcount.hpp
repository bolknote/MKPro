#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/result.hpp"

#include <vector>

namespace mkpro::core {

inline constexpr const char* kPackedBcdHornerThresholdStatement =
    "v2_internal_packed_bcd_horner_threshold";
inline constexpr const char* kPackedBcdBinaryPrepareStatement =
    "v2_internal_packed_bcd_binary_prepare";
inline constexpr const char* kPackedBcdNonzeroScoreStatement =
    "v2_internal_packed_bcd_nonzero_score";
inline constexpr const char* kPackedBcdOneHotUpdateStatement =
    "v2_internal_packed_bcd_one_hot_update";
inline constexpr const char* kPackedBcdHistoryPrependStatement =
    "v2_internal_packed_bcd_history_prepend";
inline constexpr const char* kPackedBcdLoopResetStatement =
    "v2_internal_packed_bcd_loop_reset";

int fold_proved_packed_bcd_popcount_loops(
    V2Program& program, std::vector<OptimizationReport>& optimizations);

} // namespace mkpro::core
