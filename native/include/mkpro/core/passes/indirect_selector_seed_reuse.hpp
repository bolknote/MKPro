#pragma once

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"

#include <map>
#include <string>

namespace mkpro::core::passes {

// Reuse a producer's selector-equivalent value before layout. The numerical
// value need not be equal, but the first admitted indirect access must make
// the two register states exactly equal before any ordinary observation.
PassResult indirect_selector_seed_reuse(const std::vector<IrOp>& ops,
                                        const PassContext& context);
// Conditional alternative for a joint representation/layout transaction.
// The result cannot be published before its selector equations are closed.
struct DeferredIndirectSelectorSeedReuse {
  PassResult transformation;
  std::map<std::string, int> encoded_target_origins;
};

DeferredIndirectSelectorSeedReuse deferred_indirect_selector_seed_reuse(
    const std::vector<IrOp>& ops, const PassContext& context,
    const PostLayoutControlFlowOptions& control,
    const std::map<std::string, int>& encoded_target_origins);

IrPass indirect_selector_seed_reuse_pass();

} // namespace mkpro::core::passes
