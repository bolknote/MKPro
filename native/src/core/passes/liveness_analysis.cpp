#include "mkpro/core/passes/liveness_analysis.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/cfg.hpp"

#include <algorithm>
#include <deque>
#include <exception>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
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

// A bounded pushdown expansion keeps a return attached to its actual caller.
// Repeated loop iterations reuse the same state; the bound is on proof states,
// not iterations of the compiled program. Unsupported graphs use the original
// context-insensitive fixed point without changing any optimization premise.
std::optional<LivenessInfo> matched_call_liveness(const std::vector<IrOp>& ops,
                                                 LivenessOptions options) {
  if (ops.empty() ||
      std::none_of(ops.begin(), ops.end(), [](const IrOp& op) {
        return op.kind == IrKind::Call || op.kind == IrKind::IndirectCall;
      }) ||
      std::any_of(ops.begin(), ops.end(), [](const IrOp& op) {
        return op.meta.raw || op.kind == IrKind::OrphanAddress ||
               std::find(op.meta.roles.begin(), op.meta.roles.end(),
                         kResumableErrorPaddingRole) != op.meta.roles.end();
      })) {
    return std::nullopt;
  }

  const ControlFlowGraph graph =
      build_control_flow_graph(ops, BuildCfgOptions{.terminal_stop_fallthrough = false});
  if (!graph.targets_are_exact())
    return std::nullopt;

  const CfgTargetIndexes indexes = build_target_indexes(ops);
  const auto address_one = indexes.address_index.find(1);
  std::vector<std::size_t> call_returns;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (ops.at(index).kind == IrKind::Call || ops.at(index).kind == IrKind::IndirectCall) {
      if (index + 1U == ops.size())
        return std::nullopt;
      call_returns.push_back(index + 1U);
    }
  }

  struct ExecutionState {
    std::size_t instruction = 0;
    std::vector<std::size_t> returns;
    // Unreachable fragments are still analyzed. Their unmodeled caller prefix
    // returns conservatively to every call continuation, but calls made inside
    // the fragment retain their own matched suffix.
    bool unknown_prefix = false;
  };
  using Key = std::tuple<std::size_t, std::vector<std::size_t>, bool>;
  constexpr std::size_t kMaximumStates = 8192U;
  constexpr std::size_t kMaximumEdges = 65536U;
  constexpr std::size_t kMaximumReturnDepth = 5U;
  std::map<Key, std::size_t> identities;
  std::vector<ExecutionState> states;
  std::vector<std::vector<std::size_t>> successors;
  std::vector<bool> covered(ops.size(), false);
  bool failed = false;
  std::size_t edge_count = 0;
  const auto intern = [&](ExecutionState state) -> std::optional<std::size_t> {
    if (state.instruction >= ops.size() || state.returns.size() > kMaximumReturnDepth) {
      failed = true;
      return std::nullopt;
    }
    const Key key{state.instruction, state.returns, state.unknown_prefix};
    if (const auto existing = identities.find(key); existing != identities.end())
      return existing->second;
    if (states.size() >= kMaximumStates) {
      failed = true;
      return std::nullopt;
    }
    const std::size_t id = states.size();
    identities.emplace(key, id);
    covered.at(state.instruction) = true;
    states.push_back(std::move(state));
    successors.emplace_back();
    return id;
  };
  const auto connect = [&](std::size_t from, ExecutionState target) {
    const std::optional<std::size_t> to = intern(std::move(target));
    if (!to.has_value())
      return;
    auto& row = successors.at(from);
    if (std::find(row.begin(), row.end(), *to) == row.end()) {
      row.push_back(*to);
      if (++edge_count > kMaximumEdges)
        failed = true;
    }
  };

  (void)intern(ExecutionState{});
  std::size_t processed = 0;
  // Do not let physically present but unreachable code silently disappear
  // from the allocation proof. Seed such fragments only after all ordinary
  // entry contexts have been explored, avoiding fictitious roots in callees.
  for (std::size_t seed = 0; seed < ops.size() && !failed; ++seed) {
    if (!covered.at(seed))
      (void)intern(ExecutionState{.instruction = seed, .unknown_prefix = true});
    while (processed < states.size() && !failed) {
      const std::size_t from = processed++;
      const ExecutionState state = states.at(from);
      const IrOp& op = ops.at(state.instruction);
      if (op.kind == IrKind::Return) {
        ExecutionState target = state;
        if (!target.returns.empty()) {
          target.instruction = target.returns.back();
          target.returns.pop_back();
          connect(from, std::move(target));
        } else if (state.unknown_prefix) {
          for (const std::size_t continuation : call_returns) {
            target.instruction = continuation;
            connect(from, target);
          }
          if (address_one != indexes.address_index.end()) {
            target.instruction = static_cast<std::size_t>(address_one->second);
            connect(from, target);
          }
        } else if (address_one != indexes.address_index.end()) {
          // An empty hardware return goes to physical 01, not main/00.
          target.instruction = static_cast<std::size_t>(address_one->second);
          connect(from, std::move(target));
        } else {
          failed = true;
        }
        continue;
      }
      const bool call = op.kind == IrKind::Call || op.kind == IrKind::IndirectCall;
      if (call && (state.returns.size() == kMaximumReturnDepth ||
                   graph.edges.at(state.instruction).empty())) {
        failed = true;
        continue;
      }
      for (const CfgEdge& edge : graph.edges.at(state.instruction)) {
        if (edge.target < 0 || static_cast<std::size_t>(edge.target) >= ops.size()) {
          failed = true;
          break;
        }
        ExecutionState target = state;
        target.instruction = static_cast<std::size_t>(edge.target);
        if (call)
          target.returns.push_back(state.instruction + 1U);
        connect(from, std::move(target));
      }
    }
  }
  if (failed)
    return std::nullopt;

  std::vector<RegisterEffects> effects;
  effects.reserve(ops.size());
  for (const IrOp& op : ops)
    effects.push_back(register_effects(op));
  const RegisterValueSet universe =
      register_universe(ops, effects, options.include_physical_register_universe);
  for (RegisterEffects& effect : effects) {
    if (effect.uses_all_registers)
      effect.uses.insert(universe.begin(), universe.end());
    if (effect.may_define_any_register)
      effect.may_defs.insert(universe.begin(), universe.end());
  }

  std::vector<std::vector<std::size_t>> predecessors(states.size());
  for (std::size_t from = 0; from < states.size(); ++from)
    for (const std::size_t to : successors.at(from))
      predecessors.at(to).push_back(from);
  std::vector<RegisterValueSet> live_in(states.size());
  std::vector<RegisterValueSet> live_out(states.size());
  std::deque<std::size_t> worklist;
  std::vector<bool> queued(states.size(), true);
  for (std::size_t id = states.size(); id-- > 0U;)
    worklist.push_back(id);
  while (!worklist.empty()) {
    const std::size_t id = worklist.front();
    worklist.pop_front();
    queued.at(id) = false;
    RegisterValueSet next_out;
    for (const std::size_t to : successors.at(id))
      next_out.insert(live_in.at(to).begin(), live_in.at(to).end());
    const RegisterEffects& effect = effects.at(states.at(id).instruction);
    RegisterValueSet next_in = effect.uses;
    for (const std::string& reg : next_out)
      if (!effect.must_defs.contains(reg))
        next_in.insert(reg);
    if (next_in == live_in.at(id) && next_out == live_out.at(id))
      continue;
    live_in.at(id) = std::move(next_in);
    live_out.at(id) = std::move(next_out);
    for (const std::size_t predecessor : predecessors.at(id)) {
      if (!queued.at(predecessor)) {
        queued.at(predecessor) = true;
        worklist.push_back(predecessor);
      }
    }
  }

  LivenessInfo result;
  result.live_in.resize(ops.size());
  result.live_out.resize(ops.size());
  result.includes_physical_register_universe = options.include_physical_register_universe;
  result.matched_call_contexts = true;
  result.call_context_lifetimes.reserve(states.size());
  for (std::size_t id = 0; id < states.size(); ++id) {
    const std::size_t instruction = states.at(id).instruction;
    result.live_in.at(instruction).insert(live_in.at(id).begin(), live_in.at(id).end());
    result.live_out.at(instruction).insert(live_out.at(id).begin(), live_out.at(id).end());
    result.call_context_lifetimes.push_back(CallContextLifetime{
        .instruction = instruction,
        .live_in = std::move(live_in.at(id)),
        .live_out = std::move(live_out.at(id)),
    });
  }
  return result;
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
  if (std::optional<LivenessInfo> matched = matched_call_liveness(ops, options))
    return std::move(*matched);

  const ControlFlowGraph graph = build_control_flow_graph(
      ops, BuildCfgOptions{
               .indirect_call_fallthrough = true,
               .unknown_indirect_flow_to_all = options.unknown_indirect_flow_to_all,
               .unresolved_direct_flow_to_all = options.unresolved_direct_flow_to_all,
               .terminal_stop_fallthrough = false,
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

  const auto contribute = [&](std::size_t index, const RegisterValueSet& live_in,
                              const RegisterValueSet& live_out) {
    add_clique(graph, live_in);
    add_clique(graph, live_out);
    RegisterValueSet definitions = effects.at(index).must_defs;
    definitions.insert(effects.at(index).may_defs.begin(), effects.at(index).may_defs.end());
    if (effects.at(index).may_define_any_register)
      definitions.insert(graph_universe.begin(), graph_universe.end());

    RegisterValueSet live_at_definition = live_in;
    live_at_definition.insert(live_out.begin(), live_out.end());
    add_clique(graph, definitions);
    for (const std::string& definition : definitions) {
      graph.neighbors.try_emplace(definition);
      for (const std::string& live : live_at_definition)
        add_interference(graph, definition, live);
    }
  };

  std::set<std::size_t> covered_context_instructions;
  for (const CallContextLifetime& row : liveness.call_context_lifetimes)
    covered_context_instructions.insert(row.instruction);
  const bool complete_contexts =
      liveness.matched_call_contexts && covered_context_instructions.size() == ops.size() &&
      (ops.empty() || *covered_context_instructions.rbegin() < ops.size());
  if (complete_contexts) {
    for (const CallContextLifetime& row : liveness.call_context_lifetimes)
      contribute(row.instruction, row.live_in, row.live_out);
  } else {
    for (std::size_t index = 0; index < ops.size(); ++index)
      contribute(index, liveness.live_in.at(index), liveness.live_out.at(index));
  }

  return graph;
}

RegisterInterferenceGraph build_register_interference_graph(const std::vector<IrOp>& ops) {
  return build_register_interference_graph(ops, compute_liveness(ops));
}

} // namespace mkpro::core::passes
