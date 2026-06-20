#include "mkpro/core/register_allocator.hpp"

#include "mkpro/core/emit/lowering_helpers.hpp"

#include <array>
#include <stdexcept>

namespace mkpro::core {

namespace {

constexpr std::string_view k_dispatch_scratch_prefix = "__dispatch_";
constexpr std::string_view k_grid4_mask_scratch_prefix = "__grid4_mask_";
constexpr std::string_view k_bit_mask_scratch_prefix = "__bit_mask_";
constexpr std::string_view k_if_selector_scratch_prefix = "__if_selector_";
constexpr std::string_view k_bank_selector_prefix = "__bank_selector_";
constexpr std::string_view k_display_template_value_prefix = "__display_value_";
constexpr std::string_view k_display_template_loop_prefix = "__display_loop_";
constexpr std::string_view k_display_template_mask_prefix = "__display_mask_";

bool starts_with(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

std::optional<int> first_free(const std::set<int>& used, std::initializer_list<int> order) {
  for (const int candidate : order) {
    if (!used.contains(candidate))
      return candidate;
  }
  return std::nullopt;
}

std::optional<int> first_free_ascending(const std::set<int>& used) {
  for (const int candidate : register_order_indices()) {
    if (!used.contains(candidate))
      return candidate;
  }
  return std::nullopt;
}

std::optional<int> first_free_descending(const std::set<int>& used) {
  const auto& order = register_order_indices();
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    if (!used.contains(*it))
      return *it;
  }
  return std::nullopt;
}

}  // namespace

const std::vector<int>& register_order_indices() {
  static const std::vector<int> order = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                                        0x8, 0x9, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e};
  return order;
}

std::string register_name_for_index(int index) {
  if (index < k_min_register_index || index > k_max_register_index)
    throw std::out_of_range("MK-61 register index is outside 0..e");
  if (index < 10)
    return std::to_string(index);
  return std::string(1, static_cast<char>('a' + index - 10));
}

std::optional<int> pick_register_index_for_variable(std::string_view variable,
                                                     const std::set<int>& used) {
  if (starts_with(variable, k_dispatch_scratch_prefix) ||
      starts_with(variable, k_grid4_mask_scratch_prefix) ||
      starts_with(variable, k_bit_mask_scratch_prefix) ||
      starts_with(variable, k_if_selector_scratch_prefix) ||
      starts_with(variable, k_bank_selector_prefix) ||
      starts_with(variable, k_display_template_mask_prefix)) {
    return first_free_descending(used);
  }

  if (starts_with(variable, k_display_template_loop_prefix))
    return first_free(used, {0x1, 0x0, 0x2, 0x3});

  if (starts_with(variable, k_display_template_value_prefix))
    return first_free(used, {0x2, 0x0, 0x3, 0x4, 0x5, 0x6, 0x8, 0x9, 0x0a, 0x0b,
                             0x0c, 0x0d, 0x0e});

  if (variable == emit::k_coord_list_pointer)
    return first_free(used, {0x5, 0x4, 0x6});

  if (variable == emit::k_coord_list_counter)
    return first_free(used, {0x2, 0x3, 0x1, 0x0});

  if (variable == emit::k_coord_list_current || variable == emit::k_coord_list_dx)
    return first_free(used, {0x0, 0x1, 0x3, 0x4});

  if (starts_with(variable, emit::k_spatial_count_scratch_prefix)) {
    if (variable == emit::spatial_count_counter_scratch_name()) {
      const auto counter = first_free(used, {0x0, 0x1, 0x2, 0x3});
      if (counter.has_value())
        return counter;
    }
    return first_free_descending(used);
  }

  return first_free_ascending(used);
}

std::optional<int> pick_constant_register_index(const std::set<int>& used) {
  return first_free_descending(used);
}

}  // namespace mkpro::core
