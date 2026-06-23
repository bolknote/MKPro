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

IrOp ir_call(const std::string& target) {
  IrOp op;
  op.kind = IrKind::Call;
  op.opcode = 0x53;
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
                    std::vector<std::string>({"shared_helper"}),
            "IR scanner should group symbolic terminal ПП hints by shared target label");
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
    require(search.extracted_tail_fragments == 1 && search.has_opportunity && search.materialized,
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
                std::next(fragment_it)->opcode == 8,
            "IR tail layout scanner should keep the whole terminal suffix, not only the last op");
    require(std::next(fragment_it, 2) != search.materialized_items.end() &&
                std::next(fragment_it, 2)->kind == MachineItemKind::Op &&
                std::next(fragment_it, 2)->opcode == 7,
            "IR tail layout scanner should preserve suffix operation order");
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
    }
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
