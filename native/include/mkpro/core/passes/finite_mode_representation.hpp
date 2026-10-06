#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

// Select the stored representation of a finite, internal mode from its uses.
// Every reaching definition must be an ordinary literal and every read must
// apply the same supported projection. Calls are followed with their exact
// return contexts; aliases, external entries and hidden stack state are proof
// obligations, not assumptions based on source names.
PassResult finite_mode_representation(const std::vector<IrOp>& ops,
                                      const PassContext& context);
IrPass finite_mode_representation_pass();

} // namespace mkpro::core::passes
