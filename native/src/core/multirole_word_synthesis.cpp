#include "mkpro/core/multirole_word_synthesis.hpp"

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

} // namespace mkpro::core
