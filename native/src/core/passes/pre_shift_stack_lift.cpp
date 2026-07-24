#include "mkpro/core/passes/pre_shift_stack_lift.hpp"

#include "mkpro/core/opcodes.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

constexpr int kStackLiftOpcode = 0x0e;
constexpr int kFReverseOpcode = 0x25;
constexpr int kYToXOpcode = 0x3e;

bool is_stack_lift(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode == kStackLiftOpcode && !has_rewrite_barrier(op);
}

bool is_f_reverse(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode == kFReverseOpcode &&
         !has_rewrite_barrier(op) && !is_display_focus_sensitive(op) &&
         op.meta.roles.empty();
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

bool hidden_x2_sync_after_reverse_lift_is_unobserved(
    const std::vector<IrOp>& ops, int after_lift_index,
    const DirectReturnAnalysisContext& context) {
  if (after_lift_index >= static_cast<int>(ops.size()))
    return true;
  if (is_terminal_halt_x2_sync(&ops.at(static_cast<std::size_t>(after_lift_index))))
    return true;
  if (x2_next_x_preserving_x2_sync_index(ops, after_lift_index, context).has_value())
    return true;
  if (x2_next_stack_preserving_return_x2_sync_index(ops, after_lift_index, context)
          .has_value())
    return true;
  return x2_next_hard_x2_overwrite_index(ops, after_lift_index, context).has_value();
}

bool manual_3e_entry_has_restorable_previous_cell(const std::vector<IrOp>& ops,
                                                  int op_index) {
  int address = 0;
  bool previous_physical_cell_is_3e = false;
  for (int index = 0; index < op_index; ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    const int cells = cells_per_op(op);
    if (cells == 0)
      continue;
    previous_physical_cell_is_3e =
        cells == 1 && op.kind == IrKind::Plain && op.opcode == kYToXOpcode;
    address += cells;
  }
  return address > 0 && !previous_physical_cell_is_3e;
}

bool output_requires_manual_3e_entry(OutputFormat output) {
  return output == OutputFormat::Listing || output == OutputFormat::Keys ||
         output == OutputFormat::All;
}

bool can_deliver_3e_at(const std::vector<IrOp>& ops, int op_index,
                       const PassContext& context) {
  if (!output_requires_manual_3e_entry(context.options.output))
    return true;
  return manual_3e_entry_has_restorable_previous_cell(ops, op_index);
}

IrOp y_to_x_copy_op(const IrOp& source) {
  IrOp replacement = source;
  replacement.opcode = kYToXOpcode;
  replacement.meta.mnemonic = opcode_by_code(kYToXOpcode).name;
  replacement.meta.comment.reset();
  replacement.meta.roles.clear();
  return replacement;
}

} // namespace

PassResult pre_shift_stack_lift(const std::vector<IrOp>& ops, const PassContext& context) {
  std::vector<bool> remove(ops.size(), false);
  std::vector<std::optional<IrOp>> replace(ops.size());
  int removed_lifts = 0;
  int folded_3e = 0;
  const DirectReturnAnalysisContext return_context = direct_return_analysis_context(ops);

  for (int index = 0; index + 1 < static_cast<int>(ops.size()); ++index) {
    const IrOp& reverse = ops.at(static_cast<std::size_t>(index));
    const IrOp& lift = ops.at(static_cast<std::size_t>(index + 1));
    if (!is_f_reverse(reverse) || !is_stack_lift(lift) || !lift.meta.roles.empty() ||
        is_display_focus_sensitive(lift))
      continue;
    if (!can_deliver_3e_at(ops, index, context))
      continue;
    if (!hidden_x2_sync_after_reverse_lift_is_unobserved(ops, index + 2, return_context))
      continue;

    replace.at(static_cast<std::size_t>(index)) = y_to_x_copy_op(reverse);
    remove.at(static_cast<std::size_t>(index + 1)) = true;
    ++folded_3e;
  }

  for (int index = 0; index + 1 < static_cast<int>(ops.size()); ++index) {
    if (remove.at(static_cast<std::size_t>(index)))
      continue;
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (!is_stack_lift(op) || !op.meta.roles.empty())
      continue;

    auto mark_remove = [&]() {
      if (!remove.at(static_cast<std::size_t>(index))) {
        remove.at(static_cast<std::size_t>(index)) = true;
        ++removed_lifts;
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

  const int applied = removed_lifts + folded_3e;
  if (applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size() - static_cast<std::size_t>(applied));
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (!remove.at(index))
      result.push_back(replace.at(index).value_or(ops.at(index)));
  }

  std::vector<AppliedOptimization> optimizations;
  if (removed_lifts > 0) {
    optimizations.push_back(AppliedOptimization{
        .name = "pre-shift-stack-lift",
        .detail = "Removed " + std::to_string(removed_lifts) + " В↑ lift" +
                  (removed_lifts == 1 ? "" : "s") +
                  " already supplied by a following stack-shifting command, made dead "
                  "before a hard X2 overwrite, or made dead by a terminal halt sync.",
    });
  }
  if (folded_3e > 0) {
    optimizations.push_back(AppliedOptimization{
        .name = "y-to-x-copy-3e",
        .detail = "Folded " + std::to_string(folded_3e) +
                  " F reverse; В↑ pair" + (folded_3e == 1 ? "" : "s") +
                  " to undocumented 3E where the old hidden-X2 sync is unobserved.",
    });
  }

  return PassResult{.ops = std::move(result),
                    .applied = applied,
                    .optimizations = std::move(optimizations)};
}

IrPass pre_shift_stack_lift_pass() {
  return IrPass{
      .name = "pre-shift-stack-lift",
      .run = pre_shift_stack_lift,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
