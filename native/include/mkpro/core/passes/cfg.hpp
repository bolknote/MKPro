#pragma once

#include "mkpro/core/passes/helpers.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core::passes {

struct CfgTargetIndexes {
  std::map<std::string, int> label_index;
  std::map<int, int> address_index;
};

enum class CfgEdgeKind {
  Normal,
  Fallthrough,
  Jump,
};

struct CfgEdge {
  int target = 0;
  CfgEdgeKind kind = CfgEdgeKind::Normal;
};

struct NumericFlowTargetLayoutGuard {
  int latest_target_index = -1;

  bool can_delete_at(int index) const {
    return index >= latest_target_index;
  }
};

struct BuildCfgOptions {
  bool indirect_call_fallthrough = false;
  bool unknown_indirect_flow_to_all = false;
};

CfgTargetIndexes build_target_indexes(const std::vector<IrOp>& ops);
std::optional<NumericFlowTargetLayoutGuard>
numeric_flow_target_layout_guard(const std::vector<IrOp>& ops);
std::vector<std::vector<CfgEdge>> build_cfg_edges(const std::vector<IrOp>& ops,
                                                  BuildCfgOptions options = {});
std::vector<std::vector<int>> build_cfg_successors(const std::vector<IrOp>& ops,
                                                   BuildCfgOptions options = {});
std::string loop_counter_register(const std::string& counter);

} // namespace mkpro::core::passes
