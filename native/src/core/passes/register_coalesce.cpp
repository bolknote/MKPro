#include "mkpro/core/passes/register_coalesce.hpp"

#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

constexpr int kDirectStoreBase = 0x40;
constexpr int kDirectRecallBase = 0x60;

using RegisterSet = std::set<std::string>;
using IndexSet = std::set<int>;
using RegisterRangeMap = std::map<std::string, IndexSet>;

std::optional<std::string> loop_counter_register(const std::string& counter) {
  if (counter == "L0")
    return "0";
  if (counter == "L1")
    return "1";
  if (counter == "L2")
    return "2";
  if (counter == "L3")
    return "3";
  return std::nullopt;
}

bool is_indirect_access(const IrOp& op) {
  return op.kind == IrKind::IndirectStore || op.kind == IrKind::IndirectRecall ||
         op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
         op.kind == IrKind::IndirectCondJump;
}

RegisterSet gather_used_registers(const std::vector<IrOp>& ops) {
  RegisterSet registers;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Store || op.kind == IrKind::Recall)
      registers.insert(op.register_name);
    if (is_indirect_access(op)) {
      registers.insert(op.register_name);
      if (const std::optional<std::set<std::string>> memory_targets =
              known_indirect_memory_targets(op)) {
        registers.insert(memory_targets->begin(), memory_targets->end());
      }
    }
    if (op.kind == IrKind::Loop) {
      if (const std::optional<std::string> counter = loop_counter_register(op.counter))
        registers.insert(*counter);
    }
  }
  return registers;
}

RegisterRangeMap live_range_per_register(const std::vector<IrOp>& ops, const RegisterSet& registers,
                                         bool def_aware) {
  const LivenessInfo liveness = compute_liveness(ops);
  RegisterRangeMap ranges;
  for (const std::string& reg : registers) {
    ranges.emplace(reg, IndexSet{});
  }

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const int position = static_cast<int>(index);
    for (const std::string& reg : liveness.live_in.at(index)) {
      if (const auto found = ranges.find(reg); found != ranges.end())
        found->second.insert(position);
    }
    for (const std::string& reg : liveness.live_out.at(index)) {
      if (const auto found = ranges.find(reg); found != ranges.end())
        found->second.insert(position);
    }

    if (def_aware) {
      const IrOp& op = ops.at(index);
      if (op.kind == IrKind::Store) {
        if (const auto found = ranges.find(op.register_name); found != ranges.end())
          found->second.insert(position);
      } else if (op.kind == IrKind::IndirectStore) {
        if (const std::optional<std::set<std::string>> targets =
                known_indirect_memory_targets(op)) {
          for (const std::string& target : *targets) {
            if (const auto found = ranges.find(target); found != ranges.end())
              found->second.insert(position);
          }
        }
      }
    }
  }

  return ranges;
}

bool intersects(const IndexSet& left, const IndexSet& right) {
  const IndexSet& smaller = left.size() < right.size() ? left : right;
  const IndexSet& larger = left.size() < right.size() ? right : left;
  return std::any_of(smaller.begin(), smaller.end(),
                     [&](int value) { return larger.contains(value); });
}

bool uses_indirect_access(const std::vector<IrOp>& ops, const std::string& register_name) {
  for (const IrOp& op : ops) {
    if (!is_indirect_access(op))
      continue;
    if (op.register_name == register_name)
      return true;
    if (const std::optional<std::set<std::string>> memory_targets =
            known_indirect_memory_targets(op);
        memory_targets.has_value() && memory_targets->contains(register_name)) {
      return true;
    }
  }
  return false;
}

bool has_unknown_indirect_memory_targets(const std::vector<IrOp>& ops) {
  return std::any_of(ops.begin(), ops.end(), [](const IrOp& op) {
    return (op.kind == IrKind::IndirectStore || op.kind == IrKind::IndirectRecall) &&
           !known_indirect_memory_targets(op).has_value();
  });
}

bool uses_display_focus_sensitive_access(const std::vector<IrOp>& ops,
                                         const std::string& register_name) {
  return std::any_of(ops.begin(), ops.end(), [&](const IrOp& op) {
    return (op.kind == IrKind::Store || op.kind == IrKind::Recall) &&
           op.register_name == register_name && is_display_focus_sensitive(op);
  });
}

bool uses_loop_counter(const std::vector<IrOp>& ops, const std::string& register_name) {
  return std::any_of(ops.begin(), ops.end(), [&](const IrOp& op) {
    if (op.kind != IrKind::Loop)
      return false;
    return loop_counter_register(op.counter) == register_name;
  });
}

void union_into(IndexSet& target, const IndexSet& source) {
  target.insert(source.begin(), source.end());
}

int coalesced_opcode(IrKind kind, const std::string& register_name) {
  const int base = kind == IrKind::Store ? kDirectStoreBase : kDirectRecallBase;
  return base + register_index(register_name);
}

IrOp rewrite_register_op(const IrOp& op, const std::string& register_name) {
  if (op.kind != IrKind::Store && op.kind != IrKind::Recall)
    return op;

  IrOp rewritten = op;
  const int opcode = coalesced_opcode(op.kind, register_name);
  rewritten.register_name = register_name;
  rewritten.opcode = opcode;
  rewritten.meta.mnemonic = opcode_by_code(opcode).name;
  return rewritten;
}

RegisterSet excluded_registers(const std::vector<IrOp>& ops, const RegisterSet& registers) {
  RegisterSet excluded;
  for (const std::string& reg : registers) {
    if (uses_indirect_access(ops, reg) || uses_display_focus_sensitive_access(ops, reg) ||
        uses_loop_counter(ops, reg)) {
      excluded.insert(reg);
    }
  }
  return excluded;
}

struct CoalesceResult {
  std::vector<IrOp> ops;
  int applied = 0;
};

CoalesceResult coalesce_non_overlapping(const std::vector<IrOp>& ops) {
  const std::map<std::string, std::string> mapping = compute_non_overlapping_register_mapping(ops);
  if (mapping.empty())
    return CoalesceResult{.ops = ops, .applied = 0};

  std::vector<IrOp> result;
  result.reserve(ops.size());
  for (const IrOp& op : ops) {
    if (op.kind != IrKind::Store && op.kind != IrKind::Recall) {
      result.push_back(op);
      continue;
    }
    const auto replacement = mapping.find(op.register_name);
    result.push_back(replacement == mapping.end() ? op
                                                  : rewrite_register_op(op, replacement->second));
  }

  return CoalesceResult{.ops = std::move(result), .applied = static_cast<int>(mapping.size())};
}

CoalesceResult coalesce_copies(const std::vector<IrOp>& ops) {
  const RegisterSet registers = gather_used_registers(ops);
  if (registers.size() <= 1U)
    return CoalesceResult{.ops = ops, .applied = 0};

  const LivenessInfo liveness = compute_liveness(ops);
  const RegisterSet excluded = excluded_registers(ops, registers);

  std::map<std::string, std::vector<int>> store_indices;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind == IrKind::Store) {
      store_indices[op.register_name].push_back(static_cast<int>(index));
    }
  }

  std::map<std::string, std::string> mapping;
  std::set<int> drop_indices;
  for (std::size_t index = 0; index + 1U < ops.size(); ++index) {
    const IrOp& recall = ops.at(index);
    const IrOp& store = ops.at(index + 1U);
    if (recall.kind != IrKind::Recall || store.kind != IrKind::Store)
      continue;

    const std::string& source = recall.register_name;
    const std::string& dest = store.register_name;
    if (source == dest || mapping.contains(dest) || mapping.contains(source))
      continue;
    if (excluded.contains(source) || excluded.contains(dest))
      continue;

    const auto dest_stores = store_indices.find(dest);
    if (dest_stores == store_indices.end() || dest_stores->second.size() != 1U ||
        dest_stores->second.front() != static_cast<int>(index + 1U)) {
      continue;
    }

    if (liveness.live_in.at(index).contains(dest))
      continue;

    bool diverges = false;
    if (const auto source_stores = store_indices.find(source);
        source_stores != store_indices.end()) {
      for (const int store_index : source_stores->second) {
        if (liveness.live_out.at(static_cast<std::size_t>(store_index)).contains(dest)) {
          diverges = true;
          break;
        }
      }
    }
    if (diverges)
      continue;

    mapping[dest] = source;
    drop_indices.insert(static_cast<int>(index + 1U));
  }

  if (mapping.empty())
    return CoalesceResult{.ops = ops, .applied = 0};

  std::vector<IrOp> result;
  result.reserve(ops.size() - drop_indices.size());
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (drop_indices.contains(static_cast<int>(index)))
      continue;
    const IrOp& op = ops.at(index);
    if (op.kind == IrKind::Store || op.kind == IrKind::Recall) {
      const auto replacement = mapping.find(op.register_name);
      result.push_back(replacement == mapping.end() ? op
                                                    : rewrite_register_op(op, replacement->second));
    } else {
      result.push_back(op);
    }
  }

  return CoalesceResult{.ops = std::move(result), .applied = static_cast<int>(mapping.size())};
}

} // namespace

std::map<std::string, std::string>
compute_non_overlapping_register_mapping(const std::vector<IrOp>& ops,
                                         RegisterCoalesceMappingOptions options) {
  const RegisterSet registers = gather_used_registers(ops);
  std::map<std::string, std::string> mapping;
  if (registers.size() <= 1U || has_unknown_indirect_memory_targets(ops))
    return mapping;

  const LivenessInfo liveness = compute_liveness(ops);
  const RegisterSet live_at_entry =
      liveness.live_in.empty() ? RegisterSet{} : liveness.live_in.front();
  RegisterRangeMap ranges = live_range_per_register(ops, registers, options.def_aware);

  std::vector<std::string> ordered(registers.begin(), registers.end());
  for (std::size_t left = 0; left < ordered.size(); ++left) {
    const std::string& a = ordered.at(left);
    if (mapping.contains(a))
      continue;
    if (live_at_entry.contains(a))
      continue;
    if (!options.def_aware && uses_indirect_access(ops, a))
      continue;
    if (uses_display_focus_sensitive_access(ops, a))
      continue;
    if (!options.def_aware && uses_loop_counter(ops, a))
      continue;

    for (std::size_t right = left + 1U; right < ordered.size(); ++right) {
      const std::string& b = ordered.at(right);
      if (mapping.contains(b))
        continue;
      if (live_at_entry.contains(b))
        continue;
      if (uses_indirect_access(ops, b))
        continue;
      if (uses_display_focus_sensitive_access(ops, b))
        continue;
      if (uses_loop_counter(ops, b))
        continue;

      IndexSet& range_a = ranges.at(a);
      const IndexSet& range_b = ranges.at(b);
      if (intersects(range_a, range_b))
        continue;

      mapping[b] = a;
      union_into(range_a, range_b);
      break;
    }
  }

  return mapping;
}

PassResult register_coalesce(const std::vector<IrOp>& ops, const PassContext& context) {
  if (ops.empty())
    return PassResult{.ops = {}, .applied = 0, .optimizations = {}};

  if (std::any_of(ops.begin(), ops.end(), [](const IrOp& op) { return has_rewrite_barrier(op); }))
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<AppliedOptimization> optimizations;
  std::vector<IrOp> current = ops;
  int applied = 0;

  CoalesceResult non_overlap = coalesce_non_overlapping(current);
  current = std::move(non_overlap.ops);
  applied += non_overlap.applied;
  if (non_overlap.applied > 0) {
    optimizations.push_back(AppliedOptimization{
        .name = "register-coalesce",
        .detail = "Coalesced " + std::to_string(non_overlap.applied) +
                  " non-overlapping register live range(s).",
    });
  }

  if (context.options.coalesce_copies) {
    CoalesceResult copies = coalesce_copies(current);
    current = std::move(copies.ops);
    applied += copies.applied;
    if (copies.applied > 0) {
      optimizations.push_back(AppliedOptimization{
          .name = "copy-coalesce",
          .detail = "Coalesced " + std::to_string(copies.applied) +
                    " non-diverging copy assignment(s), dropping the copy and freeing a register.",
      });
    }
  }

  if (applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  return PassResult{
      .ops = std::move(current),
      .applied = applied,
      .optimizations = std::move(optimizations),
  };
}

IrPass register_coalesce_pass() {
  return IrPass{
      .name = "register-coalesce",
      .run = register_coalesce,
      .layout_safe = true,
  };
}

} // namespace mkpro::core::passes
