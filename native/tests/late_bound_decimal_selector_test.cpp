#include "mkpro/core/late_bound_decimal_selector.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::tests {

namespace {

using core::LateBoundDecimalSelectorPart;

MachineItem placeholder(LateBoundDecimalSelectorPart part, const std::string& target) {
  MachineItem item = MachineItem::op(0x00, "0");
  item.roles.push_back(core::make_late_bound_decimal_selector_role(part, target));
  return item;
}

void append_filler(std::vector<MachineItem>& items, int cells) {
  for (int index = 0; index < cells; ++index)
    items.push_back(MachineItem::op(0x10, "+"));
}

bool has_diagnostic(const core::LateBoundDecimalSelectorResult& result, std::string_view code) {
  return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [code](const Diagnostic& diagnostic) { return diagnostic.code == code; });
}

std::vector<MachineItem> one_pair_at_target_address(const std::string& target, int target_address) {
  std::vector<MachineItem> items = {
      placeholder(LateBoundDecimalSelectorPart::High, target),
      placeholder(LateBoundDecimalSelectorPart::Low, target),
  };
  append_filler(items, target_address - 2);
  items.push_back(MachineItem::label(target));
  items.push_back(MachineItem::op(0x50, "C/P"));
  return items;
}

} // namespace

void late_bound_decimal_selector_binds_only_proved_pairs() {
  // The binder uses the final cell layout, permits repeated charges to the same
  // punctuation-bearing label, and preserves all non-digit metadata.
  {
    const std::string target = "shared:tail/entry";
    std::vector<MachineItem> items = {
        MachineItem::label("main"),
        placeholder(LateBoundDecimalSelectorPart::High, target),
        placeholder(LateBoundDecimalSelectorPart::Low, target),
        placeholder(LateBoundDecimalSelectorPart::High, target),
        placeholder(LateBoundDecimalSelectorPart::Low, target),
    };
    items.at(1).comment = "late charge";
    append_filler(items, 8);
    items.push_back(MachineItem::label("zero-width-alias"));
    items.push_back(MachineItem::label(target));
    items.push_back(MachineItem::op(0x50, "C/P"));

    const core::LateBoundDecimalSelectorResult result =
        core::bind_late_bound_decimal_selectors(items);
    require(result.diagnostics.empty(), "valid selector pairs should bind without diagnostics");
    require(result.applied == 2, "both charges to the same label should be bound");
    require(result.proofs.size() == 2U, "each applied charge should have a proof record");
    require(result.items.at(1).opcode == 1 && result.items.at(2).opcode == 2 &&
                result.items.at(3).opcode == 1 && result.items.at(4).opcode == 2,
            "target address 12 should materialize as decimal digit pairs 1,2");
    require(result.items.at(1).mnemonic == "1" && result.items.at(2).mnemonic == "2",
            "bound digit mnemonics should agree with their opcodes");
    require(result.items.at(1).comment == "late charge" && !result.items.at(1).roles.empty(),
            "binding should preserve comments and auditable marker roles");
    require(result.proofs.at(0).target_label == target &&
                result.proofs.at(0).target_address == 12 &&
                result.proofs.at(0).high_cell_address == 0 &&
                result.proofs.at(0).low_cell_address == 1 && result.proofs.at(0).high_digit == 1 &&
                result.proofs.at(0).low_digit == 2,
            "proof should record the final target and both charged cells");
    const std::optional<std::vector<std::string>> target_labels =
        core::late_bound_decimal_selector_target_labels(items);
    require(target_labels.has_value() &&
                *target_labels == std::vector<std::string>{target},
            "layout consumers should recover the same unique opaque target set as the binder");
  }

  // A missing target is a hard, atomic failure: even an otherwise valid pair is
  // left as the original two zero placeholders.
  {
    std::vector<MachineItem> items = {
        placeholder(LateBoundDecimalSelectorPart::High, "missing"),
        placeholder(LateBoundDecimalSelectorPart::Low, "missing"),
        MachineItem::op(0x50, "C/P"),
    };
    const core::LateBoundDecimalSelectorResult result =
        core::bind_late_bound_decimal_selectors(items);
    require(result.applied == 0 && result.proofs.empty(),
            "a missing target must prevent every application");
    require(result.items.at(0).opcode == 0 && result.items.at(1).opcode == 0,
            "atomic failure must retain the original placeholders");
    require(has_diagnostic(result, "late-decimal-selector-missing-label"),
            "missing targets should have a stable diagnostic code");
  }

  // Fixed-width charges may deliberately use a leading zero when a caller
  // proves that an early helper lives at 00..09.
  {
    const std::vector<MachineItem> items = one_pair_at_target_address("early", 8);
    core::LateBoundDecimalSelectorOptions options;
    options.minimum_target_address = 0;
    const core::LateBoundDecimalSelectorResult result =
        core::bind_late_bound_decimal_selectors(items, options);
    require(result.diagnostics.empty() && result.applied == 1 &&
                result.items.at(0).opcode == 0 && result.items.at(1).opcode == 8,
            "target address 08 should bind as an explicit leading-zero pair");
  }

  // Pair structure is strict: intervening labels/items, reversed halves, and
  // target disagreement are rejected rather than guessed.
  {
    std::vector<MachineItem> items = {
        placeholder(LateBoundDecimalSelectorPart::High, "target"),
        MachineItem::label("between"),
        placeholder(LateBoundDecimalSelectorPart::Low, "target"),
    };
    append_filler(items, 8);
    items.push_back(MachineItem::label("target"));
    const core::LateBoundDecimalSelectorResult result =
        core::bind_late_bound_decimal_selectors(items);
    require(has_diagnostic(result, "late-decimal-selector-nonadjacent-pair"),
            "a label between selector halves should break the strict pair");
    require(has_diagnostic(result, "late-decimal-selector-unexpected-low"),
            "an unpaired low half should also be diagnosed");
  }
  {
    std::vector<MachineItem> items = {
        placeholder(LateBoundDecimalSelectorPart::High, "left"),
        placeholder(LateBoundDecimalSelectorPart::Low, "right"),
    };
    append_filler(items, 8);
    items.push_back(MachineItem::label("left"));
    items.push_back(MachineItem::label("right"));
    const core::LateBoundDecimalSelectorResult result =
        core::bind_late_bound_decimal_selectors(items);
    require(has_diagnostic(result, "late-decimal-selector-mismatched-target"),
            "adjacent halves must name the same target");
  }

  // Markers do not authorize rewriting arbitrary code or raw source cells.
  {
    std::vector<MachineItem> items = one_pair_at_target_address("target", 10);
    items.at(0).opcode = 7;
    items.at(0).mnemonic = "7";
    const core::LateBoundDecimalSelectorResult result =
        core::bind_late_bound_decimal_selectors(items);
    require(has_diagnostic(result, "late-decimal-selector-not-placeholder"),
            "a marked non-placeholder digit must be rejected");
    require(result.items.at(0).opcode == 7, "rejected ordinary code must not be overwritten");
  }

  // Empty marker targets, duplicate labels, and addresses outside 10..99 each
  // fail explicitly.
  {
    std::vector<MachineItem> items = one_pair_at_target_address("target", 10);
    items.at(0).roles = {"late-decimal-selector-high:"};
    const core::LateBoundDecimalSelectorResult result =
        core::bind_late_bound_decimal_selectors(items);
    require(has_diagnostic(result, "late-decimal-selector-malformed-marker"),
            "an empty marker suffix must be rejected");
    require(!core::late_bound_decimal_selector_target_labels(items).has_value(),
            "the public target collector must fail closed on the same malformed marker");
  }
  {
    std::vector<MachineItem> items = one_pair_at_target_address("target", 10);
    items.push_back(MachineItem::label("target"));
    const core::LateBoundDecimalSelectorResult result =
        core::bind_late_bound_decimal_selectors(items);
    require(has_diagnostic(result, "late-decimal-selector-duplicate-label"),
            "ambiguous duplicate targets must be rejected");
  }
  for (const int address : {9, 100}) {
    const std::vector<MachineItem> items = one_pair_at_target_address("target", address);
    const core::LateBoundDecimalSelectorResult result =
        core::bind_late_bound_decimal_selectors(items);
    require(has_diagnostic(result, "late-decimal-selector-target-out-of-range"),
            "one- and three-digit target addresses must be rejected");
    require(result.applied == 0 && result.items.at(0).opcode == 0 && result.items.at(1).opcode == 0,
            "out-of-range failure must be atomic");
  }
  {
    const std::vector<MachineItem> items = one_pair_at_target_address("target", 10);
    core::LateBoundDecimalSelectorOptions options;
    options.maximum_target_address = 100;
    const core::LateBoundDecimalSelectorResult result =
        core::bind_late_bound_decimal_selectors(items, options);
    require(has_diagnostic(result, "late-decimal-selector-invalid-options"),
            "options must not widen a two-digit charge beyond address 99");
    require(result.applied == 0 && result.items.at(0).opcode == 0,
            "invalid options must leave the input untouched");
  }

  // The public marker constructor rejects an unusable empty label immediately.
  {
    bool threw = false;
    try {
      (void)core::make_late_bound_decimal_selector_role(LateBoundDecimalSelectorPart::High, "");
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    require(threw, "marker construction should reject an empty target label");
  }
}

} // namespace mkpro::tests
