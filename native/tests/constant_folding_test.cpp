#include "mkpro/core/passes/constant_folding.hpp"

#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp plain(int opcode, std::string mnemonic, bool raw = false) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
  op.meta.raw = raw;
  return op;
}

IrOp stop() {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  op.semantic = "halt";
  op.meta.mnemonic = "С/П";
  return op;
}

core::passes::PassResult run_constant_folding(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::constant_folding(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_bounded_symbolic_superoptimizer(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::bounded_symbolic_superoptimizer(
      ops, core::passes::PassContext{.options = options});
}

struct StackObservation {
  std::array<std::string, 5> values;
  std::string program_counter;
  bool stopped = false;
};

std::string describe_stack(const StackObservation& observation) {
  return "X1=" + observation.values.at(0) + ", X=" + observation.values.at(1) +
         ", Y=" + observation.values.at(2) + ", Z=" + observation.values.at(3) +
         ", T=" + observation.values.at(4);
}

StackObservation observe_stack_program(const std::vector<int>& opcodes) {
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(opcodes);
  require(loaded.diagnostics.empty(), "symbolic-superoptimizer emulator probe should load");
  calc.set_register("x1", "9.125");
  calc.set_register("x", "3.25");
  calc.set_register("y", "4.5");
  calc.set_register("z", "5.75");
  calc.set_register("t", "6.125");
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(300, 5);
  return StackObservation{
      .values = {calc.read_register("x1"), calc.read_register("x"), calc.read_register("y"),
                 calc.read_register("z"), calc.read_register("t")},
      .program_counter = calc.program_counter(),
      .stopped = run.stopped,
  };
}

} // namespace

void constant_folding_matches_typescript_contract() {
  {
    const core::passes::PassResult result =
        run_constant_folding({plain(0x00, "0"), plain(0x10, "+"), stop()});

    require(result.applied == 1, "constant-folding did not drop identity 0+ pair");
    require(result.ops.size() == 1, "constant-folding produced wrong op count for 0+");
    require(result.ops.at(0).kind == IrKind::Stop, "constant-folding dropped wrong op for 0+");
    require(result.optimizations.size() == 1, "constant-folding did not report optimization");
    require(result.optimizations.at(0).name == "constant-folding",
            "constant-folding reported wrong optimization name");
    require(result.optimizations.at(0).detail ==
                "Dropped 1 identity arithmetic operation(s) (0+ or 1*).",
            "constant-folding reported wrong optimization detail");
  }

  {
    const core::passes::PassResult result =
        run_constant_folding({plain(0x01, "1"), plain(0x12, "*"), stop()});

    require(result.applied == 1, "constant-folding did not drop identity 1* pair");
    require(result.ops.size() == 1, "constant-folding produced wrong op count for 1*");
    require(result.ops.at(0).kind == IrKind::Stop, "constant-folding dropped wrong op for 1*");
  }

  {
    const core::passes::PassResult result =
        run_constant_folding({plain(0x00, "0"), plain(0x11, "-"), stop()});

    require(result.applied == 0, "constant-folding incorrectly folded 0-");
    require(result.ops.size() == 3, "constant-folding changed non-identity subtraction");
  }

  {
    const core::passes::PassResult result =
        run_constant_folding({plain(0x01, "1"), plain(0x13, "/"), stop()});

    require(result.applied == 0, "constant-folding incorrectly folded 1/");
    require(result.ops.size() == 3, "constant-folding changed non-identity division");
  }

  {
    const core::passes::PassResult result =
        run_constant_folding({plain(0x00, "0", true), plain(0x10, "+"), stop()});

    require(result.applied == 0, "constant-folding ignored raw literal barrier");
    require(result.ops.size() == 3, "constant-folding changed raw literal sequence");
  }

  {
    const core::passes::PassResult result =
        run_constant_folding({plain(0x00, "0"), plain(0x10, "+", true), stop()});

    require(result.applied == 0, "constant-folding ignored raw arithmetic barrier");
    require(result.ops.size() == 3, "constant-folding changed raw arithmetic sequence");
  }

  {
    const core::passes::PassResult result = run_constant_folding(
        {plain(0x00, "0"), plain(0x10, "+"), plain(0x01, "1"), plain(0x12, "*"), stop()});

    require(result.applied == 2, "constant-folding did not fold both identity pairs");
    require(result.ops.size() == 1, "constant-folding produced wrong op count for two folds");
  }

  {
    const core::passes::PassResult result = run_bounded_symbolic_superoptimizer(
        {plain(0x14, "X↔Y"), plain(0x14, "X↔Y"), plain(0x14, "X↔Y"), stop()});
    require(result.applied == 1,
            "bounded symbolic superoptimizer did not replace equivalent stack region");
    require(result.ops.size() == 2,
            "bounded symbolic superoptimizer produced wrong stack-region size");
    require(result.ops.at(0).opcode == 0x14 && result.ops.at(1).kind == IrKind::Stop,
            "bounded symbolic superoptimizer selected the wrong shortest sequence");
    require(result.optimizations.size() == 1 &&
                result.optimizations.at(0).name == "bounded-symbolic-superoptimizer",
            "bounded symbolic superoptimizer did not report its rewrite");

    const StackObservation original = observe_stack_program({0x14, 0x14, 0x14, 0x50});
    const StackObservation optimized = observe_stack_program({0x14, 0x50});
    require(original.stopped && optimized.stopped,
            "symbolic-superoptimizer direct emulator probes should stop");
    require(original.values == optimized.values,
            "symbolic-superoptimizer rewrite must preserve X/Y/Z/T/X1: original " +
                describe_stack(original) + "; optimized " + describe_stack(optimized));

    const StackObservation original_x2 =
        observe_stack_program({0x0e, 0x14, 0x14, 0x14, 0x54, 0x0a, 0x50});
    const StackObservation optimized_x2 =
        observe_stack_program({0x0e, 0x14, 0x54, 0x0a, 0x50});
    require(original_x2.stopped && optimized_x2.stopped &&
                original_x2.values == optimized_x2.values,
            "symbolic-superoptimizer rewrite must preserve hidden X2 restored through dot");

    const StackObservation original_call =
        observe_stack_program({0x53, 0x03, 0x50, 0x14, 0x14, 0x14, 0x52});
    const StackObservation optimized_call =
        observe_stack_program({0x53, 0x03, 0x50, 0x14, 0x52});
    require(original_call.stopped && optimized_call.stopped &&
                original_call.program_counter == optimized_call.program_counter,
            "symbolic-superoptimizer rewrite must preserve subroutine return-stack flow");
    require(original_call.values == optimized_call.values,
            "symbolic-superoptimizer subroutine rewrite must preserve stack state");
  }

  {
    const core::passes::PassResult result = run_bounded_symbolic_superoptimizer(
        {plain(0x0d, "Cx"), plain(0x0d, "Cx"), plain(0x0d, "Cx"), stop()});
    require(result.applied == 1 && result.ops.size() == 3 &&
                result.ops.at(0).opcode == 0x0d && result.ops.at(1).opcode == 0x0d &&
                result.ops.at(2).kind == IrKind::Stop,
            "bounded symbolic superoptimizer did not shorten repeated exact clear state");
  }

  {
    const core::passes::PassResult result = run_bounded_symbolic_superoptimizer(
        {plain(0x14, "X↔Y"), plain(0x14, "X↔Y"), plain(0x14, "X↔Y"),
         plain(0x01, "1"), stop()});
    require(result.applied == 0 && result.ops.size() == 5,
            "bounded symbolic superoptimizer crossed a previous-command-sensitive digit");
  }

  {
    const core::passes::PassResult result = run_bounded_symbolic_superoptimizer(
        {plain(0x14, "X↔Y", true), plain(0x14, "X↔Y"), plain(0x14, "X↔Y"), stop()});
    require(result.applied == 0 && result.ops.size() == 4,
            "bounded symbolic superoptimizer crossed a raw rewrite barrier");
  }
}

} // namespace mkpro::tests
