#include "mkpro/core/mk61_trig.hpp"
#include "mkpro/emulator/mk61.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

namespace fs = std::filesystem;

using mkpro::core::mk61_trig::AngleMode;
using mkpro::core::mk61_trig::Function;

constexpr std::array<std::string_view, 3> kModes = {"rad", "deg", "grad"};
constexpr std::array<std::string_view, 3> kFunctions = {"sin", "cos", "tg"};

constexpr std::array<std::string_view, 48> kBaseValues = {
    "-360", "-270", "-180", "-120", "-100", "-90", "-60", "-45",
    "-30",  "-10",  "-1",   "-0.5", "-0.25", "-0.1", "0",   "0.0001",
    "0.001", "0.01", "0.1", "0.25", "0.5",   "1",    "2",   "3",
    "3.1415926", "6.2831853", "10", "30", "45", "50", "60", "89",
    "90", "99", "100", "120", "150", "180", "200", "270", "300", "360",
    "999", "-999", "1.2345678", "-1.2345678", "12.345678", "-12.345678",
};

constexpr std::array<std::string_view, 7> kWideMantissas = {
    "1", "1.0000001", "1.2345678", "2.7182818", "3.1415926", "5.5555555",
    "9.8765432",
};

constexpr std::array<std::string_view, 12> kForbiddenRuntimeTokens = {
    "microcommand_rom",
    "sync_rom",
    "command_rom",
    "struct Rom",
    "kIk130",
    "rom->",
    "micro_orders",
    "native/src/emulator",
    "mk61.cpp",
    "rom.cpp",
    "execute_order_list",
    "generated_address_dispatch",
};

struct Options {
  fs::path source_root = fs::current_path();
  bool stress = false;
  int max_failures = 40;
  std::vector<std::string> modes;
  std::vector<std::string> functions;
};

std::string trim_copy(std::string value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
                      return std::isspace(ch) != 0;
                    }).base();
  if (first >= last)
    return {};
  return std::string(first, last);
}

std::string normalize_mode(std::string value) {
  if (value == "grd")
    return "grad";
  return value;
}

bool valid_mode(std::string_view value) {
  return std::find(kModes.begin(), kModes.end(), value) != kModes.end();
}

bool valid_function(std::string_view value) {
  return std::find(kFunctions.begin(), kFunctions.end(), value) != kFunctions.end();
}

std::string require_option_value(int argc, char** argv, int& index, std::string_view option) {
  if (index + 1 >= argc)
    throw std::runtime_error(std::string(option) + " requires a value");
  ++index;
  return argv[index];
}

void print_usage(std::ostream& out) {
  out << "Usage: mkpro_validate_trig [options]\n"
      << "\n"
      << "Options:\n"
      << "  --source-root DIR   Repository root for source-shape checks\n"
      << "  --stress            Include deterministic stress values\n"
      << "  --max-fail N        Maximum failures to print before stopping; 0 means no limit\n"
      << "  --mode MODE         Limit to rad, deg, or grad; may be repeated\n"
      << "  --function FN       Limit to sin, cos, or tg; may be repeated\n"
      << "  --help              Show this help\n";
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--help") {
      print_usage(std::cout);
      std::exit(0);
    }
    if (arg == "--stress") {
      options.stress = true;
      continue;
    }
    if (arg == "--source-root") {
      options.source_root = require_option_value(argc, argv, index, arg);
      continue;
    }
    if (arg.starts_with("--source-root=")) {
      options.source_root = arg.substr(std::string_view("--source-root=").size());
      continue;
    }
    if (arg == "--max-fail") {
      options.max_failures = std::stoi(require_option_value(argc, argv, index, arg));
      continue;
    }
    if (arg.starts_with("--max-fail=")) {
      options.max_failures = std::stoi(arg.substr(std::string_view("--max-fail=").size()));
      continue;
    }
    if (arg == "--mode") {
      options.modes.push_back(normalize_mode(require_option_value(argc, argv, index, arg)));
      continue;
    }
    if (arg.starts_with("--mode=")) {
      options.modes.push_back(normalize_mode(arg.substr(std::string_view("--mode=").size())));
      continue;
    }
    if (arg == "--function") {
      options.functions.push_back(require_option_value(argc, argv, index, arg));
      continue;
    }
    if (arg.starts_with("--function=")) {
      options.functions.push_back(arg.substr(std::string_view("--function=").size()));
      continue;
    }
    throw std::runtime_error("unknown option: " + arg);
  }

  if (options.max_failures < 0)
    throw std::runtime_error("--max-fail must be non-negative");
  for (const std::string& mode : options.modes) {
    if (!valid_mode(mode))
      throw std::runtime_error("unknown mode: " + mode);
  }
  for (const std::string& function : options.functions) {
    if (!valid_function(function))
      throw std::runtime_error("unknown function: " + function);
  }
  return options;
}

std::string scale_decimal(std::string_view mantissa, int exponent) {
  const std::string mantissa_text(mantissa);
  const std::size_t dot = mantissa_text.find('.');
  const std::string whole = dot == std::string::npos ? mantissa_text : mantissa_text.substr(0, dot);
  const std::string frac = dot == std::string::npos ? "" : mantissa_text.substr(dot + 1);
  const std::string digits = whole + frac;
  const auto point = static_cast<std::ptrdiff_t>(whole.size()) + exponent;

  std::string out;
  if (point <= 0) {
    out = "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
  } else if (static_cast<std::size_t>(point) >= digits.size()) {
    out = digits + std::string(static_cast<std::size_t>(point) - digits.size(), '0');
  } else {
    const auto split = static_cast<std::size_t>(point);
    out = digits.substr(0, split) + "." + digits.substr(split);
  }

  if (out.find('.') != std::string::npos) {
    while (!out.empty() && out.back() == '0')
      out.pop_back();
    if (!out.empty() && out.back() == '.')
      out.pop_back();
  }
  return out.empty() ? "0" : out;
}

std::vector<std::string> corpus_values() {
  std::vector<std::string> result;
  std::unordered_set<std::string> seen;
  auto add = [&](std::string value) {
    if (seen.insert(value).second)
      result.push_back(std::move(value));
  };

  for (std::string_view value : kBaseValues)
    add(std::string(value));
  for (std::string_view mantissa : kWideMantissas) {
    for (int exponent = -4; exponent <= 4; ++exponent) {
      std::string value = scale_decimal(mantissa, exponent);
      add(value);
      if (value != "0")
        add("-" + value);
    }
  }
  return result;
}

std::vector<std::string> stress_values(const std::vector<std::string>& base_values) {
  std::vector<std::string> result;
  std::unordered_set<std::string> seen(base_values.begin(), base_values.end());
  auto add = [&](std::string value) {
    if (seen.insert(value).second)
      result.push_back(std::move(value));
  };

  std::uint32_t state = 0x4D4B61U;
  for (int index = 0; index < 64; ++index) {
    state = static_cast<std::uint32_t>((1103515245ULL * state + 12345ULL) & 0x7FFFFFFFULL);
    const std::uint32_t mantissa = 10000000U + state % 90000000U;
    state = static_cast<std::uint32_t>((1103515245ULL * state + 12345ULL) & 0x7FFFFFFFULL);
    const int exponent = static_cast<int>(state % 9U) - 4;
    const std::string digits = std::to_string(mantissa);
    std::string value = scale_decimal(digits.substr(0, 1) + "." + digits.substr(1), exponent);
    add(value);
    add("-" + value);
  }
  return result;
}

std::vector<std::string> selected_or_default(const std::vector<std::string>& selected,
                                             const std::array<std::string_view, 3>& defaults) {
  if (!selected.empty())
    return selected;
  std::vector<std::string> result;
  result.reserve(defaults.size());
  for (std::string_view value : defaults)
    result.emplace_back(value);
  return result;
}

std::string read_text(const fs::path& path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("failed to read " + path.string());
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void validate_no_runtime_rom_tables(const fs::path& source_root) {
  const std::string source =
      read_text(source_root / "native" / "src" / "core" / "mk61_trig.cpp") + "\n" +
      read_text(source_root / "native" / "include" / "mkpro" / "core" / "mk61_trig.hpp");

  std::vector<std::string> found;
  for (std::string_view token : kForbiddenRuntimeTokens) {
    if (source.find(token) != std::string::npos)
      found.emplace_back(token);
  }
  if (!found.empty()) {
    std::ostringstream message;
    message << "forbidden runtime ROM/generated-dispatch tokens:";
    for (const std::string& token : found)
      message << ' ' << token;
    throw std::runtime_error(message.str());
  }
}

int opcode_for_function(std::string_view function) {
  if (function == "sin")
    return 0x1c;
  if (function == "cos")
    return 0x1d;
  if (function == "tg")
    return 0x1e;
  throw std::runtime_error("unknown function: " + std::string(function));
}

AngleMode angle_mode_for(std::string_view mode) {
  if (mode == "rad")
    return AngleMode::Rad;
  if (mode == "deg")
    return AngleMode::Deg;
  if (mode == "grad")
    return AngleMode::Grad;
  throw std::runtime_error("unknown mode: " + std::string(mode));
}

Function function_for(std::string_view function) {
  if (function == "sin")
    return Function::Sin;
  if (function == "cos")
    return Function::Cos;
  if (function == "tg")
    return Function::Tg;
  throw std::runtime_error("unknown function: " + std::string(function));
}

std::string canonical_display(std::string text) {
  text = trim_copy(std::move(text));
  static const std::regex exponent_suffix(R"(^(.+?)( +)([- ][0-9]{2})$)");
  std::smatch match;
  if (std::regex_match(text, match, exponent_suffix))
    return match[1].str() + match[3].str();
  return text;
}

std::string emulator_display(std::string_view mode,
                             std::string_view function,
                             const std::string& value) {
  mkpro::emulator::MK61 calc(mkpro::emulator::MK61Options{.angle_mode = std::string(mode)});
  calc.set_register("x", value);
  const mkpro::emulator::ProgramLoadResult loaded =
      calc.load_program({opcode_for_function(function), 0x50});
  if (!loaded.diagnostics.empty()) {
    std::ostringstream message;
    message << "emulator load diagnostics:";
    for (const std::string& diagnostic : loaded.diagnostics)
      message << ' ' << diagnostic;
    throw std::runtime_error(message.str());
  }

  calc.press_sequence({"В/О", "С/П"});
  const mkpro::emulator::RunResult run = calc.run_until_stable(1000, 5);
  if (!run.stopped)
    throw std::runtime_error("emulator did not stop: " + run.signature);
  return calc.display_text();
}

std::string native_display(std::string_view mode,
                           std::string_view function,
                           const std::string& value) {
  return mkpro::core::mk61_trig::calculate_display(angle_mode_for(mode), function_for(function),
                                                   value);
}

std::size_t validate_values(const Options& options) {
  std::vector<std::string> values = corpus_values();
  if (options.stress) {
    std::vector<std::string> extra_values = stress_values(values);
    values.insert(values.end(), extra_values.begin(), extra_values.end());
  }

  const std::vector<std::string> modes = selected_or_default(options.modes, kModes);
  const std::vector<std::string> functions = selected_or_default(options.functions, kFunctions);

  std::size_t checked = 0;
  std::vector<std::string> failures;
  const bool stop_after_failures = options.max_failures > 0;

  for (const std::string& mode : modes) {
    for (const std::string& function : functions) {
      for (const std::string& value : values) {
        ++checked;
        try {
          const std::string emulator = emulator_display(mode, function, value);
          const std::string native = native_display(mode, function, value);
          const std::string emulator_canonical = canonical_display(emulator);
          const std::string native_canonical = canonical_display(native);
          if (emulator_canonical != native_canonical) {
            failures.push_back(mode + " " + function + " " + value +
                               ": emulator display=" + emulator_canonical +
                               " raw=" + trim_copy(emulator) +
                               "; native display=" + native_canonical +
                               " raw=" + trim_copy(native));
          }
        } catch (const std::exception& error) {
          failures.push_back(mode + " " + function + " " + value + ": " + error.what());
        }
        if (stop_after_failures &&
            failures.size() >= static_cast<std::size_t>(options.max_failures)) {
          break;
        }
      }
      if (stop_after_failures &&
          failures.size() >= static_cast<std::size_t>(options.max_failures)) {
        break;
      }
    }
    if (stop_after_failures &&
        failures.size() >= static_cast<std::size_t>(options.max_failures)) {
      break;
    }
  }

  if (!failures.empty()) {
    std::cout << "checked=" << checked << " failures=" << failures.size() << '\n';
    for (const std::string& failure : failures)
      std::cout << failure << '\n';
    throw std::runtime_error("MK-61 trig validation failed");
  }
  return checked;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_options(argc, argv);
    validate_no_runtime_rom_tables(options.source_root);
    const std::size_t checked = validate_values(options);
    std::cout << "mk61-trig-ok checked=" << checked << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "mkpro_validate_trig: " << error.what() << '\n';
    return 1;
  }
}
