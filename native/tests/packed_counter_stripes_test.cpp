#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

bool has_register_prefix(const CompileResult& result, const std::string& prefix) {
  return std::any_of(result.registers.begin(), result.registers.end(),
                     [&](const auto& entry) { return entry.first.rfind(prefix, 0) == 0; });
}

bool has_preload_value(const CompileResult& result, const std::string& value) {
  return std::any_of(result.preloads.begin(), result.preloads.end(),
                     [&](const PreloadReport& preload) { return preload.value == value; });
}

std::string compact(std::string value) {
  std::string out;
  for (const char ch : value) {
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
      out.push_back(ch);
  }
  return out;
}

std::string emulator_preload_value(std::string value) {
  for (char& ch : value) {
    if (std::toupper(static_cast<unsigned char>(ch)) == 'F')
      ch = '_';
  }
  return value;
}

std::vector<int> step_opcodes(const CompileResult& result) {
  std::vector<int> opcodes;
  opcodes.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    opcodes.push_back(step.opcode);
  return opcodes;
}

std::string run_compiled_with_preloads(const CompileResult& result) {
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(result));
  require(loaded.diagnostics.empty(), "compiled program should load into emulator");
  for (const PreloadReport& preload : result.preloads)
    calc.set_register(preload.register_name, emulator_preload_value(preload.value));
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(700, 5);
  require(run.stopped, "compiled program should stop in emulator");
  return compact(calc.display_text());
}

bool has_contiguous_opcodes(const CompileResult& result, const std::vector<int>& opcodes) {
  const std::vector<int> actual = step_opcodes(result);
  if (actual.size() < opcodes.size())
    return false;
  for (std::size_t index = 0; index + opcodes.size() <= actual.size(); ++index) {
    if (std::equal(opcodes.begin(), opcodes.end(),
                   actual.begin() + static_cast<std::vector<int>::difference_type>(index)))
      return true;
  }
  return false;
}

bool has_decimal_extract_preload_sequence(const CompileResult& result) {
  for (std::size_t index = 0; index + 3U < result.steps.size(); ++index) {
    const ResolvedStep& slash = result.steps.at(index);
    const ResolvedStep& frac = result.steps.at(index + 1U);
    const ResolvedStep& multiply = result.steps.at(index + 2U);
    const ResolvedStep& integer = result.steps.at(index + 3U);
    if (slash.opcode != 0x13 || frac.opcode != 0x35 || multiply.opcode != 0x12 ||
        integer.opcode != 0x34) {
      continue;
    }
    bool has_divisor = false;
    bool has_scale = false;
    const std::size_t preload_start = index > 3U ? index - 3U : 0U;
    for (std::size_t preload_index = preload_start; preload_index < index; ++preload_index) {
      has_divisor = has_divisor || result.steps.at(preload_index).comment == "preload const 10000";
      has_scale = has_scale || result.steps.at(preload_index).comment == "preload const 100";
    }
    if (has_divisor && has_scale)
      return true;
  }
  return false;
}

const CandidateReport* find_candidate(const std::vector<CandidateReport>& candidates,
                                      const std::string& variant) {
  const auto it = std::find_if(candidates.begin(), candidates.end(),
                               [&](const CandidateReport& candidate) {
                                 return candidate.variant == variant;
                               });
  return it == candidates.end() ? nullptr : &*it;
}

std::string diagnostics_text(const CompileResult& result) {
  std::string text;
  for (const Diagnostic& diagnostic : result.diagnostics) {
    if (!text.empty())
      text += "; ";
    text += diagnostic.code + ": " + diagnostic.message;
  }
  return text;
}

} // namespace

void packed_counter_stripes_match_typescript_contract() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;

  const CompileResult display_pair = compile_source(R"mkpro(
program DotCounterDisplay {
  state {
    floor: counter 1..9 = 9
    room: counter 0..6 = 0
  }

  loop {
    if room == 0 {
      room++
    }
    if floor == 9 {
      floor--
    }
    show(floor, ".", room)
  }
}
)mkpro",
                                                    options);

  require(display_pair.implemented, "packed counter stripe display should compile");
  require(display_pair.diagnostics.empty(),
          "packed counter stripe display should not report diagnostics: " +
              diagnostics_text(display_pair));
  require(has_optimization(display_pair, "packed-counter-stripes"),
          "literal-separated small counters should be packed into one decimal register");
  require(!has_optimization(display_pair, "fl-unit-decrement"),
          "packed counter stripes should avoid the separate floor decrement strategy");
  require(display_pair.registers.contains("__packed_counter_0"),
          "packed counter stripe should allocate __packed_counter_0");
  require(!display_pair.registers.contains("floor"),
          "packed counter stripe should remove floor state");
  require(!display_pair.registers.contains("room"),
          "packed counter stripe should remove room state");
  require(!has_register_prefix(display_pair, "__display_expr_"),
          "packed counter stripe display should not allocate display-expression scratch");
  require(display_pair.listing.find("__packed_counter_0") != std::string::npos,
          "listing should reference the packed decimal register");
  const CandidateReport* decimal_pack =
      find_candidate(display_pair.candidates, "decimal-pack-state");
  require(decimal_pack != nullptr && decimal_pack->selected,
          "packed counter stripe rewrite should surface decimal-pack-state as selected");

  CompileOptions forced_floor_row_options = options;
  forced_floor_row_options.pack_counter_stripes = true;
  const CompileResult forced_floor_row = compile_source(R"mkpro(
program PackedFloorRowCounters {
  state {
    floor: counter 1..9 = 9
    room: counter 0..6 = 0
    row: packed = bit_not(5 / 9)
  }

  loop {
    if room < 6 {
      room++
    }
    show(floor, ".", bit_not(row + 2 / pow10(room + 1)))
  }
}
)mkpro",
                                                        forced_floor_row_options);
  require(forced_floor_row.implemented,
          "forced packed floor-row counters should compile: " + diagnostics_text(forced_floor_row));
  require(forced_floor_row.diagnostics.empty(),
          "forced packed floor-row counters should not report diagnostics: " +
              diagnostics_text(forced_floor_row));
  require(has_optimization(forced_floor_row, "packed-counter-stripes"),
          "forced floor-row counters should report packed-counter-stripes");
  require(has_optimization(forced_floor_row, "floor-packed-row-display"),
          "forced floor-row counters should preserve floor-packed row display lowering");
  require(forced_floor_row.registers.contains("__packed_counter_0"),
          "forced floor-row counters should allocate __packed_counter_0");
  require(!forced_floor_row.registers.contains("floor"),
          "forced floor-row counters should remove floor state");
  require(!forced_floor_row.registers.contains("room"),
          "forced floor-row counters should remove room state");

  CompileOptions multi_options = options;
  multi_options.pack_counter_stripes = true;
  multi_options.pack_counter_stripe_names = {"a", "b", "c"};
  const CompileResult multi = compile_source(R"mkpro(
program MultiPackedCounters {
  state {
    a: counter 0..9 = 1
    b: counter 0..9 = 2
    c: counter 0..9 = 3
  }

  loop {
    a++
    b++
    c--
    if b >= 3 {
      halt(c)
    }
    else {
      halt(0)
    }
  }
}
)mkpro",
                                             multi_options);
  require(multi.implemented, "forced three-counter packing should compile");
  require(multi.diagnostics.empty(),
          "forced three-counter packing should not report diagnostics: " + diagnostics_text(multi));
  require(has_optimization(multi, "packed-counter-stripes"),
          "forced three-counter packing should report packed-counter-stripes");
  require(multi.registers.contains("__packed_counter_0"),
          "forced three-counter packing should allocate __packed_counter_0");
  require(!multi.registers.contains("a"), "forced three-counter packing should remove a");
  require(!multi.registers.contains("b"), "forced three-counter packing should remove b");
  require(!multi.registers.contains("c"), "forced three-counter packing should remove c");

  CompileOptions two_digit_options = options;
  two_digit_options.disable_candidate_search = true;
  two_digit_options.pack_counter_stripes = true;
  two_digit_options.pack_counter_stripe_names = {"a", "b", "c"};
  const std::string two_digit_source = R"mkpro(
program TwoDigitPackedCounters {
  state {
    a: counter 0..99 = 75
    b: counter 0..99 = 61
    c: counter 0..99 = 20
  }

  loop {
    halt(b)
  }
}
)mkpro";
  const CompileResult two_digit = compile_source(two_digit_source, two_digit_options);
  require(two_digit.implemented, "forced two-digit counter packing should compile");
  require(two_digit.diagnostics.empty(),
          "forced two-digit counter packing should not report diagnostics: " +
              diagnostics_text(two_digit));
  require(has_optimization(two_digit, "packed-counter-stripes"),
          "forced two-digit counter packing should report packed-counter-stripes");
  require(has_preload_value(two_digit, "10000") && has_preload_value(two_digit, "100"),
          "middle two-digit stripe extraction should preload decimal divisor and scale");
  require(has_decimal_extract_preload_sequence(two_digit),
          "middle two-digit stripe extraction should use preloaded divisor/scale commands");
  CompileOptions two_digit_without_decimal_preload_options = two_digit_options;
  two_digit_without_decimal_preload_options.suppress_constant_preloads.insert("100");
  two_digit_without_decimal_preload_options.suppress_constant_preloads.insert("10000");
  const CompileResult two_digit_without_decimal_preload =
      compile_source(two_digit_source, two_digit_without_decimal_preload_options);
  require(two_digit_without_decimal_preload.implemented,
          "forced two-digit counter packing without decimal preloads should compile");
  require(two_digit.steps.size() < two_digit_without_decimal_preload.steps.size(),
          "middle two-digit stripe extraction should be smaller with decimal preloads");

  CompileOptions wide_options = options;
  wide_options.pack_counter_stripes = true;
  wide_options.pack_counter_stripe_names = {"score", "lives", "room"};
  const CompileResult wide = compile_source(R"mkpro(
program WidePackedCounters {
  state {
    score: counter 0..99 = 12
    lives: counter 0..9 = 3
    room: counter 0..9 = 4
  }

  loop {
    score += 10
    lives--
    room++
    if score >= 22 {
      halt(room)
    }
    else {
      halt(0)
    }
  }
}
)mkpro",
                                            wide_options);
  require(wide.implemented, "forced fixed-width counter packing should compile");
  require(wide.diagnostics.empty(),
          "forced fixed-width counter packing should not report diagnostics: " +
              diagnostics_text(wide));
  require(has_optimization(wide, "packed-counter-stripes"),
          "forced fixed-width counter packing should report packed-counter-stripes");
  require(wide.registers.contains("__packed_counter_0"),
          "forced fixed-width counter packing should allocate __packed_counter_0");
  require(!wide.registers.contains("score"),
          "forced fixed-width counter packing should remove score");
  require(!wide.registers.contains("lives"),
          "forced fixed-width counter packing should remove lives");
  require(!wide.registers.contains("room"),
          "forced fixed-width counter packing should remove room");
}

void sentinel_decimal_pack_matches_strategy_contract() {
  CompileOptions logical_options;
  logical_options.analysis = true;
  logical_options.budget = 999;
  logical_options.disable_candidate_search = true;
  const CompileResult logical = compile_source(R"mkpro(
program LogicalPackedFieldExtract {
  state {
    packed: packed = 1002943
    __packed_counter_mask_0: packed = 800FF077
  }

  loop {
    halt(int(frac(bit_and(packed, __packed_counter_mask_0)) * pow10(4)))
  }
}
)mkpro",
                                               logical_options);
  require(logical.implemented, "logical packed field extract should compile");
  require(logical.diagnostics.empty(),
          "logical packed field extract should not report diagnostics: " +
              diagnostics_text(logical));
  require(has_optimization(logical, "logical-packed-field-extract"),
          "logical packed field expression should use the focused mask lowering");
  require(has_preload_value(logical, "800FF077"),
          "logical packed field mask should remain a setup/preload value");
  require(has_contiguous_opcodes(logical, {0x37, 0x35, 0x04, 0x15, 0x12, 0x34}),
          "logical packed field extract should emit K AND, frac, 4, F10x, multiply, int");
  const std::string logical_display = run_compiled_with_preloads(logical);
  require(logical_display.find("29,") != std::string::npos,
          "logical packed field extract should recover YY=29 through emulator, got " +
              logical_display);

  CompileOptions sentinel_options = logical_options;
  sentinel_options.sentinel_decimal_pack = true;
  sentinel_options.sentinel_decimal_pack_names = {"xx", "yy", "zz"};
  const CompileResult sentinel = compile_source(R"mkpro(
program SentinelDecimalPack {
  state {
    xx: counter 0..99 = 0
    yy: counter 0..99 = 29
    zz: counter 0..99 = 43
  }

  loop {
    halt(yy)
  }
}
)mkpro",
                                                sentinel_options);
  require(sentinel.implemented, "sentinel decimal pack should compile");
  require(sentinel.diagnostics.empty(),
          "sentinel decimal pack should not report diagnostics: " + diagnostics_text(sentinel));
  require(has_optimization(sentinel, "sentinel-decimal-pack"),
          "forced sentinel decimal packing should report sentinel-decimal-pack");
  require(has_optimization(sentinel, "logical-packed-field-extract"),
          "sentinel middle field read should use logical mask extraction");
  require(sentinel.registers.contains("__packed_counter_0"),
          "sentinel decimal pack should allocate __packed_counter_0");
  require(sentinel.registers.contains("__packed_counter_mask_0"),
          "sentinel decimal pack should allocate a hidden mask register");
  require(!sentinel.registers.contains("xx"), "sentinel decimal pack should remove xx");
  require(!sentinel.registers.contains("yy"), "sentinel decimal pack should remove yy");
  require(!sentinel.registers.contains("zz"), "sentinel decimal pack should remove zz");
  require(has_preload_value(sentinel, "1002943"),
          "sentinel decimal pack should store leading-one form 1002943");
  require(has_preload_value(sentinel, "800FF077"),
          "sentinel decimal pack should preload the logical mask");
  require(has_contiguous_opcodes(sentinel, {0x37, 0x35, 0x04, 0x15, 0x12, 0x34}),
          "sentinel middle field read should emit the logical mask sequence");
  const std::string sentinel_display = run_compiled_with_preloads(sentinel);
  require(sentinel_display.find("29,") != std::string::npos,
          "sentinel decimal pack should preserve yy=29 when xx has leading zeros, got " +
              sentinel_display);
}

} // namespace mkpro::tests
