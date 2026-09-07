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
using Node = std::size_t;
using Nodes = std::set<Node>;
using Reaching = std::array<Nodes, kRegisters>;

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

  Node add(int color, bool anchored = false) {
    const Node id = parents.size();
    parents.push_back(id);
    colors.push_back(color);
    fixed.push_back(anchored);
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

  const auto anchored = [&](std::size_t site) {
    const auto& op = ops[site];
    return (op.kind != IrKind::Store && op.kind != IrKind::Recall) ||
           has_rewrite_barrier(op) || is_display_focus_sensitive(op);
  };
  for (std::size_t site = 0; site < ops.size(); ++site) {
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
  const auto accepts = [&](Node a, Node b, int color, std::size_t copy_store) {
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
  }
  if (removed.empty())
    return unchanged();

  std::vector<IrOp> result;
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
    result.push_back(std::move(op));
  }
  if (trace)
    std::cerr << "[register-web-copy-coalesce] removed=" << removed.size()
              << " definitions=" << webs.parents.size()
              << " execution-states=" << sites.size() << '\n';
  return {.ops = std::move(result), .applied = static_cast<int>(removed.size()),
          .optimizations = {{"register-web-copy-coalesce",
              "Removed " + std::to_string(removed.size()) +
              " copy store(s) by coalescing exact reaching-definition webs; preserved "
              "entry values, hardware-sensitive epochs and matched caller lifetimes."}}};
}

IrPass register_web_copy_coalesce_pass() {
  return {.name = "register-web-copy-coalesce", .run = register_web_copy_coalesce,
          .layout_safe = false};
}

} // namespace mkpro::core::passes
