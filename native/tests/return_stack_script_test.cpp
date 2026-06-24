#include "mkpro/compiler.hpp"
#include "mkpro/core/return_stack_script.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

MachineItem digit(int value) {
  return MachineItem::op(value, std::to_string(value));
}

MachineItem stop() {
  return MachineItem::op(0x50, "С/П");
}

MachineItem plus_op() {
  return MachineItem::op(0x10, "+");
}

std::vector<MachineItem> call(const std::string& target) {
  return {
      MachineItem::op(0x53, "ПП"),
      MachineItem::address(target),
  };
}

MachineItem proven_indirect_call(int opcode, int target_address) {
  MachineItem item = MachineItem::op(opcode, "К ПП");
  item.comment = "preloaded indirect-target=" + std::to_string(target_address) +
                 " indirect flow";
  return item;
}

std::vector<MachineItem> jump(const std::string& target) {
  return {
      MachineItem::op(0x51, "БП"),
      MachineItem::address(target),
  };
}

IrOp ir_plain(int opcode) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::to_string(opcode);
  return op;
}

IrOp ir_stop() {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  op.semantic = "halt";
  op.meta.mnemonic = "С/П";
  return op;
}

IrOp ir_return() {
  IrOp op;
  op.kind = IrKind::Return;
  op.opcode = 0x52;
  op.meta.mnemonic = "В/О";
  return op;
}

IrOp ir_jump(const std::string& target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = target;
  op.meta.mnemonic = "БП";
  return op;
}

std::vector<IrOp> ir_jump_body(const std::string& target) {
  return {ir_jump(target)};
}

IrOp ir_raw_jump(const std::string& target) {
  IrOp op = ir_plain(0x51);
  op.target = target;
  op.meta.mnemonic = "БП";
  return op;
}

IrOp ir_raw_cond_jump(int opcode, const std::string& target) {
  IrOp op = ir_plain(opcode);
  op.target = target;
  op.meta.mnemonic = "x?0";
  return op;
}

IrOp ir_cond_jump(const std::string& target) {
  IrOp op;
  op.kind = IrKind::CondJump;
  op.opcode = 0x57;
  op.target = target;
  op.condition = "x=0";
  op.meta.mnemonic = "x=0";
  return op;
}

IrOp ir_loop(const std::string& target) {
  IrOp op;
  op.kind = IrKind::Loop;
  op.opcode = 0x5d;
  op.target = target;
  op.counter = "0";
  op.meta.mnemonic = "L0";
  return op;
}

IrOp ir_indirect_call(int opcode = 0xa0) {
  IrOp op;
  op.kind = IrKind::IndirectCall;
  op.opcode = opcode;
  op.register_name = "0";
  op.meta.mnemonic = "К ПП";
  return op;
}

IrOp ir_indirect_cond_jump(int opcode = 0x70) {
  IrOp op;
  op.kind = IrKind::IndirectCondJump;
  op.opcode = opcode;
  op.register_name = "0";
  op.condition = "x=0";
  op.meta.mnemonic = "К x=0";
  return op;
}

IrOp ir_raw_indirect_jump(int opcode = 0x80) {
  IrOp op = ir_plain(opcode);
  op.meta.mnemonic = "К БП";
  return op;
}

IrOp ir_indirect_jump(int opcode = 0x80) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.opcode = opcode;
  op.register_name = "0";
  op.meta.mnemonic = "К БП";
  return op;
}

IrOp ir_call(const std::string& target) {
  IrOp op;
  op.kind = IrKind::Call;
  op.opcode = 0x53;
  op.target = target;
  op.meta.mnemonic = "ПП";
  return op;
}

IrOp ir_raw_call(const std::string& target) {
  IrOp op = ir_plain(0x53);
  op.target = target;
  op.meta.mnemonic = "ПП";
  return op;
}

IrOp ir_label(const std::string& name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = name;
  return op;
}

std::vector<IrOp> direct_tail(int value, const std::string& target) {
  return {
      ir_plain(value),
      ir_jump(target),
  };
}

std::vector<MachineItem> repeated_stop_layout(int cells) {
  return std::vector<MachineItem>(static_cast<std::size_t>(cells), stop());
}

void append(std::vector<MachineItem>& items, const std::vector<MachineItem>& tail) {
  items.insert(items.end(), tail.begin(), tail.end());
}

void append(std::vector<IrOp>& items, const std::vector<IrOp>& tail) {
  items.insert(items.end(), tail.begin(), tail.end());
}

std::string read_fixture_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::vector<MachineItem> counted_script_program(int count) {
  std::vector<MachineItem> items;
  for (int index = 1; index <= count; ++index) {
    items.push_back(MachineItem::label("charge_t" + std::to_string(index)));
    append(items, call(index == count ? "entry" : "charge_t" + std::to_string(index + 1)));
    items.push_back(MachineItem::label("t" + std::to_string(index)));
    items.push_back(digit(index % 10));
    if (index == 1) {
      items.push_back(stop());
    } else {
      append(items, jump("t" + std::to_string(index - 1)));
    }
  }
  items.push_back(MachineItem::label("entry"));
  append(items, jump("t" + std::to_string(count)));
  return items;
}

std::vector<MachineItem> dirty_overflow_script_program(bool safe_dirty_target = true) {
  std::vector<MachineItem> items;
  while (core::machine_cell_count(items) < 26)
    items.push_back(plus_op());

  for (int index = 1; index <= 5; ++index) {
    items.push_back(MachineItem::label("charge_t" + std::to_string(index)));
    append(items, call(index == 5 ? "entry" : "charge_t" + std::to_string(index + 1)));
    items.push_back(MachineItem::label("t" + std::to_string(index)));
    items.push_back(digit(index % 10));
    append(items, jump(index == 1 ? "dirty_target" : "t" + std::to_string(index - 1)));
  }
  items.push_back(MachineItem::label("entry"));
  append(items, jump("t5"));

  while (core::machine_cell_count(items) < 84)
    items.push_back(plus_op());
  items.push_back(MachineItem::label("dirty_target"));
  items.push_back(safe_dirty_target ? plus_op() : stop());
  return items;
}

std::vector<MachineItem> three_step_script_program() {
  std::vector<MachineItem> items;
  items.push_back(MachineItem::label("charge_t1"));
  append(items, call("charge_t2"));
  items.push_back(MachineItem::label("t1"));
  items.push_back(digit(1));
  items.push_back(stop());

  items.push_back(MachineItem::label("charge_t2"));
  append(items, call("charge_t3"));
  items.push_back(MachineItem::label("t2"));
  items.push_back(digit(2));
  append(items, jump("t1"));

  items.push_back(MachineItem::label("charge_t3"));
  append(items, call("entry"));
  items.push_back(MachineItem::label("t3"));
  items.push_back(digit(3));
  append(items, jump("t2"));

  items.push_back(MachineItem::label("entry"));
  append(items, jump("t3"));
  return items;
}

std::vector<MachineItem> overlay_tie_script_program() {
  std::vector<MachineItem> items;
  items.push_back(MachineItem::label("charge_t1"));
  append(items, call("charge_t2"));
  items.push_back(MachineItem::label("t1"));
  items.push_back(digit(1));
  items.push_back(stop());

  items.push_back(MachineItem::label("charge_t2"));
  append(items, call("entry"));
  items.push_back(MachineItem::label("t2"));
  items.push_back(digit(2));
  append(items, jump("t1"));
  items.push_back(MachineItem::label("overlay_tail"));
  items.push_back(digit(2));

  items.push_back(MachineItem::label("entry"));
  append(items, jump("t2"));
  items.push_back(MachineItem::label("overlay_entry"));
  items.push_back(digit(6));
  return items;
}

std::vector<MachineItem> proven_indirect_call_script_program() {
  std::vector<MachineItem> items;
  items.push_back(MachineItem::label("charge_t1"));
  items.push_back(proven_indirect_call(0xa0, 3));
  items.push_back(MachineItem::label("t1"));
  items.push_back(digit(1));
  items.push_back(stop());

  items.push_back(MachineItem::label("charge_t2"));
  items.push_back(proven_indirect_call(0xa1, 7));
  items.push_back(MachineItem::label("t2"));
  items.push_back(digit(2));
  append(items, jump("t1"));

  items.push_back(MachineItem::label("entry"));
  append(items, jump("t2"));
  return items;
}

int count_opcode(const std::vector<MachineItem>& items, int opcode) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [&](const MachineItem& item) {
    return item.kind == MachineItemKind::Op && item.opcode == opcode;
  }));
}

bool has_optimization(const core::PostLayoutIndirectFlowResult& result,
                      const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const core::passes::AppliedOptimization& item) {
                       return item.name == name;
                     });
}

bool optimization_detail_contains(const core::PostLayoutIndirectFlowResult& result,
                                  const std::string& name, const std::string& needle) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const core::passes::AppliedOptimization& item) {
                       return item.name == name &&
                              item.detail.find(needle) != std::string::npos;
                     });
}

} // namespace

void return_stack_script_matches_mk61_strategy_contract() {
  {
    const std::string source =
        read_fixture_text(std::filesystem::current_path() / "examples" / "river-battle.mkpro");
    CompileOptions options;
    options.return_stack_script = true;
    const CompileResult result = compile_source(source, options);
    require(result.implemented,
            "--return-stack-script should preserve successful non-return-stack rescue candidates");
  }

  {
    const std::vector<core::ReturnStackReturnStep> steps =
        core::simulate_mk61_return_stack({19, 27, 35, 43, 51}, 6);
    std::vector<int> targets;
    for (const core::ReturnStackReturnStep& step : steps)
      targets.push_back(step.target_address);
    require(targets == std::vector<int>({52, 44, 36, 28, 20, 100}),
            "return-stack model should expose five scripted returns and then A0-ish dirty flow");
    require(steps.at(4).stack_after_return == std::vector<int>({99, 99, 99, 99, 99}),
            "return-stack model should duplicate the oldest low digit after stack exhaustion");
  }

  {
    const std::vector<core::ReturnStackReturnStep> steps =
        core::simulate_mk61_return_stack({27, 35, 43, 51, 59}, 6);
    std::vector<int> targets;
    for (const core::ReturnStackReturnStep& step : steps)
      targets.push_back(step.target_address);
    require(targets == std::vector<int>({60, 52, 44, 36, 28, 78}),
            "return-stack model should explain the observed 77 -> 78 dirty dispatch");
  }

  {
    const core::DirtyReturnStackDispatchPlan plan =
        core::plan_dirty_return_stack_dispatch({27, 35, 43, 51, 59}, 6);
    require(plan.clean_targets == std::vector<int>({60, 52, 44, 36, 28}),
            "dirty dispatch plan should preserve the five clean scripted returns");
    require(plan.dirty_return_addresses == std::vector<int>({77}),
            "dirty dispatch plan should expose the dirty stored return address");
    require(plan.dirty_targets == std::vector<int>({78}),
            "dirty dispatch plan should expose the first 77-derived target");
    require(plan.high_risk, "dirty overflow dispatch should be marked high-risk");
    require(!plan.risk_reason.empty(), "dirty overflow dispatch should explain its risk");
    require(!plan.enabled && !plan.rejection_reason.empty(),
            "dirty overflow dispatch should be disabled outside size-rescue mode");
  }

  {
    const core::DirtyReturnStackDispatchPlan plan =
        core::plan_dirty_return_stack_dispatch({27, 35, 43, 51, 59}, 6,
                                               {.size_rescue = true});
    require(plan.enabled, "dirty overflow dispatch should require explicit size-rescue mode");
    require(plan.rejection_reason.empty(),
            "enabled dirty overflow dispatch should not carry a rejection reason");
  }

  {
    const core::DirtyReturnStackDispatchPlan plan =
        core::plan_dirty_return_stack_dispatch(
            {27, 35, 43, 51, 59}, 6,
            {.size_rescue = true, .min_dirty_targets = 0});
    require(!plan.enabled &&
                plan.rejection_reason.find("at least one requested dirty target") !=
                    std::string::npos,
            "dirty overflow dispatch should reject a non-positive dirty-target request");
  }

  {
    std::vector<core::ReturnStackTailBlock> tails;
    tails.push_back(core::ReturnStackTailBlock{
        .label = "t1",
        .body = {ir_plain(1), ir_stop()},
    });
    tails.push_back(core::ReturnStackTailBlock{
        .label = "t2",
        .body = direct_tail(2, "t1"),
    });
    tails.push_back(core::ReturnStackTailBlock{
        .label = "t3",
        .body = direct_tail(3, "t2"),
    });
    const core::ReturnStackStartupLayoutPlan plan =
        core::build_return_stack_startup_layout(tails, ir_jump_body("t3"));

    require(plan.transitions == 3, "startup layout producer should count scripted transitions");
    require(plan.injected_call_sites == 3,
            "startup layout producer should charge one ПП per transition");
    require(plan.existing_call_sites == 0 && plan.paid_call_sites == 3,
            "pure startup prologue should pay for all injected ПП charge sites");
    require(plan.transition_savings == 3,
            "startup layout producer should count one saved cell per В/О replacement");
    require(plan.charge_cost == 6 && plan.net_savings == -3 && !plan.profitable,
            "pure one-shot startup prologue should be rejected by strict cost threshold");
    require(plan.strategy == "one-shot-startup-prologue" && !plan.rejection_reason.empty(),
            "unprofitable pure startup prologue should carry strategy/rejection metadata");
    require(plan.one_shot_proved && plan.no_backward_charge_jumps &&
                plan.no_external_charge_entries && plan.unique_entry_after_charge &&
                plan.tail_order_proved,
            "startup analyzer should prove the one-shot charging-chain invariants");
    require(plan.ordered_tail_labels == std::vector<std::string>({"t1", "t2", "t3"}),
            "startup analyzer should expose the physical tail order it proved from IR");
    require(!plan.proofs.empty(), "startup analyzer should expose proof details");
    require(core::optimize_post_layout_return_stack_script(plan.items).applied == 3,
            "startup layout producer should place ПП callsites so the detector can prove them");
  }

  {
    std::vector<core::ReturnStackTailBlock> tails;
    for (int index = 5; index >= 1; --index) {
      tails.push_back(core::ReturnStackTailBlock{
          .label = "t" + std::to_string(index),
          .body = index == 1 ? std::vector<IrOp>{ir_plain(1), ir_stop()}
                             : direct_tail(index, "t" + std::to_string(index - 1)),
      });
    }
    std::vector<core::ReturnStackExistingCallSite> existing_call_sites;
    for (int index = 1; index <= 5; ++index) {
      existing_call_sites.push_back(core::ReturnStackExistingCallSite{
          .label = "existing_call_" + std::to_string(index),
          .target_label = index == 5 ? "__return_stack_entry"
                                     : "__return_stack_charge_" + std::to_string(index),
          .continuation_label = "t" + std::to_string(index),
          .source_address = index * 10,
      });
    }
    const core::ReturnStackLayoutOpportunityAnalysis analysis =
        core::analyze_return_stack_layout_opportunity(
            core::ReturnStackLayoutOpportunity{
                .tails = tails,
                .entry_body = ir_jump_body("t5"),
                .entry_label = "__return_stack_entry",
                .existing_call_sites = existing_call_sites,
            });
    const core::ReturnStackStartupLayoutPlan plan =
        core::materialize_return_stack_layout(analysis);

    require(plan.strategy == "existing-callsite-layout",
            "layout producer should distinguish existing-callsite charging");
    require(plan.tail_order_proved &&
                plan.ordered_tail_labels ==
                    std::vector<std::string>({"t1", "t2", "t3", "t4", "t5"}),
            "layout producer should reorder real IR tails into return-stack physical order");
    require(plan.existing_call_sites == 5 && plan.paid_call_sites == 0,
            "existing-callsite layout should treat all charge sites as free");
    require(plan.charge_cost == 0 && plan.net_savings == 5 && plan.profitable,
            "existing-callsite layout should be profitable when charging is free");
    require(core::optimize_post_layout_return_stack_script(plan.items).applied == 5,
            "profitable generated layout should still be provable by the detector");
  }

  {
    std::vector<core::ReturnStackTailBlock> tails = {
        core::ReturnStackTailBlock{.label = "t1", .body = {ir_plain(1), ir_stop()}},
        core::ReturnStackTailBlock{.label = "t2", .body = direct_tail(2, "t1")},
    };
    const core::ReturnStackLayoutOpportunityAnalysis analysis =
        core::analyze_return_stack_layout_opportunity(
            core::ReturnStackLayoutOpportunity{
                .tails = tails,
                .entry_body = ir_jump_body("t2"),
                .existing_call_sites =
                    {
                        core::ReturnStackExistingCallSite{
                            .label = "bad_existing_call",
                            .target_label = "wrong_target",
                            .continuation_label = "t1",
                            .source_address = 12,
                        },
                    },
            });
    require(analysis.plan.existing_call_sites == 0 && analysis.plan.paid_call_sites == 2,
            "startup analyzer should count only concrete existing callsites that match the "
            "generated charge edge and continuation");
    require(!analysis.plan.risk_reasons.empty(),
            "startup analyzer should explain ignored existing-callsite candidates");
  }

  {
    std::vector<core::ReturnStackTailBlock> tails = {
        core::ReturnStackTailBlock{.label = "t1", .body = {ir_plain(1), ir_stop()}},
        core::ReturnStackTailBlock{.label = "t2", .body = direct_tail(2, "t1")},
    };
    const core::ReturnStackLayoutOpportunityAnalysis analysis =
        core::analyze_return_stack_layout_opportunity(
            core::ReturnStackLayoutOpportunity{
                .tails = tails,
                .entry_body = ir_jump_body("t2"),
                .entry_at_address_zero = false,
                .single_start_jump = false,
            });
    require(!analysis.plan.one_shot_proved && !analysis.plan.profitable,
            "layout analyzer should reject opportunities without one-shot proof");
    require(analysis.plan.rejection_reason.find("one-shot") != std::string::npos,
            "one-shot rejection should be explicit");
  }

  {
    std::vector<core::ReturnStackTailBlock> tails = {
        core::ReturnStackTailBlock{.label = "t1", .body = {ir_plain(1), ir_stop()}},
        core::ReturnStackTailBlock{.label = "t2", .body = direct_tail(2, "t1")},
    };
    const core::ReturnStackLayoutOpportunityAnalysis analysis =
        core::analyze_return_stack_layout_opportunity(
            core::ReturnStackLayoutOpportunity{
                .tails = tails,
                .entry_body = ir_jump_body("t2"),
            },
            {.min_net_savings = 0, .size_rescue = true});
    require(!analysis.plan.allowed_by_size_rescue || analysis.plan.net_savings == 0,
            "size-rescue admission should only apply to zero-net opportunities");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t3"));
    ops.push_back(ir_label("t3"));
    append(ops, direct_tail(3, "t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity,
            "IR tail layout scanner should detect whole-program tail chains before layout");
    require(search.analysis.plan.tail_order_proved &&
                search.analysis.plan.ordered_tail_labels ==
                    std::vector<std::string>({"t1", "t2", "t3"}),
            "IR tail layout scanner should feed analyze_return_stack_layout_opportunity");
    require(core::optimize_post_layout_return_stack_script(search.analysis.plan.items).applied == 3,
            "pre-layout tail-chain materialization should create a provable charge chain");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("external"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("charge2"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.materialized &&
                search.rejection_reason.find("external CFG entry") != std::string::npos,
            "existing ПП reuse should reject external CFG entries into moved tails");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("external_raw"));
    IrOp raw_jump = ir_plain(0x51);
    raw_jump.target = std::string("t2");
    raw_jump.meta.mnemonic = "БП";
    ops.push_back(raw_jump);
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("charge2"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.materialized &&
                search.rejection_reason.find("external CFG entry") != std::string::npos,
            "IR CFG should treat raw addressed БП opcodes as external entries into moved tails");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("charge2"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "IR tail layout scanner should detect real existing ПП charge chains");
    require(search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.paid_call_sites == 0 &&
                search.analysis.plan.profitable,
            "existing ПП charge chains should remove injected charge cost");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "existing ПП charge-chain materialization should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_raw_call("charge2"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_raw_call("entry"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "IR tail layout scanner should detect raw ПП existing charge chains");
    require(search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.paid_call_sites == 0 &&
                search.analysis.plan.profitable,
            "raw ПП existing charge chains should remove injected charge cost");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "raw ПП existing charge-chain materialization should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("short_charge1"));
    ops.push_back(ir_call("short_charge2"));
    ops.push_back(ir_label("short_t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("short_charge2"));
    ops.push_back(ir_call("short_entry"));
    ops.push_back(ir_label("short_t2"));
    append(ops, direct_tail(2, "short_t1"));
    ops.push_back(ir_label("short_entry"));
    append(ops, ir_jump_body("short_t2"));
    ops.push_back(ir_label("long_charge1"));
    ops.push_back(ir_call("long_charge2"));
    ops.push_back(ir_label("long_t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("long_charge2"));
    ops.push_back(ir_call("long_charge3"));
    ops.push_back(ir_label("long_t2"));
    append(ops, direct_tail(2, "long_t1"));
    ops.push_back(ir_label("long_charge3"));
    ops.push_back(ir_call("long_entry"));
    ops.push_back(ir_label("long_t3"));
    append(ops, direct_tail(3, "long_t2"));
    ops.push_back(ir_label("long_entry"));
    append(ops, ir_jump_body("long_t3"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 3 &&
                search.analysis.plan.transitions == 3,
            "existing ПП scanner should keep scanning and choose the larger free charge chain");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("early_charge1"));
    ops.push_back(ir_call("early_charge2"));
    ops.push_back(ir_label("early_t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("early_charge2"));
    ops.push_back(ir_call("early_entry"));
    ops.push_back(ir_label("early_t2"));
    append(ops, direct_tail(2, "early_t1"));
    ops.push_back(ir_label("early_entry"));
    append(ops, ir_jump_body("early_t2"));
    ops.push_back(ir_label("later_charge1"));
    ops.push_back(ir_call("later_entry"));
    ops.push_back(ir_label("later_t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("later_charge2"));
    ops.push_back(ir_call("later_entry"));
    ops.push_back(ir_label("later_t2"));
    append(ops, direct_tail(2, "later_t1"));
    ops.push_back(ir_label("later_charge3"));
    ops.push_back(ir_call("later_entry"));
    ops.push_back(ir_label("later_t3"));
    append(ops, direct_tail(3, "later_t2"));
    ops.push_back(ir_label("later_entry"));
    append(ops, ir_jump_body("later_t3"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 3 &&
                search.analysis.plan.transitions == 3,
            "top-level IR scanner should choose a stronger same-target candidate over an earlier "
            "existing-chain candidate");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("early_charge1"));
    ops.push_back(ir_call("early_charge2"));
    ops.push_back(ir_label("early_t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("early_charge2"));
    ops.push_back(ir_call("early_entry"));
    ops.push_back(ir_label("early_t2"));
    append(ops, direct_tail(2, "early_t1"));
    ops.push_back(ir_label("early_entry"));
    append(ops, ir_jump_body("early_t2"));
    ops.push_back(ir_label("later_charge1"));
    ops.push_back(ir_call("later_entry"));
    ops.push_back(ir_label("later_t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("later_charge2"));
    ops.push_back(ir_call("later_entry"));
    ops.push_back(ir_label("later_t2"));
    append(ops, direct_tail(2, "later_t1"));
    ops.push_back(ir_label("later_charge3"));
    ops.push_back(ir_call("later_entry"));
    ops.push_back(ir_label("later_t3"));
    append(ops, direct_tail(3, "later_t2"));
    ops.push_back(ir_label("later_entry"));
    append(ops, ir_jump_body("later_t3"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout_with_pipeline(
            ops, repeated_stop_layout(120), CompileOptions{});
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 3 &&
                search.analysis.plan.transitions == 3,
            "pipeline-aware IR scanner should materialize the selected candidate before measuring "
            "the post-layout pipeline");
    require(search.pipeline_compared && search.pipeline_candidate_better &&
                search.pipeline_candidates_measured >= 2 &&
                search.pipeline_candidate_final_cells < search.pipeline_current_final_cells,
            "pipeline-aware IR scanner should expose the full-pipeline win used by the compiler "
            "after measuring all materialized candidates");
    require(search.rejection_reason.empty(),
            "pipeline-aware IR scanner should clear local heuristic rejections after a full-"
            "pipeline win");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("charge2"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch baseline =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(baseline.materialized,
            "pipeline rejection fixture should first produce a materialized baseline");

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout_with_pipeline(
            ops, baseline.materialized_items, CompileOptions{});
    require(search.materialized && search.pipeline_compared &&
                !search.pipeline_candidate_better &&
                search.pipeline_candidate_final_cells == search.pipeline_current_final_cells &&
                search.rejection_reason.find("full post-layout pipeline") !=
                    std::string::npos &&
                search.rejection_reason.find("candidate " +
                                             std::to_string(search.pipeline_candidate_final_cells) +
                                             " cells vs current " +
                                             std::to_string(search.pipeline_current_final_cells) +
                                             " cells") != std::string::npos &&
                search.rejection_reason.find("after measuring 1 materialized candidate") !=
                    std::string::npos,
            "pipeline-aware IR scanner should explain materialized ties with the full-pipeline "
            "rejection, concrete candidate/current sizes, and measured candidate count, not the "
            "local net-savings heuristic");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("long_charge1"));
    ops.push_back(ir_call("long_entry"));
    ops.push_back(ir_label("long_t2"));
    append(ops, direct_tail(2, "long_t1"));
    ops.push_back(ir_label("long_charge2"));
    ops.push_back(ir_call("long_entry"));
    ops.push_back(ir_label("long_t3"));
    append(ops, direct_tail(3, "long_t2"));
    ops.push_back(ir_label("long_t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("long_entry"));
    append(ops, ir_jump_body("long_t3"));

    ops.push_back(ir_label("short_charge1"));
    ops.push_back(ir_call("short_entry"));
    ops.push_back(ir_label("short_t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("short_charge2"));
    ops.push_back(ir_call("short_entry"));
    ops.push_back(ir_label("short_t2"));
    append(ops, direct_tail(2, "short_t1"));
    ops.push_back(ir_label("short_entry"));
    append(ops, ir_jump_body("short_t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.transitions == 2 &&
                search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.paid_call_sites == 0 &&
                search.analysis.plan.net_savings == 2,
            "non-pipeline IR scanner should choose the better analyzed profitable candidate, "
            "not just the longer pre-analysis candidate");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("charge2"));
    ops.push_back(ir_label("t1_alias"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "existing ПП scanner should use CFG fallthrough through empty alias labels");
    require(search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.paid_call_sites == 0,
            "CFG-derived existing ПП continuations should still be counted as free charge sites");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "CFG-derived existing ПП materialization should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("charge2_alias"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2_alias"));
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry_alias"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry_alias"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "existing ПП scanner should resolve charge target aliases through the IR CFG");
    require(search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.paid_call_sites == 0,
            "CFG-resolved existing ПП target aliases should still be counted as free charges");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "CFG-resolved existing ПП target aliases should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("external"));
    append(ops, ir_jump_body("t1_alias"));
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("charge2"));
    ops.push_back(ir_label("t1_alias"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.materialized &&
                search.rejection_reason.find("external CFG entry") != std::string::npos,
            "CFG-derived existing ПП aliases should reject external entries into moved labels");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("external"));
    append(ops, ir_jump_body("charge1"));
    ops.push_back(ir_label("prefix"));
    ops.push_back(ir_plain(9));
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("charge2"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.materialized &&
                search.rejection_reason.find("external CFG entry") != std::string::npos,
            "existing ПП reuse should reject multiple external entries into the first charge site");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("external"));
    append(ops, ir_jump_body("bad_t1"));
    ops.push_back(ir_label("bad_charge1"));
    ops.push_back(ir_call("bad_charge2"));
    ops.push_back(ir_label("bad_t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("bad_charge2"));
    ops.push_back(ir_call("bad_entry"));
    ops.push_back(ir_label("bad_t2"));
    append(ops, direct_tail(2, "bad_t1"));
    ops.push_back(ir_label("bad_entry"));
    append(ops, ir_jump_body("bad_t2"));
    ops.push_back(ir_label("good_charge1"));
    ops.push_back(ir_call("good_entry"));
    ops.push_back(ir_label("good_t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("good_charge2"));
    ops.push_back(ir_call("good_entry"));
    ops.push_back(ir_label("good_t2"));
    append(ops, direct_tail(2, "good_t1"));
    ops.push_back(ir_label("good_entry"));
    append(ops, ir_jump_body("good_t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.transitions == 2,
            "unsafe existing ПП CFG candidates should not block later independent safe "
            "retargeting candidates");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("prefix"));
    ops.push_back(ir_plain(9));
    ops.push_back(ir_call("charge2"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.extracted_existing_callsite_fragments == 1 &&
                search.extracted_tail_fragments == 0 && search.has_opportunity &&
                search.materialized,
            "IR scanner should extract terminal ПП suffixes into CFG callsite blocks");
    require(search.symbolic_existing_callsite_hints == 2 &&
                search.symbolic_existing_callsite_target_groups == 2 &&
                search.symbolic_existing_callsite_largest_target_group == 1 &&
                search.symbolic_existing_callsite_target_labels ==
                    std::vector<std::string>({"charge2", "entry"}),
            "IR scanner should expose symbolic terminal ПП hint target labels before retargeting");
    require(search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.paid_call_sites == 0,
            "extracted terminal ПП suffixes should count as free existing charge sites");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "terminal ПП suffix extraction should remain provable post-layout");
    const auto prefix_it =
        std::find_if(search.materialized_items.begin(), search.materialized_items.end(),
                     [](const MachineItem& item) { return item.kind == MachineItemKind::Label &&
                                                           item.name == "prefix"; });
    require(prefix_it != search.materialized_items.end(),
            "terminal ПП suffix extraction should preserve the original prefix label");
    require(std::next(prefix_it) != search.materialized_items.end() &&
                std::next(prefix_it)->kind == MachineItemKind::Op &&
                std::next(prefix_it)->opcode == 9,
            "terminal ПП suffix extraction should preserve prefix work before the hidden callsite");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("prefix_raw"));
    ops.push_back(ir_plain(9));
    ops.push_back(ir_raw_call("charge2"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.extracted_existing_callsite_fragments == 1 &&
                search.symbolic_existing_callsite_hints == 2 && search.has_opportunity &&
                search.materialized,
            "IR scanner should extract terminal raw ПП suffixes into CFG callsite blocks");
    require(search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.paid_call_sites == 0,
            "extracted terminal raw ПП suffixes should count as free existing charge sites");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "terminal raw ПП suffix extraction should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("first"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_call("shared_helper"));
    ops.push_back(ir_label("after_first"));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("second"));
    ops.push_back(ir_plain(2));
    ops.push_back(ir_call("shared_helper"));
    ops.push_back(ir_label("after_second"));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("shared_helper"));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.symbolic_existing_callsite_hints == 2 &&
                search.symbolic_existing_callsite_target_groups == 1 &&
                search.symbolic_existing_callsite_largest_target_group == 2 &&
                search.symbolic_existing_callsite_target_labels ==
                    std::vector<std::string>({"shared_helper"}) &&
                search.symbolic_existing_callsite_target_group_details ==
                    std::vector<std::string>({"shared_helper=2"}) &&
                search.symbolic_existing_callsite_source_labels ==
                    std::vector<std::string>({"first", "second"}) &&
                search.symbolic_existing_callsite_source_target_details ==
                    std::vector<std::string>({"first->shared_helper",
                                              "second->shared_helper"}),
            "IR scanner should group symbolic terminal ПП hints by shared target label and "
            "expose their source labels/details");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("first"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_call("helper_a"));
    ops.push_back(ir_label("after_first"));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("second"));
    ops.push_back(ir_plain(2));
    ops.push_back(ir_call("helper_b"));
    ops.push_back(ir_label("after_second"));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("helper_a"));
    ops.push_back(ir_label("helper_b"));
    ops.push_back(ir_label("shared_helper"));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.symbolic_existing_callsite_hints == 2 &&
                search.symbolic_existing_callsite_target_groups == 1 &&
                search.symbolic_existing_callsite_largest_target_group == 2 &&
                search.symbolic_existing_callsite_target_labels ==
                    std::vector<std::string>({"shared_helper"}) &&
                search.symbolic_existing_callsite_target_group_details ==
                    std::vector<std::string>({"shared_helper=2"}) &&
                search.symbolic_existing_callsite_source_labels ==
                    std::vector<std::string>({"first", "second"}) &&
                search.symbolic_existing_callsite_source_target_details ==
                    std::vector<std::string>({"first->shared_helper",
                                              "second->shared_helper"}),
            "IR scanner should group symbolic terminal ПП hints by CFG-resolved target aliases "
            "and expose their source/count/canonical-target details");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "same-target terminal ПП groups should retarget into a proven charge chain when their "
            "continuations already form the entry tail-chain");
    require(search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.paid_call_sites == 0 &&
                search.analysis.plan.profitable,
            "same-target terminal ПП retargeting should reuse existing callsites as free charges");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "same-target terminal ПП retargeting should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("entry_alias"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry_alias"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry_alias"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.paid_call_sites == 0,
            "same-target terminal ПП retargeting should resolve entry aliases through the IR CFG");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "CFG-resolved same-target terminal ПП retargeting should remain provable "
            "post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("entry_a"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry_b"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("entry_a"));
    ops.push_back(ir_label("entry_b"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.paid_call_sites == 0,
            "same-target terminal ПП retargeting should group different entry aliases by "
            "their CFG-resolved entry block");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "mixed same-target entry aliases should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("a_charge1"));
    ops.push_back(ir_call("a_entry"));
    ops.push_back(ir_label("a_t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("a_charge2"));
    ops.push_back(ir_call("a_entry"));
    ops.push_back(ir_label("a_t2"));
    append(ops, direct_tail(2, "a_t1"));
    ops.push_back(ir_label("a_entry"));
    append(ops, ir_jump_body("a_t2"));
    ops.push_back(ir_label("b_charge1"));
    ops.push_back(ir_call("b_entry"));
    ops.push_back(ir_label("b_t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("b_charge2"));
    ops.push_back(ir_call("b_entry"));
    ops.push_back(ir_label("b_t2"));
    append(ops, direct_tail(2, "b_t1"));
    ops.push_back(ir_label("b_charge3"));
    ops.push_back(ir_call("b_entry"));
    ops.push_back(ir_label("b_t3"));
    append(ops, direct_tail(3, "b_t2"));
    ops.push_back(ir_label("b_entry"));
    append(ops, ir_jump_body("b_t3"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 3 &&
                search.analysis.plan.transitions == 3,
            "same-target ПП retargeting should keep scanning and choose the larger free call group");
  }


  {
    std::vector<IrOp> ops;
    for (int index = 1; index <= 5; ++index) {
      ops.push_back(ir_label("charge" + std::to_string(index)));
      ops.push_back(ir_call("entry"));
      ops.push_back(ir_label("t" + std::to_string(index)));
      if (index == 1) {
        ops.push_back(ir_plain(1));
        ops.push_back(ir_stop());
      } else {
        append(ops, direct_tail(index, "t" + std::to_string(index - 1)));
      }
    }
    ops.push_back(ir_label("noise_charge"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("noise_tail"));
    ops.push_back(ir_plain(9));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t5"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 5 &&
                search.analysis.plan.paid_call_sites == 0 &&
                search.analysis.plan.transitions == 5,
            "same-target ПП retargeting should search bounded subgroups when the full call group "
            "exceeds return-stack depth");
    require(core::scan_return_stack_script_opportunity(search.materialized_items).possible,
            "bounded same-target ПП subgroup retargeting should remain visible to post-layout "
            "return-stack script scanning");
  }


  {
    std::vector<IrOp> ops;
    for (int index = 1; index <= 12; ++index) {
      ops.push_back(ir_label("noise_prefix_charge" + std::to_string(index)));
      ops.push_back(ir_call("entry"));
      ops.push_back(ir_label("noise_prefix_tail" + std::to_string(index)));
      ops.push_back(ir_plain(index % 10));
      ops.push_back(ir_stop());
    }
    for (int index = 5; index >= 1; --index) {
      ops.push_back(ir_label("chain_charge" + std::to_string(index)));
      ops.push_back(ir_call("entry"));
      ops.push_back(ir_label("t" + std::to_string(index)));
      if (index == 1) {
        ops.push_back(ir_plain(1));
        ops.push_back(ir_stop());
      } else {
        append(ops, direct_tail(index, "t" + std::to_string(index - 1)));
      }
      ops.push_back(ir_label("noise_between_charge" + std::to_string(index)));
      ops.push_back(ir_call("entry"));
      ops.push_back(ir_label("noise_between_tail" + std::to_string(index)));
      ops.push_back(ir_plain((index + 3) % 10));
      ops.push_back(ir_stop());
    }
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t5"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 5 &&
                search.analysis.plan.paid_call_sites == 0 &&
                search.analysis.plan.transitions == 5,
            "same-target ПП retargeting should recover chain-derived subgroups before bounded "
            "combination search is exhausted by noise");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("noop"));
    ops.push_back(ir_return());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "same-target no-op helper ПП groups should retarget into a synthetic charge chain");
    require(search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.paid_call_sites == 0 &&
                search.analysis.plan.profitable,
            "same-target no-op helper retargeting should reuse existing callsites as free charges");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "same-target no-op helper retargeting should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("noop_alias"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("noop_alias"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("noop_alias"));
    ops.push_back(ir_label("noop_impl"));
    ops.push_back(ir_return());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.transitions == 2,
            "same-target no-op helper retargeting should resolve empty helper aliases through "
            "the IR CFG");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "CFG-resolved no-op helper aliases should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("noop_a"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("noop_b"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("noop_a"));
    ops.push_back(ir_label("noop_b"));
    ops.push_back(ir_label("noop_impl"));
    ops.push_back(ir_return());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.transitions == 2,
            "same-target no-op helper retargeting should group different helper aliases by their "
            "CFG-resolved return block");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "mixed no-op helper aliases should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    for (int index = 1; index <= 3; ++index) {
      ops.push_back(ir_label("ignored_charge" + std::to_string(index)));
      ops.push_back(ir_call("noop"));
      ops.push_back(ir_label("ignored_tail" + std::to_string(index)));
      ops.push_back(ir_plain(index));
      ops.push_back(ir_stop());
    }
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("charge3"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("t3"));
    append(ops, direct_tail(3, "t2"));
    ops.push_back(ir_label("noop"));
    ops.push_back(ir_return());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 3 &&
                search.analysis.plan.transitions == 3,
            "same-target no-op helper retargeting should search bounded subgroups when the full "
            "helper call group exceeds return-stack depth");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 3,
            "bounded no-op helper subgroup retargeting should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("ignored_charge1"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("ignored_tail1"));
    ops.push_back(ir_plain(4));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("ignored_charge2"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("ignored_tail2"));
    ops.push_back(ir_plain(5));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge3"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("t3"));
    append(ops, direct_tail(3, "t2"));
    ops.push_back(ir_label("ignored_charge3"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("ignored_tail3"));
    ops.push_back(ir_plain(6));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("noop"));
    ops.push_back(ir_return());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 3 &&
                search.analysis.plan.transitions == 3,
            "same-target no-op helper retargeting should search non-contiguous bounded "
            "subgroups");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 3,
            "non-contiguous no-op helper subgroup retargeting should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    for (int index = 1; index <= 12; ++index) {
      ops.push_back(ir_label("noise_charge" + std::to_string(index)));
      ops.push_back(ir_call("noop"));
      ops.push_back(ir_label("noise_tail" + std::to_string(index)));
      ops.push_back(ir_plain(index % 10));
      ops.push_back(ir_stop());
    }
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("charge3"));
    ops.push_back(ir_call("noop"));
    ops.push_back(ir_label("t3"));
    append(ops, direct_tail(3, "t2"));
    ops.push_back(ir_label("noop"));
    ops.push_back(ir_return());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 3 &&
                search.analysis.plan.transitions == 3,
            "same-target no-op helper retargeting should derive tail-chain subgroups before the "
            "bounded combination search cap is exhausted");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 3,
            "chain-derived no-op helper subgroup retargeting should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    append(ops, ir_jump_body("entry"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity,
            "IR tail layout scanner should synthesize an entry label before leading IR ops");
    require(search.materialized,
            "synthetic-entry tail chains should still produce a structural materialization");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_plain(9));
    append(ops, ir_jump_body("entry"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "synthetic-entry tail chains should preserve leading prefix work before the first "
            "real label");
    const auto generated_entry_it =
        std::find_if(search.materialized_items.begin(), search.materialized_items.end(),
                     [](const MachineItem& item) {
                       return item.kind == MachineItemKind::Label &&
                              item.name == "__return_stack_entry_0";
                     });
    require(generated_entry_it != search.materialized_items.end(),
            "synthetic-entry materialization should keep the generated charging entry label");
    require(std::next(generated_entry_it) != search.materialized_items.end() &&
                std::next(generated_entry_it)->kind == MachineItemKind::Op &&
                std::next(generated_entry_it)->opcode == 9 &&
                std::next(generated_entry_it, 2) != search.materialized_items.end() &&
                std::next(generated_entry_it, 2)->kind == MachineItemKind::Op &&
                std::next(generated_entry_it, 2)->opcode == 0x51,
            "synthetic-entry materialization should keep leading work in the generated entry "
            "before the original entry jump");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_plain(9));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "synthetic-entry tail chains should preserve leading fallthrough prefix work before "
            "the first real label");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "synthetic-entry fallthrough prefixes should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("short_entry"));
    append(ops, ir_jump_body("s2"));
    ops.push_back(ir_label("s2"));
    append(ops, direct_tail(2, "s1"));
    ops.push_back(ir_label("s1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("long_entry"));
    append(ops, ir_jump_body("l4"));
    ops.push_back(ir_label("l4"));
    append(ops, direct_tail(4, "l3"));
    ops.push_back(ir_label("l3"));
    append(ops, direct_tail(3, "l2"));
    ops.push_back(ir_label("l2"));
    append(ops, direct_tail(2, "l1"));
    ops.push_back(ir_label("l1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized &&
                search.analysis.plan.transitions == 4,
            "embedded CFG tail-chain scanner should keep scanning and choose the longest valid "
            "materialization candidate");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2_alias"));
    ops.push_back(ir_label("t2_alias"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1_alias"));
    ops.push_back(ir_label("t1_alias"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "embedded tail-chain scanner should follow CFG fallthrough through empty aliases");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "CFG alias-normalized embedded tail chains should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_raw_cond_jump(0x57, "skip"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("skip"));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "embedded tail-chain scanner should split raw conditional prefixes before a chain");
    const auto entry_it =
        std::find_if(search.materialized_items.begin(), search.materialized_items.end(),
                     [](const MachineItem& item) {
                       return item.kind == MachineItemKind::Label && item.name == "entry";
                     });
    require(entry_it != search.materialized_items.end() &&
                std::next(entry_it) != search.materialized_items.end() &&
                std::next(entry_it)->kind == MachineItemKind::Op &&
                std::next(entry_it)->opcode == 0x57,
            "raw conditional prefixes should stay before the materialized charge chain");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "raw conditional-prefix embedded tail chains should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_cond_jump("skip"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("skip"));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "embedded tail-chain scanner should split semantic conditional prefixes before a "
            "chain");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "semantic conditional-prefix embedded tail chains should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_loop("repeat"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("repeat"));
    ops.push_back(ir_plain(8));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "embedded tail-chain scanner should split semantic loop prefixes before a "
            "fallthrough chain");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "semantic loop-prefix fallthrough tail chains should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_return());
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.has_opportunity && !search.materialized,
            "embedded tail-chain scanner should not follow CFG fallthrough after semantic "
            "return terminators");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_stop());
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.has_opportunity && !search.materialized,
            "embedded tail-chain scanner should not follow CFG fallthrough after semantic "
            "stop terminators");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("done"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("done"));
    ops.push_back(ir_plain(8));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.has_opportunity && !search.materialized,
            "embedded tail-chain scanner should not follow CFG fallthrough after semantic "
            "jump terminators");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_call("helper"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("helper"));
    ops.push_back(ir_return());
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "embedded tail-chain scanner should follow CFG call continuation edges before a "
            "tail chain");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "call-continuation embedded tail chains should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("external"));
    ops.push_back(ir_cond_jump("t2"));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.materialized && search.cfg_tail_external_entry_rejections >= 1 &&
                search.cfg_tail_external_entry_labels == std::vector<std::string>({"t2"}) &&
                search.cfg_tail_external_predecessor_labels ==
                    std::vector<std::string>({"external"}),
            "embedded tail-chain scanner should treat semantic conditional jump targets as "
            "external CFG entries into a candidate tail chain");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_loop("skip"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("skip"));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "embedded tail-chain scanner should split semantic loop prefixes before a chain");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "semantic loop-prefix embedded tail chains should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_call("helper"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("helper"));
    ops.push_back(ir_return());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "embedded tail-chain scanner should split direct-call prefixes before a chain");
    require(search.extracted_existing_callsite_fragments == 0,
            "direct-call prefixes should stay CFG prefixes instead of becoming callsite "
            "fragments");
    const auto entry_it =
        std::find_if(search.materialized_items.begin(), search.materialized_items.end(),
                     [](const MachineItem& item) {
                       return item.kind == MachineItemKind::Label && item.name == "entry";
                     });
    require(entry_it != search.materialized_items.end() &&
                std::next(entry_it) != search.materialized_items.end() &&
                std::next(entry_it)->kind == MachineItemKind::Op &&
                std::next(entry_it)->opcode == 0x53,
            "direct-call prefixes should stay before the materialized charge chain");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "direct-call-prefix embedded tail chains should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_indirect_call());
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "embedded tail-chain scanner should split indirect-call prefixes before a chain");
    const auto entry_it =
        std::find_if(search.materialized_items.begin(), search.materialized_items.end(),
                     [](const MachineItem& item) {
                       return item.kind == MachineItemKind::Label && item.name == "entry";
                     });
    require(entry_it != search.materialized_items.end() &&
                std::next(entry_it) != search.materialized_items.end() &&
                std::next(entry_it)->kind == MachineItemKind::Op &&
                std::next(entry_it)->opcode == 0xa0,
            "indirect-call prefixes should stay before the materialized charge chain");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "indirect-call-prefix embedded tail chains should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_indirect_cond_jump());
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "embedded tail-chain scanner should split indirect conditional prefixes before a "
            "chain");
    const auto entry_it =
        std::find_if(search.materialized_items.begin(), search.materialized_items.end(),
                     [](const MachineItem& item) {
                       return item.kind == MachineItemKind::Label && item.name == "entry";
                     });
    require(entry_it != search.materialized_items.end() &&
                std::next(entry_it) != search.materialized_items.end() &&
                std::next(entry_it)->kind == MachineItemKind::Op &&
                std::next(entry_it)->opcode == 0x70,
            "indirect conditional prefixes should stay before the materialized charge chain");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "indirect conditional-prefix embedded tail chains should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_raw_indirect_jump());
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.materialized,
            "embedded tail-chain scanner should not materialize unreachable blocks after raw "
            "indirect jumps");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_indirect_jump());
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.materialized,
            "embedded tail-chain scanner should not materialize unreachable blocks after "
            "semantic indirect jumps");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_raw_jump("t2"));
    ops.push_back(ir_label("t2"));
    ops.push_back(ir_plain(2));
    ops.push_back(ir_raw_jump("t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "embedded tail-chain scanner should treat raw БП opcodes as terminal jumps");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "raw БП embedded tail chains should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_plain(9));
    ops.push_back(ir_plain(6));
    ops.push_back(ir_plain(7));
    append(ops, ir_jump_body("t1"));
    ops.push_back(ir_label("alternate"));
    ops.push_back(ir_plain(8));
    ops.push_back(ir_plain(7));
    append(ops, ir_jump_body("t1"));
    ops.push_back(ir_label("existing_tail"));
    ops.push_back(ir_plain(6));
    ops.push_back(ir_plain(7));
    append(ops, ir_jump_body("t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.reused_existing_tail_fragments == 1 &&
                search.extracted_tail_fragments == 1 &&
                search.rewritten_tail_fragments >= 2,
            "IR tail layout scanner should reuse whole existing tail blocks instead of "
            "duplicating longer common suffixes");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("charge1"));
    ops.push_back(ir_call("charge2"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(7));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("charge2"));
    ops.push_back(ir_call("entry"));
    ops.push_back(ir_label("t2"));
    ops.push_back(ir_plain(9));
    ops.push_back(ir_plain(7));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.reused_existing_tail_fragments == 1 &&
                search.extracted_tail_fragments == 0 &&
                search.rewritten_tail_fragments == 1 &&
                search.has_opportunity && search.materialized &&
                search.analysis.plan.existing_call_sites == 2 &&
                search.analysis.plan.paid_call_sites == 0 &&
                search.analysis.plan.profitable,
            "IR tail layout scanner should rescan existing-call chains after rewrite-only "
            "whole-tail reuse");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("body"));
    ops.push_back(ir_label("body"));
    ops.push_back(ir_plain(9));
    append(ops, ir_jump_body("t1"));
    ops.push_back(ir_plain(8));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "IR tail layout scanner should split internal flow boundaries into CFG tail blocks");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "internally split CFG tail chains should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "call_tail"));
    ops.push_back(ir_label("call_tail"));
    ops.push_back(ir_call("helper"));
    ops.push_back(ir_label("after_helper"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());
    ops.push_back(ir_label("helper"));
    ops.push_back(ir_return());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity && search.materialized,
            "IR tail layout scanner should allow a terminal ПП fragment only as the final stop "
            "tail after scripted returns are consumed");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "terminal stop ПП fragments should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.has_opportunity && search.cfg_tail_entry_candidates == 1 &&
                search.cfg_tail_short_chain_candidates == 1 &&
                search.cfg_tail_valid_chain_candidates == 0,
            "IR tail layout scanner should expose one-tail candidates as short CFG chains");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("body"));
    ops.push_back(ir_label("body"));
    ops.push_back(ir_plain(7));
    ops.push_back(ir_label("done"));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.has_opportunity && search.cfg_tail_entry_candidates == 1 &&
                search.cfg_tail_broken_chain_candidates == 1 &&
                search.cfg_tail_nonterminal_chain_candidates == 1 &&
                search.cfg_tail_nonterminal_break_labels == std::vector<std::string>({"body"}),
            "IR tail layout scanner should expose nonterminal blocks as broken CFG chains");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_plain(9));
    ops.push_back(ir_plain(8));
    ops.push_back(ir_plain(7));
    append(ops, ir_jump_body("t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.extracted_tail_fragments == 1 && search.rewritten_tail_fragments == 1 &&
                search.has_opportunity && search.materialized,
            "IR tail layout scanner should extract terminal suffixes from inside labelled blocks");
    const auto fragment_it =
        std::find_if(search.materialized_items.begin(), search.materialized_items.end(),
                     [](const MachineItem& item) {
                       return item.kind == MachineItemKind::Label &&
                              item.name == "__return_stack_tail_fragment_0";
                     });
    require(fragment_it != search.materialized_items.end(),
            "IR tail layout scanner should materialize the extracted suffix as a hidden tail");
    require(std::next(fragment_it) != search.materialized_items.end() &&
                std::next(fragment_it)->kind == MachineItemKind::Op &&
                std::next(fragment_it)->opcode == 7,
            "IR tail layout scanner should extract the shortest useful terminal suffix");
    const auto entry_it =
        std::find_if(search.materialized_items.begin(), search.materialized_items.end(),
                     [](const MachineItem& item) {
                       return item.kind == MachineItemKind::Label &&
                              item.name == "__return_stack_entry_0";
                     });
    require(entry_it != search.materialized_items.end(),
            "IR tail layout scanner should materialize the generated charging entry label");
    require(std::next(entry_it) != search.materialized_items.end() &&
                std::next(entry_it)->kind == MachineItemKind::Op &&
                std::next(entry_it)->opcode == 9 &&
                std::next(entry_it, 2) != search.materialized_items.end() &&
                std::next(entry_it, 2)->kind == MachineItemKind::Op &&
                std::next(entry_it, 2)->opcode == 8,
            "IR tail layout scanner should keep prefix work before the extracted suffix");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_plain(9));
    ops.push_back(ir_plain(8));
    ops.push_back(ir_plain(7));
    ops.push_back(ir_raw_jump("t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.extracted_tail_fragments == 1 && search.rewritten_tail_fragments == 1 &&
                search.has_opportunity && search.materialized,
            "IR tail layout scanner should extract terminal raw БП suffixes from inside labelled "
            "blocks");
    require(core::optimize_post_layout_return_stack_script(search.materialized_items).applied == 2,
            "terminal raw БП suffix extraction should remain provable post-layout");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_plain(9));
    ops.push_back(ir_plain(8));
    ops.push_back(ir_plain(7));
    append(ops, ir_jump_body("t1"));
    ops.push_back(ir_label("alternate"));
    ops.push_back(ir_plain(6));
    ops.push_back(ir_plain(7));
    append(ops, ir_jump_body("t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.extracted_tail_fragments == 1 && search.rewritten_tail_fragments == 2,
            "IR tail layout scanner should materialize identical terminal suffix bodies once");
  }

  {
    std::vector<IrOp> ops;
    IrOp left_shared = ir_plain(7);
    left_shared.meta.comment = "left source";
    IrOp left_jump = ir_jump("t1");
    left_jump.meta.comment = "left jump";
    IrOp right_shared = ir_plain(7);
    right_shared.meta.comment = "right source";
    IrOp right_jump = ir_jump("t1");
    right_jump.meta.comment = "right jump";

    ops.push_back(ir_label("entry"));
    ops.push_back(ir_plain(9));
    ops.push_back(ir_plain(8));
    ops.push_back(left_shared);
    ops.push_back(left_jump);
    ops.push_back(ir_label("alternate"));
    ops.push_back(ir_plain(6));
    ops.push_back(right_shared);
    ops.push_back(right_jump);
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.extracted_tail_fragments == 1 && search.rewritten_tail_fragments == 2,
            "IR tail layout scanner should deduplicate semantically identical suffixes even when "
            "comments differ");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    ops.push_back(ir_plain(9));
    ops.push_back(ir_plain(6));
    ops.push_back(ir_plain(7));
    append(ops, ir_jump_body("t1"));
    ops.push_back(ir_label("alternate"));
    ops.push_back(ir_plain(8));
    ops.push_back(ir_plain(6));
    ops.push_back(ir_plain(7));
    append(ops, ir_jump_body("t1"));
    ops.push_back(ir_label("shorter"));
    ops.push_back(ir_plain(5));
    ops.push_back(ir_plain(7));
    append(ops, ir_jump_body("t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.extracted_tail_fragments == 2 && search.rewritten_tail_fragments == 3,
            "IR tail layout scanner should prefer longer common terminal suffixes when they "
            "are shared by multiple blocks");
  }



  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("prefix"));
    append(ops, ir_jump_body("entry"));
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(search.has_opportunity,
            "IR tail layout scanner should detect embedded movable tail chains");
    require(search.analysis.plan.rejection_reason.find("net-savings") != std::string::npos,
            "embedded injected-charge chains should still be rejected when they are not profitable");
  }

  {
    std::vector<IrOp> ops;
    ops.push_back(ir_label("entry"));
    append(ops, ir_jump_body("t2"));
    ops.push_back(ir_label("external"));
    append(ops, ir_jump_body("t1"));
    ops.push_back(ir_label("t2"));
    append(ops, direct_tail(2, "t1"));
    ops.push_back(ir_label("t1"));
    ops.push_back(ir_plain(1));
    ops.push_back(ir_stop());

    const core::ReturnStackIrTailLayoutSearch search =
        core::analyze_return_stack_ir_tail_layout(ops);
    require(!search.materialized && search.cfg_tail_valid_chain_candidates == 1 &&
                search.cfg_tail_external_entry_rejections == 1,
            "IR tail layout scanner should expose CFG blockers for valid-length chains with "
            "external tail entries");
  }

  {
    const std::vector<MachineItem> program = three_step_script_program();
    const core::ReturnStackScriptOpportunityScan scan =
        core::scan_return_stack_script_opportunity(program);
    require(scan.possible && scan.direct_call_sites == 3 && scan.direct_jumps == 3 &&
                scan.chained_call_sites >= 2,
            "return-stack pre-scan should recognize possible scripted charge-chain programs");

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_return_stack_script(program);

    require(result.applied == 3,
            "return-stack script should rewrite the fixed T3 -> T2 -> T1 script");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 3,
            "return-stack script should save one cell per scripted transition");
    require(count_opcode(result.items, 0x52) == 3,
            "return-stack script should emit one В/О per rewritten transition");
    require(count_opcode(result.items, 0x51) == 0,
            "return-stack script should remove the direct jumps in the fixed script");
    require(count_opcode(result.items, 0x53) == 3,
            "return-stack script should keep the existing ПП charge chain");
    require(has_optimization(result, "return-stack-script"),
            "return-stack script should report optimization metadata");
  }

  {
    const std::vector<MachineItem> program = proven_indirect_call_script_program();
    const core::ReturnStackScriptOpportunityScan scan =
        core::scan_return_stack_script_opportunity(program);
    require(scan.possible && scan.direct_call_sites == 2 && scan.chained_call_sites == 1,
            "return-stack pre-scan should accept proven indirect К ПП charge-chain callsites");

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_return_stack_script(program);
    require(result.applied == 2,
            "return-stack script should rewrite reverse tails charged by proven indirect К ПП");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 2,
            "proven indirect К ПП return-stack script should save one cell per rewritten tail");
    require(count_opcode(result.items, 0x52) == 2,
            "proven indirect К ПП return-stack script should emit В/О continuations");
    require(count_opcode(result.items, 0xa0) == 1 && count_opcode(result.items, 0xa1) == 1,
            "proven indirect К ПП return-stack script should keep the existing charge calls");
  }

  {
    const std::vector<MachineItem> program = counted_script_program(5);
    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_return_stack_script(program);

    require(result.applied == 5,
            "return-stack script should rewrite the maximum five-transition script");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 5,
            "five-transition return-stack script should save five cells");
    require(count_opcode(result.items, 0x52) == 5,
            "five-transition return-stack script should emit five В/О commands");
    require(count_opcode(result.items, 0x53) == 5,
            "five-transition return-stack script should keep five ПП charge sites");
  }

  {
    const std::vector<MachineItem> program = dirty_overflow_script_program();
    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_return_stack_script(program);

    require(result.applied == 6,
            "dirty-overflow return-stack script should rewrite five clean jumps plus the dirty "
            "dispatch jump");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 6,
            "dirty-overflow return-stack script should save the dirty jump address cell too");
    require(count_opcode(result.items, 0x52) == 6,
            "dirty-overflow return-stack script should emit six В/О commands");
    const core::DirtyReturnStackDispatchPlan dirty =
        core::plan_dirty_return_stack_dispatch({27, 31, 35, 39, 43}, 6, result.items,
                                               {.size_rescue = true});
    require(dirty.enabled && dirty.layout_proved &&
                dirty.dirty_targets == std::vector<int>({78}),
            "dirty-overflow return-stack script should leave the shifted dirty target executable");

    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout({27, 31, 35, 39, 43}, 6,
                                                          result.items,
                                                          {.size_rescue = true});
    require(allocation.allocated && allocation.dispatch.layout_proved &&
                allocation.padding_cells == 0 &&
                core::machine_cell_count(allocation.items) ==
                    core::machine_cell_count(result.items),
            "dirty-dispatch allocator should return the existing proven layout when no padding is "
            "needed");
    require(has_optimization(result, "return-stack-dirty-dispatch"),
            "dirty-overflow no-padding proof should report dirty-dispatch metadata");
    require(!has_optimization(result, "return-stack-dirty-dispatch-allocator"),
            "dirty-overflow no-padding proof should not report allocator repair metadata");
    require(optimization_detail_contains(result, "return-stack-dirty-dispatch",
                                         "Proved existing executable dirty-dispatch cell"),
            "dirty-overflow no-padding metadata should distinguish existing-cell proof from "
            "padding repair");
    require(optimization_detail_contains(result, "return-stack-dirty-dispatch",
                                         "dirty target cell 78"),
            "dirty-overflow no-padding metadata should expose the proved dirty target cell");
    require(optimization_detail_contains(result, "return-stack-dirty-dispatch",
                                         "dirty return address 77"),
            "dirty-overflow no-padding metadata should expose the dirty stored return address");
    require(optimization_detail_contains(result, "return-stack-dirty-dispatch",
                                         "covering 1 dirty return"),
            "dirty-overflow no-padding metadata should expose how many dirty returns were "
            "covered");
  }

  {
    const std::vector<MachineItem> program = dirty_overflow_script_program(false);
    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_return_stack_script(program);

    require(result.applied == 6,
            "dirty-overflow return-stack script should repair unsafe dirty cells before rewrite");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 5,
            "dirty-overflow repair should still be profitable after inserting one safe cell");
    require(has_optimization(result, "return-stack-dirty-dispatch-allocator"),
            "dirty-overflow repair should report allocator metadata");
    require(!has_optimization(result, "return-stack-dirty-dispatch"),
            "dirty-overflow repair should not report proof-only dirty-dispatch metadata");
    require(optimization_detail_contains(result, "return-stack-dirty-dispatch-allocator",
                                         "Inserted 1 executable dirty-dispatch cell"),
            "dirty-overflow repair metadata should expose the inserted safe cell count");
    require(optimization_detail_contains(result, "return-stack-dirty-dispatch-allocator",
                                         "across 1 fixed-point repair round"),
            "dirty-overflow repair metadata should expose the fixed-point repair round count");
    require(optimization_detail_contains(result, "return-stack-dirty-dispatch-allocator",
                                         "dirty target cell 78"),
            "dirty-overflow repair metadata should expose the proved dirty target cell");
    require(optimization_detail_contains(result, "return-stack-dirty-dispatch-allocator",
                                         "dirty return address 77"),
            "dirty-overflow repair metadata should expose the dirty stored return address");
    require(optimization_detail_contains(result, "return-stack-dirty-dispatch-allocator",
                                         "covering 1 dirty return"),
            "dirty-overflow repair metadata should expose how many dirty returns were covered");
    require(count_opcode(result.items, 0x52) == 6,
            "dirty-overflow repair should still emit six В/О commands");
    const core::DirtyReturnStackDispatchPlan dirty =
        core::plan_dirty_return_stack_dispatch({27, 31, 35, 39, 43}, 6, result.items,
                                               {.size_rescue = true});
    require(dirty.enabled && dirty.layout_proved &&
                dirty.dirty_targets == std::vector<int>({78}),
            "dirty-overflow repair should prove the inserted dirty target cell");
  }

  {
    std::vector<MachineItem> one_return;
    one_return.push_back(MachineItem::label("charge"));
    append(one_return, call("entry"));
    one_return.push_back(MachineItem::label("tail"));
    one_return.push_back(digit(1));
    one_return.push_back(stop());
    one_return.push_back(MachineItem::label("entry"));
    append(one_return, jump("tail"));

    const core::ReturnStackScriptOpportunityScan scan =
        core::scan_return_stack_script_opportunity(one_return);
    require(!scan.possible && scan.rejection_reason.find("at least two") != std::string::npos,
            "return-stack pre-scan should cheaply reject one-transition programs");
  }

  {
    const std::vector<MachineItem> program = overlay_tie_script_program();
    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_return_stack_script(program);

    require(result.applied == 0,
            "return-stack script should not replace a БП address that address-code-overlay can "
            "reuse at equal size");
    const core::PostLayoutIndirectFlowResult overlay =
        core::optimize_post_layout_address_code_overlay(program);
    require(overlay.applied == 2,
            "overlay tie fixture should prove two address/code overlays");
    require(core::machine_cell_count(overlay.items) == core::machine_cell_count(program) - 2,
            "overlay tie fixture should save the same two cells without consuming return stack");
  }

  {
    const std::vector<MachineItem> program = three_step_script_program();
    const core::ReturnStackPostLayoutPipelineComparison comparison =
        core::compare_return_stack_post_layout_pipeline(program, program, CompileOptions{});
    require(comparison.current.return_stack_script_applied == 3 &&
                comparison.candidate.return_stack_script_applied == 3,
            "layout-aware pipeline comparison should run return-stack-script on both baseline "
            "and materialized candidates");
    require(comparison.current.final_cells == comparison.candidate.final_cells &&
                !comparison.candidate_better,
            "identical layouts should tie after the full return-stack-script/overlay/flow pipeline");
  }

  {
    std::vector<MachineItem> ambiguous = three_step_script_program();
    ambiguous.push_back(MachineItem::label("external"));
    append(ambiguous, jump("t2"));
    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_return_stack_script(ambiguous);

    require(result.applied == 0,
            "return-stack script should reject targets with extra non-script incoming flow");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(ambiguous),
            "rejected return-stack script should preserve the program");
  }

  {
    std::vector<MachineItem> one_return;
    one_return.push_back(MachineItem::label("charge"));
    append(one_return, call("entry"));
    one_return.push_back(MachineItem::label("tail"));
    one_return.push_back(digit(1));
    one_return.push_back(stop());
    one_return.push_back(MachineItem::label("entry"));
    append(one_return, jump("tail"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_return_stack_script(one_return);
    require(result.applied == 0,
            "return-stack script should not spend the strategy on a one-transition script");
  }

  {
    const std::vector<std::vector<int>> matrix = {
        {19, 27, 35, 43, 51, 99, 100},
        {27, 35, 43, 51, 59, 77, 78},
        {35, 43, 51, 59, 67, 55, 56},
        {43, 51, 59, 67, 75, 33, 34},
        {51, 59, 67, 75, 83, 11, 12},
    };
    for (const std::vector<int>& row : matrix) {
      std::vector<MachineItem> layout = repeated_stop_layout(row.at(6) + 2);
      layout.at(static_cast<std::size_t>(row.at(6))) = digit(8);
      const core::DirtyReturnStackDispatchPlan plan =
          core::plan_dirty_return_stack_dispatch(
              {row.at(0), row.at(1), row.at(2), row.at(3), row.at(4)}, 6, layout,
              {.size_rescue = true});
      require(plan.enabled && plan.layout_proved,
              "dirty dispatch layout proof should accept safe matrix cells");
      require(plan.dirty_return_addresses == std::vector<int>({row.at(5)}) &&
                  plan.dirty_targets == std::vector<int>({row.at(6)}),
              "dirty dispatch matrix should expose stored return and actual PC");
      require(plan.cell_proofs.size() == 1 && plan.cell_proofs.front().safe &&
                  plan.cell_proofs.front().required_opcode == 0x08,
              "dirty dispatch proof should expose the required opcode");

      const std::vector<core::DirtyReturnStackDispatchAllocationPlan> allocations =
          core::allocate_dirty_return_stack_dispatch_layouts(layout, {.size_rescue = true});
      const auto allocation_it =
          std::find_if(allocations.begin(), allocations.end(),
                       [&](const core::DirtyReturnStackDispatchAllocationPlan& allocation) {
                         return allocation.allocated && allocation.dispatch.layout_proved &&
                                allocation.padding_cells == 0 &&
                                allocation.dispatch.dirty_targets ==
                                    std::vector<int>({row.at(6)});
                       });
      require(allocation_it != allocations.end(),
              "dirty dispatch allocator search should include every generated matrix family");
    }

    std::vector<MachineItem> combined_layout = repeated_stop_layout(102);
    for (const std::vector<int>& row : matrix)
      combined_layout.at(static_cast<std::size_t>(row.at(6))) = digit(8);
    const std::vector<core::DirtyReturnStackDispatchAllocationPlan> allocations =
        core::allocate_dirty_return_stack_dispatch_layouts(combined_layout,
                                                           {.size_rescue = true});
    std::set<int> proved_dirty_targets;
    for (const core::DirtyReturnStackDispatchAllocationPlan& allocation : allocations) {
      if (allocation.allocated && allocation.dispatch.layout_proved &&
          allocation.padding_cells == 0 && allocation.dispatch.dirty_targets.size() == 1U) {
        proved_dirty_targets.insert(allocation.dispatch.dirty_targets.front());
      }
    }
    require(proved_dirty_targets == std::set<int>({12, 34, 56, 78, 100}),
            "dirty dispatch allocator search should stop after one full distinct dirty-target "
            "cycle");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(14);
    layout.at(12) = digit(8);
    const std::vector<core::DirtyReturnStackDispatchAllocationPlan> allocations =
        core::allocate_dirty_return_stack_dispatch_layouts(layout, {.size_rescue = true});

    const auto dirty_12 =
        std::find_if(allocations.begin(), allocations.end(),
                     [](const core::DirtyReturnStackDispatchAllocationPlan& allocation) {
                       return allocation.allocated && allocation.dispatch.layout_proved &&
                              allocation.padding_cells == 0 &&
                              allocation.dispatch.dirty_targets == std::vector<int>({12});
                     });
    require(dirty_12 != allocations.end(),
            "dirty dispatch allocator search should include the low-digit-1 target 12 family");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(14);
    layout.at(12) = digit(8);
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {51, 59, 67, 75, 83}, 6, layout,
            {.size_rescue = true, .max_fixed_point_rounds = 0});
    require(allocation.allocated && allocation.dispatch.layout_proved &&
                allocation.padding_cells == 0 && allocation.fixed_point_rounds == 0 &&
                allocation.dispatch.dirty_targets == std::vector<int>({12}),
            "dirty dispatch allocator should accept already-proved layouts even when the "
            "fixed-point repair round budget is zero");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(14);
    layout.at(12) = digit(8);
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {51, 59, 67, 75, 83}, 6, layout,
            {.size_rescue = true, .max_padding_cells = -1});
    require(allocation.allocated && allocation.dispatch.layout_proved &&
                allocation.padding_cells == 0 && allocation.dispatch.dirty_targets ==
                    std::vector<int>({12}),
            "dirty dispatch allocator should accept already-proved layouts even when the "
            "padding repair budget is negative because no padding search is needed");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(102);
    layout.at(100) = digit(8);
    layout.at(78) = stop();
    const std::vector<core::DirtyReturnStackDispatchAllocationPlan> allocations =
        core::allocate_dirty_return_stack_dispatch_layouts(
            layout, {.size_rescue = true, .max_padding_cells = 2});

    const auto safe_first =
        std::find_if(allocations.begin(), allocations.end(),
                     [](const core::DirtyReturnStackDispatchAllocationPlan& allocation) {
                       return allocation.allocated && allocation.dispatch.layout_proved &&
                              allocation.padding_cells == 0 &&
                              allocation.dispatch.dirty_targets == std::vector<int>({100});
                     });
    const auto repaired_later =
        std::find_if(allocations.begin(), allocations.end(),
                     [](const core::DirtyReturnStackDispatchAllocationPlan& allocation) {
                       return allocation.allocated && allocation.dispatch.layout_proved &&
                              allocation.padding_cells == 1 &&
                              allocation.dispatch.dirty_targets == std::vector<int>({78});
                     });
    require(safe_first != allocations.end() && repaired_later != allocations.end(),
            "dirty dispatch layout search should keep scanning after an already-safe stack and "
            "also expose later repair allocations");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    layout.at(78) = MachineItem::op(0x51, "БП");
    const core::DirtyReturnStackDispatchPlan plan =
        core::plan_dirty_return_stack_dispatch({27, 35, 43, 51, 59}, 6, layout,
                                               {.size_rescue = true});
    require(!plan.enabled && !plan.layout_proved &&
                plan.cell_proofs.front().rejection_reason.find("address-taking") !=
                    std::string::npos,
            "dirty dispatch proof should reject address-taking opcode without operand");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(76);
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {27, 35, 43, 51, 59}, 6, layout,
            {.size_rescue = true, .max_padding_cells = 4});
    require(allocation.allocated && allocation.padding_cells == 3 &&
                allocation.fixed_point_rounds == 1 && allocation.size_rescue_only &&
                !allocation.control_flow_rewrite_enabled && allocation.dispatch.layout_proved,
            "dirty dispatch allocator should append safe executable cells up to a dirty target");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(76);
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {27, 35, 43, 51, 59}, 6, layout,
            {.size_rescue = true, .max_padding_cells = 4, .allow_append_padding = false});
    require(!allocation.allocated &&
                allocation.rejection_reason.find("append-padding search is disabled") !=
                    std::string::npos,
            "dirty dispatch allocator should honor disabled append-padding search");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    layout.at(78) = stop();
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {27, 35, 43, 51, 59}, 6, layout,
            {.size_rescue = true, .max_padding_cells = 2});
    require(allocation.allocated && allocation.padding_cells == 1 &&
                allocation.fixed_point_rounds == 1 && allocation.dispatch.layout_proved,
            "dirty dispatch allocator should repair unsafe existing target cells by insertion");
    require(core::machine_cell_count(allocation.items) == 81 &&
                allocation.items.at(78).kind == MachineItemKind::Op &&
                allocation.items.at(78).opcode == 0x10,
            "dirty dispatch allocator should insert a safe executable cell at the dirty target");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    layout.at(10) = MachineItem::op(0x51, "БП");
    layout.at(11) = MachineItem::address(5);
    layout.at(78) = stop();
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {27, 35, 43, 51, 59}, 6, layout,
            {.size_rescue = true, .max_padding_cells = 2});
    require(allocation.allocated && allocation.padding_cells == 1 &&
                allocation.fixed_point_rounds == 1 && allocation.dispatch.layout_proved,
            "dirty dispatch allocator should repair insertion layouts when numeric targets stay "
            "left of the insertion address");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    layout.at(10) = MachineItem::op(0x51, "БП");
    layout.at(11) = MachineItem::address(78);
    layout.at(78) = stop();
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {27, 35, 43, 51, 59}, 6, layout,
            {.size_rescue = true, .max_padding_cells = 2});
    const int* remapped_target = std::get_if<int>(&allocation.items.at(11).target);
    require(allocation.allocated && allocation.padding_cells == 1 &&
                allocation.fixed_point_rounds == 1 && allocation.dispatch.layout_proved &&
                remapped_target != nullptr && *remapped_target == 79,
            "dirty dispatch allocator should remap numeric targets shifted by repair insertion");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(105);
    layout.at(10) = MachineItem::op(0x51, "БП");
    layout.at(11) = MachineItem::address(104);
    layout.at(78) = stop();
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {27, 35, 43, 51, 59}, 6, layout,
            {.size_rescue = true, .max_padding_cells = 2});
    require(!allocation.allocated &&
                allocation.rejection_reason.find("outside official 00..A4 cells") !=
                    std::string::npos,
            "dirty dispatch allocator should reject numeric target remaps beyond A4");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    layout.at(10) = MachineItem::op(0xa0, "КП");
    layout.at(10).comment = "indirect-target=78; note; indirect-target=79";
    layout.at(78) = stop();
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {27, 35, 43, 51, 59}, 6, layout,
            {.size_rescue = true, .max_padding_cells = 2});
    require(allocation.allocated && allocation.items.at(10).comment.has_value() &&
                *allocation.items.at(10).comment ==
                    "indirect-target=79; note; indirect-target=80",
            "dirty dispatch allocator should remap every proven indirect target comment shifted "
            "by repair insertion");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(105);
    layout.at(10) = MachineItem::op(0xa0, "КП");
    layout.at(10).comment = "indirect-target=104";
    layout.at(78) = stop();
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {27, 35, 43, 51, 59}, 6, layout,
            {.size_rescue = true, .max_padding_cells = 2});
    require(!allocation.allocated &&
                allocation.rejection_reason.find("proven indirect target comments outside") !=
                    std::string::npos,
            "dirty dispatch allocator should reject proven indirect target comment remaps beyond "
            "A4");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    layout.at(78) = stop();
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {27, 35, 43, 51, 59}, 6, layout,
            {.size_rescue = true, .max_padding_cells = 0});
    require(!allocation.allocated && allocation.padding_cells == 0 &&
                allocation.rejection_reason.find("above limit 0") != std::string::npos,
            "dirty dispatch allocator should honor a zero padding budget instead of inserting "
            "a safe dirty-target cell");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    layout.at(78) = stop();
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {27, 35, 43, 51, 59}, 6, layout,
            {.size_rescue = true, .max_padding_cells = -1});
    require(!allocation.allocated && allocation.padding_cells == 0 &&
                allocation.rejection_reason.find("non-negative padding budget") !=
                    std::string::npos,
            "dirty dispatch allocator should reject negative padding budgets before running "
            "fixed-point repair search");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    layout.at(78) = stop();
    const core::DirtyReturnStackDispatchAllocationPlan allocation =
        core::allocate_dirty_return_stack_dispatch_layout(
            {27, 35, 43, 51, 59}, 6, layout,
            {.size_rescue = true, .max_padding_cells = 2, .max_fixed_point_rounds = 0});
    require(!allocation.allocated && allocation.fixed_point_rounds == 0 &&
                allocation.rejection_reason.find("at least one fixed-point round") !=
                    std::string::npos,
            "dirty dispatch allocator should honor a zero fixed-point round budget instead of "
            "silently running one repair round");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    layout.at(78) = digit(8);
    const core::DirtyReturnStackDispatchPlan plan =
        core::plan_dirty_return_stack_dispatch({27, 35, 43, 51, 59}, 8, layout,
                                               {.size_rescue = true});
    require(plan.enabled && plan.layout_proved,
            "dirty dispatch proof should accept repeated dirty returns to the same safe cell");
    require(plan.dirty_return_addresses == std::vector<int>({77, 77, 77}) &&
                plan.dirty_targets == std::vector<int>({78, 78, 78}) &&
                plan.cell_proofs.size() == 3,
            "dirty dispatch proof should expose every repeated dirty stored return and actual PC");
    require(std::all_of(plan.cell_proofs.begin(), plan.cell_proofs.end(),
                        [](const core::DirtyReturnStackDispatchCellProof& proof) {
                          return proof.safe && proof.required_opcode == 0x08;
                        }),
            "dirty dispatch proof should prove each repeated dirty target cell");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    layout.at(78) = digit(8);
    const std::vector<core::DirtyReturnStackDispatchAllocationPlan> allocations =
        core::allocate_dirty_return_stack_dispatch_layouts(
            layout, {.size_rescue = true, .min_dirty_targets = 3});

    const auto repeated_dirty =
        std::find_if(allocations.begin(), allocations.end(),
                     [](const core::DirtyReturnStackDispatchAllocationPlan& allocation) {
                       return allocation.allocated && allocation.dispatch.layout_proved &&
                              allocation.padding_cells == 0 &&
                              allocation.dispatch.dirty_targets ==
                                  std::vector<int>({78, 78, 78});
                     });
    require(repeated_dirty != allocations.end(),
            "dirty dispatch allocator search should honor requested multi-dirty target counts");
    require(!allocations.empty() && allocations.front().allocated &&
                allocations.front().dispatch.layout_proved &&
                allocations.front().padding_cells == 0 &&
                allocations.front().dispatch.dirty_targets ==
                    std::vector<int>({78, 78, 78}),
            "dirty dispatch allocator search should rank already-proved layouts before earlier "
            "candidate stacks that need padding or fail");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    layout.at(78) = digit(7);
    layout.at(79) = digit(8);
    const core::DirtyReturnStackDispatchPlan plan =
        core::plan_dirty_return_stack_dispatch({27, 35, 43, 51, 59}, 6, layout,
                                               {.size_rescue = true});
    require(!plan.enabled && !plan.layout_proved &&
                plan.cell_proofs.front().rejection_reason.find("number-entry") !=
                    std::string::npos,
            "dirty dispatch proof should reject number-entry glue");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    layout.at(78) = stop();
    const core::DirtyReturnStackDispatchPlan plan =
        core::plan_dirty_return_stack_dispatch({27, 35, 43, 51, 59}, 6, layout,
                                               {.size_rescue = true});
    require(!plan.enabled && !plan.layout_proved &&
                plan.cell_proofs.front().rejection_reason.find("stops") != std::string::npos,
            "dirty dispatch proof should reject stop cells when fallthrough is required");
  }

  {
    std::vector<MachineItem> layout = repeated_stop_layout(80);
    MachineItem formal = MachineItem::address(0);
    formal.formal_opcode = 0xfa;
    layout.at(78) = formal;
    const core::DirtyReturnStackDispatchPlan plan =
        core::plan_dirty_return_stack_dispatch({27, 35, 43, 51, 59}, 6, layout,
                                               {.size_rescue = true});
    require(!plan.enabled && !plan.layout_proved &&
                plan.cell_proofs.front().rejection_reason.find("formal") != std::string::npos,
            "dirty dispatch proof should reject formal/super-dark cells without separate proof");
  }
}

} // namespace mkpro::tests
