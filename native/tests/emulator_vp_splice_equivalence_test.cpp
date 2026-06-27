#include "mkpro/core/passes/index.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

constexpr int kVp = 0x0c;
constexpr int kKnop = 0x54;
constexpr int kK1 = 0x55;
constexpr int kK2 = 0x56;
constexpr int kDot = 0x0a;
constexpr int kSignChange = 0x0b;
constexpr int kClearX = 0x0d;
constexpr int kFPi = 0x20;
constexpr int kStore1 = 0x41;
constexpr int kF0 = 0xf0;
constexpr int kStop = 0x50;
constexpr int kReturn = 0x52;
constexpr int kCall = 0x53;

MachineItem op(int opcode) {
  return MachineItem::op(opcode, std::to_string(opcode));
}

std::vector<MachineItem> machine_items(const std::vector<int>& program) {
  std::vector<MachineItem> items;
  items.reserve(program.size());
  for (const int opcode : program)
    items.push_back(op(opcode));
  return items;
}

std::vector<int> codes(const std::vector<MachineItem>& items) {
  std::vector<int> result;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Op)
      result.push_back(item.opcode);
  }
  return result;
}

core::passes::RunPassesResult optimize(const std::vector<int>& program) {
  CompileOptions options;
  options.delivery = DeliveryMode::Hex;
  options.budget = 105;
  options.analysis = false;
  return core::passes::run_ir_passes(machine_items(program), options);
}

std::vector<int> optimized_codes(const std::vector<int>& program) {
  return codes(optimize(program).items);
}

std::string compact(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) { return std::isspace(ch) != 0; }),
              value.end());
  return value;
}

std::string display(const std::vector<int>& program) {
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(program);
  require(loaded.diagnostics.empty(), "VP-splice fixture should load into the emulator");
  calc.press_sequence({"В/О", "С/П"});
  (void)calc.run_until_stable(300, 4);
  return compact(calc.display_text());
}

std::string code_text(const std::vector<int>& program) {
  std::ostringstream out;
  out << "[";
  for (std::size_t index = 0; index < program.size(); ++index) {
    if (index != 0)
      out << " ";
    out << std::hex << std::uppercase << program.at(index);
  }
  out << "]";
  return out.str();
}

void require_equal_codes(const std::vector<int>& actual, const std::vector<int>& expected,
                         const std::string& context) {
  require(actual == expected,
          context + " expected " + code_text(expected) + ", got " + code_text(actual));
}

void require_display_contains(const std::vector<int>& program, const std::string& needle,
                              const std::string& context) {
  const std::string actual = display(program);
  require(actual.find(needle) != std::string::npos,
          context + " expected display containing " + needle + ", got " + actual);
}

void require_same_display(const std::vector<int>& left, const std::vector<int>& right,
                          const std::string& context) {
  const std::string actual = display(left);
  const std::string expected = display(right);
  require(actual == expected,
          context + " expected equal displays, got " + actual + " vs " + expected);
}

void require_different_display(const std::vector<int>& left, const std::vector<int>& right,
                               const std::string& context) {
  const std::string actual = display(left);
  const std::string unexpected = display(right);
  require(actual != unexpected,
          context + " expected different displays, both were " + actual);
}

bool has_optimization(const core::passes::RunPassesResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const core::passes::AppliedOptimization& optimization) {
                       return optimization.name == name;
                     });
}

} // namespace

void emulator_vp_splice_equivalence_matches_typescript_contract() {
  {
    require_display_contains({0x05, kVp, 0x03, kStop}, "5000",
                             "single VP should enter exponent notation");
    require_display_contains({0x05, kVp, kVp, 0x03, kStop}, "5000",
                             "repeated VP should collapse on the emulator");
    require_display_contains({0x05, kKnop, kVp, 0x03, kStop}, "5000",
                             "KNOP before VP should be exponent-entry empty");
    require_display_contains({0x05, kK1, kVp, 0x03, kStop}, "5000",
                             "K1 before VP should be exponent-entry empty");
    require_display_contains({0x05, kK2, kVp, 0x03, kStop}, "5000",
                             "K2 before VP should be exponent-entry empty");

    require_same_display({0x02, kK1, kCall, 0x07, kVp, 0x03, kStop, kKnop, kReturn},
                         {0x02, kCall, 0x06, kVp, 0x03, kStop, kKnop, kReturn},
                         "transparent subroutine return should preserve empty-op VP equivalence");
  }

  {
    const core::passes::RunPassesResult result = optimize(
        {0x05, kVp, kVp, kKnop, kVp, kK1, kVp, kK2, kVp, 0x03, kStop});
    require(has_optimization(result, "vp-exponent-splice"),
            "vp-exponent-splice optimization should be reported");
    const std::vector<int> result_codes = codes(result.items);
    require(std::count(result_codes.begin(), result_codes.end(), kVp) == 1,
            "vp-exponent-splice should leave one VP");
    require(std::count(result_codes.begin(), result_codes.end(), kKnop) == 0,
            "vp-exponent-splice should remove KNOP");
    require(std::count(result_codes.begin(), result_codes.end(), kK1) == 0,
            "vp-exponent-splice should remove K1");
    require(std::count(result_codes.begin(), result_codes.end(), kK2) == 0,
            "vp-exponent-splice should remove K2");
    require_display_contains(result_codes, "5000",
                             "collapsed VP program should still compute the same exponent");
  }

  {
    require_same_display({0x05, kVp, 0x03, kKnop, kStop}, {0x05, kVp, 0x03, kStop},
                         "empty op after exponent digit should be removable before stop");
    require_same_display({0x05, kVp, 0x03, kK1, kStop}, {0x05, kVp, 0x03, kStop},
                         "K1 after exponent digit should be removable before stop");
    require_same_display({0x05, kVp, 0x03, kK2, kStop}, {0x05, kVp, 0x03, kStop},
                         "K2 after exponent digit should be removable before stop");
    require_same_display({0x05, kVp, 0x03, kKnop, kSignChange, kStop},
                         {0x05, kVp, 0x03, kSignChange, kStop},
                         "empty op after exponent digit should be removable before sign");
    require_same_display({0x05, kVp, 0x03, kKnop, kDot, kStop},
                         {0x05, kVp, 0x03, kDot, kStop},
                         "empty op after exponent digit should be removable before dot");
    require_different_display({0x05, kVp, 0x03, kKnop, 0x04, kStop},
                              {0x05, kVp, 0x03, 0x04, kStop},
                              "empty op before another digit should be kept");
  }

  {
    require_equal_codes(optimized_codes({0x05, kVp, 0x03, kKnop, kSignChange, kStop}),
                        {0x05, kVp, 0x03, kSignChange, kStop},
                        "pass should remove exponent-digit empty separator before sign");
    require_same_display(optimized_codes({0x05, kVp, 0x03, kKnop, kSignChange, kStop}),
                         {0x05, kVp, 0x03, kKnop, kSignChange, kStop},
                         "removing exponent-digit empty separator should preserve display");

    require_equal_codes(optimized_codes({0x05, kVp, 0x03, kKnop, 0x04, kStop}),
                        {0x05, kVp, 0x03, kKnop, 0x04, kStop},
                        "pass should keep exponent-digit separator before another digit");
  }

  {
    require_same_display({0x01, 0x02, kVp, 0x03, kFPi, kKnop, kSignChange, kStop},
                         {0x01, 0x02, kVp, 0x03, kFPi, kSignChange, kStop},
                         "empty op before VP-context sign should be removable");
    require_same_display({0x05, kVp, 0x03, kFPi, kSignChange, kSignChange, 0x04, kStop},
                         {0x05, kVp, 0x03, kFPi, 0x04, kStop},
                         "closed sign pair before digit should be removable");
    require_same_display(
        {0x05, kVp, 0x03, kFPi, kSignChange, kSignChange, kKnop, 0x04, kStop},
        {0x05, kVp, 0x03, kFPi, kKnop, 0x04, kStop},
        "closed sign pair before empty-op digit should be removable");
    require_same_display({0x05, kVp, 0x03, kFPi, kSignChange, 0x04, kStop},
                         {0x05, kVp, 0x03, kFPi, 0x04, kStop},
                         "single VP-context sign before digit should be removable");
    require_same_display({0x05, kVp, 0x03, kFPi, kSignChange, kKnop, 0x04, kStop},
                         {0x05, kVp, 0x03, kFPi, kKnop, 0x04, kStop},
                         "single VP-context sign before empty-op digit should be removable");
    require_different_display({0x05, kVp, 0x03, kFPi, kSignChange, kSignChange, kStop},
                              {0x05, kVp, 0x03, kFPi, kStop},
                              "closed sign pair before stop should not be blindly removed");
    require_different_display({0x05, kVp, kSignChange, 0x04, kStop},
                              {0x05, kVp, 0x04, kStop},
                              "open VP-context sign before digit should not be removed");
    require_different_display({0x05, kVp, 0x03, kFPi, kSignChange, kStop},
                              {0x05, kVp, 0x03, kFPi, kStop},
                              "single VP-context sign before stop should not be removed");

    require_equal_codes(optimized_codes({0x01, 0x02, kVp, 0x03, kFPi, kKnop, kSignChange, kStop}),
                        {0x01, 0x02, kVp, 0x03, kFPi, kSignChange, kStop},
                        "pass should remove empty op before VP-context sign");
    require_same_display(
        optimized_codes({0x01, 0x02, kVp, 0x03, kFPi, kKnop, kSignChange, kStop}),
        {0x01, 0x02, kVp, 0x03, kFPi, kKnop, kSignChange, kStop},
        "empty-op VP-context sign rewrite should preserve display");

    require_equal_codes(
        optimized_codes({0x05, kVp, 0x03, kFPi, kSignChange, kSignChange, 0x04, kStop}),
        {0x05, kVp, 0x03, kFPi, 0x04, kStop},
        "pass should collapse sign pair before digit");
    require_same_display(
        optimized_codes({0x05, kVp, 0x03, kFPi, kSignChange, kSignChange, 0x04, kStop}),
        {0x05, kVp, 0x03, kFPi, kSignChange, kSignChange, 0x04, kStop},
        "sign-pair before digit rewrite should preserve display");

    require_equal_codes(
        optimized_codes(
            {0x05, kVp, 0x03, kFPi, kSignChange, kSignChange, kKnop, 0x04, kStop}),
        {0x05, kVp, 0x03, kFPi, 0x04, kStop},
        "pass should collapse sign pair before empty-op digit");
    require_same_display(
        optimized_codes(
            {0x05, kVp, 0x03, kFPi, kSignChange, kSignChange, kKnop, 0x04, kStop}),
        {0x05, kVp, 0x03, kFPi, kSignChange, kSignChange, kKnop, 0x04, kStop},
        "sign-pair before empty-op digit rewrite should preserve display");

    require_equal_codes(
        optimized_codes({0x05, kVp, 0x03, kFPi, kSignChange, kKnop, 0x04, kStop}),
        {0x05, kVp, 0x03, kFPi, 0x04, kStop},
        "pass should collapse single sign before empty-op digit");
    require_same_display(
        optimized_codes({0x05, kVp, 0x03, kFPi, kSignChange, kKnop, 0x04, kStop}),
        {0x05, kVp, 0x03, kFPi, kSignChange, kKnop, 0x04, kStop},
        "single sign before empty-op digit rewrite should preserve display");
  }

  {
    require_same_display({0x05, kVp, 0x03, kFPi, kSignChange, kClearX, kStop},
                         {0x05, kVp, 0x03, kFPi, kClearX, kStop},
                         "VP-context sign before dead X2 overwrite should be removable");
    require_same_display({0x05, kVp, kSignChange, kKnop, kClearX, kStop},
                         {0x05, kVp, kKnop, kClearX, kStop},
                         "VP-context sign before empty dead overwrite should be removable");
    require_same_display({0x05, kVp, kK1, kK2, kClearX, 0x10, kStop},
                         {0x05, kVp, kClearX, 0x10, kStop},
                         "empty VP restore before dead overwrite should be removable");
    require_same_display({0x05, kVp, 0x03, kFPi, kSignChange, kClearX, 0x10, kStop},
                         {0x05, kVp, 0x03, kFPi, kClearX, 0x10, kStop},
                         "dead X remains dead even before a later binary op");

    require_equal_codes(optimized_codes({0x05, kVp, 0x03, kFPi, kSignChange, kClearX, kStop}),
                        {0x05, kVp, 0x03, kClearX, kStop},
                        "pass should remove dead VP-context sign restore before Cx");
    require_same_display(
        optimized_codes({0x05, kVp, 0x03, kFPi, kSignChange, kClearX, kStop}),
        {0x05, kVp, 0x03, kFPi, kSignChange, kClearX, kStop},
        "dead VP-context sign restore rewrite should preserve display");

    require_equal_codes(optimized_codes({0x05, kVp, kK1, kK2, kClearX, kStop}),
                        {0x05, kClearX, kStop},
                        "pass should remove dead empty VP restore before Cx");
    require_same_display(optimized_codes({0x05, kVp, kK1, kK2, kClearX, kStop}),
                         {0x05, kVp, kK1, kK2, kClearX, kStop},
                         "dead empty VP restore rewrite should preserve display");
  }

  {
    require_same_display({0x00, 0x02, kF0, kSignChange, kSignChange, kStop},
                         {0x00, 0x02, kF0, kStop},
                         "closed decimal sign pair should collapse away from VP context");
    require_equal_codes(optimized_codes({0x00, 0x02, kF0, kSignChange, kSignChange, kStop}),
                        {0x00, 0x02, kF0, kStop},
                        "pass should remove closed decimal sign pair away from VP context");
    require_same_display(
        optimized_codes({0x00, 0x02, kF0, kSignChange, kSignChange, kStop}),
        {0x00, 0x02, kF0, kSignChange, kSignChange, kStop},
        "closed decimal sign pair rewrite should preserve display");

    require_different_display({kClearX, kSignChange, kSignChange, kVp, kStop},
                              {kClearX, kVp, kStop},
                              "closed sign pair before VP should be kept when context differs");
    require_equal_codes(optimized_codes({kClearX, kSignChange, kSignChange, kVp, kStop}),
                        {kClearX, kSignChange, kSignChange, kVp, kStop},
                        "pass should keep closed sign pair before VP when context differs");
  }

  {
    require_same_display({0x02, kF0, kSignChange, 0x03, kStop}, {0x02, kF0, 0x03, kStop},
                         "closed-context sign before fresh digit should collapse");
    require_same_display({0x02, kF0, kKnop, kSignChange, kK1, 0x03, kStop},
                         {0x02, kF0, 0x03, kStop},
                         "closed-context empty/sign run before fresh digit should collapse");
    require_different_display({0x02, kF0, kSignChange, kVp, 0x03, kStop},
                              {0x02, kF0, kVp, 0x03, kStop},
                              "closed-context sign before VP should not collapse as digit entry");

    require_equal_codes(
        optimized_codes({0x02, kF0, kSignChange, kKnop, kSignChange, 0x03, kStop}),
        {0x02, kF0, 0x03, kStop},
        "pass should collapse closed-context restore run before fresh digit");
    require_same_display(
        optimized_codes({0x02, kF0, kSignChange, kKnop, kSignChange, 0x03, kStop}),
        {0x02, kF0, kSignChange, kKnop, kSignChange, 0x03, kStop},
        "closed-context restore run rewrite should preserve display");
  }

  {
    require_same_display({0x02, kF0, kVp, kSignChange, kSignChange, 0x03, kStop},
                         {0x02, kF0, kVp, 0x03, kStop},
                         "closed decimal X2 sync should expose exponent-entry sign toggles");
    require_same_display({0x02, kF0, kSignChange, kSignChange, kVp, 0x03, kStop},
                         {0x02, kF0, kVp, 0x03, kStop},
                         "closed sign pair before VP should collapse with decimal context");
    require_same_display({0x00, 0x02, kSignChange, kSignChange, kVp, 0x03, kStop},
                         {0x00, 0x02, kVp, 0x03, kStop},
                         "visible decimal pair before VP should collapse");
    require_different_display({kClearX, kSignChange, kSignChange, kVp, 0x03, kStop},
                              {kClearX, kVp, 0x03, kStop},
                              "Cx sign pair before VP should not collapse");
    require_different_display({0x00, kSignChange, kSignChange, kVp, 0x03, kStop},
                              {0x00, kVp, 0x03, kStop},
                              "zero sign pair before VP should not collapse without proof");
    require_different_display({0x02, kF0, kStore1, kVp, 0x03, kStop},
                              {0x02, kF0, kVp, 0x03, kStop},
                              "store before VP should block decimal sync proof");

    require_equal_codes(
        optimized_codes({0x02, kF0, kVp, kSignChange, kSignChange, 0x03, kStop}),
        {0x02, kF0, kVp, 0x03, kStop},
        "pass should remove exponent-entry sign pair after decimal X2 sync");
    require_same_display(
        optimized_codes({0x02, kF0, kVp, kSignChange, kSignChange, 0x03, kStop}),
        {0x02, kF0, kVp, kSignChange, kSignChange, 0x03, kStop},
        "exponent-entry sign pair after decimal X2 sync should preserve display");

    require_equal_codes(
        optimized_codes({0x02, kF0, kSignChange, kSignChange, kVp, 0x03, kStop}),
        {0x02, kF0, kVp, 0x03, kStop},
        "pass should remove decimal sign pair before VP");
    require_same_display(
        optimized_codes({0x02, kF0, kSignChange, kSignChange, kVp, 0x03, kStop}),
        {0x02, kF0, kSignChange, kSignChange, kVp, 0x03, kStop},
        "decimal sign pair before VP rewrite should preserve display");

    require_equal_codes(
        optimized_codes({0x00, 0x02, kSignChange, kSignChange, kVp, 0x03, kStop}),
        {0x00, 0x02, kVp, 0x03, kStop},
        "pass should remove visible decimal sign pair before VP");
    require_same_display(
        optimized_codes({0x00, 0x02, kSignChange, kSignChange, kVp, 0x03, kStop}),
        {0x00, 0x02, kSignChange, kSignChange, kVp, 0x03, kStop},
        "visible decimal sign pair before VP rewrite should preserve display");
  }
}

} // namespace mkpro::tests
