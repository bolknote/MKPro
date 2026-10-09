#include "mkpro/core/passes/tail_branch_inversion.hpp"

#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

struct InvertedCondition {
  std::string condition;
  int opcode = 0;
};

std::optional<InvertedCondition> inverted_condition(const std::string& condition) {
  if (condition == "==0")
    return InvertedCondition{.condition = "!=0", .opcode = 0x57};
  if (condition == "!=0")
    return InvertedCondition{.condition = "==0", .opcode = 0x5e};
  if (condition == "<0")
    return InvertedCondition{.condition = ">=0", .opcode = 0x59};
  if (condition == ">=0")
    return InvertedCondition{.condition = "<0", .opcode = 0x5c};
  return std::nullopt;
}

std::map<std::string, int> count_label_refs(const std::vector<IrOp>& ops) {
  std::map<std::string, int> refs;
  for (const IrOp& op : ops) {
    if (op.meta.indirect_flow_targets)
      for (const auto& target : *op.meta.indirect_flow_targets)
        if (const auto* label = std::get_if<std::string>(&target))
          refs[*label] += 1;
    if (op.kind != IrKind::Jump && op.kind != IrKind::CondJump && op.kind != IrKind::Call &&
        op.kind != IrKind::Loop) {
      continue;
    }
    if (const auto* target = std::get_if<std::string>(&op.target))
      refs[*target] += 1;
  }
  return refs;
}

} // namespace

PassResult tail_branch_inversion(const std::vector<IrOp>& ops, const PassContext& context) {
  if (!context.options.tail_branch_inversion)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const auto input = lower_ir_to_machine(ops);
  PostLayoutControlFlowOptions flow_options;
  flow_options.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(context.options));
  const auto input_flow = build_post_layout_control_flow(input, flow_options);
  if (!input_flow.proved)
    return PassResult{.ops = ops};
  std::vector<std::size_t> old_machine;
  std::size_t machine_index = 0;
  for (const auto& op : ops) {
    old_machine.push_back(machine_index);
    machine_index += op.kind == IrKind::Label ? 1U
        : static_cast<std::size_t>(cells_per_op(op));
  }
  std::vector<std::optional<std::size_t>> ir_mapping(ops.size());
  PostLayoutExecutionRelocationOptions transport;
  transport.allow_bypassed_direct_jumps = true;
  const std::map<std::string, int> refs = count_label_refs(ops);
  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;

  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    ir_mapping[static_cast<std::size_t>(index)] = result.size();
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    const auto* target = std::get_if<std::string>(&op.target);
    if (op.kind != IrKind::CondJump || target == nullptr || has_rewrite_barrier(op)) {
      result.push_back(op);
      continue;
    }

    const std::optional<int> jump_index = next_executable_index(ops, index + 1);
    if (!jump_index.has_value() || *jump_index != index + 1) {
      result.push_back(op);
      continue;
    }
    const IrOp& jump = ops.at(static_cast<std::size_t>(*jump_index));
    if (jump.kind != IrKind::Jump || has_rewrite_barrier(jump)) {
      result.push_back(op);
      continue;
    }

    const int label_index = *jump_index + 1;
    if (label_index >= static_cast<int>(ops.size())) {
      result.push_back(op);
      continue;
    }
    const IrOp& label = ops.at(static_cast<std::size_t>(label_index));
    const auto ref_count = refs.find(label.name);
    if (label.kind != IrKind::Label || label.name != *target || ref_count == refs.end() ||
        ref_count->second != 1) {
      result.push_back(op);
      continue;
    }

    const std::optional<InvertedCondition> inverted = inverted_condition(op.condition);
    if (!inverted.has_value() ||
        !prove_post_layout_direct_conditional_inversion_effects(
            input, input_flow, old_machine[static_cast<std::size_t>(index)])) {
      result.push_back(op);
      continue;
    }

    IrOp inverted_op = op;
    inverted_op.condition = inverted->condition;
    inverted_op.target = jump.target;
    inverted_op.opcode = inverted->opcode;
    inverted_op.meta.mnemonic = opcode_by_code(inverted->opcode).name;
    if (op.meta.comment == "case mismatch") {
      inverted_op.meta.comment = op.meta.comment;
    } else {
      inverted_op.meta.comment =
          op.meta.comment.has_value() && op.meta.comment->starts_with("false branch")
              ? std::optional<std::string>{std::string("direct tail branch") +
                                           op.meta.comment->substr(std::string("false branch").size())}
              : std::optional<std::string>{"direct tail branch"};
    }
    inverted_op.target_meta = jump.target_meta;
    result.push_back(std::move(inverted_op));
    transport.inverted_direct_conditional_items.push_back(
        old_machine[static_cast<std::size_t>(index)]);
    // Direct and indirect references were counted; this label is now unused.
    index = label_index;
    ++applied;
  }

  if (applied == 0)
    return PassResult{.ops = std::move(result), .applied = 0, .optimizations = {}};

  const auto output = lower_ir_to_machine(result);
  std::vector<std::size_t> new_machine;
  machine_index = 0;
  for (const auto& op : result) {
    new_machine.push_back(machine_index);
    machine_index += op.kind == IrKind::Label ? 1U
        : static_cast<std::size_t>(cells_per_op(op));
  }
  std::vector<std::optional<std::size_t>> mapping(input.size());
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (!ir_mapping[i]) continue;
    const auto count = ops[i].kind == IrKind::Label ? 1U
        : static_cast<std::size_t>(cells_per_op(ops[i]));
    for (std::size_t cell = 0; cell < count; ++cell)
      mapping[old_machine[i] + cell] = new_machine[*ir_mapping[i]] + cell;
  }
  if (!prove_post_layout_execution_relocation(
          input, output, input_flow, build_post_layout_control_flow(output, flow_options),
          mapping, transport).proved)
    return PassResult{.ops = ops};

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "tail-branch-inversion",
                  .detail = "Inverted " + std::to_string(applied) + " branch" +
                            (applied == 1 ? "" : "es") +
                            " whose then-path was only a tail jump.",
              },
          },
  };
}

IrPass tail_branch_inversion_pass() {
  return IrPass{
      .name = "tail-branch-inversion",
      .run = tail_branch_inversion,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
