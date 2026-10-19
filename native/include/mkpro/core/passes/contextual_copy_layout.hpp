#pragma once

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"

namespace mkpro::core::passes {

struct ContextualCopyStoreProof {
  bool proved = false;
  std::size_t work = 0;
  bool exhausted = false;
  std::size_t dead_contexts = 0;
  std::size_t fixed_point_contexts = 0;
  std::size_t unreachable_contexts = 0;
};

// Data-only certificate. In each exact caller context the new register value
// is either unobserved, identical to its old word on a proved finite integral
// domain, or unreachable under the complete guarded reaching-definition set.
// Removing the recall additionally needs the independent stack/entry proof;
// deleting either cell additionally needs rooted command/return relocation.
ContextualCopyStoreProof prove_contextual_copy_store_elision(
    const std::vector<IrOp>& ops, const AuthoritativePostLayoutControlFlow& flow,
    std::size_t store, const std::vector<PreloadReport>& runtime_preloads,
    std::size_t maximum_work = 400000, bool closed_artifact_precision = false);

std::vector<RegisterWebRecoloringRequest> contextual_copy_register_requests(
    const std::vector<IrOp>& ops, std::size_t maximum_requests = 4);

struct ContextualCopyLayoutOptions {
  const std::vector<PreloadReport>* runtime_preloads = nullptr;
  std::size_t maximum_candidates = 2;
  std::size_t maximum_work = 400000;
  std::size_t maximum_copy_sites = 4;
  std::size_t maximum_prefix_commands = 4;
};

// Complete transactions: web allocation + contextual copy elimination + a
// bounded shared-prefix/jump interchange. Every unchanged indirect entry is
// pinned by identity, not by a name or a chosen address. The ordinary compact
// deletion is retained when no interchange is necessary. Only final closed
// artifacts with smaller cell counts are returned.
std::vector<PassResult> contextual_copy_layout_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const ContextualCopyLayoutOptions& options = {});

} // namespace mkpro::core::passes
