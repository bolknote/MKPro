#pragma once

#include "mkpro/core/formal_address.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

// These are observations of the stored value, not source-language patterns.
enum class ConstantObservationKind { Identity, Negate, Sign, Integer, Fraction };

struct ConstantObservation {
  ConstantObservationKind kind = ConstantObservationKind::Identity;
  std::string value;
};

struct ConstantAddressConstraint {
  std::string selector;
  int target = 0;
  AddressSpaceModel model = AddressSpaceModel::Standard;
};

struct ConstantSynthesisRequest {
  std::vector<ConstantObservation> observations;
  std::optional<ConstantAddressConstraint> address;
  std::size_t maximum_work = 4096;
};

enum class ConstantSynthesisStatus { Solved, Unsatisfiable, Unsupported, Exhausted };

struct ConstantSynthesisResult {
  ConstantSynthesisStatus status = ConstantSynthesisStatus::Unsupported;
  std::optional<std::string> value;
  std::size_t work = 0;
};

// Solve intersections of exact projection preimages and an optional stable
// indirect-address constraint. The search domain is ordinary, non-exponential
// keyboard literals with at most eight digits, including the leading zero.
// Signed zero, raw BCD and mutating selectors are outside this certificate.
// "Unsatisfiable" refers only to this explicitly bounded representation domain.
ConstantSynthesisResult synthesize_constant(const ConstantSynthesisRequest& request);

// An independent certificate checker; never accept search output merely because
// it was enumerated. Address observations include exact selector write-back.
bool constant_satisfies_observations(const std::string& value,
                                    const ConstantSynthesisRequest& request);

struct FiniteConstantSynthesisOptions {
  std::size_t maximum_work = 4096;
  std::size_t maximum_candidates = 64;
};

struct FiniteConstantSynthesisResult {
  std::vector<std::vector<std::string>> encodings;
  std::size_t work = 0;
  bool exhausted = false;
};

// Columns describe consumers; each column lists the required outputs for the
// same finite set of input modes. Generate encodings from the columns' rational
// lattices and intersections of cheap decoder preimages, not fixed magic seeds.
// Returned encodings are proposals, not equivalence proofs: the caller must
// certify each complete consumer table, aliases, CFG and hidden machine state.
FiniteConstantSynthesisResult synthesize_finite_constant_encodings(
    const std::vector<std::vector<std::string>>& consumer_values,
    const FiniteConstantSynthesisOptions& options = {});

} // namespace mkpro::core
