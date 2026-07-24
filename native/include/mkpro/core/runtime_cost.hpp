#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/result.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

inline constexpr int kRuntimeCostLoopTraversalBound = 8;

const std::array<int, 256>& mk61_opcode_runtime_cost_centiseconds();

RuntimeCostReport estimate_runtime_cost(
    const std::vector<MachineItem>& items,
    AddressSpaceModel address_space_model = AddressSpaceModel::Standard);

bool runtime_cost_candidate_is_better(std::size_t candidate_cells,
                                      const RuntimeCostReport& candidate,
                                      std::size_t incumbent_cells,
                                      const RuntimeCostReport& incumbent);

std::optional<std::string> runtime_cost_tie_break_reason(
    std::size_t candidate_cells, const RuntimeCostReport& candidate,
    std::size_t incumbent_cells, const RuntimeCostReport& incumbent);

} // namespace mkpro::core
