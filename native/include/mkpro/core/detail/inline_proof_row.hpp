#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::detail {

// Exact graph rows normally have a small, hardware-bounded shape. Store the
// common prefix in the row itself without imposing a bound on proof inputs.
template <class T, std::size_t Capacity>
class InlineProofRow {
  static_assert(Capacity > 0 && std::is_trivially_copyable_v<T>);
  struct Small {
    std::array<T, Capacity> values{};
    std::size_t size = 0;
  };
  using Overflow = std::vector<T>;

 public:
  InlineProofRow() = default;
  InlineProofRow(std::initializer_list<T> values) {
    for (const auto& value : values) push_back(value);
  }

  std::size_t size() const noexcept {
    if (const auto* row = std::get_if<Small>(&storage_)) return row->size;
    return std::get<Overflow>(storage_).size();
  }
  bool empty() const noexcept { return size() == 0; }
  const T* data() const noexcept {
    if (const auto* row = std::get_if<Small>(&storage_)) return row->values.data();
    const auto& overflow = std::get<Overflow>(storage_);
    // Keep pointer ranges valid even on an emptied or moved-from overflow.
    return overflow.empty() ? &empty_value_ : overflow.data();
  }
  T* data() noexcept {
    if (auto* row = std::get_if<Small>(&storage_)) return row->values.data();
    auto& overflow = std::get<Overflow>(storage_);
    return overflow.empty() ? &empty_value_ : overflow.data();
  }
  T* begin() noexcept { return data(); }
  T* end() noexcept { return data() + size(); }
  const T* begin() const noexcept { return data(); }
  const T* end() const noexcept { return data() + size(); }
  T& operator[](std::size_t i) noexcept {
    if (auto* row = std::get_if<Small>(&storage_)) return row->values[i];
    return std::get<Overflow>(storage_)[i];
  }
  const T& operator[](std::size_t i) const noexcept { return data()[i]; }
  T& at(std::size_t i) {
    if (i >= size()) throw std::out_of_range("proof row index");
    return (*this)[i];
  }
  const T& at(std::size_t i) const {
    if (i >= size()) throw std::out_of_range("proof row index");
    return (*this)[i];
  }
  const T& front() const noexcept { return (*this)[0]; }
  const T& back() const noexcept { return (*this)[size() - 1]; }

  void push_back(T value) {
    if (auto* row = std::get_if<Small>(&storage_)) {
      if (row->size < Capacity) {
        row->values[row->size++] = value;
        return;
      }
      Overflow overflow;
      overflow.reserve(Capacity * 2U);
      overflow.insert(overflow.end(), row->values.begin(), row->values.end());
      overflow.push_back(value);
      storage_.template emplace<Overflow>(std::move(overflow));
    } else {
      std::get<Overflow>(storage_).push_back(value);
    }
  }
  void clear() noexcept { storage_.template emplace<Small>(); }

  // For an already sorted row, retain the same publication order as a set.
  void insert_sorted_unique(T value) {
    const auto first = begin();
    const auto place = std::lower_bound(first, end(), value);
    if (place != end() && *place == value) return;
    const auto offset = place - first;
    if (auto* row = std::get_if<Small>(&storage_)) {
      if (row->size < Capacity) {
        auto* const at = row->values.data() + offset;
        auto* const last = row->values.data() + row->size;
        std::move_backward(at, last, last + 1);
        *at = value;
        ++row->size;
        return;
      }
      Overflow overflow;
      overflow.reserve(Capacity * 2U);
      overflow.insert(overflow.end(), row->values.begin(), row->values.end());
      overflow.insert(overflow.begin() + offset, value);
      storage_.template emplace<Overflow>(std::move(overflow));
    } else {
      auto& overflow = std::get<Overflow>(storage_);
      overflow.insert(overflow.begin() + offset, value);
    }
  }

  template <class Predicate>
  void erase_if(Predicate predicate) {
    if (auto* row = std::get_if<Small>(&storage_)) {
      const auto first = row->values.begin();
      row->size = static_cast<std::size_t>(
          std::remove_if(first, first + row->size, predicate) - first);
    } else {
      std::erase_if(std::get<Overflow>(storage_), predicate);
    }
  }

  void sort_unique() {
    if (auto* row = std::get_if<Small>(&storage_)) {
      const auto first = row->values.begin();
      const auto last = first + row->size;
      std::sort(first, last);
      row->size = static_cast<std::size_t>(std::unique(first, last) - first);
    } else {
      auto& overflow = std::get<Overflow>(storage_);
      std::sort(overflow.begin(), overflow.end());
      overflow.erase(std::unique(overflow.begin(), overflow.end()), overflow.end());
    }
  }

  bool operator==(const InlineProofRow& other) const {
    return std::equal(begin(), end(), other.begin(), other.end());
  }
  auto operator<=>(const InlineProofRow& other) const
      requires std::three_way_comparable<T> {
    return std::lexicographical_compare_three_way(begin(), end(), other.begin(), other.end());
  }
  template <class Allocator>
  bool operator==(const std::vector<T, Allocator>& other) const {
    return std::equal(begin(), end(), other.begin(), other.end());
  }

 private:
  inline static T empty_value_{};
  std::variant<Small, Overflow> storage_{Small{}};
};

} // namespace mkpro::core::detail
