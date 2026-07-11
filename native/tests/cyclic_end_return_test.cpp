#include "mkpro/core/cyclic_end_return.hpp"

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

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const MachineItem& item) {
    return item.kind != MachineItemKind::Label;
  }));
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

std::vector<MachineItem> fixture(bool one_cell_over_limit) {
  std::vector<MachineItem> items = {
      MachineItem::label("main"),
      MachineItem::op(0x52, "В/О"),
      MachineItem::op(0x09, "9"),
      MachineItem::op(0x53, "ПП"),
      MachineItem::address(std::string("square_abs")),
      MachineItem::op(0x40, "X->П 0"),
      MachineItem::op(0x04, "4"),
      MachineItem::op(0x53, "ПП"),
      MachineItem::address(std::string("abs_tail")),
      MachineItem::op(0x41, "X->П 1"),
      MachineItem::op(0x50, "С/П"),
  };
  while (cell_count(items) < 40)
    items.push_back(MachineItem::op(0x50, "С/П"));

  MachineItem root = MachineItem::label("square_abs");
  root.procedure_boundary = "start";
  root.procedure_name = "square_abs";
  items.push_back(std::move(root));
  items.push_back(MachineItem::op(0x22, "F x^2")); // physical 40
  items.push_back(MachineItem::label("abs_tail"));
  items.push_back(MachineItem::op(0x31, "F |x|")); // physical 41
  items.push_back(MachineItem::op(0x52, "В/О"));   // physical 42
  MachineItem end = MachineItem::label("__proc_end_square_abs");
  end.procedure_boundary = "end";
  end.procedure_name = "square_abs";
  items.push_back(std::move(end));

  if (one_cell_over_limit) {
    while (cell_count(items) < 106)
      items.push_back(MachineItem::op(0x50, "С/П"));
  }
  return items;
}

std::string compact(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) { return std::isspace(ch) != 0; }),
              value.end());
  return value;
}

struct Outcome {
  bool stopped = false;
  std::string r0;
  std::string r1;
};

Outcome run(const std::vector<MachineItem>& items, std::string_view context) {
  const ResolvedProgram resolved = resolve_machine_items(items, {});
  require(resolved.diagnostics.empty(), std::string(context) + " should resolve");
  std::vector<int> codes;
  codes.reserve(resolved.steps.size());
  for (const ResolvedStep& step : resolved.steps)
    codes.push_back(step.opcode);

  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), std::string(context) + " should fit physical memory");
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult stable = calc.run_until_stable(2000, 8);
  return Outcome{
      .stopped = stable.stopped,
      .r0 = compact(calc.read_register("0")),
      .r1 = compact(calc.read_register("1")),
  };
}

bool contains_reason(const core::CyclicEndReturnProof& proof, std::string_view needle) {
  return std::any_of(proof.reasons.begin(), proof.reasons.end(), [&](const std::string& reason) {
    return reason.find(needle) != std::string::npos;
  });
}

core::CyclicEndReturnOptions complete_options() {
  return core::CyclicEndReturnOptions{
      .external_entry_addresses = std::vector<int>{1},
  };
}

} // namespace

void cyclic_end_return_relocates_only_proved_direct_call_helpers() {
  const std::vector<MachineItem> input = fixture(true);
  const core::CyclicEndReturnProof proof =
      core::verify_cyclic_end_return(input, "square_abs", complete_options());
  require(proof.proved && proof.reasons.empty(),
          "isolated direct-call helper in a 106-cell artifact should be proved");
  require(proof.input_cells == 106 && proof.original_body_start_address == 40 &&
              proof.original_body_end_address == 41 && proof.body_cells == 2 &&
              proof.original_explicit_return_address == 42,
          "proof should expose the original helper body and explicit return");
  require(proof.relocated_body_start_address == 103 &&
              proof.relocated_body_end_address == 104 &&
              proof.relocated_explicit_return_address == 105,
          "two-cell body should be laid out at A3..A4 with its removable return at 105");
  require(proof.entries.size() == 2U && proof.calls.size() == 2U &&
              proof.entries.at(0).label == "square_abs" &&
              proof.entries.at(0).relocated_address == 103 &&
              proof.entries.at(1).label == "abs_tail" &&
              proof.entries.at(1).relocated_address == 104,
          "proof should retain both direct-call entries and their relocated addresses");

  const core::CyclicEndReturnResult rewritten =
      core::rewrite_cyclic_end_return(input, "square_abs", complete_options());
  require(rewritten.applied == 1 && rewritten.proof.proved &&
              rewritten.proof.final_artifact_proved,
          "proved helper should relocate only after a final-artifact recheck");
  require(cell_count(rewritten.items) == 105 && rewritten.proof.output_cells == 105,
          "rewrite should remove the sole cell beyond A4");
  const MachineItem& a4 = rewritten.items.at(item_at_address(rewritten.items, 104));
  require(a4.kind == MachineItemKind::Op && a4.opcode == 0x31 && a4.comment.has_value() &&
              a4.comment->find("cyclic-end boundary return via 00") != std::string::npos &&
              std::find(a4.roles.begin(), a4.roles.end(), "cyclic-end-return:A4-to-00") !=
                  a4.roles.end(),
          "final A4 body command should carry an auditable cyclic-return marker");

  const ResolvedProgram resolved = resolve_machine_items(rewritten.items, {});
  require(resolved.diagnostics.empty() && resolved.steps.size() == 105U,
          "rewritten artifact should resolve exactly through A4");
  require(resolved.steps.at(3).opcode == 0xa3 && resolved.steps.at(7).opcode == 0xa4,
          "direct helper calls should follow their labels to relocated A3/A4 entries");

  const Outcome ordinary = run(fixture(false), "ordinary explicit-return helper");
  const Outcome cyclic = run(rewritten.items, "cyclic A4-return helper");
  require(ordinary.stopped && cyclic.stopped && ordinary.r0 == cyclic.r0 &&
              ordinary.r1 == cyclic.r1 && cyclic.r0 == "81," && cyclic.r1 == "4,",
          "A4-to-00 return should preserve both whole-helper and internal-tail calls");

  const core::CyclicEndReturnResult automatic =
      core::optimize_cyclic_end_return(input, complete_options());
  require(automatic.applied == 1 && automatic.proof.helper_label == "square_abs" &&
              automatic.proof.final_artifact_proved,
          "automatic scan should select the same proved helper");

  {
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(input, "square_abs");
    require(rejected.applied == 0 && contains_reason(rejected.proof, "external-entry set"),
            "omitting external-entry completeness must fail closed");
  }

  {
    core::CyclicEndReturnOptions expanded;
    expanded.address_space_model = AddressSpaceModel::Mk61SMiniExpanded;
    expanded.external_entry_addresses = std::vector<int>{1};
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(input, "square_abs", expanded);
    require(rejected.applied == 0 && contains_reason(rejected.proof, "standard"),
            "expanded 112-cell profile must reject A4-to-00 wrap assumptions");
  }

  {
    std::vector<MachineItem> no_return_zero = input;
    no_return_zero.at(item_at_address(no_return_zero, 0)) = MachineItem::op(0x00, "0");
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(no_return_zero, "square_abs", complete_options());
    require(rejected.applied == 0 && contains_reason(rejected.proof, "physical 00"),
            "missing В/О at physical00 must fail closed");
  }

  {
    std::vector<MachineItem> indirect = input;
    indirect.at(item_at_address(indirect, 20)) = MachineItem::op(0x8e, "К БП e");
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(indirect, "square_abs", complete_options());
    require(rejected.applied == 0 && contains_reason(rejected.proof, "indirect flow"),
            "unproved indirect flow must reject helper relocation");
  }

  {
    std::vector<MachineItem> non_call = input;
    non_call.at(item_at_address(non_call, 6)) = MachineItem::op(0x51, "БП");
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(non_call, "square_abs", complete_options());
    require(rejected.applied == 0 && contains_reason(rejected.proof, "non-ПП"),
            "jump entry into the helper must fail closed");
  }

  {
    std::vector<MachineItem> body_flow = input;
    body_flow.at(item_at_address(body_flow, 40)) = MachineItem::op(0x8e, "К БП e");
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(body_flow, "square_abs", complete_options());
    require(rejected.applied == 0 && contains_reason(rejected.proof, "control flow"),
            "helper body control flow must reject cyclic suffix layout");
  }

  {
    std::vector<MachineItem> old_fallthrough = input;
    old_fallthrough.at(item_at_address(old_fallthrough, 39)) = MachineItem::op(0x01, "1");
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(old_fallthrough, "square_abs", complete_options());
    require(rejected.applied == 0 && contains_reason(rejected.proof, "old site"),
            "fallthrough into the helper's old location must reject relocation");
  }

  {
    std::vector<MachineItem> new_fallthrough = input;
    new_fallthrough.at(item_at_address(new_fallthrough, 105)) = MachineItem::op(0x01, "1");
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(new_fallthrough, "square_abs", complete_options());
    require(rejected.applied == 0 && contains_reason(rejected.proof, "relocated helper"),
            "fallthrough from the remaining tail into the relocated helper must fail closed");
  }

  {
    std::vector<MachineItem> fixed_address = input;
    fixed_address.at(item_at_address(fixed_address, 3)).target = 40;
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(fixed_address, "square_abs", complete_options());
    require(rejected.applied == 0 && contains_reason(rejected.proof, "fixed numeric"),
            "numeric address operands must fail closed across relocation");
  }

  {
    std::vector<MachineItem> overlaid = input;
    const std::size_t operand = item_at_address(overlaid, 3);
    overlaid.insert(overlaid.begin() + static_cast<std::ptrdiff_t>(operand),
                    MachineItem::label("operand_overlay"));
    overlaid.at(item_at_address(overlaid, 20)) = MachineItem::op(0x51, "БП");
    overlaid.at(item_at_address(overlaid, 21)) =
        MachineItem::address(std::string("operand_overlay"));
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(overlaid, "square_abs", complete_options());
    require(rejected.applied == 0 && contains_reason(rejected.proof, "address/code overlay"),
            "executable address operands must fail closed when relocation would change them: " +
                (rejected.proof.reasons.empty() ? std::string("no reason")
                                                : rejected.proof.reasons.front()));
  }

  {
    std::vector<MachineItem> eof_target = input;
    eof_target.at(item_at_address(eof_target, 20)) = MachineItem::op(0x51, "БП");
    eof_target.at(item_at_address(eof_target, 21)) =
        MachineItem::address(std::string("past_program"));
    eof_target.push_back(MachineItem::label("past_program"));
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(eof_target, "square_abs", complete_options());
    require(rejected.applied == 0 && contains_reason(rejected.proof, "executable command cell"),
            "a symbolic EOF label must not pass as an executable direct-flow target");
  }

  {
    std::vector<MachineItem> removed_return_target = input;
    removed_return_target.at(item_at_address(removed_return_target, 20)) =
        MachineItem::op(0x53, "ПП");
    removed_return_target.at(item_at_address(removed_return_target, 21)) =
        MachineItem::address(std::string("removed_return_entry"));
    removed_return_target.insert(
        removed_return_target.begin() +
            static_cast<std::ptrdiff_t>(item_at_address(removed_return_target, 42)),
        MachineItem::label("removed_return_entry"));
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(removed_return_target, "square_abs", complete_options());
    require(rejected.applied == 0 && contains_reason(rejected.proof, "executable command cell"),
            "the final gate must reject a symbolic label stranded by return removal");
  }

  {
    std::vector<MachineItem> wrong_size = input;
    wrong_size.erase(wrong_size.begin() +
                     static_cast<std::ptrdiff_t>(item_at_address(wrong_size, 105)));
    const core::CyclicEndReturnResult rejected =
        core::rewrite_cyclic_end_return(wrong_size, "square_abs", complete_options());
    require(rejected.applied == 0 && contains_reason(rejected.proof, "exactly one"),
            "artifact without exactly one removable over-limit cell must fail closed");
  }
}

} // namespace mkpro::tests
