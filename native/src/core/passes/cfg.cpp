#include "mkpro/core/passes/cfg.hpp"

#include "mkpro/core/passes/helpers.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

std::optional<int> numeric_flow_target(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Loop:
  case IrKind::OrphanAddress:
    if (const auto* target = std::get_if<int>(&op.target))
      return *target;
    return std::nullopt;
  default:
    return std::nullopt;
  }
}

bool return_acts_as_address_one_jump(const IrOp& op) {
  return op.kind == IrKind::Return && op.meta.comment == "optimized БП 01";
}

} // namespace

CfgTargetIndexes build_target_indexes(const std::vector<IrOp>& ops) {
  CfgTargetIndexes indexes;
  int address = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind == IrKind::Label) {
      indexes.label_index[op.name] = static_cast<int>(index);
      continue;
    }
    indexes.address_index[address] = static_cast<int>(index);
    address += cells_per_op(op);
  }
  return indexes;
}

std::optional<NumericFlowTargetLayoutGuard>
numeric_flow_target_layout_guard(const std::vector<IrOp>& ops) {
  const CfgTargetIndexes indexes = build_target_indexes(ops);
  int latest_target_index = -1;
  for (const IrOp& op : ops) {
    const std::optional<int> target = numeric_flow_target(op);
    if (!target.has_value())
      continue;
    const auto target_index = indexes.address_index.find(*target);
    if (target_index == indexes.address_index.end())
      return std::nullopt;
    latest_target_index = std::max(latest_target_index, target_index->second);
  }
  return NumericFlowTargetLayoutGuard{.latest_target_index = latest_target_index};
}

ControlFlowGraph build_control_flow_graph(const std::vector<IrOp>& ops, BuildCfgOptions options) {
  const CfgTargetIndexes indexes = build_target_indexes(ops);
  ControlFlowGraph graph{.edges = std::vector<std::vector<CfgEdge>>(ops.size())};

  std::vector<int> call_returns;
  std::vector<int> executable_indexes;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind != IrKind::Label)
      executable_indexes.push_back(static_cast<int>(index));
    const int next = static_cast<int>(index + 1U);
    if ((op.kind == IrKind::Call || op.kind == IrKind::IndirectCall) &&
        next < static_cast<int>(ops.size())) {
      call_returns.push_back(next);
    }
  }

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    const int next = static_cast<int>(index + 1U);
    auto add_edge = [&](int target, CfgEdgeKind kind) {
      std::vector<CfgEdge>& row = graph.edges.at(index);
      const bool duplicate = std::any_of(row.begin(), row.end(), [&](const CfgEdge& edge) {
        return edge.target == target && edge.kind == kind;
      });
      if (!duplicate)
        row.push_back(CfgEdge{.target = target, .kind = kind});
    };
    auto fallthrough = [&]() {
      if (next < static_cast<int>(ops.size()))
        add_edge(next, CfgEdgeKind::Fallthrough);
    };
    auto jump_to = [&](const IrTarget& target) -> bool {
      std::optional<int> target_index;
      if (const auto* label = std::get_if<std::string>(&target)) {
        const auto found = indexes.label_index.find(*label);
        if (found != indexes.label_index.end())
          target_index = found->second;
      } else if (const auto* address = std::get_if<int>(&target)) {
        const auto found = indexes.address_index.find(*address);
        if (found != indexes.address_index.end())
          target_index = found->second;
      }
      if (target_index.has_value()) {
        add_edge(*target_index, CfgEdgeKind::Jump);
        return true;
      }
      return false;
    };
    auto jump_to_address = [&](int target) -> bool {
      const auto found = indexes.address_index.find(target);
      if (found != indexes.address_index.end()) {
        add_edge(found->second, CfgEdgeKind::Jump);
        return true;
      }
      return false;
    };
    auto flow_to_all = [&](bool enabled) {
      if (!enabled)
        return;
      for (const int target : executable_indexes)
        add_edge(target, CfgEdgeKind::Jump);
    };
    auto record_uncertainty = [&](CfgUncertaintyKind kind, bool expand_to_all) {
      graph.uncertainties.push_back(
          CfgUncertainty{.source = static_cast<int>(index), .kind = kind});
      flow_to_all(expand_to_all);
    };
    auto direct_jump_to = [&](const IrTarget& target) {
      if (!jump_to(target)) {
        record_uncertainty(CfgUncertaintyKind::UnresolvedDirectTarget,
                           options.unresolved_direct_flow_to_all);
      }
    };
    auto indirect_targets = [&]() -> std::optional<std::vector<IrTarget>> {
      if (op.meta.indirect_flow_targets.has_value()) {
        if (op.meta.indirect_flow_targets->empty())
          return std::nullopt;
        return *op.meta.indirect_flow_targets;
      }
      if (const std::optional<int> target = known_indirect_flow_target(op))
        return std::vector<IrTarget>{IrTarget{*target}};
      const std::vector<std::string> labels = computed_dispatch_target_labels(op);
      if (labels.empty())
        return std::nullopt;
      std::vector<IrTarget> targets;
      targets.reserve(labels.size());
      for (const std::string& label : labels)
        targets.push_back(IrTarget{label});
      return targets;
    };
    auto indirect_jump_to_targets = [&]() {
      const std::optional<std::vector<IrTarget>> targets = indirect_targets();
      if (!targets.has_value()) {
        record_uncertainty(CfgUncertaintyKind::UnknownIndirectTarget,
                           options.unknown_indirect_flow_to_all);
        return;
      }
      bool unresolved = false;
      for (const IrTarget& target : *targets)
        unresolved = !jump_to(target) || unresolved;
      if (unresolved) {
        record_uncertainty(CfgUncertaintyKind::UnresolvedIndirectTarget,
                           options.unknown_indirect_flow_to_all);
      }
    };

    switch (op.kind) {
    case IrKind::Label:
    case IrKind::Store:
    case IrKind::Recall:
    case IrKind::IndirectStore:
    case IrKind::IndirectRecall:
    case IrKind::Plain:
    case IrKind::OrphanAddress:
    case IrKind::Stop:
      fallthrough();
      break;
    case IrKind::Jump:
      direct_jump_to(op.target);
      break;
    case IrKind::CondJump:
    case IrKind::Loop:
      direct_jump_to(op.target);
      fallthrough();
      break;
    case IrKind::Call:
      direct_jump_to(op.target);
      break;
    case IrKind::IndirectJump:
      indirect_jump_to_targets();
      break;
    case IrKind::IndirectCall:
      indirect_jump_to_targets();
      if (options.indirect_call_fallthrough)
        fallthrough();
      break;
    case IrKind::IndirectCondJump:
      indirect_jump_to_targets();
      fallthrough();
      break;
    case IrKind::Return:
      if (return_acts_as_address_one_jump(op)) {
        if (!jump_to_address(1)) {
          record_uncertainty(CfgUncertaintyKind::UnresolvedDirectTarget,
                             options.unresolved_direct_flow_to_all);
        }
      } else {
        for (const int target : call_returns)
          add_edge(target, CfgEdgeKind::Normal);
      }
      break;
    }
  }

  return graph;
}

std::vector<std::vector<CfgEdge>> build_cfg_edges(const std::vector<IrOp>& ops,
                                                  BuildCfgOptions options) {
  return build_control_flow_graph(ops, options).edges;
}

std::vector<std::vector<int>> build_cfg_successors(const std::vector<IrOp>& ops,
                                                   BuildCfgOptions options) {
  std::vector<std::vector<int>> successors;
  for (const std::vector<CfgEdge>& edges : build_cfg_edges(ops, options)) {
    std::vector<int> row;
    row.reserve(edges.size());
    for (const CfgEdge& edge : edges)
      row.push_back(edge.target);
    successors.push_back(std::move(row));
  }
  return successors;
}

std::string loop_counter_register(const std::string& counter) {
  if (counter == "L0")
    return "0";
  if (counter == "L1")
    return "1";
  if (counter == "L2")
    return "2";
  if (counter == "L3")
    return "3";
  return "";
}

} // namespace mkpro::core::passes
