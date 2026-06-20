#include "mkpro/core/passes/vp_x2_peephole.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

namespace {

bool has_roles(const IrOp& op) {
  return !op.meta.roles.empty();
}

bool is_free_standing_plain(const IrOp& op) {
  return op.kind == IrKind::Plain && !has_rewrite_barrier(op) && !is_display_focus_sensitive(op) &&
         !has_roles(op);
}

std::string lower_ascii(std::string_view value) {
  std::string lowered(value);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return lowered;
}

bool is_vp_x2_boundary(const IrOp& op) {
  if (!is_free_standing_plain(op) || op.opcode != 0x0c)
    return false;
  if (!op.meta.comment.has_value() && !op.meta.tactic.has_value())
    return false;
  const std::string details = lower_ascii(
      op.meta.comment.value_or(std::string()) + ";" + op.meta.tactic.value_or(std::string()));
  return details.find("вп") != std::string::npos || details.find("vp") != std::string::npos ||
         details.find("x2") != std::string::npos || details.find("display") != std::string::npos;
}

bool is_fraction_restore(const IrOp& op) {
  return is_free_standing_plain(op) && op.opcode == 0x0a;
}

bool is_noop_unary_restore(const IrOp& op) {
  return is_free_standing_plain(op) && (op.opcode == 0x31 || op.opcode == 0x32 ||
                                        op.opcode == 0x34 || op.opcode == 0x35);
}

} // namespace

PassResult vp_x2_peephole(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  std::vector<bool> remove(ops.size(), false);
  int applied = 0;

  for (std::size_t index = 0; index + 1 < ops.size(); ++index) {
    const IrOp& boundary = ops.at(index);
    if (!is_vp_x2_boundary(boundary))
      continue;

    const std::size_t candidate = index + 1;
    const IrOp& next = ops.at(candidate);
    if (!is_free_standing_plain(next))
      continue;
    if (is_fraction_restore(next) || is_noop_unary_restore(next)) {
      remove.at(candidate) = true;
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
          AppliedOptimization{.name = "vp-fraction-restore",
                             .detail = "Removed " + std::to_string(applied) +
                                       " redundant fraction/unary restore op(s) after a VП/X2 boundary."},
      },
  };
}

IrPass vp_x2_peephole_pass() {
  return IrPass{
      .name = "vp-x2-peephole",
      .run = vp_x2_peephole,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
