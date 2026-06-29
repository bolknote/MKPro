#pragma once

// Address-formula solver (shipping core).
//
// Given a dispatch table {input value -> jump target address}, search a library
// of cheap MK-61 unary functions plus an affine pre-transform (scale, offset) so
// that resolve(op(scale * input + offset)) lands on every requested target.
//
// This is the in-pipeline counterpart of the offline research tool
// (tools/address_solver.cpp).  Crucially it is EMULATOR-FREE: candidate formulas
// are confirmed with the compiler's own static indirect-address model
// (evaluate_indirect_address), never by running the emulator.  The emulator
// stays an offline oracle in the research tool only.
//
// Trig ops (Ftg / Farctg) interpret/produce angles per the Р/Г/ГРД switch, so
// they are only offered when the angle mode is contractually FIXED for the whole
// run (expected_mode_only(...)).  sin/cos are intentionally absent: their exact
// ROM rule is not derived, so they must never back address synthesis.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::core {

// Angle mode the trig ops compile for.  Only meaningful when the dispatch run
// has a contractually fixed mode (see expected_mode_only).
enum class SolverAngleMode {
  Deg,
  Rad,
  Grd,
};

struct AddressConstraint {
  double input = 0.0;  // the live register/selector value at the call site
  int target = 0;      // the program address that value must reach
};

struct AddressFormula {
  std::string op_name;       // e.g. "id", "int", "tan"
  int op_opcode = -1;        // MK-61 opcode for the unary op, -1 for identity
  double scale = 1.0;        // affine pre-multiply
  double offset = 0.0;       // affine pre-add
  int approx_cost = 0;       // rough cell cost (op + literals + indirect jump)
  bool uses_fixed_angle = false;  // true => requires a pinned angle mode
};

// Canonical static resolver: a register/selector value -> indirect FLOW target.
// Delegates to evaluate_indirect_address (no emulator).  Returns nullopt when the
// value does not resolve to a usable flow target.
std::optional<int> resolve_flow_target(std::string_view selector, double value);

struct AddressSolverOptions {
  bool allow_affine = true;        // search a (scale, offset) pair, not just scale
  bool angle_fixed = false;        // offer trig ops (mode is contractually pinned)
  SolverAngleMode angle_mode = SolverAngleMode::Deg;
  double scale_min = -180.0;
  double scale_max = 180.0;
  double scale_step = 0.1;
  double offset_abs_max = 99.0;    // affine offset search bound (integer steps)
};

// Search the cheapest op + affine so that, for every constraint,
// resolve_flow_target(selector, op(scale*input + offset)) == target.
// Every returned formula is re-confirmed through resolve_flow_target (the static
// model), so callers can trust it without the emulator.  Returns nullopt when no
// formula in the configured library/grid satisfies all constraints.
std::optional<AddressFormula> search_address_formula(
    const std::vector<AddressConstraint>& constraints, std::string_view selector,
    const AddressSolverOptions& options);

}  // namespace mkpro::core
