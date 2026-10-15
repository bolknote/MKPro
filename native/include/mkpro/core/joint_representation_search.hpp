#pragma once

#include "mkpro/core/layout_search_artifact.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/search_frontier.hpp"

#include <functional>
#include <vector>

namespace mkpro::core {

// Preserve an already proved checkpoint across memory-model search roots only
// when opcode effects, selectors and exact execution/return contexts agree.
// This is not a relocation or permission to reinterpret short-side aliases.
std::optional<LayoutSearchArtifact> transport_layout_search_artifact(
    const LayoutSearchArtifact& artifact, FeatureProfile source_profile,
    FeatureProfile target_profile, std::string* rejection = nullptr);

// Reusable proof-valid edges; the compiler's existing complete-artifact
// finalizer, not a local cell delta, decides whether to publish a candidate.
std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
expand_joint_representation_transactions(const LayoutSearchArtifact& artifact,
                                         const CompileOptions& options,
                                         bool preload_environment_complete = true);

// This search keeps semantic alternatives with unresolved physical bindings.
// Encoding, register-web equalities and region sharing can compose before a
// single placement/preload solution is required. The finalizer sees only
// closed, independently checked artifacts, never a conditional candidate.
inline constexpr TransactionSearchLimits kJointRepresentationConstraintLimits{
    .maximum_expansions = 12, .maximum_candidates = 64,
    .maximum_depth = 6, .maximum_growth_cells = 4};

using JointRepresentationFinalizer =
    std::function<std::optional<LayoutSearchArtifact>(const LayoutSearchArtifact&)>;
TransactionSearchResult<LayoutSearchArtifact> search_joint_representation_constraints(
    const LayoutSearchArtifact& root, const CompileOptions& options,
    const JointRepresentationFinalizer& finalize = {},
    const TransactionSearchLimits& limits = kJointRepresentationConstraintLimits,
    bool preload_environment_complete = true);

// Validate nonwinning complete artifacts in a separate bounded composition
// frontier. Exploration never replaces the mandatory publication finalizer.
TransactionSearchResult<LayoutSearchArtifact> search_joint_representation_constraints(
    const LayoutSearchArtifact& root, const CompileOptions& options,
    const JointRepresentationFinalizer& finalize, const TransactionSearchLimits& limits,
    bool preload_environment_complete,
    const JointRepresentationFinalizer& finalize_intermediate);

namespace passes {
PassResult joint_representation_search(const std::vector<IrOp>& ops,
                                       const PassContext& context);
IrPass joint_representation_search_pass();
} // namespace passes
} // namespace mkpro::core
