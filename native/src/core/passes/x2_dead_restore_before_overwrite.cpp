#include "mkpro/core/passes/x2_dead_restore_before_overwrite.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

constexpr int kDotOpcode = 0x0a;
constexpr int kSignChangeOpcode = 0x0b;
constexpr int kVpOpcode = 0x0c;
constexpr int kCxOpcode = 0x0d;
constexpr int kFPiOpcode = 0x20;
constexpr int kFracOpcode = 0x35;
constexpr int kEmptyKnopOpcode = 0x54;
constexpr int kEmptyK1Opcode = 0x55;
constexpr int kEmptyK2Opcode = 0x56;

bool is_free_standing_plain(const IrOp& op) {
  return op.kind == IrKind::Plain && !op.meta.raw && op.meta.roles.empty();
}

bool is_digit_opcode(int opcode) {
  return opcode >= 0x00 && opcode <= 0x09;
}

bool is_x2_empty_op(const IrOp& op) {
  if (!is_free_standing_plain(op))
    return false;
  return op.opcode == kEmptyKnopOpcode || op.opcode == kEmptyK1Opcode ||
         op.opcode == kEmptyK2Opcode;
}

bool has_numeric_restore_context(const std::vector<IrOp>& ops, int index) {
  if (index <= 0)
    return false;
  const IrOp& previous = ops.at(static_cast<std::size_t>(index - 1));
  return is_free_standing_plain(previous) &&
         (is_digit_opcode(previous.opcode) || previous.opcode == kFracOpcode ||
          previous.opcode == kVpOpcode);
}

bool is_dead_restore_candidate(const std::vector<IrOp>& ops, int index) {
  const IrOp& op = ops.at(static_cast<std::size_t>(index));
  if (!is_free_standing_plain(op))
    return false;
  if (op.opcode == kDotOpcode || op.opcode == kSignChangeOpcode)
    return has_numeric_restore_context(ops, index);
  if (op.opcode == kVpOpcode)
    return has_numeric_restore_context(ops, index);
  if (op.opcode == kFPiOpcode)
    return has_numeric_restore_context(ops, index);
  return false;
}

std::vector<int> removable_restore_run(const std::vector<IrOp>& ops, int start) {
  if (!is_dead_restore_candidate(ops, start))
    return {};

  std::vector<int> removable = {start};
  int cursor = start + 1;
  if (ops.at(static_cast<std::size_t>(start)).opcode == kFPiOpcode &&
      cursor < static_cast<int>(ops.size())) {
    const IrOp& next = ops.at(static_cast<std::size_t>(cursor));
    if (is_free_standing_plain(next) && next.opcode == kSignChangeOpcode) {
      removable.push_back(cursor);
      ++cursor;
    }
  }
  while (cursor < static_cast<int>(ops.size()) && is_x2_empty_op(ops.at(static_cast<std::size_t>(cursor)))) {
    removable.push_back(cursor);
    ++cursor;
  }

  if (cursor >= static_cast<int>(ops.size()))
    return {};
  const IrOp& terminal = ops.at(static_cast<std::size_t>(cursor));
  if (!is_free_standing_plain(terminal) || terminal.opcode != kCxOpcode)
    return {};
  return removable;
}

} // namespace

PassResult x2_dead_restore_before_overwrite(const std::vector<IrOp>& ops,
                                           const PassContext& context) {
  (void)context;
  std::set<int> remove;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    if (remove.contains(index))
      continue;
    const std::vector<int> run = removable_restore_run(ops, index);
    remove.insert(run.begin(), run.end());
  }

  if (remove.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> next;
  next.reserve(ops.size() - remove.size());
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    if (!remove.contains(index))
      next.push_back(ops.at(static_cast<std::size_t>(index)));
  }

  const int applied = static_cast<int>(remove.size());
  return PassResult{
      .ops = std::move(next),
      .applied = applied,
      .optimizations = {AppliedOptimization{
          .name = "x2-dead-restore-before-overwrite",
          .detail = "Removed " + std::to_string(applied) +
                    " dead X/X2 restore/separator cell(s) overwritten before it can be observed.",
      }},
  };
}

IrPass x2_dead_restore_before_overwrite_pass() {
  return IrPass{
      .name = "x2-dead-restore-before-overwrite",
      .run = x2_dead_restore_before_overwrite,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
