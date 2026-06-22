#include "mkpro/core/return_stack_script.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <optional>
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

std::vector<MachineItem> call(const std::string& target) {
  return {
      MachineItem::op(0x53, "ПП"),
      MachineItem::address(target),
  };
}

std::vector<MachineItem> jump(const std::string& target) {
  return {
      MachineItem::op(0x51, "БП"),
      MachineItem::address(target),
  };
}

std::vector<MachineItem> direct_tail(int value, const std::string& target) {
  return {
      digit(value),
      MachineItem::op(0x51, "БП"),
      MachineItem::address(target),
  };
}

void append(std::vector<MachineItem>& items, const std::vector<MachineItem>& tail) {
  items.insert(items.end(), tail.begin(), tail.end());
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
        .body = {digit(1), stop()},
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
        core::build_return_stack_startup_layout(tails, jump("t3"));

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
    require(core::optimize_post_layout_return_stack_script(plan.items).applied == 3,
            "startup layout producer should place ПП callsites so the detector can prove them");
  }

  {
    std::vector<core::ReturnStackTailBlock> tails;
    for (int index = 1; index <= 5; ++index) {
      tails.push_back(core::ReturnStackTailBlock{
          .label = "t" + std::to_string(index),
          .body = index == 1 ? std::vector<MachineItem>{digit(1), stop()}
                             : direct_tail(index, "t" + std::to_string(index - 1)),
      });
    }
    const core::ReturnStackStartupLayoutPlan plan =
        core::build_return_stack_startup_layout(tails, jump("t5"),
                                                "__return_stack_entry",
                                                {.existing_call_sites = 5});

    require(plan.strategy == "existing-callsite-layout",
            "layout producer should distinguish existing-callsite charging");
    require(plan.existing_call_sites == 5 && plan.paid_call_sites == 0,
            "existing-callsite layout should treat all charge sites as free");
    require(plan.charge_cost == 0 && plan.net_savings == 5 && plan.profitable,
            "existing-callsite layout should be profitable when charging is free");
    require(core::optimize_post_layout_return_stack_script(plan.items).applied == 5,
            "profitable generated layout should still be provable by the detector");
  }

  {
    const std::vector<MachineItem> program = three_step_script_program();
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
}

} // namespace mkpro::tests
