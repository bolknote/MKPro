#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

struct RegisterEffects {
  RegisterValueSet uses;
  RegisterValueSet must_defs;
  RegisterValueSet may_defs;
  bool uses_all_registers = false;
  bool may_define_any_register = false;
};

struct LivenessInfo {
  std::vector<RegisterValueSet> live_in;
  std::vector<RegisterValueSet> live_out;
  bool control_flow_targets_are_exact = true;
  std::vector<int> conservative_flow_sources;
  bool includes_physical_register_universe = true;
};

struct LivenessOptions {
  bool unknown_indirect_flow_to_all = true;
  bool unresolved_direct_flow_to_all = true;
  // Logical-register allocation has an unbounded symbolic namespace. Adding
  // R0..Re as unrelated nodes there would consume fifteen colors before any
  // source value is considered.
  bool include_physical_register_universe = true;
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
