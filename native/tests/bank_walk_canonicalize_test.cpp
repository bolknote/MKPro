#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& entry) { return entry.name == name; });
}

std::string run_display(const CompileResult& result, const std::string& context) {
  require(result.implemented,
          context + " should compile" +
              (result.diagnostics.empty() ? std::string()
                                          : ": " + result.diagnostics.front().message));
  require(result.diagnostics.empty(), context + " should not report diagnostics");
  std::vector<int> codes;
  codes.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), context + " should load without diagnostics");
  for (const PreloadReport& preload : result.preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(6000, 6);
  require(run.stopped, context + " should halt");
  return trim_ascii(calc.display_text());
}

// Self-decrement mark_one + monolithic candidate_score: the tic-tac-toe
// bank-walk shape the canonicalization pass targets.
constexpr const char* kBankWalkSource = R"mkpro(
program BankWalkProbe {
  state {
    lines: packed[4..7] = [44444.4, 44444.4, 44444.4, 44444.4]
    x: counter 0..5 = 1
    y: counter 0..5 = 2
    best_score: packed = 0
    score: packed = 0
    line: packed = 0
    slot: counter 0..8 = 4
  }

  fn mark_one() {
    slot--
    lines[slot] = packed_add(lines[slot], line, best_score)
    report = bit_and(lines[slot], 88888834)
    if frac(report) != 0 {
      halt(report)
    }
  }

  fn mark_lines_and_check(mark_sign) {
    best_score = mark_sign
    slot = 8
    line = x
    mark_one()
    line = y
    mark_one()
    normalize(x + y)
    mark_one()
    normalize(x - y)
    mark_one()
  }

  fn candidate_score() {
    score = packed_score(lines[7], x) + packed_score(lines[6], y)
    normalize(x + y)
    score += packed_score(lines[5], line)
    normalize(x - y)
    score += packed_score(lines[4], line)
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }

  loop {
    mark_lines_and_check(1)
    candidate_score()
    halt(score)
  }
}
)mkpro";

constexpr const char* kBorrowedBankWalkSource = R"mkpro(
program BorrowedBankWalkProbe {
  state {
    lines: packed[4..7] = [44444.4, 44444.4, 44444.4, 44444.4]
    scratch: packed = 9
    x: counter 0..5 = 1
    y: counter 0..5 = 2
    best_score: packed = 0
    line: packed = 0
    slot: counter 0..8 = 4
  }

  fn mark_one() {
    slot--
    lines[slot] = packed_add(lines[slot], line, best_score)
    report = bit_and(lines[slot], 88888834)
    if frac(report) != 0 {
      halt(report)
    }
  }

  fn mark_lines_and_check(mark_sign) {
    best_score = mark_sign
    slot = 8
    line = x
    mark_one()
    line = y
    mark_one()
    normalize(x + y)
    mark_one()
    normalize(x - y)
    mark_one()
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }

  loop {
    show(scratch)
    mark_lines_and_check(1)
    scratch = lines[4]
    mark_lines_and_check(-1)
    scratch = lines[4]
    halt(scratch)
  }
}
)mkpro";

} // namespace

void canonicalize_packed_line_bank_walk_rewrites_explicit_slot_leaves() {
  CompileOptions options;
  options.budget = 999999;
  options.disable_candidate_search = true;

  CompileOptions canonical_options = options;
  canonical_options.canonicalize_packed_line_bank_walks = true;

  const CompileResult baseline = compile_source(kBankWalkSource, options);
  require(!has_optimization(baseline, "canonicalize-packed-line-bank-walk"),
          "pass should stay off without its option flag");

  const CompileResult canonical = compile_source(kBankWalkSource, canonical_options);
  require(has_optimization(canonical, "canonicalize-packed-line-bank-walk"),
          "bank walks should be rewritten into explicit slot leaf calls");

  const auto canonical_report =
      std::find_if(canonical.optimizations.begin(), canonical.optimizations.end(),
                   [](const OptimizationReport& entry) {
                     return entry.name == "canonicalize-packed-line-bank-walk";
                   });
  require(canonical_report != canonical.optimizations.end(),
          "bank-walk optimization report should be present");
  require(canonical_report->detail.find("mark_lines_and_check") != std::string::npos,
          "mark walk should be rewritten");
  require(canonical_report->detail.find("candidate_score") != std::string::npos,
          "score walk should be rewritten");

  // The rewrite must not change observable semantics: both variants mark the
  // same cells and halt with the same candidate score.
  const std::string baseline_display = run_display(baseline, "implicit-walk baseline");
  const std::string canonical_display = run_display(canonical, "explicit-walk variant");
  require(canonical_display == baseline_display,
          "explicit-walk variant should match the implicit baseline: canonical=" +
              canonical_display + " baseline=" + baseline_display);

  CompileOptions mutating_options = canonical_options;
  mutating_options.packed_line_family_mutating_selector_update_check_tail = true;
  const CompileResult mutating = compile_source(kBankWalkSource, mutating_options);
  require(has_optimization(mutating, "canonicalize-packed-line-bank-walk"),
          "mutating candidate should canonicalize the marker bank walk");
  require(has_optimization(mutating, "packed-line-family-score-accumulator"),
          "mutating candidate should preserve packed-line score accumulator lowering");

  const auto mutating_canonical_report =
      std::find_if(mutating.optimizations.begin(), mutating.optimizations.end(),
                   [](const OptimizationReport& entry) {
                     return entry.name == "canonicalize-packed-line-bank-walk";
                   });
  require(mutating_canonical_report != mutating.optimizations.end(),
          "mutating bank-walk optimization report should be present");
  require(mutating_canonical_report->detail.find("mark_lines_and_check") != std::string::npos,
          "mutating candidate should rewrite the mark walk");
  require(mutating_canonical_report->detail.find("candidate_score") == std::string::npos,
          "mutating candidate should leave the specialized score walk intact");

  const CompileResult borrowed_baseline = compile_source(kBorrowedBankWalkSource, options);
  CompileOptions borrowed_options = options;
  borrowed_options.canonicalize_packed_line_bank_walks = true;
  borrowed_options.packed_line_family_borrowed_mutating_selector_update_check_tail = true;
  const CompileResult borrowed = compile_source(kBorrowedBankWalkSource, borrowed_options);
  require(has_optimization(
              borrowed, "packed-line-family-borrowed-mutating-selector-update-check-tail"),
          "borrowed candidate should use a low register whose owner is dead across the marker");
  require(borrowed.steps.size() < borrowed_baseline.steps.size(),
          "borrowed selector should reduce the compact bank-walk fixture size");
  const std::string borrowed_baseline_display =
      run_display(borrowed_baseline, "borrowed-selector baseline");
  const std::string borrowed_display = run_display(borrowed, "borrowed-selector variant");
  require(borrowed_display == borrowed_baseline_display,
          "borrowed-selector variant should preserve bank-walk semantics: borrowed=" +
              borrowed_display + " baseline=" + borrowed_baseline_display);

  CompileOptions aliased_borrowed_options = borrowed_options;
  aliased_borrowed_options.forced_register_shares.push_back(
      RegisterShare{.free_register = "8", .keep_register = "0"});
  const CompileResult aliased_borrowed =
      compile_source(kBorrowedBankWalkSource, aliased_borrowed_options);
  const auto aliased_borrow_report =
      std::find_if(aliased_borrowed.optimizations.begin(), aliased_borrowed.optimizations.end(),
                   [](const OptimizationReport& entry) {
                     return entry.name ==
                            "packed-line-family-borrowed-mutating-selector-update-check-tail";
                   });
  require(aliased_borrow_report != aliased_borrowed.optimizations.end() &&
              aliased_borrow_report->detail.find("scratch") != std::string::npos,
          "borrowed proof should accept a source owner rewritten after every marker call");
  require(run_display(aliased_borrowed, "aliased borrowed-selector variant") ==
              borrowed_baseline_display,
          "borrowing an aliased dead source owner should preserve bank-walk semantics");

  std::string live_owner_source = kBorrowedBankWalkSource;
  const std::string dead_owner_tail =
      "scratch = lines[4]\n    mark_lines_and_check(-1)\n    scratch = lines[4]\n    halt(scratch)";
  const std::size_t dead_owner_tail_pos = live_owner_source.find(dead_owner_tail);
  require(dead_owner_tail_pos != std::string::npos,
          "borrowed-selector fixture should contain its dead-owner continuation");
  live_owner_source.replace(dead_owner_tail_pos, dead_owner_tail.size(),
                            "scratch = lines[4]\n    mark_lines_and_check(-1)\n    halt(scratch)");
  const CompileResult live_owner = compile_source(live_owner_source, aliased_borrowed_options);
  require(!has_optimization(
              live_owner, "packed-line-family-borrowed-mutating-selector-update-check-tail"),
          "borrowed candidate should reject a register owner live after the marker call");
}

} // namespace mkpro::tests
