#include "mkpro/core/passes/return_zero_jump.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

PassResult return_zero_jump(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  const bool uses_call = std::any_of(ops.begin(), ops.end(), [](const IrOp& op) {
    return op.kind == IrKind::Call || op.kind == IrKind::IndirectCall;
  });
  if (uses_call)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const std::map<std::string, int> labels = calculate_label_addresses(ops);
  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;
  int current_address = 0;

  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label) {
      result.push_back(op);
      continue;
    }

    if (op.kind == IrKind::Jump && !has_rewrite_barrier(op)) {
      const std::optional<int> resolved = target_address(op.target, labels);
      const bool targets_backward =
          std::holds_alternative<int>(op.target) ||
          (resolved.has_value() && *resolved < current_address);
      if (resolved.has_value() && *resolved == 1 && targets_backward) {
        IrOp ret;
        ret.kind = IrKind::Return;
        ret.opcode = 0x52;
        ret.meta.mnemonic = "В/О";
        ret.meta.comment = "optimized БП 01";
        result.push_back(std::move(ret));
        ++applied;
        current_address += 1;
        continue;
      }
    }

    result.push_back(op);
    current_address += cells_per_op(op);
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          applied > 0
              ? std::vector<AppliedOptimization>{
                    AppliedOptimization{
                        .name = "return-zero-jump",
                        .detail = "Replaced " + std::to_string(applied) +
                                  " БП 01 sequence with В/О under empty-return-stack assumption.",
                    },
                }
              : std::vector<AppliedOptimization>{},
  };
}

IrPass return_zero_jump_pass() {
  return IrPass{
      .name = "return-zero-jump",
      .run = return_zero_jump,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
