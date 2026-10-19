#pragma once
#include "mkpro/core/passes/register_coalesce.hpp"
namespace mkpro::core::passes {
// Demand an equality between two entry operands, not entire source variables.
std::vector<RegisterWebRecoloringRequest>
idempotent_entry_prologue_register_requests(const std::vector<IrOp>& ops);
// Factor an existing two-input prologue into a common leaf. Another caller
// may acquire its pure calls only after a complete finite input/identity proof.
// Both memory-backed and one-X leaf entries are considered independently.
// These are conditional layout alternatives; the caller must bind selectors.
std::vector<PassResult> deferred_idempotent_entry_prologue_candidates(
    const std::vector<IrOp>& ops, const std::vector<PreloadReport>& preloads,
    const PostLayoutControlFlowOptions& control, const std::map<std::string, int>& encoded_origins);
} // namespace mkpro::core::passes

