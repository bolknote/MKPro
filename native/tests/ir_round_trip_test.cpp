#include "mkpro/core/ir.hpp"

#include "test_support.hpp"

#include <algorithm>
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

} // namespace

void ir_round_trip_matches_typescript_contract() {
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
