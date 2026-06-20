#include "mkpro/core/passes/store_recall_peephole.hpp"

#include "mkpro/core/passes/recall_removal.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

PassResult store_recall_peephole(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  return run_recall_removal_pass(
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
}

IrPass store_recall_peephole_pass() {
  return IrPass{
      .name = "store-recall-peephole",
      .run = store_recall_peephole,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
