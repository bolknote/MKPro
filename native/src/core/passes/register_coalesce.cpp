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
constexpr std::size_t kMaximumHardwareRegisterCount = 16U;

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

bool uses_manual_interaction_register(const std::vector<IrOp>& ops,
                                      const std::string& register_name) {
  return std::any_of(ops.begin(), ops.end(), [&](const IrOp& op) {
    return op.meta.manual_interaction.has_value() && op.register_name == register_name;
  });
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
        uses_loop_counter(ops, reg) || uses_manual_interaction_register(ops, reg)) {
      excluded.insert(reg);
    }
  }
  return excluded;
}

struct RegisterColor {
  std::string representative;
  RegisterSet members;
};

bool color_accepts(const RegisterInterferenceGraph& graph, const RegisterColor& color,
                   const std::string& reg) {
  return std::none_of(color.members.begin(), color.members.end(),
                      [&](const std::string& member) { return graph.interferes(member, reg); });
}

std::size_t interference_degree(const RegisterInterferenceGraph& graph, const std::string& reg) {
  const auto found = graph.neighbors.find(reg);
  return found == graph.neighbors.end() ? 0U : found->second.size();
}

std::string select_coloring_node(const RegisterInterferenceGraph& graph,
                                 const RegisterSet& unassigned,
                                 const std::map<std::string, std::size_t>& assignments) {
  std::string selected;
  std::size_t selected_saturation = 0;
  std::size_t selected_degree = 0;
  bool has_selected = false;
  for (const std::string& reg : unassigned) {
    std::set<std::size_t> adjacent_colors;
    const auto neighbors = graph.neighbors.find(reg);
    if (neighbors != graph.neighbors.end()) {
      for (const std::string& neighbor : neighbors->second) {
        const auto assignment = assignments.find(neighbor);
        if (assignment != assignments.end())
          adjacent_colors.insert(assignment->second);
      }
    }
    const std::size_t saturation = adjacent_colors.size();
    const std::size_t degree = interference_degree(graph, reg);
    if (!has_selected || saturation > selected_saturation ||
        (saturation == selected_saturation && degree > selected_degree) ||
        (saturation == selected_saturation && degree == selected_degree && reg < selected)) {
      selected = reg;
      selected_saturation = saturation;
      selected_degree = degree;
      has_selected = true;
    }
  }
  return selected;
}

std::vector<RegisterColor> greedy_register_coloring(const RegisterInterferenceGraph& graph,
                                                    const std::vector<std::string>& anchors,
                                                    const std::vector<std::string>& movable) {
  std::vector<RegisterColor> colors;
  std::map<std::string, std::size_t> assignments;
  for (const std::string& anchor : anchors) {
    assignments[anchor] = colors.size();
    colors.push_back(RegisterColor{.representative = anchor, .members = RegisterSet{anchor}});
  }

  RegisterSet unassigned(movable.begin(), movable.end());
  while (!unassigned.empty()) {
    const std::string reg = select_coloring_node(graph, unassigned, assignments);
    std::optional<std::size_t> accepted;
    for (std::size_t color = 0; color < colors.size(); ++color) {
      if (color_accepts(graph, colors.at(color), reg)) {
        accepted = color;
        break;
      }
    }
    if (!accepted.has_value()) {
      accepted = colors.size();
      colors.push_back(RegisterColor{.representative = reg});
    }
    colors.at(*accepted).members.insert(reg);
    assignments[reg] = *accepted;
    unassigned.erase(reg);
  }
  return colors;
}

class OptimalRegisterColoring {
public:
  OptimalRegisterColoring(const RegisterInterferenceGraph& graph,
                          const std::vector<std::string>& anchors,
                          const std::vector<std::string>& movable)
      : graph_(graph), unassigned_(movable.begin(), movable.end()),
        best_(greedy_register_coloring(graph, anchors, movable)), best_count_(best_.size()) {
    for (const std::string& anchor : anchors) {
      assignments_[anchor] = colors_.size();
      colors_.push_back(RegisterColor{.representative = anchor, .members = RegisterSet{anchor}});
    }
  }

  std::vector<RegisterColor> solve() {
    search();
    return best_;
  }

private:
  void search() {
    if (unassigned_.empty()) {
      if (colors_.size() < best_count_) {
        best_ = colors_;
        best_count_ = colors_.size();
      }
      return;
    }
    if (colors_.size() >= best_count_)
      return;

    const std::string reg = select_coloring_node(graph_, unassigned_, assignments_);
    unassigned_.erase(reg);

    for (std::size_t color = 0; color < colors_.size(); ++color) {
      if (!color_accepts(graph_, colors_.at(color), reg))
        continue;
      colors_.at(color).members.insert(reg);
      assignments_[reg] = color;
      search();
      assignments_.erase(reg);
      colors_.at(color).members.erase(reg);
    }

    if (colors_.size() + 1U < best_count_) {
      const std::size_t color = colors_.size();
      colors_.push_back(RegisterColor{.representative = reg, .members = RegisterSet{reg}});
      assignments_[reg] = color;
      search();
      assignments_.erase(reg);
      colors_.pop_back();
    }

    unassigned_.insert(reg);
  }

  const RegisterInterferenceGraph& graph_;
  RegisterSet unassigned_;
  std::map<std::string, std::size_t> assignments_;
  std::vector<RegisterColor> colors_;
  std::vector<RegisterColor> best_;
  std::size_t best_count_ = 0;
};

class PrecoloredRegisterGraphSolver {
public:
  PrecoloredRegisterGraphSolver(const RegisterInterferenceGraph& graph,
                                const PrecoloredRegisterAllocationOptions& options)
      : graph_(graph), options_(options) {
    for (const auto& [node, neighbors] : graph.neighbors) {
      nodes_.insert(node);
      nodes_.insert(neighbors.begin(), neighbors.end());
    }
    for (const auto& [node, color] : options.fixed_colors) {
      (void)color;
      nodes_.insert(node);
    }
    for (const auto& [node, color] : options.preferred_colors) {
      (void)color;
      nodes_.insert(node);
    }
  }

  std::optional<std::map<std::string, int>> solve() {
    if (options_.color_count <= 0)
      return std::nullopt;
    for (const auto& [node, color] : options_.fixed_colors) {
      if (color < 0 || color >= options_.color_count)
        return std::nullopt;
      assignments_[node] = color;
      anchored_colors_.insert(color);
    }
    for (const auto& [node, color] : assignments_) {
      const auto neighbors = graph_.neighbors.find(node);
      if (neighbors == graph_.neighbors.end())
        continue;
      for (const std::string& neighbor : neighbors->second) {
        const auto assigned = assignments_.find(neighbor);
        if (assigned != assignments_.end() && assigned->second == color)
          return std::nullopt;
      }
    }
    for (const auto& [node, color] : assignments_) {
      (void)color;
      nodes_.erase(node);
    }
    if (!greedy_seed()) {
      assignments_.clear();
      nodes_.clear();
      for (const auto& [node, neighbors] : graph_.neighbors) {
        nodes_.insert(node);
        nodes_.insert(neighbors.begin(), neighbors.end());
      }
      for (const auto& [node, color] : options_.fixed_colors) {
        assignments_[node] = color;
        nodes_.erase(node);
      }
      for (const auto& [node, color] : options_.preferred_colors) {
        (void)color;
        if (!assignments_.contains(node))
          nodes_.insert(node);
      }
      if (!search())
        return std::nullopt;
    }
    return assignments_;
  }

private:
  bool accepts(const std::string& node, int color) const {
    const auto neighbors = graph_.neighbors.find(node);
    if (neighbors == graph_.neighbors.end())
      return true;
    return std::none_of(neighbors->second.begin(), neighbors->second.end(),
                        [&](const std::string& neighbor) {
                          const auto assigned = assignments_.find(neighbor);
                          return assigned != assignments_.end() && assigned->second == color;
                        });
  }

  std::string select_node() const {
    std::string selected;
    std::size_t selected_saturation = 0;
    std::size_t selected_degree = 0;
    bool found = false;
    for (const std::string& node : nodes_) {
      std::set<int> adjacent_colors;
      const auto neighbors = graph_.neighbors.find(node);
      if (neighbors != graph_.neighbors.end()) {
        for (const std::string& neighbor : neighbors->second) {
          const auto assigned = assignments_.find(neighbor);
          if (assigned != assignments_.end())
            adjacent_colors.insert(assigned->second);
        }
      }
      const std::size_t saturation = adjacent_colors.size();
      const std::size_t degree =
          neighbors == graph_.neighbors.end() ? 0U : neighbors->second.size();
      if (!found || saturation > selected_saturation ||
          (saturation == selected_saturation && degree > selected_degree) ||
          (saturation == selected_saturation && degree == selected_degree && node < selected)) {
        selected = node;
        selected_saturation = saturation;
        selected_degree = degree;
        found = true;
      }
    }
    return selected;
  }

  std::vector<int> candidate_colors(const std::string& node, bool break_symmetry) const {
    std::vector<int> colors;
    const auto preferred = options_.preferred_colors.find(node);
    if (preferred != options_.preferred_colors.end() && preferred->second >= 0 &&
        preferred->second < options_.color_count && accepts(node, preferred->second)) {
      colors.push_back(preferred->second);
    }

    std::set<int> used_colors;
    for (const auto& [assigned_node, color] : assignments_) {
      (void)assigned_node;
      used_colors.insert(color);
    }
    bool tried_unused_unanchored = false;
    for (int color = 0; color < options_.color_count; ++color) {
      if (std::find(colors.begin(), colors.end(), color) != colors.end() || !accepts(node, color)) {
        continue;
      }
      const bool unused_unanchored =
          !used_colors.contains(color) && !anchored_colors_.contains(color);
      if (break_symmetry && unused_unanchored && tried_unused_unanchored)
        continue;
      colors.push_back(color);
      if (unused_unanchored)
        tried_unused_unanchored = true;
    }
    return colors;
  }

  bool greedy_seed() {
    const RegisterSet remaining = nodes_;
    for (std::size_t count = 0; count < remaining.size(); ++count) {
      const std::string node = select_node();
      const std::vector<int> colors = candidate_colors(node, true);
      if (colors.empty())
        return false;
      assignments_[node] = colors.front();
      nodes_.erase(node);
    }
    return true;
  }

  bool search() {
    if (nodes_.empty())
      return true;
    const std::string node = select_node();
    nodes_.erase(node);
    for (const int color : candidate_colors(node, true)) {
      assignments_[node] = color;
      if (search())
        return true;
      assignments_.erase(node);
    }
    nodes_.insert(node);
    return false;
  }

  const RegisterInterferenceGraph& graph_;
  const PrecoloredRegisterAllocationOptions& options_;
  RegisterSet nodes_;
  std::map<std::string, int> assignments_;
  std::set<int> anchored_colors_;
};

std::map<std::string, std::string>
mapping_from_interference_graph(const std::vector<IrOp>& ops, const RegisterSet& registers,
                                const LivenessInfo& liveness,
                                const RegisterInterferenceGraph& graph,
                                bool allow_live_at_entry_anchor_reuse) {
  const RegisterSet live_at_entry =
      liveness.live_in.empty() ? RegisterSet{} : liveness.live_in.front();
  std::vector<std::string> anchors;
  std::vector<std::string> movable;
  for (const std::string& reg : registers) {
    if (uses_display_focus_sensitive_access(ops, reg) ||
        (live_at_entry.contains(reg) && !allow_live_at_entry_anchor_reuse))
      continue;
    if (live_at_entry.contains(reg) || uses_indirect_access(ops, reg) ||
        uses_loop_counter(ops, reg) ||
        uses_manual_interaction_register(ops, reg)) {
      anchors.push_back(reg);
    } else {
      movable.push_back(reg);
    }
  }

  if (movable.empty())
    return {};

  const std::vector<RegisterColor> colors =
      anchors.size() + movable.size() <= kMaximumHardwareRegisterCount
          ? OptimalRegisterColoring(graph, anchors, movable).solve()
          : greedy_register_coloring(graph, anchors, movable);
  const RegisterSet anchor_set(anchors.begin(), anchors.end());
  std::map<std::string, std::string> mapping;
  for (const RegisterColor& color : colors) {
    // An anchor is tied to its physical register. For an all-movable color,
    // choose the lowest physical register independently of DSATUR traversal.
    // In particular, a proof for the standard profile must prefer R0..Re over
    // the temporary Rf overflow color whenever they can share a lifetime.
    const auto anchored = std::find_if(color.members.begin(), color.members.end(),
                                       [&](const std::string& member) {
                                         return anchor_set.contains(member);
                                       });
    const std::string& representative =
        anchored == color.members.end() ? *color.members.begin() : *anchored;
    for (const std::string& member : color.members) {
      if (member != representative)
        mapping[member] = representative;
    }
  }
  return mapping;
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
  const RegisterInterferenceGraph interference = build_register_interference_graph(ops, liveness);
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
    if (interference.interferes(source, dest))
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

std::optional<std::map<std::string, int>>
color_precolored_register_graph(const RegisterInterferenceGraph& graph,
                                const PrecoloredRegisterAllocationOptions& options) {
  return PrecoloredRegisterGraphSolver(graph, options).solve();
}

std::map<std::string, std::string>
compute_non_overlapping_register_mapping(const std::vector<IrOp>& ops,
                                         RegisterCoalesceMappingOptions options) {
  const RegisterSet registers = gather_used_registers(ops);
  if (registers.size() <= 1U || has_unknown_indirect_memory_targets(ops) ||
      std::any_of(ops.begin(), ops.end(), [](const IrOp& op) { return op.meta.raw; }))
    return {};

  const LivenessInfo liveness = compute_liveness(ops);
  const RegisterInterferenceGraph graph = build_register_interference_graph(ops, liveness);
  return mapping_from_interference_graph(ops, registers, liveness, graph,
                                         options.allow_live_at_entry_anchor_reuse);
}

PassResult register_coalesce(const std::vector<IrOp>& ops, const PassContext& context) {
  if (ops.empty())
    return PassResult{.ops = {}, .applied = 0, .optimizations = {}};

  if (std::any_of(ops.begin(), ops.end(), [](const IrOp& op) { return op.meta.raw; }))
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<AppliedOptimization> optimizations;
  std::vector<IrOp> current = ops;
  int applied = 0;

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
