#include "mkpro/core/post_layout_control_flow.hpp"

#include "mkpro/emulator/mk61.hpp"

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


void formal_program_counter_contract() {
  using core::PostLayoutControlFlowOptions;
  using core::PostLayoutExecutionState;
  const auto contains = [](const core::AuthoritativePostLayoutControlFlow& facts,
                           int physical, int formal) {
    return std::any_of(facts.execution_states.begin(), facts.execution_states.end(),
                      [&](const PostLayoutExecutionState& state) {
                        return state.address == physical && state.formal_opcode == formal;
                      });
  };
  const std::vector<std::pair<int, std::vector<std::string>>> counters = {
      {0xa4, {"-4", "-5", "-6", "-7"}},
      {0xb1, {"L1", "L2", "L3", "L4"}},
      {0xf9, {" 9", "00", "01", "02"}},
      {0xfa, {" -", "01", "02", "03"}},
      {0xff, {"  ", "06", "07", "08"}},
      {0x9f, {"9 ", "-6", "-7", "-8"}},
      {0xac, {"-С", "L3", "L4", "L5"}},
      {0x1a, {"1-", "21", "22", "23"}},
  };
  for (const auto& [target, expected] : counters) {
    emulator::MK61 calc;
    std::vector<int> codes(105, 0x54);
    codes[90] = 0x51;
    codes[91] = target;
    require(calc.load_program(codes).diagnostics.empty(), "formal counter fact must load");
    calc.press_sequence({"БП", "9", "0"});
    for (const auto& pc : expected) {
      calc.press("ПП");
      require(calc.program_counter() == pc, "hardware formal counter sequence must match");
    }
  }
  for (const auto [before, after] : std::vector<std::pair<int, int>>{
           {0xa4, 0xa5}, {0xb1, 0xb2}, {0xf9, 0}, {0xfa, 1}, {0xff, 6},
           {0x9f, 0xa6}, {0xac, 0xb3}, {0x1a, 0x21}})
    require(formal_address_successor_opcode(before) == after,
            "shared counter transfer must retain branch discontinuities and decimal carry");

  {
    std::vector<MachineItem> image(105, op(0x54));
    image[7] = stop(StopDisposition::Terminal);
    PostLayoutControlFlowOptions options;
    options.main_entry = 104;
    const auto facts = core::build_post_layout_control_flow(image, options);
    require(facts.proved && facts.execution_states.size() == 16 &&
                contains(facts, 0, 0xa5) && contains(facts, 0, 0xb2),
            "A4 must enter both side branches without merging their physical-00 contexts");
  }
  {
    std::vector<MachineItem> parallel(106, op(0x54));
    parallel.back() = stop(StopDisposition::Terminal);
    PostLayoutControlFlowOptions options;
    options.main_entry = 104;
    const auto logical = core::build_post_layout_control_flow(parallel, options);
    require(logical.proved && logical.execution_states.size() == 2 &&
                logical.execution_states.back().address == 105 &&
                !logical.execution_states.front().formal_opcode.has_value() &&
                !logical.execution_states.back().formal_opcode.has_value(),
            "unplaced logical cell 105 must not alias physical 00");
    parallel[0] = stop(StopDisposition::Terminal);
    options.main_formal_opcode = 0xa4;
    const auto physical = core::build_post_layout_control_flow(parallel, options);
    require(physical.proved && contains(physical, 0, 0xa5),
            "explicit hardware cursors remain distinct from the parallel layout space");
    options.main_formal_opcode = 0xb1;
    require(!core::build_post_layout_control_flow(parallel, options).proved,
            "a formal main counter must match the typed physical entry");
  }

  for (const int entry : {0xb1, 0xf9, 0xfa, 0xa4}) {
    const int position = formal_address_info(entry).actual;
    for (const bool call : {false, true}) {
      std::vector<MachineItem> image(105, op(0x54));
      image[0] = op(0x08);
      image[1] = stop(StopDisposition::Terminal);
      image[2] = stop(StopDisposition::Terminal);
      if (entry == 0xfa) image[1] = op(0x08);
      image[8] = op(0x60);
      image[9] = call ? op(0x52) : stop(StopDisposition::Terminal);
      image[10] = op(0x61);
      image[11] = stop(StopDisposition::Terminal);
      image[90] = op(0x51);
      image[91] = MachineItem::address(position);
      image[91].formal_opcode = entry;
      image.at(static_cast<std::size_t>(position)) = op(call ? 0x53 : 0x51);
      if (position + 1 < 105) image.at(static_cast<std::size_t>(position + 1)) = MachineItem::address(10);
      PostLayoutControlFlowOptions options;
      options.main_entry = 90;
      const auto facts = core::build_post_layout_control_flow(image, options);
      const int operand_address = entry == 0xfa ? 1 : 0;
      const int return_address = entry == 0xfa ? 2 : 1;
      const int return_formal = entry == 0xb1 ? 0xb3 : entry == 0xa4 ? 0xa6 : return_address;
      require(facts.proved && contains(facts, 8, 8) &&
                  !contains(facts, 10, 0x10),
              "a split command must fetch its actual operand, not the physical neighbour");
      const auto branch = std::find_if(facts.execution_states.begin(), facts.execution_states.end(),
                                      [&](const auto& state) { return state.formal_opcode == entry; });
      require(branch != facts.execution_states.end() &&
                  branch->operand_item_index == static_cast<std::size_t>(operand_address),
              "the graph must publish the actual cross-boundary operand identity");
      if (call) {
        const auto leaf = std::find_if(facts.execution_states.begin(), facts.execution_states.end(),
                                      [](const auto& state) { return state.address == 8; });
        require(leaf != facts.execution_states.end() &&
                    leaf->return_stack == std::vector<int>{return_address} &&
                    leaf->formal_return_stack == std::vector<std::optional<int>>{return_formal} &&
                    contains(facts, return_address, return_formal),
                "a call must preserve the formal counter when it pushes and pops its return");

        emulator::MK61 calc;
        std::vector<int> codes;
        for (const auto& item : image) {
          if (item.kind == MachineItemKind::Op) codes.push_back(item.opcode);
          else if (item.formal_opcode.has_value()) codes.push_back(*item.formal_opcode);
          else codes.push_back(official_address_to_opcode(std::get<int>(item.target)));
        }
        require(calc.load_program(codes).diagnostics.empty(), "split-call fact must load");
        calc.set_register("0", "11");
        calc.set_register("1", "22");
        calc.press_sequence({"БП", "9", "0", "ПП", "ПП", "ПП", "ПП"});
        const std::string expected = entry == 0xb1 ? "L3" : entry == 0xa4 ? "-6"
                                           : entry == 0xfa ? "02" : "01";
        require(calc.program_counter() == expected &&
                    std::stod(calc.read_register("x")) == 11,
                "hardware must use the same split operand and exact saved return counter");
      }
    }
  }

  for (const int entry : {0xb1, 0xfa}) {
    std::vector<MachineItem> image(105, stop(StopDisposition::Terminal));
    const int position = formal_address_info(entry).actual;
    const int continuation = entry == 0xb1 ? 0 : 1;
    image.at(static_cast<std::size_t>(position)) = op(0x54);
    image.at(static_cast<std::size_t>(continuation)) = op(0x52);
    image[90] = op(0xa7);
    image[90].indirect_flow_targets = std::vector<IrTarget>{position};
    PostLayoutControlFlowOptions options;
    options.main_entry = 90;
    options.proved_indirect_formal_targets[90] = {entry};
    const auto facts = core::build_post_layout_control_flow(image, options);
    require(facts.proved && contains(facts, position, entry) && contains(facts, 91, 0x91),
            "an indirect formal entry must use its real continuation and caller frame");

    emulator::MK61 calc;
    std::vector<int> codes;
    for (const auto& item : image) codes.push_back(item.opcode);
    require(calc.load_program(codes).diagnostics.empty(), "indirect formal fixture must load");
    calc.set_register("7", format_formal_address_opcode(entry));
    calc.press_sequence({"БП", "9", "0", "ПП", "ПП", "ПП"});
    require(calc.program_counter() == "91", "indirect aliases must return to the actual caller");

    image.at(static_cast<std::size_t>(position)) = stop(StopDisposition::Resumable);
    const auto resume = core::build_post_layout_control_flow(image, options);
    require(resume.proved &&
                std::any_of(resume.external_entries.begin(), resume.external_entries.end(),
                            [&](const auto& external) {
                              return external.kind == core::ExternalEntryKind::ResumableStop &&
                                  external.entry.address == continuation &&
                                  external.formal_opcode == (entry == 0xb1 ? 0xb2 : 1) &&
                                  external.formal_return_stack ==
                                      std::vector<std::optional<int>>{0x91};
                            }),
            "manual continuation must keep its formal PC and saved return context");

    auto invalid = options;
    invalid.proved_indirect_formal_targets[90] = {0x90};
    require(!core::build_post_layout_control_flow(image, invalid).proved,
            "formal indirect metadata must cover exactly its typed physical destinations");
    invalid.proved_indirect_formal_targets[90] = {entry, entry};
    require(!core::build_post_layout_control_flow(image, invalid).proved,
            "duplicate encoded indirect facts must fail closed");
    invalid.proved_indirect_formal_targets[90] = {256};
    require(!core::build_post_layout_control_flow(image, invalid).proved,
            "out-of-byte formal indirect metadata must fail closed");
    invalid.proved_indirect_formal_targets.erase(90);
    invalid.proved_indirect_formal_targets[89] = {entry};
    require(!core::build_post_layout_control_flow(image, invalid).proved,
            "formal metadata on a non-flow instruction must fail closed");
  }
}

void formal_address_operand_ownership_contract() {
  using core::PostLayoutControlFlowOptions;
  for (const auto model : {AddressSpaceModel::Standard, AddressSpaceModel::Mk61SMiniExpanded}) {
    const int limit = official_program_step_limit(model);
    const auto count = static_cast<std::size_t>(limit);
    const int caller = official_address_to_opcode(limit - 1, model);
    const int returned = formal_address_successor_opcode(formal_address_successor_opcode(caller));
    std::vector<MachineItem> image(count, op(0x54));
    image.at(0) = MachineItem::address(std::string("leaf"));
    image.at(1) = stop(StopDisposition::Terminal);
    image.at(20) = op(0x52);
    image.back() = op(0x53);
    // Zero-width labels must not change the physical operand or return address.
    image.insert(image.begin() + 20, MachineItem::label("leaf"));
    PostLayoutControlFlowOptions options;
    options.address_space_model = model;
    options.main_entry = limit - 1;
    const auto facts = core::build_post_layout_control_flow(image, options);
    require(facts.proved && facts.execution_states.size() == 3U &&
                facts.execution_states.front().operand_item_index == 0U &&
                facts.execution_states.at(1).address == 20 &&
                facts.execution_states.at(1).return_stack == std::vector<int>{1} &&
                facts.execution_states.at(1).formal_return_stack ==
                    std::vector<std::optional<int>>{returned} &&
                facts.execution_states.back().formal_opcode == returned,
            "a boundary call must own its wrapped address operand in either memory profile");

    auto unknown = image;
    unknown.at(0).target = std::string("absent");
    const auto unresolved = core::build_post_layout_control_flow(unknown, options);
    require(!unresolved.proved && reason_contains(unresolved, "no resolved execution target"),
            "a fetched non-adjacent address word must still resolve to a real command");

    auto invalid = image;
    invalid.at(0).formal_opcode = 256;
    require(!core::build_post_layout_control_flow(invalid, options).proved,
            "a wrapped operand must retain encoded-address validation");

    auto orphaned = image;
    orphaned.at(5) = MachineItem::address(std::string("leaf"));
    const auto orphan = core::build_post_layout_control_flow(orphaned, options);
    require(!orphan.proved && reason_contains(orphan, "orphan address operand"),
            "a real wrapped operand must not excuse another unconsumed address word");

    auto opcode_word = image;
    opcode_word.at(0) = op(0x20);
    const auto encoded = core::build_post_layout_control_flow(opcode_word, options);
    require(encoded.proved && encoded.execution_states == facts.execution_states &&
                encoded.execution_edges == facts.execution_edges,
            "typed and opcode-encoded forms of the same fetched byte must have the same CFG");
  }

  std::vector<MachineItem> side(105, op(0x54));
  side.at(0) = MachineItem::address(20);
  side.at(1) = stop(StopDisposition::Terminal);
  side.at(6) = op(0x53);
  side.at(7) = MachineItem::address(30);
  side.at(20) = op(0x52);
  side.at(30) = stop(StopDisposition::Terminal);
  PostLayoutControlFlowOptions options;
  options.main_entry = 6;
  options.main_formal_opcode = 0xb1;
  const auto facts = core::build_post_layout_control_flow(side, options);
  require(facts.proved && facts.execution_states.front().operand_item_index == 0U &&
              facts.execution_states.at(1).address == 20 &&
              facts.execution_states.back().formal_opcode == 0xb3,
          "a side entry must account for its fetched typed operand away from the physical boundary");

  auto missing = side;
  missing.at(0).target = std::string("absent");
  require(!core::build_post_layout_control_flow(missing, options).proved,
          "side-entry operand ownership must not accept an unresolved target");
}

} // namespace

void post_layout_control_flow_matches_typed_contract() {
  formal_program_counter_contract();
  formal_address_operand_ownership_contract();
  {
    MachineItem error = op(0x29, "К ÷");
    error.stop_disposition = StopDisposition::Resumable;
    MachineItem padding = op(0x54, "К НОП");
    padding.roles.push_back(kResumableErrorPaddingRole);
    const std::vector<MachineItem> resumable_error = {
        error, padding, op(0x40, "X->П 0"),
        stop(StopDisposition::Terminal),
    };
    const auto facts =
        core::build_post_layout_control_flow(resumable_error);
    require(facts.proved &&
                has_entry(facts, 2, core::ExternalEntryKind::ResumableStop),
            "typed resumable ЕГГ0Г should admit PC+2 as its external continuation");
    require(std::none_of(
                facts.execution_states.begin(), facts.execution_states.end(),
                [](const core::PostLayoutExecutionState& state) {
                  return state.address == 1;
                }) &&
                std::any_of(
                    facts.execution_states.begin(), facts.execution_states.end(),
                    [](const core::PostLayoutExecutionState& state) {
                      return state.address == 2;
                    }),
            "typed resumable ЕГГ0Г must skip exactly its one physical padding cell");

    std::vector<MachineItem> unknown = resumable_error;
    unknown.front().stop_disposition = StopDisposition::Unknown;
    const auto rejected = core::build_post_layout_control_flow(unknown);
    require(!rejected.proved &&
                reason_contains(rejected, "unknown disposition"),
            "an untyped reachable error opcode must fail the exact CFG closed");
  }

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
    const std::vector<MachineItem> closed_call = {
        op(0x53, "ПП"), MachineItem::address(std::string("leaf")),
        stop(StopDisposition::Terminal), MachineItem::label("leaf"),
        op(0x52, "В/О"),
    };
    const auto ordinary = core::build_post_layout_control_flow(closed_call);
    require(ordinary.proved && ordinary.maximum_observed_return_depth == 1,
            "the ordinary helper return must retain its exact caller frame");
    for (const IrTarget& target : {IrTarget{1}, IrTarget{99},
                                  IrTarget{std::string("absent_policy_target")}}) {
      core::PostLayoutControlFlowOptions options;
      options.empty_return_target = target;
      const auto unused = core::build_post_layout_control_flow(closed_call, options);
      require(unused.proved && !unused.empty_return_target.has_value() &&
                  unused.execution_states == ordinary.execution_states &&
                  unused.execution_successors == ordinary.execution_successors &&
                  unused.external_entries == ordinary.external_entries,
              "an unused empty-return policy must not reject or enlarge the exact CFG");
    }

    core::PostLayoutControlFlowOptions options;
    options.empty_return_target = 1; // The address operand, not a command.
    auto empty_path = closed_call;
    empty_path.front() = op(0x51, "БП");
    const auto empty = core::build_post_layout_control_flow(empty_path, options);
    require(!empty.proved && reason_contains(empty, "typed empty-return target"),
            "a reachable empty return must still reject an operand as its destination");

    auto manual_resume = closed_call;
    manual_resume.at(2).stop_disposition = StopDisposition::Resumable;
    const auto resumed = core::build_post_layout_control_flow(manual_resume, options);
    require(!resumed.proved && reason_contains(resumed, "typed empty-return target"),
            "an empty return reached after manual resume must validate the same policy");

    const auto valid = core::build_post_layout_control_flow(
        {op(0x52, "В/О"), stop(StopDisposition::Terminal)}, options);
    require(valid.proved && valid.empty_return_target.has_value() &&
                valid.empty_return_target->address == 1 && valid.execution_states.size() == 2,
            "a reachable empty return must keep its real executable policy edge");
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
    std::vector<MachineItem> discarded = indirect;
    discarded.at(memory_recall).discarded_indirect_recall_value = true;
    discarded.at(memory_recall).indirect_memory_targets.reset();
    const auto facts = core::build_post_layout_control_flow(discarded);
    std::vector<int> all_registers;
    for (int reg = 0; reg <= 0x0e; ++reg)
      all_registers.push_back(reg);
    require(facts.proved &&
                facts.indirect_memory_targets.at(memory_recall) == all_registers,
            "discarded recall must retain all possible memory aliases");
    discarded.at(memory_recall).raw = true;
    require(!core::build_post_layout_control_flow(discarded).proved,
            "raw code must not acquire an implicit discarded-read contract");
    discarded.at(memory_recall).raw = false;
    discarded.at(memory_recall).indirect_memory_targets = std::vector<int>{};
    require(!core::build_post_layout_control_flow(discarded).proved,
            "an explicitly empty target claim remains invalid");
    discarded = indirect;
    discarded.at(memory_store).discarded_indirect_recall_value = true;
    discarded.at(memory_store).indirect_memory_targets.reset();
    require(!core::build_post_layout_control_flow(discarded).proved,
            "a memory write cannot masquerade as a discarded read");
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
