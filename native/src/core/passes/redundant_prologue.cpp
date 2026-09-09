#include "mkpro/core/passes/redundant_prologue.hpp"

#include "mkpro/core/passes/cfg.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

struct PrologueSegment {
  std::vector<IrOp> ops;
  int start_index = -1;
  int end_index = -1;
};

struct BackwardSegment {
  std::vector<IrOp> ops;
  int start_index = -1;
  std::optional<std::string> virtual_head_register;
};

struct RemoveRange {
  int start = 0;
  int end = 0;
};

bool is_show_display_op(const IrOp& op) {
  if (op.kind == IrKind::Recall)
    return true;
  if (op.kind == IrKind::Plain) {
    if (op.opcode == 0x10 || op.opcode == 0x12)
      return true;
    if (op.opcode >= 0 && op.opcode <= 0x0a)
      return true;
    if (op.opcode == 0x0e)
      return true;
  }
  return false;
}

bool is_show_stop(const IrOp& op) {
  // A resumable stop is an observable interaction, even when both displays
  // are identical. Moving the backedge before it would skip one user resume
  // on every iteration. Only source-terminal displays may be shared here.
  return op.kind == IrKind::Stop && op.opcode == 0x50 &&
         op.meta.stop_disposition == StopDisposition::Terminal &&
         op.semantic == "halt";
}

bool has_display_rewrite_barrier(const IrOp& op) {
  return has_rewrite_barrier(op) || !op.meta.roles.empty() ||
         op.procedure_boundary.has_value();
}

PrologueSegment collect_forward_prologue(const std::vector<IrOp>& ops, int from) {
  std::vector<IrOp> collected;
  int index = from;
  while (index < static_cast<int>(ops.size()) &&
         ops.at(static_cast<std::size_t>(index)).kind == IrKind::Label) {
    ++index;
  }

  const int start_index = index;
  while (index < static_cast<int>(ops.size())) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (has_display_rewrite_barrier(op))
      return PrologueSegment{};
    if (op.kind == IrKind::Label) {
      ++index;
      continue;
    }
    if (is_show_display_op(op)) {
      collected.push_back(op);
      ++index;
      continue;
    }
    if (is_show_stop(op)) {
      collected.push_back(op);
      return PrologueSegment{
          .ops = std::move(collected), .start_index = start_index, .end_index = index + 1};
    }
    return PrologueSegment{};
  }
  return PrologueSegment{};
}

BackwardSegment collect_backward_prologue(const std::vector<IrOp>& ops, int before_index) {
  std::vector<IrOp> collected;
  int index = before_index - 1;
  bool saw_stop = false;

  while (index >= 0) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (has_display_rewrite_barrier(op))
      break;
    if (op.kind == IrKind::Label) {
      --index;
      continue;
    }
    if (!saw_stop) {
      if (is_show_stop(op) && !has_rewrite_barrier(op)) {
        collected.push_back(op);
        saw_stop = true;
        --index;
        continue;
      }
      return BackwardSegment{};
    }
    if (is_show_display_op(op) && !has_rewrite_barrier(op)) {
      collected.push_back(op);
      --index;
      continue;
    }
    break;
  }

  if (!saw_stop)
    return BackwardSegment{};

  int start_index = index + 1;
  while (start_index < before_index &&
         ops.at(static_cast<std::size_t>(start_index)).kind == IrKind::Label) {
    ++start_index;
  }

  std::optional<std::string> virtual_head_register;
  int scan = index;
  while (scan >= 0 && ops.at(static_cast<std::size_t>(scan)).kind == IrKind::Label) {
    --scan;
  }
  if (scan >= 0) {
    const IrOp& prior = ops.at(static_cast<std::size_t>(scan));
    if (prior.kind == IrKind::Store && !has_rewrite_barrier(prior))
      virtual_head_register = prior.register_name;
  }

  std::reverse(collected.begin(), collected.end());
  return BackwardSegment{
      .ops = std::move(collected),
      .start_index = start_index,
      .virtual_head_register = std::move(virtual_head_register),
  };
}

bool ops_equivalent(const IrOp& left, const IrOp& right) {
  if (left.kind != right.kind)
    return false;
  if (left.kind == IrKind::Recall)
    return left.register_name == right.register_name;
  if (left.kind == IrKind::Plain)
    return left.opcode == right.opcode;
  if (left.kind == IrKind::Stop)
    return left.semantic == right.semantic &&
           left.meta.stop_disposition == right.meta.stop_disposition;
  return false;
}

bool segments_match(const std::vector<IrOp>& left, const std::vector<IrOp>& right) {
  if (left.empty() || left.size() != right.size())
    return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (!ops_equivalent(left.at(index), right.at(index)))
      return false;
  }
  return true;
}

bool virtual_head_preserves_display_state(const std::vector<IrOp>& head) {
  // Both executions start with X equal to the just-stored register. The
  // inserted head recall may nevertheless shift all three deeper stack slots
  // and replace hidden X2. Track equality, not merely source register use.
  StackValueEqualityState equality;
  equality.stack_equal = {true, false, false, false};
  equality.x2_equal = false;
  bool number_entry_active = false;  // Store and recall both close digit entry.
  for (std::size_t i = 1; i < head.size(); ++i) {
    const IrOp& op = head.at(i);
    if (is_show_stop(op))
      return stack_values_fully_equal(equality);
    StackValueEqualityTransfer transfer;
    if (op.kind == IrKind::Plain && op.opcode >= 0 && op.opcode <= 9) {
      transfer = transfer_decimal_digit_equality(equality, number_entry_active);
      number_entry_active = true;
    } else {
      transfer = transfer_stack_value_equality(
          equality, op.opcode,
          op.kind == IrKind::Recall ? StackValueEqualityStepKind::Recall
                                   : StackValueEqualityStepKind::Plain);
      number_entry_active = false;
    }
    if (transfer == StackValueEqualityTransfer::Rejected)
      return false;
  }
  return false;
}

bool ranges_overlap(const std::vector<RemoveRange>& ranges, int start, int end) {
  return std::any_of(ranges.begin(), ranges.end(), [&](const RemoveRange& range) {
    return !(end <= range.start || start >= range.end);
  });
}

} // namespace

PassResult redundant_prologue_elimination(const std::vector<IrOp>& ops,
                                          const PassContext& context) {
  (void)context;

  std::map<std::string, int> label_index;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label)
      label_index[op.name] = index;
  }

  std::optional<ControlFlowGraph> control;
  std::optional<NumericFlowTargetLayoutGuard> numeric_targets;
  std::vector<RemoveRange> remove_ranges;
  int applied = 0;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    const auto* target = std::get_if<std::string>(&op.target);
    if (op.kind != IrKind::Jump || target == nullptr || has_rewrite_barrier(op))
      continue;

    const auto label_at = label_index.find(*target);
    if (label_at == label_index.end())
      continue;

    const PrologueSegment head_forward = collect_forward_prologue(ops, label_at->second);
    if (head_forward.ops.empty())
      continue;

    const BackwardSegment head_backward = collect_backward_prologue(ops, index);
    if (head_backward.ops.empty())
      continue;

    bool matched = segments_match(head_backward.ops, head_forward.ops);
    if (!matched && head_backward.virtual_head_register.has_value()) {
      const IrOp& first_forward = head_forward.ops.at(0);
      if (first_forward.kind == IrKind::Recall &&
          first_forward.register_name == *head_backward.virtual_head_register) {
        std::vector<IrOp> suffix(head_forward.ops.begin() + 1, head_forward.ops.end());
        // Replacing a store-carried X by a fresh recall can change Y/Z/T
        // and decimal-entry X2 even though the displayed X is unchanged.
        const int prior_index = head_backward.start_index - 1;
        if (segments_match(head_backward.ops, suffix) && prior_index >= 0 &&
            ops.at(static_cast<std::size_t>(prior_index)).kind == IrKind::Store &&
            !has_display_rewrite_barrier(ops.at(static_cast<std::size_t>(prior_index))) &&
            virtual_head_preserves_display_state(head_forward.ops)) {
          matched = true;
        }
      }
    }
    if (!matched)
      continue;

    const int start = head_backward.start_index;
    const int end = index;
    const int forward_end = head_forward.end_index;
    if (start < forward_end)
      continue;

    bool has_intermediate_content = false;
    for (int scan = forward_end; scan < start; ++scan) {
      const IrOp& intermediate = ops.at(static_cast<std::size_t>(scan));
      if (intermediate.kind == IrKind::Label)
        continue;
      has_intermediate_content = true;
      break;
    }
    if (!has_intermediate_content)
      continue;
    if (ranges_overlap(remove_ranges, start, end))
      continue;

    if (!control.has_value()) {
      control = build_control_flow_graph(ops);
      numeric_targets = numeric_flow_target_layout_guard(ops);
    }
    if (!control->targets_are_exact() || !numeric_targets.has_value())
      continue;
    bool safe = true;
    for (int scan = start; scan < end; ++scan) {
      if (ops.at(static_cast<std::size_t>(scan)).kind != IrKind::Label &&
          !numeric_targets->can_delete_at(scan)) {
        safe = false;
      }
    }
    // Keeping an inner label does not keep its entry semantics: the label
    // would now lead to the full head instead of the requested suffix.
    for (std::size_t source = 0; source < control->edges.size() && safe; ++source) {
      if (static_cast<int>(source) >= start && static_cast<int>(source) < end)
        continue;
      for (const CfgEdge& edge : control->edges.at(source)) {
        if (edge.target < start || edge.target >= end)
          continue;
        if (edge.target != start || static_cast<int>(source) != start - 1 ||
            edge.kind == CfgEdgeKind::Jump) {
          safe = false;
          break;
        }
      }
    }
    if (!safe)
      continue;

    remove_ranges.push_back(RemoveRange{.start = start, .end = end});
    ++applied;
  }

  if (applied == 0) {
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
  }

  std::vector<bool> should_remove(ops.size(), false);
  for (const RemoveRange& range : remove_ranges) {
    for (int index = range.start; index < range.end; ++index)
      should_remove.at(static_cast<std::size_t>(index)) = true;
  }

  std::vector<IrOp> result;
  result.reserve(ops.size());
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (!should_remove.at(index) || op.kind == IrKind::Label)
      result.push_back(op);
  }

  int total_cells = 0;
  for (const RemoveRange& range : remove_ranges)
    total_cells += range.end - range.start;

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "redundant-prologue-elimination",
                  .detail = "Removed " + std::to_string(applied) +
" terminal display prologue(s) immediately before a jump to their "
                            "identical loop head (" +
                            std::to_string(total_cells) + " cells).",
              },
          },
  };
}

IrPass redundant_prologue_elimination_pass() {
  return IrPass{
      .name = "redundant-prologue-elimination",
      .run = redundant_prologue_elimination,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
