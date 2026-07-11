#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/result.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace mkpro::core {

struct TerminalRetryCellRef {
  std::size_t item_index = 0;
  int address = -1;
};

struct TerminalRetryCallRef {
  TerminalRetryCellRef call;
  TerminalRetryCellRef operand;
};

struct TerminalRetryRegisterAccessRef {
  TerminalRetryCellRef cell;
  int opcode = -1;
};

// A register move which is safe without re-running expression lowering: every
// listed use is a direct store/recall, whose stack and X2 behavior is
// independent of the selected physical register.
struct TerminalRetryRegisterRemap {
  int from_register = -1;
  int to_register = -1;
  bool moves_preload = false;
  std::size_t preload_index = 0;
  std::string preload_value;
  std::vector<TerminalRetryRegisterAccessRef> accesses;
};

// Facts consumed transactionally by the outer component builder.  This pass
// does not emit its seven-cell prefix by itself: until the separately proved
// helper-entry component is placed at the derived constant target, such an
// intermediate artifact would not be executable.
struct TerminalRetrySentinelPlan {
  bool proved = false;
  std::vector<std::string> reasons;

  int input_cells = 0;
  int source_prefix_cells = 0;
  int replacement_prefix_cells = 0;
  int projected_cells = 0;
  int projected_delta = 0;

  std::string header_label;
  std::string helper_label;
  std::string success_label;

  TerminalRetryCellRef header_recall;
  TerminalRetryCellRef prompt_stop;
  TerminalRetryCellRef first_input_store;
  TerminalRetryCellRef second_input_store;
  TerminalRetryCallRef local_helper_call;
  TerminalRetryCellRef occupied_join;
  TerminalRetryCellRef occupied_store;
  TerminalRetryCellRef changed_subtract;
  TerminalRetryCallRef changed_branch;
  TerminalRetryCellRef sentinel_recall;
  TerminalRetryCellRef sentinel_store;
  TerminalRetryCellRef retry_flow;
  TerminalRetryCellRef success_entry;
  std::vector<TerminalRetryCallRef> complete_helper_calls;

  int display_register = -1;
  int first_input_register = -1;
  int second_input_register = -1;
  int occupied_register = -1;
  int low_input_slot_register = -1;
  int sentinel_register = -1;
  int immutable_constant_register = -1;
  int direct_only_dynamic_register = -1;
  int free_stable_raw_seed_register = -1;
  int retry_flow_register = -1;

  std::size_t removed_sentinel_preload_index = 0;
  std::string removed_sentinel_preload_value;
  int rehomed_constant_flow_target = -1;
  std::vector<TerminalRetryRegisterRemap> register_remaps;
  std::map<std::string, std::string> projected_logical_registers;

  // Exact source->post-prefix address facts needed to reindex roles and
  // external component plans without consulting comments or source names.
  std::vector<std::pair<int, int>> address_reindex;

  // Seven cells emitted only by the transactional outer rewrite:
  //   00 В/О; 01 recall second input; 02 recall low slot; 03 STOP;
  //   04 store low slot; 05 indirect PP constant; 06 indirect x=0 low.
  std::vector<MachineItem> replacement_prefix;

  bool bare_return_zero_proved = false;
  bool manual_pp_then_run_proved = false;
  int target_prompt_stop_address = -1;
  int target_first_input_store_address = -1;
  int target_helper_call_address = -1;
  int target_helper_entry_address = -1;
  int target_retry_conditional_address = -1;
  std::vector<int> admitted_low_inputs;
};

struct TerminalRetrySentinelDiscoveryOptions {
  // MachineItems cannot prove which manual key resumes each entered() phase.
  // The caller must supply this fact from the manual-entry protocol analysis.
  bool manual_pp_then_run_proved = false;
  // This is the complete source-proved input domain, not a program signature.
  // Discovery checks every value through the generic indirect-address oracle
  // and accepts any non-empty subset whose predecrement target remains inside
  // the four-cell manual return header.
  std::vector<int> admitted_low_inputs;

  // Complete physical targets for indirect flow cells used by this local
  // pattern.  In particular, the source retry flow must have the header as its
  // sole target. Unknown or additional targets fail closed.
  std::map<std::size_t, std::vector<int>> proved_indirect_flow_targets;

};

// Discovers only the manual entry/retry prefix and a register-access remap.
// It deliberately does not inspect score/max/loop/draw semantics or helper
// bodies. Matching uses opcodes, CFG targets, preload data, and liveness.
TerminalRetrySentinelPlan discover_terminal_retry_sentinel_plan(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const std::map<std::string, std::string>& logical_registers,
    const TerminalRetrySentinelDiscoveryOptions& options = {});

} // namespace mkpro::core
