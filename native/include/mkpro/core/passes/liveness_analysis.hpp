#pragma once

#include "mkpro/core/passes/helpers.hpp"

#include <utility>

namespace mkpro::core::passes {

struct RegisterEffects {
  RegisterValueSet uses;
  RegisterValueSet must_defs;
  RegisterValueSet may_defs;
  bool uses_all_registers = false;
  bool may_define_any_register = false;
};

struct CallContextLifetime {
  std::size_t instruction = 0;
  RegisterValueSet live_in;
  RegisterValueSet live_out;
};

struct LivenessInfo {
  std::vector<RegisterValueSet> live_in;
  std::vector<RegisterValueSet> live_out;
  bool control_flow_targets_are_exact = true;
  std::vector<int> conservative_flow_sources;
  bool includes_physical_register_universe = true;
  // The public per-instruction sets remain conservative unions. Interference
  // must use the separate rows: unioning two invocations of a shared helper
  // before making a clique invents conflicts between unrelated callers.
  bool matched_call_contexts = false;
  std::vector<CallContextLifetime> call_context_lifetimes;
  // These pairs have equal, compiler-owned entry values and no feasible
  // read of either old value after a write to the other, before its next
  // definition. Ordinary live sets deliberately remain conservative unions.
  std::set<std::pair<std::string, std::string>> guarded_disjoint_pairs = {};
};

struct LivenessOptions {
  bool unknown_indirect_flow_to_all = true;
  bool unresolved_direct_flow_to_all = true;
  // Logical-register allocation has an unbounded symbolic namespace. Adding
  // R0..Re as unrelated nodes there would consume fifteen colors before any
  // source value is considered.
  bool include_physical_register_universe = true;
  // Only explicit scalar literal classes may authorize entry-value sharing.
  // Absence keeps the ordinary liveness/interference contract unchanged.
  std::map<std::string, std::string> equal_entry_value_classes = {};
  // A complete compiler-owned program starts at instruction zero. This only
  // narrows the guarded proof's entry set; ordinary live sets still include
  // disconnected fragments. Standalone IR fragments keep arbitrary entries.
  bool closed_program_entry = false;
};

struct RegisterInterferenceGraph {
  std::map<std::string, RegisterValueSet> neighbors;

  bool interferes(const std::string& left, const std::string& right) const;
};

RegisterEffects register_effects(const IrOp& op);
LivenessInfo compute_liveness(const std::vector<IrOp>& ops, LivenessOptions options = {});
RegisterInterferenceGraph build_register_interference_graph(const std::vector<IrOp>& ops);
RegisterInterferenceGraph build_register_interference_graph(const std::vector<IrOp>& ops,
                                                            const LivenessInfo& liveness);

} // namespace mkpro::core::passes
