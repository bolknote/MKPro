#include "mkpro/core/passes/store_recall_peephole.hpp"

#include "mkpro/core/passes/recall_removal.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"
#include "mkpro/core/passes/outline.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {
namespace {

int schedule_stable_selector_before_operand(std::vector<IrOp>& ops) {
  if (has_numeric_outline_flow_target(ops))
    return 0;
  int applied = 0;
  for (std::size_t index = 0; index + 6U < ops.size(); ++index) {
    const auto& operand = ops[index];
    const auto& selector_value = ops[index + 1U];
    const auto& selector_store = ops[index + 2U];
    const auto& discard_selector = ops[index + 3U];
    const auto& indexed = ops[index + 4U];
    const auto& binary = ops[index + 5U];
    const auto direct_recall = [](const IrOp& op) {
      return op.kind == IrKind::Recall && op.opcode >= 0x60 && op.opcode <= 0x6e;
    };
    if (!direct_recall(operand) || !direct_recall(selector_value) ||
        selector_store.kind != IrKind::Store || selector_store.opcode < 0x47 ||
        selector_store.opcode > 0x4e ||
        operand.register_name == selector_store.register_name ||
        discard_selector.kind != IrKind::Plain || discard_selector.opcode != 0x25 ||
        indexed.kind != IrKind::IndirectRecall || indexed.opcode < 0xd7 ||
        indexed.opcode > 0xde || indexed.register_name != selector_store.register_name ||
        !known_indirect_memory_targets(indexed).has_value() ||
        binary.kind != IrKind::Plain || binary.opcode < 0x37 || binary.opcode > 0x39)
      continue;
    const auto begin = ops.begin() + static_cast<std::ptrdiff_t>(index);
    if (std::any_of(begin, begin + 6, [](const IrOp& op) {
          return has_rewrite_barrier(op) || is_display_focus_sensitive(op) ||
                 !op.meta.roles.empty() || !op.target_meta.roles.empty() ||
                 !op.semantic.empty();
        }))
      continue;

    // Recall operand; recall index; store selector; rotate; indirect recall;
    // bitwise op. Prepare the selector first and swap the two bitwise inputs.
    // Stable R7..RE does not mutate on recall, and cannot alias the operand's
    // source. The bitwise result and memory agree, but the retained stack and
    // last-X values do not: prove they converge before any observation.
    StackValueEqualityState after;
    after.stack_equal = {true, false, false, false};
    after.x1_equal = false;
    after.x2_equal = false;
    if (!prove_ir_stack_entry_equality(ops, index + 6U, after))
      continue;
    const std::array<IrOp, 5> reordered{
        selector_value, selector_store, indexed, operand, binary};
    std::copy(reordered.begin(), reordered.end(), begin);
    ops.erase(begin + 5);
    ++applied;
    index += 4U;
  }
  return applied;
}

} // namespace

PassResult store_recall_peephole(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  PassResult result = run_recall_removal_pass(
      ops,
      RecallRemovalReport{
          .name = "store-recall-peephole",
          .detail = [](int count) {
            return "Dropped " + std::to_string(count) +
                   " redundant П->X immediately after X->П to the same register.";
          },
      },
      [&](RecallRemovalEngine& engine) {
        for (std::size_t index = 0; index < ops.size(); ++index) {
          const IrOp& current = ops.at(index);
          const std::optional<std::string> stored_register =
              stored_current_x_value_register(current);
          if (!stored_register.has_value() || index + 1U >= ops.size())
            continue;

          const std::optional<std::string> recalled_register =
              removable_recall_value_register(ops.at(index + 1U));
          if (!recalled_register.has_value())
            continue;

          RecallRemovalPlanOverrides overrides;
          overrides.require_value_proof = *stored_register != *recalled_register;
          const std::optional<RecallRemovalStackSchedulerPlan> removal_plan =
              engine.plan(static_cast<int>(index + 1U), overrides);
          const bool value_proves_in_x =
              removal_plan.has_value() && removal_plan->analysis.value_proof.has_value() &&
              removal_plan->analysis.value_proof->in_x;
          if ((*stored_register == *recalled_register || value_proves_in_x) &&
              removal_plan.has_value() && removal_plan->removable) {
            engine.removed().insert(static_cast<int>(index + 1U));
          }
        }
      });
  const int scheduled = schedule_stable_selector_before_operand(result.ops);
  if (scheduled > 0) {
    result.applied += scheduled;
    result.optimizations.push_back(AppliedOptimization{
        .name = "stable-indirect-selector-operand-scheduling",
        .detail = "Prepared " + std::to_string(scheduled) +
                  " stable indirect selector(s) before a commutative bitwise operand, "
                  "saving one rotation each; proved memory/result equality and "
                  "Y/Z/T/X1/X2 convergence before observation.",
    });
  }
  return result;
}

IrPass store_recall_peephole_pass() {
  return IrPass{
      .name = "store-recall-peephole",
      .run = store_recall_peephole,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
