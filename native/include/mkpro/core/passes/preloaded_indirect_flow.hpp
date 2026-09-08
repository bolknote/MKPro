#pragma once

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/formal_address.hpp"

#include <set>
#include <string>
#include <vector>

namespace mkpro::core::passes {

// Unconstrained registers come first; decimal-only registers may be read by
// counter-mutation instructions, but only with proved dead machine results.
struct StableFlowSelectorRegisters {
  std::vector<std::string> registers;
  std::set<std::string> decimal_only;
};

StableFlowSelectorRegisters available_stable_flow_selectors(
    const std::vector<IrOp>& ops, const std::set<std::string>& reserved,
    AddressSpaceModel model);

struct IndirectFlowOptions {
  bool relax_max_target_guard = false;
  // When true, sites whose target lies AFTER the site address (forward) are also
  // eligible for indirect conversion. Off by default because pre-layout the
  // exact addresses are not yet pinned; the post-layout rescue sets this.
  bool allow_forward_targets = false;
};

PassResult run_preloaded_indirect_flow(const std::vector<IrOp>& ops,
                                       const PassContext& context,
                                       const IndirectFlowOptions& flow_options = {});
PassResult preloaded_indirect_flow(const std::vector<IrOp>& ops, const PassContext& context);
IrPass preloaded_indirect_flow_pass();

PassResult runtime_indirect_call_flow(const std::vector<IrOp>& ops, const PassContext& context);
IrPass runtime_indirect_call_flow_pass();

} // namespace mkpro::core::passes
