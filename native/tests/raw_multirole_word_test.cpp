#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/multirole_word_synthesis.hpp"
#include "mkpro/core/raw_bcd_word.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace mkpro;
using tests::require;

std::string compact(std::string value) {
  value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
  return value;
}

std::vector<std::string> words() {
  std::vector<std::string> result;
  for (int order = 1; order <= 4; ++order)
    for (const char digit : std::string("ABCD"))
      for (const bool negative : {false, true})
        result.push_back((negative ? "-" : "") + std::string(1, digit) + "E-" + std::to_string(order));
  return result;
}

void complete_rom_product_domain() {
  for (const auto& word : words()) {
    const auto decoded = core::decode_raw_single_hex_word(word);
    require(decoded.has_value(), "enumerated word must decode");
    for (int power = -8; power <= 8; ++power) {
      const auto fact = core::evaluate_raw_right_power10_product(power, word);
      require(fact.has_value(), "power product must be certified inside its finite domain");
      emulator::MK61 raw;
      emulator::MK61 ordinary;
      raw.set_register("1", word);
      ordinary.set_register("1", (decoded->negative ? "-1E" : "1E") +
                                    std::to_string(decoded->exponent + 1));
      for (auto* calc : {&raw, &ordinary}) {
        calc->set_register("2", "1E" + std::to_string(power));
        calc->load_program({0x62, 0x61, 0x12, 0x50});
        calc->press_sequence({"В/О", "С/П"});
        require(calc->run_until_stable(500, 4).stopped, "product probe must stop");
      }
      emulator::MK61 expected;
      expected.set_register("0", (fact->negative ? "-1E" : "1E") + std::to_string(fact->exponent));
      require(compact(raw.read_register("x")) == compact(ordinary.read_register("x")) &&
                  compact(raw.read_register("x")) == compact(expected.read_register("0")),
              "certified raw product must match ROM: 1E" + std::to_string(power) + " * " + word);
      for (const std::string reg : {"y", "z", "t"})
        require(raw.read_register(reg) == ordinary.read_register(reg),
                "binary product must retain lower stack cells");
      require(raw.read_register("x1") == raw.read_register("1") &&
                  raw.read_register("x1") != ordinary.read_register("x1"),
              "numeric coefficient equivalence MUST NOT assert X1 equivalence");
    }
  }
  require(!core::evaluate_raw_right_power10_product(-9, "DE-2") &&
              !core::evaluate_raw_right_power10_product(9, "DE-2") &&
              !core::evaluate_raw_right_power10_product(0, "EE-2"),
          "unknown operand or raw-word families must fail closed");
  emulator::MK61 reversed;
  reversed.set_register("1", "DE-2");
  reversed.set_register("2", "1");
  reversed.load_program({0x61, 0x62, 0x12, 0x50});
  reversed.press_sequence({"В/О", "С/П"});
  require(reversed.run_until_stable(500, 4).stopped &&
              compact(reversed.read_register("x")) != "1,-1",
          "the raw right coefficient must not be made commutative");
}

void complete_rom_direct_selector_domain() {
  for (const auto& word : words()) {
    const auto decoded = core::decode_raw_single_hex_word(word);
    for (int reg = 7; reg <= 14; ++reg) {
      const std::string selector(1, "0123456789abcde"[reg]);
      for (const auto operation : {core::IndirectOperationKind::Flow, core::IndirectOperationKind::Memory}) {
        const auto fact = core::evaluate_indirect_address(selector, word, operation);
        if (decoded->exponent == -4) {
          require(!fact, "order -4 changes the delivered word and is outside the direct codec");
          continue;
        }
        require(fact && core::indirect_writeback_preserves_literal_value(*fact, word),
                "stable raw selector must preserve its delivered word");
        const auto repeated = core::evaluate_indirect_address(selector, *fact->result_value, operation);
        require(repeated && repeated->result_value == fact->result_value &&
                    repeated->actual_flow_target == fact->actual_flow_target &&
                    repeated->memory_target == fact->memory_target,
                "a repeated access must retain word type and target");
        emulator::MK61 calc;
        std::vector<int> program(105, 0x50);
        program[10] = (operation == core::IndirectOperationKind::Flow ? 0x80 : 0xd0) + reg;
        calc.load_program(program);
        calc.set_register(selector, word);
        calc.set_register("0", "7");
        const auto delivered = calc.read_register(selector);
        for (int access = 0; access < 2; ++access) {
          calc.press_sequence({"БП", "1", "0", "С/П"});
          require(calc.run_until_stable(500, 4).stopped, "direct selector probe must stop");
          require(calc.read_register(selector) == delivered,
                  "ROM must preserve a repeatedly used raw selector");
          if (operation == core::IndirectOperationKind::Flow)
            require(fact->actual_flow_target == 0 && calc.program_counter() == "01",
                    "raw selector must choose the certified physical command");
          else
            require(fact->memory_target == 0 && compact(calc.read_register("x")) == "7,",
                    "raw selector must choose the certified memory register");
        }
      }
    }
    for (const std::string counter : {"0", "3", "4", "6"})
      require(!core::evaluate_indirect_address(counter, word, core::IndirectOperationKind::Flow),
              "raw mutating selectors have no stable-word certificate");
  }
  // The rejected boundary genuinely changes the word, including sign fill.
  for (const std::string word : {"DE-4", "-DE-4"}) {
    emulator::MK61 calc;
    calc.set_register("a", word);
    const auto before = calc.read_register("a");
    std::vector<int> program(105, 0x50);
    program[10] = 0x8a;
    calc.load_program(program);
    calc.press_sequence({"БП", "1", "0", "С/П"});
    require(calc.run_until_stable(500, 4).stopped && calc.read_register("a") != before,
            "order -4 must not be silently admitted as immutable raw state");
  }
}

core::RawWordSynthesisRequest request_for(int coefficient_exponent,
                                        const std::vector<int>& destinations) {
  core::RawWordSynthesisRequest request;
  for (const int sign : {1, -1}) {
    core::RawWordMode mode;
    mode.sign = sign;
    for (int input = -8; input <= 8; ++input)
      mode.right_products.push_back({input, input + coefficient_exponent, sign});
    mode.control = {
        {core::RawWordControlView::StoredWord, {"return", "a", {0}}, sign},
        {core::RawWordControlView::DegreeTangent, {"callback", "e", destinations}, sign}};
    request.modes.push_back(std::move(mode));
  }
  request.transitions = {{0, 1, core::RawWordTransitionKind::Negate},
                         {1, 0, core::RawWordTransitionKind::Negate}};
  return request;
}

void synthesis_is_joint_and_independently_checked() {
  auto request = request_for(-1, {88});
  const auto result = core::synthesize_raw_multirole_words(request);
  require(result.encodings.size() == 1U && !result.exhausted &&
              result.encodings[0].words == std::vector<std::string>{"DE-2", "-DE-2"} &&
              core::certify_raw_multirole_words(result.encodings[0], request),
          "coefficient, two addresses and the closed transition must synthesize one shared word");
  const auto other = core::synthesize_raw_multirole_words(request_for(-2, {65}));
  require(other.encodings.size() == 1U &&
              other.encodings[0].words == std::vector<std::string>{"CE-3", "-CE-3"},
          "changing consumer constraints must synthesize a different word");
  const auto freedom = core::synthesize_raw_multirole_words(request_for(0, {9, 26, 55}));
  require(freedom.encodings.size() == 3U,
          "layout freedom must retain all independently certified word/target alternatives");
  const auto repeat = core::synthesize_raw_multirole_words(request_for(0, {9, 26, 55}));
  require(repeat.work == freedom.work && repeat.encodings.size() == freedom.encodings.size(),
          "search work and alternatives must be deterministic");
  for (std::size_t i = 0; i < freedom.encodings.size(); ++i)
    require(repeat.encodings[i].words == freedom.encodings[i].words &&
                repeat.encodings[i].target_bindings == freedom.encodings[i].target_bindings,
            "deterministic search must include target assignments");
  auto forged = result.encodings[0];
  forged.words[1] = "-CE-2";
  require(!core::certify_raw_multirole_words(forged, request), "forged state edge must fail");
  forged = result.encodings[0];
  forged.target_bindings["callback"] = 89;
  require(!core::certify_raw_multirole_words(forged, request), "stale layout certificate must fail");
  require(core::synthesize_raw_multirole_words(request_for(-2, {88})).encodings.empty(),
          "arithmetic and control requirements must be intersected, not accepted separately");
  request.modes[1].control[1].address.identity = "another-callback";
  require(core::synthesize_raw_multirole_words(request).encodings.empty(),
          "different logical destinations must not silently share one physical cell");
  request = request_for(-1, {88});
  request.angle_mode = core::mk61_trig::AngleMode::Rad;
  require(core::synthesize_raw_multirole_words(request).encodings.empty(),
          "unsupported angle mode must fail closed");
  request = request_for(-1, {88});
  request.transitions[0].kind = core::RawWordTransitionKind::Identity;
  require(core::synthesize_raw_multirole_words(request).encodings.empty(),
          "consumer-equivalent rows must also satisfy every state transition");
  request = request_for(-1, {88});
  request.modes[0].control[0].address.selector = "3";
  require(core::synthesize_raw_multirole_words(request).encodings.empty(),
          "a decrementing selector is not an immutable dual-use word");
  const auto bounded = core::synthesize_raw_multirole_words(request_for(0, {9, 26, 55}),
      {.maximum_work = 2, .maximum_candidates = 64});
  require(bounded.exhausted && bounded.work <= 2U,
          "search exhaustion must be explicit rather than an impossibility claim");
  const auto capped = core::synthesize_raw_multirole_words(request_for(0, {9, 26, 55}),
      {.maximum_work = 4096, .maximum_candidates = 1});
  require(capped.exhausted && capped.encodings.size() == 1U,
          "candidate bound must retain valid results and report incompleteness");
}

} // namespace

int main() {
  try {
    complete_rom_product_domain();
    complete_rom_direct_selector_domain();
    synthesis_is_joint_and_independently_checked();
    std::cout << "raw_multirole_word_test: ok\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "raw_multirole_word_test: " << error.what() << '\n';
    return 1;
  }
}
