#include "mkpro/core/rules.hpp"

#include <algorithm>
#include <map>

namespace mkpro::core {

namespace {

void collect_calls(const V2Statement& statement, std::set<std::string>& calls) {
  if (statement.kind == "v2_invoke" && statement.name.has_value())
    calls.insert(*statement.name);
  for (const V2Statement& child : statement.body)
    collect_calls(child, calls);
  for (const V2Statement& child : statement.then_body)
    collect_calls(child, calls);
  for (const V2Statement& child : statement.else_body)
    collect_calls(child, calls);
  for (const V2MatchCase& match_case : statement.cases) {
    if (match_case.action != nullptr)
      collect_calls(*match_case.action, calls);
  }
  if (statement.otherwise != nullptr)
    collect_calls(*statement.otherwise, calls);
}

bool statement_returns_value(const V2Statement& statement) {
  if (statement.kind == "v2_return")
    return true;
  for (const V2Statement& child : statement.body) {
    if (statement_returns_value(child))
      return true;
  }
  for (const V2Statement& child : statement.then_body) {
    if (statement_returns_value(child))
      return true;
  }
  for (const V2Statement& child : statement.else_body) {
    if (statement_returns_value(child))
      return true;
  }
  for (const V2MatchCase& match_case : statement.cases) {
    if (match_case.action != nullptr && statement_returns_value(*match_case.action))
      return true;
  }
  return statement.otherwise != nullptr && statement_returns_value(*statement.otherwise);
}

bool rule_returns_value(const V2Rule& rule) {
  return std::any_of(rule.body.begin(), rule.body.end(), [](const V2Statement& statement) {
    return statement_returns_value(statement);
  });
}

std::map<std::string, std::set<std::string>> rule_call_graph(const V2Program& program) {
  std::map<std::string, std::set<std::string>> graph;
  for (const V2Rule& rule : program.rules) {
    std::set<std::string> calls;
    for (const V2Statement& statement : rule.body)
      collect_calls(statement, calls);
    graph[rule.name] = std::move(calls);
  }
  return graph;
}

bool rule_reaches(const std::map<std::string, std::set<std::string>>& graph,
                  const std::string& start, const std::string& target) {
  std::set<std::string> visiting;
  auto visit = [&](const std::string& name, const auto& self) -> bool {
    const auto calls_it = graph.find(name);
    if (calls_it == graph.end())
      return false;
    for (const std::string& callee : calls_it->second) {
      if (callee == target)
        return true;
      if (visiting.contains(callee))
        continue;
      visiting.insert(callee);
      if (self(callee, self))
        return true;
    }
    return false;
  };
  visiting.insert(start);
  return visit(start, visit);
}

}  // namespace

std::set<std::string> inline_statement_rule_names(const V2Program& program) {
  const std::map<std::string, std::set<std::string>> graph = rule_call_graph(program);
  std::set<std::string> names;
  for (const V2Rule& rule : program.rules) {
    if (!rule.params.empty())
      continue;
    if (rule_returns_value(rule))
      continue;
    if (rule_reaches(graph, rule.name, rule.name))
      continue;
    names.insert(rule.name);
  }
  return names;
}

}  // namespace mkpro::core
