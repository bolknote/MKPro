#include "mkpro/core/opcodes.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

int code_for(std::string_view name) {
  const OpcodeInfo* opcode = find_opcode_name(name);
  require(opcode != nullptr, "missing opcode alias: " + std::string(name));
  return opcode->code;
}

bool contains_manual(const std::vector<DeliveryMode>& modes) {
  return std::ranges::find(modes, DeliveryMode::Manual) != modes.end();
}

}  // namespace

void opcode_catalog_matches_typescript_contract() {
  require(opcode_catalog().size() == 256, "opcode catalog should cover all 256 opcodes");
  require(opcode_by_code(0).code == 0, "opcode 00 should exist");
  require(opcode_by_code(0xff).code == 0xff, "opcode FF should exist");

  require(code_for("К СЧ") == 0x3b, "К СЧ lookup should work");
  require(code_for("K СЧ") == 0x3b, "Latin K lookup should work");
  require(code_for("k сч") == 0x3b, "lowercase lookup should work");
  require(code_for("FBx") == 0x0f, "compact FBx lookup should work");
  require(code_for("Х->П 0") == 0x40, "Cyrillic X storage lookup should work");
  require(code_for("X->П 0") == 0x40, "Latin X storage lookup should work");
  require(code_for("3B") == 0x3b, "raw hex 3B lookup should work");
  require(code_for("50") == 0x50, "raw hex 50 lookup should work");

  require(code_to_address(0x99) == 99, "99 should decode to formal address 99");
  require(code_to_address(0xa4) == 104, "A4 should decode to formal address 104");
  require(code_to_address(0x9f) == 105, "9F should decode to formal address 105");
  require(code_to_address(0xc5) == 125, "C5 should decode to formal address 125");
  require(code_to_address(0xff) == 165, "FF should decode to formal address 165");

  const std::vector<std::pair<std::string, int>> aliases = {
      {"←→", 0x14},       {"F 10^{x}", 0x15}, {"F e^{x}", 0x16},
      {"F sin^{-1}", 0x19}, {"F cos^{-1}", 0x1a}, {"F tg^{-1}", 0x1b},
      {"F π", 0x20},      {"F√", 0x21},      {"F x^{2}", 0x22},
      {"F x^{y}", 0x24},  {"F↻", 0x25},      {"К°→′", 0x26},
      {"К+", 0x26},       {"К−", 0x27},       {"К×", 0x28},
      {"К÷", 0x29},       {"К°→′\"", 0x2a},  {"К°←′\"", 0x30},
      {"К∣x∣", 0x31},     {"К°←′", 0x33},    {"К∧", 0x37},
      {"К∨", 0x38},       {"F x≠0", 0x57},   {"F x≥0", 0x59},
      {"К x≠0 0", 0x70},  {"К x≥0 e", 0x9e},
  };
  for (const auto& [name, code] : aliases) {
    require(code_for(name) == code, "command-page alias mismatch: " + name);
  }

  const auto& undoc = opcode_by_code(0xf7);
  require(undoc.risk == OpcodeRisk::Undocumented, "F7 should remain undocumented");
  require(!contains_manual(undoc.enterable), "F7 should not be manually enterable");

  const auto& raw_display = opcode_by_code(0x5f);
  require(raw_display.name == "raw display 5F", "5F should be modeled as raw display");
  require(raw_display.risk == OpcodeRisk::Undocumented, "5F should be undocumented");

  require(opcode_by_code(0x00).x2_effect == X2Effect::Restores, "00 X2 effect");
  require(opcode_by_code(0x0a).x2_effect == X2Effect::Restores, "0A X2 effect");
  require(opcode_by_code(0x0b).x2_effect == X2Effect::Restores, "0B X2 effect");
  require(opcode_by_code(0x0c).x2_effect == X2Effect::Restores, "0C X2 effect");
  require(opcode_by_code(0x10).x2_effect == X2Effect::Preserves, "10 X2 effect");
  require(opcode_by_code(0x22).x2_effect == X2Effect::Preserves, "22 X2 effect");
  require(opcode_by_code(0x40).x2_effect == X2Effect::Preserves, "40 X2 effect");
  require(opcode_by_code(0xb0).x2_effect == X2Effect::Preserves, "B0 X2 effect");
  require(opcode_by_code(0xe0).x2_effect == X2Effect::Preserves, "E0 X2 effect");
  require(opcode_by_code(0x0d).x2_effect == X2Effect::Affects, "0D X2 effect");
  require(opcode_by_code(0x52).x2_effect == X2Effect::Affects, "52 X2 effect");
  require(opcode_by_code(0x60).x2_effect == X2Effect::Affects, "60 X2 effect");
  require(opcode_by_code(0xd0).x2_effect == X2Effect::Affects, "D0 X2 effect");
  require(opcode_by_code(0xf0).x2_effect == X2Effect::Affects, "F0 X2 effect");
  require(opcode_by_code(0x27).x2_effect == X2Effect::Affects, "27 X2 effect");
  require(opcode_by_code(0x57).x2_effect == X2Effect::Unknown, "57 X2 effect");
  require(opcode_by_code(0x5d).x2_effect == X2Effect::Unknown, "5D X2 effect");
  require(opcode_by_code(0x57).conditional_x2_effect->fallthrough == X2Effect::Affects,
          "57 fallthrough should affect X2");
  require(opcode_by_code(0x57).conditional_x2_effect->jump == X2Effect::Preserves,
          "57 jump should preserve X2");
  require(opcode_by_code(0x70).conditional_x2_effect->fallthrough == X2Effect::Preserves,
          "70 fallthrough should preserve X2");
  require(opcode_by_code(0xe0).conditional_x2_effect->jump == X2Effect::Preserves,
          "E0 jump should preserve X2");

  require(opcode_by_code(0x00).stack_effect == StackEffect::Barrier, "00 stack effect");
  require(opcode_by_code(0x0a).stack_effect == StackEffect::Barrier, "0A stack effect");
  require(opcode_by_code(0x0c).stack_effect == StackEffect::Barrier, "0C stack effect");
  require(opcode_by_code(0x0e).stack_effect == StackEffect::Shifts, "0E stack effect");
  require(opcode_by_code(0x20).stack_effect == StackEffect::Shifts, "20 stack effect");
  require(opcode_by_code(0x60).stack_effect == StackEffect::Shifts, "60 stack effect");
  require(opcode_by_code(0xd0).stack_effect == StackEffect::Shifts, "D0 stack effect");
  require(opcode_by_code(0x10).stack_effect == StackEffect::ConsumeYDrop, "10 stack effect");
  require(opcode_by_code(0x13).stack_effect == StackEffect::ConsumeYDrop, "13 stack effect");
  require(opcode_by_code(0x14).stack_effect == StackEffect::ConsumeYKeep, "14 stack effect");
  require(opcode_by_code(0x24).stack_effect == StackEffect::ConsumeYKeep, "24 stack effect");
  require(opcode_by_code(0x36).stack_effect == StackEffect::ConsumeYKeep, "36 stack effect");
  require(opcode_by_code(0x3e).stack_effect == StackEffect::ConsumeYKeep, "3E stack effect");
  require(opcode_by_code(0x35).stack_effect == StackEffect::Preserves, "35 stack effect");
  require(opcode_by_code(0x3d).stack_effect == StackEffect::Preserves, "3D stack effect");
  require(opcode_by_code(0x0f).stack_effect == StackEffect::Exposes, "0F stack effect");
  require(opcode_by_code(0x25).stack_effect == StackEffect::Exposes, "25 stack effect");
  require(opcode_by_code(0x5f).stack_effect == StackEffect::Unknown, "5F stack effect");
}

}  // namespace mkpro::tests
