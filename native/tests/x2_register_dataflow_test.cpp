#include "mkpro/core/passes/helpers.hpp"

#include "test_support.hpp"

#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

int register_index(const std::string& register_name) {
  if (register_name.size() != 1U)
    return 0;
  const char ch = register_name.at(0);
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'a' && ch <= 'e')
    return 10 + (ch - 'a');
  return 0;
}

IrOp label(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
  return op;
}

IrOp recall(std::string register_name) {
  IrOp op;
  op.kind = IrKind::Recall;
  op.register_name = std::move(register_name);
  op.opcode = 0x60 + register_index(op.register_name);
  op.meta.mnemonic = "П->X";
  return op;
}

IrOp store(std::string register_name) {
  IrOp op;
  op.kind = IrKind::Store;
  op.register_name = std::move(register_name);
  op.opcode = 0x40 + register_index(op.register_name);
  op.meta.mnemonic = "X->П";
  return op;
}

IrOp plain(int opcode, std::string mnemonic) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
  return op;
}

IrOp jump_to(std::string target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = std::move(target);
  op.meta.mnemonic = "БП";
  return op;
}

IrOp cjump(std::string target) {
  IrOp op;
  op.kind = IrKind::CondJump;
  op.condition = "==0";
  op.opcode = 0x5e;
  op.target = std::move(target);
  op.meta.mnemonic = "F x=0";
  return op;
}

IrOp loop(std::string target) {
  IrOp op;
  op.kind = IrKind::Loop;
  op.counter = "L0";
  op.opcode = 0x5d;
  op.target = std::move(target);
  op.meta.mnemonic = "F L0";
  return op;
}

IrOp call_to(std::string target) {
  IrOp op;
  op.kind = IrKind::Call;
  op.opcode = 0x53;
  op.target = std::move(target);
  op.meta.mnemonic = "ПП";
  op.meta.comment = "proc call";
  return op;
}

IrOp known_target_indirect_recall(std::string selector, std::string target) {
  IrOp op;
  op.kind = IrKind::IndirectRecall;
  op.register_name = std::move(selector);
  op.opcode = 0xd0 + register_index(op.register_name);
  op.meta.mnemonic = "К П->X";
  op.meta.comment = "indirect-memory-target=" + target;
  return op;
}

IrOp known_target_indirect_jump(std::string selector, int target) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = std::move(selector);
  op.opcode = 0x80 + register_index(op.register_name);
  op.meta.mnemonic = "К БП";
  op.meta.comment = "indirect-target=" + std::to_string(target);
  return op;
}

IrOp known_target_indirect_cjump(std::string selector, int target) {
  IrOp op;
  op.kind = IrKind::IndirectCondJump;
  op.condition = "==0";
  op.register_name = std::move(selector);
  op.opcode = 0xe0 + register_index(op.register_name);
  op.meta.mnemonic = "К x=0";
  op.meta.comment = "indirect-target=" + std::to_string(target);
  return op;
}

IrOp halt() {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  op.semantic = "halt";
  op.meta.mnemonic = "С/П";
  return op;
}

IrOp ret() {
  IrOp op;
  op.kind = IrKind::Return;
  op.opcode = 0x52;
  op.meta.mnemonic = "В/О";
  return op;
}

core::passes::RegisterValueSet registers(std::initializer_list<std::string> values) {
  return core::passes::RegisterValueSet(values.begin(), values.end());
}

std::string join(const std::vector<std::string>& values) {
  std::ostringstream out;
  out << "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0U)
      out << ", ";
    out << values.at(index);
  }
  out << "]";
  return out.str();
}

std::vector<std::string> state_text(
    const std::optional<core::passes::RegisterValueSet>& state) {
  require(state.has_value(), "expected a defined register state");
  return std::vector<std::string>(state->begin(), state->end());
}

void require_state(const std::optional<core::passes::RegisterValueSet>& state,
                   std::initializer_list<std::string> expected,
                   const std::string& message) {
  const std::vector<std::string> actual = state_text(state);
  const std::vector<std::string> expected_values(expected.begin(), expected.end());
  require(actual == expected_values,
          message + ": expected " + join(expected_values) + ", got " + join(actual));
}

} // namespace

void x2_register_dataflow_matches_typescript_contract() {
  {
    const std::vector<IrOp> program = {recall("2"), plain(0x20, "F pi"), store("3"),
                                       known_target_indirect_recall("7", "5"),
                                       plain(0x0d, "Cx"), halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(1), {"2"}, "x2 state after direct recall");
    require_state(states.at(2), {"2"}, "x2 state through preserving plain op");
    require_state(states.at(3), {"2"}, "x2 state through store");
    require_state(states.at(4), {"5"}, "x2 state after stable indirect recall");
    require_state(states.at(5), {}, "x2 state after Cx");
  }

  {
    const std::vector<IrOp> program = {recall("2"), store("3"), plain(0x20, "F pi"),
                                       halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(1), {"2"}, "x2 alias initial recall");
    require_state(states.at(2), {"2", "3"}, "x2 alias created by store");
    require_state(states.at(3), {"2", "3"}, "x2 alias survives preserving op");
  }

  {
    const std::vector<IrOp> program = {recall("2"), plain(0xf0, "F* empty F0"),
                                       plain(0x54, "К НОП"), halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(1), {"2"}, "x2 F* initial recall");
    require_state(states.at(2), {"2"}, "x2 sync through F* empty opcode");
    require_state(states.at(3), {"2"}, "x2 sync through K NOP");
  }

  {
    const std::vector<IrOp> program = {recall("2"), plain(0x0e, "В↑"),
                                       plain(0x54, "К НОП"), halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(1), {"2"}, "x2 stack lift initial recall");
    require_state(states.at(2), {"2"}, "x2 sync through stack lift");
    require_state(states.at(3), {"2"}, "x2 stack lift through K NOP");
  }

  {
    const std::vector<IrOp> program = {recall("2"), plain(0x0e, "В↑"),
                                       plain(0x35, "К {x}"), plain(0x3e, "Y->X"),
                                       plain(0x0e, "В↑"), halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(3), {"2"}, "x2 Y alias before Y->X");
    require_state(states.at(4), {"2"}, "x2 Y alias copied to X");
    require_state(states.at(5), {"2"}, "x2 Y alias after later stack lift");
  }

  {
    const std::vector<IrOp> program = {recall("2"), plain(0x20, "F pi"), store("2"),
                                       plain(0x3e, "Y->X"), plain(0x0e, "В↑"), halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(2), {"2"}, "x2 stale Y pre-overwrite");
    require_state(states.at(3), {}, "x2 stale Y dropped after overwrite");
    require_state(states.at(5), {}, "x2 stale Y stays dropped");
  }

  {
    const std::vector<IrOp> program = {recall("2"), store("3"), plain(0x35, "К {x}"),
                                       store("3"), plain(0x20, "F pi"), halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(2), {"2", "3"}, "x2 alias before non-X2 overwrite");
    require_state(states.at(3), {"2", "3"}, "x2 alias at non-X2 overwrite");
    require_state(states.at(4), {"2"}, "x2 alias dropped for overwritten register");
    require_state(states.at(5), {"2"}, "x2 surviving alias after overwrite");
  }

  {
    const std::vector<IrOp> program = {recall("2"), cjump("jumped"), plain(0x20, "F pi"),
                                       jump_to("done"), label("jumped"),
                                       plain(0x20, "F pi"), label("done"), halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(2), {"2"}, "x2 direct conditional fallthrough state");
    require_state(states.at(4), {"2"}, "x2 direct conditional jump state");
    require_state(states.at(5), {"2"}, "x2 direct conditional target body state");
  }

  {
    core::passes::X2RegisterEdgeState input;
    input.x = registers({"4"});
    input.x2 = registers({"2"});
    const auto projected_fallthrough = core::passes::transfer_x2_register_state_for_edge(
        input, cjump("skip"), core::passes::X2DataflowEdgeKind::Fallthrough);
    const auto projected_jump = core::passes::transfer_x2_register_state_for_edge(
        input, cjump("skip"), core::passes::X2DataflowEdgeKind::Jump);

    require_state(projected_fallthrough, {"4"}, "x2 direct conditional fallthrough projection");
    require_state(projected_jump, {"2"}, "x2 direct conditional jump projection");
  }

  {
    core::passes::X2RegisterEdgeState loop_input;
    loop_input.x = registers({"0", "4"});
    loop_input.x2 = registers({"0", "4"});
    const auto projected_loop_jump = core::passes::transfer_x2_register_state_for_edge(
        loop_input, loop("done"), core::passes::X2DataflowEdgeKind::Jump);

    core::passes::X2RegisterEdgeState indirect_input;
    indirect_input.x = registers({"1", "4"});
    indirect_input.x2 = registers({"1", "4"});
    const auto projected_indirect_fallthrough =
        core::passes::transfer_x2_register_state_for_edge(
            indirect_input, known_target_indirect_cjump("1", 7),
            core::passes::X2DataflowEdgeKind::Fallthrough);
    const auto projected_indirect_jump = core::passes::transfer_x2_register_state_for_edge(
        indirect_input, known_target_indirect_cjump("1", 7),
        core::passes::X2DataflowEdgeKind::Jump);

    require_state(projected_loop_jump, {"4"}, "x2 loop projection drops counter");
    require_state(projected_indirect_fallthrough, {"1", "4"},
                  "x2 indirect conditional fallthrough projection");
    require_state(projected_indirect_jump, {"4"},
                  "x2 indirect conditional jump drops mutated selector");
  }

  {
    const std::vector<IrOp> program = {recall("4"), known_target_indirect_jump("8", 3),
                                       halt(), label("tail"), plain(0x20, "F pi"), halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(4), {"4"}, "x2 follows stable indirect target");
    require_state(states.at(5), {"4"}, "x2 stable indirect target body");
  }

  {
    const std::vector<IrOp> program = {recall("1"), plain(0x35, "К {x}"), store("2"),
                                       known_target_indirect_cjump("8", 7),
                                       plain(0x20, "F pi"), jump_to("done"),
                                       label("target"), plain(0x20, "F pi"),
                                       label("done"), halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(4), {"1"}, "x2 stable indirect conditional fallthrough");
    require_state(states.at(7), {"1"}, "x2 stable indirect conditional target");
  }

  {
    const std::vector<IrOp> program = {recall("1"), plain(0x35, "К {x}"), store("2"),
                                       known_target_indirect_cjump("1", 7),
                                       plain(0x20, "F pi"), jump_to("done"),
                                       label("target"), plain(0x20, "F pi"),
                                       label("done"), halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(4), {"1"}, "x2 mutated indirect conditional fallthrough");
    require_state(states.at(7), {}, "x2 mutated indirect conditional target");
  }

  {
    const std::vector<IrOp> program = {recall("1"), store("2"),
                                       known_target_indirect_jump("1", 4), halt(),
                                       label("tail"), plain(0x20, "F pi"), halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(5), {"2"}, "x2 indirect jump drops only mutated selector");
  }

  {
    const std::vector<IrOp> program = {recall("1"), known_target_indirect_jump("1", 3),
                                       halt(), label("tail"), plain(0x20, "F pi"), halt()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(4), {}, "x2 indirect jump clears mutated selector");
    require_state(states.at(5), {}, "x2 mutated selector remains cleared");
  }

  {
    const std::vector<IrOp> program = {label("main"), call_to("load"), plain(0x20, "F pi"),
                                       halt(), label("load"), recall("4"), ret()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(2), {"4"}, "x2 direct return syncs returned X");
    require_state(states.at(3), {"4"}, "x2 direct return state after preserving op");
  }

  {
    const std::vector<IrOp> program = {recall("1"), plain(0x35, "К {x}"), call_to("noop"),
                                       plain(0x20, "F pi"), halt(), label("noop"), ret()};
    const auto states = core::passes::compute_x2_register_states(program);

    require_state(states.at(2), {"1"}, "x2 before unknown-X return");
    require_state(states.at(3), {}, "x2 return-time state when X is unknown");
    require_state(states.at(4), {}, "x2 after unknown-X return preserving op");
  }
}

} // namespace mkpro::tests
