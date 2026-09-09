#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/register_allocator.hpp"
#include "mkpro/core/passes/branch_target_x_reuse.hpp"
#include "mkpro/core/passes/conditional_branch_trampoline.hpp"
#include "mkpro/core/passes/flow_x_reuse.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/passes/index.hpp"
#include "mkpro/core/passes/indirect_addressing.hpp"
#include "mkpro/core/passes/jump_thread.hpp"
#include "mkpro/core/passes/jump_to_next.hpp"
#include "mkpro/core/passes/pre_shift_stack_lift.hpp"
#include "mkpro/core/passes/preloaded_indirect_flow.hpp"
#include "mkpro/core/passes/redundant_literal_reload.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "mkpro/core/passes/redundant_prologue.hpp"
#include "mkpro/core/passes/return_suffix_gadget.hpp"
#include "mkpro/core/passes/return_trampoline.hpp"
#include "mkpro/core/passes/return_zero_jump.hpp"
#include "mkpro/core/passes/shared_call_tail.hpp"
#include "mkpro/core/passes/shared_straight_line_helper.hpp"
#include "mkpro/core/passes/shared_terminal_tail.hpp"
#include "mkpro/core/passes/store_recall_peephole.hpp"
#include "mkpro/core/passes/tail_branch_inversion.hpp"
#include "mkpro/core/passes/tail_call.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <optional>
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

IrOp proc_start(std::string name) {
  IrOp op = label(std::move(name));
  op.procedure_boundary = "start";
  return op;
}

IrOp jump_to(std::string target, bool raw = false) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = std::move(target);
  op.meta.mnemonic = "БП";
  op.meta.raw = raw;
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

IrOp numeric_cjump(int target) {
  IrOp op;
  op.kind = IrKind::CondJump;
  op.condition = "==0";
  op.opcode = 0x57;
  op.target = target;
  op.meta.mnemonic = "F x=0";
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

IrOp cjump_to(std::string condition, int opcode, std::string target) {
  IrOp op;
  op.kind = IrKind::CondJump;
  op.condition = std::move(condition);
  op.opcode = opcode;
  op.target = std::move(target);
  op.meta.mnemonic = "F x=0";
  op.meta.comment = "false branch";
  return op;
}

IrOp plain(int opcode = 0x10) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = "+";
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

IrOp known_target_indirect_recall(std::string selector, std::string target) {
  IrOp op;
  op.kind = IrKind::IndirectRecall;
  op.register_name = std::move(selector);
  op.opcode = 0xd0 + register_index(op.register_name);
  op.meta.mnemonic = "К П->X";
  op.meta.comment = "indirect-memory-target=" + target;
  return op;
}

IrOp known_target_indirect_store(std::string selector, std::string target) {
  IrOp op;
  op.kind = IrKind::IndirectStore;
  op.register_name = std::move(selector);
  op.opcode = 0xb0 + register_index(op.register_name);
  op.meta.mnemonic = "К X->П";
  op.meta.comment = "indirect-memory-target=" + target;
  return op;
}

IrOp indirect_jump(std::string selector) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = std::move(selector);
  op.opcode = 0x80 + register_index(op.register_name);
  op.meta.mnemonic = "К БП";
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

IrOp known_target_indirect_call(std::string selector, int target) {
  IrOp op;
  op.kind = IrKind::IndirectCall;
  op.register_name = std::move(selector);
  op.opcode = 0xa0 + register_index(op.register_name);
  op.meta.mnemonic = "К ПП";
  op.meta.comment = "indirect-target=" + std::to_string(target);
  return op;
}

IrOp known_target_indirect_cjump(std::string selector, int target) {
  IrOp op;
  op.kind = IrKind::IndirectCondJump;
  op.register_name = std::move(selector);
  op.opcode = 0x90 + register_index(op.register_name);
  op.meta.mnemonic = "К x>=0";
  op.meta.comment = "indirect-target=" + std::to_string(target);
  return op;
}

IrOp stop(std::string semantic) {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  op.semantic = std::move(semantic);
  op.meta.mnemonic = "С/П";
  return op;
}

IrOp ret() {
  IrOp op;
  op.kind = IrKind::Return;
  op.opcode = 0x52;
  op.meta.mnemonic = "В/О";
  op.meta.comment = "implicit return from proc";
  return op;
}

int machine_cell_count(const std::vector<IrOp>& ops) {
  int cells = 0;
  for (const IrOp& op : ops)
    cells += core::passes::cells_per_op(op);
  return cells;
}

core::passes::PassResult run_jump_to_next(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::jump_to_next_threading(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_jump_thread(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::jump_thread(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_flow_x_reuse(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::flow_x_reuse(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_branch_target_x_reuse(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::branch_target_x_reuse(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_stable_indirect_flow(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::stable_indirect_flow(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_indirect_memory_table(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::indirect_memory_table(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_preloaded_indirect_flow(const std::vector<IrOp>& ops) {
  CompileOptions options;
  options.preloaded_indirect_flow = true;
  return core::passes::preloaded_indirect_flow(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_preloaded_indirect_flow(const std::vector<IrOp>& ops,
                                                     FeatureProfile feature_profile) {
  CompileOptions options;
  options.preloaded_indirect_flow = true;
  options.feature_profile = feature_profile;
  return core::passes::preloaded_indirect_flow(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_runtime_indirect_call_flow(const std::vector<IrOp>& ops) {
  CompileOptions options;
  options.runtime_indirect_call_flow = true;
  return core::passes::runtime_indirect_call_flow(ops,
                                                  core::passes::PassContext{.options = options});
}

core::passes::PassResult run_tail_call(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::tail_call_lowering(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_tail_branch_inversion(const std::vector<IrOp>& ops, bool enabled) {
  CompileOptions options;
  options.tail_branch_inversion = enabled;
  return core::passes::tail_branch_inversion(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_conditional_branch_trampoline(const std::vector<IrOp>& ops,
                                                           bool enabled) {
  CompileOptions options;
  options.conditional_branch_trampoline = enabled;
  return core::passes::conditional_branch_trampoline(ops,
                                                     core::passes::PassContext{.options = options});
}

core::passes::PassResult run_redundant_prologue(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::redundant_prologue_elimination(
      ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_shared_terminal_tail(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::shared_terminal_tail(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_shared_call_tail(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::shared_call_tail(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_return_suffix_gadget(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::return_suffix_gadget(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_shared_straight_line_helper(const std::vector<IrOp>& ops,
                                                         bool allow_direct_calls = false) {
  CompileOptions options;
  options.shared_straight_line_call_bodies = allow_direct_calls;
  return core::passes::shared_straight_line_helper(ops,
                                                   core::passes::PassContext{.options = options});
}

core::passes::PassResult run_return_zero_jump(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::return_zero_jump(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_return_trampoline(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::return_trampoline(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_store_recall_peephole(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::store_recall_peephole(ops, core::passes::PassContext{.options = options});
}

core::passes::PassResult run_pre_shift_stack_lift(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::pre_shift_stack_lift(ops, core::passes::PassContext{.options = options});
}

} // namespace

void indirect_flow_target_marker_requires_strict_boundary() {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = "7";
  op.opcode = 0xb7;

  op.meta.comment = "preloaded R7=42 indirect-target=12";
  std::optional<int> target = core::passes::known_indirect_flow_target(op);
  require(target.has_value() && *target == 12,
          "indirect-target marker should accept an end-of-comment boundary");

  op.meta.comment = "preloaded R7=42 indirect-target=12; verified";
  target = core::passes::known_indirect_flow_target(op);
  require(target.has_value() && *target == 12,
          "indirect-target marker should accept a semicolon boundary");

  op.meta.comment = "preloaded R7=42 indirect-target=12garbage";
  require(!core::passes::known_indirect_flow_target(op).has_value(),
          "indirect-target marker should reject alphabetic suffixes");

  op.meta.comment = "preloaded R7=42 indirect-target=12.5";
  require(!core::passes::known_indirect_flow_target(op).has_value(),
          "indirect-target marker should reject decimal-looking suffixes");

  op.meta.comment = "preloaded R7=42 indirect-target=abc";
  require(!core::passes::known_indirect_flow_target(op).has_value(),
          "indirect-target marker should reject missing numeric targets");

  op.meta.comment = "preloaded R7=42 indirect-target=105";
  require(!core::passes::known_indirect_flow_target(op).has_value(),
          "indirect-target marker should reject targets outside official cells");
  target = core::passes::known_indirect_flow_target(op, AddressSpaceModel::Mk61SMiniExpanded);
  require(target.has_value() && *target == 105,
          "expanded indirect-target marker should accept added official cells");
}

void pass_pipeline_matches_initial_typescript_contract() {
  {
    IrOp terminal = stop("halt");
    terminal.meta.stop_disposition = StopDisposition::Terminal;
    const auto observe = [](const std::vector<IrOp>& program, const std::string& input = "23") {
      const auto resolved = resolve_machine_items(lower_ir_to_machine(program));
      require(resolved.diagnostics.empty(), "stable-selector fixture must resolve");
      std::vector<int> codes;
      for (const auto& step : resolved.steps)
        codes.push_back(step.opcode);
      // Read selectors through ROM instructions, not the debugger's padded
      // raw-register spelling after an indirect write-back.
      for (const int code : {0x67, 0x50, 0x68, 0x50})
        codes.push_back(code);
      emulator::MK61 calc;
      require(calc.load_program(codes).diagnostics.empty(), "stable-selector fixture must load");
      calc.set_register("0", "47.25").set_register("1", "11").set_register("2", input)
          .set_register("c", "61").set_register("x", "19").set_register("y", "29")
          .set_register("z", "31").set_register("t", "37").set_register("x1", "41");
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(600, 5).stopped, "stable-selector fixture must stop");
      std::vector<std::string> state;
      for (const auto* reg : {"x", "y", "z", "t", "x1"})
        state.push_back(calc.read_register(reg));
      calc.press(".");
      state.push_back(calc.display_text());
      for (int reg = 7; reg <= 8; ++reg) {
        calc.press("С/П");
        require(calc.run_until_stable(600, 5).stopped,
                "selector readback must stop through the real ROM recall");
        state.push_back(calc.display_text());
      }
      calc.press("ВП");
      state.push_back(calc.display_text());
      return state;
    };

    // A store or recall terminates the entry of 1. The following 2 is not 12.
    for (const bool through_recall : {false, true}) {
      std::vector<IrOp> program = {plain(1), store("7")};
      if (through_recall)
        program.push_back(recall("7"));
      const std::vector<IrOp> suffix = {plain(2), store("8"), recall("c"), terminal};
      program.insert(program.end(), suffix.begin(), suffix.end());
      const auto kept = run_indirect_memory_table(program);
      require(kept.applied == 0 && observe(program) == observe(kept.ops),
              "separate literals must not invent a selector for Rc");
      program.at(program.size() - 2U) = recall("2");
      const auto shorter = run_indirect_memory_table(program);
      require(shorter.applied == 1 && observe(program) == observe(shorter.ops),
              "the actual selector value 2 must retain a proved equivalent memory rewrite");
      const auto& access = shorter.ops.at(shorter.ops.size() - 2U);
      require(access.meta.indirect_memory_targets == std::optional(std::vector<int>{2}),
              "memory rewrite must publish its complete physical target set");
    }

    const std::vector<IrOp> split_literal = {
        plain(1), label("middle"), plain(2), store("7"), recall("2"), terminal};
    const auto no_split = run_indirect_memory_table(split_literal);
    require(no_split.applied == 0 && observe(split_literal) == observe(no_split.ops),
            "a label is not a hardware number-entry delimiter");

    IrOp selector_store = store("7");
    selector_store.meta.logical_register_name = "selector";
    IrOp data_recall = recall("2");
    data_recall.meta.logical_register_name = "data";
    const std::vector<IrOp> logical = {plain(2), selector_store, data_recall, terminal};
    const auto identities = run_indirect_memory_table(logical);
    require(identities.applied == 1 &&
                identities.ops.at(2).meta.logical_register_name == "selector" &&
                identities.ops.at(2).meta.logical_indirect_memory_targets ==
                    std::optional(std::vector<std::string>{"data"}) &&
                observe(logical) == observe(identities.ops),
            "selector and accessed logical value must remain distinct after indirect lowering");
    auto incomplete = logical;
    incomplete.at(1).meta.logical_register_name.reset();
    require(run_indirect_memory_table(incomplete).applied == 0,
            "incomplete logical identities must not be guessed from a physical register");

    const std::vector<IrOp> forward = {
        plain(7), store("7"), numeric_jump(7), plain(1), terminal,
        plain(9), recall("0"), terminal};
    const auto pinned = run_stable_indirect_flow(forward);
    require(pinned.applied == 0 && observe(forward) == observe(pinned.ops),
            "a forward literal selector cannot outlive deletion of the preceding address cell");

    const std::vector<IrOp> backward = {
        jump_to("entry"), label("target"), recall("0"), terminal,
        label("entry"), plain(2), store("7"), numeric_jump(2)};
    const auto folded = run_stable_indirect_flow(backward);
    require(folded.applied == 1 &&
                machine_cell_count(folded.ops) == machine_cell_count(backward) - 1 &&
                observe(backward) == observe(folded.ops),
            "a proved stationary selector must retain its one-cell backward jump saving");
    IrOp conditional = cjump_to("==0", 0x5e, "target");
    conditional.target = 2;
    const std::vector<IrOp> conditional_program = {
        jump_to("entry"), label("target"), recall("0"), recall("1"), terminal,
        label("entry"), plain(2), store("7"), recall("2"), conditional,
        recall("0"), recall("1"), terminal};
    const auto conditional_fold = run_stable_indirect_flow(conditional_program);
    require(conditional_fold.applied == 1,
            "a proved conditional must reuse the stable selector after X2 convergence");
    for (const auto* input : {"0", "23"})
      require(observe(conditional_program, input) == observe(conditional_fold.ops, input),
              "both conditional outcomes must preserve full stack and hidden entry state");
  }

  {
    CompileOptions options;
    options.late_literal_preloads = true;
    std::set<std::string> reserved;
    for (int index = 0; index < 15; ++index)
      reserved.insert(core::register_name_for_index(index));
    reserved.erase("a");
    const auto run = [&](const std::vector<IrOp>& ops,
                          const std::set<std::string>& unavailable) {
      return core::passes::late_literal_preloads(
          ops, core::passes::PassContext{.options = options}, unavailable);
    };
    IrOp terminal = stop("halt");
    terminal.meta.stop_disposition = StopDisposition::Terminal;
    const std::vector<IrOp> original = {
        recall("0"), store("0"), plain(1), plain(2), plain(3), plain(4),
        plain(5), plain(6), plain(7), plain(8), plain(0x10),
        recall("1"), plain(0x10), terminal,
    };
    const auto pooled = run(original, reserved);
    require(pooled.applied == 7 && pooled.preloads.size() == 1U &&
                pooled.preloads.front().register_name == "a" &&
                pooled.preloads.front().value == "12345678",
            "late data pooling must use a proved unused standard register");
    require(machine_cell_count(pooled.ops) == machine_cell_count(original) - 7,
            "an eight-digit preload must replace seven program cells");

    const auto execute = [&](const std::vector<IrOp>& ops,
                             const std::string& initial_x = "7",
                             bool observe_last_x = false) {
      std::vector<int> codes;
      const auto labels = core::passes::calculate_label_addresses(ops);
      for (const MachineItem& item : lower_ir_to_machine(ops)) {
        if (item.kind == MachineItemKind::Label)
          continue;
        if (item.kind == MachineItemKind::Address) {
          const int address = std::holds_alternative<int>(item.target)
              ? std::get<int>(item.target) : labels.at(std::get<std::string>(item.target));
          require(address >= 0 && address < 100,
                  "literal fixture addresses must fit ordinary decimal MK-61 cells");
          codes.push_back((address / 10) * 16 + address % 10);
        } else {
          codes.push_back(item.opcode);
        }
      }
      for (int count = 0; count < 4; ++count) {
        codes.push_back(count == 0 && observe_last_x ? 0x0f : 0x25);
        codes.push_back(0x50);
      }
      emulator::MK61 calculator;
      require(calculator.load_program(codes).diagnostics.empty(),
              "literal fixture must load on a standard MK-61");
      calculator.set_register("0", initial_x).set_register("1", "3");
      calculator.set_register("7", "0");
      // Identical entry machine state; the original never reads this slot.
      calculator.set_register("a", "12345678");
      calculator.press_sequence({"В/О", "С/П"});
      require(calculator.run_until_stable(1000, 8).stopped,
              "literal fixture must stop");
      std::vector<std::string> observed{calculator.display_text()};
      // Subsequent stack operations observe Y/Z/T as well as the result.
      for (int count = 0; count < 4; ++count) {
        calculator.press("С/П");
        require(calculator.run_until_stable(1000, 8).stopped,
                "stack observation must stop after the next rotation");
        observed.push_back(calculator.display_text());
      }
      return observed;
    };
    require(execute(original) == execute(pooled.ops),
            "literal pooling must preserve calculator output and stack contents");

    for (const IrOp& memory_prefix : {
             recall("0"), known_target_indirect_recall("7", "0"),
             known_target_indirect_store("7", "0")}) {
      auto after_memory = original;
      after_memory.at(1) = memory_prefix;
      if (memory_prefix.kind == IrKind::IndirectStore ||
          memory_prefix.kind == IrKind::IndirectRecall)
        after_memory.at(1).meta.indirect_memory_targets = std::vector<int>{0};
      auto manual_pool = after_memory;
      manual_pool.at(2) = recall("a");
      manual_pool.erase(manual_pool.begin() + 3, manual_pool.begin() + 10);
      require(execute(after_memory) == execute(manual_pool),
              "memory prefix must give literal entry the same physical lift as recall: " +
                  std::to_string(memory_prefix.opcode));
      const auto memory_pool = run(after_memory, reserved);
      require(memory_pool.applied == 7,
              "late literal proof must support the emulator-proved memory prefix: " +
                  std::to_string(memory_prefix.opcode));
    }

    auto branched = original;
    branched.insert(branched.begin() + 11,
                    {cjump_to("!=0", 0x57, "literal_join"),
                     store("2"), label("literal_join")});
    const auto branch_pool = run(branched, reserved);
    require(branch_pool.applied == 7,
            "an address-taking conditional is flow, not an opaque X2 consumer");
    for (const std::string& input : {std::string{"7"}, std::string{"-12345678"}})
      require(execute(branched, input) == execute(branch_pool.ops, input),
              "both conditional outcomes must preserve literal output and the complete stack");

    std::vector<IrOp> called = {jump_to("literal_main"), label("literal_helper")};
    called.insert(called.end(), original.begin(), original.begin() + 11);
    called.insert(called.end(), {ret(), label("literal_main"), call_to("literal_helper"),
                                 store("3"), call_to("literal_helper"),
                                 recall("1"), plain(0x10), terminal});
    const auto call_pool = run(called, reserved);
    require(call_pool.applied == 7,
            "exact matched returns must carry pending X2 inequality to the caller");
    require(execute(called) == execute(call_pool.ops) &&
                execute(called, "7", true) == execute(call_pool.ops, "7", true),
            "all call contexts must preserve output, X/Y/Z/T and physical last-X");

    auto followed_by_digit = called;
    followed_by_digit.insert(followed_by_digit.end() - 3, {plain(4), store("2")});
    const auto digit_pool = run(followed_by_digit, reserved);
    require(digit_pool.applied == 7,
            "ordinary decimal entry must not be treated as an opaque X2 restore");
    require(execute(followed_by_digit) == execute(digit_pool.ops) &&
                execute(followed_by_digit, "7", true) == execute(digit_pool.ops, "7", true),
            "decimal entry in the caller must preserve physical stack and last-X observations");

    auto no_lift = original;
    no_lift.at(1) = plain(0x0e);
    require(run(no_lift, reserved).applied == 0,
            "Enter suppression is not proof of a fresh literal stack lift");
    auto observable_x2 = original;
    observable_x2.at(11) = plain(0x0a);
    require(run(observable_x2, reserved).applied == 0,
            "a decimal-point restore must preserve the original entered X2");
    auto exponent = original;
    exponent.at(10) = plain(0x0c);
    require(run(exponent, reserved).applied == 0,
            "a mantissa still consumed by VP is not an ordinary integer literal");
    auto raw = original;
    raw.at(5).meta.raw = true;
    require(run(raw, reserved).applied == 0, "raw numeric cells must remain intact");
    auto indirect = original;
    indirect.at(1) = known_target_indirect_store("2", "a");
    require(run(indirect, reserved).applied == 0,
            "an indirectly written register is not a spare literal pool slot");
    auto fixed_target = original;
    fixed_target.insert(fixed_target.begin(), numeric_jump(7));
    require(run(fixed_target, reserved).applied == 0,
            "a numeric entry with an unproved automatic lift must fail closed");
    reserved.insert("a");
    require(run(original, reserved).applied == 0,
            "exhausting R0-RE must not allocate MK61S-only Rf");
  }

  {
    CompileOptions options;
    options.defer_return_suffix_until_callee_hole = true;
    const auto run = [&](const std::vector<IrOp>& ops) {
      return core::passes::run_ir_passes(lower_ir_to_machine(ops), options);
    };
    const auto has_store = [](const core::passes::RunPassesResult& result,
                              int opcode) {
      return std::any_of(result.items.begin(), result.items.end(),
                         [&](const MachineItem& item) {
                           return item.kind == MachineItemKind::Op && item.opcode == opcode;
                         });
    };
    const auto forwarded = run({store("4"), recall("4"), plain(0x10), stop("halt")});
    require(!has_store(forwarded, 0x44) && has_store(forwarded, 0x0e),
            "early cleanup must free a dead register while retaining the consumed stack lift");
    require(std::any_of(forwarded.optimizations.begin(), forwarded.optimizations.end(),
                        [](const auto& optimization) {
                          return optimization.name == "relocatable-ir-cleanup";
                        }),
            "phase-ordered extraction must report the preceding cleanup fixed point");

    const auto across_input =
        run({store("4"), stop("read x"), recall("4"), plain(0x10), stop("halt")});
    require(has_store(across_input, 0x44),
            "early cleanup must not free state read after manual input");

    IrOp opaque_call;
    opaque_call.kind = IrKind::IndirectCall;
    opaque_call.register_name = "7";
    opaque_call.opcode = 0xa7;
    opaque_call.meta.mnemonic = "K PP 7";
    const auto across_opaque_call =
        run({store("4"), opaque_call, recall("4"), plain(0x10), stop("halt")});
    require(has_store(across_opaque_call, 0x44),
            "early cleanup must keep state an unresolved indirect callee can observe");

    IrOp raw_store = store("4");
    raw_store.meta.raw = true;
    const auto raw = run({raw_store, recall("4"), plain(0x10), stop("halt")});
    require(has_store(raw, 0x44), "early cleanup must preserve raw register writes");
  }

  {
    const std::vector<core::passes::IrPass>& pipeline = core::passes::pass_pipeline();
    const auto pass_index = [&](std::string_view name) {
      const auto found = std::find_if(pipeline.begin(), pipeline.end(), [&](const auto& pass) {
        return pass.name == name;
      });
      require(found != pipeline.end(), "pass pipeline should contain " + std::string(name));
      return std::distance(pipeline.begin(), found);
    };
    require(pass_index("return-suffix-gadget") < pass_index("call-continuation-composition"),
            "sum/difference suffix sharing must run before continuation composition");
    require(pass_index("return-suffix-gadget") < pass_index("tail-call-lowering"),
            "sum/difference suffix sharing must run before tail-call lowering");
  }

  {
    const core::passes::PassResult result =
        run_jump_to_next({jump_to("next"), label("next"), plain()});
    require(result.applied == 1, "jump-to-next did not remove direct fallthrough jump");
    require(result.ops.size() == 2, "jump-to-next produced wrong op count");
    require(result.ops.at(0).kind == IrKind::Label, "jump-to-next removed following label");
    require(result.ops.at(1).kind == IrKind::Plain, "jump-to-next removed following op");
    require(result.optimizations.size() == 1, "jump-to-next did not report optimization");
    require(result.optimizations.at(0).name == "jump-to-next-threading",
            "jump-to-next reported wrong optimization name");
  }

  {
    const core::passes::PassResult result =
        run_jump_to_next({jump_to("target"), label("other"), label("target"), plain()});
    require(result.applied == 1, "jump-to-next did not skip intervening labels");
    require(result.ops.size() == 3, "jump-to-next skipped-label case produced wrong op count");
  }

  {
    const core::passes::PassResult result =
        run_jump_to_next({jump_to("target"), plain(), label("target")});
    require(result.applied == 0, "jump-to-next removed a non-fallthrough jump");
    require(result.ops.size() == 3, "jump-to-next changed non-fallthrough op count");
  }

  {
    const core::passes::PassResult result =
        run_jump_to_next({jump_to("target", true), label("target")});
    require(result.applied == 0, "jump-to-next ignored raw rewrite barrier");
    require(result.ops.size() == 2, "jump-to-next changed raw-barrier op count");
  }

  {
    const std::vector<MachineItem> items = {
        MachineItem::op(0x51, "БП"),
        MachineItem::address(std::string("next")),
        MachineItem::label("next"),
        MachineItem::op(0x50, "С/П"),
    };
    const CompileOptions options;
    const core::passes::RunPassesResult result = core::passes::run_ir_passes(items, options);
    require(result.applied == 1, "run_ir_passes did not apply jump-to-next");
    const auto pass_count = result.pass_counts.find("jump-to-next-threading");
    require(pass_count != result.pass_counts.end(), "run_ir_passes did not track pass count");
    require(pass_count->second == 1, "run_ir_passes tracked wrong pass count");
    require(result.items.size() == 2, "run_ir_passes produced wrong machine item count");
    require(result.items.at(0).kind == MachineItemKind::Label,
            "run_ir_passes removed fallthrough label");
    require(result.items.at(1).kind == MachineItemKind::Op && result.items.at(1).opcode == 0x50,
            "run_ir_passes removed following stop");
  }

  {
    const std::vector<MachineItem> items = {
        MachineItem::op(0x4f, "X->П f / R0 alias"),
        MachineItem::op(0x40, "X->П 0"),
        // Keep the stored value live independently of current-X forwarding.
        MachineItem::op(0x07, "7"),
        MachineItem::op(0x6f, "П->X f / R0 alias"),
        MachineItem::op(0x10, "+"),
        MachineItem::op(0x50, "С/П"),
    };
    CompileOptions standard_options;
    const core::passes::RunPassesResult standard =
        core::passes::run_ir_passes(items, standard_options);
    require(std::none_of(standard.items.begin(), standard.items.end(), [](const MachineItem& item) {
              return item.kind == MachineItemKind::Op && item.opcode == 0x4f;
            }) &&
                std::any_of(standard.items.begin(), standard.items.end(), [](const MachineItem& item) {
                  return item.kind == MachineItemKind::Op && item.opcode == 0x40;
                }),
            "standard IR pipeline did not treat 4F/6F as R0 aliases: " +
                machine_items_to_json(standard.items));

    CompileOptions expanded_options;
    expanded_options.feature_profile = FeatureProfile::Mk61SMiniExpanded;
    const core::passes::RunPassesResult expanded =
        core::passes::run_ir_passes(items, expanded_options);
    require(std::any_of(expanded.items.begin(), expanded.items.end(), [](const MachineItem& item) {
              return item.kind == MachineItemKind::Op && item.opcode == 0x4f;
            }) &&
                std::none_of(expanded.items.begin(), expanded.items.end(), [](const MachineItem& item) {
                  return item.kind == MachineItemKind::Op && item.opcode == 0x40;
                }),
            "expanded IR pipeline confused the real Rf with the standard R0 alias: " +
                machine_items_to_json(expanded.items));
  }

  {
    const std::vector<MachineItem> items =
        lower_ir_to_machine({plain(0x02), store("1"), call_to("inc"), stop("halt"), label("inc"),
                             recall("1"), plain(0x01), plain(0x10), ret()});
    const CompileOptions options;
    const core::passes::RunPassesResult result = core::passes::run_ir_passes(items, options);
    const bool kept_parameter_recall =
        std::any_of(result.items.begin(), result.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Op && item.opcode == 0x61;
        });
    require(kept_parameter_recall,
            "run_ir_passes removed a function parameter recall whose stack lift feeds +");
  }

  {
    auto require_kept_constant_indexed_access = [](std::vector<IrOp> ops, std::string target_register,
                                                  const std::string& fixture) {
      for (IrOp& op : ops) {
        if (op.kind == IrKind::Store && op.register_name == target_register)
          op.meta.comment = "set slots_2";
        if (op.kind == IrKind::Recall && op.register_name == target_register)
          op.meta.comment = "recall slots_2";
      }
      const std::vector<MachineItem> items = lower_ir_to_machine(ops);
      CompileOptions options;
      options.disable_interprocedural_opts = true;
      const core::passes::RunPassesResult result = core::passes::run_ir_passes(items, options);
      const bool kept_store =
          std::any_of(result.items.begin(), result.items.end(), [](const MachineItem& item) {
            return item.kind == MachineItemKind::Op && item.comment == "set slots_2";
          });
      const bool kept_recall =
          std::any_of(result.items.begin(), result.items.end(), [](const MachineItem& item) {
            return item.kind == MachineItemKind::Op && item.comment == "recall slots_2";
          });
      require(kept_store, "run_ir_passes removed live constant-indexed state store in " + fixture);
      require(kept_recall,
              "run_ir_passes removed live constant-indexed state recall in " + fixture);
    };
    require_kept_constant_indexed_access(
        {stop("read x"), store("2"), plain(0x00), store("0"), recall("2"), stop("halt")},
        "2", "stored-dead-x fixture");
    require_kept_constant_indexed_access(
        {stop("read x"), store("2"), plain(0x00), recall("2"), stop("halt")},
        "2", "literal-invalidated-x fixture");
    require_kept_constant_indexed_access({label("loop"), stop("read x"), store("2"), plain(0x00),
                                          store("0"), recall("2"), stop("halt"),
                                          jump_to("loop")},
                                         "2", "looping stored-dead-x fixture");
    require_kept_constant_indexed_access({label("loop"), stop("read x"), store("0"), plain(0x00),
                                          store("1"), recall("0"), stop("halt"),
                                          jump_to("loop")},
                                         "0", "looping-r0-target fixture");
    require_kept_constant_indexed_access({label("loop"), stop("read x"), store("1"), store("0"),
                                          plain(0x00), store("1"), recall("0"), stop("halt"),
                                          jump_to("loop")},
                                         "0", "stale-register-memory-alias fixture");
    const std::vector<MachineItem> stale_alias_items = lower_ir_to_machine(
        {label("loop"), stop("read x"), store("1"), store("0"), plain(0x00), store("1"),
         recall("0"), stop("halt"), jump_to("loop")});
    CompileOptions stale_alias_options;
    stale_alias_options.disable_interprocedural_opts = true;
    const core::passes::RunPassesResult stale_alias_result =
        core::passes::run_ir_passes(stale_alias_items, stale_alias_options);
    const auto pass_count = [&](const std::string& name) {
      const auto found = stale_alias_result.pass_counts.find(name);
      return found == stale_alias_result.pass_counts.end() ? 0 : found->second;
    };
    require(pass_count("dead-store-elimination") == 2,
            "stale register memory alias fixture should match TS dead-store count");
    require(pass_count("store-recall-peephole") == 0,
            "stale register memory alias fixture should not remove the live recall");
  }

  {
    IrOp shown = stop("halt");
    shown.meta.stop_disposition = StopDisposition::Terminal;
    const core::passes::PassResult result = run_redundant_prologue({
        label("head"),
        recall("1"),
        plain(0x10),
        shown,
        plain(0x31),
        recall("1"),
        plain(0x10),
        shown,
        jump_to("head"),
    });
    require(result.applied == 1, "redundant-prologue did not remove duplicate loop prologue");
    require(result.ops.size() == 6, "redundant-prologue produced wrong op count");
    require(result.ops.at(0).kind == IrKind::Label, "redundant-prologue removed loop label");
    require(result.ops.at(1).kind == IrKind::Recall, "redundant-prologue removed head recall");
    require(result.ops.at(2).kind == IrKind::Plain && result.ops.at(2).opcode == 0x10,
            "redundant-prologue removed head display op");
    require(result.ops.at(3).kind == IrKind::Stop, "redundant-prologue removed head stop");
    require(result.ops.at(4).kind == IrKind::Plain && result.ops.at(4).opcode == 0x31,
            "redundant-prologue removed intermediate content");
    require(result.ops.at(5).kind == IrKind::Jump, "redundant-prologue removed loop jump");
    require(result.optimizations.size() == 1, "redundant-prologue did not report optimization");
    require(result.optimizations.at(0).name == "redundant-prologue-elimination",
            "redundant-prologue reported wrong optimization name");
  }

  {
    IrOp shown = stop("halt");
    shown.meta.stop_disposition = StopDisposition::Terminal;
    shown.meta.roles = {kTypedDisplayObservationRole};
    const std::vector<IrOp> program{
        label("head"), recall("1"), plain(0x10), shown, plain(0x31),
        recall("1"), plain(0x10), shown, jump_to("head")};
    const auto shared = run_redundant_prologue(program);
    require(shared.applied == 1 && shared.ops.at(3).meta.roles == shown.meta.roles,
            "typed terminal observation metadata must not disable identical prologue sharing");
    auto different_contracts = program;
    different_contracts.at(3).meta.roles.clear();
    require(run_redundant_prologue(different_contracts).applied == 0,
            "terminal sharing must not replace a full-state stop by a weaker display contract");
    auto bound = program;
    bound.at(3).meta.roles.push_back("unknown-layout-contract");
    require(run_redundant_prologue(bound).applied == 0,
            "terminal observation metadata must not hide a real layout role");
    auto misplaced = program;
    misplaced.at(1).meta.roles = {kTypedDisplayObservationRole};
    require(run_redundant_prologue(misplaced).applied == 0,
            "only a typed terminal stop can carry an address-neutral display role here");
  }

  {
    const core::passes::PassResult result = run_redundant_prologue({
        label("head"),
        recall("1"),
        stop("show"),
        recall("2"),
        stop("show"),
        jump_to("head"),
    });
    require(result.applied == 0, "redundant-prologue removed a non-matching prologue");
    require(result.ops.size() == 6, "redundant-prologue changed non-matching op count");
  }

  {
    const core::passes::PassResult result =
        run_tail_call({call_to("proc"), jump_to("ret"), label("ret"), ret(), label("proc"), ret()});
    require(result.applied == 1,
            "tail-call must not specialize an already handled return continuation: applied=" +
                std::to_string(result.applied) + " ops=" + ir_ops_to_json(result.ops));
    require(result.ops.size() == 5, "tail-call return-label case produced wrong op count");
    require(result.ops.at(0).kind == IrKind::Jump, "tail-call did not lower call to jump");
    require(std::get<std::string>(result.ops.at(0).target) == "proc",
            "tail-call produced wrong jump target");
    require(result.ops.at(4).kind == IrKind::Return,
            "ordinary tail call must keep the shorter callee return");
    require(result.optimizations.size() == 1, "tail-call did not report optimization");
    require(result.optimizations.at(0).name == "tail-call-lowering",
            "tail-call reported wrong optimization name");
  }

  {
    IrOp charge_entry = call_to("skeleton");
    charge_entry.meta.comment =
        "callee-hole charge-entry call; proof=skeleton; selector=e";
    const core::passes::PassResult result =
        run_tail_call({charge_entry, ret(), label("skeleton"), ret()});
    require(result.applied == 1,
            "tail-call did not lower a proved callee-hole charge entry");
    require(result.ops.at(0).kind == IrKind::Jump,
            "callee-hole charge entry did not become a tail jump");
    require(result.ops.at(0).meta.comment ==
                "callee-hole charge-entry tail transfer; proof=skeleton; selector=e",
            "tail-call did not preserve the callee-hole proof as a typed tail transfer");
  }

  {
    // Main falls through the same return after the call has already popped it.
    // Replacing that return with a caller jump would change the empty-stack path.
    const std::vector<IrOp> shared_with_main = {
        call_to("proc"), jump_to("cont"), label("cont"), plain(0x31), label("proc"), ret()};
    const auto rejected = run_tail_call(shared_with_main);
    require(rejected.applied == 0 &&
                ir_ops_to_json(rejected.ops) == ir_ops_to_json(shared_with_main),
            "tail-call must preserve a return also reachable from the main frame");

    const core::passes::PassResult result = run_tail_call(
        {call_to("proc"), jump_to("cont"), label("cont"), plain(0x31),
         jump_to("cont"), label("proc"), ret()});
    require(result.applied == 2, "tail-call did not rewrite call and proc return continuation");
    require(result.ops.size() == 6, "tail-call continuation case produced wrong op count");
    require(result.ops.at(0).kind == IrKind::Jump, "tail-call did not lower continuation call");
    require(result.ops.at(5).kind == IrKind::Jump, "tail-call did not lower proc return");
    require(std::get<std::string>(result.ops.at(5).target) == "cont",
            "tail-call return continuation target mismatch");
  }

  {
    // Empty-stack V/O resumes at physical 01, not at the main-loop head 00.
    // Direct, indirect and shared-return forms must retain their call frames.
    const std::vector<std::vector<IrOp>> fixtures = {
        {label("main"), call_to("finish_turn"), plain(0x01), call_to("finish_turn"),
         jump_to("main"), proc_start("finish_turn"), plain(0x02), ret()},
        {label("main"), call_to("finish_turn"), known_target_indirect_jump("b", 0),
         proc_start("finish_turn"), plain(0x02), ret()},
        {label("main"), call_to("finish_turn"), plain(0x01), call_to("finish_turn"),
         jump_to("main"), proc_start("finish_turn"), plain(0x02),
         jump_to("shared_return"), proc_start("shared_return"), plain(0x03), ret()},
    };
    for (const auto& input : fixtures) {
      const core::passes::PassResult result = run_tail_call(input);
      require(result.applied == 0,
              "tail-call must not equate the 00 continuation with empty-stack return");
      require(result.optimizations.empty(),
              "rejected empty-stack rewrite must not report an optimization");
      require(ir_ops_to_json(result.ops) == ir_ops_to_json(input),
              "rejected empty-stack rewrite must preserve calls and explicit continuations");
    }
  }

  {
    const core::passes::PassResult disabled = run_tail_branch_inversion(
        {cjump_to("==0", 0x5e, "then"), jump_to("tail"), label("then"), plain(0x31)}, false);
    require(disabled.applied == 0, "tail-branch-inversion ignored disabled option");

    const core::passes::PassResult enabled = run_tail_branch_inversion(
        {cjump_to("==0", 0x5e, "then"), jump_to("tail"), label("then"), plain(0x31)}, true);
    require(enabled.applied == 1, "tail-branch-inversion did not invert tail branch");
    require(enabled.ops.size() == 2, "tail-branch-inversion produced wrong op count");
    require(enabled.ops.at(0).kind == IrKind::CondJump, "tail-branch-inversion changed op kind");
    require(enabled.ops.at(0).condition == "!=0", "tail-branch-inversion condition mismatch");
    require(enabled.ops.at(0).opcode == 0x57, "tail-branch-inversion opcode mismatch");
    require(std::get<std::string>(enabled.ops.at(0).target) == "tail",
            "tail-branch-inversion target mismatch");
  }

  {
    const std::vector<IrOp> input = {
        cjump_to("==0", 0x5e, "miss"),
        plain(0x31),
        cjump_to("==0", 0x5e, "miss"),
        plain(0x32),
    };
    const core::passes::PassResult disabled = run_conditional_branch_trampoline(input, false);
    require(disabled.applied == 0, "conditional-branch-trampoline ignored disabled option");

    const core::passes::PassResult enabled = run_conditional_branch_trampoline(input, true);
    require(enabled.applied == 1, "conditional-branch-trampoline did not retarget matching branch");
    require(enabled.ops.size() == 5, "conditional-branch-trampoline produced wrong op count");
    require(enabled.ops.at(0).kind == IrKind::CondJump,
            "conditional-branch-trampoline changed first op kind");
    require(std::get<std::string>(enabled.ops.at(0).target) == "__conditional_branch_trampoline_0",
            "conditional-branch-trampoline first branch target mismatch");
    require(enabled.ops.at(2).kind == IrKind::Label && enabled.ops.at(2).hidden,
            "conditional-branch-trampoline did not insert hidden label");
    require(enabled.ops.at(3).kind == IrKind::CondJump,
            "conditional-branch-trampoline moved equivalent branch incorrectly");
  }

  {
    const core::passes::PassResult result = run_shared_terminal_tail(
        {plain(0x31), plain(0x32), ret(), plain(0x31), plain(0x32), ret()});
    require(result.applied == 1, "shared-terminal-tail did not share duplicate tail");
    require(result.ops.size() == 5, "shared-terminal-tail produced wrong op count");
    require(result.ops.at(0).kind == IrKind::Label,
            "shared-terminal-tail did not insert helper label");
    require(result.ops.at(0).name == "__shared_terminal_tail_0",
            "shared-terminal-tail inserted wrong helper label");
    require(result.ops.at(4).kind == IrKind::Jump,
            "shared-terminal-tail did not replace duplicate suffix with jump");
    require(std::get<std::string>(result.ops.at(4).target) == "__shared_terminal_tail_0",
            "shared-terminal-tail replacement target mismatch");
    require(result.optimizations.size() == 1, "shared-terminal-tail did not report optimization");
    require(result.optimizations.at(0).name == "shared-terminal-tail",
            "shared-terminal-tail reported wrong optimization name");
  }

  {
    const core::passes::PassResult result =
        run_shared_call_tail({call_to("helper"), jump_to("done"), call_to("helper"),
                              jump_to("done"), call_to("helper"), jump_to("done")});
    require(result.applied == 3, "shared-call-tail did not replace all repeated call tails");
    require(result.ops.size() == 6, "shared-call-tail produced wrong op count");
    require(result.ops.at(0).kind == IrKind::Jump,
            "shared-call-tail did not replace first tail with jump");
    require(std::get<std::string>(result.ops.at(0).target) == "__shared_call_tail_0",
            "shared-call-tail replacement target mismatch");
    require(result.ops.at(3).kind == IrKind::Label &&
                result.ops.at(3).name == "__shared_call_tail_0",
            "shared-call-tail did not append helper label");
    require(result.ops.at(4).kind == IrKind::Call && result.ops.at(5).kind == IrKind::Jump,
            "shared-call-tail helper body mismatch");
    require(result.optimizations.size() == 1, "shared-call-tail did not report optimization");
    require(result.optimizations.at(0).name == "shared-call-tail",
            "shared-call-tail reported wrong optimization name");
  }

  {
    const core::passes::PassResult result = run_return_suffix_gadget(
        {plain(0x31), plain(0x32), ret(), plain(0x31), plain(0x32), ret()});
    require(result.applied == 1, "return-suffix-gadget did not share duplicate return suffix");
    require(result.ops.size() == 5, "return-suffix-gadget jump case produced wrong op count");
    require(result.ops.at(0).kind == IrKind::Label,
            "return-suffix-gadget did not insert target label");
    require(result.ops.at(0).name == "__return_suffix_gadget_0",
            "return-suffix-gadget inserted wrong target label");
    require(result.ops.at(4).kind == IrKind::Jump,
            "return-suffix-gadget did not replace duplicate return suffix with jump");
    require(std::get<std::string>(result.ops.at(4).target) == "__return_suffix_gadget_0",
            "return-suffix-gadget jump target mismatch");
    require(result.optimizations.size() == 1, "return-suffix-gadget did not report optimization");
    require(result.optimizations.at(0).name == "return-suffix-gadget",
            "return-suffix-gadget reported wrong optimization name");
  }

  {
    const core::passes::PassResult result = run_return_suffix_gadget(
        {plain(0x31), plain(0x32), plain(0x33), ret(), plain(0x31), plain(0x32), plain(0x33)});
    require(result.applied == 1, "return-suffix-gadget did not create body call gadget");
    require(result.ops.size() == 6, "return-suffix-gadget call case produced wrong op count");
    require(result.ops.at(0).kind == IrKind::Label,
            "return-suffix-gadget body call did not insert target label");
    require(result.ops.at(5).kind == IrKind::Call,
            "return-suffix-gadget did not replace duplicate body with call");
    require(std::get<std::string>(result.ops.at(5).target) == "__return_suffix_gadget_0",
            "return-suffix-gadget call target mismatch");
  }

  {
    const core::passes::PassResult result = run_return_suffix_gadget(
        {recall("5"), recall("1"), recall("2"), plain(0x10), call_to("normalize"),
         call_to("mark"), recall("4"), recall("1"), recall("2"), plain(0x11),
         call_to("normalize"), jump_to("mark")});
    require(result.applied == 2,
            "sum/difference tail gadget should apply one canonicalization and one shared tail");
    require(result.ops.size() == 11,
            "sum/difference tail gadget should remove one opcode and three encoded cells");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                        [](const core::passes::AppliedOptimization& optimization) {
                          return optimization.name == "sum-difference-tail-gadget";
                        }),
            "sum/difference tail gadget should report its algebraic proof");
    require(std::any_of(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
              return op.kind == IrKind::Plain && op.opcode == 0x0b;
            }),
            "sum/difference tail gadget should form a-b as (-b)+a");
  }

  {
    const core::passes::PassResult result = run_shared_straight_line_helper(
        {label("first"), recall("1"), recall("2"), plain(0x10), store("3"), recall("4"), store("5"),
         plain(0x20), label("second"), recall("1"), recall("2"), plain(0x10), store("3"),
         recall("4"), store("5"), plain(0x21), stop("halt")});
    require(result.applied == 2,
            "shared-straight-line-helper did not replace repeated straight-line bodies");
    int helper_calls = 0;
    bool saw_helper_label = false;
    bool saw_helper_return = false;
    for (const IrOp& op : result.ops) {
      if (op.kind == IrKind::Call &&
          std::get<std::string>(op.target) == "__shared_straight_line_helper_0")
        ++helper_calls;
      if (op.kind == IrKind::Label && op.name == "__shared_straight_line_helper_0")
        saw_helper_label = true;
      if (op.kind == IrKind::Return)
        saw_helper_return = true;
    }
    require(helper_calls == 2, "shared-straight-line-helper emitted wrong helper call count");
    require(saw_helper_label, "shared-straight-line-helper did not append helper label");
    require(saw_helper_return, "shared-straight-line-helper did not append helper return");
    require(result.optimizations.size() == 1,
            "shared-straight-line-helper did not report optimization");
    require(result.optimizations.at(0).name == "shared-straight-line-helper",
            "shared-straight-line-helper reported wrong optimization name");
  }

  {
    const core::passes::PassResult disabled = run_shared_straight_line_helper(
        {recall("1"), call_to("helper"), recall("2"), plain(0x10), store("3"), recall("1"),
         call_to("helper"), recall("2"), plain(0x10), store("3")},
        false);
    require(disabled.applied == 0,
            "shared-straight-line-helper included direct calls while option was disabled");
    const core::passes::PassResult enabled = run_shared_straight_line_helper(
        {recall("1"), call_to("helper"), recall("2"), plain(0x10), store("3"), recall("1"),
         call_to("helper"), recall("2"), plain(0x10), store("3")},
        true);
    require(enabled.applied == 2,
            "shared-straight-line-helper did not include direct calls when option was enabled");
  }

  {
    IrOp sensitive = recall("1");
    sensitive.meta.comment = "show display";
    const core::passes::PassResult result = run_shared_straight_line_helper(
        {sensitive, recall("2"), plain(0x10), store("3"), sensitive, recall("2"), plain(0x10),
         store("3"), sensitive, recall("2"), plain(0x10), store("3")});
    require(result.applied == 0, "shared-straight-line-helper optimized a display-sensitive block");
  }

  {
    const std::vector<IrOp> program = {label("first"),
                                       recall("1"),
                                       recall("2"),
                                       plain(0x10),
                                       store("3"),
                                       recall("4"),
                                       store("5"),
                                       plain(0x20),
                                       label("second"),
                                       recall("1"),
                                       recall("2"),
                                       plain(0x10),
                                       store("3"),
                                       recall("4"),
                                       store("5"),
                                       plain(0x21),
                                       label("suffix"),
                                       plain(0x10),
                                       store("3"),
                                       recall("4"),
                                       store("5"),
                                       plain(0x17),
                                       stop("halt")};
    const core::passes::PassResult result = run_shared_straight_line_helper(program);
    require(result.applied == 3,
            "shared-straight-line-helper did not add repeated suffix entries");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                        [](const core::passes::AppliedOptimization& optimization) {
                          return optimization.name == "multi-entry-straight-line-helper";
                        }),
            "shared-straight-line-helper did not report multi-entry extraction");
    const int helper_calls =
        static_cast<int>(std::count_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
          return op.kind == IrKind::Call &&
                 std::get<std::string>(op.target) == "__shared_straight_line_helper_0";
        }));
    const int entry_calls =
        static_cast<int>(std::count_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
          return op.kind == IrKind::Call &&
                 std::get<std::string>(op.target) == "__shared_straight_line_helper_1";
        }));
    const auto helper = std::find_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
      return op.kind == IrKind::Label && op.name == "__shared_straight_line_helper_0";
    });
    const auto entry = std::find_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
      return op.kind == IrKind::Label && op.name == "__shared_straight_line_helper_1";
    });
    require(helper_calls == 2, "shared-straight-line-helper emitted wrong body call count");
    require(entry_calls == 1, "shared-straight-line-helper emitted wrong entry call count");
    require(helper != result.ops.end(), "shared-straight-line-helper did not append helper body");
    require(entry != result.ops.end(), "shared-straight-line-helper did not append helper entry");
    require(helper < entry, "shared-straight-line-helper entry should be inside helper body");
    require(machine_cell_count(result.ops) < machine_cell_count(program),
            "shared-straight-line-helper multi-entry result did not reduce cell count");
  }

  {
    const std::vector<IrOp> program = {label("anchor"),
                                       recall("1"),
                                       recall("2"),
                                       plain(0x10),
                                       store("3"),
                                       recall("4"),
                                       plain(0x20),
                                       label("suffix_one"),
                                       plain(0x10),
                                       store("3"),
                                       recall("4"),
                                       plain(0x21),
                                       label("suffix_two"),
                                       plain(0x10),
                                       store("3"),
                                       recall("4"),
                                       plain(0x17),
                                       label("suffix_three"),
                                       plain(0x10),
                                       store("3"),
                                       recall("4"),
                                       plain(0x22),
                                       label("suffix_four"),
                                       plain(0x10),
                                       store("3"),
                                       recall("4"),
                                       plain(0x18),
                                       stop("halt")};
    const core::passes::PassResult result = run_shared_straight_line_helper(program);
    require(result.applied == 5,
            "shared-straight-line-helper did not anchor a unique body for repeated suffixes");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                        [](const core::passes::AppliedOptimization& optimization) {
                          return optimization.name == "multi-entry-straight-line-helper";
                        }),
            "anchored shared-straight-line-helper did not report multi-entry extraction");
    const int helper_calls =
        static_cast<int>(std::count_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
          return op.kind == IrKind::Call &&
                 std::get<std::string>(op.target) == "__shared_straight_line_helper_0";
        }));
    const int entry_calls =
        static_cast<int>(std::count_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
          return op.kind == IrKind::Call &&
                 std::get<std::string>(op.target) == "__shared_straight_line_helper_1";
        }));
    const auto helper = std::find_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
      return op.kind == IrKind::Label && op.name == "__shared_straight_line_helper_0";
    });
    const auto entry = std::find_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
      return op.kind == IrKind::Label && op.name == "__shared_straight_line_helper_1";
    });
    require(helper_calls == 1, "anchored shared-straight-line-helper emitted wrong body call count");
    require(entry_calls == 4, "anchored shared-straight-line-helper emitted wrong entry call count");
    require(helper != result.ops.end(), "anchored shared-straight-line-helper missing body label");
    require(entry != result.ops.end(), "anchored shared-straight-line-helper missing entry label");
    require(helper < entry, "anchored shared-straight-line-helper entry should be inside helper body");
    require(machine_cell_count(result.ops) < machine_cell_count(program),
            "anchored shared-straight-line-helper result did not reduce cell count");
  }

  {
    const std::vector<IrOp> program = {
        label("first"),  call_to("spatial"), recall("e"), plain(0x10), store("e"),
        recall("3"),    plain(0x01),        label("second"), call_to("spatial"),
        recall("e"),    plain(0x10),        store("e"),      recall("3"),
        plain(0x02),    label("third"),     call_to("spatial"), recall("e"),
        plain(0x10),    store("e"),         recall("3"),     plain(0x03),
        stop("halt")};
    const core::passes::PassResult result = run_shared_straight_line_helper(program, true);
    require(result.applied == 3,
            "shared-straight-line-helper did not replace the repeated accumulator body");

    const auto helper = std::find_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
      return op.kind == IrKind::Label && op.name == "__shared_straight_line_helper_0";
    });
    require(helper != result.ops.end(), "accumulator shared helper missing");
    const auto helper_return =
        std::find_if(helper, result.ops.end(), [](const IrOp& op) {
          return op.kind == IrKind::Return;
        });
    require(helper_return != result.ops.end(), "accumulator shared helper return missing");
    const bool helper_stores_total =
        std::any_of(helper, helper_return, [](const IrOp& op) {
          return op.kind == IrKind::Store && op.register_name == "e";
        });
    require(helper_stores_total,
            "shared-straight-line-helper chose the shorter prefix instead of the accumulator store body");
  }

  {
    IrOp branch = jump_to("start");
    branch.meta.source_line = 73;
    branch.meta.roles = {"loop-edge-origin"};
    branch.meta.semantic_call_origins = {123};
    const std::vector<IrOp> program = {plain(0x01), label("start"), plain(0x02), branch};
    const core::passes::PassResult result = run_return_zero_jump(program);
    require(result.ops.at(3).meta.source_line == 73 &&
                result.ops.at(3).meta.roles == branch.meta.roles &&
                result.ops.at(3).meta.semantic_call_origins == branch.meta.semantic_call_origins,
            "empty-return lowering must retain compiler-owned command provenance");
    require(result.applied == 1, "return-zero-jump did not replace backward jump to 01");
    require(result.ops.at(3).kind == IrKind::Return, "return-zero-jump did not emit return opcode");
    require(result.optimizations.size() == 1, "return-zero-jump did not report optimization");
    require(result.optimizations.at(0).name == "return-zero-jump",
            "return-zero-jump reported wrong optimization name");
  }

  {
    IrOp terminal = stop("halt");
    terminal.meta.stop_disposition = StopDisposition::Terminal;
    IrOp exit = numeric_cjump(8);
    const std::vector<IrOp> pinned = {
        recall("0"), label("again"), recall("1"), exit, jump_to("again"),
        plain(0x54), plain(0x54), recall("2"), terminal};
    const auto result = run_return_zero_jump(pinned);
    require(result.applied == 0 && ir_ops_to_json(result.ops) == ir_ops_to_json(pinned),
            "deleting BP 01's operand must not shift a different numeric destination");

    for (int barrier = 0; barrier < 3; ++barrier) {
      std::vector<IrOp> guarded = {
          plain(0x01), label("start"), plain(0x02), jump_to("start")};
      if (barrier == 0)
        guarded.back().meta.raw = true;
      else if (barrier == 1)
        guarded.back().meta.manual_interaction = ManualInteractionAnchor{
            .protocol_id = 1, .phase = 0, .kind = ManualInteractionAnchorKind::SingleStepCommand};
      else
        guarded.back().target_meta.formal_opcode = 0xb3;
      require(run_return_zero_jump(guarded).applied == 0,
              "raw, manual and noncanonical formal jumps must not become empty returns");
    }
  }

  {
    const core::passes::PassResult with_call =
        run_return_zero_jump({plain(0x01), call_to("proc"), jump_to("start"), label("start")});
    require(with_call.applied == 0, "return-zero-jump ignored call safety guard");
  }

  {
    const std::vector<IrOp> program = {
        label("ret0"), ret(), plain(0x01),
        cjump_to("==0", 0x57, "ret0"), plain(0x02),
        jump_to("ret0"), stop("halt"),
    };
    const core::passes::PassResult result = run_return_trampoline(program);
    require(result.applied == 2,
            "return-trampoline did not replace both direct branches to В/О@00");
    require(result.ops.at(3).kind == IrKind::IndirectCondJump,
            "return-trampoline did not emit indirect conditional jump");
    require(result.ops.at(3).opcode == 0xe7,
            "return-trampoline used the wrong indirect conditional opcode");
    require(result.ops.at(5).kind == IrKind::IndirectJump,
            "return-trampoline did not emit indirect jump");
    require(result.ops.at(5).opcode == 0x87,
            "return-trampoline used the wrong indirect jump opcode");
    require(result.preloads.size() == 1, "return-trampoline did not report selector preload");
    require(result.preloads.at(0).register_name == "7" && result.preloads.at(0).value == "B2",
            "return-trampoline picked the wrong return-zero selector preload");
    require(!result.preloads.at(0).counts_against_program,
            "return-trampoline selector preload should not count against program cells");
    require(result.optimizations.size() == 1, "return-trampoline did not report optimization");
    require(result.optimizations.at(0).name == "return-trampoline",
            "return-trampoline reported wrong optimization name");
    require(machine_cell_count(result.ops) == machine_cell_count(program) - 2,
            "return-trampoline did not save one cell per rewritten branch");
  }

  {
    const core::passes::PassResult result =
        run_return_trampoline({label("zero"), plain(0x01), jump_to("zero")});
    require(result.applied == 0,
            "return-trampoline optimized without В/О in physical cell 00");
  }

  {
    const core::passes::PassResult result =
        run_return_trampoline({label("ret0"), ret(), plain(0x01),
                               cjump_to("==0", 0x57, "ret0"), numeric_jump(6),
                               stop("halt")});
    require(result.applied == 0,
            "return-trampoline shifted code before a later absolute numeric target");
  }

  {
    const core::passes::PassResult result =
        run_store_recall_peephole({store("2"), recall("2"), stop("halt")});
    require(result.applied == 1,
            "store-recall-peephole did not remove same-register direct recall");
    require(result.ops.size() == 2,
            "store-recall-peephole produced wrong op count for direct pair");
    require(result.ops.at(0).kind == IrKind::Store,
            "store-recall-peephole removed the store instead of the recall");
    require(result.ops.at(1).kind == IrKind::Stop,
            "store-recall-peephole did not leave following op in place");
    require(result.optimizations.size() == 1, "store-recall-peephole did not report optimization");
    require(result.optimizations.at(0).name == "store-recall-peephole",
            "store-recall-peephole reported wrong optimization name");
  }

  {
    const core::passes::PassResult result =
        run_store_recall_peephole({store("2"), recall("3"), stop("halt")});
    require(result.applied == 0,
            "store-recall-peephole removed different-register recall without value proof");
    require(result.ops.size() == 3, "store-recall-peephole changed different-register pair");
  }

  {
    const core::passes::PassResult result =
        run_store_recall_peephole({recall("2"), store("3"), recall("2"), stop("halt")});
    require(result.applied == 1,
            "store-recall-peephole did not remove different-register recall with value proof");
    require(result.ops.size() == 3,
            "store-recall-peephole produced wrong op count for value-proof pair");
    require(result.ops.at(0).kind == IrKind::Recall,
            "store-recall-peephole removed the original value producer");
    require(result.ops.at(1).kind == IrKind::Store,
            "store-recall-peephole removed the value-preserving store");
    require(result.ops.at(2).kind == IrKind::Stop,
            "store-recall-peephole did not leave following stop");
  }

  {
    const core::passes::PassResult result =
        run_store_recall_peephole({recall("2"), store("2"), plain(0x00), store("0"),
                                   recall("2"), stop("halt")});
    require(result.applied == 0,
            "store-recall-peephole removed recall after a decimal literal overwrote X");
    require(result.ops.size() == 6,
            "store-recall-peephole changed literal-invalidated recall proof fixture");
    require(result.ops.at(4).kind == IrKind::Recall,
            "store-recall-peephole dropped the recall needed after literal entry");
  }

  {
    const core::passes::PassResult result =
        run_store_recall_peephole({known_target_indirect_store("8", "2"),
                                   known_target_indirect_recall("8", "2"), stop("halt")});
    require(result.applied == 1,
            "store-recall-peephole did not remove stable indirect same-target recall");
    require(result.ops.size() == 2,
            "store-recall-peephole produced wrong op count for stable indirect pair");
    require(result.ops.at(0).kind == IrKind::IndirectStore,
            "store-recall-peephole removed stable indirect store");
  }

  {
    const core::passes::PassResult result =
        run_store_recall_peephole({known_target_indirect_store("4", "2"),
                                   known_target_indirect_recall("4", "2"), stop("halt")});
    require(result.applied == 0, "store-recall-peephole removed mutating indirect selector pair");
    require(result.ops.size() == 3,
            "store-recall-peephole changed mutating indirect selector pair");
  }

  {
    const core::passes::PassResult result =
        run_store_recall_peephole({store("2"), recall("2"), plain(0x0c), stop("halt")});
    require(result.applied == 0, "store-recall-peephole removed immediate X2 restore sync");
    require(result.ops.size() == 4, "store-recall-peephole changed immediate X2 restore context");
  }

  {
    const std::vector<IrOp> program = {
        recall("2"),
        plain(0x0e),
        store("1"),
        recall("1"),
        plain(0x10),
        stop("halt"),
    };
    const auto x2_register_states = core::passes::compute_x2_register_states(program);
    core::passes::X2ValueStatesOptions value_options;
    value_options.track_register_memory = true;
    const auto x2_value_states =
        core::passes::compute_x2_value_states(program, value_options);
    const core::passes::DirectReturnAnalysisContext context =
        core::passes::direct_return_analysis_context(program);

    const std::optional<core::passes::RecallRemovalAnalysis> analysis =
        core::passes::analyze_recall_removal(program, 3, x2_register_states.at(3),
                                             x2_value_states.at(3), &context);
    require(analysis.has_value(), "recall-removal analysis rejected duplicate-Y fixture");
    require(analysis->exposes_stack_lift,
            "recall-removal analysis did not detect exposed stack lift");
    require(!analysis->exposes_x2_restore,
            "recall-removal analysis reported unexpected X2 restore exposure");
    require(!analysis->removable,
            "recall-removal analysis removed stack-lifting recall before scheduler");

    const std::optional<core::passes::RecallRemovalStackSchedulerPlan> plan =
        core::passes::plan_recall_removal_with_stack_scheduler(
            program, 3, x2_register_states.at(3), x2_value_states.at(3), context);
    require(plan.has_value(), "recall-removal scheduler rejected duplicate-Y fixture");
    require(plan->stack_lift_producer_index.has_value() &&
                *plan->stack_lift_producer_index == 1,
            "recall-removal scheduler did not find the previous duplicate-Y producer");
    require(plan->stack_lift_already_supplied,
            "recall-removal scheduler did not accept duplicate-Y stack proof");
    require(plan->removable,
            "recall-removal scheduler did not make the redundant recall removable");
  }

  {
    const std::vector<IrOp> program = {
        recall("2"),
        plain(0x0e),
        store("1"),
        recall("1"),
        plain(0x10),
        stop("halt"),
    };
    const auto x2_register_states = core::passes::compute_x2_register_states(program);
    core::passes::X2ValueStatesOptions value_options;
    value_options.track_register_memory = true;
    const auto x2_value_states =
        core::passes::compute_x2_value_states(program, value_options);
    const core::passes::DirectReturnAnalysisContext context =
        core::passes::direct_return_analysis_context(program);
    const std::set<int> removed_indexes = {1};
    core::passes::RecallRemovalStackSchedulerOptions options;
    options.removed_indexes = &removed_indexes;

    const std::optional<core::passes::RecallRemovalStackSchedulerPlan> plan =
        core::passes::plan_recall_removal_with_stack_scheduler(
            program, 3, x2_register_states.at(3), x2_value_states.at(3), context, options);
    require(plan.has_value(), "recall-removal scheduler rejected invalidated-producer fixture");
    require(plan->stack_lift_producer_index.has_value() &&
                *plan->stack_lift_producer_index == 1,
            "recall-removal scheduler lost the invalidated duplicate-Y producer");
    require(!plan->stack_lift_already_supplied,
            "recall-removal scheduler accepted an invalidated duplicate-Y producer");
    require(!plan->removable,
            "recall-removal scheduler removed recall with invalidated duplicate-Y producer");
  }

  {
    IrOp preload_c = recall("c");
    preload_c.meta.comment = "preload const 100";
    IrOp preload_d = recall("d");
    preload_d.meta.comment = "preload const 100";
    const std::vector<IrOp> program = {preload_c, preload_d, stop("halt")};
    core::passes::X2ValueStatesOptions value_options;
    value_options.track_register_memory = true;
    const auto x2_value_states =
        core::passes::compute_x2_value_states(program, value_options);
    const core::passes::DirectReturnAnalysisContext context =
        core::passes::direct_return_analysis_context(program);

    const std::optional<core::passes::RecallRemovalAnalysis> analysis =
        core::passes::analyze_recall_removal(program, 1, std::nullopt,
                                             x2_value_states.at(1), &context);
    require(analysis.has_value(), "recall-removal analysis rejected preload value fixture");
    require(analysis->value_proof.has_value() && analysis->value_proof->in_x,
            "recall-removal did not prove preloaded constant already in X");
    require(analysis->redundant_sync_value,
            "recall-removal did not prove preloaded constant already synced in X2");
  }

  {
    const core::passes::PassResult result =
        run_pre_shift_stack_lift({plain(0x0e), recall("1"), plain(0x12), stop("halt")});
    require(result.applied == 1,
            "pre-shift-stack-lift did not remove lift supplied by following recall");
    require(result.ops.size() == 3, "pre-shift-stack-lift produced wrong op count before recall");
    require(result.ops.at(0).kind == IrKind::Recall,
            "pre-shift-stack-lift removed following recall");
    require(result.optimizations.size() == 1, "pre-shift-stack-lift did not report optimization");
    require(result.optimizations.at(0).name == "pre-shift-stack-lift",
            "pre-shift-stack-lift reported wrong optimization name");
  }

  {
    const core::passes::PassResult result = run_pre_shift_stack_lift({plain(0x0e), stop("halt")});
    require(result.applied == 1, "pre-shift-stack-lift did not remove lift before terminal halt");
    require(result.ops.size() == 1 && result.ops.at(0).kind == IrKind::Stop,
            "pre-shift-stack-lift changed terminal halt");
  }

  {
    const core::passes::PassResult result =
        run_pre_shift_stack_lift({plain(0x0e), stop("pause"), plain(0x10), stop("halt")});
    require(result.applied == 0, "pre-shift-stack-lift removed lift before resumable pause");
  }

  {
    const core::passes::PassResult result =
        run_pre_shift_stack_lift({recall("1"), plain(0x0e), plain(0x0a), stop("halt")});
    require(result.applied == 1,
            "pre-shift-stack-lift did not remove post-recall lift before stack-safe op");
    require(result.ops.size() == 3, "pre-shift-stack-lift produced wrong op count after recall");
    require(result.ops.at(0).kind == IrKind::Recall,
            "pre-shift-stack-lift removed previous recall");
  }

  {
    const core::passes::PassResult result =
        run_pre_shift_stack_lift({recall("1"), plain(0x0e), plain(0x10), stop("halt")});
    require(result.applied == 0,
            "pre-shift-stack-lift removed lift before stack-consuming command");
  }

  {
    const core::passes::PassResult result =
        run_jump_thread({jump_to("A"), label("A"), jump_to("B"), label("B"), stop("halt")});
    require(result.applied >= 1, "jump-thread did not chase jump-to-jump trampoline");
    require(result.ops.at(0).kind == IrKind::Jump,
            "jump-thread removed the original jump instead of retargeting it");
    require(std::get<std::string>(result.ops.at(0).target) == "B",
            "jump-thread retargeted to the wrong final label");
    require(result.optimizations.size() == 1, "jump-thread did not report optimization");
    require(result.optimizations.at(0).name == "jump-thread",
            "jump-thread reported wrong optimization name");
  }

  {
    const core::passes::PassResult result = run_jump_thread(
        {cjump_to("x=0", 0x57, "A"), label("A"), jump_to("B"), label("B"), stop("halt")});
    require(result.applied == 1, "jump-thread did not retarget conditional jump trampoline");
    require(std::get<std::string>(result.ops.at(0).target) == "B",
            "jump-thread retargeted conditional jump to wrong label");
  }

  {
    const core::passes::PassResult result = run_flow_x_reuse(
        {recall("4"), jump_to("tail"), plain(0x00), label("tail"), recall("4"), store("5")});
    require(result.applied == 1, "flow-x-reuse did not drop recall reached through direct jump");
    require(result.optimizations.size() == 1, "flow-x-reuse did not report optimization");
    require(result.optimizations.at(0).name == "flow-x-reuse",
            "flow-x-reuse reported wrong optimization name");
  }

  {
    const core::passes::PassResult result =
        run_flow_x_reuse({recall("4"), known_target_indirect_jump("8", 3), stop("halt"),
                          label("tail"), recall("4"), stop("halt")});
    require(result.applied == 1, "flow-x-reuse did not follow proved stable indirect flow target");
  }

  {
    const core::passes::PassResult result = run_flow_x_reuse(
        {recall("4"), indirect_jump("8"), stop("halt"), label("tail"), recall("4"), stop("halt")});
    require(result.applied == 0, "flow-x-reuse optimized across unknown indirect flow target");
  }

  {
    const core::passes::PassResult result =
        run_flow_x_reuse({recall("1"), known_target_indirect_jump("1", 3), stop("halt"),
                          label("tail"), recall("1"), stop("halt")});
    require(result.applied == 0, "flow-x-reuse preserved mutated indirect selector X fact");
  }

  {
    const core::passes::PassResult result =
        run_flow_x_reuse({recall("5"), jump_to("tail"), plain(0x00), label("tail"),
                          known_target_indirect_recall("7", "5"), store("6")});
    require(result.applied == 1,
            "flow-x-reuse did not drop stable indirect recall with target already in X");
  }

  {
    const core::passes::PassResult result =
        run_flow_x_reuse({plain(0x02), store("1"), call_to("inc"), stop("halt"), label("inc"),
                          recall("1"), plain(0x01), plain(0x10), ret()});
    require(result.applied == 0,
            "flow-x-reuse removed a function parameter recall whose stack lift feeds +");
  }

  {
    const core::passes::PassResult result = run_flow_x_reuse(
        {label("loop"), plain(0x02), store("1"), call_to("inc"), stop("halt"), jump_to("loop"),
         label("inc"), recall("1"), plain(0x01), plain(0x10), ret()});
    require(result.applied == 1,
            "flow-x-reuse did not carry the call-entry X proof into the direct callee");
    require(result.ops.size() == 10,
            "flow-x-reuse produced wrong op count for direct callee X proof");
    const bool kept_target_recall =
        std::any_of(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
          return op.kind == IrKind::Recall && op.register_name == "1";
        });
    require(!kept_target_recall,
            "flow-x-reuse kept recall removed by the TypeScript direct-callee proof");
  }

  {
    const core::passes::PassResult result = run_branch_target_x_reuse(
        {recall("6"), cjump_to("x=0", 0x57, "target"), jump_to("end"), label("target"), recall("6"),
         plain(0x32), label("end"), stop("halt")});
    require(result.applied == 1,
            "branch-target-x-reuse did not drop recall at direct branch target");
    require(result.optimizations.size() == 1, "branch-target-x-reuse did not report optimization");
    require(result.optimizations.at(0).name == "branch-target-x-reuse",
            "branch-target-x-reuse reported wrong optimization name");
  }

  {
    const core::passes::PassResult result =
        run_branch_target_x_reuse({recall("6"), cjump_to("x=0", 0x57, "target"), stop("halt"),
                                   label("target"), recall("6"), plain(0x32), stop("halt")});
    require(result.applied == 1, "branch-target-x-reuse did not treat stop as target separator");
  }

  {
    const core::passes::PassResult result = run_branch_target_x_reuse(
        {known_target_indirect_recall("7", "6"), cjump_to("x=0", 0x57, "target"), jump_to("end"),
         label("target"), known_target_indirect_recall("8", "6"), plain(0x32), label("end"),
         stop("halt")});
    require(result.applied == 1,
            "branch-target-x-reuse did not drop stable indirect target recall");
  }

  {
    const core::passes::PassResult result = run_branch_target_x_reuse(
        {recall("1"), known_target_indirect_cjump("1", 4), jump_to("end"), label("target"),
         recall("1"), plain(0x32), label("end"), stop("halt")});
    require(result.applied == 0,
            "branch-target-x-reuse removed target recall for mutating indirect selector");
  }

  {
    // There is no command 12 in either old fixture. A numeric spelling alone
    // is not a target identity and cannot justify a shrinking branch rewrite.
    for (const IrOp& branch : {numeric_jump(12), numeric_cjump(12)}) {
      const std::vector<IrOp> incomplete = {
          plain(0x01), plain(0x02), store("7"), branch, stop("halt")};
      const auto result = run_stable_indirect_flow(incomplete);
      require(result.applied == 0 &&
                  ir_ops_to_json(result.ops) == ir_ops_to_json(incomplete),
              "stable-indirect-flow must reject nonexistent numeric destinations");
    }
  }

  {
    const core::passes::PassResult result = run_stable_indirect_flow(
        {plain(0x01), plain(0x02), store("4"), numeric_jump(13), stop("halt")});
    require(result.applied == 0, "stable-indirect-flow rewrote through a mutating selector");
  }

  {
    const core::passes::PassResult result = run_stable_indirect_flow(
        {plain(0x01), plain(0x02), store("7"), jump_to("late"), label("late"), stop("halt")});
    require(result.applied == 0, "stable-indirect-flow rewrote unresolved label target");
  }

  {
    IrOp terminal = stop("halt");
    terminal.meta.stop_disposition = StopDisposition::Terminal;
    const core::passes::PassResult result =
        run_indirect_memory_table({plain(0x02), store("7"), recall("2"), terminal});
    require(result.applied == 1,
            "indirect-memory-table did not rewrite direct recall through stable selector");
    require(result.optimizations.size() == 1, "indirect-memory-table did not report optimization");
    require(result.ops.at(2).kind == IrKind::IndirectRecall,
            "indirect-memory-table emitted wrong op kind for recall");
    require(result.ops.at(2).register_name == "7",
            "indirect-memory-table used wrong selector for recall");
    require(result.ops.at(2).opcode == 0xd7,
            "indirect-memory-table emitted wrong indirect recall opcode");
  }

  {
    IrOp terminal = stop("halt");
    terminal.meta.stop_disposition = StopDisposition::Terminal;
    const core::passes::PassResult result =
        run_indirect_memory_table({plain(0x02), store("8"), plain(0x09), store("2"), terminal});
    require(result.applied == 1,
            "indirect-memory-table did not rewrite direct store through stable selector");
    require(result.ops.at(3).kind == IrKind::IndirectStore,
            "indirect-memory-table emitted wrong op kind for store");
    require(result.ops.at(3).register_name == "8",
            "indirect-memory-table used wrong selector for store");
    require(result.ops.at(3).opcode == 0xb8,
            "indirect-memory-table emitted wrong indirect store opcode");
  }

  {
    std::vector<IrOp> program;
    for (int i = 0; i < 13; ++i)
      program.push_back(plain(0x00));
    program.push_back(stop("halt"));
    program.push_back(numeric_jump(13));
    const core::passes::PassResult result = run_preloaded_indirect_flow(program);
    require(result.applied == 1,
            "preloaded-indirect-flow did not rewrite address-stable backward numeric branch");
    require(result.ops.back().kind == IrKind::IndirectJump,
            "preloaded-indirect-flow emitted wrong op kind");
    require(result.ops.back().register_name == "7",
            "preloaded-indirect-flow used wrong selector register");
    require(result.ops.back().opcode == 0x87, "preloaded-indirect-flow emitted wrong opcode");
    require(result.preloads.size() == 1, "preloaded-indirect-flow did not report selector preload");
    require(result.preloads.at(0).register_name == "7" && result.preloads.at(0).value == "C5",
            "preloaded-indirect-flow selected wrong preload");
  }

  {
    const core::passes::PassResult result =
        run_preloaded_indirect_flow({numeric_jump(48), stop("halt")});
    require(result.applied == 0,
            "preloaded-indirect-flow rewrote forward branch that can shift target addresses");
  }

  {
    std::vector<IrOp> program;
    for (int i = 0; i < 106; ++i)
      program.push_back(plain(0x00));
    program.push_back(numeric_jump(105));
    const core::passes::PassResult result =
        run_preloaded_indirect_flow(program, FeatureProfile::Mk61SMiniExpanded);
    require(result.applied == 1,
            "expanded preloaded-indirect-flow did not rewrite added-cell numeric branch");
    require(result.ops.back().kind == IrKind::IndirectJump &&
                result.ops.back().register_name == "7" && result.ops.back().opcode == 0x87,
            "expanded preloaded-indirect-flow emitted wrong indirect branch");
    require(result.preloads.size() == 1 && result.preloads.at(0).register_name == "7" &&
                result.preloads.at(0).value == "A5",
            "expanded preloaded-indirect-flow should preload A5 for physical cell 105");
  }

  {
    std::vector<IrOp> program;
    for (int i = 0; i < 48; ++i)
      program.push_back(plain(0x00));
    program.push_back(plain(0x09));
    program.push_back(numeric_jump(1));
    program.push_back(numeric_jump(48));
    const core::passes::PassResult result = run_preloaded_indirect_flow(program);
    require(result.applied == 2,
            "preloaded-indirect-flow did not rewrite super-dark-compatible target");
    require(result.preloads.size() == 2,
            "preloaded-indirect-flow reported wrong super-dark preload count");
    require(result.preloads.at(0).register_name == "7" && result.preloads.at(0).value == "B3",
            "preloaded-indirect-flow selected wrong first formal preload");
    require(result.preloads.at(1).register_name == "8" && result.preloads.at(1).value == "FA",
            "preloaded-indirect-flow selected wrong super-dark preload");
    const auto found = std::find_if(result.optimizations.begin(), result.optimizations.end(),
                                    [](const core::passes::AppliedOptimization& optimization) {
                                      return optimization.name == "preloaded-super-dark-flow";
                                    });
    require(found != result.optimizations.end(),
            "preloaded-indirect-flow did not report super-dark optimization");
  }

  {
    std::vector<IrOp> program = {
        plain(0x09),
        store("7"),
        label("helper"),
        plain(0x00),
        ret(),
        call_to("helper"),
        call_to("helper"),
        call_to("helper"),
        call_to("helper"),
        call_to("helper"),
    };
    const core::passes::PassResult result = run_runtime_indirect_call_flow(program);
    require(result.applied == 5,
            "runtime-indirect-call-flow did not rewrite repeated backward helper calls");
    require(result.optimizations.size() == 1,
            "runtime-indirect-call-flow did not report optimization");
    require(result.optimizations.at(0).name == "runtime-indirect-call-flow",
            "runtime-indirect-call-flow reported wrong optimization name");
    const int indirect_calls =
        static_cast<int>(std::count_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
          return op.kind == IrKind::IndirectCall && op.register_name == "7";
        }));
    require(indirect_calls == 5,
            "runtime-indirect-call-flow emitted wrong number of indirect calls");
  }

  {
    std::vector<IrOp> program = {
        label("leaf_a"), recall("8"), recall("9"), recall("a"), recall("b"),
        recall("c"),     recall("d"), recall("e"), ret(),
        label("leaf_b"), recall("8"), recall("9"), recall("a"), recall("b"),
        recall("c"),     recall("d"), recall("e"), ret(),
        call_to("leaf_a"), call_to("leaf_a"), call_to("leaf_a"),
        call_to("leaf_a"), call_to("leaf_a"), call_to("leaf_a"),
        call_to("leaf_b"), call_to("leaf_b"), call_to("leaf_b"),
        call_to("leaf_b"), call_to("leaf_b"), call_to("leaf_b"),
    };
    const core::passes::PassResult result = run_runtime_indirect_call_flow(program);
    require(result.applied == 12,
            "runtime-indirect-call-flow did not reuse one selector across leaf-call phases");
    const int selector_charges =
        static_cast<int>(std::count_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
          return op.kind == IrKind::Store && op.register_name == "7" &&
                 op.meta.comment.has_value() &&
                 op.meta.comment->starts_with("runtime indirect call selector ");
        }));
    require(selector_charges == 2,
            "phase selector reuse should charge R7 once for each leaf-call phase");
    const int indirect_calls =
        static_cast<int>(std::count_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
          return op.kind == IrKind::IndirectCall && op.register_name == "7";
        }));
    require(indirect_calls == 12,
            "phase selector reuse emitted calls through more than one stable register");
    const auto phase_reuse =
        std::find_if(result.optimizations.begin(), result.optimizations.end(),
                     [](const core::passes::AppliedOptimization& optimization) {
                       return optimization.name ==
                              "runtime-indirect-call-phase-selector-reuse";
                     });
    require(phase_reuse != result.optimizations.end(),
            "runtime-indirect-call-flow did not report phase selector reuse");
  }

  {
    auto x_argument_call_to = [](std::string target) {
      IrOp op = call_to(std::move(target));
      op.meta.roles.push_back("x-argument-call");
      return op;
    };
    std::vector<IrOp> program = {
        plain(0x09),
        store("7"),
        label("helper"),
        plain(0x00),
        ret(),
        x_argument_call_to("helper"),
        x_argument_call_to("helper"),
        x_argument_call_to("helper"),
        x_argument_call_to("helper"),
        x_argument_call_to("helper"),
    };
    const core::passes::PassResult result = run_runtime_indirect_call_flow(program);
    require(result.applied == 0,
            "runtime-indirect-call-flow must not clobber X-argument calls");
    const int indirect_calls =
        static_cast<int>(std::count_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
          return op.kind == IrKind::IndirectCall;
        }));
    require(indirect_calls == 0,
            "runtime-indirect-call-flow rewrote an X-argument call");
  }

  {
    std::vector<IrOp> program = {
        label("helper"),   recall("7"),
        recall("8"),       recall("9"),
        recall("a"),       recall("b"),
        recall("c"),       recall("d"),
        recall("e"),       ret(),
        call_to("helper"), call_to("helper"),
        call_to("helper"), call_to("helper"),
        call_to("helper"),
    };
    const core::passes::PassResult result = run_runtime_indirect_call_flow(program);
    require(result.applied == 0,
            "runtime-indirect-call-flow borrowed a register used by helper body");
  }

  {
    const core::passes::X2StackEffectAnalysis effect =
        core::passes::analyze_x2_stack_effect(nullptr);
    require(effect.x2_effect == X2Effect::Unknown,
            "analyze_x2_stack_effect should treat missing op as unknown X2 effect");
    require(effect.stack_effect == StackEffect::Unknown,
            "analyze_x2_stack_effect should treat missing op as unknown stack effect");
    require(effect.stack_barrier,
            "analyze_x2_stack_effect should mark unknown stack effect as barrier");
  }

  {
    IrOp raw = plain(0x10);
    raw.meta.raw = true;
    const core::passes::X2StackEffectAnalysis effect = core::passes::analyze_x2_stack_effect(raw);
    require(effect.x2_effect == X2Effect::Unknown,
            "analyze_x2_stack_effect should treat raw ops as unknown X2 effect");
    require(effect.stack_effect == StackEffect::Unknown,
            "analyze_x2_stack_effect should treat raw ops as unknown stack effect");
  }

  {
    const core::passes::X2StackEffectAnalysis effect =
        core::passes::analyze_x2_stack_effect(plain(0x0e));
    require(effect.stack_shifts, "analyze_x2_stack_effect did not mark В↑ as stack shift");
    require(effect.x2_affects, "analyze_x2_stack_effect did not mark В↑ as X2-affecting");
    require(effect.stack_lift_and_x2_sync,
            "analyze_x2_stack_effect did not mark В↑ as stack lift and X2 sync");
  }

  {
    const core::passes::X2StackEffectAnalysis effect =
        core::passes::analyze_x2_stack_effect(plain(0x0d));
    require(effect.hard_x2_overwrite_without_stack_use,
            "analyze_x2_stack_effect did not mark Cx as hard X2 overwrite");
    require(effect.stack_preserves, "analyze_x2_stack_effect did not preserve stack for Cx");
  }

  {
    const core::passes::X2StackEffectAnalysis effect =
        core::passes::analyze_x2_stack_effect(plain(0x32));
    require(!effect.hard_x2_overwrite_without_stack_use,
            "analyze_x2_stack_effect must not treat input-dependent K sign as a hard overwrite");
  }

  {
    const core::passes::X2StackEffectAnalysis effect =
        core::passes::analyze_x2_stack_effect(plain(0x10));
    require(effect.stack_consumes, "analyze_x2_stack_effect did not mark + as stack consumer");
    require(effect.x2_preserves, "analyze_x2_stack_effect did not mark + as X2-preserving");
  }

  {
    const std::vector<IrOp> program = {call_to("helper"), label("helper"), plain(0x54), ret()};
    const core::passes::DirectReturnAnalysisContext context =
        core::passes::direct_return_analysis_context(program);
    const std::optional<int> target =
        core::passes::direct_call_target_index(program.at(0), context);
    require(target.has_value() && *target == 1,
            "direct_return_analysis_context resolved direct call to wrong label index");
    const bool transparent = core::passes::direct_call_returns_through_transparent_range(
        program, program.at(0), context,
        [](const IrOp& op) { return op.kind == IrKind::Plain && op.opcode == 0x54; });
    require(transparent,
            "direct_call_returns_through_transparent_range rejected transparent return helper");
  }

  {
    const std::vector<IrOp> program = {call_to("helper"), label("helper"), plain(0x0d), ret()};
    const core::passes::DirectReturnAnalysisContext context =
        core::passes::direct_return_analysis_context(program);
    const bool transparent = core::passes::direct_call_returns_through_transparent_range(
        program, program.at(0), context,
        [](const IrOp& op) { return op.kind == IrKind::Plain && op.opcode == 0x54; });
    require(!transparent,
            "direct_call_returns_through_transparent_range accepted nontransparent helper");
  }

  {
    const std::vector<IrOp> program = {
        call_to("outer"), label("inner"),   plain(0x54), ret(),
        label("outer"),   call_to("inner"), ret(),
    };
    const core::passes::DirectReturnAnalysisContext context =
        core::passes::direct_return_analysis_context(program);
    const bool transparent =
        core::passes::known_return_call_returns_through_nested_transparent_range(
            program, program.at(0), context,
            [](const IrOp& op) { return op.kind == IrKind::Plain && op.opcode == 0x54; });
    require(transparent,
            "known_return_call_returns_through_nested_transparent_range rejected nested helper");
  }

  {
    const std::vector<IrOp> program = {
        known_target_indirect_call("7", 1),
        label("helper"),
        plain(0x54),
        ret(),
    };
    const core::passes::DirectReturnAnalysisContext context =
        core::passes::direct_return_analysis_context(program);
    const std::optional<int> target =
        core::passes::known_return_call_target_index(program.at(0), context);
    require(target.has_value() && *target == 2,
            "known_return_call_target_index resolved indirect call to wrong executable index");
    const bool transparent = core::passes::known_return_call_returns_through_transparent_range(
        program, program.at(0), context,
        [](const IrOp& op) { return op.kind == IrKind::Plain && op.opcode == 0x54; });
    require(transparent,
            "known_return_call_returns_through_transparent_range rejected proved indirect helper");
  }
}

} // namespace mkpro::tests
