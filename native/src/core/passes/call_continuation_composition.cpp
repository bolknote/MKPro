#include "mkpro/core/passes/call_continuation_composition.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

struct Region {
  int label = 0;
  int begin = 0;
  int end = 0;
};

struct Composition {
  std::string producer;
  std::string consumer;
  Region producer_region;
  std::vector<int> continuation_calls;
  std::vector<int> returns;
  IrOp tail_jump;
  int saved_cells = 0;
};

std::optional<std::string> symbolic_target(const IrTarget& target) {
  if (const auto* value = std::get_if<std::string>(&target))
    return *value;
  return std::nullopt;
}

int machine_cells(const std::vector<IrOp>& ops) {
  int cells = 0;
  for (const MachineItem& item : lower_ir_to_machine(ops)) {
    if (item.kind != MachineItemKind::Label)
      ++cells;
  }
  return cells;
}

std::map<std::string, int> label_indexes(const std::vector<IrOp>& ops) {
  std::map<std::string, int> labels;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    if (ops.at(static_cast<std::size_t>(index)).kind == IrKind::Label)
      labels.emplace(ops.at(static_cast<std::size_t>(index)).name, index);
  }
  return labels;
}

std::set<std::string> direct_call_targets(const std::vector<IrOp>& ops) {
  std::set<std::string> targets;
  for (const IrOp& op : ops) {
    if (op.kind != IrKind::Call)
      continue;
    if (const std::optional<std::string> target = symbolic_target(op.target);
        target.has_value()) {
      targets.insert(*target);
    }
  }
  return targets;
}

std::optional<Region> procedure_region(const std::vector<IrOp>& ops,
                                       const std::map<std::string, int>& labels,
                                       const std::set<std::string>& call_targets,
                                       const std::string& target) {
  const auto entry = labels.find(target);
  if (entry == labels.end())
    return std::nullopt;

  const int label = entry->second;
  const IrOp& entry_op = ops.at(static_cast<std::size_t>(label));
  int end = static_cast<int>(ops.size());
  for (int index = label + 1; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind != IrKind::Label)
      continue;
    if (op.procedure_boundary == "end" && entry_op.procedure_name.has_value() &&
        op.procedure_name == entry_op.procedure_name) {
      end = index;
      break;
    }
    if (op.procedure_boundary == "start" || call_targets.contains(op.name)) {
      end = index;
      break;
    }
  }
  if (label + 1 >= end)
    return std::nullopt;
  return Region{.label = label, .begin = label + 1, .end = end};
}

bool is_flow(const IrOp& op) {
  return op.kind == IrKind::Jump || op.kind == IrKind::CondJump ||
         op.kind == IrKind::Call || op.kind == IrKind::Loop ||
         op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
         op.kind == IrKind::IndirectCondJump;
}

bool is_indirect_flow(const IrOp& op) {
  return op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
         op.kind == IrKind::IndirectCondJump;
}

bool target_enters_region(const IrTarget& target,
                          const std::map<std::string, int>& labels,
                          const Region& region) {
  const std::optional<std::string> name = symbolic_target(target);
  if (!name.has_value())
    return true;
  const auto entry = labels.find(*name);
  return entry != labels.end() && entry->second >= region.label && entry->second < region.end;
}

bool has_unmodelled_entry_or_exit(const std::vector<IrOp>& ops,
                                  const std::map<std::string, int>& labels,
                                  const Region& region,
                                  const std::string& producer) {
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    const bool inside = index >= region.begin && index < region.end;

    if (inside && (has_rewrite_barrier(op) || op.kind == IrKind::Stop ||
                   is_indirect_flow(op))) {
      return true;
    }

    if (is_indirect_flow(op)) {
      if (!op.meta.indirect_flow_targets.has_value())
        return true;
      for (const IrTarget& target : *op.meta.indirect_flow_targets) {
        if (target_enters_region(target, labels, region))
          return true;
      }
      continue;
    }

    if (!is_flow(op))
      continue;
    const bool enters = target_enters_region(op.target, labels, region);
    if (!inside && enters) {
      const std::optional<std::string> target = symbolic_target(op.target);
      if (op.kind != IrKind::Call || !target.has_value() || *target != producer)
        return true;
    }
    if (inside && (op.kind == IrKind::Jump || op.kind == IrKind::CondJump ||
                   op.kind == IrKind::Loop) && !enters) {
      return true;
    }
  }
  return false;
}

IrOp tail_jump_from_call(const IrOp& call) {
  IrOp out = call;
  out.kind = IrKind::Jump;
  out.opcode = 0x51;
  out.register_name.clear();
  out.meta.mnemonic = "БП";
  out.meta.comment = "composed call continuation tail jump";
  out.target_meta.comment = "composed call continuation";
  return out;
}

std::vector<IrOp> apply_composition(const std::vector<IrOp>& ops,
                                    const Composition& composition) {
  const std::set<int> removed(composition.continuation_calls.begin(),
                              composition.continuation_calls.end());
  const std::set<int> replaced(composition.returns.begin(), composition.returns.end());
  std::vector<IrOp> rewritten;
  rewritten.reserve(ops.size() - removed.size());
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    if (removed.contains(index))
      continue;
    if (replaced.contains(index)) {
      IrOp jump = composition.tail_jump;
      jump.meta.source_line = ops.at(static_cast<std::size_t>(index)).meta.source_line;
      rewritten.push_back(std::move(jump));
      continue;
    }
    rewritten.push_back(ops.at(static_cast<std::size_t>(index)));
  }
  return rewritten;
}

std::optional<Composition> best_composition(const std::vector<IrOp>& ops) {
  const std::map<std::string, int> labels = label_indexes(ops);
  const std::set<std::string> call_targets = direct_call_targets(ops);
  std::map<std::string, std::vector<int>> calls;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind != IrKind::Call)
      continue;
    if (const std::optional<std::string> target = symbolic_target(op.target);
        target.has_value()) {
      calls[*target].push_back(index);
    }
  }

  const int input_cells = machine_cells(ops);
  std::optional<Composition> best;
  for (const auto& [producer, producer_calls] : calls) {
    const std::optional<Region> region =
        procedure_region(ops, labels, call_targets, producer);
    if (!region.has_value() ||
        has_unmodelled_entry_or_exit(ops, labels, *region, producer)) {
      continue;
    }

    std::optional<std::string> consumer;
    std::vector<int> continuation_calls;
    IrOp representative_call;
    bool valid = !producer_calls.empty();
    for (const int call_index : producer_calls) {
      if (call_index >= region->begin && call_index < region->end) {
        valid = false;
        break;
      }
      const int continuation_index = call_index + 1;
      if (continuation_index >= static_cast<int>(ops.size())) {
        valid = false;
        break;
      }
      const IrOp& continuation = ops.at(static_cast<std::size_t>(continuation_index));
      const std::optional<std::string> target = symbolic_target(continuation.target);
      if (continuation.kind != IrKind::Call || !target.has_value() ||
          *target == producer || has_rewrite_barrier(continuation)) {
        valid = false;
        break;
      }
      if (consumer.has_value() && *consumer != *target) {
        valid = false;
        break;
      }
      if (!consumer.has_value()) {
        consumer = *target;
        representative_call = continuation;
      } else {
        representative_call.meta.semantic_call_origins.insert(
            representative_call.meta.semantic_call_origins.end(),
            continuation.meta.semantic_call_origins.begin(),
            continuation.meta.semantic_call_origins.end());
      }
      continuation_calls.push_back(continuation_index);
    }
    if (!valid || !consumer.has_value())
      continue;

    std::vector<int> returns;
    for (int index = region->begin; index < region->end; ++index) {
      if (ops.at(static_cast<std::size_t>(index)).kind == IrKind::Return) {
        if (has_rewrite_barrier(ops.at(static_cast<std::size_t>(index)))) {
          valid = false;
          break;
        }
        returns.push_back(index);
      }
    }
    if (!valid || returns.empty())
      continue;

    Composition candidate{
        .producer = producer,
        .consumer = *consumer,
        .producer_region = *region,
        .continuation_calls = std::move(continuation_calls),
        .returns = std::move(returns),
        .tail_jump = tail_jump_from_call(representative_call),
    };
    std::sort(candidate.tail_jump.meta.semantic_call_origins.begin(),
              candidate.tail_jump.meta.semantic_call_origins.end());
    candidate.tail_jump.meta.semantic_call_origins.erase(
        std::unique(candidate.tail_jump.meta.semantic_call_origins.begin(),
                    candidate.tail_jump.meta.semantic_call_origins.end()),
        candidate.tail_jump.meta.semantic_call_origins.end());

    const std::vector<IrOp> trial = apply_composition(ops, candidate);
    candidate.saved_cells = input_cells - machine_cells(trial);
    if (candidate.saved_cells <= 0)
      continue;
    if (!best.has_value() || candidate.saved_cells > best->saved_cells ||
        (candidate.saved_cells == best->saved_cells && candidate.producer < best->producer)) {
      best = std::move(candidate);
    }
  }
  return best;
}

} // namespace

PassResult call_continuation_composition(const std::vector<IrOp>& ops,
                                         const PassContext& context) {
  (void)context;
  std::vector<IrOp> current = ops;
  int applied = 0;
  int saved_cells = 0;
  std::vector<std::string> pairs;
  while (const std::optional<Composition> composition = best_composition(current)) {
    saved_cells += composition->saved_cells;
    applied += static_cast<int>(composition->continuation_calls.size());
    pairs.push_back(composition->producer + " -> " + composition->consumer);
    current = apply_composition(current, *composition);
  }
  if (applied == 0)
    return PassResult{.ops = ops};

  std::string detail = "Composed " + std::to_string(applied) +
                       " immediate helper continuation call" +
                       (applied == 1 ? "" : "s") + " into tail flow, saving " +
                       std::to_string(saved_cells) + " machine cell" +
                       (saved_cells == 1 ? "" : "s") + ": ";
  for (std::size_t index = 0; index < pairs.size(); ++index) {
    if (index != 0)
      detail += ", ";
    detail += pairs.at(index);
  }
  detail += ". Every direct call of each producer had the same immediate consumer, "
            "and all entries, exits, barriers, and indirect flows were proved closed.";
  return PassResult{
      .ops = std::move(current),
      .applied = applied,
      .optimizations = {{.name = "call-continuation-composition", .detail = std::move(detail)}},
  };
}

IrPass call_continuation_composition_pass() {
  return IrPass{
      .name = "call-continuation-composition",
      .run = call_continuation_composition,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
