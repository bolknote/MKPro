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

enum class CfgUncertaintyKind {
  UnknownIndirectTarget,
  UnresolvedDirectTarget,
  UnresolvedIndirectTarget,
};

struct CfgUncertainty {
  int source = -1;
  CfgUncertaintyKind kind = CfgUncertaintyKind::UnknownIndirectTarget;
};

// A conservative graph may contain more successors than are reachable at
// runtime. `uncertainties` records every place where exact target information
// was unavailable, so proof consumers can distinguish an exact graph from a
// safe over-approximation.
struct ControlFlowGraph {
  std::vector<std::vector<CfgEdge>> edges;
  std::vector<CfgUncertainty> uncertainties;

  bool targets_are_exact() const {
    return uncertainties.empty();
  }
};

struct NumericFlowTargetLayoutGuard {
  int latest_target_index = -1;

  bool can_delete_at(int index) const {
    return index >= latest_target_index;
  }
};

struct BuildCfgOptions {
  bool indirect_call_fallthrough = false;
  bool unknown_indirect_flow_to_all = true;
  bool unresolved_direct_flow_to_all = true;
};

CfgTargetIndexes build_target_indexes(const std::vector<IrOp>& ops);
std::optional<NumericFlowTargetLayoutGuard>
numeric_flow_target_layout_guard(const std::vector<IrOp>& ops);
ControlFlowGraph build_control_flow_graph(const std::vector<IrOp>& ops,
                                          BuildCfgOptions options = {});
std::vector<std::vector<CfgEdge>> build_cfg_edges(const std::vector<IrOp>& ops,
                                                  BuildCfgOptions options = {});
std::vector<std::vector<int>> build_cfg_successors(const std::vector<IrOp>& ops,
                                                   BuildCfgOptions options = {});
std::string loop_counter_register(const std::string& counter);

} // namespace mkpro::core::passes
