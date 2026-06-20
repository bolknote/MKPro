#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace mkpro {

namespace {

const std::array<std::string, 15> kRegisters = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e",
};

std::string hex_byte(int value) {
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << value;
  return out.str();
}

std::vector<DeliveryMode> all_modes() {
  return {DeliveryMode::Manual, DeliveryMode::Loader, DeliveryMode::Hex};
}

std::vector<DeliveryMode> only_hex() {
  return {DeliveryMode::Hex};
}

std::vector<DeliveryMode> loader_or_hex() {
  return {DeliveryMode::Loader, DeliveryMode::Hex};
}

ConditionalX2Effect direct_conditional_x2_effect() {
  return ConditionalX2Effect{.fallthrough = X2Effect::Affects, .jump = X2Effect::Preserves};
}

ConditionalX2Effect indirect_conditional_x2_effect() {
  return ConditionalX2Effect{.fallthrough = X2Effect::Preserves, .jump = X2Effect::Preserves};
}

OpcodeInfo info(int code, std::string name, std::string keys = "", OpcodeInfo extra = {}) {
  if (keys.empty()) keys = name;
  OpcodeInfo result;
  result.code = code;
  result.hex = hex_byte(code);
  result.name = std::move(name);
  result.keys = std::move(keys);
  result.enterable = extra.enterable.empty() ? all_modes() : std::move(extra.enterable);
  result.takes_address = extra.takes_address;
  result.x2_effect = extra.x2_effect;
  result.conditional_x2_effect = extra.conditional_x2_effect;
  result.stack_effect = extra.stack_effect;
  result.risk = extra.risk;
  return result;
}

OpcodeInfo with_enterable(std::vector<DeliveryMode> enterable) {
  OpcodeInfo result;
  result.enterable = std::move(enterable);
  return result;
}

OpcodeInfo with_effects(X2Effect x2_effect, StackEffect stack_effect) {
  OpcodeInfo result;
  result.x2_effect = x2_effect;
  result.stack_effect = stack_effect;
  return result;
}

std::string replace_all(std::string value, std::string_view from, std::string_view to) {
  if (from.empty()) return value;
  std::size_t pos = 0;
  while ((pos = value.find(from, pos)) != std::string::npos) {
    value.replace(pos, from.size(), to);
    pos += to.size();
  }
  return value;
}

std::string normalize_name(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }

  value = replace_all(std::move(value), "←→", "<->");
  value = replace_all(std::move(value), "→", "->");
  value = replace_all(std::move(value), "←", "<-");
  value = replace_all(std::move(value), "^{", "^");
  value = replace_all(std::move(value), "}", "");
  value = replace_all(std::move(value), "π", "pi");
  value = replace_all(std::move(value), "√", "sqrt");
  value = replace_all(std::move(value), "↻", "reverse");
  value = replace_all(std::move(value), "≠", "!=");
  value = replace_all(std::move(value), "≥", ">=");
  value = replace_all(std::move(value), "≤", "<=");
  value = replace_all(std::move(value), "×", "*");
  value = replace_all(std::move(value), "÷", "/");
  value = replace_all(std::move(value), "−", "-");
  value = replace_all(std::move(value), "∣", "|");
  value = replace_all(std::move(value), "Х", "X");
  value = replace_all(std::move(value), "х", "x");
  value = replace_all(std::move(value), "K", "К");
  value = replace_all(std::move(value), "k", "к");
  value = replace_all(std::move(value), "А", "а");
  value = replace_all(std::move(value), "Б", "б");
  value = replace_all(std::move(value), "В", "в");
  value = replace_all(std::move(value), "Г", "г");
  value = replace_all(std::move(value), "Д", "д");
  value = replace_all(std::move(value), "Е", "е");
  value = replace_all(std::move(value), "З", "з");
  value = replace_all(std::move(value), "И", "и");
  value = replace_all(std::move(value), "К", "к");
  value = replace_all(std::move(value), "Л", "л");
  value = replace_all(std::move(value), "Н", "н");
  value = replace_all(std::move(value), "О", "о");
  value = replace_all(std::move(value), "П", "п");
  value = replace_all(std::move(value), "Р", "р");
  value = replace_all(std::move(value), "С", "с");
  value = replace_all(std::move(value), "Ч", "ч");

  if (value.size() >= 2 && (value[0] == 'F' || value[0] == 'f') &&
      std::isspace(static_cast<unsigned char>(value[1])) == 0) {
    value.insert(value.begin() + 1, ' ');
  } else if (value.rfind("К", 0) == 0 && value.size() > 2 &&
             std::isspace(static_cast<unsigned char>(value[2])) == 0) {
    value.insert(2, " ");
  } else if (value.rfind("к", 0) == 0 && value.size() > 2 &&
             std::isspace(static_cast<unsigned char>(value[2])) == 0) {
    value.insert(2, " ");
  }

  std::string compact;
  bool last_space = false;
  for (char ch : value) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      if (!last_space) compact.push_back(' ');
      last_space = true;
    } else {
      compact.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
      last_space = false;
    }
  }
  return compact;
}

std::vector<std::string> normalize_aliases(const OpcodeInfo& item) {
  std::unordered_set<std::string> aliases;
  aliases.insert(item.name);
  aliases.insert(item.keys);
  aliases.insert(item.hex);
  aliases.insert(replace_all(item.name, "->", "→"));
  aliases.insert(replace_all(item.name, "П->X", "Пх"));
  aliases.insert(replace_all(item.name, "X->П", "хП"));

  if (item.name == "К ∧") {
    aliases.insert("KΛ");
    aliases.insert("К AND");
  }
  if (item.name == "К ∨") {
    aliases.insert("KV");
    aliases.insert("К OR");
  }
  if (item.name == "К ⊕") {
    aliases.insert("K⊕");
    aliases.insert("К XOR");
  }
  if (item.name == "К °->′") {
    aliases.insert("К+");
    aliases.insert("K+");
  }
  if (item.name == "К ИНВ") aliases.insert("Кинв");
  if (item.name == "К ЗН") aliases.insert("Кзн");
  if (item.name == "К |x|") aliases.insert("K|x|");
  if (item.name == "К [x]") aliases.insert("K[x]");
  if (item.name == "К {x}") aliases.insert("K{x}");
  if (item.name == "F Вx") aliases.insert("FBx");
  if (item.name == "С/П") aliases.insert("STOP");
  if (item.name == "В/О") aliases.insert("RTN");

  std::vector<std::string> result;
  result.reserve(aliases.size());
  for (const auto& alias : aliases) result.push_back(normalize_name(alias));
  return result;
}

std::vector<OpcodeInfo> build_opcode_catalog() {
  std::vector<OpcodeInfo> result;
  result.reserve(256);
  for (int code = 0; code <= 0xff; ++code) {
    OpcodeInfo extra = with_enterable(only_hex());
    extra.risk = OpcodeRisk::Undocumented;
    extra.x2_effect = X2Effect::Unknown;
    extra.stack_effect = StackEffect::Unknown;
    result.push_back(info(code, "undoc " + hex_byte(code), "undoc " + hex_byte(code), extra));
  }

  auto set = [&result](OpcodeInfo item) { result.at(static_cast<std::size_t>(item.code)) = item; };

  for (int i = 0; i <= 9; ++i) {
    set(info(i, std::to_string(i), std::to_string(i),
             with_effects(X2Effect::Restores, StackEffect::Barrier)));
  }
  set(info(0x0a, ".", ".", with_effects(X2Effect::Restores, StackEffect::Barrier)));
  set(info(0x0b, "/-/", "/-/", with_effects(X2Effect::Restores, StackEffect::Barrier)));
  set(info(0x0c, "ВП", "ВП", with_effects(X2Effect::Restores, StackEffect::Barrier)));
  set(info(0x0d, "Cx", "Cx", with_effects(X2Effect::Affects, StackEffect::Preserves)));
  set(info(0x0e, "В↑", "В↑", with_effects(X2Effect::Affects, StackEffect::Shifts)));
  set(info(0x0f, "F Вx", "F Вx", with_effects(X2Effect::Affects, StackEffect::Exposes)));

  set(info(0x10, "+", "+", with_effects(X2Effect::Preserves, StackEffect::ConsumeYDrop)));
  set(info(0x11, "-", "-", with_effects(X2Effect::Preserves, StackEffect::ConsumeYDrop)));
  set(info(0x12, "*", "*", with_effects(X2Effect::Preserves, StackEffect::ConsumeYDrop)));
  set(info(0x13, "/", "/", with_effects(X2Effect::Preserves, StackEffect::ConsumeYDrop)));
  set(info(0x14, "<->", "<->", with_effects(X2Effect::Preserves, StackEffect::ConsumeYKeep)));
  set(info(0x15, "F 10^x"));
  set(info(0x16, "F e^x"));
  set(info(0x17, "F lg"));
  set(info(0x18, "F ln"));
  set(info(0x19, "F sin^-1"));
  set(info(0x1a, "F cos^-1"));
  set(info(0x1b, "F tg^-1"));
  set(info(0x1c, "F sin"));
  set(info(0x1d, "F cos"));
  set(info(0x1e, "F tg"));
  {
    OpcodeInfo extra = with_enterable(loader_or_hex());
    extra.risk = OpcodeRisk::Undocumented;
    set(info(0x1f, "empty 1F", "1F", extra));
  }

  set(info(0x20, "F pi", "F pi", with_effects(X2Effect::Preserves, StackEffect::Shifts)));
  set(info(0x21, "F sqrt"));
  set(info(0x22, "F x^2"));
  set(info(0x23, "F 1/x"));
  set(info(0x24, "F x^y", "F x^y",
           with_effects(X2Effect::Preserves, StackEffect::ConsumeYKeep)));
  set(info(0x25, "F reverse", "F reverse",
           with_effects(X2Effect::Preserves, StackEffect::Exposes)));
  set(info(0x26, "К °->′"));
  for (const auto& entry : {std::pair{0x27, "К -"}, std::pair{0x28, "К *"},
                            std::pair{0x29, "К /"}}) {
    OpcodeInfo extra = with_effects(X2Effect::Affects, StackEffect::Barrier);
    extra.risk = OpcodeRisk::Dangerous;
    set(info(entry.first, entry.second, entry.second, extra));
  }
  set(info(0x2a, "К °->′\""));
  for (int code = 0x2b; code <= 0x2e; ++code) {
    OpcodeInfo extra = with_enterable(loader_or_hex());
    extra.risk = OpcodeRisk::Dangerous;
    extra.x2_effect = X2Effect::Affects;
    extra.stack_effect = StackEffect::Barrier;
    set(info(code, "error " + hex_byte(code), hex_byte(code), extra));
  }
  {
    OpcodeInfo extra = with_enterable(loader_or_hex());
    extra.risk = OpcodeRisk::Undocumented;
    set(info(0x2f, "empty 2F", "2F", extra));
  }

  set(info(0x30, "К °<-′\""));
  set(info(0x31, "К |x|"));
  set(info(0x32, "К ЗН"));
  set(info(0x33, "К °<-′"));
  set(info(0x34, "К [x]"));
  set(info(0x35, "К {x}"));
  set(info(0x36, "К max", "К max",
           with_effects(X2Effect::Preserves, StackEffect::ConsumeYKeep)));
  set(info(0x37, "К ∧", "К ∧",
           with_effects(X2Effect::Preserves, StackEffect::ConsumeYKeep)));
  set(info(0x38, "К ∨", "К ∨",
           with_effects(X2Effect::Preserves, StackEffect::ConsumeYKeep)));
  set(info(0x39, "К ⊕", "К ⊕",
           with_effects(X2Effect::Preserves, StackEffect::ConsumeYKeep)));
  set(info(0x3a, "К ИНВ"));
  set(info(0x3b, "К СЧ"));
  {
    OpcodeInfo extra = with_enterable(loader_or_hex());
    extra.risk = OpcodeRisk::Dangerous;
    extra.x2_effect = X2Effect::Affects;
    extra.stack_effect = StackEffect::Barrier;
    set(info(0x3c, "error 3C", "3C", extra));
  }
  {
    OpcodeInfo extra = with_enterable(loader_or_hex());
    extra.risk = OpcodeRisk::Undocumented;
    set(info(0x3d, "alias 3D", "3D", extra));
  }
  {
    OpcodeInfo extra = with_effects(X2Effect::Preserves, StackEffect::ConsumeYKeep);
    extra.enterable = loader_or_hex();
    extra.risk = OpcodeRisk::Undocumented;
    set(info(0x3e, "Y->X", "3E", extra));
  }
  {
    OpcodeInfo extra = with_enterable(loader_or_hex());
    extra.risk = OpcodeRisk::Undocumented;
    set(info(0x3f, "empty 3F", "3F", extra));
  }

  for (int i = 0; i <= 0xe; ++i) {
    const auto index = static_cast<std::size_t>(i);
    set(info(0x40 + i, "X->П " + kRegisters.at(index)));
  }
  {
    OpcodeInfo extra = with_enterable(loader_or_hex());
    extra.risk = OpcodeRisk::Undocumented;
    set(info(0x4f, "X->П 0 alias", "4F", extra));
  }

  set(info(0x50, "С/П", "С/П", with_effects(X2Effect::Affects, StackEffect::Barrier)));
  {
    OpcodeInfo extra;
    extra.takes_address = true;
    set(info(0x51, "БП", "БП", extra));
  }
  set(info(0x52, "В/О", "В/О", with_effects(X2Effect::Affects, StackEffect::Barrier)));
  {
    OpcodeInfo extra;
    extra.takes_address = true;
    set(info(0x53, "ПП", "ПП", extra));
  }
  set(info(0x54, "К НОП"));
  set(info(0x55, "К 1"));
  set(info(0x56, "К 2"));
  for (const auto& entry :
       {std::pair{0x57, "F x!=0"}, std::pair{0x58, "F L2"}, std::pair{0x59, "F x>=0"},
        std::pair{0x5a, "F L3"}, std::pair{0x5b, "F L1"}, std::pair{0x5c, "F x<0"},
        std::pair{0x5d, "F L0"}, std::pair{0x5e, "F x=0"}}) {
    OpcodeInfo extra;
    extra.takes_address = true;
    extra.x2_effect = X2Effect::Unknown;
    extra.conditional_x2_effect = direct_conditional_x2_effect();
    set(info(entry.first, entry.second, entry.second, extra));
  }
  {
    OpcodeInfo extra = with_enterable(loader_or_hex());
    extra.risk = OpcodeRisk::Undocumented;
    extra.stack_effect = StackEffect::Unknown;
    set(info(0x5f, "raw display 5F", "5F", extra));
  }

  for (int i = 0; i <= 0xe; ++i) {
    const auto index = static_cast<std::size_t>(i);
    set(info(0x60 + i, "П->X " + kRegisters.at(index), "П->X " + kRegisters.at(index),
             with_effects(X2Effect::Affects, StackEffect::Shifts)));
  }
  {
    OpcodeInfo extra = with_effects(X2Effect::Affects, StackEffect::Shifts);
    extra.enterable = loader_or_hex();
    extra.risk = OpcodeRisk::Undocumented;
    set(info(0x6f, "П->X 0 alias", "6F", extra));
  }

  const std::vector<std::pair<int, std::string>> indirect_blocks = {
      {0x70, "К x!=0"}, {0x80, "К БП"},   {0x90, "К x>=0"}, {0xa0, "К ПП"},
      {0xb0, "К X->П"}, {0xc0, "К x<0"}, {0xd0, "К П->X"}, {0xe0, "К x=0"},
  };
  for (const auto& [base, name] : indirect_blocks) {
    const bool conditional = base == 0x70 || base == 0x90 || base == 0xc0 || base == 0xe0;
    for (int i = 0; i <= 0xe; ++i) {
      const auto index = static_cast<std::size_t>(i);
      OpcodeInfo extra = with_effects(base == 0xd0 ? X2Effect::Affects : X2Effect::Preserves,
                                      base == 0xd0 ? StackEffect::Shifts : StackEffect::Preserves);
      if (conditional) extra.conditional_x2_effect = indirect_conditional_x2_effect();
      set(info(base + i, name + " " + kRegisters.at(index), name + " " + kRegisters.at(index),
               extra));
    }
    OpcodeInfo alias_extra =
        with_effects(base == 0xd0 ? X2Effect::Affects : X2Effect::Preserves,
                     base == 0xd0 ? StackEffect::Shifts : StackEffect::Preserves);
    alias_extra.enterable = loader_or_hex();
    alias_extra.risk = OpcodeRisk::Undocumented;
    if (conditional) alias_extra.conditional_x2_effect = indirect_conditional_x2_effect();
    set(info(base + 0xf, name + " 0 alias", hex_byte(base + 0xf), alias_extra));
  }

  for (int code = 0xf0; code <= 0xff; ++code) {
    OpcodeInfo extra = with_effects(X2Effect::Affects, StackEffect::Preserves);
    extra.enterable = loader_or_hex();
    extra.risk = OpcodeRisk::Undocumented;
    set(info(code, "F* empty " + hex_byte(code), hex_byte(code), extra));
  }

  return result;
}

std::unordered_map<std::string, const OpcodeInfo*> build_opcode_name_index(
    const std::vector<OpcodeInfo>& catalog) {
  std::unordered_map<std::string, const OpcodeInfo*> result;
  for (const auto& item : catalog) {
    for (const auto& alias : normalize_aliases(item)) {
      result[alias] = &item;
    }
  }
  return result;
}

}  // namespace

const std::vector<OpcodeInfo>& opcode_catalog() {
  static const std::vector<OpcodeInfo> catalog = build_opcode_catalog();
  return catalog;
}

const OpcodeInfo& opcode_by_code(int code) {
  if (code < 0 || code > 0xff) {
    throw std::runtime_error("Unknown opcode " + hex_byte(code));
  }
  return opcode_catalog().at(static_cast<std::size_t>(code));
}

const OpcodeInfo* find_opcode_name(std::string_view name) {
  static const std::unordered_map<std::string, const OpcodeInfo*> by_name =
      build_opcode_name_index(opcode_catalog());
  const auto it = by_name.find(normalize_name(std::string(name)));
  return it == by_name.end() ? nullptr : it->second;
}

int register_index(std::string_view reg) {
  const auto it = std::find(kRegisters.begin(), kRegisters.end(), std::string(reg));
  if (it == kRegisters.end()) {
    throw std::runtime_error("Unknown register " + std::string(reg));
  }
  return static_cast<int>(std::distance(kRegisters.begin(), it));
}

std::string register_from_text(std::string_view text) {
  std::string normalized(text);
  while (!normalized.empty() &&
         std::isspace(static_cast<unsigned char>(normalized.front())) != 0) {
    normalized.erase(normalized.begin());
  }
  while (!normalized.empty() &&
         std::isspace(static_cast<unsigned char>(normalized.back())) != 0) {
    normalized.pop_back();
  }
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  if (normalized == "а") normalized = "a";
  else if (normalized == "в")
    normalized = "b";
  else if (normalized == "с")
    normalized = "c";
  else if (normalized == "д")
    normalized = "d";
  else if (normalized == "е")
    normalized = "e";

  (void)register_index(normalized);
  return normalized;
}

int address_to_opcode(int address) {
  return official_address_to_opcode(address);
}

int code_to_address(int code) {
  return formal_address_ordinal(code);
}

std::string format_address(int address) {
  return format_official_address(address);
}

std::string x2_effect_name(X2Effect effect) {
  switch (effect) {
    case X2Effect::Affects:
      return "affects";
    case X2Effect::Restores:
      return "restores";
    case X2Effect::Preserves:
      return "preserves";
    case X2Effect::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string stack_effect_name(StackEffect effect) {
  switch (effect) {
    case StackEffect::Preserves:
      return "preserves";
    case StackEffect::Shifts:
      return "shifts";
    case StackEffect::ConsumeYDrop:
      return "consume-y-drop";
    case StackEffect::ConsumeYKeep:
      return "consume-y-keep";
    case StackEffect::Exposes:
      return "exposes";
    case StackEffect::Barrier:
      return "barrier";
    case StackEffect::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string opcode_risk_name(OpcodeRisk risk) {
  switch (risk) {
    case OpcodeRisk::Documented:
      return "documented";
    case OpcodeRisk::Undocumented:
      return "undocumented";
    case OpcodeRisk::Dangerous:
      return "dangerous";
  }
  return "documented";
}

}  // namespace mkpro
