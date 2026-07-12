#include "mkpro/core/passes/jump_thread.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

std::map<std::string, std::size_t> jump_thread_label_indexes(const std::vector<IrOp>& ops) {
  std::map<std::string, std::size_t> labels;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind == IrKind::Label)
      labels[op.name] = index;
  }
  return labels;
}

struct ThreadedTarget {
  std::string label;
  std::vector<std::uint64_t> semantic_call_origins;
};

std::optional<ThreadedTarget> follow_label(const std::vector<IrOp>& ops,
                                           const std::map<std::string, std::size_t>& labels,
                                           std::string start) {
  std::string current = std::move(start);
  std::vector<std::uint64_t> origins;
  std::set<std::string> seen;
  while (!seen.contains(current)) {
    seen.insert(current);
    const auto label = labels.find(current);
    if (label == labels.end())
      return ThreadedTarget{.label = std::move(current),
                            .semantic_call_origins = std::move(origins)};

    std::size_t cursor = label->second + 1U;
    while (cursor < ops.size() && ops.at(cursor).kind == IrKind::Label)
      ++cursor;
    if (cursor >= ops.size())
      return ThreadedTarget{.label = std::move(current),
                            .semantic_call_origins = std::move(origins)};

    const IrOp& next = ops.at(cursor);
    if (next.kind != IrKind::Jump)
      return ThreadedTarget{.label = std::move(current),
                            .semantic_call_origins = std::move(origins)};
    const auto* target = std::get_if<std::string>(&next.target);
    if (target == nullptr || has_rewrite_barrier(next))
      return ThreadedTarget{.label = std::move(current),
                            .semantic_call_origins = std::move(origins)};
    origins.insert(origins.end(), next.meta.semantic_call_origins.begin(),
                   next.meta.semantic_call_origins.end());
    current = *target;
  }
  return ThreadedTarget{.label = std::move(current), .semantic_call_origins = std::move(origins)};
}

} // namespace

PassResult jump_thread(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  const std::map<std::string, std::size_t> labels = jump_thread_label_indexes(ops);
  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;

  for (const IrOp& op : ops) {
    const auto* target = std::get_if<std::string>(&op.target);
    if ((op.kind == IrKind::Jump || op.kind == IrKind::CondJump || op.kind == IrKind::Call) &&
        target != nullptr && !has_rewrite_barrier(op)) {
      const std::optional<ThreadedTarget> final = follow_label(ops, labels, *target);
      if (final.has_value() && final->label != *target) {
        IrOp threaded = op;
        threaded.target = final->label;
        threaded.meta.semantic_call_origins.insert(threaded.meta.semantic_call_origins.end(),
                                                   final->semantic_call_origins.begin(),
                                                   final->semantic_call_origins.end());
        std::sort(threaded.meta.semantic_call_origins.begin(),
                  threaded.meta.semantic_call_origins.end());
        threaded.meta.semantic_call_origins.erase(
            std::unique(threaded.meta.semantic_call_origins.begin(),
                        threaded.meta.semantic_call_origins.end()),
            threaded.meta.semantic_call_origins.end());
        result.push_back(std::move(threaded));
        ++applied;
        continue;
      }
    }
    result.push_back(op);
  }

  if (applied == 0)
    return PassResult{.ops = std::move(result), .applied = 0, .optimizations = {}};

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "jump-thread",
                  .detail = "Threaded " + std::to_string(applied) +
                            " direct control transfer(s) through trampoline labels to the final "
                            "target.",
              },
          },
  };
}

IrPass jump_thread_pass() {
  return IrPass{
      .name = "jump-thread",
      .run = jump_thread,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
