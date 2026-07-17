#include "mkpro/core/post_layout_control_flow.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

MachineItem op(int opcode, std::string mnemonic = {}) {
  return MachineItem::op(opcode, mnemonic.empty() ? std::to_string(opcode) : std::move(mnemonic));
}

MachineItem stop(StopDisposition disposition) {
  MachineItem item = op(0x50, "С/П");
  item.stop_disposition = disposition;
  return item;
}

MachineItem anchored(MachineItem item, int protocol, int phase, ManualInteractionAnchorKind kind) {
  item.manual_interaction = ManualInteractionAnchor{
      .protocol_id = protocol,
      .phase = phase,
      .kind = kind,
  };
  return item;
}

std::vector<int> return_addresses(const core::PostLayoutExternalEntryState& entry) {
  std::vector<int> addresses;
  for (const core::PostLayoutCommandIdentity& slot : entry.return_stack)
    addresses.push_back(slot.address);
  return addresses;
}

bool has_entry(const core::AuthoritativePostLayoutControlFlow& facts, int address,
               core::ExternalEntryKind kind, const std::vector<int>& returns = {}) {
  return std::any_of(facts.external_entries.begin(), facts.external_entries.end(),
                     [&](const core::PostLayoutExternalEntryState& entry) {
                       return entry.entry.address == address && entry.kind == kind &&
                              return_addresses(entry) == returns;
                     });
}

bool reason_contains(const core::AuthoritativePostLayoutControlFlow& facts,
                     const std::string& fragment) {
  return std::any_of(facts.reasons.begin(), facts.reasons.end(), [&](const std::string& reason) {
    return reason.find(fragment) != std::string::npos;
  });
}

std::vector<MachineItem> two_phase_protocol_program() {
  std::vector<MachineItem> items;
  items.push_back(MachineItem::label("entry_alias"));
  items.push_back(op(0x51, "БП"));
  items.push_back(MachineItem::address(std::string("prompt_alias")));
  items.push_back(MachineItem::label("prompt_alias"));
  items.push_back(
      anchored(stop(StopDisposition::Resumable), 17, -1, ManualInteractionAnchorKind::PromptStop));
  items.push_back(
      anchored(op(0x40, "X->П 0"), 17, 0, ManualInteractionAnchorKind::SingleStepCommand));
  items.push_back(
      anchored(op(0x41, "X->П 1"), 17, 1, ManualInteractionAnchorKind::ContinuousResume));
  items.push_back(stop(StopDisposition::Terminal));
  return items;
}

std::vector<MachineItem> typed_indirect_program(std::size_t& condition, std::size_t& memory_store,
                                                std::size_t& memory_recall, std::size_t& call,
                                                std::size_t& jump) {
  std::vector<MachineItem> items;
  condition = items.size();
  items.push_back(op(0x77, "К x!=0 7"));
  items.back().indirect_flow_targets = std::vector<IrTarget>{std::string("branch_alias")};
  items.back().comment = "ignored indirect-target=99";
  items.back().roles = {"ignored-semantic-role"};
  items.push_back(stop(StopDisposition::Terminal));
  items.push_back(MachineItem::label("branch_alias"));
  memory_store = items.size();
  items.push_back(op(0xb7, "К X->П 7"));
  items.back().indirect_memory_targets = std::vector<int>{1, 2};
  memory_recall = items.size();
  items.push_back(op(0xd8, "К П->X 8"));
  items.back().indirect_memory_targets = std::vector<int>{3, 4};
  call = items.size();
  items.push_back(op(0xa7, "К ПП 7"));
  items.back().indirect_flow_targets = std::vector<IrTarget>{std::string("callee_alias")};
  items.push_back(stop(StopDisposition::Terminal));
  items.push_back(MachineItem::label("callee_alias"));
  jump = items.size();
  items.push_back(op(0x87, "К БП 7"));
  items.back().indirect_flow_targets = std::vector<IrTarget>{7};
  items.push_back(MachineItem::label("return_alias"));
  items.push_back(op(0x52, "В/О"));
  return items;
}

} // namespace

void post_layout_control_flow_matches_typed_contract() {
  {
    std::vector<MachineItem> shifted_main = {
        op(0x52, "В/О"), MachineItem::label("opaque_entry"),
        stop(StopDisposition::Terminal)};
    core::PostLayoutControlFlowOptions options;
    options.main_entry = std::string("opaque_entry");
    const auto facts = core::build_post_layout_control_flow(shifted_main, options);
    require(facts.proved && has_entry(facts, 1, core::ExternalEntryKind::Main),
            "an exact typed main identity may differ from physical 00");

    const auto empty_return = core::build_post_layout_control_flow(shifted_main);
    require(!empty_return.proved && reason_contains(empty_return, "empty return stack"),
            "physical-00 return with no typed alternate main must fail closed");
  }

  {
    std::vector<MachineItem> cyclic;
    cyclic.push_back(op(0x52, "В/О"));                       // 00
    cyclic.push_back(MachineItem::label("typed_main"));
    cyclic.push_back(op(0x53, "ПП"));                       // 01
    cyclic.push_back(MachineItem::address(std::string("suffix_helper"))); // 02
    cyclic.push_back(stop(StopDisposition::Terminal));       // 03
    while (static_cast<int>(std::count_if(
               cyclic.begin(), cyclic.end(),
               [](const MachineItem& item) { return item.kind != MachineItemKind::Label; })) <
           103) {
      cyclic.push_back(stop(StopDisposition::Terminal));
    }
    cyclic.push_back(MachineItem::label("suffix_helper"));
    cyclic.push_back(op(0x0d, "Cx")); // A3
    cyclic.push_back(op(0x0d, "Cx")); // A4 -> 00 -> В/О
    core::PostLayoutControlFlowOptions options;
    options.main_entry = std::string("typed_main");
    const auto facts = core::build_post_layout_control_flow(cyclic, options);
    require(facts.proved && facts.maximum_observed_return_depth == 1,
            "a full 105-cell artifact must model the hardware A4-to-00 continuation exactly");
  }

  {
    const core::AuthoritativePostLayoutControlFlow facts =
        core::build_post_layout_control_flow(two_phase_protocol_program());
    require(facts.proved && facts.reasons.empty(),
            "generic two-phase manual protocol should produce authoritative facts");
    require(has_entry(facts, 0, core::ExternalEntryKind::Main) &&
                has_entry(facts, 3, core::ExternalEntryKind::ManualSingleStep) &&
                has_entry(facts, 4, core::ExternalEntryKind::ManualContinuous),
            "manual protocol should admit main, PP store, and continuous-resume entries");
    require(std::none_of(facts.external_entries.begin(), facts.external_entries.end(),
                         [](const core::PostLayoutExternalEntryState& entry) {
                           return entry.entry.address == 2;
                         }),
            "PromptStop itself must not become an ordinary external resume entry");
    require(
        std::any_of(facts.execution_states.begin(), facts.execution_states.end(),
                    [](const core::PostLayoutExecutionState& state) { return state.address == 3; }),
        "exact execution graph should include a manual single-step store before resume");
  }

  {
    std::vector<MachineItem> unreachable;
    unreachable.push_back(stop(StopDisposition::Terminal));
    unreachable.push_back(anchored(stop(StopDisposition::Resumable), 23, -1,
                                   ManualInteractionAnchorKind::PromptStop));
    unreachable.push_back(
        anchored(op(0x40, "X->П 0"), 23, 0, ManualInteractionAnchorKind::SingleStepCommand));
    unreachable.push_back(
        anchored(op(0x41, "X->П 1"), 23, 1, ManualInteractionAnchorKind::ContinuousResume));
    unreachable.push_back(stop(StopDisposition::Terminal));
    const auto facts = core::build_post_layout_control_flow(unreachable);
    require(facts.proved && facts.external_entries.size() == 1U &&
                facts.external_entries.front().kind == core::ExternalEntryKind::Main,
            "typed manual phases are admitted only after their prompt is reached from main");
  }

  {
    std::vector<MachineItem> items;
    items.push_back(op(0x53, "ПП"));
    items.push_back(MachineItem::address(std::string("sub_alias")));
    items.push_back(stop(StopDisposition::Terminal));
    items.push_back(MachineItem::label("sub_alias"));
    items.push_back(stop(StopDisposition::Resumable));
    items.push_back(op(0x52, "В/О"));
    const core::AuthoritativePostLayoutControlFlow facts =
        core::build_post_layout_control_flow(items);
    require(facts.proved && facts.maximum_observed_return_depth == 1,
            "direct call/resume/return should have an exact one-slot return stack");
    require(facts.execution_states.size() == 4U &&
                facts.execution_successors.size() == facts.execution_states.size() &&
                facts.explored_states == facts.execution_states.size(),
            "authoritative control flow should expose every exact return-stack state and edge");
    require(has_entry(facts, 0, core::ExternalEntryKind::Main) &&
                has_entry(facts, 4, core::ExternalEntryKind::ResumableStop, {2}),
            "resumable STOP in a subroutine should preserve its exact return continuation");
  }

  {
    std::vector<MachineItem> acyclic;
    acyclic.push_back(op(0xe9, "К x=0 9"));
    acyclic.back().indirect_flow_targets = std::vector<IrTarget>{std::string("branch_write")};
    acyclic.back().borrowed_entry_phase_selector = true;
    acyclic.push_back(op(0x49, "X->П 9"));
    acyclic.push_back(stop(StopDisposition::Terminal));
    acyclic.push_back(MachineItem::label("branch_write"));
    acyclic.push_back(op(0x49, "X->П 9"));
    acyclic.push_back(stop(StopDisposition::Terminal));
    const core::PostLayoutBorrowedSelectorProof safe =
        core::prove_post_layout_borrowed_entry_selectors(acyclic);
    require(safe.proved && safe.selector_registers == 1U && safe.selector_states > 0U,
            "entry-phase selector should be valid when every continuation overwrites it "
            "before ordinary use");

    std::vector<MachineItem> recurring;
    recurring.push_back(op(0xe9, "К x=0 9"));
    recurring.back().indirect_flow_targets = std::vector<IrTarget>{std::string("write")};
    recurring.back().borrowed_entry_phase_selector = true;
    recurring.push_back(MachineItem::label("write"));
    recurring.push_back(op(0x49, "X->П 9"));
    recurring.push_back(op(0x51, "БП"));
    recurring.push_back(MachineItem::address(0));
    const core::PostLayoutBorrowedSelectorProof unsafe =
        core::prove_post_layout_borrowed_entry_selectors(recurring);
    require(!unsafe.proved && std::any_of(unsafe.reasons.begin(), unsafe.reasons.end(),
                                          [](const std::string& reason) {
                                            return reason.find("reachable after") !=
                                                   std::string::npos;
                                          }),
            "entry-phase selector must be rejected when a later write loops back to it");
  }

  std::size_t condition = 0;
  std::size_t memory_store = 0;
  std::size_t memory_recall = 0;
  std::size_t call = 0;
  std::size_t jump = 0;
  const std::vector<MachineItem> indirect =
      typed_indirect_program(condition, memory_store, memory_recall, call, jump);
  {
    const core::AuthoritativePostLayoutControlFlow facts =
        core::build_post_layout_control_flow(indirect);
    require(facts.proved && facts.maximum_observed_return_depth == 1,
            "typed indirect condition/call/jump graph should prove exact flow");
    require(facts.indirect_flow_targets.at(condition).front().address == 2 &&
                facts.indirect_flow_targets.at(condition).front().labels ==
                    std::vector<std::string>{"branch_alias"} &&
                facts.indirect_flow_targets.at(call).front().address == 6 &&
                facts.indirect_flow_targets.at(jump).front().address == 7 &&
                facts.indirect_flow_targets.at(jump).front().labels ==
                    std::vector<std::string>{"return_alias"},
            "symbolic and numeric indirect facts should bind to exact command identities");
    require(facts.indirect_memory_targets.at(memory_store) == std::vector<int>({1, 2}) &&
                facts.indirect_memory_targets.at(memory_recall) == std::vector<int>({3, 4}),
            "typed indirect-memory facts should retain complete post-mutation register sets");
  }

  {
    std::vector<MachineItem> missing = indirect;
    missing.at(condition).indirect_flow_targets.reset();
    const auto facts = core::build_post_layout_control_flow(missing);
    require(!facts.proved && reason_contains(facts, "indirect-flow fact is missing"),
            "missing indirect-flow metadata must fail closed");
  }

  {
    std::vector<MachineItem> missing = indirect;
    missing.at(memory_store).indirect_memory_targets.reset();
    const auto facts = core::build_post_layout_control_flow(missing);
    require(!facts.proved && reason_contains(facts, "indirect-memory fact is missing"),
            "missing indirect-memory metadata must fail closed");
  }

  {
    std::vector<MachineItem> duplicate = indirect;
    duplicate.at(condition).indirect_flow_targets =
        std::vector<IrTarget>{std::string("branch_alias"), 2};
    const auto facts = core::build_post_layout_control_flow(duplicate);
    require(!facts.proved && reason_contains(facts, "duplicate command identities"),
            "alias and numeric spellings of one indirect target must not duplicate identity");
  }

  {
    std::vector<MachineItem> unknown = indirect;
    unknown.at(1).stop_disposition = StopDisposition::Unknown;
    const auto facts = core::build_post_layout_control_flow(unknown);
    require(!facts.proved && reason_contains(facts, "unknown disposition"),
            "an Unknown STOP must fail even when another branch could avoid it");
  }

  {
    std::vector<MachineItem> malformed = two_phase_protocol_program();
    malformed.at(5).manual_interaction->kind = ManualInteractionAnchorKind::ContinuousResume;
    const auto facts = core::build_post_layout_control_flow(malformed);
    require(!facts.proved && reason_contains(facts, "non-single-step intermediate phase"),
            "manual protocol with an early continuous phase must fail closed");
  }

  {
    std::vector<MachineItem> recursive;
    recursive.push_back(op(0xa7, "К ПП 7"));
    recursive.back().indirect_flow_targets = std::vector<IrTarget>{0};
    recursive.push_back(stop(StopDisposition::Terminal));
    const auto facts = core::build_post_layout_control_flow(recursive);
    require(!facts.proved && reason_contains(facts, "return-stack depth"),
            "sixth nested call must fail the five-level return-stack proof");
  }

  {
    std::vector<MachineItem> branching;
    branching.push_back(op(0x77, "К x!=0 7"));
    branching.back().indirect_flow_targets = std::vector<IrTarget>{0};
    branching.push_back(stop(StopDisposition::Terminal));
    core::PostLayoutControlFlowOptions options;
    options.maximum_execution_states = 1;
    const auto facts = core::build_post_layout_control_flow(branching, options);
    require(!facts.proved && reason_contains(facts, "execution-state cap"),
            "execution-state cap must fail closed instead of truncating reachability");
  }

  {
    const auto facts = core::build_post_layout_control_flow({op(0x0d, "Cx")});
    require(!facts.proved && reason_contains(facts, "missing executable successor"),
            "reachable fall-off must fail closed");
  }
}

} // namespace mkpro::tests

#ifdef MKPRO_STANDALONE_POST_LAYOUT_CONTROL_FLOW_TEST
namespace mkpro::tests {
void ir_round_trip_matches_typescript_contract();
}

int main() {
  try {
    mkpro::tests::ir_round_trip_matches_typescript_contract();
    mkpro::tests::post_layout_control_flow_matches_typed_contract();
    std::cout << "[PASS] ir_round_trip_matches_typescript_contract\n"
                 "[PASS] post_layout_control_flow_matches_typed_contract\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "[FAIL] post_layout_control_flow_matches_typed_contract: " << error.what() << '\n';
    return 1;
  }
}
#endif
