#include "mkpro/core/passes/semantic_region_folding.hpp"

#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"

#include <algorithm>
#include <functional>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>

namespace mkpro::core::passes {
namespace {

struct Component {
  std::size_t begin = 0;
  std::size_t end = 0;
  int cells = 0;
};

bool ends_chain(const IrOp& op) {
  return op.kind == IrKind::Return || op.kind == IrKind::Jump ||
         op.kind == IrKind::IndirectJump ||
         (op.kind == IrKind::Stop && op.meta.stop_disposition == StopDisposition::Terminal);
}

bool symbolic(const std::vector<IrOp>& ops) {
  if (ops.empty() || ops.size() > 640U)
    return false;
  for (const auto& op : ops) {
    if (op.meta.raw || op.meta.logical_register_analysis ||
        op.kind == IrKind::OrphanAddress || op.target_meta.formal_opcode ||
        op.meta.indirect_flow_formal_targets)
      return false;
    if ((op.kind == IrKind::Call || op.kind == IrKind::Jump ||
         op.kind == IrKind::CondJump || op.kind == IrKind::Loop) &&
        !std::holds_alternative<std::string>(op.target))
      return false;
    if (op.meta.indirect_flow_targets)
      for (const auto& target : *op.meta.indirect_flow_targets)
        if (!std::holds_alternative<std::string>(target))
          return false;
    for (const auto& role : op.meta.roles)
      if (role == kResumableErrorPaddingRole || role == "exec" ||
          role.starts_with("late-decimal-selector-") ||
          role.starts_with("finalization-cell-origin:"))
        return false;
  }
  return true;
}

std::vector<Component> components(const std::vector<IrOp>& ops) {
  std::vector<Component> result;
  Component current;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    current.cells += cells_per_op(ops[i]);
    if (ends_chain(ops[i])) {
      current.end = i + 1U;
      result.push_back(current);
      current = Component{i + 1U, i + 1U, 0};
    }
  }
  if (current.begin != ops.size()) {
    current.end = ops.size();
    result.push_back(current);
  }
  return result;
}

bool observation(const IrOp& op) {
  return op.kind == IrKind::Stop || op.meta.manual_interaction.has_value();
}

PostLayoutControlFlowOptions flow_options(const PassContext& context) {
  PostLayoutControlFlowOptions result;
  result.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(context.options));
  return result;
}

std::vector<std::size_t> machine_indexes(const std::vector<IrOp>& ops) {
  std::vector<std::size_t> result;
  std::size_t index = 0;
  for (const auto& op : ops) {
    result.push_back(index);
    index += op.kind == IrKind::Label ? 1U : static_cast<std::size_t>(cells_per_op(op));
  }
  return result;
}

bool relocated(const std::vector<IrOp>& before, const std::vector<IrOp>& after,
               const std::vector<std::size_t>& mapping, const PassContext& context) {
  const auto input = lower_ir_to_machine(before);
  const auto output = lower_ir_to_machine(after);
  const auto old_machine = machine_indexes(before);
  const auto new_machine = machine_indexes(after);
  std::vector<std::optional<std::size_t>> items(input.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    if (mapping[i] >= after.size())
      return false;
    const std::size_t count = before[i].kind == IrKind::Label
        ? 1U : static_cast<std::size_t>(cells_per_op(before[i]));
    for (std::size_t cell = 0; cell < count; ++cell)
      items[old_machine[i] + cell] = new_machine[mapping[i]] + cell;
  }
  const auto options = flow_options(context);
  const auto old_flow = build_post_layout_control_flow(input, options);
  const auto new_flow = build_post_layout_control_flow(output, options);
  return old_flow.proved && new_flow.proved &&
         prove_post_layout_execution_relocation(input, output, old_flow, new_flow, items).proved;
}

auto effect_fields(const IrOp& op) {
  return std::tie(op.kind, op.register_name, op.condition, op.counter, op.opcode,
                  op.semantic, op.meta.raw, op.meta.roles, op.meta.stop_disposition,
                  op.meta.indirect_flow_formal_targets, op.meta.indirect_memory_targets,
                  op.meta.logical_register_name, op.meta.logical_indirect_memory_targets,
                  op.meta.logical_register_analysis, op.meta.discarded_indirect_recall_value,
                  op.meta.borrowed_entry_phase_selector, op.meta.tactic,
                  op.target_meta.roles, op.target_meta.formal_opcode);
}

std::map<std::string, int> positions(const std::vector<IrOp>& ops) {
  std::map<std::string, int> result;
  int address = 0;
  for (const auto& op : ops) {
    if (op.kind == IrKind::Label)
      result.emplace(op.name, address);
    address += cells_per_op(op);
  }
  return result;
}

bool fixed_indirect_targets(const std::vector<IrOp>& before, const std::vector<IrOp>& after) {
  const auto old_positions = positions(before);
  const auto new_positions = positions(after);
  for (const auto& op : before)
    if (op.meta.indirect_flow_targets)
      for (const auto& target : *op.meta.indirect_flow_targets) {
        const auto* label = std::get_if<std::string>(&target);
        if (!label || !old_positions.contains(*label) || !new_positions.contains(*label) ||
            old_positions.at(*label) != new_positions.at(*label))
          return false;
      }
  return true;
}

} // namespace

std::optional<SymbolicRegionLayout> layout_symbolic_regions(
    const std::vector<IrOp>& ops, const std::map<std::string, int>& required_targets,
    const PassContext& context, std::size_t maximum_work) {
  if (!symbolic(ops))
    return std::nullopt;
  const auto parts = components(ops);
  if (parts.empty() || parts.size() > 24U)
    return std::nullopt;
  std::map<std::string, std::pair<std::size_t, int>> labels;
  for (std::size_t part = 0; part < parts.size(); ++part) {
    int offset = 0;
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i) {
      if (ops[i].kind == IrKind::Label && !labels.emplace(ops[i].name, std::pair{part, offset}).second)
        return std::nullopt;
      offset += cells_per_op(ops[i]);
    }
  }
  std::vector<std::optional<int>> starts(parts.size());
  starts[0] = 0; // The externally entered root is never moved.
  // A manual protocol is a local layout anchor, not a ban on optimizing
  // unrelated procedures. Keep its entire physical fallthrough component in
  // place; the matched-context transport proof still checks every external
  // entry and continuation after all other components have been placed.
  int original_address = 0;
  for (std::size_t part = 0; part < parts.size(); ++part) {
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i)
      if (ops[i].meta.manual_interaction)
        starts[part] = original_address;
    original_address += parts[part].cells;
  }
  for (const auto& [label, address] : required_targets) {
    const auto found = labels.find(label);
    if (found == labels.end())
      return std::nullopt;
    const auto [part, offset] = found->second;
    const int start = address - offset;
    if (start < 0 || (starts[part] && *starts[part] != start))
      return std::nullopt;
    starts[part] = start;
  }
  std::size_t work = 0;
  std::vector<std::size_t> order;
  std::vector<bool> used(parts.size(), false);
  std::function<bool(int)> place = [&](int address) {
    if (work++ >= maximum_work)
      return false;
    if (order.size() == parts.size())
      return true;
    std::optional<std::size_t> due;
    int deadline = 1000000;
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (used[i] || !starts[i])
        continue;
      if (*starts[i] < address)
        return false;
      if (*starts[i] == address) {
        if (due && parts[i].cells != 0 && parts[*due].cells != 0)
          return false;
        due = i;
      } else {
        deadline = std::min(deadline, *starts[i]);
      }
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (used[i] || (due && i != *due) || (starts[i] && *starts[i] != address) ||
          address + parts[i].cells > deadline)
        continue;
      used[i] = true;
      order.push_back(i);
      if (place(address + parts[i].cells))
        return true;
      order.pop_back();
      used[i] = false;
      if (work >= maximum_work)
        return false;
    }
    return false;
  };
  if (!place(0))
    return std::nullopt;
  SymbolicRegionLayout result;
  result.work = work;
  std::vector<std::size_t> mapping(ops.size());
  for (const auto part : order)
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i) {
      mapping[i] = result.ops.size();
      result.changed = result.changed || mapping[i] != i;
      result.ops.push_back(ops[i]);
    }
  if (!relocated(ops, result.ops, mapping, context))
    return std::nullopt;
  return result;
}

PassResult semantic_region_folding(const std::vector<IrOp>& ops, const PassContext& context) {
  const auto unchanged = [&] { return PassResult{.ops = ops}; };
  if (!symbolic(ops))
    return unchanged();
  const auto parts = components(ops);
  if (parts.size() < 3U)
    return unchanged();
  const std::size_t none = ops.size();
  std::vector<std::size_t> executable;
  std::vector<std::size_t> next(ops.size() + 1U, none);
  for (std::size_t i = ops.size(); i > 0U; --i)
    next[i - 1U] = ops[i - 1U].kind == IrKind::Label ? next[i] : i - 1U;
  std::map<std::string, std::size_t> labels;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].kind == IrKind::Label) {
      if (!labels.emplace(ops[i].name, next[i]).second)
        return unchanged();
    } else {
      executable.push_back(i);
    }
  }
  std::vector<std::vector<std::size_t>> successors(ops.size());
  for (const auto i : executable) {
    const auto& op = ops[i];
    const bool direct = op.kind == IrKind::Call || op.kind == IrKind::Jump ||
                        op.kind == IrKind::CondJump || op.kind == IrKind::Loop;
    if (direct) {
      const auto& label = std::get<std::string>(op.target);
      if (!labels.contains(label) || labels.at(label) == none)
        return unchanged();
      successors[i].push_back(labels.at(label));
    }
    if (op.meta.indirect_flow_targets)
      for (const auto& target : *op.meta.indirect_flow_targets) {
        const auto& label = std::get<std::string>(target);
        if (!labels.contains(label) || labels.at(label) == none)
          return unchanged();
        successors[i].push_back(labels.at(label));
      }
    if (!ends_chain(op) && next[i + 1U] != none)
      successors[i].push_back(next[i + 1U]);
  }
  // Greatest fixed point of effect-labelled, ordered successor equivalence.
  // Calls include both the callee and the exact continuation; returns are
  // transported later through the authoritative matched-context graph.
  std::vector<std::size_t> classes(ops.size(), 0);
  std::size_t work = 0;
  for (std::size_t round = 0; round <= executable.size(); ++round) {
    std::vector<std::size_t> representatives;
    auto refined = classes;
    for (const auto i : executable) {
      bool found = false;
      for (const auto representative : representatives) {
        if (++work > 1000000U)
          return unchanged();
        if (observation(ops[i]) || observation(ops[representative]) ||
            classes[i] != classes[representative] ||
            effect_fields(ops[i]) != effect_fields(ops[representative]) ||
            successors[i].size() != successors[representative].size())
          continue;
        bool same = true;
        for (std::size_t edge = 0; edge < successors[i].size(); ++edge)
          same = same && classes[successors[i][edge]] == classes[successors[representative][edge]];
        if (same) {
          refined[i] = refined[representative];
          found = true;
          break;
        }
      }
      if (!found) {
        refined[i] = representatives.size();
        representatives.push_back(i);
      }
    }
    if (refined == classes)
      break;
    classes = std::move(refined);
  }
  std::vector<std::vector<std::size_t>> commands(parts.size());
  for (std::size_t part = 0; part < parts.size(); ++part)
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i)
      if (ops[i].kind != IrKind::Label)
        commands[part].push_back(i);
  std::vector<std::size_t> representative(parts.size());
  std::iota(representative.begin(), representative.end(), 0U);
  int saved = 0;
  for (std::size_t part = 1; part < parts.size(); ++part) {
    if (commands[part].empty() ||
        std::any_of(commands[part].begin(), commands[part].end(),
                    [&](std::size_t i) { return observation(ops[i]); }))
      continue;
    for (std::size_t keep = 1; keep < part; ++keep) {
      if (representative[keep] != keep || commands[keep].size() != commands[part].size())
        continue;
      bool same = true;
      for (std::size_t k = 0; k < commands[part].size(); ++k)
        same = same && classes[commands[part][k]] == classes[commands[keep][k]];
      if (same) {
        representative[part] = keep;
        saved += parts[part].cells;
        break;
      }
    }
  }
  if (saved == 0)
    return unchanged();
  std::vector<std::size_t> canonical(ops.size());
  std::iota(canonical.begin(), canonical.end(), 0U);
  std::map<std::size_t, std::vector<std::size_t>> aliases;
  std::map<std::size_t, std::vector<std::uint64_t>> origins;
  for (std::size_t part = 0; part < parts.size(); ++part) {
    const auto keep = representative[part];
    if (keep == part)
      continue;
    std::size_t command = 0;
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i) {
      if (command >= commands[keep].size())
        return unchanged();
      const auto at = commands[keep][command];
      canonical[i] = at;
      if (ops[i].kind == IrKind::Label) {
        aliases[at].push_back(i);
      } else {
        auto& merged = origins[at];
        merged.insert(merged.end(), ops[i].meta.semantic_call_origins.begin(),
                      ops[i].meta.semantic_call_origins.end());
        ++command;
      }
    }
  }
  std::vector<IrOp> rewritten;
  std::vector<std::size_t> mapping(ops.size(), none);
  for (std::size_t part = 0; part < parts.size(); ++part) {
    if (representative[part] != part)
      continue;
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i) {
      for (const auto alias : aliases[i]) {
        mapping[alias] = rewritten.size();
        rewritten.push_back(ops[alias]);
      }
      mapping[i] = rewritten.size();
      auto op = ops[i];
      auto& merged = op.meta.semantic_call_origins;
      merged.insert(merged.end(), origins[i].begin(), origins[i].end());
      std::sort(merged.begin(), merged.end());
      merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
      rewritten.push_back(std::move(op));
    }
  }
  for (std::size_t i = 0; i < ops.size(); ++i)
    if (mapping[i] == none)
      mapping[i] = mapping[canonical[i]];
  if (!fixed_indirect_targets(ops, rewritten) || !relocated(ops, rewritten, mapping, context))
    return unchanged();
  return PassResult{.ops = std::move(rewritten), .applied = 1,
      .optimizations = {{.name = "semantic-multi-entry-region-folding",
          .detail = "Merged effect-bisimilar CFG regions, retaining every labelled entry, "
                    "matched return context and call provenance; identical executed opcodes "
                    "preserve stack/X1/X2. Removed " + std::to_string(saved) + " cells."}}};
}

std::vector<RegisterWebRecoloringRequest> semantic_region_register_requests(
    const std::vector<IrOp>& ops) {
  std::vector<RegisterWebRecoloringRequest> result;
  if (!symbolic(ops))
    return result;
  const auto parts = components(ops);
  std::vector<std::vector<std::size_t>> commands(parts.size());
  for (std::size_t part = 1; part < parts.size(); ++part)
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i)
      if (ops[i].kind != IrKind::Label)
        commands[part].push_back(i);
  for (std::size_t left = 1; left < parts.size(); ++left) {
    for (std::size_t right = left + 1U; right < parts.size(); ++right) {
      if (commands[left].size() < 2U || commands[left].size() != commands[right].size())
        continue;
      RegisterWebRecoloringRequest request;
      bool compatible = true;
      for (std::size_t at = 0; at < commands[left].size(); ++at) {
        const auto a_index = commands[left][at];
        const auto b_index = commands[right][at];
        auto a = ops[a_index];
        auto b = ops[b_index];
        if (a.kind == b.kind && (a.kind == IrKind::Store || a.kind == IrKind::Recall)) {
          if (a.register_name != b.register_name)
            request.emplace_back(a_index, b_index);
          a.register_name.clear();
          b.register_name.clear();
          a.opcode &= 0xf0;
          b.opcode &= 0xf0;
        }
        compatible = compatible && !observation(a) && !observation(b) &&
                     effect_fields(a) == effect_fields(b);
      }
      if (compatible && !request.empty()) {
        result.push_back(std::move(request));
        if (result.size() == 6U)
          return result;
      }
    }
  }
  return result;
}

IrPass semantic_region_folding_pass() {
  return {.name = "semantic-multi-entry-region-folding", .run = semantic_region_folding,
          .layout_safe = false};
}

} // namespace mkpro::core::passes
