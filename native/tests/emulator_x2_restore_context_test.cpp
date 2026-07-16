#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string compact(std::string text) {
  text.erase(std::remove_if(text.begin(), text.end(),
                            [](unsigned char ch) { return std::isspace(ch) != 0; }),
             text.end());
  return text;
}

std::string run_x(const std::vector<int>& codes) {
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), "X2 context fixture should load");
  calc.press_sequence({"В/О", "С/П"});
  (void)calc.run_until_stable(300, 5);
  return compact(calc.read_register("x"));
}

std::string run_x_with_registers(const std::vector<int>& codes,
                                 const std::map<std::string, std::string>& registers) {
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), "X2 context fixture should load");
  for (const auto& [name, value] : registers)
    calc.set_register(name, value);
  calc.press_sequence({"В/О", "С/П"});
  (void)calc.run_until_stable(300, 5);
  return compact(calc.read_register("x"));
}

std::string mk61_hex_literal(const std::string& text) {
  std::string result;
  for (char ch : text) {
    switch (ch) {
      case 'A':
      case 'a':
        result += "-";
        break;
      case 'B':
      case 'b':
        result += "L";
        break;
      case 'C':
      case 'c':
        result += "С";
        break;
      case 'D':
      case 'd':
        result += "Г";
        break;
      case 'E':
      case 'e':
        result += "Е";
        break;
      case 'F':
      case 'f':
        result += "_";
        break;
      default:
        result.push_back(ch);
        break;
    }
  }
  return result;
}

std::string run_x_with_hex_register(const std::vector<int>& codes, const std::string& value) {
  return run_x_with_registers(codes, {{"1", mk61_hex_literal(value)}});
}

std::string run_signature(const std::vector<int>& codes) {
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), "X2 signature fixture should load");
  calc.press_sequence({"В/О", "С/П"});
  return calc.run_until_stable(300, 5).signature;
}

std::vector<int> concat(std::vector<int> left, const std::vector<int>& right) {
  left.insert(left.end(), right.begin(), right.end());
  return left;
}

std::string code_text(const std::vector<int>& codes) {
  std::ostringstream out;
  out << "[";
  for (std::size_t index = 0; index < codes.size(); ++index) {
    if (index != 0)
      out << " ";
    out << std::hex << codes[index];
  }
  out << "]";
  return out.str();
}

void expect_x(const std::vector<int>& codes, const std::string& expected,
              const std::string& context) {
  const std::string actual = run_x(codes);
  require(actual == expected,
          context + " expected X=" + expected + ", got " + actual + " for " + code_text(codes));
}

void expect_x_with_hex_register(const std::vector<int>& codes, const std::string& value,
                                const std::string& expected, const std::string& context) {
  const std::string actual = run_x_with_hex_register(codes, value);
  require(actual == expected,
          context + " expected X=" + expected + ", got " + actual + " for " + value);
}

void expect_equal_runs(const std::vector<int>& left, const std::vector<int>& right,
                       const std::string& context) {
  const std::string actual = run_x(left);
  const std::string expected = run_x(right);
  require(actual == expected,
          context + " expected equal X values, got " + actual + " vs " + expected);
}

void expect_different_runs(const std::vector<int>& left, const std::vector<int>& right,
                           const std::string& context) {
  const std::string actual = run_x(left);
  const std::string unexpected = run_x(right);
  require(actual != unexpected,
          context + " expected different X values, both were " + actual);
}

}  // namespace

void emulator_x2_restore_context_matches_typescript_contract() {
  {
    const std::vector<int> with_store = {0x20, 0x35, 0x41, 0x0c, 0x50};
    const std::vector<int> without_store = {0x20, 0x35, 0x0c, 0x50};
    expect_x(with_store, "1,", "X->P before VP changes restored X2");
    expect_x(without_store, "4,", "VP without store keeps previous X2");
  }

  {
    const std::vector<std::pair<std::vector<int>, std::string>> cases = {
        {{0x01, 0x41, 0x0c, 0x03, 0x50}, "0,"},
        {{0x02, 0xf0, 0x41, 0x0c, 0x03, 0x50}, "0,"},
        {{0x01, 0x02, 0x41, 0x0c, 0x03, 0x50}, "2000,"},
        {{0x01, 0x00, 0x41, 0x0c, 0x03, 0x50}, "0,"},
        {{0x00, 0x00, 0x41, 0x0c, 0x03, 0x50}, "10000,"},
        {{0x00, 0x05, 0x41, 0x0c, 0x03, 0x50}, "5000,"},
        {{0x01, 0x0a, 0x41, 0x0c, 0x03, 0x50}, "0,"},
        {{0x01, 0x0a, 0x02, 0x41, 0x0c, 0x03, 0x50}, "200,"},
        {{0x02, 0x0b, 0x41, 0x0c, 0x03, 0x50}, "-9000,"},
        {{0x01, 0x0a, 0x02, 0x0b, 0x41, 0x0c, 0x03, 0x50}, "-9200,"},
        {{0x00, 0x0b, 0x41, 0x0c, 0x03, 0x50}, "-1000,"},
    };
    for (const auto& [program, expected] : cases)
      expect_x(program, expected, "X->P before VP splices decimal hidden X2 mantissa");
  }

  {
    const std::vector<int> base = {0x61, 0x41, 0x0c, 0x03, 0x50};
    const std::vector<int> sign_pair = {0x61, 0x41, 0x0c, 0x0b, 0x0b, 0x03, 0x50};
    for (const std::string& value : {"FACE", "FA", "A", "D"}) {
      require(run_x_with_hex_register(sign_pair, value) == run_x_with_hex_register(base, value),
              "store-splice sign-pair equivalence should hold for " + value);
    }
  }

  {
    const std::vector<int> immediate = {0x61, 0x41, 0x0c, 0x03, 0x50};
    const std::vector<std::vector<int>> delayed_programs = {
        {0x61, 0x41, 0x54, 0x0c, 0x03, 0x50},
        {0x61, 0x41, 0xf0, 0x0c, 0x03, 0x50},
        {0x61, 0x41, 0x0e, 0x0c, 0x03, 0x50},
    };
    expect_x_with_hex_register(immediate, "FACE", "3,",
                               "immediate structural X->P should preserve tail");
    for (const std::vector<int>& program : delayed_programs)
      expect_x_with_hex_register(program, "FACE", "1000,",
                                 "delayed command should not preserve store-spliced tail");
  }

  {
    const std::vector<int> r7_targets_r1 = {0x01, 0x47};
    const std::vector<std::pair<std::vector<int>, std::string>> suffixes = {
        {{0x01, 0xb7, 0x0c, 0x03, 0x50}, "0,"},
        {{0x01, 0x02, 0xb7, 0x0c, 0x03, 0x50}, "2000,"},
        {{0x00, 0x00, 0xb7, 0x0c, 0x03, 0x50}, "10000,"},
        {{0x00, 0x05, 0xb7, 0x0c, 0x03, 0x50}, "5000,"},
        {{0x01, 0x0a, 0xb7, 0x0c, 0x03, 0x50}, "0,"},
        {{0x01, 0x0a, 0x02, 0xb7, 0x0c, 0x03, 0x50}, "200,"},
        {{0x02, 0x0b, 0xb7, 0x0c, 0x03, 0x50}, "-9000,"},
        {{0x00, 0x0b, 0xb7, 0x0c, 0x03, 0x50}, "-1000,"},
    };
    for (const auto& [suffix, expected] : suffixes)
      expect_x(concat(r7_targets_r1, suffix), expected,
               "K X->P before VP splices decimal hidden X2 mantissa");
  }

  {
    const std::vector<std::pair<std::vector<int>, std::string>> cases = {
        {{0x02, 0xf0, 0x41, 0x0b, 0x0c, 0x03, 0x50}, "-2000,"},
        {{0x01, 0x00, 0x41, 0x0b, 0x0c, 0x03, 0x50}, "-10000,"},
        {{0x00, 0x05, 0x41, 0x0b, 0x0c, 0x03, 0x50}, "-5000,"},
        {{0x00, 0x00, 0x41, 0x0b, 0x0c, 0x03, 0x50}, "-1000,"},
        {{0x02, 0x0b, 0x41, 0x0b, 0x0c, 0x03, 0x50}, "2000,"},
        {{0x02, 0xf0, 0x41, 0x0b, 0x0b, 0x0c, 0x03, 0x50}, "2000,"},
        {{0x02, 0xf0, 0x41, 0x54, 0x0b, 0x0c, 0x03, 0x50}, "-2000,"},
    };
    for (const auto& [program, expected] : cases)
      expect_x(program, expected, "closed-context sign after X->P should use original X2");
  }

  {
    const std::vector<int> r7_targets_r1 = {0x01, 0x47};
    expect_x(concat(r7_targets_r1, {0x02, 0xf0, 0xb7, 0x0b, 0x0c, 0x03, 0x50}),
             "-2000,", "closed-context sign after K X->P should use original X2");
    expect_x(concat(r7_targets_r1, {0x02, 0x0b, 0xb7, 0x0b, 0x0c, 0x03, 0x50}),
             "2000,", "closed-context sign after K X->P should use original X2");
  }

  {
    expect_equal_runs({0x02, 0xf0, 0x41, 0x0a, 0x0b, 0x0c, 0x03, 0x50},
                      {0x02, 0xf0, 0x41, 0x0b, 0x0c, 0x03, 0x50},
                      "dot before store-backed sign restore should be redundant");
    expect_equal_runs({0x02, 0xf0, 0x41, 0x0a, 0x0b, 0x0b, 0x0c, 0x03, 0x50},
                      {0x02, 0xf0, 0x41, 0x0b, 0x0b, 0x0c, 0x03, 0x50},
                      "dot before double sign restore should be redundant");
  }

  {
    expect_x({0x20, 0x35, 0x41, 0x61, 0x0c, 0x50}, "1,415926-1",
             "P->X before VP should sync X2");
    expect_x({0x20, 0x35, 0x41, 0x0c, 0x50}, "1,",
             "without P->X, VP should use store-spliced context");
  }

  {
    const std::vector<int> before_stack_copy = {0x03, 0x0e, 0x04};
    expect_x(concat(before_stack_copy, {0x3e, 0x0c, 0x02, 0x50}), "400,",
             "Y->X should keep immediate VP transient source");
    expect_x(concat(before_stack_copy, {0x3e, 0x54, 0x0c, 0x02, 0x50}), "300,",
             "empty op should close Y->X transient source");
    expect_x(concat(before_stack_copy, {0x14, 0x0c, 0x02, 0x50}), "400,",
             "X<->Y should keep immediate VP transient source");
    expect_x(concat(before_stack_copy, {0x14, 0x54, 0x0c, 0x02, 0x50}), "300,",
             "empty op should close X<->Y transient source");
  }

  {
    expect_x({0x03, 0x0e, 0x04, 0x11, 0x54, 0x0c, 0x02, 0x50}, "100,",
             "delayed VP splice should use first significant decimal source digit");
    expect_x({0x02, 0x0b, 0x0e, 0x04, 0x14, 0x54, 0x0c, 0x02, 0x50}, "200,",
             "delayed VP splice ignores sign");
    expect_x({0x00, 0x0a, 0x02, 0x0b, 0x0e, 0x04, 0x14, 0x54, 0x0c, 0x02, 0x50},
             "200,", "delayed VP splice should use fractional first significant digit");
    expect_x({0x00, 0x0b, 0x0e, 0x04, 0x14, 0x54, 0x0c, 0x02, 0x50}, "0,",
             "delayed VP splice should handle signed zero");
  }

  {
    expect_x({0x20, 0x35, 0x41, 0x61, 0x53, 0x08, 0x0c, 0x50, 0x52}, "1,415926-1",
             "direct subroutine return should sync X2 before VP");
    expect_x({0x20, 0x35, 0x41, 0x53, 0x07, 0x0c, 0x50, 0x52}, "1,415926-1",
             "direct subroutine return should sync X2 without recall");
  }

  {
    expect_x({0x02, 0x35, 0x54, 0x0a, 0x50}, "2,", "KNOP should not sync X2");
    for (int opcode = 0xf0; opcode <= 0xff; ++opcode) {
      expect_x({0x02, 0x35, opcode, 0x50}, "0,", "F* empty opcode should preserve X");
      expect_x({0x02, 0x35, opcode, 0x0a, 0x50}, "0,", "F* empty opcode should sync X2");
    }
  }

  {
    expect_x({0x01, 0x0a, 0x02, 0xf0, 0x35, 0x50}, "2,-1",
             "K fractional keeps sign for negative non-integers");
    expect_x({0x01, 0x0a, 0x02, 0x0b, 0xf0, 0x35, 0x50}, "-2,-1",
             "K fractional keeps sign for negative non-integers");
    expect_x({0x02, 0x0b, 0xf0, 0x35, 0x50}, "0,",
             "K fractional on negative integer should be zero");
  }

  {
    const std::vector<int> signed_zero = {0x02, 0x0b, 0xf0, 0x35, 0xf0};
    expect_x(concat(signed_zero, {0x0c, 0x03, 0x50}), "-1000,",
             "signed-zero context should feed VP");
    expect_x(concat(signed_zero, {0x0a, 0x0c, 0x03, 0x50}), "-1000,",
             "dot should preserve signed-zero context");
    expect_x(concat(signed_zero, {0x0b, 0x0c, 0x03, 0x50}), "-1000,",
             "sign should preserve signed-zero context");
  }

  {
    expect_x({0x01, 0x0a, 0x02, 0xf0, 0x34, 0x50}, "1,",
             "K integer part should take signed integer part");
    expect_x({0x01, 0x0a, 0x02, 0x0b, 0xf0, 0x34, 0x50}, "-1,",
             "K integer part should take signed integer part");
    expect_x({0x00, 0x0a, 0x02, 0x0b, 0xf0, 0x34, 0x50}, "0,",
             "K integer part should handle negative fraction");
  }

  {
    expect_x({0x01, 0x0a, 0x02, 0x0b, 0xf0, 0x31, 0x50}, "1,2",
             "K abs should map concrete decimals exactly");
    expect_x({0x00, 0x0a, 0x02, 0x0b, 0xf0, 0x31, 0x50}, "2,-1",
             "K abs should map negative fractions exactly");
    expect_x({0x01, 0x0a, 0x02, 0x0b, 0xf0, 0x32, 0x50}, "-1,",
             "K sign should map negative decimal exactly");
    expect_x({0x00, 0xf0, 0x32, 0x50}, "0,", "K sign should map zero exactly");
    expect_x({0x01, 0x0a, 0x02, 0xf0, 0x32, 0x50}, "1,",
             "K sign should map positive decimal exactly");
  }

  {
    expect_x({0x01, 0x0e, 0x00, 0x24, 0x50}, "0,", "F x^y zero-base result");
    expect_x({0x00, 0x0a, 0x05, 0x0e, 0x00, 0x24, 0x50}, "0,",
             "F x^y fractional zero-base result");
    expect_x({0x01, 0x0b, 0x0e, 0x00, 0x24, 0x50}, "0,",
             "F x^y negative zero-base result");
    expect_x({0x00, 0x0e, 0x00, 0x24, 0x50}, "0,", "F x^y zero-zero result");
  }

  {
    expect_x({0x01, 0x0e, 0x02, 0x24, 0x50}, "2,", "F x^y exponent-one result");
    expect_x({0x01, 0x0e, 0x00, 0x0a, 0x05, 0x24, 0x50}, "5,-1",
             "F x^y fractional exponent-one result");
    expect_x({0x01, 0x0e, 0x01, 0x0a, 0x02, 0x24, 0x50}, "1,2",
             "F x^y decimal exponent-one result");
    expect_x({0x01, 0x0e, 0x02, 0x0b, 0x24, 0x50}, "-2,",
             "F x^y negative exponent-one result");
  }

  {
    expect_x({0x08, 0x15, 0x50}, "1,8", "scientific decimal should restore through X2");
    expect_x({0x08, 0x15, 0x0e, 0x0a, 0x50}, "1,8",
             "scientific decimal dot should restore through X2");
    expect_x({0x01, 0x0c, 0x08, 0x50}, "1,8", "VP scientific decimal");
    expect_x({0x01, 0x0c, 0x08, 0x0e, 0x0a, 0x50}, "1,8",
             "VP scientific decimal dot restore");
    expect_x({0x01, 0x0e, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
              0x10, 0x0e, 0x0a, 0x50},
             "1,8", "scientific exact decimal after addition should restore through X2");
  }

  {
    expect_x({0x08, 0x15, 0x0e, 0x0a, 0x0c, 0x02, 0x50}, "1,10",
             "scientific X2 restore feeds next VP");
    expect_x({0x08, 0x0b, 0x15, 0x0e, 0x0a, 0x0c, 0x02, 0x50}, "1,-6",
             "negative scientific X2 restore feeds next VP");
    expect_x({0x01, 0x0e, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
              0x10, 0x0e, 0x0a, 0x0c, 0x02, 0x50},
             "1,10", "scientific X2 restore after addition feeds next VP");
  }

  {
    expect_x({0x08, 0x15, 0x0e, 0x0b, 0x50}, "-1,8",
             "closed sign should preserve scientific X2 display");
    expect_x({0x08, 0x15, 0x0e, 0x0b, 0x0c, 0x02, 0x50}, "-1,10",
             "closed sign scientific X2 should feed VP");
    expect_x({0x08, 0x0b, 0x15, 0x0e, 0x0b, 0x50}, "-1,-8",
             "closed sign preserves negative scientific X2");
    expect_x({0x08, 0x0b, 0x15, 0x0e, 0x0b, 0x0c, 0x02, 0x50}, "-1,-6",
             "closed sign negative scientific X2 should feed VP");
  }

  {
    expect_x({0x02, 0x35, 0x54, 0x0a, 0x50}, "2,", "KNOP should not sync X2");
    expect_x({0x02, 0x35, 0x0e, 0x50}, "0,", "stack lift should preserve X");
    expect_x({0x02, 0x35, 0x0e, 0x0a, 0x50}, "0,", "stack lift should sync X2");
  }

  {
    expect_x({0x01, 0x40, 0x02, 0x35, 0x54, 0x0a, 0x50}, "2,",
             "F L0 baseline KNOP should not sync X2");
    expect_x({0x01, 0x40, 0x02, 0x35, 0x5d, 0x08, 0x50}, "0,",
             "F L0 fallthrough should preserve X");
    expect_x({0x01, 0x40, 0x02, 0x35, 0x5d, 0x08, 0x0a, 0x50}, "0,",
             "F L0 fallthrough should sync X2");
  }

  {
    // P->X synchronizes X2 before the conditional.  The direct jump preserves
    // it, while fallthrough synchronizes the same visible X again.  Therefore
    // both mutually exclusive stores and the final recall can be replaced by
    // one dot restore.
    const std::vector<int> stored_flag = {
        0x61, 0x5e, 0x06, 0x42, 0x51, 0x07, 0x42, 0x20, 0x62, 0x50,
    };
    const std::vector<int> hidden_flag = {
        0x61, 0x5e, 0x05, 0x51, 0x05, 0x20, 0x0a, 0x50,
    };
    const auto compare = [&](const std::string& value, const std::string& context) {
      const std::map<std::string, std::string> registers = {{"1", value}};
      const std::string hidden = run_x_with_registers(hidden_flag, registers);
      const std::string stored = run_x_with_registers(stored_flag, registers);
      require(hidden == stored, context + " expected equal X values, got " + hidden +
                                    " vs " + stored);
      return hidden;
    };
    const std::string nonzero_result =
        compare("3", "conditional jump keeps a nonzero flag in X2");
    const std::string zero_result =
        compare("0", "conditional fallthrough keeps a zero flag in X2");
    require(nonzero_result != zero_result,
            "branch-merged hidden X2 flag should preserve both boolean outcomes, got " +
                nonzero_result + " and " + zero_result);
  }

  {
    expect_x({0x00, 0x02, 0xf0, 0x0b, 0x50}, "-2,",
             "closed sign should update proved decimal X2");
    expect_x({0x00, 0x02, 0xf0, 0x0b, 0x0a, 0x50}, "-2,",
             "dot after closed sign should be redundant");
    expect_x({0x00, 0x02, 0x0b, 0xf0, 0x0b, 0x50}, "2,",
             "double sign should update proved decimal X2");
    expect_x({0x0d, 0x0b, 0x50}, "0,", "closed sign on exponent entry should normalize");
    expect_x({0x0d, 0x0b, 0x0a, 0x50}, "0,", "dot after normalized exponent sign");
  }

  {
    require(run_signature({0x05, 0x0c, 0x03, 0x0a, 0x50}).find("ЕГГ0Г") != std::string::npos,
            "active exponent-entry dot should be unsafe");
    require(run_signature({0x00, 0x05, 0x0c, 0x03, 0x0a, 0x50}).find("ЕГГ0Г") !=
                std::string::npos,
            "leading-zero active exponent-entry dot should be unsafe");
    expect_x({0x00, 0x05, 0x0c, 0x03, 0xf0, 0x0a, 0x50}, "5000,",
             "closing X2 sync should normalize active exponent-entry dot");
  }

  {
    expect_x({0x00, 0x02, 0x35, 0x0a, 0x0c, 0x03, 0x50}, "22000,",
             "dot-restored leading-zero X2 changes following VP shape");
    expect_x({0x02, 0x0c, 0x03, 0x50}, "2000,", "ordinary normalized VP baseline");
  }

  {
    const std::vector<int> original = {0x01, 0x02, 0xf0, 0x01, 0x02, 0x55, 0x0c, 0x03, 0x50};
    const std::vector<int> restored = {0x01, 0x02, 0xf0, 0x0a, 0x55, 0x0c, 0x03, 0x50};
    const std::vector<int> nooped = {0x01, 0x02, 0xf0, 0x55, 0x0c, 0x03, 0x50};
    const std::vector<int> signed_original = {0x01, 0x02, 0x0b, 0xf0, 0x01, 0x02,
                                              0x0b, 0x55, 0x0c, 0x03, 0x50};
    const std::vector<int> signed_restored = {0x01, 0x02, 0x0b, 0xf0, 0x0a,
                                              0x55, 0x0c, 0x03, 0x50};
    const std::vector<int> signed_nooped = {0x01, 0x02, 0x0b, 0xf0, 0x55, 0x0c, 0x03, 0x50};
    const std::vector<int> exponent_original = {0x05, 0x0c, 0x03, 0xf0, 0x05, 0x0c,
                                                0x03, 0x55, 0x0c, 0x02, 0x50};
    const std::vector<int> exponent_restored = {0x05, 0x0c, 0x03, 0xf0, 0x0a,
                                                0x55, 0x0c, 0x02, 0x50};
    const std::vector<int> signed_exponent_original = {0x05, 0x0b, 0x0c, 0x03, 0xf0,
                                                       0x05, 0x0b, 0x0c, 0x03, 0x55,
                                                       0x0c, 0x02, 0x50};
    const std::vector<int> signed_exponent_restored = {0x05, 0x0b, 0x0c, 0x03, 0xf0,
                                                       0x0a, 0x55, 0x0c, 0x02, 0x50};
    const std::vector<int> fractional_exponent_original = {0x05, 0x0c, 0x03, 0x0b, 0xf0,
                                                           0x05, 0x0c, 0x03, 0x0b, 0x55,
                                                           0x0c, 0x02, 0x50};
    const std::vector<int> fractional_exponent_restored = {0x05, 0x0c, 0x03, 0x0b, 0xf0,
                                                           0x0a, 0x55, 0x0c, 0x02, 0x50};

    expect_equal_runs(restored, original, "dot-restored normalized decimal");
    expect_equal_runs(nooped, restored, "empty op after restored decimal");
    expect_equal_runs(signed_restored, signed_original, "signed dot-restored decimal");
    expect_equal_runs(signed_nooped, signed_restored, "empty op after signed restored decimal");
    expect_equal_runs(exponent_restored, exponent_original, "exponent dot-restored decimal");
    expect_equal_runs(signed_exponent_restored, signed_exponent_original,
                      "signed exponent dot-restored decimal");
    expect_equal_runs(fractional_exponent_restored, fractional_exponent_original,
                      "fractional exponent dot-restored decimal");
    expect_different_runs({0x00, 0x0b, 0xf0, 0x00, 0x0b, 0x55, 0x0c, 0x03, 0x50},
                          {0x00, 0x0b, 0xf0, 0x0a, 0x55, 0x0c, 0x03, 0x50},
                          "signed zero original and restored must differ");
    expect_different_runs({0x00, 0x05, 0x0c, 0x03, 0xf0, 0x0a, 0x55, 0x0c, 0x02, 0x50},
                          {0x00, 0x05, 0x0c, 0x03, 0xf0, 0x00, 0x05, 0x0c,
                           0x03, 0x55, 0x0c, 0x02, 0x50},
                          "leading-zero exponent original and restored must differ");
  }

  {
    const std::string with_fraction =
        run_x_with_registers({0x61, 0x20, 0x0c, 0x35, 0x50}, {{"1", "1.23"}});
    const std::string without_fraction =
        run_x_with_registers({0x61, 0x20, 0x0c, 0x50}, {{"1", "1.23"}});
    require(with_fraction == "2,3-1",
            "plain VP restore followed by K fractional should keep fractional tail");
    require(without_fraction == "1,23", "plain VP restore baseline should preserve register value");
  }
}

}  // namespace mkpro::tests
