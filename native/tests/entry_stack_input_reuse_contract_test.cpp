#include "mkpro/core/passes/entry_stack_input_reuse.hpp"

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp store(std::string register_name) {
  return make_store(std::move(register_name));
}

IrOp recall(std::string register_name) {
  return make_recall(std::move(register_name));
}

IrOp label(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
  return op;
}

IrOp plain(int opcode, std::string mnemonic) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
  return op;
}

IrOp call(std::string target) {
  IrOp op;
  op.kind = IrKind::Call;
  op.opcode = 0x53;
  op.target = std::move(target);
  op.meta.mnemonic = "ПП";
  return op;
}

IrOp jump(std::string target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = std::move(target);
  op.meta.mnemonic = "БП";
  return op;
}

IrOp ret() {
  IrOp op;
  op.kind = IrKind::Return;
  op.opcode = 0x52;
  op.semantic = "return";
  op.meta.mnemonic = "В/О";
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

core::passes::PassResult run_entry_stack_reuse(const std::vector<IrOp>& ops) {
  return core::passes::entry_stack_input_reuse(
      ops, core::passes::PassContext{.options = CompileOptions{}});
}

int cell_count(const std::vector<IrOp>& ops) {
  int cells = 0;
  for (const IrOp& op : ops)
    cells += core::passes::cells_per_op(op);
  return cells;
}

std::string compact(std::string value) {
  std::string result;
  for (const char ch : value) {
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
      result.push_back(ch);
  }
  return result;
}

} // namespace

void entry_stack_input_reuse_preserves_direct_call_contract() {
  {
    emulator::MK61 calc;
    const emulator::ProgramLoadResult loaded =
        calc.load_program({0x53, 0x03, 0x50, 0x52});
    require(loaded.diagnostics.empty(), "direct-call stack fact should load");
    calc.set_register("X", "11");
    calc.set_register("Y", "22");
    calc.set_register("Z", "33");
    calc.set_register("T", "44");
    calc.press_sequence({"В/О", "С/П"});
    const emulator::RunResult run = calc.run_until_stable(100, 5);
    require(run.stopped && calc.program_counter() == "03",
            "ПП followed by В/О should return to the caller stop");
    require(compact(calc.read_register("X")) == "11," &&
                compact(calc.read_register("Y")) == "22," &&
                compact(calc.read_register("Z")) == "33," &&
                compact(calc.read_register("T")) == "44,",
            "ПП/В/О should preserve the visible X/Y/Z/T data stack");
  }

  {
    const std::vector<IrOp> input = {
        recall("4"),
        recall("5"),
        call("helper"),
        recall("4"),
        recall("5"),
        call("helper"),
        jump("end"),
        label("helper"),
        plain(0x14, "X↔Y"),
        recall("4"),
        store("6"),
        ret(),
        label("end"),
        halt(),
    };
    const core::passes::PassResult result = run_entry_stack_reuse(input);
    require(result.applied == 1,
            "uniform multi-caller Y fact should remove one helper recall");
    require(cell_count(input) == 15 && cell_count(result.ops) == 14,
            "entry-stack reuse size baseline should be exactly 15->14 cells");
  }

  {
    const core::passes::PassResult result = run_entry_stack_reuse({
        recall("4"),
        recall("5"),
        call("helper"),
        recall("6"),
        recall("5"),
        call("helper"),
        jump("end"),
        label("helper"),
        plain(0x14, "X↔Y"),
        recall("4"),
        store("6"),
        ret(),
        label("end"),
        halt(),
    });
    require(result.applied == 0,
            "different caller Y values must intersect to unknown at the callee entry");
  }

  {
    const core::passes::PassResult result = run_entry_stack_reuse({
        recall("4"),
        recall("5"),
        call("helper"),
        plain(0x10, "+"),
        halt(),
        label("helper"),
        plain(0x14, "X↔Y"),
        recall("4"),
        store("6"),
        ret(),
    });
    require(result.applied == 0,
            "caller continuation consuming the recall lift must keep the helper recall");
  }

  {
    const core::passes::PassResult result = run_entry_stack_reuse({
        recall("4"),
        recall("5"),
        call("helper"),
        halt(),
        label("helper"),
        recall("0"),
        recall("4"),
        store("6"),
        ret(),
    });
    require(result.applied == 0,
            "an unbalanced helper lift must not leave the caller Y fact in X");
  }
}

} // namespace mkpro::tests
