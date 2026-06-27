#include "mkpro/core/passes/index.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <map>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

constexpr int kDot = 0x0a;
constexpr int kSignChange = 0x0b;
constexpr int kVp = 0x0c;
constexpr int kCx = 0x0d;
constexpr int kFPi = 0x20;
constexpr int kFrac = 0x35;
constexpr int kK1 = 0x55;
constexpr int kStop = 0x50;
constexpr int kRecall1 = 0x61;

MachineItem op(int opcode) {
  return MachineItem::op(opcode, std::to_string(opcode));
}

std::vector<int> codes(const std::vector<MachineItem>& items) {
  std::vector<int> result;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Op)
      result.push_back(item.opcode);
  }
  return result;
}

std::vector<int> optimized_codes(const std::vector<int>& program) {
  std::vector<MachineItem> items;
  for (const int opcode : program)
    items.push_back(op(opcode));

  CompileOptions options;
  options.delivery = DeliveryMode::Hex;
  options.budget = 105;
  options.analysis = false;
  return codes(core::passes::run_ir_passes(items, options).items);
}

std::string compact(std::string value) {
  std::string out;
  for (const char ch : value) {
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
      out.push_back(ch);
  }
  return out;
}

void set_registers(emulator::MK61& calc, const std::map<std::string, std::string>& registers) {
  for (const auto& [name, value] : registers)
    calc.set_register(name, value);
}

std::string display(const std::vector<int>& program,
                    const std::map<std::string, std::string>& registers = {}) {
  emulator::MK61 calc;
  set_registers(calc, registers);
  calc.load_program(program);
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(300, 5);
  return compact(calc.display_text());
}

std::string signature(const std::vector<int>& program,
                      const std::map<std::string, std::string>& registers = {}) {
  emulator::MK61 calc;
  set_registers(calc, registers);
  calc.load_program(program);
  calc.press_sequence({"В/О", "С/П"});
  return calc.run_until_stable(300, 5).signature;
}

} // namespace

void emulator_x2_dead_restore_matches_typescript_contract() {
  {
    const std::vector<int> dot_program = {0x00, 0x02, kFrac, kDot, kK1, kCx, kStop};
    const std::vector<int> sign_program = {0x00, 0x02, kFrac, kSignChange, kCx, kStop};
    const std::vector<int> vp_program = {0x05, kVp, kK1, kCx, kStop};

    const std::vector<int> optimized_dot = optimized_codes(dot_program);
    require(optimized_dot == std::vector<int>({0x00, 0x02, kFrac, kCx, kStop}),
            "x2-dead-restore should remove dead dot restore before Cx");
    require(display(optimized_dot) == display(dot_program),
            "x2-dead-restore dot rewrite should preserve emulator display");

    const std::vector<int> optimized_sign = optimized_codes(sign_program);
    require(optimized_sign == std::vector<int>({0x00, 0x02, kFrac, kCx, kStop}),
            "x2-dead-restore should remove dead sign restore before Cx");
    require(display(optimized_sign) == display(sign_program),
            "x2-dead-restore sign rewrite should preserve emulator display");

    const std::vector<int> optimized_vp = optimized_codes(vp_program);
    require(optimized_vp == std::vector<int>({0x05, kCx, kStop}),
            "x2-dead-restore should remove dead VP restore before Cx");
    require(display(optimized_vp) == display(vp_program),
            "x2-dead-restore VP rewrite should preserve emulator display");
  }

  {
    const std::vector<int> program = {kRecall1, kFPi, kDot, kCx, kStop};
    const std::vector<int> optimized = optimized_codes(program);
    require(optimized == program,
            "x2-dead-restore should keep register-only dot restores that can error");
    require(signature(optimized, {{"1", "Г"}}).find("ЕГГ0Г") != std::string::npos,
            "register-only dot restore should preserve the TS-observed hex-register error");
  }

  {
    const std::vector<int> with_vp = {kRecall1, kVp, kK1, kCx, kStop};
    const std::vector<int> without_vp = {kRecall1, kCx, kStop};
    for (const std::string& value : {"Г", "FA", "-FA"}) {
      require(display(without_vp, {{"1", value}}) == display(with_vp, {{"1", value}}),
              "structural VP before Cx should be observationally dead for hex/super values");
    }
  }
}

} // namespace mkpro::tests
