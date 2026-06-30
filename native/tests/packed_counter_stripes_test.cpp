#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>

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

} // namespace mkpro::tests
