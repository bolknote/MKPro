#include "mkpro/core/passes/dead_store_elimination.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/cfg.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"

#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

bool is_number_entry(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode <= 0x0c;
}

bool leaves_entry_open(const IrOp& op) {
  if (is_number_entry(op))
    return true;
  return op.kind == IrKind::Stop && op.semantic == "input";
}

const IrOp* previous_effective_op(const std::vector<IrOp>& ops, int index) {
  for (int cursor = index - 1; cursor >= 0; --cursor) {
    const IrOp& op = ops.at(static_cast<std::size_t>(cursor));
    if (op.kind != IrKind::Label)
      return &op;
  }
  return nullptr;
}

const IrOp* next_effective_op(const std::vector<IrOp>& ops, int index) {
  for (int cursor = index + 1; cursor < static_cast<int>(ops.size()); ++cursor) {
    const IrOp& op = ops.at(static_cast<std::size_t>(cursor));
    if (op.kind != IrKind::Label)
      return &op;
  }
  return nullptr;
}

bool finalizes_number_entry(const std::vector<IrOp>& ops, int index) {
  const IrOp* previous = previous_effective_op(ops, index);
  const IrOp* next = next_effective_op(ops, index);
  return previous != nullptr && next != nullptr && leaves_entry_open(*previous) &&
         is_number_entry(*next);
}

bool is_vp_op(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode == 0x0c;
}

bool provides_vp_restore_context(const std::vector<IrOp>& ops, int index) {
  const IrOp* next = next_effective_op(ops, index);
  return next != nullptr && is_vp_op(*next);
}

bool is_continuous_resume_anchor(const IrOp& op) {
  return op.meta.manual_interaction.has_value() &&
         op.meta.manual_interaction->kind == ManualInteractionAnchorKind::ContinuousResume;
}

std::optional<std::string> dead_store_target(const IrOp& op) {
  if (op.kind == IrKind::Store)
    return op.register_name;
  if (op.kind != IrKind::IndirectStore)
    return std::nullopt;
  if (!mkpro::core::is_stable_indirect_selector(op.register_name))
    return std::nullopt;
  return known_indirect_memory_target(op);
}

bool return_acts_as_address_one_jump(const IrOp& op) {
  return op.kind == IrKind::Return && op.meta.comment == "optimized БП 01";
}

std::optional<std::size_t> call_following_store(const std::vector<IrOp>& ops,
                                                std::size_t store_index) {
  std::size_t call_index = store_index + 1U;
  while (call_index < ops.size() && ops[call_index].kind == IrKind::Label)
    ++call_index;
  if (call_index >= ops.size() ||
      (ops[call_index].kind != IrKind::Call &&
       ops[call_index].kind != IrKind::IndirectCall)) {
    return std::nullopt;
  }
  return call_index;
}

bool call_continuation_overwrites_target(const std::vector<IrOp>& ops,
                                         std::size_t call_index,
                                         const std::string& register_name) {
  std::size_t continuation = call_index + 1U;
  while (continuation < ops.size() && ops[continuation].kind == IrKind::Label)
    ++continuation;
  if (continuation >= ops.size())
    return false;
  const RegisterEffects effects = register_effects(ops[continuation]);
  return !effects.uses_all_registers &&
         !effects.uses.contains(register_name) &&
         effects.must_defs.contains(register_name);
}

struct ExactCallState {
  int op_index = 0;
  std::vector<int> return_stack;

  bool operator<(const ExactCallState& other) const {
    return std::tie(op_index, return_stack) <
           std::tie(other.op_index, other.return_stack);
  }
};

// Ordinary liveness deliberately joins every В/О with every call continuation.
// Recheck a finalized artifact with the actual bounded MK-61 return stack.
// Every unresolved edge, empty-stack return, or state-cap overflow fails closed.
bool exact_call_stack_proves_dead_store(
    const std::vector<IrOp>& ops, std::size_t store_index,
    const std::string& register_name, const ControlFlowGraph& graph,
    const std::set<int>& uncertain_sources) {
  if (store_index + 1U >= ops.size())
    return true;
  constexpr std::size_t kMaximumReturnDepth = 5U;
  constexpr std::size_t kMaximumStates = 20000U;
  std::deque<ExactCallState> pending;
  std::set<ExactCallState> visited;
  const auto enqueue = [&](ExactCallState state) {
    const auto [_, inserted] = visited.insert(state);
    if (!inserted)
      return true;
    if (visited.size() > kMaximumStates)
      return false;
    pending.push_back(std::move(state));
    return true;
  };
  if (!enqueue(ExactCallState{
          .op_index = static_cast<int>(store_index + 1U)})) {
    return false;
  }
  while (!pending.empty()) {
    ExactCallState state = std::move(pending.front());
    pending.pop_front();
    if (state.op_index < 0 ||
        state.op_index >= static_cast<int>(ops.size()))
      continue;

    const IrOp& op = ops.at(static_cast<std::size_t>(state.op_index));
    const RegisterEffects effects = register_effects(op);
    if (effects.uses_all_registers || effects.uses.contains(register_name))
      return false;
    if (effects.must_defs.contains(register_name))
      continue;
    if (uncertain_sources.contains(state.op_index))
      return false;

    if (op.kind == IrKind::Return && !return_acts_as_address_one_jump(op)) {
      if (state.return_stack.empty())
        return false;
      state.op_index = state.return_stack.back();
      state.return_stack.pop_back();
      if (!enqueue(std::move(state)))
        return false;
      continue;
    }

    const std::vector<CfgEdge>& edges =
        graph.edges.at(static_cast<std::size_t>(state.op_index));
    if (op.kind == IrKind::Call || op.kind == IrKind::IndirectCall) {
      const int continuation = state.op_index + 1;
      if (continuation >= static_cast<int>(ops.size()) ||
          state.return_stack.size() >= kMaximumReturnDepth || edges.empty()) {
        return false;
      }
      for (const CfgEdge& edge : edges) {
        ExactCallState called = state;
        called.op_index = edge.target;
        called.return_stack.push_back(continuation);
        if (!enqueue(std::move(called)))
          return false;
      }
      continue;
    }
    for (const CfgEdge& edge : edges) {
      ExactCallState successor = state;
      successor.op_index = edge.target;
      if (!enqueue(std::move(successor)))
        return false;
    }
  }
  return true;
}

} // namespace

PassResult eliminate_dead_stores(const std::vector<IrOp>& ops,
                                 const PassContext& context,
                                 bool finalization_context) {
  (void)context;
  if (ops.empty())
    return PassResult{.ops = {}, .applied = 0, .optimizations = {}};

  const LivenessInfo liveness =
      compute_liveness(ops, LivenessOptions{.unknown_indirect_flow_to_all = true});
  std::optional<ControlFlowGraph> exact_graph;
  std::set<int> exact_uncertain_sources;
  std::set<int> removed;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    const std::optional<std::string> target = dead_store_target(op);
    if (!target.has_value())
      continue;
    // A PP phase is itself an externally executed single command and therefore
    // cannot disappear.  A continuous-resume store may be dead, however: in
    // that case its entry anchor is transferred to the next surviving command
    // below, preserving both the protocol and the byte-saving DSE rewrite.
    if (op.meta.raw ||
        (op.meta.manual_interaction.has_value() && !is_continuous_resume_anchor(op)))
      continue;
    if (liveness.live_out.at(index).contains(*target)) {
      if (!finalization_context)
        continue;
      const std::optional<std::size_t> call_index =
          call_following_store(ops, index);
      if (!call_index.has_value() ||
          !call_continuation_overwrites_target(ops, *call_index, *target)) {
        continue;
      }
      if (!exact_graph.has_value()) {
        exact_graph = build_control_flow_graph(
            ops, BuildCfgOptions{
                     .indirect_call_fallthrough = false,
                     .unknown_indirect_flow_to_all = false,
                     .unresolved_direct_flow_to_all = false,
                 });
        for (const CfgUncertainty& uncertainty : exact_graph->uncertainties)
          exact_uncertain_sources.insert(uncertainty.source);
      }
      const bool proved = exact_call_stack_proves_dead_store(
          ops, index, *target, *exact_graph, exact_uncertain_sources);
      if (!proved) {
        continue;
      }
    }
    if (finalizes_number_entry(ops, static_cast<int>(index)))
      continue;
    if (provides_vp_restore_context(ops, static_cast<int>(index)))
      continue;
    removed.insert(static_cast<int>(index));
  }

  if (removed.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::map<std::size_t, ManualInteractionAnchor> transferred_anchors;
  std::set<int> retained_anchors;
  for (const int removed_index : removed) {
    const IrOp& removed_op = ops.at(static_cast<std::size_t>(removed_index));
    if (!is_continuous_resume_anchor(removed_op))
      continue;
    std::optional<std::size_t> target;
    for (std::size_t candidate = static_cast<std::size_t>(removed_index) + 1U;
         candidate < ops.size(); ++candidate) {
      if (removed.contains(static_cast<int>(candidate)) ||
          ops.at(candidate).kind == IrKind::Label) {
        continue;
      }
      if (ops.at(candidate).kind != IrKind::OrphanAddress)
        target = candidate;
      break;
    }
    if (!target.has_value() || ops.at(*target).meta.manual_interaction.has_value() ||
        transferred_anchors.contains(*target)) {
      retained_anchors.insert(removed_index);
      continue;
    }
    transferred_anchors.emplace(*target, *removed_op.meta.manual_interaction);
  }
  for (const int retained : retained_anchors)
    removed.erase(retained);
  if (removed.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size() - removed.size());
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (removed.contains(static_cast<int>(index)))
      continue;
    IrOp op = ops.at(index);
    if (const auto transferred = transferred_anchors.find(index);
        transferred != transferred_anchors.end()) {
      op.meta.manual_interaction = transferred->second;
    }
    result.push_back(std::move(op));
  }

  return PassResult{
      .ops = std::move(result),
      .applied = static_cast<int>(removed.size()),
      .optimizations =
          {
          AppliedOptimization{
                  .name = finalization_context
                              ? "finalization-dead-store-elimination"
                              : "dead-store-elimination",
                  .detail = "Removed " + std::to_string(removed.size()) +
                            " store(s) to register(s) never read before the next assignment.",
              },
          },
  };
}

PassResult dead_store_elimination(const std::vector<IrOp>& ops,
                                  const PassContext& context) {
  return eliminate_dead_stores(ops, context, false);
}

PassResult finalization_dead_store_elimination(
    const std::vector<IrOp>& ops, const PassContext& context) {
  return eliminate_dead_stores(ops, context, true);
}

IrPass dead_store_elimination_pass() {
  return IrPass{
      .name = "dead-store-elimination",
      .run = dead_store_elimination,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
