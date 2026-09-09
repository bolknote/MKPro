#include "mkpro/core/passes/dead_code_after_halt.hpp"

#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/passes/cfg.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <vector>

namespace mkpro::core::passes {

namespace {

std::optional<std::set<int>> reachable_from_entry(
    const std::vector<IrOp>& ops, const CompileOptions& options) {
  // Erasure here transports symbolic identities, not encoded addresses or
  // external instruction-entry protocols. Those belong to final-layout
  // transactions with their own address/continuation proofs.
  for (const IrOp& op : ops) {
    // A raw NOP has no encoded target; keep the cell itself as a root rather
    // than freezing unrelated symbolic blocks. Manual anchors are also roots
    // in the conservative competitor, never candidates for erasure.
    const bool conservative_anchor = !options.exact_terminal_dead_code_elimination &&
        ((!op.meta.raw && op.meta.manual_interaction.has_value()) ||
         (op.meta.raw && !op.meta.manual_interaction.has_value() &&
          op.kind == IrKind::Plain && op.opcode == 0x54));
    const bool indirect_flow = op.kind == IrKind::IndirectJump ||
        op.kind == IrKind::IndirectCall || op.kind == IrKind::IndirectCondJump;
    const bool symbolic_indirect_flow = !options.exact_terminal_dead_code_elimination &&
        op.meta.indirect_flow_targets.has_value() &&
        !op.meta.indirect_flow_targets->empty() &&
        !op.meta.indirect_flow_formal_targets.has_value() &&
        std::all_of(op.meta.indirect_flow_targets->begin(),
                    op.meta.indirect_flow_targets->end(), [](const IrTarget& target) {
                      return std::holds_alternative<std::string>(target);
                    });
    if ((has_rewrite_barrier(op) && !conservative_anchor) ||
        op.kind == IrKind::OrphanAddress ||
        (indirect_flow && !symbolic_indirect_flow) ||
        ((op.kind == IrKind::Jump || op.kind == IrKind::Call ||
          op.kind == IrKind::CondJump || op.kind == IrKind::Loop) &&
         !std::holds_alternative<std::string>(op.target)) ||
        (op.opcode == 0x29 &&
         op.meta.stop_disposition != StopDisposition::Terminal)) {
      return std::nullopt;
    }
  }

  if (!options.exact_terminal_dead_code_elimination) {
    // Preserve ordinary physical stop-continuation geometry as a competitor:
    // local source-terminal erasure can forfeit a larger address/code saving.
    const auto graph = build_control_flow_graph(
        ops, BuildCfgOptions{.indirect_call_fallthrough = true});
    std::set<int> visited;
    std::vector<int> pending;
    if (!ops.empty()) pending.push_back(0);
    for (std::size_t index = 0; index < ops.size(); ++index)
      if (has_rewrite_barrier(ops.at(index)))
        pending.push_back(static_cast<int>(index));
    // The conservative CFG joins known callers but does not model an empty
    // return stack. Preserve the hardware's physical-01 continuation as well.
    if (std::any_of(ops.begin(), ops.end(),
                    [](const IrOp& op) { return op.kind == IrKind::Return; })) {
      const auto indexes = build_target_indexes(ops);
      const auto entry = indexes.address_index.find(1);
      if (entry != indexes.address_index.end()) pending.push_back(entry->second);
    }
    while (!pending.empty()) {
      const int index = pending.back();
      pending.pop_back();
      if (!visited.insert(index).second) continue;
      for (const auto& edge : graph.edges.at(static_cast<std::size_t>(index)))
        pending.push_back(edge.target);
    }
    return visited;
  }

  const auto items = lower_ir_to_machine(ops);
  const auto flow = build_post_layout_control_flow(
      items, {.address_space_model = address_space_model_for_feature_profile(
                  effective_optimizer_feature_profile(options)),
              .empty_return_target = 1});
  if (!flow.proved)
    return std::nullopt;

  std::set<std::size_t> reachable_items;
  for (const auto& state : flow.execution_states)
    reachable_items.insert(state.item_index);
  std::set<int> reachable_ops;
  std::size_t item_index = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    // Labels occupy one MachineItem but no program cell. Address operands
    // belong to their owning IR transfer and must survive with that transfer.
    const std::size_t item_count =
        ops.at(index).kind == IrKind::Label
            ? 1U : static_cast<std::size_t>(cells_per_op(ops.at(index)));
    for (std::size_t offset = 0; offset < item_count; ++offset)
      if (reachable_items.contains(item_index + offset))
        reachable_ops.insert(static_cast<int>(index));
    item_index += item_count;
  }
  return reachable_ops;
}

} // namespace

PassResult dead_code_after_halt(const std::vector<IrOp>& ops, const PassContext& context) {
  if (ops.empty())
    return PassResult{.ops = {}, .applied = 0, .optimizations = {}};

  const auto reachable = reachable_from_entry(ops, context.options);
  if (!reachable.has_value() || reachable->size() == ops.size())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (reachable->contains(static_cast<int>(index)) || op.kind == IrKind::Label) {
      result.push_back(op);
      continue;
    }
    ++applied;
  }

  if (applied == 0)
    return PassResult{.ops = std::move(result), .applied = 0, .optimizations = {}};

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = context.options.exact_terminal_dead_code_elimination
                              ? "exact-terminal-unreachable-code" : "dead-code-after-halt",
                  .detail = "Removed " + std::to_string(applied) +
                            (context.options.exact_terminal_dead_code_elimination
                                 ? " unreachable op(s) using the exact stop/return CFG."
                                 : " unreachable op(s) from the conservative entry CFG."),
              },
          },
  };
}

IrPass dead_code_after_halt_pass() {
  return IrPass{
      .name = "dead-code-after-halt",
      .run = dead_code_after_halt,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
