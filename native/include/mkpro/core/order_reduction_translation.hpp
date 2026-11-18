#pragma once

#include "mkpro/core/post_layout_numeric_ranges.hpp"

namespace mkpro::core {

struct OrderReductionTranslationProof {
  bool proved = false;
  int minimum_seed = 0, maximum_seed = 0;
  std::size_t work = 0;
  std::vector<std::string> reasons;
};

// A negative-only preflight over the same relational transfer. Assume
// numeric side conditions can hold. Walk alias-independent transfers and
// defer unresolved paths; an unconditional rejection is still decisive.
// False includes inconclusive.
bool order_reduction_translation_is_structurally_impossible(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    std::size_t first_digit, std::size_t last_digit);

// Prove a family of integer translations of one fresh two-digit literal.
// Copies, equal-offset subtraction, positive ordinary Kmax and sign tests
// are relational operations, not a formula recognizer. Every addition must
// keep BOTH accumulators in the same eight-digit exponent band. Unknown
// consumers, selector observations, side entries and live hidden differences
// reject. Bounds and CFG must describe this exact immutable input artifact.
OrderReductionTranslationProof prove_order_reduction_translation(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const PostLayoutNumericRangeProof& bounds,
    std::size_t first_digit, std::size_t last_digit,
    std::size_t maximum_work = 100000);

} // namespace mkpro::core
