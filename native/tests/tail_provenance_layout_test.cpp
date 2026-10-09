#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/post_layout_indirect_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;

void require(bool value, const std::string& reason) {
  if (!value) throw std::runtime_error(reason);
}
void append(std::vector<MachineItem>& items, std::initializer_list<int> codes) {
  for (const int code : codes)
    items.push_back(MachineItem::op(code, "tail provenance fixture"));
}
int address_of(const std::vector<MachineItem>& items, const std::string& label) {
  int address = 0;
  for (const auto& item : items) {
    if (item.kind == MachineItemKind::Label) {
      if (item.name == label) return address;
    } else {
      ++address;
    }
  }
  throw std::runtime_error("missing tail label");
}
std::vector<MachineItem> fixture(int selector, bool literal) {
  std::vector<MachineItem> items;
  append(items, {0x61, 0x53});
  items.push_back(MachineItem::address(std::string("caller")));
  append(items, {0x62, 0x63, 0x64, 0x65, 0x10, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  items.push_back(MachineItem::label("caller"));
  append(items, {literal ? 5 : 0x66, 0x80 + selector});
  items.back().indirect_flow_targets = std::vector<IrTarget>{std::string("tail")};
  items.back().roles = {"statement-proc-call", "x-argument-call",
                       "empty-return-startup-tail-call"};
  items.push_back(MachineItem::label("unrelated"));
  append(items, {0, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  items.push_back(MachineItem::label("tail"));
  append(items, {0x47, 0x68, 0x69, 0x60, 0x61, 0x10, 0x52});
  return items;
}
NaturalTargetComponentLayoutResult optimize(const std::vector<MachineItem>& items,
                                             const std::string& selector,
                                             bool allow_annotations = false) {
  const std::vector<PreloadReport> preloads{
      {.register_name = selector, .value = std::to_string(address_of(items, "tail"))}};
  NaturalTargetComponentLayoutOptions options;
  options.maximum_subset_states = 512;
  options.maximum_anchor_combinations = 32;
  options.allow_standalone_fallthrough_jump_fold = true;
  options.allow_unreferenced_jump_labels = allow_annotations;
  return optimize_natural_target_component_layout(
      items, preloads, build_post_layout_control_flow(items), options);
}
std::vector<std::string> observe(const std::vector<MachineItem>& items,
                                 const std::vector<PreloadReport>& preloads,
                                 const std::string& selector, const std::string& seed) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "tail fixture must resolve");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "tail fixture must fit ROM");
  constexpr std::array<const char*, 15> names{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  for (std::size_t i = 0; i < names.size(); ++i)
    calc.set_register(names[i], std::to_string(i + 11U));
  for (const auto& preload : preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.set_register("6", seed);
  calc.set_register("X", "101");
  calc.set_register("Y", "103");
  calc.set_register("Z", "107");
  calc.set_register("T", "109");
  calc.set_register("X1", "113");
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(4000, 6).stopped, "tail fixture must return to caller and stop");
  std::vector<std::string> result;
  for (const char* name : {"X", "Y", "Z", "T", "X1"})
    result.push_back(calc.read_register(name));
  for (const char* name : names)
    if (selector != name) result.push_back(calc.read_register(name));
  // The selector is address-only and independently rebound by layout.
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}
void contracts() {
  for (const int reg : {10, 12, 14})
    for (const bool literal : {false, true}) {
      const std::string selector(1, "0123456789abcde"[reg]);
      const auto before = fixture(reg, literal);
      const auto result = optimize(before, selector);
      require(result.applied > 0 && result.plan.fallthrough_jump_folds == 1,
              "call provenance must not block a proved tail fallthrough");
      require(machine_cell_count(result.items) < machine_cell_count(before),
              "complete tail transaction must shrink");
      require(result.plan.proved && result.plan.final_artifact_proved &&
                  result.plan.control_flow_equivalent && result.plan.call_return_equivalent &&
                  result.plan.stack_and_x2_equivalent && result.plan.indirect_memory_equivalent &&
                  result.plan.data_projection_equivalent && result.plan.final_control_flow.proved,
              "provenance must not substitute for any final proof");
      const auto repeated = optimize(before, selector);
      require(machine_cell_count(repeated.items) == machine_cell_count(result.items) &&
                  repeated.plan.fallthrough_jump_folds == result.plan.fallthrough_jump_folds,
              "tail placement must be deterministic");
      const std::vector<PreloadReport> original_preloads{
          {.register_name = selector, .value = std::to_string(address_of(before, "tail"))}};
      for (const char* seed : {"0", "-3.125", "17"})
        require(observe(before, original_preloads, selector, seed) ==
                    observe(result.items, result.preloads, selector, seed),
                "tail removal must preserve memory, stack/X1/X2 and nonempty return frames");
      for (const char* protected_role : {"protected-layout-cell",
                                         "late-decimal-selector-consumer",
                                         "finalization-cell-origin:17"}) {
        auto protected_items = before;
        for (auto& item : protected_items)
          if (item.kind == MachineItemKind::Op && item.opcode == 0x80 + reg)
            item.roles.push_back(protected_role);
        require(optimize(protected_items, selector).plan.fallthrough_jump_folds == 0,
                "unknown or binding roles remain protected");
      }
      auto raw = before;
      for (auto& item : raw)
        if (item.kind == MachineItemKind::Op && item.opcode == 0x80 + reg)
          item.raw = true;
      require(optimize(raw, selector).plan.fallthrough_jump_folds == 0,
              "call provenance must not admit raw jumps");
    }
}

void labelled_jump_contracts() {
  for (const bool direct : {false, true}) {
    for (const bool literal : {false, true}) {
      auto plain = fixture(10, literal);
      if (direct) {
        const auto at = std::find_if(plain.begin(), plain.end(), [](const auto& item) {
          return item.kind == MachineItemKind::Op && item.opcode == 0x8a;
        });
        at->opcode = 0x51;
        at->indirect_flow_targets.reset();
        plain.insert(at + 1, MachineItem::address(std::string("tail")));
      }
      const auto baseline = optimize(plain, "a");
      require(baseline.applied > 0 && baseline.plan.fallthrough_jump_folds == 1,
              "unlabelled control fixture must fold");
      require(machine_cell_count(plain) == (direct ? 21 : 20),
              "labelled-jump input size baseline");

      auto labelled = plain;
      const auto at = std::find_if(labelled.begin(), labelled.end(), [&](const auto& item) {
        return item.kind == MachineItemKind::Op && item.opcode == (direct ? 0x51 : 0x8a);
      });
      const auto jump = static_cast<std::size_t>(at - labelled.begin());
      labelled.insert(at, {MachineItem::label("unused_note"),
                           MachineItem::label("another_opaque_alias")});
      const auto result = optimize(labelled, "a", true);
      require(result.applied > 0 && result.plan.fallthrough_jump_folds == 1 &&
                  machine_cell_count(result.items) == machine_cell_count(baseline.items),
              "unused zero-width aliases must not change the complete size baseline");
      require(result.plan.proved && result.plan.final_artifact_proved &&
                  result.plan.control_flow_equivalent && result.plan.call_return_equivalent &&
                  result.plan.stack_and_x2_equivalent &&
                  result.plan.indirect_memory_equivalent && result.plan.data_projection_equivalent,
              "unused-label removal still requires every independent final proof");
      require(address_of(result.items, "unused_note") == address_of(result.items, "tail") &&
                  address_of(result.items, "another_opaque_alias") ==
                      address_of(result.items, "tail"),
              "non-executable aliases must survive on the fallthrough destination");
      const std::vector<PreloadReport> preloads{
          {.register_name="a", .value=std::to_string(address_of(labelled, "tail"))}};
      for (const char* seed : {"0", "-3.125", "17"})
        require(observe(labelled, preloads, "a", seed) ==
                    observe(result.items, result.preloads, "a", seed),
                "labelled folding must preserve registers, stack, X1/X2 and nested returns");
      const auto repeated = optimize(labelled, "a", true);
      require(repeated.items.size() == result.items.size() &&
                  std::equal(repeated.items.begin(), repeated.items.end(), result.items.begin(),
                             machine_items_equal),
              "label transport must be deterministic");

      require(optimize(labelled, "a").plan.fallthrough_jump_folds == 0,
              "the established greedy geometry must keep its independent incumbent");
      const LayoutSearchArtifact root{.items=labelled, .preloads=preloads};
      const auto alternatives = expand_joint_representation_transactions(root, {}, true);
      const auto proposed = std::find_if(alternatives.begin(), alternatives.end(),
          [](const auto& edge) { return edge.rule == "unreferenced-jump-label-layout"; });
      require(proposed != alternatives.end() &&
                  machine_cell_count(proposed->node.items) < machine_cell_count(root.items),
              "annotation-aware stitching must reach the complete-artifact frontier");
      for (const char* seed : {"0", "-3.125", "17"})
        require(observe(labelled, preloads, "a", seed) ==
                    observe(proposed->node.items, proposed->node.preloads, "a", seed),
                "the closed frontier candidate must retain the ROM contract");
      const auto incomplete = expand_joint_representation_transactions(root, {}, false);
      require(std::none_of(incomplete.begin(), incomplete.end(), [](const auto& edge) {
                return edge.rule == "unreferenced-jump-label-layout";
              }), "incomplete preload environments must not enable post-layout alternatives");


      for (int variant = 0; variant < 5; ++variant) {
        auto protected_items = labelled;
        auto& annotation = protected_items.at(jump);
        if (variant == 0) annotation.roles = {"protected-layout-cell"};
        if (variant == 1) annotation.raw = true;
        if (variant == 2) annotation.procedure_boundary = "start";
        if (variant == 3) annotation.formal_opcode = 0xf9;
        if (variant == 4)
          annotation.manual_interaction = ManualInteractionAnchor{
              1, 0, ManualInteractionAnchorKind::SingleStepCommand};
        require(optimize(protected_items, "a", true).plan.fallthrough_jump_folds == 0,
                "protected, raw, formal, procedure and manual labels remain barriers");
      }

      for (const bool indirect_entry : {false, true}) {
        auto referenced = labelled;
        std::vector<MachineItem> entry;
        if (indirect_entry) {
          append(entry, {0xab});
          entry.back().indirect_flow_targets =
              std::vector<IrTarget>{std::string("unused_note")};
        } else {
          append(entry, {0x53});
          entry.push_back(MachineItem::address(std::string("unused_note")));
        }
        referenced.insert(referenced.begin() + 1, entry.begin(), entry.end());
        require(optimize(referenced, "a", true).plan.fallthrough_jump_folds == 0,
                "a real direct or indirect entry is not an unused annotation");
      }
      auto resumed = labelled;
      auto prompt = MachineItem::op(0x50, "resume directly onto jump");
      prompt.stop_disposition = StopDisposition::Resumable;
      resumed.insert(resumed.begin() + static_cast<std::ptrdiff_t>(jump), prompt);
      require(optimize(resumed, "a", true).plan.fallthrough_jump_folds == 0,
              "an externally resumable jump cannot lose its entry boundary");
    }
  }
}

} // namespace

int main() {
  try {
    contracts();
    labelled_jump_contracts();
    std::cout << "Tail provenance layout/ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
