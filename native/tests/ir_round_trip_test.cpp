#include "mkpro/core/ir.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

MachineItem op(int opcode, std::string mnemonic) {
  return MachineItem::op(opcode, std::move(mnemonic));
}

void require_kind(const IrOp& op, IrKind kind, const std::string& message) {
  require(op.kind == kind, message + ": got " + ir_kind_name(op.kind));
}

void formal_indirect_entry_metadata_contract() {
  using core::PostLayoutControlFlowOptions;
  using core::PostLayoutExecutionState;
  using core::IndirectOperationKind;
  const auto has_state = [](const core::AuthoritativePostLayoutControlFlow& facts,
                            int physical, int formal) {
    return std::any_of(facts.execution_states.begin(), facts.execution_states.end(),
                       [&](const PostLayoutExecutionState& state) {
                         return state.address == physical && state.formal_opcode == formal;
                       });
  };
  const auto canonical = core::noncanonical_indirect_flow_entries(
      core::evaluate_indirect_address("7", "76", IndirectOperationKind::Flow));
  require(!canonical.has_value(),
          "ordinary selector entries must retain the relocatable logical/physical contract");
  for (const auto& invalid : {
           std::optional<core::IndirectAddressEvaluation>{},
           core::evaluate_indirect_address("7", "6", IndirectOperationKind::Memory)}) {
    const auto fact = core::noncanonical_indirect_flow_entries(invalid);
    require(fact.has_value() && fact->empty(),
            "an incomplete decoder fact must not invent an ordinary entry");
  }

  for (const auto& selector : std::vector<std::string>{"B1", "FA"}) {
    const auto decoded =
        core::evaluate_indirect_address("7", selector, IndirectOperationKind::Flow);
    require(decoded.has_value() && decoded->formal_address.has_value(),
            "typed indirect test selector must decode");
    const int encoded = decoded->formal_address->opcode;
    const int physical = decoded->formal_address->actual;
    const int next_encoded = formal_address_successor_opcode(encoded);
    const int continuation = formal_address_info(next_encoded).actual;
    std::vector<MachineItem> image(105, op(0x54, "NOP"));
    image.at(static_cast<std::size_t>(continuation)) = op(0x52, "return");
    image.at(static_cast<std::size_t>(physical + 1)) = op(0x50, "stop");
    image.at(static_cast<std::size_t>(physical + 1)).stop_disposition =
        StopDisposition::Terminal;
    image.at(90) = op(0xa7, "indirect call");
    image.at(90).indirect_flow_targets = std::vector<IrTarget>{physical};
    image.at(90).indirect_flow_formal_targets =
        core::noncanonical_indirect_flow_entries(decoded);
    image.at(91) = op(0x50, "stop");
    image.at(91).stop_disposition = StopDisposition::Terminal;
    image.at(90).comment = "deliberately removed before the proof";
    const auto raised = raise_machine_to_ir(image);
    auto restored = lower_ir_to_machine(raised);
    require(machine_items_equal(image.at(90), restored.at(90)),
            "IR round-trip must retain exact indirect entry encodings");
    require(ir_ops_to_json(raised).find("\"indirectFlowFormalTargets\":[" +
                                        std::to_string(encoded) + "]") != std::string::npos &&
                machine_items_to_json(restored).find("\"indirectFlowFormalTargets\":[" +
                                                     std::to_string(encoded) + "]") !=
                    std::string::npos,
            "machine and IR JSON must expose encoded entry facts");
    for (MachineItem& item : restored)
      item.comment.reset();
    PostLayoutControlFlowOptions options;
    options.main_entry = 90;
    const auto facts = core::build_post_layout_control_flow(restored, options);
    require(facts.proved && has_state(facts, physical, encoded) &&
                has_state(facts, continuation, next_encoded) && has_state(facts, 91, 0x91),
            "typed metadata alone must preserve noncanonical call/return continuations");

    auto changed = restored.at(90);
    changed.indirect_flow_formal_targets =
        std::vector<int>{official_address_to_opcode(physical)};
    require(!machine_items_equal(restored.at(90), changed),
            "equal physical targets must not erase different entry modes");

    options.proved_indirect_formal_targets[90] = {encoded};
    require(core::build_post_layout_control_flow(restored, options).proved,
            "matching explicit and attached entry facts must be accepted");
    options.proved_indirect_formal_targets[90] = {official_address_to_opcode(physical)};
    require(!core::build_post_layout_control_flow(restored, options).proved,
            "a canonical override must not hide a noncanonical attached entry");
    options.proved_indirect_formal_targets.clear();

    auto both = restored;
    both.at(90).indirect_flow_formal_targets =
        std::vector<int>{encoded, official_address_to_opcode(physical)};
    options.proved_indirect_formal_targets[90] =
        {official_address_to_opcode(physical), encoded};
    require(core::build_post_layout_control_flow(both, options).proved,
            "complete entry sets must be order-independent and retain distinct aliases");
    options.proved_indirect_formal_targets.clear();

    for (const auto& invalid : std::vector<std::vector<int>>{{}, {encoded, encoded}, {256}, {0}}) {
      auto rejected = restored;
      rejected.at(90).indirect_flow_formal_targets = invalid;
      require(!core::build_post_layout_control_flow(rejected, options).proved,
              "empty, duplicate, invalid, and stale encoded target facts must fail closed");
    }
    auto missing = restored;
    missing.at(90).indirect_flow_targets.reset();
    require(!core::build_post_layout_control_flow(missing, options).proved,
            "encoded entry evidence does not replace complete physical target evidence");
    auto wrong_source = restored;
    wrong_source.at(80).indirect_flow_formal_targets = std::vector<int>{encoded};
    require(!core::build_post_layout_control_flow(wrong_source, options).proved,
            "an entry fact on an ordinary instruction must be rejected");

    std::vector<int> codes;
    for (const MachineItem& item : restored)
      codes.push_back(item.opcode);
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(),
            "typed indirect entry emulator fixture must load");
    calc.set_register("7", selector);
    calc.press_sequence({"БП", "9", "0"});
    calc.press("ПП");
    require(calc.program_counter() == (selector == "B1" ? "L1" : " -"),
            "the hardware must enter the encoded indirect target");
    calc.press("ПП");
    require(calc.program_counter() == (selector == "B1" ? "L2" : "01"),
            "the hardware must follow the encoded rather than adjacent physical successor");
    calc.press("ПП");
    require(calc.program_counter() == "91",
            "a noncanonical callee must return to the original caller counter");
  }

  std::vector<MachineItem> logical(106, op(0x54, "NOP"));
  logical.at(104) = op(0x87, "indirect jump");
  logical.at(104).indirect_flow_targets = std::vector<IrTarget>{105};
  logical.at(105) = op(0x50, "stop");
  logical.at(105).stop_disposition = StopDisposition::Terminal;
  PostLayoutControlFlowOptions logical_options;
  logical_options.main_entry = 104;
  const auto unplaced = core::build_post_layout_control_flow(logical, logical_options);
  require(unplaced.proved && unplaced.execution_states.size() == 2 &&
              unplaced.execution_states.back().address == 105 &&
              !unplaced.execution_states.back().formal_opcode.has_value(),
          "ordinary indirect targets beyond A4 must remain separate unplaced logical cells");
}

void formal_execution_relocation_contract() {
  using core::PostLayoutControlFlowOptions;
  using core::PostLayoutExecutionEdgeKind;
  using core::PostLayoutExecutionRelocationOptions;
  const auto mapping = [](std::size_t count) {
    std::vector<std::optional<std::size_t>> result;
    for (std::size_t i = 0; i < count; ++i) result.push_back(i);
    return result;
  };
  const auto terminal = [] {
    auto item = op(0x50, "stop");
    item.stop_disposition = StopDisposition::Terminal;
    return item;
  };
  const auto flow_at = [](const std::vector<MachineItem>& image, int entry = 90,
                          AddressSpaceModel model = AddressSpaceModel::Standard) {
    PostLayoutControlFlowOptions options;
    options.main_entry = entry;
    options.address_space_model = model;
    const auto flow = core::build_post_layout_control_flow(image, options);
    require(flow.proved, "execution transport fixture must have an exact CFG");
    return flow;
  };
  const auto execute = [](const std::vector<MachineItem>& image,
                          const std::string& selector, int steps) {
    std::vector<int> codes;
    for (const auto& item : image) {
      codes.push_back(item.kind == MachineItemKind::Address
                          ? item.formal_opcode.value_or(
                                official_address_to_opcode(std::get<int>(item.target)))
                          : item.opcode);
    }
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(),
            "execution transport emulator fixture must load");
    calc.set_register("7", selector);
    calc.press("Cx");
    calc.press_sequence({"БП", "9", "0"});
    for (int i = 0; i < steps; ++i) calc.press("ПП");
    return calc.display_text();
  };

  {
    std::vector<MachineItem> before(105, op(0x54, "NOP"));
    before.at(0) = op(0x01, "1");
    before.at(1) = terminal();
    before.at(7) = op(0x02, "2");
    before.at(8) = terminal();
    before.at(90) = op(0x87, "indirect jump");
    before.at(90).indirect_flow_targets = std::vector<IrTarget>{6};
    before.at(90).indirect_flow_formal_targets = std::vector<int>{0xb1, 0x06};
    auto after = before;
    std::swap(after.at(0), after.at(7));
    std::swap(after.at(1), after.at(8));
    auto moved = mapping(before.size());
    moved.at(0) = 7;
    moved.at(7) = 0;
    moved.at(1) = 8;
    moved.at(8) = 1;
    const auto old_flow = flow_at(before), new_flow = flow_at(after);
    const auto projected = [&](const core::AuthoritativePostLayoutControlFlow& flow,
                               bool remap) {
      std::set<std::size_t> nodes;
      std::set<std::pair<std::size_t, std::size_t>> edges;
      const auto item = [&](std::size_t state) {
        const auto original = flow.execution_states.at(state).item_index;
        return remap ? *moved.at(original) : original;
      };
      for (std::size_t i = 0; i < flow.execution_states.size(); ++i) {
        require(flow.execution_states.at(i).return_stack.empty(),
                "projection counterexample must not depend on hidden caller frames");
        nodes.insert(item(i));
        for (const auto target : flow.execution_successors.at(i))
          edges.emplace(item(i), item(target));
      }
      return std::pair{nodes, edges};
    };
    require(projected(old_flow, true) == projected(new_flow, false),
            "counterexample must fool a physical-only graph comparison");
    require(!core::prove_post_layout_execution_relocation(
                 before, after, old_flow, new_flow, moved).proved,
            "identical physical graphs must not permit an unproved B1/06 permutation");
    require(execute(before, "B1", 4) != execute(after, "B1", 4) &&
                execute(before, "6", 4) != execute(after, "6", 4),
            "unlabelled context merging must expose a real emulator behavior difference");

    PostLayoutExecutionRelocationOptions transported;
    transported.indirect_entry_remap[90] = {{0xb1, 0x06}, {0x06, 0xb1}};
    require(core::prove_post_layout_execution_relocation(
                before, after, old_flow, new_flow, moved, transported).proved &&
                execute(before, "B1", 4) == execute(after, "6", 4) &&
                execute(before, "6", 4) == execute(after, "B1", 4),
            "a separately proved selector transport may legitimately change entry modes");
    auto invalid = transported;
    invalid.indirect_entry_remap[90][0xb1] = 256;
    require(!core::prove_post_layout_execution_relocation(
                 before, after, old_flow, new_flow, moved, invalid).proved,
            "selector transport must reject invalid encoded counters");
    invalid = transported;
    invalid.indirect_entry_remap[90][0xb1] = 0xfa;
    require(!core::prove_post_layout_execution_relocation(
                 before, after, old_flow, new_flow, moved, invalid).proved,
            "selector transport must name an actual corresponding alternative");
    invalid = transported;
    invalid.indirect_entry_remap[90][0x99] = 0x99;
    require(!core::prove_post_layout_execution_relocation(
                 before, after, old_flow, new_flow, moved, invalid).proved,
            "unused selector mappings must not count as proof evidence");
    invalid = transported;
    invalid.indirect_entry_remap[6] = {{0xb1, 0x06}};
    require(!core::prove_post_layout_execution_relocation(
                 before, after, old_flow, new_flow, moved, invalid).proved,
            "a selector mapping cannot be attached to a plain command");
  }

  {
    std::vector<MachineItem> before(105, op(0x54, "NOP"));
    before.at(0) = op(0x04, "4");
    before.at(1) = terminal();
    before.at(90) = op(0x87, "indirect jump");
    before.at(90).indirect_flow_targets = std::vector<IrTarget>{0};
    before.at(90).indirect_flow_formal_targets = std::vector<int>{0xb2};
    auto after = before;
    after.at(90).indirect_flow_formal_targets = std::vector<int>{0xa5};
    const auto old_flow = flow_at(before), new_flow = flow_at(after);
    require(core::prove_post_layout_execution_relocation(
                before, after, old_flow, new_flow, mapping(before.size())).proved &&
                execute(before, "B2", 3) == execute(after, "A5", 3),
            "single forced entries may change side branch when every continuation agrees");
    PostLayoutExecutionRelocationOptions contradictory;
    contradictory.indirect_entry_remap[90] = {{0xb2, 0xb2}};
    require(!core::prove_post_layout_execution_relocation(
                 before, after, old_flow, new_flow, mapping(before.size()), contradictory).proved,
            "even a singleton entry must obey an explicitly supplied selector map");
  }

  {
    std::vector<MachineItem> before(105, op(0x54, "NOP"));
    before.at(4) = op(0x53, "call");
    before.at(5) = MachineItem::address(20);
    before.at(6) = terminal();
    before.at(20) = op(0x52, "return");
    before.at(90) = op(0x51, "jump");
    before.at(91) = MachineItem::address(4);
    before.at(91).formal_opcode = 0xa9;
    auto after = before;
    after.at(91).formal_opcode.reset();
    auto old_flow = flow_at(before), new_flow = flow_at(after);
    const auto saved = [](const core::AuthoritativePostLayoutControlFlow& flow, int encoded) {
      return std::any_of(flow.execution_states.begin(), flow.execution_states.end(),
                         [&](const auto& state) {
                           return state.address == 20 &&
                                  state.return_stack == std::vector<int>{6} &&
                                  state.formal_return_stack ==
                                      std::vector<std::optional<int>>{encoded};
                         });
    };
    require(saved(old_flow, 0xb1) && saved(new_flow, 0x06),
            "caller modes must produce different formal frames at the same physical return");
    require(core::prove_post_layout_execution_relocation(
                before, after, old_flow, new_flow, mapping(before.size())).proved &&
                execute(before, "0", 4) == execute(after, "0", 4),
            "different saved formal frames are safe when their continuations are equivalent");

    for (auto* image : {&before, &after}) {
      image->at(6) = op(0x54, "NOP");
      image->at(0) = op(0x01, "1");
      image->at(1) = terminal();
      image->at(7) = op(0x02, "2");
      image->at(8) = terminal();
    }
    old_flow = flow_at(before);
    new_flow = flow_at(after);
    require(!core::prove_post_layout_execution_relocation(
                 before, after, old_flow, new_flow, mapping(before.size())).proved &&
                execute(before, "0", 6) != execute(after, "0", 6),
            "formal return frames must remain paired until their differing continuations emerge");
  }

  {
    std::vector<MachineItem> before = {
        op(0x51, "jump"), MachineItem::address(std::string("body")), op(0x54, "dead"),
        MachineItem::label("body"), op(0x01, "1"), terminal(),
    };
    auto after = before;
    after.erase(after.begin() + 2);
    auto moved = mapping(before.size());
    moved.at(2).reset();
    for (std::size_t i = 3; i < moved.size(); ++i) moved.at(i) = i - 1U;
    const auto old_flow = flow_at(before, 0), new_flow = flow_at(after, 0);
    require(core::prove_post_layout_execution_relocation(
                before, after, old_flow, new_flow, moved).proved,
            "ordinary deletion and symbolic address relocation must remain supported");
    auto wrong_operand = moved;
    wrong_operand.at(1) = 0;
    require(!core::prove_post_layout_execution_relocation(
                 before, after, old_flow, new_flow, wrong_operand).proved,
            "address-word identity must follow the declared command relocation");
    PostLayoutExecutionRelocationOptions bounded;
    bounded.maximum_state_pairs = 1;
    require(!core::prove_post_layout_execution_relocation(
                 before, after, old_flow, new_flow, moved, bounded).proved,
            "the relational worklist must stop at its explicit pair budget");
  }

  {
    std::vector<MachineItem> before = {
        op(0x57, "conditional"), MachineItem::address(4),
        op(0x01, "1"), terminal(), op(0x02, "2"), terminal(),
    };
    auto after = before;
    std::swap(after.at(2), after.at(4));
    std::swap(after.at(3), after.at(5));
    auto moved = mapping(before.size());
    moved.at(2) = 4;
    moved.at(4) = 2;
    moved.at(3) = 5;
    moved.at(5) = 3;
    require(!core::prove_post_layout_execution_relocation(
                 before, after, flow_at(before, 0), flow_at(after, 0), moved).proved,
            "taken and fallthrough alternatives cannot be exchanged by graph isomorphism");
    std::vector<MachineItem> joined = {
        op(0x57, "conditional"), MachineItem::address(2), terminal(),
    };
    const auto joined_flow = flow_at(joined, 0);
    require(joined_flow.execution_successors.front().size() == 1U &&
                joined_flow.execution_edges.front().size() == 2U &&
                joined_flow.execution_edges.front().at(0).kind ==
                    PostLayoutExecutionEdgeKind::DirectTarget &&
                joined_flow.execution_edges.front().at(1).kind ==
                    PostLayoutExecutionEdgeKind::Fallthrough &&
                core::prove_post_layout_execution_relocation(
                    joined, joined, joined_flow, joined_flow, mapping(joined.size())).proved,
            "distinct branch labels must survive a shared successor");
    auto incomplete = joined_flow;
    incomplete.execution_edges.front().clear();
    require(!core::prove_post_layout_execution_relocation(
                 joined, joined, joined_flow, incomplete, mapping(joined.size())).proved,
            "labelled and projected successor representations must agree");
  }

  {
    const auto standard = AddressSpaceModel::Standard;
    const auto expanded = AddressSpaceModel::Mk61SMiniExpanded;
    require(official_program_step_limit(standard) == 105 &&
                official_program_step_limit(expanded) == 112 &&
                official_program_last_address(expanded) == 111,
            "MK61S expanded memory must be exactly 105 plus seven command cells");
    for (int address = 105; address < 112; ++address) {
      const int encoded = official_address_to_opcode(address, expanded);
      const auto ordinary = formal_address_info(encoded, expanded);
      const auto side = formal_address_info(encoded, standard);
      require(ordinary.actual == address && ordinary.kind == FormalAddressKind::Official &&
                  side.actual == address - 105 && side.kind == FormalAddressKind::ShortSide,
              "A5 through B1 must be real MK61S cells, not stock short-side aliases");
      const auto selector = format_formal_address_opcode(encoded);
      require(!core::noncanonical_indirect_flow_entries(core::evaluate_indirect_address(
                   "7", selector, core::IndirectOperationKind::Flow, expanded)).has_value() &&
                  core::noncanonical_indirect_flow_entries(core::evaluate_indirect_address(
                      "7", selector, core::IndirectOperationKind::Flow, standard)) ==
                      std::optional<std::vector<int>>{{encoded}},
              "selector metadata must classify ordinary entries by the selected memory model");

      std::vector<MachineItem> image(112, op(0x54, "NOP"));
      image.at(static_cast<std::size_t>(address)) = terminal();
      image.at(90) = op(0x87, "indirect jump");
      image.at(90).indirect_flow_targets = std::vector<IrTarget>{address};
      image.at(90).indirect_flow_formal_targets = std::vector<int>{encoded};
      const auto flow = flow_at(image, 90, expanded);
      require(flow.execution_states.size() == 2U &&
                  flow.execution_states.back().address == address &&
                  flow.execution_states.back().formal_opcode == encoded,
              "expanded indirect entries must reach the seven additional command identities");
      PostLayoutControlFlowOptions wrong_profile;
      wrong_profile.main_entry = 90;
      require(!core::build_post_layout_control_flow(image, wrong_profile).proved,
              "stock decoding must reject expanded entry facts with a different projection");
    }

    const std::vector<MachineItem> one_command{terminal()};
    const auto stock_flow = flow_at(one_command, 0, standard);
    const auto expanded_flow = flow_at(one_command, 0, expanded);
    require(!core::prove_post_layout_execution_relocation(
                 one_command, one_command, stock_flow, expanded_flow, mapping(1)).proved,
            "a relocation proof must not mix different target memory models");
  }

  // Expanded-memory contracts exercise the compiler's selected address model;
  // the stock hardware emulator above is not an emulator for the 112-cell profile.
  for (const auto model : {AddressSpaceModel::Standard, AddressSpaceModel::Mk61SMiniExpanded}) {
    const int limit = official_program_step_limit(model);
    const auto count = static_cast<std::size_t>(limit);
    const int last_encoded = official_address_to_opcode(limit - 1, model);
    const int wrapped_encoded = formal_address_successor_opcode(last_encoded);
    require(formal_address_info(wrapped_encoded, model).actual == 0,
            "selected-profile program boundary must enter its correct side counter");

    std::vector<MachineItem> image(count, op(0x54, "NOP"));
    image.at(0) = terminal();
    const auto boundary = flow_at(image, 104, model);
    require(boundary.execution_states.size() == count - 104U + 1U,
            "expanded execution must visit all seven real extra cells before wrapping");
    for (std::size_t i = 0; i + 1U < boundary.execution_states.size(); ++i) {
      const int address = 104 + static_cast<int>(i);
      require(boundary.execution_states.at(i).address == address &&
                  boundary.execution_states.at(i).formal_opcode ==
                      official_address_to_opcode(address, model),
              "ordinary boundary commands must keep their selected-profile physical identity");
    }
    require(boundary.execution_states.back().address == 0 &&
                boundary.execution_states.back().formal_opcode == wrapped_encoded,
            "wrapping must retain the encoded side entry rather than canonicalize to 00");

    image.at(count - 1U) = op(0xa7, "indirect call");
    image.at(count - 1U).indirect_flow_targets = std::vector<IrTarget>{20};
    image.at(20) = op(0x52, "return");
    const auto called = flow_at(image, limit - 1, model);
    require(called.execution_states.size() == 3U &&
                called.execution_states.at(1).return_stack == std::vector<int>{0} &&
                called.execution_states.at(1).formal_return_stack ==
                    std::vector<std::optional<int>>{wrapped_encoded} &&
                called.execution_states.back().formal_opcode == wrapped_encoded,
            "a boundary call must return through the selected model's encoded continuation");

    image.at(count - 1U) = op(0x50, "resumable stop");
    image.at(count - 1U).stop_disposition = StopDisposition::Resumable;
    const auto stopped = flow_at(image, limit - 1, model);
    require(stopped.execution_states.size() == 2U &&
                stopped.execution_edges.front().front().kind ==
                    PostLayoutExecutionEdgeKind::Resume &&
                stopped.execution_states.back().formal_opcode == wrapped_encoded,
            "STOP continuation must use the expanded or stock boundary without conflating them");

    image.at(count - 1U) = op(0x54, "error padding");
    image.at(count - 2U) = op(0x29, "resumable error");
    image.at(count - 2U).stop_disposition = StopDisposition::Resumable;
    const auto resumed = flow_at(image, limit - 2, model);
    require(resumed.execution_states.size() == 2U &&
                resumed.execution_states.back().address == 0 &&
                resumed.execution_states.back().formal_opcode == wrapped_encoded,
            "resumable error must skip exactly one real padding cell at either profile boundary");

    std::vector<MachineItem> before(count + 1U, op(0x54, "NOP"));
    before.at(0) = op(0x51, "jump");
    before.at(1) = MachineItem::address(limit);
    before.at(count) = terminal();
    auto after = before;
    after.erase(after.begin() + 2);
    after.at(1).target = limit - 1;
    auto moved = mapping(before.size());
    moved.at(2).reset();
    for (std::size_t i = 3; i < moved.size(); ++i) moved.at(i) = i - 1U;
    const auto old_flow = flow_at(before, 0, model);
    const auto new_flow = flow_at(after, 0, model);
    require(!old_flow.execution_states.back().formal_opcode.has_value() &&
                new_flow.execution_states.back().formal_opcode == last_encoded &&
                core::prove_post_layout_execution_relocation(
                    before, after, old_flow, new_flow, moved).proved,
            "logical overflow must relocate into the selected 105- or 112-cell physical image");
  }
}

} // namespace

void ir_round_trip_matches_typescript_contract() {
  formal_execution_relocation_contract();
  formal_indirect_entry_metadata_contract();
  {
    std::vector<MachineItem> items;
    items.push_back(MachineItem::label("loop"));
    items.push_back(op(0x41, "X->П 1"));
    items.back().comment = "store value";
    items.back().source_line = 12;
    items.back().raw = true;
    items.push_back(op(0x51, "БП"));
    items.back().comment = "jump back";
    items.back().source_line = 13;
    items.push_back(MachineItem::address(std::string("loop")));
    items.back().comment = "loop target";
    items.back().source_line = 13;

    const std::vector<MachineItem> restored = lower_ir_to_machine(raise_machine_to_ir(items));
    require(restored.size() == items.size(), "machine round-trip changed item count");
    for (std::size_t index = 0; index < items.size(); ++index) {
      require(machine_items_equal(items.at(index), restored.at(index)),
              "machine round-trip mismatch at item " + std::to_string(index) + "\nexpected " +
                  machine_items_to_json({items.at(index)}) + "\nactual " +
                  machine_items_to_json({restored.at(index)}));
    }
  }

  {
    const std::vector<MachineItem> items = {
        op(0x51, "БП"),     MachineItem::address(std::string("main")),
        op(0x53, "ПП"),     MachineItem::address(12),
        op(0x57, "F x!=0"), MachineItem::address(std::string("skip")),
        op(0x59, "F x>=0"), MachineItem::address(std::string("skip")),
        op(0x5c, "F x<0"),  MachineItem::address(std::string("skip")),
        op(0x5e, "F x=0"),  MachineItem::address(std::string("skip")),
        op(0x58, "F L2"),   MachineItem::address(std::string("loop")),
        op(0x5b, "F L1"),   MachineItem::address(std::string("loop")),
        op(0x52, "В/О"),
    };
    const std::vector<IrOp> ir = raise_machine_to_ir(items);
    require(ir.size() == 9, "addressed op raise produced wrong IR count");
    require_kind(ir.at(0), IrKind::Jump, "expected jump");
    require_kind(ir.at(1), IrKind::Call, "expected call");
    require_kind(ir.at(2), IrKind::CondJump, "expected !=0 conditional jump");
    require(ir.at(2).condition == "!=0", "wrong !=0 condition");
    require_kind(ir.at(3), IrKind::CondJump, "expected >=0 conditional jump");
    require(ir.at(3).condition == ">=0", "wrong >=0 condition");
    require_kind(ir.at(4), IrKind::CondJump, "expected <0 conditional jump");
    require(ir.at(4).condition == "<0", "wrong <0 condition");
    require_kind(ir.at(5), IrKind::CondJump, "expected ==0 conditional jump");
    require(ir.at(5).condition == "==0", "wrong ==0 condition");
    require_kind(ir.at(6), IrKind::Loop, "expected L2 loop");
    require(ir.at(6).counter == "L2", "wrong L2 counter");
    require_kind(ir.at(7), IrKind::Loop, "expected L1 loop");
    require(ir.at(7).counter == "L1", "wrong L1 counter");
    require_kind(ir.at(8), IrKind::Return, "expected return");
  }

  {
    const std::vector<MachineItem> items = {
        op(0x41, "X->П 1"),  op(0x65, "П->X 5"),  op(0xb3, "К X->П 3"), op(0xd7, "К П->X 7"),
        op(0x82, "К БП 2"),  op(0xa4, "К ПП 4"),  op(0x71, "К x!=0 1"), op(0x93, "К x>=0 3"),
        op(0xc5, "К x<0 5"), op(0xe7, "К x=0 7"),
    };
    const std::vector<IrOp> ir = raise_machine_to_ir(items);
    require_kind(ir.at(0), IrKind::Store, "expected direct store");
    require(ir.at(0).register_name == "1", "wrong store register");
    require_kind(ir.at(1), IrKind::Recall, "expected direct recall");
    require(ir.at(1).register_name == "5", "wrong recall register");
    require_kind(ir.at(2), IrKind::IndirectStore, "expected indirect store");
    require_kind(ir.at(3), IrKind::IndirectRecall, "expected indirect recall");
    require_kind(ir.at(4), IrKind::IndirectJump, "expected indirect jump");
    require_kind(ir.at(5), IrKind::IndirectCall, "expected indirect call");
    require_kind(ir.at(6), IrKind::IndirectCondJump, "expected indirect !=0 jump");
    require(ir.at(6).condition == "!=0", "wrong indirect !=0 condition");
    require_kind(ir.at(9), IrKind::IndirectCondJump, "expected indirect ==0 jump");
    require(ir.at(9).condition == "==0", "wrong indirect ==0 condition");
  }

  {
    const std::vector<MachineItem> rf_items = {
        op(0x4f, "X->П f"),
        op(0x6f, "П->X f"),
    };
    const std::vector<IrOp> standard = raise_machine_to_ir(rf_items);
    require_kind(standard.at(0), IrKind::Store,
                 "standard profile should type 4F as the undocumented R0 store alias");
    require_kind(standard.at(1), IrKind::Recall,
                 "standard profile should type 6F as the undocumented R0 recall alias");
    require(standard.at(0).register_name == "0" && standard.at(1).register_name == "0",
            "standard-profile 4F/6F aliases lost their R0 identity");

    const std::vector<IrOp> expanded =
        raise_machine_to_ir(rf_items, FeatureProfile::Mk61SMiniExpanded);
    require_kind(expanded.at(0), IrKind::Store, "expanded profile should type the Rf store");
    require_kind(expanded.at(1), IrKind::Recall, "expanded profile should type the Rf recall");
    require(expanded.at(0).register_name == "f" && expanded.at(1).register_name == "f",
            "expanded-profile Rf operations lost their register identity");
  }

  {
    const std::vector<IrOp> aliases = raise_machine_to_ir({
        op(0x8f, "К БП 0 alias"),
        op(0xbf, "К X->П 0 alias"),
        op(0xdf, "К П->X 0 alias"),
    });
    require_kind(aliases.at(0), IrKind::IndirectJump,
                 "standard 8F alias should retain indirect R0 flow semantics");
    require_kind(aliases.at(1), IrKind::IndirectStore,
                 "standard BF alias should retain indirect R0 store semantics");
    require_kind(aliases.at(2), IrKind::IndirectRecall,
                 "standard DF alias should retain indirect R0 recall semantics");
    require(std::all_of(aliases.begin(), aliases.end(),
                        [](const IrOp& alias) { return alias.register_name == "0"; }),
            "standard xF aliases lost their R0 selector identity");
  }

  {
    std::vector<MachineItem> stops = {
        op(0x50, "С/П"), op(0x50, "С/П"), op(0x50, "С/П"), op(0x50, "С/П"),
        op(0x50, "С/П"), op(0x50, "С/П"), op(0x50, "С/П"),
    };
    stops.at(0).comment = "halt";
    stops.at(1).comment = "pause";
    stops.at(2).comment = "show main";
    stops.at(3).comment = "ask key";
    stops.at(4).comment = "read x";
    stops.at(5).comment = "implicit final stop";
    const std::vector<IrOp> ir = raise_machine_to_ir(stops);
    const std::vector<std::string> expected = {
        "halt", "pause", "show", "ask", "input", "halt", "unknown",
    };
    for (std::size_t index = 0; index < expected.size(); ++index) {
      require_kind(ir.at(index), IrKind::Stop, "expected stop");
      require(ir.at(index).semantic == expected.at(index),
              "stop semantic mismatch at index " + std::to_string(index));
    }
  }

  {
    std::vector<MachineItem> typed_stops = {
        op(0x50, "С/П"),
        op(0x50, "С/П"),
        op(0x50, "С/П"),
    };
    typed_stops.at(0).stop_disposition = StopDisposition::Resumable;
    typed_stops.at(0).manual_interaction = ManualInteractionAnchor{
        .protocol_id = 3,
        .phase = -1,
        .kind = ManualInteractionAnchorKind::PromptStop,
    };
    typed_stops.at(1).stop_disposition = StopDisposition::Terminal;
    typed_stops.at(2).raw = true;

    const std::vector<MachineItem> restored =
        lower_ir_to_machine(raise_machine_to_ir(typed_stops));
    require(restored.size() == typed_stops.size(),
            "typed STOP round-trip changed item count");
    for (std::size_t index = 0; index < typed_stops.size(); ++index) {
      require(machine_items_equal(typed_stops.at(index), restored.at(index)),
              "typed STOP round-trip lost compiler-owned provenance");
    }
    require(restored.at(2).stop_disposition == StopDisposition::Unknown,
            "raw STOP without compiler-owned provenance must remain unknown");
  }

  {
    std::vector<MachineItem> typed_indirect = {
        op(0xa7, "К ПП 7"),
        op(0xb8, "К X->П 8"),
        op(0xd1, "К П->X 1"),
    };
    typed_indirect.at(0).indirect_flow_targets =
        std::vector<IrTarget>{std::string("callee"), 42};
    typed_indirect.at(1).indirect_memory_targets = std::vector<int>{2, 4, 6};
    typed_indirect.at(2).discarded_indirect_recall_value = true;
    const std::vector<MachineItem> restored =
        lower_ir_to_machine(raise_machine_to_ir(typed_indirect));
    require(restored.size() == typed_indirect.size() &&
                machine_items_equal(restored.at(0), typed_indirect.at(0)) &&
                machine_items_equal(restored.at(1), typed_indirect.at(1)) &&
                machine_items_equal(restored.at(2), typed_indirect.at(2)),
            "typed complete indirect target sets must survive machine/IR round-trip");
    const std::string json = machine_items_to_json(restored);
    require(json.find("\"indirectFlowTargets\":[\"callee\",42]") != std::string::npos &&
                json.find("\"indirectMemoryTargets\":[2,4,6]") != std::string::npos &&
                json.find("\"discardedIndirectRecallValue\":true") != std::string::npos,
            "typed complete indirect target sets should be visible in machine JSON");
  }

  {
    const std::vector<LayoutIrCell> cells = {
        {.address = 0, .opcode = 0x41, .roles = {"exec"}, .tactic = "store"},
        {.address = 1, .opcode = 0x51, .roles = {"exec"}, .tactic = "jump"},
        {.address = 2, .opcode = 0x10, .roles = {"address"}, .tactic = "jump target"},
        {.address = 3, .opcode = 0x65, .roles = {"exec"}, .tactic = "recall"},
        {.address = 4, .opcode = 0x10, .roles = {"exec"}, .tactic = "add"},
        {.address = 5, .opcode = 0x50, .roles = {"exec"}, .tactic = "halt"},
    };
    const std::vector<IrOp> ir = raise_layout_to_ir(cells);
    const LowerLayoutResult lowered = lower_ir_to_layout(ir);
    require(ir.size() == 5, "layout raise produced wrong IR count");
    require_kind(ir.at(1), IrKind::Jump, "expected layout jump");
    require(std::get<int>(ir.at(1).target) == 0x10, "layout jump target mismatch");
    require(lowered.cells.size() == cells.size(), "layout lowering changed cell count");
    for (std::size_t index = 0; index < cells.size(); ++index) {
      require(lowered.cells.at(index).opcode == cells.at(index).opcode,
              "layout opcode mismatch at cell " + std::to_string(index));
      require(lowered.cells.at(index).address == cells.at(index).address,
              "layout address mismatch at cell " + std::to_string(index));
    }
  }

  {
    IrOp label;
    label.kind = IrKind::Label;
    label.name = "start";
    IrOp jump;
    jump.kind = IrKind::Jump;
    jump.opcode = 0x51;
    jump.target = std::string("start");
    jump.meta.mnemonic = "БП";
    jump.target_meta.formal_opcode = 0x99;
    jump.target_meta.comment = "formal target";
    const LowerLayoutResult lowered = lower_ir_to_layout({label, jump}, {.default_tactic = "auto"});
    require(lowered.address_of_label.size() == 1, "label address table was not populated");
    require(lowered.address_of_label.at(0).name == "start", "label table name mismatch");
    require(lowered.address_of_label.at(0).address == 0, "label table address mismatch");
    require(lowered.cells.at(1).opcode == 0x99, "formal opcode did not override target address");
    require(lowered.cells.at(1).tactic == "formal target", "target comment did not become tactic");
  }
}

} // namespace mkpro::tests
