#include "mkpro/compiler.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/contextual_copy_transport.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/passes/semantic_region_folding.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/joint_constraint_origins.hpp"
#include "mkpro/core/joint_proof_input.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/detail/layout_jobs.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

namespace mkpro::core { std::size_t joint_completed_cache_hits_for_testing(); }
namespace mkpro::core { std::size_t joint_literal_closure_reuses_for_testing(); }
namespace mkpro::core { std::size_t contextual_completed_cache_hits_for_testing(); }
namespace mkpro::core {
bool contextual_identity_expansion_demand_for_testing(const std::vector<IrOp>&,
    const std::vector<PreloadReport>&, const CompileOptions&);
std::optional<LayoutSearchArtifact> contextual_transport_identity_for_testing(
    const LayoutSearchArtifact&, const CompileOptions&, bool);
}
namespace mkpro::core {
std::vector<passes::RegisterWebRecoloringRequest> hardware_epoch_storage_requests_for_testing(
    const std::vector<IrOp>&, std::size_t);
std::vector<passes::RegisterWebRecoloringRequest> hardware_home_eviction_requests_for_testing(
    const std::vector<IrOp>&,
    const std::vector<passes::RegisterWebRecoloringRequest>&,
    const std::vector<passes::RegisterWebRecoloringRequest>&, std::size_t);
}

namespace mkpro::core {
std::size_t contextual_negative_cache_hits_for_testing();
std::optional<LayoutSearchArtifact> contextual_transport_identity_for_testing(
    const LayoutSearchArtifact&, const CompileOptions&);
}

namespace {
void counter_classification() {
  for (const auto model : {AddressSpaceModel::Standard, AddressSpaceModel::Mk61SMiniExpanded}) {
    for (int opcode = 0; opcode <= 255; ++opcode) {
      const auto info = formal_address_info(opcode, model);
      PostLayoutExecutionState state;
      state.address = info.actual; state.formal_opcode = opcode;
      const bool canonical = info.actual >= 0 && info.actual < official_program_step_limit(model) &&
          opcode == official_address_to_opcode(info.actual, model);
      require(has_noncanonical_post_layout_counter(state, model) != canonical,
          "every physical counter must distinguish canonical execution from a side/dark alias");
    }
    PostLayoutExecutionState virtual_state;
    virtual_state.address = official_program_step_limit(model) + 17;
    require(!has_noncanonical_post_layout_counter(virtual_state, model),
        "unplaced logical addresses are not physical aliases");
    virtual_state.formal_opcode = 0;
    require(has_noncanonical_post_layout_counter(virtual_state, model),
        "an inconsistent physical counter must fail closed");
  }
  PostLayoutExecutionState a5;
  a5.address = 105; a5.formal_opcode = 0xa5;
  require(has_noncanonical_post_layout_counter(a5, AddressSpaceModel::Standard) &&
          !has_noncanonical_post_layout_counter(a5, AddressSpaceModel::Mk61SMiniExpanded),
      "A5 is an alias on stock MK-61 but ordinary program memory in the 112-cell profile");
}
std::vector<MachineItem> fixture(bool stale = false, bool raw = false, bool sum_stack = false) {
  MachineEmitter e;
  e.emit_jump(0x53, "formal call", "worker");
  e.items.back().formal_opcode = stale ? 0x21 : 0x20;
  e.emit_number("7"); e.emit_op(0x42);
  for (int i = 0; i < 4; ++i) e.emit_op(0x64);
  if (sum_stack) for (int i = 0; i < 3; ++i) e.emit_op(0x10);
  e.emit_stop(StopDisposition::Terminal);
  while (std::count_if(e.items.begin(), e.items.end(), [](const auto& item) {
    return item.kind != MachineItemKind::Label;
  }) < 20) e.emit_op(0x54);
  e.emit_label("worker");
  e.emit_op(0x61); e.emit_op(0x42);
  e.items.back().raw = raw;
  e.emit_op(0x62); e.emit_number("2"); e.emit_op(0x12); e.emit_op(0x44); e.emit_op(0x52);
  return e.items;
}
std::vector<std::string> run(const std::vector<MachineItem>& items,
                           const std::vector<PreloadReport>& words) {
  const auto image = resolve_machine_items(items);
  require(image.diagnostics.empty(), "formal transport image resolves");
  std::vector<int> bytes;
  for (const auto& step : image.steps) bytes.push_back(step.opcode);
  emulator::MK61 machine;
  require(machine.load_program(bytes).diagnostics.empty(), "formal transport fits stock ROM");
  for (const auto& word : words) machine.set_register(word.register_name, word.value);
  machine.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(machine.run_until_stable(5000,8).stopped, "formal call returns to the original continuation");
  std::vector<std::string> result;
  for (const auto* reg : {"X","Y","Z","T","X1","1","2","4","a"})
    result.push_back(machine.read_register(reg));
  machine.press("8"); result.push_back(machine.display_text());
  return result;
}
void neutral_symbolic_root_and_rom() {
  MachineEmitter e;
  e.emit_op(0x52);
  e.emit_jump(0x53, "canonical formal call", "worker");
  e.items.back().formal_opcode = 0x20;
  e.emit_op(0xaa);
  e.items.back().indirect_flow_targets = std::vector<IrTarget>{40};
  for (int i = 0; i < 4; ++i) e.emit_op(0x64);
  e.emit_stop(StopDisposition::Terminal);
  const auto pad = [&](int target) {
    while (std::count_if(e.items.begin(), e.items.end(), [](const auto& item) {
      return item.kind != MachineItemKind::Label;
    }) < target) e.emit_op(0x54);
  };
  pad(20); e.emit_label("worker");
  e.emit_op(0x61); e.emit_op(0x42); e.emit_op(0x62);
  e.emit_number("2"); e.emit_op(0x12); e.emit_op(0x44); e.emit_op(0x52);
  pad(40); e.emit_op(0x64); e.emit_op(0x52);
  const LayoutSearchArtifact original{e.items,
      {{.register_name="1",.value="3.25"}, {.register_name="a",.value="40"}}, {}};
  CompileOptions options; options.coalesce_copies = true;
  require(passes::contextual_copy_transport_candidates(
      raise_machine_to_ir(original.items), {options}).empty(),
      "ordinary IR transport cannot consume this exact formal counter");
  const auto normalized = contextual_transport_identity_for_testing(original, options);
  require(normalized.has_value(),
      "a neutral copy/layout root must retain numeric destinations as symbolic identities");
  require(!contextual_transport_identity_for_testing(original, options, false),
      "the historical frozen branch must not manufacture a symbolic indirect destination");
  auto frozen = LayoutSearchArtifact{fixture(), {{.register_name="1", .value="3.25"}}, {}};
  for (auto& item : frozen.items)
    if (item.kind == MachineItemKind::Address) { item.target = 20; break; }
  const auto retained = contextual_transport_identity_for_testing(frozen, options, false);
  require(retained && same_layout_search_artifact(*retained, frozen) &&
      run(retained->items, retained->preloads) == run(frozen.items, frozen.preloads),
      "an unrelated frozen identity must retain every old physical target, byte and ROM observation");
  bool found = false;
  for (const auto& item : normalized->items) if (item.indirect_flow_targets) {
    found = true;
    for (const auto& target : *item.indirect_flow_targets)
      require(std::holds_alternative<std::string>(target),
          "the neutral edge must not restore delivered numeric targets");
  }
  require(found && same_layout_search_artifact({.preloads=original.preloads},
      {.preloads=normalized->preloads}), "normalization may not invent or replace setup owners");
  const auto before = resolve_machine_items(original.items);
  const auto after = resolve_machine_items(normalized->items);
  require(before.diagnostics.empty() && after.diagnostics.empty() &&
      before.steps.size() == after.steps.size(), "neutral normalization has no code-size debt");
  for (std::size_t i = 0; i < before.steps.size(); ++i)
    require(before.steps[i].opcode == after.steps[i].opcode,
        "the encoded counter, operands and every executable byte remain identical");
  require(run(original.items, original.preloads) == run(normalized->items, normalized->preloads),
      "neutral symbolic entry preserves stock ROM stack, memory, input mode and exact returns");
  for (const IrTarget& invalid : {IrTarget{2}, IrTarget{999}, IrTarget{std::string("missing")}}) {
    auto broken = original;
    for (auto& item : broken.items) if (item.indirect_flow_targets)
      item.indirect_flow_targets = std::vector<IrTarget>{invalid};
    require(!contextual_transport_identity_for_testing(broken, options),
        "operand, absent and unresolved command destinations must still fail closed");
  }
}
void proof_and_rom() {
  const std::vector<PreloadReport> words{{.register_name="1",.value="3.25"}};
  CompileOptions options; options.coalesce_copies = true;
  PostLayoutControlFlowOptions control; control.empty_return_target = 1;
  const auto candidates = [&](const std::vector<MachineItem>& items, bool formal) {
    std::map<std::string,int> origins; int pc = 0;
    for (const auto& item : items) {
      if (item.kind == MachineItemKind::Label) origins.emplace(item.name, pc);
      else ++pc;
    }
    return passes::contextual_copy_transport_candidates(raise_machine_to_ir(items), {options},
        {.control=formal?&control:nullptr, .encoded_target_origins=formal?&origins:nullptr,
         .runtime_preloads=&words, .preserve_formal_execution_frames=formal});
  };
  const auto original = fixture();
  require(candidates(original, false).empty(), "IR-only transport may not reinterpret formal operands");
  const auto proved = candidates(original, true);
  require(!proved.empty(), "complete formal relocation admits a safe contextual copy transport");
  for (const auto& candidate : proved) {
    const auto rewritten = lower_ir_to_machine(candidate.transformation.ops);
    require(resolve_machine_items(rewritten).steps.size() < resolve_machine_items(original).steps.size(),
        "formal-safe transport removes an actual cell");
    require(run(original, words) == run(rewritten, words),
        "formal-safe transport preserves stack, last-X, memory, numeric entry and exact return continuation on ROM");
  }
  require(candidates(fixture(true), true).empty(), "stale formal target cannot inherit a label proof");
  require(candidates(fixture(false, true), true).empty(), "raw copy remains a proof barrier");
}

void footprint_layout_and_rom() {
  MachineEmitter e;
  e.emit_jump(0x53, "first ordinary call", "first");
  e.emit_jump(0x53, "second ordinary call", "second");
  e.emit_jump(0x53, "side-space call", "side");
  e.items.back().formal_opcode = 0xf0;
  for (int i = 0; i < 4; ++i) e.emit_op(0x63);
  e.emit_stop(StopDisposition::Terminal);
  const auto cells = [&] {
    return std::count_if(e.items.begin(), e.items.end(), [](const auto& item) {
      return item.kind != MachineItemKind::Label;
    });
  };
  const int first = static_cast<int>(cells());
  e.emit_label("first");
  e.emit_op(0x61); e.emit_number("2"); e.emit_op(0x10); e.emit_op(0x43); e.emit_op(0x52);
  const int second = static_cast<int>(cells());
  e.emit_label("second");
  e.emit_op(0x61); e.emit_number("3"); e.emit_op(0x10); e.emit_op(0x44); e.emit_op(0x52);
  const int side = formal_address_info(0xf0).actual;
  while (cells() < side - 1) e.emit_op(0x54);
  e.emit_op(0x52);
  e.emit_label("side"); e.emit_op(0x66); e.emit_op(0x52);
  const auto ops = raise_machine_to_ir(e.items);
  passes::SymbolicRegionProjection projection;
  projection.preserve_formal_execution_frames = true;
  projection.relocate_canonical_formal_frames = true;
  for (const auto& op : ops) projection.cell_widths.push_back(passes::cells_per_op(op));
  CompileOptions options; options.coalesce_copies = true;
  PostLayoutControlFlowOptions control; control.empty_return_target = 1;
  const auto placed = passes::layout_symbolic_regions(ops,
      {{"first",second},{"second",first},{"side",side}}, {options}, 4096, &control, true, &projection);
  require(placed && placed->changed,
      "a side-space entry must not freeze unrelated ordinary components before joint placement");
  const std::vector<PreloadReport> words{{.register_name="1",.value="3.25"},
      {.register_name="6",.value="11"}};
  require(run(e.items, words) == run(lower_ir_to_machine(placed->ops), words),
      "ordinary component permutation must preserve the side-space trace and full observed ROM state");
  require(!passes::layout_symbolic_regions(ops, {{"side",side-1}}, {options},
      4096, &control, true, &projection), "the actual noncanonical footprint must remain pinned");
  auto changed_width = projection;
  for (std::size_t i = 0; i < ops.size(); ++i)
    if (ops[i].kind == IrKind::Recall && ops[i].opcode == 0x66) changed_width.cell_widths[i] = 0;
  require(!passes::layout_symbolic_regions(ops, {}, {options},
      4096, &control, true, &changed_width), "a projected edit inside the side trace must still reject");
}

std::vector<MachineItem> coefficient_fixture(bool alias, bool observe_last_x = false) {
  MachineEmitter e;
  const int counter = alias ? 0x2a : 0x20;
  const int target = formal_address_info(counter).actual;
  e.emit_jump(0x53, "first coefficient epoch", "coefficient");
  e.items.back().formal_opcode = counter;
  e.emit_op(0x44);
  e.emit_jump(0x53, "second coefficient epoch", "coefficient");
  e.items.back().formal_opcode = counter;
  e.emit_op(0x45);
  if (observe_last_x) { e.emit_op(0x0f); e.emit_op(0x49); }
  for (int i = 0; i < 4; ++i) e.emit_op(0x60);
  e.emit_number("0"); e.emit_op(0x10);
  e.emit_op(observe_last_x ? 0x69 : 0x64); e.emit_op(0x65);
  e.emit_stop(StopDisposition::Terminal);
  while (std::count_if(e.items.begin(), e.items.end(), [](const auto& item) {
    return item.kind != MachineItemKind::Label;
  }) < target) e.emit_op(0x54);
  e.emit_label("coefficient");
  e.emit_op(0x68); e.emit_op(0x0b); e.emit_op(0x48);
  e.emit_number("2"); e.emit_op(0x15);
  e.emit_op(0x68); e.emit_number("10"); e.emit_op(0x13); e.emit_op(0x12);
  e.emit_op(0x52);
  return e.items;
}
void finite_encoding_and_rom() {
  const std::vector<PreloadReport> words{{.register_name="0",.value="0"},
      {.register_name="8",.value="1"}};
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  const auto candidates = [&](const std::vector<MachineItem>& items) {
    return passes::deferred_finite_mode_representation_candidates(raise_machine_to_ir(items), {options},
        {.maximum_candidates=4,.allow_target_relayout=true,.runtime_preloads=&words,
         .preserve_compiler_selector_charges=true,.encode_preloaded_state=true,
         .preserve_formal_execution_frames=true,.relocate_canonical_formal_frames=true});
  };
  const auto original = coefficient_fixture(false);
  const auto encoded = candidates(original);
  require(!encoded.empty(), "canonical formal calls must not suppress finite coefficient encoding");
  bool saw_closed_raw = false;
  for (const auto& candidate : encoded) {
    auto updated = words;
    for (const auto& replacement : candidate.preload_replacements) {
      const auto owner = std::find_if(updated.begin(), updated.end(), [&](const auto& word) {
        return word.register_name == replacement.before.register_name;
      });
      require(owner != updated.end() && owner->value == replacement.before.value,
          "finite-state initializer replacement retains its exact original owner");
      *owner = replacement.after;
    }
    const auto rewritten = lower_ir_to_machine(candidate.transformation.ops);
    require(resolve_machine_items(rewritten).steps.size() < resolve_machine_items(original).steps.size(),
        "canonical-frame finite encoding must remove real coefficient instructions");
    const auto before = run(original, words), after = run(rewritten, updated);
    std::string differences;
    for (std::size_t i = 0; i < before.size(); ++i)
      if (before[i] != after[i]) differences += " slot " + std::to_string(i) +
          " [" + before[i] + "] != [" + after[i] + "]";
    require(before == after,
        "both finite epochs, X/Y, subsequent memory reads, numeric entry and returns match on stock ROM:" + differences);
    const bool raw = std::any_of(candidate.transformation.ops.begin(),candidate.transformation.ops.end(),
        [](const auto& op) { return op.meta.raw; });
    if (!raw) continue;
    require(candidate.numeric_layout_certificate != nullptr,
        "a proved closed raw orbit must carry its independent placement certificate");
    saw_closed_raw = true;
    const auto& certificate = candidate.numeric_layout_certificate;
    require(certificate->matches(rewritten,updated,AddressSpaceModel::Standard,false),
        "closed numeric certificate must recheck its actual delivered environment");
    require(!certificate->matches(rewritten,updated,AddressSpaceModel::Mk61SMiniExpanded,false) &&
            !certificate->matches(rewritten,updated,AddressSpaceModel::Standard,true),
        "numeric layout proofs may not cross model or input-contract cache boundaries");
    passes::SymbolicRegionProjection projection;
    projection.preserve_formal_execution_frames = true;
    projection.relocate_canonical_formal_frames = true;
    for (const auto& op : candidate.transformation.ops)
      projection.cell_widths.push_back(passes::cells_per_op(op));
    PostLayoutControlFlowOptions control; control.empty_return_target = 1;
    require(!passes::layout_symbolic_regions(candidate.transformation.ops,{}, {options},
        4096,&control,true,&projection), "ordinary raw code is still a placement barrier without a certificate");
    projection.numeric_layout_certificate = certificate;
    projection.numeric_layout_preloads = &updated;
    const auto placed = passes::layout_symbolic_regions(candidate.transformation.ops,{}, {options},
        4096,&control,true,&projection);
    require(placed && run(original,words)==run(lower_ir_to_machine(placed->ops),updated),
        "proved raw numeric bytes must compose with final layout and retain both ROM epochs");
    auto changed = updated;
    const auto carrier = std::find_if(changed.begin(),changed.end(),[](const auto& word) {
      return word.register_name=="8";
    });
    require(carrier!=changed.end(),"encoded carrier owner exists"); carrier->value="1";
    require(!certificate->matches(rewritten,changed,AddressSpaceModel::Standard,false),
        "a changed carrier word cannot reuse a numeric placement certificate");
    auto raw_changed=rewritten;
    const auto product=std::find_if(raw_changed.begin(),raw_changed.end(),[](const auto& item) {
      return item.kind==MachineItemKind::Op && item.raw && item.opcode==0x12;
    });
    require(product!=raw_changed.end(),"raw right-product witness exists"); product->opcode=0x13;
    require(!certificate->matches(raw_changed,updated,AddressSpaceModel::Standard,false),
        "a changed numeric opcode invalidates the closed operation language");
    auto extra_raw=rewritten; auto unknown=MachineItem::op(0x21,"unproved raw operation");
    unknown.raw=true; extra_raw.push_back(unknown);
    require(!certificate->matches(extra_raw,updated,AddressSpaceModel::Standard,false),
        "even an unreachable foreign raw command must not inherit the certificate");
    auto alias=rewritten; auto store=MachineItem::op(0xb7,"carrier alias");
    store.indirect_memory_targets=std::vector<int>{8}; alias.push_back(store);
    require(!certificate->matches(alias,updated,AddressSpaceModel::Standard,false),
        "an indirect carrier write invalidates the closed orbit");
    auto pooled=updated; pooled.push_back({.register_name="e",.value="88888834"});
    require(certificate->matches(rewritten,pooled,AddressSpaceModel::Standard,false),
        "an independently proved ordinary literal pool must not destroy an unchanged closed orbit");
  }
  require(candidates(coefficient_fixture(true)).empty(),
      "a repeated noncanonical entry cannot inherit the canonical finite-encoding permission");
  auto without_transition = coefficient_fixture(false);
  for (auto& item : without_transition)
    if (item.kind == MachineItemKind::Op && item.opcode == 0x0b)
      item.opcode = 0x31;
  require(candidates(without_transition).empty(),
      "preloaded sign-orbit encoding requires a genuine closed negation transition");
  auto foreign_transition = coefficient_fixture(false);
  for (auto& item : foreign_transition)
    if (item.kind == MachineItemKind::Op && item.opcode == 0x48)
      item.opcode = 0x49;
  require(candidates(foreign_transition).empty(),
      "a negation stored to another home is not a closed preloaded orbit");
  require(saw_closed_raw,"raw coefficient encoding and layout must actually compose, not merely pass ordinary alternatives");
  for (const auto& candidate : candidates(coefficient_fixture(false, true)))
    require(std::none_of(candidate.transformation.ops.begin(), candidate.transformation.ops.end(),
        [](const auto& op) { return op.meta.raw; }),
        "a future last-X observation must still reject opaque raw-coefficient encoding");
}

std::vector<MachineItem> nullable_fixture(bool observe_selector, bool opaque_body) {
  MachineEmitter e;
  e.emit_label("startup_return"); e.emit_op(0x52);
  e.emit_label("main"); e.emit_jump(0x53,"normalized zero producer","producer");
  if(observe_selector) { e.emit_op(0x6a); e.emit_op(0x42); }
  for(int i=0;i<4;++i) e.emit_op(0x61);
  e.emit_number("1"); e.emit_op(0x10);
  e.emit_number("1"); e.emit_op(0x4a);
  for(int i=0;i<4;++i) e.emit_op(0x61);
  e.emit_stop(StopDisposition::Terminal);
  const auto cells=[&] {
    return std::count_if(e.items.begin(),e.items.end(),[](const auto& item) {
      return item.kind!=MachineItemKind::Label;
    });
  };
  while(cells()<20) e.emit_op(0x54);
  e.emit_label("producer"); e.emit_op(0x61); e.emit_op(0x61); e.emit_op(0x11); e.emit_op(0x43);
  e.emit_op(0x73); e.items.back().indirect_flow_targets=std::vector<IrTarget>{"zero_return"};
  e.emit_op(0x52);
  while(cells()<30) e.emit_op(0x54);
  e.emit_label("leaf"); e.emit_op(0x52);
  while(cells()<38) e.emit_op(0x54);
  e.emit_number("30"); e.emit_label("traversal"); e.emit_op(0x4a);
  e.items.back().roles={"selector-sunk-literal-id:generic-readonly",
      "selector-sunk-literal-value:30"};
  if(opaque_body) e.emit_op(0x21,"opaque numeric operation",std::nullopt,std::nullopt,true);
  e.emit_op(0xaa); e.items.back().indirect_flow_targets=std::vector<IrTarget>{"leaf"};
  e.emit_op(0x52);
  while(cells()<99) e.emit_op(0x54);
  e.emit_label("zero_return"); e.emit_op(0x52);
  e.emit_op(0x54); e.emit_op(0x54); e.emit_op(0x52);
  return e.items;
}
void lazy_projection_without_null_completion() {
  MachineEmitter emitter;
  emitter.emit_op(0x52);
  emitter.emit_label("main");
  emitter.emit_number("2"); emitter.emit_number("3"); emitter.emit_op(0x12);
  emitter.emit_op(0xab);
  emitter.items.back().indirect_flow_targets = std::vector<IrTarget>{"worker"};
  emitter.emit_op(0x44);
  emitter.emit_number("0"); emitter.emit_op(0x48);
  for (int i=0; i<4; ++i) emitter.emit_op(0x60);
  emitter.emit_number("-2"); emitter.emit_jump(0x53, "ordinary caller", "worker");
  emitter.emit_op(0x45);
  emitter.emit_number("0"); emitter.emit_op(0x48);
  for (int i=0; i<4; ++i) emitter.emit_op(0x60);
  emitter.emit_number("0"); emitter.emit_op(0x10);
  emitter.emit_stop(StopDisposition::Terminal);
  emitter.emit_label("spare");
  while (std::count_if(emitter.items.begin(), emitter.items.end(), [](const auto& item) {
    return item.kind != MachineItemKind::Label;
  }) < 50) emitter.emit_op(0x54);
  emitter.emit_label("worker");
  emitter.emit_op(0x32); emitter.emit_op(0x48); emitter.emit_op(0x68);
  emitter.emit_number("0"); emitter.emit_op(0x10); emitter.emit_op(0x52);
  const auto original = emitter.items;
  const std::vector<PreloadReport> words{{.register_name="0", .value="0"},
      {.register_name="8", .value="0"}, {.register_name="b", .value="50"}};
  const std::map<std::string, int> origins{{"main",1}, {"worker",50}};
  CompileOptions options; options.coalesce_copies = true;
  PostLayoutControlFlowOptions control; control.empty_return_target = 1;
  const auto candidates = [&](const auto& items) {
    return passes::contextual_lazy_unit_projection_candidates(raise_machine_to_ir(items), {options},
        {.maximum_work=400000, .maximum_candidates=2, .control=&control,
         .encoded_target_origins=&origins, .runtime_preloads=&words,
         .preserve_formal_execution_frames=true});
  };
  require(std::none_of(original.begin(), original.end(), [](const auto& item) {
    return item.kind == MachineItemKind::Op && item.opcode >= 0x70 && item.opcode <= 0x73;
  }), "lazy projection fixture has no null-return branch or callback dispatcher");
  const auto proved = candidates(original);
  require(!proved.empty(), "sign-only storage projection must not require moving a null continuation");
  for (const auto& candidate : proved) {
    require(candidate.target_origins.empty() && candidate.selector_origins.empty(),
        "an independent projection may not invent an address or retarget the fixed return");
    const auto rewritten = lower_ir_to_machine(candidate.transformation.ops);
    int address = 0, worker = -1;
    for (const auto& item : rewritten) {
      if (item.kind == MachineItemKind::Label) {
        if (item.name == "worker") worker = address;
      } else ++address;
    }
    require(worker >= 0 && build_post_layout_control_flow(rewritten, control).proved,
        "independent projection must retain a final rooted command identity");
    // Rb is a control-only epoch, unlike the observed Ra. Do not hand-edit
    // an unproved selector to make ROM pass: obtain the independent
    // whole-program writeback/data certificate.
    mkpro::core::detail::PreparedJointProofInput proof(original, words, control);
    const auto rebound = proof.rebind_selector(words.back(), 50, worker);
    require(rebound.has_value(), "moved worker requires its independent selector certificate");
    auto delivered_words = words;
    delivered_words.back().value = *rebound;
    require(run(original, words) == run(rewritten, delivered_words),
        "independent projection preserves stock ROM values, next input, hidden state and returns");
  }
  auto opaque = original;
  const auto sign = std::find_if(opaque.begin(), opaque.end(), [](const auto& item) {
    return item.kind == MachineItemKind::Op && item.opcode == 0x32;
  });
  require(sign != opaque.end(), "fixture has a real source projection");
  sign->raw = true;
  require(candidates(opaque).empty(), "opaque projections must not inherit the unit theorem");
  auto unknown = original;
  const auto producer = std::find_if(unknown.begin(), unknown.end(), [](const auto& item) {
    return item.kind == MachineItemKind::Op && item.opcode == 2;
  });
  require(producer != unknown.end(), "fixture has its independent positive producer");
  *producer = MachineItem::op(0x66, "unknown caller value");
  require(candidates(unknown).empty(),
      "an uninitialized caller value must never acquire an ordinary positive assumption");
  auto address_use = original;
  auto recall = std::find_if(address_use.begin(), address_use.end(), [](const auto& item) {
    return item.kind == MachineItemKind::Op && item.opcode == 0x68;
  });
  require(recall != address_use.end(), "fixture has an affected recall");
  auto indexed = MachineItem::op(0xd8, "observable physical selector");
  indexed.indirect_memory_targets = std::vector<int>{1};
  address_use.insert(recall + 1, indexed);
  require(candidates(address_use).empty(),
      "using the carrier as a physical selector must prevent delayed sign projection");
  auto observed_address = original;
  const auto stop = std::find_if(observed_address.begin(), observed_address.end(), [](const auto& item) {
    return item.kind == MachineItemKind::Op && item.opcode == 0x50;
  });
  require(stop != observed_address.end(), "fixture has a final observation");
  observed_address.insert(stop, MachineItem::op(0x6b, "observable address value"));
  mkpro::core::detail::PreparedJointProofInput visible_proof(observed_address, words, control);
  require(!visible_proof.rebind_selector(words.back(), 50, 51),
      "a later data observation must prohibit the otherwise control-only address retuning");
}

void independent_nullable_completion() {
  lazy_projection_without_null_completion();
  CompileOptions options; options.coalesce_copies=true;
  PostLayoutControlFlowOptions control; control.empty_return_target=1;
  const std::vector<PreloadReport> words{{.register_name="1",.value="2"},
      {.register_name="a",.value="7"}};
  const auto candidates=[&](const std::vector<MachineItem>& items) {
    std::map<std::string,int> origins; int address=0;
    for(const auto& item:items) {
      if(item.kind==MachineItemKind::Label) origins.emplace(item.name,address);
      else ++address;
    }
    return passes::contextual_nullable_callback_projection_candidates(raise_machine_to_ir(items),{options},
        {.maximum_candidates=2,.control=&control,.encoded_target_origins=&origins,
         .runtime_preloads=&words});
  };
  const auto original=nullable_fixture(false,false);
  require(std::none_of(original.begin(),original.end(),[](const auto& item) {
    return item.kind==MachineItemKind::Op && item.opcode==0x32;
  }),"independent nullable fixture deliberately has no sign projection");
  const auto proved=candidates(original);
  require(!proved.empty(),"readonly null completion must not require an unrelated sign rewrite");
  for(const auto& candidate:proved) {
    require(candidate.selector_origins.empty() && !candidate.target_origins.empty(),
        "new null command origins must never be confused with selector-register word origins");
    require(std::any_of(candidate.target_origins.begin(),candidate.target_origins.end(),[](const auto& origin) {
      return origin.second==0;
    }),"null completion retains the independently checked physical-00 return obligation");
    std::map<std::string,int> bindings{{"zero_return",99},{"main",1},{"leaf",30}};
    bindings.insert(candidate.target_origins.begin(),candidate.target_origins.end());
    passes::SymbolicRegionProjection projection;
    projection.defer_indirect_entries=true;
    for(const auto& op:candidate.transformation.ops)
      projection.cell_widths.push_back(passes::cells_per_op(op));
    const auto closed=passes::layout_symbolic_regions(candidate.transformation.ops,bindings,
        {options},4096,&control,true,&projection);
    require(closed && run(original,words)==run(lower_ir_to_machine(closed->ops),words),
        "independent null completion must close real 00/99 addresses and preserve stock ROM state/returns");
  }
  require(candidates(nullable_fixture(true,false)).empty(),
      "a later selector read before overwrite must prohibit null completion");
  require(candidates(nullable_fixture(false,true)).empty(),
      "an opaque body may not inherit readonly arithmetic totality");
}
void numeric_formal_origin_metadata() {
  for(const auto model:{AddressSpaceModel::Standard,AddressSpaceModel::Mk61SMiniExpanded}) {
    const int limit=official_program_step_limit(model);
    for(int byte=0;byte<=255;++byte) {
      const auto exact=formal_address_info(byte,model);
      auto operand=MachineItem::address((exact.actual+1)%limit);
      operand.formal_opcode=byte;
      const auto normalized=normalize_natural_target_overflow_formals({operand},model);
      require(normalized && normalized->front().formal_opcode==byte &&
          std::get<int>(normalized->front().target)==exact.actual,
          "numeric formal metadata must name the exact unchanged hardware byte in each memory model");
    }
    auto virtual_operand=MachineItem::address(limit+17);
    virtual_operand.formal_opcode=0x20;
    const auto logical=normalize_natural_target_overflow_formals({virtual_operand},model);
    require(logical && !logical->front().formal_opcode &&
        std::get<int>(logical->front().target)==limit+17,
        "a discarded official resolver cache must not reinterpret unplaced logical code as dark memory");
  }
  auto original=fixture();
  for(auto& item:original)if(item.kind==MachineItemKind::Address && item.formal_opcode)item.target=21;
  const auto normalized=normalize_natural_target_overflow_formals(original);
  require(normalized.has_value(),"a stale numeric description of a valid formal call must normalize");
  const auto before=resolve_machine_items(original),after=resolve_machine_items(*normalized);
  require(before.diagnostics.empty() && after.diagnostics.empty() && before.steps.size()==after.steps.size(),
      "formal metadata normalization must retain the complete real byte image");
  for(std::size_t i=0;i<before.steps.size();++i)
    require(before.steps[i].opcode==after.steps[i].opcode,"formal normalization may not rewrite a hardware operand byte");
  const std::vector<PreloadReport> words{{.register_name="1",.value="3.25"}};
  require(run(original,words)==run(*normalized,words),
      "numeric formal normalization preserves stock ROM values, stack, entry shadow and return continuation");
  auto opaque=original;opaque[1].raw=true;
  const auto kept=normalize_natural_target_overflow_formals(opaque);
  require(kept && std::get<int>(kept->at(1).target)==21,
      "opaque numeric operands retain their independently owned metadata");
}
void literal_preload_provenance() {
  PreloadReport word{.register_name="8",.value="3.25",.setup_source_line=91};
  require(detail::joint_literal_word(word),"a diagnostic source line is not a dynamic setup expression");
  word.setup_expression=true;
  require(!detail::joint_literal_word(word),"dynamic initializer evaluation remains an ownership barrier");
  word.setup_expression=false;word.setup_expression_text="external_value";
  require(!detail::joint_literal_word(word),"setup expression text may not masquerade as a delivered literal");
  word.setup_expression_text.reset();word.setup_target_name="entry";
  require(!detail::joint_literal_word(word),"a symbolic setup address is not an ordinary delivered literal");
  word.setup_target_name.reset();word.counts_against_program=true;
  require(!detail::joint_literal_word(word),"program-time setup may not be moved into an entry preload");
}
void pending_formal_recoloring() {
  CompileOptions demand_options;
  std::vector<MachineItem> full_bank;
  for (int home=0; home<15; ++home) full_bank.push_back(MachineItem::op(0x60+home, "occupied home"));
  full_bank.push_back(MachineItem::op(2, "literal"));
  full_bank.push_back(MachineItem::op(3, "literal"));
  auto typed_stop = MachineItem::op(0x50, "typed display");
  typed_stop.roles.push_back(kTypedDisplayObservationRole);
  typed_stop.stop_disposition = StopDisposition::Terminal;
  full_bank.push_back(typed_stop);
  auto typed_input = MachineItem::op(0x43, "typed input store");
  typed_input.manual_interaction = ManualInteractionAnchor{
      0, 0, ManualInteractionAnchorKind::SingleStepCommand};
  full_bank.insert(full_bank.begin(), typed_input);
  auto known_read = MachineItem::op(0xd3, "known physical alias");
  known_read.indirect_memory_targets = std::vector<int>{3};
  full_bank.insert(full_bank.end()-1, known_read);
  require(!contextual_identity_expansion_demand_for_testing(raise_machine_to_ir(full_bank), {}, demand_options),
      "a newly normalized identity must not replay compound allocation with no producer or free literal home");
  const auto demand = [&](const std::vector<MachineItem>& items,
                          const std::vector<PreloadReport>& words) {
    return contextual_identity_expansion_demand_for_testing(raise_machine_to_ir(items), words, demand_options);
  };
  const std::vector<PreloadReport> orbit_word{{.register_name="8", .value="1"}};
  require(demand({MachineItem::op(0x68,"read"), MachineItem::op(0x0b,"negate"),
          MachineItem::op(0x48,"write")}, orbit_word),
      "a closed finite preload epoch retains its independent neutral composition lane");
  const std::vector<PreloadReport> no_words;
  require(demand({MachineItem::op(0xd3,"self-indexed read"), MachineItem::op(0x49,"copy")}, no_words) &&
      demand({MachineItem::op(2,"literal"), MachineItem::op(3,"literal")}, no_words),
      "hardware displacement and available literal storage must still activate the new neutral lane");
  const auto cycle_code = raise_machine_to_ir({MachineItem::op(0x40, "first epoch"),
      MachineItem::op(0x43, "producer"), MachineItem::op(0xd3, "self reload"),
      MachineItem::op(0x49, "copy destination"), MachineItem::op(0x40, "later epoch"),
      MachineItem::op(0xb0, "hardware home")});
  const std::vector<passes::RegisterWebRecoloringRequest> cycle_primary{{{1,5}}},
      cycle_copies{{{2,3}}};
  const passes::RegisterWebRecoloringRequest expected_cycle{{1,5},{2,3},{4,3}};
  const auto cycles = hardware_home_eviction_requests_for_testing(cycle_code, cycle_primary, cycle_copies, 32);
  require(!cycles.empty() && cycles.front() == expected_cycle,
      "a hardware affinity must close the displaced recent epoch instead of guessing unrelated first stores");
  require(hardware_home_eviction_requests_for_testing(cycle_code, cycle_primary, cycle_copies, 0).empty() &&
      hardware_home_eviction_requests_for_testing(cycle_code, {{{999,5}}}, cycle_copies, 32).empty(),
      "disabled or malformed hardware eviction demands must fail closed");
  auto opaque_cycle = cycle_code; opaque_cycle[4].meta.raw = true;
  const auto opaque_cycles = hardware_home_eviction_requests_for_testing(
      opaque_cycle, cycle_primary, cycle_copies, 32);
  require(std::find(opaque_cycles.begin(), opaque_cycles.end(), expected_cycle) == opaque_cycles.end(),
      "an opaque definition cannot be chosen as a displaced ordinary epoch");
  MachineEmitter emitter;
  emitter.emit_jump(0x53,"formal call","worker");
  emitter.items.back().formal_opcode=0x20;
  emitter.emit_op(0xab); emitter.items.back().indirect_flow_targets=std::vector<IrTarget>{"leaf"};
  emitter.emit_number("7"); emitter.emit_op(0x42);
  emitter.emit_number("0"); emitter.emit_op(0x43);
  emitter.emit_op(0xd3); emitter.items.back().indirect_memory_targets=std::vector<int>{3};
  for(int i=0;i<4;++i)emitter.emit_op(0x64);
  emitter.emit_stop(StopDisposition::Terminal);
  const auto pad=[&](int target){
    while(std::count_if(emitter.items.begin(),emitter.items.end(),[](const auto& item){
      return item.kind!=MachineItemKind::Label;
    })<target)emitter.emit_op(0x54);
  };
  pad(20);emitter.emit_label("worker");
  emitter.emit_op(0x61);emitter.emit_op(0x42);emitter.emit_op(0x62);
  emitter.emit_number("2");emitter.emit_op(0x12);emitter.emit_op(0x44);emitter.emit_op(0x52);
  pad(40);emitter.emit_label("leaf");emitter.emit_op(0x52);
  // A pending transaction already owns enough cells to place leaf at 60;
  // allocation itself may not create padding or change the selector word.
  emitter.emit_label("spare");
  for(int i=0;i<19;++i)emitter.emit_op(0x54);
  emitter.emit_op(0x52);
  const auto original=emitter.items;
  const auto ops=raise_machine_to_ir(original);
  std::size_t producer=ops.size(),anchor=ops.size();
  bool in_worker=false;
  for(std::size_t i=0;i<ops.size();++i){
    if(ops[i].kind==IrKind::Label && ops[i].name=="worker")in_worker=true;
    if(in_worker && ops[i].kind==IrKind::Store && ops[i].register_name=="2")producer=i;
    if(ops[i].kind==IrKind::IndirectRecall && ops[i].opcode==0xd3)anchor=i;
  }
  require(producer<ops.size() && anchor<ops.size(),"complete deferred recoloring has real operand anchors");
  CompileOptions options;options.coalesce_copies=true;
  PostLayoutControlFlowOptions control;control.empty_return_target=1;
  const std::map<std::string,int> origins{{"worker",20},{"leaf",60}};
  const std::vector<PreloadReport> words{{.register_name="1",.value="3.25"},{.register_name="b",.value="60"}};
  const passes::RegisterWebRecoloringRequest wanted{{producer, anchor}};
  const auto automatic = hardware_epoch_storage_requests_for_testing(ops, 32);
  const auto generated = std::find(automatic.begin(), automatic.end(), wanted);
  require(generated != automatic.end(),
      "hardware epoch affinity must be discovered without a copied-store provenance tag");
  require(hardware_epoch_storage_requests_for_testing(ops, 0).empty(),
      "automatic hardware affinities respect a disabled request budget");
  const std::vector<passes::RegisterWebRecoloringRequest> requests{*generated};
  std::vector<std::vector<IrOp>> candidates;
  const auto collect=[&](const auto& code){candidates.push_back(code);};
  passes::for_each_deferred_register_web_recoloring(ops,{options},requests,control,origins,collect,&words,true);
  require(candidates.empty(),"legacy allocation must not silently reopen a pending formal geometry");
  passes::for_each_deferred_register_web_recoloring(ops,{options},requests,control,origins,collect,&words,true,true);
  require(!candidates.empty(),"complete equal-width allocation must transport pending selectors with exact formal frames");
  const auto close=[&](const auto& code){
    passes::SymbolicRegionProjection projection;
    projection.defer_indirect_entries=true;projection.preserve_formal_execution_frames=true;
    projection.relocate_canonical_formal_frames=true;
    for(const auto& op:code)projection.cell_widths.push_back(passes::cells_per_op(op));
    return passes::layout_symbolic_regions(code,origins,{options},4096,&control,true,&projection);
  };
  const auto closed_original=close(ops);
  require(closed_original.has_value(),"pending original must close the genuine selector word at address 60");
  for(const auto& candidate:candidates){
    require(candidate[producer].register_name=="3","self-indexed hardware anchor retains R3");
    const auto closed=close(candidate);
    require(closed && run(lower_ir_to_machine(closed_original->ops),words)==
        run(lower_ir_to_machine(closed->ops),words),
        "pending recoloring plus final geometry must preserve ROM stack, last-X, inputs and return continuation");
  }
  auto opaque=ops;opaque[producer].meta.raw=true;candidates.clear();
  const auto opaque_requests = hardware_epoch_storage_requests_for_testing(opaque, 32);
  require(std::find(opaque_requests.begin(), opaque_requests.end(), wanted) == opaque_requests.end(),
      "an opaque store cannot become an automatic hardware epoch anchor");
  passes::for_each_deferred_register_web_recoloring(opaque,{options},requests,control,origins,collect,&words,true,true);
  require(candidates.empty(),"complete formal allocation does not authorize raw arithmetic");
  candidates.clear();
  passes::for_each_deferred_register_web_recoloring(ops,{options},requests,control,origins,collect,nullptr,true,true);
  require(candidates.empty(),"pending formal allocation requires its complete runtime owner");
}
void completed_joint_search_cache() {
  CompileOptions options; options.analysis = true; options.budget = 999999;
  const LayoutSearchArtifact root{fixture(false, true),
      {{.register_name="1", .value="3.25"}}, {{"joint-cache-root", "complete exact input"}}};
  const TransactionSearchLimits limits{.maximum_expansions=1, .maximum_candidates=2,
      .maximum_depth=1, .maximum_growth_cells=4};
  const auto literal_before = joint_literal_closure_reuses_for_testing();
  const auto first = search_joint_representation_constraints(root, options, {}, limits, true);
  require(joint_literal_closure_reuses_for_testing() > literal_before,
      "revisiting the same immutable pending root must reuse its entire literal closure, not just its parametric half");
  const auto before = joint_completed_cache_hits_for_testing();
  const auto second = search_joint_representation_constraints(root, options, {}, limits, true);
  require(joint_completed_cache_hits_for_testing() == before + 1 &&
      first.expanded == second.expanded && first.admitted == second.admitted &&
      first.duplicate == second.duplicate && first.rejected == second.rejected &&
      first.budget_exhausted == second.budget_exhausted &&
      first.incumbent.has_value() == second.incumbent.has_value() &&
      (!first.incumbent || same_layout_search_artifact(*first.incumbent, *second.incumbent)),
      "an identical completed pure frontier must preserve its artifact and all search statistics");
  auto lowered_policy = options;
  lowered_policy.alternating_sign_toggle_args = !options.alternating_sign_toggle_args;
  lowered_policy.zero_underflow_optimizer_root = !options.zero_underflow_optimizer_root;
  lowered_policy.fast_candidate_threshold_ms += 17;
  lowered_policy.collect_logical_register_allocation = !options.collect_logical_register_allocation;
  lowered_policy.compact_logical_register_allocation = !options.compact_logical_register_allocation;
  lowered_policy.repair_logical_register_assignment_preferences = !options.repair_logical_register_assignment_preferences;
  lowered_policy.preserve_logical_register_instruction_classes = !options.preserve_logical_register_instruction_classes;
  lowered_policy.logical_register_dead_store_elimination = !options.logical_register_dead_store_elimination;
  lowered_policy.stack_argument_helper_entries = !options.stack_argument_helper_entries;
  lowered_policy.single_x_expression_helper_entries = !options.single_x_expression_helper_entries;
  lowered_policy.general_constant_preloads = !options.general_constant_preloads;
  lowered_policy.disable_candidate_search = !options.disable_candidate_search;
  const auto shared_before = joint_completed_cache_hits_for_testing();
  (void)search_joint_representation_constraints(root, lowered_policy, {}, limits, true);
  require(joint_completed_cache_hits_for_testing() == shared_before + 1,
      "source-generation policy must not replay an identical immutable machine frontier");
  const auto miss = [&](const auto& artifact, const auto& policy,
                        const auto& budget, bool complete) {
    const auto old = joint_completed_cache_hits_for_testing();
    (void)search_joint_representation_constraints(artifact, policy, {}, budget, complete);
    require(joint_completed_cache_hits_for_testing() == old,
        "every changed proof input, search budget and preload contract must miss the cache");
  };
  auto changed = root; changed.items.front().source_line = 91; miss(changed, options, limits, true);
  changed = root; changed.preloads.front().value = "3.5"; miss(changed, options, limits, true);
  changed = root; changed.optimizations.front().detail = "different proof history";
  const auto history_before = joint_completed_cache_hits_for_testing();
  (void)search_joint_representation_constraints(changed, options, {}, limits, true);
  require(joint_completed_cache_hits_for_testing() == history_before + 1,
      "pure machine search must not replay an unchanged artifact solely for a different incoming report history");
  auto policy = options; policy.ordinary_decimal_keyboard_input = true;
  miss(root, policy, limits, true);
  policy = options; policy.suppress_constant_preloads.insert("3.25");
  miss(root, policy, limits, true);
  policy = options; policy.reserve_suppressed_constant_preload_slots.insert("3.25");
  miss(root, policy, limits, true);
  auto budget = limits; ++budget.maximum_depth; miss(root, options, budget, true);
  budget = limits; budget.prioritize_composition_depth = true; miss(root, options, budget, true);
  miss(root, options, limits, false);
  const JointRepresentationFinalizer identity = [](const auto& value) {
    return std::optional<LayoutSearchArtifact>{value};
  };
  const auto old = joint_completed_cache_hits_for_testing();
  (void)search_joint_representation_constraints(root, options, identity, limits, true);
  (void)search_joint_representation_constraints(root, options, {}, limits, true, identity);
  require(joint_completed_cache_hits_for_testing() == old,
      "an arbitrary finalizer or continuation callback must never reuse a pure-search cache entry");
  // All four recalled stack values participate in the displayed sum. Removing
  // a deeper producer would now change X rather than only a dead terminal Z.
  const LayoutSearchArtifact positive{fixture(false, false, true),
      {{.register_name="1", .value="3.25"}}, {{"first-caller", "source evidence A"}}};
  const auto winning = search_joint_representation_constraints(positive, options, {}, limits, true);
  require(winning.incumbent.has_value(), "report-rebasing fixture must really produce a smaller proved artifact");
  auto other = positive;
  other.optimizations = {{"second-caller", "source evidence B"}, {"second-fact", "must survive"}};
  const auto reuse_before = joint_completed_cache_hits_for_testing();
  const auto reused = search_joint_representation_constraints(other, options, {}, limits, true);
  require(joint_completed_cache_hits_for_testing() == reuse_before + 1 && reused.incumbent &&
      same_layout_search_artifact(*winning.incumbent, *reused.incumbent) &&
      reused.incumbent->optimizations.size() == winning.incumbent->optimizations.size() + 1 &&
      reused.incumbent->optimizations[0].name == "second-caller" &&
      reused.incumbent->optimizations[0].detail == "source evidence B" &&
      reused.incumbent->optimizations[1].name == "second-fact" &&
      std::equal(winning.incumbent->optimizations.begin()+1, winning.incumbent->optimizations.end(),
          reused.incumbent->optimizations.begin()+2, [](const auto& a, const auto& b) {
            return a.name==b.name && a.detail==b.detail;
          }), "a cached winning suffix must retain exactly the current caller's evidence, never the previous prefix");
  const auto original_observation = run(positive.items, positive.preloads);
  const auto reused_observation = run(reused.incumbent->items, reused.incumbent->preloads);
  if (original_observation != reused_observation) {
    std::cerr << "relative-cache original:";
    for (const auto& value : original_observation) std::cerr << ' ' << value;
    std::cerr << "\nrelative-cache selected:";
    for (const auto& value : reused_observation) std::cerr << ' ' << value;
    std::cerr << '\n';
    for (const auto& report : reused.incumbent->optimizations)
      std::cerr << report.name << ": " << report.detail << '\n';
    for (const auto& item : reused.incumbent->items)
      std::cerr << item.opcode << ' ' << item.mnemonic << '\n';
  }
  require(original_observation == reused_observation,
      "a history-rebased winning artifact must preserve complete stock ROM state and the next input");
}

void shared_joint_proof_work() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999999;
  const LayoutSearchArtifact root{fixture(false, false, true),
      {{.register_name = "1", .value = "3.25"}}, {{"shared-joint-root", "source A"}}};
  const TransactionSearchLimits limits{.maximum_expansions = 1, .maximum_candidates = 2,
      .maximum_depth = 1, .maximum_growth_cells = 4};
  using Outcome = TransactionSearchResult<LayoutSearchArtifact>;
  Outcome first, second, changed;
  const auto work = detail::make_joint_layout_work();
  const detail::JointLayoutWorkScope scope(work);
  // Use independent search threads so a hit cannot come from the caller's
  // older thread-local completed ledger.
  const auto run_search = [&](Outcome& result, const LayoutSearchArtifact& input) {
    std::thread worker([&] {
      const detail::JointLayoutWorkScope inherit(work);
      result = search_joint_representation_constraints(input, options, {}, limits, true);
    });
    worker.join();
  };
  run_search(first, root);
  const auto before = detail::joint_layout_work_stats(*work);
  run_search(second, root);
  const auto after = detail::joint_layout_work_stats(*work);
  require(after.variants.hits + after.closures.hits > before.variants.hits + before.closures.hits &&
          first.incumbent && second.incumbent &&
          first.expanded == second.expanded && first.admitted == second.admitted &&
          first.rejected == second.rejected && first.duplicate == second.duplicate &&
          first.budget_exhausted == second.budget_exhausted &&
          same_layout_search_artifact(*first.incumbent, *second.incumbent),
      "independent threads reuse exact subproofs without changing the joint frontier");
  auto other = root;
  other.optimizations = {{"shared-joint-root", "source B"}};
  run_search(changed, other);
  const auto rebased = detail::joint_layout_work_stats(*work);
  require(rebased.variants.hits + rebased.closures.hits > after.variants.hits + after.closures.hits &&
          changed.incumbent && changed.incumbent->optimizations.front().detail == "source B" &&
          same_layout_search_artifact(*first.incumbent, *changed.incumbent) &&
          run(root.items, root.preloads) == run(changed.incumbent->items, changed.incumbent->preloads),
      "shared proof stages must preserve the current report prefix and ROM state");
}

void completed_search_cache() {
  CompileOptions options;options.analysis=true;options.budget=999999;
  LayoutSearchArtifact root{fixture(false,true),{{.register_name="1",.value="3.25"}},
      {{"cache-regression-root","exact immutable input"}}};
  require(contextual_copy_transport_layout_transactions(root,options).empty(),
      "cache regression uses a completed nonwinning search");
  const auto before=contextual_negative_cache_hits_for_testing();
  require(contextual_copy_transport_layout_transactions(root,options).empty() &&
      contextual_negative_cache_hits_for_testing()==before+1,
      "an identical completed search must be reused");
  const auto miss=[&](const auto& changed,const auto& changed_options){
    const auto old=contextual_negative_cache_hits_for_testing();
    (void)contextual_copy_transport_layout_transactions(changed,changed_options);
    require(contextual_negative_cache_hits_for_testing()==old,
        "changed proof input, reports or compiler policy must never reuse a completed search");
  };
  auto changed=root;changed.preloads.front().value="3.5";miss(changed,options);
  changed=root;changed.items.front().source_line=91;miss(changed,options);
  changed=root;changed.optimizations.front().detail="different transaction history";miss(changed,options);
  auto policy=options;policy.ordinary_decimal_keyboard_input=true;miss(root,policy);
  policy=options;policy.fast_candidate_search=!policy.fast_candidate_search;miss(root,policy);
  policy=options;policy.feature_profile=FeatureProfile::Mk61SMiniExpanded;miss(root,policy);
  policy=options;policy.preloaded_constant_registers.emplace("9","7");miss(root,policy);
  policy = options;
  policy.alternating_sign_toggle_args = !options.alternating_sign_toggle_args;
  policy.zero_underflow_optimizer_root = !options.zero_underflow_optimizer_root;
  policy.fast_candidate_threshold_ms += 17;
  policy.collect_logical_register_allocation = !options.collect_logical_register_allocation;
  policy.compact_logical_register_allocation = !options.compact_logical_register_allocation;
  policy.repair_logical_register_assignment_preferences = !options.repair_logical_register_assignment_preferences;
  policy.preserve_logical_register_instruction_classes = !options.preserve_logical_register_instruction_classes;
  policy.logical_register_dead_store_elimination = !options.logical_register_dead_store_elimination;
  policy.stack_argument_helper_entries = !options.stack_argument_helper_entries;
  policy.single_x_expression_helper_entries = !options.single_x_expression_helper_entries;
  policy.general_constant_preloads = !options.general_constant_preloads;
  policy.disable_candidate_search = !options.disable_candidate_search;
  const auto shared_before = contextual_completed_cache_hits_for_testing();
  (void)contextual_copy_transport_layout_transactions(root, policy);
  require(contextual_completed_cache_hits_for_testing() == shared_before + 1,
      "completed machine search must ignore only independently audited source-generation switches");
  const LayoutSearchArtifact positive{fixture(false, false),
      {{.register_name="1", .value="3.25"}}, {{"positive-cache-root", "exact emitted input"}}};
  const auto winners = contextual_copy_transport_layout_transactions(positive, options);
  require(!winners.empty(), "positive cache fixture must actually produce a smaller proved artifact");
  const auto winning_before = contextual_completed_cache_hits_for_testing();
  const auto cached_winners = contextual_copy_transport_layout_transactions(positive, policy);
  require(contextual_completed_cache_hits_for_testing() == winning_before + 1 &&
      winners.size() == cached_winners.size(), "completed winning frontiers must be reused too");
  for (std::size_t at=0; at<winners.size(); ++at) {
    require(same_layout_search_artifact(winners[at].node, cached_winners[at].node) &&
        winners[at].rule == cached_winners[at].rule && winners[at].proved == cached_winners[at].proved,
        "a cache hit must preserve every winning command, preload and transaction proof flag");
    require(run(positive.items, positive.preloads) ==
        run(cached_winners[at].node.items, cached_winners[at].node.preloads),
        "reused winning artifacts must retain stock ROM state and the next input");
  }
  completed_joint_search_cache();
}
void measure_game() {
  std::ifstream input(std::string(MKPRO_FORMAL_COPY_REPO_ROOT) +
      "/examples/pending-optimizer/tic-tac-toe-4x4.mkpro");
  require(input.good(), "unchanged actual game source is available");
  const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  CompileOptions options; options.analysis = true; options.budget = 999999;
  options.ordinary_decimal_keyboard_input = true;
  const auto result = compile_source(source, options);
  require(result.implemented && result.diagnostics.empty(), "actual game compiles under the unchanged input contract");
  std::cout << "4x4 stock ordinary-decimal, formal-safe composition: " << result.steps.size() << " cells\n";
  require(result.steps.size() <= 117U, "joint formal lane must preserve the actual 117-cell incumbent");
  for (const auto& item : result.items)
    require(item.kind != MachineItemKind::Op || (item.opcode != 0x4f && item.opcode != 0x6f),
        "the target remains the stock register bank without Rf");
  if (const auto* path = std::getenv("MKPRO_FORMAL_COPY_ARTIFACT_DUMP")) {
    std::ofstream out(path); out << machine_items_to_json(result.items);
  }
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--game") { measure_game(); return 0; }
    counter_classification(); proof_and_rom(); footprint_layout_and_rom(); finite_encoding_and_rom();
    independent_nullable_completion();numeric_formal_origin_metadata();literal_preload_provenance();
    pending_formal_recoloring();completed_search_cache();shared_joint_proof_work();neutral_symbolic_root_and_rom();
    std::cout << "formal copy composition compiler/ROM tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
