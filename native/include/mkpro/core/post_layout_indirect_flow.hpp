#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/result.hpp"

#include <vector>

namespace mkpro::core {

struct PostLayoutIndirectFlowResult {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  std::vector<passes::AppliedOptimization> optimizations;
  int applied = 0;
};

PostLayoutIndirectFlowResult
optimize_post_layout_indirect_flow(const std::vector<MachineItem>& items,
                                   const CompileOptions& options, int rescue_above = 105);
PostLayoutIndirectFlowResult optimize_post_layout_fractional_r0_flow(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& existing_flow_preloads = {});
PostLayoutIndirectFlowResult
optimize_post_layout_address_code_overlay(const std::vector<MachineItem>& items,
                                         const std::vector<PreloadReport>& preloads = {},
                                         const CompileOptions& options = {});
PostLayoutIndirectFlowResult
optimize_post_layout_stop_tail_reuse(const std::vector<MachineItem>& items,
                                     const std::vector<PreloadReport>& preloads,
                                     const CompileOptions& options = {});

int machine_cell_count(const std::vector<MachineItem>& items);

} // namespace mkpro::core
