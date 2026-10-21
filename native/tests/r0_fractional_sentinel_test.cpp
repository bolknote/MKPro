#include "mkpro/core/passes/r0_fractional_sentinel.hpp"
#include "mkpro/compiler.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp plain(int opcode, std::string mnemonic = "") {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = mnemonic.empty() ? std::to_string(opcode) : std::move(mnemonic);
  return op;
}

IrOp store(std::string register_name) {
  return make_store(std::move(register_name));
}

IrOp recall(std::string register_name) {
  return make_recall(std::move(register_name));
}

IrOp indirect_recall(std::string register_name) {
  IrOp op;
  op.kind = IrKind::IndirectRecall;
  op.register_name = std::move(register_name);
  op.opcode = 0xd0;
  op.meta.mnemonic = "К П->X";
  return op;
}

IrOp indirect_store(std::string register_name) {
  IrOp op;
  op.kind = IrKind::IndirectStore;
  op.register_name = std::move(register_name);
  op.opcode = 0xb0;
  op.meta.mnemonic = "К X->П";
  return op;
}

IrOp indirect_jump(std::string register_name) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = std::move(register_name);
  op.opcode = 0x80;
  op.meta.mnemonic = "К БП";
  return op;
}

IrOp numeric_jump(int target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = target;
  op.meta.mnemonic = "БП";
  return op;
}

IrOp numeric_call(int target) {
  IrOp op;
  op.kind = IrKind::Call;
  op.opcode = 0x53;
  op.target = target;
  op.meta.mnemonic = "ПП";
  return op;
}

IrOp numeric_cjump(int target) {
  IrOp op;
  op.kind = IrKind::CondJump;
  op.condition = "==0";
  op.opcode = 0x5e;
  op.target = target;
  op.meta.mnemonic = "F x=0";
  return op;
}

IrOp symbolic_cjump(std::string target) {
  IrOp op = numeric_cjump(0);
  op.target = std::move(target);
  return op;
}

IrOp label(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
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

IrOp sentinel_recall() {
  IrOp op = recall("e");
  op.meta.mnemonic = "П->X e";
  op.meta.comment = "preload const -99999999";
  return op;
}

std::vector<IrOp> fractional_r0_prefix() {
  return {plain(0x00, "0"), plain(0x0a, "."), plain(0x05, "5"), store("0")};
}

std::vector<IrOp> direct_sentinel_literal() {
  return {plain(0x09, "9"), plain(0x09, "9"), plain(0x09, "9"), plain(0x09, "9"),  plain(0x09, "9"),
          plain(0x09, "9"), plain(0x09, "9"), plain(0x09, "9"), plain(0x0b, "/-/")};
}

core::passes::PassResult run_r0_fractional_sentinel(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::r0_fractional_sentinel(ops, core::passes::PassContext{.options = options});
}

void append(std::vector<IrOp>& target, std::vector<IrOp> source) {
  target.insert(target.end(), std::make_move_iterator(source.begin()),
                std::make_move_iterator(source.end()));
}

} // namespace

void r0_fractional_sentinel_matches_typescript_contract() {
  {
    std::vector<IrOp> program = {symbolic_cjump("done")};
    append(program, direct_sentinel_literal());
    program.push_back(store("1"));
    program.push_back(label("done"));
    program.push_back(halt());

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 1,
            "r0-fractional-sentinel did not fold a zero-path sentinel literal");
    require(result.ops.size() == 6,
            "zero-path sentinel fold did not replace ten cells with three");
    require(result.ops.at(1).kind == IrKind::Store &&
                result.ops.at(1).register_name == "3" &&
                result.ops.at(2).kind == IrKind::IndirectRecall &&
                result.ops.at(2).register_name == "3" &&
                result.ops.at(3).kind == IrKind::Store &&
                result.ops.at(3).register_name == "1",
            "zero-path sentinel fold emitted the wrong underflow sequence");
    require(result.ops.at(2).meta.indirect_memory_targets &&
                *result.ops.at(2).meta.indirect_memory_targets == std::vector<int>{3},
            "the proved-zero R3 underflow records its exact self-read target");

    using namespace core;
    auto machine = lower_ir_to_machine(result.ops);
    machine.back().stop_disposition = StopDisposition::Terminal;
    const auto memory = std::find_if(machine.begin(), machine.end(), [](const auto& item) {
      return item.kind == MachineItemKind::Op && item.opcode == 0xd3;
    });
    require(memory != machine.end() && memory->indirect_memory_targets &&
                *memory->indirect_memory_targets == std::vector<int>{3},
            "lowering retains the underflow's complete memory fact");
    const auto roundtrip = raise_machine_to_ir(machine);
    const auto recalled = std::find_if(roundtrip.begin(), roundtrip.end(), [](const auto& op) {
      return op.kind == IrKind::IndirectRecall && op.opcode == 0xd3;
    });
    require(recalled != roundtrip.end() && recalled->meta.indirect_memory_targets &&
                *recalled->meta.indirect_memory_targets == std::vector<int>{3},
            "representation replay retains the same exact self-read fact");
    require(build_post_layout_control_flow(machine, {}).proved,
            "the transformed zero-path fixture has an authoritative complete CFG");
    auto incomplete = machine;
    incomplete[static_cast<std::size_t>(memory - machine.begin())].indirect_memory_targets.reset();
    require(!build_post_layout_control_flow(incomplete, {}).proved,
            "the same opcodes without their memory proof must still fail closed");

    auto source = lower_ir_to_machine(program);
    source.back().stop_disposition = StopDisposition::Terminal;
    const auto play = [](const std::vector<MachineItem>& items, const char* input) {
      const auto resolved = resolve_machine_items(items);
      require(resolved.diagnostics.empty(), "zero-path ROM fixture resolves");
      std::vector<int> code;
      for (const auto& step : resolved.steps) code.push_back(step.opcode);
      emulator::MK61 calc;
      require(calc.load_program(code).diagnostics.empty(), "zero-path fixture fits stock ROM");
      calc.set_register("1", "19").set_register("2", "23").set_register("3", "17");
      calc.input_number(input).press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
      require(calc.run_until_stable(5000, 8).stopped, "both zero and nonzero paths reach their stop");
      return std::vector<std::string>{calc.read_register("X"), calc.read_register("1"),
                                      calc.read_register("2")};
    };
    for (const char* input : {"0", "7"})
      require(play(source, input) == play(machine, input),
              "ROM preserves the exact sentinel, destination and unrelated source state");
  }

  {
    std::vector<IrOp> program = {symbolic_cjump("done")};
    append(program, direct_sentinel_literal());
    program.push_back(store("1"));
    program.push_back(recall("3"));
    program.push_back(label("done"));
    program.push_back(halt());

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 0,
            "r0-fractional-sentinel borrowed a live R3 scratch register");
  }

  {
    std::vector<IrOp> program = direct_sentinel_literal();
    program.push_back(store("1"));
    program.push_back(halt());

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 0,
            "r0-fractional-sentinel folded a literal without a proved-zero path");
  }

  {
    IrOp nonzero_guard = symbolic_cjump("done");
    nonzero_guard.condition = "!=0";
    std::vector<IrOp> program = {nonzero_guard};
    append(program, direct_sentinel_literal());
    program.push_back(store("1"));
    program.push_back(label("done"));
    program.push_back(halt());

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 0,
            "r0-fractional-sentinel treated a nonzero fallthrough as zero");
  }

  {
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(indirect_recall("0"));
    program.push_back(recall("3"));

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 1,
            "r0-fractional-sentinel did not remove redundant direct R3 recall");
    require(result.ops.back().kind == IrKind::IndirectRecall,
            "r0-fractional-sentinel removed the indirect recall instead of direct R3 recall");
  }

  {
    std::vector<IrOp> program = {plain(0x00, "0"), plain(0x0a, "."), plain(0x01, "1"),
                                 plain(0x02, "2"), store("0"),       indirect_recall("0"),
                                 recall("3")};
    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 1,
            "r0-fractional-sentinel did not recognize multi-digit fractional R0 literal");
    require(result.ops.back().kind == IrKind::IndirectRecall,
            "r0-fractional-sentinel left redundant R3 recall after multi-digit literal");
  }

  {
    std::vector<IrOp> program = {plain(0x0a, "."), plain(0x05, "5"), store("0"),
                                 indirect_recall("0"), recall("3")};
    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 1,
            "r0-fractional-sentinel did not recognize explicit leading-dot R0 literal");
  }

  {
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(indirect_recall("0"));
    program.push_back(recall("3"));
    program.push_back(recall("0"));

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 0,
            "r0-fractional-sentinel removed direct R3 recall while R0 was live");
    require(result.ops.size() == program.size(), "r0-fractional-sentinel changed live-R0 program");
  }

  {
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(indirect_store("0"));
    program.push_back(store("3"));

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 1, "r0-fractional-sentinel did not remove redundant direct R3 store");
    require(result.ops.back().kind == IrKind::IndirectStore,
            "r0-fractional-sentinel removed indirect store instead of direct R3 store");
  }

  {
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(store("1"));
    program.push_back(indirect_recall("0"));
    program.push_back(recall("3"));

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 1,
            "r0-fractional-sentinel did not preserve fractional R0 through other stores");
  }

  {
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(indirect_recall("7"));
    program.push_back(numeric_jump(99));

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 1,
            "r0-fractional-sentinel did not preserve fractional R0 through indirect memory");
    require(result.ops.back().kind == IrKind::IndirectJump && result.ops.back().opcode == 0x80 &&
                result.ops.back().register_name == "0",
            "r0-fractional-sentinel did not rewrite direct jump to fractional R0 jump");
  }

  {
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(indirect_jump("7"));
    program.push_back(numeric_jump(99));

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 0,
            "r0-fractional-sentinel ignored unrelated indirect flow proof barrier");
    require(result.ops.size() == program.size(),
            "r0-fractional-sentinel changed unrelated-indirect-flow program");
  }

  {
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(indirect_recall("0"));
    program.push_back(sentinel_recall());
    program.push_back(store("0"));
    program.push_back(recall("0"));

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 2,
            "r0-fractional-sentinel did not remove redundant sentinel store and recall");
    require(result.ops.size() == 6,
            "r0-fractional-sentinel produced wrong sentinel store/recall op count");
    require(result.ops.back().kind == IrKind::Recall && result.ops.back().register_name == "e",
            "r0-fractional-sentinel removed sentinel source recall");
  }

  {
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(indirect_store("0"));
    append(program, direct_sentinel_literal());
    program.push_back(store("0"));

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 1,
            "r0-fractional-sentinel did not recognize direct -99999999 sentinel literal");
    require(result.ops.back().kind == IrKind::Plain && result.ops.back().opcode == 0x0b,
            "r0-fractional-sentinel removed the direct sentinel literal sign");
  }

  {
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(numeric_jump(99));

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 1, "r0-fractional-sentinel did not rewrite direct jump to 99");
    require(result.ops.back().kind == IrKind::IndirectJump && result.ops.back().opcode == 0x80,
            "r0-fractional-sentinel produced wrong indirect jump");
  }

  {
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(numeric_cjump(99));
    program.push_back(halt());

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 1,
            "r0-fractional-sentinel did not rewrite direct conditional jump to 99");
    require(result.ops.at(4).kind == IrKind::IndirectCondJump && result.ops.at(4).opcode == 0xe0 &&
                result.ops.at(4).register_name == "0",
            "r0-fractional-sentinel produced wrong indirect conditional jump");
  }

  {
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(numeric_cjump(99));
    program.push_back(recall("0"));

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 0,
            "r0-fractional-sentinel rewrote conditional jump while R0 was live");
  }

  {
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(numeric_call(99));
    program.push_back(halt());
    for (int index = 0; index < 92; ++index) {
      program.push_back(plain(0x54, "КНОП"));
    }
    program.push_back(ret());

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 1, "r0-fractional-sentinel did not rewrite direct call to 99");
    require(result.ops.at(4).kind == IrKind::IndirectCall && result.ops.at(4).opcode == 0xa0,
            "r0-fractional-sentinel produced wrong indirect call");
  }

  {
    IrOp other_recall = recall("e");
    other_recall.meta.mnemonic = "П->X e";
    other_recall.meta.comment = "preload const -123";
    std::vector<IrOp> program = fractional_r0_prefix();
    program.push_back(indirect_recall("0"));
    program.push_back(other_recall);
    program.push_back(store("0"));

    const core::passes::PassResult result = run_r0_fractional_sentinel(program);

    require(result.applied == 0,
            "r0-fractional-sentinel treated non-sentinel R0 store as sentinel");
  }
}

} // namespace mkpro::tests
