#pragma once

#include "mkpro/core/passes/helpers.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace mkpro::core::passes {

PassResult redundant_literal_reload(const std::vector<IrOp>& ops,
                                    const PassContext& context);
PassResult finalization_redundant_literal_reload(
    const std::vector<IrOp>& ops, const PassContext& context);

// Find one post-layout digit reload whose visible X is already equal and whose
// extra stack/X2 lift is proved to converge across the exact call/return CFG.
// Geometry is deliberately not changed here; the caller must perform its
// normal target/preload retarget transaction after deleting the returned cell.
std::optional<std::size_t>
post_layout_redundant_literal_reload_item(const std::vector<MachineItem>& items);

IrPass redundant_literal_reload_pass();

} // namespace mkpro::core::passes
