#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/pre_shift_stack_lift.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "ir_pass_test_support.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace mkpro::tests {
namespace {
using namespace irbuild;
namespace passes = core::passes;

IrOp op(int opcode) { return plain(opcode, "fixture"); }

IrOp stop(bool terminal) {
  IrOp result = terminal ? halt() : pause();
  result.meta.stop_disposition = terminal ? StopDisposition::Terminal : StopDisposition::Resumable;
  return result;
}

std::vector<IrOp> fixture() {
  return {
      recall("e"), call("outer"),
      recall("1"), recall("2"), recall("3"), recall("4"),
      call("unrelated"), op(0x10), stop(false), op(0x0c), op(2), stop(true),
      label("outer"), call("leaf"), ret(),
      label("leaf"), op(0x22), op(0x0e), store("8"), ret(),
      label("unrelated"), ret(),
  };
}

std::size_t lift_index(const std::vector<IrOp>& ops) {
  const auto found = std::find_if(ops.begin(), ops.end(), [](const IrOp& item) {
    return item.kind == IrKind::Plain && item.opcode == 0x0e;
  });
  require(found != ops.end(), "fixture must contain its Enter lift");
  return static_cast<std::size_t>(found - ops.begin());
}

std::vector<IrOp> without_lift(std::vector<IrOp> ops) {
  ops.erase(ops.begin() + static_cast<std::vector<IrOp>::difference_type>(lift_index(ops)));
  return ops;
}

bool matched_report(const passes::PassResult& result) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [](const auto& item) { return item.name == "matched-return-stack-lift"; });
}

passes::PassResult optimize(const std::vector<IrOp>& ops, bool disabled = false) {
  auto options = noop_options();
  options.disable_interprocedural_opts = disabled;
  return passes::pre_shift_stack_lift(ops, {options});
}

std::vector<std::string> observe(const std::vector<IrOp>& ops, const std::string& seed) {
  const auto items = lower_ir_to_machine(ops);
  require(core::build_post_layout_control_flow(items).proved,
          "fixture must have exact call/return and resume contexts");
  const auto resolved = resolve_machine_items(items, {});
  require(resolved.diagnostics.empty(), "fixture addresses must resolve");
  std::vector<int> bytes;
  for (const auto& step : resolved.steps)
    bytes.push_back(step.opcode);
  require(bytes.size() <= 105, "ROM fixture must fit stock memory");
  emulator::MK61 calculator({.extended = false});
  require(calculator.load_program(bytes).diagnostics.empty(), "ROM fixture must load");
  for (const char* reg : {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                          "a", "b", "c", "d"})
    calculator.set_register(reg, "17");
  calculator.set_register("e", seed);
  calculator.set_register("X", "19");
  calculator.set_register("Y", "23");
  calculator.set_register("Z", "31");
  calculator.set_register("T", "43");
  calculator.set_register("X1", "59");
  calculator.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  std::vector<std::string> result;
  for (int phase = 0; phase < 2; ++phase) {
    require(calculator.run_until_stable(4000, 6).stopped,
            "matched returns must reach the declared stops");
    result.push_back(calculator.display_text());
    for (const char* reg : {"X", "Y", "Z", "T", "X1", "0", "1", "2", "3", "4", "5",
                            "6", "7", "8", "9", "a", "b", "c", "d", "e"})
      result.push_back(calculator.read_register(reg));
    if (phase == 0)
      calculator.press("\u0421/\u041f");
  }
  return result;
}

} // namespace

void matched_return_stack_lift_preserves_contracts() {
  const auto original = fixture();
  const auto result = optimize(original);
  require(result.applied == 1 && matched_report(result),
          "exact caller contexts must remove the lift rejected by joined returns");
  require_ops_equal(result.ops, without_lift(original), "only the proved lift may disappear");
  require_ops_equal(optimize(original).ops, result.ops, "the proof must be deterministic");
  require(optimize(result.ops).applied == 0, "the resulting pass must be idempotent");
  require_ops_equal(optimize(original, true).ops, original, "interprocedural opt-out");

  const auto reject = [](const std::vector<IrOp>& input, const std::string& reason) {
    const auto rejected = optimize(input);
    require(!matched_report(rejected), reason + ": no matched-return proof report");
    require_ops_equal(rejected.ops, input, reason);
  };

  auto live = original;
  live.insert(live.begin() + 2, {op(0x10), store("9")});
  reject(live, "the actual caller consumes the changed Y");

  auto other_caller = original;
  other_caller[6] = call("leaf");
  reject(other_caller, "every caller must hide the difference, not only the first");

  auto early_stop = original;
  early_stop.insert(early_stop.begin() + 2, stop(false));
  reject(early_stop, "full stack equality is required before a resumable stop");

  auto branch = original;
  branch.insert(branch.begin() + 2, {cjump("observed"), jump("washed"),
                                   label("observed"), op(0x10), store("9"), label("washed")});
  reject(branch, "one observing branch is sufficient to reject a deletion");

  auto hidden_restore = original;
  hidden_restore.insert(hidden_restore.begin() + 2, {op(0x14), op(0x0c), op(2), store("9")});
  reject(hidden_restore, "a differing value must not feed exponent restoration");

  for (int offset = 0; offset != 3; ++offset) {
    const auto index = lift_index(original) + static_cast<std::size_t>(offset);
    auto raw = original;
    raw[index].meta.raw = true;
    reject(raw, "raw lift/store/return is protected");
    auto manual = original;
    manual[index].meta.manual_interaction.emplace();
    reject(manual, "manual lift/store/return is protected");
    auto role = original;
    role[index].meta.roles.push_back("fixture-protected-cell");
    reject(role, "layout-role lift/store/return is protected");
    auto display = original;
    display[index].meta.comment = "display-sensitive fixture";
    reject(display, "display-sensitive lift/store/return is protected");
  }

  auto numeric = original;
  numeric.push_back(numeric_jump(0));
  reject(numeric, "numeric geometry cannot move without a retarget transaction");
  auto formal = original;
  formal[1].target_meta.formal_opcode = 0x10;
  reject(formal, "formal direct targets cannot move");
  auto indirect = original;
  indirect.push_back(indirect_jump("e"));
  reject(indirect, "materialized indirect control flow cannot move");
  auto orphan = original;
  orphan.push_back(orphan_address());
  reject(orphan, "executable address operands are not relocatable");

  auto open_entry = original;
  open_entry.insert(open_entry.begin() +
                        static_cast<std::vector<IrOp>::difference_type>(lift_index(open_entry) + 1),
                    op(7));
  reject(open_entry, "a literal after Enter does not have a proved common entry mode");

  auto recursive = original;
  recursive[13] = call("outer");
  reject(recursive, "unreachable or overflowing recursive contexts are not proofs");
}

void emulator_matched_return_stack_lift_matches_original() {
  const auto original = fixture();
  const auto result = optimize(original);
  require(matched_report(result), "ROM equivalence must exercise the new optimization");
  for (const char* seed : {"0", "-11.25", "777.7777"})
    require(observe(original, seed) == observe(result.ops, seed),
            "deletion must preserve all registers, full stack, X1, VP/X2 and nested returns");

  auto live = original;
  live.insert(live.begin() + 2, {op(0x10), store("9")});
  require(!matched_report(optimize(live)), "observable negative fixture must remain unoptimized");
  require(observe(live, "3") != observe(without_lift(live), "3"),
          "the ROM must witness the rejected live-Y counterexample");
}

} // namespace mkpro::tests
