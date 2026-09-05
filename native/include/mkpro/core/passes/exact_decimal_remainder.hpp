#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

// Precision proof for ordinary decimal arithmetic, not exotic/raw numerals.
// Every signed fractional quotient is a multiple of 1 / decimal_scale;
// both its shift by one and its scaled numerator fit eight significant digits.
struct ExactDecimalRemainderProof {
  int fraction_digits = 0;
  int decimal_scale = 1;
};

std::optional<ExactDecimalRemainderProof>
prove_exact_decimal_remainder_precision(int divisor);

PassResult exact_decimal_remainder_correction(const std::vector<IrOp>& ops,
                                             const PassContext& context);
IrPass exact_decimal_remainder_correction_pass();

} // namespace mkpro::core::passes
