#pragma once

#include "mkpro/core/passes/helpers.hpp"

#include <cstddef>
#include <vector>

namespace mkpro::core::passes {

struct CommonProcessorFactoringOptions {
  const std::vector<PreloadReport>* runtime_preloads = nullptr;
  std::size_t maximum_candidates = 2;
  std::size_t maximum_domain_pairs = 64;
  std::size_t maximum_proof_work = 100000;
};

// Deferred, equivalence-proved alternatives, NOT standalone rewrites.
// Factor P; store u; recall v; P into the shared consumer of two arguments.
// A caller with already-normalized arguments can adopt this ABI only after an
// exhaustive exact-decimal proof over its complete finite input ranges and a
// full stack/register/X1/X2 continuation proof. Other callers retain a body
// entry. Selector words and physical addresses remain layout obligations.
// No function name, constant value, source expression or program size is used.
std::vector<PassResult> common_processor_factoring_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const CommonProcessorFactoringOptions& options = {});

} // namespace mkpro::core::passes
