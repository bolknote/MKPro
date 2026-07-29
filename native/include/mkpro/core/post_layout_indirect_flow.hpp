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
PostLayoutIndirectFlowResult
optimize_post_layout_super_dark_address_overlay(const std::vector<MachineItem>& items,
                                                const CompileOptions& options,
                                                int rescue_above = 105);
PostLayoutIndirectFlowResult optimize_post_layout_fractional_r0_flow(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& existing_flow_preloads = {},
    const CompileOptions& options = {});
PostLayoutIndirectFlowResult
optimize_post_layout_address_code_overlay(const std::vector<MachineItem>& items,
                                         const std::vector<PreloadReport>& preloads = {},
                                         const CompileOptions& options = {});
PostLayoutIndirectFlowResult
optimize_post_layout_error_padding_code_overlay(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads = {},
    const CompileOptions& options = {});
PostLayoutIndirectFlowResult
optimize_post_layout_code_overlays(const std::vector<MachineItem>& items,
                                   const std::vector<PreloadReport>& preloads = {},
                                   const CompileOptions& options = {});
PostLayoutIndirectFlowResult
optimize_post_layout_stop_tail_reuse(const std::vector<MachineItem>& items,
                                     const std::vector<PreloadReport>& preloads,
                                     const CompileOptions& options = {});
// Converts a direct `БП` to physical 01 into a one-cell `В/О` when the branch
// provably runs on an empty return stack in every execution state: the MK-61
// continues at physical 01 after an empty-stack В/О (pinned by
// emulator_vo_empty_continuation_facts). Intended to run before any selector
// value or anchor address is solved, so later layout machinery rebuilds the
// geometry of the shrunk artifact itself. The `empty_stack_loop_return`
// lowering option manufactures this shape by placing a one-cell К НОП entry
// pad in front of the main loop head.
PostLayoutIndirectFlowResult
optimize_post_layout_empty_stack_loop_return(const std::vector<MachineItem>& items,
                                             const CompileOptions& options = {});

// Converts direct `ПП addr` / `БП addr` into one-cell indirect flow through a
// stable register whose exact value at the branch site is proved by the
// flow-sensitive stable-register value analysis. Intended to run after every
// charge value (including late-bound decimal selector charges) has been
// materialized, so runtime-charged registers can serve additional direct
// flows to the address they already deliver.
PostLayoutIndirectFlowResult
optimize_post_layout_charged_selector_flow(const std::vector<MachineItem>& items,
                                           const std::vector<PreloadReport>& preloads,
                                           const CompileOptions& options = {});

int machine_cell_count(const std::vector<MachineItem>& items);

} // namespace mkpro::core
