#include "mkpro/core/interprocedural_value_propagation.hpp"

#include "mkpro/core/parser.hpp"
#include "mkpro/core/rule_cfg.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core {

namespace {

constexpr int kMaxRounds = 1000;
constexpr std::size_t kMaxTerms = 8;
constexpr long long kMaxMagnitude = 1'000'000;

struct Rat {
  long long n = 0;
  long long d = 1;
  bool valid = true;
};

bool operator==(const Rat& left, const Rat& right) {
  return left.n == right.n && left.d == right.d && left.valid == right.valid;
}

struct LinForm {
  Rat constant;
  std::map<std::string, Rat> terms;
};

bool operator==(const LinForm& left, const LinForm& right) {
  return left.constant == right.constant && left.terms == right.terms;
}

using State = std::map<std::string, LinForm>;

long long gcd_ll(long long a, long long b) {
  a = std::llabs(a);
  b = std::llabs(b);
  while (b != 0) {
    const long long next = a % b;
    a = b;
    b = next;
  }
  return a == 0 ? 1 : a;
}

std::optional<long long> int128_to_long_long(__int128 value) {
  if (value < static_cast<__int128>(std::numeric_limits<long long>::min()) ||
      value > static_cast<__int128>(std::numeric_limits<long long>::max())) {
    return std::nullopt;
  }
  return static_cast<long long>(value);
}

Rat invalid_rat() {
  return Rat{.valid = false};
}

Rat rat(long long n, long long d = 1) {
  if (d == 0)
    return invalid_rat();
  if (d < 0) {
    n = -n;
    d = -d;
  }
  const long long g = gcd_ll(n, d);
  return Rat{.n = n / g, .d = d / g};
}

Rat rat_from_int128(__int128 n, __int128 d) {
  const std::optional<long long> numer = int128_to_long_long(n);
  const std::optional<long long> denom = int128_to_long_long(d);
  if (!numer.has_value() || !denom.has_value())
    return invalid_rat();
  return rat(*numer, *denom);
}

bool rat_valid(const Rat& value) {
  return value.valid && value.d != 0 && std::llabs(value.n) <= kMaxMagnitude &&
         std::llabs(value.d) <= kMaxMagnitude;
}

Rat rat_add(const Rat& left, const Rat& right) {
  if (!left.valid || !right.valid)
    return invalid_rat();
  return rat_from_int128(static_cast<__int128>(left.n) * right.d +
                             static_cast<__int128>(right.n) * left.d,
                         static_cast<__int128>(left.d) * right.d);
}

Rat rat_mul(const Rat& left, const Rat& right) {
  if (!left.valid || !right.valid)
    return invalid_rat();
  return rat_from_int128(static_cast<__int128>(left.n) * right.n,
                         static_cast<__int128>(left.d) * right.d);
}

bool rat_equal(const Rat& left, const Rat& right) {
  return left.valid == right.valid && left.n == right.n && left.d == right.d;
}

LinForm const_form(const Rat& value) {
  return LinForm{.constant = value};
}

LinForm single_var(const std::string& name) {
  return LinForm{
      .constant = rat(0),
      .terms = {{name, rat(1)}},
  };
}

bool form_valid(const LinForm& form) {
  if (!rat_valid(form.constant) || form.terms.size() > kMaxTerms)
    return false;
  return std::all_of(form.terms.begin(), form.terms.end(),
                     [](const auto& entry) { return rat_valid(entry.second); });
}

LinForm add_forms(const LinForm& left, const LinForm& right) {
  std::map<std::string, Rat> terms = left.terms;
  for (const auto& [name, coeff] : right.terms) {
    const auto existing = terms.find(name);
    const Rat next = rat_add(existing == terms.end() ? rat(0) : existing->second, coeff);
    if (!next.valid || next.n == 0)
      terms.erase(name);
    else
      terms[name] = next;
  }
  return LinForm{.constant = rat_add(left.constant, right.constant), .terms = std::move(terms)};
}

LinForm scale_form(const LinForm& form, const Rat& factor) {
  std::map<std::string, Rat> terms;
  for (const auto& [name, coeff] : form.terms) {
    const Rat next = rat_mul(coeff, factor);
    if (next.valid && next.n != 0)
      terms[name] = next;
  }
  return LinForm{.constant = rat_mul(form.constant, factor), .terms = std::move(terms)};
}

bool forms_equal(const LinForm& left, const LinForm& right) {
  return rat_equal(left.constant, right.constant) && left.terms == right.terms;
}

std::string trim_ascii(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text.at(begin))))
    ++begin;
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text.at(end - 1U))))
    --end;
  return text.substr(begin, end - begin);
}

std::optional<Rat> decimal_to_rat(const std::string& raw) {
  std::string text = trim_ascii(raw);
  if (text.empty())
    return std::nullopt;
  bool negative = false;
  if (text.front() == '+' || text.front() == '-') {
    negative = text.front() == '-';
    text.erase(text.begin());
  }
  if (text.empty())
    return std::nullopt;

  std::string integer;
  std::string fractional;
  bool seen_dot = false;
  for (const char ch : text) {
    if (ch == '.') {
      if (seen_dot)
        return std::nullopt;
      seen_dot = true;
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(ch)))
      return std::nullopt;
    if (seen_dot)
      fractional.push_back(ch);
    else
      integer.push_back(ch);
  }
  if (integer.empty() && fractional.empty())
    return std::nullopt;

  __int128 numer = 0;
  for (const char ch : integer)
    numer = numer * 10 + static_cast<int>(ch - '0');
  __int128 denom = 1;
  for (const char ch : fractional) {
    numer = numer * 10 + static_cast<int>(ch - '0');
    denom *= 10;
  }
  if (negative)
    numer = -numer;
  Rat result = rat_from_int128(numer, denom);
  if (!rat_valid(result))
    return std::nullopt;
  return result;
}

LinForm value_of(const State& state, const std::string& name) {
  const auto it = state.find(name);
  return it == state.end() ? single_var(name) : it->second;
}

std::optional<LinForm> affine_form(const Expression& expression, const State& state) {
  if (expression.kind == "string" || expression.kind == "indexed" || expression.kind == "call")
    return std::nullopt;
  if (expression.kind == "number") {
    const std::optional<Rat> value = decimal_to_rat(expression.raw);
    return value.has_value() ? std::optional<LinForm>{const_form(*value)} : std::nullopt;
  }
  if (expression.kind == "identifier")
    return value_of(state, expression.name);
  if (expression.kind == "unary") {
    if (expression.expr == nullptr)
      return std::nullopt;
    const std::optional<LinForm> inner = affine_form(*expression.expr, state);
    return inner.has_value() ? std::optional<LinForm>{scale_form(*inner, rat(-1))} : std::nullopt;
  }
  if (expression.kind != "binary" || expression.left == nullptr || expression.right == nullptr)
    return std::nullopt;

  const std::optional<LinForm> left = affine_form(*expression.left, state);
  const std::optional<LinForm> right = affine_form(*expression.right, state);
  if (!left.has_value() || !right.has_value())
    return std::nullopt;

  std::optional<LinForm> result;
  if (expression.op == "+") {
    result = add_forms(*left, *right);
  } else if (expression.op == "-") {
    result = add_forms(*left, scale_form(*right, rat(-1)));
  } else if (expression.op == "*") {
    if (left->terms.empty())
      result = scale_form(*right, left->constant);
    else if (right->terms.empty())
      result = scale_form(*left, right->constant);
  } else if (expression.op == "/" && right->terms.empty() && right->constant.n != 0) {
    result = scale_form(*left, rat(right->constant.d, right->constant.n));
  }

  return result.has_value() && form_valid(*result) ? result : std::nullopt;
}

State apply_def(const State& state, const std::string& name,
                const std::optional<LinForm>& form) {
  State next;
  for (const auto& [key, value] : state) {
    if (key == name || value.terms.contains(name))
      continue;
    next[key] = value;
  }
  if (form.has_value() && !form->terms.contains(name) && form_valid(*form))
    next[name] = *form;
  return next;
}

std::optional<Expression> parse_expression_safe(const std::string& text, int line) {
  try {
    return parse_expression(text, line);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<std::string> assignment_target_name(const V2Statement& statement) {
  if (!statement.target.has_value())
    return std::nullopt;
  const std::optional<Expression> target = parse_expression_safe(*statement.target, statement.line);
  if (!target.has_value() || target->kind != "identifier")
    return std::nullopt;
  return target->name;
}

std::optional<Expression> effective_assignment_expression(const V2Statement& statement) {
  if (!statement.expr.has_value())
    return std::nullopt;
  const std::optional<Expression> expr = parse_expression_safe(*statement.expr, statement.line);
  if (!expr.has_value())
    return std::nullopt;
  if (statement.kind == "v2_assign")
    return expr;
  if (statement.kind != "v2_update" || !statement.op.has_value())
    return std::nullopt;
  const std::optional<std::string> target_name = assignment_target_name(statement);
  if (!target_name.has_value())
    return std::nullopt;
  std::string op;
  if (*statement.op == "+=")
    op = "+";
  else if (*statement.op == "-=")
    op = "-";
  else
    return std::nullopt;
  Expression target;
  target.kind = "identifier";
  target.name = *target_name;
  Expression result;
  result.kind = "binary";
  result.op = std::move(op);
  result.left = std::make_shared<Expression>(std::move(target));
  result.right = std::make_shared<Expression>(*expr);
  return result;
}

State transfer(const State& state, const RuleCfgNode& node) {
  if (node.barrier)
    return {};
  V2Statement* assign = node.assign;
  if (assign != nullptr) {
    const std::optional<std::string> target_name = assignment_target_name(*assign);
    const std::optional<Expression> expr = effective_assignment_expression(*assign);
    const std::optional<LinForm> form =
        expr.has_value() && expression_is_call_free(*expr) ? affine_form(*expr, state) : std::nullopt;
    return target_name.has_value() ? apply_def(state, *target_name, form) : state;
  }
  State next = state;
  for (const std::string& def : node.defs)
    next = apply_def(next, def, std::nullopt);
  return next;
}

bool holds_in(const State& state, const std::string& name, const LinForm& form) {
  const LinForm lhs = value_of(state, name);
  LinForm rhs = const_form(form.constant);
  for (const auto& [term, coeff] : form.terms)
    rhs = add_forms(rhs, scale_form(value_of(state, term), coeff));
  return forms_equal(lhs, rhs);
}

std::optional<State> meet(const std::vector<std::optional<State>>& states) {
  std::vector<State> reached;
  for (const std::optional<State>& state : states) {
    if (state.has_value())
      reached.push_back(*state);
  }
  if (reached.empty())
    return std::nullopt;

  State result;
  for (const State& state : reached) {
    for (const auto& [name, form] : state) {
      if (result.contains(name))
        continue;
      if (std::all_of(reached.begin(), reached.end(),
                      [&](const State& other) { return holds_in(other, name, form); })) {
        result[name] = form;
      }
    }
  }
  return result;
}

bool states_equal(const std::optional<State>& left, const std::optional<State>& right) {
  if (!left.has_value() || !right.has_value())
    return left.has_value() == right.has_value();
  return *left == *right;
}

std::vector<std::vector<int>> build_predecessors(const RuleCfg& cfg) {
  std::vector<std::vector<int>> predecessors(cfg.nodes.size());
  for (const RuleCfgNode& node : cfg.nodes) {
    for (const int successor : node.succ)
      predecessors.at(static_cast<std::size_t>(successor)).push_back(node.id);
  }
  return predecessors;
}

int expression_cost(const Expression& expression) {
  if (expression.kind == "number" || expression.kind == "string" || expression.kind == "identifier")
    return 1;
  if (expression.kind == "indexed")
    return 2 + (expression.index == nullptr ? 0 : expression_cost(*expression.index));
  if (expression.kind == "unary")
    return 1 + (expression.expr == nullptr ? 0 : expression_cost(*expression.expr));
  if (expression.kind == "binary") {
    return 1 + (expression.left == nullptr ? 0 : expression_cost(*expression.left)) +
           (expression.right == nullptr ? 0 : expression_cost(*expression.right));
  }
  if (expression.kind == "call")
    return 100;
  return 100;
}

} // namespace

int propagate_values_interprocedurally(V2Program& program,
                                       std::vector<OptimizationReport>& optimizations) {
  const std::set<std::string> fields = program_state_fields(program);
  if (fields.empty())
    return 0;

  const RuleCfg cfg = build_rule_cfg(program);
  const std::vector<std::vector<int>> predecessors = build_predecessors(cfg);
  std::vector<std::optional<State>> out(cfg.nodes.size());

  bool changed = true;
  int rounds = 0;
  while (changed && rounds < kMaxRounds) {
    changed = false;
    ++rounds;
    for (std::size_t i = 0; i < cfg.nodes.size(); ++i) {
      std::vector<std::optional<State>> incoming;
      for (const int predecessor : predecessors.at(i))
        incoming.push_back(out.at(static_cast<std::size_t>(predecessor)));
      if (static_cast<int>(i) == cfg.entry_node)
        incoming.push_back(State{});
      const std::optional<State> in_state = meet(incoming);
      const std::optional<State> new_out =
          in_state.has_value() ? std::optional<State>{transfer(*in_state, cfg.nodes.at(i))}
                               : std::nullopt;
      if (!states_equal(new_out, out.at(i))) {
        out.at(i) = new_out;
        changed = true;
      }
    }
  }
  if (rounds >= kMaxRounds)
    return 0;

  int rewrites = 0;
  for (const RuleCfgNode& node : cfg.nodes) {
    V2Statement* assign = node.assign;
    if (assign == nullptr)
      continue;
    const std::optional<std::string> target_name = assignment_target_name(*assign);
    const std::optional<Expression> expr = effective_assignment_expression(*assign);
    if (!target_name.has_value() || !expr.has_value())
      continue;
    if (!expression_is_call_free(*expr) || (expr->kind != "binary" && expr->kind != "unary"))
      continue;

    std::vector<std::optional<State>> incoming;
    for (const int predecessor : predecessors.at(static_cast<std::size_t>(node.id)))
      incoming.push_back(out.at(static_cast<std::size_t>(predecessor)));
    if (node.id == cfg.entry_node)
      incoming.push_back(State{});
    const std::optional<State> in_state = meet(incoming);
    if (!in_state.has_value())
      continue;
    const std::optional<LinForm> target_form = affine_form(*expr, *in_state);
    if (!target_form.has_value())
      continue;

    std::optional<std::string> replacement;
    for (const std::string& candidate : fields) {
      if (candidate == *target_name)
        continue;
      if (forms_equal(value_of(*in_state, candidate), *target_form) && form_valid(*target_form)) {
        replacement = candidate;
        break;
      }
    }
    if (!replacement.has_value() || expression_cost(*expr) <= 1)
      continue;
    assign->kind = "v2_assign";
    assign->expr = *replacement;
    assign->op.reset();
    ++rewrites;
  }

  if (rewrites == 0)
    return 0;
  optimizations.push_back(OptimizationReport{
      .name = "interprocedural-value-propagation",
      .detail = "Replaced " + std::to_string(rewrites) +
                " recomputed expression(s) with an equal live variable proved equivalent on "
                "every path.",
  });
  return rewrites;
}

} // namespace mkpro::core
