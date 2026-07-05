#pragma once

#include <optional>
#include <string_view>

namespace mkpro {

enum class FeatureProfile {
  Standard,
  Mk61SMiniExpanded,
};

inline constexpr std::string_view kMk61SMiniExpandedFeatureId = "mk61s-mini-expand";

inline std::optional<FeatureProfile> parse_feature_profile(std::string_view value) {
  if (value == "standard" || value == "mk61")
    return FeatureProfile::Standard;
  if (value == kMk61SMiniExpandedFeatureId)
    return FeatureProfile::Mk61SMiniExpanded;
  return std::nullopt;
}

inline std::string_view feature_profile_id(FeatureProfile profile) {
  switch (profile) {
    case FeatureProfile::Standard:
      return "mk61";
    case FeatureProfile::Mk61SMiniExpanded:
      return kMk61SMiniExpandedFeatureId;
  }
  return "mk61";
}

inline bool feature_profile_has_expanded_program_space(FeatureProfile profile) {
  return profile == FeatureProfile::Mk61SMiniExpanded;
}

inline bool feature_profile_has_rf_register(FeatureProfile profile) {
  return profile == FeatureProfile::Mk61SMiniExpanded;
}

inline int feature_profile_program_step_limit(FeatureProfile profile) {
  return feature_profile_has_expanded_program_space(profile) ? 112 : 105;
}

inline int feature_profile_max_register_index(FeatureProfile profile) {
  return feature_profile_has_rf_register(profile) ? 0x0f : 0x0e;
}

}  // namespace mkpro
