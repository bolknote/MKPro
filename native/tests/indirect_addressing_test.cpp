#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/compiler.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/zero_underflow_constant.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

void require_mutation(core::IndirectSelectorMutation actual,
                      core::IndirectSelectorMutation expected,
                      const std::string& message) {
  require(actual == expected, message);
}

}  // namespace

void indirect_addressing_matches_typescript_contract() {
  require_mutation(core::indirect_selector_mutation("0"),
                   core::IndirectSelectorMutation::PreDecrement,
                   "R0 should pre-decrement for indirect addressing");
  require_mutation(core::indirect_selector_mutation("4"),
                   core::IndirectSelectorMutation::PreIncrement,
                   "R4 should pre-increment for indirect addressing");
  require_mutation(core::indirect_selector_mutation("7"),
                   core::IndirectSelectorMutation::Stable,
                   "R7 should be stable for indirect addressing");
  require(core::is_stable_indirect_selector("e"), "Re should be a stable indirect selector");

  require(core::memory_target_from_transformed("02") == 2,
          "transformed 02 should target R2");
  require(core::memory_target_from_transformed("14") == 0x0e,
          "nonzero tens transformed 14 should target Re");
  require(core::memory_target_from_transformed("-99999999") == 3,
          "negative transformed selector should use the nonzero-tens tail table");

  const auto pre_increment =
      core::evaluate_indirect_address("5", "7", core::IndirectOperationKind::Memory);
  require(pre_increment.has_value(), "R5 memory selector 7 should evaluate");
  require(pre_increment->mutation == core::IndirectSelectorMutation::PreIncrement,
          "R5 should report pre-increment mutation");
  require(pre_increment->transformed == "8", "R5 selector 7 should transform to 8");
  require(pre_increment->memory_target == 8, "transformed 8 should target R8");

  const auto stable_memory =
      core::evaluate_indirect_address("7", "14", core::IndirectOperationKind::Memory);
  require(stable_memory.has_value(), "R7 memory selector 14 should evaluate");
  require(stable_memory->mutation == core::IndirectSelectorMutation::Stable,
          "R7 should report stable mutation");
  require(stable_memory->transformed == "14", "stable selector should preserve 14");
  require(stable_memory->memory_target == 0x0e, "stable transformed 14 should target Re");

  // These are ROM word-format facts, not numeric equality assumptions.
  // Expected targets and post-update words are independent of the evaluator.
  struct SelectorWordCase {
    std::string value;
    int decrement_target;
    int increment_target;
    int stable_target;
    int decrement_memory;
    int increment_memory;
    int stable_memory;
    std::string decrement_word;
    std::string increment_word;
    std::string stable_word;
  };
  const std::vector<SelectorWordCase> word_cases{
      {"-0", 89, 91, 90, 3, 11, 10,
       "-99999989", "-99999991", "-99999990"},
      {"-0.5", 89, 91, 90, 3, 11, 10,
       "-99999989", "-99999991", "-99999990"},
      {"-0.9", 89, 91, 90, 3, 11, 10,
       "-99999989", "-99999991", "-99999990"},
      {"-3.25", 92, 94, 93, 12, 14, 13,
       "-99999992", "-99999994", "-99999993"},
      {"5E-1", 99, 1, 0, 3, 1, 0,
       "4.9999999E-1", "5.0000001E-1", "5E-1"},
      {"-5E-1", 99, 1, 0, 3, 1, 0,
       "-4.9999999E-1", "-5.0000001E-1", "-5E-1"},
      {"4.1200076E-1", 75, 77, 76, 0, 1, 0,
       "4.1200075E-1", "4.1200077E-1", "4.1200076E-1"},
      {"-4.1200076E-1", 75, 77, 76, 0, 1, 0,
       "-4.1200075E-1", "-4.1200077E-1", "-4.1200076E-1"},
      {"-2.2600029E-1", 28, 30, 29, 2, 10, 3,
       "-2.2600028E-1", "-2.2600030E-1", "-2.2600029E-1"},
      {"1E3", 99, 1, 0, 3, 1, 0, "00000999", "00001001", "00001000"},
      {"1.0E3", 99, 1, 0, 3, 1, 0, "00000999", "00001001", "00001000"},
      {"1E0", 0, 2, 1, 0, 2, 1, "00000000", "00000002", "00000001"},
  };
  for (const auto& word : word_cases) {
    for (const int reg : {0, 3, 4, 6, 7, 14}) {
      const std::string selector = reg == 14 ? "e" : std::to_string(reg);
      const int target = reg <= 3 ? word.decrement_target
                          : reg <= 6 ? word.increment_target : word.stable_target;
      const int memory = reg <= 3 ? word.decrement_memory
                          : reg <= 6 ? word.increment_memory : word.stable_memory;
      const std::string& changed_word =
          reg <= 3 ? word.decrement_word
          : reg <= 6 ? word.increment_word : word.stable_word;
      const auto decoded = core::evaluate_indirect_address(
          selector, word.value, core::IndirectOperationKind::Flow);
      require(decoded.has_value() && decoded->actual_flow_target == target,
              "word spelling must select its ROM flow target: R" + selector + "=" + word.value);
      const auto decoded_memory = core::evaluate_indirect_address(
          selector, word.value, core::IndirectOperationKind::Memory);
      require(decoded_memory.has_value() && decoded_memory->memory_target == memory &&
                  decoded_memory->result_value == decoded->result_value,
              "flow and memory addressing must share the same word mutation");

      emulator::MK61 expected;
      expected.set_register(selector, changed_word);
      const std::string expected_word = expected.read_register(selector);
      emulator::MK61 jump;
      require(jump.load_program({0x80 + reg, 0x50}).diagnostics.empty(),
              "selector-flow ROM fixture must load");
      jump.set_register(selector, word.value);
      jump.press_sequence({"В/О", "ПП"});
      const std::string expected_pc =
          (target < 10 ? "0" : "") + std::to_string(target);
      require(jump.program_counter() == expected_pc &&
                  jump.read_register(selector) == expected_word,
              "single-step flow must confirm both target and write-back: R" +
                  selector + "=" + word.value);

      emulator::MK61 recall;
      require(recall.load_program({0xd0 + reg, 0x50}).diagnostics.empty(),
              "selector-memory ROM fixture must load");
      for (int bank = 0; bank < 15; ++bank) {
        const std::string name = bank < 10 ? std::to_string(bank)
                                          : std::string(1, static_cast<char>('a' + bank - 10));
        recall.set_register(name, std::to_string(200 + bank));
      }
      recall.set_register(selector, word.value);
      const std::string memory_name = memory < 10 ? std::to_string(memory)
                                                  : std::string(1, static_cast<char>('a' + memory - 10));
      // A recall normalizes the copy in X without rewriting the source word:
      // for example, R3 can retain 00000999 while X contains 999.
      // Use an independent direct recall, not string equality with memory.
      require(expected.load_program({0x60 + memory, 0x50}).diagnostics.empty(),
              "direct-recall ROM reference must load");
      if (memory != reg)
        expected.set_register(memory_name, std::to_string(200 + memory));
      expected.press_sequence({"В/О", "С/П"});
      require(expected.run_until_stable(1000, 6).stopped,
              "direct-recall ROM reference must stop");
      const std::string expected_x = expected.read_register("X");
      require(expected.read_register(selector) == expected_word,
              "direct recall must preserve the independent selector word");

      recall.press_sequence({"В/О", "С/П"});
      require(recall.run_until_stable(1000, 6).stopped,
              "indirect recall must stop: R" + selector + "=" + word.value);
      require(recall.read_register("X") == expected_x,
              "indirect recall must match direct recall of its selected bank: R" +
                  selector + "=" + word.value);
      require(recall.read_register(selector) == expected_word,
              "indirect recall must preserve the exact selector write-back: R" +
                  selector + "=" + word.value);
    }
  }

  // Independent ROM words cover every order-units class and several decades.
  // Expected mantissas are pinned values, not the decoder's shift algorithm.
  struct ShiftedOrderWord {
    int order;
    int write_back_order;
    std::string positive;
    std::string negative;
  };
  const std::vector<ShiftedOrderWord> shifted_orders{
      {1, 1, "41200076", "41200076"},
      {2, 2, "41200076", "41200076"},
      {3, 3, "41200076", "41200076"},
      {4, 3, "04120007", "94120007"},
      {5, 3, "00412000", "99412000"},
      {6, 3, "00041200", "99941200"},
      {7, 3, "00004120", "99994120"},
      {8, 3, "00000412", "99999412"},
      {9, 3, "00000041", "99999941"},
      {10, 3, "00000004", "99999994"},
      {11, 11, "41200076", "41200076"},
      {14, 13, "04120007", "94120007"},
      {20, 13, "00000004", "99999994"},
      {93, 93, "41200076", "41200076"},
      {94, 93, "04120007", "94120007"},
      {99, 93, "00000041", "99999941"},
  };
  constexpr std::array<int, 10> nonzero_tens_memory{
      10, 11, 12, 13, 14, 0, 0, 1, 2, 3};
  for (const auto& word : shifted_orders) {
    for (const bool negative : {false, true}) {
      for (const int reg : {0, 3, 4, 6, 7, 14}) {
        const std::string selector = reg == 14 ? "e" : std::to_string(reg);
        const int delta = reg < 4 ? -1 : reg < 7 ? 1 : 0;
        std::string value = (negative ? "-" : "") +
                            std::string("4.1200076E-") + std::to_string(word.order);
        const std::string context = "R" + selector + "=" + value;
        emulator::MK61 jump_calc, memory_calc;
        require(jump_calc.load_program({0x80 + reg, 0x50}).diagnostics.empty() &&
                    memory_calc.load_program({0xd0 + reg, 0x50}).diagnostics.empty(),
                "repeated selector fixtures must load");
        jump_calc.set_register(selector, value);
        for (int bank = 0; bank < 15; ++bank) {
          const std::string name = bank < 10 ? std::to_string(bank)
              : std::string(1, static_cast<char>('a' + bank - 10));
          memory_calc.set_register(name, std::to_string(200 + bank));
        }
        memory_calc.set_register(selector, value);
        for (int repeat = 0; repeat < 3; ++repeat) {
          const int mantissa = std::stoi(negative ? word.negative : word.positive) +
                               delta * (repeat + 1);
          std::string digits = std::to_string(mantissa);
          digits.insert(0, 8U - digits.size(), '0');
          const std::string write_back = (negative ? "-" : "") +
              digits.substr(0, 1) + "." + digits.substr(1) + "E-" +
              std::to_string(word.write_back_order);
          const int target = mantissa % 100;
          const int memory_target = target < 10 ? target
              : nonzero_tens_memory.at(static_cast<std::size_t>(target % 10));
          const auto flow = core::evaluate_indirect_address(
              selector, value, core::IndirectOperationKind::Flow);
          const auto memory = core::evaluate_indirect_address(
              selector, value, core::IndirectOperationKind::Memory);
          require(flow.has_value() && flow->actual_flow_target == target &&
                      flow->result_value.has_value() && memory.has_value() &&
                      memory->memory_target == memory_target &&
                      memory->result_value == flow->result_value,
                  "repeated flow/memory projections must match ROM words: " + context);

          emulator::MK61 expected, replay_word;
          for (int bank = 0; bank < 15; ++bank) {
            const std::string name = bank < 10 ? std::to_string(bank)
                : std::string(1, static_cast<char>('a' + bank - 10));
            expected.set_register(name, std::to_string(200 + bank));
          }
          expected.set_register(selector, write_back);
          const std::string expected_word = expected.read_register(selector);
          replay_word.set_register(selector, *flow->result_value);
          require(replay_word.read_register(selector) == expected_word,
                  "model write-back must recreate the exact stored word: " + context);

          jump_calc.press_sequence({"В/О", "ПП"});
          const std::string expected_pc = (target < 10 ? "0" : "") + std::to_string(target);
          require(jump_calc.program_counter() == expected_pc &&
                      jump_calc.read_register(selector) == expected_word,
                  "repeated indirect flow must preserve target and exact word: " + context);
          require(expected.load_program({0x60 + memory_target, 0x50}).diagnostics.empty(),
                  "independent direct memory oracle must load");
          expected.press_sequence({"В/О", "С/П"});
          memory_calc.press_sequence({"В/О", "С/П"});
          require(expected.run_until_stable(1000, 6).stopped &&
                      memory_calc.run_until_stable(1000, 6).stopped,
                  "both memory probes must stop: " + context);
          require(memory_calc.read_register("X") == expected.read_register("X") &&
                      memory_calc.read_register(selector) == expected_word,
                  "repeated indirect memory must match direct recall and write-back: " + context);
          value = *flow->result_value;
        }
      }
    }
  }
  for (const std::string value :
       {"14", "1E3", "4.1200076E-1", "-5E-1", "-99999999", "B2"}) {
    const auto decoded = core::evaluate_indirect_address(
        "a", value, core::IndirectOperationKind::Flow);
    require(decoded.has_value() &&
                core::indirect_writeback_preserves_literal_value(*decoded, value),
            "proved immutable literals must remain available as selectors: " + value);
  }
  for (const std::string value :
       {"14.375", "0.25", "-7", "-2E-7", "2E-7", "-4.1200076E-14"}) {
    const auto decoded = core::evaluate_indirect_address(
        "a", value, core::IndirectOperationKind::Flow);
    require(decoded.has_value() &&
                !core::indirect_writeback_preserves_literal_value(*decoded, value),
            "stable selector classes must not certify destructive data write-back: " + value);
  }

  for (const double value : {-0.0, -0.5}) {
    const auto decoded = core::evaluate_indirect_address(
        "7", value, core::IndirectOperationKind::Flow);
    require(decoded.has_value() && decoded->actual_flow_target == 90 &&
                decoded->result_value == "-99999990",
            "the double overload must not erase the sign of fractional zero");
  }
  for (const std::string value : {"1E-100", "1E100", "1E+3", "1E-0", "1E-00",
                                 "1.23456789E-1", "12E-1", "0.5E-1"}) {
    require(!core::evaluate_indirect_address("7", value, core::IndirectOperationKind::Flow),
            "unsupported scientific word formats must not fall back to a raw hex address");
  }
  require(!core::evaluate_indirect_address("0", "1E-1", core::IndirectOperationKind::Flow) &&
              !core::evaluate_indirect_address("4", "9.9999999E-1",
                                              core::IndirectOperationKind::Memory),
          "unproved mantissa boundary carry/borrow must fail closed");
  const auto raw_ambiguous = core::evaluate_indirect_address(
      "7", "0x1E3", core::IndirectOperationKind::Flow);
  require(raw_ambiguous.has_value() && raw_ambiguous->formal_address.has_value() &&
              raw_ambiguous->formal_address->opcode == 0xe3 &&
              raw_ambiguous->actual_flow_target == 31,
          "an explicit raw BCD word must remain distinct from scientific 1E3");

  struct RawSelectorCase {
    std::string value;
    std::string rom_word;
    std::string stored_word;
    std::string pc;
    int formal;
    int physical;
    int memory;
  };
  const std::array<RawSelectorCase, 5> raw_cases{{
      {"0x1E3", "1\u04153", "000001\u04153", "\u04153", 0xe3, 31, 13},
      {"0X2e3", "2\u04153", "000002\u04153", "\u04153", 0xe3, 31, 13},
      {"0x1E0", "1\u04150", "000001\u04150", "\u04150", 0xe0, 28, 10},
      {"0xB2", "L2", "000000L2", "L2", 0xb2, 0, 12},
      {"B2", "L2", "000000L2", "L2", 0xb2, 0, 12},
  }};
  for (const auto& word : raw_cases) {
    for (const int reg : {7, 10, 14}) {
      const std::string selector = reg == 10 ? "a" : reg == 14 ? "e" : "7";
      std::vector<int> codes(105, 0x54);
      codes[0] = 0x61;
      codes[2] = 0x80 + reg;
      codes.at(static_cast<std::size_t>(word.physical)) = 0x61;
      emulator::MK61 jump;
      require(jump.load_program(codes).diagnostics.empty(), "raw selector fixture must load");
      jump.set_register("1", "314");
      jump.set_register(selector, word.rom_word);
      emulator::MK61 expected;
      expected.set_register(selector, word.stored_word);
      expected.set_register("X", "314");
      std::string value = word.value;
      for (int repeat = 0; repeat < 3; ++repeat) {
        const auto flow = core::evaluate_indirect_address(
            selector, value, core::IndirectOperationKind::Flow);
        const auto memory = core::evaluate_indirect_address(
            selector, value, core::IndirectOperationKind::Memory);
        require(flow && flow->formal_address && flow->result_value &&
                    flow->formal_address->opcode == word.formal &&
                    flow->actual_flow_target == word.physical &&
                    memory && memory->memory_target == word.memory &&
                    memory->result_value == flow->result_value,
                "raw BCD type and both addresses must survive repeated write-back: " + value);
        require(flow->result_value->starts_with("0x") &&
                    core::indirect_writeback_preserves_literal_value(*flow, word.value),
                "raw write-back must remain explicitly typed and preserve its literal");
        jump.press_sequence({"\u0412/\u041e", "\u041f\u041f", "\u041f\u041f", "\u041f\u041f"});
        require(jump.program_counter() == word.pc &&
                    jump.read_register(selector) == expected.read_register(selector),
                "ROM must retain the raw counter and exact padded BCD word");
        jump.set_register("X", "17");
        jump.press_sequence({"\u041f\u041f"});
        require(jump.read_register("X") == expected.read_register("X"),
                "the raw ROM counter must actually fetch from its physical alias");
        value = *flow->result_value;
      }
    }
    for (const std::string reg : {"0", "3", "4", "6"}) {
      require(!core::evaluate_indirect_address(reg, word.value,
                                              core::IndirectOperationKind::Flow) &&
                  !core::evaluate_indirect_address(reg, word.value,
                                                   core::IndirectOperationKind::Memory),
              "unproved arithmetic on nondecimal BCD counters must remain rejected");
    }
  }
  const auto ordinary_scientific = core::evaluate_indirect_address(
      "7", "1E3", core::IndirectOperationKind::Flow);
  require(ordinary_scientific && ordinary_scientific->actual_flow_target == 0 &&
              !core::indirect_writeback_preserves_literal_value(*raw_ambiguous, "1E3") &&
              !core::indirect_writeback_preserves_literal_value(*ordinary_scientific, "0x1E3"),
          "literal preservation must not erase the distinction between scientific and raw BCD");

  const auto fractional_r0 =
      core::evaluate_indirect_address("0", "0.5", core::IndirectOperationKind::Memory);
  require(fractional_r0.has_value(), "R0 fractional memory selector should evaluate");
  require(fractional_r0->transformed == "99", "R0 fractional selector should transform to 99");
  require(fractional_r0->memory_target == 3, "R0 fractional memory selector should target R3");
  require(fractional_r0->result_value == "-99999999",
          "R0 fractional selector should leave the sentinel result value");

  const auto official_flow =
      core::evaluate_indirect_address("7", "99", core::IndirectOperationKind::Flow);
  require(official_flow.has_value(), "official indirect flow target should evaluate");
  require(official_flow->flow_target == 99, "transformed 99 should flow to address 99");
  require(official_flow->formal_address.has_value() &&
              official_flow->formal_address->opcode == 0x99,
          "official flow target 99 should use formal opcode 99");

  const auto super_dark_flow =
      core::evaluate_indirect_address("7", "FA", core::IndirectOperationKind::Flow);
  require(super_dark_flow.has_value(), "hex super-dark indirect flow target should evaluate");
  require(super_dark_flow->flow_target == 0xfa,
          "hex transformed FA should keep the hex flow target");
  require(super_dark_flow->formal_address.has_value() &&
              super_dark_flow->formal_address->kind == FormalAddressKind::SuperDark,
          "formal FA should be reported as super-dark");
  require(super_dark_flow->super_dark.has_value() &&
              super_dark_flow->super_dark->entry_address == 48 &&
              super_dark_flow->super_dark->continuation_address == 1,
          "FA should expose the super-dark entry and continuation");

  const auto stock_a5 =
      core::evaluate_indirect_address("7", "A5", core::IndirectOperationKind::Flow);
  require(stock_a5.has_value() && stock_a5->actual_flow_target == 0,
          "stock A5 indirect flow should remain a dark alias to 00");

  const auto expanded_a5 = core::evaluate_indirect_address(
      "7", "A5", core::IndirectOperationKind::Flow, AddressSpaceModel::Mk61SMiniExpanded);
  require(expanded_a5.has_value() && expanded_a5->actual_flow_target == 105,
          "expanded A5 indirect flow should target physical cell 105");

  const auto expanded_b2 = core::evaluate_indirect_address(
      "7", "B2", core::IndirectOperationKind::Flow, AddressSpaceModel::Mk61SMiniExpanded);
  require(expanded_b2.has_value() && expanded_b2->actual_flow_target == 0,
          "expanded B2 indirect flow should be the first dark alias to 00");

  // The numeric selector evaluator alone cannot certify rematerialization:
  // delivered zero and a fractional seed have different ROM behavior.
  const auto fixture = [](bool inverted) {
    std::vector<MachineItem> items{
        MachineItem::op(0x61, "recall"),
        MachineItem::op(0x62, "recall"),
        MachineItem::op(0x11, "subtract"),
        MachineItem::op(inverted ? 0x57 : 0x5e, "test"),
        MachineItem::address(inverted ? "zero" : "done")};
    if (inverted) {
      items.push_back(MachineItem::op(0x51, "jump"));
      items.push_back(MachineItem::address("done"));
    }
    items.push_back(MachineItem::label("zero"));
    items.push_back(MachineItem::op(0x68, "constant"));
    items.push_back(MachineItem::op(0x43, "store"));
    items.push_back(MachineItem::label("done"));
    auto stop = MachineItem::op(0x50, "stop");
    stop.stop_disposition = StopDisposition::Terminal;
    items.push_back(stop);
    return raise_machine_to_ir(items);
  };
  for (const bool inverted : {false, true}) {
    const auto input = fixture(inverted);
    const auto optimized = core::passes::rematerialize_zero_underflow_constant(
        input, {{"8", "-99999999"}});
    require(optimized.applied == 1 && optimized.ops.size() == input.size(),
            "both zero-branch directions must permit a same-width rematerialization");
    require(std::any_of(optimized.ops.begin(), optimized.ops.end(), [](const auto& op) {
      return op.kind == IrKind::IndirectRecall && op.opcode == 0xd3 &&
          op.meta.indirect_memory_targets == std::optional<std::vector<int>>{{3}};
    }), "self-indexed recall must carry its exact memory target");
    auto unknown = input;
    unknown[2].opcode = 0x54;
    require(core::passes::rematerialize_zero_underflow_constant(
                unknown, {{"8", "-99999999"}}).applied == 0,
            "zero comparison alone must not certify an unknown number representation");
    auto raw = input;
    raw[0].meta.raw = true;
    require(core::passes::rematerialize_zero_underflow_constant(
                raw, {{"8", "-99999999"}}).applied == 0,
            "raw entry must fail closed");
    auto wrong_register = input;
    for (auto& op : wrong_register)
      if (op.kind == IrKind::Store) op = make_store("2");
    // R3 is live at the branch entry in this variant, so another destination
    // cannot borrow it. Keep a read before any overwrite after the pair.
    wrong_register.insert(wrong_register.end() - 1, make_recall("3"));
    require(core::passes::rematerialize_zero_underflow_constant(
                wrong_register, {{"8", "-99999999"}}).applied == 0,
            "a different destination must not clobber an observed R3 value");
    require(core::passes::rematerialize_zero_underflow_constant(
                input, {{"8", "-99999998"}}).applied == 0,
            "a different constant must not reuse the zero-underflow fact");
  }


  const auto helper_fixture = [](int destination, bool observe_scratch, bool mixed_calls) {
    std::vector<MachineItem> items{MachineItem::op(0x0d, "clear"),
        MachineItem::op(0x53, "call"), MachineItem::address("wrapper")};
    if (mixed_calls) {
      items.push_back(MachineItem::op(0x61, "unknown X"));
      items.push_back(MachineItem::op(0x53, "call"));
      items.push_back(MachineItem::address("wrapper"));
    }
    if (observe_scratch) {
      items.push_back(MachineItem::op(0x63, "observe scratch"));
    } else {
      items.push_back(MachineItem::op(0x05, "overwrite value"));
      items.push_back(MachineItem::op(0x43, "overwrite scratch"));
    }
    items.push_back(MachineItem::op(0x60 + destination, "result"));
    auto stop = MachineItem::op(0x50, "stop");
    stop.stop_disposition = StopDisposition::Terminal;
    items.push_back(stop);
    items.push_back(MachineItem::label("wrapper"));
    items.push_back(MachineItem::op(0x45, "transparent store"));
    items.push_back(MachineItem::op(0x53, "nested call"));
    items.push_back(MachineItem::address("constructor"));
    items.push_back(MachineItem::op(0x52, "return"));
    items.push_back(MachineItem::label("constructor"));
    items.push_back(MachineItem::op(0x68, "constant"));
    auto output = MachineItem::op(0x40 + destination, "destination");
    output.comment = "set display_state";
    items.push_back(output);
    items.push_back(MachineItem::op(0x52, "return"));
    return raise_machine_to_ir(items);
  };
  const auto observe_helper = [](const std::vector<IrOp>& ops) {
    const auto resolved = resolve_machine_items(lower_ir_to_machine(ops));
    require(resolved.diagnostics.empty(), "helper ROM fixture must resolve");
    std::vector<int> codes;
    for (const auto& step : resolved.steps) codes.push_back(step.opcode);
    emulator::MK61 calculator;
    require(calculator.load_program(codes).diagnostics.empty(), "helper ROM fixture must load");
    calculator.set_register("8", "-99999999");
    calculator.set_register("3", "314");
    calculator.set_register("X", "7");
    calculator.set_register("Y", "13");
    calculator.set_register("Z", "23");
    calculator.set_register("T", "37");
    calculator.press_sequence({"В/О", "С/П"});
    require(calculator.run_until_stable(2000, 6).stopped, "nested helper calls must return");
    std::vector<std::string> out{calculator.display_text(), calculator.program_counter()};
    for (const std::string reg : {"X", "Y", "Z", "T", "X1", "3", "5", "8", "9"})
      out.push_back(calculator.read_register(reg));
    calculator.press_sequence({"ВП"});
    out.push_back(calculator.display_text());
    for (const std::string reg : {"X", "Y", "Z", "T", "X1"})
      out.push_back(calculator.read_register(reg));
    return out;
  };
  for (const auto model : {AddressSpaceModel::Standard, AddressSpaceModel::Mk61SMiniExpanded}) {
    for (const int destination : {3, 9}) {
      const auto input = helper_fixture(destination, false, false);
      const auto optimized = core::passes::rematerialize_zero_underflow_constant(
          input, {{"8", "-99999999"}}, model);
      require(optimized.applied == 1 &&
                  optimized.ops.size() == input.size() + (destination == 3 ? 0 : 1),
              "exact zero must survive nested calls and stores in either address profile");
      require(observe_helper(input) == observe_helper(optimized.ops),
              "scratch rematerialization must preserve nested returns, stack, X1 and hidden X2");
    }
    for (const int destination : {3, 9}) {
      for (const int barrier : {0, 1, 2}) {
        auto anchored = helper_fixture(destination, false, false);
        const auto recall = std::find_if(anchored.begin(), anchored.end(), [](const IrOp& op) {
          return op.kind == IrKind::Recall && op.register_name == "8";
        });
        require(recall != anchored.end() && recall + 1 != anchored.end(),
                "typed-barrier fixture must contain the constant/store pair");
        auto& store = *(recall + 1);
        if (barrier == 0) store.meta.roles.push_back("display-byte");
        else if (barrier == 1) store.meta.raw = true;
        else store.meta.manual_interaction.emplace();
        require(core::passes::rematerialize_zero_underflow_constant(
                    anchored, {{"8", "-99999999"}}, model).applied == 0,
                "typed display, raw and manual anchors must still prohibit rematerialization");
      }
    }
    require(core::passes::rematerialize_zero_underflow_constant(
                helper_fixture(9, true, false), {{"8", "-99999999"}}, model).applied == 0,
            "a scratch read after returning to the caller must reject rematerialization");
    require(core::passes::rematerialize_zero_underflow_constant(
                helper_fixture(9, false, true), {{"8", "-99999999"}}, model).applied == 0,
            "one unknown caller must poison a shared constructor even when another passes zero");
    require(core::passes::rematerialize_zero_underflow_constant(
                helper_fixture(9, false, false), {{"8", "-99999999"}, {"3", "0"}}, model)
                .applied == 0,
            "a compiler-owned constant-pool register must never be borrowed as scratch");
  }

  const auto rom = [](const std::vector<int>& prefix, const std::string& seed,
                      bool replacement, const std::vector<std::string>& suffix) {
    std::vector<int> codes = prefix;
    const std::vector<int> tail = replacement ? std::vector<int>{0x43, 0xd3, 0x50}
                                              : std::vector<int>{0x6c, 0x43, 0x50};
    codes.insert(codes.end(), tail.begin(), tail.end());
    emulator::MK61 calculator;
    require(calculator.load_program(codes).diagnostics.empty(), "ROM fact must load");
    calculator.set_register("b", seed);
    calculator.set_register("c", "-99999999");
    calculator.set_register("3", "314");
    calculator.set_register("X", "7");
    calculator.set_register("Y", "13");
    calculator.set_register("Z", "23");
    calculator.set_register("T", "37");
    calculator.press_sequence({"В/О", "С/П"});
    require(calculator.run_until_stable(2000, 6).stopped, "ROM fact must stop");
    std::vector<std::string> observation{calculator.display_text(), calculator.program_counter()};
    for (const std::string reg : {"X", "Y", "Z", "T", "X1", "3"})
      observation.push_back(calculator.read_register(reg));
    if (!suffix.empty())
      calculator.press_sequence(suffix);
    observation.push_back(calculator.display_text());
    for (const std::string reg : {"X", "Y", "Z", "T", "X1", "3"})
      observation.push_back(calculator.read_register(reg));
    return observation;
  };
  for (const std::vector<std::string>& suffix :
       {std::vector<std::string>{}, {"ВП"}, {"."}, {"F", "В↑"}, {"1"}}) {
    require(rom({0x0d}, "19", false, suffix) == rom({0x0d}, "19", true, suffix),
            "cleared zero must preserve stack, X1, hidden X2 and keyboard entry");
    for (const std::string seed : {"19", "44444.4", "-0.226", "1E-50", "0.41200076"})
      require(rom({0x6b, 0x0e, 0x11}, seed, false, suffix) ==
                  rom({0x6b, 0x0e, 0x11}, seed, true, suffix),
              "arithmetic zero must preserve the complete observed ROM continuation");
    require(rom({0x6b}, "0.5", false, suffix) != rom({0x6b}, "0.5", true, suffix),
            "fractional input is a counterexample to numeric-only selector rematerialization");
  }

  {
    const std::string source = R"(program ZeroRepresentationRoots {
      state {
        value: packed
        output: packed = 0
      }
      loop {
        show(output)
        value = entered()
        if value == 17 { output = -99999999 }
        else { output = value }
      }
    })";
    CompileOptions ordinary_options;
    ordinary_options.disable_candidate_search = true;
    const auto ordinary = compile_source(source, ordinary_options);
    CompileOptions root_options;
    root_options.zero_underflow_optimizer_root = true;
    const auto alternate = compile_source(source, root_options);
    const auto selected = compile_source(source);
    require(ordinary.implemented && alternate.implemented && selected.implemented,
            "independent representation roots must compile");
    require(std::any_of(alternate.optimizations.begin(), alternate.optimizations.end(),
                        [](const OptimizationReport& optimization) {
                          return optimization.name == "zero-underflow-constant-rematerialization";
                        }),
            "the independent root fixture must actually exercise rematerialization");
    require(selected.steps.size() <= ordinary.steps.size() &&
                selected.steps.size() <= alternate.steps.size(),
            "a complete representation search must retain its smaller incumbent");
    const auto comparisons = [](const CompileResult& result) {
      return std::count_if(result.optimizations.begin(), result.optimizations.end(),
                          [](const OptimizationReport& optimization) {
                            return optimization.name == "zero-underflow-optimizer-roots";
                          });
    };
    require(comparisons(selected) == 1 && comparisons(alternate) == 0 &&
                comparisons(ordinary) == 0,
            "root expansion must be bounded and distinct from explicit lowering variants");
    const auto comparison = std::find_if(
        selected.optimizations.begin(), selected.optimizations.end(),
        [](const OptimizationReport& optimization) {
          return optimization.name == "zero-underflow-optimizer-roots";
        });
    require(comparison != selected.optimizations.end() &&
                comparison->detail.find("proof gate rejected") == std::string::npos &&
                comparison->detail.find("rematerialization did not apply") == std::string::npos,
            "complete proved roots must compete by final cost, not by the root's unset lowering flags");
    const auto observe = [](const CompileResult& result) {
      std::vector<int> codes;
      for (const auto& step : result.steps) codes.push_back(step.opcode);
      emulator::MK61 calculator;
      require(calculator.load_program(codes).diagnostics.empty(), "root fixture must load");
      for (const auto& preload : result.preloads)
        calculator.set_register(preload.register_name, preload.value);
      calculator.press_sequence({"В/О", "С/П"});
      require(calculator.run_until_stable(2000, 6).stopped, "root fixture must show its prompt");
      std::vector<std::string> outputs{calculator.read_register("X")};
      for (const std::vector<std::string>& keys :
           std::vector<std::vector<std::string>>{{"1", "7", "С/П"}, {"2", "3", "С/П"},
                                                {"1", "7", "С/П"}}) {
        calculator.press_sequence(keys);
        require(calculator.run_until_stable(2000, 6).stopped,
                "either representation must resume the same input protocol");
        outputs.push_back(calculator.read_register("X"));
      }
      return outputs;
    };
    const auto observed = observe(selected);
    require(observed == observe(ordinary) && observed == observe(alternate),
            "complete root selection must preserve the ROM input/output transcript");
    emulator::MK61 expected;
    expected.set_register("X", "-99999999");
    require(observed.at(1) == expected.read_register("X") &&
                observed.at(3) == expected.read_register("X"),
            "both equality visits must report the requested sentinel");
    expected.set_register("X", "23");
    require(observed.at(2) == expected.read_register("X"),
            "the nonzero branch must still report the entered value");
  }


}

}  // namespace mkpro::tests
