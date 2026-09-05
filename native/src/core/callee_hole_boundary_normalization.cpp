#include "mkpro/core/callee_hole_boundary_normalization.hpp"

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/passes/cfg.hpp"
#include "mkpro/core/passes/return_suffix_gadget.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace mkpro::core {
namespace {

std::optional<std::size_t> executable(const std::vector<IrOp>& ops, std::size_t index) {
  while (index < ops.size() && ops[index].kind == IrKind::Label)
    ++index;
  return index < ops.size() ? std::optional<std::size_t>(index) : std::nullopt;
}

std::optional<std::map<std::string, std::size_t>>
label_entries(const std::vector<IrOp>& ops) {
  std::map<std::string, std::size_t> result;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].kind != IrKind::Label)
      continue;
    const auto entry = executable(ops, i + 1);
    // End-of-program boundary labels need not designate an instruction.
    if (!result.emplace(ops[i].name, entry.value_or(ops.size())).second)
      return std::nullopt;
  }
  return result;
}

std::optional<std::size_t> symbolic_entry(
    const IrTarget& target, const std::map<std::string, std::size_t>& entries,
    std::size_t size) {
  const auto* label = std::get_if<std::string>(&target);
  if (label == nullptr)
    return std::nullopt;
  const auto found = entries.find(*label);
  return found != entries.end() && found->second < size
             ? std::optional<std::size_t>(found->second)
             : std::nullopt;
}

bool equality_prefix(const StackEntryProofReader& reader, std::size_t entry,
                     StackValueEqualityState state, unsigned depth) {
  if (depth > 5)
    return false;
  bool number_entry = false;
  std::set<std::tuple<std::size_t, int, bool>> seen;
  for (unsigned count = 0; count < 128; ++count) {
    if (stack_values_fully_equal(state))
      return true;
    if (!seen.emplace(entry, stack_value_equality_key(state), number_entry).second)
      return false;
    const auto node = reader(entry);
    if (!node.has_value() || node->barrier)
      return false;
    if (node->call) {
      if (node->call_targets.empty())
        return false;
      if (opcode_by_code(node->opcode).x2_effect == X2Effect::Affects)
        state.x2_equal = state.stack_equal[0];
      return std::all_of(node->call_targets.begin(), node->call_targets.end(),
                         [&](std::size_t target) {
                           return equality_prefix(reader, target, state, depth + 1);
                         });
    }
    StackValueEqualityTransfer transfer;
    if (node->kind == StackValueEqualityStepKind::Plain &&
        node->opcode >= 0 && node->opcode <= 9) {
      transfer = transfer_decimal_digit_equality(state, number_entry);
      number_entry = true;
    } else {
      transfer = transfer_stack_value_equality(state, node->opcode, node->kind);
      number_entry = false;
    }
    if (transfer == StackValueEqualityTransfer::Rejected)
      return false;
    if (stack_values_fully_equal(state))
      return true;
    if (!node->next.has_value())
      return false;
    entry = *node->next;
  }
  return false;
}

bool wrapper_op(const IrOp& op) {
  if (passes::has_rewrite_barrier(op) || op.target_meta.formal_opcode.has_value() ||
      !op.target_meta.roles.empty() || !op.semantic.empty() ||
      op.meta.indirect_flow_targets.has_value())
    return false;
  for (const auto& role : op.meta.roles) {
    if (op.kind != IrKind::Call ||
        (role != "statement-proc-call" && role != "x-argument-call"))
      return false;
  }
  if (op.kind == IrKind::Recall)
    return true;
  if (op.kind == IrKind::Call || op.kind == IrKind::Jump)
    return std::holds_alternative<std::string>(op.target);
  if (op.kind != IrKind::Plain)
    return false;
  const auto& info = opcode_by_code(op.opcode);
  return op.opcode > 9 && op.opcode != 0x2a && !info.takes_address &&
         info.stack_effect != StackEffect::Barrier &&
         info.stack_effect != StackEffect::Unknown &&
         info.x2_effect != X2Effect::Restores && info.x2_effect != X2Effect::Unknown;
}

} // namespace

bool prove_stack_entry_equality(const StackEntryProofReader& reader,
                                std::size_t entry, StackValueEqualityState state) {
  return equality_prefix(reader, entry, state, 0);
}

StackValueEqualityState xyz_preserving_selector_charge_state() {
  StackValueEqualityState state;
  state.stack_equal = {true, true, true, false};
  state.x1_equal = false;
  state.x2_equal = false;
  return state;
}

bool prove_ir_stack_entry_equality(const std::vector<IrOp>& ops,
                                   std::size_t entry, StackValueEqualityState state) {
  const auto labels = label_entries(ops);
  const auto start = executable(ops, entry);
  if (!labels.has_value() || !start.has_value())
    return false;
  return prove_stack_entry_equality(
      [&](std::size_t index) -> std::optional<StackEntryProofNode> {
        if (index >= ops.size())
          return std::nullopt;
        const IrOp& op = ops[index];
        StackEntryProofNode node;
        node.opcode = op.opcode;
        node.next = executable(ops, index + 1);
        node.barrier = passes::has_rewrite_barrier(op);
        if (op.kind == IrKind::Plain)
          node.kind = StackValueEqualityStepKind::Plain;
        else if (op.kind == IrKind::Recall)
          node.kind = StackValueEqualityStepKind::Recall;
        else if (op.kind == IrKind::Store)
          node.kind = StackValueEqualityStepKind::Store;
        else if (op.kind == IrKind::Call) {
          node.call = true;
          if (!op.target_meta.formal_opcode.has_value()) {
            const auto target = symbolic_entry(op.target, *labels, ops.size());
            if (target.has_value())
              node.call_targets.push_back(*target);
          }
        }
        return node;
      }, *start, state);
}

CalleeHoleBoundaryNormalization
normalize_callee_hole_boundaries(const std::vector<IrOp>& ops) {
  CalleeHoleBoundaryNormalization result{.ops = ops};
  const auto entries = label_entries(ops);
  if (!entries.has_value())
    return result;
  std::map<std::string, std::vector<IrOp>> wrappers;
  for (const auto& [label, start] : *entries) {
    std::vector<IrOp> body;
    int cells = 0;
    for (std::size_t i = start; i < ops.size() && cells <= 8; ++i) {
      const IrOp& op = ops[i];
      if (!wrapper_op(op))
        break;
      if (op.kind == IrKind::Jump) {
        if (body.empty() || std::get<std::string>(op.target) == label)
          break;
        IrOp call = op;
        call.kind = IrKind::Call;
        call.opcode = 0x53;
        call.meta.mnemonic = "PP";
        call.meta.comment = "exposed symbolic tail-call boundary";
        body.push_back(std::move(call));
        wrappers.emplace(label, std::move(body));
        break;
      }
      body.push_back(op);
      cells += passes::cells_per_op(op);
    }
  }
  std::vector<IrOp> expanded;
  std::set<std::string> expanded_labels;
  for (const IrOp& op : ops) {
    const auto* label = std::get_if<std::string>(&op.target);
    if (op.kind != IrKind::Call || label == nullptr || !wrapper_op(op) ||
        !wrappers.contains(*label)) {
      expanded.push_back(op);
      continue;
    }
    auto body = wrappers.at(*label);
    auto& origins = body.back().meta.semantic_call_origins;
    origins.insert(origins.end(), op.meta.semantic_call_origins.begin(),
                    op.meta.semantic_call_origins.end());
    std::sort(origins.begin(), origins.end());
    origins.erase(std::unique(origins.begin(), origins.end()), origins.end());
    expanded.insert(expanded.end(), body.begin(), body.end());
    expanded_labels.insert(*label);
    ++result.expanded_calls;
  }
  std::set<std::string> referenced;
  for (const IrOp& op : expanded) {
    if (op.kind == IrKind::Call || op.kind == IrKind::Jump ||
        op.kind == IrKind::CondJump || op.kind == IrKind::Loop) {
      if (const auto* label = std::get_if<std::string>(&op.target))
        referenced.insert(*label);
    }
    for (const auto& target : op.meta.indirect_flow_targets.value_or(std::vector<IrTarget>{}))
      if (const auto* label = std::get_if<std::string>(&target))
        referenced.insert(*label);
  }
  std::erase_if(expanded, [&](const IrOp& op) {
    return op.kind == IrKind::Label && expanded_labels.contains(op.name) &&
           !referenced.contains(op.name) && !op.procedure_boundary.has_value() &&
           !passes::has_rewrite_barrier(op) && op.meta.roles.empty();
  });
  result.ops = passes::canonicalize_outlining_arithmetic_tails(
      expanded, result.arithmetic_groups, [&](std::size_t continuation) {
        StackValueEqualityState equality;
        equality.stack_equal = {true, true, true, true};
        equality.x1_equal = false;
        equality.x2_equal = false;
        return prove_ir_stack_entry_equality(expanded, continuation, equality);
      });

  // A terminal jump and a call followed by return have the same continuation.
  // Expose that equality before matching regions; otherwise their last leaf
  // is excluded from the shared skeleton. Only straight-line symbolic leaves
  // qualify, and the speculative growth is repaid by the enclosing pass.
  const auto tail_entries = label_entries(result.ops);
  std::vector<IrOp> canonical;
  for (const IrOp& op : result.ops) {
    bool leaf_return = false;
    if (tail_entries.has_value() && op.kind == IrKind::Jump && wrapper_op(op)) {
      const auto start = symbolic_entry(op.target, *tail_entries, result.ops.size());
      if (start.has_value()) {
        for (std::size_t i = *start; i < result.ops.size() && i - *start < 32; ++i) {
          const IrOp& leaf = result.ops[i];
          if (passes::has_rewrite_barrier(leaf))
            break;
          if (leaf.kind == IrKind::Label)
            continue;
          if (leaf.kind == IrKind::Return) {
            leaf_return = true;
            break;
          }
          if (leaf.kind != IrKind::Plain && leaf.kind != IrKind::Recall &&
              leaf.kind != IrKind::Store)
            break;
        }
      }
    }
    if (!leaf_return) {
      canonical.push_back(op);
      continue;
    }
    IrOp call = op;
    call.kind = IrKind::Call;
    call.opcode = 0x53;
    call.meta.mnemonic = "PP";
    canonical.push_back(std::move(call));
    IrOp continuation;
    continuation.kind = IrKind::Return;
    continuation.opcode = 0x52;
    continuation.meta.mnemonic = "B/O";
    canonical.push_back(std::move(continuation));
    ++result.expanded_calls;
  }
  result.ops = std::move(canonical);
  return result;
}

bool callee_hole_return_stack_fits(const std::vector<IrOp>& ops) {
  const auto labels = label_entries(ops);
  const auto entry = executable(ops, 0);
  if (!labels.has_value() || !entry.has_value())
    return false;
  using State = std::pair<std::size_t, std::vector<std::size_t>>;
  std::vector<State> pending{{*entry, {}}};
  std::set<State> seen;
  while (!pending.empty()) {
    State current = std::move(pending.back());
    pending.pop_back();
    if (!seen.insert(current).second)
      continue;
    if (seen.size() > 10000)
      return false;
    const auto [index, returns] = std::move(current);
    if (index >= ops.size())
      return false;
    const IrOp& op = ops[index];
    // A preserved manual-input annotation constrains rewrites, not control
    // flow: its typed instruction still has the same return-stack effect.
    // Raw instructions, including raw calls, must instead fail closed.
    if (op.meta.raw || op.kind == IrKind::OrphanAddress)
      return false;
    const auto next = executable(ops, index + 1);
    const auto push_next = [&] {
      if (!next.has_value()) return false;
      pending.emplace_back(*next, returns);
      return true;
    };
    const bool call = op.kind == IrKind::Call || op.kind == IrKind::IndirectCall;
    const bool direct = op.kind == IrKind::Call || op.kind == IrKind::Jump ||
                        op.kind == IrKind::CondJump || op.kind == IrKind::Loop;
    const bool indirect = op.kind == IrKind::IndirectCall ||
                          op.kind == IrKind::IndirectJump ||
                          op.kind == IrKind::IndirectCondJump;
    if (direct || indirect) {
      std::vector<IrTarget> targets = indirect
          ? op.meta.indirect_flow_targets.value_or(std::vector<IrTarget>{})
          : std::vector<IrTarget>{op.target};
      if (targets.empty() || op.target_meta.formal_opcode.has_value())
        return false;
      auto nested = returns;
      if (call) {
        if (nested.size() >= 5 || !next.has_value())
          return false;
        nested.push_back(*next);
      }
      for (const auto& target : targets) {
        const auto destination = symbolic_entry(target, *labels, ops.size());
        if (!destination.has_value())
          return false;
        pending.emplace_back(*destination, nested);
      }
      if (op.kind == IrKind::CondJump || op.kind == IrKind::Loop ||
          op.kind == IrKind::IndirectCondJump) {
        if (!push_next()) return false;
      }
    } else if (op.kind == IrKind::Return) {
      if (returns.empty())
        return false;
      auto nested = returns;
      const auto destination = nested.back();
      nested.pop_back();
      pending.emplace_back(destination, std::move(nested));
    } else if (op.kind == IrKind::Stop && op.meta.stop_disposition == StopDisposition::Terminal) {
      continue;
    } else {
      if (!push_next()) return false;
    }
  }
  return true;
}

bool selector_charge_entry_closer_opcode(int opcode) {
  return opcode == 0x53 || (opcode >= 0x40 && opcode <= 0x4e) ||
         (opcode >= 0xa7 && opcode <= 0xae);
}

bool selector_charge_has_automatic_entry_lift(const std::vector<IrOp>& ops,
                                             std::size_t entry) {
  if (entry == 0 || entry >= ops.size())
    return false;
  const auto graph = passes::build_control_flow_graph(
      ops, {.unknown_indirect_flow_to_all = false,
            .unresolved_direct_flow_to_all = false});
  if (!graph.targets_are_exact())
    return false;
  std::vector<std::vector<std::size_t>> predecessors(ops.size());
  for (std::size_t source = 0; source < graph.edges.size(); ++source)
    for (const auto& edge : graph.edges[source]) {
      if (edge.target < 0 || static_cast<std::size_t>(edge.target) >= ops.size())
        return false;
      predecessors[static_cast<std::size_t>(edge.target)].push_back(source);
    }
  std::vector<std::size_t> pending{entry};
  std::set<std::size_t> visited;
  bool saw_closer = false;
  while (!pending.empty()) {
    const std::size_t index = pending.back();
    pending.pop_back();
    if (!visited.insert(index).second)
      continue;
    if (index == 0 || predecessors[index].empty())
      return false;
    for (std::size_t predecessor : predecessors[index]) {
      const auto& op = ops[predecessor];
      if (op.kind == IrKind::Label) {
        pending.push_back(predecessor);
        continue;
      }
      if (op.meta.raw || op.meta.manual_interaction.has_value() ||
          (op.kind != IrKind::Store && op.kind != IrKind::Call &&
           op.kind != IrKind::IndirectCall) ||
          !selector_charge_entry_closer_opcode(op.opcode))
        return false;
      saw_closer = true;
    }
  }
  return saw_closer;
}

} // namespace mkpro::core
