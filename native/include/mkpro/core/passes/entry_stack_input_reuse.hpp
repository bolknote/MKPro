#pragma once

#include "mkpro/core/helper_invariant_recall_hoist.hpp"
#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

PassResult call_entry_materialization_order(const std::vector<IrOp>& ops,
                                            const PassContext& context);
IrPass call_entry_materialization_order_pass();

// Re-run the same materialization-order/entry-X composition after layout,
// when helper sharing and authoritative indirect-flow proofs are available.
// The supplied hoist options are the complete address proof for `items`; the
// result is unchanged unless the atomic reorder + tail-hoist + entry-recall
// removal is fully proved and strictly reduces the machine artifact.
HelperInvariantRecallHoistResult post_layout_call_entry_materialization_order(
    const std::vector<MachineItem>& items,
    const HelperInvariantRecallHoistOptions& hoist_options,
    const PassContext& context);

PassResult early_helper_invariant_recall_hoist(const std::vector<IrOp>& ops,
                                                const PassContext& context);
IrPass early_helper_invariant_recall_hoist_pass();
PassResult entry_stack_input_reuse(const std::vector<IrOp>& ops, const PassContext& context);
IrPass entry_stack_input_reuse_pass();

} // namespace mkpro::core::passes
