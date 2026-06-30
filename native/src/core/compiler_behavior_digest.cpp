#include "mkpro/core/compiler_behavior_digest.hpp"

#include "mkpro/emulator/mk61.hpp"

#include <cctype>
#include <exception>
#include <initializer_list>
#include <string>
#include <vector>

namespace mkpro {

namespace {

// Behavioral-observation harness for CLI/debug tooling and regression tests.
// This used to be the optimizer-internal acceptance guard for aggressive
// indirect-flow candidates; candidate selection must not consult it now.
// Dangerous candidates are accepted only by optimizer_static_gate_accepts, where
// each optimization boundary supplies its own local proof obligation.
struct EquivalenceTurn {
  bool stopped = false;
  std::string display;
  bool operator==(const EquivalenceTurn& other) const {
    return stopped == other.stopped && display == other.display;
  }
  bool operator!=(const EquivalenceTurn& other) const { return !(*this == other); }
};

struct EquivalenceObservation {
  // `runnable` is false when the candidate could not be exercised in this
  // harness at all (a load diagnostic or an emulator parse/runtime exception,
  // e.g. a preload value the emulator number parser cannot accept). We treat
  // "not runnable" as its own observable bucket so that a baseline and a
  // candidate that are both un-runnable for the same structural reason still
  // compare equal, while a candidate that becomes un-runnable when the
  // baseline runs is rejected.
  bool runnable = false;
  // One entry per interaction turn: turn 0 is the display after the initial
  // В/О С/П, and each subsequent entry is the display after pressing С/П with
  // the next scenario input. Capturing every turn (rather than only the final
  // state) lets the comparison stop once the reference program goes idle.
  std::vector<EquivalenceTurn> turns;
};

// Preload values are carried in the compiler's hex-ish register encoding
// (A/B/C/D/E/F nibbles). The emulator's number parser expects MK-61 display
// glyphs, so translate the same way the indirect-flow equivalence test does
// before handing a value to set_register. Without this, dark-entry selector
// preloads (e.g. "B2") would throw inside the emulator and make debug behavior
// digests spuriously treat a candidate as un-runnable.
std::string emulator_preload_literal(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
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

// Behavior-digest helper for CLI/debug tooling. Optimizer candidate acceptance
// must not call this path: dangerous candidates are accepted only by local
// static proof obligations at their optimization boundary.
EquivalenceObservation run_equivalence_observation(const CompileResult& result,
                                                   const std::vector<int>& inputs) {
  EquivalenceObservation observation;
  try {
    emulator::MK61 calc;
    if (result.setup_program.has_value()) {
      std::vector<int> setup_codes;
      setup_codes.reserve(result.setup_program->steps.size());
      for (const ResolvedStep& step : result.setup_program->steps)
        setup_codes.push_back(step.opcode);
      const emulator::ProgramLoadResult setup_loaded = calc.load_program(setup_codes);
      if (!setup_loaded.diagnostics.empty())
        return observation;
      calc.press_sequence({"В/О", "С/П"});
      calc.run_until_stable(2000, 6);
    }

    std::vector<int> codes;
    codes.reserve(result.steps.size());
    for (const ResolvedStep& step : result.steps)
      codes.push_back(step.opcode);
    const emulator::ProgramLoadResult loaded = calc.load_program(codes);
    if (!loaded.diagnostics.empty())
      return observation;
    for (const PreloadReport& preload : result.preloads)
      calc.set_register(preload.register_name, emulator_preload_literal(preload.value));

    calc.press_sequence({"В/О", "С/П"});
    {
      const emulator::RunResult run = calc.run_until_stable(2000, 6);
      observation.turns.push_back(
          EquivalenceTurn{.stopped = run.stopped, .display = calc.display_text()});
    }
    for (const int value : inputs) {
      // Clear X before each key so consecutive turns enter a fresh number
      // rather than concatenating digits, matching how the calculator is
      // actually driven (see emulator_indirect_flow_equivalence_test).
      calc.input_number(std::to_string(value), /*clear=*/true);
      calc.press("С/П");
      const emulator::RunResult run = calc.run_until_stable(2000, 6);
      observation.turns.push_back(
          EquivalenceTurn{.stopped = run.stopped, .display = calc.display_text()});
    }
    observation.runnable = true;
  } catch (const std::exception&) {
    // Any emulator-side failure leaves `runnable` false; the comparison logic
    // below treats that conservatively.
    observation.runnable = false;
  }
  return observation;
}

const std::vector<std::vector<int>>& equivalence_scenarios() {
  static const std::vector<std::vector<int>> kScenarios = {
      {},          {1},         {2},          {5},          {8},
      {1, 2},      {2, 1},      {7, 3},       {3, 3},       {5, 5},
      {1, 20},     {20, 20},    {3, 7, 3},    {3, 7, 7},    {3, 7, 5},
      {7, 3, 7},   {4, 5, 5},   {4, 5, 1},    {8, 9, 12},   {1, 20, 16},
      {9, 12, 5},  {3, 10, 4},  {4, 6, 5},    {1, 2, 3, 4},
  };
  return kScenarios;
}

std::vector<EquivalenceObservation> equivalence_observations(const CompileResult& result) {
  std::vector<EquivalenceObservation> observations;
  observations.reserve(equivalence_scenarios().size());
  for (const std::vector<int>& inputs : equivalence_scenarios())
    observations.push_back(run_equivalence_observation(result, inputs));
  return observations;
}

std::string equivalence_observations_digest(const std::vector<EquivalenceObservation>& obs) {
  const std::vector<std::vector<int>>& scenarios = equivalence_scenarios();
  std::string out;
  for (std::size_t index = 0; index < obs.size(); ++index) {
    out += '[';
    if (index < scenarios.size()) {
      for (std::size_t i = 0; i < scenarios[index].size(); ++i) {
        if (i != 0)
          out += ',';
        out += std::to_string(scenarios[index][i]);
      }
    }
    out += "] ";
    if (!obs[index].runnable) {
      out += "<not-runnable>";
    } else {
      for (std::size_t t = 0; t < obs[index].turns.size(); ++t) {
        if (t != 0)
          out += " | ";
        out += obs[index].turns[t].stopped ? "stop:" : "run:";
        out += obs[index].turns[t].display;
      }
    }
    out += '\n';
  }
  return out;
}

}  // namespace

std::string program_behavior_digest(const CompileResult& result) {
  return equivalence_observations_digest(equivalence_observations(result));
}

}  // namespace mkpro
