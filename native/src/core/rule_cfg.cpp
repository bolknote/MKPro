#include "mkpro/core/rule_cfg.hpp"

#include "mkpro/core/parser.hpp"
#include "mkpro/core/state_banks.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace mkpro::core {

namespace {

constexpr const char* kMainRoutineName = "__main";

std::vector<std::string> sorted_vector(const std::set<std::string>& values) {
  return {values.begin(), values.end()};
}

void add_all(std::set<std::string>& target, const std::vector<std::string>& values) {
  target.insert(values.begin(), values.end());
}

void add_unique_successor(std::vector<int>& successors, int target) {
  if (std::find(successors.begin(), successors.end(), target) == successors.end())
    successors.push_back(target);
}

std::optional<Expression> parse_expression_safe(const std::string& text, int line) {
  try {
    return parse_expression(text, line);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

const V2StateField* state_bank_field_for_expression(const V2Program& program,
                                                    const Expression& expression) {
  if (expression.kind != "indexed")
    return nullptr;
  const std::string key = bank_member_key(expression.base, expression.field);
  const auto field_it =
      std::find_if(program.state.begin(), program.state.end(), [&](const V2StateField& field) {
        return field.bank.has_value() && state_bank_key(field) == key;
      });
  return field_it == program.state.end() ? nullptr : &*field_it;
}

std::vector<std::string> storage_names_for_state_field(const V2StateField& field) {
  if (!field.bank.has_value())
    return {field.name};
  std::vector<std::string> names;
  for (int index = field.bank->min; index <= field.bank->max; ++index)
    names.push_back(state_bank_element_name(field, index));
  return names;
}

std::vector<std::string> indexed_state_storage_names(const V2Program& program,
                                                     const Expression& expression) {
  const V2StateField* field = state_bank_field_for_expression(program, expression);
  if (field == nullptr || expression.index == nullptr)
    return {};
  if (const std::optional<int> index = numeric_index_value(*expression.index)) {
    if (*index < field->bank->min || *index > field->bank->max)
      return {};
    return {state_bank_element_name(*field, *index)};
  }
  return storage_names_for_state_field(*field);
}

void collect_indexed_reads(const V2Program& program, const Expression& expression,
                           std::set<std::string>& out) {
  if (expression.kind == "indexed") {
    if (expression.index != nullptr)
      collect_indexed_reads(program, *expression.index, out);
    add_all(out, indexed_state_storage_names(program, expression));
    return;
  }
  if (expression.kind == "unary" && expression.expr != nullptr) {
    collect_indexed_reads(program, *expression.expr, out);
    return;
  }
  if (expression.kind == "binary") {
    if (expression.left != nullptr)
      collect_indexed_reads(program, *expression.left, out);
    if (expression.right != nullptr)
      collect_indexed_reads(program, *expression.right, out);
    return;
  }
  if (expression.kind == "call") {
    for (const Expression& arg : expression.args)
      collect_indexed_reads(program, arg, out);
  }
}

std::vector<std::string> expression_uses(const V2Program& program, const Expression& expression) {
  std::set<std::string> uses;
  collect_expression_vars(expression, uses);
  collect_indexed_reads(program, expression, uses);
  return sorted_vector(uses);
}

std::vector<std::string> expression_text_uses(const V2Program& program, const std::string& text,
                                              int line) {
  const std::optional<Expression> expression = parse_expression_safe(text, line);
  return expression.has_value() ? expression_uses(program, *expression)
                                : std::vector<std::string>{};
}

void collect_expression_call_names(const Expression& expression, std::vector<std::string>& out) {
  if (expression.kind == "indexed") {
    if (expression.index != nullptr)
      collect_expression_call_names(*expression.index, out);
    return;
  }
  if (expression.kind == "unary" && expression.expr != nullptr) {
    collect_expression_call_names(*expression.expr, out);
    return;
  }
  if (expression.kind == "binary") {
    if (expression.left != nullptr)
      collect_expression_call_names(*expression.left, out);
    if (expression.right != nullptr)
      collect_expression_call_names(*expression.right, out);
    return;
  }
  if (expression.kind == "call") {
    for (const Expression& arg : expression.args)
      collect_expression_call_names(arg, out);
    out.push_back(expression.callee);
  }
}

std::vector<std::string> expression_call_names(const Expression& expression) {
  std::vector<std::string> out;
  collect_expression_call_names(expression, out);
  return out;
}

std::vector<std::string> display_item_uses(const V2Program& program, const DisplayItem& item) {
  if (item.expr.has_value())
    return expression_uses(program, *item.expr);
  if (item.kind == "source" && !item.name.empty())
    return {item.name};
  return {};
}

std::vector<std::string> display_uses(const V2Program& program, const V2Statement& statement) {
  std::set<std::string> uses;
  if (statement.target.has_value())
    add_all(uses, expression_text_uses(program, *statement.target, statement.line));
  if (statement.expr.has_value())
    add_all(uses, expression_text_uses(program, *statement.expr, statement.line));
  if (statement.items.has_value()) {
    for (const DisplayItem& item : *statement.items)
      add_all(uses, display_item_uses(program, item));
  }
  return sorted_vector(uses);
}

std::vector<std::string> predicate_uses(const V2Program& program, const V2Predicate& predicate,
                                        int line) {
  std::set<std::string> uses;
  if (predicate.kind == "v2_compare") {
    add_all(uses, expression_text_uses(program, predicate.left, line));
    add_all(uses, expression_text_uses(program, predicate.right, line));
  } else if (predicate.kind == "v2_contains") {
    add_all(uses, expression_text_uses(program, predicate.collection, line));
    add_all(uses, expression_text_uses(program, predicate.item, line));
  }
  return sorted_vector(uses);
}

struct Fragment {
  int entry = 0;
  std::vector<int> exits;
};

class Builder {
public:
  explicit Builder(V2Program& program) : program_(program) {}

  RuleCfg build() {
    routine_entry_[kMainRoutineName] = add(RuleCfgNode{});
    routine_exit_[kMainRoutineName] = add(RuleCfgNode{});
    for (V2Rule& rule : program_.rules) {
      routine_entry_[rule.name] = add(RuleCfgNode{});
      routine_exit_[rule.name] = add(RuleCfgNode{});
    }

    Fragment main = build_sequence(program_.body);
    link({routine_entry_.at(kMainRoutineName)}, main.entry);
    link(main.exits, routine_exit_.at(kMainRoutineName));

    for (V2Rule& rule : program_.rules) {
      Fragment body = build_sequence(rule.body);
      link({routine_entry_.at(rule.name)}, body.entry);
      link(body.exits, routine_exit_.at(rule.name));
    }

    return RuleCfg{
        .nodes = std::move(nodes_),
        .entry_node = routine_entry_.at(kMainRoutineName),
        .routine_entry = std::move(routine_entry_),
        .routine_exit = std::move(routine_exit_),
    };
  }

private:
  int add(RuleCfgNode node) {
    const int id = static_cast<int>(nodes_.size());
    node.id = id;
    nodes_.push_back(std::move(node));
    return id;
  }

  void link(const std::vector<int>& exits, int target) {
    for (const int exit : exits)
      add_unique_successor(nodes_.at(static_cast<std::size_t>(exit)).succ, target);
  }

  Fragment build_sequence(std::vector<V2Statement>& statements) {
    std::optional<int> entry;
    std::vector<int> pending;
    for (V2Statement& statement : statements) {
      Fragment fragment = build_statement(statement);
      if (!entry.has_value())
        entry = fragment.entry;
      else
        link(pending, fragment.entry);
      pending = std::move(fragment.exits);
    }
    if (!entry.has_value()) {
      const int nop = add(RuleCfgNode{});
      return Fragment{.entry = nop, .exits = {nop}};
    }
    return Fragment{.entry = *entry, .exits = pending};
  }

  Fragment build_statement(V2Statement& statement) {
    if (statement.kind == "v2_assign")
      return build_assign(statement);
    if (statement.kind == "v2_update")
      return build_update(statement);
    if (statement.kind == "v2_read" && statement.target.has_value()) {
      const int node = add(RuleCfgNode{.defs = {*statement.target}});
      return Fragment{.entry = node, .exits = {node}};
    }
    if (statement.kind == "v2_show" || statement.kind == "v2_stop" ||
        statement.kind == "v2_preview" || statement.kind == "v2_return") {
      return build_expression_call_statement(
          statement, RuleCfgNode{.uses = display_uses(program_, statement)});
    }
    if (statement.kind == "v2_invoke")
      return build_invoke(statement);
    if (statement.kind == "v2_raw") {
      const int node = add(RuleCfgNode{.barrier = true});
      return Fragment{.entry = node, .exits = {node}};
    }
    if (statement.kind == "v2_if")
      return build_if(statement);
    if (statement.kind == "v2_match")
      return build_match(statement);
    if (statement.kind == "v2_loop") {
      Fragment body = build_sequence(statement.body);
      link(body.exits, body.entry);
      return Fragment{.entry = body.entry, .exits = body.exits};
    }
    if (statement.kind == "v2_while")
      return build_while(statement);
    if (statement.kind == "v2_block")
      return build_sequence(statement.body);

    const int node = add(RuleCfgNode{.barrier = true});
    return Fragment{.entry = node, .exits = {node}};
  }

  Fragment build_assign(V2Statement& statement) {
    if (!statement.target.has_value() || !statement.expr.has_value()) {
      const int node = add(RuleCfgNode{.barrier = true});
      return Fragment{.entry = node, .exits = {node}};
    }
    const std::optional<Expression> target =
        parse_expression_safe(*statement.target, statement.line);
    const std::optional<Expression> expr = parse_expression_safe(*statement.expr, statement.line);
    if (!target.has_value() || !expr.has_value()) {
      const int node = add(RuleCfgNode{.barrier = true});
      return Fragment{.entry = node, .exits = {node}};
    }
    if (target->kind == "identifier") {
      return build_expression_call_fragment(*expr,
                                            RuleCfgNode{.defs = {target->name},
                                                        .uses = expression_uses(program_, *expr),
                             .assign = &statement});
    }
    if (target->kind == "indexed") {
      std::set<std::string> uses;
      add_all(uses, expression_uses(program_, *expr));
      if (target->index != nullptr)
        collect_expression_vars(*target->index, uses);
      const int node = add(RuleCfgNode{
          .defs = indexed_state_storage_names(program_, *target),
          .uses = sorted_vector(uses),
      });
      return Fragment{.entry = node, .exits = {node}};
    }
    const int node = add(RuleCfgNode{.barrier = true});
    return Fragment{.entry = node, .exits = {node}};
  }

  Fragment build_update(V2Statement& statement) {
    if (!statement.target.has_value() || !statement.expr.has_value()) {
      const int node = add(RuleCfgNode{.barrier = true});
      return Fragment{.entry = node, .exits = {node}};
    }
    const std::optional<Expression> target =
        parse_expression_safe(*statement.target, statement.line);
    std::set<std::string> uses;
    add_all(uses, expression_text_uses(program_, *statement.expr, statement.line));
    std::vector<std::string> defs;
    if (target.has_value() && target->kind == "identifier") {
      defs = {target->name};
      uses.insert(target->name);
    } else if (target.has_value() && target->kind == "indexed") {
      defs = indexed_state_storage_names(program_, *target);
      uses.insert(defs.begin(), defs.end());
      if (target->index != nullptr)
        collect_expression_vars(*target->index, uses);
    } else {
      const int node = add(RuleCfgNode{.barrier = true});
      return Fragment{.entry = node, .exits = {node}};
    }
    const int node = add(RuleCfgNode{.defs = defs, .uses = sorted_vector(uses)});
    return Fragment{.entry = node, .exits = {node}};
  }

  Fragment build_expression_call_statement(V2Statement& statement, RuleCfgNode final_node) {
    if (statement.target.has_value()) {
      const std::optional<Expression> target =
          parse_expression_safe(*statement.target, statement.line);
      if (target.has_value())
        return build_expression_call_fragment(*target, std::move(final_node));
    }
    if (statement.expr.has_value()) {
      const std::optional<Expression> expr = parse_expression_safe(*statement.expr, statement.line);
      if (expr.has_value())
        return build_expression_call_fragment(*expr, std::move(final_node));
    }
    return add_plain(std::move(final_node));
  }

  Fragment build_expression_call_fragment(const Expression& expression, RuleCfgNode final_node) {
    const std::vector<std::string> calls = expression_call_names(expression);
    if (calls.empty())
      return add_plain(std::move(final_node));

    const int entry = add(RuleCfgNode{.uses = expression_uses(program_, expression)});
    std::vector<int> exits{entry};
    for (const std::string& call : calls) {
      const auto callee_entry = routine_entry_.find(call);
      const auto callee_exit = routine_exit_.find(call);
      if (callee_entry == routine_entry_.end() || callee_exit == routine_exit_.end()) {
        const int barrier = add(RuleCfgNode{.barrier = true});
        link(exits, barrier);
        exits = {barrier};
        continue;
      }
      const int call_node = add(RuleCfgNode{.succ = {callee_entry->second}});
      link(exits, call_node);
      exits = {callee_exit->second};
    }

    const int final = add(std::move(final_node));
    link(exits, final);
    return Fragment{.entry = entry, .exits = {final}};
  }

  Fragment build_invoke(V2Statement& statement) {
    if (!statement.name.has_value()) {
      const int node = add(RuleCfgNode{.barrier = true});
      return Fragment{.entry = node, .exits = {node}};
    }
    const auto callee_entry = routine_entry_.find(*statement.name);
    const auto callee_exit = routine_exit_.find(*statement.name);
    if (callee_entry == routine_entry_.end() || callee_exit == routine_exit_.end()) {
      const int node = add(RuleCfgNode{.barrier = true});
      return Fragment{.entry = node, .exits = {node}};
    }
    std::vector<int> exits;
    std::optional<int> entry;
    if (!statement.args.empty()) {
      const V2Rule* rule = nullptr;
      for (const V2Rule& candidate : program_.rules) {
        if (candidate.name == *statement.name) {
          rule = &candidate;
          break;
        }
      }
      if (rule == nullptr || rule->params.size() != statement.args.size()) {
        const int node = add(RuleCfgNode{.barrier = true});
        return Fragment{.entry = node, .exits = {node}};
      }

      std::set<std::string> uses;
      bool has_call_arg = false;
      for (const std::string& arg : statement.args) {
        const std::optional<Expression> expression = parse_expression_safe(arg, statement.line);
        if (!expression.has_value()) {
          const int node = add(RuleCfgNode{.barrier = true});
          return Fragment{.entry = node, .exits = {node}};
        }
        add_all(uses, expression_uses(program_, *expression));
        if (!expression_call_names(*expression).empty())
          has_call_arg = true;
      }
      if (has_call_arg) {
        const int barrier = add(RuleCfgNode{.barrier = true});
        entry = barrier;
        exits = {barrier};
      }
      const int args_node =
          add(RuleCfgNode{.defs = rule->params, .uses = sorted_vector(uses)});
      if (!entry.has_value())
        entry = args_node;
      else
        link(exits, args_node);
      exits = {args_node};
    }
    const int node = add(RuleCfgNode{.succ = {callee_entry->second}});
    if (!entry.has_value())
      entry = node;
    else
      link(exits, node);
    return Fragment{.entry = *entry, .exits = {callee_exit->second}};
  }

  Fragment build_if(V2Statement& statement) {
    const int test =
        add(RuleCfgNode{.uses = statement.predicate.has_value()
                                    ? predicate_uses(program_, *statement.predicate, statement.line)
                                    : std::vector<std::string>{}});
    Fragment then_fragment = build_sequence(statement.then_body);
    link({test}, then_fragment.entry);
    std::vector<int> exits = then_fragment.exits;
    if (statement.has_else_body || !statement.else_body.empty()) {
      Fragment else_fragment = build_sequence(statement.else_body);
      link({test}, else_fragment.entry);
      exits.insert(exits.end(), else_fragment.exits.begin(), else_fragment.exits.end());
    } else {
      exits.push_back(test);
    }
    return Fragment{.entry = test, .exits = exits};
  }

  Fragment build_match(V2Statement& statement) {
    std::set<std::string> uses;
    if (statement.expr.has_value())
      add_all(uses, expression_text_uses(program_, *statement.expr, statement.line));
    for (const V2MatchCase& match_case : statement.cases) {
      for (const std::string& value : match_case.values)
        add_all(uses, expression_text_uses(program_, value, match_case.line));
    }
    const int test = add(RuleCfgNode{.uses = sorted_vector(uses)});
    std::vector<int> exits;
    for (const V2MatchCase& match_case : statement.cases) {
      std::vector<V2Statement> empty;
      Fragment action =
          match_case.action == nullptr ? build_sequence(empty) : build_statement(*match_case.action);
      link({test}, action.entry);
      exits.insert(exits.end(), action.exits.begin(), action.exits.end());
    }
    if (statement.otherwise != nullptr) {
      Fragment action = build_statement(*statement.otherwise);
      link({test}, action.entry);
      exits.insert(exits.end(), action.exits.begin(), action.exits.end());
    } else {
      exits.push_back(test);
    }
    return Fragment{.entry = test, .exits = exits};
  }

  Fragment build_while(V2Statement& statement) {
    const int test =
        add(RuleCfgNode{.uses = statement.predicate.has_value()
                                    ? predicate_uses(program_, *statement.predicate, statement.line)
                                    : std::vector<std::string>{}});
    Fragment body = build_sequence(statement.body);
    link({test}, body.entry);
    link(body.exits, test);
    return Fragment{.entry = test, .exits = {test}};
  }

  Fragment add_plain(RuleCfgNode node) {
    const int id = add(std::move(node));
    return Fragment{.entry = id, .exits = {id}};
  }

  V2Program& program_;
  std::vector<RuleCfgNode> nodes_;
  std::map<std::string, int> routine_entry_;
  std::map<std::string, int> routine_exit_;
};

} // namespace

void collect_expression_vars(const Expression& expression, std::set<std::string>& out) {
  if (expression.kind == "identifier") {
    out.insert(expression.name);
    return;
  }
  if (expression.kind == "indexed") {
    if (expression.index != nullptr)
      collect_expression_vars(*expression.index, out);
    return;
  }
  if (expression.kind == "unary" && expression.expr != nullptr) {
    collect_expression_vars(*expression.expr, out);
    return;
  }
  if (expression.kind == "binary") {
    if (expression.left != nullptr)
      collect_expression_vars(*expression.left, out);
    if (expression.right != nullptr)
      collect_expression_vars(*expression.right, out);
    return;
  }
  if (expression.kind == "call") {
    for (const Expression& arg : expression.args)
      collect_expression_vars(arg, out);
  }
}

std::vector<std::string> expression_vars(const Expression& expression) {
  std::set<std::string> out;
  collect_expression_vars(expression, out);
  return sorted_vector(out);
}

bool expression_is_call_free(const Expression& expression) {
  if (expression.kind == "call")
    return false;
  if (expression.kind == "indexed")
    return expression.index == nullptr || expression_is_call_free(*expression.index);
  if (expression.kind == "unary")
    return expression.expr != nullptr && expression_is_call_free(*expression.expr);
  if (expression.kind == "binary") {
    return expression.left != nullptr && expression_is_call_free(*expression.left) &&
           expression.right != nullptr && expression_is_call_free(*expression.right);
  }
  return true;
}

RuleCfg build_rule_cfg(V2Program& program) {
  return Builder(program).build();
}

std::set<std::string> program_state_fields(const V2Program& program) {
  std::set<std::string> fields;
  for (const V2StateField& field : program.state) {
    const std::vector<std::string> names = storage_names_for_state_field(field);
    fields.insert(names.begin(), names.end());
  }
  return fields;
}

} // namespace mkpro::core
