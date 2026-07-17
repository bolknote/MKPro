#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

constexpr int kIp1 = 0x61;
constexpr int kIp2 = 0x62;
constexpr int kAdd = 0x10;
constexpr int kSubtract = 0x11;
constexpr int kMultiply = 0x12;
constexpr int kDivide = 0x13;
constexpr int kStackLift = 0x0e;
constexpr int kDot = 0x0a;
constexpr int kFPi = 0x20;
constexpr int kSquare = 0x22;
constexpr int kKSign = 0x32;
constexpr int kF0 = 0xf0;
constexpr int kStop = 0x50;

struct BinaryCase {
  std::string left;
  std::string right;
  std::string expected;
};

struct UnaryCase {
  std::string value;
  std::vector<int> codes;
  std::string expected;
};

std::string compact(std::string value) {
  std::string out;
  for (const char ch : value) {
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
      out.push_back(ch);
  }
  return out;
}

std::string run_binary_register_program(const std::string& r1, const std::string& r2,
                                        int op) {
  emulator::MK61 calc;
  calc.set_register("1", r1);
  calc.set_register("2", r2);
  calc.load_program({kIp1, kIp2, op, kStop});
  calc.press_sequence({"В/О", "С/П"});
  (void)calc.run_until_stable(300, 4);
  return compact(calc.display_text());
}

std::string add_registers(const std::string& r1, const std::string& r2) {
  return run_binary_register_program(r1, r2, kAdd);
}

std::string subtract_registers(const std::string& r1, const std::string& r2) {
  return run_binary_register_program(r1, r2, kSubtract);
}

std::string multiply_registers(const std::string& r1, const std::string& r2) {
  return run_binary_register_program(r1, r2, kMultiply);
}

std::string divide_registers(const std::string& r1, const std::string& r2) {
  return run_binary_register_program(r1, r2, kDivide);
}

std::string run_unary_register_program(const std::string& r1, std::vector<int> codes) {
  emulator::MK61 calc;
  calc.set_register("1", r1);
  codes.push_back(kStop);
  calc.load_program(codes);
  calc.press_sequence({"В/О", "С/П"});
  (void)calc.run_until_stable(300, 4);
  return compact(calc.display_text());
}

void require_equal(const std::string& actual, const std::string& expected,
                   const std::string& context) {
  require(actual == expected, context + " expected " + expected + ", got " + actual);
}

void require_not_equal(const std::string& actual, const std::string& unexpected,
                       const std::string& context) {
  require(actual != unexpected, context + " should not equal " + unexpected);
}

void require_contains(const std::string& actual, const std::string& expected,
                      const std::string& context) {
  require(actual.find(expected) != std::string::npos,
          context + " expected display containing " + expected + ", got " + actual);
}

void require_binary_table(const std::vector<BinaryCase>& cases,
                          const std::string& operation,
                          const auto& run) {
  for (const BinaryCase& test_case : cases) {
    require_equal(run(test_case.left, test_case.right), test_case.expected,
                  operation + "(" + test_case.left + ", " + test_case.right + ")");
  }
}

void require_unary_table(const std::vector<UnaryCase>& cases, const std::string& operation) {
  for (const UnaryCase& test_case : cases) {
    require_equal(run_unary_register_program(test_case.value, test_case.codes),
                  test_case.expected, operation + "(" + test_case.value + ")");
  }
}

} // namespace

void emulator_hex_arithmetic_facts_match_typescript_contract() {
  {
    emulator::MK61 calc;
    calc.set_register("1", "Г");
    require_equal(compact(calc.read_register("1")), "Г,",
                  "hex mantissa digit should round-trip through memory");
  }

  require_contains(add_registers("Г", "3"), "16",
                   "hex digit in Y should add as decimal value");
  require_contains(add_registers("С", "4"), "16",
                   "hex C in Y should add as decimal value");
  require_equal(add_registers("3", "Г"), "0,",
                "hex digit in X should collapse addition to zero");
  require_equal(add_registers("4", "С"), "0,",
                "hex C in X should collapse addition to zero");

  require_binary_table(
      {
          {"B", "0", "11,"},  {"0", "B", "1,"},   {"Г", "4", "17,"},
          {"3", "С", "5,"},   {"-", "18", "28,"},  {"18", "-", "28,"},
          {"С", "16", "28,"}, {"16", "С", "28,"},  {"Е", "18", "32,"},
          {"18", "Е", "32,"}, {"-", "B", "5,"},    {"Г", "Е", "11,"},
      },
      "add", add_registers);

  require_binary_table(
      {
          {"-", "0", "10,"},    {"B", "1", "0,"},    {"0", "B", "-1,"},
          {"С", "2", "0,"},     {"Е", "5", "9,"},    {"-", "18", "-8,"},
          {"18", "-", "8,"},    {"B", "18", "-7,"},  {"18", "B", "23,"},
          {"С", "16", "-4,"},   {"16", "С", "20,"},  {"10", "Е", "12,"},
          {"0", "С", "-2,"},    {"2", "С", "-10,"},  {"4", "Е", "-10,"},
          {"-", "Е", "-4,"},    {"Е", "-", "4,"},
      },
      "subtract", subtract_registers);

  require_unary_table(
      {
          {"С", {kIp1, 0x01, kSubtract}, "1,"},
          {"Г", {kIp1, 0x01, kSubtract}, "2,"},
          {"Е", {kIp1, 0x01, kSubtract}, "3,"},
          {"С", {kIp1, 0x01, kSubtract, kSquare}, "1,"},
          {"Г", {kIp1, 0x01, kSubtract, kSquare}, "4,"},
          {"Е", {kIp1, 0x01, kSubtract, kSquare}, "9,"},
      },
      "hex subtract-one");

  require_unary_table(
      {
          {"-", {kIp1, kSquare}, "00,"},
          {"B", {kIp1, kSquare}, "10,"},
          {"С", {kIp1, kSquare}, "20,"},
          {"Г", {kIp1, kSquare}, "30,"},
          {"Е", {kIp1, kSquare}, "0,"},
          {"_", {kIp1, kSquare}, "0,"},
          {"0С", {kIp1, kSquare}, "20,"},
          {"-0Г", {kIp1, kSquare}, "30,"},
          {"B0", {kIp1, kSquare}, "1000,"},
      },
      "single hex square");

  require_unary_table(
      {
          {"B0", {kIp1, kSquare}, "1000,"},
          {"С0", {kIp1, kSquare}, "2000,"},
          {"Г0", {kIp1, kSquare}, "3000,"},
          {"0B0", {kIp1, kSquare}, "1000,"},
          {"B00", {kIp1, kSquare}, "100000,"},
          {"BE-3", {kIp1, kSquare}, "1,-05"},
          {"BE-2", {kIp1, kSquare}, "1,-03"},
          {"BE-1", {kIp1, kSquare}, "1,-01"},
          {"BE1", {kIp1, kSquare}, "1000,"},
          {"BE4", {kIp1, kSquare}, "1,09"},
          {"СE-2", {kIp1, kSquare}, "2,-03"},
          {"ГE3", {kIp1, kSquare}, "30000000,"},
          {"AE-2", {kIp1, kSquare}, "0,-03"},
          {"AE2", {kIp1, kSquare}, "000000,"},
          {"ЕE-2", {kIp1, kSquare}, "0,"},
          {"Е0", {kIp1, kSquare}, "0,"},
          {"_E2", {kIp1, kSquare}, "0,"},
          {"_0", {kIp1, kSquare}, "0,"},
          {"0,A", {kIp1, kSquare}, "0,-01"},
          {"0,0A", {kIp1, kSquare}, "0,-03"},
          {"0,AE2", {kIp1, kSquare}, "0000,"},
          {"0,AE0", {kIp1, kSquare}, "0,-01"},
          {"0,0AE2", {kIp1, kSquare}, "00,"},
          {"0,Е", {kIp1, kSquare}, "0,"},
          {"0,0Е", {kIp1, kSquare}, "0,"},
          {"0,ЕE2", {kIp1, kSquare}, "0,"},
          {"0,_", {kIp1, kSquare}, "0,"},
          {"0,0_", {kIp1, kSquare}, "0,"},
          {"0,_E2", {kIp1, kSquare}, "0,"},
          {"0,BE2", {kIp1, kSquare}, "1000,"},
          {"0,BE0", {kIp1, kSquare}, "1,-01"},
          {"0,0BE2", {kIp1, kSquare}, "10,"},
          {"0,СE2", {kIp1, kSquare}, "2000,"},
          {"0,ГE2", {kIp1, kSquare}, "3000,"},
      },
      "scaled hex square");

  require_unary_table(
      {
          {"FA", {kIp1, kSquare}, "0,"},
          {"-FA", {kIp1, kSquare}, "0,"},
          {"FB", {kIp1, kSquare}, "0,"},
          {"FF", {kIp1, kSquare}, "0,"},
      },
      "super-number square");

  require_unary_table(
      {
          {"-", {kIp1, kSquare, kF0, kFPi, kDot}, "0,"},
          {"-", {kIp1, kStackLift, 0x01, 0x08, kMultiply}, "020,"},
          {"-", {kIp1, kStackLift, 0x01, 0x08, kMultiply, kF0, kFPi, kDot}, "20,"},
      },
      "X2-affecting sync");

  {
    emulator::MK61 calc;
    calc.set_register("1", "-");
    calc.set_register("2", "18");
    calc.load_program({kIp1, kIp2, kMultiply, kStop});
    calc.press_sequence({"В/О", "С/П"});
    (void)calc.run_until_stable(300, 4);
    require_equal(calc.display_text(), "020,", "left hex A times 18 display");
    require_equal(compact(calc.read_register("x")), "20,", "left hex A times 18 X register");
  }

  {
    emulator::MK61 calc;
    calc.set_register("1", "18");
    calc.set_register("2", "-");
    calc.load_program({kIp1, kIp2, kMultiply, kStop});
    calc.press_sequence({"В/О", "С/П"});
    (void)calc.run_until_stable(300, 4);
    require_equal(calc.display_text(), "180,", "right hex A times 18 display");
    require_equal(compact(calc.read_register("x")), "180,", "right hex A times 18 X register");
  }

  require_binary_table(
      {
          {"-", "1", "0,"},      {"-", "-", "00,"},    {"B", "Г", "10,"},
          {"-", "18", "020,"},   {"-", "16", "000,"},  {"B", "18", "054,"},
          {"B", "17", "043,"},   {"С", "5", "32,"},    {"С", "16", "904,"},
          {"Г", "3", "23,"},     {"Г", "15", "923,"},  {"Е", "18", "948,"},
          {"Е", "17", "934,"},   {"Е", "С", "40,"},
      },
      "multiply left hex", multiply_registers);

  require_binary_table(
      {
          {"1", "-", "10,"},   {"16", "-", "160,"}, {"18", "-", "180,"},
          {"18", "B", "180,"}, {"17", "B", "170,"}, {"9", "С", "90,"},
          {"16", "С", "160,"}, {"5", "Г", "50,"},   {"15", "Г", "150,"},
          {"18", "Е", "0,"},   {"17", "Е", "0,"},
      },
      "multiply right hex", multiply_registers);

  require_binary_table(
      {
          {"-", "-", "1,"},           {"-", "Г", "4,-01"},
          {"B", "Г", "6,-01"},        {"С", "B", "1,2525252"},
          {"Г", "С", "1,23"},         {"Е", "Г", "1,2"},
          {"-", "С", "ЕГГ0Г"},        {"B", "2", "5,5"},
          {"Г", "8", "1,625"},        {"Г", "16", "8,125-01"},
          {"Е", "17", "8,2352941-01"}, {"Е", "18", "7,7777777-01"},
          {"-", "10", "0,-01"},       {"1", "-", "ЕГГ0Г"},
          {"10", "-", "ЕГГ0Г"},       {"0", "-", "9,090909-01"},
          {"9", "B", "0,4444443-01"}, {"16", "B", "9,2525252"},
          {"10", "С", "9,9099099"},   {"12", "Г", "0,"},
          {"15", "Е", "0,2292929"},   {"5", "Г", "0,-01"},
          {"3", "С", "ЕГГ0Г"},
      },
      "divide single hex", divide_registers);

  require_unary_table(
      {
          {"8F", {kIp1, kKSign}, "1,"},
          {"-8F", {kIp1, kKSign}, "-1,"},
          {"0A", {kIp1, kKSign}, "1,"},
          {"-0A", {kIp1, kKSign}, "-1,"},
          {"A", {kIp1, 0x0c, 0x02, 0x0b, kKSign}, "1,"},
          {"Г", {kIp1, 0x0c, 0x02, 0x0b, kKSign}, "1,"},
      },
      "K sign structural hex");
  {
    emulator::MK61 calc;
    calc.set_register("x", "2");
    calc.load_program({kKSign, kStop});
    calc.press_sequence({"В/О", "С/П"});
    (void)calc.run_until_stable(300, 4);
    require_equal(compact(calc.read_register("x1")), "2,",
                  "K sign should copy its input X into hidden X1");
  }
  require_not_equal(run_unary_register_program("0F", {kIp1, kKSign}), "1,",
                    "0F should not be a pinned positive structural sign");
  require_not_equal(run_unary_register_program("F", {kIp1, 0x0c, 0x02, 0x0b, kKSign}),
                    "1,", "F should not be a pinned positive structural sign");

  require_unary_table(
      {
          {"FA", {kIp1, kKSign}, ",00000,,"},
          {"FB", {kIp1, kKSign}, ",00000,,"},
          {"FF", {kIp1, kKSign}, ",00000,,"},
          {"-FA", {kIp1, kKSign}, ",00000,,"},
          {"FAE2", {kIp1, kKSign}, ",00000,,"},
          {"FA", {kIp1, 0x0c, 0x02, kKSign}, "-00,"},
      },
      "K sign super numbers");

  for (const std::string& literal : {"A", "B", "C"}) {
    require_equal(run_unary_register_program(literal, {kIp1, 0x42, 0x20, 0x0a}),
                  run_unary_register_program(literal, {kIp1}),
                  "dot restore should be safe for " + literal);
    require_equal(run_unary_register_program(literal, {kIp1, 0x0b, 0x0b, 0x0a}),
                  run_unary_register_program(literal, {kIp1}),
                  "dot restore after sign pair should be safe for " + literal);
  }
  require_equal(run_unary_register_program("D", {kIp1, 0x42, 0x20, 0x0a}), "ЕГГ0Г",
                "dot restore should error for D");
  require_equal(run_unary_register_program("D", {kIp1, 0x0b, 0x0b, 0x0a}), "ЕГГ0Г",
                "dot restore after sign pair should error for D");
  require_not_equal(run_unary_register_program("F", {kIp1, 0x42, 0x20, 0x0a}),
                    run_unary_register_program("F", {kIp1}),
                    "dot restore should not be safe for F");
  require_not_equal(run_unary_register_program("F", {kIp1, 0x0b, 0x0b, 0x0a}),
                    run_unary_register_program("F", {kIp1}),
                    "dot restore after sign pair should not be safe for F");

  require_binary_table(
      {
          {"AE-2", "1", "0,-02"},     {"AE-2", "18", "0,2"},
          {"BE-2", "18", "0,54"},     {"CE-2", "16", "9,04"},
          {"AE-2", "15", "9,9"},      {"CE-2", "17", "9,"},
          {"1", "ГE-2", "1,-01"},     {"2", "ГE-2", "2,-01"},
          {"4", "ГE-2", "4,-01"},     {"5", "ГE-2", "5,-01"},
          {"8", "ГE-2", "8,-01"},     {"16", "ГE-2", "1,6"},
          {"18", "BE-2", "1,8"},      {"17", "CE-2", "1,7"},
          {"18", "ЕE-2", "0,"},       {"ГE-2", "1", "3,-02"},
          {"ГE-2", "5", "5,3-01"},    {"ГE-2", "16", "9,2"},
          {"ЕE-2", "18", "9,48"},
      },
      "multiply single hex exponent minus two", multiply_registers);

  require_binary_table(
      {
          {"ГE-1", "5", "5,3"},       {"BE-1", "18", "05,4"},
          {"AE-3", "1", "0,-03"},     {"BE-3", "18", "0,54-01"},
          {"ГE1", "8", "600,"},       {"CE1", "16", "9040,"},
          {"16", "AE-3", "1,6-01"},   {"18", "BE-1", "18,"},
          {"1", "AE0", "10,"},        {"18", "ГE1", "1800,"},
          {"18", "ЕE1", "0,"},        {"ГE2", "16", "92000,"},
          {"Г00", "16", "92000,"},    {"BE5", "18", "05400000,"},
          {"ЕE5", "18", "94800000,"}, {"18", "BE2", "18000,"},
          {"18", "ГE5", "18000000,"}, {"BE6", "18", "0,5408"},
          {"ГE8", "16", "9,210"},     {"18", "ГE8", "1,810"},
      },
      "multiply scaled hex exponent", multiply_registers);

  require_binary_table(
      {
          {"ГE-2", "0", "1,3-01"},   {"9", "ГE-2", "9,13"},
          {"BE-2", "0", "1,1-01"},   {"ЕE-2", "6", "6,14"},
      },
      "add hex exponent", add_registers);
  require_binary_table(
      {
          {"ГE-2", "1", "-8,7-01"},  {"ГE-2", "16", "-15,87"},
          {"BE-2", "1", "-8,9-01"},  {"0", "BE-2", "5,-02"},
          {"0", "ГE-2", "3,-02"},    {"0", "ЕE-2", "2,-02"},
          {"18", "ГE-2", "18,03"},
      },
      "subtract hex exponent", subtract_registers);

  require_binary_table(
      {
          {"ГE-3", "9", "9,013"},       {"ГE-7", "9", "9,0000013"},
          {"ГE-8", "0", "1,3-07"},      {"ГE-8", "1", "1,0000001"},
          {"ГE-9", "0", "1,3-08"},      {"9", "BE-1", "10,1"},
      },
      "add exact negative hex exponent", add_registers);
  require_binary_table(
      {
          {"ГE-1", "9", "-7,7"},        {"9", "BE-3", "9,005"},
          {"0", "ГE-8", "3,-08"},       {"0", "ГE-9", "3,-09"},
          {"9", "ГE-8", "9,"},          {"9", "ЕE-1", "9,2"},
      },
      "subtract exact negative hex exponent", subtract_registers);

  require_binary_table(
      {
          {"AE0", "9", "19,"},          {"9", "AE0", "3,"},
          {"AE1", "1", "01,"},          {"BE1", "10", "120,"},
          {"ГE1", "18", "148,"},        {"1", "AE1", "01,"},
          {"10", "AE1", "10,"},         {"18", "ГE1", "48,"},
      },
      "add hex exponent zero/plus one", add_registers);
  require_binary_table(
      {
          {"ГE0", "9", "4,"},           {"9", "ГE0", "-4,"},
          {"AE1", "1", "-61,"},         {"BE1", "10", "00,"},
          {"ЕE1", "18", "22,"},         {"1", "AE1", "-99,"},
          {"10", "BE1", "-100,"},       {"18", "ЕE1", "-22,"},
      },
      "subtract hex exponent zero/plus one", subtract_registers);

  require_binary_table(
      {
          {"AE-2", "1", "0,-02"},       {"BE-2", "18", "6,1111111-03"},
          {"CE-2", "16", "7,5-03"},     {"AE-2", "10", "0,-03"},
          {"ГE-2", "17", "7,6470588-03"}, {"ГE-2", "0", "ЕГГ0Г"},
          {"ГE-2", "2", "6,5-02"},      {"ГE-2", "16", "8,125-03"},
          {"ЕE-2", "18", "7,7777777-03"}, {"0", "AE-2", "90,90909"},
          {"1", "AE-2", "ЕГГ0Г"},       {"9", "AE-2", "89,90909"},
          {"18", "BE-2", "943,43434"},  {"15", "BE-2", "900,"},
          {"3", "CE-2", "ЕГГ0Г"},       {"12", "ГE-2", "000,"},
          {"0", "ГE-2", "99,099099"},   {"5", "ГE-2", "00,"},
          {"16", "ГE-2", "920,"},       {"16", "ЕE-2", "052,92929"},
          {"ГE-1", "5", "2,6-01"},      {"BE-1", "18", "6,1111111-02"},
          {"AE-3", "1", "0,-03"},       {"BE-3", "18", "6,1111111-04"},
          {"ГE1", "8", "16,25"},        {"CE1", "16", "7,5"},
          {"18", "BE-3", "9434,3434"},  {"18", "BE-1", "94,343434"},
          {"18", "BE1", "9,4343434-01"}, {"12", "ГE-3", "0000,"},
          {"16", "ЕE-1", "05,292929"},  {"ГE2", "16", "81,25"},
          {"Г00", "16", "81,25"},       {"BE5", "18", "61111,111"},
          {"ЕE5", "18", "77777,777"},   {"18", "BE2", "9,4343434-02"},
          {"18", "ГE5", "9,6-05"},      {"BE9", "18", "6,111111108"},
          {"18", "BE9", "9,4343434-09"},
      },
      "divide hex exponents", divide_registers);
}

} // namespace mkpro::tests
