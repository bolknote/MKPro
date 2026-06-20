#include "mkpro/core/passes/duplicate_failure_tail.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

struct LabelRun {
  std::vector<std::string> labels;
  int next = 0;
};

bool is_zero_digit(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode == 0x00;
}

bool is_pause_like(const IrOp& op) {
  return op.kind == IrKind::Stop;
}

bool is_unconditional_jump(const IrOp& op) {
  return op.kind == IrKind::Jump;
}

bool is_terminal_flow(const IrOp& op) {
  return op.kind == IrKind::Jump || op.kind == IrKind::IndirectJump || op.kind == IrKind::Return;
}

bool cannot_fall_through(const IrOp* op) {
  return op == nullptr || is_terminal_flow(*op);
}

bool previous_executable_cannot_fall_through(const std::vector<IrOp>& ops, int index) {
  for (int cursor = index - 1; cursor >= 0; --cursor) {
    const IrOp& op = ops.at(static_cast<std::size_t>(cursor));
    if (op.kind == IrKind::Label)
      return false;
    return cannot_fall_through(&op);
  }
  return true;
}

bool can_remove_label(const IrOp& op) {
  return op.kind == IrKind::Label && !op.procedure_boundary.has_value();
}

std::optional<LabelRun> removable_label_run(const std::vector<IrOp>& ops, int index) {
  LabelRun run;
  int cursor = index;
  while (cursor < static_cast<int>(ops.size())) {
    const IrOp& op = ops.at(static_cast<std::size_t>(cursor));
    if (!can_remove_label(op))
      break;
    run.labels.push_back(op.name);
    ++cursor;
  }
  if (run.labels.empty())
    return std::nullopt;
  run.next = cursor;
  return run;
}

std::string target_key(const IrTarget& target) {
  if (const auto* label = std::get_if<std::string>(&target))
    return *label;
  return std::to_string(std::get<int>(target));
}

std::optional<std::string> terminal_flow_key(const IrOp& op) {
  if (op.kind == IrKind::Jump)
    return "jump:" + target_key(op.target) + ":" + std::to_string(op.opcode);
  if (op.kind == IrKind::IndirectJump)
    return "indirect-jump:" + op.register_name + ":" + std::to_string(op.opcode);
  if (op.kind == IrKind::Return)
    return "return:" + std::to_string(op.opcode);
  return std::nullopt;
}

const IrOp* op_at(const std::vector<IrOp>& ops, int index) {
  if (index < 0 || index >= static_cast<int>(ops.size()))
    return nullptr;
  return &ops.at(static_cast<std::size_t>(index));
}

void remove_range(std::set<int>& remove, int first, int last) {
  for (int index = first; index <= last; ++index)
    remove.insert(index);
}

bool target_is_string(const IrOp& op) {
  return std::holds_alternative<std::string>(op.target);
}

const std::string& string_target(const IrOp& op) {
  return std::get<std::string>(op.target);
}

} // namespace

PassResult duplicate_failure_tail(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  std::map<std::string, std::string> rewrite;
  std::set<int> remove;
  int applied = 0;
  int pause_only_applied = 0;
  std::map<std::string, std::string> separated_pause_tails;

  for (int i = 0; i + 2 < static_cast<int>(ops.size()); ++i) {
    if (remove.contains(i))
      continue;
    const std::optional<LabelRun> run = removable_label_run(ops, i);
    if (!run.has_value())
      continue;
    const IrOp* pause = op_at(ops, run->next);
    const IrOp* flow = op_at(ops, run->next + 1);
    if (pause != nullptr && is_pause_like(*pause) && flow != nullptr && is_terminal_flow(*flow) &&
        previous_executable_cannot_fall_through(ops, i) && !has_rewrite_barrier(*pause) &&
        !has_rewrite_barrier(*flow)) {
      const std::optional<std::string> key = terminal_flow_key(*flow);
      if (!key.has_value())
        continue;
      const auto kept_label = separated_pause_tails.find(*key);
      if (kept_label == separated_pause_tails.end()) {
        separated_pause_tails[*key] = run->labels.at(0);
        i = run->next + 1;
        continue;
      }
      for (const std::string& label : run->labels)
        rewrite[label] = kept_label->second;
      remove_range(remove, i, run->next + 1);
      ++applied;
      ++pause_only_applied;
      i = run->next + 1;
    }
  }

  for (int i = 0; i + 7 < static_cast<int>(ops.size()); ++i) {
    if (remove.contains(i))
      continue;
    const IrOp* first_label = op_at(ops, i);
    const IrOp* first_pause = op_at(ops, i + 1);
    const IrOp* first_flow_label = op_at(ops, i + 2);
    const IrOp* first_flow = op_at(ops, i + 3);
    const IrOp* second_label = op_at(ops, i + 4);
    const IrOp* second_pause = op_at(ops, i + 5);
    const IrOp* second_flow_label = op_at(ops, i + 6);
    const IrOp* second_flow = op_at(ops, i + 7);

    if (first_label != nullptr && can_remove_label(*first_label) && first_pause != nullptr &&
        is_pause_like(*first_pause) && first_flow_label != nullptr &&
        can_remove_label(*first_flow_label) && first_flow != nullptr &&
        is_terminal_flow(*first_flow) && second_label != nullptr &&
        can_remove_label(*second_label) && second_pause != nullptr &&
        is_pause_like(*second_pause) && second_flow_label != nullptr &&
        can_remove_label(*second_flow_label) && second_flow != nullptr &&
        is_terminal_flow(*second_flow) &&
        terminal_flow_key(*first_flow) == terminal_flow_key(*second_flow) &&
        !has_rewrite_barrier(*first_pause) && !has_rewrite_barrier(*first_flow) &&
        !has_rewrite_barrier(*second_pause) && !has_rewrite_barrier(*second_flow)) {
      rewrite[first_label->name] = second_label->name;
      rewrite[first_flow_label->name] = second_flow_label->name;
      remove_range(remove, i, i + 3);
      ++applied;
      ++pause_only_applied;
      i += 3;
    }
  }

  for (int i = 0; i + 5 < static_cast<int>(ops.size()); ++i) {
    if (remove.contains(i))
      continue;
    const IrOp* first_label = op_at(ops, i);
    const IrOp* first_pause = op_at(ops, i + 1);
    const IrOp* first_flow = op_at(ops, i + 2);
    const IrOp* second_label = op_at(ops, i + 3);
    const IrOp* second_pause = op_at(ops, i + 4);
    const IrOp* second_flow = op_at(ops, i + 5);

    if (first_label != nullptr && can_remove_label(*first_label) && first_pause != nullptr &&
        is_pause_like(*first_pause) && first_flow != nullptr && is_terminal_flow(*first_flow) &&
        second_label != nullptr && can_remove_label(*second_label) && second_pause != nullptr &&
        is_pause_like(*second_pause) && second_flow != nullptr && is_terminal_flow(*second_flow) &&
        terminal_flow_key(*first_flow) == terminal_flow_key(*second_flow) &&
        !has_rewrite_barrier(*first_pause) && !has_rewrite_barrier(*first_flow) &&
        !has_rewrite_barrier(*second_pause) && !has_rewrite_barrier(*second_flow)) {
      rewrite[first_label->name] = second_label->name;
      remove_range(remove, i, i + 2);
      ++applied;
      ++pause_only_applied;
      i += 2;
    }
  }

  for (int i = 0; i + 8 < static_cast<int>(ops.size()); ++i) {
    if (remove.contains(i))
      continue;
    const IrOp* first_label = op_at(ops, i);
    const IrOp* first_zero = op_at(ops, i + 1);
    const IrOp* first_pause = op_at(ops, i + 2);
    const IrOp* trampoline_label = op_at(ops, i + 3);
    const IrOp* trampoline_jump = op_at(ops, i + 4);
    const IrOp* second_label = op_at(ops, i + 5);
    const IrOp* second_zero = op_at(ops, i + 6);
    const IrOp* second_pause = op_at(ops, i + 7);
    const IrOp* end_label = op_at(ops, i + 8);

    if (first_label != nullptr && first_label->kind == IrKind::Label && first_zero != nullptr &&
        is_zero_digit(*first_zero) && first_pause != nullptr && is_pause_like(*first_pause) &&
        trampoline_label != nullptr && trampoline_label->kind == IrKind::Label &&
        trampoline_jump != nullptr && is_unconditional_jump(*trampoline_jump) &&
        target_is_string(*trampoline_jump) && second_label != nullptr &&
        second_label->kind == IrKind::Label && second_zero != nullptr &&
        is_zero_digit(*second_zero) && second_pause != nullptr && is_pause_like(*second_pause) &&
        end_label != nullptr && end_label->kind == IrKind::Label &&
        string_target(*trampoline_jump) == end_label->name && !has_rewrite_barrier(*first_zero) &&
        !has_rewrite_barrier(*first_pause) && !has_rewrite_barrier(*trampoline_jump) &&
        !has_rewrite_barrier(*second_zero) && !has_rewrite_barrier(*second_pause)) {
      rewrite[first_label->name] = second_label->name;
      rewrite[trampoline_label->name] = end_label->name;
      remove_range(remove, i, i + 4);
      ++applied;
      i += 4;
    }
  }

  if (applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size());
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    if (remove.contains(index))
      continue;
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if ((op.kind == IrKind::Jump || op.kind == IrKind::CondJump || op.kind == IrKind::Call ||
         op.kind == IrKind::Loop) &&
        std::holds_alternative<std::string>(op.target)) {
      const std::string& target = std::get<std::string>(op.target);
      const auto replacement = rewrite.find(target);
      if (replacement != rewrite.end()) {
        IrOp rewritten = op;
        rewritten.target = replacement->second;
        result.push_back(std::move(rewritten));
        continue;
      }
    }
    result.push_back(op);
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "duplicate-failure-tail-merge",
                  .detail = "Merged " + std::to_string(applied) +
                            " duplicate failure tail(s), including " +
                            std::to_string(pause_only_applied) + " pause-only tail(s).",
              },
          },
  };
}

IrPass duplicate_failure_tail_pass() {
  return IrPass{
      .name = "duplicate-failure-tail-merge",
      .run = duplicate_failure_tail,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
