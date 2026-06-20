#include "mkpro/core/passes/redundant_prologue.hpp"

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
    if (op.opcode <= 0x0a)
      return true;
    if (op.opcode == 0x0e)
      return true;
  }
  return false;
}

bool is_show_stop(const IrOp& op) {
  return op.kind == IrKind::Stop && (op.semantic == "show" || op.semantic == "halt");
}

PrologueSegment collect_forward_prologue(const std::vector<IrOp>& ops, int from) {
  std::vector<IrOp> collected;
  int index = from;
  while (index < static_cast<int>(ops.size()) &&
         ops.at(static_cast<std::size_t>(index)).kind == IrKind::Label) {
    ++index;
  }

  while (index < static_cast<int>(ops.size())) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label) {
      ++index;
      continue;
    }
    if (is_show_display_op(op) && !has_rewrite_barrier(op)) {
      collected.push_back(op);
      ++index;
      continue;
    }
    if (is_show_stop(op) && !has_rewrite_barrier(op)) {
      collected.push_back(op);
      return PrologueSegment{.ops = std::move(collected)};
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
    return left.semantic == right.semantic;
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
        if (segments_match(head_backward.ops, suffix))
          matched = true;
      }
    }
    if (!matched)
      continue;

    const int start = head_backward.start_index;
    const int end = index;
    const int forward_end = label_at->second + static_cast<int>(head_forward.ops.size());
    if (start <= forward_end)
      continue;

    bool has_intermediate_content = false;
    for (int scan = forward_end + 1; scan < start; ++scan) {
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
                            " display/halt prologue(s) immediately before a jump to their "
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
