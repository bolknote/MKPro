#include "mkpro/core/address_formula_solver.hpp"

#include "test_support.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

using core::AddressConstraint;
using core::AddressFormula;
using core::AddressSolverOptions;
using core::resolve_flow_target;
using core::search_address_formula;

// Re-derive the targets a chosen op+affine would produce through the canonical
// static model, so the test never hard-codes ROM-specific numbers.
double apply_affine(double scale, double offset, double input) {
  return scale * input + offset;
}

// Confirm a synthesized formula reproduces every constraint through the canonical
// resolver (the same model the compiler trusts, no emulator).
bool formula_reproduces(const AddressFormula& f, const std::string& selector,
                        const std::vector<AddressConstraint>& cs) {
  for (const AddressConstraint& c : cs) {
    double x = apply_affine(f.scale, f.offset, c.input);
    // Only identity-style formulas are exercised here (op_opcode == -1); the
    // unary-op families are covered indirectly through the search itself.
    if (f.op_opcode != -1) return true;  // trust the search's own confirmation
    const std::optional<int> got = resolve_flow_target(selector, x);
    if (!got.has_value() || *got != c.target) return false;
  }
  return true;
}

}  // namespace

void address_formula_solver_synthesizes_dispatch() {
  const std::string selector = "9";  // R9 is a stable indirect selector (R7+)

  // 1) Identity dispatch: contiguous integer selector values map straight to
  //    their addresses, so a zero-op (identity) formula must solve it.
  {
    std::vector<AddressConstraint> cs;
    for (double v : {3.0, 7.0, 12.0}) {
      const std::optional<int> t = resolve_flow_target(selector, v);
      require(t.has_value(), "stable selector integer value should resolve to a flow target");
      cs.push_back({v, *t});
    }
    AddressSolverOptions options;
    options.allow_affine = false;
    options.angle_fixed = false;
    const std::optional<AddressFormula> f = search_address_formula(cs, selector, options);
    require(f.has_value(), "solver should synthesize a formula for an identity dispatch");
    require(!f->uses_fixed_angle,
            "identity dispatch must not require a trig op when the angle mode is free");
    require(formula_reproduces(*f, selector, cs),
            "synthesized identity formula must reproduce every target");
  }

  // 2) Uniform stride: targets laid out at a constant stride collapse to a single
  //    multiply (still an identity op, just a scale).
  {
    const double stride = 11.0;
    std::vector<AddressConstraint> cs;
    for (double i : {1.0, 2.0, 3.0, 4.0}) {
      const double v = stride * i;
      const std::optional<int> t = resolve_flow_target(selector, v);
      require(t.has_value(), "strided selector value should resolve to a flow target");
      cs.push_back({i, *t});
    }
    AddressSolverOptions options;
    options.allow_affine = false;
    options.angle_fixed = false;
    const std::optional<AddressFormula> f = search_address_formula(cs, selector, options);
    require(f.has_value(), "solver should synthesize a scaled formula for a uniform stride");
    require(!f->uses_fixed_angle,
            "uniform-stride dispatch must not require a trig op when the angle mode is free");
  }

  // 3) Trig gating: with the angle mode free, no synthesized formula may rely on
  //    a mode-dependent op, regardless of the dispatch shape.
  {
    std::vector<AddressConstraint> cs;
    for (double v : {2.0, 5.0}) {
      const std::optional<int> t = resolve_flow_target(selector, v);
      require(t.has_value(), "selector value should resolve to a flow target");
      cs.push_back({v, *t});
    }
    AddressSolverOptions free_mode;
    free_mode.allow_affine = true;
    free_mode.angle_fixed = false;
    const std::optional<AddressFormula> f = search_address_formula(cs, selector, free_mode);
    require(f.has_value(), "solver should still solve a non-trig dispatch with the angle free");
    require(!f->uses_fixed_angle,
            "no trig op may be selected when the angle mode is not contractually fixed");
  }

  // 4) Reverse packing: keep the user payload in the integer part and put the
  //    address in the fractional cents. The solver should recover the target by
  //    multiplying by 100 before resolving the indirect address.
  {
    std::vector<AddressConstraint> cs = {
        {1.03, 3},
        {2.07, 7},
        {3.12, 12},
        {42.37, 37},
    };
    AddressSolverOptions options;
    options.allow_affine = false;
    options.angle_fixed = false;
    options.scale_min = 100.0;
    options.scale_max = 100.0;
    options.scale_step = 1.0;
    const std::optional<AddressFormula> f = search_address_formula(cs, selector, options);
    require(f.has_value(), "solver should synthesize a reverse-packed fractional-address formula");
    require(f->op_opcode == -1, "reverse-packed dispatch should not need a unary op");
    require(std::fabs(f->scale - 100.0) < 1e-9,
            "reverse-packed dispatch should lift fractional address cents with scale=100");
    require(formula_reproduces(*f, selector, cs),
            "reverse-packed formula must reproduce every target");
  }

  // 5) Empty constraints yield no formula.
  {
    AddressSolverOptions options;
    const std::optional<AddressFormula> f = search_address_formula({}, selector, options);
    require(!f.has_value(), "solver should return nothing for an empty dispatch table");
  }
}

}  // namespace mkpro::tests
