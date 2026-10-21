#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/passes/contextual_copy_transport.hpp"
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
using mkpro::tests::require;
struct Fixture { std::vector<MachineItem> items; std::vector<PreloadReport> preloads; };
int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
      [](const auto& item) { return item.kind != MachineItemKind::Label; }));
}
Fixture fixture(int kind, const char* input) {
  MachineEmitter e;
  e.emit_op(0x52); e.emit_label("main");
  // Exercise selector writeback in the source itself, before any comparison.
  e.emit_op(0xd9); e.items.back().indirect_memory_targets = std::vector<int>{7};
  for (int i = 0; i < 4; ++i) e.emit_op(0x69);
  e.emit_op(0x68); e.emit_jump(0x53, "observed existing leaf result", "leaf"); e.emit_op(0x4b);
  if (kind == 9) { e.emit_number("0"); e.emit_op(0x4e); }
  const auto observe_work = [&] {
    if (kind == 1) e.emit_op(0x4c);
    if (kind == 2) { e.emit_op(0x0f); e.emit_op(0x4c); }
    if (kind == 3) { e.emit_op(0x0a); e.emit_op(0x4c); }
  };
  e.emit_op(0x68); e.emit_jump(0x53, "first ignored invocation", "work"); observe_work();
  // Both outcomes execute in one delivered environment. The second call must
  // keep its own return frame and must not inherit the valued leaf's contract.
  e.emit_number("0"); e.emit_op(0x48);
  e.emit_op(0x68); e.emit_jump(0x53, "second ignored invocation", "work"); observe_work();
  e.emit_op(0xd7); e.items.back().indirect_memory_targets = std::vector<int>{9};
  e.emit_op(0x40);
  // Do not let a coincidental unchanged scalar input become an independent
  // constant-address shortcut: this fixture measures the new edge composition.
  e.emit_number("1"); e.emit_op(0x48);
  for (int i = 0; i < 4; ++i) e.emit_op(0x69);
  e.emit_op(0x10); e.emit_op(0x0f); e.emit_op(0x46);
  e.emit_stop(StopDisposition::Terminal); e.items.back().roles.push_back(kTypedDisplayObservationRole);
  e.emit_label("work");
  if (kind == 7) { e.emit_stop(StopDisposition::Resumable); e.items.back().roles.clear(); }
  e.emit_op(0x68); e.emit_op(0x35);
  e.emit_jump(kind == 8 ? 0x51 : 0x57, "only this edge may add pure work", "leaf_return");
  e.emit_op(0x4a);
  e.emit_label("leaf"); e.emit_op(0x34);
  if (kind == 4) e.items.back().raw = true;
  if (kind == 5) { e.emit_op(0); e.emit_op(0x13); }
  else if (kind == 9) { e.emit_op(0x6e); e.emit_op(0x10); }
  else { e.emit_op(4); e.emit_op(0x10); }
  if (kind == 10) e.emit_op(0x0c);
  e.emit_label("leaf_return"); e.emit_op(0x52);
  int pc = 0, entry = -1;
  for (const auto& item : e.items) {
    if (item.kind == MachineItemKind::Label) { if (item.name == "leaf") entry = pc; }
    else ++pc;
  }
  require(entry >= 0, "synthetic leaf has an entry");
  // Two direct address operands disappear only after the neutral edge and
  // the existing leaf call share one selector. The original source does
  // not use this spare word as control flow and needs no padded alignment.
  Fixture result{e.items, {{.register_name="8", .value=input},
      {.register_name="9", .value="97"}, {.register_name="7", .value="9"},
      {.register_name="e", .value=std::to_string(entry - 2)}}};
  if (kind == 6) result.preloads.erase(result.preloads.begin());
  return result;
}
std::vector<std::string> observe(const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "synthetic edge artifact resolves");
  std::vector<int> bytes; for (const auto& step : resolved.steps) bytes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.command_limit() == 105 && calc.load_program(bytes).diagnostics.empty(), "stock ROM image");
  for (const auto& preload : preloads) calc.set_register(preload.register_name, preload.value);
  calc.set_register("X", "31"); calc.set_register("Y", "41");
  calc.set_register("Z", "53"); calc.set_register("T", "67");
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(5000, 8).stopped, "real terminal is reached");
  std::vector<std::string> result;
  for (const char* slot : {"X", "Y", "Z", "T"}) result.push_back(calc.read_register(slot));
  // Re is an internal address-only carrier. Compare every other physical
  // word without stripping zeros or spaces, including the raw alias in R0.
  for (const char name : std::string("0123456789abcd"))
    result.push_back(calc.read_register(std::string(1, name)));
  return result;
}
void control_contract(const Fixture& original, const std::vector<MachineItem>& image) {
  PostLayoutControlFlowOptions control; control.empty_return_target = 1;
  const auto before = build_post_layout_control_flow(original.items, control);
  const auto after = build_post_layout_control_flow(image, control);
  std::vector<std::optional<std::size_t>> mapping(original.items.size());
  for (std::size_t i = 0; i < mapping.size(); ++i) mapping[i] = i;
  PostLayoutReturnCompletion completion;
  bool in_body = false;
  for (std::size_t i = 0; i < original.items.size(); ++i) {
    const auto& item = original.items[i];
    if (item.kind == MachineItemKind::Label && item.name == "leaf") in_body = true;
    if (item.kind != MachineItemKind::Op) continue;
    if (item.opcode == 0x57) completion.redirected_direct_branch_items.push_back(i);
    if (in_body) completion.body_items.push_back(i);
  }
  require(completion.redirected_direct_branch_items.size() == 1 && !completion.body_items.empty(),
      "certificate identifies one exact conditional and its existing leaf");
  completion.old_return_item = completion.body_items.back();
  PostLayoutExecutionRelocationOptions proof; proof.pure_return_completions.push_back(completion);
  require(prove_post_layout_execution_relocation(original.items, image, before, after, mapping, proof).proved,
      "the edge-qualified certificate transports every rooted frame");
  for (int kind = 0; kind < 4; ++kind) {
    auto invalid = proof; auto& body = invalid.pure_return_completions.front();
    if (kind == 0) body.redirected_direct_branch_items.push_back(body.redirected_direct_branch_items.front());
    if (kind == 1) body.redirected_direct_branch_items.clear();
    if (kind == 2) body.body_items.erase(body.body_items.begin());
    if (kind == 3) body.redirected_direct_branch_items.front() = 0;
    require(!prove_post_layout_execution_relocation(original.items, image, before, after, mapping, invalid).proved,
        "duplicate, unqualified, interior and nonconditional certificates fail closed");
  }
}
void data_and_rom() {
  for (int kind = 0; kind < 11; ++kind) for (const char* input : {"0", "0.25", "1"}) {
    const auto f = fixture(kind, input); const auto ops = raise_machine_to_ir(f.items);
    CompileOptions options;
    const auto candidates = passes::contextual_edge_return_completion_candidates(ops, {options},
        {.maximum_work=200000, .maximum_candidates=4, .maximum_copy_sites=8, .runtime_preloads=&f.preloads});
    require(!candidates.empty() == (kind == 0),
        "only ignored edges to total readonly code qualify; X/X1/X2, raw, zero divisor, unknown input, resume and mutation reject");
    require(passes::contextual_edge_return_completion_candidates(ops, {options},
        {.maximum_work=1, .runtime_preloads=&f.preloads}).empty(), "incomplete proofs cannot yield candidates");
    if (kind != 0) continue;
    const auto expected = observe(f.items, f.preloads);
    PostLayoutControlFlowOptions control; control.empty_return_target = 1;
    NaturalTargetComponentLayoutOptions layout; layout.allow_unreferenced_jump_labels = true;
    const auto old_flow = build_post_layout_control_flow(f.items, control);
    const auto baseline = optimize_natural_target_component_layout(f.items, f.preloads, old_flow, layout);
    int best = cells(f.items);
    if (baseline.applied > 0) best = cells(baseline.items);
    bool reduced = false;
    for (const auto& candidate : candidates) {
      const auto image = lower_ir_to_machine(candidate.transformation.ops);
      require(cells(image) == cells(f.items), "the semantic candidate is honestly priced as neutral");
      require(observe(image, f.preloads) == expected,
          "other callers retain their observed leaf results, full final stack and every physical data word");
      control_contract(f, image);
      const auto flow = build_post_layout_control_flow(image, control);
      const auto packed = optimize_natural_target_component_layout(image, f.preloads, flow, layout);
      std::cout << "edge input=" << input << " original=" << cells(f.items)
                << " baseline=" << best << " candidate=" << cells(packed.items)
                << " applied=" << packed.applied << '\n';
      for (const auto& word : packed.preloads)
        std::cout << "  R" << word.register_name << '=' << word.value << '\n';
      for (const auto& reason : packed.plan.reasons) std::cout << "  " << reason << '\n';
      if (packed.applied <= 0) continue;
      require(packed.plan.final_artifact_proved && packed.plan.control_flow_equivalent &&
          packed.plan.stack_and_x2_equivalent && packed.plan.call_return_equivalent,
          "compare only fully proved final selector/layout transactions");
      require(observe(packed.items, packed.preloads) == expected,
          "ROM proves combined selector, placement, stack and return behavior");
      reduced = reduced || cells(packed.items) < best;
    }
    require(reduced, "a neutral semantic edge and shared selector layout beat the ordinary size baseline");
  }
}
} // namespace
int main() {
  try { data_and_rom(); return 0; }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

