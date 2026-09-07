#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/opcodes.hpp"

#include "mkpro/core/post_layout_control_flow.hpp"

#include <array>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>

namespace mkpro::core::passes {
namespace {

constexpr int kRegisters = 15;
constexpr std::array<int, 4> kLoopOpcodes{0x5d, 0x5b, 0x58, 0x5a};
using Node = std::size_t;
using Nodes = std::set<Node>;
using Reaching = std::array<Nodes, kRegisters>;

std::optional<int> counter_register(const IrOp& op) {
  if (op.kind != IrKind::Loop || op.counter.size() != 2 || op.counter[0] != 'L' ||
      op.counter[1] < '0' || op.counter[1] > '3')
    return std::nullopt;
  const int reg = op.counter[1] - '0';
  if (op.opcode != kLoopOpcodes[static_cast<std::size_t>(reg)])
    return std::nullopt;
  return reg;
}

bool append(Nodes& into, const Nodes& from) {
  const auto before = into.size();
  into.insert(from.begin(), from.end());
  return before != into.size();
}

std::optional<int> physical(const std::string& name) {
  if (name.size() != 1)
    return std::nullopt;
  if (name[0] >= '0' && name[0] <= '9')
    return name[0] - '0';
  if (name[0] >= 'a' && name[0] <= 'e')
    return name[0] - 'a' + 10;
  return std::nullopt;
}

bool symbolic_flow(const IrOp& op) {
  if (op.kind == IrKind::OrphanAddress || op.target_meta.formal_opcode.has_value())
    return false;
  if ((op.kind == IrKind::Jump || op.kind == IrKind::CondJump ||
       op.kind == IrKind::Call || op.kind == IrKind::Loop) &&
      !std::holds_alternative<std::string>(op.target))
    return false;
  if (op.meta.indirect_flow_targets.has_value())
    for (const auto& target : *op.meta.indirect_flow_targets)
      if (!std::holds_alternative<std::string>(target))
        return false;
  return true;
}

struct Webs {
  std::vector<Node> parents;
  std::vector<int> colors;
  std::vector<bool> fixed;
  std::vector<std::set<int>> allowed;

  Node add(int color, bool anchored = false) {
    const Node id = parents.size();
    parents.push_back(id);
    colors.push_back(color);
    fixed.push_back(anchored);
    allowed.emplace_back();
    for (int reg = 0; reg < kRegisters; ++reg)
      allowed.back().insert(reg);
    return id;
  }

  Node root(Node node) {
    if (parents[node] != node)
      parents[node] = root(parents[node]);
    return parents[node];
  }

  Node join(Node a, Node b, int color) {
    a = root(a);
    b = root(b);
    if (a > b)
      std::swap(a, b);
    parents[b] = a;
    colors[a] = color;
    fixed[a] = fixed[a] || fixed[b];
    for (auto candidate = allowed[a].begin(); candidate != allowed[a].end(); ) {
      if (!allowed[b].contains(*candidate))
        candidate = allowed[a].erase(candidate);
      else
        ++candidate;
    }
    return a;
  }

  void same_register(const Nodes& nodes) {
    if (nodes.empty())
      return;
    Node first = root(*nodes.begin());
    for (const Node node : nodes)
      first = join(first, node, colors[first]);
  }
};

struct Copy {
  std::size_t store = 0;
  Node source = 0;
  Node destination = 0;
};

} // namespace

PassResult register_web_copy_coalesce(const std::vector<IrOp>& ops,
                                      const PassContext& context) {
  const bool trace = std::getenv("MKPRO_NATIVE_TRACE_REGISTER_WEBS") != nullptr;
  const auto unchanged = [&](const char* reason = "no admissible copy merge") {
    if (trace && context.options.coalesce_copies)
      std::cerr << "[register-web-copy-coalesce] skipped: " << reason << '\n';
    return PassResult{.ops = ops};
  };
  if (!context.options.coalesce_copies || ops.empty())
    return unchanged();
  bool has_copy = false;
  for (std::size_t i = 1; i < ops.size(); ++i)
    has_copy = has_copy || (ops[i - 1].kind == IrKind::Recall &&
                            ops[i].kind == IrKind::Store);
  if (!has_copy)
    return unchanged("no adjacent recall/store");

  std::vector<RegisterEffects> effects;
  std::map<int, std::size_t> instructions;
  int address = 0;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    const auto& op = ops[i];
    if (op.meta.raw || op.meta.logical_register_analysis || !symbolic_flow(op)) {
      if (trace)
        std::cerr << "[register-web-copy-coalesce] opaque-index=" << i
                  << " opcode=" << op.opcode << '\n';
      return unchanged("opaque, logical or frozen operation");
    }
    if (!op.register_name.empty() && !physical(op.register_name).has_value())
      return unchanged("nonstandard register operand");
    if (op.kind == IrKind::Loop && !counter_register(op).has_value())
      return unchanged("inconsistent loop counter operand");
    effects.push_back(register_effects(op));
    const auto& effect = effects.back();
    if (effect.uses_all_registers || effect.may_define_any_register)
      return unchanged("unknown register effects");
    for (const auto* names : {&effect.uses, &effect.must_defs, &effect.may_defs})
      for (const auto& name : *names)
        if (!physical(name).has_value())
          return unchanged();
    if (op.kind != IrKind::Label)
      instructions.emplace(address, i);
    address += cells_per_op(op);
  }
  const auto machine = lower_ir_to_machine(ops);
  auto flow = build_post_layout_control_flow(machine);
  if (!flow.proved) {
    PostLayoutControlFlowOptions options;
    options.empty_return_target = IrTarget{1};
    flow = build_post_layout_control_flow(machine, options);
  }
  if (!flow.proved || flow.execution_states.empty()) {
    if (trace && !flow.reasons.empty())
      std::cerr << "[register-web-copy-coalesce] CFG: " << flow.reasons.front() << '\n';
    return unchanged("exact execution graph unavailable");
  }

  std::vector<std::size_t> sites;
  for (const auto& state : flow.execution_states) {
    const auto found = instructions.find(state.address);
    if (found == instructions.end())
      return unchanged();
    sites.push_back(found->second);
  }

  Webs webs;
  for (int reg = 0; reg < kRegisters; ++reg)
    webs.add(reg, true); // Preserve every possible setup-time entry value.
  std::vector<std::map<int, Node>> definitions(ops.size());
  for (std::size_t i = 0; i < ops.size(); ++i) {
    for (const auto* names : {&effects[i].must_defs, &effects[i].may_defs})
      for (const auto& name : *names) {
        const int reg = *physical(name);
        if (!definitions[i].contains(reg))
          definitions[i][reg] = webs.add(reg);
      }
  }
  std::vector<Reaching> incoming(sites.size());
  std::vector<bool> queued(sites.size(), false);
  std::deque<std::size_t> pending{0};
  queued[0] = true;
  for (int reg = 0; reg < kRegisters; ++reg)
    incoming[0][static_cast<std::size_t>(reg)].insert(static_cast<Node>(reg));
  while (!pending.empty()) {
    const auto state = pending.front();
    pending.pop_front();
    queued[state] = false;
    const auto site = sites[state];
    Reaching out = incoming[state];
    for (const auto& name : effects[site].must_defs) {
      const int reg = *physical(name);
      out[static_cast<std::size_t>(reg)] = {definitions[site].at(reg)};
    }
    for (const auto& name : effects[site].may_defs) {
      const int reg = *physical(name);
      out[static_cast<std::size_t>(reg)].insert(definitions[site].at(reg));
    }
    for (const auto next : flow.execution_successors[state]) {
      bool changed = false;
      for (int reg = 0; reg < kRegisters; ++reg)
        changed = append(incoming[next][static_cast<std::size_t>(reg)],
                         out[static_cast<std::size_t>(reg)]) || changed;
      if (changed && !queued[next]) {
        pending.push_back(next);
        queued[next] = true;
      }
    }
  }

  // An emitted instruction has one physical operand, even when reached from
  // different callers. Join all definitions that can reach that static use.
  std::vector<std::map<int, Nodes>> uses(ops.size());
  std::vector<std::vector<std::size_t>> predecessors(sites.size());
  for (std::size_t state = 0; state < sites.size(); ++state) {
    const auto site = sites[state];
    for (const auto& name : effects[site].uses) {
      const int reg = *physical(name);
      append(uses[site][reg], incoming[state][static_cast<std::size_t>(reg)]);
      if (definitions[site].contains(reg))
        uses[site][reg].insert(definitions[site].at(reg));
    }
    for (const auto& name : effects[site].may_defs) {
      const int reg = *physical(name);
      append(uses[site][reg], incoming[state][static_cast<std::size_t>(reg)]);
      uses[site][reg].insert(definitions[site].at(reg));
    }
    for (const auto next : flow.execution_successors[state])
      predecessors[next].push_back(state);
  }
  for (const auto& by_register : uses)
    for (const auto& [reg, nodes] : by_register) {
      (void)reg;
      webs.same_register(nodes);
    }

  const auto movable_counter = [&](std::size_t site) {
    const auto& op = ops[site];
    return counter_register(op).has_value() && !has_rewrite_barrier(op) &&
           !is_display_focus_sensitive(op) && op.meta.roles.empty() &&
           op.target_meta.roles.empty();
  };
  const auto anchored = [&](std::size_t site) {
    const auto& op = ops[site];
    if (movable_counter(site))
      return false;
    return (op.kind != IrKind::Store && op.kind != IrKind::Recall) ||
           has_rewrite_barrier(op) || is_display_focus_sensitive(op);
  };
  for (std::size_t site = 0; site < ops.size(); ++site) {
    if (movable_counter(site)) {
      // FL0..FL3 differ only in the register they decrement. This is a
      // constraint on this definition web, not on every epoch of its name.
      for (const auto& [reg, nodes] : uses[site]) {
        (void)reg;
        for (const Node node : nodes)
          webs.allowed[webs.root(node)] = {0, 1, 2, 3};
      }
      for (const auto& [reg, node] : definitions[site]) {
        (void)reg;
        webs.allowed[webs.root(node)] = {0, 1, 2, 3};
      }
    }
    if (!anchored(site))
      continue;
    for (const auto& [reg, nodes] : uses[site]) {
      (void)reg;
      for (const Node node : nodes)
        webs.fixed[webs.root(node)] = true;
    }
    for (const auto& [reg, node] : definitions[site]) {
      (void)reg;
      webs.fixed[webs.root(node)] = true;
    }
  }

  std::vector<Copy> copies;
  for (std::size_t site = 1; site < ops.size(); ++site) {
    if (ops[site - 1].kind != IrKind::Recall || ops[site].kind != IrKind::Store ||
        anchored(site - 1) || anchored(site))
      continue;
    const int source = *physical(ops[site - 1].register_name);
    const int destination = *physical(ops[site].register_name);
    if (uses[site - 1][source].empty())
      continue;
    bool reached = false;
    bool exact_copy = true;
    for (std::size_t state = 0; state < sites.size(); ++state) {
      if (sites[state] != site)
        continue;
      reached = true;
      if (state == 0 || predecessors[state].empty())
        exact_copy = false;
      for (const auto prior : predecessors[state])
        exact_copy = exact_copy && sites[prior] == site - 1;
    }
    if (reached && exact_copy)
      copies.push_back({site, webs.root(*uses[site - 1][source].begin()),
                        webs.root(definitions[site].at(destination))});
  }
  if (copies.empty())
    return unchanged("no exact movable copy predecessor");

  std::vector<Nodes> live_in(sites.size()), live_out(sites.size());
  std::vector<Nodes> read(sites.size()), must_def(sites.size()), all_def(sites.size());
  for (std::size_t state = 0; state < sites.size(); ++state) {
    const auto site = sites[state];
    for (const auto& name : effects[site].uses)
      for (const Node node : incoming[state][static_cast<std::size_t>(*physical(name))])
        read[state].insert(webs.root(node));
    for (const auto& name : effects[site].must_defs)
      must_def[state].insert(webs.root(definitions[site].at(*physical(name))));
    for (const auto& [reg, node] : definitions[site]) {
      (void)reg;
      all_def[state].insert(webs.root(node));
    }
  }
  pending.clear();
  for (std::size_t state = 0; state < sites.size(); ++state) {
    pending.push_back(state);
    queued[state] = true;
  }
  while (!pending.empty()) {
    const auto state = pending.front();
    pending.pop_front();
    queued[state] = false;
    Nodes out;
    for (const auto next : flow.execution_successors[state])
      append(out, live_in[next]);
    Nodes in = out;
    for (const Node node : must_def[state])
      in.erase(node);
    append(in, read[state]);
    live_out[state] = std::move(out);
    if (in == live_in[state])
      continue;
    live_in[state] = std::move(in);
    for (const auto prior : predecessors[state])
      if (!queued[prior]) {
        pending.push_back(prior);
        queued[prior] = true;
      }
  }

  std::map<std::size_t, Node> copy_source;
  for (const auto& copy : copies)
    copy_source[copy.store] = copy.source;
  std::set<std::pair<Node, Node>> interference;
  for (std::size_t state = 0; state < sites.size(); ++state)
    for (const Node defined : all_def[state])
      for (const Node live : live_out[state]) {
        // A copy creates an equal value. Other definitions still conflict
        // with a live source/destination and therefore prevent divergence.
        const auto copy = copy_source.find(sites[state]);
        if (defined != live && (copy == copy_source.end() || copy->second != live))
          interference.emplace(std::min(defined, live), std::max(defined, live));
      }

  std::set<std::size_t> removed;
  int joint_merges = 0;
  int recolored_assignments = 0;
  const auto joint_coloring = [&](Node a, Node b, std::size_t copy_store)
      -> std::optional<std::vector<int>> {
    if (webs.fixed[a] && webs.fixed[b] && webs.colors[a] != webs.colors[b])
      return std::nullopt;
    const auto name = [&](Node node) {
      node = webs.root(node);
      return "web:" + std::to_string(node == b ? a : node);
    };
    RegisterInterferenceGraph graph;
    PrecoloredRegisterAllocationOptions allocation;
    allocation.color_count = kRegisters;
    allocation.greedy_only = true;
    for (Node node = 0; node < webs.parents.size(); ++node) {
      if (webs.root(node) != node)
        continue;
      const std::string key = name(node);
      graph.neighbors.try_emplace(key);
      allocation.preferred_colors.try_emplace(key, webs.colors[node]);
      const auto [domain, inserted_domain] =
          allocation.allowed_colors.emplace(key, webs.allowed[node]);
      if (!inserted_domain) {
        for (auto color = domain->second.begin(); color != domain->second.end(); ) {
          if (!webs.allowed[node].contains(*color))
            color = domain->second.erase(color);
          else
            ++color;
        }
      }
      if (domain->second.empty())
        return std::nullopt;
      if (webs.fixed[node]) {
        const auto [entry, inserted] = allocation.fixed_colors.emplace(key, webs.colors[node]);
        if (!inserted && entry->second != webs.colors[node])
          return std::nullopt;
      }
    }
    allocation.preferred_colors[name(a)] = webs.fixed[b] ? webs.colors[b] : webs.colors[a];
    const auto add_edge = [&](const std::string& left, const std::string& right) {
      graph.neighbors[left].insert(right);
      graph.neighbors[right].insert(left);
    };
    for (const auto& [left, right] : interference) {
      const Node x = webs.root(left), y = webs.root(right);
      if (x == y)
        continue;
      if (name(x) == name(y))
        return std::nullopt;
      add_edge(name(x), name(y));
    }
    // Pool immutability is a whole-program constraint: later passes may add
    // reads which are absent from this execution graph. A removed copy does
    // not write the pool, but every surviving retargeted definition does.
    for (const auto& [reg, value] : context.options.preloaded_constant_registers) {
      (void)value;
      const auto color = physical(reg);
      if (!color.has_value())
        return std::nullopt;
      const std::string anchor = "pool:" + reg;
      graph.neighbors.try_emplace(anchor);
      allocation.fixed_colors[anchor] = *color;
      for (std::size_t site = 0; site < definitions.size(); ++site) {
        if (site == copy_store || removed.contains(site))
          continue;
        for (const auto& [original, node] : definitions[site])
          if (original != *color)
            add_edge(anchor, name(node));
      }
    }
    const auto colors = color_precolored_register_graph(graph, allocation);
    if (!colors.has_value())
      return std::nullopt;
    auto result = webs.colors;
    for (Node node = 0; node < webs.parents.size(); ++node) {
      if (webs.root(node) != node)
        continue;
      const int color = colors->at(name(node));
      if (color < 0 || color >= kRegisters ||
          !webs.allowed[node].contains(color) ||
          (webs.fixed[node] && color != webs.colors[node]))
        return std::nullopt;
      result[node] = color;
    }
    for (const auto& [left, right] : interference) {
      const Node x = webs.root(left), y = webs.root(right);
      if (x != y && result[x] == result[y])
        return std::nullopt;
    }
    return result;
  };
  const auto accepts = [&](Node a, Node b, int color, std::size_t copy_store) {
    if (!webs.allowed[a].contains(color) || !webs.allowed[b].contains(color))
      return false;
    if ((webs.fixed[a] && webs.colors[a] != color) ||
        (webs.fixed[b] && webs.colors[b] != color))
      return false;
    const std::string register_name(1, "0123456789abcde"[color]);
    if (context.options.preloaded_constant_registers.contains(register_name)) {
      // Later passes may introduce new recalls of this compiler-owned pool
      // entry. Existing CFG liveness alone cannot authorize making it mutable.
      // The disappearing copy is harmless; any other newly retargeted write
      // would invalidate the pool's whole-program constant contract.
      for (std::size_t site = 0; site < definitions.size(); ++site) {
        if (site == copy_store || removed.contains(site))
          continue;
        for (const auto& [original_register, node] : definitions[site]) {
          const Node root = webs.root(node);
          if ((root == a || root == b) && original_register != color)
            return false;
        }
      }
    }
    for (const auto& [left, right] : interference) {
      const Node x = webs.root(left);
      const Node y = webs.root(right);
      const bool x_inside = x == a || x == b;
      const bool y_inside = y == a || y == b;
      if (x != y && x_inside && y_inside)
        return false;
      if (x_inside && !y_inside && webs.colors[y] == color)
        return false;
      if (y_inside && !x_inside && webs.colors[x] == color)
        return false;
    }
    return true;
  };
  for (const auto& copy : copies) {
    const Node source = webs.root(copy.source);
    const Node destination = webs.root(copy.destination);
    for (const int color : {webs.colors[source], webs.colors[destination]}) {
      if (!accepts(source, destination, color, copy.store))
        continue;
      webs.join(source, destination, color);
      removed.insert(copy.store);
      break;
    }
    if (removed.contains(copy.store))
      continue;
    // Local coalescing tries the two current colors. If a different epoch
    // occupies both choices, recolor the existing web graph as a whole. This
    // never splits a shared static operand or changes a hardware anchor.
    if (auto colors = joint_coloring(source, destination, copy.store)) {
      for (Node node = 0; node < webs.parents.size(); ++node)
        if (webs.root(node) == node && webs.colors[node] != colors->at(node))
          ++recolored_assignments;
      webs.colors = std::move(*colors);
      webs.join(source, destination, webs.colors[source]);
      removed.insert(copy.store);
      ++joint_merges;
    }
  }
  if (removed.empty())
    return unchanged();

  std::vector<IrOp> result;
  int relocated_counters = 0;
  for (std::size_t site = 0; site < ops.size(); ++site) {
    if (removed.contains(site))
      continue;
    IrOp op = ops[site];
    if (op.kind == IrKind::Store || op.kind == IrKind::Recall) {
      const int reg = *physical(op.register_name);
      std::optional<Node> node;
      if (op.kind == IrKind::Store)
        node = definitions[site].at(reg);
      else if (!uses[site][reg].empty())
        node = *uses[site][reg].begin();
      if (node.has_value()) {
        const int color = webs.colors[webs.root(*node)];
        op.register_name = std::string(1, "0123456789abcde"[color]);
        op.opcode = (op.kind == IrKind::Store ? 0x40 : 0x60) + color;
        op.meta.mnemonic = opcode_by_code(op.opcode).name;
      }
    }
    if (const auto original = counter_register(op)) {
      const Node node = definitions[site].at(*original);
      const int color = webs.colors[webs.root(node)];
      if (color != *original) {
        op.counter = "L" + std::to_string(color);
        op.opcode = kLoopOpcodes[static_cast<std::size_t>(color)];
        op.meta.mnemonic = opcode_by_code(op.opcode).name;
        ++relocated_counters;
      }
    }
    result.push_back(std::move(op));
  }
  if (trace)
    std::cerr << "[register-web-copy-coalesce] removed=" << removed.size()
              << " definitions=" << webs.parents.size()
              << " execution-states=" << sites.size() << '\n';
  std::vector<AppliedOptimization> optimizations{{"register-web-copy-coalesce",
      "Removed " + std::to_string(removed.size()) +
      " copy store(s) by coalescing exact reaching-definition webs; preserved "
      "entry values, hardware-sensitive epochs and matched caller lifetimes."}};
  if (joint_merges > 0)
    optimizations.push_back({"register-web-joint-coloring",
        "Removed " + std::to_string(joint_merges) +
        " additional copy store(s) using " + std::to_string(recolored_assignments) +
        " web assignment change(s) from a joint, precolored interference graph. "
        "Deterministic greedy witnesses preserve setup, hardware and constant-pool anchors; "
        "failed coloring keeps the incumbent without exhaustive search."});
  if (relocated_counters > 0)
    optimizations.push_back({"register-web-counter-role-coalescing",
        "Retargeted " + std::to_string(relocated_counters) +
        " FL counter instruction(s) within R0..R3 while removing copy stores. "
        "Web domains preserve live source values, entry/setup ownership, shared "
        "operands and manual/hardware anchors without spills or Rf."});
  return {.ops = std::move(result), .applied = static_cast<int>(removed.size()),
          .optimizations = std::move(optimizations)};
}

IrPass register_web_copy_coalesce_pass() {
  return {.name = "register-web-copy-coalesce", .run = register_web_copy_coalesce,
          .layout_safe = false};
}

} // namespace mkpro::core::passes
