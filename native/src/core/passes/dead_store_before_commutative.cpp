#include "mkpro/core/passes/dead_store_before_commutative.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
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
  return op.kind == IrKind::Plain &&
         (op.opcode == 0x10 || op.opcode == 0x12 || op.opcode == 0x37 ||
          op.opcode == 0x38 || op.opcode == 0x39);
}

bool program_observes_last_x(const std::vector<IrOp>& ops) {
  return std::any_of(ops.begin(), ops.end(), [](const IrOp& op) {
    return op.kind == IrKind::Plain && op.opcode == 0x0f;
  });
}

using StackDependencies = std::array<unsigned, 4>;

StackDependencies lift_dependencies(const StackDependencies& input,
                                    unsigned fresh_x) {
  return {fresh_x, input.at(0), input.at(1), input.at(2)};
}

bool transfer_pure_value_dependencies(StackDependencies& state,
                                      const IrOp& op) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  if (op.kind == IrKind::Recall) {
    state = lift_dependencies(state, 0U);
    return true;
  }
  if (op.kind != IrKind::Plain ||
      (op.opcode >= 0x00 && op.opcode <= 0x0c) || op.opcode == 0x0f)
    return false;

  const StackDependencies input = state;
  switch (analyze_x2_stack_effect(op).stack_effect) {
  case StackEffect::Preserves:
    // Cx and K random are nullary producers. Other supported preserving
    // commands are unary transforms of the current X value.
    state.at(0) = (op.opcode == 0x0d || op.opcode == 0x3b) ? 0U
                                                           : input.at(0);
    return true;
  case StackEffect::Shifts:
    state = lift_dependencies(input, input.at(0));
    return true;
  case StackEffect::ConsumeYDrop:
    state = {input.at(0) | input.at(1), input.at(2), input.at(3),
             input.at(3)};
    return true;
  case StackEffect::ConsumeYKeep:
    state = {input.at(0) | input.at(1), input.at(1), input.at(2),
             input.at(3)};
    return true;
  case StackEffect::Exposes:
    state = {input.at(1), input.at(2), input.at(3), 0U};
    return true;
  case StackEffect::Barrier:
  case StackEffect::Unknown:
    return false;
  }
  return false;
}

bool direct_call_is_pure_independent_stack_lift(
    const std::vector<IrOp>& ops, const IrOp& call,
    const DirectReturnAnalysisContext& return_context) {
  if (call.kind != IrKind::Call || has_rewrite_barrier(call) ||
      is_display_focus_sensitive(call)) {
    return false;
  }
  const std::optional<int> target =
      direct_call_target_index(call, return_context);
  if (!target.has_value())
    return false;

  StackDependencies state{1U, 2U, 4U, 8U};
  int cursor = *target;
  if (cursor < 0 || cursor >= static_cast<int>(ops.size()))
    return false;
  if (ops.at(static_cast<std::size_t>(cursor)).kind == IrKind::Label)
    ++cursor;
  for (; cursor < static_cast<int>(ops.size()); ++cursor) {
    const IrOp& op = ops.at(static_cast<std::size_t>(cursor));
    if (op.kind == IrKind::Label) {
      if (return_context.label_entries.contains(cursor))
        return false;
      continue;
    }
    if (op.kind == IrKind::Return) {
      return state.at(0) == 0U;
    }
    if (!transfer_pure_value_dependencies(state, op))
      return false;
  }
  return false;
}

struct CommutativeProducerShape {
  std::string key;
  bool canonical = false;
};

std::optional<CommutativeProducerShape> commutative_producer_shape(
    const std::vector<IrOp>& ops, std::size_t index,
    const DirectReturnAnalysisContext& return_context) {
  if (index + 3U >= ops.size())
    return std::nullopt;
  const IrOp& first = ops.at(index);
  const IrOp& second = ops.at(index + 1U);
  const IrOp& join = ops.at(index + 2U);
  const IrOp& store = ops.at(index + 3U);
  const bool canonical = first.kind == IrKind::Recall &&
                         second.kind == IrKind::Call;
  const IrOp& recall = canonical ? first : second;
  const IrOp& call = canonical ? second : first;
  const X2StackEffectAnalysis join_effect = analyze_x2_stack_effect(join);
  const bool candidate_kinds = recall.kind == IrKind::Recall &&
                               call.kind == IrKind::Call &&
                               is_commutative_alu(join) &&
                               store.kind == IrKind::Store;
  const bool pure_lift =
      candidate_kinds && direct_call_is_pure_independent_stack_lift(
                             ops, call, return_context);
  const bool stack_projection_safe =
      canonical ||
      !removing_stack_lift_can_expose_stack(ops,
                                            static_cast<int>(index + 3U));
  const bool x2_projection_safe =
      join_effect.x2_affects ||
      !x2_sync_can_expose_context_sensitive_restore(
          ops, static_cast<int>(index + 3U), {});
  if (recall.kind != IrKind::Recall || call.kind != IrKind::Call ||
      !is_commutative_alu(join) || store.kind != IrKind::Store ||
      !stack_projection_safe || !x2_projection_safe ||
      (join_effect.stack_effect != StackEffect::ConsumeYDrop &&
       join_effect.stack_effect != StackEffect::ConsumeYKeep) ||
      has_rewrite_barrier(recall) || has_rewrite_barrier(join) ||
      has_rewrite_barrier(store) ||
      first.procedure_name != second.procedure_name ||
      first.procedure_name != join.procedure_name ||
      first.procedure_name != store.procedure_name ||
      first.hidden != second.hidden || first.hidden != join.hidden ||
      first.hidden != store.hidden ||
      !pure_lift) {
    return std::nullopt;
  }
  const auto* target = std::get_if<std::string>(&call.target);
  if (target == nullptr)
    return std::nullopt;
  return CommutativeProducerShape{
      .key = recall.register_name + "|" + *target + "|" +
             std::to_string(join.opcode) + "|" + store.register_name,
      .canonical = canonical,
  };
}

int canonicalize_duplicate_commutative_producers(std::vector<IrOp>& ops) {
  if (program_observes_last_x(ops))
    return 0;
  const DirectReturnAnalysisContext return_context =
      direct_return_analysis_context(ops);
  std::map<std::string, int> canonical_counts;
  for (std::size_t index = 0; index + 3U < ops.size(); ++index) {
    const std::optional<CommutativeProducerShape> shape =
        commutative_producer_shape(ops, index, return_context);
    if (shape.has_value() && shape->canonical)
      ++canonical_counts[shape->key];
  }

  int canonicalized = 0;
  for (std::size_t index = 0; index + 3U < ops.size(); ++index) {
    const std::optional<CommutativeProducerShape> shape =
        commutative_producer_shape(ops, index, return_context);
    if (!shape.has_value() || shape->canonical ||
        canonical_counts[shape->key] == 0) {
      continue;
    }
    std::swap(ops.at(index), ops.at(index + 1U));
    ++canonicalized;
    index += 3U;
  }
  return canonicalized;
}

} // namespace

PassResult dead_store_before_commutative(const std::vector<IrOp>& ops,
                                         const PassContext& context) {
  (void)context;

  std::vector<IrOp> canonical_ops = ops;
  const int canonicalized =
      canonicalize_duplicate_commutative_producers(canonical_ops);

  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;

  for (std::size_t index = 0; index < canonical_ops.size(); ++index) {
    const IrOp& current = canonical_ops.at(index);
    const IrOp* next = index + 1U < canonical_ops.size()
                           ? &canonical_ops.at(index + 1U)
                           : nullptr;
    const IrOp* after = index + 2U < canonical_ops.size()
                            ? &canonical_ops.at(index + 2U)
                            : nullptr;
    if (current.kind == IrKind::Store && next != nullptr && next->kind == IrKind::Recall &&
        after != nullptr && is_commutative_alu(*after) && !has_rewrite_barrier(current) &&
        !has_rewrite_barrier(*next) && !has_rewrite_barrier(*after) &&
        !register_read_before_next_write(canonical_ops,
                                         static_cast<int>(index + 3U),
                                         current.register_name)) {
      ++applied;
      continue;
    }
    result.push_back(current);
  }

  std::vector<AppliedOptimization> optimizations;
  if (canonicalized > 0) {
    optimizations.push_back(AppliedOptimization{
        .name = "commutative-producer-order-canonicalization",
        .detail = "Canonicalized " + std::to_string(canonicalized) +
                  " proof-equivalent commutative producer region(s) to "
                  "expose shared continuations.",
    });
  }
  if (applied > 0) {
    optimizations.push_back(AppliedOptimization{
        .name = "dead-temp-store",
        .detail = "Removed " + std::to_string(applied) +
                  " temp store(s) whose X value was consumed directly by "
                  "stack scheduling.",
    });
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied + canonicalized,
      .optimizations = std::move(optimizations),
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
