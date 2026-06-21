#include "mkpro/core/interprocedural_dse.hpp"

#include "mkpro/core/parser.hpp"
#include "mkpro/core/rule_cfg.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core {

namespace {

bool sets_equal(const std::set<std::string>& left, const std::set<std::string>& right) {
  return left == right;
}

std::set<std::string> collect_all_vars(const RuleCfg& cfg) {
  std::set<std::string> vars;
  for (const RuleCfgNode& node : cfg.nodes) {
    vars.insert(node.defs.begin(), node.defs.end());
    vars.insert(node.uses.begin(), node.uses.end());
  }
  return vars;
}

std::vector<std::set<std::string>> compute_live_out(const RuleCfg& cfg,
                                                    const std::set<std::string>& all_vars) {
  const std::size_t n = cfg.nodes.size();
  std::vector<std::set<std::string>> live_in(n);
  std::vector<std::set<std::string>> live_out(n);
  bool changed = true;
  int rounds = 0;
  while (changed && rounds < 1000) {
    changed = false;
    ++rounds;
    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
      const RuleCfgNode& node = cfg.nodes.at(static_cast<std::size_t>(i));
      std::set<std::string> new_out;
      for (const int successor : node.succ) {
        const std::set<std::string>& successor_in = live_in.at(static_cast<std::size_t>(successor));
        new_out.insert(successor_in.begin(), successor_in.end());
      }

      const std::vector<std::string> defs = node.barrier ? std::vector<std::string>{} : node.defs;
      std::set<std::string> new_in =
          node.barrier ? all_vars : std::set<std::string>{node.uses.begin(), node.uses.end()};
      for (const std::string& reg : new_out) {
        if (std::find(defs.begin(), defs.end(), reg) == defs.end())
          new_in.insert(reg);
      }

      if (!sets_equal(new_in, live_in.at(static_cast<std::size_t>(i))) ||
          !sets_equal(new_out, live_out.at(static_cast<std::size_t>(i)))) {
        live_in.at(static_cast<std::size_t>(i)) = std::move(new_in);
        live_out.at(static_cast<std::size_t>(i)) = std::move(new_out);
        changed = true;
      }
    }
  }
  return live_out;
}

std::optional<Expression> parse_expression_safe(const std::string& text, int line) {
  try {
    return parse_expression(text, line);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

V2Statement statement_from_pruned_vector(std::vector<V2Statement> statements, int line) {
  if (statements.size() == 1)
    return std::move(statements.front());
  return V2Statement{
      .kind = "v2_block",
      .body = std::move(statements),
      .line = line,
  };
}

std::vector<V2Statement> prune_statements(const std::vector<V2Statement>& statements,
                                          const std::set<const V2Statement*>& doomed);

V2Statement prune_statement(const V2Statement& statement,
                            const std::set<const V2Statement*>& doomed) {
  V2Statement result = statement;
  if (statement.kind == "v2_loop" || statement.kind == "v2_while" || statement.kind == "v2_block") {
    result.body = prune_statements(statement.body, doomed);
  } else if (statement.kind == "v2_if") {
    result.then_body = prune_statements(statement.then_body, doomed);
    result.else_body = prune_statements(statement.else_body, doomed);
  } else if (statement.kind == "v2_match") {
    for (V2MatchCase& match_case : result.cases) {
      if (match_case.action == nullptr)
        continue;
      std::vector<V2Statement> pruned = prune_statements({*match_case.action}, doomed);
      match_case.action = std::make_shared<V2Statement>(
          statement_from_pruned_vector(std::move(pruned), match_case.line));
    }
    if (result.otherwise != nullptr) {
      std::vector<V2Statement> pruned = prune_statements({*result.otherwise}, doomed);
      result.otherwise = std::make_shared<V2Statement>(
          statement_from_pruned_vector(std::move(pruned), statement.line));
    }
  }
  return result;
}

std::vector<V2Statement> prune_statements(const std::vector<V2Statement>& statements,
                                          const std::set<const V2Statement*>& doomed) {
  std::vector<V2Statement> result;
  result.reserve(statements.size());
  for (const V2Statement& statement : statements) {
    if (doomed.contains(&statement))
      continue;
    result.push_back(prune_statement(statement, doomed));
  }
  return result;
}

} // namespace

int eliminate_interprocedural_dead_stores(V2Program& program,
                                          std::vector<OptimizationReport>& optimizations) {
  const std::set<std::string> fields = program_state_fields(program);
  if (fields.empty())
    return 0;

  const RuleCfg cfg = build_rule_cfg(program);
  const std::set<std::string> all_vars = collect_all_vars(cfg);
  const std::vector<std::set<std::string>> live_out = compute_live_out(cfg, all_vars);

  std::set<const V2Statement*> doomed;
  for (const RuleCfgNode& node : cfg.nodes) {
    const V2Statement* assign = node.assign;
    if (assign == nullptr || !assign->target.has_value() || !assign->expr.has_value())
      continue;
    const std::optional<Expression> target = parse_expression_safe(*assign->target, assign->line);
    if (!target.has_value() || target->kind != "identifier")
      continue;
    if (!fields.contains(target->name))
      continue;
    const std::optional<Expression> expr = parse_expression_safe(*assign->expr, assign->line);
    if (!expr.has_value() || !expression_is_call_free(*expr))
      continue;
    if (live_out.at(static_cast<std::size_t>(node.id)).contains(target->name))
      continue;
    doomed.insert(assign);
  }

  if (doomed.empty())
    return 0;

  program.body = prune_statements(program.body, doomed);
  for (V2Rule& rule : program.rules)
    rule.body = prune_statements(rule.body, doomed);

  optimizations.push_back(OptimizationReport{
      .name = "interprocedural-dead-store",
      .detail = "Removed " + std::to_string(doomed.size()) +
                " store(s) whose value is always overwritten before it can be observed across "
                "the rule call graph.",
  });
  return static_cast<int>(doomed.size());
}

} // namespace mkpro::core
