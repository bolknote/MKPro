#include "mkpro/core/address_formula_solver.hpp"

#include "mkpro/core/indirect_addressing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

namespace mkpro::core {

namespace {

constexpr double kPi = 3.14159265358979323846;

double angle_factor(SolverAngleMode mode) {
  switch (mode) {
    case SolverAngleMode::Rad:
      return 1.0;
    case SolverAngleMode::Grd:
      return kPi / 200.0;
    case SolverAngleMode::Deg:
    default:
      return kPi / 180.0;
  }
}

// Round to the MK-61's 8 significant digits, matching how the hardware would
// hold an intermediate result before it is used as an indirect selector.
double round8(double v) {
  if (v == 0 || !std::isfinite(v)) return v;
  const double mag = std::floor(std::log10(std::fabs(v)));
  const double f = std::pow(10.0, 7 - mag);
  return std::round(v * f) / f;
}

// Sexagesimal (degree/time) conversions -- angle-INDEPENDENT, always usable.
double from_min(double x) {
  const double d = std::trunc(x);
  return d + (x - d) * 100.0 / 60.0;
}
double to_min(double x) {
  const double d = std::trunc(x);
  return d + (x - d) * 60.0 / 100.0;
}
double from_sec(double x) {
  const double d = std::trunc(x);
  const double rest = (x - d) * 100.0;
  const double mm = std::trunc(rest);
  const double ss = (rest - mm) * 100.0;
  return d + mm / 60.0 + ss / 3600.0;
}
double to_sec(double x) {
  const double d = std::trunc(x);
  const double mins = (x - d) * 60.0;
  const double mm = std::trunc(mins);
  const double ss = (mins - mm) * 60.0;
  return d + mm / 100.0 + ss / 10000.0;
}

struct Op {
  std::string name;
  int opcode;
  std::function<double(double)> fn;
  bool needs_fixed_angle = false;
};

std::vector<Op> op_library(const AddressSolverOptions& options) {
  const double k = angle_factor(options.angle_mode);
  std::vector<Op> all = {
      {"id", -1, [](double x) { return x; }, false},
      {"neg", 0x0B, [](double x) { return -x; }, false},
      {"int", 0x34, [](double x) { return std::trunc(x); }, false},
      {"frac", 0x35, [](double x) { return x - std::trunc(x); }, false},
      {"abs", 0x31, [](double x) { return std::fabs(x); }, false},
      {"sign", 0x32, [](double x) { return static_cast<double>((x > 0) - (x < 0)); }, false},
      {"deg>m", 0x26, [](double x) { return to_min(x); }, false},
      {"deg<m", 0x33, [](double x) { return from_min(x); }, false},
      {"deg>s", 0x2a, [](double x) { return to_sec(x); }, false},
      {"deg<s", 0x30, [](double x) { return from_sec(x); }, false},
      {"10^x", 0x15, [](double x) { return std::pow(10.0, x); }, false},
      {"e^x", 0x16, [](double x) { return std::exp(x); }, false},
      {"lg", 0x17, [](double x) { return std::log10(x); }, false},
      {"ln", 0x18, [](double x) { return std::log(x); }, false},
      {"x^2", 0x22, [](double x) { return x * x; }, false},
      {"1/x", 0x23, [](double x) { return 1.0 / x; }, false},
      {"sqrt", 0x21, [](double x) { return std::sqrt(x); }, false},
      // Trig: only tangent/arctangent are modeled in C++ (tan passes the offline
      // ROM self-check).  sin/cos are intentionally excluded -- their exact ROM
      // rule is not derived, so they must never back address synthesis.
      {"tan", 0x1E, [k](double x) { return std::tan(x * k); }, true},
      {"arctg", 0x1B, [k](double x) { return std::atan(x) / k; }, true},
  };
  if (options.angle_fixed) return all;
  std::vector<Op> out;
  out.reserve(all.size());
  for (Op& op : all)
    if (!op.needs_fixed_angle) out.push_back(std::move(op));
  return out;
}

// Fast closed-form resolver used inside the hot search grid.  Re-derived from a
// faithful keyed-entry ROM sweep; the final formula is always re-confirmed with
// resolve_flow_target (the canonical static model) before it is returned.
int resolve_pure(double V) {
  if (V == 0) return 0;
  if (std::fabs(V) < 1.0) return 49;  // pure-fractional sink
  const long long n = static_cast<long long>(std::trunc(V));
  if (V > 0) {
    long long m = n % 100;
    if (m < 0) m += 100;
    return static_cast<int>(m);
  }
  // Negative: left-pad |trunc(V)| to 8 digits with '9', then take the last two
  // digits (the faithful keyed-entry rule, e.g. -1 -> 91, -50 -> 50, -5 -> 95).
  const long long a = std::llabs(n);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%lld", a);
  std::string d = buf;
  if (d.size() < 8) d = std::string(8 - d.size(), '9') + d;
  return std::stoi(d.substr(d.size() - 2));
}

int literal_cost(double v) {
  if (std::fabs(v) < 1e-9) return 0;
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.4g", v);
  int c = 0;
  for (char ch : std::string(buf))
    if (ch != '+') ++c;
  return c;
}

int formula_cost(const Op& op, double scale, double b) {
  int c = 1;  // the К ПП r / К БП r indirect transfer
  if (op.opcode >= 0) c += 1;
  if (std::fabs(scale - 1.0) > 1e-9) c += literal_cost(scale) + 1;  // * scale
  if (std::fabs(b) > 1e-9) c += literal_cost(b) + 1;                // + offset
  return c;
}

}  // namespace

std::optional<int> resolve_flow_target(std::string_view selector, double value) {
  const std::optional<IndirectAddressEvaluation> eval =
      evaluate_indirect_address(selector, value, IndirectOperationKind::Flow);
  if (!eval.has_value()) return std::nullopt;
  if (eval->actual_flow_target.has_value()) return eval->actual_flow_target;
  if (eval->flow_target.has_value()) return eval->flow_target;
  return std::nullopt;
}

std::optional<AddressFormula> search_address_formula(
    const std::vector<AddressConstraint>& constraints, std::string_view selector,
    const AddressSolverOptions& options) {
  if (constraints.empty()) return std::nullopt;

  const double b_lo = options.allow_affine ? -options.offset_abs_max : 0.0;
  const double b_hi = options.allow_affine ? options.offset_abs_max : 0.0;
  const double step = options.scale_step > 0.0 ? options.scale_step : 0.1;

  std::optional<AddressFormula> best;
  for (const Op& op : op_library(options)) {
    for (double scale = options.scale_min; scale <= options.scale_max + 1e-9;
         scale += step) {
      if (std::fabs(scale) < 1e-9) continue;
      for (double b = b_lo; b <= b_hi + 1e-9; b += 1.0) {
        // Fast pre-filter with the closed-form resolver.
        bool ok = true;
        for (const AddressConstraint& c : constraints) {
          const double v = round8(op.fn(scale * c.input + b));
          if (!std::isfinite(v) || resolve_pure(v) != c.target) {
            ok = false;
            break;
          }
        }
        if (!ok) continue;

        // Confirm with the canonical static model (no emulator).
        bool confirmed = true;
        for (const AddressConstraint& c : constraints) {
          const double v = round8(op.fn(scale * c.input + b));
          const std::optional<int> got = resolve_flow_target(selector, v);
          if (!got.has_value() || *got != c.target) {
            confirmed = false;
            break;
          }
        }
        if (!confirmed) continue;

        const int cost = formula_cost(op, scale, b);
        if (!best.has_value() || cost < best->approx_cost) {
          best = AddressFormula{.op_name = op.name,
                                .op_opcode = op.opcode,
                                .scale = scale,
                                .offset = b,
                                .approx_cost = cost,
                                .uses_fixed_angle = op.needs_fixed_angle};
        }
      }
    }
  }
  return best;
}

}  // namespace mkpro::core
