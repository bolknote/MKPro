#include "mkpro/core/passes/vp_splice.hpp"

#include "mkpro/core/passes/helpers.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace mkpro::core::passes {

namespace {

constexpr int kVpOpcode = 0x0c;
constexpr int kSignChangeOpcode = 0x0b;
constexpr int kEmptyFirstOpcode = 0x54;
constexpr int kEmptyLastOpcode = 0x56;

bool has_roles(const IrOp& op) {
  return !op.meta.roles.empty();
}

bool is_free_standing_plain(const IrOp& op) {
  return op.kind == IrKind::Plain && !has_rewrite_barrier(op) && !is_display_focus_sensitive(op) &&
         !has_roles(op);
}

bool is_vp(const IrOp& op) {
  return is_free_standing_plain(op) && op.opcode == kVpOpcode;
}

bool is_empty_vp_separator(const IrOp& op) {
  return is_free_standing_plain(op) && op.opcode >= kEmptyFirstOpcode && op.opcode <= kEmptyLastOpcode;
}

bool is_sign_pair_candidate(const IrOp& left, const IrOp& right) {
  return is_free_standing_plain(left) && is_free_standing_plain(right) &&
         left.opcode == kSignChangeOpcode && right.opcode == kSignChangeOpcode;
}

bool is_digit(const IrOp& op) {
  return is_free_standing_plain(op) && op.opcode >= 0 && op.opcode <= 9;
}

} // namespace

PassResult vp_splice(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  if (ops.empty()) {
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
  }

  std::vector<bool> remove(ops.size(), false);
  int applied = 0;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& current = ops.at(index);
    if (!is_free_standing_plain(current))
      continue;

    if (is_vp(current) && index + 1U < ops.size() && is_vp(ops.at(index + 1U))) {
      remove.at(index + 1U) = true;
      ++applied;
      continue;
    }

    if (is_empty_vp_separator(current) && index + 1U < ops.size() && is_vp(ops.at(index + 1U))) {
      remove.at(index) = true;
      ++applied;
      continue;
    }

    if (is_vp(current) && index + 1U < ops.size() && is_empty_vp_separator(ops.at(index + 1U))) {
      remove.at(index + 1U) = true;
      ++applied;
      continue;
    }

    if (index + 1U < ops.size() && is_sign_pair_candidate(current, ops.at(index + 1U))) {
      remove.at(index + 1U) = true;
      ++applied;
      continue;
    }

    if (index > 0 && index + 1U < ops.size() && is_sign_pair_candidate(ops.at(index - 1U), current) &&
        is_digit(ops.at(index + 1U))) {
      remove.at(index) = true;
      ++applied;
    }
  }

  if (applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size() - static_cast<std::size_t>(applied));
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (!remove.at(index))
      result.push_back(ops.at(index));
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations = {
          AppliedOptimization{
              .name = "vp-exponent-splice",
              .detail = "Collapsed " + std::to_string(applied) +
                        " redundant VP/empty/sign cell(s) in restore context.",
          },
      },
  };
}

IrPass vp_splice_pass() {
  return IrPass{
      .name = "vp-splice",
      .run = vp_splice,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
