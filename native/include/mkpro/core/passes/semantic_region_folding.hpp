#pragma once

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core::passes {

// Each component is a maximal physical fallthrough chain. Multiple labelled
// entries within a component are retained. No new jump, call or return is added.
struct SymbolicRegionLayout {
  std::vector<IrOp> ops;
  std::size_t work = 0;
  bool changed = false;
};

// The caller owns the data proof for any changed runtime selectors. This
// routine proves only identity-preserving permutation and exact CFG/return
// transport. Every unchanged selector must also appear among required targets.
std::optional<SymbolicRegionLayout> layout_symbolic_regions(
    const std::vector<IrOp>& ops, const std::map<std::string, int>& required_targets,
    const PassContext& context, std::size_t maximum_work = 4096);

// Operand equalities are only allocator requests. The existing web allocator
// proves interference, aliases and entry values; folding rechecks the result.
std::vector<RegisterWebRecoloringRequest> semantic_region_register_requests(
    const std::vector<IrOp>& ops);

PassResult semantic_region_folding(const std::vector<IrOp>& ops,
                                   const PassContext& context);
IrPass semantic_region_folding_pass();

} // namespace mkpro::core::passes
