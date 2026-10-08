#pragma once

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"

namespace mkpro::core { struct NaturalTargetComponentLayoutOptions; }

namespace mkpro::core::passes {

// Move a caller's literal behind a shared selector store instead of restoring
// it by rotating the stack. Every other caller must prove that its displaced
// input is dead. This runs only on symbolic, compiler-owned selector charges.
PassResult selector_charge_literal_sinking(const std::vector<IrOp>& ops,
                                           const PassContext& context);

// Re-derive the sunk literal and its unique producer identity from the final
// artifact, then prove the continuation for one independently checked charge.
// The caller must also prove that this charge reaches the named store and
// that all incoming entries/selector uses are accounted for.
bool prove_selector_charge_sunk_literal_entry(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    std::size_t store, std::size_t charge_start, int leaf);

struct SelectorChargeLiteralLayoutResult {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  AuthoritativePostLayoutControlFlow final_control_flow;
  std::vector<AppliedOptimization> optimizations;
  int applied = 0;
  int removed_cells = 0;
  std::vector<std::string> reasons;
};

// Retry the same IR proof after tail-entry/fallthrough layout. Numeric targets
// become opaque command identities; every indirect destination must keep its
// physical address, so no preload or runtime selector is silently retuned.
// Empty-stack return semantics require an explicit caller-supplied policy.
SelectorChargeLiteralLayoutResult optimize_post_layout_selector_charge_literal_sinking(
    const std::vector<MachineItem>& items,
    const PostLayoutControlFlowOptions& options = {});

// Joint data/control materialization: place the literal-consuming leaf at
// the literal's value, then reuse its selector as the data input. Other
// modes must independently discard that input. Compare only final layouts.
// Geometry is also solved after literal erasure; immutable companion addresses
// are explicit deferred obligations, never permission to change observed data.
SelectorChargeLiteralLayoutResult optimize_selector_data_literal_coalescing(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control,
    const NaturalTargetComponentLayoutOptions& layout_options);

struct SelectorDataLiteralLayoutFrontier {
  std::vector<SelectorChargeLiteralLayoutResult> alternatives;
  std::vector<std::string> reasons;
};

// Keep independently proved alternatives, including neutral or bounded-debt
// transactions which release a conflicting immutable flow selector.
// Only the caller's complete downstream result may replace the incumbent.
SelectorDataLiteralLayoutFrontier selector_data_literal_layout_frontier(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control,
    const NaturalTargetComponentLayoutOptions& layout_options);

} // namespace mkpro::core::passes
