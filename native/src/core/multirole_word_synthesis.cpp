#include "mkpro/core/multirole_word_synthesis.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/raw_bcd_unary_selector.hpp"
#include "mkpro/core/raw_bcd_word.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <utility>

namespace mkpro::core {
namespace {

bool dimensions(const std::vector<std::vector<std::string>>& columns,
                const std::vector<FiniteWordAddressDomain>& domains) {
  if (columns.empty() || columns.size() > 8U || domains.empty() || domains.size() > 8U)
    return false;
  for (const auto& column : columns)
    if (column.size() != domains.size())
      return false;
  for (const auto& domain : domains) {
    if (domain.identity.empty() || domain.selector.size() != 1U ||
        std::string("789abcde").find(domain.selector[0]) == std::string::npos ||
        domain.targets.empty() || domain.targets.size() > 100U ||
        std::any_of(domain.targets.begin(), domain.targets.end(),
                    [](int target) { return target < 0 || target > 99; }))
      return false;
  }
  return true;
}

ConstantSynthesisRequest row_request(
    const std::vector<std::vector<std::string>>& columns,
    const std::vector<ConstantObservationKind>& decoders,
    const FiniteWordAddressDomain& domain, std::size_t row, int target) {
  ConstantSynthesisRequest request;
  request.address = ConstantAddressConstraint{domain.selector, target, domain.model};
  for (std::size_t column = 0; column < columns.size(); ++column)
    request.observations.push_back({decoders[column], columns[column][row]});
  return request;
}

} // namespace

bool certify_multirole_words(
    const MultiroleWordEncoding& encoding,
    const std::vector<std::vector<std::string>>& columns,
    const std::vector<FiniteWordAddressDomain>& domains) {
  if (!dimensions(columns, domains) || encoding.words.size() != domains.size() ||
      encoding.targets.size() != domains.size() || encoding.decoders.size() != columns.size())
    return false;
  std::map<std::string, int> bindings;
  std::map<int, std::string> occupants;
  for (std::size_t row = 0; row < domains.size(); ++row) {
    const auto& domain = domains[row];
    const int target = encoding.targets[row];
    if (std::find(domain.targets.begin(), domain.targets.end(), target) == domain.targets.end() ||
        (bindings.contains(domain.identity) && bindings.at(domain.identity) != target) ||
        (occupants.contains(target) && occupants.at(target) != domain.identity) ||
        !constant_satisfies_observations(
            encoding.words[row], row_request(columns, encoding.decoders, domain, row, target)))
      return false;
    bindings[domain.identity] = target;
    occupants[target] = domain.identity;
  }
  return true;
}

MultiroleWordSynthesisResult synthesize_multirole_words(
    const std::vector<std::vector<std::string>>& columns,
    const std::vector<FiniteWordAddressDomain>& domains,
    const FiniteConstantSynthesisOptions& options) {
  MultiroleWordSynthesisResult result;
  if (!dimensions(columns, domains))
    return result;
  const auto charge = [&]() {
    if (result.work >= options.maximum_work) {
      result.exhausted = true;
      return false;
    }
    ++result.work;
    return true;
  };
  using Kind = ConstantObservationKind;
  // Sign first exposes address-only freedom without exhausting the budget on
  // incompatible identity tuples. This order is fixed and source independent.
  constexpr std::array<Kind, 5> kinds{
      Kind::Sign, Kind::Identity, Kind::Negate, Kind::Integer, Kind::Fraction};
  std::vector<Kind> decoders(columns.size());
  std::set<std::vector<std::string>> seen;
  bool row_budget_exhausted = false;
  std::function<void(std::size_t)> select_decoders;
  select_decoders = [&](std::size_t column) {
    if (!charge())
      return;
    if (column < columns.size()) {
      for (const auto kind : kinds) {
        decoders[column] = kind;
        select_decoders(column + 1U);
        if (result.exhausted)
          return;
      }
      return;
    }
    struct Choice { std::string word; int target; };
    std::vector<std::vector<Choice>> choices(domains.size());
    // Reserve search work for all rows rather than allowing the first row's
    // address domain to starve every following mode.
    const std::size_t per_row = std::max<std::size_t>(
        1U, (options.maximum_work - result.work) / (domains.size() + 1U));
    for (std::size_t row = 0; row < domains.size(); ++row) {
      const auto begin_work = result.work;
      for (const int target : domains[row].targets) {
        if (!charge())
          return;
        auto request = row_request(columns, decoders, domains[row], row, target);
        request.maximum_work = std::min(
            options.maximum_work - result.work, per_row - std::min(per_row, result.work - begin_work));
        const auto solved = synthesize_constant(request);
        result.work += solved.work;
        row_budget_exhausted = row_budget_exhausted ||
            solved.status == ConstantSynthesisStatus::Exhausted;
        if (solved.value)
          choices[row].push_back({*solved.value, target});
        if (choices[row].size() >= 8U || result.work - begin_work >= per_row) {
          row_budget_exhausted = true;
          break;
        }
      }
      if (choices[row].empty())
        return;
    }
    // Breadth-first changes of row choices avoid a Cartesian DFS that varies
    // only the last mode before reaching the candidate bound.
    std::vector<std::vector<std::size_t>> queue(1, std::vector<std::size_t>(domains.size(), 0));
    std::set<std::vector<std::size_t>> visited{queue.front()};
    for (std::size_t next = 0; next < queue.size(); ++next) {
      if (!charge())
        return;
      const auto indexes = queue[next];
      MultiroleWordEncoding encoding;
      encoding.decoders = decoders;
      for (std::size_t row = 0; row < domains.size(); ++row) {
        encoding.words.push_back(choices[row][indexes[row]].word);
        encoding.targets.push_back(choices[row][indexes[row]].target);
      }
      if (certify_multirole_words(encoding, columns, domains) &&
          seen.insert(encoding.words).second) {
        if (result.encodings.size() >= options.maximum_candidates) {
          result.exhausted = true;
          return;
        }
        result.encodings.push_back(std::move(encoding));
      }
      for (std::size_t row = 0; row < domains.size(); ++row) {
        auto following = indexes;
        if (++following[row] < choices[row].size() && visited.insert(following).second)
          queue.push_back(std::move(following));
      }
    }
  };
  select_decoders(0);
  result.exhausted = result.exhausted || row_budget_exhausted;
  return result;
}

namespace {

bool valid_raw_request(const RawWordSynthesisRequest& request) {
  if (request.modes.empty() || request.modes.size() > 8U || request.transitions.size() > 64U)
    return false;
  for (const auto& mode : request.modes) {
    if ((mode.sign && *mode.sign != -1 && *mode.sign != 1) ||
        mode.right_products.size() > 17U || mode.control.size() > 8U)
      return false;
    for (const auto& product : mode.right_products)
      if (product.input_power10 < -8 || product.input_power10 > 8 ||
          product.output_power10 < -11 || product.output_power10 > 8 ||
          (product.output_sign != -1 && product.output_sign != 1))
        return false;
    for (const auto& observation : mode.control) {
      const auto& address = observation.address;
      if (address.identity.empty() || address.selector.size() != 1U ||
          std::string("789abcde").find(address.selector[0]) == std::string::npos ||
          address.targets.empty() || address.targets.size() > 100U ||
          std::any_of(address.targets.begin(), address.targets.end(),
                      [](int target) { return target < 0 || target > 99; }) ||
          (observation.sign && (*observation.sign < -1 || *observation.sign > 1)))
        return false;
      switch (observation.view) {
      case RawWordControlView::StoredWord: break;
      case RawWordControlView::DegreeTangent:
        if (request.angle_mode != mk61_trig::AngleMode::Deg) return false;
        break;
      default: return false;
      }
    }
  }
  for (const auto& edge : request.transitions) {
    if (edge.from >= request.modes.size() || edge.to >= request.modes.size())
      return false;
    switch (edge.kind) {
    case RawWordTransitionKind::Identity:
    case RawWordTransitionKind::Negate: break;
    default: return false;
    }
  }
  return true;
}

bool bind_raw_target(std::map<std::string, int>& bindings,
                     const std::string& identity, int target) {
  const auto old = bindings.find(identity);
  if (old != bindings.end())
    return old->second == target;
  for (const auto& [other, occupied] : bindings)
    if (other != identity && occupied == target)
      return false;
  bindings.emplace(identity, target);
  return true;
}

// Recomputed by the certificate checker, independently of enumeration.
std::optional<std::map<std::string, int>> raw_mode_bindings(
    const std::string& spelling, const RawWordMode& mode,
    mk61_trig::AngleMode angle_mode) {
  const auto word = decode_raw_single_hex_word(spelling);
  if (!word || word->canonical_word != spelling ||
      (mode.sign && *mode.sign != (word->negative ? -1 : 1)))
    return std::nullopt;
  for (const auto& observation : mode.right_products) {
    const auto product = evaluate_raw_right_power10_product(
        observation.input_power10, spelling);
    if (!product || product->exponent != observation.output_power10 ||
        (product->negative ? -1 : 1) != observation.output_sign)
      return std::nullopt;
  }
  std::map<std::string, int> bindings;
  for (const auto& observation : mode.control) {
    const auto& address = observation.address;
    std::string decoded = spelling;
    int decoded_sign = word->negative ? -1 : 1;
    if (observation.view == RawWordControlView::DegreeTangent) {
      const auto unary = evaluate_raw_bcd_unary_indirect_selector(
          angle_mode, mk61_trig::Function::Tg, spelling, address.selector, address.model);
      if (!unary)
        return std::nullopt;
      decoded = unary->raw_result;
      decoded_sign = decoded == "0" ? 0 : (decoded.starts_with("-") ? -1 : 1);
    }
    if (observation.sign && *observation.sign != decoded_sign)
      return std::nullopt;
    const auto flow = evaluate_indirect_address(
        address.selector, decoded, IndirectOperationKind::Flow, address.model);
    if (!flow || !flow->actual_flow_target || !flow->result_value ||
        std::find(address.targets.begin(), address.targets.end(), *flow->actual_flow_target) ==
            address.targets.end())
      return std::nullopt;
    if (observation.view == RawWordControlView::StoredWord &&
        !indirect_writeback_preserves_literal_value(*flow, decoded))
      return std::nullopt;
    const auto repeated = evaluate_indirect_address(
        address.selector, *flow->result_value, IndirectOperationKind::Flow, address.model);
    if (!repeated || repeated->actual_flow_target != flow->actual_flow_target ||
        repeated->flow_target != flow->flow_target ||
        !indirect_writeback_preserves_literal_value(*repeated, *flow->result_value) ||
        !bind_raw_target(bindings, address.identity, *flow->actual_flow_target))
      return std::nullopt;
  }
  return bindings;
}

bool raw_transitions_match(const std::vector<std::string>& words,
                           const std::vector<RawWordTransition>& transitions) {
  for (const auto& edge : transitions) {
    // During enumeration, check every edge as soon as both ends are bound.
    if (edge.from >= words.size() || edge.to >= words.size())
      continue;
    std::string next = words[edge.from];
    if (edge.kind == RawWordTransitionKind::Negate)
      next = next.starts_with("-") ? next.substr(1) : "-" + next;
    if (next != words[edge.to])
      return false;
  }
  return true;
}

} // namespace

bool certify_raw_multirole_words(const RawMultiroleWordEncoding& encoding,
                                const RawWordSynthesisRequest& request) {
  if (!valid_raw_request(request) || encoding.words.size() != request.modes.size() ||
      !raw_transitions_match(encoding.words, request.transitions))
    return false;
  std::map<std::string, int> bindings;
  for (std::size_t mode = 0; mode < request.modes.size(); ++mode) {
    const auto row = raw_mode_bindings(encoding.words[mode], request.modes[mode], request.angle_mode);
    if (!row)
      return false;
    for (const auto& [identity, target] : *row)
      if (!bind_raw_target(bindings, identity, target))
        return false;
  }
  return bindings == encoding.target_bindings;
}

RawMultiroleWordSynthesisResult synthesize_raw_multirole_words(
    const RawWordSynthesisRequest& request, const FiniteConstantSynthesisOptions& options) {
  RawMultiroleWordSynthesisResult result;
  if (!valid_raw_request(request))
    return result;
  const auto charge = [&]() {
    if (result.work >= options.maximum_work) {
      result.exhausted = true;
      return false;
    }
    ++result.work;
    return true;
  };
  struct Choice {
    std::string word;
    std::map<std::string, int> bindings;
  };
  std::vector<std::vector<Choice>> choices(request.modes.size());
  for (std::size_t row = 0; row < request.modes.size(); ++row) {
    for (int exponent = 1; exponent <= 4; ++exponent) {
      for (const char digit : std::string("ABCD")) {
        for (const bool negative : {false, true}) {
          if (!charge()) return result;
          const auto word = (negative ? "-" : "") + std::string(1, digit) +
                            "E-" + std::to_string(exponent);
          const auto bindings = raw_mode_bindings(word, request.modes[row], request.angle_mode);
          if (bindings)
            choices[row].push_back({word, *bindings});
        }
      }
    }
    if (choices[row].empty())
      return result;
  }
  std::function<void(RawMultiroleWordEncoding)> select;
  select = [&](RawMultiroleWordEncoding partial) {
    if (result.exhausted || !charge()) return;
    const auto row = partial.words.size();
    if (row == request.modes.size()) {
      if (!certify_raw_multirole_words(partial, request)) return;
      if (result.encodings.size() >= options.maximum_candidates) {
        result.exhausted = true;
        return;
      }
      result.encodings.push_back(std::move(partial));
      return;
    }
    for (const auto& choice : choices[row]) {
      auto next = partial;
      next.words.push_back(choice.word);
      if (!charge()) return;
      if (!raw_transitions_match(next.words, request.transitions)) continue;
      bool compatible = true;
      for (const auto& [identity, target] : choice.bindings)
        compatible = bind_raw_target(next.target_bindings, identity, target) && compatible;
      if (compatible) select(std::move(next));
      if (result.exhausted) return;
    }
  };
  select({});
  return result;
}

} // namespace mkpro::core
