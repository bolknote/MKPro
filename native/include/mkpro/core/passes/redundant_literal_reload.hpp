#pragma once

#include "mkpro/core/passes/helpers.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace mkpro::core::passes {

struct SingleDigitLateSelectorPlan {
  std::size_t leading_zero_item = 0;
  std::size_t low_digit_item = 0;
  int leading_zero_address = 0;
  int target_address = 0;
  std::string target_label;
};

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

// Finds a bound 0N late-decimal selector whose target is in 00..09. Removing
// the leading zero changes only the raw X2 spelling after the surviving digit;
// the exact call/return CFG must prove that difference dead before any restore
// or display observation. Geometry and selector rebinding are left to the
// finalization transaction.
std::optional<SingleDigitLateSelectorPlan>
post_layout_single_digit_late_selector_plan(const std::vector<MachineItem>& items);

IrPass redundant_literal_reload_pass();

} // namespace mkpro::core::passes
