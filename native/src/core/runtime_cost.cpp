#include "mkpro/core/runtime_cost.hpp"

#include "mkpro/core/post_layout_control_flow.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <sstream>
#include <utility>

namespace mkpro::core {

namespace {

constexpr const char* kRuntimeCostPolicy =
    "sum each externally admitted entry once; unknown branch probabilities use "
    "the maximum-cost successor; each cyclic SCC is charged 8 traversals; "
    "unproved indirect flow makes the estimate unavailable";

long long saturating_add(long long left, long long right) {
  if (right > 0 && left > std::numeric_limits<long long>::max() - right)
    return std::numeric_limits<long long>::max();
  return left + right;
}

long long saturating_multiply(long long value, int factor) {
  if (value > 0 && factor > 0 &&
      value > std::numeric_limits<long long>::max() / factor) {
    return std::numeric_limits<long long>::max();
  }
  return value * factor;
}

std::string join_reasons(const std::vector<std::string>& reasons) {
  std::ostringstream out;
  for (std::size_t index = 0; index < reasons.size(); ++index) {
    if (index != 0)
      out << "; ";
    out << reasons[index];
  }
  return out.str();
}

bool entry_matches_state(const PostLayoutExternalEntryState& entry,
                         const PostLayoutExecutionState& state) {
  if (entry.entry.item_index != state.item_index ||
      entry.return_stack.size() != state.return_stack.size()) {
    return false;
  }
  for (std::size_t index = 0; index < entry.return_stack.size(); ++index) {
    if (entry.return_stack[index].address != state.return_stack[index])
      return false;
  }
  return true;
}

bool has_executable_physical_address(const std::vector<MachineItem>& items,
                                     int target_address) {
  int address = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      continue;
    if (address == target_address)
      return item.kind == MachineItemKind::Op;
    ++address;
  }
  return false;
}

} // namespace

const std::array<int, 256>& mk61_opcode_runtime_cost_centiseconds() {
  static const std::array<int, 256> costs = [] {
    // Context-free conservative timings from the MK-61 timing classes in
    // docs/reference/uf-commands.txt. Operand-dependent commands use their
    // general, slower class.
    std::array<int, 256> table{};
    table.fill(26);
    const auto set_range = [&](int first, int last, int cost) {
      for (int opcode = first; opcode <= last; ++opcode)
        table[static_cast<std::size_t>(opcode)] = cost;
    };

    set_range(0x10, 0x14, 20);
    set_range(0x15, 0x18, 120);
    set_range(0x19, 0x1e, 150);
    table[0x20] = 20;
    table[0x21] = 22;
    table[0x22] = 20;
    table[0x23] = 20;
    table[0x24] = 200;
    set_range(0x25, 0x3a, 20);
    table[0x3b] = 40;

    set_range(0x50, 0x5f, 20);
    for (const int opcode : {0x58, 0x5a, 0x5b, 0x5d})
      table[static_cast<std::size_t>(opcode)] = 40;

    set_range(0x70, 0xaf, 20);
    set_range(0xb0, 0xbf, 26);
    set_range(0xc0, 0xcf, 20);
    set_range(0xd0, 0xdf, 26);
    set_range(0xe0, 0xef, 20);
    return table;
  }();
  return costs;
}

RuntimeCostReport estimate_runtime_cost(const std::vector<MachineItem>& items,
                                        AddressSpaceModel address_space_model) {
  RuntimeCostReport report;
  report.unit = "centiseconds";
  report.loop_traversal_bound = kRuntimeCostLoopTraversalBound;
  report.policy = kRuntimeCostPolicy;
  if (items.empty()) {
    report.reason = "empty machine artifact has no executable CFG";
    return report;
  }

  PostLayoutControlFlowOptions options;
  options.address_space_model = address_space_model;
  if (has_executable_physical_address(items, 1))
    options.empty_return_target = 1;
  const AuthoritativePostLayoutControlFlow flow =
      build_post_layout_control_flow(items, options);
  report.cfg_states = static_cast<int>(flow.execution_states.size());
  report.entry_count = static_cast<int>(flow.external_entries.size());
  if (!flow.proved) {
    report.reason = "authoritative post-layout CFG is unproved";
    if (!flow.reasons.empty())
      report.reason += ": " + join_reasons(flow.reasons);
    return report;
  }
  if (flow.execution_states.empty() ||
      flow.execution_successors.size() != flow.execution_states.size()) {
    report.reason = "authoritative post-layout CFG has no complete execution-state graph";
    return report;
  }

  const std::size_t state_count = flow.execution_states.size();
  for (std::size_t state = 0; state < state_count; ++state) {
    if (flow.execution_states[state].item_index >= items.size()) {
      report.reason = "authoritative CFG references an item outside the final artifact";
      return report;
    }
    for (const std::size_t successor : flow.execution_successors[state]) {
      if (successor >= state_count) {
        report.reason = "authoritative CFG contains an invalid successor state";
        return report;
      }
    }
    if (flow.execution_successors[state].size() > 1U)
      ++report.branch_points;
  }

  std::vector<int> discovery(state_count, -1);
  std::vector<int> low_link(state_count, -1);
  std::vector<int> component_of(state_count, -1);
  std::vector<std::size_t> stack;
  std::vector<bool> on_stack(state_count, false);
  int next_discovery = 0;
  int component_count = 0;
  const auto visit = [&](const auto& self, std::size_t state) -> void {
    discovery[state] = next_discovery;
    low_link[state] = next_discovery;
    ++next_discovery;
    stack.push_back(state);
    on_stack[state] = true;
    for (const std::size_t successor : flow.execution_successors[state]) {
      if (discovery[successor] < 0) {
        self(self, successor);
        low_link[state] = std::min(low_link[state], low_link[successor]);
      } else if (on_stack[successor]) {
        low_link[state] = std::min(low_link[state], discovery[successor]);
      }
    }
    if (low_link[state] != discovery[state])
      return;
    while (true) {
      const std::size_t member = stack.back();
      stack.pop_back();
      on_stack[member] = false;
      component_of[member] = component_count;
      if (member == state)
        break;
    }
    ++component_count;
  };
  for (std::size_t state = 0; state < state_count; ++state) {
    if (discovery[state] < 0)
      visit(visit, state);
  }

  std::vector<int> component_sizes(static_cast<std::size_t>(component_count), 0);
  std::vector<bool> cyclic(static_cast<std::size_t>(component_count), false);
  std::vector<long long> local_cost(static_cast<std::size_t>(component_count), 0);
  const auto& opcode_cost = mk61_opcode_runtime_cost_centiseconds();
  for (std::size_t state = 0; state < state_count; ++state) {
    const int component = component_of[state];
    ++component_sizes[static_cast<std::size_t>(component)];
    const MachineItem& item = items[flow.execution_states[state].item_index];
    if (item.kind != MachineItemKind::Op) {
      report.reason = "authoritative CFG execution state does not identify an opcode";
      return report;
    }
    local_cost[static_cast<std::size_t>(component)] = saturating_add(
        local_cost[static_cast<std::size_t>(component)],
        opcode_cost[static_cast<std::size_t>(item.opcode & 0xff)]);
  }

  std::vector<std::vector<int>> component_successors(
      static_cast<std::size_t>(component_count));
  for (std::size_t state = 0; state < state_count; ++state) {
    const int component = component_of[state];
    for (const std::size_t successor : flow.execution_successors[state]) {
      const int successor_component = component_of[successor];
      if (successor_component == component) {
        if (successor == state ||
            component_sizes[static_cast<std::size_t>(component)] > 1) {
          cyclic[static_cast<std::size_t>(component)] = true;
        }
        continue;
      }
      component_successors[static_cast<std::size_t>(component)].push_back(
          successor_component);
    }
  }
  for (std::size_t component = 0; component < component_successors.size(); ++component) {
    auto& successors = component_successors[component];
    std::sort(successors.begin(), successors.end());
    successors.erase(std::unique(successors.begin(), successors.end()), successors.end());
    if (component_sizes[component] > 1)
      cyclic[component] = true;
    if (cyclic[component]) {
      ++report.cyclic_sccs;
      local_cost[component] =
          saturating_multiply(local_cost[component], kRuntimeCostLoopTraversalBound);
    }
  }

  std::vector<long long> memo(static_cast<std::size_t>(component_count), -1);
  const auto path_cost = [&](const auto& self, int component) -> long long {
    long long& cached = memo[static_cast<std::size_t>(component)];
    if (cached >= 0)
      return cached;
    long long successor_cost = 0;
    for (const int successor :
         component_successors[static_cast<std::size_t>(component)]) {
      successor_cost = std::max(successor_cost, self(self, successor));
    }
    cached = saturating_add(local_cost[static_cast<std::size_t>(component)],
                            successor_cost);
    return cached;
  };

  if (flow.external_entries.empty()) {
    report.reason = "authoritative CFG exposes no externally admitted entry";
    return report;
  }
  long long total = 0;
  for (const PostLayoutExternalEntryState& entry : flow.external_entries) {
    long long entry_cost = -1;
    for (std::size_t state = 0; state < state_count; ++state) {
      if (!entry_matches_state(entry, flow.execution_states[state]))
        continue;
      entry_cost =
          std::max(entry_cost, path_cost(path_cost, component_of[state]));
    }
    if (entry_cost < 0) {
      report.reason = "an externally admitted entry has no exact execution-state match";
      return report;
    }
    total = saturating_add(total, entry_cost);
  }

  report.available = true;
  report.score = total;
  report.reason = "estimated from the authoritative bounded post-layout CFG";
  return report;
}

bool runtime_cost_candidate_is_better(std::size_t candidate_cells,
                                      const RuntimeCostReport& candidate,
                                      std::size_t incumbent_cells,
                                      const RuntimeCostReport& incumbent) {
  if (candidate_cells != incumbent_cells)
    return candidate_cells < incumbent_cells;
  return candidate.available && incumbent.available &&
         candidate.score < incumbent.score;
}

std::optional<std::string> runtime_cost_tie_break_reason(
    std::size_t candidate_cells, const RuntimeCostReport& candidate,
    std::size_t incumbent_cells, const RuntimeCostReport& incumbent) {
  if (candidate_cells != incumbent_cells || !candidate.available ||
      !incumbent.available || candidate.score >= incumbent.score) {
    return std::nullopt;
  }
  return "equal " + std::to_string(candidate_cells) +
         "-cell proof-valid candidates; selected estimated runtime " +
         std::to_string(candidate.score) + " " + candidate.unit + " over " +
         std::to_string(incumbent.score) +
         " using the authoritative bounded-CFG policy";
}

} // namespace mkpro::core
