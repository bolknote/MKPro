#include "mkpro/core/passes/constant_folding.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

std::optional<int> digit_of(const IrOp& op) {
  if (op.kind == IrKind::Plain && op.opcode >= 0x00 && op.opcode <= 0x09)
    return op.opcode;
  return std::nullopt;
}

std::optional<std::string> alu_opcode(const IrOp& op) {
  if (op.kind != IrKind::Plain)
    return std::nullopt;
  if (op.opcode == 0x10)
    return "+";
  if (op.opcode == 0x11)
    return "-";
  if (op.opcode == 0x12)
    return "*";
  if (op.opcode == 0x13)
    return "/";
  return std::nullopt;
}

bool is_identity_plus(const IrOp& op, const IrOp* previous) {
  if (previous == nullptr)
    return false;
  const std::optional<int> digit = digit_of(*previous);
  if (!digit.has_value() || *digit != 0)
    return false;
  return alu_opcode(op) == "+";
}

bool is_identity_mul(const IrOp& op, const IrOp* previous) {
  if (previous == nullptr)
    return false;
  const std::optional<int> digit = digit_of(*previous);
  if (!digit.has_value() || *digit != 1)
    return false;
  return alu_opcode(op) == "*";
}

} // namespace

PassResult constant_folding(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;

  for (const IrOp& op : ops) {
    const IrOp* previous = result.empty() ? nullptr : &result.back();
    if ((is_identity_plus(op, previous) || is_identity_mul(op, previous)) && previous != nullptr &&
        previous->kind == IrKind::Plain && !previous->meta.raw && op.kind == IrKind::Plain &&
        !op.meta.raw) {
      result.pop_back();
      ++applied;
      continue;
    }
    result.push_back(op);
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          applied > 0
              ? std::vector<AppliedOptimization>{
                    AppliedOptimization{
                        .name = "constant-folding",
                        .detail = "Dropped " + std::to_string(applied) +
                                  " identity arithmetic operation(s) (0+ or 1*).",
                    },
                }
              : std::vector<AppliedOptimization>{},
  };
}

IrPass constant_folding_pass() {
  return IrPass{
      .name = "constant-folding",
      .run = constant_folding,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
