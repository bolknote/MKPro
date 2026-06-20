#include "mkpro/core/passes/dead_code_after_halt.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

std::set<int> reachable_from_entry(const std::vector<IrOp>& ops) {
  std::map<std::string, int> label_index;
  std::map<int, int> address_index;
  int address = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind == IrKind::Label) {
      label_index[op.name] = static_cast<int>(index);
    } else {
      address_index[address] = static_cast<int>(index);
      address += cells_per_op(op);
    }
  }

  std::set<int> visited;
  std::vector<int> stack;
  std::vector<int> call_returns;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    const int next = static_cast<int>(index + 1U);
    if ((op.kind == IrKind::Call || op.kind == IrKind::IndirectCall) &&
        next < static_cast<int>(ops.size())) {
      call_returns.push_back(next);
    }
  }
  if (!ops.empty())
    stack.push_back(0);

  while (!stack.empty()) {
    const int index = stack.back();
    stack.pop_back();
    if (visited.contains(index))
      continue;
    visited.insert(index);

    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    auto fallthrough = [&]() {
      if (index + 1 < static_cast<int>(ops.size()))
        stack.push_back(index + 1);
    };
    auto target = [&](const IrTarget& flow_target) {
      if (const auto* label = std::get_if<std::string>(&flow_target)) {
        const auto found = label_index.find(*label);
        if (found != label_index.end())
          stack.push_back(found->second);
        return;
      }
      const auto* numeric = std::get_if<int>(&flow_target);
      if (numeric == nullptr)
        return;
      const auto found = address_index.find(*numeric);
      if (found != address_index.end())
        stack.push_back(found->second);
    };
    auto target_address = [&](int flow_target) {
      const auto found = address_index.find(flow_target);
      if (found != address_index.end())
        stack.push_back(found->second);
    };

    switch (op.kind) {
    case IrKind::Label:
    case IrKind::Store:
    case IrKind::Recall:
    case IrKind::IndirectStore:
    case IrKind::IndirectRecall:
    case IrKind::Plain:
    case IrKind::OrphanAddress:
    case IrKind::Stop:
      fallthrough();
      break;
    case IrKind::Return:
      for (const int call_return : call_returns)
        stack.push_back(call_return);
      break;
    case IrKind::Jump:
      target(op.target);
      break;
    case IrKind::CondJump:
    case IrKind::Loop:
      target(op.target);
      fallthrough();
      break;
    case IrKind::Call:
      target(op.target);
      break;
    case IrKind::IndirectJump:
      if (const std::optional<int> known_target = known_indirect_flow_target(op))
        target_address(*known_target);
      break;
    case IrKind::IndirectCall:
      if (const std::optional<int> known_target = known_indirect_flow_target(op))
        target_address(*known_target);
      fallthrough();
      break;
    case IrKind::IndirectCondJump:
      if (const std::optional<int> known_target = known_indirect_flow_target(op))
        target_address(*known_target);
      fallthrough();
      break;
    }
  }

  return visited;
}

} // namespace

PassResult dead_code_after_halt(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  if (ops.empty())
    return PassResult{.ops = {}, .applied = 0, .optimizations = {}};

  const std::set<int> reachable = reachable_from_entry(ops);
  if (reachable.size() == ops.size())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (reachable.contains(static_cast<int>(index)) || op.kind == IrKind::Label) {
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
                  .name = "dead-code-after-halt",
                  .detail = "Removed " + std::to_string(applied) +
                            " unreachable op(s) from the entry CFG.",
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
