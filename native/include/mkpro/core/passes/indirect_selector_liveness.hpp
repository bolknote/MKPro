#pragma once

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/passes/cfg.hpp"
#include "mkpro/core/passes/helpers.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace mkpro::core::passes {

// An indirect memory operation writes both its destination/result and its
// selector. Proving the loaded X or target store dead says nothing about that
// second effect. Follow every CFG continuation to a complete overwrite, and
// reject intervening reads, unresolved aliases, or interaction barriers.
// The owner keeps the input IR immutable; its CFG is built lazily only once.
class IndirectSelectorWritebackLiveness {
public:
  explicit IndirectSelectorWritebackLiveness(const std::vector<IrOp>& ops)
      : ops_(ops) {}

  bool dead_after(int access_index) const {
    const auto& ops = ops_;
    if (access_index < 0 || access_index >= static_cast<int>(ops.size()))
      return false;
    const IrOp& access = ops.at(static_cast<std::size_t>(access_index));
    if (access.kind != IrKind::IndirectRecall && access.kind != IrKind::IndirectStore)
      return true;
    if (access.meta.logical_register_analysis || has_rewrite_barrier(access) ||
        access.meta.manual_interaction.has_value())
      return false;
    if (!is_stable_indirect_selector(access.register_name))
      return false;
    const std::string& selector = access.register_name;
    const int selector_index = register_index(selector);
    if (!control_.has_value())
      control_ = build_control_flow_graph(ops, {.terminal_stop_fallthrough = false});
    const auto& control = *control_;
    if (!control.targets_are_exact())
      return false;
    std::vector<int> pending;
    for (const auto& edge : control.edges.at(static_cast<std::size_t>(access_index)))
      pending.push_back(edge.target);
    std::vector<bool> seen(ops.size(), false);
    while (!pending.empty()) {
      const int index = pending.back();
      pending.pop_back();
      if (index < 0 || index >= static_cast<int>(ops.size()))
        return false;
      if (seen.at(static_cast<std::size_t>(index)))
        continue;
      seen.at(static_cast<std::size_t>(index)) = true;
      const IrOp& op = ops.at(static_cast<std::size_t>(index));
      if (has_rewrite_barrier(op) || op.meta.manual_interaction.has_value())
        return false;
      if (op.kind == IrKind::Store && op.register_name == selector)
        continue;
      if ((op.kind == IrKind::Recall && op.register_name == selector) ||
          (op.kind == IrKind::Loop && loop_counter_register(op.counter) == selector))
        return false;
      switch (op.kind) {
      case IrKind::IndirectStore:
      case IrKind::IndirectRecall: {
        if (op.register_name == selector)
          return false;
        const auto target = known_indirect_memory_target(op);
        if (target.has_value() && *target == selector) {
          if (op.kind == IrKind::IndirectStore)
            continue; // Independent selector; this complete store kills the old word.
          return false;
        }
        if (!target.has_value()) {
          if (!op.meta.indirect_memory_targets.has_value() ||
              op.meta.indirect_memory_targets->empty() ||
              std::find(op.meta.indirect_memory_targets->begin(),
                        op.meta.indirect_memory_targets->end(), selector_index) !=
                  op.meta.indirect_memory_targets->end())
            return false;
        }
        break;
      }
      case IrKind::IndirectJump:
      case IrKind::IndirectCall:
      case IrKind::IndirectCondJump:
        if (op.register_name == selector)
          return false;
        break;
      default:
        break;
      }
      for (const auto& edge : control.edges.at(static_cast<std::size_t>(index)))
        pending.push_back(edge.target);
    }
    return true;
  }

private:
  const std::vector<IrOp>& ops_;
  mutable std::optional<ControlFlowGraph> control_;
};

} // namespace mkpro::core::passes

