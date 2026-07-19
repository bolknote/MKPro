#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/result.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::core {

// A two-cell decimal number-entry charge that is filled only after procedure
// ordering and all other layout-changing transformations have finished.
enum class LateBoundDecimalSelectorPart {
  High,
  Low,
};

struct LateBoundDecimalSelectorOptions {
  // The MK-61 charge is deliberately limited to a two-digit decimal address.
  // Callers may admit a leading-zero target (00..09), while addresses above
  // 99 can never be represented by this fixed-width charge.
  int minimum_target_address = 10;
  int maximum_target_address = 99;

  // Strict placeholders make accidental rewriting of an ordinary numeric
  // literal impossible. The default marker represents a pair of zero digits.
  int placeholder_opcode = 0x00;
  std::string placeholder_mnemonic = "0";
};

struct LateBoundDecimalSelectorProof {
  std::string target_label;
  int target_address = 0;
  std::size_t high_item_index = 0;
  std::size_t low_item_index = 0;
  int high_cell_address = 0;
  int low_cell_address = 0;
  int high_digit = 0;
  int low_digit = 0;
};

struct LateBoundDecimalSelectorResult {
  std::vector<MachineItem> items;
  int applied = 0;
  std::vector<Diagnostic> diagnostics;
  std::vector<LateBoundDecimalSelectorProof> proofs;
};

// Returns one of these exact role forms:
//   late-decimal-selector-high:<target-label>
//   late-decimal-selector-low:<target-label>
// The target is the complete suffix, so punctuation in label names is safe.
// Empty target labels are rejected.
std::string make_late_bound_decimal_selector_role(LateBoundDecimalSelectorPart part,
                                                  std::string_view target_label);

// Returns the unique opaque target labels named by all well-formed high/low
// marker roles. A malformed marker fails closed so layout passes can use this
// as the same authoritative target set as the final binder.
std::optional<std::vector<std::string>>
late_bound_decimal_selector_target_labels(const std::vector<MachineItem>& items);

// Resolve every strictly marked adjacent high/low placeholder pair against the
// layout represented by `items`. This operation is atomic: any malformed pair,
// missing/duplicate label, or out-of-range target returns the original items
// with applied == 0 and no proof records.
LateBoundDecimalSelectorResult
bind_late_bound_decimal_selectors(const std::vector<MachineItem>& items,
                                  const LateBoundDecimalSelectorOptions& options = {});

} // namespace mkpro::core
