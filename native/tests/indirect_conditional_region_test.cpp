#include "mkpro/core/passes/semantic_region_folding.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using tests::require;

std::map<std::string, int> positions(const std::vector<MachineItem>& code) {
  std::map<std::string, int> out;
  int address = 0;
  for (const auto& item : code) {
    if (item.kind == MachineItemKind::Label) out.emplace(item.name, address);
    else ++address;
  }
  return out;
}

std::vector<MachineItem> fixture(int opcode, bool observe_selector = false, bool nested = false) {
  std::vector<MachineItem> code;
  const auto op = [&](int byte) { code.push_back(MachineItem::op(byte, opcode_by_code(byte).name)); };
  op(0x53); code.push_back(MachineItem::address(std::string(nested ? "wrapper" : "dispatch")));
  if (observe_selector) op(0x67);
  op(0x50); code.back().stop_disposition = StopDisposition::Terminal;
  code.push_back(MachineItem::label("dispatch")); op(0x61);
  op(opcode); code.back().indirect_flow_targets = std::vector<IrTarget>{std::string("other")};
  op(0x62); op(0x31); op(0x52);
  code.push_back(MachineItem::label("other")); op(0x63); op(0x52);
  if (nested) {
    code.push_back(MachineItem::label("wrapper")); op(0x53);
    code.push_back(MachineItem::address(std::string("dispatch"))); op(0x52);
  }
  return code;
}

std::vector<PreloadReport> preloads(const std::vector<MachineItem>& code, int reg = 7) {
  return {{.register_name = "1", .value = "0"}, {.register_name = "2", .value = "12.25"},
      {.register_name = "3", .value = "-3.75"},
      {.register_name = std::string(1, "0123456789abcde"[reg]),
       .value = std::to_string(10000000 + positions(code).at("other"))}};
}

std::vector<core::passes::DeferredIndirectConditionalRegionExchange> candidates(
    const std::vector<MachineItem>& code, const std::vector<PreloadReport>& setup,
    std::optional<int> origin = std::nullopt) {
  CompileOptions options;
  return core::passes::indirect_conditional_region_exchange_candidates(
      raise_machine_to_ir(code), setup, {{"other", origin.value_or(positions(code).at("other"))}},
      core::passes::PassContext{options}, {}, 4);
}

std::vector<std::string> observe(const std::vector<MachineItem>& code,
    const std::vector<PreloadReport>& setup, const std::string& seed) {
  const auto resolved = resolve_machine_items(code);
  require(resolved.diagnostics.empty(), "exchange fixture resolves");
  std::vector<int> bytes;
  for (const auto& step : resolved.steps) bytes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(bytes).diagnostics.empty(), "exchange fixture fits stock memory");
  for (const auto* name : {"X", "Y", "Z", "T", "X1"}) calc.set_register(name, "2.5");
  for (const auto& word : setup) calc.set_register(word.register_name, word.value);
  calc.set_register("1", seed);
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(3000, 8).stopped, "exchanged nested returns reach the original stop");
  std::vector<std::string> out;
  for (const auto* name : {"X", "Y", "Z", "T", "X1", "1", "2", "3"}) out.push_back(calc.read_register(name));
  calc.press("."); out.push_back(calc.display_text(true)); // X2/numeric-entry observation.
  return out;
}

void positive() {
  for (const int opcode : {0x77, 0xe7, 0x97, 0xc7, 0x9e}) {
    for (const bool nested : {false, true}) {
      const auto original = fixture(opcode, false, nested);
      const auto setup = preloads(original, opcode & 15);
      const auto found = candidates(original, setup);
      require(found.size() == 1U, "all four complementary stable conditions admit a closed arm exchange");
      const auto& proposal = found.front();
      require(proposal.transformation.preloads.empty(), "deferred proposal cannot publish a retuned initializer");
      const auto output = lower_ir_to_machine(proposal.transformation.ops);
      require(resolve_machine_items(output).steps.size() == resolve_machine_items(original).steps.size(),
          "exchange is zero-growth, not a hidden branch insertion");
      require(!nested || resolve_machine_items(original).steps.size() == 13U,
          "nested return-context size baseline remains thirteen cells");
      require(nested || resolve_machine_items(original).steps.size() == 10U,
          "standalone zero-growth baseline remains ten cells");
      require(proposal.encoded_target_origins.size() == 1U, "exactly one newly named word-origin equation is exported");
      const auto& [target, origin] = *proposal.encoded_target_origins.begin();
      require(origin == positions(original).at("other"), "origin describes the delivered word, not tentative geometry");
      auto rebound = setup;
      const auto reg = std::find_if(rebound.begin(), rebound.end(), [&](const auto& p) {
        return p.register_name == std::string(1, "0123456789abcde"[opcode & 15]);
      });
      const auto value = core::rebind_stable_preloaded_indirect_flow_selector(
          original, *reg, core::build_post_layout_control_flow(original), origin,
          positions(output).at(target), AddressSpaceModel::Standard, &setup);
      require(value.has_value(), "final selector change obtains its own data/write-back proof");
      reg->value = *value;
      for (const auto* seed : {"-7", "-0.25", "0", "0.25", "9", "DE-2", "-DE-2"})
        require(observe(original, setup, seed) == observe(output, rebound, seed),
            "both labelled outcomes preserve complete ROM stack, X1/X2 and nested return observations");
    }
  }
}

void pinned_layout_saving() {
  auto original = fixture(0x97);
  original.insert(original.begin() + 2, {
      MachineItem::op(0x53, "call fixed anchor"), MachineItem::address(std::string("anchor"))});
  original.push_back(MachineItem::label("anchor"));
  original.push_back(MachineItem::op(0x31, "abs"));
  original.push_back(MachineItem::op(0x52, "return"));
  const auto setup = preloads(original);
  const auto found = candidates(original, setup);
  require(found.size() == 1U, "pinned layout retains the independently certified polarity alternative");
  CompileOptions options;
  const auto place = [&](const std::vector<IrOp>& ops) {
    core::passes::SymbolicRegionProjection projection;
    for (const auto& op : ops) projection.cell_widths.push_back(core::passes::cells_per_op(op));
    projection.defer_indirect_entries = true;
    projection.maximum_padding_cells = 4;
    return core::passes::layout_symbolic_regions(ops, {{"anchor", 9}},
        core::passes::PassContext{options}, 4096, nullptr, true, &projection);
  };
  const auto before = place(raise_machine_to_ir(original));
  const auto after = place(found.front().transformation.ops);
  require(before.has_value() && after.has_value(), "both polarities obtain complete legal fixed-anchor layouts");
  const auto neutral = raise_machine_to_ir(original);
  require(core::passes::symbolic_region_constraints_may_fit(neutral, {}, {{"anchor", 9}},
              "anchor", 9, 12, 4, 17),
          "relaxed geometry must retain the successful anchored layout");
  require(!core::passes::symbolic_region_constraints_may_fit(neutral, {}, {{"anchor", 9}},
              "anchor", 10, 12, 4, 17),
          "an exact anchor cannot satisfy a disjoint encoding range");
  require(!core::passes::symbolic_region_constraints_may_fit(neutral, {}, {{"anchor", 9}},
              "anchor", 9, 12, 4, 14),
          "an exclusive ceiling must charge the minimum necessary padding");
  auto relabelled = original;
  for (auto& item : relabelled)
    if (item.kind == MachineItemKind::Op) item.comment = "independent input";
  const auto repeated = place(raise_machine_to_ir(relabelled));
  require(repeated && repeated->work == before->work &&
      repeated->projected_targets == before->projected_targets &&
      repeated->projected_cells == before->projected_cells,
      "same padding geometry preserves the chosen layout and charged work for another artifact");
  const auto old_code = lower_ir_to_machine(before->ops), new_code = lower_ir_to_machine(after->ops);
  require(resolve_machine_items(old_code).steps.size() == 16U &&
      resolve_machine_items(new_code).steps.size() == 14U,
      "joint polarity and placement reduce the fixed-anchor baseline from sixteen cells to fourteen");
  require(positions(old_code).at("anchor") == 9 && positions(new_code).at("anchor") == 9,
      "the saving cannot move or reinterpret the fixed address");
  const auto close = [&](const std::vector<MachineItem>& code, const std::string& target) {
    auto result = setup;
    const auto word = core::rebind_stable_preloaded_indirect_flow_selector(original, result.back(),
        core::build_post_layout_control_flow(original), positions(original).at("other"),
        positions(code).at(target), AddressSpaceModel::Standard, &setup);
    require(word.has_value(), "each placed polarity discharges its own complete selector obligation");
    result.back().value = *word;
    require(core::build_post_layout_control_flow(code).proved, "final concrete control graph is proved");
    return result;
  };
  const auto old_setup = close(old_code, "other");
  const auto new_setup = close(new_code, found.front().encoded_target_origins.begin()->first);
  for (const auto* seed : {"-7", "0", "3.25"}) {
    const auto expected = observe(original, setup, seed);
    require(observe(old_code, old_setup, seed) == expected && observe(new_code, new_setup, seed) == expected,
        "the measured placement saving preserves full ROM state and both hardware return frames");
  }
}

void numeric_entry_modes() {
  for (const int opcode : {0x77, 0xe7, 0x97, 0xc7}) for (const int digit : {0, 3}) {
    auto original = fixture(opcode);
    for (auto& item : original) {
      if (item.opcode == 0x61) item.opcode = digit;
      else if (item.opcode == 0x62) item.opcode = 4;
      else if (item.opcode == 0x63) item.opcode = 5;
    }
    const auto setup = preloads(original);
    const auto found = candidates(original, setup);
    require(found.size() == 1U, "numeric-entry condition fixture admits complementary arm exchange");
    const auto output = lower_ir_to_machine(found.front().transformation.ops);
    const auto& [target, origin] = *found.front().encoded_target_origins.begin();
    auto rebound = setup;
    const auto value = core::rebind_stable_preloaded_indirect_flow_selector(original, rebound.back(),
        core::build_post_layout_control_flow(original), origin, positions(output).at(target),
        AddressSpaceModel::Standard, &setup);
    require(value.has_value(), "numeric-entry selector obtains a final data proof");
    rebound.back().value = *value;
    require(observe(original, setup, "0") == observe(output, rebound, "0"),
        "a digit following either arm observes identical numeric-entry and X2 state");
  }
}

void negative() {
  const auto original = fixture(0x97);
  const auto setup = preloads(original);
  auto nonfixed = setup;
  nonfixed.back().value = std::to_string(positions(original).at("other"));
  require(candidates(original, nonfixed).empty(),
      "numeric address equality does not prove unchanged full-word write-back");
  require(candidates(original, {}).empty(), "missing initialization is not immutable storage");
  require(candidates(original, setup, positions(original).at("other") + 1).empty(),
      "a stale encoded origin cannot be inferred from the new position");
  auto code = original;
  code.insert(code.begin(), MachineItem::op(0x47, "write selector"));
  require(candidates(code, preloads(code)).empty(), "direct selector writes reject");
  code = original;
  auto alias = MachineItem::op(0xb8, "aliased selector write");
  alias.indirect_memory_targets = std::vector<int>{7};
  code.insert(code.begin(), alias);
  auto alias_setup = preloads(code); alias_setup.push_back({.register_name = "8", .value = "7"});
  require(candidates(code, alias_setup).empty(), "aliased selector writes reject");
  code = fixture(0x93);
  require(candidates(code, preloads(code, 3)).empty(), "mutating low-register conditions are not complementary transports");
  code = fixture(0x97, true);
  require(candidates(code, preloads(code)).empty(), "an observed exact selector value cannot be changed");
  code = original;
  auto second = MachineItem::op(0xa7, "second family consumer");
  second.indirect_flow_targets = std::vector<IrTarget>{std::string("other")};
  code.insert(code.begin(), second);
  require(candidates(code, preloads(code)).empty(), "a shared word cannot keep two different target meanings");
  code = original;
  code.front().raw = true;
  require(candidates(code, setup).empty(), "raw control remains a rewrite barrier");
  code = original;
  auto branch = std::find_if(code.begin(), code.end(), [](const auto& item) { return item.opcode == 0x97; });
  branch->indirect_flow_formal_targets = std::vector<int>{0xb1};
  require(candidates(code, setup).empty(), "short-side target encodings cannot be silently canonicalized");

  code = original;
  branch = std::find_if(code.begin(), code.end(), [](const auto& item) { return item.opcode == 0x97; });
  const auto item = static_cast<std::size_t>(branch - code.begin());
  branch->opcode = 0xc7; // Deliberately leave the original arms in place.
  std::vector<std::optional<std::size_t>> mapping(original.size());
  for (std::size_t i = 0; i < mapping.size(); ++i) mapping[i] = i;
  core::PostLayoutExecutionRelocationOptions transport;
  transport.inverted_indirect_conditional_items = {item};
  require(!core::prove_post_layout_execution_relocation(original, code,
      core::build_post_layout_control_flow(original), core::build_post_layout_control_flow(code), mapping, transport).proved,
      "an opcode flip without the two matching arm relocations is rejected");
  branch->opcode = 0xc8;
  require(!core::prove_post_layout_execution_relocation(original, code,
      core::build_post_layout_control_flow(original), core::build_post_layout_control_flow(code), mapping, transport).proved,
      "condition inversion cannot change the address carrier");
}
} // namespace

int main() {
  try {
    positive(); pinned_layout_saving(); numeric_entry_modes(); negative();
    std::cout << "indirect conditional region compiler and ROM tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
