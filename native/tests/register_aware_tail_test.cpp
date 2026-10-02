#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/passes/shared_call_tail.hpp"
#include "mkpro/core/passes/shared_terminal_tail.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "ir_pass_test_support.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {
namespace {
using namespace irbuild;
namespace passes = core::passes;

CompileOptions sharing_options() {
  CompileOptions options;
  options.coalesce_copies = true;
  options.register_aware_tail_sharing = true;
  return options;
}

IrOp operation(int opcode) { return plain(opcode, "fixture"); }

IrOp stop(bool terminal) {
  IrOp result = terminal ? halt() : pause();
  result.meta.stop_disposition = terminal ? StopDisposition::Terminal : StopDisposition::Resumable;
  return result;
}

int cells(const std::vector<IrOp>& ops) {
  int result = 0;
  for (const auto& op : ops)
    result += passes::cells_per_op(op);
  return result;
}

bool shared_after_recoloring(const passes::PassResult& result) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [](const auto& item) { return item.name == "register-aware-tail-sharing"; });
}

std::vector<std::vector<IrOp>> recolor(
    const std::vector<IrOp>& ops,
    const std::vector<passes::RegisterWebRecoloringRequest>& requests,
    const CompileOptions& options = sharing_options()) {
  std::vector<std::vector<IrOp>> results;
  passes::for_each_register_web_recoloring(ops, {options}, requests,
      [&](std::vector<IrOp> candidate) { results.push_back(std::move(candidate)); });
  return results;
}

std::vector<IrOp> region_fixture(const std::string& first = "4",
                                  const std::string& second = "5") {
  return {
      recall("e"), call("outer_left"), stop(false), operation(0x0c), operation(2), stop(false),
      recall("d"), call("outer_right"), stop(false), operation(0x0c), operation(2), stop(true),
      label("outer_left"), call("left"), ret(),
      label("outer_right"), call("right"), ret(),
      label("left"), store(first), recall(first), operation(0x0e), operation(0x10), ret(),
      label("right"), store(second), recall(second), operation(0x0e), operation(0x10), ret(),
  };
}

std::vector<std::string> observe(const std::vector<IrOp>& ops,
                                 const std::string& first, const std::string& second) {
  const auto machine = lower_ir_to_machine(ops);
  const auto flow = core::build_post_layout_control_flow(machine);
  require(flow.proved, "shared-region fixture must retain an exact matched-call CFG");
  const auto resolved = resolve_machine_items(machine, {});
  require(resolved.diagnostics.empty(), "shared-region addresses must resolve");
  std::vector<int> bytes;
  for (const auto& step : resolved.steps)
    bytes.push_back(step.opcode);
  require(bytes.size() <= 105, "shared-region ROM fixture must fit stock memory");
  emulator::MK61 calc;
  require(calc.load_program(bytes).diagnostics.empty(), "shared-region ROM fixture must load");
  calc.set_register("e", first);
  calc.set_register("d", second);
  calc.set_register("b", "73");
  calc.set_register("c", "29");
  calc.set_register("X", "5");
  calc.set_register("Y", "17");
  calc.set_register("Z", "31");
  calc.set_register("T", "43");
  calc.set_register("X1", "59");
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  std::vector<std::string> result;
  for (int phase = 0; phase < 4; ++phase) {
    require(calc.run_until_stable(4000, 6).stopped,
            "shared-region nested return must reach every declared stop");
    result.push_back(calc.display_text());
    for (const char* reg : {"X", "Y", "Z", "T", "X1", "b", "c", "d", "e"})
      result.push_back(calc.read_register(reg));
    if (phase != 3)
      calc.press("\u0421/\u041f");
  }
  return result;
}

} // namespace

void register_aware_tail_sharing_preserves_web_contracts() {
  using namespace irbuild;
  const auto options = sharing_options();
  const std::vector<IrOp> disjoint{
      operation(1), store("4"), recall("4"), operation(2), store("5"), recall("5"), stop(true)};
  const auto variants = recolor(disjoint, {{{1, 4}, {2, 5}}});
  require(variants.size() == 1 && variants[0].size() == disjoint.size() &&
              variants[0][1].register_name == variants[0][4].register_name &&
              variants[0][2].register_name == variants[0][5].register_name,
          "disjoint web constraints must recolor without deleting stores or requiring a copy");
  require(ir_ops_to_json(recolor(disjoint, {{{1, 4}, {2, 5}}})[0]) ==
              ir_ops_to_json(variants[0]), "web recoloring must be deterministic");
  require(recolor(disjoint, {{{999, 1}}, {{0, 1}}, {{1, 6}}, {}}).empty(),
          "invalid, non-register and empty operand requests must fail closed");

  const std::vector<IrOp> conflicts{
      operation(1), store("4"), operation(2), store("5"), recall("4"), recall("5"),
      operation(3), store("6"), recall("6"), stop(true)};
  require(recolor(conflicts, {{{1, 3}}}).empty(),
          "joining simultaneously live distinct values must be rejected before coloring");
  require(recolor(conflicts, {{{1, 3}}, {{3, 7}}}).size() == 1,
          "a rejected request must not contaminate the next independent partition");
  require(recolor({recall("4"), recall("5"), operation(0x10), stop(true)}, {{{0, 1}}}).empty(),
          "different setup-time entry values must keep their original physical homes");

  const std::vector<IrOp> pool{
      recall("e"), operation(1), store("4"), recall("4"), stop(true)};
  require(recolor(pool, {{{0, 2}}}).size() == 1,
          "an ordinary dead entry value may share its physical home with a later epoch");
  auto pooled = options;
  pooled.preloaded_constant_registers["e"] = "2";
  require(recolor(pool, {{{0, 2}}}, pooled).empty(),
          "an immutable pool must remain protected even after its last current read");

  auto raw = disjoint;
  raw[1].meta.raw = true;
  require(recolor(raw, {{{1, 4}}}).empty(), "raw instructions must block speculative recoloring");
  auto manual = disjoint;
  manual[1].meta.manual_interaction.emplace();
  manual[4].meta.manual_interaction.emplace();
  require(recolor(manual, {{{1, 4}}}).empty(), "manual register assignments must stay anchored");
  auto roles = disjoint;
  roles[1].meta.roles.push_back("fixture-fixed-home");
  roles[4].meta.roles.push_back("fixture-fixed-home");
  require(recolor(roles, {{{1, 4}}}).empty(), "layout-role operands must stay anchored");
  auto unreachable = disjoint;
  unreachable.insert(unreachable.end(), {store("7"), recall("7")});
  require(recolor(unreachable, {{{1, 7}}}).empty(), "unreachable operands are not entry proofs");
  auto unknown = disjoint;
  unknown.insert(unknown.end() - 1, indirect_recall("e"));
  require(recolor(unknown, {{{1, 4}}}).empty(), "unknown indirect aliases must reject recoloring");
  auto fixed = disjoint;
  fixed[0] = numeric_jump(1);
  require(recolor(fixed, {{{1, 4}}}).empty(), "numeric control-flow geometry must remain frozen");
  auto malformed = disjoint;
  malformed[1].opcode = 0x45;
  require(recolor(malformed, {{{1, 4}}}).empty(), "operand/opcode disagreement must fail closed");
  auto extended = disjoint;
  extended[1].register_name = "f";
  extended[1].opcode = 0x4f;
  require(recolor(extended, {{{1, 4}}}).empty(), "ordinary recoloring must not invent Rf");
  auto disabled = options;
  disabled.coalesce_copies = false;
  require(recolor(disjoint, {{{1, 4}}}, disabled).empty(), "allocation opt-out must be respected");

  const std::vector<IrOp> counter{
      recall("e"), operation(2), store("0"), label("again"), operation(0x54),
      loop("again"), recall("0"), stop(true)};
  require(recolor(counter, {{{0, 2}}}).empty(), "an FL epoch cannot be assigned outside R0-R3");

  for (const auto& homes : {std::pair{"4", "5"}, std::pair{"8", "a"}, std::pair{"2", "3"}}) {
    const auto original = region_fixture(homes.first, homes.second);
    auto ordinary_options = options;
    ordinary_options.register_aware_tail_sharing = false;
    const auto ordinary = passes::shared_terminal_tail(original, {ordinary_options});
    const auto optimized = passes::shared_terminal_tail(original, {options});
    require(cells(original) == 30 && cells(ordinary.ops) == 29 && cells(optimized.ops) == 27 &&
                shared_after_recoloring(optimized),
            "register-aware sharing must beat literal suffix sharing by two cells, independently of names");
    require(ir_ops_to_json(passes::shared_terminal_tail(original, {options}).ops) ==
                ir_ops_to_json(optimized.ops), "shared region selection must be deterministic");
    auto no_interprocedural = options;
    no_interprocedural.disable_interprocedural_opts = true;
    require(!shared_after_recoloring(passes::shared_terminal_tail(original, {no_interprocedural})),
            "interprocedural opt-out must prevent the new allocation/sharing package");
  }
}

void emulator_register_aware_tail_sharing_matches_original() {
  const auto original = region_fixture();
  const auto optimized = core::passes::shared_terminal_tail(original, {sharing_options()});
  require(shared_after_recoloring(optimized), "ROM regression must exercise the new optimization");
  for (const auto& inputs : {std::pair{"0", "7"}, std::pair{"-11.25", "0.125"},
                            std::pair{"7777777", "99"}}) {
    require(observe(original, inputs.first, inputs.second) ==
                observe(optimized.ops, inputs.first, inputs.second),
            "recoloring and sharing must preserve displays, X/Y/Z/T/X1, VP-observable X2, "
            "setup values and both nested return continuations");
  }
}

void shared_call_tail_preserves_protected_occurrences() {
  using namespace irbuild;
  std::vector<IrOp> original;
  for (int index = 0; index < 7; ++index) {
    original.push_back(label("case_" + std::to_string(index)));
    IrOp invocation = call("leaf");
    IrOp continuation = jump("done");
    if (index == 3)
      invocation.meta.raw = true;
    if (index == 4)
      invocation.meta.manual_interaction.emplace();
    if (index == 5)
      continuation.meta.raw = true;
    if (index == 6)
      continuation.meta.manual_interaction.emplace();
    original.push_back(std::move(invocation));
    original.push_back(std::move(continuation));
  }
  original.insert(original.end(), {label("done"), stop(true), label("leaf"), ret()});
  const auto optimized = core::passes::shared_call_tail(original, {sharing_options()});
  require(optimized.applied == 3 && cells(optimized.ops) == cells(original) - 2,
          "only three unprotected call/jump pairs may share the helper");
  for (int index = 3; index < 7; ++index) {
    const std::string name = "case_" + std::to_string(index);
    const auto entry = std::find_if(optimized.ops.begin(), optimized.ops.end(),
        [&](const IrOp& op) { return op.kind == IrKind::Label && op.name == name; });
    require(entry != optimized.ops.end() && optimized.ops.end() - entry >= 3,
            "every protected occurrence must retain its entry label and both commands");
    const auto offset = static_cast<std::size_t>(index * 3 + 1);
    require_ops_equal({*(entry + 1), *(entry + 2)}, {original[offset], original[offset + 1]},
                      "protected call/jump occurrence");
  }
}

} // namespace mkpro::tests
