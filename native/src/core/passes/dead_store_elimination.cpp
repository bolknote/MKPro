#include "mkpro/core/passes/dead_store_elimination.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

bool is_number_entry(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode <= 0x0c;
}

bool leaves_entry_open(const IrOp& op) {
  if (is_number_entry(op))
    return true;
  return op.kind == IrKind::Stop && op.semantic == "input";
}

const IrOp* previous_effective_op(const std::vector<IrOp>& ops, int index) {
  for (int cursor = index - 1; cursor >= 0; --cursor) {
    const IrOp& op = ops.at(static_cast<std::size_t>(cursor));
    if (op.kind != IrKind::Label)
      return &op;
  }
  return nullptr;
}

const IrOp* next_effective_op(const std::vector<IrOp>& ops, int index) {
  for (int cursor = index + 1; cursor < static_cast<int>(ops.size()); ++cursor) {
    const IrOp& op = ops.at(static_cast<std::size_t>(cursor));
    if (op.kind != IrKind::Label)
      return &op;
  }
  return nullptr;
}

bool finalizes_number_entry(const std::vector<IrOp>& ops, int index) {
  const IrOp* previous = previous_effective_op(ops, index);
  const IrOp* next = next_effective_op(ops, index);
  return previous != nullptr && next != nullptr && leaves_entry_open(*previous) &&
         is_number_entry(*next);
}

bool is_vp_op(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode == 0x0c;
}

bool provides_vp_restore_context(const std::vector<IrOp>& ops, int index) {
  const IrOp* next = next_effective_op(ops, index);
  return next != nullptr && is_vp_op(*next);
}

std::optional<std::string> dead_store_target(const IrOp& op) {
  if (op.kind == IrKind::Store)
    return op.register_name;
  if (op.kind != IrKind::IndirectStore)
    return std::nullopt;
  if (!mkpro::core::is_stable_indirect_selector(op.register_name))
    return std::nullopt;
  return known_indirect_memory_target(op);
}

} // namespace

PassResult dead_store_elimination(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  if (ops.empty())
    return PassResult{.ops = {}, .applied = 0, .optimizations = {}};

  const LivenessInfo liveness = compute_liveness(ops);
  std::set<int> removed;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    const std::optional<std::string> target = dead_store_target(op);
    if (!target.has_value())
      continue;
    if (has_rewrite_barrier(op))
      continue;
    if (liveness.live_out.at(index).contains(*target))
      continue;
    if (finalizes_number_entry(ops, static_cast<int>(index)))
      continue;
    if (provides_vp_restore_context(ops, static_cast<int>(index)))
      continue;
    removed.insert(static_cast<int>(index));
  }

  if (removed.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size() - removed.size());
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (!removed.contains(static_cast<int>(index)))
      result.push_back(ops.at(index));
  }

  return PassResult{
      .ops = std::move(result),
      .applied = static_cast<int>(removed.size()),
      .optimizations =
          {
              AppliedOptimization{
                  .name = "dead-store-elimination",
                  .detail = "Removed " + std::to_string(removed.size()) +
                            " store(s) to register(s) never read before the next assignment.",
              },
          },
  };
}

IrPass dead_store_elimination_pass() {
  return IrPass{
      .name = "dead-store-elimination",
      .run = dead_store_elimination,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
