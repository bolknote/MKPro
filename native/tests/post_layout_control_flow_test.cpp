#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/indirect_read_observability.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/tail_call.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"
#include "mkpro/core/post_layout_indirect_flow.hpp"

#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <iostream>
#include <map>
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
  {
    const std::vector<MachineItem> bytes = {
        op(0x53), op(0x04), stop(StopDisposition::Terminal), op(0x54), op(0x52),
    };
    require(!core::build_post_layout_control_flow(bytes).proved,
            "ordinary typed IR must not silently treat an opcode as an address word");
    PostLayoutControlFlowOptions image;
    image.opcode_address_words = {1};
    const auto facts = core::build_post_layout_control_flow(bytes, image);
    require(facts.proved && facts.execution_states.front().operand_item_index == 1U &&
                facts.execution_states.back().address == 2,
            "an explicitly encoded operand must call and return through its real byte");
    image.opcode_address_words = {1, 1};
    require(!core::build_post_layout_control_flow(bytes, image).proved,
            "duplicate encoded operand declarations must fail closed");
    image.opcode_address_words = {0, 1};
    require(!core::build_post_layout_control_flow(bytes, image).proved,
            "an encoded word with no operand owner must fail closed");
  }
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


void indirect_conversion_counter_transport_contract() {
  const auto fixture = [](bool explicit_return) {
    std::vector<MachineItem> items;
    for (int address = 0; address < 50; ++address) {
      if (address == 39)
        items.push_back(MachineItem::label("suffix"));
      if (address == 0)
        items.push_back(op(0x52));
      else if (address == 1)
        items.push_back(op(0x53));
      else if (address == 2) {
        auto operand = MachineItem::address(std::string("suffix"));
        operand.formal_opcode = 0xf1;
        items.push_back(std::move(operand));
      } else if (address >= 39 && address <= 47)
        items.push_back(op(address == 47 && explicit_return ? 0x52 : 0x54));
      else
        items.push_back(stop(StopDisposition::Terminal));
    }
    return items;
  };
  const auto opcodes = [](const std::vector<MachineItem>& items) {
    std::map<std::string, int> labels;
    int address = 0;
    for (const auto& item : items) {
      if (item.kind == MachineItemKind::Label)
        labels.emplace(item.name, address);
      else
        ++address;
    }
    std::vector<int> codes;
    for (const auto& item : items) {
      if (item.kind == MachineItemKind::Op)
        codes.push_back(item.opcode);
      else if (item.kind == MachineItemKind::Address) {
        if (item.formal_opcode.has_value())
          codes.push_back(*item.formal_opcode);
        else {
          const auto* label = std::get_if<std::string>(&item.target);
          codes.push_back(official_address_to_opcode(
              label ? labels.at(*label) : std::get<int>(item.target)));
        }
      }
    }
    return codes;
  };
  const auto observe = [&](const std::vector<MachineItem>& items,
                           const std::vector<PreloadReport>& preloads,
                           const std::string& expected_pc) {
    emulator::MK61 calc;
    require(calc.load_program(opcodes(items)).diagnostics.empty(),
            "counter-transport ROM fixture must load");
    calc.set_register("x", "11").set_register("y", "13")
        .set_register("z", "17").set_register("t", "19").set_register("x1", "23");
    for (const auto& preload : preloads)
      calc.set_register(preload.register_name, preload.value);
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(1000, 6).stopped &&
                calc.program_counter() == expected_pc,
            "side-space helper must return to its own caller, not fall through");
    std::vector<std::string> values;
    for (const auto* name : {"x", "y", "z", "t", "x1"})
      values.push_back(calc.read_register(name));
    calc.press(".");
    values.push_back(calc.read_register("x"));
    return values;
  };

  for (const auto model : {AddressSpaceModel::Standard, AddressSpaceModel::Mk61SMiniExpanded}) {
    for (const bool explicit_return : {false, true}) {
      const auto before = fixture(explicit_return);
      auto after = before;
      after.at(1) = op(0xa7);
      after.at(1).indirect_flow_targets = std::vector<IrTarget>{std::string("suffix")};
      after.erase(after.begin() + 2);
      std::vector<std::optional<std::size_t>> mapping(before.size());
      for (std::size_t i = 0; i < before.size(); ++i)
        if (i != 2U)
          mapping.at(i) = i < 2U ? i : i - 1U;
      core::PostLayoutControlFlowOptions control_options;
      control_options.address_space_model = model;
      control_options.empty_return_target = 1;
      const auto old_flow = core::build_post_layout_control_flow(before, control_options);
      const auto new_flow = core::build_post_layout_control_flow(after, control_options);
      require(old_flow.proved && new_flow.proved,
              "both counter variants must have complete CFGs, even when their behavior differs");
      core::PostLayoutExecutionRelocationOptions transport;
      transport.direct_to_indirect_flow_items = {1};
      const auto proof = core::prove_post_layout_execution_relocation(
          before, after, old_flow, new_flow, mapping, transport);
      require(proof.proved == explicit_return,
              "physical destination equality must not erase an implicit side-space return");
      require(!core::prove_post_layout_execution_relocation(
                  before, after, old_flow, new_flow, mapping).proved,
              "an opcode-family change needs an explicit conversion contract");
      transport.direct_to_indirect_flow_items.push_back(1);
      require(!core::prove_post_layout_execution_relocation(
                  before, after, old_flow, new_flow, mapping, transport).proved,
              "duplicate conversion declarations must fail closed");

      if (model != AddressSpaceModel::Standard)
        continue; // The bundled ROM is stock MK-61, not the 112-cell MK61S.
      const auto optimized = core::optimize_post_layout_indirect_flow(before, {}, 0);
      require((optimized.applied > 0) == explicit_return,
              "post-layout lowering must preserve boundary returns but still shorten safe calls");
      const auto baseline = observe(before, {}, "04");
      const auto actual = observe(optimized.items, optimized.preloads,
                                 explicit_return ? "03" : "04");
      require(baseline == actual,
              "safe selector transport must preserve stack, X1 and dot-observable X2");
    }
  }
}

void executable_operand_image_contract() {
  std::vector<MachineItem> items(53, op(0x54));
  items.at(0) = op(0xa7);
  items.at(0).indirect_flow_targets = std::vector<IrTarget>{3};
  items.at(1) = stop(StopDisposition::Terminal);
  items.at(2) = op(0x51);
  items.at(3) = MachineItem::address(52);
  items.at(3).roles = {"exec"};
  items.at(3).opcode = 0x29; // irrelevant: the actual encoded operand is 0x52
  items.at(52) = stop(StopDisposition::Terminal);
  const auto flow = core::build_post_layout_control_flow(items);
  require(flow.proved && flow.maximum_observed_return_depth == 1 &&
              flow.indirect_flow_targets.at(0).front().item_index == 3U,
          "an overlaid address-52 word must execute BO and retain the caller frame");
  const auto image = core::materialize_post_layout_byte_image(items);
  require(image.has_value() && image->items.at(3).opcode == 0x52 &&
              image->options.opcode_address_words == std::vector<std::size_t>{3} &&
              items.at(3).kind == MachineItemKind::Address,
          "byte decoding must preserve the published typed operand and item identities");
  const auto values = core::analyze_stable_register_value_flow(
      items, {{.register_name = "7", .value = "3"}}, flow);
  require(values.proved && values.before_item.at(1).at(0) == "3",
          "selector proof must follow the same executed operand bytes as the CFG");

  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "overlaid operand ROM fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "overlaid operand ROM fixture must load");
  calc.set_register("7", "3").set_register("x", "47.25");
  calc.press_sequence({"В/О", "С/П"});
  const auto stopped = calc.run_until_stable(600, 5);
  std::string actual_x = calc.read_register("x");
  std::replace(actual_x.begin(), actual_x.end(), ',', '.');
  require(stopped.stopped && std::stod(actual_x) == 47.25 &&
              calc.program_counter() == "02",
          "hardware must return from operand 0x52 without truncating X or executing an error");

  auto bad = items;
  bad.at(3).roles.clear();
  require(!core::build_post_layout_control_flow(bad).proved,
          "an unmarked address word is not an admitted executable entry");
  bad = items;
  bad.at(3).formal_opcode = 0x53;
  require(!core::build_post_layout_control_flow(bad).proved,
          "an overlaid formal byte must still name the operand's actual target");
  bad = items;
  bad.at(3).target = std::string("absent");
  require(!core::build_post_layout_control_flow(bad).proved,
          "an unresolved overlay label must fail closed");
  bad = items;
  bad.resize(106, op(0x54));
  require(!core::build_post_layout_control_flow(bad).proved &&
              core::build_post_layout_control_flow(
                  bad, {.address_space_model = AddressSpaceModel::Mk61SMiniExpanded}).proved,
          "physical overlay decoding must not alias a stock over-window logical image");

  std::vector<MachineItem> clobber(48, op(0x54));
  clobber.at(0) = op(0x60);
  clobber.at(1) = op(0x88);
  clobber.at(1).indirect_flow_targets = std::vector<IrTarget>{4};
  clobber.at(2) = stop(StopDisposition::Terminal);
  clobber.at(3) = op(0x51);
  clobber.at(4) = MachineItem::address(47);
  clobber.at(4).roles = {"exec"}; // actual opcode 0x47 stores unknown X into R7
  clobber.at(5) = op(0x87);
  clobber.at(5).indirect_flow_targets = std::vector<IrTarget>{47};
  clobber.at(47) = stop(StopDisposition::Terminal);
  const auto clobber_flow = core::build_post_layout_control_flow(clobber);
  const auto clobber_values = core::analyze_stable_register_value_flow(
      clobber, {{.register_name = "7", .value = "47"},
                {.register_name = "8", .value = "4"}}, clobber_flow);
  require(clobber_flow.proved && clobber_values.proved &&
              !clobber_values.before_item.at(5).at(0),
          "an executed operand store must invalidate its real selector destination");
}

void tail_return_ownership_contract() {
  const auto run = [](const std::vector<MachineItem>& items, const std::string& input) {
    const auto resolved = resolve_machine_items(items);
    require(resolved.diagnostics.empty(), "return ownership ROM fixture must resolve");
    std::vector<int> codes;
    for (const auto& step : resolved.steps)
      codes.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(), "return ownership fixture must load");
    calc.set_register("0", "47.25").set_register("x", input).set_register("y", "13")
        .set_register("z", "17").set_register("t", "19").set_register("x1", "23");
    calc.press_sequence({"В/О", "С/П"});
    const auto stopped = calc.run_until_stable(600, 5);
    std::string actual_x = calc.read_register("x");
    std::replace(actual_x.begin(), actual_x.end(), ',', '.');
    require(stopped.stopped && std::stod(actual_x) == 47.25,
            "every caller must reach its own continuation rather than enter a call loop");
    std::vector<std::string> values;
    for (const auto* reg : {"x", "y", "z", "t", "x1"})
      values.push_back(calc.read_register(reg));
    calc.press(".");
    values.push_back(calc.display_text());
    return values;
  };
  const CompileOptions options;
  const std::vector<MachineItem> mixed = {
      op(0x53), MachineItem::address("second"),
      op(0x51), MachineItem::address("again"),
      MachineItem::label("first_caller"),
      op(0x53), MachineItem::address("first"),
      op(0x60), stop(StopDisposition::Terminal),
      MachineItem::label("first"),
      op(0x51), MachineItem::address("shared"),
      MachineItem::label("second"), op(0x54),
      MachineItem::label("shared"), op(0x52),
      MachineItem::label("again"),
      op(0x51), MachineItem::address("first_caller"),
  };
  const auto untouched = core::passes::tail_call_lowering(
      raise_machine_to_ir(mixed), {.options = options});
  require(untouched.applied == 0,
          "linear placement must not hide another caller of a shared return");
  const auto unchanged = lower_ir_to_machine(untouched.ops);
  require(core::build_post_layout_control_flow(unchanged).proved &&
              run(mixed, "11") == run(unchanged, "11"),
          "mixed return owners must retain stack, X1, X2 and their separate continuations");

  const std::vector<MachineItem> shared = {
      op(0x5e), MachineItem::address("first_caller"),
      op(0x53), MachineItem::address("second"),
      op(0x51), MachineItem::address("done"),
      MachineItem::label("first_caller"),
      op(0x53), MachineItem::address("first"),
      op(0x51), MachineItem::address("done"),
      MachineItem::label("first"),
      op(0x51), MachineItem::address("shared"),
      MachineItem::label("second"), op(0x54),
      MachineItem::label("shared"), op(0x52),
      MachineItem::label("done"), op(0x60), stop(StopDisposition::Terminal),
  };
  const auto optimized = core::passes::tail_call_lowering(
      raise_machine_to_ir(shared), {.options = options});
  const auto shorter = lower_ir_to_machine(optimized.ops);
  require(optimized.applied == 3 &&
              core::machine_cell_count(shorter) < core::machine_cell_count(shared) &&
              core::build_post_layout_control_flow(shorter).proved,
          "all owners with one continuation may share a transactional return rewrite");
  for (const auto* input : {"0", "11"})
    require(run(shared, input) == run(shorter, input),
            "both shared-tail entries must preserve stack, X1 and dot-observable X2");

  // The outer return is claimed by continuation specialization. The nested
  // call must not simultaneously consume that return as an ordinary tail call.
  const std::vector<MachineItem> nested = {
      op(0x54), stop(StopDisposition::Resumable),
      op(0x62), op(0x5e), MachineItem::address("other"),
      op(0x53), MachineItem::address("outer"),
      op(0x51), MachineItem::address("done"),
      MachineItem::label("other"),
      op(0x53), MachineItem::address("inner"), op(0x60),
      stop(StopDisposition::Terminal),
      MachineItem::label("outer"), op(0x53), MachineItem::address("inner"), op(0x52),
      MachineItem::label("inner"), op(0x61), op(0x52),
      MachineItem::label("done"), op(0x60), stop(StopDisposition::Terminal),
  };
  const auto observe_nested = [&](const std::vector<MachineItem>& image,
                                   const std::string& branch) {
    const auto resolved = resolve_machine_items(image);
    require(resolved.diagnostics.empty(), "nested continuation fixture must resolve");
    std::vector<int> codes;
    for (const auto& step : resolved.steps)
      codes.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(),
            "nested continuation fixture must load");
    calc.set_register("0", "47.25").set_register("1", "13").set_register("2", branch)
        .set_register("x", "17").set_register("y", "19")
        .set_register("z", "23").set_register("t", "29").set_register("x1", "31");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(600, 5).stopped,
            "nested continuation fixture must reach its initial prompt");
    calc.press("С/П");
    require(calc.run_until_stable(600, 5).stopped,
            "nested continuation fixture must reach the caller's report");
    std::string x = calc.read_register("x");
    std::replace(x.begin(), x.end(), ',', '.');
    require(std::stod(x) == 47.25,
            "a nested return must not escape to the physical-01 startup prompt");
    std::vector<std::string> values;
    for (const auto* reg : {"x", "y", "z", "t", "x1"})
      values.push_back(calc.read_register(reg));
    calc.press(".");
    values.push_back(calc.display_text());
    return values;
  };
  for (const bool labelled_return : {false, true}) {
    auto input = nested;
    if (labelled_return) {
      const auto outer = std::find_if(input.begin(), input.end(), [](const MachineItem& item) {
        return item.kind == MachineItemKind::Label && item.name == "outer";
      });
      const auto returned = outer + 3;
      input.insert(returned, {op(0x51), MachineItem::address("outer_return"),
                              MachineItem::label("outer_return")});
    }
    const auto transaction = core::passes::tail_call_lowering(
        raise_machine_to_ir(input), {.options = options});
    const auto output = lower_ir_to_machine(transaction.ops);
    require(transaction.applied > 0 &&
                core::machine_cell_count(output) <= core::machine_cell_count(input) &&
                core::build_post_layout_control_flow(output, {.empty_return_target = 1}).proved,
            "continuation specialization must keep a coherent smaller call-frame graph");
    for (const auto* branch : {"0", "1"})
      require(observe_nested(input, branch) == observe_nested(output, branch),
              "direct and labelled planned returns must preserve both caller continuations");
  }

  auto raw = raise_machine_to_ir({op(0x53), MachineItem::address("leaf"), op(0x52),
                                  MachineItem::label("leaf"), op(0x52)});
  raw.at(0).meta.raw = true;
  require(core::passes::tail_call_lowering(raw, {.options = options}).applied == 0,
          "a raw call must not lose its return frame");
}

namespace {

void typed_display_indirect_read_liveness_contract() {
  const auto projected_stop = [](StopDisposition disposition) {
    auto item = stop(disposition);
    item.roles.push_back(kTypedDisplayObservationRole);
    return item;
  };
  const auto fixture = [&](const std::vector<int>& suffix) {
    auto read = op(0xd0);
    read.discarded_indirect_recall_value = true;
    read.indirect_memory_targets = std::vector<int>{7};
    std::vector<MachineItem> items{
        op(8), op(0x40), read, op(0x62), op(0x0b), op(0x63),
        projected_stop(StopDisposition::Resumable)};
    for (int code : suffix) items.push_back(op(code));
    items.push_back(projected_stop(StopDisposition::Terminal));
    return items;
  };
  const auto proved = [](const std::vector<MachineItem>& items) {
    const auto flow = core::build_post_layout_control_flow(items);
    return flow.proved &&
           core::prove_discarded_indirect_selector_reads_unobserved(items, flow, 7);
  };
  const auto observe = [](const std::vector<MachineItem>& items,
                          const std::string& data, const std::string& input,
                          bool probe_prompt = false) {
    const auto resolved = resolve_machine_items(items);
    require(resolved.diagnostics.empty(), "typed display fixture must resolve");
    std::vector<int> codes;
    for (const auto& step : resolved.steps) codes.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(),
            "typed display fixture must load into the stock ROM");
    calc.set_register("7", data).set_register("2", "2.375")
        .set_register("3", "3").set_register("4", "4")
        .set_register("5", "5").set_register("6", "6").set_register("1", "1");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(600, 5).stopped, "typed prompt must stop");
    std::vector<std::string> values;
    const auto snapshot = [&]() {
      values.push_back(calc.display_text());
      for (const char* reg : {"x", "y", "x1", "0"})
        values.push_back(calc.read_register(reg));
    };
    snapshot();
    if (probe_prompt) {
      calc.press(".");
      values.push_back(calc.display_text());
      return values;
    }
    if (!input.empty()) calc.input_number(input, true);
    calc.press("С/П");
    require(calc.run_until_stable(600, 5).stopped, "typed continuation must stop");
    snapshot();
    calc.press(".");
    values.push_back(calc.display_text());
    return values;
  };
  // The two internal stack words need not agree at the prompt, but every
  // continuation must forget them before an ordinary operation reads them.
  const auto erased = fixture({0x64, 0x65, 0x66, 0x61});
  require(proved(erased), "a typed display may suspend a proved dead deep-stack value");
  const auto terminal = fixture({0x64});
  require(proved(terminal), "a typed terminal display may discard internal Z/T");
  for (const auto& program : {erased, terminal}) {
    for (const std::string& seed :
         {"1", "9", "123.375", "-888", "0.0000025", "99999999"}) {
      for (const std::string& input : {"", "0", "2.75", "-9"}) {
        require(observe(program, "17", input) == observe(program, seed, input),
                "typed display must preserve X/Y/X1/X2 and counter observations");
      }
      require(observe(program, "17", "", true) == observe(program, seed, "", true),
              "fresh recall/sign must synchronize X2 before the prompt");
    }
  }
  const auto exposed = fixture({0x25, 0x25});
  require(!proved(exposed) &&
              observe(exposed, "17", "") != observe(exposed, "53", ""),
          "a continuation that rotates the old Z into X must reject the projection");
  auto visible_y = terminal;
  visible_y.erase(visible_y.begin() + 5);
  require(!proved(visible_y), "preview Y remains observable at typed displays");
  auto opaque = erased;
  for (auto& item : opaque) item.roles.clear();
  require(!proved(opaque), "stop disposition alone cannot invent an observation mask");
  for (bool manual : {false, true}) {
    auto protected_prompt = erased;
    if (manual) protected_prompt.at(6).manual_interaction.emplace();
    else protected_prompt.at(6).raw = true;
    require(!proved(protected_prompt), "raw and explicit manual UI remain proof barriers");
  }
  auto recovered_error = erased;
  recovered_error.at(6).opcode = 0x29;
  const auto recovery_flow = core::build_post_layout_control_flow(recovered_error);
  require(!core::prove_discarded_indirect_selector_reads_unobserved(
              recovered_error, recovery_flow, 7),
          "a typed resumable error cannot inherit the ordinary numeric-input protocol");
  auto interrupted_sign = erased;
  interrupted_sign.insert(interrupted_sign.begin() + 4, op(0x54));
  require(!proved(interrupted_sign),
          "a fresh recall sign proof must not be inferred across an intervening command");
  auto stale = core::build_post_layout_control_flow(erased);
  auto terminal_prompt = erased;
  terminal_prompt.at(6).stop_disposition = StopDisposition::Terminal;
  require(!core::prove_discarded_indirect_selector_reads_unobserved(
              terminal_prompt, stale, 7),
          "a stale resume edge cannot be erased by a terminal observation contract");
}

void discarded_indirect_read_chain_contract() {
  const auto fixture = [](std::vector<int> suffix) {
    std::vector<MachineItem> items{
        MachineItem::op(8, "8"), MachineItem::op(0x40, "store"),
        MachineItem::op(0x41, "store")};
    for (int selector : {0, 1}) {
      MachineItem read = MachineItem::op(0xd0 + selector, "discarded read");
      read.discarded_indirect_recall_value = true;
      read.indirect_memory_targets = std::vector<int>{7};
      items.push_back(read);
    }
    for (int code : suffix) items.push_back(MachineItem::op(code, "suffix"));
    MachineItem stop = MachineItem::op(0x50, "stop");
    stop.stop_disposition = StopDisposition::Terminal;
    items.push_back(stop);
    return items;
  };
  const auto proved = [](const std::vector<MachineItem>& items) {
    const auto flow = core::build_post_layout_control_flow(items);
    return core::prove_discarded_indirect_selector_reads_unobserved(items, flow, 7);
  };
  const auto observe = [](const std::vector<MachineItem>& items, const std::string& value) {
    const auto resolved = resolve_machine_items(items);
    require(resolved.diagnostics.empty(), "discarded-read fixture must resolve");
    std::vector<int> codes;
    for (const auto& step : resolved.steps) codes.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(), "discarded-read fixture must load");
    calc.set_register("7", value);
    for (int reg = 2; reg <= 5; ++reg)
      calc.set_register(std::to_string(reg), std::to_string(reg));
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(500, 5).stopped, "discarded-read fixture must stop");
    std::vector<std::string> result{calc.display_text()};
    for (const char* reg : {"x", "y", "z", "t", "x1", "0", "1"})
      result.push_back(calc.read_register(reg));
    calc.press(".");
    result.push_back(calc.display_text());
    return result;
  };
  const auto erased = fixture({0x62, 0x63, 0x64, 0x65});
  require(core::build_post_layout_control_flow(erased).proved,
          "positive discarded-read fixture needs exact control flow");
  require(proved(erased), "successive discarded reads may converge after full stack erasure");
  require(observe(erased, "17") == observe(erased, "53"),
          "accepted read chains must preserve all stack, X2 and counter observations");

  const auto partial = fixture({0x62, 0x63, 0x64});
  require(!proved(partial) && observe(partial, "17") != observe(partial, "53"),
          "three recalls must not hide an unequal T at the stop");
  const auto last_only = fixture({0x0d, 0x62});
  require(!proved(last_only) && observe(last_only, "17") != observe(last_only, "53"),
          "a later discarded read must retain taint from the earlier read in Y/Z/T");
  const auto stored = fixture({0x42, 0x62, 0x63, 0x64, 0x65});
  require(!proved(stored), "a discarded value stored to memory cannot be erased by stack cleanup");
  for (bool manual : {false, true}) {
    auto opaque = erased;
    if (manual) opaque.at(4).manual_interaction.emplace();
    else opaque.at(4).raw = true;
    require(!proved(opaque), "raw or operator-visible reads must remain proof barriers");
  }
}

} // namespace

void post_layout_control_flow_matches_typed_contract() {
  typed_display_indirect_read_liveness_contract();
  discarded_indirect_read_chain_contract();
  executable_operand_image_contract();
  tail_return_ownership_contract();
  indirect_conversion_counter_transport_contract();
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
