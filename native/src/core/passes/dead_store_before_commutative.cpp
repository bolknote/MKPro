#include "mkpro/core/passes/dead_store_before_commutative.hpp"

#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

bool register_read_before_next_write(const std::vector<IrOp>& ops, int start,
                                     const std::string& register_name) {
  for (int index = start; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Recall && op.register_name == register_name)
      return true;
    if (op.kind == IrKind::Store && op.register_name == register_name)
      return false;
    if (op.kind != IrKind::Label && op.kind != IrKind::Plain &&
        op.kind != IrKind::OrphanAddress) {
      return true;
    }
  }
  return false;
}

bool is_commutative_alu(const IrOp& op) {
  return op.kind == IrKind::Plain && (op.opcode == 0x10 || op.opcode == 0x12);
}

} // namespace

PassResult dead_store_before_commutative(const std::vector<IrOp>& ops,
                                         const PassContext& context) {
  (void)context;

  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& current = ops.at(index);
    const IrOp* next = index + 1U < ops.size() ? &ops.at(index + 1U) : nullptr;
    const IrOp* after = index + 2U < ops.size() ? &ops.at(index + 2U) : nullptr;
    if (current.kind == IrKind::Store && next != nullptr && next->kind == IrKind::Recall &&
        after != nullptr && is_commutative_alu(*after) && !has_rewrite_barrier(current) &&
        !has_rewrite_barrier(*next) && !has_rewrite_barrier(*after) &&
        !register_read_before_next_write(ops, static_cast<int>(index + 3U),
                                         current.register_name)) {
      ++applied;
      continue;
    }
    result.push_back(current);
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          applied > 0
              ? std::vector<AppliedOptimization>{
                    AppliedOptimization{
                        .name = "dead-temp-store",
                        .detail = "Removed " + std::to_string(applied) +
                                  " temp store(s) whose X value was consumed directly by stack scheduling.",
                    },
                }
              : std::vector<AppliedOptimization>{},
  };
}

IrPass dead_store_before_commutative_pass() {
  return IrPass{
      .name = "dead-temp-store",
      .run = dead_store_before_commutative,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
