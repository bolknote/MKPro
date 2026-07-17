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
#include <string>
#include <utility>

namespace mkpro::core::passes {

namespace {

constexpr int kMaxFixpointIterations = 8;

struct RunOnIrResult {
  std::vector<IrOp> ops;
  int applied = 0;
  std::vector<AppliedOptimization> optimizations;
  std::map<std::string, int> pass_counts;
  std::vector<PreloadReport> preloads;
};

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
      aggregate.pass_counts[std::string(pass.name)] += result.applied;
      if (result.applied <= 0)
        continue;

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
