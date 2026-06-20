#include "mkpro/core/passes/duplicate_failure_tail.hpp"

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

IrOp protected_label(std::string name) {
  IrOp op = label(std::move(name));
  op.procedure_boundary = "entry";
  return op;
}

IrOp pause(bool raw = false) {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  op.semantic = "halt";
  op.meta.mnemonic = "С/П";
  op.meta.raw = raw;
  return op;
}

IrOp plain(int opcode, std::string mnemonic, bool raw = false) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
  op.meta.raw = raw;
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

IrOp jump(std::string target, bool raw = false) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = std::move(target);
  op.meta.mnemonic = "БП";
  op.meta.raw = raw;
  return op;
}

IrOp cjump(std::string target) {
  IrOp op;
  op.kind = IrKind::CondJump;
  op.condition = "==0";
  op.opcode = 0x57;
  op.target = std::move(target);
  op.meta.mnemonic = "F x=0";
  op.target_meta.comment = "kept target meta";
  op.target_meta.source_line = 42;
  op.target_meta.roles = {"branch-target"};
  op.target_meta.formal_opcode = 0x57;
  return op;
}

IrOp indirect_jump(std::string selector, bool raw = false) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = std::move(selector);
  op.opcode = 0x80;
  op.meta.mnemonic = "К БП";
  op.meta.raw = raw;
  return op;
}

int machine_cell_count(const std::vector<IrOp>& ops) {
  int count = 0;
  for (const IrOp& op : ops)
    count += core::passes::cells_per_op(op);
  return count;
}

bool has_label(const std::vector<IrOp>& ops, const std::string& name) {
  return std::any_of(ops.begin(), ops.end(),
                     [&](const IrOp& op) { return op.kind == IrKind::Label && op.name == name; });
}

core::passes::PassResult run_duplicate_failure_tail(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::duplicate_failure_tail(ops, core::passes::PassContext{.options = options});
}

} // namespace

void duplicate_failure_tail_matches_typescript_contract() {
  {
    const std::vector<IrOp> program = {
        label("first_pause"),  pause(), label("first_continue"),  indirect_jump("8"),
        label("second_pause"), pause(), label("second_continue"), indirect_jump("8"),
    };
    const core::passes::PassResult result = run_duplicate_failure_tail(program);

    require(result.applied == 1, "duplicate-failure-tail did not merge adjacent pause tails");
    require(result.optimizations.size() == 1, "duplicate-failure-tail did not report optimization");
    require(result.optimizations.at(0).name == "duplicate-failure-tail-merge",
            "duplicate-failure-tail reported wrong optimization name");
    require(result.optimizations.at(0).detail.find("pause-only") != std::string::npos,
            "duplicate-failure-tail detail did not mention pause-only tails");
    require(!has_label(result.ops, "first_pause"),
            "duplicate-failure-tail kept removed first_pause label");
    require(!has_label(result.ops, "first_continue"),
            "duplicate-failure-tail kept removed first_continue label");
    require(machine_cell_count(result.ops) == machine_cell_count(program) - 2,
            "duplicate-failure-tail removed wrong number of cells");
  }

  {
    const std::vector<IrOp> program = {
        label("first_pause"),
        pause(),
        indirect_jump("8"),
        label("middle"),
        recall("1"),
        jump("after_middle"),
        label("second_pause"),
        pause(),
        indirect_jump("8"),
        label("after_middle"),
        recall("2"),
        label("fallthrough_pause"),
        pause(),
        indirect_jump("8"),
    };
    const core::passes::PassResult result = run_duplicate_failure_tail(program);

    require(result.applied == 1,
            "duplicate-failure-tail did not merge separated no-fallthrough tail");
    require(!has_label(result.ops, "second_pause"),
            "duplicate-failure-tail kept removable separated tail");
    require(has_label(result.ops, "fallthrough_pause"),
            "duplicate-failure-tail removed fallthrough-reachable tail");
    require(machine_cell_count(result.ops) == machine_cell_count(program) - 2,
            "duplicate-failure-tail separated tail cell count mismatch");
  }

  {
    const std::vector<IrOp> program = {
        label("first_pause"),
        label("first_alias"),
        pause(),
        indirect_jump("8"),
        label("middle"),
        recall("1"),
        jump("after_middle"),
        label("second_pause"),
        label("second_alias"),
        pause(),
        indirect_jump("8"),
        label("after_middle"),
        recall("2"),
    };
    const core::passes::PassResult result = run_duplicate_failure_tail(program);

    require(result.applied == 1,
            "duplicate-failure-tail did not merge separated tail behind label run");
    require(!has_label(result.ops, "second_pause"),
            "duplicate-failure-tail kept second_pause label-run label");
    require(!has_label(result.ops, "second_alias"),
            "duplicate-failure-tail kept second_alias label-run label");
    require(machine_cell_count(result.ops) == machine_cell_count(program) - 2,
            "duplicate-failure-tail label-run cell count mismatch");
  }

  {
    const std::vector<IrOp> program = {
        cjump("first_pause"), label("first_pause"),  pause(),
        indirect_jump("8"),   label("second_pause"), pause(),
        indirect_jump("8"),
    };
    const core::passes::PassResult result = run_duplicate_failure_tail(program);

    require(result.applied == 1, "duplicate-failure-tail did not rewrite branch target");
    require(std::get<std::string>(result.ops.at(0).target) == "second_pause",
            "duplicate-failure-tail rewrote branch target incorrectly");
    require(result.ops.at(0).target_meta.comment == "kept target meta",
            "duplicate-failure-tail did not preserve target comment metadata");
    require(result.ops.at(0).target_meta.source_line == 42,
            "duplicate-failure-tail did not preserve target source metadata");
    require(result.ops.at(0).target_meta.roles.size() == 1,
            "duplicate-failure-tail did not preserve target roles metadata");
    require(result.ops.at(0).target_meta.formal_opcode == 0x57,
            "duplicate-failure-tail did not preserve target formal opcode metadata");
  }

  {
    const std::vector<IrOp> program = {
        protected_label("first_pause"), pause(), indirect_jump("8"),
        label("second_pause"),          pause(), indirect_jump("8"),
    };
    const core::passes::PassResult result = run_duplicate_failure_tail(program);

    require(result.applied == 0, "duplicate-failure-tail removed procedure-boundary label");
    require(result.ops.size() == program.size(),
            "duplicate-failure-tail changed protected-label program");
  }

  {
    const std::vector<IrOp> program = {
        label("first_pause"),  pause(true), indirect_jump("8"),
        label("second_pause"), pause(),     indirect_jump("8"),
    };
    const core::passes::PassResult result = run_duplicate_failure_tail(program);

    require(result.applied == 0, "duplicate-failure-tail ignored raw pause barrier");
    require(result.ops.size() == program.size(), "duplicate-failure-tail changed raw program");
  }

  {
    const std::vector<IrOp> program = {
        label("first"),  plain(0x00, "0"), pause(), label("trampoline"), jump("end"),
        label("second"), plain(0x00, "0"), pause(), label("end"),
    };
    const core::passes::PassResult result = run_duplicate_failure_tail(program);

    require(result.applied == 1, "duplicate-failure-tail did not merge zero-pause trampoline tail");
    require(!has_label(result.ops, "first"), "duplicate-failure-tail kept first zero-pause label");
    require(!has_label(result.ops, "trampoline"), "duplicate-failure-tail kept trampoline label");
  }
}

} // namespace mkpro::tests
