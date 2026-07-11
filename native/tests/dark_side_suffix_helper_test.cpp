#include "mkpro/core/dark_side_suffix_helper.hpp"

#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::tests {

namespace {

int cell_count(const std::vector<MachineItem>& items);

std::vector<MachineItem> suffix_fixture() {
  std::vector<MachineItem> items = {
      MachineItem::op(0x52, "В/О"), MachineItem::op(0x09, "9"),
      MachineItem::op(0x53, "ПП"),  MachineItem::address(std::string("whole_helper")),
      MachineItem::op(0x40, "хП0"), MachineItem::op(0x04, "4"),
      MachineItem::op(0x53, "ПП"),  MachineItem::address(std::string("tail_entry")),
      MachineItem::op(0x41, "хП1"), MachineItem::op(0x50, "С/П"),
  };

  // Keep official control fenced off before the helper.  The helper starts at
  // 44, so its four executable cells end exactly at physical 47/F9.
  while (static_cast<int>(std::count_if(items.begin(), items.end(), [](const MachineItem& item) {
           return item.kind != MachineItemKind::Label;
         })) < 44) {
    items.push_back(MachineItem::op(0x50, "С/П"));
  }

  items.push_back(MachineItem::label("whole_helper"));
  items.push_back(MachineItem::op(0x21, "Fx^1/2")); // 44
  items.push_back(MachineItem::op(0x0b, "/-/"));    // 45
  items.push_back(MachineItem::label("tail_entry"));
  items.push_back(MachineItem::op(0x31, "F|x|")); // 46
  items.push_back(MachineItem::op(0x22, "Fx^2")); // 47/F9
  items.push_back(MachineItem::op(0x52, "В/О"));  // removable 48
  return items;
}

std::vector<MachineItem> nine_cell_suffix_fixture() {
  std::vector<MachineItem> items = {
      MachineItem::op(0x52, "В/О"), MachineItem::op(0x07, "7"),
      MachineItem::op(0x53, "ПП"),  MachineItem::address(std::string("nine_cell_helper")),
      MachineItem::op(0x40, "хП0"), MachineItem::op(0x50, "С/П"),
  };
  while (cell_count(items) < 39)
    items.push_back(MachineItem::op(0x50, "С/П"));
  items.push_back(MachineItem::label("nine_cell_helper"));
  for (int index = 0; index < 9; ++index)
    items.push_back(MachineItem::op(0x31, "F|x|"));
  items.push_back(MachineItem::op(0x52, "В/О"));
  return items;
}

std::size_t item_at_address(const std::vector<MachineItem>& items, int wanted) {
  int address = 0;
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (items.at(index).kind == MachineItemKind::Label)
      continue;
    if (address == wanted)
      return index;
    ++address;
  }
  throw std::runtime_error("fixture address is absent: " + std::to_string(wanted));
}

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const MachineItem& item) {
    return item.kind != MachineItemKind::Label;
  }));
}

std::string compact(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) { return std::isspace(ch) != 0; }),
              value.end());
  return value;
}

struct EmulatorOutcome {
  bool stopped = false;
  std::string pc;
  std::string x;
  std::string r0;
  std::string r1;
};

EmulatorOutcome run(const std::vector<MachineItem>& items) {
  const ResolvedProgram resolved = resolve_machine_items(items, {});
  require(resolved.diagnostics.empty(), "dark-side fixture should resolve without diagnostics");
  std::vector<int> codes;
  codes.reserve(resolved.steps.size());
  for (const ResolvedStep& step : resolved.steps)
    codes.push_back(step.opcode);

  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), "dark-side fixture should fit the MK-61 program memory");
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult stable = calc.run_until_stable(1200, 6);
  return EmulatorOutcome{
      .stopped = stable.stopped,
      .pc = calc.program_counter(),
      .x = compact(calc.read_register("x")),
      .r0 = compact(calc.read_register("0")),
      .r1 = compact(calc.read_register("1")),
  };
}

bool contains_reason(const core::DarkSideSuffixHelperProof& proof, std::string_view needle) {
  return std::any_of(proof.reasons.begin(), proof.reasons.end(), [&](const std::string& reason) {
    return reason.find(needle) != std::string::npos;
  });
}

} // namespace

void dark_side_suffix_helper_rewrites_only_proved_layouts() {
  const std::vector<MachineItem> baseline = suffix_fixture();
  const core::DarkSideSuffixHelperProof proof =
      core::verify_dark_side_suffix_helper(baseline, "whole_helper");
  require(proof.proved && proof.reasons.empty(),
          "straight helper ending at F9 with В/О@48 should be proved");
  require(proof.body_start_address == 44 && proof.body_end_address == 47 && proof.body_cells == 4 &&
              proof.explicit_return_address == 48,
          "proof should expose the exact physical suffix and removable return");
  require(proof.entries.size() == 2U && proof.calls.size() == 2U,
          "proof should retain both full-body and internal-suffix entries");
  require(proof.entries.at(0).label == "whole_helper" && proof.entries.at(0).entry_address == 44 &&
              proof.entries.at(0).formal_opcode == 0xf6 &&
              proof.entries.at(1).label == "tail_entry" &&
              proof.entries.at(1).entry_address == 46 && proof.entries.at(1).formal_opcode == 0xf8,
          "physical 44/46 entries should bind to formal F6/F8 aliases");

  const core::DarkSideSuffixHelperResult rewritten =
      core::rewrite_dark_side_suffix_helper(baseline, "whole_helper");
  require(rewritten.applied == 1 && rewritten.proof.proved && rewritten.proof.final_artifact_proved,
          "proved helper should rewrite only after a final-artifact recheck");
  require(cell_count(baseline) == 49 && cell_count(rewritten.items) == 48,
          "implicit F9 return should remove exactly the explicit В/О cell");
  require(rewritten.items.at(item_at_address(rewritten.items, 3)).formal_opcode == 0xf6 &&
              rewritten.items.at(item_at_address(rewritten.items, 7)).formal_opcode == 0xf8,
          "final direct-call operands should contain the proved dark aliases");
  const MachineItem& f9 = rewritten.items.at(item_at_address(rewritten.items, 47));
  require(f9.comment.has_value() &&
              f9.comment->find("dark-side suffix boundary return") != std::string::npos,
          "final F9 cell should carry an auditable boundary-return proof marker");

  const EmulatorOutcome direct = run(baseline);
  const EmulatorOutcome dark = run(rewritten.items);
  require(direct.stopped && dark.stopped && direct.pc == dark.pc && direct.x == dark.x &&
              direct.r0 == dark.r0 && direct.r1 == dark.r1,
          "formal dark calls with implicit boundary return should emulate exactly like direct "
          "calls: direct=" +
              direct.pc + "/" + direct.x + "/" + direct.r0 + "/" + direct.r1 + " dark=" + dark.pc +
              "/" + dark.x + "/" + dark.r0 + "/" + dark.r1);
  require(dark.r0 == "9," && dark.r1 == "16,",
          "full and internal suffix entries should both execute their intended unary bodies");

  const core::DarkSideSuffixHelperResult automatic =
      core::optimize_dark_side_suffix_helper(baseline);
  require(automatic.applied == 1 && automatic.proof.helper_label == "whole_helper" &&
              automatic.proof.final_artifact_proved,
          "post-layout candidate scan should select the same universally proved helper");

  // Some physical entries have two formal spellings (physical 39 is both EB
  // and F1).  The binder chooses the lowest, non-F spelling used by the compact
  // reference layout and proves it through the same ROM behavior.
  {
    const std::vector<MachineItem> nine_cells = nine_cell_suffix_fixture();
    const core::DarkSideSuffixHelperResult eb =
        core::rewrite_dark_side_suffix_helper(nine_cells, "nine_cell_helper");
    require(eb.applied == 1 && eb.proof.entries.size() == 1U &&
                eb.proof.entries.front().entry_address == 39 &&
                eb.proof.entries.front().formal_opcode == 0xeb,
            "physical39 should prefer formal EB over the equivalent F1 alias");
    const EmulatorOutcome direct_nine = run(nine_cells);
    const EmulatorOutcome dark_nine = run(eb.items);
    require(direct_nine.stopped && dark_nine.stopped && direct_nine.r0 == dark_nine.r0 &&
                dark_nine.r0 == "7,",
            "the generic nine-cell EB suffix should preserve direct-call behavior");
  }

  // Crossing the side space reaches physical 00; without В/О there, the
  // apparent F9 layout is not a subroutine and must fail closed.
  {
    std::vector<MachineItem> no_return_zero = baseline;
    no_return_zero.at(item_at_address(no_return_zero, 0)) = MachineItem::op(0x00, "0");
    const core::DarkSideSuffixHelperResult rejected =
        core::rewrite_dark_side_suffix_helper(no_return_zero, "whole_helper");
    require(rejected.applied == 0 && contains_reason(rejected.proof, "physical 00"),
            "missing В/О at physical00 must reject the boundary-return rewrite");
  }

  // Internal control flow is not a straight-line helper, even when the final
  // addresses happen to line up.
  {
    std::vector<MachineItem> unsafe = baseline;
    const std::size_t body_cell = item_at_address(unsafe, 45);
    unsafe.at(body_cell) = MachineItem::op(0x80, "КБП0");
    const core::DarkSideSuffixHelperResult rejected =
        core::rewrite_dark_side_suffix_helper(unsafe, "whole_helper");
    require(rejected.applied == 0 && rejected.items.size() == unsafe.size() &&
                contains_reason(rejected.proof, "contains control flow"),
            "body control flow must reject the rewrite atomically");
  }

  // Ending at 46 rather than F9 cannot acquire the side-space boundary return.
  {
    std::vector<MachineItem> misaligned = baseline;
    misaligned.erase(misaligned.begin() +
                     static_cast<std::ptrdiff_t>(item_at_address(misaligned, 20)));
    const core::DarkSideSuffixHelperResult rejected =
        core::rewrite_dark_side_suffix_helper(misaligned, "whole_helper");
    require(rejected.applied == 0 && contains_reason(rejected.proof, "explicit В/О"),
            "a helper not ending at physical F9 must be rejected");
  }

  // Every official entry reference must be a direct ПП; a jump would observe
  // different continuation semantics after deleting the return.
  {
    std::vector<MachineItem> jumped = baseline;
    jumped.at(item_at_address(jumped, 2)).opcode = 0x51;
    jumped.at(item_at_address(jumped, 2)).mnemonic = "БП";
    const core::DarkSideSuffixHelperResult rejected =
        core::rewrite_dark_side_suffix_helper(jumped, "whole_helper");
    require(rejected.applied == 0 && contains_reason(rejected.proof, "non-ПП reference"),
            "non-call official helper entries must fail closed");
  }

  // Unknown computed flow is rejected.  A complete target proof is accepted
  // only when every target remains below the moved suffix.
  {
    std::vector<MachineItem> computed = baseline;
    const std::size_t flow_item = item_at_address(computed, 20);
    computed.at(flow_item) = MachineItem::op(0x80, "КБП0");
    const core::DarkSideSuffixHelperResult unknown =
        core::rewrite_dark_side_suffix_helper(computed, "whole_helper");
    require(unknown.applied == 0 &&
                contains_reason(unknown.proof, "no complete final-artifact target proof"),
            "unproved indirect flow must reject the transformation");

    core::DarkSideSuffixHelperOptions safe_options;
    safe_options.proved_indirect_flow_targets[flow_item] = {0};
    const core::DarkSideSuffixHelperResult safe =
        core::rewrite_dark_side_suffix_helper(computed, "whole_helper", safe_options);
    require(safe.applied == 1 && safe.proof.final_artifact_proved,
            "complete indirect targets below the suffix should satisfy final proof");

    core::DarkSideSuffixHelperOptions shifted_options;
    shifted_options.proved_indirect_flow_targets[flow_item] = {50};
    const core::DarkSideSuffixHelperResult shifted =
        core::rewrite_dark_side_suffix_helper(computed, "whole_helper", shifted_options);
    require(shifted.applied == 0 && contains_reason(shifted.proof, "can enter or shift"),
            "indirect targets crossing deleted physical48 must be rejected");
  }
}

} // namespace mkpro::tests
