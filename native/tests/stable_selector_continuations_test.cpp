#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "ir_pass_test_support.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace mkpro::tests {
namespace {
using namespace irbuild;

IrOp op(int opcode) { return plain(opcode, "fixture"); }

IrOp stop(bool terminal) {
  IrOp result = terminal ? halt() : pause();
  result.meta.stop_disposition = terminal ? StopDisposition::Terminal : StopDisposition::Resumable;
  return result;
}

IrOp dispatch(const std::string& reg) {
  IrOp result = indirect_call(reg);
  result.meta.indirect_flow_targets = std::vector<IrTarget>{std::string("safe"),
                                                         std::string("observer")};
  return result;
}

std::vector<IrOp> fixture(const std::string& reg = "a") {
  return {recall("e"), dispatch(reg), recall("1"), dispatch(reg),
          recall("2"), recall("3"), recall("4"), recall("5"),
          op(0x10), stop(false), op(0x0c), op(2), stop(true),
          label("safe"), op(0x0e), store("8"), ret(),
          label("observer"), op(0x25), op(0x25), op(0x10), store("9"), ret()};
}

int label_address(const std::vector<MachineItem>& items, const std::string& name) {
  int address = 0;
  for (const auto& item : items) {
    if (item.kind == MachineItemKind::Label && item.name == name)
      return address;
    if (item.kind != MachineItemKind::Label)
      ++address;
  }
  throw std::runtime_error("fixture label is missing");
}

bool lift_is_unobserved(const std::vector<IrOp>& ops) {
  const auto items = lower_ir_to_machine(ops);
  const auto flow = core::build_post_layout_control_flow(items);
  core::StackValueEqualityState difference;
  difference.stack_equal = {true, false, false, false};
  difference.x1_equal = true;
  difference.x2_equal = false;
  return core::prove_post_layout_stack_entry_equality(
      items, flow, label_address(items, "safe") + 1, difference);
}

std::vector<std::string> observe(const std::vector<IrOp>& ops,
                                 const std::string& target, const std::string& seed) {
  const auto items = lower_ir_to_machine(ops);
  const auto resolved = resolve_machine_items(items, {});
  require(resolved.diagnostics.empty(), "ROM fixture must resolve");
  std::vector<int> bytes;
  for (const auto& step : resolved.steps)
    bytes.push_back(step.opcode);
  require(bytes.size() <= 105, "fixture must fit the stock MK-61");
  emulator::MK61 calculator({.extended = false});
  require(calculator.load_program(bytes).diagnostics.empty(), "fixture must load");
  for (const char* reg : {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                          "b", "c", "d"})
    calculator.set_register(reg, "17");
  calculator.set_register("a", std::to_string(label_address(items, target)));
  calculator.set_register("e", seed);
  calculator.set_register("X", "19");
  calculator.set_register("Y", "23");
  calculator.set_register("Z", "31");
  calculator.set_register("T", "43");
  calculator.set_register("X1", "59");
  calculator.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  std::vector<std::string> observations;
  for (int phase = 0; phase < 2; ++phase) {
    require(calculator.run_until_stable(4000, 6).stopped, "fixture must reach its stop");
    observations.push_back(calculator.display_text());
    // Ra is an explicitly relocated code address, not game data.
    for (const char* reg : {"X", "Y", "Z", "T", "X1", "0", "1", "2", "3", "4", "5",
                            "6", "7", "8", "9", "b", "c", "d", "e"})
      observations.push_back(calculator.read_register(reg));
    if (phase == 0)
      calculator.press("\u0421/\u041f");
  }
  return observations;
}

enum class ChargeCase { Direct, ThroughCall, UnknownEntry, UnknownWrite, Resume, Raw };

std::vector<IrOp> charged_fixture(ChargeCase kind = ChargeCase::Direct,
                                  const std::string& reg = "a", bool shortened = false) {
  std::vector<IrOp> ops{recall("e"), store("0"), label("charge_digits"), op(0), op(0)};
  if (kind == ChargeCase::UnknownEntry)
    ops.erase(ops.begin() + 1);
  if (kind == ChargeCase::UnknownWrite)
    ops.push_back(recall("e"));
  ops.push_back(kind == ChargeCase::ThroughCall ? call("charge_store") : store(reg));
  if (kind == ChargeCase::Resume)
    ops.push_back(stop(false));
  if (kind == ChargeCase::Raw) {
    auto opaque = op(0x54);
    opaque.meta.raw = true;
    ops.push_back(std::move(opaque));
  }
  ops.push_back(label("charged_lift"));
  if (!shortened)
    ops.push_back(op(0x0e));
  ops.push_back(dispatch(reg));
  for (const char* r : {"1", "2", "3", "4", "5"}) ops.push_back(recall(r));
  ops.insert(ops.end(), {op(0x10), stop(false), op(0x0c), op(2), stop(true),
                         label("safe"), recall("1"), recall("2"), recall("3"),
                         recall("4"), op(0x10), ret(), label("observer"),
                         op(0x25), op(0x25), op(0x10), store("9"), ret()});
  if (kind == ChargeCase::ThroughCall)
    ops.insert(ops.end(), {label("charge_store"), store(reg), ret()});
  const int target = label_address(lower_ir_to_machine(ops), "safe");
  const auto digits = std::find_if(ops.begin(), ops.end(),
      [](const auto& value) { return value.kind == IrKind::Label && value.name == "charge_digits"; });
  require(digits != ops.end() && target >= 10 && target < 100, "charged fixture needs two digits");
  *(digits + 1) = op(target / 10);
  *(digits + 2) = op(target % 10);
  return ops;
}

bool charged_lift_is_unobserved(const std::vector<IrOp>& ops) {
  const auto items = lower_ir_to_machine(ops);
  const auto flow = core::build_post_layout_control_flow(items);
  core::StackValueEqualityState difference;
  difference.stack_equal = {true, false, false, false};
  difference.x1_equal = true;
  difference.x2_equal = false;
  return core::prove_post_layout_stack_entry_equality(
      items, flow, label_address(items, "charged_lift") + 1, difference);
}

void literal_selector_contracts() {
  for (const auto kind : {ChargeCase::Direct, ChargeCase::ThroughCall})
    for (const char* reg : {"7", "a", "c", "e"}) {
      const auto source = charged_fixture(kind, reg);
      require(charged_lift_is_unobserved(source),
              "a proved selector charge must exclude an impossible FIRST callback");
      const auto items = lower_ir_to_machine(source);
      const auto flow = core::build_post_layout_control_flow(items);
      const auto graph = core::build_selector_consistent_execution_graph(items, flow);
      require(graph.proved && graph.pruned_edges > 0, "literal charge must refine runtime flow");
      const int observer = label_address(items, "observer");
      require(std::none_of(graph.original_states.begin(), graph.original_states.end(),
                          [&](auto state) { return flow.execution_states[state].address == observer; }),
              "a literal proof must eliminate the unreachable callback, not merely later mixed sequences");
    }
  for (const auto kind : {ChargeCase::UnknownEntry, ChargeCase::UnknownWrite,
                         ChargeCase::Resume, ChargeCase::Raw})
    require(!charged_lift_is_unobserved(charged_fixture(kind)),
            "unknown entry/write, user resume and opaque effects must forget the numeric selector");
  require(!charged_lift_is_unobserved(charged_fixture(ChargeCase::Direct, "0")),
          "a delivered literal does not make an auto-mutating selector stable");
}

} // namespace

void stable_selector_continuations_preserve_contracts() {
  literal_selector_contracts();
  const auto original = fixture();
  const auto items = lower_ir_to_machine(original);
  const auto flow = core::build_post_layout_control_flow(items);
  require(flow.proved, "fixture needs authoritative call/return contexts");
  const auto graph = core::build_selector_consistent_execution_graph(items, flow);
  require(graph.proved && graph.pruned_edges > 0,
          "repeated stable dispatch must exclude inconsistent target sequences");
  require(lift_is_unobserved(original),
          "an impossible observer call must not block a stack equality proof");
  const auto repeated = core::build_selector_consistent_execution_graph(items, flow);
  require(graph.original_states == repeated.original_states && graph.successors == repeated.successors,
          "refinement must be deterministic");
  const auto capped = core::build_selector_consistent_execution_graph(items, flow, 1);
  require(!capped.proved && capped.original_states.empty() && capped.successors.empty(),
          "a failed bounded refinement must not publish a partial graph");

  for (const char* reg : {"7", "c", "e"})
    require(lift_is_unobserved(fixture(reg)), "stable selectors are not tied to a fixed register");
  require(!lift_is_unobserved(fixture("0")), "auto-mutating selectors cannot retain a target fact");
  auto other_reg = original;
  other_reg[3] = dispatch("b");
  require(!lift_is_unobserved(other_reg), "independent selector registers need independent choices");
  auto overwritten = original;
  overwritten.insert(overwritten.begin() + 3, store("a"));
  require(!lift_is_unobserved(overwritten), "a direct selector write invalidates its choice");
  for (const auto& targets : {std::vector<int>{10}, std::vector<int>{1, 10}}) {
    auto aliased = original;
    auto write = indirect_store("c");
    write.meta.indirect_memory_targets = targets;
    aliased.insert(aliased.begin() + 3, write);
    require(!lift_is_unobserved(aliased), "definite and possible indirect writes both invalidate");
  }
  auto unrelated = original;
  unrelated.insert(unrelated.begin() + 3, store("b"));
  require(lift_is_unobserved(unrelated), "unrelated writes must not discard a stable selector fact");
  auto resumed = original;
  resumed.insert(resumed.begin() + 3, stop(false));
  require(!lift_is_unobserved(resumed), "observation and user resume remain barriers");
  auto opaque = original;
  auto raw = op(0x54);
  raw.meta.raw = true;
  opaque.insert(opaque.begin() + 3, raw);
  require(!lift_is_unobserved(opaque), "raw commands remain opaque");
  auto missing = original;
  missing[3].meta.indirect_flow_targets.reset();
  require(!lift_is_unobserved(missing), "an incomplete target set cannot prove a continuation");
  auto logical = original;
  logical.insert(logical.end(), 110, op(0x54));
  require(lift_is_unobserved(logical), "oversized logical addresses remain a separate target domain");

  // A conditional's fallthrough supplies no information about its selector.
  auto conditional = indirect_cjump("a");
  conditional.meta.indirect_flow_targets = dispatch("a").meta.indirect_flow_targets;
  const auto conditional_items = lower_ir_to_machine({
      call("choose"), dispatch("a"), stop(true), label("choose"), conditional, ret(),
      label("safe"), ret(), label("observer"), ret()});
  const auto conditional_flow = core::build_post_layout_control_flow(conditional_items);
  const auto conditional_graph = core::build_selector_consistent_execution_graph(
      conditional_items, conditional_flow);
  require(conditional_graph.proved, "conditional fixture must refine");
  bool unknown_fallthrough = false;
  bool known_taken = false;
  for (std::size_t i = 0; i < conditional_graph.original_states.size(); ++i) {
    const auto& state = conditional_flow.execution_states[conditional_graph.original_states[i]];
    if (state.address == 2) {
      unknown_fallthrough = unknown_fallthrough || conditional_graph.successors[i].size() == 2;
      known_taken = known_taken || conditional_graph.successors[i].size() == 1;
    }
  }
  require(unknown_fallthrough && known_taken,
          "only a taken indirect edge may establish a selector choice");

  std::map<int, std::vector<int>> aliases;
  for (int word = 0; word < 256; ++word) {
    const int physical = formal_address_info(word).actual;
    if (physical >= 8 && physical < 100)
      aliases[physical].push_back(word);
  }
  const auto pair = std::find_if(aliases.begin(), aliases.end(),
                                [](const auto& entry) { return entry.second.size() >= 2; });
  require(pair != aliases.end(), "stock address model must expose a dark alias pair");
  std::vector<MachineItem> aliased_items(105, MachineItem::op(0x54, "NOP"));
  for (std::size_t i : {0U, 1U}) {
    aliased_items[i] = MachineItem::op(0xa7, "indirect call");
    aliased_items[i].indirect_flow_targets = std::vector<IrTarget>{pair->first};
    aliased_items[i].indirect_flow_formal_targets =
        std::vector<int>{pair->second[0], pair->second[1]};
  }
  aliased_items[2] = MachineItem::op(0x50, "stop");
  aliased_items[2].stop_disposition = StopDisposition::Terminal;
  aliased_items[static_cast<std::size_t>(pair->first)] = MachineItem::op(0x52, "return");
  const auto aliased_flow = core::build_post_layout_control_flow(aliased_items);
  const auto aliased_graph = core::build_selector_consistent_execution_graph(aliased_items, aliased_flow);
  require(aliased_graph.proved && aliased_graph.pruned_edges == 2,
          "distinct hardware aliases of one physical target must not be conflated");
}

void emulator_stable_selector_continuations_match_rom() {
  for (const auto kind : {ChargeCase::Direct, ChargeCase::ThroughCall}) {
    const auto source = charged_fixture(kind);
    const auto shorter = charged_fixture(kind, "a", true);
    require(charged_lift_is_unobserved(source), "literal-directed deletion must have a data proof");
    require(resolve_machine_items(lower_ir_to_machine(source), {}).steps.size() ==
                resolve_machine_items(lower_ir_to_machine(shorter), {}).steps.size() + 1U,
            "literal-directed stack baseline must remove one real cell");
    for (const char* seed : {"0", "-11.25", "777.7777"})
      require(observe(source, "observer", seed) == observe(shorter, "observer", seed),
              "runtime literal, including a shared callee store, must override the initial selector; "
              "the proved deletion must preserve full stack, X1, VP/X2, memory and returns");
  }
  const auto original = fixture();
  require(lift_is_unobserved(original), "ROM test must exercise the refined relational proof");
  auto shortened = original;
  const auto lift = std::find_if(shortened.begin(), shortened.end(),
                                [](const auto& item) { return item.opcode == 0x0e; });
  require(lift != shortened.end(), "fixture must have a lift");
  shortened.erase(lift);
  for (const char* target : {"safe", "observer"})
    for (const char* seed : {"0", "-11.25", "777.7777"})
      require(observe(original, target, seed) == observe(shortened, target, seed),
              "the proved deletion must preserve full stack, X1, VP/X2, memory and returns");
}

} // namespace mkpro::tests
