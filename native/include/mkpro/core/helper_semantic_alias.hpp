#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/result.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

// A deliberately small, exact semantic language for compiler-issued unary-X
// helper certificates.  The post-layout optimizer never interprets source
// names, comments, roles, or procedure spelling; it only evaluates these
// typed trees on a proved finite integral domain.
enum class HelperSemanticOp {
  InputX,
  Integer,
  Add,
  Subtract,
  Multiply,
  Divide,
  Truncate,
  Fraction,
  OneBasedModulo,
};

struct HelperSemanticExpr;
using HelperSemanticExprPtr = std::shared_ptr<HelperSemanticExpr>;

struct HelperSemanticExpr {
  HelperSemanticOp op = HelperSemanticOp::InputX;
  std::int64_t integer = 0;
  int width = 0;
  HelperSemanticExprPtr left;
  HelperSemanticExprPtr right;
};

struct ExactIntegralDomain {
  std::int64_t minimum = 0;
  std::int64_t maximum = -1;
  bool proved_integral = false;

  bool valid() const {
    return proved_integral && minimum <= maximum;
  }
  std::uint64_t cardinality() const;
};

// The only ABI admitted by the first implementation is the generic unary-X
// expression-helper ABI already emitted by the compiler: X becomes f(X), Y
// and Z retain their incoming values, sticky T becomes incoming Z, no memory
// register is touched, and one return frame is popped. Hidden-X2 restore
// context and physical X1 are separate proof obligations below.
enum class HelperMachineAbi {
  UnaryXPreserveYZStickyZReturnSync,
};

struct HelperSemanticContract {
  // Opaque exact label.  Consumers compare identity/address only and never
  // inspect its spelling.
  std::string entry_label;
  HelperSemanticExprPtr expression;
  ExactIntegralDomain admitted_input;
  // Premises about the actual machine representation delivered at every
  // admitted source call. Semantic equality is rejected unless derivation was
  // decimal-exact, and a domain containing zero additionally proves +0.
  bool input_decimal_derivation_exact = false;
  bool input_zero_canonical_positive = false;
  HelperMachineAbi abi = HelperMachineAbi::UnaryXPreserveYZStickyZReturnSync;
  bool all_source_entries_accounted = false;
  // Exact post-layout call command identities certified by the typed
  // producer. The verifier independently rescans all incoming edges and
  // requires set equality, so an extra raw/merged/alternate entry fails.
  std::vector<std::size_t> admitted_call_items;
  // Opaque source-call provenance. A preceding optimizer may merge call
  // sites only by carrying the exact union onto the surviving physical edge.
  std::vector<std::uint64_t> admitted_call_origins;
  // Opaque procedure-boundary identity used solely to remove the matching
  // trailing boundary label after a late post-layout alias.
  std::string procedure_boundary_id;
  // Mathematical rational equality is not enough for repeating MK-61
  // division. The producer sets this only when every admitted intermediate is
  // exact in the calculator's decimal mantissa model. Producers certify only
  // admitted forms whose complete intermediates pass that decimal proof; this
  // flag does not infer decimal exactness merely from rational equality.
  bool decimal_execution_exact = false;
  bool hidden_x2_return_sync_proved = false;
  bool x1_effect_proved = false;
  std::string x1_effect_key;
  // Immutable relocation-invariant encoding of the exact certified helper
  // body. It covers every opcode and every internal address operand by its
  // target offset from the procedure entry. Consumers recompute and require
  // exact equality, so a later pass cannot silently reuse semantic premises
  // after changing either an instruction or a branch target.
  std::string certified_body_key;
};

struct HelperSemanticAliasOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
  std::size_t maximum_domain_values = 64;
  std::size_t maximum_helper_cells = 32;
  std::size_t maximum_continuation_states = 4096;
  IrTarget main_entry = 0;
  std::optional<IrTarget> empty_return_target;
  std::vector<PreloadReport> effective_preloads;
  // The caller must immediately compose this structural rewrite with a
  // downstream pass that rebinds and proves every effective preload.
  bool defer_preload_rebind = false;
  // Numeric indirect facts are admitted only when the final authoritative CFG
  // maps every such command and target back to the same retained identities.
  bool identity_check_numeric_indirect_flow = false;
};

struct HelperSemanticAliasCallProof {
  std::size_t original_call_item = 0;
  std::size_t original_continuation_item = 0;
  bool was_indirect = false;
  bool return_stack_equivalent = false;
  bool hidden_x2_equivalent = false;
  bool x1_continuation_safe = false;
};

struct HelperSemanticAliasProof {
  bool proved = false;
  bool rewrite_proved = false;
  bool semantic_equivalent = false;
  bool machine_abi_equivalent = false;
  bool source_entries_complete = false;
  bool pre_control_flow_proved = false;
  bool final_control_flow_proved = false;
  bool external_entries_equivalent = false;
  bool indirect_memory_equivalent = false;
  bool symbolic_relocation_proved = false;
  bool fixed_targets_equivalent = false;
  bool preload_rebind_deferred = false;
  bool final_artifact_proved = false;
  std::string removed_entry_label;
  std::string replacement_entry_label;
  ExactIntegralDomain domain;
  int input_cells = 0;
  int output_cells = 0;
  int removed_body_cells = 0;
  int added_call_cells = 0;
  int source_call_origins = 0;
  std::vector<HelperSemanticAliasCallProof> calls;
  AuthoritativePostLayoutControlFlow final_control_flow;
  std::vector<std::string> reasons;
};

struct HelperSemanticAliasResult {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  HelperSemanticAliasProof proof;
  int applied = 0;
};

HelperSemanticExprPtr helper_semantic_input();
HelperSemanticExprPtr helper_semantic_integer(std::int64_t value);
HelperSemanticExprPtr helper_semantic_unary(HelperSemanticOp op, HelperSemanticExprPtr value);
HelperSemanticExprPtr helper_semantic_binary(HelperSemanticOp op, HelperSemanticExprPtr left,
                                             HelperSemanticExprPtr right);
HelperSemanticExprPtr helper_semantic_one_based_modulo(HelperSemanticExprPtr value, int width);
bool helper_semantic_decimal_execution_exact(const HelperSemanticExprPtr& expression,
                                             const ExactIntegralDomain& domain,
                                             std::size_t maximum_values = 64);
std::optional<std::string> helper_semantic_alias_body_key(const std::vector<MachineItem>& items,
                                                          const std::string& entry_label,
                                                          std::size_t maximum_helper_cells = 32);

// Scan all ordered contract pairs, choose the most profitable completely
// proved alias, redirect the complete call set, and erase the redundant body.
// Failure is fail-closed and returns `items` unchanged.
HelperSemanticAliasResult
optimize_helper_semantic_alias(const std::vector<MachineItem>& items,
                               const std::vector<HelperSemanticContract>& contracts,
                               const HelperSemanticAliasOptions& options = {});

} // namespace mkpro::core
