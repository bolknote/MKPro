// Address solver -- research/diagnostic tool for "address synthesis".
//
// Purpose: given a dispatch table {input -> target address}, search a library of
// cheap MK-61 unary functions (id/neg/int/frac/abs/sign/10^x/lg/ln/x^2/1/x/sqrt,
// the sexagesimal conversions, and the angle-mode trig incl. Ftg=0x1E) plus an
// affine (scale, offset) so that resolve(op(scale*input + b)) hits every target.
// A Lagrange polynomial fallback solves arbitrary non-uniform handler layouts.
// The SEARCH uses a pure resolver (the indirect-address rule derived from a
// faithful keyed-entry ROM sweep); every candidate is then VALIDATED end-to-end
// through the emulator (the real ROM computes both the function value and the
// indirect-address resolution), so only ROM-confirmed formulas are reported.
//
// It also includes an INVERTED "address menu" probe (see
// ttt_address_synthesis_probe): for a set of live register values it lists which
// program addresses are reachable with 0 or 1 cheap op, which is the input a
// co-design layout uses to place helper bodies at register-resolvable addresses.
//
// This is NOT part of the shipping compiler; it is the offline oracle behind the
// planned co-design indirect-flow optimization. Build with:
//   cmake -S native -B build -DMKPRO_BUILD_TOOLS=ON && cmake --build build --target mkpro_address_solver
#include "mkpro/emulator/mk61.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace mkpro::emulator;

// The fixed angle mode the searcher is compiling for.  Trig VALUES depend on the
// Р/Г/ГРД switch, so the PURE trig formulas below convert with the matching
// factor.  (The emulator is NEVER called from the search path -- it is only an
// OFFLINE oracle used to derive and validate these closed-form models.)
static std::string g_angle_mode = "deg";

// ---- pure resolver: register value -> jump target ------------------------
// Re-derived from FAITHFUL keyed-entry ROM sweep (set_register gave a wrong
// fractional rule).  Rules:
//   * v == 0            -> 0
//   * 0 < |v| < 1       -> 49  (fractional sink: ALL fractions collapse here)
//   * |v| >= 1, v > 0   -> trunc(v) mod 100
//   * |v| >= 1, v < 0   -> 9s-complement of |trunc(v)| (e.g. -1->91, -50->50)
static int resolve_pure(double V) {
  if (V == 0) return 0;
  if (std::fabs(V) < 1.0) return 49;  // pure-fractional sink
  const long long n = static_cast<long long>(std::trunc(V));
  if (V > 0) {
    long long m = n % 100;
    if (m < 0) m += 100;
    return static_cast<int>(m);
  }
  long long a = std::llabs(n);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%lld", a);
  std::string d = buf;
  if (d.size() < 8) d = std::string(8 - d.size(), '9') + d;
  return std::stoi(d.substr(d.size() - 2));
}

static double round8(double v) {
  if (v == 0 || !std::isfinite(v)) return v;
  const double mag = std::floor(std::log10(std::fabs(v)));
  const double f = std::pow(10.0, 7 - mag);
  return std::round(v * f) / f;
}

static constexpr double kPi = 3.14159265358979323846;
// angle->radians factor for the current fixed mode (deg/rad/grd).
static double angle_factor() {
  if (g_angle_mode == "rad") return 1.0;
  if (g_angle_mode == "grd") return kPi / 200.0;
  return kPi / 180.0;  // deg
}

// ---- sexagesimal (degree/time) conversions, candidate pure models ----
// °←′  : DD.MM  (minutes packed in fraction) -> decimal DD.ddd
static double from_min(double x) {
  const double d = std::trunc(x);
  return d + (x - d) * 100.0 / 60.0;
}
// °→′  : decimal DD.ddd -> DD.MM
static double to_min(double x) {
  const double d = std::trunc(x);
  return d + (x - d) * 60.0 / 100.0;
}
// °←′" : DD.MMSS -> decimal DD.ddd
static double from_sec(double x) {
  const double d = std::trunc(x);
  const double rest = (x - d) * 100.0;        // MM.SS
  const double mm = std::trunc(rest);
  const double ss = (rest - mm) * 100.0;      // SS
  return d + mm / 60.0 + ss / 3600.0;
}
// °→′" : decimal DD.ddd -> DD.MMSS
static double to_sec(double x) {
  const double d = std::trunc(x);
  const double mins = (x - d) * 60.0;         // total minutes
  const double mm = std::trunc(mins);
  const double ss = (mins - mm) * 60.0;       // seconds
  return d + mm / 100.0 + ss / 10000.0;
}

struct Op {
  std::string name;
  int opcode;
  std::function<double(double)> fn;
  bool needs_fixed_angle = false;  // true => result depends on the Р/Г/ГРД switch
};

// The full library.  Trig ops interpret/produce angles per the hardware switch,
// so they are only sound when the angle mode is contractually FIXED for the whole
// run.  The rest are angle-independent and always usable (even in games that
// require the player to move the switch mid-play).
static std::vector<Op> ops(bool angle_fixed) {
  std::vector<Op> all = {
      {"id", -1, [](double x) { return x; }, false},
      {"neg", 0x0B, [](double x) { return -x; }, false},
      {"int", 0x34, [](double x) { return std::trunc(x); }, false},
      {"frac", 0x35, [](double x) { return x - std::trunc(x); }, false},
      {"abs", 0x31, [](double x) { return std::fabs(x); }, false},
      {"sign", 0x32, [](double x) { return (x > 0) - (x < 0); }, false},
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
      // Trig: PURE closed forms, converted with the current fixed-mode factor.
      // (tan passes the offline ROM self-check; sin/cos do NOT yet -- their true
      //  rule still needs deriving, so treat their hits as unvalidated.)
      {"tan", 0x1E, [](double x) { return std::tan(x * angle_factor()); }, true},
      {"sin", 0x1C, [](double x) { return std::sin(x * angle_factor()); }, true},
      {"cos", 0x1D, [](double x) { return std::cos(x * angle_factor()); }, true},
      {"arctg", 0x1B, [](double x) { return std::atan(x) / angle_factor(); }, true},
  };
  std::vector<Op> out;
  for (Op& op : all)
    if (angle_fixed || !op.needs_fixed_angle) out.push_back(op);
  return out;
}

// ---- emulator oracle (offline validation) ----
static double parse_display(const std::string& s) {
  std::string mant;
  std::string expo;
  bool in_exp = false;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == ' ') {
      if (!mant.empty()) in_exp = true;
      continue;
    }
    if (c == ',') c = '.';
    (in_exp ? expo : mant).push_back(c);
  }
  double m = mant.empty() ? 0.0 : std::strtod(mant.c_str(), nullptr);
  int e = expo.empty() ? 0 : std::atoi(expo.c_str());
  return m * std::pow(10.0, e);
}

// Convert a double to MK-61 digit-entry keystrokes (mantissa, sign, ВП exponent),
// exactly as a real program would key a literal -- this normalizes X the way the
// microcode expects (set_register does NOT, which corrupts iterative ops).
static std::vector<int> value_keys(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.8g", v);
  std::string s = buf;
  std::string mant = s;
  std::string exp;
  if (auto pos = s.find_first_of("eE"); pos != std::string::npos) {
    mant = s.substr(0, pos);
    exp = s.substr(pos + 1);
  }
  std::vector<int> keys;
  bool mant_neg = false;
  for (char c : mant) {
    if (c == '-') { mant_neg = true; continue; }
    if (c == '+') continue;
    if (c == '.') { keys.push_back(0x0A); continue; }
    if (c >= '0' && c <= '9') keys.push_back(c - '0');
  }
  if (mant_neg) keys.push_back(0x0B);  // /-/
  if (!exp.empty()) {
    bool exp_neg = false;
    std::vector<int> ed;
    for (char c : exp) {
      if (c == '-') { exp_neg = true; continue; }
      if (c == '+') continue;
      if (c >= '0' && c <= '9') ed.push_back(c - '0');
    }
    keys.push_back(0x0C);  // ВП
    for (int d : ed) keys.push_back(d);
    if (exp_neg) keys.push_back(0x0B);
  }
  return keys;
}

static double emu_op(int opcode, double arg) {
  if (opcode < 0) return arg;
  MK61 calc(MK61Options{.extended = true, .angle_mode = g_angle_mode});
  calc.power_on();
  std::vector<int> prog = value_keys(arg);
  prog.push_back(opcode);
  prog.push_back(0x50);
  calc.load_program(prog);
  calc.press_sequence({"В/О", "С/П"});
  return parse_display(calc.read_register("x"));
}

static int emu_resolve(double value) {
  MK61 calc(MK61Options{.extended = true, .angle_mode = "deg"});
  calc.power_on();
  // Phase 1: key the value and store it into register E (normalized like runtime).
  std::vector<int> prog1 = value_keys(value);
  prog1.push_back(0x4E);  // x -> П E
  prog1.push_back(0x50);
  calc.load_program(prog1);
  calc.press_sequence({"В/О", "С/П"});
  // Phase 2: indirect call via E; targets are all halts so PC reveals the target.
  std::vector<int> prog2(105, 0x50);
  prog2[0] = 0xAE;  // К ПП e
  calc.load_program(prog2);
  calc.press_sequence({"В/О", "С/П"});
  int pc = -1;
  try { pc = std::stoi(calc.program_counter()); } catch (...) {}
  if (pc <= 0) return 0;       // target 0 (recursion / loop head)
  return pc - 1;               // С/П executed at target, PC = target+1
}

struct Constraint { double input; int target; };

// Approximate per-formula cell cost: 1 cell per op opcode, plus the literal cost
// of the scale and offset constants (digits + sign + decimal point), plus the
// final indirect jump.  Rough, but enough to compare candidate formulas.
static int literal_cost(double v) {
  if (std::fabs(v) < 1e-9) return 0;
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.4g", v);
  int c = 0;
  for (char ch : std::string(buf)) if (ch != '+') ++c;  // each char ~ 1 keystroke
  return c;
}
static int formula_cost(const Op& op, double scale, double b) {
  int c = 1;                               // the К ПП r indirect jump
  if (op.opcode >= 0) c += 1;              // the unary function opcode
  if (std::fabs(scale - 1.0) > 1e-9) c += literal_cost(scale) + 1;  // *scale
  if (std::fabs(b) > 1e-9) c += literal_cost(b) + 1;                // +b
  return c;
}

// Search op + affine (scale,b) so resolve_pure(op(scale*input + b)) == target.
// b=0 reproduces the simple op(scale*input) family; the offset adds the second
// degree of freedom that lets non-uniformly spaced handlers be solved.
static void search(const std::string& title, const std::vector<Constraint>& cs,
                   bool affine = false, bool angle_fixed = true, int max_hits = 4) {
  std::cout << "\n=== " << title << (affine ? "  [affine]" : "")
            << (angle_fixed ? "" : "  [angle NOT fixed: trig excluded]") << " ===\n";
  int hits = 0;
  int best_cost = 1 << 30;
  for (const Op& op : ops(angle_fixed)) {
    for (double scale = -180.0; scale <= 180.0; scale += 0.1) {
      if (std::fabs(scale) < 1e-9) continue;
      const double b_lo = affine ? -99.0 : 0.0;
      const double b_hi = affine ? 99.0 : 0.0;
      for (double b = b_lo; b <= b_hi + 1e-9; b += 1.0) {
        bool ok = true;
        for (const Constraint& c : cs) {
          const double v = round8(op.fn(scale * c.input + b));
          if (!std::isfinite(v) || resolve_pure(v) != c.target) { ok = false; break; }
        }
        if (!ok) continue;

        // validate end-to-end on the real ROM
        bool emu_ok = true;
        for (const Constraint& c : cs) {
          const double v = emu_op(op.opcode, scale * c.input + b);
          if (emu_resolve(v) != c.target) { emu_ok = false; break; }
        }
        const int cost = formula_cost(op, scale, b);
        best_cost = std::min(best_cost, cost);
        std::printf("  FOUND op=%-5s scale=%.4f b=%+.0f  ~cost=%d cells  emulator=%s\n",
                    op.name.c_str(), scale, b, cost, emu_ok ? "OK" : "MISMATCH");
        if (++hits >= max_hits) {
          std::printf("  (more solutions exist; cheapest so far ~%d cells)\n", best_cost);
          return;
        }
      }
    }
  }
  if (hits == 0) std::cout << "  no solution in this library/grid\n";
}

// List, per op, the first scale that solves the dispatch (pure-resolver check),
// honoring the angle-fixed flag.  Makes the trig-gating visible.
static void enumerate_ops(const std::string& title, const std::vector<Constraint>& cs,
                          bool angle_fixed) {
  std::cout << "\n=== " << title
            << (angle_fixed ? "  [angle FIXED]" : "  [angle NOT fixed]") << " ===\n";
  for (const Op& op : ops(angle_fixed)) {
    bool found = false;
    double sol = 0.0;
    for (double scale = -180.0; scale <= 180.0 && !found; scale += 0.1) {
      if (std::fabs(scale) < 1e-9) continue;
      bool ok = true;
      for (const Constraint& c : cs) {
        const double v = round8(op.fn(scale * c.input));
        if (!std::isfinite(v) || resolve_pure(v) != c.target) { ok = false; break; }
      }
      if (ok) { found = true; sol = scale; }
    }
    std::printf("  %-5s %s%s\n", op.name.c_str(),
                found ? "solves (scale=" : "no solution",
                found ? (std::to_string(sol) + ")" +
                         (op.needs_fixed_angle ? "  <-- trig (mode-gated)" : "")).c_str()
                      : "");
  }
}

// ---- exact solver: fit a polynomial through ALL points (always solvable) ----
// For k distinct inputs there is a unique degree-(k-1) polynomial p with
// p(input_i) = target_i.  Since every target is in [0,99] the resolver is the
// identity on it, so resolve_pure(p(input_i)) == target_i by construction.
// This is the "solve the equation" path for arbitrary, non-uniform handlers.
static std::vector<double> lagrange_coeffs(const std::vector<Constraint>& cs) {
  const size_t k = cs.size();
  std::vector<double> coeff(k, 0.0);          // coeff[i] = x^i coefficient
  for (size_t i = 0; i < k; ++i) {
    // basis poly L_i(x) = prod_{j!=i} (x - x_j)/(x_i - x_j), scaled by target_i
    std::vector<double> basis(k, 0.0);
    basis[0] = 1.0;
    size_t deg = 0;
    double denom = 1.0;
    for (size_t j = 0; j < k; ++j) {
      if (j == i) continue;
      // multiply basis by (x - x_j)
      for (size_t d = deg + 1; d-- > 0;) {
        basis[d + 1] += basis[d];
        basis[d] *= -cs[j].input;
      }
      ++deg;
      denom *= (cs[i].input - cs[j].input);
    }
    const double w = cs[i].target / denom;
    for (size_t d = 0; d <= deg; ++d) coeff[d] += basis[d] * w;
  }
  return coeff;
}

static double poly_eval(const std::vector<double>& c, double x) {
  double acc = 0.0;
  for (size_t i = c.size(); i-- > 0;) acc = acc * x + c[i];  // Horner
  return acc;
}

static void solve_poly(const std::string& title, const std::vector<Constraint>& cs) {
  std::cout << "\n=== " << title << "  [polynomial fit] ===\n";
  const std::vector<double> c = lagrange_coeffs(cs);
  std::printf("  degree %zu polynomial, coeffs (x^0..):", c.size() - 1);
  int cost = 1;  // indirect jump
  for (size_t i = 0; i < c.size(); ++i) {
    const double v = round8(c[i]);
    std::printf(" %.4g", v);
    cost += literal_cost(v) + (i == 0 ? 0 : 2);  // const + (mul,add) per Horner step
  }
  std::printf("   ~cost=%d cells\n", cost);
  bool emu_ok = true;
  for (const Constraint& cc : cs) {
    const double v = round8(poly_eval(c, cc.input));
    const long long iv = std::llround(v);
    const int pure = resolve_pure(static_cast<double>(iv));
    const int emu = emu_resolve(static_cast<double>(iv));
    std::printf("    input=%g -> p=%lld  pure=%d emu=%d  want=%d %s\n",
                cc.input, iv, pure, emu, cc.target,
                (pure == cc.target && emu == cc.target) ? "OK" : "MISMATCH");
    if (pure != cc.target || emu != cc.target) emu_ok = false;
  }
  std::printf("  %s\n", emu_ok ? "ALL OK (equation solved + ROM-validated)" : "MISMATCH");
}

// Validate each pure model against the real ROM over a value sweep.  Only models
// that agree with the emulator may be trusted in the fast pure search.
static void self_check() {
  std::cout << "\n=== model self-check (pure model vs ROM, deg mode) ===\n";
  const std::vector<double> xs = {-12.9, -3.7, -2.0, -1.0, -0.5, -0.155, 0.0,
                                  0.155, 0.5,  1.0,  2.3,  7.0,  12.9,   88.0};
  for (const Op& op : ops(/*angle_fixed=*/true)) {
    if (op.opcode < 0) continue;  // id: trivial
    int agree = 0, checked = 0;
    std::string first_bad;
    for (double x : xs) {
      const double m = round8(op.fn(x));
      if (!std::isfinite(m)) continue;  // model undefined here -> skip
      const double e = emu_op(op.opcode, x);
      ++checked;
      if (std::fabs(m - e) <= 1e-6 * std::max(1.0, std::fabs(e))) {
        ++agree;
      } else if (first_bad.empty()) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "x=%g model=%g rom=%g", x, m, e);
        first_bad = buf;
      }
    }
    std::printf("  %-5s %2d/%2d agree%s%s\n", op.name.c_str(), agree, checked,
                first_bad.empty() ? "" : "   first mismatch: ",
                first_bad.c_str());
  }
}

// Validate the pure address resolver against the real ROM (keyed entry).
static void resolve_self_check() {
  std::cout << "\n=== resolve_pure vs ROM (keyed entry) ===\n";
  const std::vector<double> vs = {0.0,  0.1,  -0.1, 0.155, -0.155, 0.9, -0.9,
                                  1.0,  -1.0, 5.0,  -5.0,  7.0,    -7.0, 12.0,
                                  -12.0, 50.0, -50.0, 90.0, -90.0, 1.5, -1.5,
                                  12.9, -12.9, 53.0, -53.0};
  int agree = 0;
  for (double v : vs) {
    const int p = resolve_pure(v), e = emu_resolve(v);
    if (p == e) ++agree;
    else std::printf("  MISMATCH v=%g pure=%d rom=%d\n", v, p, e);
  }
  std::printf("  %d/%zu agree\n", agree, vs.size());
}

// Single-register address synthesis probe for tic-tac-toe-4x4.
// For each AVAILABLE register value (data the program already holds) and each
// cheap unary op, compute resolve(op(value)) and report which helper/branch
// TARGET addresses it hits.  A 1-op hit means that register can serve as the
// indirect-call selector for that target at the cost of ONE extra cell (the op),
// reusing existing data -- the reference's Ftg(-Ra)->Re trick, generalized.
static void ttt_address_synthesis_probe() {
  std::cout << "\n=== ttt-4x4 single-register address synthesis (deg mode) ===\n";
  struct Reg { std::string name; double value; };
  const std::vector<Reg> regs = {
      {"Ra=88888834", 88888834.0}, {"Rb=0.41200076", 0.41200076},
      {"Rc=34.226...", 34.22600029}, {"R4..7=44444.4", 44444.4},
      {"x=1", 1.0}, {"x=2", 2.0}, {"x=3", 3.0}, {"x=4", 4.0},
  };
  // INVERTED view: the MENU of program addresses (0..104) reachable from each
  // register with 0 or 1 cheap op, ROM-validated.  The co-design layout can then
  // place a helper body at any reachable address and call it via that register
  // (cost: 0 cells if op==id, else 1 cell for the op) instead of a 2-cell direct.
  std::map<int, std::vector<std::string>> menu;  // address -> producers
  for (const Reg& r : regs) {
    for (const Op& op : ops(/*angle_fixed=*/true)) {
      const double v = round8(op.fn(r.value));
      if (!std::isfinite(v)) continue;
      const int pa = resolve_pure(v);
      if (pa < 0 || pa > 104) continue;
      const int ea = emu_resolve(v);
      if (pa != ea) continue;  // only ROM-validated entries
      char buf[64];
      std::snprintf(buf, sizeof buf, "%s|%s%s", r.name.c_str(), op.name.c_str(),
                    op.needs_fixed_angle ? "(fix)" : "");
      menu[pa].push_back(buf);
    }
  }
  for (const auto& [addr, producers] : menu) {
    std::printf("  addr %3d <-", addr);
    for (const std::string& p : producers) std::printf("  %s", p.c_str());
    std::printf("\n");
  }
  std::cout << "  (these are the addresses a co-design layout can target for free/1-op)\n";
}

int main() {
  self_check();
  resolve_self_check();
  ttt_address_synthesis_probe();
  // ---- FAITHFUL dispatch tests (rules confirmed by keyed-entry ROM sweep) ----

  // A) sign dispatch on an integer flag: +n -> n, -n -> 90+n.  Pure 'id', no trig.
  search("A) sign flag +-1 -> handlers {1, 91}",
         {{+1.0, 1}, {-1.0, 91}});

  // B) sign flag with handlers placed at 5 / 95 (chosen layout).
  search("B) sign flag +-5 -> handlers {5, 95}",
         {{+5.0, 5}, {-5.0, 95}});

  // C) k=4 match on index 1..4 -> handlers laid contiguously at 1..4 ('id').
  search("C) index 1..4 -> {1,2,3,4} (contiguous handlers)",
         {{1.0, 1}, {2.0, 2}, {3.0, 3}, {4.0, 4}});

  // D) k=4 match with uniform stride-11 layout -> one multiply ('id', scale 11).
  search("D) index 1..4 -> {11,22,33,44} (uniform stride)",
         {{1.0, 11}, {2.0, 22}, {3.0, 33}, {4.0, 44}});

  // E) k=4 NON-uniform layout: scale-only fails, affine may, polynomial always.
  search("E) index 1..4 -> {7,23,40,88} (non-uniform, scale)",
         {{1.0, 7}, {2.0, 23}, {3.0, 40}, {4.0, 88}}, /*affine=*/false);
  search("E) index 1..4 -> {7,23,40,88} (non-uniform, affine)",
         {{1.0, 7}, {2.0, 23}, {3.0, 40}, {4.0, 88}}, /*affine=*/true);
  solve_poly("E) index 1..4 -> {7,23,40,88} (non-uniform)",
             {{1.0, 7}, {2.0, 23}, {3.0, 40}, {4.0, 88}});

  // F) the fractional sink: ANY |v|<1 collapses to address 49, so fractional
  //    values cannot separate branches -- dispatch must stay on integers.
  std::cout << "\n=== F) fractional sink demo ===\n";
  std::printf("  resolve_pure(0.10)=%d  resolve_pure(0.90)=%d  resolve_pure(-0.30)=%d"
              "  -> all collapse, no dispatch possible\n",
              resolve_pure(0.10), resolve_pure(0.90), resolve_pure(-0.30));

  // G) angle-switch gating on a faithful integer sign dispatch.
  enumerate_ops("G) sign flag +-1 -> {1,91}", {{+1.0, 1}, {-1.0, 91}}, /*fixed=*/true);
  enumerate_ops("G) sign flag +-1 -> {1,91}", {{+1.0, 1}, {-1.0, 91}}, /*fixed=*/false);
  return 0;
}
