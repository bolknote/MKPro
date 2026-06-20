#include "mkpro/core/rules.hpp"

#include "test_support.hpp"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

V2Statement invoke(std::string name) {
  V2Statement statement;
  statement.kind = "v2_invoke";
  statement.name = std::move(name);
  return statement;
}

V2Statement return_value() {
  V2Statement statement;
  statement.kind = "v2_return";
  statement.expr = "1";
  return statement;
}

V2Rule rule(std::string name, std::vector<V2Statement> body) {
  V2Rule result;
  result.name = std::move(name);
  result.body = std::move(body);
  return result;
}

}  // namespace

void rules_match_typescript_contract() {
  V2Program program;
  program.rules.push_back(rule("plain", {}));
  program.rules.push_back(rule("direct_recursive", {invoke("direct_recursive")}));
  program.rules.push_back(rule("mutual_a", {invoke("mutual_b")}));
  program.rules.push_back(rule("mutual_b", {invoke("mutual_a")}));
  program.rules.push_back(rule("returns", {return_value()}));

  V2Rule parameterized = rule("parameterized", {});
  parameterized.params.push_back("value");
  program.rules.push_back(parameterized);

  const std::set<std::string> inline_rules = core::inline_statement_rule_names(program);
  require(inline_rules.contains("plain"), "plain no-arg statement rule should inline");
  require(!inline_rules.contains("direct_recursive"),
          "direct-recursive no-arg rule should compile as a callable procedure");
  require(!inline_rules.contains("mutual_a"),
          "mutual-recursive no-arg rule should compile as a callable procedure");
  require(!inline_rules.contains("mutual_b"),
          "mutual-recursive callee should compile as a callable procedure");
  require(!inline_rules.contains("returns"), "value-returning function should not inline");
  require(!inline_rules.contains("parameterized"), "parameterized function should not inline");
}

}  // namespace mkpro::tests
