#include "mkpro/compiler.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/contextual_copy_transport.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/passes/semantic_region_folding.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/joint_constraint_origins.hpp"
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
std::vector<MachineItem> fixture(bool stale = false, bool raw = false) {
  MachineEmitter e;
  e.emit_jump(0x53, "formal call", "worker");
  e.items.back().formal_opcode = stale ? 0x21 : 0x20;
  e.emit_number("7"); e.emit_op(0x42);
  for (int i = 0; i < 4; ++i) e.emit_op(0x64);
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
void independent_nullable_completion() {
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
  const std::vector<passes::RegisterWebRecoloringRequest> requests{{{producer,anchor}}};
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
  passes::for_each_deferred_register_web_recoloring(opaque,{options},requests,control,origins,collect,&words,true,true);
  require(candidates.empty(),"complete formal allocation does not authorize raw arithmetic");
  candidates.clear();
  passes::for_each_deferred_register_web_recoloring(ops,{options},requests,control,origins,collect,nullptr,true,true);
  require(candidates.empty(),"pending formal allocation requires its complete runtime owner");
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
    pending_formal_recoloring();
    std::cout << "formal copy composition compiler/ROM tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
