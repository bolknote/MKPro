#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/post_layout_indirect_flow.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace mkpro::tests {
namespace {

MachineItem stopped(StopDisposition disposition) {
  auto item = MachineItem::op(0x50, "stop");
  item.stop_disposition = disposition;
  return item;
}

MachineItem phase(MachineItem item, int number, ManualInteractionAnchorKind kind) {
  item.manual_interaction = ManualInteractionAnchor{.protocol_id = 12, .phase = number, .kind = kind};
  return item;
}

std::size_t after_label(const std::vector<MachineItem>& items, const std::string& label) {
  for (std::size_t index = 0; index + 1U < items.size(); ++index)
    if (items.at(index).kind == MachineItemKind::Label && items.at(index).name == label)
      return index + 1U;
  throw std::runtime_error("missing test label " + label);
}

std::vector<MachineItem> called_protocol() {
  return {
      MachineItem::op(0x05, "5"), MachineItem::op(0x47, "store 7"),
      MachineItem::op(0x53, "call"), MachineItem::address("input"),
      MachineItem::op(0x53, "call"), MachineItem::address("input"),
      stopped(StopDisposition::Terminal),
      MachineItem::label("input"),
      phase(stopped(StopDisposition::Resumable), -1, ManualInteractionAnchorKind::PromptStop),
      phase(MachineItem::op(0x49, "store 9"), 0, ManualInteractionAnchorKind::SingleStepCommand),
      phase(MachineItem::op(0x4a, "store a"), 1, ManualInteractionAnchorKind::ContinuousResume),
      MachineItem::label("after_input"),
      MachineItem::op(0x67, "recall 7"), MachineItem::op(0x52, "return"),
  };
}

std::vector<MachineItem> manual_calls(int first_register) {
  return {
      MachineItem::op(0x60 + first_register, "recall first"),
      phase(stopped(StopDisposition::Resumable), -1, ManualInteractionAnchorKind::PromptStop),
      phase(MachineItem::op(0x40 + first_register, "store first"), 0,
            ManualInteractionAnchorKind::SingleStepCommand),
      phase(MachineItem::op(0x41, "store second"), 1, ManualInteractionAnchorKind::ContinuousResume),
      MachineItem::op(0x60 + first_register, "recall first"),
      MachineItem::op(0x61, "recall second"), MachineItem::op(0x10, "+"),
      MachineItem::op(0x53, "call"), MachineItem::address("double"),
      MachineItem::op(0x53, "call"), MachineItem::address("double"),
      stopped(StopDisposition::Terminal),
      MachineItem::label("double"),
      MachineItem::op(0x02, "2"), MachineItem::op(0x12, "*"),
      MachineItem::op(0x52, "return"),
  };
}

std::vector<MachineItem> resume_contexts(bool error) {
  auto pause = MachineItem::op(error ? 0x29 : 0x50, "pause");
  pause.stop_disposition = StopDisposition::Resumable;
  std::vector<MachineItem> items{
      MachineItem::op(0x05, "5"), MachineItem::op(0x47, "store 7"),
      MachineItem::op(0x53, "call"), MachineItem::address("pause"),
      MachineItem::op(0x67, "recall 7"), MachineItem::op(0x48, "store 8"),
      MachineItem::op(0x06, "6"), MachineItem::op(0x47, "store 7"),
      MachineItem::op(0x53, "call"), MachineItem::address("pause"),
      MachineItem::op(0x67, "recall 7"), MachineItem::op(0x49, "store 9"),
      MachineItem::label("done"), stopped(StopDisposition::Terminal),
      MachineItem::label("pause"), pause};
  // A real write makes the skipped error cell observable if the continuation
  // is off by one; it must not overwrite the charged R7.
  if (error) items.push_back(MachineItem::op(0x47, "skipped store 7"));
  items.push_back(MachineItem::op(0x4a, "store entered X"));
  items.push_back(MachineItem::op(0x52, "return"));
  return items;
}

void observed_bank_cannot_supply_flow_selectors() {
  auto read = MachineItem::op(0xd5, "read selected data");
  read.indirect_memory_targets = std::vector<int>{6, 7, 8, 9, 10, 11, 12, 13, 14};
  const std::vector<MachineItem> items{
      read, MachineItem::op(0x53, "call"), MachineItem::address("twice"),
      MachineItem::op(0x53, "call"), MachineItem::address("twice"),
      stopped(StopDisposition::Terminal),
      MachineItem::label("twice"), MachineItem::op(0x02, "2"),
      MachineItem::op(0x12, "*"), MachineItem::op(0x52, "return")};
  CompileOptions options;
  options.aggressive_post_layout_indirect_flow = true;
  options.preloaded_indirect_flow = true;
  options.forward_indirect_flow = true;
  const auto optimized = core::optimize_post_layout_indirect_flow(items, options, 0);
  require(optimized.applied == 0 && optimized.preloads.empty(),
          "every stable flow selector overlaps the observed indirect data window");
  require(core::build_post_layout_control_flow(items).proved,
          "the rejected selector layout must have a valid baseline CFG");
  const auto image = resolve_machine_items(items);
  require(image.diagnostics.empty(), "observed data-window ROM image must resolve");
  std::vector<int> codes;
  for (const auto& step : image.steps) codes.push_back(step.opcode);
  for (int target = 6; target <= 14; ++target) {
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(), "observed data-window ROM image loads");
    for (int index = 6; index <= 14; ++index) {
      const std::string reg(1, "0123456789abcde"[index]);
      calc.set_register(reg, std::to_string(100 + index));
    }
    calc.set_register("5", std::to_string(target - 1));
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(600, 5).stopped &&
                std::stoi(calc.read_register("x")) == 4 * (100 + target),
            "every possible data register affects the visible result and cannot be an address");
  }
}

void exact_resume_predecessors() {
  for (bool error : {false, true}) {
    const auto items = resume_contexts(error);
    const auto flow = core::build_post_layout_control_flow(items);
    require(flow.proved, "both called resume contexts must have an exact CFG");
    const auto values = core::analyze_stable_register_value_flow(items, {}, flow);
    const auto done = after_label(items, "done");
    const auto pause = after_label(items, "pause");
    require(values.proved && values.before_item.at(done).at(0) == "6" &&
                values.before_item.at(done).at(1) == "5" &&
                values.before_item.at(done).at(2) == "6",
            "a repeated pause must not mix register charges between caller frames");
    require(!values.before_item.at(done).at(3),
            "a store of user-entered X must stay unknown after every resume");
    if (error)
      require(!values.before_item.contains(pause + 1U),
              "error padding is not an executed value-flow state");

    auto unlabelled = flow;
    unlabelled.execution_edges.clear();
    require(!core::analyze_stable_register_value_flow(items, {}, unlabelled).proved,
            "resume analysis requires authoritative labelled edges");
    auto unlinked = flow;
    for (auto& edges : unlinked.execution_edges)
      for (auto& edge : edges)
        if (edge.kind == core::PostLayoutExecutionEdgeKind::Resume)
          edge.kind = core::PostLayoutExecutionEdgeKind::Fallthrough;
    require(!core::analyze_stable_register_value_flow(items, {}, unlinked).proved,
            "a matching physical successor without a Resume edge is not a user boundary");
    auto missing_root = flow;
    std::erase_if(missing_root.external_entries, [](const auto& entry) {
      return entry.kind == core::ExternalEntryKind::ResumableStop;
    });
    require(!core::analyze_stable_register_value_flow(items, {}, missing_root).proved,
            "unclassified resume roots must not preserve a pre-input X value");
    auto wrong_frame = flow;
    const auto entry = std::find_if(
        wrong_frame.external_entries.begin(), wrong_frame.external_entries.end(),
        [](const auto& root) { return root.kind == core::ExternalEntryKind::ResumableStop; });
    require(entry != wrong_frame.external_entries.end() && !entry->formal_return_stack.empty(),
            "the resume fixture must retain a caller return frame");
    entry->formal_return_stack.front() = 0xff;
    require(!core::analyze_stable_register_value_flow(items, {}, wrong_frame).proved,
            "resume matching must preserve formal as well as physical return frames");
    for (const bool wrong_opcode : {false, true}) {
      auto stale = items;
      if (wrong_opcode) stale.at(pause).opcode = 0x54;
      else stale.at(pause).stop_disposition = StopDisposition::Terminal;
      require(!core::analyze_stable_register_value_flow(stale, {}, flow).proved,
              "a stale graph must not invent a resumable stop");
    }

    const auto resolved = resolve_machine_items(items);
    require(resolved.diagnostics.empty(), "resume-context ROM fixture must resolve");
    std::vector<int> codes;
    for (const auto& step : resolved.steps) codes.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(), "resume-context ROM fixture must load");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(600, 5).stopped, "the first called pause must stop");
    require(std::stoi(calc.read_register("7")) == 5, "first pause must retain its charge");
    calc.input_number("2", true).press("С/П");
    require(calc.run_until_stable(600, 5).stopped, "the second called pause must stop");
    require(std::stoi(calc.read_register("7")) == 6 &&
                std::stoi(calc.read_register("8")) == 5,
            "the first call must return to its own continuation");
    calc.input_number("3", true).press("С/П");
    require(calc.run_until_stable(600, 5).stopped, "the second call must return to final stop");
    require(std::stoi(calc.read_register("7")) == 6 &&
                std::stoi(calc.read_register("8")) == 5 &&
                std::stoi(calc.read_register("9")) == 6 &&
                std::stoi(calc.read_register("a")) == 3,
            "ROM resumes must preserve caller contexts and skip exactly the error pad");
  }
}

std::string literal(std::string text) {
  std::string result;
  for (char ch : text) {
    switch (ch) {
      case 'A': result += "-"; break;
      case 'B': result += "L"; break;
      case 'C': result += "С"; break;
      case 'D': result += "Г"; break;
      case 'E': result += "Е"; break;
      case 'F': result += "_"; break;
      default: result += ch; break;
    }
  }
  return result;
}

std::vector<std::string> run_manual_calls(const std::vector<MachineItem>& items,
                                         const std::vector<PreloadReport>& preloads,
                                         int first_register, int first, int second) {
  const auto image = resolve_machine_items(items);
  require(image.diagnostics.empty(), "manual-flow ROM image must resolve");
  std::vector<int> codes;
  for (const auto& step : image.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "manual-flow ROM image must load");
  for (const auto& preload : preloads)
    calc.set_register(preload.register_name, literal(preload.value));
  calc.set_register(std::to_string(first_register), "6");
  for (const std::string seed : {"14", "13", "12", "11"})
    calc.input_number(seed, true).press("В↑");
  calc.input_number("0", true);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(600, 5).stopped, "manual-flow prompt must stop");
  calc.input_number(std::to_string(first), true).press("ПП");
  require(calc.run_until_stable(600, 5).stopped, "manual-flow single step must stop");
  calc.input_number(std::to_string(second), true).press("С/П");
  require(calc.run_until_stable(600, 5).stopped, "manual-flow calls must return and stop");
  require(std::stod(calc.read_register("X")) == (first + second) * 4,
          "manual input and both helper calls must affect the result");
  require(std::stod(calc.read_register(std::to_string(first_register))) == first &&
              std::stod(calc.read_register("1")) == second,
          "manual input stores must preserve both entered values");
  std::vector<std::string> result;
  for (const std::string reg : {"X", "Y", "Z", "T", "X1"})
    result.push_back(calc.read_register(reg));
  calc.press(".");
  result.push_back(calc.display_text());
  return result;
}

} // namespace

void stable_register_value_flow_preserves_manual_protocol_state() {
  exact_resume_predecessors();
  observed_bank_cannot_supply_flow_selectors();
  const auto items = called_protocol();
  const auto flow = core::build_post_layout_control_flow(items);
  require(flow.proved, "called manual protocol must have an authoritative graph");
  const std::vector<PreloadReport> preloads{{.register_name = "8", .value = "99"}};
  const auto values = core::analyze_stable_register_value_flow(items, preloads, flow);
  require(values.proved, "typed manual input must inherit predecessor register values");
  const auto after = after_label(items, "after_input");
  const auto& registers = values.before_item.at(after);
  require(registers.at(0) == "5" && registers.at(1) == "99",
          "runtime charge and setup selector must survive both input phases and both calls");
  require(!registers.at(2) && !registers.at(3),
          "each newly entered X must invalidate the register written by its own phase");
  require(std::count_if(flow.external_entries.begin(), flow.external_entries.end(),
                       [](const auto& entry) {
                         return entry.kind == core::ExternalEntryKind::ManualSingleStep ||
                                entry.kind == core::ExternalEntryKind::ManualContinuous;
                       }) == 4,
          "manual protocol must retain both distinct caller return contexts");

  auto overwritten = items;
  overwritten.at(after - 3U).opcode = 0x47;
  const auto overwritten_flow = core::build_post_layout_control_flow(overwritten);
  require(overwritten_flow.proved, "manual selector overwrite remains a valid protocol");
  const auto overwritten_values =
      core::analyze_stable_register_value_flow(overwritten, preloads, overwritten_flow);
  require(overwritten_values.proved && !overwritten_values.before_item.at(after).at(0) &&
              overwritten_values.before_item.at(after).at(1) == "99",
          "input into a charged register must invalidate it without losing other registers");

  auto missing_anchor = flow;
  const auto manual = std::find_if(missing_anchor.external_entries.begin(),
                                   missing_anchor.external_entries.end(), [](const auto& entry) {
                                     return entry.kind == core::ExternalEntryKind::ManualSingleStep;
                                   });
  require(manual != missing_anchor.external_entries.end(), "fixture must expose a manual phase");
  manual->manual_interaction.reset();
  require(!core::analyze_stable_register_value_flow(items, preloads, missing_anchor).proved,
          "an untyped manual root must not inherit preloads");

  auto wrong_frame = flow;
  const auto framed = std::find_if(wrong_frame.external_entries.begin(),
                                   wrong_frame.external_entries.end(), [](const auto& entry) {
                                     return entry.kind == core::ExternalEntryKind::ManualSingleStep;
                                   });
  require(framed != wrong_frame.external_entries.end() && !framed->formal_return_stack.empty(),
          "called manual phase must carry its formal return frame");
  framed->formal_return_stack.front() = 0xff;
  require(!core::analyze_stable_register_value_flow(items, preloads, wrong_frame).proved,
          "matching physical addresses must not hide a different formal return frame");

  auto detached = flow;
  const auto prompt = after_label(items, "input");
  for (std::size_t index = 0; index < detached.execution_states.size(); ++index) {
    if (detached.execution_states.at(index).item_index == prompt) {
      detached.execution_successors.at(index).clear();
      detached.execution_edges.at(index).clear();
    }
  }
  require(!core::analyze_stable_register_value_flow(items, preloads, detached).proved,
          "a manual phase detached from its prompt must not acquire setup values");

  for (int first_register : {0, 7}) {
    const auto before = manual_calls(first_register);
    CompileOptions options;
    options.aggressive_post_layout_indirect_flow = true;
    options.preloaded_indirect_flow = true;
    options.forward_indirect_flow = true;
    const auto after_calls = core::optimize_post_layout_indirect_flow(before, options, 0);
    require(after_calls.applied >= 2 &&
                core::machine_cell_count(after_calls.items) < core::machine_cell_count(before),
            "proved manual input must permit repeated calls through a spare selector");
    if (first_register == 7)
      require(std::none_of(after_calls.preloads.begin(), after_calls.preloads.end(),
                           [](const auto& preload) { return preload.register_name == "7"; }),
              "an entered data register must never be borrowed as the call selector");
    for (const auto& pair : std::vector<std::pair<int, int>>{{0, 0}, {2, 3}, {7, 4}})
      require(run_manual_calls(before, {}, first_register, pair.first, pair.second) ==
                  run_manual_calls(after_calls.items, after_calls.preloads,
                                   first_register, pair.first, pair.second),
              "manual call packing must preserve result, X/Y/Z/T, X1 and X2 in the ROM");
  }
}

} // namespace mkpro::tests
