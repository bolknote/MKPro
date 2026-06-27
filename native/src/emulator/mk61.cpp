#include "mkpro/emulator/mk61.hpp"

#include "mkpro/emulator/rom.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace mkpro::emulator {

namespace {

constexpr std::array<std::string_view, 16> kDigitSymbols = {
    "0", "1", "2", "3", "4", "5", "6", "7",
    "8", "9", "-", "L", "С", "Г", "Е", " ",
};

constexpr std::array<std::array<int, 2>, 15> kMemoryPageAddresses = {{
    {{1, 41}},  {{1, 83}},  {{1, 125}}, {{1, 167}}, {{1, 209}},
    {{1, 251}}, {{2, 41}},  {{2, 83}},  {{2, 125}}, {{2, 167}},
    {{2, 209}}, {{2, 251}}, {{3, 41}},  {{4, 41}},  {{5, 41}},
}};

constexpr std::array<std::array<int, 15>, 3> kPagePermutationsExtended = {{
    {{1, 2, 3, 4, 5, 14, 13, 12, 6, 7, 8, 9, 10, 11, 0}},
    {{10, 11, 6, 7, 2, 3, 4, 5, 0, 1, 14, 13, 12, 8, 9}},
    {{14, 13, 12, 10, 11, 6, 7, 8, 9, 4, 5, 0, 1, 2, 3}},
}};

constexpr std::array<std::array<int, 15>, 3> kPagePermutationsBasic = {{
    {{1, 2, 3, 4, 5, 13, 12, 6, 7, 8, 9, 10, 11, 0}},
    {{3, 4, 5, 0, 1, 13, 12, 8, 9, 10, 11, 6, 7, 2}},
    {{5, 0, 1, 2, 3, 13, 12, 10, 11, 6, 7, 8, 9, 4}},
}};

constexpr std::array<std::array<int, 2>, 15> kStackAddresses = {{
    {{1, 34}},  {{1, 76}},  {{1, 118}}, {{1, 160}}, {{1, 202}},
    {{1, 244}}, {{2, 34}},  {{2, 76}},  {{2, 118}}, {{2, 160}},
    {{2, 202}}, {{2, 244}}, {{3, 34}},  {{4, 34}},  {{5, 34}},
}};

constexpr std::array<std::array<int, 5>, 3> kStackPermutationsExtended = {{
    {{8, 9, 10, 11, 0}},
    {{14, 13, 12, 8, 9}},
    {{5, 0, 1, 2, 3}},
}};

constexpr std::array<std::array<int, 5>, 3> kStackPermutationsBasic = {{
    {{8, 9, 10, 11, 0}},
    {{10, 11, 6, 7, 2}},
    {{6, 7, 8, 9, 4}},
}};

std::size_t idx(int value) {
  if (value < 0)
    throw std::out_of_range("negative emulator index");
  return static_cast<std::size_t>(value);
}

std::string ascii_upper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return value;
}

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

void replace_all(std::string& value, std::string_view from, std::string_view to) {
  if (from.empty())
    return;
  std::size_t pos = 0;
  while ((pos = value.find(from, pos)) != std::string::npos) {
    value.replace(pos, from.size(), to);
    pos += to.size();
  }
}

std::string normalize_mnemonic(std::string token) {
  token = trim_ascii(std::move(token));
  replace_all(token, "\xC2\xA0", "");
  replace_all(token, " ", "");
  replace_all(token, "\t", "");
  replace_all(token, "\n", "");
  replace_all(token, "\r", "");
  replace_all(token, "−", "-");
  replace_all(token, "–", "-");
  replace_all(token, "—", "-");
  replace_all(token, "⋅", "*");
  replace_all(token, "×", "*");
  replace_all(token, "÷", "/");
  replace_all(token, ":", "/");
  replace_all(token, "К", "K");
  replace_all(token, "к", "K");
  replace_all(token, "Х", "X");
  replace_all(token, "х", "X");
  replace_all(token, "x", "X");
  token = ascii_upper(std::move(token));
  replace_all(token, "≥", ">=");
  replace_all(token, "⩾", ">=");
  replace_all(token, "≠", "#");
  replace_all(token, "<>", "#");
  replace_all(token, "!=", "#");
  return token;
}

std::pair<int, int> normalize_key(const std::string& key) {
  static const std::array<std::pair<std::string_view, std::pair<int, int>>, 43> kKeys = {{
      {"0", {2, 1}},     {"1", {3, 1}},      {"2", {4, 1}},
      {"3", {5, 1}},     {"4", {6, 1}},      {"5", {7, 1}},
      {"6", {8, 1}},     {"7", {9, 1}},      {"8", {10, 1}},
      {"9", {11, 1}},    {",", {7, 8}},      {".", {7, 8}},
      {"/-/", {8, 8}},   {"+/-", {8, 8}},    {"ВП", {9, 8}},
      {"EXP", {9, 8}},   {"CX", {10, 8}},    {"СХ", {10, 8}},
      {"СX", {10, 8}},   {"В↑", {11, 8}},    {"ENTER", {11, 8}},
      {"+", {2, 8}},     {"-", {3, 8}},      {"*", {4, 8}},
      {"/", {5, 8}},     {"↔", {6, 8}},      {"X↔Y", {6, 8}},
      {"F", {11, 9}},    {"K", {10, 9}},     {"П→X", {8, 9}},
      {"ПX", {8, 9}},    {"ИП", {8, 9}},     {"X→П", {6, 9}},
      {"XП", {6, 9}},    {"П", {6, 9}},      {"БП", {3, 9}},
      {"ПП", {5, 9}},    {"В/О", {4, 9}},    {"В/0", {4, 9}},
      {"С/П", {2, 9}},   {"ШГ→", {7, 9}},    {"ШГ←", {9, 9}},
      {"K", {10, 9}},
  }};
  const std::string normalized = normalize_mnemonic(key);
  for (const auto& [name, code] : kKeys) {
    if (normalize_mnemonic(std::string(name)) == normalized)
      return code;
  }
  throw std::invalid_argument("unknown MK-61 key: " + key);
}

int parse_angle_mode(const std::string& mode) {
  std::string normalized = ascii_upper(trim_ascii(mode));
  replace_all(normalized, "Р", "R");
  if (normalized == "RAD" || normalized == "R" || normalized == "RADIAN" ||
      normalized == "RADIANS") {
    return 10;
  }
  if (normalized == "DEG" || normalized == "DEGREE" || normalized == "DEGREES" ||
      normalized == "Г") {
    return 11;
  }
  if (normalized == "GRAD" || normalized == "GRADS" || normalized == "ГРД") {
    return 12;
  }
  if (normalized == "10" || normalized == "11" || normalized == "12")
    return std::stoi(normalized);
  throw std::invalid_argument("unknown angle mode: " + mode);
}

struct RegisterTarget {
  bool memory = true;
  int index = 0;
};

RegisterTarget normalize_register(std::string text) {
  text = ascii_upper(trim_ascii(std::move(text)));
  if (!text.empty() && (text.front() == 'R' || text.starts_with("Р")))
    text.erase(0, text.front() == 'R' ? 1U : std::string("Р").size());
  replace_all(text, "А", "A");
  replace_all(text, "В", "B");
  replace_all(text, "С", "C");
  replace_all(text, "Д", "D");
  replace_all(text, "Е", "E");
  if (text == "X1")
    return RegisterTarget{.memory = false, .index = 0};
  if (text == "X")
    return RegisterTarget{.memory = false, .index = 1};
  if (text == "Y")
    return RegisterTarget{.memory = false, .index = 2};
  if (text == "Z")
    return RegisterTarget{.memory = false, .index = 3};
  if (text == "T")
    return RegisterTarget{.memory = false, .index = 4};
  if (text.size() == 1U) {
    const char ch = text.front();
    const int index = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'A' && ch <= 'E' ? ch - 'A' + 10 : -1;
    if (index >= 0 && index <= 14)
      return RegisterTarget{.memory = true, .index = index};
  }
  throw std::invalid_argument("unknown MK-61 register: " + text);
}

struct ParsedNumber {
  bool mantissa_negative = false;
  std::vector<int> mantissa;
  bool exponent_negative = false;
  int exponent = 0;
};

bool consume_utf8(std::string_view text, std::size_t& pos, std::string_view token) {
  if (text.substr(pos, token.size()) != token)
    return false;
  pos += token.size();
  return true;
}

int nibble_from_text(std::string_view text, std::size_t& pos) {
  const unsigned char ch = static_cast<unsigned char>(text[pos]);
  if (ch >= '0' && ch <= '9') {
    ++pos;
    return static_cast<int>(ch - '0');
  }
  if (ch == '-' || ch == 'A') {
    ++pos;
    return 10;
  }
  if (ch == 'B' || ch == 'L') {
    ++pos;
    return 11;
  }
  if (ch == 'C') {
    ++pos;
    return 12;
  }
  if (ch == 'D') {
    ++pos;
    return 13;
  }
  if (ch == 'E') {
    ++pos;
    return 14;
  }
  if (ch == 'F' || ch == '_') {
    ++pos;
    return 15;
  }
  if (consume_utf8(text, pos, "А"))
    return 10;
  if (consume_utf8(text, pos, "В"))
    return 11;
  if (consume_utf8(text, pos, "С"))
    return 12;
  if (consume_utf8(text, pos, "Г") || consume_utf8(text, pos, "Д"))
    return 13;
  if (consume_utf8(text, pos, "Е"))
    return 14;
  throw std::invalid_argument("cannot parse MK-61 number digit");
}

std::vector<int> nibbles_from_text(std::string_view text) {
  std::vector<int> nibbles;
  for (std::size_t pos = 0; pos < text.size();)
    nibbles.push_back(nibble_from_text(text, pos));
  return nibbles;
}

ParsedNumber parse_number_literal(std::string value) {
  std::string text = ascii_upper(trim_ascii(std::move(value)));
  replace_all(text, ".", ",");

  bool negative = false;
  if (text.size() > 1U && text.front() == '-') {
    negative = true;
    text.erase(text.begin());
  }

  std::string exponent_text;
  bool exponent_negative = false;
  const std::size_t exponent_pos = text.find('E');
  if (exponent_pos != std::string::npos && exponent_pos + 1U < text.size()) {
    std::string maybe_exponent = text.substr(exponent_pos + 1U);
    bool ok = true;
    if (!maybe_exponent.empty() && maybe_exponent.front() == '-') {
      exponent_negative = true;
      maybe_exponent.erase(maybe_exponent.begin());
    }
    ok = !maybe_exponent.empty() &&
         std::all_of(maybe_exponent.begin(), maybe_exponent.end(),
                     [](unsigned char ch) { return std::isdigit(ch) != 0; });
    if (ok) {
      exponent_text = maybe_exponent;
      text.erase(exponent_pos);
    }
  }

  const std::size_t comma = text.find(',');
  const std::string integer_part = comma == std::string::npos ? text : text.substr(0, comma);
  const std::string fractional_part = comma == std::string::npos ? "" : text.substr(comma + 1U);
  if (integer_part.empty())
    throw std::invalid_argument("cannot parse MK-61 number literal: empty integer part");

  std::vector<int> integer_nibbles = nibbles_from_text(integer_part);
  std::vector<int> fractional_nibbles = nibbles_from_text(fractional_part);
  int exponent = exponent_text.empty() ? 0 : std::stoi(exponent_text);
  const int integer_digits = static_cast<int>(integer_nibbles.size());
  exponent += (integer_digits - 1) * (exponent_negative ? -1 : 1);
  if (exponent_negative)
    exponent = 100 - exponent;
  if (exponent >= 100)
    throw std::invalid_argument("number exponent is out of range");

  std::vector<int> mantissa = std::move(integer_nibbles);
  mantissa.insert(mantissa.end(), fractional_nibbles.begin(), fractional_nibbles.end());
  return ParsedNumber{
      .mantissa_negative = negative,
      .mantissa = std::move(mantissa),
      .exponent_negative = exponent_negative,
      .exponent = exponent,
  };
}

std::string trim_right_spaces(std::string value) {
  while (!value.empty() && value.back() == ' ')
    value.pop_back();
  return value;
}

std::string trim_spaces(std::string value) {
  value = trim_right_spaces(std::move(value));
  while (!value.empty() && value.front() == ' ')
    value.erase(value.begin());
  return value;
}

}  // namespace

struct MK61::Impl {
  struct IR2 {
    std::array<int, 252> memory{};
    int input = 0;
    int output = 0;
    int tick_index = 0;

    void reset() {
      memory.fill(0);
      input = 0;
      output = 0;
      tick_index = 0;
    }

    void tick() {
      output = memory[idx(tick_index)];
      memory[idx(tick_index)] = input;
      ++tick_index;
      if (tick_index == 252)
        tick_index = 0;
    }
  };

  struct Accumulator {
    int alpha = 0;
    int beta = 0;
    int gamma = 0;
    int sigma = 0;
  };

  struct IK13 {
    std::span<const int> microcommand_rom;
    std::span<const int> sync_rom;
    std::span<const int> command_rom;
    std::array<int, 42> memory{};
    std::array<int, 42> r{};
    std::array<int, 42> stack{};
    int s = 0;
    int s1 = 0;
    int l = 0;
    int t = 0;
    int carry = 0;
    std::array<int, 42> j{};
    int tick_index = 0;
    int command = 0;
    int sync_address = 0;
    int input = 0;
    int output = 0;
    int key_x = 0;
    int key_y = 0;
    std::array<bool, 14> commas{};

    explicit IK13(const ChipRom& rom)
        : microcommand_rom(rom.microcommands),
          sync_rom(rom.sync_programs),
          command_rom(rom.commands) {
      reset();
    }

    void reset() {
      memory.fill(0);
      r.fill(0);
      stack.fill(0);
      s = 0;
      s1 = 0;
      l = 0;
      t = 0;
      carry = 0;
      for (int i = 0; i < 42; ++i)
        j[idx(i)] = i < 6 ? i : i < 21 ? i % 3 + 3 : i % 9;
      tick_index = 0;
      command = 0;
      sync_address = 0;
      input = 0;
      output = 0;
      key_x = 0;
      key_y = 0;
      commas.fill(false);
    }

    void execute_micro_order(int number, Accumulator& acc) {
      switch (number) {
        case 0:
          acc.alpha |= r[idx(tick_index)];
          break;
        case 1:
          acc.alpha |= memory[idx(tick_index)];
          break;
        case 2:
          acc.alpha |= stack[idx(tick_index)];
          break;
        case 3:
          acc.alpha |= ~r[idx(tick_index)] & 0b1111;
          break;
        case 4:
          if (l == 0)
            acc.alpha |= 0xA;
          break;
        case 5:
          acc.alpha |= s;
          break;
        case 6:
          acc.alpha |= 4;
          break;
        case 7:
          acc.beta |= s;
          break;
        case 8:
          acc.beta |= ~s & 0b1111;
          break;
        case 9:
          acc.beta |= s1;
          break;
        case 10:
          acc.beta |= 6;
          break;
        case 11:
          acc.beta |= 1;
          break;
        case 12:
          acc.gamma |= l & 1;
          break;
        case 13:
          acc.gamma |= ~l & 1;
          break;
        case 14:
          acc.gamma |= ~t & 1;
          break;
        case 15:
          r[idx(tick_index)] = r[idx((tick_index + 3) % 42)];
          break;
        case 16:
          r[idx(tick_index)] = acc.sigma;
          break;
        case 17:
          r[idx(tick_index)] = s;
          break;
        case 18:
          r[idx(tick_index)] = r[idx(tick_index)] | s | acc.sigma;
          break;
        case 19:
          r[idx(tick_index)] = s | acc.sigma;
          break;
        case 20:
          r[idx(tick_index)] = r[idx(tick_index)] | s;
          break;
        case 21:
          r[idx(tick_index)] = r[idx(tick_index)] | acc.sigma;
          break;
        case 22:
          r[idx((tick_index + 41) % 42)] = acc.sigma;
          break;
        case 23:
          r[idx((tick_index + 40) % 42)] = acc.sigma;
          break;
        case 24:
          memory[idx(tick_index)] = s;
          break;
        case 25:
          l = carry;
          break;
        case 26:
          s = s1;
          break;
        case 27:
          s = acc.sigma;
          break;
        case 28:
          s = s1 | acc.sigma;
          break;
        case 29:
          s1 = acc.sigma;
          break;
        case 30:
          break;
        case 31:
          s1 = s1 | acc.sigma;
          break;
        case 32:
          stack[idx((tick_index + 2) % 42)] = stack[idx((tick_index + 1) % 42)];
          stack[idx((tick_index + 1) % 42)] = stack[idx(tick_index)];
          stack[idx(tick_index)] = acc.sigma;
          break;
        case 33: {
          const int x = stack[idx(tick_index)];
          stack[idx(tick_index)] = stack[idx((tick_index + 1) % 42)];
          stack[idx((tick_index + 1) % 42)] = stack[idx((tick_index + 2) % 42)];
          stack[idx((tick_index + 2) % 42)] = x;
          break;
        }
        case 34: {
          const int x = stack[idx(tick_index)];
          const int y = stack[idx((tick_index + 1) % 42)];
          const int z = stack[idx((tick_index + 2) % 42)];
          stack[idx(tick_index)] = acc.sigma | y;
          stack[idx((tick_index + 1) % 42)] = x | z;
          stack[idx((tick_index + 2) % 42)] = y | x;
          break;
        }
        default:
          break;
      }
    }

    void tick() {
      if (tick_index == 0) {
        command = command_rom[idx(r[36] + 16 * r[39])];
        if (((command >> 16) & 0b111111) == 0)
          t = 0;
      }

      const int nine = tick_index / 9;
      const int tick_in_nine = tick_index - nine * 9;
      if (tick_in_nine == 0 && !(nine > 0 && nine < 3)) {
        if (nine < 3)
          sync_address = command & 0b1111111;
        else if (nine == 3)
          sync_address = (command >> 7) & 0b1111111;
        else if (nine == 4) {
          sync_address = (command >> 14) & 0b11111111;
          if (sync_address > 31) {
            if (tick_index == 36) {
              r[37] = sync_address & 0b1111;
              r[40] = sync_address >> 4;
            }
            sync_address = 95;
          }
        }
      }

      int sync_microcommand = sync_rom[idx(sync_address * 9 + j[idx(tick_index)])];
      sync_microcommand &= 0b111111;
      if (sync_microcommand > 59) {
        sync_microcommand = (sync_microcommand - 60) * 2;
        if (l == 0)
          ++sync_microcommand;
        sync_microcommand += 60;
      }

      int microcommand = microcommand_rom[idx(sync_microcommand)];
      std::array<int, 28> micro_orders{};
      for (int i = 0; i < 28; ++i) {
        micro_orders[idx(i)] = microcommand & 1;
        microcommand >>= 1;
      }

      const int trio = tick_index / 3;
      Accumulator acc;

      if (micro_orders[25] == 1 && trio != key_x - 1)
        s1 |= key_y;

      for (int i = 0; i < 12; ++i) {
        if (micro_orders[idx(i)] == 1)
          execute_micro_order(i, acc);
      }

      if (((command >> 16) & 0b111111) > 0) {
        if (key_y == 0)
          t = 0;
      } else {
        if (trio == key_x - 1 && key_y > 0) {
          s1 = key_y;
          t = 1;
        }
        commas[idx(trio)] = l > 0;
      }

      for (int i = 12; i < 15; ++i) {
        if (micro_orders[idx(i)] == 1)
          execute_micro_order(i, acc);
      }

      const int sum = acc.alpha + acc.beta + acc.gamma;
      acc.sigma = sum & 0b1111;
      carry = (sum >> 4) & 1;

      if (((command >> 22) & 1) == 0 || nine == 4) {
        const int field = (micro_orders[17] << 2) | (micro_orders[16] << 1) | micro_orders[15];
        if (field > 0)
          execute_micro_order(field + 14, acc);
        for (int i = 18; i < 20; ++i) {
          if (micro_orders[idx(i)] == 1)
            execute_micro_order(i + 4, acc);
        }
      }

      for (int i = 20; i < 22; ++i) {
        if (micro_orders[idx(i)] == 1)
          execute_micro_order(i + 4, acc);
      }

      for (int i = 0; i < 3; ++i) {
        const int field = (micro_orders[idx(23 + i * 2)] << 1) | micro_orders[idx(22 + i * 2)];
        if (field > 0)
          execute_micro_order(field + 25 + i * 3, acc);
      }

      output = memory[idx(tick_index)];
      memory[idx(tick_index)] = input;
      ++tick_index;
      if (tick_index == 42)
        tick_index = 0;
    }

    void close_ring(int value) { memory[idx((tick_index + 41) % 42)] = value; }
  };

  bool extended = true;
  int angle_mode = 10;
  IR2 ir2a;
  IR2 ir2b;
  IK13 ik1302{rom_chip(RomChip::Ik1302)};
  IK13 ik1303{rom_chip(RomChip::Ik1303)};
  IK13 ik1306{rom_chip(RomChip::Ik1306)};
  std::array<int, 12> display_digits{};
  std::array<bool, 12> display_commas{};
  bool powered = false;

  explicit Impl(MK61Options options)
      : extended(options.extended), angle_mode(parse_angle_mode(options.angle_mode)) {
    power_on();
  }

  void reset() {
    ir2a.reset();
    ir2b.reset();
    ik1302.reset();
    ik1303.reset();
    ik1306.reset();
    display_digits.fill(0);
    display_commas.fill(false);
  }

  void power_on() {
    if (powered)
      return;
    powered = true;
    frame(std::nullopt);
  }

  void power_off() {
    powered = false;
    reset();
  }

  void tick() {
    ik1302.input = ir2b.output;
    ik1302.tick();
    ik1303.input = ik1302.output;
    ik1303.tick();
    if (extended) {
      ik1306.input = ik1303.output;
      ik1306.tick();
      ir2a.input = ik1306.output;
    } else {
      ir2a.input = ik1303.output;
    }
    ir2a.tick();
    ir2b.input = ir2a.output;
    ir2b.tick();
    ik1302.close_ring(ir2b.output);
  }

  void frame(std::optional<std::pair<int, int>> pressed_key) {
    ik1303.key_y = 1;
    ik1303.key_x = angle_mode;

    if (pressed_key.has_value()) {
      ik1302.key_x = pressed_key->first;
      ik1302.key_y = pressed_key->second;
    }

    for (int i = 0; i < 560; ++i) {
      for (int j = 0; j < 42; ++j)
        tick();
    }

    update_display();
    ik1302.key_x = 0;
    ik1302.key_y = 0;
  }

  void update_display() {
    for (int j = 0; j < 9; ++j)
      display_digits[idx(j)] = ik1302.r[idx((8 - j) * 3)];
    for (int j = 0; j < 3; ++j)
      display_digits[idx(j + 9)] = ik1302.r[idx((11 - j) * 3)];
    for (int j = 0; j < 9; ++j)
      display_commas[idx(j)] = ik1302.commas[idx(9 - j)];
    for (int j = 0; j < 3; ++j)
      display_commas[idx(j + 9)] = ik1302.commas[idx(12 - j)];
  }

  int memory_phase() const { return ir2a.tick_index / 84; }

  void sync_memory_phase(int target = 1) {
    for (int i = 0; i < 3; ++i) {
      if (ir2a.tick_index == target * 84)
        return;
      frame(std::nullopt);
    }
    throw std::runtime_error("cannot sync MK-61 memory phase");
  }

  int command_limit() const { return extended ? 105 : 98; }

  std::array<int, 2> command_address(int index, int phase) const {
    const int whole = index / 7;
    const int rest = index % 7;
    const auto& permutation = extended ? kPagePermutationsExtended : kPagePermutationsBasic;
    const auto base = kMemoryPageAddresses[idx(permutation[idx(phase)][idx(whole)])];
    if (rest == 0)
      return base;
    return std::array<int, 2>{base[0], base[1] - 42 + rest * 6};
  }

  int& memory_cell(int chip, int address) {
    switch (chip) {
      case 1:
        return ir2a.memory[idx(address)];
      case 2:
        return ir2b.memory[idx(address)];
      case 3:
        return ik1302.memory[idx(address)];
      case 4:
        return ik1303.memory[idx(address)];
      case 5:
        return ik1306.memory[idx(address)];
      default:
        throw std::invalid_argument("unknown MK-61 chip");
    }
  }

  const int& memory_cell(int chip, int address) const {
    switch (chip) {
      case 1:
        return ir2a.memory[idx(address)];
      case 2:
        return ir2b.memory[idx(address)];
      case 3:
        return ik1302.memory[idx(address)];
      case 4:
        return ik1303.memory[idx(address)];
      case 5:
        return ik1306.memory[idx(address)];
      default:
        throw std::invalid_argument("unknown MK-61 chip");
    }
  }

  void write_command(int index, int code, int phase) {
    const auto address = command_address(index, phase);
    memory_cell(address[0], address[1]) = code / 16;
    memory_cell(address[0], address[1] - 3) = code % 16;
  }

  int read_command(int index, int phase) const {
    const auto address = command_address(index, phase);
    return memory_cell(address[0], address[1]) * 16 + memory_cell(address[0], address[1] - 3);
  }

  void write_number(int chip, int address, const ParsedNumber& number) {
    memory_cell(chip, address) = number.exponent_negative ? 9 : 0;
    memory_cell(chip, address - 3) = number.exponent / 10;
    memory_cell(chip, address - 6) = number.exponent % 10;
    memory_cell(chip, address - 9) = number.mantissa_negative ? 9 : 0;
    for (int i = 0; i < 8; ++i) {
      memory_cell(chip, address - 3 * (i + 4)) =
          i < static_cast<int>(number.mantissa.size()) ? number.mantissa[idx(i)] : 0;
    }
  }

  std::string read_number(int chip, int address) const {
    int exponent = memory_cell(chip, address - 3) * 10 + memory_cell(chip, address - 6);
    if (memory_cell(chip, address) == 9)
      exponent = -(100 - exponent);
    int index = 0;
    while (memory_cell(chip, address - 33 + index * 3) == 0) {
      if (exponent == 7 - index || index == 7)
        break;
      ++index;
    }
    std::vector<int> digits;
    while (index < 8) {
      digits.push_back(memory_cell(chip, address - 33 + index * 3));
      ++index;
    }
    std::reverse(digits.begin(), digits.end());

    std::string mantissa = memory_cell(chip, address - 9) == 9 ? "-" : "";
    bool comma = false;
    for (index = 0; index < static_cast<int>(digits.size()); ++index) {
      mantissa += kDigitSymbols[idx(digits[idx(index)])];
      if ((index == 0 && (exponent < 0 || exponent > 7)) || index == exponent) {
        mantissa += ",";
        comma = true;
      }
    }
    if (!comma)
      mantissa += ",";
    if (exponent < 0 || exponent > 7) {
      while (mantissa.size() < 12U)
        mantissa += " ";
      mantissa += std::to_string(exponent);
    }
    return mantissa;
  }
};

MK61::MK61(MK61Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}

MK61::~MK61() = default;
MK61::MK61(MK61&&) noexcept = default;
MK61& MK61::operator=(MK61&&) noexcept = default;

MK61& MK61::reset() {
  impl_->reset();
  return *this;
}

MK61& MK61::power_on() {
  impl_->power_on();
  return *this;
}

MK61& MK61::power_off() {
  impl_->power_off();
  return *this;
}

MK61& MK61::frame() {
  impl_->frame(std::nullopt);
  return *this;
}

MK61& MK61::frame(int key_x, int key_y) {
  impl_->frame(std::pair<int, int>{key_x, key_y});
  return *this;
}

MK61& MK61::press(const std::string& key) {
  const auto [x, y] = normalize_key(key);
  return press(x, y);
}

MK61& MK61::press(int key_x, int key_y) {
  impl_->frame(std::pair<int, int>{key_x, key_y});
  impl_->frame(std::nullopt);
  return *this;
}

MK61& MK61::press_sequence(const std::vector<std::string>& keys) {
  for (const auto& key : keys)
    press(key);
  return *this;
}

MK61& MK61::input_number(const std::string& value, bool clear) {
  std::string text = ascii_upper(trim_ascii(value));
  replace_all(text, ".", ",");
  if (clear)
    press("Сx");
  for (std::size_t pos = 0; pos < text.size(); ++pos) {
    const char ch = text[pos];
    if (ch >= '0' && ch <= '9')
      press(std::string(1, ch));
    else if (ch == ',' || ch == '.')
      press(",");
    else if (ch == '-')
      press("/-/");
    else if (ch == 'E')
      press("ВП");
    else if (ch != ' ')
      throw std::invalid_argument("unsupported MK-61 input character");
  }
  return *this;
}

MK61& MK61::run_frames(int count) {
  for (int i = 0; i < count; ++i)
    impl_->frame(std::nullopt);
  return *this;
}

RunResult MK61::run_until_stable(int max_frames, int stable_frames) {
  std::string previous;
  int stable = 0;
  bool has_previous = false;
  for (int frame = 0; frame < max_frames; ++frame) {
    impl_->frame(std::nullopt);
    const std::string signature = program_counter() + "|" + display_text();
    if (has_previous && signature == previous) {
      ++stable;
      if (stable >= stable_frames)
        return RunResult{.stopped = true, .frames = frame + 1, .signature = signature};
    } else {
      previous = signature;
      has_previous = true;
      stable = 0;
    }
  }
  return RunResult{.stopped = false, .frames = max_frames, .signature = previous};
}

std::vector<DisplayCell> MK61::display_cells() const {
  std::vector<DisplayCell> cells;
  for (int index = 0; index < 12; ++index) {
    const int digit = impl_->display_digits[idx(index)];
    cells.push_back(DisplayCell{
        .symbol = digit >= 0 && digit < static_cast<int>(kDigitSymbols.size())
                      ? std::string(kDigitSymbols[idx(digit)])
                      : "?",
        .comma = impl_->display_commas[idx(index)],
        .digit = digit,
    });
  }
  return cells;
}

std::string MK61::display_text(bool raw) const {
  const std::vector<DisplayCell> cells = display_cells();
  std::string text;
  for (const auto& cell : cells) {
    text += cell.symbol;
    if (cell.comma)
      text += ",";
    else if (raw)
      text += " ";
  }
  return raw ? text : trim_spaces(text);
}

std::string MK61::program_counter() const {
  const int high = impl_->ik1302.r[34];
  const int low = impl_->ik1302.r[31];
  const std::string high_text =
      high >= 0 && high < static_cast<int>(kDigitSymbols.size()) ? std::string(kDigitSymbols[idx(high)]) : "?";
  const std::string low_text =
      low >= 0 && low < static_cast<int>(kDigitSymbols.size()) ? std::string(kDigitSymbols[idx(low)]) : "?";
  return high_text + low_text;
}

int MK61::memory_phase() const { return impl_->memory_phase(); }

int MK61::command_limit() const { return impl_->command_limit(); }

ProgramLoadResult MK61::load_program(const std::vector<int>& program, bool clear_rest) {
  impl_->power_on();
  impl_->sync_memory_phase(1);

  ProgramLoadResult parsed{.codes = program, .diagnostics = {}};
  const int limit = impl_->command_limit();
  if (static_cast<int>(parsed.codes.size()) > limit) {
    parsed.diagnostics.push_back("Program was truncated from " +
                                 std::to_string(parsed.codes.size()) + " to " +
                                 std::to_string(limit) + " commands.");
  }

  const int count = std::min(static_cast<int>(parsed.codes.size()), limit);
  for (int i = 0; i < count; ++i)
    impl_->write_command(i, parsed.codes[idx(i)], 1);
  if (clear_rest) {
    for (int i = count; i < limit; ++i)
      impl_->write_command(i, 0, 1);
  }
  return parsed;
}

std::vector<int> MK61::read_program_codes(int count) {
  impl_->sync_memory_phase(1);
  const int limit = count < 0 ? impl_->command_limit() : count;
  std::vector<int> codes;
  for (int i = 0; i < limit; ++i)
    codes.push_back(impl_->read_command(i, 1));
  return codes;
}

MK61& MK61::set_register(const std::string& register_name, const std::string& value) {
  impl_->power_on();
  impl_->sync_memory_phase(1);

  const RegisterTarget target = normalize_register(register_name);
  const ParsedNumber parsed = parse_number_literal(value);
  if (target.memory) {
    const auto& permutation = impl_->extended ? kPagePermutationsExtended : kPagePermutationsBasic;
    const auto address = kMemoryPageAddresses[idx(permutation[1][idx(target.index)])];
    impl_->write_number(address[0], address[1] - 8, parsed);
  } else {
    const auto& permutation = impl_->extended ? kStackPermutationsExtended : kStackPermutationsBasic;
    const auto address = kStackAddresses[idx(permutation[1][idx(target.index)])];
    impl_->write_number(address[0], address[1], parsed);
  }
  return *this;
}

std::string MK61::read_register(const std::string& register_name) {
  impl_->sync_memory_phase(1);
  const RegisterTarget target = normalize_register(register_name);
  if (target.memory) {
    const auto& permutation = impl_->extended ? kPagePermutationsExtended : kPagePermutationsBasic;
    const auto address = kMemoryPageAddresses[idx(permutation[1][idx(target.index)])];
    return impl_->read_number(address[0], address[1] - 8);
  }
  const auto& permutation = impl_->extended ? kStackPermutationsExtended : kStackPermutationsBasic;
  const auto address = kStackAddresses[idx(permutation[1][idx(target.index)])];
  return impl_->read_number(address[0], address[1]);
}

}  // namespace mkpro::emulator
