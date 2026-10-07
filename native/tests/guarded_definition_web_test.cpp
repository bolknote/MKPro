#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using namespace mkpro::core::passes;
using mkpro::tests::require;

struct Fixture {
  std::vector<IrOp> ops;
  std::size_t old_definition = 0;
  std::size_t new_definition = 0;
  std::size_t use = 0;
};

Fixture fixture(int payload, int guard, bool exit_guard = true, bool bypass_write = false,
                bool selecting = true) {
  MachineEmitter e;
  e.emit_jump(0x53, "call", "work");
  e.emit_stop(StopDisposition::Terminal);
  e.emit_label("work");
  e.emit_op(0x0d);
  e.emit_number("-1");
  e.emit_op(0x40 + guard);
  e.emit_number("8");
  e.emit_op(0x40 + payload);
  e.emit_number("2");
  e.emit_op(0xb0 + payload, std::nullopt, "old-definition");
  e.items.back().indirect_memory_targets = std::vector<int>{7};
  e.emit_op(0x69);
  e.emit_op(0x60 + guard);
  e.emit_op(selecting ? 0x36 : 0x10);
  e.emit_op(0x40 + guard);
  e.emit_op(0x11);
  e.emit_jump(0x5e, "unequal", "next");
  if (bypass_write) {
    e.emit_op(0x66);
    e.emit_jump(0x5e, "bypass", "next");
  }
  e.emit_number("4");
  e.emit_op(0x40 + payload, std::nullopt, "new-definition");
  e.emit_label("next");
  if (exit_guard) {
    e.emit_op(0x60 + guard);
    e.emit_jump(0x5c, "nonnegative", "live");
    e.emit_number("0");
    e.emit_op(0x52);
  }
  e.emit_label("live");
  e.emit_op(0x60 + payload, std::nullopt, "queried-read");
  e.emit_op(0x43);
  e.emit_jump(0x5a, "fixed counter", "after");
  e.items[e.items.size() - 2U].roles = {"fixed-counter-abi"};
  e.emit_label("after");
  e.emit_op(0x63);
  e.emit_op(0x52);
  Fixture result{.ops = raise_machine_to_ir(e.items)};
  for (std::size_t i = 0; i < result.ops.size(); ++i) {
    const auto& comment = result.ops[i].meta.comment;
    if (comment == "old-definition") result.old_definition = i;
    if (comment == "new-definition") result.new_definition = i;
    if (comment == "queried-read") result.use = i;
  }
  return result;
}

GuardedDefinitionUseQuery query(const Fixture& f, bool old) {
  return {old ? f.old_definition : f.new_definition, f.use, f.ops[f.use].register_name};
}

auto exclusions(const Fixture& f) {
  return prove_unreachable_definition_uses(f.ops,
      build_post_layout_control_flow(lower_ir_to_machine(f.ops)), {query(f, true), query(f, false)});
}

PassResult optimize(const Fixture& f) {
  CompileOptions options;
  options.coalesce_copies = true;
  return register_web_copy_coalesce(f.ops, PassContext{options});
}

std::vector<std::string> observe(const std::vector<IrOp>& ops, int candidate,
                                 const std::string& seed, bool bypass = false) {
  const auto resolved = resolve_machine_items(lower_ir_to_machine(ops));
  require(resolved.diagnostics.empty(), "guarded fixture must resolve");
  std::vector<int> bytes;
  for (const auto& step : resolved.steps) bytes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(bytes).diagnostics.empty(), "guarded fixture must fit ROM");
  for (const char* reg : {"X", "Y", "Z", "T", "X1", "3"}) calc.set_register(reg, seed);
  calc.set_register("9", std::to_string(candidate));
  calc.set_register("6", bypass ? "1" : "0");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(3000, 8).stopped, "guarded fixture must return to its caller");
  std::vector<std::string> result;
  for (const char* reg : {"X", "Y", "Z", "T", "X1", "7"}) result.push_back(calc.read_register(reg));
  // Private dead payload/counter epochs are intentionally not observations.
  // Their surviving values are exposed by real program recalls instead.
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}

void compare_rom(const Fixture& original, const PassResult& result, bool bypass = false) {
  require(build_post_layout_control_flow(lower_ir_to_machine(result.ops)).proved,
          "recolored code must retain exact final caller/return contexts");
  for (const int candidate : {-2, -1, 0, 1, 9})
    for (const std::string seed : {"0", "-7", "3.25"})
      require(observe(original.ops, candidate, seed, bypass) ==
                  observe(result.ops, candidate, seed, bypass),
              "guarded coalescing must preserve ROM stack, X1, X2, bank writes and returns");
}

void run_tests() {
  for (const int payload : {0, 1, 2}) {
    for (const int guard : {8, 12}) {
      const Fixture positive = fixture(payload, guard);
      const auto proved = exclusions(positive);
      require(proved.contains(query(positive, true)),
              "old hardware epoch cannot reach the guarded payload use");
      require(!proved.contains(query(positive, false)),
              "the selected payload definition remains live");
      const auto result = optimize(positive);
      require(result.applied == 1 && result.ops.size() + 1U == positive.ops.size(),
              "guarded web splitting must remove the copy into the fixed counter");
      require(std::any_of(result.optimizations.begin(), result.optimizations.end(), [](const auto& p) {
        return p.name == "guarded-definition-webs";
      }), "actual copy removal must report its guarded proof");
      require(optimize(Fixture{.ops = result.ops}).applied == 0,
              "guarded coalescing must reach a fixed point");
      compare_rom(positive, result);
    }
  }
  for (const auto& negative : {fixture(0, 8, false), fixture(1, 12, true, true),
                              fixture(2, 8, true, false, false)}) {
    require(!exclusions(negative).contains(query(negative, true)),
            "missing guards, optional writes and nonselect operations cannot erase old-value edges");
    const auto result = optimize(negative);
    require(result.applied == 0, "observable old hardware epoch must block incompatible web merge");
    compare_rom(negative, result, true);
  }
  auto unknown = fixture(0, 8);
  unknown.ops[unknown.old_definition].meta.indirect_memory_targets.reset();
  require(exclusions(unknown).empty() && optimize(unknown).applied == 0,
          "unknown indirect aliasing must fail closed");
  auto raw = fixture(0, 8);
  raw.ops[raw.old_definition].meta.raw = true;
  require(exclusions(raw).empty() && optimize(raw).applied == 0,
          "raw hardware state cannot authorize web splitting");
  const auto f = fixture(0, 8);
  auto bad_flow = build_post_layout_control_flow(lower_ir_to_machine(f.ops));
  bad_flow.proved = false;
  require(prove_unreachable_definition_uses(f.ops, bad_flow, {query(f, true)}).empty(),
          "an unproved CFG cannot authorize lifetime refinement");
  std::vector<GuardedDefinitionUseQuery> excessive(65U, query(f, true));
  require(prove_unreachable_definition_uses(f.ops,
      build_post_layout_control_flow(lower_ir_to_machine(f.ops)), excessive).empty(),
      "query-budget exhaustion must retain every incumbent edge");
}
} // namespace

int main() {
  try {
    run_tests();
    std::cout << "guarded definition-web compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
