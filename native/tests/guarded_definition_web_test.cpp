#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/layout_search_artifact.hpp"
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


std::vector<IrOp> input_copy_fixture(int input, int counter, bool pin_input=false,
                                      bool expose_input=false) {
 MachineEmitter e;
 const auto anchor=[&](ManualInteractionAnchorKind kind,int phase) {
   ManualInteractionAnchor a; a.protocol_id=7;a.phase=phase;a.kind=kind;
   e.items.back().manual_interaction=a;
 };
 e.emit_number("0");
 e.emit_stop(StopDisposition::Resumable);
 anchor(ManualInteractionAnchorKind::PromptStop,-1);
 e.emit_op(0x40+input,std::nullopt,"manual-store");
 anchor(ManualInteractionAnchorKind::SingleStepCommand,0);
 if(pin_input)e.items.back().roles.push_back("fixed-input-contract");
 e.emit_op(0x44);
 anchor(ManualInteractionAnchorKind::ContinuousResume,1);
 e.emit_jump(0x53,"call","consume");
 e.emit_number("2");
 e.emit_op(0x40+counter);
 const int codes[]={0x5d,0x5b,0x58,0x5a};
 e.emit_jump(codes[counter],"counter","prepared");
 e.items[e.items.size()-2U].roles={"fixed-counter-abi"};
 e.emit_label("prepared");
 e.emit_op(0x60+counter);
 e.emit_op(0x40+input,std::nullopt,"staging-copy");
 e.emit_jump(0x53,"call","consume");
 // A private staging home is explicitly overwritten before final observation.
 if (!expose_input) { e.emit_number("0"); e.emit_op(0x40 + input); }
 e.emit_stop(StopDisposition::Terminal);
 e.emit_label("consume");
 e.emit_op(0x60+input);
 e.emit_op(0x10);
 e.emit_op(0x52);
 return raise_machine_to_ir(e.items);
}


std::vector<std::string> input_copy_observe(const std::vector<IrOp>& ops,
                                          int first, int second, int candidate,
                                          int rounds = 1, bool whole_bank = false) {
  const auto resolved = resolve_machine_items(lower_ir_to_machine(ops));
  require(resolved.diagnostics.empty(), "manual web fixture must resolve");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "manual web fixture fits stock memory");
  for (const char* name : {"X", "Y", "Z", "T", "X1", "0", "1", "2", "3", "4", "5", "6",
                           "7", "8", "9", "a", "b", "c", "d", "e"})
    calc.set_register(name, "3.25");
  calc.set_register("9", std::to_string(candidate));
  calc.press_sequence({"В/О", "С/П"});
  std::vector<std::string> observations;
  for (int round = 0; round < rounds; ++round) {
    require(calc.run_until_stable(4000, 8).stopped, "manual protocol reaches its prompt");
    observations.push_back(calc.display_text(true));
    calc.press(std::to_string(first));
    calc.press("ПП");
    observations.push_back(calc.display_text(true));
    for (const char* name : {"X", "Y", "Z", "T", "X1"})
      observations.push_back(calc.read_register(name));
    calc.press(std::to_string(second));
    calc.press("С/П");
  }
  require(calc.run_until_stable(4000, 8).stopped, "manual web fixture returns and terminates");
  for (const char* name : {"X", "Y", "Z", "T", "X1", "4", "7"})
    observations.push_back(calc.read_register(name));
  if (whole_bank)
    for (int r = 0; r < 15; ++r)
      observations.push_back(calc.read_register(std::string(1, "0123456789abcde"[r])));
  calc.press(".");
  observations.push_back(calc.display_text(true));
  return observations;
}

void manual_web_contracts() {
  CompileOptions options;
  options.coalesce_copies = true;
  for (const auto& [input, counter] : std::vector<std::pair<int, int>>{{0, 1}, {2, 0}}) {
    const auto original = input_copy_fixture(input, counter);
    require(register_web_copy_coalesce(original, {options}).applied == 0,
            "ordinary coalescing keeps manual operands anchored");
    const auto result = deferred_register_web_copy_coalesce(original, {options}, {}, {});
    require(result.applied == 1 && result.ops.size() + 1U == original.size(),
            "proved manual register transport removes exactly the staging store");
    const auto manual = std::find_if(result.ops.begin(), result.ops.end(),
        [](const auto& op) { return op.meta.comment == "manual-store"; });
    require(manual != result.ops.end() && manual->opcode == 0x40 + counter &&
                manual->meta.manual_interaction &&
                manual->meta.manual_interaction->kind == ManualInteractionAnchorKind::SingleStepCommand,
            "manual phase stays one direct store, with only its physical register changed");
    std::vector<ManualInteractionAnchor> old_phases, new_phases;
    for (const auto& op : original)
      if (op.meta.manual_interaction) old_phases.push_back(*op.meta.manual_interaction);
    for (const auto& op : result.ops)
      if (op.meta.manual_interaction) new_phases.push_back(*op.meta.manual_interaction);
    require(old_phases == new_phases && new_phases.size() == 3U,
            "manual protocol identity, phase order and phase count must remain unchanged");
    require(build_post_layout_control_flow(lower_ir_to_machine(result.ops)).proved,
            "manual store transport preserves final command and return boundaries");
    for (int first : {0, 3, 9})
      for (int second : {0, 4, 8})
        require(input_copy_observe(original, first, second, 0, 1, true) ==
                    input_copy_observe(result.ops, first, second, 0, 1, true),
                "manual PP/CP protocol, stack, X1/X2, output stores and returns must match ROM");
    require(deferred_register_web_copy_coalesce(result.ops, {options}, {}, {}).applied == 0,
            "manual web closure reaches a fixed point");
    const auto exposed = input_copy_fixture(input, counter, false, true);
    const auto preserved = deferred_register_web_copy_coalesce(exposed, {options}, {}, {});
    require(preserved.applied == 0,
            "terminal bank outputs stay observable without a program recall");
    require(input_copy_observe(exposed, 3, 4, 0, 1, true) ==
                input_copy_observe(preserved.ops, 3, 4, 0, 1, true),
            "terminal register observations must remain identical");
    auto pinned = input_copy_fixture(input, counter, true);
    require(deferred_register_web_copy_coalesce(pinned, {options}, {}, {}).applied == 0,
            "other input roles remain pinned");
    auto broken = original;
    for (auto& op : broken)
      if (op.meta.comment == "manual-store") op.meta.manual_interaction->phase = 2;
    require(deferred_register_web_copy_coalesce(broken, {options}, {}, {}).applied == 0,
            "a broken input protocol must not authorize register transport");
    auto raw = original;
    raw.front().meta.raw = true;
    require(deferred_register_web_copy_coalesce(raw, {options}, {}, {}).applied == 0,
            "raw effects remain barriers");
  }
}


std::vector<IrOp> loop_input_copy_fixture(int input, int payload, bool live_old = false) {
 MachineEmitter e;
 const auto anchor=[&](ManualInteractionAnchorKind kind,int phase) {
   ManualInteractionAnchor a;a.protocol_id=9;a.phase=phase;a.kind=kind;
   e.items.back().manual_interaction=a;
 };
 e.emit_number("2");e.emit_op(0x43);
 e.emit_label("round");
 e.emit_number("0");e.emit_stop(StopDisposition::Resumable);
 anchor(ManualInteractionAnchorKind::PromptStop,-1);
 e.emit_op(0x40+input,std::nullopt,"manual-store");
 anchor(ManualInteractionAnchorKind::SingleStepCommand,0);
 e.emit_op(0x44);anchor(ManualInteractionAnchorKind::ContinuousResume,1);
 e.emit_jump(0x53,"call","consume");
 e.emit_number("-1");e.emit_op(0x48);
 e.emit_number("8");e.emit_op(0x40+payload);
 e.emit_number("2");
 e.emit_op(0xb0+payload);
 e.items.back().indirect_memory_targets=std::vector<int>{7};
 e.emit_op(0x69);e.emit_op(0x68);e.emit_op(0x36);e.emit_op(0x48);e.emit_op(0x11);
 e.emit_jump(0x5e,"not selected","next");
 e.emit_number("4");e.emit_op(0x40+payload);
 e.emit_label("next");
 if(!live_old){e.emit_op(0x68);e.emit_jump(0x5c,"nonnegative","selected");e.emit_jump(0x51,"skip","finish");e.emit_label("selected");}
 e.emit_op(0x60+payload);e.emit_op(0x40+input,std::nullopt,"staging-copy");
 e.emit_jump(0x53,"call","consume");
 e.emit_label("finish");
 e.emit_jump(0x5a,"repeat","round");
 e.items[e.items.size()-2U].roles={"fixed-counter-abi"};
 e.emit_stop(StopDisposition::Terminal);
 e.emit_label("consume");e.emit_op(0x60+input);e.emit_op(0x10);e.emit_op(0x52);
 return raise_machine_to_ir(e.items);
}


void available_definition_contracts() {
  CompileOptions options;
  options.coalesce_copies = true;
  for (const auto& [input, payload] : std::vector<std::pair<int, int>>{{1, 0}, {2, 1}}) {
    const auto original = loop_input_copy_fixture(input, payload);
    const auto result = register_web_copy_coalesce(original, {options});
    require(result.applied == 1 && result.ops.size() + 1U == original.size(),
            "a guarded old value cannot remain live across its physical overwrite");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                [](const auto& p) { return p.name == "guarded-definition-webs"; }),
            "the loop regression exercises guarded def/use exclusions");
    for (int candidate : {-2, -1, 0, 1, 9})
      for (int input_value : {0, 3, 9})
        require(input_copy_observe(original, input_value, 4, candidate, 2) ==
                    input_copy_observe(result.ops, input_value, 4, candidate, 2),
                "available-value liveness preserves both loop iterations and input boundaries");
    const auto live = loop_input_copy_fixture(input, payload, true);
    const auto unchanged = register_web_copy_coalesce(live, {options});
    require(unchanged.applied == 0,
            "removing the guard keeps the hardware epoch genuinely live");
    require(input_copy_observe(live, 3, 4, -2, 2) ==
                input_copy_observe(unchanged.ops, 3, 4, -2, 2),
            "the negative live-epoch fixture is executable on ROM");
  }
}


LayoutSearchArtifact callback_web_fixture(int carrier, int counter, int selector) {
   MachineEmitter e;
  const auto charge = [&](const std::string& name) {
    for (auto part : {LateBoundDecimalSelectorPart::High, LateBoundDecimalSelectorPart::Low}) {
      e.emit_op(0);
      e.items.back().roles = {make_late_bound_decimal_selector_role(part, name),
          "late-decimal-selector-register:" + std::string(1, "0123456789abcde"[selector])};
    }
    e.emit_op(0x40 + selector);
    e.emit_jump(0x53, "call", "dispatch");
  };
  e.emit_number("2"); e.emit_op(0x40 + carrier);
  charge("first");
  e.emit_number("8"); e.emit_op(0x40 + carrier);
  charge("second");
  // Address words are internal: overwrite every observable stack slot before STOP.
  for (int code : {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10}) e.emit_op(code);
  e.emit_stop(StopDisposition::Terminal);
  e.emit_label("dispatch");
  e.emit_op(0xa0 + selector);
  e.items.back().indirect_flow_targets = std::vector<IrTarget>{std::string("first"),std::string("second")};
  e.items.back().roles = {"late-decimal-selector-consumer"};
  e.emit_op(0x52);
  e.emit_label("first");
  e.emit_op(0x60 + carrier); e.emit_op(0x40 + counter);
  const int counters[] = {0x5d,0x5b,0x58,0x5a};
  e.emit_jump(counters[counter], "counter", "first_done");
  e.items[e.items.size()-2U].roles = {"fixed-counter-abi"};
  e.emit_label("first_done");
  e.emit_op(0x60 + counter); e.emit_op(0x52);
  e.emit_label("second");
  e.emit_number("1"); e.emit_op(0xb0 + carrier);
  e.items.back().indirect_memory_targets = std::vector<int>{7};
  e.emit_op(0x52);
  auto f = rebind_layout_search_artifact({.items=e.items});
  if(!f)throw std::runtime_error("callback fixture did not bind");
  return *f;
}


std::map<std::string, int> web_callback_origins(const LayoutSearchArtifact& f) {
  std::map<std::string, int> result;
  int address = 0;
  for (const auto& item : f.items)
    if (item.kind == MachineItemKind::Label) result.emplace(item.name, address);
    else ++address;
  return result;
}

std::vector<std::string> callback_web_observe(const LayoutSearchArtifact& f, int selector,
                                            const std::string& seed) {
  const auto resolved = resolve_machine_items(f.items);
  require(resolved.diagnostics.empty(), "callback web fixture resolves");
  std::vector<int> bytes;
  for (const auto& step : resolved.steps) bytes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(bytes).diagnostics.empty(), "callback web fixture fits stock memory");
  for (int r = 0; r < 15; ++r)
    calc.set_register(std::string(1, "0123456789abcde"[r]), seed);
  for (const char* r : {"X", "Y", "Z", "T", "X1"}) calc.set_register(r, seed);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(4000, 8).stopped, "nested callbacks return to terminal stop");
  std::vector<std::string> result;
  for (const char* r : {"X", "Y", "Z", "T", "X1"}) result.push_back(calc.read_register(r));
  for (int r = 0; r < 15; ++r)
    if (r != selector) result.push_back(calc.read_register(std::string(1, "0123456789abcde"[r])));
  require(static_cast<int>(std::stod(calc.read_register(
              std::string(1, "0123456789abcde"[selector])))) == web_callback_origins(f).at("second"),
          "final selector word names the relocated callback");
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}

void deferred_callback_web_contracts() {
  CompileOptions options;
  options.coalesce_copies = true;
  for (const auto& registers : std::vector<std::vector<int>>{{1, 0, 9}, {2, 3, 8}}) {
    const auto original = callback_web_fixture(registers[0], registers[1], registers[2]);
    const auto origins = web_callback_origins(original);
    const auto ops = raise_machine_to_ir(original.items);
    require(register_web_copy_coalesce(ops, {options}).applied == 0,
            "unrefined callback union retains the incompatible hardware epoch");
    const auto result = deferred_register_web_copy_coalesce(ops, {options}, {}, origins);
    require(result.applied == 1 && result.ops.size() + 1U == ops.size(),
            "exact selected callbacks permit one copy deletion across distinct epochs");
    const auto closed = rebind_layout_search_artifact({.items = lower_ir_to_machine(result.ops)});
    require(closed && build_post_layout_control_flow(closed->items).proved,
            "a deferred web candidate needs separately rebound final addresses");
    for (const std::string seed : {"0", "-2", "3.25", "8.ABCDEF1"})
      require(callback_web_observe(original, registers[2], seed) ==
                  callback_web_observe(*closed, registers[2], seed),
              "callback refinement preserves every bank value, stack, X1/X2 and nested return");
    auto missing = origins;
    missing.erase("first");
    require(deferred_register_web_copy_coalesce(ops, {options}, {}, missing).applied == 0,
            "a missing origin cannot exclude a real callback");
    auto collision = origins;
    collision["second"] = collision["first"];
    require(deferred_register_web_copy_coalesce(ops, {options}, {}, collision).applied == 0,
            "colliding encoded callback origins are not a proof");
    auto wrong = origins;
    ++wrong["first"];
    require(deferred_register_web_copy_coalesce(ops, {options}, {}, wrong).applied == 0,
            "a contradicted selector charge must not make a live epoch unreachable");
    auto opaque = ops;
    for (auto& op : opaque)
      if (op.meta.indirect_memory_targets) op.meta.indirect_memory_targets.reset();
    require(deferred_register_web_copy_coalesce(opaque, {options}, {}, origins).applied == 0,
            "unknown indirect memory writes remain barriers");
  }
}

void run_tests() {
  deferred_callback_web_contracts();
  available_definition_contracts();
  manual_web_contracts();
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
