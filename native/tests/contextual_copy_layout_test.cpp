#include "mkpro/compiler.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/detail/exact_value_terms.hpp"
#include "mkpro/core/detail/cell_provenance.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/passes/contextual_copy_layout.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>
#include <utility>

namespace mkpro {
// Exercise the final IR/budget boundary independently of earlier source passes.
CompileResult close_final_contextual_layouts_for_testing(
    const std::string&, CompileResult, const CompileOptions&, const CompileOptions&);
}

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

void append(std::vector<MachineItem>& items, std::initializer_list<int> bytes) {
  for (const auto byte : bytes) items.push_back(MachineItem::op(byte, "independent copy fixture"));
}

std::vector<MachineItem> fixture(bool negative = false, bool untyped = false) {
  std::vector<MachineItem> items;
  append(items, {0x67, 0x44, 0x53});
  items.push_back(MachineItem::address(std::string("project")));
  append(items, {0, 0x44, 2});
  if (negative) append(items, {0x0b});
  append(items, {0x44, 0x53});
  items.push_back(MachineItem::address(std::string("project")));
  append(items, {0x65, 0x64, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  if (!untyped) items.back().roles.push_back(kTypedDisplayObservationRole);
  items.push_back(MachineItem::label("project"));
  append(items, {0x64, 0x31, 0x41, 0x61, 0x44, 0x52});
  return items;
}

std::vector<PreloadReport> preloads() {
  return {{.register_name="4",.value="0"}, {.register_name="5",.value="17"},
          {.register_name="7",.value="-3"}};
}

std::vector<std::string> observe(const std::vector<MachineItem>& image,
                                 const std::vector<PreloadReport>& setup) {
  const auto listing = resolve_machine_items(image);
  require(listing.diagnostics.empty(), "test image resolves");
  std::vector<int> bytes;
  for (const auto& step : listing.steps) bytes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.command_limit() == 105, "test uses stock memory and no Rf");
  require(calc.load_program(bytes).diagnostics.empty(), "test fits real ROM");
  for (const auto& word : setup) calc.set_register(word.register_name,word.value);
  calc.press_sequence({"В/О","С/П"});
  require(calc.run_until_stable(3000,8).stopped,"test stops in ROM");
  return {calc.read_register("X"),calc.read_register("Y"),calc.read_register("4")};
}

void unsupported_register_terms() {
  for (const auto kind : {IrKind::Recall, IrKind::Store})
    for (const auto* name : {"f", "unknown", "?"}) {
      detail::exact_terms::Evaluation evaluation;
      IrOp op;
      op.kind = kind;
      op.register_name = name;
      op.opcode = kind == IrKind::Recall ? 0x6f : 0x4f;
      evaluation.execute(op);
      require(!evaluation.valid, "unsupported exact-term registers reject without throwing");
    }
}

void contextual_proof_domain_contracts() {
  CompileOptions options;
  const auto source = fixture();
  const auto original = raise_machine_to_ir(source);
  const auto flow = build_post_layout_control_flow(source);
  const auto setup = preloads();
  std::size_t store = original.size();
  for (std::size_t i = 1; i < original.size(); ++i)
    if (original[i-1].kind == IrKind::Recall && original[i-1].register_name == "1" &&
        original[i].kind == IrKind::Store && original[i].register_name == "4") store = i;
  require(store < original.size(), "domain fixture has a copy");
  for (const auto* role : {"finalization-cell-origin:17",
                           "shared-helper-dual-mode-0:origin:1"}) {
    require(detail::is_cell_origin_provenance(role), "complete descriptive schema is recognized");
    auto ops = original;
    ops[store].meta.roles.push_back(role);
    require(passes::prove_contextual_copy_store_elision(ops, flow, store, setup).proved,
            "valid provenance retains the exact data proof");
  }
  for (const auto* role : {"finalization-cell-origin:", "finalization-cell-origin:future-meaning",
                           "finalization-cell-origin:17:semantic", "shared-helper-dual-mode-:origin:1",
                           "shared-helper-dual-mode-terminal:origin:1",
                           "shared-helper-dual-mode-0:origin:",
                           "shared-helper-dual-mode-0:origin:1:semantic"}) {
    require(!detail::is_cell_origin_provenance(role), "malformed or unknown role is not descriptive");
    for (const bool target : {false, true}) {
      auto ops = original;
      (target ? ops[store].target_meta.roles : ops[store].meta.roles).push_back(role);
      require(!passes::prove_contextual_copy_store_elision(ops, flow, store, setup).proved,
              "both opcode and operand roles retain unknown obligations");
      require(passes::contextual_copy_layout_candidates(
                  ops, {options}, {.runtime_preloads = &setup}).empty(),
              "candidate construction cannot bypass a malformed proof role");
    }
  }
  for (const auto* name : {"f", "unknown", "?"}) {
    auto ops = original;
    ops[store-1].register_name = name;
    ops[store-1].opcode = 0x6f;
    require(passes::contextual_copy_register_requests(ops).empty(),
            "unsupported copy requests fail closed without parsing exceptions");
    require(!passes::prove_contextual_copy_store_elision(ops, flow, store, setup).proved,
            "a fifteen-register proof cannot admit malformed or configuration effects");
  }
  auto mismatched = original;
  mismatched[store-1].opcode = 0x62;
  require(!passes::prove_contextual_copy_store_elision(mismatched, flow, store, setup).proved,
          "physical register name must agree with its machine opcode");
  auto extended = setup;
  extended.push_back({.register_name = "f", .value = "0"});
  require(!passes::prove_contextual_copy_store_elision(original, flow, store, extended).proved,
          "an extended configuration initializer is not a spare stock data register");
}

void contextual_copy_contracts() {
  CompileOptions options;
  options.coalesce_copies = true;
  const auto setup = preloads();
  const auto source = fixture();
  const auto ops = raise_machine_to_ir(source);
  const auto flow = build_post_layout_control_flow(source);
  std::size_t store = ops.size();
  for (std::size_t i=1;i<ops.size();++i)
    if (ops[i-1].kind==IrKind::Recall && ops[i-1].register_name=="1" &&
        ops[i].kind==IrKind::Store && ops[i].register_name=="4") store=i;
  require(store<ops.size(),"fixture has a shared copy site");
  const auto proof=passes::prove_contextual_copy_store_elision(ops,flow,store,setup);
  require(proof.proved && proof.dead_contexts==1 && proof.fixed_point_contexts==1,
          "one store is dead in one caller and idempotent in another");
  const auto candidates=passes::contextual_copy_layout_candidates(
      ops,{options},{.runtime_preloads=&setup});
  require(!candidates.empty(),"context-specific proofs compose into a smaller artifact");
  const auto changed=lower_ir_to_machine(candidates.front().ops);
  require(resolve_machine_items(source).steps.size()==19 &&
          resolve_machine_items(changed).steps.size()==17,"independent 19 -> 17 cell baseline");
  require(observe(source,setup)==observe(changed,setup),"ROM preserves the public pair and later reads");
  require(std::stod(observe(source,setup).at(0))==2.0,"retained register value is observable");

  // A complete late root must reach the same atomic proof even without a
  // recoloring request and without enabling an earlier greedy IR pass.
  CompileOptions late_options;
  const LayoutSearchArtifact root{source, setup, {}};
  const auto late = contextual_copy_layout_transactions(root, late_options);
  require(!late.empty(), "complete-artifact finalization discovers an already coalesced copy");
  require(resolve_machine_items(late.front().node.items).steps.size() == 17,
          "late publication retains the two-cell saving");
  require(late.front().node.preloads.size() == setup.size(), "no implicit preload takeover");
  require(observe(source, setup) == observe(late.front().node.items, late.front().node.preloads),
          "the late artifact preserves setup and subsequent register reads in ROM");

  const auto ordinary_edges = expand_joint_representation_transactions(root, late_options, true);
  require(std::none_of(ordinary_edges.begin(), ordinary_edges.end(), [](const auto& edge) {
            return edge.rule == "joint-contextual-copy-layout";
          }), "new closure must not consume an established search beam");

  for (const auto& rejected : {fixture(true),fixture(false,true)}) {
    const auto alternatives=passes::contextual_copy_layout_candidates(
        raise_machine_to_ir(rejected),{options},{.runtime_preloads=&setup});
    require(alternatives.empty(),"non-fixed-point or untyped physical-state observation rejects erasure");
    require(contextual_copy_layout_transactions({rejected, setup, {}}, late_options).empty(),
            "late publication cannot bypass a rejected data or observation proof");
  }
  auto opaque=ops;
  opaque[store].meta.roles.push_back("unknown-proof-role");
  require(passes::contextual_copy_layout_candidates(
      opaque,{options},{.runtime_preloads=&setup}).empty(),"unknown erasure role is retained");
  opaque=ops;
  opaque[store].meta.raw=true;
  require(!passes::prove_contextual_copy_store_elision(
      opaque,flow,store,setup).proved,"raw code cannot inherit a typed data proof");
  const auto exhausted=passes::prove_contextual_copy_store_elision(ops,flow,store,setup,1);
  require(!exhausted.proved && exhausted.exhausted,"proof budget exhaustion exports no witness");
}

void budget_publication_contracts() {
  struct Case { FeatureProfile profile; int padding; int budget; };
  for (const auto test : {Case{FeatureProfile::Standard, 0, 17},
                          Case{FeatureProfile::Standard, 88, 105},
                          Case{FeatureProfile::Mk61SMiniExpanded, 95, 112}}) {
    auto source = fixture();
    source.insert(source.begin(), static_cast<std::size_t>(test.padding),
                  MachineItem::op(0x54, "independent reachable no-op"));
    CompileOptions options;
    options.feature_profile = test.profile;
    options.budget = test.budget;
    CompileResult oversized;
    oversized.feature_profile = test.profile;
    oversized.budget = options.budget;
    oversized.items = source;
    oversized.preloads = preloads();
    oversized.steps = resolve_machine_items(source, options).steps;
    oversized.diagnostics.emplace_back();
    oversized.diagnostics.back().severity = DiagnosticSeverity::Error;
    oversized.diagnostics.back().code = "BUDGET_EXCEEDED";
    oversized.diagnostics.back().message = "independent late-budget fixture";
    const std::string high_level = "program Independent { loop { halt(0) } }";
    const auto changed = close_final_contextual_layouts_for_testing(
        high_level, oversized, options, options);
    require(changed.implemented && changed.diagnostics.empty() &&
            changed.steps.size() <= static_cast<std::size_t>(test.budget),
            "a proved late closure can rescue a budget-only failure");
    if (test.profile == FeatureProfile::Standard)
      require(observe(fixture(), preloads()) == observe(changed.items, changed.preloads),
              "the 107 -> 105 closure executes in real stock ROM without Rf");
    else {
      require(changed.steps.size() == 112 && changed.feature_profile == test.profile,
              "expanded closure retains the explicit 105+7 model");
      auto stock = options;
      stock.feature_profile = FeatureProfile::Standard;
      stock.budget = 105;
      const auto rejected = close_final_contextual_layouts_for_testing(
          high_level, oversized, options, stock);
      require(!rejected.implemented && rejected.steps.size() == oversized.steps.size(),
              "an expanded artifact cannot acquire stock memory by changing proof options");
    }
    auto tight = options;
    tight.budget = 1;
    auto too_large = oversized;
    too_large.budget = tight.budget;
    const auto rejected_budget = close_final_contextual_layouts_for_testing(
        high_level, too_large, tight, tight);
    require(!rejected_budget.implemented && !rejected_budget.diagnostics.empty(),
            "a smaller result still outside the requested budget is not published");
    auto invalid = oversized;
    invalid.diagnostics.emplace_back();
    invalid.diagnostics.back().severity = DiagnosticSeverity::Error;
    invalid.diagnostics.back().code = "UNRELATED_ERROR";
    invalid.diagnostics.back().message = "not a removable budget failure";
    const auto rejected_error = close_final_contextual_layouts_for_testing(
        high_level, invalid, options, options);
    require(!rejected_error.implemented && rejected_error.diagnostics.size() == 2,
            "late size rescue never clears an unrelated error");
  }
}

void charged_prefix_annotation_contracts() {
  for (int variant = 0; variant != 4; ++variant) {
    auto source = fixture();
    for (std::size_t i = 1; i < source.size(); ++i)
      if (source[i].kind == MachineItemKind::Address && source[i - 1U].opcode == 0x53)
        source[i].target = std::string("wrapper");
    const auto target = std::find_if(source.begin(), source.end(), [](const auto& item) {
      return item.kind == MachineItemKind::Label && item.name == "project";
    });
    require(target != source.end(), "independent prefix fixture has a target");
    const auto at = static_cast<std::size_t>(target - source.begin());
    auto jump = MachineItem::op(0x51, "selector entry transfer");
    jump.comment = "callee-hole charge-entry tail transfer; proof=producer; selector=a";
    if (variant == 1)
      jump.comment = "callee-hole charge-entry tail transfer; proof=other; selector=a";
    if (variant == 2)
      jump.comment = "callee-hole charge-entry tail transfer; proof=producer; selector=b";
    if (variant == 3)
      jump.comment = "callee-hole charge-entry tail transfer; proof=producer; proof=other; selector=a";
    source.insert(source.begin() + static_cast<std::ptrdiff_t>(at), {
        MachineItem::label("wrapper"), jump, MachineItem::address(std::string("project"))});
    auto store = MachineItem::op(0x4a, "charge selector");
    store.comment = "callee-hole charge-entry store; proof=producer; selector=a";
    source.insert(source.begin() + static_cast<std::ptrdiff_t>(at + 4U), store);
    const auto setup = preloads();
    CompileOptions options;
    options.coalesce_copies = true;
    const auto alternatives = passes::contextual_copy_layout_candidates(
        raise_machine_to_ir(source), {options},
        {.runtime_preloads = &setup, .maximum_candidates = 4});
    bool interchanged = false;
    for (const auto& alternative : alternatives) {
      if (std::none_of(alternative.optimizations.begin(), alternative.optimizations.end(),
          [](const auto& report) { return report.detail.find("Interchanged") != std::string::npos; }))
        continue;
      interchanged = true;
      const auto moved = std::find_if(alternative.ops.begin(), alternative.ops.end(), [](const auto& op) {
        return op.kind == IrKind::Jump;
      });
      require(moved != alternative.ops.end() && moved->meta.comment.has_value(),
              "moved jump keeps a meaningful classification");
      require((moved->meta.comment == "internal continuation after a charged prefix") == (variant == 0),
              "only the matching entry store consumes the old transfer obligation");
      require(observe(source, setup) == observe(lower_ir_to_machine(alternative.ops), setup),
              "annotation transport and prefix interchange preserve independent ROM behavior");
      require(resolve_machine_items(source).steps.size() == 22 &&
              resolve_machine_items(lower_ir_to_machine(alternative.ops)).steps.size() == 20,
              "independent charged-prefix baseline is 22 -> 20");
    }
    require(interchanged, "independent charged-prefix interchange must be discovered");
  }
}

void transparent_jump_contracts() {
  for (const bool observe_shadow : {false,true}) {
    std::vector<MachineItem> before;
    append(before,{0x64});
    if (observe_shadow) append(before,{0x0a});
    append(before,{0x65,0x50});
    before.back().stop_disposition=StopDisposition::Terminal;
    auto after=before;
    after.insert(after.begin()+1,{MachineItem::op(0x51,"new bridge"),
                                MachineItem::address(std::string("continued")),
                                MachineItem::label("continued")});
    const auto old_flow=build_post_layout_control_flow(before);
    const auto new_flow=build_post_layout_control_flow(after);
    std::vector<std::optional<std::size_t>> mapping(before.size());
    for(std::size_t i=0;i<before.size();++i)mapping[i]=i==0?0:i+3;
    PostLayoutExecutionRelocationOptions transport;
    transport.inserted_transparent_jump_items={1};
    const auto proof=prove_post_layout_execution_relocation(
        before,after,old_flow,new_flow,mapping,transport);
    require(proof.proved!=observe_shadow,"a new bridge must hide X2 before every consumer");
    if(!observe_shadow) {
      require(observe(before,preloads())==observe(after,preloads()),"ROM agrees on transparent bridge");
      auto malformed=transport;
      malformed.inserted_transparent_jump_items={2};
      require(!prove_post_layout_execution_relocation(
          before,after,old_flow,new_flow,mapping,malformed).proved,"operand is not an executable bridge");
    }
  }

  std::vector<MachineItem> before;
  append(before,{1,2,0x64,0x65,0x50});
  before.back().stop_disposition=StopDisposition::Terminal;
  auto after=before;
  after.insert(after.begin()+1,{MachineItem::op(0x51,"would close open digits"),
                               MachineItem::address(std::string("open_digits")),
                               MachineItem::label("open_digits")});
  std::vector<std::optional<std::size_t>> map(before.size());
  for(std::size_t i=0;i<before.size();++i)map[i]=i==0?0:i+3;
  PostLayoutExecutionRelocationOptions transport;
  transport.inserted_transparent_jump_items={1};
  require(!prove_post_layout_execution_relocation(before,after,
      build_post_layout_control_flow(before),build_post_layout_control_flow(after),map,transport).proved,
      "bridge cannot silently close an open keyboard number");
}

std::vector<std::string> entry_observation(int opcode,int selector,int mode,int value,bool close) {
  std::vector<int> prefix;
  const int digit=value<0?-value:value;
  if(mode==0)prefix={0x66};
  if(mode==1){prefix={digit};if(value<0)prefix.push_back(0x0b);}
  if(mode==2){prefix={digit,0x0c,1};if(value<0)prefix.push_back(0x0b);}
  const bool direct=opcode<0x70;
  std::vector<int> bytes=prefix;
  bytes.push_back(close?0x42:opcode);
  if(direct)bytes.push_back(close?0x54:0);
  bytes.insert(bytes.end(),{9,0x41,0x50});
  const int branch_target=static_cast<int>(bytes.size());
  bytes.insert(bytes.end(),{9,0x41,0x50});
  if(direct&&!close)bytes[prefix.size()+1]=branch_target;
  emulator::MK61 calc;
  require(calc.load_program(bytes).diagnostics.empty(),"nonlocal condition probe loads");
  for(const auto* name:{"X","Y","Z","T"})calc.set_register(name,"17");
  calc.set_register("6",std::to_string(value));
  if(!direct)calc.set_register(std::string(1,"0123456789abcde"[selector]),std::to_string(branch_target));
  calc.press_sequence({"В/О","С/П"});
  require(calc.run_until_stable(1000,8).stopped,"nonlocal condition probe stops");
  std::vector<std::string> result;
  for(const auto* name:{"X","Y","Z","T","X1"})result.push_back(calc.read_register(name));
  calc.press(".");result.push_back(calc.display_text(true));
  return result;
}

void conditional_entry_facts() {
  for(const int code:{0x57,0x59,0x5c,0x5e})
    for(int mode=0;mode<3;++mode)for(int value:{-1,0,1})
      require(entry_observation(code,7,mode,value,false)==entry_observation(code,7,mode,value,true),
              "direct predicate independently closes nonlocal numeric entry");
  for(int selector=7;selector<15;++selector)for(const int family:{0x70,0x90,0xc0,0xe0})
    for(int mode=0;mode<3;++mode)for(int value:{-1,0,1})
      require(entry_observation(family+selector,selector,mode,value,false)==
              entry_observation(family+selector,selector,mode,value,true),
              "stable indirect predicate independently closes nonlocal numeric entry");
}
} // namespace

int main() {
  try {
    unsupported_register_terms();
    contextual_proof_domain_contracts();
    contextual_copy_contracts();
    budget_publication_contracts();
    charged_prefix_annotation_contracts();
    transparent_jump_contracts();
    conditional_entry_facts();
    std::cout<<"contextual copy/layout, negative guards and conditional ROM facts passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
  return 0;
}
