#include "mkpro/core/passes/index.hpp"

#include "mkpro/core/passes/arithmetic_if.hpp"
#include "mkpro/core/passes/branch_target_x_reuse.hpp"
#include "mkpro/core/passes/conditional_branch_trampoline.hpp"
#include "mkpro/core/passes/constant_folding.hpp"
#include "mkpro/core/passes/cse_display_block.hpp"
#include "mkpro/core/passes/dead_code_after_halt.hpp"
#include "mkpro/core/passes/dead_proc_elimination.hpp"
#include "mkpro/core/passes/dead_store_before_commutative.hpp"
#include "mkpro/core/passes/dead_store_elimination.hpp"
#include "mkpro/core/passes/duplicate_failure_tail.hpp"
#include "mkpro/core/passes/flow_x_reuse.hpp"
#include "mkpro/core/passes/indirect_addressing.hpp"
#include "mkpro/core/passes/indirect_selector_integer_part.hpp"
#include "mkpro/core/passes/jump_thread.hpp"
#include "mkpro/core/passes/jump_to_next.hpp"
#include "mkpro/core/passes/last_x_reuse.hpp"
#include "mkpro/core/passes/pre_shift_stack_lift.hpp"
#include "mkpro/core/passes/preloaded_indirect_flow.hpp"
#include "mkpro/core/passes/r0_fractional_sentinel.hpp"
#include "mkpro/core/passes/redundant_prologue.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/passes/return_suffix_gadget.hpp"
#include "mkpro/core/passes/return_trampoline.hpp"
#include "mkpro/core/passes/return_zero_jump.hpp"
#include "mkpro/core/passes/shared_call_tail.hpp"
#include "mkpro/core/passes/shared_straight_line_helper.hpp"
#include "mkpro/core/passes/shared_terminal_tail.hpp"
#include "mkpro/core/passes/store_recall_peephole.hpp"
#include "mkpro/core/passes/tail_branch_inversion.hpp"
#include "mkpro/core/passes/tail_call.hpp"
#include "mkpro/core/passes/vp_splice.hpp"
#include "mkpro/core/passes/vp_x2_peephole.hpp"
#include "mkpro/core/passes/x2_dead_restore_before_overwrite.hpp"
#include "mkpro/core/passes/x2_hidden_temp_restore.hpp"
#include "mkpro/core/passes/x2_literal_restore.hpp"
#include "mkpro/core/passes/x2_noop_restore.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

namespace mkpro::core::passes {

namespace {

constexpr int kMaxFixpointIterations = 8;
constexpr std::string_view kFinalizationCellOriginRole =
    "finalization-cell-origin:";

struct RunOnIrResult {
  std::vector<IrOp> ops;
  int applied = 0;
  std::vector<AppliedOptimization> optimizations;
  std::map<std::string, int> pass_counts;
  std::vector<PreloadReport> preloads;
};

std::vector<MachineItem> attach_finalization_flow_identity_labels(
    const std::vector<MachineItem>& items) {
  std::set<std::string> names;
  std::set<int> referenced_addresses;
  std::map<int, std::string> existing_label_by_address;
  int cell_count = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label) {
      names.insert(item.name);
      existing_label_by_address.try_emplace(cell_count, item.name);
    } else {
      if (item.kind == MachineItemKind::Address) {
        if (const int* target = std::get_if<int>(&item.target))
          referenced_addresses.insert(*target);
      }
      if (item.indirect_flow_targets.has_value()) {
        for (const IrTarget& target : *item.indirect_flow_targets) {
          if (const int* address = std::get_if<int>(&target))
            referenced_addresses.insert(*address);
        }
      }
      ++cell_count;
    }
  }

  std::vector<std::string> label_by_address;
  std::vector<bool> synthetic_by_address;
  label_by_address.reserve(static_cast<std::size_t>(cell_count));
  synthetic_by_address.reserve(static_cast<std::size_t>(cell_count));
  for (int address = 0; address < cell_count; ++address) {
    if (!referenced_addresses.contains(address)) {
      label_by_address.emplace_back();
      synthetic_by_address.push_back(false);
      continue;
    }
    const auto existing = existing_label_by_address.find(address);
    if (existing != existing_label_by_address.end()) {
      label_by_address.push_back(existing->second);
      synthetic_by_address.push_back(false);
      continue;
    }
    std::string label = "__finalization_cell_" + std::to_string(address);
    while (names.contains(label))
      label += "_";
    names.insert(label);
    label_by_address.push_back(std::move(label));
    synthetic_by_address.push_back(true);
  }

  const auto rebound_target = [&](const IrTarget& target) -> IrTarget {
    const int* address = std::get_if<int>(&target);
    if (address == nullptr || *address < 0 || *address >= cell_count ||
        label_by_address.at(static_cast<std::size_t>(*address)).empty()) {
      return target;
    }
    return label_by_address.at(static_cast<std::size_t>(*address));
  };

  std::vector<MachineItem> result;
  result.reserve(items.size() + label_by_address.size());
  int address = 0;
  for (const MachineItem& source : items) {
    if (source.kind == MachineItemKind::Label) {
      result.push_back(source);
      continue;
    }
    if (synthetic_by_address.at(static_cast<std::size_t>(address))) {
      MachineItem identity = MachineItem::label(
          label_by_address.at(static_cast<std::size_t>(address)));
      identity.hidden = true;
      result.push_back(std::move(identity));
    }

    MachineItem item = source;
    item.roles.push_back(std::string(kFinalizationCellOriginRole) +
                         std::to_string(address));
    if (item.kind == MachineItemKind::Address) {
      item.target = rebound_target(item.target);
      item.formal_opcode.reset();
    }
    if (item.indirect_flow_targets.has_value()) {
      for (IrTarget& target : *item.indirect_flow_targets)
        target = rebound_target(target);
    }
    result.push_back(std::move(item));
    ++address;
  }
  return result;
}

bool direct_flow_labels_resolve(const std::vector<IrOp>& ops) {
  std::set<std::string> labels;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label)
      labels.insert(op.name);
  }

  for (const IrOp& op : ops) {
    switch (op.kind) {
      case IrKind::Jump:
      case IrKind::CondJump:
      case IrKind::Loop:
      case IrKind::Call:
        break;
      case IrKind::Label:
      case IrKind::Store:
      case IrKind::Recall:
      case IrKind::IndirectStore:
      case IrKind::IndirectRecall:
      case IrKind::Plain:
      case IrKind::IndirectJump:
      case IrKind::IndirectCall:
      case IrKind::IndirectCondJump:
      case IrKind::Return:
      case IrKind::Stop:
      case IrKind::OrphanAddress:
        continue;
    }
    const std::string* target = std::get_if<std::string>(&op.target);
    if (target != nullptr && !labels.contains(*target))
      return false;
  }
  return true;
}

RunOnIrResult run_passes_on_ir(std::vector<IrOp> initial, const CompileOptions& options,
                               bool layout_only) {
  std::vector<IrOp> current = std::move(initial);
  RunOnIrResult aggregate;
  bool changed_in_iteration = true;
  int iteration = 0;

  while (changed_in_iteration && iteration < kMaxFixpointIterations) {
    changed_in_iteration = false;
    ++iteration;

    for (const IrPass& pass : pass_pipeline()) {
      if (layout_only && !pass.layout_safe)
        continue;

      const PassContext context{.options = options};
      PassResult result = pass.run(current, context);
      if (result.applied <= 0)
        continue;
      if (!direct_flow_labels_resolve(result.ops))
        continue;

      aggregate.pass_counts[std::string(pass.name)] += result.applied;
      changed_in_iteration = true;
      aggregate.applied += result.applied;
      for (const AppliedOptimization& optimization : result.optimizations) {
        const auto existing = std::find_if(
            aggregate.optimizations.begin(), aggregate.optimizations.end(),
            [&](const AppliedOptimization& entry) { return entry.name == optimization.name; });
        if (existing != aggregate.optimizations.end()) {
          existing->detail += " (+" + optimization.detail + ")";
        } else {
          aggregate.optimizations.push_back(optimization);
        }
      }
      aggregate.preloads.insert(aggregate.preloads.end(), result.preloads.begin(),
                                result.preloads.end());
      current = std::move(result.ops);
    }

  }

  aggregate.ops = std::move(current);
  return aggregate;
}

} // namespace

const std::vector<IrPass>& pass_pipeline() {
  static const std::vector<IrPass> pipeline = {
      redundant_prologue_elimination_pass(),
      tail_call_lowering_pass(),
      tail_branch_inversion_pass(),
      conditional_branch_trampoline_pass(),
      shared_call_tail_pass(),
      return_suffix_gadget_pass(),
      shared_terminal_tail_pass(),
      shared_straight_line_helper_pass(),
      callee_hole_straight_line_helper_pass(),
      return_zero_jump_pass(),
      return_trampoline_pass(),
      store_recall_peephole_pass(),
      pre_shift_stack_lift_pass(),
      jump_to_next_threading_pass(),
      jump_thread_pass(),
      flow_x_reuse_pass(),
      branch_target_x_reuse_pass(),
      stable_indirect_flow_pass(),
      preloaded_indirect_flow_pass(),
      runtime_indirect_call_flow_pass(),
      indirect_memory_table_pass(),
      x2_noop_restore_pass(),
      x2_dead_restore_before_overwrite_pass(),
      x2_hidden_temp_restore_pass(),
      x2_literal_restore_pass(),
      dead_store_before_commutative_pass(),
      dead_store_elimination_pass(),
      last_x_reuse_pass(),
      r0_fractional_sentinel_pass(),
      indirect_selector_integer_part_pass(),
      vp_splice_pass(),
      vp_x2_peephole_pass(),
      constant_folding_pass(),
      duplicate_failure_tail_pass(),
      cse_display_block_pass(),
      dead_code_after_halt_pass(),
      register_coalesce_pass(),
      arithmetic_if_pass(),
      dead_proc_elimination_pass(),
  };
  return pipeline;
}

RunPassesResult run_ir_passes(const std::vector<MachineItem>& items,
                              const CompileOptions& options) {
  RunOnIrResult result = run_passes_on_ir(
      raise_machine_to_ir(items, effective_optimizer_feature_profile(options)), options, false);
  return RunPassesResult{
      .items = lower_ir_to_machine(result.ops),
      .applied = result.applied,
      .optimizations = std::move(result.optimizations),
      .pass_counts = std::move(result.pass_counts),
      .preloads = std::move(result.preloads),
  };
}

RunPassesResult run_finalization_dead_store_elimination(
    const std::vector<MachineItem>& items, const CompileOptions& options) {
  const std::vector<MachineItem> identity_items =
      attach_finalization_flow_identity_labels(items);
  std::vector<IrOp> current =
      raise_machine_to_ir(identity_items,
                          effective_optimizer_feature_profile(options));
  RunPassesResult aggregate;
  for (int iteration = 0; iteration < kMaxFixpointIterations; ++iteration) {
    const PassResult result = finalization_dead_store_elimination(
        current, PassContext{.options = options});
    if (result.applied <= 0)
      break;
    aggregate.applied += result.applied;
    aggregate.optimizations.insert(aggregate.optimizations.end(),
                                   result.optimizations.begin(),
                                   result.optimizations.end());
    current = result.ops;
  }
  aggregate.items = lower_ir_to_machine(current);
  std::set<int> retained_addresses;
  for (MachineItem& item : aggregate.items) {
    item.roles.erase(
        std::remove_if(item.roles.begin(), item.roles.end(),
                       [&](const CellRole& role) {
                         if (!role.starts_with(kFinalizationCellOriginRole))
                           return false;
                         try {
                           retained_addresses.insert(std::stoi(
                               role.substr(kFinalizationCellOriginRole.size())));
                         } catch (const std::exception&) {
                         }
                         return true;
                       }),
        item.roles.end());
  }
  const int original_cells = static_cast<int>(std::count_if(
      items.begin(), items.end(), [](const MachineItem& item) {
        return item.kind != MachineItemKind::Label;
      }));
  for (int address = 0; address < original_cells; ++address) {
    if (!retained_addresses.contains(address))
      aggregate.removed_cell_addresses.push_back(address);
  }

  aggregate.pass_counts["finalization-dead-store-elimination"] =
      aggregate.applied;
  return aggregate;
}

RunLayoutPassesResult run_ir_passes_on_layout(const std::vector<LayoutIrCell>& cells,
                                              const CompileOptions& options) {
  RunOnIrResult result = run_passes_on_ir(raise_layout_to_ir(cells), options, true);
  LowerLayoutResult lowered = lower_ir_to_layout(result.ops);
  return RunLayoutPassesResult{
      .cells = std::move(lowered.cells),
      .applied = result.applied,
      .optimizations = std::move(result.optimizations),
      .pass_counts = std::move(result.pass_counts),
      .preloads = std::move(result.preloads),
  };
}

} // namespace mkpro::core::passes
