#include "mkpro/core/passes/dead_store_elimination.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp label(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
  return op;
}

IrOp store(std::string register_name) {
  IrOp op;
  op.kind = IrKind::Store;
  op.register_name = std::move(register_name);
  op.opcode = 0x40;
  op.meta.mnemonic = "X->П";
  return op;
}

IrOp entered_store(std::string register_name, ManualInteractionAnchorKind kind) {
  IrOp op = store(std::move(register_name));
  op.meta.manual_interaction = ManualInteractionAnchor{
      .protocol_id = 7,
      .phase = 1,
      .kind = kind,
  };
  return op;
}

IrOp recall(std::string register_name) {
  IrOp op;
  op.kind = IrKind::Recall;
  op.register_name = std::move(register_name);
  op.opcode = 0x60;
  op.meta.mnemonic = "П->X";
  return op;
}

IrOp plain(int opcode, std::string mnemonic) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
  return op;
}

IrOp call_to(std::string target) {
  IrOp op;
  op.kind = IrKind::Call;
  op.opcode = 0x53;
  op.target = std::move(target);
  op.meta.mnemonic = "ПП";
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

IrOp known_target_indirect_jump(std::string selector, int target) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = std::move(selector);
  op.opcode = 0x80;
  op.meta.mnemonic = "К БП";
  op.meta.comment = "indirect-target=" + std::to_string(target);
  return op;
}

IrOp unknown_target_indirect_jump(std::string selector) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = std::move(selector);
  op.opcode = 0x80;
  op.meta.mnemonic = "К БП";
  return op;
}

IrOp known_target_indirect_call(std::string selector, int target) {
  IrOp op;
  op.kind = IrKind::IndirectCall;
  op.register_name = std::move(selector);
  op.opcode = 0xa0;
  op.meta.mnemonic = "К ПП";
  op.meta.comment = "indirect-target=" + std::to_string(target);
  return op;
}

IrOp known_target_indirect_store(std::string selector, std::string target) {
  IrOp op;
  op.kind = IrKind::IndirectStore;
  op.register_name = std::move(selector);
  op.opcode = 0xb0;
  op.meta.mnemonic = "К X->П";
  op.meta.comment = "indirect-memory-target=" + target;
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

core::passes::PassResult run_dead_store_elimination(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::dead_store_elimination(ops, core::passes::PassContext{.options = options});
}

int store_count(const std::vector<IrOp>& ops) {
  return static_cast<int>(std::count_if(ops.begin(), ops.end(), [](const IrOp& op) {
    return op.kind == IrKind::Store || op.kind == IrKind::IndirectStore;
  }));
}

} // namespace

void dead_store_elimination_matches_typescript_contract() {
  {
    const core::passes::PassResult result = run_dead_store_elimination(
        {store("1"), plain(0x0d, "Cx"), store("1"), recall("1"), halt()});
    require(result.applied == 1, "dead-store-elimination did not remove overwritten store");
    require(store_count(result.ops) == 1,
            "dead-store-elimination left wrong store count after overwrite");
  }

  {
    IrOp raw = plain(0x0d, "raw");
    raw.meta.raw = true;
    const std::vector<IrOp> program = {store("1"), raw, store("1"), recall("1"), halt()};
    const core::passes::PassResult result = run_dead_store_elimination(program);
    require(result.applied == 1 && store_count(result.ops) == 1,
            "known raw opcode was incorrectly treated as an all-register memory barrier");
  }

  {
    IrOp raw_recall = recall("0");
    raw_recall.opcode = 0x6f;
    raw_recall.meta.raw = true;
    const std::vector<IrOp> program = {store("0"), raw_recall, store("0"), halt()};
    const core::passes::PassResult result = run_dead_store_elimination(program);
    require(result.applied == 1 && result.ops.front().kind == IrKind::Store,
            "raw R0 recall alias did not preserve the reaching R0 store");
  }

  {
    const core::passes::PassResult result =
        run_dead_store_elimination({known_target_indirect_store("8", "2"), halt()});
    require(result.applied == 1,
            "dead-store-elimination did not remove stable indirect dead store");
    require(result.ops.size() == 1 && result.ops.at(0).kind == IrKind::Stop,
            "dead-store-elimination stable indirect result mismatch");
  }

  {
    const std::vector<IrOp> program = {known_target_indirect_store("4", "2"), halt()};
    const core::passes::PassResult result = run_dead_store_elimination(program);
    require(result.applied == 0, "dead-store-elimination removed mutating indirect dead store");
    require(result.ops.size() == program.size(),
            "dead-store-elimination changed mutating indirect program");
  }

  {
    const core::passes::PassResult result =
        run_dead_store_elimination({store("1"), plain(0x0d, "Cx"), recall("1"), halt()});
    require(result.applied == 0, "dead-store-elimination removed store read later");
  }

  {
    const core::passes::PassResult result =
        run_dead_store_elimination({store("1"), halt(), recall("1"), halt()});
    require(result.applied == 0, "dead-store-elimination ignored halt/resume liveness edge");
  }

  {
    const core::passes::PassResult result = run_dead_store_elimination(
        {store("8"), numeric_call(4), store("8"), halt(), recall("8"), ret()});
    require(result.ops.at(0).kind == IrKind::Store,
            "dead-store-elimination missed numeric call callee read");
  }

  {
    const std::vector<IrOp> program = {store("1"),  known_target_indirect_jump("8", 3),
                                       halt(),      label("target"),
                                       recall("1"), halt()};
    const core::passes::PassResult result = run_dead_store_elimination(program);
    require(result.applied == 0, "dead-store-elimination missed proved indirect jump read");
  }

  {
    const std::vector<IrOp> program = {store("1"),  unknown_target_indirect_jump("8"),
                                       halt(),      label("target"),
                                       recall("1"), halt()};
    const core::passes::PassResult result = run_dead_store_elimination(program);
    require(result.applied == 0,
            "dead-store-elimination removed store before unknown indirect jump");
  }

  {
    const std::vector<IrOp> program = {store("1"),  known_target_indirect_call("8", 3),
                                       halt(),      label("callee"),
                                       recall("1"), ret()};
    const core::passes::PassResult result = run_dead_store_elimination(program);
    require(result.applied == 0, "dead-store-elimination missed proved indirect call read");
  }

  {
    const core::passes::PassResult result = run_dead_store_elimination(
        {label("main"), call_to("bump"), recall("1"), halt(), label("bump"), store("1"), ret()});
    require(result.applied == 0,
            "dead-store-elimination removed subroutine store read after return");
  }

  {
    const std::vector<IrOp> program = {plain(0x20, "F pi"), plain(0x35, "К {x}"), store("1"),
                                       plain(0x0c, "ВП"), halt()};
    const core::passes::PassResult result = run_dead_store_elimination(program);
    require(result.applied == 0,
            "dead-store-elimination removed store that provides VP restore context");
  }

  {
    const core::passes::PassResult result = run_dead_store_elimination(
        {entered_store("1", ManualInteractionAnchorKind::ContinuousResume), recall("2"), halt()});
    require(result.applied == 1 && result.ops.front().kind == IrKind::Recall &&
                result.ops.front().meta.manual_interaction.has_value() &&
                result.ops.front().meta.manual_interaction->kind ==
                    ManualInteractionAnchorKind::ContinuousResume,
            "dead continuous-resume store should transfer its entry anchor forward");
  }

  {
    const core::passes::PassResult result = run_dead_store_elimination(
        {entered_store("1", ManualInteractionAnchorKind::SingleStepCommand), halt()});
    require(result.applied == 0 && result.ops.front().kind == IrKind::Store,
            "PP single-step store is an externally executed command and must remain");
  }

  {
    const core::passes::PassResult result = run_dead_store_elimination(
        {entered_store("1", ManualInteractionAnchorKind::ContinuousResume)});
    require(result.applied == 0 && result.ops.front().kind == IrKind::Store,
            "continuous-resume anchor must not wrap at the pre-layout IR boundary");
  }

  {
    IrOp address;
    address.kind = IrKind::OrphanAddress;
    address.target = 42;
    const core::passes::PassResult result = run_dead_store_elimination(
        {entered_store("1", ManualInteractionAnchorKind::ContinuousResume), address, halt()});
    require(result.applied == 0 && result.ops.front().kind == IrKind::Store,
            "continuous-resume anchor must not move onto or across an address operand");
  }
}

} // namespace mkpro::tests
