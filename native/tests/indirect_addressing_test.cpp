#include "mkpro/core/indirect_addressing.hpp"

#include "test_support.hpp"

namespace mkpro::tests {

namespace {

void require_mutation(core::IndirectSelectorMutation actual,
                      core::IndirectSelectorMutation expected,
                      const std::string& message) {
  require(actual == expected, message);
}

}  // namespace

void indirect_addressing_matches_typescript_contract() {
  require_mutation(core::indirect_selector_mutation("0"),
                   core::IndirectSelectorMutation::PreDecrement,
                   "R0 should pre-decrement for indirect addressing");
  require_mutation(core::indirect_selector_mutation("4"),
                   core::IndirectSelectorMutation::PreIncrement,
                   "R4 should pre-increment for indirect addressing");
  require_mutation(core::indirect_selector_mutation("7"),
                   core::IndirectSelectorMutation::Stable,
                   "R7 should be stable for indirect addressing");
  require(core::is_stable_indirect_selector("e"), "Re should be a stable indirect selector");

  require(core::memory_target_from_transformed("02") == 2,
          "transformed 02 should target R2");
  require(core::memory_target_from_transformed("14") == 0x0e,
          "nonzero tens transformed 14 should target Re");
  require(core::memory_target_from_transformed("-99999999") == 3,
          "negative transformed selector should use the nonzero-tens tail table");

  const auto pre_increment =
      core::evaluate_indirect_address("5", "7", core::IndirectOperationKind::Memory);
  require(pre_increment.has_value(), "R5 memory selector 7 should evaluate");
  require(pre_increment->mutation == core::IndirectSelectorMutation::PreIncrement,
          "R5 should report pre-increment mutation");
  require(pre_increment->transformed == "8", "R5 selector 7 should transform to 8");
  require(pre_increment->memory_target == 8, "transformed 8 should target R8");

  const auto stable_memory =
      core::evaluate_indirect_address("7", "14", core::IndirectOperationKind::Memory);
  require(stable_memory.has_value(), "R7 memory selector 14 should evaluate");
  require(stable_memory->mutation == core::IndirectSelectorMutation::Stable,
          "R7 should report stable mutation");
  require(stable_memory->transformed == "14", "stable selector should preserve 14");
  require(stable_memory->memory_target == 0x0e, "stable transformed 14 should target Re");

  const auto fractional_r0 =
      core::evaluate_indirect_address("0", "0.5", core::IndirectOperationKind::Memory);
  require(fractional_r0.has_value(), "R0 fractional memory selector should evaluate");
  require(fractional_r0->transformed == "99", "R0 fractional selector should transform to 99");
  require(fractional_r0->memory_target == 3, "R0 fractional memory selector should target R3");
  require(fractional_r0->result_value == "-99999999",
          "R0 fractional selector should leave the sentinel result value");

  const auto official_flow =
      core::evaluate_indirect_address("7", "99", core::IndirectOperationKind::Flow);
  require(official_flow.has_value(), "official indirect flow target should evaluate");
  require(official_flow->flow_target == 99, "transformed 99 should flow to address 99");
  require(official_flow->formal_address.has_value() &&
              official_flow->formal_address->opcode == 0x99,
          "official flow target 99 should use formal opcode 99");

  const auto super_dark_flow =
      core::evaluate_indirect_address("7", "FA", core::IndirectOperationKind::Flow);
  require(super_dark_flow.has_value(), "hex super-dark indirect flow target should evaluate");
  require(super_dark_flow->flow_target == 0xfa,
          "hex transformed FA should keep the hex flow target");
  require(super_dark_flow->formal_address.has_value() &&
              super_dark_flow->formal_address->kind == FormalAddressKind::SuperDark,
          "formal FA should be reported as super-dark");
  require(super_dark_flow->super_dark.has_value() &&
              super_dark_flow->super_dark->entry_address == 48 &&
              super_dark_flow->super_dark->continuation_address == 1,
          "FA should expose the super-dark entry and continuation");

  const auto stock_a5 =
      core::evaluate_indirect_address("7", "A5", core::IndirectOperationKind::Flow);
  require(stock_a5.has_value() && stock_a5->actual_flow_target == 0,
          "stock A5 indirect flow should remain a dark alias to 00");

  const auto expanded_a5 = core::evaluate_indirect_address(
      "7", "A5", core::IndirectOperationKind::Flow, AddressSpaceModel::Mk61SMiniExpanded);
  require(expanded_a5.has_value() && expanded_a5->actual_flow_target == 105,
          "expanded A5 indirect flow should target physical cell 105");

  const auto expanded_b2 = core::evaluate_indirect_address(
      "7", "B2", core::IndirectOperationKind::Flow, AddressSpaceModel::Mk61SMiniExpanded);
  require(expanded_b2.has_value() && expanded_b2->actual_flow_target == 0,
          "expanded B2 indirect flow should be the first dark alias to 00");
}

}  // namespace mkpro::tests
