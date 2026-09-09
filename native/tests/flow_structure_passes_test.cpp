#include "mkpro/compiler.hpp"
#include "mkpro/core/passes/conditional_branch_trampoline.hpp"
#include "mkpro/core/passes/call_continuation_composition.hpp"
#include "mkpro/core/passes/dead_code_after_halt.hpp"
#include "mkpro/core/passes/jump_thread.hpp"
#include "mkpro/core/passes/jump_to_next.hpp"
#include "mkpro/core/passes/redundant_prologue.hpp"
#include "mkpro/core/passes/return_suffix_gadget.hpp"
#include "mkpro/core/passes/shared_straight_line_helper.hpp"
#include "mkpro/core/passes/shared_terminal_tail.hpp"
#include "mkpro/core/passes/tail_branch_inversion.hpp"
#include "mkpro/core/passes/tail_call.hpp"

#include "ir_pass_test_support.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace mkpro::tests {

// Traceability:
// - tests/compiler/passes.test.ts (flow/structure-pass cases): jump-thread,
//   jump-to-next-threading, conditional-branch-trampoline, tail-call-lowering,
//   tail-branch-inversion, return-suffix-gadget, shared-terminal-tail,
//   shared-straight-line-helper, and redundant-prologue-elimination. These
//   passes had no dedicated native ratchet before; every assertion below is a
//   1:1 port of its TS counterpart.

namespace {

int machine_cell_count(const std::vector<IrOp>& ops) {
  int count = 0;
  for (const MachineItem& item : mkpro::lower_ir_to_machine(ops)) {
    if (item.kind != MachineItemKind::Label)
      count += 1;
  }
  return count;
}

std::string target_str(const IrOp& op) {
  return std::holds_alternative<std::string>(op.target) ? std::get<std::string>(op.target)
                                                        : std::string{};
}

int find_label(const std::vector<IrOp>& ops, const std::string& name) {
  for (int i = 0; i < static_cast<int>(ops.size()); ++i) {
    if (ops.at(static_cast<std::size_t>(i)).kind == IrKind::Label &&
        ops.at(static_cast<std::size_t>(i)).name == name)
      return i;
  }
  return -1;
}

bool contains_label(const std::vector<IrOp>& ops, const std::string& name) {
  return find_label(ops, name) >= 0;
}

int count_kind(const std::vector<IrOp>& ops, IrKind kind) {
  int count = 0;
  for (const IrOp& op : ops)
    if (op.kind == kind)
      count += 1;
  return count;
}

int count_call_target(const std::vector<IrOp>& ops, const std::string& target) {
  int count = 0;
  for (const IrOp& op : ops)
    if (op.kind == IrKind::Call && target_str(op) == target)
      count += 1;
  return count;
}

} // namespace

void flow_structure_passes_match_typescript_contract() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions noop = noop_options();
  const core::passes::PassContext ctx{.options = noop};


  // --- exact terminal reachability ----------------------------------------
  {
    CompileOptions terminal_options = noop;
    terminal_options.exact_terminal_dead_code_elimination = true;
    const core::passes::PassContext terminal_context{.options = terminal_options};
    const auto terminal = [](bool error) {
      IrOp op = error ? plain(0x29, "K /") : halt();
      op.meta.stop_disposition = StopDisposition::Terminal;
      return op;
    };
    for (const bool error : {false, true}) {
      const std::vector<IrOp> program{
          call("worker"), recall("1"), terminal(error),
          plain(7, "7"), store("2"),
          label("worker"), recall("3"), plain(2, "2"),
          plain(0x12, "*"), store("1"), ret()};
      const auto ordinary = core::passes::dead_code_after_halt(program, ctx);
      require(ordinary.applied == 0, "ordinary terminal geometry remains a competitor");
      const auto optimized = core::passes::dead_code_after_halt(program, terminal_context);
      require(optimized.applied == 2 &&
                  machine_cell_count(optimized.ops) == machine_cell_count(program) - 2,
              "terminal reachability removes dead instructions but retains the called helper");
      const auto observe = [&](const std::vector<IrOp>& ops) {
        const auto resolved = mkpro::resolve_machine_items(lower_ir_to_machine(ops));
        require(resolved.diagnostics.empty(), "terminal reachability listing resolves");
        std::vector<int> codes;
        for (const auto& step : resolved.steps) codes.push_back(step.opcode);
        emulator::MK61 calc;
        require(calc.load_program(codes).diagnostics.empty(), "terminal reachability ROM load");
        calc.set_register("1", "0").set_register("2", "17").set_register("3", "6")
            .set_register("x", "23").set_register("y", "29")
            .set_register("z", "31").set_register("t", "37").set_register("x1", "41");
        calc.press_sequence({"В/О", "С/П"});
        require(calc.run_until_stable(600, 5).stopped, "terminal reachability reaches its halt");
        require(std::stoi(calc.read_register("1")) == 12 &&
                    std::stoi(calc.read_register("2")) == 17,
                "the live helper returns; the dead write is never executed");
        std::vector<std::string> values{calc.display_text()};
        for (const auto* reg : {"x", "y", "z", "t", "x1", "1", "2", "3"})
          values.push_back(calc.read_register(reg));
        calc.press(".");
        values.push_back(calc.display_text());
        return values;
      };
      require(observe(program) == observe(optimized.ops),
              "terminal erasure preserves full stack, last-X, X2 and the helper return");
    }

    const auto unchanged = [&](const std::vector<IrOp>& program) {
      const auto result = core::passes::dead_code_after_halt(program, terminal_context);
      require(result.applied == 0, "unsafe terminal geometry is not erased");
      require_ops_equal(result.ops, program, "unsafe terminal geometry stays byte-identical");
    };
    IrOp resumable_error = terminal(true);
    resumable_error.meta.stop_disposition = StopDisposition::Resumable;
    unchanged({resumable_error, plain(0x54, "K NOP"), recall("1"), halt()});
    IrOp raw_error = terminal(true);
    raw_error.meta.raw = true;
    unchanged({raw_error, plain(7, "7"), halt()});
    IrOp anchored_error = terminal(true);
    anchored_error.meta.manual_interaction =
        ManualInteractionAnchor{.protocol_id = 0, .phase = 0,
                                .kind = ManualInteractionAnchorKind::PromptStop};
    unchanged({anchored_error, plain(7, "7"), halt()});
    unchanged({numeric_jump(4), halt(), plain(9, "9"), label("target"), recall("1"), halt()});
    unchanged({indirect_jump("7"), halt(), plain(9, "9"), halt()});
    IrOp unknown_stop = halt();
    unknown_stop.meta.stop_disposition = StopDisposition::Unknown;
    unchanged({unknown_stop, plain(7, "7"), halt()});

    // Ordinary prompts retain both their continuation and its side effects.
    IrOp prompt = halt();
    prompt.semantic = "show";
    prompt.meta.stop_disposition = StopDisposition::Resumable;
    unchanged({recall("1"), prompt, plain(7, "7"), store("2"), halt()});

    const std::string source =
        "program TerminalError {\n  halt(\"ЕГГОГ\")\n  show(7)\n}\n";
    const auto ordinary = compile_source(source, noop);
    const auto compiled = compile_source(source, terminal_options);
    const auto ordinary_again = compile_source(source, noop);
    require(compiled.implemented && compiled.diagnostics.empty() &&
                compiled.steps.size() == 1U && compiled.steps.front().opcode == 0x29,
            "source terminal error erasure must not retain an unreachable show");
    require(ordinary.implemented && ordinary.steps.size() > compiled.steps.size() &&
                ordinary.hex == ordinary_again.hex,
            "ordinary/erased lowering variants need distinct reproducible cache keys");
  }

  // Conservative erasure preserves local rewrite/input anchors without
  // freezing all unrelated unreachable blocks in the compilation unit.
  {
    IrOp raw_nop = plain(0x54, "K NOP");
    raw_nop.meta.raw = true;
    IrOp prompt = halt();
    prompt.meta.stop_disposition = StopDisposition::Resumable;
    prompt.meta.manual_interaction = ManualInteractionAnchor{
        .protocol_id = 4, .phase = -1, .kind = ManualInteractionAnchorKind::PromptStop};
    IrOp input = store("7");
    input.meta.manual_interaction = ManualInteractionAnchor{
        .protocol_id = 4, .phase = 0, .kind = ManualInteractionAnchorKind::ContinuousResume};
    for (bool manual : {false, true}) {
      std::vector<IrOp> program{
          jump("live"), plain(9, "9"), store("2"), label("live")};
      if (manual) {
        program.push_back(prompt);
        program.push_back(input);
      } else {
        program.push_back(raw_nop);
      }
      program.push_back(recall("7"));
      IrOp finish = halt();
      finish.meta.stop_disposition = StopDisposition::Terminal;
      program.push_back(finish);
      const auto result = core::passes::dead_code_after_halt(program, ctx);
      require(result.applied == 2 &&
                  machine_cell_count(result.ops) == machine_cell_count(program) - 2,
              "a local NOP or input anchor must not freeze unrelated dead blocks");
      require(std::count_if(result.ops.begin(), result.ops.end(),
                            [](const IrOp& op) {
                              return op.meta.raw || op.meta.manual_interaction.has_value();
                            }) == (manual ? 2 : 1),
              "every protected instruction and manual phase remains intact");
      const auto flow = core::build_post_layout_control_flow(lower_ir_to_machine(result.ops));
      require(flow.proved, "erasure retains valid manual/ordinary control flow: " +
                               (flow.reasons.empty() ? std::string{} : flow.reasons.front()));
    }
    const std::vector<IrOp> detached{
        jump("live"), raw_nop, plain(7, "7"), label("live"), halt()};
    const auto kept = core::passes::dead_code_after_halt(detached, ctx);
    require_ops_equal(kept.ops, detached, "a detached protected NOP and its continuation survive");
    IrOp raw_digit = plain(9, "9");
    raw_digit.meta.raw = true;
    const std::vector<IrOp> opaque{
        jump("live"), plain(7, "7"), label("live"), raw_digit, halt()};
    require_ops_equal(core::passes::dead_code_after_halt(opaque, ctx).ops, opaque,
                      "arbitrary raw commands retain the conservative erasure barrier");
    IrOp symbolic = indirect_jump("7");
    symbolic.meta.indirect_flow_targets = std::vector<IrTarget>{std::string("live")};
    const std::vector<IrOp> symbolic_program{
        symbolic, plain(9, "9"), store("2"), label("live"), halt()};
    const auto symbolic_result = core::passes::dead_code_after_halt(symbolic_program, ctx);
    require(symbolic_result.applied == 2 &&
                symbolic_result.ops.front().meta.indirect_flow_targets ==
                    symbolic.meta.indirect_flow_targets,
            "typed symbolic indirect destinations survive pre-layout dead-code erasure");
    for (bool formal : {false, true}) {
      auto encoded = symbolic_program;
      if (formal)
        encoded.front().meta.indirect_flow_formal_targets = std::vector<int>{0x03};
      else
        encoded.front().meta.indirect_flow_targets = std::vector<IrTarget>{3};
      require_ops_equal(core::passes::dead_code_after_halt(encoded, ctx).ops, encoded,
                        "encoded indirect geometry is not moved by symbolic IR erasure");
    }
    const std::vector<IrOp> empty_return{ret(), recall("7"), halt()};
    require_ops_equal(core::passes::dead_code_after_halt(empty_return, ctx).ops, empty_return,
                      "an empty return stack must retain physical-01 execution");
  }

  // --- jump-thread --------------------------------------------------------
  {
    const std::vector<IrOp> program = {jump("A"), label("A"), jump("B"), label("B"), halt()};
    const auto result = core::passes::jump_thread_pass().run(program, ctx);
    require(result.applied >= 1, "jump-thread: applied >= 1");
    bool found = false;
    for (const IrOp& op : result.ops) {
      if (op.kind == IrKind::Jump) {
        require(target_str(op) == "B", "jump-thread: first jump retargeted to B");
        found = true;
        break;
      }
    }
    require(found, "jump-thread: a jump remains");
  }
  {
    IrOp source_call = call("wrapper");
    source_call.meta.semantic_call_origins = {17};
    IrOp forwarding_jump = jump("target");
    forwarding_jump.meta.semantic_call_origins = {91};
    const std::vector<IrOp> program = {
        source_call,          halt(), proc_start("wrapper"), forwarding_jump, proc_end("wrapper"),
        proc_start("target"), ret(),  proc_end("target")};
    const auto result = core::passes::jump_thread_pass().run(program, ctx);
    require_applied(result.applied, 1, "jump-thread: direct call through tail-jump wrapper");
    require(result.ops.front().kind == IrKind::Call && target_str(result.ops.front()) == "target",
            "jump-thread: call should target the forwarded procedure directly");
    require(result.ops.front().meta.semantic_call_origins == std::vector<std::uint64_t>({17, 91}),
            "jump-thread: call forwarding should preserve the opaque origin union");
  }

  // --- jump-to-next-threading ---------------------------------------------
  {
    const std::vector<IrOp> program = {jump("END"), label("END"), halt()};
    const auto result = core::passes::jump_to_next_threading_pass().run(program, ctx);
    require_applied(result.applied, 1, "jump-to-next-threading drops trailing jump");
    require(count_kind(result.ops, IrKind::Jump) == 0, "jump-to-next: no jump remains");
  }

  // --- conditional-branch-trampoline --------------------------------------
  {
    mkpro::CompileOptions options = noop;
    options.conditional_branch_trampoline = true;
    const core::passes::PassContext enabled{.options = options};
    const std::vector<IrOp> program = {recall("1"),       cjump("done"), plain(0x02, "2"),
                                       cjump("done"),      plain(0x03, "3"), label("done"),
                                       halt()};
    const auto result = core::passes::conditional_branch_trampoline_pass().run(program, enabled);
    require_applied(result.applied, 1, "conditional-branch-trampoline retargets");
    require(!result.optimizations.empty() &&
                result.optimizations.at(0).name == "conditional-branch-trampoline",
            "conditional-branch-trampoline: optimization name");
    require(contains_label(result.ops, "__conditional_branch_trampoline_0"),
            "conditional-branch-trampoline: hidden label added");
    const int label_index = find_label(result.ops, "__conditional_branch_trampoline_0");
    require(label_index >= 0 && result.ops.at(static_cast<std::size_t>(label_index)).hidden,
            "conditional-branch-trampoline: label is hidden");
    require(result.ops.size() > 1 && result.ops.at(1).kind == IrKind::CondJump &&
                target_str(result.ops.at(1)) == "__conditional_branch_trampoline_0" &&
                result.ops.at(1).condition == "==0",
            "conditional-branch-trampoline: cjump retargeted");
  }
  {
    const std::vector<IrOp> program = {cjump("done"), plain(0x02, "2"), cjump("done"),
                                       label("done"), halt()};
    const auto result = core::passes::conditional_branch_trampoline_pass().run(program, ctx);
    require_applied(result.applied, 0, "conditional-branch-trampoline disabled without candidate");
    require_ops_equal(result.ops, program, "conditional-branch-trampoline disabled: ops unchanged");
  }
  {
    IrOp blocked = cjump("done");
    blocked.meta.raw = true;
    const std::vector<IrOp> program = {blocked, plain(0x02, "2"), cjump("done"), label("done"),
                                       halt()};
    mkpro::CompileOptions options = noop;
    options.conditional_branch_trampoline = true;
    const core::passes::PassContext enabled{.options = options};
    const auto result = core::passes::conditional_branch_trampoline_pass().run(program, enabled);
    require_applied(result.applied, 0, "conditional-branch-trampoline keeps barrier branch");
    require_ops_equal(result.ops, program, "conditional-branch-trampoline barrier: ops unchanged");
  }

  // --- tail-call-lowering --------------------------------------------------
  {
    const std::vector<IrOp> program = {
        label("main"),       call("producer"), call("consumer"), jump("second"),
        label("second"),     call("producer"), call("consumer"), halt(),
        proc_start("producer"), plain(0x34, "K [x]"), ret(),
        proc_start("consumer"), plain(0x10, "+"), ret(),
    };
    const int before = machine_cell_count(program);
    const auto result = core::passes::call_continuation_composition_pass().run(program, ctx);
    require_applied(result.applied, 2,
                    "call-continuation-composition folds every identical continuation");
    require(machine_cell_count(result.ops) == before - 3,
            "call-continuation-composition saves two calls minus one tail-jump cell");
    require(count_kind(result.ops, IrKind::Call) == 2,
            "call-continuation-composition removes both consumer call sites");
    require(count_kind(result.ops, IrKind::Jump) == 2,
            "call-continuation-composition adds one producer tail jump");
    require(!result.optimizations.empty() &&
                result.optimizations.at(0).name == "call-continuation-composition",
            "call-continuation-composition optimization name");
  }
  {
    const std::vector<IrOp> mixed = {
        label("main"),       call("producer"), call("consumer"), jump("second"),
        label("second"),     call("producer"), plain(0x01, "1"), halt(),
        proc_start("producer"), plain(0x34, "K [x]"), ret(),
        proc_start("consumer"), plain(0x10, "+"), ret(),
    };
    const auto result = core::passes::call_continuation_composition_pass().run(mixed, ctx);
    require_applied(result.applied, 0,
                    "call-continuation-composition rejects mixed continuations");
    require_ops_equal(result.ops, mixed,
                      "call-continuation-composition preserves mixed continuations");
  }
  {
    const std::vector<IrOp> externally_entered = {
        label("main"),       call("producer"), call("consumer"),
        jump("producer_body"), proc_start("producer"), label("producer_body"),
        plain(0x34, "K [x]"), ret(), proc_start("consumer"),
        plain(0x10, "+"), ret(),
    };
    const auto result =
        core::passes::call_continuation_composition_pass().run(externally_entered, ctx);
    require_applied(result.applied, 0,
                    "call-continuation-composition rejects an external body entry");
    require_ops_equal(result.ops, externally_entered,
                      "call-continuation-composition preserves externally entered helper");
  }
  {
    std::vector<IrOp> program = {label("main"),    call("finish_turn"), jump("loop"),
                                  label("loop"),     halt(),              label("finish_turn"),
                                  cjump("done"),     halt(),              label("done"),
                                  ret()};
    require_applied(core::passes::tail_call_lowering_pass().run(program, ctx).applied, 0,
                    "tail-call-lowering rejects stops with unknown continuation semantics");
    for (IrOp& op : program)
      if (op.kind == IrKind::Stop)
        op.meta.stop_disposition = StopDisposition::Terminal;
    const auto result = core::passes::tail_call_lowering_pass().run(program, ctx);
    require_applied(result.applied, 2, "tail-call-lowering specializes single-continuation proc");
    require(result.ops.size() > 1 && result.ops.at(1).kind == IrKind::Jump &&
                target_str(result.ops.at(1)) == "finish_turn",
            "tail-call-lowering: call rewritten to jump finish_turn");
    require(count_kind(result.ops, IrKind::Return) == 0, "tail-call-lowering: no return remains");
    require(!result.ops.empty() && result.ops.back().kind == IrKind::Jump &&
                target_str(result.ops.back()) == "loop",
            "tail-call-lowering: terminal jump to loop");
  }
  {
    const std::vector<IrOp> program = {label("main"),        call("finish_turn"), jump("loop"),
                                       label("other"),       call("finish_turn"), jump("menu"),
                                       label("finish_turn"), ret()};
    const auto result = core::passes::tail_call_lowering_pass().run(program, ctx);
    require_applied(result.applied, 0, "tail-call-lowering refuses mixed continuations");
  }
  {
    const auto observe = [](const std::vector<IrOp>& ops) {
      const auto resolved = resolve_machine_items(lower_ir_to_machine(ops));
      require(resolved.diagnostics.empty(), "tail-return ROM fixture must resolve");
      std::vector<int> codes;
      for (const auto& step : resolved.steps)
        codes.push_back(step.opcode);
      emulator::MK61 calc;
      require(calc.load_program(codes).diagnostics.empty(),
              "tail-return ROM fixture must load");
      calc.set_register("b", "0");
      calc.press_sequence({"В/О", "С/П"});
      std::vector<std::string> observations;
      for (int phase = 0; phase < 3; ++phase) {
        require(calc.run_until_stable(200, 6).stopped,
                "tail-return fixture must reach every resumable stop");
        for (const std::string reg : {"X", "Y", "Z", "T", "X1", "0", "2"})
          observations.push_back(calc.read_register(reg));
        if (phase < 2)
          calc.press_sequence({"С/П"});
      }
      calc.press_sequence({"ВП"});
      observations.push_back(calc.display_text());
      return observations;
    };

    for (const bool indirect : {false, true}) {
      const std::vector<IrOp> program = {
          label("main"), plain(0x07, "7"), store("2"), pause(),
          call("worker"), pause(), call("worker"),
          indirect ? known_target_indirect_jump("b", 0) : jump("main"),
          proc_start("worker"), plain(0x01, "1"), store("0"), ret()};
      const auto result = core::passes::tail_call_lowering_pass().run(program, ctx);
      require_applied(result.applied, 0,
                      "tail-call-lowering must not replace continuation 00 with empty return 01");
      require_ops_equal(result.ops, program,
                        "mixed main-loop continuations must retain their call frames");
      require(observe(result.ops) == observe(program),
              "retained calls must preserve resumptions, stack, X1 and X2");

      // Reproduce the old, incorrect rewrite independently. The last return
      // skips cell 00, storing 1 in R2 instead of executing the initial 7.
      auto broken = program;
      broken[6] = jump("worker");
      broken.erase(broken.begin() + 7);
      require(observe(broken) != observe(program),
              "ROM must distinguish an empty return from the explicit loop continuation");
    }

    // A genuine tail call keeps the existing caller frame. It remains valid
    // even though manufacturing an empty main-loop return above is not.
    const std::vector<IrOp> framed = {
        label("main"), plain(0x07, "7"), store("2"), call("outer"), pause(), jump("main"),
        proc_start("outer"), call("leaf"), ret(),
        proc_start("leaf"), plain(0x01, "1"), store("0"), ret()};
    const auto framed_result = core::passes::tail_call_lowering_pass().run(framed, ctx);
    require_applied(framed_result.applied, 1,
                    "a genuine call-and-return must still become a tail jump");
    require(observe(framed_result.ops) == observe(framed),
            "tail calls with a real caller frame must preserve complete ROM observations");
  }
  {
    const std::vector<IrOp> program = {label("main"),        call("finish_turn"),  plain(0x01, "1"),
                                       call("finish_turn"),  jump("main"),         proc_start("finish_turn"),
                                       plain(0x02, "2"),     jump("shared_return"), proc_start("shared_return"),
                                       plain(0x03, "3"),     ret()};
    const auto result = core::passes::tail_call_lowering_pass().run(program, ctx);
    require_applied(result.applied, 0,
                    "a shared return tail must not manufacture an empty-stack loop return");
    require_ops_equal(result.ops, program, "shared return tails preserve mixed continuations");
  }
  {
    const std::vector<IrOp> program = {label("main"),  call("finish_turn"), jump("return_here"),
                                       label("return_here"), ret(),         cjump("finish_turn"),
                                       label("finish_turn"), ret()};
    const auto result = core::passes::tail_call_lowering_pass().run(program, ctx);
    require_applied(result.applied, 1, "tail-call-lowering call+return jump into tail jump");
    require(result.ops.size() > 2 && result.ops.at(1).kind == IrKind::Jump &&
                target_str(result.ops.at(1)) == "finish_turn",
            "tail-call-lowering: ops[1] jump finish_turn");
    require(result.ops.at(2).kind == IrKind::Label && result.ops.at(2).name == "return_here",
            "tail-call-lowering: ops[2] label return_here");
  }
  {
    const std::vector<IrOp> program = {label("main"),  call("finish_turn"), label("if_end"),
                                       ret(),          cjump("finish_turn"), label("finish_turn"),
                                       ret()};
    const auto result = core::passes::tail_call_lowering_pass().run(program, ctx);
    require_applied(result.applied, 1, "tail-call-lowering sees tail returns through labels");
    require(result.ops.size() > 2 && result.ops.at(1).kind == IrKind::Jump &&
                target_str(result.ops.at(1)) == "finish_turn",
            "tail-call-lowering: ops[1] jump finish_turn (label)");
    require(result.ops.at(2).kind == IrKind::Label && result.ops.at(2).name == "if_end",
            "tail-call-lowering: ops[2] label if_end");
  }
  {
    IrOp call;
    call.kind = IrKind::IndirectCall;
    call.register_name = "c";
    call.opcode = 0xac;
    call.meta.mnemonic = "К ПП c";
    call.meta.indirect_flow_targets = std::vector<IrTarget>{std::string("finish_turn")};
    const std::vector<IrOp> program = {call, ret(), label("finish_turn"), ret()};
    const auto result = core::passes::tail_call_lowering_pass().run(program, ctx);
    require_applied(result.applied, 1,
                    "tail-call-lowering handles an indirect call with immediate return");
    require(!result.ops.empty() && result.ops.front().kind == IrKind::IndirectJump &&
                result.ops.front().opcode == 0x8c && result.ops.front().register_name == "c",
            "tail-call-lowering preserves the selector while replacing КПП with КБП");
    require(result.ops.size() == 3,
            "tail-call-lowering should remove exactly the immediate return cell");
    require(result.ops.front().meta.mnemonic == "К БП c",
            "tail-call-lowering should name the indirect jump and its selector in IR");
    const auto lowered = mkpro::lower_ir_to_machine(result.ops);
    require(!lowered.empty() && lowered.front().mnemonic == "К БП c",
            "tail-call-lowering should retain the correct mnemonic in the machine listing");
  }

  // --- tail-branch-inversion ----------------------------------------------
  {
    const std::vector<IrOp> program = {plain(0x01, "1"),   cjump("else_path"), jump("terminal_tail"),
                                       label("else_path"), plain(0x02, "2"),   halt(),
                                       label("terminal_tail"), halt()};
    mkpro::CompileOptions options = noop;
    options.tail_branch_inversion = true;
    const core::passes::PassContext enabled{.options = options};
    const auto result = core::passes::tail_branch_inversion_pass().run(program, enabled);
    require_applied(result.applied, 1, "tail-branch-inversion inverts tail-jump then path");
    require(!result.optimizations.empty() &&
                result.optimizations.at(0).name == "tail-branch-inversion",
            "tail-branch-inversion: optimization name");
    require(result.ops.size() > 1 && result.ops.at(1).kind == IrKind::CondJump &&
                result.ops.at(1).condition == "!=0" &&
                target_str(result.ops.at(1)) == "terminal_tail" &&
                result.ops.at(1).opcode == 0x57,
            "tail-branch-inversion: inverted cjump");
    require(!contains_label(result.ops, "else_path"), "tail-branch-inversion: else_path removed");
  }

  // --- return-suffix-gadget -----------------------------------------------
  {
    const std::vector<IrOp> program = {label("first"),  plain(0x01, "1"), plain(0x02, "2"),
                                       plain(0x10, "+"), ret(),           label("second"),
                                       plain(0x02, "2"), plain(0x10, "+"), ret()};
    const auto result = core::passes::return_suffix_gadget_pass().run(program, ctx);
    require_applied(result.applied, 1, "return-suffix-gadget jumps into subroutine tail");
    require(!result.optimizations.empty() &&
                result.optimizations.at(0).name == "return-suffix-gadget",
            "return-suffix-gadget: optimization name");
    require(contains_label(result.ops, "__return_suffix_gadget_0"),
            "return-suffix-gadget: gadget label added");
    const int second = find_label(result.ops, "second");
    require(second >= 0 && result.ops.at(static_cast<std::size_t>(second + 1)).kind == IrKind::Jump &&
                target_str(result.ops.at(static_cast<std::size_t>(second + 1))) ==
                    "__return_suffix_gadget_0",
            "return-suffix-gadget: second jumps to gadget");
    require(machine_cell_count(result.ops) < machine_cell_count(program),
            "return-suffix-gadget: fewer cells");
  }
  {
    const std::vector<IrOp> program = {label("helper"), plain(0x01, "1"), plain(0x02, "2"),
                                       plain(0x10, "+"), ret(),           label("main"),
                                       plain(0x01, "1"), plain(0x02, "2"), plain(0x10, "+"),
                                       plain(0x03, "3"), halt()};
    const auto result = core::passes::return_suffix_gadget_pass().run(program, ctx);
    require_applied(result.applied, 1, "return-suffix-gadget calls tail when caller continues");
    const int main_index = find_label(result.ops, "main");
    require(main_index >= 0 &&
                result.ops.at(static_cast<std::size_t>(main_index + 1)).kind == IrKind::Call &&
                target_str(result.ops.at(static_cast<std::size_t>(main_index + 1))) ==
                    "__return_suffix_gadget_0",
            "return-suffix-gadget: main calls gadget");
    require(result.ops.at(static_cast<std::size_t>(main_index + 2)).kind == IrKind::Plain &&
                result.ops.at(static_cast<std::size_t>(main_index + 2)).opcode == 0x03,
            "return-suffix-gadget: continuation preserved");
    require(machine_cell_count(result.ops) < machine_cell_count(program),
            "return-suffix-gadget: fewer cells (continues)");
  }
  {
    const std::vector<IrOp> program = {label("main"),    recall("7"),         store("6"),
                                       call("inspect"),  jump("main"),        label("bat_jump"),
                                       call("random_coord"), store("6"),       jump("inspect"),
                                       label("inspect"), recall("6"),         ret(),
                                       label("random_coord"), plain(0x3b, "\u041a \u0421\u0427"),
                                       ret()};
    const auto result = core::passes::return_suffix_gadget_pass().run(program, ctx);
    require_applied(result.applied, 1, "return-suffix-gadget calls into existing tail-call body");
    const int main_index = find_label(result.ops, "main");
    require(main_index >= 0 &&
                result.ops.at(static_cast<std::size_t>(main_index + 2)).kind == IrKind::Call &&
                target_str(result.ops.at(static_cast<std::size_t>(main_index + 2))) ==
                    "__return_suffix_gadget_0",
            "return-suffix-gadget: main calls gadget (tail body)");
    require(result.ops.at(static_cast<std::size_t>(main_index + 3)).kind == IrKind::Jump &&
                target_str(result.ops.at(static_cast<std::size_t>(main_index + 3))) == "main",
            "return-suffix-gadget: jump main preserved");
    require(contains_label(result.ops, "__return_suffix_gadget_0"),
            "return-suffix-gadget: gadget label added (tail body)");
    require(machine_cell_count(result.ops) == machine_cell_count(program) - 1,
            "return-suffix-gadget: one fewer cell");
  }
  {
    const std::vector<IrOp> program = {label("first"),  plain(0x01, "1"), plain(0x02, "2"),
                                       plain(0x10, "+"), ret(),           label("second"),
                                       plain(0x02, "2"), plain(0x10, "+"), ret(),
                                       numeric_call(34)};
    const auto result = core::passes::return_suffix_gadget_pass().run(program, ctx);
    require_applied(result.applied, 0, "return-suffix-gadget avoids absolute numeric targets");
    require_ops_equal(result.ops, program, "return-suffix-gadget numeric: ops unchanged");
  }

  // --- shared-terminal-tail -----------------------------------------------
  {
    const std::vector<IrOp> program = {label("first"),  recall("1"),     store("2"),
                                       plain(0x0d, "Cx"), store("1"),     indirect_jump("e"),
                                       label("second"), recall("1"),     store("2"),
                                       plain(0x0d, "Cx"), store("1"),     indirect_jump("e")};
    const auto result = core::passes::shared_terminal_tail_pass().run(program, ctx);
    require_applied(result.applied, 1, "shared-terminal-tail straight-line tails");
    require(!result.optimizations.empty() &&
                result.optimizations.at(0).name == "shared-terminal-tail",
            "shared-terminal-tail: optimization name");
    require(contains_label(result.ops, "__shared_terminal_tail_0"),
            "shared-terminal-tail: tail label added");
    const int second = find_label(result.ops, "second");
    require(second >= 0 && result.ops.at(static_cast<std::size_t>(second + 1)).kind == IrKind::Jump &&
                target_str(result.ops.at(static_cast<std::size_t>(second + 1))) ==
                    "__shared_terminal_tail_0",
            "shared-terminal-tail: second jumps to tail");
    require(machine_cell_count(result.ops) == machine_cell_count(program) - 3,
            "shared-terminal-tail: three fewer cells");
  }
  {
    const std::vector<IrOp> program = {label("first"),  plain(0x00, "0"), pause(),
                                       indirect_jump("8"), label("second"), plain(0x00, "0"),
                                       pause(),          indirect_jump("8")};
    const auto result = core::passes::shared_terminal_tail_pass().run(program, ctx);
    require_applied(result.applied, 1, "shared-terminal-tail stop tails");
    require(!result.optimizations.empty() &&
                result.optimizations.at(0).name == "shared-terminal-tail",
            "shared-terminal-tail: optimization name (stop)");
    require(contains_label(result.ops, "__shared_terminal_tail_0"),
            "shared-terminal-tail: tail label added (stop)");
    const int second = find_label(result.ops, "second");
    require(second >= 0 && result.ops.at(static_cast<std::size_t>(second + 1)).kind == IrKind::Jump &&
                target_str(result.ops.at(static_cast<std::size_t>(second + 1))) ==
                    "__shared_terminal_tail_0",
            "shared-terminal-tail: second jumps to tail (stop)");
    require(machine_cell_count(result.ops) == machine_cell_count(program) - 1,
            "shared-terminal-tail: one fewer cell (stop)");
  }
  {
    const std::vector<IrOp> program = {label("first"),  recall("1"),     store("2"),
                                       jump("done"),    label("second"), recall("1"),
                                       store("2"),      jump("done"),    numeric_jump(20),
                                       label("done"),   halt()};
    const auto result = core::passes::shared_terminal_tail_pass().run(program, ctx);
    require_applied(result.applied, 0, "shared-terminal-tail avoids absolute numeric targets");
    require_ops_equal(result.ops, program, "shared-terminal-tail numeric: ops unchanged");
  }

  // --- shared-straight-line-helper ----------------------------------------
  {
    const std::vector<IrOp> program = {label("first"),  recall("1"),     recall("2"),
                                       plain(0x10, "+"), store("3"),     recall("4"),
                                       store("5"),       plain(0x20, "F pi"), label("second"),
                                       recall("1"),      recall("2"),    plain(0x10, "+"),
                                       store("3"),       recall("4"),    store("5"),
                                       plain(0x21, "F sqrt"), halt()};
    const auto result = core::passes::shared_straight_line_helper_pass().run(program, ctx);
    require_applied(result.applied, 2, "shared-straight-line-helper extracts repeated bodies");
    require(!result.optimizations.empty() &&
                result.optimizations.at(0).name == "shared-straight-line-helper",
            "shared-straight-line-helper: optimization name");
    require(contains_label(result.ops, "__shared_straight_line_helper_0"),
            "shared-straight-line-helper: helper label added");
    require(count_call_target(result.ops, "__shared_straight_line_helper_0") == 2,
            "shared-straight-line-helper: two calls to helper");
    require(!result.ops.empty() && result.ops.back().kind == IrKind::Return,
            "shared-straight-line-helper: helper ends with return");
    require(machine_cell_count(result.ops) < machine_cell_count(program),
            "shared-straight-line-helper: fewer cells");
  }
  {
    const std::vector<IrOp> program = {label("first"),  recall("1"),     call("normalize"),
                                       recall("2"),      plain(0x10, "+"), store("3"),
                                       plain(0x20, "F pi"), label("second"), recall("1"),
                                       call("normalize"), recall("2"),    plain(0x10, "+"),
                                       store("3"),       plain(0x21, "F sqrt"), label("normalize"),
                                       plain(0x34, "\u041a [x]"), ret()};
    mkpro::CompileOptions options = noop;
    options.shared_straight_line_call_bodies = true;
    const core::passes::PassContext enabled{.options = options};
    const auto result = core::passes::shared_straight_line_helper_pass().run(program, enabled);
    require_applied(result.applied, 2, "shared-straight-line-helper extracts bodies with calls");
    require(count_call_target(result.ops, "__shared_straight_line_helper_0") == 2,
            "shared-straight-line-helper: two helper calls (direct call body)");
    bool keeps_normalize = false;
    for (const IrOp& op : result.ops)
      if (op.kind == IrKind::Call && target_str(op) == "normalize" && op.opcode == 0x53)
        keeps_normalize = true;
    require(keeps_normalize, "shared-straight-line-helper: keeps direct normalize call");
    require(machine_cell_count(result.ops) < machine_cell_count(program),
            "shared-straight-line-helper: fewer cells (call body)");
  }
  {
    const std::vector<IrOp> program = {recall("1"),     call("normalize"), store("2"),
                                       plain(0x01, "1"), recall("1"),      call("normalize"),
                                       store("2"),       plain(0x02, "2"), label("normalize"),
                                       plain(0x34, "\u041a [x]"), ret()};
    const auto result = core::passes::shared_straight_line_helper_pass().run(program, ctx);
    require_applied(result.applied, 0, "shared-straight-line-helper keeps call bodies behind flag");
    require_ops_equal(result.ops, program, "shared-straight-line-helper flag: ops unchanged");
  }
  {
    const std::vector<IrOp> program = {label("first"),  recall("1"),     recall("2"),
                                       plain(0x10, "+"), store("3"),     plain(0x01, "1"),
                                       label("second"), recall("1"),     recall("2"),
                                       plain(0x10, "+"), store("3"),     plain(0x02, "2"),
                                       halt()};
    const auto result = core::passes::shared_straight_line_helper_pass().run(program, ctx);
    require_applied(result.applied, 0, "shared-straight-line-helper keeps returns off X2 boundary");
    require_ops_equal(result.ops, program, "shared-straight-line-helper X2: ops unchanged");
  }
  {
    const std::vector<IrOp> program = {label("first"),  recall("1"),     recall("2"),
                                       plain(0x10, "+"), store("3"),     recall("4"),
                                       store("5"),       plain(0x20, "F pi"), label("second"),
                                       recall("1"),      recall("2"),    plain(0x10, "+"),
                                       store("3"),       recall("4"),    store("5"),
                                       plain(0x21, "F sqrt"), label("suffix"), plain(0x10, "+"),
                                       store("3"),       recall("4"),    store("5"),
                                       plain(0x17, "F lg"), halt()};
    const auto result = core::passes::shared_straight_line_helper_pass().run(program, ctx);
    require_applied(result.applied, 3, "shared-straight-line-helper adds internal entries");
    bool multi_entry = false;
    for (const auto& optimization : result.optimizations)
      if (optimization.name == "multi-entry-straight-line-helper")
        multi_entry = true;
    require(multi_entry, "shared-straight-line-helper: multi-entry optimization present");
    require(count_call_target(result.ops, "__shared_straight_line_helper_0") == 2,
            "shared-straight-line-helper: two calls to helper_0");
    require(count_call_target(result.ops, "__shared_straight_line_helper_1") == 1,
            "shared-straight-line-helper: one call to helper_1");
    const int helper = find_label(result.ops, "__shared_straight_line_helper_0");
    const int entry = find_label(result.ops, "__shared_straight_line_helper_1");
    require(helper >= 0, "shared-straight-line-helper: helper_0 present");
    require(entry > helper, "shared-straight-line-helper: entry after helper");
    require(machine_cell_count(result.ops) < machine_cell_count(program),
            "shared-straight-line-helper: fewer cells (entries)");
  }
  {
    const std::vector<IrOp> program = {label("anchor"), recall("1"),     recall("2"),
                                       plain(0x10, "+"), store("3"),     recall("4"),
                                       plain(0x20, "F pi"), label("suffix_one"), plain(0x10, "+"),
                                       store("3"),       recall("4"),    plain(0x21, "F sqrt"),
                                       label("suffix_two"), plain(0x10, "+"), store("3"),
                                       recall("4"),      plain(0x17, "F lg"), label("suffix_three"),
                                       plain(0x10, "+"), store("3"),     recall("4"),
                                       plain(0x22, "F x2"), label("suffix_four"), plain(0x10, "+"),
                                       store("3"),       recall("4"),    plain(0x18, "F ln"),
                                       halt()};
    const auto result = core::passes::shared_straight_line_helper_pass().run(program, ctx);
    require_applied(result.applied, 5, "shared-straight-line-helper anchors unique body");
    bool multi_entry = false;
    for (const auto& optimization : result.optimizations)
      if (optimization.name == "multi-entry-straight-line-helper")
        multi_entry = true;
    require(multi_entry, "shared-straight-line-helper: multi-entry present (anchor)");
    require(count_call_target(result.ops, "__shared_straight_line_helper_0") == 1,
            "shared-straight-line-helper: one call to helper_0 (anchor)");
    require(count_call_target(result.ops, "__shared_straight_line_helper_1") == 4,
            "shared-straight-line-helper: four calls to helper_1 (anchor)");
    const int helper = find_label(result.ops, "__shared_straight_line_helper_0");
    const int entry = find_label(result.ops, "__shared_straight_line_helper_1");
    require(helper >= 0, "shared-straight-line-helper: helper_0 present (anchor)");
    require(entry > helper, "shared-straight-line-helper: entry after helper (anchor)");
    require(machine_cell_count(result.ops) < machine_cell_count(program),
            "shared-straight-line-helper: fewer cells (anchor)");
  }
  {
    const std::vector<IrOp> program = {numeric_jump(9), recall("1"),     recall("2"),
                                       plain(0x10, "+"), store("3"),     recall("1"),
                                       recall("2"),      plain(0x10, "+"), store("3")};
    const auto result = core::passes::shared_straight_line_helper_pass().run(program, ctx);
    require_applied(result.applied, 0, "shared-straight-line-helper avoids absolute numeric targets");
    require_ops_equal(result.ops, program, "shared-straight-line-helper numeric: ops unchanged");
  }

  // --- redundant-prologue-elimination -------------------------------------
  const auto terminal_display_stop = [] {
    IrOp op = halt();
    op.meta.stop_disposition = StopDisposition::Terminal;
    op.semantic = "halt";
    return op;
  };
  const auto show_stop = [] {
    IrOp op = halt();
    op.meta.stop_disposition = StopDisposition::Resumable;
    return op;
  };
  {
    const std::vector<IrOp> program = {label("main"),    recall("1"),     recall("2"),
                                       plain(0x10, "+"),  terminal_display_stop(),          recall("3"),
                                       store("3"),        recall("1"),     recall("2"),
                                       plain(0x10, "+"),  terminal_display_stop(),          jump("main")};
    const auto result = core::passes::redundant_prologue_elimination_pass().run(program, ctx);
    require_applied(result.applied, 1, "redundant-prologue-elimination drops duplicated prologue");
    require(result.ops.size() == program.size() - 4,
            "redundant-prologue-elimination: four ops removed");
    require(!result.ops.empty() && result.ops.back().kind == IrKind::Jump,
            "redundant-prologue-elimination: ends with jump");
  }
  {
    const std::vector<IrOp> program = {label("main"),     recall("1"),       terminal_display_stop(),
                                       recall("3"),        store("3"),        recall("1"),
                                       label("dispatch_end"), terminal_display_stop(),         jump("main")};
    const auto result = core::passes::redundant_prologue_elimination_pass().run(program, ctx);
    require_applied(result.applied, 1, "redundant-prologue-elimination preserves inner labels");
    require(contains_label(result.ops, "dispatch_end"),
            "redundant-prologue-elimination: dispatch_end preserved");
  }
  {
    const std::vector<IrOp> program = {label("main"), plain(0x00, "0"), terminal_display_stop(), jump("main")};
    const auto result = core::passes::redundant_prologue_elimination_pass().run(program, ctx);
    require_applied(result.applied, 0, "redundant-prologue-elimination refuses single-body loop");
    require(result.ops.size() == program.size(),
            "redundant-prologue-elimination: ops length unchanged");
  }
  {
    const std::vector<IrOp> program = {label("main"), recall("1"), terminal_display_stop(),  recall("3"),
                                       store("3"),    recall("2"), terminal_display_stop(),  jump("main")};
    const auto result = core::passes::redundant_prologue_elimination_pass().run(program, ctx);
    require_applied(result.applied, 0, "redundant-prologue-elimination refuses differing prologues");
  }
  {
    const auto observe = [&](const std::vector<IrOp>& program) {
      const auto resolved = resolve_machine_items(lower_ir_to_machine(program));
      require(resolved.diagnostics.empty(), "display prologue fixture must resolve");
      std::vector<int> codes;
      for (const auto& step : resolved.steps)
        codes.push_back(step.opcode);
      emulator::MK61 calc;
      require(calc.load_program(codes).diagnostics.empty(), "display prologue must load");
      calc.set_register("1", "2").set_register("2", "3");
      calc.input_number("7", true).press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(400, 5).stopped, "display prologue must stop");
      std::vector<std::string> result{calc.display_text()};
      for (const char* name : {"x", "y", "z", "t", "x1"})
        result.push_back(calc.read_register(name));
      calc.press(".");
      result.push_back(calc.display_text());
      return result;
    };
    const std::vector<IrOp> middle_entry = {
        jump("middle"), label("head"), recall("1"), recall("2"),
        plain(0x12, "*"), terminal_display_stop(), store("3"), recall("1"),
        label("middle"), recall("2"), plain(0x12, "*"), terminal_display_stop(), jump("head")};
    const auto middle = core::passes::redundant_prologue_elimination(middle_entry, ctx);
    require_applied(middle.applied, 0, "display suffix reached from outside must stay intact");
    require(observe(middle_entry).front() == "21," &&
                observe(middle_entry) == observe(middle.ops),
            "an inner entry must multiply the incoming X, not reload the head's first operand");

    const std::vector<IrOp> full_entry = {
        jump("tail"), label("head"), recall("1"), recall("2"),
        plain(0x12, "*"), terminal_display_stop(), store("3"), label("tail"), recall("1"),
        label("unreferenced"), recall("2"), plain(0x12, "*"), terminal_display_stop(), jump("head")};
    const auto full = core::passes::redundant_prologue_elimination(full_entry, ctx);
    require_applied(full.applied, 1, "whole display entry and unreferenced labels remain foldable");
    require(observe(full_entry) == observe(full.ops),
            "complete display folding must preserve stack, X1 and decimal-entry observations");

    const std::vector<IrOp> virtual_entry = {
        jump("tail"), label("head"), recall("1"), recall("2"),
        plain(0x10, "+"), terminal_display_stop(), label("tail"), store("1"),
        recall("2"), plain(0x10, "+"), terminal_display_stop(), jump("head")};
    const auto virtual_result =
        core::passes::redundant_prologue_elimination(virtual_entry, ctx);
    require_applied(virtual_result.applied, 0,
                    "store-carried display must not gain an observable recall stack lift");
    require(observe(virtual_entry) == observe(virtual_result.ops),
            "virtual head recall must preserve Y/Z/T and hidden decimal-entry state");

    const std::vector<IrOp> washed_virtual_entry = {
        jump("tail"), label("head"), recall("1"), recall("2"), recall("2"),
        recall("2"), recall("2"), terminal_display_stop(), label("tail"), store("1"),
        recall("2"), recall("2"), recall("2"), recall("2"), terminal_display_stop(), jump("head")};
    const auto washed = core::passes::redundant_prologue_elimination(washed_virtual_entry, ctx);
    require_applied(washed.applied, 1,
                    "store-carried display remains foldable after full stack resynchronization");
    require(observe(washed_virtual_entry) == observe(washed.ops),
            "proved virtual head must preserve the complete ROM-visible observation");

    auto numeric = full_entry;
    numeric.push_back(label("fixed"));
    numeric.push_back(plain(0x09, "9"));
    numeric.push_back(terminal_display_stop());
    numeric.front() = numeric_jump(core::passes::calculate_label_addresses(numeric).at("fixed"));
    require_applied(core::passes::redundant_prologue_elimination(numeric, ctx).applied, 0,
                    "display folding must not move an unrelated numeric destination");

    auto unknown_flow = full_entry;
    unknown_flow.front() = indirect_jump("7");
    require_applied(core::passes::redundant_prologue_elimination(unknown_flow, ctx).applied, 0,
                    "unknown indirect entries must block display deletion");
    for (const StopDisposition disposition :
         {StopDisposition::Unknown, StopDisposition::Resumable}) {
      auto stops = full_entry;
      for (IrOp& op : stops)
        if (op.kind == IrKind::Stop)
          op.meta.stop_disposition = disposition;
      require_applied(core::passes::redundant_prologue_elimination(stops, ctx).applied, 0,
                      "resumable or unknown stops must never lose an interaction");
    }
    for (const bool manual : {false, true}) {
      auto anchored = full_entry;
      if (manual) {
        anchored.at(12).meta.manual_interaction = ManualInteractionAnchor{
            .protocol_id = 0, .phase = 0, .kind = ManualInteractionAnchorKind::PromptStop};
      } else {
        anchored.at(8).meta.roles.push_back("fixed-display-cell");
      }
      require_applied(core::passes::redundant_prologue_elimination(anchored, ctx).applied, 0,
                      "manual and role-bound display cells must remain in place");
    }
  }
  // Repeated resumable displays still require separate user resumes.
  {
    const std::vector<IrOp> program = {
        label("head"), recall("1"), recall("2"), plain(0x12, "*"), show_stop(),
        recall("3"), plain(1, "1"), plain(0x10, "+"), store("3"),
        recall("1"), recall("2"), plain(0x12, "*"), show_stop(), jump("head")};
    const auto optimized = core::passes::redundant_prologue_elimination(program, ctx);
    require_applied(optimized.applied, 0, "identical prompts must retain both stop events");
    const auto observe_turns = [&](const std::vector<IrOp>& ops) {
      const auto resolved = resolve_machine_items(lower_ir_to_machine(ops));
      require(resolved.diagnostics.empty(), "repeated prompts must resolve");
      std::vector<int> codes;
      for (const auto& step : resolved.steps) codes.push_back(step.opcode);
      emulator::MK61 calc;
      require(calc.load_program(codes).diagnostics.empty(), "repeated prompts must load");
      calc.set_register("1", "2").set_register("2", "3").set_register("3", "0");
      calc.press_sequence({"В/О", "С/П"});
      std::vector<int> turns;
      for (int resume = 0; resume < 5; ++resume) {
        require(calc.run_until_stable(500, 5).stopped, "each prompt must stop separately");
        require(calc.display_text() == "6,", "repeated prompt contents remain equal");
        turns.push_back(std::stoi(calc.read_register("3")));
        if (resume != 4) calc.press("С/П");
      }
      return turns;
    };
    const std::vector<int> expected{0, 1, 1, 2, 2};
    require(observe_turns(program) == expected && observe_turns(optimized.ops) == expected,
            "display sharing must preserve state changes between every pair of user resumes");

    const std::string source = R"mkpro(
program RepeatedPrompts {
  state {
    turns: counter 0..9 = 0
  }
  loop {
    show(turns, 0)
    turns++
    show(turns, 0)
  }
}
)mkpro";
    for (const bool search : {false, true}) {
      CompileOptions options;
      options.disable_candidate_search = !search;
      const auto compiled = compile_source(source, options);
      require(compiled.implemented && compiled.diagnostics.empty() &&
                  compiled.steps.size() <= 105 && compiled.registers.contains("turns"),
              "source-level repeated prompts must retain their displayed state");
      emulator::MK61 calc;
      for (const auto& preload : compiled.preloads) {
        require(!preload.setup_expression, "prompt fixture needs only literal setup values");
        std::string value = preload.value;
        if (value.size() == 2U && value.find_first_of("ABCDEF") != std::string::npos) {
          const std::vector<std::string> glyphs{"-", "L", "С", "Г", "Е", "_"};
          std::string converted;
          for (char ch : value)
            converted += ch >= 'A' && ch <= 'F'
                             ? glyphs.at(static_cast<std::size_t>(ch - 'A'))
                             : std::string(1, ch);
          value = std::move(converted);
        }
        calc.set_register(preload.register_name, value);
      }
      const auto load = [&](const std::vector<ResolvedStep>& steps) {
        std::vector<int> codes;
        for (const auto& step : steps) codes.push_back(step.opcode);
        require(calc.load_program(codes).diagnostics.empty(), "prompt artifact must load");
        calc.press_sequence({"В/О", "С/П"});
        require(calc.run_until_stable(500, 5).stopped, "prompt artifact must reach its stop");
      };
      load(compiled.steps);
      std::vector<int> turns;
      for (int resume = 0; resume < 5; ++resume) {
        require(std::stoi(calc.display_text()) == expected.at(static_cast<std::size_t>(resume)) * 10,
                "source-level prompt must show the state for this exact resume");
        turns.push_back(std::stoi(calc.read_register(compiled.registers.at("turns"))));
        if (resume != 4) {
          calc.press("С/П");
          require(calc.run_until_stable(500, 5).stopped, "next source prompt must stop");
        }
      }
      require(turns == expected,
              "compiler and optimizer must preserve every source-level show interaction");
    }
  }

}

} // namespace mkpro::tests
