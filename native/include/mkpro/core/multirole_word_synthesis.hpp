#pragma once

#include "mkpro/core/constant_synthesis.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mkpro::core {

// A row describes one mode. Identity is an opaque logical destination, not a
// spelling convention; equal identities must receive the same physical entry.
struct FiniteWordAddressDomain {
  std::string identity;
  std::string selector;
  std::vector<int> targets;
  AddressSpaceModel model = AddressSpaceModel::Standard;
};

struct MultiroleWordEncoding {
  std::vector<std::string> words;
  std::vector<ConstantObservationKind> decoders;
  std::vector<int> targets;
};

struct MultiroleWordSynthesisResult {
  std::vector<MultiroleWordEncoding> encodings;
  std::size_t work = 0;
  bool exhausted = false;
};

// Intersect all data-decoder preimages with the available address domains.
// The initial domain is exact ordinary decimal words and stable R7..Re flow
// selectors. Raw BCD requires its own hardware certificate and is not coerced
// to host floating point here. A bounded search is not an impossibility proof.
MultiroleWordSynthesisResult synthesize_multirole_words(
    const std::vector<std::vector<std::string>>& consumer_values,
    const std::vector<FiniteWordAddressDomain>& addresses,
    const FiniteConstantSynthesisOptions& options = {});

bool certify_multirole_words(
    const MultiroleWordEncoding& encoding,
    const std::vector<std::vector<std::string>>& consumer_values,
    const std::vector<FiniteWordAddressDomain>& addresses);

} // namespace mkpro::core
