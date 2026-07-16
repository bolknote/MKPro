#pragma once

#include "mkpro/core/cyclic_end_return.hpp"
#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/helper_semantic_alias.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/result.hpp"
#include "mkpro/core/terminal_report_tail.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

struct TerminalCyclicLayoutOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
  int maximum_return_depth = 5;
  int maximum_execution_states = 20000;
  bool enable_return_alias = false;
  const std::vector<HelperSemanticContract>* helper_semantic_contracts = nullptr;
};

// Audit record produced entirely by the verifier.  The three legacy proof
// objects are intentionally outputs, never public inputs: every boolean they
// contain is derived from the reconstructed CFG, register liveness, external
// entries, relocation identity ledger, opcode effects, and complete indirect
// maps.
struct TerminalCyclicLayoutPlan {
  bool return_alias_proved = false;
  bool semantic_return_alias_proved = false;
  bool terminal_proved = false;
  bool cyclic_proved = false;
  bool final_artifact_proved = false;
  int input_cells = 0;
  int terminal_output_cells = 0;
  int output_cells = 0;
  int removed_cells = 0;

  RawZeroReturnSelectorProof raw_selector;
  TerminalContinuationLivenessProof continuation;
  TerminalReportTailRelocationProof relocation;
  TerminalReportTailVerification terminal_verification;
  CyclicEndReturnProof cyclic_verification;

  // Exact command identities and complete return stacks are retained across
  // each relocation.  Consumers never receive a second address-only CFG type.
  AuthoritativePostLayoutControlFlow terminal_control_flow;
  AuthoritativePostLayoutControlFlow final_control_flow;
  std::vector<std::string> reasons;
};

struct TerminalCyclicLayoutResult {
  std::vector<MachineItem> items;
  TerminalCyclicLayoutPlan plan;
  std::vector<passes::AppliedOptimization> optimizations;
  int applied = 0;
  int removed_cells = 0;
};

// Canonicalize every resolved direct-flow operand to a unique symbolic command
// identity without changing cell count or control flow. This lets subsequent
// cell-removing layout proofs rebind fixed targets instead of relying on stale
// physical addresses. Malformed, unresolved, or non-command targets fail closed.
std::optional<std::vector<MachineItem>>
symbolize_terminal_layout_direct_targets(
    const std::vector<MachineItem>& items,
    AddressSpaceModel model = AddressSpaceModel::Standard);

// Discover and verify one generic KAND/Kfrac/direct-condition/dot/STOP report
// tail.  Stable raw-zero selectors are derived from delivered preloads and
// register reaching-def analysis.  The terminal payload slot is derived by
// exploring every reachable call/return continuation to a recall+STOP sink.
// The returned plan also independently probes the 106->105 cyclic-end proof.
TerminalCyclicLayoutPlan
verify_terminal_cyclic_layout(const std::vector<MachineItem>& items,
                              const std::vector<PreloadReport>& preloads,
                              const AuthoritativePostLayoutControlFlow& control_flow,
                              const TerminalCyclicLayoutOptions& options = {});

// Reconstruct proofs, apply the two-cell terminal rewrite transactionally,
// then apply the one-cell cyclic return only when its independent complete-map
// proof and final-artifact recheck both succeed.  A failed terminal proof
// returns the input unchanged; a failed optional cyclic proof keeps the proved
// terminal result.
TerminalCyclicLayoutResult
optimize_terminal_cyclic_layout(const std::vector<MachineItem>& items,
                                const std::vector<PreloadReport>& preloads,
                                const AuthoritativePostLayoutControlFlow& control_flow,
                                const TerminalCyclicLayoutOptions& options = {});

} // namespace mkpro::core
