#include "mkpro/core/passes/cfg.hpp"

#include "mkpro/core/passes/helpers.hpp"

#include <algorithm>
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

std::vector<std::vector<CfgEdge>> build_cfg_edges(const std::vector<IrOp>& ops,
                                                  BuildCfgOptions options) {
  const CfgTargetIndexes indexes = build_target_indexes(ops);
  std::vector<std::vector<CfgEdge>> successors(ops.size());

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
    auto fallthrough = [&]() {
      if (next < static_cast<int>(ops.size()))
        successors.at(index).push_back(CfgEdge{.target = next, .kind = CfgEdgeKind::Fallthrough});
    };
    auto jump_to = [&](const IrTarget& target) {
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
        successors.at(index).push_back(CfgEdge{.target = *target_index, .kind = CfgEdgeKind::Jump});
      }
    };
    auto jump_to_address = [&](int target) {
      const auto found = indexes.address_index.find(target);
      if (found != indexes.address_index.end()) {
        successors.at(index).push_back(CfgEdge{.target = found->second, .kind = CfgEdgeKind::Jump});
      }
    };
    auto unknown_indirect_flow = [&]() {
      if (!options.unknown_indirect_flow_to_all)
        return;
      for (const int target : executable_indexes) {
        successors.at(index).push_back(CfgEdge{.target = target, .kind = CfgEdgeKind::Jump});
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
      jump_to(op.target);
      break;
    case IrKind::CondJump:
    case IrKind::Loop:
      jump_to(op.target);
      fallthrough();
      break;
    case IrKind::Call:
      jump_to(op.target);
      break;
    case IrKind::IndirectJump:
      if (const std::optional<int> target = known_indirect_flow_target(op)) {
        jump_to_address(*target);
      } else if (const std::vector<std::string> labels = computed_dispatch_target_labels(op);
                 !labels.empty()) {
        for (const std::string& label : labels)
          jump_to(IrTarget{label});
      } else {
        unknown_indirect_flow();
      }
      break;
    case IrKind::IndirectCall:
      if (const std::optional<int> target = known_indirect_flow_target(op)) {
        jump_to_address(*target);
      } else if (const std::vector<std::string> labels = computed_dispatch_target_labels(op);
                 !labels.empty()) {
        for (const std::string& label : labels)
          jump_to(IrTarget{label});
      } else {
        unknown_indirect_flow();
      }
      if (options.indirect_call_fallthrough)
        fallthrough();
      break;
    case IrKind::IndirectCondJump:
      if (const std::optional<int> target = known_indirect_flow_target(op)) {
        jump_to_address(*target);
      } else if (const std::vector<std::string> labels = computed_dispatch_target_labels(op);
                 !labels.empty()) {
        for (const std::string& label : labels)
          jump_to(IrTarget{label});
      } else {
        unknown_indirect_flow();
      }
      fallthrough();
      break;
    case IrKind::Return:
      if (return_acts_as_address_one_jump(op)) {
        jump_to_address(1);
      } else {
        for (const int target : call_returns) {
          successors.at(index).push_back(CfgEdge{.target = target, .kind = CfgEdgeKind::Normal});
        }
      }
      break;
    }
  }

  return successors;
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
