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
