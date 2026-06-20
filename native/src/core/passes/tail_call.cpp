#include "mkpro/core/passes/tail_call.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

struct TailJumpTarget {
  IrTarget continuation;
  int start = 0;
  int end = 0;
};

struct Region {
  int start = 0;
  int end = 0;
};

std::optional<std::string> string_target(const IrTarget& target) {
  if (const auto* value = std::get_if<std::string>(&target))
    return *value;
  return std::nullopt;
}

bool same_target(const IrTarget& left, const IrTarget& right) {
  return left == right;
}

bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::optional<std::string> replace_comment_prefix(std::optional<std::string> comment,
                                                  std::string_view prefix,
                                                  std::string_view replacement,
                                                  std::string fallback) {
  if (!comment.has_value())
    return fallback;
  std::string out = *comment;
  if (starts_with(out, prefix))
    out.replace(0, prefix.size(), replacement);
  return out;
}

IrTarget normalize_continuation(const std::vector<IrOp>& ops,
                                const std::map<std::string, int>& label_indexes,
                                const IrTarget& target, std::set<std::string> seen = {}) {
  const auto* label = std::get_if<std::string>(&target);
  if (label == nullptr)
    return target;
  if (seen.contains(*label))
    return target;
  seen.insert(*label);

  const auto label_index = label_indexes.find(*label);
  if (label_index == label_indexes.end())
    return target;
  const std::optional<int> executable_index = next_executable_index(ops, label_index->second + 1);
  if (!executable_index.has_value())
    return target;
  const IrOp& executable = ops.at(static_cast<std::size_t>(*executable_index));
  if (executable.kind != IrKind::Jump || has_rewrite_barrier(executable))
    return target;
  return normalize_continuation(ops, label_indexes, executable.target, std::move(seen));
}

std::map<std::string, int> build_label_indexes(const std::vector<IrOp>& ops) {
  std::map<std::string, int> label_indexes;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label)
      label_indexes[op.name] = index;
  }
  return label_indexes;
}

std::optional<IrTarget> call_continuation(const std::vector<IrOp>& ops, int index) {
  const int next_index = index + 1;
  if (next_index >= static_cast<int>(ops.size()))
    return std::nullopt;
  const IrOp& next = ops.at(static_cast<std::size_t>(next_index));
  if (next.kind == IrKind::Jump)
    return next.target;
  if (next.kind == IrKind::Label)
    return next.name;
  return std::nullopt;
}

std::map<std::string, Region> collect_callable_regions(const std::vector<IrOp>& ops,
                                                       const std::set<std::string>& call_targets) {
  std::map<std::string, Region> result;
  std::optional<std::string> current_name;
  int current_start = 0;

  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind != IrKind::Label)
      continue;
    if (!call_targets.contains(op.name))
      continue;
    if (current_name.has_value())
      result[*current_name] = Region{.start = current_start, .end = index};
    current_name = op.name;
    current_start = index + 1;
  }
  if (current_name.has_value())
    result[*current_name] = Region{.start = current_start, .end = static_cast<int>(ops.size())};
  return result;
}

bool block_has_return(const std::vector<IrOp>& ops, int start, int end) {
  for (int index = start; index < end; ++index) {
    if (ops.at(static_cast<std::size_t>(index)).kind == IrKind::Return)
      return true;
  }
  return false;
}

std::optional<std::string> terminal_tail_target(const std::vector<IrOp>& ops, int start, int end) {
  for (int index = end - 1; index >= start; --index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label)
      continue;
    if (op.kind == IrKind::Jump)
      return string_target(op.target);
    return std::nullopt;
  }
  return std::nullopt;
}

std::map<std::string, TailJumpTarget> find_tail_jump_targets(const std::vector<IrOp>& ops) {
  const std::map<std::string, int> label_indexes = build_label_indexes(ops);
  std::map<std::string, std::vector<std::optional<IrTarget>>> calls;
  std::set<std::string> non_call_flow_targets;

  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    const std::optional<std::string> target = string_target(op.target);
    if (op.kind == IrKind::Call && target.has_value()) {
      const std::optional<IrTarget> continuation = call_continuation(ops, index);
      if (continuation.has_value()) {
        calls[*target].push_back(normalize_continuation(ops, label_indexes, *continuation));
      } else {
        calls[*target].push_back(std::nullopt);
      }
      continue;
    }
    if ((op.kind == IrKind::Jump || op.kind == IrKind::CondJump || op.kind == IrKind::Loop) &&
        target.has_value()) {
      non_call_flow_targets.insert(*target);
    }
  }

  std::set<std::string> call_targets;
  for (const auto& [target, unused] : calls) {
    (void)unused;
    call_targets.insert(target);
  }

  const std::map<std::string, Region> regions = collect_callable_regions(ops, call_targets);
  std::map<std::string, TailJumpTarget> result;
  for (const auto& [target, continuations] : calls) {
    if (non_call_flow_targets.contains(target))
      continue;
    const auto region = regions.find(target);
    if (region == regions.end() || !block_has_return(ops, region->second.start, region->second.end))
      continue;
    if (continuations.empty() || !continuations.front().has_value())
      continue;
    const IrTarget first = *continuations.front();
    bool all_same = true;
    for (const std::optional<IrTarget>& continuation : continuations) {
      if (!continuation.has_value() || !same_target(*continuation, first)) {
        all_same = false;
        break;
      }
    }
    if (all_same) {
      result[target] = TailJumpTarget{
          .continuation = first,
          .start = region->second.start,
          .end = region->second.end,
      };
    }
  }
  return result;
}

std::map<int, IrTarget> collect_return_continuations(
    const std::vector<IrOp>& ops, const std::map<std::string, TailJumpTarget>& targets) {
  std::map<int, IrTarget> result;
  for (const auto& [unused_name, target] : targets) {
    (void)unused_name;
    for (int index = target.start; index < target.end; ++index) {
      if (ops.at(static_cast<std::size_t>(index)).kind == IrKind::Return)
        result[index] = target.continuation;
    }
  }
  return result;
}

std::set<std::string> collect_return_labels(const std::vector<IrOp>& ops) {
  std::set<std::string> result;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind != IrKind::Label)
      continue;
    const std::optional<int> next = next_executable_index(ops, index + 1);
    if (next.has_value() && ops.at(static_cast<std::size_t>(*next)).kind == IrKind::Return)
      result.insert(op.name);
  }
  return result;
}

std::set<std::string> collect_return_targets(const std::vector<IrOp>& ops) {
  std::set<std::string> call_targets;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Call) {
      const std::optional<std::string> target = string_target(op.target);
      if (target.has_value())
        call_targets.insert(*target);
    }
    if (op.kind == IrKind::Label && op.procedure_boundary == "start")
      call_targets.insert(op.name);
  }

  const std::map<std::string, Region> regions = collect_callable_regions(ops, call_targets);
  std::set<std::string> result;
  for (const auto& [target, region] : regions) {
    if (block_has_return(ops, region.start, region.end))
      result.insert(target);
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& [target, region] : regions) {
      if (result.contains(target))
        continue;
      const std::optional<std::string> tail = terminal_tail_target(ops, region.start, region.end);
      if (tail.has_value() && result.contains(*tail)) {
        result.insert(target);
        changed = true;
      }
    }
  }
  return result;
}

bool is_return_label(const IrTarget& target, const std::set<std::string>& return_labels) {
  const std::optional<std::string> label = string_target(target);
  return label.has_value() && return_labels.contains(*label);
}

std::string tail_call_detail(int applied, int tail_jump_count, int empty_stack_applied) {
  const std::string base =
      tail_jump_count == 0
          ? "Replaced " + std::to_string(applied) + " subroutine tail call" +
                (applied == 1 ? "" : "s") + " with direct jump(s)."
          : "Replaced " + std::to_string(applied) + " subroutine tail operation" +
                (applied == 1 ? "" : "s") + " with direct jump continuation" +
                (tail_jump_count == 1 ? "" : "s") + ".";
  if (empty_stack_applied == 0)
    return base;
  return base + " " + std::to_string(empty_stack_applied) + " site" +
         (empty_stack_applied == 1 ? "" : "s") +
         " use empty-return-stack В/О as the loop-head continuation.";
}

IrOp jump_from_return(const IrOp& op, IrTarget continuation) {
  IrOp out;
  out.kind = IrKind::Jump;
  out.target = std::move(continuation);
  out.opcode = 0x51;
  out.meta.mnemonic = "БП";
  out.meta.comment =
      replace_comment_prefix(op.meta.comment, "implicit return from proc", "tail continuation",
                             "tail continuation");
  out.meta.source_line = op.meta.source_line;
  out.target_meta.comment = "tail continuation";
  return out;
}

IrOp jump_from_call(const IrOp& op, std::string_view replacement, std::string fallback) {
  IrOp out;
  out.kind = IrKind::Jump;
  out.target = op.target;
  out.opcode = 0x51;
  out.meta = op.meta;
  out.meta.mnemonic = "БП";
  out.meta.comment =
      replace_comment_prefix(op.meta.comment, "proc call", replacement, std::move(fallback));
  out.target_meta = op.target_meta;
  return out;
}

bool loop_back_targets_head(const std::vector<IrOp>& ops,
                            const std::map<std::string, int>& label_indexes,
                            const std::map<std::string, int>& label_addresses, const IrOp& op) {
  if (op.kind == IrKind::Jump) {
    const IrTarget normalized = normalize_continuation(ops, label_indexes, op.target);
    const std::optional<int> address = target_address(normalized, label_addresses);
    return address.has_value() && *address == 0;
  }
  const std::optional<int> indirect_target = known_indirect_flow_target(op);
  return indirect_target.has_value() && *indirect_target == 0 && op.kind == IrKind::IndirectJump;
}

} // namespace

PassResult tail_call_lowering(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  const std::map<std::string, TailJumpTarget> tail_jump_targets = find_tail_jump_targets(ops);
  const std::map<int, IrTarget> return_continuations =
      collect_return_continuations(ops, tail_jump_targets);
  const std::set<std::string> return_labels = collect_return_labels(ops);
  const std::map<std::string, int> label_indexes = build_label_indexes(ops);
  const std::map<std::string, int> label_addresses = calculate_label_addresses(ops);
  const std::set<std::string> return_targets = collect_return_targets(ops);

  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;
  int empty_stack_applied = 0;
  bool seen_procedure_start = false;

  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label) {
      if (op.procedure_boundary == "start")
        seen_procedure_start = true;
      result.push_back(op);
      continue;
    }

    const IrOp* next =
        index + 1 < static_cast<int>(ops.size()) ? &ops.at(static_cast<std::size_t>(index + 1))
                                                 : nullptr;
    if (op.kind == IrKind::Return) {
      const auto continuation = return_continuations.find(index);
      if (continuation != return_continuations.end()) {
        result.push_back(jump_from_return(op, continuation->second));
        ++applied;
        continue;
      }
    }

    if (op.kind == IrKind::Call) {
      const std::optional<int> continuation_index = next_executable_index(ops, index + 1);
      const IrOp* continuation =
          continuation_index.has_value()
              ? &ops.at(static_cast<std::size_t>(*continuation_index))
              : nullptr;
      const bool continuation_is_immediate =
          continuation_index.has_value() && *continuation_index == index + 1;

      if (continuation != nullptr && continuation->kind == IrKind::Jump &&
          is_return_label(continuation->target, return_labels)) {
        result.push_back(jump_from_call(op, "tail call", "tail call"));
        if (continuation_is_immediate)
          ++index;
        ++applied;
        continue;
      }

      if (continuation != nullptr && continuation->kind == IrKind::Return) {
        result.push_back(jump_from_call(op, "tail call", "tail call"));
        if (continuation_is_immediate)
          ++index;
        ++applied;
        continue;
      }

      const std::optional<std::string> call_target = string_target(op.target);
      const auto tail_target =
          call_target.has_value() ? tail_jump_targets.find(*call_target) : tail_jump_targets.end();
      if (tail_target != tail_jump_targets.end() && next != nullptr && next->kind == IrKind::Jump &&
          same_target(normalize_continuation(ops, label_indexes, next->target),
                      tail_target->second.continuation)) {
        result.push_back(jump_from_call(op, "tail jump", "tail jump"));
        ++index;
        ++applied;
        continue;
      }

      if (!seen_procedure_start && call_target.has_value() && return_targets.contains(*call_target) &&
          next != nullptr && loop_back_targets_head(ops, label_indexes, label_addresses, *next) &&
          !has_rewrite_barrier(op) && !has_rewrite_barrier(*next)) {
        result.push_back(jump_from_call(op, "empty-stack tail call", "empty-stack tail call"));
        ++index;
        ++applied;
        ++empty_stack_applied;
        continue;
      }

      if (tail_target != tail_jump_targets.end() && next != nullptr && next->kind == IrKind::Label &&
          same_target(normalize_continuation(ops, label_indexes, next->name),
                      tail_target->second.continuation)) {
        result.push_back(jump_from_call(op, "tail jump", "tail jump"));
        ++applied;
        continue;
      }
    }

    result.push_back(op);
  }

  if (applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const int tail_jump_count = static_cast<int>(tail_jump_targets.size());
  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "tail-call-lowering",
                  .detail = tail_call_detail(applied, tail_jump_count, empty_stack_applied),
              },
          },
  };
}

IrPass tail_call_lowering_pass() {
  return IrPass{
      .name = "tail-call-lowering",
      .run = tail_call_lowering,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
