#include "mkpro/core/passes/pre_shift_stack_lift.hpp"

#include "mkpro/core/opcodes.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

bool is_stack_lift(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode == 0x0e && !has_rewrite_barrier(op);
}

bool is_terminal_halt_x2_sync(const IrOp* op) {
  return op != nullptr && op->kind == IrKind::Stop && op->semantic == "halt" &&
         !has_rewrite_barrier(*op) && !is_display_focus_sensitive(*op);
}

bool previous_producer_already_supplies_lift_x2_sync(
    const std::vector<IrOp>& ops, int lift_index,
    const DirectReturnAnalysisContext& context) {
  return x2_previous_stack_lift_and_x2_sync_producer_index(ops, lift_index, context).has_value();
}

} // namespace

PassResult pre_shift_stack_lift(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  std::vector<bool> remove(ops.size(), false);
  int applied = 0;
  const DirectReturnAnalysisContext return_context = direct_return_analysis_context(ops);

  for (int index = 0; index + 1 < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (!is_stack_lift(op))
      continue;

    auto mark_remove = [&]() {
      if (!remove.at(static_cast<std::size_t>(index))) {
        remove.at(static_cast<std::size_t>(index)) = true;
        ++applied;
      }
    };

    if (is_terminal_halt_x2_sync(&ops.at(static_cast<std::size_t>(index + 1)))) {
      if (removing_stack_lift_can_expose_stack(ops, index))
        continue;
      if (removing_recall_can_expose_x2_restore(ops, index))
        continue;
      mark_remove();
      continue;
    }
    if (previous_producer_already_supplies_lift_x2_sync(ops, index, return_context)) {
      if (removing_stack_lift_can_expose_stack(ops, index))
        continue;
      mark_remove();
      continue;
    }
    if (x2_previous_x_preserving_x2_sync_index(ops, index, return_context).has_value()) {
      if (removing_stack_lift_can_expose_stack(ops, index))
        continue;
      mark_remove();
      continue;
    }
    if (x2_previous_hard_x2_overwrite_index(ops, index, return_context).has_value()) {
      if (removing_stack_lift_can_expose_stack(ops, index))
        continue;
      mark_remove();
      continue;
    }
    if (x2_previous_stack_preserving_return_x2_sync_index(ops, index, return_context)
            .has_value()) {
      if (removing_stack_lift_can_expose_stack(ops, index))
        continue;
      mark_remove();
      continue;
    }
    const std::optional<int> producer_index =
        x2_next_stack_shifting_producer_index(ops, index + 1, return_context);
    if (producer_index.has_value()) {
      if (*producer_index == index + 1 &&
          is_stack_lift(ops.at(static_cast<std::size_t>(*producer_index))) &&
          previous_producer_already_supplies_lift_x2_sync(ops, *producer_index,
                                                          return_context) &&
          !removing_stack_lift_can_expose_stack(ops, *producer_index))
        continue;
      if (removing_pre_shift_lift_can_expose_stack(ops, *producer_index))
        continue;
      if (removing_stack_lift_can_expose_stack(ops, index))
        continue;
      if (removing_recall_can_expose_x2_restore(ops, index))
        continue;
      mark_remove();
      continue;
    }
    if (x2_next_x_preserving_x2_sync_index(ops, index + 1, return_context).has_value()) {
      if (removing_stack_lift_can_expose_stack(ops, index))
        continue;
      if (removing_recall_can_expose_x2_restore(ops, index))
        continue;
      mark_remove();
      continue;
    }
    if (x2_next_stack_preserving_return_x2_sync_index(ops, index + 1, return_context)
            .has_value()) {
      if (removing_stack_lift_can_expose_stack(ops, index))
        continue;
      if (removing_recall_can_expose_x2_restore(ops, index))
        continue;
      mark_remove();
      continue;
    }
    if (!x2_next_hard_x2_overwrite_index(ops, index + 1, return_context).has_value())
      continue;
    if (removing_stack_lift_can_expose_stack(ops, index))
      continue;
    if (removing_recall_can_expose_x2_restore(ops, index))
      continue;
    mark_remove();
  }

  if (applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size() - static_cast<std::size_t>(applied));
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (!remove.at(index))
      result.push_back(ops.at(index));
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "pre-shift-stack-lift",
                  .detail = "Removed " + std::to_string(applied) + " В↑ lift" +
                            (applied == 1 ? "" : "s") +
                            " already supplied by a following stack-shifting command, made dead "
                            "before a hard X2 overwrite, or made dead by a terminal halt sync.",
              },
          },
  };
}

IrPass pre_shift_stack_lift_pass() {
  return IrPass{
      .name = "pre-shift-stack-lift",
      .run = pre_shift_stack_lift,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
