#pragma once

#include "mkpro/core/constant_synthesis.hpp"
#include "mkpro/core/mk61_trig.hpp"

#include <cstddef>
#include <map>
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

enum class RawWordControlView { StoredWord, DegreeTangent };

struct RawWordControlObservation {
  RawWordControlView view = RawWordControlView::StoredWord;
  FiniteWordAddressDomain address;
  // Optional sign of the decoded value before any indirect write-back.
  std::optional<int> sign;
};

struct RawWordProductObservation {
  int input_power10 = 0;
  int output_power10 = 0;
  int output_sign = 1;
};

struct RawWordMode {
  std::optional<int> sign;
  std::vector<RawWordProductObservation> right_products;
  std::vector<RawWordControlObservation> control;
};

enum class RawWordTransitionKind { Identity, Negate };

struct RawWordTransition {
  std::size_t from = 0;
  std::size_t to = 0;
  RawWordTransitionKind kind = RawWordTransitionKind::Negate;
};

struct RawWordSynthesisRequest {
  std::vector<RawWordMode> modes;
  std::vector<RawWordTransition> transitions;
  mk61_trig::AngleMode angle_mode = mk61_trig::AngleMode::Deg;
};

struct RawMultiroleWordEncoding {
  std::vector<std::string> words;
  std::map<std::string, int> target_bindings;
};

struct RawMultiroleWordSynthesisResult {
  std::vector<RawMultiroleWordEncoding> encodings;
  std::size_t work = 0;
  bool exhausted = false;
};

// Intersect arithmetic observations, several control views and transitions.
// Words come from a ROM-checked grammar, never from a program-specific seed
// table. Direct observations require exact repeated write-back preservation;
// derived selectors require stable repeated targets. Unknown domains reject.
// These are representation certificates, NOT full-program equivalence proofs:
// the caller must establish exhaustive reachable inputs/modes, mode/alias
// invariants, setup delivery and hidden-state liveness before rewriting code.
RawMultiroleWordSynthesisResult synthesize_raw_multirole_words(
    const RawWordSynthesisRequest& request,
    const FiniteConstantSynthesisOptions& options = {});

bool certify_raw_multirole_words(const RawMultiroleWordEncoding& encoding,
                                const RawWordSynthesisRequest& request);

} // namespace mkpro::core
