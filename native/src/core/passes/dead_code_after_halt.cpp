#include "mkpro/core/passes/dead_code_after_halt.hpp"

#include "mkpro/core/passes/cfg.hpp"

#include <set>
#include <vector>

namespace mkpro::core::passes {

namespace {

std::set<int> reachable_from_entry(const std::vector<IrOp>& ops) {
  const ControlFlowGraph graph =
      build_control_flow_graph(ops, BuildCfgOptions{.indirect_call_fallthrough = true});
  std::set<int> visited;
  std::vector<int> stack;
  if (!ops.empty())
    stack.push_back(0);

  while (!stack.empty()) {
    const int index = stack.back();
    stack.pop_back();
    if (visited.contains(index))
      continue;
    visited.insert(index);
    for (const CfgEdge& edge : graph.edges.at(static_cast<std::size_t>(index)))
      stack.push_back(edge.target);
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
