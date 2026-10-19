#include "mkpro/compiler.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/passes/semantic_region_folding.hpp"
#include "mkpro/core/raw_bcd_word.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace mkpro {
CompileResult close_final_contextual_layouts_for_testing(
    const std::string&, CompileResult, const CompileOptions&, const CompileOptions&);
}
namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;
constexpr const char* reset_key = "\u0412/\u041e";
constexpr const char* run_key = "\u0421/\u041f";
constexpr const char* step_key = "\u041f\u041f";
enum class Variant { Normal, Identity, Implicit, PowerLiteral, InitialStack,
                     Deep, Shadow, Alias, Write, Resume, Protocol, Raw };
struct Fixture { std::vector<MachineItem> items; std::vector<PreloadReport> preloads; };
Fixture fixture(Variant variant = Variant::Normal, bool interactive = false,
                int carrier = 8, const std::string& initial = "1") {
  MachineEmitter e;
  if (variant == Variant::InitialStack) e.emit_op(0x4a);
  if (variant == Variant::Alias) {
    e.emit_number(std::to_string(carrier)); e.emit_op(0x47); e.emit_op(0x69); e.emit_op(0xb7);
    e.items.back().indirect_memory_targets = std::vector<int>{carrier};
  }
  e.emit_label("again");
  e.emit_op(0x60 + carrier);
  if (variant == Variant::Raw) e.items.back().raw = true;
  e.emit_op(0x0b); e.emit_op(0x40 + carrier);
  if (variant == Variant::Shadow) { e.emit_op(0x0a); e.emit_op(0x4a); }
  if (variant == Variant::Write) { e.emit_number("0"); e.emit_op(0x40 + carrier); }
  for (int n = 0; n < 3; ++n) {
    if (n == 0 && variant == Variant::Implicit) {
      e.emit_number("10"); e.emit_op(0x13); e.emit_op(0x69);
    } else {
      e.emit_op(0x69); e.emit_op(0x60 + carrier);
      if (variant == Variant::PowerLiteral) { e.emit_number("2"); e.emit_op(0x15); }
      else e.emit_number("10");
      e.emit_op(0x13);
    }
    e.emit_op(0x12); e.emit_op(0x44 + n);
    if (n == 0 && variant == Variant::Deep) {
      e.emit_op(0x25); e.emit_op(0x25); e.emit_op(0x4a);
    }
  }
  if (variant == Variant::Identity) { e.emit_op(0x60 + carrier); e.emit_op(0x4a); }
  if (!interactive && variant != Variant::Resume) e.emit_jump(0x5d, "repeat", "again");
  for (const int reg : {1, 2, 3, 4}) e.emit_op(0x60 + reg);
  if (interactive) {
    e.next_op_manual_interaction = ManualInteractionAnchor{7, -1, ManualInteractionAnchorKind::PromptStop};
    e.emit_stop(StopDisposition::Resumable);
    e.next_op_manual_interaction = ManualInteractionAnchor{7, 0, ManualInteractionAnchorKind::SingleStepCommand};
    e.emit_op(0x41);
    e.next_op_manual_interaction = ManualInteractionAnchor{
        variant == Variant::Protocol ? 8 : 7, 1, ManualInteractionAnchorKind::ContinuousResume};
    e.emit_op(0x42); e.emit_jump(0x51, "next phase", "again");
  } else if (variant == Variant::Resume) {
    e.emit_stop(StopDisposition::Resumable); e.items.back().roles.clear();
    e.emit_jump(0x51, "untyped resume", "again");
  } else e.emit_stop(StopDisposition::Terminal);
  Fixture f{e.items, {}};
  for (int reg = 0; reg < 15; ++reg)
    f.preloads.push_back({.register_name = std::string(1, "0123456789abcde"[reg]),
                         .value = std::to_string(reg + 11)});
  f.preloads[0].value = "3";
  f.preloads[9].value = "100";
  f.preloads[static_cast<std::size_t>(carrier)].value = initial;
  f.preloads[static_cast<std::size_t>(carrier)].lowered_data_value = initial;
  return f;
}
int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const auto& item) {
    return item.kind != MachineItemKind::Label;
  }));
}
auto candidates(const Fixture& f) {
  CompileOptions options; options.analysis = true; options.defer_return_suffix_until_callee_hole = true;
  return preloaded_finite_state_layout_transactions({f.items, f.preloads, {}}, options);
}
std::vector<std::string> observe(const LayoutSearchArtifact& artifact, int carrier, bool interactive) {
  const auto resolved = resolve_machine_items(artifact.items);
  require(resolved.diagnostics.empty(), "fixture must resolve");
  std::vector<int> code; for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.command_limit() == 105 && calc.load_program(code).diagnostics.empty(), "fixture must fit stock ROM");
  for (const auto& preload : artifact.preloads) calc.set_register(preload.register_name, preload.value);
  calc.set_register("X", "17"); calc.set_register("Y", "23"); calc.set_register("Z", "31");
  calc.set_register("T", "43"); calc.set_register("X1", "53");
  calc.press_sequence({reset_key, run_key});
  const auto stop = [&] { require(calc.run_until_stable(5000, 8).stopped, "ROM execution must stop"); };
  std::vector<std::string> result;
  const auto snapshot = [&] {
    result.push_back(calc.display_text(true));
    for (const char* reg : {"X", "Y", "Z", "T", "X1"}) result.push_back(calc.read_register(reg));
    for (int reg = 0; reg < 15; ++reg) if (reg != carrier)
      result.push_back(calc.read_register(std::string(1, "0123456789abcde"[reg])));
  };
  stop(); snapshot();
  if (interactive) for (const auto& pair : std::vector<std::pair<std::string, std::string>>{
      {"1", "2"}, {"3", "5"}, {"4", "0"}}) {
    calc.input_number(pair.first, true).press(step_key); stop();
    result.push_back(calc.read_register("1"));
    calc.input_number(pair.second, true).press(run_key); stop(); snapshot();
  }
  calc.press("."); result.push_back(calc.display_text(true));
  return result;
}
void positive(Variant variant, bool interactive, int carrier, const std::string& initial) {
  const auto before = fixture(variant, interactive, carrier, initial);
  const auto alternatives = candidates(before);
  require(!alternatives.empty(), "delivered finite state must seed an encoded candidate");
  const auto& after = alternatives.front().node;
  const int saving = variant == Variant::Identity ? 8 : variant == Variant::Implicit ? 5 : 9;
  require(cells(after.items) == cells(before.items) - saving,
          "all consumer and transition decoder commands must be charged");
  const auto& owner = after.preloads.at(static_cast<std::size_t>(carrier));
  require(owner.value != initial && owner.lowered_data_value == owner.value,
          "proved initializer encoding must update delivered data provenance");
  require(std::any_of(after.optimizations.begin(), after.optimizations.end(), [&](const auto& report) {
    return report.detail.find(initial + " -> " + owner.value) != std::string::npos;
  }), "explain must report the actual delivered finite codebook");
  require(observe({before.items, before.preloads, {}}, carrier, interactive) == observe(after, carrier, interactive),
          "ROM future reads, displays, hidden slots and entry shadow must agree");
  PostLayoutControlFlowOptions flow; flow.empty_return_target = 1;
  require(build_post_layout_control_flow(after.items, flow).proved, "final return/input contexts must re-prove");
}
void negatives() {
  for (const auto variant : {Variant::InitialStack, Variant::Deep, Variant::Shadow, Variant::Alias, Variant::Write, Variant::Resume, Variant::Raw})
    require(candidates(fixture(variant)).empty(), "unknown writes, aliases and future observations must reject");
  require(candidates(fixture(Variant::Protocol, true)).empty(), "broken input protocol must reject");
  for (int kind = 0; kind < 9; ++kind) {
    auto f = fixture(); auto& owner = f.preloads[8];
    if (kind == 0) owner.setup_expression = true;
    if (kind == 1) owner.setup_target_name = "generated";
    if (kind == 2) owner.setup_expression_text = "computed()";
    if (kind == 3) owner.lowered_data_value = "2";
    if (kind == 4) owner.value = "-0";
    if (kind == 5) owner.value = "0x13";
    if (kind == 6) f.preloads.push_back(owner);
    if (kind == 7) f.preloads.push_back({.register_name = "f", .value = "1"});
    if (kind == 8) f.preloads.erase(f.preloads.begin() + 8);
    require(candidates(f).empty(), "initializer ownership and value cannot be guessed");
  }
  auto f = fixture(); f.preloads[8].setup_source_line = 42;
  require(!candidates(f).empty(), "source location is not generated setup");
  CompileOptions options; options.defer_return_suffix_until_callee_hole = true;
  require(passes::finite_mode_representation_candidates(raise_machine_to_ir(f.items), {options},
      {.runtime_preloads = &f.preloads, .encode_preloaded_state = true}).empty(),
      "IR-only clients cannot silently replace an owned initialization");
}

void raw_coefficient_alternatives(){
  for(const bool already_scaled:{false,true}){
    MachineEmitter e;
    e.emit_label("again");e.emit_op(0x68);e.emit_op(0x0b);e.emit_op(0x48);
    for(int n=0;n<3;++n){
      e.emit_number(std::to_string(n+1));e.emit_op(0x15);
      e.emit_op(0x68);
      if(!already_scaled){e.emit_number("10");e.emit_op(0x13);}
      e.emit_op(0x12);e.emit_op(0x44+n);
    }
    e.emit_jump(0x5d,"three exact sign transitions","again");
    for(int n=0;n<4;++n)e.emit_op(0x61);
    e.emit_op(0x22);e.emit_stop(StopDisposition::Terminal);
    Fixture original{e.items,{}};
    for(int r=0;r<15;++r)original.preloads.push_back({
        .register_name=std::string(1,"0123456789abcde"[r]),.value=std::to_string(r+11)});
    original.preloads[0].value="3";
    original.preloads[8].value=already_scaled?"0.1":"1";
    original.preloads[8].lowered_data_value=original.preloads[8].value;
    CompileOptions options;options.analysis=true;options.defer_return_suffix_until_callee_hole=true;
    const auto raw_candidates=[&](const std::vector<MachineItem>& items){
      auto all=passes::deferred_finite_mode_representation_candidates(
          raise_machine_to_ir(items),{options},{.maximum_growth_cells=1,.maximum_candidates=16,
          .allow_target_relayout=true,.runtime_preloads=&original.preloads,
          .preserve_compiler_selector_charges=true,.encode_preloaded_state=true});
      std::erase_if(all,[](const auto& candidate){
        return std::none_of(candidate.preload_replacements.begin(),candidate.preload_replacements.end(),
            [](const auto& replacement){return decode_raw_single_hex_word(replacement.after.value).has_value();});
      });
      return all;
    };
    const auto alternatives=raw_candidates(original.items);
    require(!alternatives.empty(),"synthesis retains raw and ordinary finite coefficient encodings");
    for(const auto& candidate:alternatives){
      LayoutSearchArtifact after{lower_ir_to_machine(candidate.transformation.ops),original.preloads,{}};
      for(const auto& replacement:candidate.preload_replacements){
        auto owner=std::find_if(after.preloads.begin(),after.preloads.end(),
            [&](const auto& p){return p.register_name==replacement.before.register_name;});
        require(owner!=after.preloads.end() && owner->value==replacement.before.value,
            "raw initializer replacement retains exact original ownership");
        *owner=replacement.after;
      }
      require(cells(after.items)<=cells(original.items),
          "raw coefficient alternatives charge every transition and consumer decoder");
      require(observe({original.items,original.preloads,{}},8,false)==observe(after,8,false),
          "ROM preserves future state and explicit hidden slots after raw multiplication");
    }
    auto unknown=original.items,opaque=original.items;
    const auto producer=std::find_if(unknown.begin(),unknown.end(),[](const auto& op){return op.kind==MachineItemKind::Op && op.opcode==0x15;});
    require(producer!=unknown.end(),"fixture has an exponentiation producer");
    producer->opcode=0x69;producer->mnemonic="recall unknown product";
    const auto opaque_producer=std::find_if(opaque.begin(),opaque.end(),[](const auto& op){return op.kind==MachineItemKind::Op && op.opcode==0x15;});
    opaque_producer->raw=true;
    require(raw_candidates(unknown).empty() && raw_candidates(opaque).empty(),
        "an opaque or unknown left operand never receives the raw arithmetic law");
    auto observed=original.items;
    const auto store=std::find_if(observed.begin(),observed.end(),[](const auto& op){return op.kind==MachineItemKind::Op && op.opcode==0x44;});
    observed.insert(store+1,{MachineItem::op(0x0f,"last-X observation"),MachineItem::op(0x4a,"retain observed last-X")});
    require(raw_candidates(observed).empty(),"observed physical last-X rejects raw coefficient substitution");
  }
}

void compiler_contract() {
  const std::string source = R"MKPRO(
program AlternatingProducts {
  state {
    phase: packed = 1
    first: packed = 2
    second: packed = 3
    banks: packed[4..6] = [100, 200, 300]
  }
  loop {
    phase = -phase
    banks[4] = packed_add(banks[4], first, phase / 10)
    banks[5] = packed_add(banks[5], second, phase / 100)
    banks[6] = packed_add(banks[6], first, phase / 1000)
    preview(banks[5] + banks[6])
    show(banks[4])
    first = entered()
    second = entered()
  }
}
)MKPRO";
  CompileOptions options; options.analysis = true; options.disable_candidate_search = true;
  options.defer_return_suffix_until_callee_hole = true; options.budget = 999;
  options.preloaded_indexed_update_prefix = true;
  options.suppress_constant_preloads = {"10", "100", "1000"};
  const auto baseline = compile_source(source, options);
  require(baseline.implemented, "high-level fixture must compile");
  auto comparison = options; comparison.budget = 1;
  const auto optimized = close_final_contextual_layouts_for_testing(source, baseline, options, comparison);
  if (!optimized.implemented || optimized.steps.size() >= baseline.steps.size())
    std::cerr << "baseline=" << baseline.steps.size() << " optimized=" << optimized.steps.size()
              << "\n" << baseline.listing << "\n";
  require(optimized.implemented && optimized.steps.size() < baseline.steps.size(),
          "compiler must regenerate setup and publish a smaller artifact");
  require(std::any_of(optimized.optimizations.begin(), optimized.optimizations.end(), [](const auto& report) {
    return report.name == "preloaded-finite-state-carrier";
  }), "compiler must select the proved initializer encoding");
  const auto play = [](const CompileResult& result) {
    emulator::MK61 calc; std::vector<int> code;
    for (const auto& step : result.steps) code.push_back(step.opcode);
    require(calc.load_program(code).diagnostics.empty(), "compiler fixture must fit ROM");
    for (const auto& word : result.preloads) calc.set_register(word.register_name, word.value);
    calc.press_sequence({reset_key, run_key}); std::vector<std::string> output;
    for (int turn = 0; turn < 4; ++turn) {
      require(calc.run_until_stable(5000, 8).stopped, "compiled phase must reach typed prompt");
      output.push_back(calc.display_text(true)); output.push_back(calc.read_register("Y"));
      if (turn == 3) break;
      calc.input_number(std::to_string(1 + turn), true).press(step_key);
      require(calc.run_until_stable(5000, 8).stopped, "compiled PP phase must stop");
      calc.input_number(std::to_string(2 + turn), true).press(run_key);
    }
    return output;
  };
  require(play(baseline) == play(optimized), "compiled setup and repeated interactive results must agree");
  std::cout << "preloaded high-level cells " << baseline.steps.size() << " -> " << optimized.steps.size() << '\n';
}
} // namespace
int main() {
  try {
    for (const bool interactive : {false, true}) for (const int carrier : {8, 11})
      for (const char* initial : {"1", "-1"}) {
        positive(Variant::Normal, interactive, carrier, initial);
        positive(Variant::Identity, interactive, carrier, initial);
        positive(Variant::Implicit, interactive, carrier, initial);
        positive(Variant::PowerLiteral, interactive, carrier, initial);
      }
    raw_coefficient_alternatives();
    negatives(); compiler_contract();
    std::cout << "preloaded finite state compiler/ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

