#include "mkpro/core/passes/conditional_branch_trampoline.hpp"

#include "mkpro/core/passes/outline.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

constexpr std::string_view kLabelPrefix = "__conditional_branch_trampoline_";

struct BranchTrampolinePlan {
  int branch_index = 0;
  int target_index = 0;
};

bool same_target(const IrTarget& left, const IrTarget& right) {
  return left == right;
}

std::optional<int> find_later_equivalent_conditional(const std::vector<IrOp>& ops, int branch_index,
                                                     const IrOp& branch) {
  for (int index = branch_index + 1; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& candidate = ops.at(static_cast<std::size_t>(index));
    if (candidate.kind == IrKind::Label)
      continue;
    if (candidate.kind == IrKind::CondJump && candidate.condition == branch.condition &&
        candidate.opcode == branch.opcode && same_target(candidate.target, branch.target) &&
        !has_rewrite_barrier(candidate)) {
      return index;
    }
  }
  return std::nullopt;
}

std::vector<BranchTrampolinePlan> collect_plans(const std::vector<IrOp>& ops) {
  std::vector<BranchTrampolinePlan> plans;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& branch = ops.at(static_cast<std::size_t>(index));
    if (branch.kind != IrKind::CondJump || !std::holds_alternative<std::string>(branch.target) ||
        has_rewrite_barrier(branch)) {
      continue;
    }
    const std::optional<int> target = find_later_equivalent_conditional(ops, index, branch);
    if (target.has_value())
      plans.push_back(BranchTrampolinePlan{.branch_index = index, .target_index = *target});
  }
  return plans;
}

std::optional<std::string> trampoline_comment(const std::optional<std::string>& comment) {
  if (!comment.has_value())
    return "conditional branch trampoline";
  if (comment->starts_with("false branch")) {
    return std::string("conditional branch trampoline") +
           comment->substr(std::string("false branch").size());
  }
  return *comment;
}

} // namespace

PassResult conditional_branch_trampoline(const std::vector<IrOp>& ops,
                                         const PassContext& context) {
  if (!context.options.conditional_branch_trampoline)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const std::vector<BranchTrampolinePlan> plans = collect_plans(ops);
  if (plans.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  LabelAllocator labels(ops, std::string(kLabelPrefix));
  std::map<int, std::string> label_by_target_index;
  for (const BranchTrampolinePlan& plan : plans) {
    if (!label_by_target_index.contains(plan.target_index)) {
      label_by_target_index[plan.target_index] = labels.next();
    }
  }

  std::map<int, std::string> retarget_by_index;
  for (const BranchTrampolinePlan& plan : plans)
    retarget_by_index[plan.branch_index] = label_by_target_index.at(plan.target_index);

  std::vector<IrOp> result;
  result.reserve(ops.size() + label_by_target_index.size());
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const auto entry_label = label_by_target_index.find(index);
    if (entry_label != label_by_target_index.end()) {
      IrOp label;
      label.kind = IrKind::Label;
      label.name = entry_label->second;
      label.hidden = true;
      result.push_back(std::move(label));
    }

    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    const auto retarget = retarget_by_index.find(index);
    if (retarget != retarget_by_index.end() && op.kind == IrKind::CondJump) {
      IrOp retargeted = op;
      retargeted.target = retarget->second;
      retargeted.target_meta.comment = "conditional branch trampoline";
      retargeted.meta.comment = trampoline_comment(op.meta.comment);
      result.push_back(std::move(retargeted));
      continue;
    }
    result.push_back(op);
  }

  const int applied = static_cast<int>(plans.size());
  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "conditional-branch-trampoline",
                  .detail = "Retargeted " + std::to_string(applied) + " conditional branch" +
                            (applied == 1 ? "" : "es") +
                            " through a later identical conditional with the same destination.",
              },
          },
  };
}

IrPass conditional_branch_trampoline_pass() {
  return IrPass{
      .name = "conditional-branch-trampoline",
      .run = conditional_branch_trampoline,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
