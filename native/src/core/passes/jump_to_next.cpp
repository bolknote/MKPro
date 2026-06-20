#include "mkpro/core/passes/jump_to_next.hpp"

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

PassResult jump_to_next_threading(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& current = ops.at(index);
    const auto* target = std::get_if<std::string>(&current.target);
    if (current.kind == IrKind::Jump && target != nullptr && !has_rewrite_barrier(current)) {
      std::size_t cursor = index + 1U;
      bool threaded = false;
      while (cursor < ops.size() && ops.at(cursor).kind == IrKind::Label) {
        const IrOp& label = ops.at(cursor);
        if (label.name == *target) {
          threaded = true;
          break;
        }
        ++cursor;
      }
      if (threaded) {
        ++applied;
        continue;
      }
    }
    result.push_back(current);
  }

  PassResult pass_result;
  pass_result.ops = std::move(result);
  pass_result.applied = applied;
  if (applied > 0) {
    pass_result.optimizations.push_back(AppliedOptimization{
        .name = "jump-to-next-threading",
        .detail = "Removed " + std::to_string(applied) +
                  " unconditional branch to the immediately following label.",
    });
  }
  return pass_result;
}

IrPass jump_to_next_threading_pass() {
  return IrPass{
      .name = "jump-to-next-threading",
      .run = jump_to_next_threading,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
