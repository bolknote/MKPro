#include "mkpro/core/passes/arithmetic_if.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

bool is_flow_op(const IrOp& op) {
  return op.kind == IrKind::Jump || op.kind == IrKind::CondJump || op.kind == IrKind::Call ||
         op.kind == IrKind::Loop;
}

std::map<std::string, int> count_label_refs(const std::vector<IrOp>& ops) {
  std::map<std::string, int> refs;
  for (const IrOp& op : ops) {
    if (!is_flow_op(op))
      continue;
    if (const auto* target = std::get_if<std::string>(&op.target))
      refs[*target] += 1;
  }
  return refs;
}

std::optional<std::size_t> find_next_flow_op(const std::vector<IrOp>& ops, std::size_t start) {
  for (std::size_t index = start; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind == IrKind::Label)
      return std::nullopt;
    if (is_flow_op(op))
      return index;
  }
  return std::nullopt;
}

std::optional<std::size_t> find_label(const std::vector<IrOp>& ops, const std::string& name,
                                      std::size_t start) {
  for (std::size_t index = start; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind == IrKind::Label && op.name == name)
      return index;
  }
  return std::nullopt;
}

bool is_pure_linear_op(const IrOp& op) {
  if (has_rewrite_barrier(op))
    return false;
  return op.kind == IrKind::Plain || op.kind == IrKind::Store || op.kind == IrKind::Recall ||
         op.kind == IrKind::Stop;
}

bool is_pure_linear_block(const std::vector<IrOp>& ops) {
  return !ops.empty() &&
         std::all_of(ops.begin(), ops.end(), [](const IrOp& op) { return is_pure_linear_op(op); });
}

bool ops_equivalent(const std::vector<IrOp>& left, const std::vector<IrOp>& right) {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    const IrOp& a = left.at(index);
    const IrOp& b = right.at(index);
    if (a.kind != b.kind)
      return false;
    if (a.opcode != b.opcode)
      return false;
    if (a.kind == IrKind::Store && a.register_name != b.register_name)
      return false;
    if (a.kind == IrKind::Recall && a.register_name != b.register_name)
      return false;
    if (a.kind == IrKind::Stop && a.semantic != b.semantic)
      return false;
  }
  return true;
}

std::vector<IrOp> slice_ops(const std::vector<IrOp>& ops, std::size_t start, std::size_t end) {
  return std::vector<IrOp>(ops.begin() + static_cast<std::ptrdiff_t>(start),
                           ops.begin() + static_cast<std::ptrdiff_t>(end));
}

} // namespace

PassResult arithmetic_if(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  const std::map<std::string, int> label_refs = count_label_refs(ops);
  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    const auto* false_target = std::get_if<std::string>(&op.target);
    if (op.kind != IrKind::CondJump || false_target == nullptr || has_rewrite_barrier(op)) {
      result.push_back(op);
      continue;
    }

    const std::optional<std::size_t> then_jump_index = find_next_flow_op(ops, index + 1U);
    if (!then_jump_index.has_value()) {
      result.push_back(op);
      continue;
    }

    const IrOp& then_jump = ops.at(*then_jump_index);
    const auto* end_target = std::get_if<std::string>(&then_jump.target);
    if (then_jump.kind != IrKind::Jump || end_target == nullptr) {
      result.push_back(op);
      continue;
    }

    const std::size_t false_label_index = *then_jump_index + 1U;
    if (false_label_index >= ops.size()) {
      result.push_back(op);
      continue;
    }
    const IrOp& false_label = ops.at(false_label_index);
    if (false_label.kind != IrKind::Label || false_label.name != *false_target) {
      result.push_back(op);
      continue;
    }

    const std::optional<std::size_t> end_label_index =
        find_label(ops, *end_target, false_label_index + 1U);
    const auto false_ref_count = label_refs.find(*false_target);
    if (!end_label_index.has_value() || false_ref_count == label_refs.end() ||
        false_ref_count->second != 1) {
      result.push_back(op);
      continue;
    }

    const std::vector<IrOp> then_ops = slice_ops(ops, index + 1U, *then_jump_index);
    const std::vector<IrOp> else_ops = slice_ops(ops, false_label_index + 1U, *end_label_index);
    if (!is_pure_linear_block(then_ops) || !is_pure_linear_block(else_ops) ||
        !ops_equivalent(then_ops, else_ops)) {
      result.push_back(op);
      continue;
    }

    result.insert(result.end(), then_ops.begin(), then_ops.end());
    index = *end_label_index - 1U;
    ++applied;
  }

  if (applied == 0)
    return PassResult{.ops = std::move(result), .applied = 0, .optimizations = {}};

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          std::vector<AppliedOptimization>{
              AppliedOptimization{
                  .name = "arithmetic-if-pass",
                  .detail = "Collapsed " + std::to_string(applied) +
                            " conditional block(s) whose simplified branches were byte-identical.",
              },
          },
  };
}

IrPass arithmetic_if_pass() {
  return IrPass{
      .name = "arithmetic-if-pass",
      .run = arithmetic_if,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
