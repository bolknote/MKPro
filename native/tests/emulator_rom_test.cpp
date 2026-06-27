#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "mkpro/emulator/rom.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string hex_byte(int value) {
  const char* digits = "0123456789ABCDEF";
  std::string out;
  out.push_back(digits[(value >> 4) & 0x0f]);
  out.push_back(digits[value & 0x0f]);
  return out;
}

std::vector<std::string> hex_bytes(const std::vector<int>& values) {
  std::vector<std::string> out;
  for (const int value : values)
    out.push_back(hex_byte(value));
  return out;
}

int sync4(int command_word) { return (command_word >> 14) & 0xff; }

std::string compact(std::string value) {
  std::string out;
  for (const char ch : value) {
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
      out.push_back(ch);
  }
  return out;
}

bool register_value_is_zero_padded(const std::string& value, const std::string& digits) {
  std::string text = compact(value);
  const std::size_t comma = text.find(',');
  if (comma == std::string::npos)
    return false;
  std::string mantissa = text.substr(0, comma);
  while (mantissa.size() > digits.size() && mantissa.front() == '0')
    mantissa.erase(mantissa.begin());
  return mantissa == digits;
}

std::string mk61_hex_literal(const std::string& text) {
  std::string out;
  for (const char ch : text) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))) {
      case 'A':
        out.push_back('-');
        break;
      case 'B':
        out.push_back('L');
        break;
      case 'C':
        out += "С";
        break;
      case 'D':
        out += "Г";
        break;
      case 'E':
        out += "Е";
        break;
      case 'F':
        out.push_back('_');
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

struct SingleOpcodeRun {
  std::string display;
  std::string x;
  std::string r0;
  bool stopped = false;
  int frames = 0;
};

SingleOpcodeRun run_single_opcode(int code, bool extended = true, const std::string& x = "5",
                                  const std::string& y = "") {
  emulator::MK61 calc(emulator::MK61Options{.extended = extended});
  calc.set_register("x", x);
  if (!y.empty())
    calc.set_register("y", y);
  calc.load_program({code, 0x50});
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(500, 5);
  return SingleOpcodeRun{
      .display = calc.display_text(),
      .x = calc.read_register("x"),
      .r0 = calc.read_register("0"),
      .stopped = run.stopped,
      .frames = run.frames,
  };
}

struct ProgramRun {
  emulator::MK61 calc;
  std::string display;
  std::string pc;
  bool stopped = false;
  int frames = 0;
};

ProgramRun run_program_with_registers(const std::vector<int>& codes,
                                      const std::map<std::string, std::string>& registers) {
  emulator::MK61 calc;
  calc.load_program(codes);
  for (const auto& [reg, value] : registers)
    calc.set_register(reg, value);
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(700, 5);
  const std::string display = calc.display_text();
  const std::string pc = calc.program_counter();
  return ProgramRun{
      .calc = std::move(calc),
      .display = display,
      .pc = pc,
      .stopped = run.stopped,
      .frames = run.frames,
  };
}

ProgramRun run_program(const std::vector<int>& codes) {
  return run_program_with_registers(codes, {});
}

std::vector<std::string> run_program_repeated(const std::vector<int>& codes, int count) {
  emulator::MK61 calc;
  calc.load_program(codes);
  std::vector<std::string> displays;
  for (int index = 0; index < count; ++index) {
    calc.press_sequence({"В/О", "С/П"});
    calc.run_until_stable(100, 3);
    displays.push_back(compact(calc.display_text()));
  }
  return displays;
}

void preload_negative_zero_one(emulator::MK61& calc, const std::string& reg) {
  const int register_code = register_index(reg);
  calc.load_program({
      0x54, 0x01, 0x03, 0x40 + register_code, 0x01, 0x08, 0x38, 0x35, 0x0b,
      0x0c, 0x02, 0x15, 0x0e, 0x0c, 0x0b, 0x05, 0x00, 0x40 + register_code,
      0x50,
  });
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(1000, 5);
}

std::string run_negative_zero_threshold_selector(const std::string& value) {
  emulator::MK61 calc;
  preload_negative_zero_one(calc, "9");
  calc.set_register("0", value);
  calc.load_program({0x69, 0x60, 0x12, 0x0e, 0x32, 0x50});
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(500, 5);
  return calc.read_register("x");
}

}  // namespace

void emulator_rom_tables_match_typescript_contract() {
  const auto chips = emulator::rom_chips();
  require(chips.size() == 4, "native ROM table should expose all rom.cjs chips");
  for (const auto& chip : chips) {
    require(chip.commands.size() == 256, std::string(chip.name) + " command ROM length");
    require(chip.sync_programs.size() == 1152,
            std::string(chip.name) + " sync program ROM length");
    require(chip.microcommands.size() == 68, std::string(chip.name) + " microcommand ROM length");
  }

  const std::array<emulator::RomChip, 3> mk61_chips = {
      emulator::RomChip::Ik1302,
      emulator::RomChip::Ik1303,
      emulator::RomChip::Ik1306,
  };
  std::vector<int> differing_opcodes;
  for (int opcode = 0; opcode <= 0xff; ++opcode) {
    std::set<int> words;
    for (const auto chip : mk61_chips)
      words.insert(emulator::rom_chip(chip).commands[static_cast<std::size_t>(opcode)]);
    if (words.size() > 1U)
      differing_opcodes.push_back(opcode);
  }
  require(differing_opcodes.size() == 256,
          "each user opcode should differ across at least one MK-61 command ROM");

  std::vector<int> zero_on_dispatcher;
  const auto& dispatcher = emulator::rom_chip(emulator::RomChip::Ik1302);
  const auto& later = emulator::rom_chip(emulator::RomChip::Ik1303);
  for (int opcode = 0; opcode <= 0xff; ++opcode) {
    if (dispatcher.commands[static_cast<std::size_t>(opcode)] == 0)
      zero_on_dispatcher.push_back(opcode);
  }
  require(hex_bytes(zero_on_dispatcher) ==
              std::vector<std::string>{"0B", "0C", "0D", "0E", "3B", "3C", "3D", "3E"},
          "dispatcher-zero opcode list should match TypeScript ROM discoveries");
  for (const int opcode : zero_on_dispatcher) {
    require(later.commands[static_cast<std::size_t>(opcode)] != 0,
            "dispatcher-zero opcode should have later-chip microcode: " + hex_byte(opcode));
  }

  for (const int opcode : {0x34, 0x3b, 0x3d, 0x3e}) {
    const SingleOpcodeRun extended = run_single_opcode(opcode, true, "5");
    const SingleOpcodeRun basic = run_single_opcode(opcode, false, "5");
    require(extended.display.find("ЕГГ") == std::string::npos,
            "selected extended opcode should work in MK-61 mode: " + hex_byte(opcode));
    require(basic.display.find("ЕГГ") != std::string::npos,
            "selected extended opcode should trap in basic mode: " + hex_byte(opcode));
  }

  for (const auto chip : mk61_chips) {
    const auto& rom = emulator::rom_chip(chip);
    require(rom.commands[0x2a] != rom.commands[0x3d],
            "2A and 3D should remain ROM-distinct aliases");
    require(rom.commands[0x40] != rom.commands[0x4f],
            "40 and 4F should remain ROM-distinct aliases");
  }
  require(run_single_opcode(0x2a, true, "1,5").x == run_single_opcode(0x3d, true, "1,5").x,
          "2A and 3D should stay behaviorally equivalent for checked input");
  require(run_single_opcode(0x40, true, "99").r0 == "99,",
          "X->P 0 should store X in R0");
  require(run_single_opcode(0x4f, true, "99").r0 == "99,",
          "X->P F alias should store X in R0");

  const SingleOpcodeRun raw_display = run_single_opcode(0x5f);
  require(raw_display.stopped, "5F should stop in the ROM wrapper");
  require(raw_display.frames < 20, "5F should not be modeled as a non-terminating hang");
  require(raw_display.display == "0,5000000000,0,", "5F display should match TS wrapper");

  require(run_negative_zero_threshold_selector("0") == "0,",
          "1|-00 selector should keep zero below threshold");
  require(run_negative_zero_threshold_selector("0.7") == "0,",
          "1|-00 selector should keep 0.7 below threshold");
  require(run_negative_zero_threshold_selector("0.999") == "0,",
          "1|-00 selector should keep 0.999 below threshold");
  require(run_negative_zero_threshold_selector("1") == "1,",
          "1|-00 selector should flip at one");
  require(run_negative_zero_threshold_selector("2") == "1,",
          "1|-00 selector should stay selected above one");

  {
    emulator::MK61 calc;
    auto draw_randoms = [&](int count) {
      calc.load_program({0x3b, 0x50});
      std::vector<std::string> displays;
      for (int index = 0; index < count; ++index) {
        calc.press_sequence({"В/О", "С/П"});
        calc.run_until_stable(100, 3);
        displays.push_back(compact(calc.display_text()));
      }
      return displays;
    };

    const std::vector<std::string> first = draw_randoms(8);
    require(std::ranges::find(first, "1,") == first.end(),
            "К СЧ first sample should not contain ordinary 1");
    require(std::ranges::find(first, "0,") == first.end(),
            "К СЧ first sample should not contain ordinary 0");
    draw_randoms(8);

    calc.load_program({0x36, 0x50});
    calc.set_register("y", "0");
    calc.press_sequence({"В/О", "С/П"});
    calc.run_until_stable(100, 3);
    require(draw_randoms(8) == first, "К max with Y=0 should reset the random stream");

    emulator::MK61 hex_y;
    hex_y.load_program({0x3b, 0x50});
    hex_y.set_register("y", "_,0");
    hex_y.press_sequence({"В/О", "С/П"});
    hex_y.run_until_stable(100, 3);
    require(compact(hex_y.display_text()) == "0,", "hex-Y random edge case should return zero");
  }

  require(run_program_repeated({0x3b, 0x09, 0x12, 0x34, 0x01, 0x10, 0x50}, 6) ==
              std::vector<std::string>{"4,", "4,", "4,", "4,", "4,", "4,"},
          "integerizing К СЧ with К [x] should enter the TS short cycle");
  require(run_program_repeated({0x3b, 0x09, 0x12, 0x0e, 0x35, 0x11, 0x01, 0x10, 0x50},
                               6) ==
              std::vector<std::string>{"4,", "6,", "9,", "1,", "8,", "9,"},
          "flooring random via x-frac(x) should keep integer draws moving");

  std::set<int> dispatcher_f_words;
  std::set<int> extension_f_words;
  const auto& extension = emulator::rom_chip(emulator::RomChip::Ik1306);
  for (int opcode = 0xf0; opcode <= 0xff; ++opcode) {
    dispatcher_f_words.insert(dispatcher.commands[static_cast<std::size_t>(opcode)]);
    extension_f_words.insert(extension.commands[static_cast<std::size_t>(opcode)]);
    require(dispatcher.commands[static_cast<std::size_t>(opcode)] !=
                extension.commands[static_cast<std::size_t>(opcode)],
            "F0..FF dispatcher and extension words should differ: " + hex_byte(opcode));
  }
  require(dispatcher_f_words.size() > 1U, "dispatcher F0..FF words should not collapse");
  require(extension_f_words.size() > 1U, "extension F0..FF words should not collapse");

  require(sync4(dispatcher.commands[0x51]) > 31, "BP should use wide sync4 dark path");
  require(sync4(dispatcher.commands[0x52]) > 31, "B/O should use wide sync4 dark path");
  require(sync4(dispatcher.commands[0x80]) > 31, "indirect flow should use wide sync4 dark path");

  for (const auto [formal, actual] : {std::pair{0xa8, 3}, std::pair{0xc5, 13}}) {
    std::vector<int> program(105, 0x50);
    program[0] = 0x51;
    program[1] = formal;
    program[static_cast<std::size_t>(actual)] = 0x07;
    program[static_cast<std::size_t>(actual + 1)] = 0x50;
    const ProgramRun result = run_program(program);
    require(result.stopped, "formal side branch should stop");
    require(result.display.find("7") != std::string::npos,
            "formal side branch should execute physical cell " + std::to_string(actual));
  }
  require(formal_address_info(0xc5).actual == 13 && formal_address_info(0xc6).actual == 14,
          "formal dark addressed source lines should map to physical cells");

  {
    std::vector<int> program(105, 0x50);
    program[0] = 0x51;
    program[1] = 0xfb;
    program[49] = 0x08;
    program[50] = 0x09;
    program[2] = 0x50;
    const ProgramRun result = run_program(program);
    require(result.stopped, "super-dark formal branch should stop");
    require(result.display.find("8") != std::string::npos,
            "super-dark formal branch should execute mapped entry");
    require(result.display.find("9") == std::string::npos,
            "super-dark formal branch should execute only one command before continuation");
  }

  {
    std::vector<int> program(105, 0x50);
    program[0] = 0x01;
    program[1] = 0x02;
    program[2] = 0x47;
    program[3] = 0x87;
    program[4] = 0x09;
    program[12] = 0x07;
    program[13] = 0x50;
    const ProgramRun result = run_program(program);
    require(result.stopped, "stable-register indirect flow should stop");
    require(result.display.find("7") != std::string::npos,
            "stable-register indirect flow should execute target");
    require(result.display.find("9") == std::string::npos,
            "stable-register indirect flow should skip fallthrough marker");
  }

  {
    const ProgramRun recall =
        run_program({0x05, 0x42, 0x0d, 0x02, 0x47, 0xd7, 0x50});
    const ProgramRun store =
        run_program({0x02, 0x47, 0x0d, 0x09, 0xb7, 0x0d, 0xd7, 0x50});
    require(recall.stopped && recall.display.find("5") != std::string::npos,
            "stable-register indirect recall should read target memory");
    require(store.stopped && store.display.find("9") != std::string::npos,
            "stable-register indirect store should write target memory");
  }

  for (const auto& [selector, value] :
       std::vector<std::pair<std::string, std::string>>{{"0", "13"}, {"4", "11"}, {"7", "12"}}) {
    const auto model =
        core::evaluate_indirect_address(selector, value, core::IndirectOperationKind::Flow);
    require(model.has_value() && model->actual_flow_target == 12,
            "integer flow selector model should target actual cell 12");
    std::vector<int> program(105, 0x50);
    program[0] = 0x80 + register_index(selector);
    program[1] = 0x09;
    program[12] = 0x07;
    program[13] = 0x50;
    ProgramRun result = run_program_with_registers(program, {{selector, value}});
    require(result.stopped && result.display.find("7") != std::string::npos,
            "integer flow selector should execute target cell");
    require(result.display.find("9") == std::string::npos,
            "integer flow selector should skip fallthrough marker");
    require(register_value_is_zero_padded(result.calc.read_register(selector), "12"),
            "integer flow selector should leave transformed 12");
  }

  for (const auto& [selector, value] :
       std::vector<std::pair<std::string, std::string>>{{"0", "3"}, {"4", "1"}, {"7", "2"}}) {
    const auto model =
        core::evaluate_indirect_address(selector, value, core::IndirectOperationKind::Memory);
    require(model.has_value() && model->memory_target == 2,
            "integer memory selector model should target R2");
    ProgramRun result =
        run_program_with_registers({0xd0 + register_index(selector), 0x50},
                                   {{selector, value}, {"2", "7"}});
    require(result.stopped && result.display.find("7") != std::string::npos,
            "integer memory selector should recall R2");
    require(register_value_is_zero_padded(result.calc.read_register(selector), "2"),
            "integer memory selector should leave transformed 2");
  }

  {
    const auto model =
        core::evaluate_indirect_address("7", "-7", core::IndirectOperationKind::Memory);
    require(model.has_value() && model->memory_target == 1,
            "negative integer memory selector model should target R1");
    ProgramRun result = run_program_with_registers({0xd7, 0x50}, {{"7", "-7"}, {"1", "8"}});
    require(result.stopped && result.display.find("8") != std::string::npos,
            "negative integer memory selector should recall R1");
    require(compact(result.calc.read_register("7")).starts_with("-99999997,"),
            "negative integer memory selector should leave transformed sentinel");
  }

  {
    const auto flow_model =
        core::evaluate_indirect_address("0", "0,5", core::IndirectOperationKind::Flow);
    const auto recall_model =
        core::evaluate_indirect_address("0", "0,5", core::IndirectOperationKind::Memory);
    require(flow_model.has_value() && flow_model->actual_flow_target == 99,
            "fractional R0 flow model should target actual cell 99");
    require(recall_model.has_value() && recall_model->memory_target == 3,
            "fractional R0 memory model should target R3");
    std::vector<int> flow_program(105, 0x50);
    flow_program[0] = 0x80;
    flow_program[99] = 0x06;
    flow_program[100] = 0x50;
    ProgramRun flow = run_program_with_registers(flow_program, {{"0", "0,5"}});
    ProgramRun recall = run_program_with_registers({0xd0, 0x50}, {{"0", "0,5"}, {"3", "8"}});
    require(flow.stopped && flow.display.find("6") != std::string::npos,
            "fractional R0 flow should execute actual cell 99");
    require(compact(flow.calc.read_register("0")).starts_with("-99999999,"),
            "fractional R0 flow should leave sentinel");
    require(recall.stopped && recall.display.find("8") != std::string::npos,
            "fractional R0 memory should recall R3");
    require(compact(recall.calc.read_register("0")).starts_with("-99999999,"),
            "fractional R0 memory should leave sentinel");
  }

  for (int offset = 0; offset <= 5; ++offset) {
    const std::string value = hex_byte(0xfa + offset);
    const auto model =
        core::evaluate_indirect_address("7", value, core::IndirectOperationKind::Flow);
    require(model.has_value() && model->super_dark.has_value(),
            "FA..FF super-dark model should evaluate");
    require(model->super_dark->formal == 0xfa + offset,
            "super-dark formal value should match");
    require(model->super_dark->entry_address == 48 + offset,
            "super-dark entry address should match");
    require(model->super_dark->continuation_address == 1 + offset,
            "super-dark continuation address should match");

    const int marker = offset + 1;
    std::vector<int> program(105, 0x50);
    program[0] = 0x87;
    program[static_cast<std::size_t>(1 + offset)] = 0x50;
    program[static_cast<std::size_t>(48 + offset)] = marker;
    program[static_cast<std::size_t>(49 + offset)] = 0x09;
    ProgramRun result = run_program_with_registers(program, {{"7", mk61_hex_literal(value)}});
    require(result.stopped && result.display.find(std::to_string(marker)) != std::string::npos,
            "FA..FF super-dark dispatch should execute marker");
    require(result.display.find("9") == std::string::npos,
            "FA..FF super-dark dispatch should skip next entry command");
  }
}

}  // namespace mkpro::tests
