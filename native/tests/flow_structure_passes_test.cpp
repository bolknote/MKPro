#include "mkpro/core/passes/conditional_branch_trampoline.hpp"
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
#include "test_support.hpp"

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
    const std::vector<IrOp> program = {label("main"),    call("finish_turn"), jump("loop"),
                                       label("loop"),     halt(),              label("finish_turn"),
                                       cjump("done"),     halt(),              label("done"),
                                       ret()};
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
    const std::vector<IrOp> program = {label("main"),       call("finish_turn"), plain(0x01, "1"),
                                       call("finish_turn"), jump("main"),        proc_start("finish_turn"),
                                       plain(0x02, "2"),    ret()};
    const auto result = core::passes::tail_call_lowering_pass().run(program, ctx);
    require_applied(result.applied, 1, "tail-call-lowering empty-stack return terminal loop");
    require(!result.optimizations.empty() &&
                result.optimizations.at(0).detail.find("empty-return-stack") != std::string::npos,
            "tail-call-lowering: empty-return-stack detail");
    require(count_kind(result.ops, IrKind::Call) == 1, "tail-call-lowering: one call remains");
    require(count_kind(result.ops, IrKind::Jump) == 1, "tail-call-lowering: one jump remains");
    bool jump_ok = false;
    for (const IrOp& op : result.ops)
      if (op.kind == IrKind::Jump && target_str(op) == "finish_turn")
        jump_ok = true;
    require(jump_ok, "tail-call-lowering: jump targets finish_turn");
  }
  {
    const std::vector<IrOp> program = {label("main"),    call("finish_turn"),
                                       known_target_indirect_jump("b", 0), proc_start("finish_turn"),
                                       plain(0x02, "2"), ret()};
    const auto result = core::passes::tail_call_lowering_pass().run(program, ctx);
    require_applied(result.applied, 1, "tail-call-lowering proved indirect loop back");
    require(count_kind(result.ops, IrKind::IndirectJump) == 0,
            "tail-call-lowering: indirect jump removed");
    bool jump_ok = false;
    for (const IrOp& op : result.ops)
      if (op.kind == IrKind::Jump && target_str(op) == "finish_turn")
        jump_ok = true;
    require(jump_ok, "tail-call-lowering: jump targets finish_turn (indirect)");
  }
  {
    const std::vector<IrOp> program = {label("main"),        call("finish_turn"),  plain(0x01, "1"),
                                       call("finish_turn"),  jump("main"),         proc_start("finish_turn"),
                                       plain(0x02, "2"),     jump("shared_return"), proc_start("shared_return"),
                                       plain(0x03, "3"),     ret()};
    const auto result = core::passes::tail_call_lowering_pass().run(program, ctx);
    require(result.applied >= 1, "tail-call-lowering proves through terminal tail jumps");
    require(!result.optimizations.empty() &&
                result.optimizations.at(0).detail.find("empty-return-stack") != std::string::npos,
            "tail-call-lowering: empty-return-stack detail (tail jump)");
    bool jump_ok = false;
    for (const IrOp& op : result.ops)
      if (op.kind == IrKind::Jump && target_str(op) == "finish_turn")
        jump_ok = true;
    require(jump_ok, "tail-call-lowering: jump targets finish_turn (tail jump)");
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
  {
    const std::vector<IrOp> program = {label("main"),    recall("1"),     recall("2"),
                                       plain(0x10, "+"),  halt(),          recall("3"),
                                       store("3"),        recall("1"),     recall("2"),
                                       plain(0x10, "+"),  halt(),          jump("main")};
    const auto result = core::passes::redundant_prologue_elimination_pass().run(program, ctx);
    require_applied(result.applied, 1, "redundant-prologue-elimination drops duplicated prologue");
    require(result.ops.size() == program.size() - 4,
            "redundant-prologue-elimination: four ops removed");
    require(!result.ops.empty() && result.ops.back().kind == IrKind::Jump,
            "redundant-prologue-elimination: ends with jump");
  }
  {
    const std::vector<IrOp> program = {label("main"),     recall("1"),       halt(),
                                       recall("3"),        store("3"),        recall("1"),
                                       label("dispatch_end"), halt(),         jump("main")};
    const auto result = core::passes::redundant_prologue_elimination_pass().run(program, ctx);
    require_applied(result.applied, 1, "redundant-prologue-elimination preserves inner labels");
    require(contains_label(result.ops, "dispatch_end"),
            "redundant-prologue-elimination: dispatch_end preserved");
  }
  {
    const std::vector<IrOp> program = {label("main"), plain(0x00, "0"), halt(), jump("main")};
    const auto result = core::passes::redundant_prologue_elimination_pass().run(program, ctx);
    require_applied(result.applied, 0, "redundant-prologue-elimination refuses single-body loop");
    require(result.ops.size() == program.size(),
            "redundant-prologue-elimination: ops length unchanged");
  }
  {
    const std::vector<IrOp> program = {label("main"), recall("1"), halt(),  recall("3"),
                                       store("3"),    recall("2"), halt(),  jump("main")};
    const auto result = core::passes::redundant_prologue_elimination_pass().run(program, ctx);
    require_applied(result.applied, 0, "redundant-prologue-elimination refuses differing prologues");
  }
}

} // namespace mkpro::tests
