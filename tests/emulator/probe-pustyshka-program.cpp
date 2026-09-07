// Standalone verifier for the same .hex fixtures as the JavaScript probe.
// Uses only the public, unmodified native emulator API; hidden words are never
// initialized by the host. See docs/24-pustyshka-program-protocol.md.
#include "mkpro/emulator/mk61.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using mkpro::emulator::MK61;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

std::vector<int> load_hex(const std::string& path) {
  std::ifstream input(path);
  require(input.good(), "cannot open " + path);
  std::vector<int> codes;
  std::string token;
  while (input >> token) codes.push_back(std::stoi(token, nullptr, 16));
  require(codes.size() == 105, "expected 105 bytes in " + path);
  return codes;
}

void start(MK61& calc, const std::vector<int>& program, const std::string& final_pc) {
  const auto loaded = calc.load_program(program);
  require(loaded.diagnostics.empty(), "program load failed");
  calc.press("В/О");
  calc.run_frames(12);
  calc.press("С/П");
  calc.run_frames(12);
  const auto result = calc.run_until_stable(2000, 8);
  require(result.stopped, "program did not stop");
  require(calc.program_counter() == final_pc, "wrong final program counter");
  require(calc.read_program_codes() == program, "program memory changed");
}

void check(const std::vector<int>& program, const std::array<std::string, 3>& values,
           bool scalar, int variation) {
  const std::array<std::string, 3> angles = {"rad", "deg", "grad"};
  MK61 calc({.extended = true, .angle_mode = angles[variation % 3]});
  std::array<std::string, 3> expected;
  for (int i = 0; i < 3; ++i) {
    const std::string reg(1, static_cast<char>('a' + i));
    calc.set_register(reg, values[i]);
    expected[i] = calc.read_register(reg);
  }
  if (variation % 2) {
    calc.set_register("x", "-99999999");
    calc.set_register("y", "1.2345678E99");
    calc.set_register("z", "-1.2345678E-42");
    calc.set_register("t", "98765432");
    calc.set_register("x1", "-77777777");
  }
  start(calc, program, scalar ? "80" : "92");
  const int results = scalar ? 3 : 9;
  for (int i = 0; i < results; ++i) {
    require(calc.read_register(std::to_string(i)) == expected[scalar ? 2 : i % 3],
            "wrong result R" + std::to_string(i) + " for " + values[0] + "/"
                + values[1] + "/" + values[2]);
  }
  for (int i = results; i < 15; ++i) {
    require(calc.read_register(std::string(1, "0123456789abcde"[i])) == "0,",
            "an ordinary register was not cleared");
  }
  require(calc.read_register("x1") == (scalar ? "0," : expected[1]), "wrong X1");
}

int main(int argc, char** argv) {
  try {
    const std::string directory = argc > 1 ? argv[1] : "tests/emulator/fixtures";
    const auto triple = load_hex(directory + "/pustyshka-triple.hex");
    const auto scalar = load_hex(directory + "/pustyshka-scalar.hex");
    const auto demo = load_hex(directory + "/pustyshka-demo.hex");

    // Each of the 1000 values occurs in each slot. The latter two expressions
    // are permutations modulo 1000, giving mixed digit widths at each call.
    for (int value = 0; value < 1000; ++value) {
      check(triple, {std::to_string(value), std::to_string((37 * value + 111) % 1000),
                     std::to_string((997 * value + 9) % 1000)}, false, value);
    }
    std::cout << "1000 triples passed; every value 0..999 covered in every slot; "
                 "three reads per run." << std::endl;

    const std::vector<std::string> edges = {
        "0", "999", "12345678", "-12345678", "1.2345678E99",
        "-1.2345678E-42", "1E-99", "-1E-99", "9.9999999E99", "-9.9999999E99",
    };
    int scalar_count = 0;
    for (const auto& value : edges) {
      check(scalar, {"11111111", "-22222222", value}, true, scalar_count++);
    }
    std::uint32_t random = 0x61f034;
    for (int i = 0; i < 300; ++i) {
      random = random * 1664525U + 1013904223U;
      const auto digits = std::to_string(10000000U + random % 90000000U);
      random = random * 1664525U + 1013904223U;
      const int exponent = static_cast<int>(random % 199U) - 99;
      const std::string value = (i % 2 ? "-" : "") + digits.substr(0, 1) + "."
          + digits.substr(1) + "E" + std::to_string(exponent);
      check(scalar, {"11111111", "-22222222", value}, true, scalar_count++);
    }
    std::cout << scalar_count << " scalar cases passed, including signs and exponents -99..99."
              << std::endl;

    MK61 calc;
    start(calc, demo, "87");
    for (int i = 0; i < 3; ++i) {
      require(calc.read_register(std::to_string(i)) ==
                  std::array<std::string, 3>{"123,", "456,", "789,"}[i],
              "self-contained demo failed");
    }
    std::cout << "Cold-start demo passed without host-provided numeric payloads. "
                 "All 105 code bytes preserved in every run." << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
