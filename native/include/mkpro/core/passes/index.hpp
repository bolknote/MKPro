#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"

#include <map>
#include <vector>

namespace mkpro::core::passes {

struct RunPassesResult {
  std::vector<MachineItem> items;
  std::vector<int> removed_cell_addresses;
  int applied = 0;
  std::vector<AppliedOptimization> optimizations;
  std::map<std::string, int> pass_counts;
  std::vector<PreloadReport> preloads;
};

struct RunLayoutPassesResult {
  std::vector<LayoutIrCell> cells;
  int applied = 0;
  std::vector<AppliedOptimization> optimizations;
  std::map<std::string, int> pass_counts;
  std::vector<PreloadReport> preloads;
};

RunPassesResult run_ir_passes(const std::vector<MachineItem>& items,
                              const CompileOptions& options);
RunPassesResult run_finalization_dead_store_elimination(
    const std::vector<MachineItem>& items, const CompileOptions& options);
RunLayoutPassesResult run_ir_passes_on_layout(const std::vector<LayoutIrCell>& cells,
                                              const CompileOptions& options);

const std::vector<IrPass>& pass_pipeline();

} // namespace mkpro::core::passes
