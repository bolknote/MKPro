#include "mkpro/core/passes/r0_fractional_sentinel.hpp"

#include "test_support.hpp"

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
