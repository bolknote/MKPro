#include "mkpro/core/passes/tail_call.hpp"

#include "mkpro/core/opcodes.hpp"

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
  std::set<int> returns;
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

// A return belongs to the current frame, not to the nearest preceding
// procedure label. Follow jumps into shared tails, but skip nested callees:
// their returns consume a different frame. Assuming a nested call can return
// is conservative, including for recursion and non-returning calls.
std::optional<std::set<int>> frame_returns(
    const std::vector<IrOp>& ops, int entry, const std::map<std::string, int>& labels) {
  std::set<int> visited;
  std::set<int> returns;
  std::vector<int> pending{entry};
  const auto target_index = [&](const IrTarget& target) -> std::optional<int> {
    const auto* name = std::get_if<std::string>(&target);
    if (name == nullptr)
      return std::nullopt;
    const auto found = labels.find(*name);
    return found == labels.end() ? std::nullopt : std::optional(found->second);
  };
  while (!pending.empty()) {
    const int index = pending.back();
    pending.pop_back();
    if (index < 0 || index >= static_cast<int>(ops.size()))
      return std::nullopt;
    if (!visited.insert(index).second)
      continue;
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (has_rewrite_barrier(op))
      return std::nullopt;
    if (op.kind == IrKind::Return) {
      returns.insert(index);
      continue;
    }
    if (op.meta.stop_disposition == StopDisposition::Terminal)
      continue;
    if (op.kind == IrKind::Stop &&
        op.meta.stop_disposition != StopDisposition::Resumable)
      return std::nullopt;
    if (op.kind == IrKind::Plain && op.opcode == 0x29)
      return std::nullopt; // resumable errors need a separate padding/PC proof
    if (op.kind == IrKind::Jump || op.kind == IrKind::CondJump ||
        op.kind == IrKind::Loop) {
      const auto target = target_index(op.target);
      if (!target.has_value())
        return std::nullopt;
      pending.push_back(*target);
      if (op.kind == IrKind::Jump)
        continue;
    }
    pending.push_back(index + 1);
  }
  return returns;
}

std::map<std::string, TailJumpTarget> find_tail_jump_targets(const std::vector<IrOp>& ops) {
  if (ops.empty())
    return {};
  const std::map<std::string, int> labels = build_label_indexes(ops);
  std::set<std::string> unique_labels;
  std::map<std::string, std::vector<std::optional<IrTarget>>> calls;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    // Materialized addresses and unknown entries need final-layout transport,
    // not a symbolic interprocedural rewrite.
    if (has_rewrite_barrier(op) || op.target_meta.formal_opcode.has_value() ||
        op.kind == IrKind::OrphanAddress || op.kind == IrKind::IndirectCall ||
        op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCondJump)
      return {};
    if (op.kind == IrKind::Label && !unique_labels.insert(op.name).second)
      return {};
    if (op.kind == IrKind::Jump || op.kind == IrKind::CondJump ||
        op.kind == IrKind::Loop || op.kind == IrKind::Call) {
      const auto target = string_target(op.target);
      if (!target.has_value() || !labels.contains(*target))
        return {};
      if (op.kind == IrKind::Call) {
        const auto continuation = call_continuation(ops, index);
        auto& normalized = calls[*target].emplace_back();
        if (continuation.has_value())
          normalized.emplace(normalize_continuation(ops, labels, *continuation));
      }
    }
  }

  const auto main_returns = frame_returns(ops, 0, labels);
  if (!main_returns.has_value())
    return {};
  std::map<int, std::set<std::string>> owners;
  std::map<std::string, TailJumpTarget> candidates;
  for (const auto& [target, continuations] : calls) {
    const auto returns = frame_returns(ops, labels.at(target), labels);
    if (!returns.has_value())
      return {};
    for (const int returned : *returns)
      owners[returned].insert(target);
    if (returns->empty() || continuations.empty() || !continuations.front().has_value())
      continue;
    const IrTarget first = *continuations.front();
    bool same = true;
    for (const auto& continuation : continuations)
      if (!continuation.has_value() || *continuation != first)
        same = false;
    if (same) {
      // An immediate return is already handled by ordinary tail-call lowering.
      // Specializing the callee to jump to that return would add a cell and
      // compose two alternative rewrites of the same caller frame.
      const auto continuation_label = string_target(first);
      if (continuation_label.has_value()) {
        const auto continuation_entry =
            next_executable_index(ops, labels.at(*continuation_label) + 1);
        if (continuation_entry.has_value() &&
            ops.at(static_cast<std::size_t>(*continuation_entry)).kind == IrKind::Return)
          continue;
      }
      candidates.emplace(target, TailJumpTarget{.continuation = first, .returns = *returns});
    }
  }

  // A shared return may be rewritten only as one transaction over every
  // owning callee. Prune to a fixed point: removing one owner can invalidate
  // another. An empty-stack/main entry never acquires a fabricated caller.
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto candidate = candidates.begin(); candidate != candidates.end();) {
      bool safe = true;
      for (const int returned : candidate->second.returns) {
        if (main_returns->contains(returned)) {
          safe = false;
          break;
        }
        for (const auto& owner : owners.at(returned)) {
          const auto other = candidates.find(owner);
          if (other == candidates.end() ||
              other->second.continuation != candidate->second.continuation) {
            safe = false;
            break;
          }
        }
        if (!safe)
          break;
      }
      if (!safe) {
        candidate = candidates.erase(candidate);
        changed = true;
      } else {
        ++candidate;
      }
    }
  }
  return candidates;
}

std::map<int, IrTarget> collect_return_continuations(
    const std::vector<IrOp>& ops, const std::map<std::string, TailJumpTarget>& targets) {
  (void)ops;
  std::map<int, IrTarget> result;
  for (const auto& [name, target] : targets) {
    (void)name;
    for (const int returned : target.returns)
      result.emplace(returned, target.continuation);
  }
  return result;
}

std::set<std::string> collect_return_labels(
    const std::vector<IrOp>& ops, const std::map<int, IrTarget>& return_continuations) {
  std::set<std::string> result;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind != IrKind::Label || has_rewrite_barrier(op))
      continue;
    const std::optional<int> next = next_executable_index(ops, index + 1);
    if (next.has_value() && !return_continuations.contains(*next) &&
        !has_rewrite_barrier(ops.at(static_cast<std::size_t>(*next))) &&
        ops.at(static_cast<std::size_t>(*next)).kind == IrKind::Return)
      result.insert(op.name);
  }
  return result;
}



bool is_return_label(const IrTarget& target, const std::set<std::string>& return_labels) {
  const std::optional<std::string> label = string_target(target);
  return label.has_value() && return_labels.contains(*label);
}

std::string tail_call_detail(int applied, int tail_jump_count) {
  return
      tail_jump_count == 0
          ? "Replaced " + std::to_string(applied) + " subroutine tail call" +
                (applied == 1 ? "" : "s") + " with direct jump(s)."
          : "Replaced " + std::to_string(applied) + " subroutine tail operation" +
                (applied == 1 ? "" : "s") + " with direct jump continuation" +
                (tail_jump_count == 1 ? "" : "s") + ".";

}

IrOp jump_from_return(const IrOp& op, IrTarget continuation) {
  IrOp out;
  out.kind = IrKind::Jump;
  out.target = std::move(continuation);
  out.opcode = 0x51;
  out.meta = op.meta;
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
  const bool indirect = op.kind == IrKind::IndirectCall;
  out.kind = indirect ? IrKind::IndirectJump : IrKind::Jump;
  out.target = op.target;
  out.register_name = op.register_name;
  out.opcode = indirect ? 0x80 + register_index(op.register_name) : 0x51;
  out.meta = op.meta;
  out.meta.mnemonic = indirect ? "К БП " + op.register_name : "БП";
  out.meta.comment = replace_comment_prefix(op.meta.comment, "proc call", replacement, fallback);
  if (out.meta.comment == op.meta.comment) {
    out.meta.comment =
        replace_comment_prefix(op.meta.comment, "call function", replacement, std::move(fallback));
  }
  constexpr std::string_view kChargeEntryCall = "callee-hole charge-entry call; ";
  constexpr std::string_view kChargeEntryTailTransfer =
      "callee-hole charge-entry tail transfer; ";
  if (out.meta.comment.has_value() && starts_with(*out.meta.comment, kChargeEntryCall)) {
    out.meta.comment->replace(0, kChargeEntryCall.size(), kChargeEntryTailTransfer);
  }
  out.target_meta = op.target_meta;
  out.target_meta.comment =
      replace_comment_prefix(out.target_meta.comment, "call function", "proc call", "proc call");
  return out;
}



} // namespace

PassResult tail_call_lowering(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  const std::map<std::string, TailJumpTarget> tail_jump_targets = find_tail_jump_targets(ops);
  const std::map<int, IrTarget> return_continuations =
      collect_return_continuations(ops, tail_jump_targets);
  const std::set<std::string> return_labels = collect_return_labels(ops, return_continuations);
  const std::map<std::string, int> label_indexes = build_label_indexes(ops);


  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;


  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label || has_rewrite_barrier(op)) {

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

    if (op.kind == IrKind::Call || op.kind == IrKind::IndirectCall) {
      const std::optional<int> continuation_index = next_executable_index(ops, index + 1);
      const IrOp* continuation =
          continuation_index.has_value()
              ? &ops.at(static_cast<std::size_t>(*continuation_index))
              : nullptr;
      const bool continuation_is_immediate =
          continuation_index.has_value() && *continuation_index == index + 1;

      if (continuation != nullptr && !has_rewrite_barrier(*continuation) &&
          continuation->kind == IrKind::Jump &&
          is_return_label(continuation->target, return_labels)) {
        result.push_back(jump_from_call(op, "tail call", "tail call"));
        if (continuation_is_immediate)
          ++index;
        ++applied;
        continue;
      }

      // A shared-continuation transaction has already claimed this return.
      // Its caller still needs a frame: deleting call+return here would erase
      // the planned continuation and turn the nested return into an empty one.
      if (continuation != nullptr && !has_rewrite_barrier(*continuation) &&
          continuation->kind == IrKind::Return &&
          !return_continuations.contains(*continuation_index)) {
        result.push_back(jump_from_call(op, "tail call", "tail call"));
        if (continuation_is_immediate)
          ++index;
        ++applied;
        continue;
      }

      // The remaining analyses depend on a statically named direct callee.
      // The immediate return cases above are target-agnostic and are the only
      // proof-valid late lowering required for an indirect call.
      if (op.kind == IrKind::IndirectCall) {
        result.push_back(op);
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

      // A main-loop continuation at 00 is not an empty-stack return:
      // ROM resumes an empty-stack return at 01. Preserve the call frame
      // and explicit continuation here. Only final-layout proofs may use
      // the separately verified one-cell empty-return continuation.

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
                  .detail = tail_call_detail(applied, tail_jump_count),
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
