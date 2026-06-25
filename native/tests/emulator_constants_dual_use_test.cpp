#include "mkpro/core/post_layout_indirect_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <map>
#include <set>
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

std::vector<MachineItem> two_site_program() {
  std::vector<MachineItem> items;
  items.push_back(op(0x51, "БП"));
  items.push_back(address("site1"));
  items.push_back(op(0x0d, "Cx"));
  items.push_back(op(0x0d, "Cx"));
  items.push_back(op(0x0d, "Cx"));
  items.push_back(label("head"));
  items.push_back(op(0x01, "1"));
  items.push_back(op(0x50, "С/П"));
  items.push_back(label("site1"));
  items.push_back(op(0x51, "БП"));
  items.push_back(address("head"));
  items.push_back(label("site2"));
  items.push_back(op(0x51, "БП"));
  items.push_back(address("head"));
  return items;
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
      codes.push_back(std::get<int>(item.target));
    } else {
      codes.push_back(address_of.at(std::get<std::string>(item.target)));
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

void emulator_constants_dual_use_matches_typescript_contract() {
  CompileOptions options;
  options.delivery = DeliveryMode::Manual;
  options.budget = 999999;
  options.analysis = true;

  const std::vector<MachineItem> program = two_site_program();
  const core::PostLayoutIndirectFlowResult result =
      core::optimize_post_layout_indirect_flow(program, options, 0);

  require(result.applied >= 2,
          "constants-dual-use should rewrite both head-targeting sites");
  std::vector<PreloadReport> flow_preloads;
  std::copy_if(result.preloads.begin(), result.preloads.end(), std::back_inserter(flow_preloads),
               [](const PreloadReport& preload) { return !preload.counts_against_program; });
  require(!flow_preloads.empty(),
          "constants-dual-use post-layout rewrite should report at least one flow preload");

  std::set<std::string> preload_registers;
  for (const PreloadReport& preload : flow_preloads)
    preload_registers.insert(preload.register_name);

  int indirect_count = 0;
  for (const MachineItem& item : result.items) {
    if (item.kind == MachineItemKind::Op && item.opcode >= 0x80 && item.opcode <= 0x8e) {
      ++indirect_count;
      const std::string reg = std::to_string(item.opcode - 0x80);
      require(preload_registers.contains(reg),
              "constants-dual-use indirect branch should be backed by a flow preload");
    }
  }
  require(indirect_count >= 2,
          "constants-dual-use should emit indirect flow for both head-targeting branches");

  const Observation before = run(lower_items(program));
  const Observation after = run(lower_items(result.items), flow_preloads);

  require(before.stopped, "constants-dual-use baseline program should stop");
  require(before.display.find("1") != std::string::npos,
          "constants-dual-use baseline should display marker 1");
  require(after.stopped == before.stopped,
          "constants-dual-use optimized program should preserve stopped state");
  require(after.display == before.display,
          "constants-dual-use optimized program should preserve display output");
}

} // namespace mkpro::tests
