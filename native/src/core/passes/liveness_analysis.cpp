#include "mkpro/core/passes/liveness_analysis.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/cfg.hpp"

#include <algorithm>
#include <deque>
#include <exception>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace mkpro::core::passes {

namespace {

void insert_if_named(RegisterValueSet& registers, const std::string& register_name) {
  if (!register_name.empty())
    registers.insert(register_name);
}

void add_indirect_selector_definition(RegisterEffects& effects, const IrOp& op) {
  if (op.register_name.empty())
    return;
  // Symbolic analysis cannot derive the selector class from register_name, so
  // it uses the lowered opcode. Ordinary IR tests and hand-built passes may
  // carry only a base opcode; retain their physical-name contract.
  if (!op.meta.logical_register_analysis) {
    try {
      if (!core::is_stable_indirect_selector(op.register_name))
        effects.must_defs.insert(op.register_name);
    } catch (const std::exception&) {
    }
    return;
  }
  int selector = op.opcode & 0x0f;
  // The xF aliases are the R0 pre-decrement forms on the stock machine.
  if (selector == 0x0f)
    selector = 0;
  if (selector >= 0 && selector <= 6)
    effects.must_defs.insert(op.register_name);
}

RegisterValueSet physical_register_universe() {
  RegisterValueSet registers;
  for (int index = 0; index <= 9; ++index)
    registers.insert(std::to_string(index));
  for (char name = 'a'; name <= 'e'; ++name)
    registers.insert(std::string(1, name));
  return registers;
}

RegisterValueSet register_universe(const std::vector<IrOp>& ops,
                                   const std::vector<RegisterEffects>& effects,
                                   bool include_physical_register_universe) {
  RegisterValueSet registers =
      include_physical_register_universe ? physical_register_universe() : RegisterValueSet{};
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    switch (op.kind) {
    case IrKind::Store:
    case IrKind::Recall:
    case IrKind::IndirectStore:
    case IrKind::IndirectRecall:
    case IrKind::IndirectJump:
    case IrKind::IndirectCall:
    case IrKind::IndirectCondJump:
      insert_if_named(registers, op.register_name);
      break;
    case IrKind::Loop:
      insert_if_named(registers,
                      op.meta.logical_register_analysis && op.meta.logical_register_name.has_value()
                          ? *op.meta.logical_register_name
                          : loop_counter_register(op.counter));
      break;
    case IrKind::Label:
    case IrKind::Jump:
    case IrKind::CondJump:
    case IrKind::Call:
    case IrKind::Return:
    case IrKind::Stop:
    case IrKind::Plain:
    case IrKind::OrphanAddress:
      break;
    }
    registers.insert(effects.at(index).uses.begin(), effects.at(index).uses.end());
    registers.insert(effects.at(index).must_defs.begin(), effects.at(index).must_defs.end());
    registers.insert(effects.at(index).may_defs.begin(), effects.at(index).may_defs.end());
  }
  return registers;
}

bool sets_equal(const RegisterValueSet& left, const RegisterValueSet& right) {
  return left == right;
}

void add_interference(RegisterInterferenceGraph& graph, const std::string& left,
                      const std::string& right) {
  if (left.empty() || right.empty() || left == right)
    return;
  graph.neighbors[left].insert(right);
  graph.neighbors[right].insert(left);
}

void add_clique(RegisterInterferenceGraph& graph, const RegisterValueSet& registers) {
  for (auto left = registers.begin(); left != registers.end(); ++left) {
    graph.neighbors.try_emplace(*left);
    for (auto right = std::next(left); right != registers.end(); ++right)
      add_interference(graph, *left, *right);
  }
}

} // namespace

RegisterEffects register_effects(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Store:
    return RegisterEffects{.must_defs = RegisterValueSet{op.register_name}};
  case IrKind::Recall:
    return RegisterEffects{.uses = RegisterValueSet{op.register_name}};
  case IrKind::IndirectRecall: {
    if (op.meta.discarded_indirect_recall_value) {
      RegisterEffects effects{.uses = RegisterValueSet{op.register_name}};
      add_indirect_selector_definition(effects, op);
      return effects;
    }
    const std::optional<std::set<std::string>> targets = known_indirect_memory_targets(op);
    RegisterEffects effects{
        .uses = RegisterValueSet{op.register_name},
        .uses_all_registers = !targets.has_value(),
    };
    if (targets.has_value())
      effects.uses.insert(targets->begin(), targets->end());
    add_indirect_selector_definition(effects, op);
    return effects;
  }
  case IrKind::IndirectStore: {
    const std::optional<std::set<std::string>> targets = known_indirect_memory_targets(op);
    RegisterEffects effects{.uses = RegisterValueSet{op.register_name}};
    if (!targets.has_value()) {
      effects.may_define_any_register = true;
    } else if (targets->size() == 1U) {
      effects.must_defs = *targets;
    } else {
      effects.may_defs = *targets;
    }
    add_indirect_selector_definition(effects, op);
    return effects;
  }
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump: {
    RegisterEffects effects{.uses = RegisterValueSet{op.register_name}};
    add_indirect_selector_definition(effects, op);
    return effects;
  }
  case IrKind::Loop: {
    const std::string counter =
        op.meta.logical_register_analysis && op.meta.logical_register_name.has_value()
            ? *op.meta.logical_register_name
            : loop_counter_register(op.counter);
    if (counter.empty())
      return RegisterEffects{};
    return RegisterEffects{
        .uses = RegisterValueSet{counter},
        .must_defs = RegisterValueSet{counter},
    };
  }
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Return:
  case IrKind::Stop:
  case IrKind::Plain:
  case IrKind::OrphanAddress:
    return RegisterEffects{};
  }
  return RegisterEffects{};
}

LivenessInfo compute_liveness(const std::vector<IrOp>& ops, LivenessOptions options) {
  const ControlFlowGraph graph = build_control_flow_graph(
      ops, BuildCfgOptions{
               .indirect_call_fallthrough = true,
               .unknown_indirect_flow_to_all = options.unknown_indirect_flow_to_all,
               .unresolved_direct_flow_to_all = options.unresolved_direct_flow_to_all,
           });
  const std::size_t size = ops.size();
  std::vector<std::vector<int>> successors(size);
  std::vector<std::vector<int>> predecessors(size);
  for (std::size_t source = 0; source < graph.edges.size(); ++source) {
    for (const CfgEdge& edge : graph.edges.at(source)) {
      if (edge.target < 0 || edge.target >= static_cast<int>(size))
        continue;
      successors.at(source).push_back(edge.target);
      predecessors.at(static_cast<std::size_t>(edge.target)).push_back(static_cast<int>(source));
    }
  }

  std::vector<RegisterEffects> effects;
  effects.reserve(size);
  for (const IrOp& op : ops)
    effects.push_back(register_effects(op));
  const RegisterValueSet universe =
      register_universe(ops, effects, options.include_physical_register_universe);

  std::set<int> conservative_sources;
  for (const CfgUncertainty& uncertainty : graph.uncertainties)
    conservative_sources.insert(uncertainty.source);
  for (std::size_t index = 0; index < effects.size(); ++index) {
    RegisterEffects& op_effects = effects.at(index);
    if (op_effects.uses_all_registers || conservative_sources.contains(static_cast<int>(index))) {
      op_effects.uses.insert(universe.begin(), universe.end());
    }
    if (op_effects.may_define_any_register)
      op_effects.may_defs.insert(universe.begin(), universe.end());
  }

  std::vector<RegisterValueSet> live_in(size);
  std::vector<RegisterValueSet> live_out(size);

  std::deque<int> worklist;
  std::vector<bool> queued(size, true);
  for (int index = static_cast<int>(size) - 1; index >= 0; --index)
    worklist.push_back(index);

  while (!worklist.empty()) {
    const int index = worklist.front();
    worklist.pop_front();
    const std::size_t offset = static_cast<std::size_t>(index);
    queued.at(offset) = false;

    RegisterValueSet new_out;
    for (const int successor : successors.at(offset)) {
      const RegisterValueSet& successor_in = live_in.at(static_cast<std::size_t>(successor));
      new_out.insert(successor_in.begin(), successor_in.end());
    }

    const RegisterEffects& op_effects = effects.at(offset);
    RegisterValueSet new_in = op_effects.uses;
    for (const std::string& reg : new_out) {
      if (!op_effects.must_defs.contains(reg))
        new_in.insert(reg);
    }

    if (!sets_equal(new_in, live_in.at(offset)) || !sets_equal(new_out, live_out.at(offset))) {
      live_in.at(offset) = std::move(new_in);
      live_out.at(offset) = std::move(new_out);
      for (const int predecessor : predecessors.at(offset)) {
        const std::size_t predecessor_offset = static_cast<std::size_t>(predecessor);
        if (queued.at(predecessor_offset))
          continue;
        queued.at(predecessor_offset) = true;
        worklist.push_back(predecessor);
      }
    }
  }

  return LivenessInfo{
      .live_in = std::move(live_in),
      .live_out = std::move(live_out),
      .control_flow_targets_are_exact = graph.targets_are_exact(),
      .conservative_flow_sources =
          std::vector<int>(conservative_sources.begin(), conservative_sources.end()),
      .includes_physical_register_universe = options.include_physical_register_universe,
  };
}

bool RegisterInterferenceGraph::interferes(const std::string& left,
                                           const std::string& right) const {
  if (left == right)
    return false;
  const auto found = neighbors.find(left);
  return found != neighbors.end() && found->second.contains(right);
}

RegisterInterferenceGraph build_register_interference_graph(const std::vector<IrOp>& ops,
                                                            const LivenessInfo& liveness) {
  if (liveness.live_in.size() != ops.size() || liveness.live_out.size() != ops.size())
    return build_register_interference_graph(ops);

  RegisterInterferenceGraph graph;
  std::vector<RegisterEffects> effects;
  effects.reserve(ops.size());

  bool needs_physical_universe = !liveness.conservative_flow_sources.empty();
  for (const IrOp& op : ops) {
    RegisterEffects op_effects = register_effects(op);
    needs_physical_universe = needs_physical_universe || op_effects.uses_all_registers ||
                              op_effects.may_define_any_register;
    for (const std::string& reg : op_effects.uses)
      graph.neighbors.try_emplace(reg);
    for (const std::string& reg : op_effects.must_defs)
      graph.neighbors.try_emplace(reg);
    for (const std::string& reg : op_effects.may_defs)
      graph.neighbors.try_emplace(reg);
    effects.push_back(std::move(op_effects));
  }
  for (const RegisterValueSet& live : liveness.live_in) {
    for (const std::string& reg : live)
      graph.neighbors.try_emplace(reg);
  }
  for (const RegisterValueSet& live : liveness.live_out) {
    for (const std::string& reg : live)
      graph.neighbors.try_emplace(reg);
  }
  if (needs_physical_universe && liveness.includes_physical_register_universe) {
    for (const std::string& reg : physical_register_universe())
      graph.neighbors.try_emplace(reg);
  }

  RegisterValueSet graph_universe;
  for (const auto& [reg, unused_neighbors] : graph.neighbors) {
    (void)unused_neighbors;
    graph_universe.insert(reg);
  }

  for (std::size_t index = 0; index < ops.size(); ++index) {
    add_clique(graph, liveness.live_in.at(index));
    add_clique(graph, liveness.live_out.at(index));

    RegisterValueSet definitions = effects.at(index).must_defs;
    definitions.insert(effects.at(index).may_defs.begin(), effects.at(index).may_defs.end());
    if (effects.at(index).may_define_any_register)
      definitions.insert(graph_universe.begin(), graph_universe.end());

    RegisterValueSet live_at_definition = liveness.live_in.at(index);
    live_at_definition.insert(liveness.live_out.at(index).begin(),
                              liveness.live_out.at(index).end());
    add_clique(graph, definitions);
    for (const std::string& definition : definitions) {
      graph.neighbors.try_emplace(definition);
      for (const std::string& live : live_at_definition)
        add_interference(graph, definition, live);
    }
  }

  return graph;
}

RegisterInterferenceGraph build_register_interference_graph(const std::vector<IrOp>& ops) {
  return build_register_interference_graph(ops, compute_liveness(ops));
}

} // namespace mkpro::core::passes
