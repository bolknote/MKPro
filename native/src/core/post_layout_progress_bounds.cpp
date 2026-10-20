#include "mkpro/core/post_layout_progress_bounds.hpp"

#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <set>
#include <utility>

namespace mkpro::core {
namespace progress_detail {

enum class Kind { Unknown, Current, Union, Previous, Difference };
struct Token {
  Kind kind = Kind::Unknown;
  std::size_t origin = 0;
  bool operator==(const Token&) const = default;
};
struct State {
  // The carrier itself is read as Current, independently of this alias table.
  std::array<Token, 19> values{}; // X,Y,Z,T,R0..Re
  bool typing = false;
  bool operator==(const State&) const = default;
};

State merge(const State& left, const State& right) {
  State result;
  for (std::size_t i = 0; i < result.values.size(); ++i)
    if (left.values[i] == right.values[i])
      result.values[i] = left.values[i];
  result.typing = left.typing && right.typing;
  return result;
}
bool known_opcode(int code) {
  return (code >= 0 && code <= 0x3b && code != 0x1f) ||
      (code >= 0x40 && code <= 0x4e) || (code >= 0x50 && code <= 0x5e) ||
      (code >= 0x60 && code <= 0x6e) ||
      (code >= 0x70 && code <= 0xee && (code & 15) != 15);
}
bool indirect(int code) { return code >= 0x70 && code <= 0xee; }

} // namespace progress_detail

MonotoneProgressProof prove_post_layout_monotone_progress(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const std::vector<PreloadReport>& preloads, std::size_t maximum_work) {
  using namespace progress_detail;
  MonotoneProgressProof result;
  const auto size = flow.execution_states.size();
  if (!flow.proved || automatic_entry_lift.size() != size ||
      flow.execution_successors.size() != size || flow.execution_edges.size() != size ||
      !std::any_of(items.begin(), items.end(), [](const auto& item) {
        return item.kind == MachineItemKind::Op && item.opcode == 0x38 && !item.raw;
      }))
    return result;
  const auto boundaries = prove_post_layout_input_boundaries(items, flow);
  if (!boundaries.proved)
    return result;
  std::vector<std::vector<std::size_t>> predecessors(size);
  for (std::size_t state = 0; state < size; ++state)
    for (const auto next : flow.execution_successors[state])
      predecessors[next].push_back(state);
  const auto spend = [&] {
    if (result.work >= maximum_work) {
      result.exhausted = true;
      return false;
    }
    ++result.work;
    return true;
  };
  std::set<int> carriers;
  std::map<int, std::size_t> preload_counts;
  for (const auto& preload : preloads) ++preload_counts[register_index(preload.register_name)];
  for (const auto& preload : preloads) {
    const int reg = register_index(preload.register_name);
    if (reg >= 0 && reg < 15 && preload_counts[reg] == 1U && !preload.setup_expression &&
        !preload.setup_target_name && bound_ordinary_decimal(preload.value))
      carriers.insert(reg);
  }
  for (const int carrier : carriers) {
    bool legal = true;
    bool has_store = false;
    for (const auto& execution : flow.execution_states) {
      const auto& item = items[execution.item_index];
      const int code = item.opcode;
      if (item.raw || !known_opcode(code)) { legal = false; break; }
      if (code == 0x40 + carrier) {
        has_store = true;
        if (item.manual_interaction) { legal = false; break; }
      }
      constexpr std::array<int, 4> loops{0x5d, 0x5b, 0x58, 0x5a};
      if (carrier < 4 && code == loops[static_cast<std::size_t>(carrier)]) {
        legal = false; break;
      }
      if (indirect(code) && (code & 15) == carrier) { legal = false; break; }
      if ((code & 0xf0) == 0xb0) {
        const auto targets = flow.indirect_memory_targets.find(execution.item_index);
        if (targets == flow.indirect_memory_targets.end() ||
            std::find(targets->second.begin(), targets->second.end(), carrier) !=
                targets->second.end()) { legal = false; break; }
      }
    }
    if (!legal || !has_store)
      continue;
    std::vector<std::optional<State>> incoming(size), outgoing(size);
    std::deque<std::size_t> pending;
    std::vector<bool> queued(size, false);
    const auto enter = [&](std::size_t state, State value) {
      if (boundaries.input_boundary_states[state]) {
        std::fill_n(value.values.begin(), 4, Token{});
        value.typing = false;
      }
      const auto next = incoming[state] ? merge(*incoming[state], value) : value;
      if (!incoming[state] || *incoming[state] != next) {
        incoming[state] = next;
        if (!queued[state]) {
          queued[state] = true;
          pending.push_back(state);
        }
      }
    };
    for (const auto state : boundaries.main_states)
      enter(state, State{});
    while (!pending.empty() && legal) {
      if (!spend()) return result;
      const auto state = pending.front();
      pending.pop_front();
      queued[state] = false;
      State next = *incoming[state];
      const auto at = flow.execution_states[state].item_index;
      const int code = items[at].opcode, family = code & 0xf0, reg = code & 15;
      const auto old = next.values;
      const auto pop = [&] {
        next.values[1] = old[2]; next.values[2] = old[3]; next.values[3] = old[3];
      };
      const auto push = [&](Token value) {
        next.values[3] = old[2]; next.values[2] = old[1];
        next.values[1] = old[0]; next.values[0] = value;
      };
      const bool typing = next.typing;
      next.typing = false;
      if (code >= 0 && code <= 9) {
        const bool after_recall = !predecessors[state].empty() &&
            !boundaries.input_boundary_states[state] &&
            std::all_of(predecessors[state].begin(), predecessors[state].end(),
                [&](std::size_t previous) {
                  const auto& op = items[flow.execution_states[previous].item_index];
                  return !op.raw && !op.manual_interaction &&
                         op.opcode >= 0x60 && op.opcode <= 0x6e;
                });
        if (typing) next.values[0] = {};
        else if (automatic_entry_lift[state] || after_recall) push({});
        else std::fill_n(next.values.begin(), 4, Token{});
        next.typing = true;
      } else if (family == 0x40) {
        if (reg == carrier) {
          if (old[0].kind == Kind::Union) {
            for (auto& value : next.values) {
              if (value.kind == Kind::Current) value = {Kind::Previous, state};
              else if (value == old[0]) value = {Kind::Current, 0};
              else value = {};
            }
          } else if (old[0].kind != Kind::Current) {
            legal = false;
          }
        }
        next.values[static_cast<std::size_t>(4 + reg)] = next.values[0];
      } else if (family == 0x60) {
        push(reg == carrier ? Token{Kind::Current, 0}
                            : old[static_cast<std::size_t>(4 + reg)]);
      } else if (indirect(code)) {
        next.values[static_cast<std::size_t>(4 + reg)] = {};
        if (family == 0xb0) {
          for (const int target : flow.indirect_memory_targets.at(at)) {
            if (target < 0 || target >= 15) { legal = false; break; }
            next.values[static_cast<std::size_t>(4 + target)] = {};
          }
        } else if (family == 0xd0) {
          push({});
        }
      } else if (code == 0x38) {
        const bool includes = old[0].kind == Kind::Current || old[0].kind == Kind::Union ||
                              old[1].kind == Kind::Current || old[1].kind == Kind::Union;
        // A producer can run again before a carrier write. Kill its previous
        // dynamic instance, including copies in registers, before naming X.
        for (auto& value : next.values)
          if (value.kind == Kind::Union && value.origin == state) value = {};
        next.values[0] = includes ? Token{Kind::Union, state} : Token{};
        // Native logical operations retain Y, unlike ordinary arithmetic.
      } else if (code == 0x11) {
        const bool compares =
            (old[0].kind == Kind::Current && old[1].kind == Kind::Previous) ||
            (old[1].kind == Kind::Current && old[0].kind == Kind::Previous);
        next.values[0] = compares ? Token{Kind::Difference, state} : Token{};
        pop();
      } else if (code == 0x14) {
        next.values[0] = old[1]; next.values[1] = old[0];
      } else if (code == 0x25) {
        for (std::size_t i = 0; i < 4; ++i) next.values[i] = old[(i + 1) % 4];
      } else if (code == 0x0e || code == 0x0f) {
        push(code == 0x0e ? old[0] : Token{});
      } else if (code == 0x0a || code == 0x0c) {
        std::fill_n(next.values.begin(), 4, Token{});
      } else if (code == 0x50 || code == 0x51 || code == 0x52 ||
                 code == 0x53 || code == 0x54 || (code >= 0x57 && code <= 0x5e)) {
        constexpr std::array<int, 4> loops{0x5d, 0x5b, 0x58, 0x5a};
        for (std::size_t i = 0; i < loops.size(); ++i)
          if (code == loops[i]) next.values[4 + i] = {};
      } else {
        next.values[0] = {};
        const auto effect = opcode_by_code(code).stack_effect;
        if (effect == StackEffect::ConsumeYDrop || effect == StackEffect::Exposes) pop();
        else if (effect == StackEffect::Shifts) push({});
        else if (effect != StackEffect::Preserves && effect != StackEffect::ConsumeYKeep)
          std::fill_n(next.values.begin(), 4, Token{});
      }
      if (!legal) break;
      if (outgoing[state] && *outgoing[state] == next)
        continue;
      outgoing[state] = next;
      for (const auto successor : flow.execution_successors[state])
        enter(successor, next);
    }
    if (!legal) continue;

    std::set<std::size_t> updates;
    for (std::size_t state = 0; state < size; ++state)
      if (incoming[state] &&
          items[flow.execution_states[state].item_index].opcode == 0x40 + carrier &&
          incoming[state]->values[0].kind == Kind::Union)
        updates.insert(state);
    if (updates.empty()) continue;
    for (std::size_t state = 0; state < size; ++state) {
      if (!incoming[state] || incoming[state]->values[0].kind != Kind::Difference)
        continue;
      const int code = items[flow.execution_states[state].item_index].opcode;
      // Equality predicates have the same truth polarity in direct and
      // indirect form. The authoritative edge kind keeps their targets
      // separate; any selector use of the carrier was rejected above.
      const int family = code & 0xf0;
      const bool zero = code == 0x5e || family == 0xe0;
      if (code != 0x57 && code != 0x5e && family != 0x70 && family != 0xe0) continue;
      for (const auto& edge : flow.execution_edges[state]) {
        const auto growth_kind = !zero ? PostLayoutExecutionEdgeKind::Fallthrough
            : code >= 0x70 ? PostLayoutExecutionEdgeKind::IndirectTarget
                           : PostLayoutExecutionEdgeKind::DirectTarget;
        if (edge.kind != growth_kind) continue;
        const auto reaches_guard = [&](bool stop_at_update) {
          std::vector<bool> seen(size, false);
          std::deque<std::size_t> queue{edge.target_state};
          while (!queue.empty()) {
            if (!spend()) return true; // exhaustion never discharges a path
            const auto at_state = queue.front(); queue.pop_front();
            if (at_state == state) return true;
            if (seen[at_state] || (stop_at_update && updates.contains(at_state))) continue;
            seen[at_state] = true;
            for (const auto next_state : flow.execution_successors[at_state])
              queue.push_back(next_state);
          }
          return false;
        };
        if (reaches_guard(true) || result.exhausted) continue;
        if (!reaches_guard(false) || result.exhausted) continue;
        result.witness = MonotoneProgressWitness{
            carrier, state, edge.target_state, edge.kind, 29, {updates.begin(), updates.end()}};
        return result;
      }
    }
  }
  return result;
}

std::optional<ProgressExpandedControlFlow> expand_post_layout_progress(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const MonotoneProgressWitness& witness, std::size_t maximum_states) {
  const auto size = flow.execution_states.size();
  if (!flow.proved || automatic_entry_lift.size() != size ||
      flow.execution_edges.size() != size || flow.execution_successors.size() != size ||
      witness.source_state >= size || witness.target_state >= size ||
      witness.maximum_traversals != 29 ||
      std::find(flow.execution_successors[witness.source_state].begin(),
                flow.execution_successors[witness.source_state].end(),
                witness.target_state) == flow.execution_successors[witness.source_state].end())
    return std::nullopt;
  std::vector<std::vector<std::size_t>> predecessors(size);
  for (std::size_t state = 0; state < size; ++state)
    for (const auto next : flow.execution_successors[state]) {
      if (next >= size) return std::nullopt;
      predecessors[next].push_back(state);
    }
  const auto reachable = [&](const auto& edges) {
    std::vector<bool> seen(size, false);
    std::deque<std::size_t> pending{witness.source_state};
    while (!pending.empty()) {
      const auto state = pending.front(); pending.pop_front();
      if (seen[state]) continue;
      seen[state] = true;
      for (const auto next : edges[state]) pending.push_back(next);
    }
    return seen;
  };
  const auto forward = reachable(flow.execution_successors), backward = reachable(predecessors);
  if (!forward[witness.target_state] || !backward[witness.target_state])
    return std::nullopt;
  const auto boundaries = prove_post_layout_input_boundaries(items, flow);
  if (!boundaries.proved) return std::nullopt;
  ProgressExpandedControlFlow result{flow, {}, {}};
  auto& out = result.control;
  out.execution_states.clear(); out.execution_successors.clear(); out.execution_edges.clear();
  std::vector<std::vector<std::size_t>> copies(size);
  for (std::size_t state = 0; state < size; ++state) {
    const auto count = forward[state] && backward[state] ? witness.maximum_traversals + 1U : 1U;
    if (count > maximum_states || out.execution_states.size() > maximum_states - count)
      return std::nullopt;
    for (std::size_t visit = 0; visit < count; ++visit) {
      copies[state].push_back(out.execution_states.size());
      out.execution_states.push_back(flow.execution_states[state]);
      result.automatic_entry_lift.push_back(automatic_entry_lift[state]);
      result.analysis_order.push_back(forward[state] && backward[state] ? visit + 1U
          : backward[state] ? 0U : witness.maximum_traversals + 2U);
      out.execution_successors.emplace_back();
      out.execution_edges.emplace_back();
    }
  }
  for (std::size_t state = 0; state < size; ++state)
    for (std::size_t visit = 0; visit < copies[state].size(); ++visit) {
      const auto mapped = copies[state][visit];
      for (const auto& edge : flow.execution_edges[state]) {
        const bool growth = state == witness.source_state && edge.target_state == witness.target_state &&
                            edge.kind == witness.edge_kind;
        if (growth && visit == witness.maximum_traversals) continue;
        const auto next_visit = copies[edge.target_state].size() == 1U ? 0U : visit + (growth ? 1U : 0U);
        const auto target = copies[edge.target_state].at(next_visit);
        auto moved = edge; moved.target_state = target;
        out.execution_edges[mapped].push_back(moved);
        out.execution_successors[mapped].push_back(target);
      }
      auto& next = out.execution_successors[mapped];
      std::sort(next.begin(), next.end());
      next.erase(std::unique(next.begin(), next.end()), next.end());
    }
  // Do not export unreachable counter copies: a bottom fact at such a copy
  // is not an unknown real path and must not poison a joined item query.
  std::vector<bool> live(out.execution_states.size(), false);
  std::deque<std::size_t> pending;
  for (const auto state : boundaries.main_states) pending.push_back(copies[state][0]);
  while (!pending.empty()) {
    const auto state = pending.front(); pending.pop_front();
    if (live[state]) continue;
    live[state] = true;
    for (const auto next : out.execution_successors[state]) pending.push_back(next);
  }
  std::vector<std::size_t> remap(live.size(), 0);
  ProgressExpandedControlFlow pruned{flow, {}, {}};
  auto& final = pruned.control;
  final.execution_states.clear(); final.execution_successors.clear(); final.execution_edges.clear();
  for (std::size_t state = 0; state < live.size(); ++state)
    if (live[state]) {
      remap[state] = final.execution_states.size();
      final.execution_states.push_back(out.execution_states[state]);
      pruned.automatic_entry_lift.push_back(result.automatic_entry_lift[state]);
      pruned.analysis_order.push_back(result.analysis_order[state]);
      final.execution_successors.emplace_back(); final.execution_edges.emplace_back();
    }
  for (std::size_t state = 0; state < live.size(); ++state)
    if (live[state]) {
      for (const auto next : out.execution_successors[state])
        final.execution_successors[remap[state]].push_back(remap[next]);
      for (auto edge : out.execution_edges[state]) {
        edge.target_state = remap[edge.target_state];
        final.execution_edges[remap[state]].push_back(edge);
      }
    }
  final.explored_states = final.execution_states.size();
  return pruned;
}
} // namespace mkpro::core
