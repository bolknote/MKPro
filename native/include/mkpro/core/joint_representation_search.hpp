#pragma once

#include "mkpro/core/layout_search_artifact.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/search_frontier.hpp"

#include <vector>

namespace mkpro::core {

// Reusable proof-valid edges; the compiler's existing complete-artifact
// finalizer, not a local cell delta, decides whether to publish a candidate.
std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
expand_joint_representation_transactions(const LayoutSearchArtifact& artifact,
                                         const CompileOptions& options);

namespace passes {
PassResult joint_representation_search(const std::vector<IrOp>& ops,
                                       const PassContext& context);
IrPass joint_representation_search_pass();
} // namespace passes
} // namespace mkpro::core
