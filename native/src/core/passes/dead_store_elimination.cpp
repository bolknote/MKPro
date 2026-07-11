#include "mkpro/core/passes/dead_store_elimination.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
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

} // namespace

PassResult dead_store_elimination(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  if (ops.empty())
    return PassResult{.ops = {}, .applied = 0, .optimizations = {}};

  const LivenessInfo liveness =
      compute_liveness(ops, LivenessOptions{.unknown_indirect_flow_to_all = true});
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
    if (liveness.live_out.at(index).contains(*target))
      continue;
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
                  .name = "dead-store-elimination",
                  .detail = "Removed " + std::to_string(removed.size()) +
                            " store(s) to register(s) never read before the next assignment.",
              },
          },
  };
}

IrPass dead_store_elimination_pass() {
  return IrPass{
      .name = "dead-store-elimination",
      .run = dead_store_elimination,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
