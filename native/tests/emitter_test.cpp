#include "mkpro/core/emit/machine_emitter.hpp"

#include "test_support.hpp"

#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

void require_item(const MachineItem& item, MachineItemKind kind, int opcode,
                  const std::string& message) {
  require(item.kind == kind, message + ": wrong item kind");
  require(item.opcode == opcode, message + ": wrong opcode");
}

} // namespace

void emitter_matches_initial_typescript_contract() {
  {
    MachineEmitter emitter;
    emitter.emit_number("-12.3e-4");
    const std::vector<int> expected = {1, 2, 0x0a, 3, 0x0c, 4, 0x0b, 0x0b};
    require(emitter.items.size() == expected.size(), "number emission item count mismatch");
    for (std::size_t index = 0; index < expected.size(); ++index) {
      require_item(emitter.items.at(index), MachineItemKind::Op, expected.at(index),
                   "number emission mismatch at " + std::to_string(index));
    }
    require(emitter.items.at(4).comment == "exponent", "exponent comment should be preserved");
    require(emitter.items.at(6).comment == "negative exponent",
            "negative exponent comment should be preserved");
    require(emitter.items.at(7).comment == "negative number",
            "negative number comment should be preserved");
  }

  {
    MachineEmitter emitter;
    emitter.emit_number("1");
    emitter.emit_number("3");
    require(emitter.items.size() == 3, "adjacent number emission should insert separator");
    require(emitter.items.at(1).opcode == 0x0e, "adjacent number separator should be В↑");
    require(emitter.items.at(1).comment == "separate adjacent number entry",
            "adjacent number separator comment mismatch");
  }

  {
    MachineEmitter emitter;
    emitter.current_x_variable = "score";
    emitter.current_x_aliases.insert("score");
    emitter.emit_jump(0x51, "БП", std::string("loop"), "jump loop", 7);
    require(emitter.items.size() == 2, "jump should emit op and address");
    require(emitter.label_edge_x.at("loop") == "score", "jump should record label X edge");
    require(emitter.items.at(1).kind == MachineItemKind::Address, "jump target should be address");
    require(emitter.items.at(1).comment == "jump loop", "jump address comment mismatch");
    require(emitter.items.at(1).source_line == 7, "jump address source line mismatch");
  }

  {
    MachineEmitter emitter;
    emitter.emit_formal_address(0xa5, "side jump", 9);
    require(emitter.items.size() == 1, "formal address should emit one item");
    require(emitter.items.at(0).kind == MachineItemKind::Address,
            "formal address should emit address item");
    require(std::get<int>(emitter.items.at(0).target) == 105,
            "formal address target should use ordinal");
    require(emitter.items.at(0).formal_opcode == 0xa5,
            "formal address should preserve formal opcode");
    require(emitter.items.at(0).comment == "side jump; formal A5->00",
            "formal address comment mismatch");
    require(emitter.items.at(0).source_line == 9, "formal address source line mismatch");
  }

  {
    MachineEmitter emitter;
    emitter.emit_op(0x53, "ПП", "formal call");
    emitter.emit_formal_address(0xe1, "formal target");
    const ResolvedProgram resolved = resolve_machine_items(emitter.items);
    require(resolved.diagnostics.empty(),
            "resolving a high formal address should not validate its ordinal as official");
    require(resolved.steps.size() == 2, "formal call should resolve to two steps");
    require(resolved.steps.at(1).opcode == 0xe1, "formal address should preserve opcode E1");
    require(resolved.steps.at(1).mnemonic == "E1", "formal address mnemonic should be formal");
  }

  {
    MachineEmitter emitter;
    const std::string first = emitter.fresh_label("loop");
    const std::string second = emitter.fresh_label("loop");
    require(first == "__loop_0" && second == "__loop_1", "fresh labels should be deterministic");
    emitter.current_x_variable = "x";
    emitter.current_x_aliases.insert("x");
    emitter.record_label_edge("join", std::string("y"));
    emitter.emit_label("join", {.procedure_boundary = "start", .procedure_name = "joinProc"});
    require(!emitter.current_x_variable.has_value(),
            "conflicting label edge should clear current X fact");
    require(emitter.current_x_aliases.empty(), "label merge should clear stale X aliases");
    require(emitter.items.at(0).procedure_boundary == "start",
            "label procedure boundary should be preserved");
    require(emitter.items.at(0).procedure_name == "joinProc",
            "label procedure name should be preserved");
  }

  {
    MachineEmitter emitter;
    emitter.emit_label("start");
    emitter.emit_op(0x41, "X->П 1", "store");
    emitter.emit_jump(0x51, "БП", std::string("start"), "again");
    emitter.emit_label("hidden", {.hidden = true});
    emitter.emit_op(0x50, "С/П", "halt");

    const ResolvedProgram resolved = resolve_machine_items(emitter.items);
    require(resolved.diagnostics.empty(), "simple layout should not produce diagnostics");
    require(resolved.steps.size() == 4, "simple layout step count mismatch");
    require(resolved.steps.at(0).address == 0 && resolved.steps.at(0).opcode == 0x41,
            "first resolved step mismatch");
    require(resolved.steps.at(1).address == 1 && resolved.steps.at(1).hex == "51",
            "jump resolved step mismatch");
    require(resolved.steps.at(2).address == 2 && resolved.steps.at(2).opcode == 0x00,
            "address resolved step should target start label");
    require(resolved.steps.at(2).mnemonic == "00", "address resolved mnemonic mismatch");
    require(resolved.labels.size() == 1 && resolved.labels.at(0).name == "start" &&
                resolved.labels.at(0).address == 0,
            "visible label table mismatch");
  }

  {
    const std::vector<MachineItem> items = {MachineItem::address(std::string("missing"))};
    const ResolvedProgram resolved = resolve_machine_items(items);
    require(resolved.steps.empty(), "unknown label should not produce a step");
    require(resolved.diagnostics.size() == 1, "unknown label should produce one diagnostic");
    require(resolved.diagnostics.at(0).severity == DiagnosticSeverity::Error,
            "unknown label diagnostic severity mismatch");
    require(resolved.diagnostics.at(0).message == "Unknown label 'missing'",
            "unknown label diagnostic text mismatch");
  }
}

} // namespace mkpro::tests
