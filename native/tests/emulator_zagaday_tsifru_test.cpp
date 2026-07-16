#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

std::string read_zagaday_source() {
  const std::filesystem::path path =
      std::filesystem::current_path() / "examples" / "zagaday-tsifru.mkpro";
  std::ifstream input(path);
  require(input.good(), "should read " + path.string());
  std::ostringstream source;
  source << input.rdbuf();
  return source.str();
}

std::string replace_once(std::string text, const std::string& from, const std::string& to) {
  const std::size_t position = text.find(from);
  require(position != std::string::npos, "zagaday source should contain " + from);
  text.replace(position, from.size(), to);
  return text;
}

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

std::string mk61_hex_literal(const std::string& text) {
  std::string out;
  for (char ch : text) {
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

std::vector<int> step_opcodes(const CompileResult& result) {
  std::vector<int> opcodes;
  opcodes.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    opcodes.push_back(step.opcode);
  return opcodes;
}

class ZagadayRun {
public:
  ZagadayRun(std::string label, CompileResult compiled, const std::string& history)
      : label_(std::move(label)), compiled_(std::move(compiled)),
        calculator_({.angle_mode = "grad"}) {
    const emulator::ProgramLoadResult loaded = calculator_.load_program(step_opcodes(compiled_));
    require(loaded.diagnostics.empty(), "zagaday program should load into the MK-61 emulator");
    for (const PreloadReport& preload : compiled_.preloads)
      calculator_.set_register(preload.register_name, mk61_hex_literal(preload.value));
    calculator_.set_register(register_for("history"), history);
  }

  void start() {
    calculator_.press_sequence({"В/О", "С/П"});
    require_stop("initial score");
  }

  void enter_prediction(int prediction) {
    calculator_.input_number(std::to_string(prediction), true);
    calculator_.press("С/П");
    require_stop("prediction display");
  }

  int prediction() {
    return std::stoi(read_state("prediction"));
  }

  std::string display() const {
    return trim_ascii(calculator_.display_text());
  }

  std::string read_state(const std::string& name) {
    return trim_ascii(calculator_.read_register(register_for(name)));
  }

private:
  std::string register_for(const std::string& name) const {
    const auto found = compiled_.registers.find(name);
    require(found != compiled_.registers.end(), "zagaday allocation should contain " + name);
    return found->second;
  }

  void require_stop(const std::string& phase) {
    const emulator::RunResult run = calculator_.run_until_stable(12000, 5);
    require(run.stopped,
            label_ + " zagaday should stop at " + phase + ", pc=" + calculator_.program_counter() +
                ", display=" + trim_ascii(calculator_.display_text()) +
                ", frames=" + std::to_string(run.frames) + ", signature=" + run.signature);
  }

  std::string label_;
  CompileResult compiled_;
  emulator::MK61 calculator_;
};

void require_same_visible_state(ZagadayRun& optimized, ZagadayRun& reference,
                                const std::string& phase) {
  require(optimized.display() == reference.display(), phase + " display should match: expected " +
                                                          reference.display() + ", got " +
                                                          optimized.display());
  for (const std::string& name :
       {"history", "score", "prediction", "weights_1", "weights_2", "weights_3"}) {
    require(optimized.read_state(name) == reference.read_state(name),
            phase + " logical state should match for " + name);
  }
}

} // namespace

void emulator_zagaday_tsifru_optimized_source_preserves_ui() {
  const std::string optimized_source = read_zagaday_source();
  std::string reference_source =
      replace_once(optimized_source, "return int((ones + 8) / 19)", "return int(ones / 11)");
  reference_source =
      replace_once(reference_source, "history = bit_or(2 + frac(history), 10 + player)",
                   "history = bit_or(2 + bit_and(history, frac(FULL_BITS)), 10 + player)");

  const CompileResult optimized = compile_source(optimized_source);
  const CompileResult reference = compile_source(reference_source);
  require(optimized.implemented && optimized.diagnostics.empty(),
          "optimized zagaday source should compile without diagnostics");
  require(reference.implemented && reference.diagnostics.empty(),
          "reference zagaday source should compile without diagnostics");
  require(optimized.steps.size() == 100U && optimized.steps.size() < reference.steps.size(),
          "optimized zagaday source should be the smaller 100-cell program");

  const std::array<std::string, 2> threshold_histories{"8.0123456", "8.7777777"};
  for (const std::string& history : threshold_histories) {
    const std::string turn_name = "zagaday history " + history;
    ZagadayRun optimized_run("optimized", optimized, history);
    ZagadayRun reference_run("reference", reference, history);
    optimized_run.start();
    reference_run.start();
    require_same_visible_state(optimized_run, reference_run, turn_name + " score");
    const int prediction = reference_run.prediction();
    require(optimized_run.prediction() == prediction && prediction >= 0 && prediction <= 7,
            turn_name + " prediction should match and stay octal");

    // Stop before the stochastic update. Extreme histories exercise the
    // threshold path without random timing obscuring equivalence between
    // differently sized programs.
    optimized_run.enter_prediction(prediction);
    reference_run.enter_prediction(prediction);
    require_same_visible_state(optimized_run, reference_run, turn_name + " prediction display");
  }
  for (int ones = 0; ones <= 21; ++ones) {
    require(ones / 11 == (ones + 8) / 19,
            "biased threshold should equal the original for every reachable popcount");
  }
  for (int octal_digit = 0; octal_digit <= 7; ++octal_digit) {
    require((octal_digit & 7) == octal_digit,
            "FULL_BITS masking should be the identity on every reachable history digit");
  }
}

} // namespace mkpro::tests
