#include "mkpro/core/shared_helper_dual_mode_layout.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>

namespace mkpro::core {

namespace {

constexpr int kSideSpaceOrdinalBase = 112;
constexpr int kSideSpaceLastPhysical = 47;
constexpr int kOfficialTailStart = 48;

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
                                        [](const MachineItem& item) {
                                          return item.kind != MachineItemKind::Label;
                                        }));
}

struct ArtifactIndex {
  std::vector<int> addresses;
  std::map<std::string, int> label_addresses;
  std::map<int, std::vector<std::string>> labels_by_address;
  std::map<int, std::size_t> cells;
  int cell_count = 0;
};

ArtifactIndex index_artifact(const std::vector<MachineItem>& items) {
  ArtifactIndex result;
  result.addresses.resize(items.size());
  int address = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    result.addresses.at(item_index) = address;
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Label) {
      result.label_addresses[item.name] = address;
      result.labels_by_address[address].push_back(item.name);
      continue;
    }
    result.cells[address] = item_index;
    ++address;
  }
  result.cell_count = address;
  return result;
}

bool role_is(const CellRole& role, std::string_view prefix) {
  return role.size() >= prefix.size() &&
         std::string_view(role).substr(0, prefix.size()) == prefix;
}

bool has_role(const MachineItem& item, const std::string& role) {
  return std::find(item.roles.begin(), item.roles.end(), role) != item.roles.end();
}

void add_role(MachineItem& item, const std::string& role) {
  if (!has_role(item, role))
    item.roles.push_back(role);
}

void erase_transaction_roles(MachineItem& item, const std::string& marker) {
  const std::string prefix = marker + ":";
  std::erase_if(item.roles, [&](const CellRole& role) {
    return role_is(role, prefix) &&
           !role_is(role, prefix + "origin:");
  });
}

std::optional<std::size_t> unique_role_item(
    const std::vector<MachineItem>& items, const std::string& role) {
  std::optional<std::size_t> result;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (!has_role(items.at(item_index), role))
      continue;
    if (result.has_value())
      return std::nullopt;
    result = item_index;
  }
  return result;
}

std::optional<std::size_t> origin_id(const MachineItem& item,
                                     const std::string& marker) {
  const std::string prefix = marker + ":origin:";
  for (const CellRole& role : item.roles) {
    if (!role_is(role, prefix))
      continue;
    std::size_t value = 0;
    const std::string_view digits(role.data() + prefix.size(),
                                  role.size() - prefix.size());
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size())
      return value;
  }
  return std::nullopt;
}

std::string unique_marker(const std::vector<MachineItem>& items) {
  for (int suffix = 0;; ++suffix) {
    const std::string marker =
        "shared-helper-dual-mode-" + std::to_string(suffix);
    bool used = false;
    for (const MachineItem& item : items) {
      used = std::any_of(item.roles.begin(), item.roles.end(),
                         [&](const CellRole& role) {
                           return role_is(role, marker + ":");
                         });
      if (used)
        break;
    }
    if (!used)
      return marker;
  }
}

std::optional<std::size_t> next_cell_item(const std::vector<MachineItem>& items,
                                          std::size_t after) {
  for (++after; after < items.size(); ++after) {
    if (items.at(after).kind != MachineItemKind::Label)
      return after;
  }
  return std::nullopt;
}

bool is_straight_body_opcode(int opcode) {
  if (opcode_by_code(opcode).takes_address)
    return false;
  const std::vector<IrOp> raised =
      raise_machine_to_ir({MachineItem::op(opcode, opcode_by_code(opcode).name)});
  const IrKind kind = raised.empty() ? IrKind::Plain : raised.front().kind;
  switch (kind) {
  case IrKind::Store:
  case IrKind::Recall:
  case IrKind::IndirectStore:
  case IrKind::IndirectRecall:
  case IrKind::Plain:
    return true;
  default:
    return false;
  }
}

bool is_return_opcode(int opcode) {
  const std::vector<IrOp> raised =
      raise_machine_to_ir({MachineItem::op(opcode, opcode_by_code(opcode).name)});
  return !raised.empty() && raised.front().kind == IrKind::Return;
}

bool is_side_alias(const FormalAddressInfo& info) {
  return (info.kind == FormalAddressKind::LongSide ||
          info.kind == FormalAddressKind::Dark) &&
         !info.one_command;
}

int formal_side_opcode(int physical_address, AddressSpaceModel model) {
  const int ordinal = kSideSpaceOrdinalBase + physical_address;
  for (int high = 0x0b; high <= 0x0f; ++high) {
    const int low = ordinal - high * 10;
    if (low < 0 || low > 0x0f)
      continue;
    const int opcode = high * 16 + low;
    try {
      const FormalAddressInfo info = formal_address_info(opcode, model);
      if (is_side_alias(info) && info.actual == physical_address)
        return opcode;
    } catch (const std::exception&) {
      continue;
    }
  }
  return -1;
}

std::map<std::size_t, std::vector<int>> proved_target_addresses(
    const AuthoritativePostLayoutControlFlow& flow) {
  std::map<std::size_t, std::vector<int>> result;
  for (const auto& [item, targets] : flow.indirect_flow_targets) {
    std::vector<int> addresses;
    addresses.reserve(targets.size());
    for (const PostLayoutCommandIdentity& target : targets)
      addresses.push_back(target.address);
    result.emplace(item, std::move(addresses));
  }
  return result;
}

bool runtime_bound_selector(const MachineItem& item) {
  return std::any_of(item.roles.begin(), item.roles.end(), [](const CellRole& role) {
    return role.find("late-decimal-selector") != std::string::npos ||
           role.find("callee-hole") != std::string::npos ||
           role.find("runtime-charged-selector") != std::string::npos;
  });
}

std::optional<std::size_t> preload_for_register(
    const std::vector<PreloadReport>& preloads, int reg) {
  std::optional<std::size_t> result;
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    int candidate = -1;
    try {
      candidate = register_index(register_from_text(preloads.at(index).register_name));
    } catch (const std::exception&) {
      continue;
    }
    if (candidate != reg)
      continue;
    if (result.has_value())
      return std::nullopt;
    result = index;
  }
  return result;
}

void append_comment(MachineItem& item, const std::string& text) {
  if (item.comment.has_value() && !item.comment->empty())
    item.comment = *item.comment + "; " + text;
  else
    item.comment = text;
}

} // namespace

SharedHelperDualModePreparation prepare_shared_helper_dual_mode_layout(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& control_flow,
    AddressSpaceModel model) {
  SharedHelperDualModePreparation result;
  result.items = items;
  if (!control_flow.proved) {
    result.reasons.push_back("dual-mode preparation requires authoritative control flow");
    return result;
  }

  const SharedHelperContinuationOptions continuation_options{
      .address_space_model = model,
      .proved_indirect_flow_targets = proved_target_addresses(control_flow),
  };
  std::set<std::string> labels;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      labels.insert(item.name);
  }
  std::tuple<bool, std::size_t, std::size_t> best_rejection_score{};
  for (const std::string& label : labels) {
    SharedHelperContinuationProof candidate =
        verify_shared_helper_continuation(items, label, continuation_options);
    if (std::getenv("MKPRO_NATIVE_TRACE_CANDIDATES") != nullptr &&
        candidate.calls.size() >= 2U) {
      std::cerr << "[dual-mode-analysis] helper=" << label
                << " calls=" << candidate.calls.size()
                << " ordinary="
                << candidate.ordinary_call_item_indices.size();
      if (!candidate.reasons.empty())
        std::cerr << " reason=" << candidate.reasons.front();
      std::cerr << "\n";
      for (const SharedHelperContinuationCall& call : candidate.calls) {
        std::cerr << "[dual-mode-analysis]   call@" << call.call_address
                  << " tail=";
        std::optional<std::size_t> cursor = call.operand_item_index;
        for (int cell = 0; cell < 4 && cursor.has_value(); ++cell) {
          cursor = next_cell_item(items, *cursor);
          if (!cursor.has_value())
            break;
          const MachineItem& tail = items.at(*cursor);
          if (tail.kind == MachineItemKind::Op)
            std::cerr << (cell == 0 ? "" : ",")
                      << opcode_by_code(tail.opcode).name;
          else
            std::cerr << (cell == 0 ? "" : ",") << "<address>";
        }
        std::cerr << "\n";
      }
    }
    if (candidate.proved) {
      result.continuation = std::move(candidate);
      break;
    }
    const std::tuple score{
        candidate.calls.size() == 3U &&
            candidate.ordinary_call_item_indices.size() == 2U,
        candidate.ordinary_call_item_indices.size(), candidate.calls.size()};
    if (score > best_rejection_score) {
      best_rejection_score = score;
      result.continuation = std::move(candidate);
    }
  }
  if (!result.continuation.proved) {
    result.reasons = result.continuation.reasons;
    return result;
  }
  if (result.continuation.helper_body_cells <= 0 ||
      result.continuation.helper_body_cells > kOfficialTailStart) {
    result.reasons.push_back("shared helper body cannot end at the F9 boundary");
    return result;
  }

  const std::optional<std::size_t> target = next_cell_item(
      items, result.continuation.helper_label_item_index);
  if (!target.has_value() || *target >= items.size() ||
      items.at(*target).kind != MachineItemKind::Op) {
    result.reasons.push_back("shared helper has no executable root item");
    return result;
  }

  result.marker = unique_marker(items);
  for (std::size_t item_index = 0; item_index < result.items.size(); ++item_index) {
    if (result.items.at(item_index).kind == MachineItemKind::Op) {
      add_role(result.items.at(item_index),
               result.marker + ":origin:" + std::to_string(item_index));
    }
  }
  add_role(result.items.at(*target), result.marker + ":helper-root");
  add_role(result.items.at(result.continuation.helper_return_item_index),
           result.marker + ":helper-return");

  int ordinary = 0;
  for (const SharedHelperContinuationCall& call : result.continuation.calls) {
    if (call.ordinary) {
      const std::string prefix =
          result.marker + ":ordinary-" + std::to_string(ordinary++);
      add_role(result.items.at(call.call_item_index), prefix + ":call");
      add_role(result.items.at(call.join_item_index), prefix + ":join");
      add_role(result.items.at(call.store_item_index), prefix + ":store");
    } else {
      add_role(result.items.at(call.call_item_index),
               result.marker + ":divergent-call");
    }
  }
  if (ordinary != 2) {
    result.reasons.push_back("dual-mode helper does not have exactly two ordinary calls");
    return result;
  }

  result.helper_target_item_index = *target;
  result.required_final_start_address =
      kOfficialTailStart - result.continuation.helper_body_cells;
  result.applied = 1;
  return result;
}

SharedHelperDualModeLayoutResult finalize_shared_helper_dual_mode_layout(
    const SharedHelperDualModePreparation& preparation,
    const std::vector<MachineItem>& placed_items,
    const std::vector<PreloadReport>& placed_preloads,
    const AuthoritativePostLayoutControlFlow& placed_control_flow,
    AddressSpaceModel model) {
  SharedHelperDualModeLayoutResult result;
  result.items = placed_items;
  result.preloads = placed_preloads;
  result.proof.continuation_proved = preparation.continuation.proved;
  result.proof.input_cells = cell_count(placed_items);
  const auto reject = [&](std::string reason) {
    result.proof.reasons.push_back(std::move(reason));
    return result;
  };
  if (preparation.applied <= 0 || !preparation.continuation.proved)
    return reject("dual-mode preparation certificate is absent");
  if (!placed_control_flow.proved)
    return reject("placed artifact lacks authoritative control flow");

  const std::string& marker = preparation.marker;
  const auto helper_root = unique_role_item(placed_items, marker + ":helper-root");
  const auto helper_return = unique_role_item(placed_items, marker + ":helper-return");
  const auto divergent_call =
      unique_role_item(placed_items, marker + ":divergent-call");
  const auto ordinary0_call =
      unique_role_item(placed_items, marker + ":ordinary-0:call");
  const auto ordinary0_join =
      unique_role_item(placed_items, marker + ":ordinary-0:join");
  const auto ordinary0_store =
      unique_role_item(placed_items, marker + ":ordinary-0:store");
  const auto ordinary1_call =
      unique_role_item(placed_items, marker + ":ordinary-1:call");
  const auto ordinary1_join =
      unique_role_item(placed_items, marker + ":ordinary-1:join");
  const auto ordinary1_store =
      unique_role_item(placed_items, marker + ":ordinary-1:store");
  if (!helper_root || !helper_return || !divergent_call ||
      !ordinary0_call || !ordinary0_join || !ordinary0_store ||
      !ordinary1_call || !ordinary1_join || !ordinary1_store) {
    return reject("exact dual-mode command identities did not survive component layout");
  }
  const std::optional<std::size_t> divergent_operand =
      next_cell_item(placed_items, *divergent_call);
  const std::optional<std::size_t> ordinary0_operand =
      next_cell_item(placed_items, *ordinary0_call);
  const std::optional<std::size_t> ordinary1_operand =
      next_cell_item(placed_items, *ordinary1_call);
  if (!divergent_operand || !ordinary0_operand || !ordinary1_operand ||
      placed_items.at(*divergent_operand).kind != MachineItemKind::Address ||
      placed_items.at(*ordinary0_operand).kind != MachineItemKind::Address ||
      placed_items.at(*ordinary1_operand).kind != MachineItemKind::Address) {
    return reject("dual-mode call identity lost its adjacent address operand");
  }
  if (placed_items.at(*ordinary0_join).opcode != preparation.continuation.join_opcode ||
      placed_items.at(*ordinary1_join).opcode != preparation.continuation.join_opcode ||
      placed_items.at(*ordinary0_store).opcode != preparation.continuation.store_opcode ||
      placed_items.at(*ordinary1_store).opcode != preparation.continuation.store_opcode) {
    return reject("ordinary continuation bytes changed during component layout");
  }

  MachineItem shared_join = placed_items.at(*ordinary0_join);
  MachineItem shared_store = placed_items.at(*ordinary0_store);
  erase_transaction_roles(shared_join, marker);
  erase_transaction_roles(shared_store, marker);
  add_role(shared_join, marker + ":shared-join");
  add_role(shared_store, marker + ":shared-store");
  append_comment(shared_join, "shared official helper continuation");
  append_comment(shared_store, "shared official helper continuation");

  struct DirectFlowIdentity {
    std::size_t source_origin = 0;
    std::size_t target_origin = 0;
  };
  const ArtifactIndex placed_index = index_artifact(placed_items);
  std::vector<DirectFlowIdentity> direct_flows;
  for (std::size_t source = 0; source < placed_items.size(); ++source) {
    const MachineItem& command = placed_items.at(source);
    if (command.kind != MachineItemKind::Op ||
        !opcode_by_code(command.opcode).takes_address ||
        source == *divergent_call) {
      continue;
    }
    const std::optional<std::size_t> operand = next_cell_item(placed_items, source);
    if (!operand.has_value() ||
        placed_items.at(*operand).kind != MachineItemKind::Address) {
      return reject("direct flow lost its adjacent address operand");
    }
    const MachineItem& address = placed_items.at(*operand);
    std::optional<int> target_address;
    if (address.formal_opcode.has_value()) {
      try {
        target_address = formal_address_info(*address.formal_opcode, model).actual;
      } catch (const std::exception&) {
        return reject("direct flow has an undecodable formal address");
      }
    } else if (const auto* label = std::get_if<std::string>(&address.target)) {
      const auto found = placed_index.label_addresses.find(*label);
      if (found != placed_index.label_addresses.end())
        target_address = found->second;
    } else if (const auto* numeric = std::get_if<int>(&address.target)) {
      target_address = *numeric;
    }
    if (!target_address.has_value() ||
        !placed_index.cells.contains(*target_address)) {
      return reject("direct flow target cannot be identified before relocation");
    }
    const std::optional<std::size_t> source_origin = origin_id(command, marker);
    const std::optional<std::size_t> target_origin =
        origin_id(placed_items.at(placed_index.cells.at(*target_address)), marker);
    if (!source_origin.has_value() || !target_origin.has_value())
      return reject("direct flow command identity is missing before relocation");
    direct_flows.push_back({*source_origin, *target_origin});
  }

  const std::set<std::size_t> erased{
      *ordinary0_join, *ordinary0_store, *ordinary1_join, *ordinary1_store};
  std::vector<MachineItem> rewritten;
  rewritten.reserve(placed_items.size() - 2U);
  for (std::size_t item_index = 0; item_index < placed_items.size(); ++item_index) {
    if (item_index == *helper_return) {
      rewritten.push_back(shared_join);
      rewritten.push_back(shared_store);
    }
    if (erased.contains(item_index))
      continue;
    MachineItem item = placed_items.at(item_index);
    if (item_index == *ordinary0_operand || item_index == *ordinary1_operand) {
      // The neutral component layout has already resolved this direct operand
      // for the pre-rewrite address.  The atomic tail merge moves the helper
      // root, so retain the typed target identity and let the final resolver
      // encode its new official address instead of publishing a stale byte.
      item.target = preparation.continuation.helper_label;
      item.formal_opcode.reset();
    }
    if (item_index == *divergent_operand) {
      const int formal = formal_side_opcode(
          preparation.required_final_start_address, model);
      if (formal < 0)
        return reject("no exact B2..F9 alias exists for the divergent helper entry");
      item.formal_opcode = formal;
      append_comment(item, "divergent side-space helper entry");
    }
    rewritten.push_back(std::move(item));
  }

  ArtifactIndex final_index = index_artifact(rewritten);
  std::map<std::size_t, std::size_t> final_item_by_origin;
  for (std::size_t item_index = 0; item_index < rewritten.size(); ++item_index) {
    const std::optional<std::size_t> origin = origin_id(rewritten.at(item_index), marker);
    if (!origin.has_value())
      continue;
    if (!final_item_by_origin.emplace(*origin, item_index).second)
      return reject("dual-mode rewrite duplicated a command identity");
  }

  for (const DirectFlowIdentity& flow : direct_flows) {
    if (!final_item_by_origin.contains(flow.source_origin) ||
        !final_item_by_origin.contains(flow.target_origin)) {
      return reject("dual-mode rewrite erased a direct-flow command identity");
    }
    const std::size_t final_source = final_item_by_origin.at(flow.source_origin);
    const std::size_t final_target = final_item_by_origin.at(flow.target_origin);
    const std::optional<std::size_t> final_operand =
        next_cell_item(rewritten, final_source);
    if (!final_operand.has_value() ||
        rewritten.at(*final_operand).kind != MachineItemKind::Address) {
      return reject("relocated direct flow lost its address operand");
    }
    MachineItem& address = rewritten.at(*final_operand);
    const int final_target_address = final_index.addresses.at(final_target);
    const auto labels = final_index.labels_by_address.find(final_target_address);
    if (labels != final_index.labels_by_address.end() && !labels->second.empty())
      address.target = labels->second.front();
    else
      address.target = final_target_address;
    address.formal_opcode.reset();
  }

  struct RebindRequirement {
    int old_target = -1;
    int new_target = -1;
    std::size_t placed_flow_item = 0;
  };
  std::map<int, RebindRequirement> required_by_register;
  for (const auto& [placed_flow_item, targets] :
       placed_control_flow.indirect_flow_targets) {
    if (placed_flow_item >= placed_items.size())
      return reject("placed indirect-flow identity is out of range");
    const std::optional<std::size_t> flow_origin =
        origin_id(placed_items.at(placed_flow_item), marker);
    if (!flow_origin.has_value() || !final_item_by_origin.contains(*flow_origin))
      return reject("dual-mode rewrite lost an indirect-flow command identity");
    MachineItem& final_flow_item = rewritten.at(final_item_by_origin.at(*flow_origin));
    std::vector<IrTarget> rebound_targets;
    std::vector<std::pair<int, int>> target_moves;
    for (const PostLayoutCommandIdentity& target : targets) {
      if (target.item_index >= placed_items.size())
        return reject("placed indirect target identity is out of range");
      const std::optional<std::size_t> target_origin =
          origin_id(placed_items.at(target.item_index), marker);
      if (!target_origin.has_value() || !final_item_by_origin.contains(*target_origin))
        return reject("dual-mode rewrite erased an indirect-flow target");
      const std::size_t final_target_item = final_item_by_origin.at(*target_origin);
      const int final_target_address = final_index.addresses.at(final_target_item);
      const auto labels = final_index.labels_by_address.find(final_target_address);
      if (labels != final_index.labels_by_address.end() && !labels->second.empty())
        rebound_targets.emplace_back(labels->second.front());
      else
        rebound_targets.emplace_back(final_target_address);
      target_moves.emplace_back(target.address, final_target_address);
    }
    final_flow_item.indirect_flow_targets = std::move(rebound_targets);

    if (targets.size() != 1U)
      continue;
    const int reg = final_flow_item.opcode & 0x0f;
    const RebindRequirement requirement{
        .old_target = target_moves.front().first,
        .new_target = target_moves.front().second,
        .placed_flow_item = placed_flow_item,
    };
    const auto [existing, inserted] = required_by_register.emplace(reg, requirement);
    if (!inserted &&
        (existing->second.old_target != requirement.old_target ||
         existing->second.new_target != requirement.new_target)) {
      if (!runtime_bound_selector(final_flow_item))
        return reject("one selector register would require incompatible target rebinding");
    }
  }

  for (const auto& [reg, requirement] : required_by_register) {
    if (requirement.old_target == requirement.new_target)
      continue;
    const std::optional<std::size_t> preload =
        preload_for_register(result.preloads, reg);
    const std::optional<std::size_t> flow_origin =
        origin_id(placed_items.at(requirement.placed_flow_item), marker);
    if (!flow_origin.has_value() || !final_item_by_origin.contains(*flow_origin))
      return reject("selector command identity disappeared during rebinding");
    const MachineItem& final_flow_item =
        rewritten.at(final_item_by_origin.at(*flow_origin));
    if (!preload.has_value()) {
      if (!runtime_bound_selector(final_flow_item))
        return reject("moving indirect target has neither a preload nor a runtime binder");
      continue;
    }
    PreloadReport& report = result.preloads.at(*preload);
    const std::optional<std::string> rebound =
        rebind_stable_preloaded_indirect_flow_selector(
            placed_items, report, placed_control_flow,
            requirement.old_target, requirement.new_target, model);
    if (!rebound.has_value())
      return reject("stable selector preload could not be rebound by command identity");
    if (*rebound != report.value) {
      report.value = *rebound;
      ++result.proof.rebound_preloads;
    }
  }
  result.proof.selector_rebind_proved = true;

  final_index = index_artifact(rewritten);
  const auto root_address = final_index.label_addresses.find(
      preparation.continuation.helper_label);
  if (root_address == final_index.label_addresses.end() ||
      root_address->second != preparation.required_final_start_address) {
    return reject("final helper root does not land on the required F9 suffix start");
  }
  result.proof.body_start_address = root_address->second;
  result.proof.body_end_address = kSideSpaceLastPhysical;
  result.proof.shared_join_address = kOfficialTailStart;
  result.proof.shared_store_address = kOfficialTailStart + 1;
  result.proof.official_return_address = kOfficialTailStart + 2;
  for (int address = result.proof.body_start_address;
       address <= result.proof.body_end_address; ++address) {
    const auto cell = final_index.cells.find(address);
    if (cell == final_index.cells.end() ||
        rewritten.at(cell->second).kind != MachineItemKind::Op ||
        !is_straight_body_opcode(rewritten.at(cell->second).opcode)) {
      return reject("final side-space body is not straight-line through F9");
    }
  }
  const auto join_cell = final_index.cells.find(result.proof.shared_join_address);
  const auto store_cell = final_index.cells.find(result.proof.shared_store_address);
  const auto return_cell = final_index.cells.find(result.proof.official_return_address);
  const auto zero_cell = final_index.cells.find(0);
  if (join_cell == final_index.cells.end() || store_cell == final_index.cells.end() ||
      return_cell == final_index.cells.end() || zero_cell == final_index.cells.end() ||
      rewritten.at(join_cell->second).opcode != preparation.continuation.join_opcode ||
      rewritten.at(store_cell->second).opcode != preparation.continuation.store_opcode ||
      !is_return_opcode(rewritten.at(return_cell->second).opcode) ||
      !is_return_opcode(rewritten.at(zero_cell->second).opcode)) {
    return reject("final dual-mode body/tail/return geometry is not exact");
  }
  result.proof.exact_layout_proved = true;

  const int formal = formal_side_opcode(result.proof.body_start_address, model);
  int ordinary_calls = 0;
  int divergent_calls = 0;
  for (std::size_t item_index = 0; item_index < rewritten.size(); ++item_index) {
    const MachineItem& item = rewritten.at(item_index);
    if (item.kind != MachineItemKind::Op)
      continue;
    if (has_role(item, marker + ":ordinary-0:call") ||
        has_role(item, marker + ":ordinary-1:call")) {
      const std::optional<std::size_t> operand = next_cell_item(rewritten, item_index);
      if (!operand.has_value() ||
          rewritten.at(*operand).kind != MachineItemKind::Address)
        return reject("ordinary helper call lost its address operand");
      const MachineItem& address = rewritten.at(*operand);
      std::optional<int> target_address;
      if (const auto* label = std::get_if<std::string>(&address.target)) {
        const auto found = final_index.label_addresses.find(*label);
        if (found != final_index.label_addresses.end())
          target_address = found->second;
      } else if (const auto* numeric = std::get_if<int>(&address.target)) {
        target_address = *numeric;
      }
      if (address.formal_opcode.has_value() || !target_address.has_value() ||
          *target_address != preparation.required_final_start_address)
        return reject("ordinary helper call no longer uses the official entry");
      ++ordinary_calls;
    }
    if (has_role(item, marker + ":divergent-call")) {
      const std::optional<std::size_t> operand = next_cell_item(rewritten, item_index);
      if (!operand.has_value() ||
          rewritten.at(*operand).kind != MachineItemKind::Address)
        return reject("divergent helper call lost its address operand");
      const MachineItem& address = rewritten.at(*operand);
      if (!address.formal_opcode.has_value() || *address.formal_opcode != formal)
        return reject("divergent helper call no longer uses the proved side entry");
      const FormalAddressInfo info = formal_address_info(*address.formal_opcode, model);
      if (!is_side_alias(info) || info.actual != result.proof.body_start_address)
        return reject("divergent formal address does not resolve to the helper body");
      ++divergent_calls;
    }
  }
  if (ordinary_calls != 2 || divergent_calls != 1)
    return reject("final artifact changed the proved two-plus-one call family");
  result.proof.side_entry_proved = true;

  PostLayoutControlFlowOptions flow_options;
  flow_options.address_space_model = model;
  flow_options.empty_return_target = 1;
  result.proof.final_control_flow =
      build_post_layout_control_flow(rewritten, flow_options);
  if (!result.proof.final_control_flow.proved) {
    return reject("final command-identity CFG is not authoritative" +
                  (result.proof.final_control_flow.reasons.empty()
                       ? std::string{}
                       : ": " + result.proof.final_control_flow.reasons.front()));
  }
  result.proof.final_control_flow_proved = true;

  result.proof.output_cells = final_index.cell_count;
  result.proof.removed_cells =
      result.proof.input_cells - result.proof.output_cells;
  if (result.proof.removed_cells != preparation.continuation.continuation_cells ||
      result.proof.removed_cells <= 0) {
    return reject("dual-mode rewrite did not remove exactly one duplicate tail pair");
  }

  result.proof.proved = true;
  result.items = std::move(rewritten);
  result.applied = 1;
  result.removed_cells = result.proof.removed_cells;
  result.optimizations.push_back(passes::AppliedOptimization{
      .name = "shared-helper-dual-mode-layout",
      .detail = "Moved two byte-identical commutative/store continuations into one "
                "official helper tail and sent the divergent call through the same "
                "straight-line body via its proved F9 side-space entry; removed " +
                std::to_string(result.removed_cells) +
                " cells after selector, stack/X2, and return-stack proofs.",
  });
  return result;
}

} // namespace mkpro::core
