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

std::optional<Expression> parse_expression_safe(const std::string& text, int line) {
  try {
    return parse_expression(text, line);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<std::string> simple_show_target(const V2Statement& statement) {
  if (statement.kind != "v2_show")
    return std::nullopt;
  if (statement.target.has_value()) {
    const std::optional<Expression> target = parse_expression_safe(*statement.target, statement.line);
    if (target.has_value() && target->kind == "identifier")
      return target->name;
  }
  if (statement.items.has_value() && statement.items->size() == 1U) {
    const DisplayItem& item = statement.items->front();
    if (item.kind != "source")
      return std::nullopt;
    if (item.expr.has_value() && item.expr->kind == "identifier")
      return item.expr->name;
    if (!item.expr.has_value() && !item.name.empty())
      return item.name;
  }
  return std::nullopt;
}

void collect_loop_header_show_targets(const std::vector<V2Statement>& statements,
                                      std::set<std::string>& targets);

void collect_loop_header_show_target(const V2Statement& statement, std::set<std::string>& targets) {
  if (statement.kind == "v2_loop" && statement.body.size() >= 2U) {
    const V2Statement& show = statement.body.at(0);
    const V2Statement& read = statement.body.at(1);
    if (show.kind == "v2_show" && read.kind == "v2_read") {
      if (std::optional<std::string> target = simple_show_target(show))
        targets.insert(*target);
    }
  }
  collect_loop_header_show_targets(statement.body, targets);
  collect_loop_header_show_targets(statement.then_body, targets);
  collect_loop_header_show_targets(statement.else_body, targets);
  for (const V2MatchCase& match_case : statement.cases) {
    if (match_case.action != nullptr)
      collect_loop_header_show_target(*match_case.action, targets);
  }
  if (statement.otherwise != nullptr)
    collect_loop_header_show_target(*statement.otherwise, targets);
}

void collect_loop_header_show_targets(const std::vector<V2Statement>& statements,
                                      std::set<std::string>& targets) {
  for (const V2Statement& statement : statements)
    collect_loop_header_show_target(statement, targets);
}

std::set<std::string> loop_header_show_targets(const V2Program& program) {
  std::set<std::string> targets;
  collect_loop_header_show_targets(program.body, targets);
  for (const V2Rule& rule : program.rules)
    collect_loop_header_show_targets(rule.body, targets);
  return targets;
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
  const RuleCfgLiveness liveness = compute_rule_cfg_liveness(cfg);
  const std::set<std::string> loop_show_targets = loop_header_show_targets(program);

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
    if (loop_show_targets.contains(target->name))
      continue;
    const std::optional<Expression> expr = parse_expression_safe(*assign->expr, assign->line);
    if (!expr.has_value() || !expression_is_call_free(*expr))
      continue;
    if (liveness.live_out.at(static_cast<std::size_t>(node.id)).contains(target->name))
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
