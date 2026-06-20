#include "mkpro/core/passes/shared_call_tail.hpp"

#include "mkpro/core/passes/outline.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

struct SharedCallTail {
  std::string key;
  std::string label;
  IrOp call;
  IrOp continuation;
  int count = 0;
};

std::map<std::string, int> build_label_indexes(const std::vector<IrOp>& ops) {
  std::map<std::string, int> label_indexes;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label)
      label_indexes[op.name] = index;
  }
  return label_indexes;
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

std::string call_tail_key(const IrOp& call, const IrOp& continuation,
                          const std::vector<IrOp>& ops,
                          const std::map<std::string, int>& label_indexes) {
  return target_key(call.target) + "|" +
         target_key(normalize_continuation(ops, label_indexes, continuation.target));
}

std::map<std::string, SharedCallTail> collect_shared_call_tails(const std::vector<IrOp>& ops) {
  const std::map<std::string, int> label_indexes = build_label_indexes(ops);
  struct CountedTail {
    IrOp call;
    IrOp continuation;
    int count = 0;
  };
  std::map<std::string, CountedTail> counts;

  for (int index = 0; index < static_cast<int>(ops.size()) - 1; ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    const IrOp& next = ops.at(static_cast<std::size_t>(index + 1));
    if (op.kind != IrKind::Call || next.kind != IrKind::Jump)
      continue;
    if (has_rewrite_barrier(op) || has_rewrite_barrier(next))
      continue;

    IrOp continuation = next;
    continuation.target = normalize_continuation(ops, label_indexes, next.target);
    const std::string key = call_tail_key(op, continuation, ops, label_indexes);
    auto found = counts.find(key);
    if (found == counts.end()) {
      counts[key] = CountedTail{.call = op, .continuation = continuation, .count = 1};
    } else {
      found->second.count += 1;
    }
  }

  std::map<std::string, SharedCallTail> result;
  LabelAllocator labels(ops, "__shared_call_tail_");
  for (const auto& [key, candidate] : counts) {
    if (candidate.count < 3)
      continue;
    result[key] = SharedCallTail{
        .key = key,
        .label = labels.next(),
        .call = candidate.call,
        .continuation = candidate.continuation,
        .count = candidate.count,
    };
  }
  return result;
}

std::optional<std::string> shared_call_comment(const std::optional<std::string>& comment) {
  if (!comment.has_value())
    return "shared call tail";
  if (comment->starts_with("proc call")) {
    return std::string("shared call tail") + comment->substr(std::string("proc call").size());
  }
  return *comment;
}

} // namespace

PassResult shared_call_tail(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  const std::map<std::string, SharedCallTail> candidates = collect_shared_call_tails(ops);
  const std::map<std::string, int> label_indexes = build_label_indexes(ops);
  if (candidates.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;

  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    const IrOp* next =
        index + 1 < static_cast<int>(ops.size()) ? &ops.at(static_cast<std::size_t>(index + 1))
                                                 : nullptr;
    if (op.kind == IrKind::Call && next != nullptr && next->kind == IrKind::Jump) {
      const std::string key = call_tail_key(op, *next, ops, label_indexes);
      const auto candidate = candidates.find(key);
      if (candidate != candidates.end()) {
        IrOp replacement;
        replacement.kind = IrKind::Jump;
        replacement.target = candidate->second.label;
        replacement.opcode = 0x51;
        replacement.meta = op.meta;
        replacement.meta.mnemonic = "БП";
        replacement.meta.comment = shared_call_comment(op.meta.comment);
        replacement.target_meta.comment = "shared call tail";
        result.push_back(std::move(replacement));
        ++index;
        ++applied;
        continue;
      }
    }
    result.push_back(op);
  }

  for (const auto& [unused_key, candidate] : candidates) {
    (void)unused_key;
    IrOp label;
    label.kind = IrKind::Label;
    label.name = candidate.label;
    result.push_back(std::move(label));

    IrOp call = candidate.call;
    if (!call.meta.comment.has_value())
      call.meta.comment = "shared call tail helper";
    result.push_back(std::move(call));

    IrOp continuation = candidate.continuation;
    if (!continuation.meta.comment.has_value())
      continuation.meta.comment = "shared call tail continuation";
    result.push_back(std::move(continuation));
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "shared-call-tail",
                  .detail = "Shared " + std::to_string(applied) + " call+jump tail sequence" +
                            (applied == 1 ? "" : "s") + ".",
              },
          },
  };
}

IrPass shared_call_tail_pass() {
  return IrPass{
      .name = "shared-call-tail",
      .run = shared_call_tail,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
