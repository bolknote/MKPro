#pragma once

#include "mkpro/core/layout_search_artifact.hpp"

#include <functional>
#include <utility>

namespace mkpro::core {
namespace detail { struct FiniteNumericLayoutCertificateBuilder; }

// A producer-owned closed numeric invariant, not permission to optimize raw
// arithmetic. Placement may preserve the exact opaque bytes only after this
// invariant has been rechecked on the complete current code and initializer
// environment. Scalar rewrites and final CFG/return proofs remain independent.
// Source roles, reports and serialized artifacts cannot construct a certificate.
class ClosedNumericLayoutCertificate {
public:
  bool matches(const std::vector<MachineItem>& items,
               const std::vector<PreloadReport>& preloads,
               AddressSpaceModel model, bool ordinary_decimal_input) const {
    return model == model_ && ordinary_decimal_input == ordinary_decimal_input_ &&
        verifier_(items, preloads);
  }

private:
  friend struct detail::FiniteNumericLayoutCertificateBuilder;
  using Verifier = std::function<bool(const std::vector<MachineItem>&,
                                     const std::vector<PreloadReport>&)>;
  ClosedNumericLayoutCertificate(AddressSpaceModel model, bool ordinary_decimal_input,
                                 Verifier verifier)
      : model_(model), ordinary_decimal_input_(ordinary_decimal_input),
        verifier_(std::move(verifier)) {}
  const AddressSpaceModel model_;
  const bool ordinary_decimal_input_;
  const Verifier verifier_;
};
} // namespace mkpro::core
