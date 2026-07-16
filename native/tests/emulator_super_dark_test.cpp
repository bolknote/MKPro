#include "mkpro/core/post_layout_indirect_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace mkpro::tests {

namespace {

MachineItem op(int opcode, std::string mnemonic) {
  return MachineItem::op(opcode, std::move(mnemonic));
}

MachineItem address(const std::string& target) {
  return MachineItem::address(target);
}

MachineItem label(const std::string& name) {
  return MachineItem::label(name);
}

std::vector<MachineItem> super_dark_program() {
  std::vector<MachineItem> items;
  items.push_back(op(0x51, "БП"));
  items.push_back(address("site"));
  for (int index = 0; index < 4; ++index)
    items.push_back(op(0x0d, "Cx"));
  items.push_back(label("cont"));
  items.push_back(op(0x50, "С/П"));
  for (int index = 0; index < 46; ++index)
    items.push_back(op(0x0d, "Cx"));
  items.push_back(label("entry"));
  items.push_back(op(0x07, "7"));
  items.push_back(op(0x51, "БП"));
  items.push_back(address("cont"));
  items.push_back(label("site"));
  items.push_back(op(0x51, "БП"));
  items.push_back(address("entry"));
  return items;
}

std::vector<MachineItem> super_dark_address_overlay_program() {
  std::vector<MachineItem> items = {op(0x51, "БП"), address("site")};
  items.push_back(label("continuation"));
  items.push_back(op(0x51, "БП"));
  items.push_back(address("done"));
  for (int index = 0; index < 45; ++index) {
    if (index < 7) {
      const std::string name =
          index == 0 ? "8" : index == 1 ? "9"
                                          : std::string(1, static_cast<char>('a' + index - 2));
      items.push_back(op(0x48 + index, "X->П " + name));
    } else {
      items.push_back(op(0x0d, "Cx"));
    }
  }
  items.push_back(label("entry"));
  items.push_back(op(0x07, "7"));
  items.push_back(op(0x51, "БП"));
  items.back().raw = true;
  items.push_back(address("continuation"));
  items.push_back(label("site"));
  items.push_back(op(0x51, "БП"));
  items.push_back(address("entry"));
  items.push_back(label("done"));
  items.push_back(op(0x50, "С/П"));
  return items;
}

int address_opcode(int address) {
  return ((address / 10) << 4) | (address % 10);
}

std::vector<int> lower_items(const std::vector<MachineItem>& items) {
  std::map<std::string, int> address_of;
  int address = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label) {
      address_of[item.name] = address;
    } else {
      ++address;
    }
  }

  std::vector<int> codes;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      continue;
    if (item.kind == MachineItemKind::Op) {
      codes.push_back(item.opcode);
      continue;
    }
    if (std::holds_alternative<int>(item.target)) {
      codes.push_back(item.formal_opcode.value_or(address_opcode(std::get<int>(item.target))));
    } else {
      codes.push_back(item.formal_opcode.value_or(
          address_opcode(address_of.at(std::get<std::string>(item.target)))));
    }
  }
  return codes;
}

std::string mk61_hex_literal(const std::string& text) {
  std::string out;
  for (char ch : text) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))) {
      case 'A':
        out.push_back('-');
        break;
      case 'B':
        out.push_back('L');
        break;
      case 'C':
        out += "С";
        break;
      case 'D':
        out += "Г";
        break;
      case 'E':
        out += "Е";
        break;
      case 'F':
        out.push_back('_');
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

struct Observation {
  std::string display;
  bool stopped = false;
};

Observation run(const std::vector<int>& codes, const std::vector<PreloadReport>& preloads = {}) {
  emulator::MK61 calc;
  calc.load_program(codes);
  for (const PreloadReport& preload : preloads) {
    calc.set_register(preload.register_name, mk61_hex_literal(preload.value));
  }
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult stable = calc.run_until_stable(400, 5);
  return Observation{.display = calc.display_text(), .stopped = stable.stopped};
}

} // namespace

void emulator_super_dark_matches_typescript_contract() {
  CompileOptions options;
  options.delivery = DeliveryMode::Manual;
  options.budget = 999999;
  options.analysis = true;

  const std::vector<MachineItem> program = super_dark_program();
  const core::PostLayoutIndirectFlowResult result =
      core::optimize_post_layout_indirect_flow(program, options, 0);

  require(result.applied >= 1, "super-dark dispatch should select a post-layout rewrite");
  require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                      [](const core::passes::AppliedOptimization& optimization) {
                        return optimization.name == "preloaded-super-dark-flow";
                      }),
          "super-dark dispatch should report the TS optimization name");
  require(std::any_of(result.preloads.begin(), result.preloads.end(),
                      [](const PreloadReport& preload) {
                        return !preload.counts_against_program;
                      }),
          "super-dark dispatch should be backed by a flow preload");
  require(std::any_of(result.items.begin(), result.items.end(), [](const MachineItem& item) {
            return item.kind == MachineItemKind::Op && item.opcode >= 0x80 && item.opcode <= 0x8e;
          }),
          "super-dark dispatch should emit a real indirect branch");

  const Observation before = run(lower_items(program));
  const Observation after = run(lower_items(result.items), result.preloads);

  require(before.stopped, "super-dark baseline program should stop");
  require(before.display.find("7") != std::string::npos,
          "super-dark baseline should display marker 7");
  require(after.stopped == before.stopped,
          "super-dark optimized program should preserve stopped state");
  require(after.display == before.display,
          "super-dark optimized program should preserve display output");

  const std::vector<MachineItem> overlay_program = super_dark_address_overlay_program();
  const core::PostLayoutIndirectFlowResult flow_only =
      core::optimize_post_layout_indirect_flow(overlay_program, options, 0);
  const core::PostLayoutIndirectFlowResult packed =
      core::optimize_post_layout_super_dark_address_overlay(overlay_program, options, 0);
  require(core::machine_cell_count(packed.items) < core::machine_cell_count(flow_only.items),
          "joint super-dark/address overlay should save a cell beyond flow-only lowering");
  require(std::any_of(packed.optimizations.begin(), packed.optimizations.end(),
                      [](const core::passes::AppliedOptimization& optimization) {
                        return optimization.name == "super-dark-address-code-overlay";
                      }),
          "joint super-dark/address overlay should report its combined proof");
  require(std::any_of(packed.preloads.begin(), packed.preloads.end(),
                      [](const PreloadReport& preload) { return preload.value == "FA"; }),
          "joint super-dark/address overlay should preload the FA selector");

  const Observation overlay_before = run(lower_items(overlay_program));
  const Observation overlay_after = run(lower_items(packed.items), packed.preloads);
  require(overlay_before.stopped && overlay_after.stopped,
          "joint super-dark/address overlay baseline and result should stop");
  require(overlay_before.display.find("7") != std::string::npos,
          "joint super-dark/address overlay baseline should display marker 7");
  require(overlay_after.display == overlay_before.display,
          "joint super-dark/address overlay should preserve emulator-visible output");
}

} // namespace mkpro::tests
