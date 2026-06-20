#include "mkpro/core/passes/last_x_reuse.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/recall_removal.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mkpro::core::passes {

namespace {

std::string loop_counter_register_name(const IrOp& op) {
  if (op.kind != IrKind::Loop)
    return {};
  if (op.counter == "L0")
    return "0";
  if (op.counter == "L1")
    return "1";
  if (op.counter == "L2")
    return "2";
  return "3";
}

bool clobbers_x(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Label:
  case IrKind::OrphanAddress:
  case IrKind::Store:
  case IrKind::IndirectStore:
  case IrKind::Stop:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Loop:
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
    return false;
  case IrKind::Recall:
  case IrKind::IndirectRecall:
    return true;
  case IrKind::Plain:
    return !plain_preserves_x_value(op);
  case IrKind::Return:
  case IrKind::IndirectCondJump:
    return false;
  }
  return false;
}

} // namespace

PassResult last_x_reuse(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  return run_recall_removal_pass(
      ops,
      RecallRemovalReport{
          .name = "last-x-reuse",
          .detail = [](int count) {
            return "Dropped " + std::to_string(count) +
                   " recall(s) whose register value was already in X.";
          },
      },
      [&](RecallRemovalEngine& engine) {
        const std::set<int> label_entries = compute_label_entry_indexes(ops);
        std::optional<std::string> x_holds;
        bool can_trust_value_x = true;

        for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
          const IrOp& op = ops.at(static_cast<std::size_t>(index));
          if (op.kind == IrKind::Label) {
            if (label_entries.contains(index)) {
              x_holds.reset();
              can_trust_value_x = false;
            }
            continue;
          }

          if (const std::optional<std::string> stored = stored_current_x_value_register(op)) {
            x_holds = *stored;
            can_trust_value_x = true;
            continue;
          }

          if (const std::optional<std::string> recall = removable_recall_value_register(op)) {
            if (x_holds == recall || can_trust_value_x) {
              const bool direct_in_x = x_holds == recall;
              const auto removal_plan =
                  engine.plan(index, {.require_value_proof = !direct_in_x});
              const bool value_proves_in_x =
                  removal_plan.has_value() && removal_plan->analysis.value_proof.has_value() &&
                  removal_plan->analysis.value_proof->in_x;
              if ((direct_in_x || value_proves_in_x) && removal_plan.has_value() &&
                  removal_plan->removable) {
                engine.removed().insert(index);
                continue;
              }
            }
            x_holds = *recall;
            can_trust_value_x = true;
            continue;
          }

          if (is_known_return_call_op(op) &&
              x2_known_return_call_preserves_stack_x_and_x2(ops, op,
                                                            engine.direct_return_context())) {
            if (op.kind == IrKind::IndirectCall &&
                !mkpro::core::is_stable_indirect_selector(op.register_name) &&
                x_holds == op.register_name) {
              x_holds.reset();
              can_trust_value_x = false;
            }
            continue;
          }

          if (op.kind == IrKind::Jump || op.kind == IrKind::Call || op.kind == IrKind::IndirectJump ||
              op.kind == IrKind::IndirectCall || op.kind == IrKind::IndirectCondJump ||
              op.kind == IrKind::Return || op.kind == IrKind::Stop) {
            x_holds.reset();
            can_trust_value_x = false;
            continue;
          }

          if (op.kind == IrKind::CondJump) {
            continue;
          }
          if (op.kind == IrKind::Loop) {
            const std::string counter = loop_counter_register_name(op);
            if (x_holds == counter)
              x_holds.reset();
            can_trust_value_x = false;
            continue;
          }

          if (clobbers_x(op)) {
            x_holds.reset();
            can_trust_value_x = true;
          }
        }
      });
}

IrPass last_x_reuse_pass() {
  return IrPass{
      .name = "last-x-reuse",
      .run = last_x_reuse,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
