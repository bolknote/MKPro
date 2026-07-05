#pragma once

#include "mkpro/core/feature_profile.hpp"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::core {

inline constexpr int k_min_register_index = 0x0;
inline constexpr int k_max_register_index = 0x0e;

const std::vector<int>& register_order_indices();
const std::vector<int>& register_order_indices(FeatureProfile profile);
std::string register_name_for_index(int index);
std::string register_name_for_index(int index, FeatureProfile profile);

std::optional<int> pick_register_index_for_variable(std::string_view variable,
                                                     const std::set<int>& used);
std::optional<int> pick_register_index_for_variable(std::string_view variable,
                                                     const std::set<int>& used,
                                                     FeatureProfile profile);
std::optional<int> pick_constant_register_index(const std::set<int>& used);
std::optional<int> pick_constant_register_index(const std::set<int>& used,
                                                FeatureProfile profile);

}  // namespace mkpro::core
