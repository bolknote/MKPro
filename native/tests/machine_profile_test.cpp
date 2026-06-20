#include "mkpro/core/machine_profile.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>

namespace mkpro::tests {

namespace {

bool has_fact(std::string_view id) {
  const auto& facts = mk61_profile().emulator_facts;
  return std::ranges::any_of(facts, [id](const auto& fact) { return fact.id == id; });
}

const EmulatorFactReport* find_fact(std::string_view id) {
  const auto& facts = mk61_profile().emulator_facts;
  const auto it = std::ranges::find_if(facts, [id](const auto& fact) { return fact.id == id; });
  return it == facts.end() ? nullptr : &*it;
}

}  // namespace

void machine_profile_matches_typescript_contract() {
  const auto& profile = mk61_profile();
  require(profile.id == "mk61", "profile id should be mk61");
  require(machine_supports(profile, "super-dark-dispatch"),
          "profile should support super-dark-dispatch");
  require(machine_supports(profile, "r0-fractional-sentinel"),
          "profile should support r0-fractional-sentinel");
  require(machine_supports(profile, "raw-display-5f"), "profile should support raw-display-5f");
  require(has_fact("super-dark-fa-ff-indirect"), "profile should record super-dark fact");
  require(has_fact("r0-fractional-jump-99"), "profile should record fractional jump fact");
  require(has_fact("r0-fractional-selects-r3"), "profile should record fractional R3 fact");
  require(has_fact("raw-display-5f"), "profile should record raw display 5F fact");

  const EmulatorFactReport* alias = find_fact("r0-star-f-aliases");
  require(alias != nullptr, "profile should record R0 star-F aliases");
  require(alias->detail.find("do not preserve R0") != std::string::npos,
          "R0 star-F alias fact should mention R0 preservation");
}

}  // namespace mkpro::tests
