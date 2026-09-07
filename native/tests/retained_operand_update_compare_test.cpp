#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace mkpro::tests {
namespace {

void retained_require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

std::string retained_compact(std::string text) {
  text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  }), text.end());
  return text;
}

void retained_run(emulator::MK61& calc) {
  retained_require(calc.run_until_stable(12000, 8).stopped, "update/compare program must stop");
}

std::vector<int> retained_reference(int opcode, bool equal, bool reverse, bool live_snapshot,
                                    bool reversed_update, bool rewrite_branch_value = false) {
  std::vector<int> code = {0x60, 0x43, 0x61};
  if (reversed_update) code.push_back(0x60);
  code.push_back(opcode);
  code.push_back(0x42);
  code.push_back(reverse ? 0x62 : 0x63);
  code.push_back(reverse ? 0x63 : 0x62);
  code.push_back(0x11);
  code.push_back(equal ? 0x5e : 0x57);
  const std::size_t otherwise = code.size(); code.push_back(0);
  if (rewrite_branch_value) code.insert(code.end(), {0x61, 0x43});
  code.insert(code.end(), {0x62, 0x01, 0x50, 0x51});
  const std::size_t end = code.size(); code.push_back(0);
  const auto address = [](std::size_t value) {
    return static_cast<int>((value / 10U) * 16U + value % 10U);
  };
  code.at(otherwise) = address(code.size());
  if (rewrite_branch_value) code.insert(code.end(), {0x62, 0x43});
  code.insert(code.end(), {0x62, 0x02, 0x50});
  code.at(end) = address(code.size());
  if (live_snapshot) code.push_back(0x63);
  code.insert(code.end(), {0x62, 0x50});
  return code;
}

} // namespace

void compiler_retained_operand_update_compare_preserves_observations() {
  struct Case {
    std::string operation;
    int opcode;
    bool producer;
    bool equal;
    bool reverse;
    bool negated;
    bool live_snapshot;
    bool reversed_update;
    bool expected_fusion;
    bool rewrite_snapshot = false;
    bool rewrite_operand = false;
    bool invert_layout = false;
  };
  const std::array<Case, 13> cases = {{
      {"bit_or", 0x38, false, true, false, false, false, false, true},
      {"bit_or", 0x38, true, true, false, false, false, false, true},
      {"bit_and", 0x37, true, false, false, false, false, false, true},
      {"bit_xor", 0x39, false, true, true, false, false, false, true},
      {"bit_or", 0x38, true, true, false, true, false, false, true},
      {"bit_or", 0x38, false, false, true, true, false, false, true},
      {"bit_or", 0x38, true, true, false, false, true, false, true},
      {"add", 0x10, true, true, false, false, false, false, false},
      {"bit_or", 0x38, true, true, false, false, false, true, false},
      {"bit_or", 0x38, true, true, false, false, true, false, true, true},
      {"bit_or", 0x38, true, true, false, false, false, false, true, false, true},
      {"bit_or", 0x38, true, true, false, false, false, false, true, false, false, true},
      {"bit_and", 0x37, false, false, true, true, false, false, true, false, false, true},
  }};
  const std::array<std::array<std::string, 2>, 7> inputs = {{
      {"0", "11"}, {"8.1", "15"}, {"8.5", "15"}, {"8.104", "11"},
      {"8.104", "0"}, {"0", "0"}, {"8.Е33", "8.104"},
  }};
  for (const auto& test : cases) {
    const std::string operand = test.producer ? "computed" : "mask";
    const std::string operation = test.operation == "add" ? "value + " + operand :
        test.operation + "(" + (test.reversed_update ? operand + ", value" : "value, " + operand) + ")";
    const std::string comparison = (test.reverse ? "value" : "snapshot") +
        std::string(test.equal ? " == " : " != ") + (test.reverse ? "snapshot" : "value");
    const std::string branch_field = test.rewrite_snapshot ? "snapshot" :
                                     test.rewrite_operand ? "computed" : "";
    const std::string branch_continuation = branch_field.empty() ? "" :
        " preview(" + branch_field + ")\n halt(value)\n";
    const std::string source =
        "program UpdateCompare {\n state {\n value: packed\n mask: packed\n"
        " snapshot: packed\n computed: packed\n }\n"
        " show(0)\n value = entered()\n mask = entered()\n" +
        std::string(test.producer ? " computed = mask\n" : "") +
        " snapshot = value\n value = " + operation + "\n " +
        (test.negated ? "unless " : "if ") + comparison +
        " {\n" + (branch_field.empty() ? "" : " " + branch_field + " = mask\n") +
        " preview(value)\n show(1)\n" + branch_continuation + " }\n else {\n" +
        (branch_field.empty() ? "" : " " + branch_field + " = value\n") +
        " preview(value)\n show(2)\n" + branch_continuation + " }\n" +
        (test.live_snapshot && branch_field.empty() ? " preview(snapshot)\n" : "") +
        " halt(value)\n}\n";
    CompileOptions options;
    options.analysis = true; options.budget = 105; options.disable_candidate_search = true;
    options.invert_branch_order = test.invert_layout;
    const auto result = compile_source(source, options);
    std::string diagnostics;
    for (const auto& diagnostic : result.diagnostics) diagnostics += diagnostic.message + "; ";
    retained_require(result.implemented && result.diagnostics.empty(),
                     "update/compare must compile: " + operation + " / " + comparison + " / " + diagnostics);
    const bool fused = std::any_of(result.optimizations.begin(), result.optimizations.end(),
                                  [](const auto& report) {
      return report.name == "retained-operand-update-compare";
    });
    retained_require(fused == test.expected_fusion,
                     "update/compare proof selection differs for " + operation + " / " + comparison);
    retained_require(result.steps.size() <= 105U, "update/compare fixture must fit MK-61 memory");
    std::vector<int> opcodes;
    for (const auto& step : result.steps) opcodes.push_back(step.opcode);
    for (const auto& input : inputs) {
      // Addition is a negative lowering case; keep its numerical inputs ordinary.
      if (test.operation == "add" && input.at(0).find("Е") != std::string::npos) continue;
      emulator::MK61 translated({.extended = true, .angle_mode = "deg"});
      retained_require(translated.load_program(opcodes).diagnostics.empty(), "compiled closure must load");
      for (const auto& preload : result.preloads)
        translated.set_register(preload.register_name, preload.value);
      translated.press_sequence({"В/О", "С/П"}); retained_run(translated);
      translated.set_register("X", input.at(0)).press("ПП"); retained_run(translated);
      translated.set_register("X", input.at(1)).press("С/П"); retained_run(translated);

      emulator::MK61 reference({.extended = true, .angle_mode = "deg"});
      reference.load_program(retained_reference(test.opcode, test.equal != test.negated,
                                                test.reverse, test.live_snapshot || test.rewrite_operand,
                                                test.reversed_update, !branch_field.empty()));
      reference.set_register("0", input.at(0)).set_register("1", input.at(1));
      reference.press_sequence({"В/О", "С/П"}); retained_run(reference);
      const std::string context = operation + " / " + comparison + " / " + input.at(0) + "," + input.at(1);
      retained_require(retained_compact(translated.display_text()) == retained_compact(reference.display_text()),
                       "update comparison branch differs: " + context);
      retained_require(retained_compact(translated.read_register("Y")) ==
                           retained_compact(reference.read_register("Y")),
                       "update result shown in Y differs: " + context);
      translated.press("С/П"); retained_run(translated);
      reference.press("С/П"); retained_run(reference);
      retained_require(retained_compact(translated.display_text()) == retained_compact(reference.display_text()),
                       "committed update differs after resume: " + context);
      if (test.live_snapshot || test.rewrite_operand) {
        retained_require(retained_compact(translated.read_register("Y")) ==
                             retained_compact(reference.read_register("Y")),
                         "live snapshot or rewritten branch value was lost: " + context);
      }
    }
  }

  // Full native AND includes a format digit. A raw AND test must not be
  // replaced by an OR-change test, or the zero input takes the wrong branch.
  const auto raw = compile_source(
      "program RawAnd {\n grid: board(1..4,1..4)\n state {\n value: packed\n mask: packed\n"
      " x: packed=1\n y: packed=1\n }\n value=entered()\n mask=cell_mask(x,y)\n"
      " if bit_and(value,mask) != 0 {\n show(1)\n }\n else {\n"
      " value=bit_or(value,mask)\n show(0)\n }\n halt(value)\n}\n",
      [] { CompileOptions o; o.analysis=true; o.budget=105; o.disable_candidate_search=true; return o; }());
  retained_require(raw.implemented && raw.diagnostics.empty(), "raw AND regression must compile");
  std::vector<int> raw_code;
  for (const auto& step : raw.steps) raw_code.push_back(step.opcode);
  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  calc.load_program(raw_code);
  for (const auto& preload : raw.preloads) calc.set_register(preload.register_name, preload.value);
  calc.press("В/О").set_register("X", "0").press("С/П"); retained_run(calc);
  retained_require(retained_compact(calc.display_text()) == "1,", "raw AND must test its full native result");
  calc.press("С/П"); retained_run(calc);
  retained_require(retained_compact(calc.display_text()) == "0,", "raw AND occupied branch must not update state");

  // Raw membership conditions must retain their full value for both guarded
  // set and guarded clear. In particular, zero and normalized 8.x are not
  // interchangeable as raw calculator values.
  for (const bool fractional : {false, true}) for (const bool clear : {false, true}) {
    const std::string mask = fractional ? "frac(mask)" : "mask";
    const std::string update = clear ? "value=bit_and(value,bit_not(" + mask + "))\n" :
                                       "value=bit_or(value," + mask + ")\n";
    const auto guarded = compile_source(
        "program GuardedBits {\n state {\n value: packed\n mask: packed\n }\n"
        " show(0)\n value=entered()\n mask=entered()\n"
        " if bit_and(value," + mask + ") != 0 {\n" + (clear ? update : "") +
        " preview(value)\n show(1)\n }\n else {\n" + (clear ? "" : update) +
        " preview(value)\n show(0)\n }\n halt(value)\n}\n",
        [] { CompileOptions o; o.analysis=true; o.budget=105; o.disable_candidate_search=true; return o; }());
    retained_require(guarded.implemented && guarded.diagnostics.empty(), "guarded native bit operation must compile");
    std::vector<int> guarded_code;
    for (const auto& step : guarded.steps) guarded_code.push_back(step.opcode);
    std::vector<int> expected = {0x60};
    const auto load_mask = [&] {
      expected.push_back(0x61);
      if (fractional) expected.push_back(0x35);
    };
    load_mask();
    expected.insert(expected.end(), {0x37,0x57,0});
    const std::size_t otherwise = expected.size() - 1U;
    if (clear) {
      expected.push_back(0x60); load_mask();
      expected.insert(expected.end(), {0x3a,0x37,0x40});
    }
    expected.insert(expected.end(), {0x60,0x01,0x50,0x51,0});
    const std::size_t end = expected.size() - 1U;
    const auto address = [](std::size_t n) { return static_cast<int>(n / 10U * 16U + n % 10U); };
    expected.at(otherwise) = address(expected.size());
    if (!clear) {
      expected.push_back(0x60); load_mask();
      expected.insert(expected.end(), {0x38,0x40});
    }
    expected.insert(expected.end(), {0x60,0x00,0x50});
    expected.at(end) = address(expected.size());
    expected.insert(expected.end(), {0x60,0x50});
    const std::array<std::array<std::string,2>,10> guarded_inputs = {{
        {"0","11"},{"8.1","11"},{"8.5","15"},{"0","0.1"},{"8.1","0.1"},{"8.5","0.1"},
        {"3.1415926","1.0000008"},{"-3.1415926","-0.1"},{"8.Е33","0.1"},{"9999999","11"},
    }};
    for (const auto& input : guarded_inputs) {
      emulator::MK61 actual({.extended=true,.angle_mode="deg"});
      actual.load_program(guarded_code);
      for (const auto& preload : guarded.preloads) actual.set_register(preload.register_name,preload.value);
      actual.press_sequence({"В/О","С/П"}); retained_run(actual);
      actual.set_register("X",input.at(0)).press("ПП"); retained_run(actual);
      actual.set_register("X",input.at(1)).press("С/П"); retained_run(actual);
      emulator::MK61 expected_calc({.extended=true,.angle_mode="deg"});
      expected_calc.load_program(expected);
      expected_calc.set_register("0",input.at(0)).set_register("1",input.at(1));
      expected_calc.press_sequence({"В/О","С/П"}); retained_run(expected_calc);
      const std::string context = std::string(clear ? "clear " : "set ") + mask + " / " + input.at(0) + "," + input.at(1);
      retained_require(retained_compact(actual.display_text()) == retained_compact(expected_calc.display_text()),
                       "raw predicate branch differs: " + context);
      retained_require(retained_compact(actual.read_register("Y")) == retained_compact(expected_calc.read_register("Y")),
                       "guarded update differs at its first stop: " + context);
      actual.press("С/П"); retained_run(actual);
      expected_calc.press("С/П"); retained_run(expected_calc);
      retained_require(retained_compact(actual.display_text()) == retained_compact(expected_calc.display_text()),
                       "guarded update differs after resume: " + context);
    }
  }

  // Exhaust every nibble pair in every mantissa position, including the
  // calculator's nondecimal digits, for full and fractional packed masks.
  // Construct fractions with the calculator itself: a direct raw encoding of
  // a leading blank digit is not the normalized result of K{x}, and can even
  // redirect the machine's program counter. Do not use it as an ordinary input.
  const std::array<std::string,16> digits = {
      "0","1","2","3","4","5","6","7","8","9","-","L","С","Г","Е","_"};
  const std::vector<int> ordinary_clear = {
      0x60,0x61,0x37,0x57,0x10,0x60,0x61,0x3a,0x37,0x40,0x60,0x50};
  const std::vector<int> retained_clear = {
      0x60,0x61,0x37,0x57,0x10,0x54,0x0a,0x3a,0x37,0x40,0x60,0x50};
  for (const auto& left : digits) for (const auto& right : digits) {
    for (const bool fractional : {false,true}) {
      std::string value="8.",mask="8.";
      for (int position=0;position<7;++position) { value+=left; mask+=right; }
      emulator::MK61 ordinary({.extended=true,.angle_mode="deg"});
      emulator::MK61 retained({.extended=true,.angle_mode="deg"});
      ordinary.set_register("0",value).set_register("1",mask);
      retained.set_register("0",value).set_register("1",mask);
      if (fractional) {
        for (auto* machine : {&ordinary,&retained}) {
          machine->load_program({0x61,0x35,0x41,0x50});
          machine->press_sequence({"В/О","С/П"});
          retained_run(*machine);
        }
      }
      ordinary.load_program(ordinary_clear); retained.load_program(retained_clear);
      ordinary.press_sequence({"В/О","С/П"}); retained.press_sequence({"В/О","С/П"});
      retained_run(ordinary); retained_run(retained);
      retained_require(ordinary.read_register("0") == retained.read_register("0") &&
                           ordinary.display_text() == retained.display_text(),
                       "native X2-preserving conditional clear differs: " + value + "," + mask);
    }
  }
  // Boolean algebra alone does not prove an MK-61 packed-word rewrite.
  // Retain a counterexample to A & ~M -> A ^ (A & M), including its stops.
  emulator::MK61 ordinary({.extended=true,.angle_mode="deg"});
  emulator::MK61 xor_rewrite({.extended=true,.angle_mode="deg"});
  ordinary.load_program(ordinary_clear);
  xor_rewrite.load_program({0x60,0x61,0x37,0x57,0x07,0x39,0x40,0x60,0x50});
  ordinary.set_register("0","8").set_register("1","0._______");
  xor_rewrite.set_register("0","8").set_register("1","0._______");
  ordinary.press_sequence({"В/О","С/П"}); xor_rewrite.press_sequence({"В/О","С/П"});
  retained_run(ordinary); retained_run(xor_rewrite);
  retained_require(ordinary.display_text() != xor_rewrite.display_text() ||
                       ordinary.read_register("0") != xor_rewrite.read_register("0"),
                   "packed-format counterexample must prevent an unconditional AND/XOR clear rewrite");
}

} // namespace mkpro::tests
