#pragma once

#include "mkpro/core/cyclic_end_return.hpp"
#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/result.hpp"
#include "mkpro/core/terminal_report_tail.hpp"

#include <compare>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace mkpro::core {

// One compiler-protocol entry state. `return_stack.back()` is the next В/О
// destination. The compiler-owned manual-entry analysis must enumerate every
// possible stack state; an address-only entry silently assuming an empty stack
// is not accepted by this API.
struct ExternalEntryState {
  int pc = -1;
  std::vector<int> return_stack;
  auto operator<=>(const ExternalEntryState&) const = default;
};

// Authoritative non-boolean inputs produced by compiler CFG/lowering protocol
// analysis. This type is deliberately not inferred from comments, names, or
// optimizer switches. The maps are total: every indirect flow or indirect
// memory command in `items` must have exactly one entry. Terminal-vs-resumable
// STOP provenance is read directly from MachineItem::stop_disposition, so it
// cannot diverge from a separately reindexed caller-supplied set.
struct TerminalCyclicControlFlow {
  std::vector<ExternalEntryState> external_entries;
  std::map<std::size_t, std::vector<int>> indirect_flow_targets;
  std::map<std::size_t, std::vector<int>> indirect_memory_targets;
};

struct TerminalCyclicLayoutOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
  int maximum_return_depth = 5;
  int maximum_execution_states = 20000;
};

// Audit record produced entirely by the verifier.  The three legacy proof
// objects are intentionally outputs, never public inputs: every boolean they
// contain is derived from the reconstructed CFG, register liveness, external
// entries, relocation identity ledger, opcode effects, and complete indirect
// maps.
struct TerminalCyclicLayoutPlan {
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

  std::map<std::size_t, std::vector<int>> terminal_indirect_flow_targets;
  std::map<std::size_t, std::vector<int>> terminal_indirect_memory_targets;
  std::map<std::size_t, std::vector<int>> final_indirect_flow_targets;
  std::map<std::size_t, std::vector<int>> final_indirect_memory_targets;
  std::vector<ExternalEntryState> terminal_external_entries;
  std::vector<ExternalEntryState> final_external_entries;
  std::vector<std::string> reasons;
};

struct TerminalCyclicLayoutResult {
  std::vector<MachineItem> items;
  TerminalCyclicLayoutPlan plan;
  std::vector<passes::AppliedOptimization> optimizations;
  int applied = 0;
  int removed_cells = 0;
};

// Discover and verify one generic KAND/Kfrac/direct-condition/dot/STOP report
// tail.  Stable raw-zero selectors are derived from delivered preloads and
// register reaching-def analysis.  The terminal payload slot is derived by
// exploring every reachable call/return continuation to a recall+STOP sink.
// The returned plan also independently probes the 106->105 cyclic-end proof.
TerminalCyclicLayoutPlan
verify_terminal_cyclic_layout(const std::vector<MachineItem>& items,
                              const std::vector<PreloadReport>& preloads,
                              const TerminalCyclicControlFlow& control_flow,
                              const TerminalCyclicLayoutOptions& options = {});

// Reconstruct proofs, apply the two-cell terminal rewrite transactionally,
// then apply the one-cell cyclic return only when its independent complete-map
// proof and final-artifact recheck both succeed.  A failed terminal proof
// returns the input unchanged; a failed optional cyclic proof keeps the proved
// terminal result.
TerminalCyclicLayoutResult
optimize_terminal_cyclic_layout(const std::vector<MachineItem>& items,
                                const std::vector<PreloadReport>& preloads,
                                const TerminalCyclicControlFlow& control_flow,
                                const TerminalCyclicLayoutOptions& options = {});

} // namespace mkpro::core
