#include "mkpro/core/helper_semantic_alias.hpp"

#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/passes/return_suffix_gadget.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

MachineItem op(int opcode) {
  return MachineItem::op(opcode, opcode_by_code(opcode).name);
}

MachineItem terminal_stop() {
  MachineItem item = op(0x50);
  item.stop_disposition = StopDisposition::Terminal;
  return item;
}

MachineItem procedure_boundary(std::string label, std::string boundary,
                               std::string procedure = {}) {
  if (procedure.empty())
    procedure = label;
  MachineItem item = MachineItem::label(std::move(label));
  item.procedure_boundary = std::move(boundary);
  item.procedure_name = std::move(procedure);
  item.hidden = true;
  return item;
}

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const MachineItem& item) {
    return item.kind != MachineItemKind::Label;
  }));
}

bool has_label(const std::vector<MachineItem>& items, const std::string& name) {
  return std::any_of(items.begin(), items.end(), [&](const MachineItem& item) {
    return item.kind == MachineItemKind::Label && item.name == name;
  });
}

std::string compact_number(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) {
                               return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
                             }),
              value.end());
  return value;
}

struct PhysicalObservation {
  std::array<std::string, 5> stack;
  std::array<std::string, 15> memory;
  std::string display;
  std::string program_counter;
  bool stopped = false;
  bool operator==(const PhysicalObservation&) const = default;
};

PhysicalObservation run_physical_x1_probe(std::vector<int> opcodes, std::string x1,
                                          std::string x = "3.25", std::string y = "4.5") {
  emulator::MK61 calc;
  opcodes.push_back(0x50);
  const emulator::ProgramLoadResult loaded = calc.load_program(opcodes);
  require(loaded.diagnostics.empty(), "physical X1 premise probe should load");
  calc.set_register("x", std::move(x));
  calc.set_register("y", std::move(y));
  calc.set_register("z", "5.75");
  calc.set_register("t", "6.125");
  for (int index = 0; index <= 14; ++index) {
    const std::string name =
        index < 10 ? std::to_string(index) : std::string(1, static_cast<char>('a' + index - 10));
    calc.set_register(name, std::to_string(20 + index) + ".125");
  }
  calc.set_register("x1", std::move(x1));
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(300, 5);
  require(run.stopped, "physical X1 premise probe should reach its terminal stop");

  PhysicalObservation result;
  const std::array<std::string, 5> stack_names{"x1", "x", "y", "z", "t"};
  for (std::size_t index = 0; index < stack_names.size(); ++index)
    result.stack.at(index) = compact_number(calc.read_register(stack_names.at(index)));
  for (int index = 0; index <= 14; ++index) {
    const std::string name =
        index < 10 ? std::to_string(index) : std::string(1, static_cast<char>('a' + index - 10));
    result.memory.at(static_cast<std::size_t>(index)) = compact_number(calc.read_register(name));
  }
  result.display = compact_number(calc.display_text(true));
  result.program_counter = compact_number(calc.program_counter());
  result.stopped = run.stopped;
  return result;
}

PhysicalObservation run_physical_x1_probe(int opcode, std::string x1, std::string x = "3.25",
                                          std::string y = "4.5") {
  return run_physical_x1_probe(std::vector<int>{opcode}, std::move(x1), std::move(x), std::move(y));
}

bool differs_only_in_physical_x1(const PhysicalObservation& left,
                                 const PhysicalObservation& right) {
  return left.stack.at(0) != right.stack.at(0) &&
         std::equal(std::next(left.stack.begin()), left.stack.end(),
                    std::next(right.stack.begin())) &&
         left.memory == right.memory && left.display == right.display &&
         left.program_counter == right.program_counter && left.stopped == right.stopped;
}

std::string stack_text(const PhysicalObservation& observation) {
  std::string result;
  for (const std::string& value : observation.stack)
    result += (result.empty() ? "" : "/") + value;
  return result;
}

int address_of_item(const std::vector<MachineItem>& items, std::size_t item_index) {
  int address = 0;
  for (std::size_t index = 0; index < item_index; ++index) {
    if (items.at(index).kind != MachineItemKind::Label)
      ++address;
  }
  return address;
}

IrOp ir_label(std::string name) {
  IrOp result;
  result.kind = IrKind::Label;
  result.name = std::move(name);
  return result;
}

IrOp ir_call(std::string target, std::vector<std::uint64_t> origins) {
  IrOp result;
  result.kind = IrKind::Call;
  result.opcode = 0x53;
  result.target = std::move(target);
  result.meta.semantic_call_origins = std::move(origins);
  return result;
}

IrOp ir_plain(int opcode) {
  IrOp result;
  result.kind = IrKind::Plain;
  result.opcode = opcode;
  return result;
}

IrOp ir_return() {
  IrOp result;
  result.kind = IrKind::Return;
  result.opcode = 0x52;
  return result;
}

IrOp ir_stop() {
  IrOp result;
  result.kind = IrKind::Stop;
  result.opcode = 0x50;
  return result;
}

struct AliasFixture {
  std::vector<MachineItem> items;
  std::size_t call_item = 0;
  std::string source;
  std::string target;
};

struct NumericIndirectFixture {
  AliasFixture alias;
  std::size_t indirect_item = 0;
  std::size_t source_first_cell = 0;
  std::size_t retained_target = 0;
};

struct AddressTargetFixture {
  AliasFixture alias;
  std::size_t target_address_item = 0;
  std::string fallthrough_label;
  std::string return_label;
};

NumericIndirectFixture numeric_indirect_fixture() {
  NumericIndirectFixture fixture;
  fixture.alias.source = "opaque_saffron_31";
  fixture.alias.target = "opaque_indigo_73";

  auto& items = fixture.alias.items;
  items.push_back(MachineItem::label("opaque_main_19"));
  fixture.alias.call_item = items.size();
  items.push_back(op(0x53));
  items.push_back(MachineItem::address(fixture.alias.source));
  items.push_back(op(0x60));
  fixture.indirect_item = items.size();
  items.push_back(op(0x80));
  items.push_back(terminal_stop());

  items.push_back(procedure_boundary(fixture.alias.source, "start"));
  fixture.source_first_cell = items.size();
  items.push_back(op(0x01));
  items.push_back(op(0x10));
  items.push_back(op(0x52));
  items.push_back(procedure_boundary("opaque_saffron_end_32", "end", fixture.alias.source));

  items.push_back(procedure_boundary(fixture.alias.target, "start"));
  items.push_back(op(0x01));
  items.push_back(op(0x10));
  items.push_back(op(0x52));
  items.push_back(procedure_boundary("opaque_indigo_end_74", "end", fixture.alias.target));

  items.push_back(MachineItem::label("opaque_retained_sink_88"));
  fixture.retained_target = items.size();
  items.push_back(terminal_stop());
  items.at(fixture.indirect_item).indirect_flow_targets =
      std::vector<IrTarget>{address_of_item(items, fixture.retained_target)};
  return fixture;
}

AliasFixture alias_fixture(std::vector<int> continuation_opcodes, bool distinct_target_body = false,
                           bool alternate_entry = false, bool source_side_effect = false,
                           bool target_side_effect = false) {
  AliasFixture fixture;
  fixture.source = "opaque_amber_17";
  fixture.target = "opaque_cobalt_91";

  fixture.items.push_back(MachineItem::label("opaque_main_23"));
  fixture.call_item = fixture.items.size();
  fixture.items.push_back(op(0x53));
  fixture.items.push_back(MachineItem::address(fixture.source));
  for (const int opcode : continuation_opcodes)
    fixture.items.push_back(op(opcode));
  fixture.items.push_back(terminal_stop());

  if (alternate_entry) {
    fixture.items.push_back(MachineItem::label("opaque_dormant_42"));
    fixture.items.push_back(op(0x51));
    fixture.items.push_back(MachineItem::address("opaque_source_inner_64"));
    fixture.items.push_back(terminal_stop());
  }

  fixture.items.push_back(procedure_boundary(fixture.source, "start"));
  fixture.items.push_back(op(0x01));
  if (alternate_entry)
    fixture.items.push_back(MachineItem::label("opaque_source_inner_64"));
  if (source_side_effect)
    fixture.items.push_back(op(0x40));
  fixture.items.push_back(op(0x10));
  fixture.items.push_back(op(0x52));
  fixture.items.push_back(procedure_boundary("opaque_amber_end_18", "end", fixture.source));

  fixture.items.push_back(procedure_boundary(fixture.target, "start"));
  if (target_side_effect)
    fixture.items.push_back(op(0x40));
  fixture.items.push_back(op(distinct_target_body ? 0x02 : 0x01));
  fixture.items.push_back(op(0x10));
  if (distinct_target_body) {
    fixture.items.push_back(op(0x01));
    fixture.items.push_back(op(0x11));
  }
  fixture.items.push_back(op(0x52));
  fixture.items.push_back(procedure_boundary("opaque_cobalt_end_92", "end", fixture.target));
  return fixture;
}

AliasFixture alias_fixture(int continuation_opcode, bool distinct_target_body = false,
                           bool alternate_entry = false, bool source_side_effect = false,
                           bool target_side_effect = false) {
  return alias_fixture(std::vector<int>{continuation_opcode}, distinct_target_body, alternate_entry,
                       source_side_effect, target_side_effect);
}

AliasFixture indirect_alias_fixture(int register_index) {
  AliasFixture fixture = alias_fixture(0x0d);
  require(register_index >= 0 && register_index <= 14,
          "indirect alias fixture requires an MK-61 register index");
  fixture.items.at(fixture.call_item) = op(0xa0 + register_index);
  fixture.items.at(fixture.call_item).indirect_flow_targets = std::vector<IrTarget>{fixture.source};
  fixture.items.erase(fixture.items.begin() + static_cast<std::ptrdiff_t>(fixture.call_item + 1U));
  return fixture;
}

AddressTargetFixture address_target_fixture() {
  AddressTargetFixture fixture;
  fixture.alias.source = "opaque_marigold_27";
  fixture.alias.target = "opaque_ultramarine_83";
  fixture.fallthrough_label = "opaque_ultramarine_add_84";
  fixture.return_label = "opaque_ultramarine_return_85";

  auto& items = fixture.alias.items;
  items.push_back(MachineItem::label("opaque_main_26"));
  fixture.alias.call_item = items.size();
  items.push_back(op(0x53));
  items.push_back(MachineItem::address(fixture.alias.source));
  items.push_back(op(0x0d));
  items.push_back(terminal_stop());

  items.push_back(procedure_boundary(fixture.alias.source, "start"));
  items.push_back(op(0x01));
  items.push_back(op(0x10));
  items.push_back(op(0x52));
  items.push_back(procedure_boundary("opaque_marigold_end_28", "end", fixture.alias.source));

  items.push_back(procedure_boundary(fixture.alias.target, "start"));
  items.push_back(op(0x57));
  fixture.target_address_item = items.size();
  items.push_back(MachineItem::address(fixture.fallthrough_label));
  items.push_back(MachineItem::label(fixture.fallthrough_label));
  items.push_back(op(0x01));
  items.push_back(op(0x10));
  items.push_back(MachineItem::label(fixture.return_label));
  items.push_back(op(0x52));
  items.push_back(procedure_boundary("opaque_ultramarine_end_86", "end", fixture.alias.target));
  return fixture;
}

AliasFixture nested_x1_kill_fixture() {
  AliasFixture fixture = alias_fixture(std::vector<int>{0x60}, true);
  const auto stop =
      std::find_if(fixture.items.begin() + static_cast<std::ptrdiff_t>(fixture.call_item),
                   fixture.items.end(), [](const MachineItem& item) {
                     return item.kind == MachineItemKind::Op &&
                            item.stop_disposition == StopDisposition::Terminal;
                   });
  require(stop != fixture.items.end(), "nested X1 fixture should contain main stop");
  const std::size_t stop_index =
      static_cast<std::size_t>(std::distance(fixture.items.begin(), stop));
  fixture.items.insert(fixture.items.begin() + static_cast<std::ptrdiff_t>(stop_index),
                       MachineItem::address("opaque_x1_kill_55"));
  fixture.items.insert(fixture.items.begin() + static_cast<std::ptrdiff_t>(stop_index), op(0x53));
  fixture.items.push_back(procedure_boundary("opaque_x1_kill_55", "start"));
  fixture.items.push_back(op(0x15));
  fixture.items.push_back(op(0x52));
  fixture.items.push_back(procedure_boundary("opaque_x1_kill_end_56", "end", "opaque_x1_kill_55"));
  return fixture;
}

AliasFixture signed_zero_fixture() {
  AliasFixture fixture;
  fixture.source = "opaque_cerulean_41";
  fixture.target = "opaque_vermilion_67";

  fixture.items.push_back(MachineItem::label("opaque_main_37"));
  fixture.call_item = fixture.items.size();
  fixture.items.push_back(op(0x53));
  fixture.items.push_back(MachineItem::address(fixture.source));
  // The continuation starts with an X1 reconvergence shape. The zero-output
  // gate rejects before relying on rational equality to establish its visible
  // state premise; the separate emulator probe below pins the representation
  // difference at the helper boundary.
  fixture.items.push_back(op(0x00));
  fixture.items.push_back(op(0x12));
  fixture.items.push_back(op(0x0c));
  fixture.items.push_back(op(0x03));
  fixture.items.push_back(terminal_stop());

  fixture.items.push_back(procedure_boundary(fixture.source, "start"));
  fixture.items.push_back(op(0x01));
  fixture.items.push_back(op(0x12));
  fixture.items.push_back(op(0x35));
  fixture.items.push_back(op(0x52));
  fixture.items.push_back(procedure_boundary("opaque_cerulean_end_42", "end", fixture.source));

  fixture.items.push_back(procedure_boundary(fixture.target, "start"));
  fixture.items.push_back(op(0x01));
  fixture.items.push_back(op(0x0b));
  fixture.items.push_back(op(0x12));
  fixture.items.push_back(op(0x35));
  fixture.items.push_back(op(0x52));
  fixture.items.push_back(procedure_boundary("opaque_vermilion_end_68", "end", fixture.target));
  return fixture;
}

core::HelperSemanticExprPtr signed_zero_semantic(std::int64_t multiplier) {
  return core::helper_semantic_unary(
      core::HelperSemanticOp::Fraction,
      core::helper_semantic_binary(core::HelperSemanticOp::Multiply, core::helper_semantic_input(),
                                   core::helper_semantic_integer(multiplier)));
}

core::HelperSemanticExprPtr plus_one_semantic() {
  return core::helper_semantic_binary(core::HelperSemanticOp::Add, core::helper_semantic_input(),
                                      core::helper_semantic_integer(1));
}

std::vector<core::HelperSemanticContract>
contracts_for(const AliasFixture& fixture, core::ExactIntegralDomain source_domain = {0, 3, true},
              core::ExactIntegralDomain target_domain = {0, 3, true},
              std::string source_x1_key = "same-machine-effect",
              std::string target_x1_key = "same-machine-effect") {
  core::HelperSemanticContract source;
  source.entry_label = fixture.source;
  source.expression = plus_one_semantic();
  source.admitted_input = source_domain;
  source.input_decimal_derivation_exact = true;
  source.input_zero_canonical_positive = true;
  source.all_source_entries_accounted = true;
  source.admitted_call_items = {fixture.call_item};
  source.procedure_boundary_id = fixture.source;
  source.decimal_execution_exact = true;
  source.hidden_x2_return_sync_proved = true;
  source.x1_effect_proved = true;
  source.x1_effect_key = std::move(source_x1_key);
  source.certified_body_key =
      core::helper_semantic_alias_body_key(fixture.items, fixture.source).value_or("");

  core::HelperSemanticContract target;
  target.entry_label = fixture.target;
  target.expression = plus_one_semantic();
  target.admitted_input = target_domain;
  target.input_decimal_derivation_exact = true;
  target.input_zero_canonical_positive = true;
  target.decimal_execution_exact = true;
  target.hidden_x2_return_sync_proved = true;
  target.x1_effect_proved = true;
  target.x1_effect_key = std::move(target_x1_key);
  target.certified_body_key =
      core::helper_semantic_alias_body_key(fixture.items, fixture.target).value_or("");
  return {std::move(source), std::move(target)};
}

void require_rejected_unchanged(const AliasFixture& fixture,
                                const std::vector<core::HelperSemanticContract>& contracts,
                                const std::string& context) {
  const core::HelperSemanticAliasResult result =
      core::optimize_helper_semantic_alias(fixture.items, contracts);
  require(result.applied == 0 && !result.proof.proved &&
              result.items.size() == fixture.items.size() &&
              cell_count(result.items) == cell_count(fixture.items),
          context + " should fail closed without changing the artifact");
}

} // namespace

void helper_semantic_alias_rewrites_opaque_equivalent_helpers() {
  const AliasFixture fixture = alias_fixture(0x0d);
  const core::HelperSemanticAliasResult result =
      core::optimize_helper_semantic_alias(fixture.items, contracts_for(fixture));

  require(result.applied == 1 && result.proof.proved && result.proof.rewrite_proved &&
              result.proof.semantic_equivalent && result.proof.machine_abi_equivalent &&
              result.proof.source_entries_complete && result.proof.pre_control_flow_proved &&
              result.proof.final_control_flow_proved && result.proof.external_entries_equivalent &&
              result.proof.indirect_memory_equivalent && result.proof.symbolic_relocation_proved &&
              result.proof.fixed_targets_equivalent && result.proof.final_artifact_proved,
          "opaque renamed helpers should alias only after every proof obligation succeeds");
  require(result.proof.calls.size() == 1U &&
              result.proof.calls.front().original_call_item == fixture.call_item &&
              result.proof.calls.front().return_stack_equivalent &&
              result.proof.calls.front().hidden_x2_equivalent &&
              result.proof.calls.front().x1_continuation_safe,
          "the complete opaque call set should retain per-call ABI proofs");
  require(!has_label(result.items, fixture.source) && has_label(result.items, fixture.target) &&
              cell_count(result.items) == cell_count(fixture.items) - 3,
          "only the redundant three-cell helper body should be erased");
  require(result.items.at(fixture.call_item + 1U).kind == MachineItemKind::Address &&
              std::get<std::string>(result.items.at(fixture.call_item + 1U).target) ==
                  fixture.target,
          "the direct call should be retargeted by opaque identity, not label spelling");
}

void helper_semantic_alias_rejects_stale_certified_bodies() {
  {
    AliasFixture drifted = alias_fixture(0x0d);
    auto contracts = contracts_for(drifted);
    const core::HelperSemanticAliasResult certified =
        core::optimize_helper_semantic_alias(drifted.items, contracts);
    require(certified.applied == 1 && certified.proof.proved,
            "the unchanged opcode-certified helper pair should remain admissible");

    auto missing_key = contracts;
    missing_key.back().certified_body_key.clear();
    require_rejected_unchanged(drifted, missing_key,
                               "helper contract without a structural body certificate");

    const auto source_entry =
        std::find_if(drifted.items.begin(), drifted.items.end(), [&](const MachineItem& item) {
          return item.kind == MachineItemKind::Label && item.name == drifted.source;
        });
    require(source_entry != drifted.items.end(), "opcode-drift fixture should contain its source");
    const auto source_opcode =
        std::find_if(std::next(source_entry), drifted.items.end(),
                     [](const MachineItem& item) { return item.kind == MachineItemKind::Op; });
    require(source_opcode != drifted.items.end(), "opcode-drift fixture should contain a body");
    source_opcode->opcode = 0x02;
    source_opcode->mnemonic = opcode_by_code(0x02).name;
    require_rejected_unchanged(drifted, contracts, "post-certificate helper opcode mutation");
  }

  {
    AddressTargetFixture fixture = address_target_fixture();
    const auto contracts = contracts_for(fixture.alias);
    require(!contracts.front().certified_body_key.empty() &&
                !contracts.back().certified_body_key.empty(),
            "every semantic contract should carry an immutable structural body key");
    const core::HelperSemanticAliasResult certified =
        core::optimize_helper_semantic_alias(fixture.alias.items, contracts);
    require(certified.applied == 1 && certified.proof.proved,
            "the unchanged address-certified helper pair should remain admissible");

    fixture.alias.items.at(fixture.target_address_item).target = fixture.return_label;
    const std::optional<std::string> drifted_key =
        core::helper_semantic_alias_body_key(fixture.alias.items, fixture.alias.target);
    require(drifted_key.has_value() && *drifted_key != contracts.back().certified_body_key,
            "the structural key must distinguish same-opcode bodies with different internal "
            "branch targets");
    require_rejected_unchanged(fixture.alias, contracts,
                               "post-certificate internal address-target mutation");
  }
}

void helper_semantic_alias_rebinds_numeric_indirect_targets_by_identity() {
  const NumericIndirectFixture fixture = numeric_indirect_fixture();
  const auto contracts = contracts_for(fixture.alias);

  {
    const core::HelperSemanticAliasResult rejected =
        core::optimize_helper_semantic_alias(fixture.alias.items, contracts);
    require(rejected.applied == 0 && !rejected.proof.proved &&
                rejected.items.size() == fixture.alias.items.size(),
            "numeric indirect-flow facts should remain disabled by default");
  }

  core::HelperSemanticAliasOptions options;
  options.identity_check_numeric_indirect_flow = true;
  const core::HelperSemanticAliasResult rebound =
      core::optimize_helper_semantic_alias(fixture.alias.items, contracts, options);
  require(rebound.applied == 1 && rebound.proof.proved && rebound.proof.fixed_targets_equivalent &&
              !rebound.proof.symbolic_relocation_proved,
          "an explicitly enabled numeric target should move only after identity proof");
  require(rebound.items.at(fixture.indirect_item).indirect_flow_targets.has_value() &&
              rebound.items.at(fixture.indirect_item).indirect_flow_targets->size() == 1U,
          "the retained indirect command should keep one exact target fact");
  const auto* rebound_address =
      std::get_if<int>(&rebound.items.at(fixture.indirect_item).indirect_flow_targets->front());
  require(rebound_address != nullptr &&
              *rebound_address == address_of_item(rebound.items, rebound.items.size() - 1U) &&
              *rebound_address == address_of_item(fixture.alias.items, fixture.retained_target) - 3,
          "the numeric fact should follow the retained target identity across helper deletion");

  {
    NumericIndirectFixture missing = numeric_indirect_fixture();
    missing.alias.items.at(missing.indirect_item).indirect_flow_targets =
        std::vector<IrTarget>{999};
    require_rejected_unchanged(missing.alias, contracts_for(missing.alias),
                               "out-of-range numeric indirect target");
    const core::HelperSemanticAliasResult explicitly_enabled = core::optimize_helper_semantic_alias(
        missing.alias.items, contracts_for(missing.alias), options);
    require(explicitly_enabled.applied == 0 && !explicitly_enabled.proof.proved &&
                explicitly_enabled.items.size() == missing.alias.items.size(),
            "identity checking must reject a numeric target with no original command identity");
  }

  {
    NumericIndirectFixture removed = numeric_indirect_fixture();
    removed.alias.items.at(removed.indirect_item).indirect_flow_targets =
        std::vector<IrTarget>{address_of_item(removed.alias.items, removed.source_first_cell)};
    const core::HelperSemanticAliasResult explicitly_enabled = core::optimize_helper_semantic_alias(
        removed.alias.items, contracts_for(removed.alias), options);
    require(explicitly_enabled.applied == 0 && !explicitly_enabled.proof.proved &&
                explicitly_enabled.items.size() == removed.alias.items.size(),
            "identity checking must reject a numeric target whose command would be removed");
  }
}

void helper_semantic_alias_rejects_mutating_indirect_call_selectors() {
  for (int register_index = 0; register_index <= 6; ++register_index) {
    const AliasFixture fixture = indirect_alias_fixture(register_index);
    require_rejected_unchanged(fixture, contracts_for(fixture),
                               "indirect helper call through mutating R" +
                                   std::to_string(register_index));
  }

  for (int register_index = 7; register_index <= 14; ++register_index) {
    const AliasFixture fixture = indirect_alias_fixture(register_index);
    const core::HelperSemanticAliasResult result =
        core::optimize_helper_semantic_alias(fixture.items, contracts_for(fixture));
    require(result.applied == 1 && result.proof.proved && result.proof.added_call_cells == 1 &&
                result.proof.calls.size() == 1U && result.proof.calls.front().was_indirect,
            "indirect helper call through stable R7..Re should remain admissible");
    require(result.items.at(fixture.call_item).kind == MachineItemKind::Op &&
                result.items.at(fixture.call_item).opcode == 0x53 &&
                result.items.at(fixture.call_item + 1U).kind == MachineItemKind::Address &&
                std::get<std::string>(result.items.at(fixture.call_item + 1U).target) ==
                    fixture.target &&
                !has_label(result.items, fixture.source) &&
                cell_count(result.items) == cell_count(fixture.items) - 2,
            "stable indirect helper call should be directized without changing a selector");
  }
}

void helper_semantic_alias_emulator_pins_x1_transfer_classes() {
  const PhysicalObservation preserved_left = run_physical_x1_probe(0x54, "1.25");
  const PhysicalObservation preserved_right = run_physical_x1_probe(0x54, "9.75");
  require(differs_only_in_physical_x1(preserved_left, preserved_right),
          "the emulator probe must begin with only physical X1 differing");

  for (int opcode = 0x00; opcode <= 0x09; ++opcode) {
    const PhysicalObservation left = run_physical_x1_probe(opcode, "1.25");
    const PhysicalObservation right = run_physical_x1_probe(opcode, "9.75");
    require(differs_only_in_physical_x1(left, right),
            "decimal digit " + std::to_string(opcode) +
                " must preserve the sole physical X1 difference");

    const PhysicalObservation exposed_left =
        run_physical_x1_probe(std::vector<int>{opcode, 0x0f}, "1.25");
    const PhysicalObservation exposed_right =
        run_physical_x1_probe(std::vector<int>{opcode, 0x0f}, "9.75");
    require(!(exposed_left == exposed_right),
            "F Вx after decimal digit " + std::to_string(opcode) +
                " must expose the preserved physical X1 difference");
  }

  for (int opcode = 0x60; opcode <= 0x6e; ++opcode) {
    const PhysicalObservation left = run_physical_x1_probe(opcode, "1.25");
    const PhysicalObservation right = run_physical_x1_probe(opcode, "9.75");
    require(differs_only_in_physical_x1(left, right),
            "direct recall " + std::to_string(opcode) +
                " must preserve, not reconverge, differing physical X1: " + stack_text(left) +
                " vs " + stack_text(right));
  }

  const PhysicalObservation prefix_left =
      run_physical_x1_probe(std::vector<int>{0x60, 0x01}, "1.25");
  const PhysicalObservation prefix_right =
      run_physical_x1_probe(std::vector<int>{0x60, 0x01}, "9.75");
  require(differs_only_in_physical_x1(prefix_left, prefix_right),
          "recall and decimal digit should preserve the sole physical X1 difference");

  for (int opcode = 0x10; opcode <= 0x13; ++opcode) {
    const PhysicalObservation left = run_physical_x1_probe(opcode, "1.25");
    const PhysicalObservation right = run_physical_x1_probe(opcode, "9.75");
    require(left == right, "binary opcode " + std::to_string(opcode) +
                               " should reconverge differing physical X1: " + stack_text(left) +
                               " vs " + stack_text(right));
  }

  const PhysicalObservation positive_equal_subtraction = run_physical_x1_probe(0x11, "5", "1", "1");
  require(positive_equal_subtraction.stack.at(1) == "0,",
          "subtracting equal strictly-positive operands should produce canonical +0");

  const std::vector<int> signed_zero_branch{0x59, 0x05, 0x01, 0x51, 0x06, 0x02};
  const PhysicalObservation positive_zero_branch =
      run_physical_x1_probe(signed_zero_branch, "5", "0");
  const PhysicalObservation negative_zero_branch =
      run_physical_x1_probe(signed_zero_branch, "5", "-0");
  require(positive_zero_branch.stack.at(1) == "1," && negative_zero_branch.stack.at(1) == "2,",
          "F x>=0 must keep +0 on the nonnegative path but send -0 down the negative path");

  const PhysicalObservation divide_zero_left = run_physical_x1_probe(0x13, "1.25", "0", "-4.5");
  const PhysicalObservation divide_zero_right = run_physical_x1_probe(0x13, "9.75", "0", "-4.5");
  require(divide_zero_left == divide_zero_right,
          "division by zero should enter the same error state after physical X1 reconvergence");

  const PhysicalObservation chain_left =
      run_physical_x1_probe(std::vector<int>{0x60, 0x01, 0x11}, "1.25");
  const PhysicalObservation chain_right =
      run_physical_x1_probe(std::vector<int>{0x60, 0x01, 0x11}, "9.75");
  require(chain_left == chain_right,
          "recall/digit/subtract should reconverge at the binary operation");

  const PhysicalObservation power_left = run_physical_x1_probe(0x15, "1.25");
  const PhysicalObservation power_right = run_physical_x1_probe(0x15, "9.75");
  require(power_left == power_right, "F 10^x should reconverge differing physical X1");

  const PhysicalObservation overflow_left = run_physical_x1_probe(0x15, "1.25", "100");
  const PhysicalObservation overflow_right = run_physical_x1_probe(0x15, "9.75", "100");
  require(overflow_left == overflow_right,
          "overflowing F 10^x should enter the same error state after X1 reconvergence");

  const PhysicalObservation call_return_left =
      run_physical_x1_probe(std::vector<int>{0x53, 0x04, 0x50, 0x54, 0x52}, "1.25");
  const PhysicalObservation call_return_right =
      run_physical_x1_probe(std::vector<int>{0x53, 0x04, 0x50, 0x54, 0x52}, "9.75");
  require(differs_only_in_physical_x1(call_return_left, call_return_right),
          "direct call and matching return should preserve the sole physical X1 difference");

  const PhysicalObservation called_kill_left =
      run_physical_x1_probe(std::vector<int>{0x53, 0x04, 0x50, 0x54, 0x15, 0x52}, "1.25");
  const PhysicalObservation called_kill_right =
      run_physical_x1_probe(std::vector<int>{0x53, 0x04, 0x50, 0x54, 0x15, 0x52}, "9.75");
  require(called_kill_left == called_kill_right,
          "a direct call should permit reconvergence inside the callee before return");

  const PhysicalObservation returned_observer_left =
      run_physical_x1_probe(std::vector<int>{0x53, 0x04, 0x0f, 0x50, 0x52}, "1.25");
  const PhysicalObservation returned_observer_right =
      run_physical_x1_probe(std::vector<int>{0x53, 0x04, 0x0f, 0x50, 0x52}, "9.75");
  require(!(returned_observer_left == returned_observer_right),
          "F Вx after direct call/return should expose the preserved physical X1 difference");

  const std::vector<int> direct_call = {0x53, 0x06, 0x0f, 0x50, 0x00, 0x00, 0x01, 0x12, 0x52};
  const std::vector<int> call_through_jump = {0x53, 0x06, 0x0f, 0x50, 0x00, 0x00,
                                              0x51, 0x09, 0x00, 0x01, 0x12, 0x52};
  for (const std::string& x1 : {std::string("1.25"), std::string("9.75")}) {
    for (const std::string& x : {std::string("3.25"), std::string("0"), std::string("-0")}) {
      const PhysicalObservation direct = run_physical_x1_probe(direct_call, x1, x);
      const PhysicalObservation forwarded = run_physical_x1_probe(call_through_jump, x1, x);
      require(direct == forwarded,
              "direct call and call-through-unconditional-jump must have identical physical "
              "stack, X1, signed-zero, memory, display, and continuation state");
    }
  }

  const PhysicalObservation observer_left =
      run_physical_x1_probe(std::vector<int>{0x60, 0x0f}, "1.25");
  const PhysicalObservation observer_right =
      run_physical_x1_probe(std::vector<int>{0x60, 0x0f}, "9.75");
  require(!(observer_left == observer_right),
          "F Вx should expose the still-differing X1 and remain a proof barrier");
}

void helper_semantic_alias_rejects_unproved_decimal_or_domain_claims() {
  const core::ExactIntegralDomain small_domain{-4, 8, true};
  const auto modulo_three =
      core::helper_semantic_one_based_modulo(core::helper_semantic_input(), 3);
  const auto modulo_four = core::helper_semantic_one_based_modulo(core::helper_semantic_input(), 4);
  const auto modulo_five = core::helper_semantic_one_based_modulo(core::helper_semantic_input(), 5);
  require(!core::helper_semantic_decimal_execution_exact(modulo_three, small_domain) &&
              core::helper_semantic_decimal_execution_exact(modulo_four, small_domain) &&
              core::helper_semantic_decimal_execution_exact(modulo_five, small_domain),
          "decimal certification should reject repeating /3 but retain exact /4 and /5 domains");

  const AliasFixture fixture = alias_fixture(0x0d);
  {
    auto contracts = contracts_for(fixture);
    contracts.front().decimal_execution_exact = false;
    require_rejected_unchanged(fixture, contracts, "uncertified decimal execution");
  }
  {
    auto contracts = contracts_for(fixture);
    contracts.front().input_decimal_derivation_exact = false;
    require_rejected_unchanged(fixture, contracts, "uncertified decimal input derivation");
  }
  {
    auto contracts = contracts_for(fixture);
    contracts.front().input_zero_canonical_positive = false;
    require_rejected_unchanged(fixture, contracts, "numeric zero without canonical +0 proof");
  }
  {
    auto contracts = contracts_for(fixture, core::ExactIntegralDomain{0, 3, false},
                                   core::ExactIntegralDomain{0, 3, true});
    require_rejected_unchanged(fixture, contracts, "unknown source domain");
  }
  {
    auto contracts = contracts_for(fixture, core::ExactIntegralDomain{-4, 8, true},
                                   core::ExactIntegralDomain{-3, 8, true});
    require_rejected_unchanged(fixture, contracts, "target domain that misses one input");
  }
  {
    const AliasFixture signed_zero = signed_zero_fixture();
    core::HelperSemanticContract source;
    source.entry_label = signed_zero.source;
    source.expression = signed_zero_semantic(1);
    source.admitted_input = {-1, -1, true};
    source.input_decimal_derivation_exact = true;
    source.input_zero_canonical_positive = true;
    source.all_source_entries_accounted = true;
    source.admitted_call_items = {signed_zero.call_item};
    source.procedure_boundary_id = signed_zero.source;
    source.decimal_execution_exact = true;
    source.hidden_x2_return_sync_proved = true;
    source.x1_effect_proved = true;
    source.x1_effect_key = "negative-zero-machine-effect";
    source.certified_body_key =
        core::helper_semantic_alias_body_key(signed_zero.items, signed_zero.source).value_or("");

    core::HelperSemanticContract target;
    target.entry_label = signed_zero.target;
    target.expression = signed_zero_semantic(-1);
    target.admitted_input = {-1, -1, true};
    target.input_decimal_derivation_exact = true;
    target.input_zero_canonical_positive = true;
    target.decimal_execution_exact = true;
    target.hidden_x2_return_sync_proved = true;
    target.x1_effect_proved = true;
    target.x1_effect_key = "positive-zero-machine-effect";
    target.certified_body_key =
        core::helper_semantic_alias_body_key(signed_zero.items, signed_zero.target).value_or("");

    require_rejected_unchanged(signed_zero, {source, target},
                               "rationally equal but representation-distinct signed zero");

    const PhysicalObservation negative_zero = run_physical_x1_probe(
        {0x53, 0x06, 0x0c, 0x03, 0x50, 0x54, 0x01, 0x12, 0x35, 0x52}, "5", "-1");
    const PhysicalObservation positive_zero = run_physical_x1_probe(
        {0x53, 0x06, 0x0c, 0x03, 0x50, 0x54, 0x01, 0x0b, 0x12, 0x35, 0x52}, "5", "-1");
    require(!(negative_zero == positive_zero),
            "VP should expose the signed-zero distinction hidden by rational equality: " +
                stack_text(negative_zero) + " display=" + negative_zero.display + " vs " +
                stack_text(positive_zero) + " display=" + positive_zero.display);
  }
}

void helper_semantic_alias_rejects_extra_entries_and_side_effects() {
  {
    const AliasFixture alternate = alias_fixture(0x0d, false, true);
    require_rejected_unchanged(alternate, contracts_for(alternate), "alternate interior entry");
  }
  {
    const AliasFixture source_store = alias_fixture(0x0d, false, false, true);
    require_rejected_unchanged(source_store, contracts_for(source_store),
                               "source register side effect");
  }
  {
    const AliasFixture width_five_shape = alias_fixture(0x0d, false, false, false, true);
    auto contracts = contracts_for(width_five_shape);
    const auto modulo_five =
        core::helper_semantic_one_based_modulo(core::helper_semantic_input(), 5);
    contracts.front().expression = modulo_five;
    contracts.back().expression = modulo_five;
    require_rejected_unchanged(width_five_shape, contracts,
                               "width-five scratch/register ABI side effect");
  }
}

void helper_semantic_alias_requires_x1_forgetting_continuations() {
  {
    const AliasFixture observer = alias_fixture(0x0a, true);
    require_rejected_unchanged(observer,
                               contracts_for(observer, {0, 3, true}, {0, 3, true},
                                             "source-machine-effect", "target-machine-effect"),
                               "unsupported decimal transfer before physical X1 reconvergence");
  }

  {
    const AliasFixture recall_only = alias_fixture(0x60, true);
    require_rejected_unchanged(recall_only,
                               contracts_for(recall_only, {0, 3, true}, {0, 3, true},
                                             "source-machine-effect", "target-machine-effect"),
                               "a direct register recall that preserves physical X1");
  }
  {
    const AliasFixture observed = alias_fixture(std::vector<int>{0x60, 0x0f}, true);
    require_rejected_unchanged(observed,
                               contracts_for(observed, {0, 3, true}, {0, 3, true},
                                             "source-machine-effect", "target-machine-effect"),
                               "an observer reached before physical X1 reconvergence");
  }
  {
    const AliasFixture killed = alias_fixture(std::vector<int>{0x60, 0x01, 0x11}, true);
    const core::HelperSemanticAliasResult result = core::optimize_helper_semantic_alias(
        killed.items, contracts_for(killed, {0, 3, true}, {0, 3, true}, "source-machine-effect",
                                    "target-machine-effect"));
    require(result.applied == 1 && result.proof.proved && result.proof.calls.size() == 1U &&
                result.proof.calls.front().x1_continuation_safe,
            "recall/digit/subtract should prove reconvergence at the binary operation");
  }
  {
    const AliasFixture killed = nested_x1_kill_fixture();
    const core::HelperSemanticAliasResult result = core::optimize_helper_semantic_alias(
        killed.items, contracts_for(killed, {0, 3, true}, {0, 3, true}, "source-machine-effect",
                                    "target-machine-effect"));
    require(result.applied == 1 && result.proof.proved && result.proof.calls.size() == 1U &&
                result.proof.calls.front().x1_continuation_safe,
            "the continuation walker should follow a direct call to an F 10^x kill");
  }
}

void helper_semantic_alias_call_origins_survive_return_suffix_merging() {
  const std::vector<IrOp> input{
      ir_label("opaque_first_body"),
      ir_call("opaque_leaf", {11}),
      ir_plain(0x31),
      ir_plain(0x32),
      ir_return(),
      ir_label("opaque_second_body"),
      ir_call("opaque_leaf", {29}),
      ir_plain(0x31),
      ir_plain(0x32),
      ir_return(),
      ir_label("opaque_unrelated_body"),
      ir_call("opaque_other_leaf", {97}),
      ir_stop(),
  };
  const core::passes::PassResult merged = core::passes::return_suffix_gadget(
      input, core::passes::PassContext{.options = CompileOptions{}});
  require(merged.applied > 0, "the synthetic duplicate return suffix should be merged");

  int leaf_calls = 0;
  int unrelated_calls = 0;
  for (const IrOp& item : merged.ops) {
    if (item.kind != IrKind::Call || !std::holds_alternative<std::string>(item.target))
      continue;
    const std::string& target = std::get<std::string>(item.target);
    if (target == "opaque_leaf") {
      ++leaf_calls;
      require(item.meta.semantic_call_origins == std::vector<std::uint64_t>({11, 29}),
              "the surviving physical helper edge should carry the exact origin union");
    } else if (target == "opaque_other_leaf") {
      ++unrelated_calls;
      require(item.meta.semantic_call_origins == std::vector<std::uint64_t>({97}),
              "an unrelated origin must not be mixed into the merged helper edge");
    }
  }
  require(leaf_calls == 1 && unrelated_calls == 1,
          "suffix merging should leave one unioned helper edge and one unrelated edge");
}

} // namespace mkpro::tests
