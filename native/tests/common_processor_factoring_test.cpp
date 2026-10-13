#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/passes/common_processor_factoring.hpp"
#include "mkpro/core/passes/semantic_region_folding.hpp"
#include "mkpro/core/shared_helper_dual_mode_layout.hpp"
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

struct Fixture {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
};
int count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
      [](const auto& item) { return item.kind != MachineItemKind::Label; }));
}
Fixture fixture(int a, int b, bool absolute = false, bool weak_body = false) {
  MachineEmitter e;
  e.emit_op(0x52);
  e.emit_jump(0x53, "outer call", "caller");
  e.emit_stop(StopDisposition::Terminal);
  e.emit_label("caller");
  e.emit_op(0x66);
  e.emit_jump(0x53, "first argument", "projection");
  e.emit_op(0x42);
  e.emit_op(0x61);
  e.emit_jump(0x53, "second argument", "projection");
  e.emit_jump(0x53, "ordinary", "consumer");
  e.emit_op(0x10);
  e.emit_op(0x49);
  e.emit_op(0x68);
  e.emit_jump(0x53, "divergent", "consumer");
  e.emit_op(0x12);
  e.emit_op(0x4a);
  e.emit_op(0x64);
  e.emit_op(0x42);
  e.emit_op(0x65);
  e.emit_jump(0x53, "already projected", "consumer");
  e.emit_op(0x10);
  e.emit_op(0x49);
  e.emit_op(0x68);
  e.emit_op(0x52);
  e.emit_label("projection");
  if (absolute) {
    e.emit_op(0x31);
  } else {
    for (int code : {0x34, 2, 0x13, 0x35, 2, 0x12, 0x34}) e.emit_op(code);
  }
  e.emit_op(0x52);
  // Unreachable independent returning cells make the side-space geometry
  // explicit. No opcode or source identity is inferred from this placement.
  while (count(e.items) < 42) e.emit_op(0x52);
  e.emit_label("consumer");
  if (weak_body) {
    e.emit_op(0x41);
  } else {
    for (int code : {0x69, 0x14, 0x41, 0x22, 0x62, 0x68, 0x12, 0x31, 0x34, 0x10})
      e.emit_op(code);
  }
  e.emit_op(0x52);
  return {e.items, {{.register_name = "4", .value = std::to_string(a)},
                    {.register_name = "5", .value = std::to_string(b)}}};
}

std::vector<passes::PassResult> alternatives(const Fixture& f,
                                             std::size_t budget = 100000) {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  return passes::common_processor_factoring_candidates(
      raise_machine_to_ir(f.items), {options},
      {.runtime_preloads = &f.preloads, .maximum_proof_work = budget});
}

std::vector<std::string> observe(const std::vector<MachineItem>& items,
                                 const Fixture& f, const std::string& input) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "common processor fixture resolves");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "common processor fixture fits ROM");
  for (int reg = 0; reg < 15; ++reg)
    calc.set_register(std::string(1, "0123456789abcde"[reg]), std::to_string(reg + 1));
  for (const auto& preload : f.preloads) calc.set_register(preload.register_name, preload.value);
  calc.set_register("1", input);
  calc.set_register("6", input);
  for (const char* reg : {"X", "Y", "Z", "T", "X1"}) calc.set_register(reg, "-17.125");
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(6000, 8).stopped, "nested common processor returns and stops");
  std::vector<std::string> result;
  for (const char* reg : {"X", "Y", "Z", "T", "X1"}) result.push_back(calc.read_register(reg));
  for (int reg = 0; reg < 15; ++reg)
    result.push_back(calc.read_register(std::string(1, "0123456789abcde"[reg])));
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}


Fixture register_fixture(int a, int b, bool absolute, bool reversed, bool weak = false) {
  auto f = fixture(a, b, absolute, weak);
  for (std::size_t i = 1; i < f.items.size(); ++i) {
    if (f.items[i].kind != MachineItemKind::Address) continue;
    if (f.items[i].target == IrTarget{std::string("consumer")} &&
        f.items[i - 1].mnemonic == "ordinary") {
      f.items.insert(f.items.begin() + static_cast<std::ptrdiff_t>(i - 1),
                     MachineItem::op(0x41, "register second argument"));
      ++i;
    } else if (f.items[i].target == IrTarget{std::string("consumer")} &&
               f.items[i - 1].mnemonic == "already projected") {
      const std::size_t start = i - 4;
      if (reversed) {
        f.items[start] = MachineItem::op(0x65, "second argument");
        f.items[start + 1] = MachineItem::op(0x41, "store second argument");
        f.items[start + 2] = MachineItem::op(0x64, "first argument");
        f.items.insert(f.items.begin() + static_cast<std::ptrdiff_t>(i - 1),
                       MachineItem::op(0x42, "store first argument"));
      } else {
        f.items.insert(f.items.begin() + static_cast<std::ptrdiff_t>(i - 1),
                       MachineItem::op(0x41, "store second argument"));
      }
      ++i;
    }
  }
  bool body = false;
  for (std::size_t i = 0; i < f.items.size(); ++i) {
    if (f.items[i].kind == MachineItemKind::Label && f.items[i].name == "consumer")
      body = true;
    if (body && !weak && f.items[i].kind == MachineItemKind::Op && f.items[i].opcode == 0x14)
      f.items[i] = MachineItem::op(0x61, "recall second register argument");
    if (body && !weak && f.items[i].kind == MachineItemKind::Op && f.items[i].opcode == 0x41) {
      f.items.erase(f.items.begin() + static_cast<std::ptrdiff_t>(i));
      break;
    }
  }
  return f;
}

void register_argument_contracts() {
  for (bool absolute : {false, true})
    for (bool reversed : {false, true})
      for (int a : {0, 1})
        for (int b : {0, 1}) {
          const auto f = register_fixture(a, b, absolute, reversed);
          const auto candidates = alternatives(f);
          require(candidates.size() == 1, "register arguments need a proved generic candidate");
          const auto items = lower_ir_to_machine(candidates.front().ops);
          require(count(items) == count(f.items) - 1, "one fewer materialized argument cell");
          for (const char* input : {"-3.5", "0", "1", "9.25", "1E99", "-1E99", "8.ABCD123"})
            require(observe(items, f, input) == observe(f.items, f, input),
                    "register ABI all words, stack, X1/X2 and nested returns");
          require(ir_ops_to_json(alternatives(f).front().ops) ==
                      ir_ops_to_json(candidates.front().ops),
                  "both register-argument orders have deterministic proofs");
        }
  const auto f = register_fixture(1, 0, false, false);
  const auto candidates = alternatives(f);
  const auto neutral = lower_ir_to_machine(candidates.front().ops);
  PostLayoutControlFlowOptions control;
  control.empty_return_target = 1;
  const auto flow = build_post_layout_control_flow(neutral, control);
  const auto prep = prepare_shared_helper_dual_mode_layout(neutral, flow);
  require(flow.proved && prep.applied == 1 && prep.continuation.proved &&
              prep.continuation.helper_body_cells == 9,
          "register ABI retains matched returns and a distinct body entry");
  const auto closed = finalize_shared_helper_dual_mode_layout(prep, prep.items, f.preloads, flow);
  require(closed.proof.proved && closed.proof.final_control_flow.proved &&
              count(f.items) == 54 && count(closed.items) == 51,
          "register ABI plus dual-use placement: 54 -> 51");
  for (const char* input : {"-3.5", "0", "9.25", "8.ABCD123"})
    require(observe(closed.items, f, input) == observe(f.items, f, input),
            "joint register-argument and dual-use tail rewrite ROM observation");

  for (bool reversed : {false, true}) {
    require(alternatives(register_fixture(2, 1, false, reversed)).empty(),
            "register ABI must prove both arguments' fixed points");
    require(alternatives(register_fixture(1, 0, false, reversed, true)).empty(),
            "register arguments do not make live stack/X1/X2 disposable");
    require(alternatives(register_fixture(1, 0, false, reversed), 1).empty(),
            "register ABI exhausts the same proof budget and fails closed");
    auto unknown = register_fixture(1, 0, false, reversed);
    unknown.preloads.erase(unknown.preloads.begin());
    require(alternatives(unknown).empty(), "register ABI does not guess an unknown input domain");
  }
}

void destructive_argument_transport_contracts() {
  auto alias = fixture(1, 1, false);
  for (std::size_t i = 1; i < alias.items.size(); ++i)
    if (alias.items[i].kind == MachineItemKind::Address &&
        alias.items[i - 1].mnemonic == "already projected")
      alias.items[i - 2] = MachineItem::op(0x62, "read destroyed argument");
  require(alternatives(alias).empty(),
          "a post-store singleton cannot prove the overwritten incoming word");
  auto unsafe = alias.items;
  for (std::size_t i = 0; i < unsafe.size(); ++i)
    if (unsafe[i].kind == MachineItemKind::Op && unsafe[i].mnemonic == "already projected") {
      const std::size_t start = i - 3;
      unsafe.erase(unsafe.begin() + static_cast<std::ptrdiff_t>(start),
                   unsafe.begin() + static_cast<std::ptrdiff_t>(i));
      const std::vector<MachineItem> wrong{
          MachineItem::op(0x62, "old second argument"),
          MachineItem::op(0x41, "raw second argument"),
          MachineItem::op(0x64, "first argument"),
          MachineItem::op(0x42, "first argument store"),
          MachineItem::op(0x61, "second argument")};
      unsafe.insert(unsafe.begin() + static_cast<std::ptrdiff_t>(start), wrong.begin(), wrong.end());
      break;
    }
  require(observe(unsafe, alias, "0") != observe(alias.items, alias, "0"),
          "ROM retains the destructive-transport counterexample");

  for (bool reversed : {false, true}) {
    auto f = register_fixture(1, 1, false, reversed);
    for (std::size_t i = 0; i < f.items.size(); ++i)
      if (f.items[i].kind == MachineItemKind::Op && f.items[i].mnemonic == "already projected")
        f.items[i - 2] = MachineItem::op(reversed ? 0x61 : 0x62, "read destroyed argument");
    require(alternatives(f).empty(),
            "neither register-argument order may read through a destructive store");
  }
}


void partitioned_entry_contracts() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  for (bool registers : {false, true})
    for (bool absolute : {false, true}) {
      const auto f = registers ? register_fixture(1, 0, absolute, false) : fixture(1, 0, absolute);
      const auto proposals = passes::common_processor_partitioned_entry_candidates(
          raise_machine_to_ir(f.items), {options}, {.runtime_preloads = &f.preloads});
      require(proposals.size() == 1, "separate prologue/body entry is a generic independent alternative");
      const auto items = lower_ir_to_machine(proposals.front().ops);
      require(count(items) == count(f.items), "entry partitioning is neutral before coupled placement");
      for (const char* input : {"-3.5", "0", "9.25", "1E99", "8.ABCD123"})
        require(observe(items, f, input) == observe(f.items, f, input),
                "partitioned entries keep all other callers and their full observations unchanged");
      PostLayoutControlFlowOptions control;
      control.empty_return_target = 1;
      const auto flow = build_post_layout_control_flow(items, control);
      const auto prep = prepare_shared_helper_dual_mode_layout(items, flow);
      require(flow.proved && prep.applied == 1 && prep.continuation.proved,
              "both entry paths have a closed joint tail certificate");
      const auto final = finalize_shared_helper_dual_mode_layout(prep, prep.items, f.preloads, flow);
      if (registers) {
        require(!final.proof.proved,
                "partitioning does not guess a missing one-cell geometry adjustment");
      } else {
        require(final.proof.proved && final.proof.final_control_flow.proved && count(final.items) == 51,
                "different normalization paths compose at 53 -> 51 cells");
        for (const char* input : {"-3.5", "0", "9.25"})
          require(observe(final.items, f, input) == observe(f.items, f, input),
                  "coupled tail closure and distinct normalization paths match ROM");
      }
      require(passes::common_processor_partitioned_entry_candidates(
          raise_machine_to_ir(f.items), {options},
          {.runtime_preloads = &f.preloads, .maximum_proof_work = 1}).empty(),
          "partitioned proof budget fails closed");
    }
  auto nonfixed = fixture(2, 1);
  const auto body_entry = std::find_if(nonfixed.items.begin(), nonfixed.items.end(),
      [](const auto& item) { return item.kind == MachineItemKind::Label && item.name == "consumer"; });
  nonfixed.items.insert(body_entry, MachineItem::op(0x52, "independent geometry cell"));
  require(passes::common_processor_partitioned_entry_candidates(
      raise_machine_to_ir(nonfixed.items), {options},
      {.runtime_preloads = &nonfixed.preloads}).size() == 1,
      "unchanged callers need no invented fixed-point domain");
  const auto result = search_joint_representation_constraints(
      {.items = nonfixed.items, .preloads = nonfixed.preloads}, {},
      [](const LayoutSearchArtifact& candidate) -> std::optional<LayoutSearchArtifact> {
        bool partitioned = false, tail = false;
        for (const auto& report : candidate.optimizations) {
          partitioned = partitioned || report.name == "partitioned-common-processor";
          tail = tail || report.name == "joint-projected-tail-layout";
        }
        return partitioned && tail ? std::optional{candidate} : std::nullopt;
      }, {.maximum_expansions = 8, .maximum_candidates = 24,
          .maximum_depth = 3, .maximum_growth_cells = 2});
  require(result.incumbent && count(result.incumbent->items) == 52,
          "independent closure composes entry partition, allocation and tail geometry");
  for (const char* input : {"-3.5", "0", "9.25"})
    require(observe(result.incumbent->items, nonfixed, input) == observe(nonfixed.items, nonfixed, input),
            "complete partitioned-entry closure preserves register/stack/return observations");
}

void composition() {
  for (bool absolute : {false, true})
    for (int a : {0, 1})
      for (int b : {0, 1}) {
        const auto f = fixture(a, b, absolute);
        const auto candidates = alternatives(f);
        require(candidates.size() == 1, "two different projections must yield one generic ABI candidate");
        const auto factored = lower_ir_to_machine(candidates.front().ops);
        require(count(f.items) == 53 && count(factored) == 53,
                "neutral common-processor step must not invent a standalone saving");
        PostLayoutControlFlowOptions options;
        options.empty_return_target = 1;
        const auto flow = build_post_layout_control_flow(factored, options);
        require(flow.proved, "factored CFG/return contexts");
        const auto prep = prepare_shared_helper_dual_mode_layout(factored, flow);
        require(prep.applied == 1 && prep.continuation.proved &&
                    prep.continuation.helper_body_cells == 10,
                "discover two prologue entries and one divergent body entry");
        for (const auto& call : prep.continuation.calls)
          require(call.ordinary ? call.entry_address < prep.continuation.helper_body_start_address
                                : call.entry_address == prep.continuation.helper_body_start_address,
                  "ordinary and divergent entry identities must remain distinct");
        const auto merged = finalize_shared_helper_dual_mode_layout(
            prep, prep.items, f.preloads, flow);
        require(merged.applied == 1 && merged.proof.proved &&
                    merged.proof.final_control_flow.proved && count(merged.items) == 51,
                "common-processor plus side-entry tail composition: 53 -> 51");
        for (const auto& input : {"-3.5", "0", "1", "9.25"}) {
          const auto expected = observe(f.items, f, input);
          require(observe(factored, f, input) == expected,
                  "ABI rewrite must preserve all registers, stack, X1 and exposed X2");
          require(observe(merged.items, f, input) == expected,
                  "composed ordinary/side-entry returns must preserve the full observation");
        }
        const auto repeated = alternatives(f);
        require(ir_ops_to_json(repeated.front().ops) == ir_ops_to_json(candidates.front().ops),
                "discovery and exact-domain proof must be deterministic");
      }
}

void negative_proofs() {
  require(alternatives(fixture(2, 1)).empty(), "outside the projection's fixed-point set");
  auto unknown = fixture(1, 0);
  unknown.preloads.erase(unknown.preloads.begin());
  require(alternatives(unknown).empty(), "unknown finite range cannot be sampled");
  require(alternatives(fixture(1, 0), 1).empty(), "exhausted proof fails closed");
  require(alternatives(fixture(1, 0, false, true)).empty(),
          "same returned X is insufficient when deeper stack/X1 remain observable");
  auto raw = fixture(1, 0);
  for (auto& item : raw.items)
    if (item.kind == MachineItemKind::Op && item.opcode == 0x34) { item.raw = true; break; }
  require(alternatives(raw).empty(), "raw callee cannot acquire an arithmetic certificate");
  auto mutating = fixture(1, 0);
  for (std::size_t i = 1; i < mutating.items.size(); ++i)
    if (mutating.items[i].kind == MachineItemKind::Address &&
        mutating.items[i].target == IrTarget{std::string("projection")}) {
      mutating.items[i - 1] = MachineItem::op(0xa0, "mutating call");
      mutating.items[i - 1].indirect_flow_targets =
          std::vector<IrTarget>{std::string("projection")};
      mutating.items.erase(mutating.items.begin() + static_cast<std::ptrdiff_t>(i));
      break;
    }
  require(alternatives(mutating).empty(), "do not replay a mutating selector");
  auto side = fixture(1, 0);
  for (std::size_t i = 0; i < side.items.size(); ++i)
    if (side.items[i].kind == MachineItemKind::Op && side.items[i].opcode == 0x42) {
      side.items.insert(side.items.begin() + static_cast<std::ptrdiff_t>(i),
                        MachineItem::label("external_middle"));
      break;
    }
  side.items.push_back(MachineItem::op(0x51, "extra entry"));
  side.items.push_back(MachineItem::address(std::string("external_middle")));
  require(alternatives(side).empty(), "external prefix entry must not be erased");

  auto deep = fixture(1, 0);
  deep.items[2].target = std::string("wrapper0");
  for (int i = 0; i < 3; ++i) {
    deep.items.push_back(MachineItem::label("wrapper" + std::to_string(i)));
    deep.items.push_back(MachineItem::op(0x53, "nested call"));
    deep.items.push_back(MachineItem::address(i == 2 ? std::string("caller")
                                                    : "wrapper" + std::to_string(i + 1)));
    deep.items.push_back(MachineItem::op(0x52, "return"));
  }
  require(alternatives(deep).empty(), "a sixth live return frame is never admitted");

  const auto good = fixture(1, 0);
  const auto candidate = alternatives(good);
  auto items = lower_ir_to_machine(candidate.front().ops);
  PostLayoutControlFlowOptions options;
  options.empty_return_target = 1;
  const auto prep = prepare_shared_helper_dual_mode_layout(
      items, build_post_layout_control_flow(items, options));
  require(prep.applied == 1, "negative multi-entry fixture preparation");
  for (auto& item : items)
    if (item.kind == MachineItemKind::Address &&
        item.target == IrTarget{std::string("projection")}) {
      item.target = std::string("consumer"); // recursively re-enter the prologue
      break;
    }
  require(prepare_shared_helper_dual_mode_layout(
      items, build_post_layout_control_flow(items, options)).applied == 0,
      "recursive prologue is not a straight returning entry path");
}

void complete_artifact_contracts() {
  auto f = fixture(1, 0);
  bool body = false;
  for (auto& item : f.items) {
    if (item.kind == MachineItemKind::Label && item.name == "consumer") body = true;
    if (body && item.kind == MachineItemKind::Op && item.opcode == 0x68)
      item.roles.push_back(std::string(kRetunableNaturalFractionalSelectorRolePrefix) + "0.125000");
    if (body && item.kind == MachineItemKind::Op && item.opcode == 0x69)
      item.roles.push_back("helper-invariant-recall-hoist:root");
    if (body && item.kind == MachineItemKind::Op && item.opcode == 0x14)
      item.roles.push_back("helper-invariant-recall-hoist:entry-swap");
  }
  const auto transported = alternatives(f);
  require(transported.size() == 1, "unchanged data/address recall is not an ABI barrier");
  int roles = 0, abi_roles = 0;
  for (const auto& op : transported.front().ops)
    for (const auto& role : op.meta.roles)
      if (role.starts_with(kRetunableNaturalFractionalSelectorRolePrefix)) ++roles;
      else if (role.starts_with("helper-invariant-recall-hoist:")) ++abi_roles;
  require(roles == 1, "do not drop or duplicate the transported selector obligation");
  require(abi_roles == 2, "compose with the existing hoisted-recall ABI, retaining its identities");

  const auto root = fixture(1, 0);
  LayoutSearchArtifact artifact{.items = root.items, .preloads = root.preloads};
  CompileOptions options;
  const auto result = search_joint_representation_constraints(
      artifact, options, {}, {.maximum_expansions = 8, .maximum_candidates = 24,
                              .maximum_depth = 4, .maximum_growth_cells = 2});
  require(result.incumbent && count(result.incumbent->items) < count(root.items),
          "complete constraint search must not reject the proved empty-return startup");
  PostLayoutControlFlowOptions control;
  control.empty_return_target = 1;
  const auto flow = build_post_layout_control_flow(result.incumbent->items, control);
  require(flow.proved && flow.maximum_observed_return_depth <= 5,
          "the authoritative graph, not an unconditional empty-return ban, proves safety");
  require(observe(result.incumbent->items, root, "-3.5") == observe(root.items, root, "-3.5"),
          "closed startup and return continuations agree with ROM");
}

void projected_geometry_contracts() {
  auto f = fixture(1, 0, true);
  const auto consumer = std::find_if(f.items.begin(), f.items.end(),
      [](const auto& item) { return item.kind == MachineItemKind::Label &&
                                   item.name == "consumer"; });
  f.items.insert(consumer, MachineItem::op(0x52, "independent return"));
  const auto candidates = alternatives(f);
  require(candidates.size() == 1, "shifted common processor still factors");
  const auto neutral = lower_ir_to_machine(candidates.front().ops);
  PostLayoutControlFlowOptions control;
  control.empty_return_target = 1;
  const auto prep = prepare_shared_helper_dual_mode_layout(
      neutral, build_post_layout_control_flow(neutral, control));
  require(prep.applied == 1, "shifted dual-mode certificate");
  require(!finalize_shared_helper_dual_mode_layout(
      prep, prep.items, f.preloads,
      build_post_layout_control_flow(prep.items, control)).proof.proved,
      "rewriting at the unplaced positions must fail, not invent a side entry");
  const auto widths = shared_helper_dual_mode_projected_widths(prep);
  require(widths && widths->size() == prep.items.size(), "certificate supplies final widths");
  auto broken = prep;
  broken.items[broken.continuation.helper_return_item_index].opcode = 0x10;
  require(!shared_helper_dual_mode_projected_widths(broken),
          "a stale rewrite certificate cannot supply projected widths");

  const auto tail_only = [](const LayoutSearchArtifact& artifact)
      -> std::optional<LayoutSearchArtifact> {
    for (const auto& report : artifact.optimizations)
      if (report.name == "joint-projected-tail-layout") return artifact;
    return std::nullopt;
  };
  const auto result = search_joint_representation_constraints(
      {.items = f.items, .preloads = f.preloads}, {}, tail_only,
      {.maximum_expansions = 8, .maximum_candidates = 24,
       .maximum_depth = 3, .maximum_growth_cells = 2});
  require(result.incumbent && count(result.incumbent->items) == count(f.items) - 2,
          "final-width placement must close the neutral ABI and tail rewrite together");
  for (const auto& input : {"-3.5", "0", "9.25"})
    require(observe(result.incumbent->items, f, input) == observe(f.items, f, input),
            "projected layout preserves full ROM observation and nested return contexts");

  const auto ops = raise_machine_to_ir(prep.items);
  CompileOptions options;
  passes::SymbolicRegionProjection invalid;
  invalid.cell_widths.assign(ops.size(), 1);
  require(!passes::layout_symbolic_regions(ops, {}, {options}, 4096, &control, true, &invalid),
          "labels cannot acquire fictitious executable cells");
  invalid.cell_widths.clear();
  require(!passes::layout_symbolic_regions(ops, {}, {options}, 4096, &control, true, &invalid),
          "incomplete sizing projections fail closed");
}

void selector_provenance_contracts() {
  auto f = fixture(1, 0, true);
  // Two ordinary projection calls share an address/data word. Its physical
  // target moves twice: once during factoring and once in the tail layout.
  for (std::size_t i = 1; i < f.items.size(); ++i)
    if (f.items[i].kind == MachineItemKind::Address &&
        f.items[i].target == IrTarget{std::string("projection")}) {
      f.items[i - 1] = MachineItem::op(0xad, "stable projection call");
      f.items[i - 1].indirect_flow_targets = std::vector<IrTarget>{std::string("projection")};
      f.items.erase(f.items.begin() + static_cast<std::ptrdiff_t>(i));
      --i;
    }
  // Extra independent returning components permit both sides of the exact
  // side-space boundary without adding executable padding to the solution.
  for (int i = 0; i < 4; ++i) f.items.push_back(MachineItem::op(0x52, "independent return"));
  int address = 0, projection_address = -1;
  bool body = false;
  for (auto& item : f.items) {
    if (item.kind == MachineItemKind::Label && item.name == "projection")
      projection_address = address;
    if (item.kind == MachineItemKind::Label && item.name == "consumer") body = true;
    if (body && item.kind == MachineItemKind::Op && item.opcode == 0x68) {
      item.opcode = 0x6d;
      item.roles.push_back(std::string(kRetunableNaturalFractionalSelectorRolePrefix) + "0.125000");
    }
    if (item.kind != MachineItemKind::Label) ++address;
  }
  require(projection_address > 0 && projection_address < 100, "synthetic selector domain");
  const std::string encoded = "1.25000" +
      std::string(projection_address < 10 ? "0" : "") +
      std::to_string(projection_address) + "E-1";
  f.preloads.push_back({.register_name = "d", .value = encoded,
                       .retunable_natural_fractional_prefix = "0.125000"});
  const auto candidates = alternatives(f);
  require(candidates.size() == 1, "shared stable projection selector factors");
  const auto neutral = lower_ir_to_machine(candidates.front().ops);
  PostLayoutControlFlowOptions control;
  control.empty_return_target = 1;
  const auto flow = build_post_layout_control_flow(neutral, control);
  require(rebind_stable_preloaded_indirect_flow_selector(
      neutral, f.preloads.back(), flow, projection_address, 1).has_value() &&
      !rebind_stable_preloaded_indirect_flow_selector(
      neutral, f.preloads.back(), flow, projection_address, 0).has_value(),
      "a fractional codec is movable even though target zero is not representable");

  const auto result = search_joint_representation_constraints(
      {.items = f.items, .preloads = f.preloads}, {},
      [](const LayoutSearchArtifact& artifact) -> std::optional<LayoutSearchArtifact> {
        for (const auto& report : artifact.optimizations)
          if (report.name == "joint-projected-tail-layout") return artifact;
        return std::nullopt;
      }, {.maximum_expansions = 8, .maximum_candidates = 24,
          .maximum_depth = 3, .maximum_growth_cells = 2});
  require(result.incumbent && count(result.incumbent->items) < count(f.items),
          "encoded selector origins must survive deferred geometry");
  Fixture rebound{result.incumbent->items, result.incumbent->preloads};
  for (const auto& input : {"-3.5", "0", "9.25"}) {
    auto expected = observe(f.items, f, input);
    auto actual = observe(rebound.items, rebound, input);
    // Rd is an internal retunable representation, not an exposed raw output.
    expected.erase(expected.begin() + 18);
    actual.erase(actual.begin() + 18);
    require(actual == expected, "data projection, stack/X1/X2 and returns survive retuning");
  }

  const auto prep = prepare_shared_helper_dual_mode_layout(neutral, flow);
  require(prep.applied == 1, "selector provenance negative fixture");
  const auto projected_widths = shared_helper_dual_mode_projected_widths(prep);
  require(projected_widths.has_value(), "selector sizing certificate");
  const auto ops = raise_machine_to_ir(prep.items);
  passes::SymbolicRegionProjection projection;
  std::size_t item = 0;
  for (const auto& op : ops) {
    const auto length = op.kind == IrKind::Label
        ? 1U : static_cast<std::size_t>(passes::cells_per_op(op));
    int width = 0;
    for (std::size_t cell = 0; cell < length; ++cell)
      width += projected_widths->at(item++);
    projection.cell_widths.push_back(width);
  }
  CompileOptions options;
  const auto placed = passes::layout_symbolic_regions(
      ops, {{prep.continuation.helper_label, prep.required_final_start_address}},
      {options}, 4096, &control, true, &projection);
  require(placed.has_value(), "selector negative cases use a genuinely placed geometry");
  const auto placed_items = lower_ir_to_machine(placed->ops);
  const auto placed_flow = build_post_layout_control_flow(placed_items, control);
  const std::map<std::string, int> correct{{"d", projection_address}};
  require(finalize_shared_helper_dual_mode_layout(
      prep, placed_items, f.preloads, placed_flow, AddressSpaceModel::Standard, &correct).proof.proved,
      "the same placed artifact accepts the genuine encoded origin");
  const std::map<std::string, int> missing, wrong{{"d", projection_address + 1}};
  require(!finalize_shared_helper_dual_mode_layout(
      prep, placed_items, f.preloads, placed_flow, AddressSpaceModel::Standard, &missing).proof.proved,
      "missing selector provenance must fail closed");
  require(!finalize_shared_helper_dual_mode_layout(
      prep, placed_items, f.preloads, placed_flow, AddressSpaceModel::Standard, &wrong).proof.proved,
      "forged selector origin must fail exact word decoding");
}

void stable_split_bridge_contracts() {
  MachineEmitter e;
  e.emit_jump(0x51, "entry", "work");
  e.emit_op(0x52); // Independent one-cell component, not execution padding.
  e.emit_label("work");
  e.emit_op(0x67);
  e.emit_op(0xee);
  e.items.back().indirect_flow_targets = std::vector<IrTarget>{std::string("inner")};
  e.emit_op(0x68);
  e.emit_op(0x46);
  e.emit_label("inner");
  e.emit_label("inner_alias");
  e.emit_op(0x66);
  e.emit_op(0x69);
  e.emit_op(0x10);
  e.emit_stop(StopDisposition::Terminal);
  Fixture f{e.items, {{.register_name = "e", .value = "77770007"}}};
  const std::map<std::string, int> bindings{{"inner", 7}};
  CompileOptions options;
  PostLayoutControlFlowOptions control;
  const auto split = [&](const Fixture& input,
                         const std::map<std::string, int>& origins,
                         std::size_t maximum = 2) {
    return passes::stable_target_split_bridge_candidates(
        raise_machine_to_ir(input.items), input.preloads, origins,
        {options}, control, maximum);
  };
  const auto candidates = split(f, bindings);
  require(candidates.size() == 1, "one stable target admits one proved component split");
  require(count(lower_ir_to_machine(candidates.front().ops)) == count(f.items) + 1,
          "a bridge must retain its real enabling cost");
  require(ir_ops_to_json(split(f, bindings).front().ops) ==
              ir_ops_to_json(candidates.front().ops), "bridge selection is deterministic");
  const auto placed = passes::layout_symbolic_regions(
      candidates.front().ops, bindings, {options}, 4096, &control, true);
  require(placed.has_value(), "split pieces can satisfy the original selector identity");
  const auto final_items = lower_ir_to_machine(placed->ops);
  require(count(final_items) == 12 && count(f.items) == 11,
          "layout must not hide the extra bridge cell as padding or a saving");
  require(placed->projected_targets.at("inner") == 7 &&
              placed->projected_targets.at("inner_alias") == 7,
          "all entry aliases bypass the bridge and retain their target");
  for (const char* branch : {"0", "1", "-1"}) {
    auto input = f;
    input.preloads.push_back({.register_name = "7", .value = branch});
    for (const char* seed : {"0", "-3.5", "9.25"})
      require(observe(final_items, input, seed) == observe(f.items, input, seed),
              "both bridge paths preserve all registers, stack, X1, X2 and control");
  }
  // This public search publishes only a reduction. A bridge alone is useful
  // geometry, not license to replace the incumbent with a larger program.
  const auto alone = search_joint_representation_constraints(
      {.items = f.items, .preloads = f.preloads}, {}, {},
      {.maximum_expansions = 4, .maximum_candidates = 8,
       .maximum_depth = 2, .maximum_growth_cells = 1});
  require(!alone.incumbent || count(alone.incumbent->items) < count(f.items),
          "unrepaid split cost is never published");

  auto denormalized = f;
  denormalized.preloads.front().value = "7";
  denormalized.preloads.push_back({.register_name = "7", .value = "0"});
  require(split(denormalized, bindings).empty(),
          "equal numeric selector values do not prove an unchanged stored word");
  const auto old_word = observe(f.items, denormalized, "0");
  const auto new_word = observe(final_items, denormalized, "0");
  require(old_word[19] != new_word[19],
          "ROM must retain the 7-to-00000007 counterexample, not normalize it away");
  for (const char* word : {"7.7770007E7", "7.0000007E-1"}) {
    auto encoded = f;
    encoded.preloads.front().value = word;
    const auto encoded_candidates = split(encoded, bindings);
    // The existing preload codec admits canonical negative-order words, but
    // deliberately rejects positive exponent spellings. A word certificate
    // must not bypass that independent setup-delivery restriction.
    const std::size_t admitted = std::string(word).ends_with("E-1") ? 1U : 0U;
    require(encoded_candidates.size() == admitted,
            "word-shape and setup-delivery certificates are independent obligations");
    for (const char* branch : {"0", "1"}) {
      encoded.preloads.push_back({.register_name = "7", .value = branch});
      require(observe(final_items, encoded, "0") == observe(f.items, encoded, "0"),
              "positive and negative order word certificates must agree with ROM");
      encoded.preloads.pop_back();
    }
  }

  require(split(f, bindings, 0).empty(), "zero candidate budget disables splitting");
  require(split(f, {}).empty(), "a speculative PC is not an encoded word origin");
  require(split(f, {{"inner", 8}}).empty(), "a forged encoded origin fails closed");
  auto missing = f; missing.preloads.clear();
  require(split(missing, bindings).empty(), "a setup-time selector needs its delivered word");
  auto duplicate = f; duplicate.preloads.push_back(f.preloads.front());
  require(split(duplicate, bindings).empty(), "ambiguous delivered words fail closed");
  auto mutable_word = f;
  for (auto& item : mutable_word.items)
    if (item.kind == MachineItemKind::Op && item.opcode == 0x46) item.opcode = 0x4e;
  require(split(mutable_word, bindings).empty(), "an extra selector use cannot follow an arbitrary write");
  auto partial_number = f;
  for (auto& item : partial_number.items)
    if (item.kind == MachineItemKind::Op && item.opcode == 0x46) item.opcode = 4;
  require(split(partial_number, bindings).empty(), "a jump must not close an unfinished numeric entry");
  auto call_resume = f;
  for (auto& item : call_resume.items)
    if (item.kind == MachineItemKind::Op && item.opcode == 0x46) {
      item.opcode = 0xad;
      item.indirect_flow_targets = std::vector<IrTarget>{std::string("returning")};
    }
  call_resume.items.push_back(MachineItem::label("returning"));
  call_resume.items.push_back(MachineItem::op(0x52, "return"));
  require(split(call_resume, bindings).empty(), "a return continuation cannot become a hidden bridge entry");
  auto raw = f;
  for (auto& item : raw.items)
    if (item.kind == MachineItemKind::Op && item.opcode == 0x68) item.raw = true;
  require(split(raw, bindings).empty(), "raw effects cannot inherit a transparency proof");
}

} // namespace

int main() {
  try {
    register_argument_contracts();
    destructive_argument_transport_contracts();
    partitioned_entry_contracts();
    composition();
    negative_proofs();
    complete_artifact_contracts();
    projected_geometry_contracts();
    selector_provenance_contracts();
    stable_split_bridge_contracts();
    std::cout << "common processor compiler/ROM and size baseline passed (53/54 -> 51)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
