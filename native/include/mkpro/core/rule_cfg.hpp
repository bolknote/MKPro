#pragma once

#include "mkpro/core/ast.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace mkpro::core {

struct RuleCfgNode {
  int id = 0;
  std::vector<int> succ;
  std::vector<std::string> defs;
  std::vector<std::string> uses;
  const V2Statement* assign = nullptr;
  bool barrier = false;
};

struct RuleCfg {
  std::vector<RuleCfgNode> nodes;
  int entry_node = 0;
  std::map<std::string, int> routine_entry;
  std::map<std::string, int> routine_exit;
};

void collect_expression_vars(const Expression& expression, std::set<std::string>& out);
std::vector<std::string> expression_vars(const Expression& expression);
bool expression_is_call_free(const Expression& expression);

RuleCfg build_rule_cfg(const V2Program& program);
std::set<std::string> program_state_fields(const V2Program& program);

} // namespace mkpro::core
