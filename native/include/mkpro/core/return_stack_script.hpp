#pragma once

#include "mkpro/core/post_layout_indirect_flow.hpp"

#include <optional>
#include <vector>

namespace mkpro::core {

struct ReturnStackReturnStep {
  int stored_return_address = 0;
  int target_address = 0;
  std::vector<int> stack_after_return;
};

std::vector<int> mk61_return_stack_after_call(std::vector<int> stack,
                                              int stored_return_address);
std::optional<ReturnStackReturnStep> mk61_return_stack_after_return(
    const std::vector<int>& stack);
std::vector<ReturnStackReturnStep> simulate_mk61_return_stack(
    std::vector<int> stack, int return_count);

PostLayoutIndirectFlowResult
optimize_post_layout_return_stack_script(const std::vector<MachineItem>& items);

} // namespace mkpro::core
