#include "mkpro/core/flow_sensitive_call_domains.hpp"

#include "mkpro/core/emit/lowering/expr.hpp"
#include "mkpro/core/int128.hpp"
#include "mkpro/core/parser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core {

namespace {

struct AbstractValue {
  ExactIntegralDomain domain;
  bool decimal_derivation_exact = false;
  bool zero_canonical_positive = false;
};

using Domains = std::map<std::string, AbstractValue>;

struct AbstractState {
  bool reachable = false;
  Domains domains;
};

struct FlowResult {
  AbstractState fallthrough;
  AbstractState returned;
};

struct SiteFact {
  bool seen = false;
  bool valid = true;
  AbstractValue value;
};

constexpr std::int64_t kExactEightDigitInteger = 99999999;

bool contains_zero(const ExactIntegralDomain& domain) {
  return domain.valid() && domain.minimum <= 0 && domain.maximum >= 0;
}

bool within_exact_eight_digit_integer_range(const ExactIntegralDomain& domain) {
  return domain.valid() && domain.minimum >= -kExactEightDigitInteger &&
         domain.maximum <= kExactEightDigitInteger;
}

AbstractValue abstract_value(ExactIntegralDomain domain, bool decimal_derivation_exact,
                             bool zero_canonical_positive) {
  if (!contains_zero(domain))
    zero_canonical_positive = true;
  return AbstractValue{domain, decimal_derivation_exact, zero_canonical_positive};
}

std::optional<std::int64_t> checked_i64(Int128 value) {
  if (value < Int128{std::numeric_limits<std::int64_t>::min()} ||
      value > Int128{std::numeric_limits<std::int64_t>::max()}) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(static_cast<long long>(value));
}

std::optional<std::int64_t> integer_literal(const Expression& expression) {
  if (expression.kind != "number")
    return std::nullopt;
  const std::string& text = expression.raw.empty() ? expression.text : expression.raw;
  std::int64_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

ExactIntegralDomain unite(const ExactIntegralDomain& left, const ExactIntegralDomain& right) {
  if (!left.valid())
    return right;
  if (!right.valid())
    return left;
  return ExactIntegralDomain{std::min(left.minimum, right.minimum),
                             std::max(left.maximum, right.maximum), true};
}

AbstractValue unite(const AbstractValue& left, const AbstractValue& right) {
  return abstract_value(unite(left.domain, right.domain),
                        left.decimal_derivation_exact && right.decimal_derivation_exact,
                        left.zero_canonical_positive && right.zero_canonical_positive);
}

std::optional<AbstractValue> arithmetic_value(const AbstractValue& left, const AbstractValue& right,
                                              const std::string& op) {
  ExactIntegralDomain domain;
  if (op == "+" || op == "-") {
    const Int128 minimum = op == "+" ? Int128{left.domain.minimum} + Int128{right.domain.minimum}
                                     : Int128{left.domain.minimum} - Int128{right.domain.maximum};
    const Int128 maximum = op == "+" ? Int128{left.domain.maximum} + Int128{right.domain.maximum}
                                     : Int128{left.domain.maximum} - Int128{right.domain.minimum};
    const auto checked_minimum = checked_i64(minimum);
    const auto checked_maximum = checked_i64(maximum);
    if (!checked_minimum.has_value() || !checked_maximum.has_value())
      return std::nullopt;
    domain = ExactIntegralDomain{*checked_minimum, *checked_maximum, true};
  } else if (op == "*") {
    const std::array<Int128, 4> products{
        Int128{left.domain.minimum} * Int128{right.domain.minimum},
        Int128{left.domain.minimum} * Int128{right.domain.maximum},
        Int128{left.domain.maximum} * Int128{right.domain.minimum},
        Int128{left.domain.maximum} * Int128{right.domain.maximum},
    };
    const auto [minimum, maximum] = std::minmax_element(products.begin(), products.end());
    const auto checked_minimum = checked_i64(*minimum);
    const auto checked_maximum = checked_i64(*maximum);
    if (!checked_minimum.has_value() || !checked_maximum.has_value())
      return std::nullopt;
    domain = ExactIntegralDomain{*checked_minimum, *checked_maximum, true};
  } else {
    return std::nullopt;
  }

  bool zero_canonical_positive = !contains_zero(domain);
  if (!zero_canonical_positive && op == "-") {
    // Emulator-pinned premise: subtracting equal strictly-positive operands
    // produces canonical +0. Other zero-producing shapes remain unknown.
    const std::int64_t equal_minimum = std::max(left.domain.minimum, right.domain.minimum);
    const std::int64_t equal_maximum = std::min(left.domain.maximum, right.domain.maximum);
    zero_canonical_positive = equal_minimum > 0 && equal_minimum <= equal_maximum;
  }
  const bool decimal_exact = left.decimal_derivation_exact && right.decimal_derivation_exact &&
                             within_exact_eight_digit_integer_range(domain);
  return abstract_value(domain, decimal_exact, zero_canonical_positive);
}

AbstractState join(const AbstractState& left, const AbstractState& right) {
  if (!left.reachable)
    return right;
  if (!right.reachable)
    return left;
  AbstractState result{.reachable = true};
  // Missing means Unknown (top), so a name remains finite only when both
  // incoming paths prove it finite.
  for (const auto& [name, domain] : left.domains) {
    const auto other = right.domains.find(name);
    if (other != right.domains.end())
      result.domains.emplace(name, unite(domain, other->second));
  }
  return result;
}

bool same_state(const AbstractState& left, const AbstractState& right) {
  if (left.reachable != right.reachable || left.domains.size() != right.domains.size())
    return false;
  if (!left.reachable)
    return true;
  auto a = left.domains.begin();
  auto b = right.domains.begin();
  for (; a != left.domains.end(); ++a, ++b) {
    if (a->first != b->first || a->second.domain.minimum != b->second.domain.minimum ||
        a->second.domain.maximum != b->second.domain.maximum ||
        a->second.domain.proved_integral != b->second.domain.proved_integral ||
        a->second.decimal_derivation_exact != b->second.decimal_derivation_exact ||
        a->second.zero_canonical_positive != b->second.zero_canonical_positive) {
      return false;
    }
  }
  return true;
}

std::string invert_comparison(std::string op) {
  if (op == "==")
    return "!=";
  if (op == "!=")
    return "==";
  if (op == "<")
    return ">=";
  if (op == "<=")
    return ">";
  if (op == ">")
    return "<=";
  if (op == ">=")
    return "<";
  return {};
}

std::string swap_comparison(std::string op) {
  if (op == "<")
    return ">";
  if (op == "<=")
    return ">=";
  if (op == ">")
    return "<";
  if (op == ">=")
    return "<=";
  return op;
}

class Analyzer {
public:
  Analyzer(const V2Program& program, std::set<std::string> targets)
      : program_(program), targets_(std::move(targets)) {
    for (const V2Rule& rule : program_.rules)
      rules_.emplace(rule.name, &rule);
    for (const V2Const& constant : program_.consts)
      scan_expression_text(constant.expr, constant.line);
    for (const V2StateField& field : program_.state) {
      if (field.initial.has_value())
        scan_expression_text(*field.initial, field.line);
    }
    scan_statements_for_expression_calls(program_.body);
    for (const V2Rule& rule : program_.rules)
      scan_statements_for_expression_calls(rule.body);
    collect_syntactic_calls(program_.body);
    for (const V2Rule& rule : program_.rules)
      collect_syntactic_calls(rule.body);
    initialize_constants();
  }

  std::map<std::string, FlowSensitiveCallDomainProof> run() {
    AbstractState initial{.reachable = true};
    for (const V2StateField& field : program_.state) {
      if (!field.initial.has_value() || field.initial_stack.has_value())
        continue;
      const std::optional<AbstractValue> domain = parse_domain(*field.initial, field.line, initial);
      if (domain.has_value())
        initial.domains[field.name] = *domain;
    }
    std::vector<std::string> call_stack;
    (void)analyze_block(program_.body, initial, call_stack);

    std::map<std::string, FlowSensitiveCallDomainProof> result;
    for (const std::string& target : targets_) {
      FlowSensitiveCallDomainProof proof;
      proof.call_sites = syntactic_calls_[target];
      proof.valid = sound_ && proof.call_sites > 0;
      int seen = 0;
      const auto target_facts = facts_.find(target);
      if (target_facts != facts_.end()) {
        for (const auto& [unused_site, fact] : target_facts->second) {
          (void)unused_site;
          if (!fact.seen)
            continue;
          ++seen;
          proof.valid = proof.valid && fact.valid && fact.value.domain.valid();
          if (!proof.domain.valid()) {
            proof.domain = fact.value.domain;
            proof.decimal_derivation_exact = fact.value.decimal_derivation_exact;
            proof.zero_canonical_positive = fact.value.zero_canonical_positive;
          } else {
            proof.domain = unite(proof.domain, fact.value.domain);
            proof.decimal_derivation_exact =
                proof.decimal_derivation_exact && fact.value.decimal_derivation_exact;
            proof.zero_canonical_positive =
                proof.zero_canonical_positive && fact.value.zero_canonical_positive;
          }
        }
      }
      if (!contains_zero(proof.domain))
        proof.zero_canonical_positive = true;
      proof.valid = proof.valid && seen == proof.call_sites && proof.domain.valid();
      result.emplace(target, proof);
    }
    return result;
  }

private:
  static constexpr int kMaximumLoopIterations = 64;
  static constexpr int kMaximumCallDepth = 64;

  const V2Program& program_;
  std::set<std::string> targets_;
  std::map<std::string, const V2Rule*> rules_;
  Domains constants_;
  std::map<std::string, int> syntactic_calls_;
  std::map<std::string, std::map<const V2Statement*, SiteFact>> facts_;
  bool sound_ = true;

  bool expression_invokes_rule(const Expression& expression) const {
    if (expression.kind == "call" && rules_.contains(expression.callee))
      return true;
    if (expression.index != nullptr && expression_invokes_rule(*expression.index))
      return true;
    if (expression.expr != nullptr && expression_invokes_rule(*expression.expr))
      return true;
    if (expression.left != nullptr && expression_invokes_rule(*expression.left))
      return true;
    if (expression.right != nullptr && expression_invokes_rule(*expression.right))
      return true;
    return std::any_of(expression.args.begin(), expression.args.end(),
                       [&](const Expression& arg) { return expression_invokes_rule(arg); });
  }

  void scan_expression_text(const std::string& text, int line) {
    try {
      if (expression_invokes_rule(parse_expression(text, line)))
        sound_ = false;
    } catch (const std::exception&) {
      // The parser owns syntax validation. A non-expression display fragment
      // is irrelevant here; any transfer that needs it will still become
      // Unknown when analyzed.
    }
  }

  void scan_statements_for_expression_calls(const std::vector<V2Statement>& statements) {
    for (const V2Statement& statement : statements) {
      if (statement.target.has_value())
        scan_expression_text(*statement.target, statement.line);
      if (statement.expr.has_value())
        scan_expression_text(*statement.expr, statement.line);
      for (const std::string& argument : statement.args)
        scan_expression_text(argument, statement.line);
      if (statement.predicate.has_value()) {
        scan_expression_text(statement.predicate->left, statement.line);
        scan_expression_text(statement.predicate->right, statement.line);
        if (!statement.predicate->collection.empty())
          scan_expression_text(statement.predicate->collection, statement.line);
        if (!statement.predicate->item.empty())
          scan_expression_text(statement.predicate->item, statement.line);
      }
      if (statement.items.has_value()) {
        for (const DisplayItem& item : *statement.items) {
          if (item.expr.has_value() && expression_invokes_rule(*item.expr))
            sound_ = false;
        }
      }
      for (const V2RawInput& input : statement.inputs)
        scan_expression_text(input.expr, input.line);
      for (const V2MatchCase& match_case : statement.cases) {
        for (const std::string& value : match_case.values)
          scan_expression_text(value, match_case.line);
        if (match_case.action != nullptr)
          scan_statements_for_expression_calls(std::vector<V2Statement>{*match_case.action});
      }
      if (statement.otherwise != nullptr)
        scan_statements_for_expression_calls(std::vector<V2Statement>{*statement.otherwise});
      scan_statements_for_expression_calls(statement.body);
      scan_statements_for_expression_calls(statement.then_body);
      scan_statements_for_expression_calls(statement.else_body);
    }
  }

  void collect_syntactic_calls(const std::vector<V2Statement>& statements) {
    for (const V2Statement& statement : statements) {
      if (statement.kind == "v2_invoke" && statement.name.has_value() &&
          targets_.contains(*statement.name)) {
        ++syntactic_calls_[*statement.name];
      }
      collect_syntactic_calls(statement.body);
      collect_syntactic_calls(statement.then_body);
      collect_syntactic_calls(statement.else_body);
      for (const V2MatchCase& match_case : statement.cases) {
        if (match_case.action != nullptr)
          collect_syntactic_calls(std::vector<V2Statement>{*match_case.action});
      }
      if (statement.otherwise != nullptr)
        collect_syntactic_calls(std::vector<V2Statement>{*statement.otherwise});
    }
  }

  void initialize_constants() {
    AbstractState state{.reachable = true};
    for (std::size_t iteration = 0; iteration <= program_.consts.size(); ++iteration) {
      bool changed = false;
      state.domains = constants_;
      for (const V2Const& constant : program_.consts) {
        if (constants_.contains(constant.name))
          continue;
        const std::optional<AbstractValue> domain =
            parse_domain(constant.expr, constant.line, state);
        if (!domain.has_value())
          continue;
        constants_[constant.name] = *domain;
        state.domains[constant.name] = *domain;
        changed = true;
      }
      if (!changed)
        break;
    }
  }

  std::optional<AbstractValue> parse_domain(const std::string& text, int line,
                                            const AbstractState& state) const {
    try {
      return expression_domain(parse_expression(text, line), state);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }

  std::optional<AbstractValue> expression_domain(const Expression& expression,
                                                 const AbstractState& state) const {
    if (const auto literal = integer_literal(expression)) {
      const ExactIntegralDomain domain{*literal, *literal, true};
      return abstract_value(domain, within_exact_eight_digit_integer_range(domain), true);
    }
    if (expression.kind == "identifier") {
      const auto value = state.domains.find(expression.name);
      if (value != state.domains.end())
        return value->second;
      const auto constant = constants_.find(expression.name);
      return constant == constants_.end() ? std::nullopt
                                          : std::optional<AbstractValue>{constant->second};
    }
    if (expression.kind == "unary" && expression.expr != nullptr &&
        (expression.op == "-" || expression.op == "+")) {
      const auto value = expression_domain(*expression.expr, state);
      if (!value.has_value())
        return std::nullopt;
      if (expression.op == "+")
        return value;
      if (value->domain.minimum == std::numeric_limits<std::int64_t>::min() ||
          value->domain.maximum == std::numeric_limits<std::int64_t>::min()) {
        return std::nullopt;
      }
      const ExactIntegralDomain domain{-value->domain.maximum, -value->domain.minimum, true};
      return abstract_value(
          domain, value->decimal_derivation_exact && within_exact_eight_digit_integer_range(domain),
          !contains_zero(domain));
    }
    if (expression.kind == "binary" && expression.left != nullptr && expression.right != nullptr &&
        (expression.op == "+" || expression.op == "-" || expression.op == "*")) {
      const auto left = expression_domain(*expression.left, state);
      const auto right = expression_domain(*expression.right, state);
      if (!left.has_value() || !right.has_value())
        return std::nullopt;
      return arithmetic_value(*left, *right, expression.op);
    }
    if (expression.kind != "call")
      return std::nullopt;

    if (expression.callee == "grid_norm" || expression.callee == "grid_wrap") {
      const std::optional<int> width = emit::grid_norm_call_width(expression.args);
      if (!width.has_value() || *width != 4)
        return std::nullopt;
      const auto operand = expression_domain(expression.args.front(), state);
      if (!operand.has_value() || !operand->decimal_derivation_exact ||
          (contains_zero(operand->domain) && !operand->zero_canonical_positive) ||
          !helper_semantic_decimal_execution_exact(
              helper_semantic_one_based_modulo(helper_semantic_input(), *width), operand->domain)) {
        return std::nullopt;
      }
      // Even the compact width-four body is not an unconditional sanitizer:
      // division rounding can turn a large integer remainder into a
      // fractional result (for example 9999999 -> 2.8). Admit it only when
      // the finite input derivation and every decimal intermediate are proved
      // exact. Other widths use a different rounded quotient/multiply body and
      // remain fail-closed here.
      const ExactIntegralDomain domain{1, *width, true};
      return abstract_value(domain, within_exact_eight_digit_integer_range(domain), true);
    }
    if (expression.callee == "entered" && expression.args.size() == 2U) {
      const auto minimum = expression_domain(expression.args.at(0), state);
      const auto maximum = expression_domain(expression.args.at(1), state);
      if (!minimum.has_value() || !maximum.has_value() ||
          minimum->domain.minimum != minimum->domain.maximum ||
          maximum->domain.minimum != maximum->domain.maximum ||
          minimum->domain.minimum > maximum->domain.minimum) {
        return std::nullopt;
      }
      const ExactIntegralDomain domain{minimum->domain.minimum, maximum->domain.minimum, true};
      return abstract_value(domain,
                            minimum->decimal_derivation_exact &&
                                maximum->decimal_derivation_exact &&
                                within_exact_eight_digit_integer_range(domain),
                            !contains_zero(domain));
    }
    if ((expression.callee == "int" || expression.callee == "trunc") &&
        expression.args.size() == 1U) {
      return expression_domain(expression.args.front(), state);
    }
    if (expression.callee == "frac" && expression.args.size() == 1U) {
      const auto value = expression_domain(expression.args.front(), state);
      if (!value.has_value())
        return std::nullopt;
      const ExactIntegralDomain domain{0, 0, true};
      const bool zero_canonical_positive =
          value->domain.minimum >= 0 &&
          (!contains_zero(value->domain) || value->zero_canonical_positive);
      return abstract_value(domain, value->decimal_derivation_exact, zero_canonical_positive);
    }
    return std::nullopt;
  }

  void observe_call(const V2Statement& statement, const AbstractState& input) {
    if (!statement.name.has_value() || !targets_.contains(*statement.name))
      return;
    SiteFact& fact = facts_[*statement.name][&statement];
    fact.seen = true;
    if (statement.args.size() != 1U) {
      fact.valid = false;
      return;
    }
    const std::optional<AbstractValue> value =
        parse_domain(statement.args.front(), statement.line, input);
    if (!value.has_value()) {
      fact.valid = false;
      return;
    }
    fact.value = fact.value.domain.valid() ? unite(fact.value, *value) : *value;
  }

  AbstractState refine(const AbstractState& input, const V2Predicate& predicate, bool truth,
                       int line) const {
    if (!input.reachable || predicate.kind != "v2_compare")
      return input;
    AbstractState result = input;
    try {
      Expression left = parse_expression(predicate.left, line);
      Expression right = parse_expression(predicate.right, line);
      std::string op = truth ? predicate.op : invert_comparison(predicate.op);
      if (left.kind != "identifier" && right.kind == "identifier") {
        std::swap(left, right);
        op = swap_comparison(op);
      }
      if (left.kind != "identifier" || op.empty())
        return result;
      const auto boundary = expression_domain(right, input);
      if (!boundary.has_value() || !boundary->decimal_derivation_exact ||
          boundary->domain.minimum != boundary->domain.maximum)
        return result;
      const std::int64_t value = boundary->domain.minimum;
      auto current = result.domains.find(left.name);
      if (current == result.domains.end()) {
        if (op == "==") {
          const ExactIntegralDomain domain{value, value, true};
          // Numeric equality cannot distinguish an externally supplied -0
          // from +0, even though it does prove any nonzero singleton exactly.
          result.domains[left.name] =
              abstract_value(domain, within_exact_eight_digit_integer_range(domain), value != 0);
        }
        return result;
      }
      // An inexact abstract singleton describes the unrounded mathematical
      // expression, not necessarily the value compared by the calculator.
      // It must not make either branch unreachable: a later assignment of an
      // exact constant would otherwise launder the rounded value into a
      // false call-domain certificate.
      if (!current->second.decimal_derivation_exact)
        return result;

      const bool ordered = op == "<" || op == "<=" || op == ">" || op == ">=";
      if (ordered && value == 0 && contains_zero(current->second.domain) &&
          (!current->second.zero_canonical_positive || !boundary->zero_canonical_positive)) {
        // F x>=0/F x<0 distinguish -0 from +0. A plain numeric interval
        // cannot soundly split a possibly noncanonical zero across the two
        // sign branches, especially when one side assigns a new exact value.
        return result;
      }
      ExactIntegralDomain domain = current->second.domain;
      if (op == "==") {
        domain.minimum = std::max(domain.minimum, value);
        domain.maximum = std::min(domain.maximum, value);
      } else if (op == "!=") {
        if (domain.minimum == value && domain.maximum == value)
          result.reachable = false;
        return result;
      } else if (op == "<") {
        if (value == std::numeric_limits<std::int64_t>::min())
          result.reachable = false;
        else
          domain.maximum = std::min(domain.maximum, value - 1);
      } else if (op == "<=") {
        domain.maximum = std::min(domain.maximum, value);
      } else if (op == ">") {
        if (value == std::numeric_limits<std::int64_t>::max())
          result.reachable = false;
        else
          domain.minimum = std::max(domain.minimum, value + 1);
      } else if (op == ">=") {
        domain.minimum = std::max(domain.minimum, value);
      }
      if (domain.valid()) {
        current->second = abstract_value(domain, current->second.decimal_derivation_exact,
                                         current->second.zero_canonical_positive);
      } else {
        result.reachable = false;
      }
      if (!result.reachable)
        result.domains.clear();
    } catch (const std::exception&) {
    }
    return result;
  }

  FlowResult analyze_loop(const V2Statement& statement, const AbstractState& input,
                          std::vector<std::string>& call_stack) {
    AbstractState header = input;
    AbstractState returned;
    bool converged = false;
    for (int iteration = 0; iteration < kMaximumLoopIterations; ++iteration) {
      AbstractState body_input = header;
      if (statement.kind == "v2_while" && statement.predicate.has_value()) {
        body_input = refine(header, *statement.predicate, true, statement.line);
      }
      const FlowResult body = analyze_block(statement.body, body_input, call_stack);
      returned = join(returned, body.returned);
      const AbstractState next = join(input, body.fallthrough);
      if (same_state(header, next)) {
        converged = true;
        header = next;
        break;
      }
      header = next;
    }
    if (!converged) {
      // A finite interval chain can grow for arbitrarily many iterations.
      // Rather than issuing a certificate from an unproved widening, reject
      // this whole analysis run.
      sound_ = false;
      return statement.kind == "v2_loop"
                 ? FlowResult{.returned = returned}
                 : FlowResult{.fallthrough = unknown_state(input), .returned = returned};
    }
    if (statement.kind == "v2_loop")
      return FlowResult{.fallthrough = {}, .returned = returned};
    AbstractState exit = header;
    if (statement.predicate.has_value())
      exit = refine(header, *statement.predicate, false, statement.line);
    return FlowResult{.fallthrough = exit, .returned = returned};
  }

  FlowResult analyze_call(const V2Statement& statement, const AbstractState& input,
                          std::vector<std::string>& call_stack) {
    observe_call(statement, input);
    if (!statement.name.has_value()) {
      sound_ = false;
      return FlowResult{.fallthrough = unknown_state(input)};
    }
    const auto callee = rules_.find(*statement.name);
    if (callee == rules_.end()) {
      sound_ = false;
      return FlowResult{.fallthrough = unknown_state(input)};
    }
    if (call_stack.size() >= kMaximumCallDepth ||
        std::find(call_stack.begin(), call_stack.end(), *statement.name) != call_stack.end()) {
      sound_ = false;
      return FlowResult{.fallthrough = unknown_state(input)};
    }
    const V2Rule& rule = *callee->second;
    if (rule.params.size() != statement.args.size()) {
      sound_ = false;
      return FlowResult{.fallthrough = unknown_state(input)};
    }

    std::vector<std::optional<AbstractValue>> saved;
    AbstractState entry = input;
    saved.reserve(rule.params.size());
    for (std::size_t index = 0; index < rule.params.size(); ++index) {
      const auto old = entry.domains.find(rule.params.at(index));
      saved.push_back(old == entry.domains.end() ? std::nullopt
                                                 : std::optional<AbstractValue>{old->second});
      const std::optional<AbstractValue> argument =
          parse_domain(statement.args.at(index), statement.line, input);
      if (argument.has_value())
        entry.domains[rule.params.at(index)] = *argument;
      else
        entry.domains.erase(rule.params.at(index));
    }

    call_stack.push_back(*statement.name);
    const FlowResult body = analyze_block(rule.body, entry, call_stack);
    call_stack.pop_back();
    AbstractState output = join(body.fallthrough, body.returned);
    for (std::size_t index = 0; index < rule.params.size(); ++index) {
      if (saved.at(index).has_value())
        output.domains[rule.params.at(index)] = *saved.at(index);
      else
        output.domains.erase(rule.params.at(index));
    }
    return FlowResult{.fallthrough = output};
  }

  AbstractState unknown_state(const AbstractState& input) const {
    return AbstractState{.reachable = input.reachable};
  }

  FlowResult analyze_statement(const V2Statement& statement, const AbstractState& input,
                               std::vector<std::string>& call_stack) {
    if (!input.reachable)
      return FlowResult{.fallthrough = input};
    if (statement.kind == "v2_assign" || statement.kind == "v2_update" ||
        statement.kind == "v2_read") {
      AbstractState output = input;
      if (!statement.target.has_value()) {
        sound_ = false;
        return FlowResult{.fallthrough = unknown_state(input)};
      }
      std::string target;
      try {
        const Expression parsed_target = parse_expression(*statement.target, statement.line);
        if (parsed_target.kind == "indexed") {
          output.domains.erase(parsed_target.base);
          return FlowResult{.fallthrough = output};
        }
        if (parsed_target.kind != "identifier") {
          sound_ = false;
          return FlowResult{.fallthrough = unknown_state(input)};
        }
        target = parsed_target.name;
      } catch (const std::exception&) {
        sound_ = false;
        return FlowResult{.fallthrough = unknown_state(input)};
      }
      std::optional<AbstractValue> value;
      if (statement.kind != "v2_read" && statement.expr.has_value())
        value = parse_domain(*statement.expr, statement.line, input);
      if (statement.kind == "v2_update" && value.has_value()) {
        const auto previous = input.domains.find(target);
        if (previous == input.domains.end() || !statement.op.has_value() ||
            (*statement.op != "+=" && *statement.op != "-=" && *statement.op != "*=")) {
          value.reset();
        } else {
          const std::string op =
              *statement.op == "+=" ? "+" : (*statement.op == "-=" ? "-" : "*");
          value = arithmetic_value(previous->second, *value, op);
        }
      }
      if (value.has_value())
        output.domains[target] = *value;
      else
        output.domains.erase(target);
      return FlowResult{.fallthrough = output};
    }
    if (statement.kind == "v2_invoke")
      return analyze_call(statement, input, call_stack);
    if (statement.kind == "v2_if" && statement.predicate.has_value()) {
      const bool then_truth = !statement.negated;
      const FlowResult then_result = analyze_block(
          statement.then_body, refine(input, *statement.predicate, then_truth, statement.line),
          call_stack);
      const FlowResult else_result = analyze_block(
          statement.else_body, refine(input, *statement.predicate, !then_truth, statement.line),
          call_stack);
      return FlowResult{.fallthrough = join(then_result.fallthrough, else_result.fallthrough),
                        .returned = join(then_result.returned, else_result.returned)};
    }
    if (statement.kind == "v2_while" || statement.kind == "v2_loop")
      return analyze_loop(statement, input, call_stack);
    if (statement.kind == "v2_block")
      return analyze_block(statement.body, input, call_stack);
    if (statement.kind == "v2_match") {
      AbstractState fallthrough;
      AbstractState returned;
      for (const V2MatchCase& match_case : statement.cases) {
        if (match_case.action == nullptr)
          continue;
        const FlowResult arm = analyze_statement(*match_case.action, input, call_stack);
        fallthrough = join(fallthrough, arm.fallthrough);
        returned = join(returned, arm.returned);
      }
      if (statement.otherwise != nullptr) {
        const FlowResult arm = analyze_statement(*statement.otherwise, input, call_stack);
        fallthrough = join(fallthrough, arm.fallthrough);
        returned = join(returned, arm.returned);
      } else {
        fallthrough = join(fallthrough, input);
      }
      return FlowResult{.fallthrough = fallthrough, .returned = returned};
    }
    if (statement.kind == "v2_raw") {
      AbstractState output = input;
      for (const V2RawOutput& raw_output : statement.outputs)
        output.domains.erase(raw_output.target);
      for (const std::string& clobber : statement.clobbers)
        output.domains.erase(clobber);
      return FlowResult{.fallthrough = output};
    }
    if (statement.kind == "v2_return")
      return FlowResult{.returned = input};
    if (statement.kind == "v2_stop")
      return {};
    if (statement.kind == "v2_show" || statement.kind == "v2_preview")
      return FlowResult{.fallthrough = input};

    // Any new statement kind must receive an explicit transfer function before
    // it can contribute a proof.
    sound_ = false;
    return FlowResult{.fallthrough = unknown_state(input)};
  }

  FlowResult analyze_block(const std::vector<V2Statement>& statements, const AbstractState& input,
                           std::vector<std::string>& call_stack) {
    AbstractState fallthrough = input;
    AbstractState returned;
    for (const V2Statement& statement : statements) {
      const FlowResult step = analyze_statement(statement, fallthrough, call_stack);
      fallthrough = step.fallthrough;
      returned = join(returned, step.returned);
      if (!fallthrough.reachable)
        break;
    }
    return FlowResult{.fallthrough = fallthrough, .returned = returned};
  }
};

} // namespace

std::map<std::string, FlowSensitiveCallDomainProof>
prove_flow_sensitive_call_domains(const V2Program& program,
                                  const std::set<std::string>& rule_names) {
  return Analyzer(program, rule_names).run();
}

} // namespace mkpro::core
