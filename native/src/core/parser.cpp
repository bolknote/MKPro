#include "mkpro/core/parser.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>

namespace mkpro {

namespace {

struct SourceLine {
  std::string text;
  int line = 0;
};

const std::unordered_set<std::string> kReservedRuleNames = {
    "else", "fn",    "halt",   "if",     "loop", "match", "otherwise",
    "program", "read", "requires", "return", "show", "state",
};

std::string trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  return value;
}

bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

std::string strip_comment(const std::string& text) {
  bool quoted = false;
  bool escaped = false;
  for (std::size_t index = 0; index < text.size(); ++index) {
    const char ch = text.at(index);
    if (escaped) {
      escaped = false;
      continue;
    }
    if (quoted && ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      quoted = !quoted;
      continue;
    }
    if (!quoted && ch == '#')
      return text.substr(0, index);
    if (!quoted && ch == '/' && index + 1 < text.size() && text.at(index + 1) == '/') {
      return text.substr(0, index);
    }
  }
  return text;
}

std::vector<std::string> tokenize_inline_grammar(const std::string& text) {
  std::vector<std::string> tokens;
  std::string token;
  int paren_depth = 0;
  int bracket_depth = 0;
  bool quoted = false;
  bool escaped = false;

  for (char ch : text) {
    if (escaped) {
      token.push_back(ch);
      escaped = false;
      continue;
    }
    if (quoted) {
      token.push_back(ch);
      if (ch == '\\')
        escaped = true;
      else if (ch == '"')
        quoted = false;
      continue;
    }
    if (ch == '"') {
      quoted = true;
      token.push_back(ch);
      continue;
    }
    if (ch == '(') {
      ++paren_depth;
      token.push_back(ch);
      continue;
    }
    if (ch == ')' && paren_depth > 0) {
      --paren_depth;
      token.push_back(ch);
      continue;
    }
    if (ch == '[') {
      ++bracket_depth;
      token.push_back(ch);
      continue;
    }
    if (ch == ']' && bracket_depth > 0) {
      --bracket_depth;
      token.push_back(ch);
      continue;
    }
    if (ch == ';' && paren_depth == 0 && bracket_depth == 0) {
      const std::string trimmed = trim(token);
      if (!trimmed.empty())
        tokens.push_back(trimmed);
      token.clear();
      continue;
    }
    if ((ch == '{' || ch == '}') && paren_depth == 0 && bracket_depth == 0) {
      const std::string trimmed = trim(token);
      if (!trimmed.empty())
        tokens.push_back(trimmed);
      token.clear();
      tokens.emplace_back(1, ch);
      continue;
    }
    token.push_back(ch);
  }

  const std::string trimmed = trim(token);
  if (!trimmed.empty())
    tokens.push_back(trimmed);
  return tokens;
}

std::vector<std::string> tokenize_inline_blocks(const std::string& text) {
  const std::vector<std::string> raw_tokens = tokenize_inline_grammar(text);
  std::vector<std::string> lines;
  std::string pending;

  for (const auto& token : raw_tokens) {
    if (token == "{") {
      if (!trim(pending).empty())
        lines.push_back(trim(pending) + " {");
      else
        lines.push_back("{");
      pending.clear();
      continue;
    }
    if (token == "}") {
      const std::string body = trim(pending);
      if (!body.empty())
        lines.push_back(body);
      lines.push_back("}");
      pending.clear();
      continue;
    }
    if (!pending.empty())
      pending.push_back(' ');
    pending += token;
  }
  if (!trim(pending).empty())
    lines.push_back(trim(pending));
  return lines;
}

std::vector<SourceLine> normalize_source_line(const std::string& text, int line) {
  const std::string stripped = trim(strip_comment(text));
  if (stripped.empty())
    return {};
  std::vector<SourceLine> result;
  for (const auto& line_text : tokenize_inline_blocks(stripped)) {
    result.push_back(SourceLine{.text = line_text, .line = line});
  }
  return result;
}

std::vector<SourceLine> normalize_source(const std::string& source) {
  std::vector<SourceLine> result;
  std::istringstream input(source);
  std::string line;
  int line_number = 1;
  while (std::getline(input, line)) {
    auto lines = normalize_source_line(line, line_number);
    result.insert(result.end(), lines.begin(), lines.end());
    ++line_number;
  }
  return result;
}

std::vector<std::string> split_args(const std::string& text) {
  std::vector<std::string> args;
  std::size_t start = 0;
  int depth = 0;
  int bracket_depth = 0;
  bool quote = false;
  bool escaped = false;
  for (std::size_t index = 0; index < text.size(); ++index) {
    const char ch = text.at(index);
    if (quote) {
      if (escaped)
        escaped = false;
      else if (ch == '\\')
        escaped = true;
      else if (ch == '"')
        quote = false;
      continue;
    }
    if (ch == '"') {
      quote = true;
      continue;
    }
    if (ch == '(')
      ++depth;
    else if (ch == ')' && depth > 0)
      --depth;
    else if (ch == '[')
      ++bracket_depth;
    else if (ch == ']' && bracket_depth > 0)
      --bracket_depth;
    else if (ch == ',' && depth == 0 && bracket_depth == 0) {
      const std::string part = trim(text.substr(start, index - start));
      if (!part.empty())
        args.push_back(part);
      start = index + 1;
    }
  }
  const std::string part = trim(text.substr(start));
  if (!part.empty())
    args.push_back(part);
  return args;
}

bool is_identifier_start(unsigned char ch) {
  return std::isalpha(ch) != 0 || ch == '_' || ch >= 0x80;
}

bool is_identifier_continue(unsigned char ch) {
  return std::isalnum(ch) != 0 || ch == '_' || ch >= 0x80;
}

bool is_identifier_text(const std::string& text) {
  if (text.empty() || !is_identifier_start(static_cast<unsigned char>(text.front())))
    return false;
  return std::all_of(text.begin() + 1, text.end(), [](char ch) {
    return is_identifier_continue(static_cast<unsigned char>(ch));
  });
}

bool is_numeric_literal_text(const std::string& text) {
  static const std::regex pattern(R"(^-?\d+(?:\.\d+)?(?:e[+-]?\d+)?$)", std::regex::icase);
  return std::regex_match(trim(text), pattern);
}

std::optional<std::pair<std::string, std::string>> parse_call(const std::string& text) {
  std::size_t index = 0;
  while (index < text.size() && std::isspace(static_cast<unsigned char>(text.at(index))) != 0) {
    ++index;
  }
  const std::size_t name_start = index;
  if (index >= text.size() || !is_identifier_start(static_cast<unsigned char>(text.at(index)))) {
    return std::nullopt;
  }
  while (index < text.size() &&
         is_identifier_continue(static_cast<unsigned char>(text.at(index)))) {
    ++index;
  }
  const std::string name = text.substr(name_start, index - name_start);
  while (index < text.size() && std::isspace(static_cast<unsigned char>(text.at(index))) != 0) {
    ++index;
  }
  if (index >= text.size() || text.at(index) != '(')
    return std::nullopt;
  const std::size_t open = index;
  int depth = 0;
  bool quote = false;
  bool escaped = false;
  for (; index < text.size(); ++index) {
    const char ch = text.at(index);
    if (quote) {
      if (escaped)
        escaped = false;
      else if (ch == '\\')
        escaped = true;
      else if (ch == '"')
        quote = false;
      continue;
    }
    if (ch == '"') {
      quote = true;
      continue;
    }
    if (ch == '(') {
      ++depth;
      continue;
    }
    if (ch != ')')
      continue;
    --depth;
    if (depth == 0) {
      if (!trim(text.substr(index + 1)).empty())
        return std::nullopt;
      return std::pair{name, text.substr(open + 1, index - open - 1)};
    }
    if (depth < 0)
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<std::pair<std::string, std::string>> parse_named_call(const std::string& text,
                                                                    const std::string& name) {
  auto call = parse_call(text);
  if (!call.has_value() || call->first != name)
    return std::nullopt;
  return call;
}

std::string parse_quoted_text(const std::string& text, int line, std::string_view context) {
  if (text.size() < 2 || text.front() != '"' || text.back() != '"') {
    throw ParseError("Invalid " + std::string(context) + " string literal '" + text + "'", line);
  }
  std::string result;
  bool escaped = false;
  for (std::size_t index = 1; index + 1 < text.size(); ++index) {
    const char ch = text.at(index);
    if (escaped) {
      switch (ch) {
      case '"':
      case '\\':
        result.push_back(ch);
        break;
      case 'n':
        result.push_back('\n');
        break;
      case 'r':
        result.push_back('\r');
        break;
      case 't':
        result.push_back('\t');
        break;
      default:
        result.push_back(ch);
        break;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    result.push_back(ch);
  }
  if (escaped)
    throw ParseError("Invalid " + std::string(context) + " string literal '" + text + "'", line);
  return result;
}

std::vector<std::string> parse_identifier_list(const std::string& text) {
  std::vector<std::string> values;
  for (const auto& part : split_args(text)) {
    const std::string value = trim(part);
    if (!value.empty())
      values.push_back(value);
  }
  return values;
}

std::vector<std::string> parse_comma_identifier_list(const std::string& text, int line) {
  const std::string trimmed = trim(text);
  if (trimmed.empty())
    return {};
  std::vector<std::string> params;
  std::size_t start = 0;
  for (std::size_t index = 0; index <= text.size(); ++index) {
    if (index != text.size() && text.at(index) != ',')
      continue;
    std::string part = trim(text.substr(start, index - start));
    if (part.empty())
      throw ParseError("Function parameters must be comma-separated identifiers", line);
    params.push_back(std::move(part));
    start = index + 1;
  }
  return params;
}

std::vector<std::string> tokenize_expression(const std::string& source, int line) {
  std::vector<std::string> tokens;
  std::size_t index = 0;
  while (index < source.size()) {
    while (index < source.size() &&
           std::isspace(static_cast<unsigned char>(source.at(index))) != 0) {
      ++index;
    }
    if (index >= source.size())
      break;
    const char ch = source.at(index);
    if (ch == '"') {
      const std::size_t start = index++;
      bool escaped = false;
      bool closed = false;
      while (index < source.size()) {
        const char current = source.at(index++);
        if (escaped) {
          escaped = false;
        } else if (current == '\\') {
          escaped = true;
        } else if (current == '"') {
          closed = true;
          break;
        }
      }
      if (!closed)
        throw ParseError("Unclosed expression string literal", line);
      tokens.push_back(source.substr(start, index - start));
      continue;
    }
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (is_identifier_start(uch)) {
      const std::size_t start = index++;
      while (index < source.size() &&
             is_identifier_continue(static_cast<unsigned char>(source.at(index)))) {
        ++index;
      }
      tokens.push_back(source.substr(start, index - start));
      continue;
    }
    if (std::isdigit(uch) != 0) {
      const std::size_t start = index++;
      while (index < source.size() &&
             std::isdigit(static_cast<unsigned char>(source.at(index))) != 0) {
        ++index;
      }
      if (index < source.size() && source.at(index) == '.') {
        ++index;
        while (index < source.size() &&
               std::isdigit(static_cast<unsigned char>(source.at(index))) != 0) {
          ++index;
        }
      }
      if (index < source.size() && (source.at(index) == 'e' || source.at(index) == 'E')) {
        const std::size_t exponent = index++;
        if (index < source.size() && (source.at(index) == '+' || source.at(index) == '-'))
          ++index;
        const std::size_t digits = index;
        while (index < source.size() &&
               std::isdigit(static_cast<unsigned char>(source.at(index))) != 0) {
          ++index;
        }
        if (digits == index)
          index = exponent;
      }
      tokens.push_back(source.substr(start, index - start));
      continue;
    }
    if (index + 1 < source.size()) {
      const std::string two = source.substr(index, 2);
      if (two == "==" || two == "!=" || two == "<=" || two == ">=") {
        tokens.push_back(two);
        index += 2;
        continue;
      }
    }
    if (std::string("()[].+-*/,").find(ch) != std::string::npos) {
      tokens.emplace_back(1, ch);
      ++index;
      continue;
    }
    throw ParseError("Cannot tokenize expression near '" + source.substr(index) + "'", line);
  }
  return tokens;
}

class ExpressionParser {
public:
  ExpressionParser(std::string source, int line)
      : source_(std::move(source)), line_(line), tokens_(tokenize_expression(source_, line_)) {}

  Expression parse() {
    Expression expr = parse_additive();
    if (!done()) {
      throw ParseError("Unexpected token '" + peek() + "' in expression '" + source_ + "'", line_);
    }
    return expr;
  }

private:
  Expression parse_additive() {
    Expression left = parse_multiplicative();
    while (peek_optional() == "+" || peek_optional() == "-") {
      const std::string op = next();
      Expression result;
      result.kind = "binary";
      result.op = op;
      result.left = std::make_shared<Expression>(std::move(left));
      result.right = std::make_shared<Expression>(parse_multiplicative());
      left = std::move(result);
    }
    return left;
  }

  Expression parse_multiplicative() {
    Expression left = parse_unary();
    while (peek_optional() == "*" || peek_optional() == "/") {
      const std::string op = next();
      Expression result;
      result.kind = "binary";
      result.op = op;
      result.left = std::make_shared<Expression>(std::move(left));
      result.right = std::make_shared<Expression>(parse_unary());
      left = std::move(result);
    }
    return left;
  }

  Expression parse_unary() {
    if (peek_optional() == "-") {
      (void)next();
      Expression result;
      result.kind = "unary";
      result.op = "-";
      result.expr = std::make_shared<Expression>(parse_unary());
      return result;
    }
    return parse_primary();
  }

  Expression parse_primary() {
    const std::string token = next();
    if (token == "(") {
      Expression expr = parse_additive();
      expect(")");
      return expr;
    }
    if (starts_with(token, "\"")) {
      Expression expr;
      expr.kind = "string";
      expr.text = parse_quoted_text(token, line_, "expression");
      return expr;
    }
    if (is_numeric_literal_text(token) && token.front() != '-') {
      Expression expr;
      expr.kind = "number";
      expr.raw = token;
      return expr;
    }
    if (is_identifier_text(token)) {
      if (peek_optional() == "(") {
        (void)next();
        Expression expr;
        expr.kind = "call";
        expr.callee = token;
        if (peek_optional() != ")") {
          do {
            expr.args.push_back(parse_additive());
            if (peek_optional() != ",")
              break;
            (void)next();
          } while (!done());
        }
        expect(")");
        return expr;
      }
      if (peek_optional() == "[") {
        (void)next();
        auto index = std::make_shared<Expression>(parse_additive());
        expect("]");
        Expression expr;
        expr.kind = "indexed";
        expr.base = token;
        expr.index = std::move(index);
        if (peek_optional() == ".") {
          (void)next();
          const std::string member = next();
          if (!is_identifier_text(member)) {
            throw ParseError("Expected indexed field name, got '" + member + "' in expression '" +
                                 source_ + "'",
                             line_);
          }
          expr.field = member;
        }
        return expr;
      }
      Expression expr;
      expr.kind = "identifier";
      expr.name = token;
      return expr;
    }
    throw ParseError("Unexpected token '" + token + "' in expression '" + source_ + "'", line_);
  }

  void expect(const std::string& token) {
    const std::string actual = next();
    if (actual != token) {
      throw ParseError(
          "Expected '" + token + "', got '" + actual + "' in expression '" + source_ + "'", line_);
    }
  }

  [[nodiscard]] bool done() const {
    return index_ >= tokens_.size();
  }

  std::string peek() const {
    if (done())
      throw ParseError("Unexpected end of expression '" + source_ + "'", line_);
    return tokens_.at(index_);
  }

  std::string peek_optional() const {
    return done() ? std::string{} : tokens_.at(index_);
  }

  std::string next() {
    const std::string token = peek();
    ++index_;
    return token;
  }

  std::string source_;
  int line_;
  std::vector<std::string> tokens_;
  std::size_t index_ = 0;
};

struct DisplayToken {
  std::string kind;
  std::string text;
};

std::vector<DisplayToken> tokenize_display_items(const std::string& text, int line) {
  std::vector<DisplayToken> tokens;
  std::size_t index = 0;
  while (index < text.size()) {
    const char ch = text.at(index);
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      ++index;
      continue;
    }
    if (ch == ',') {
      tokens.push_back(DisplayToken{.kind = "comma"});
      ++index;
      continue;
    }
    if (ch == '"') {
      const std::size_t start = index++;
      bool escaped = false;
      bool closed = false;
      while (index < text.size()) {
        const char current = text.at(index++);
        if (escaped)
          escaped = false;
        else if (current == '\\')
          escaped = true;
        else if (current == '"') {
          closed = true;
          break;
        }
      }
      if (!closed)
        throw ParseError("Unclosed display string literal", line);
      tokens.push_back(DisplayToken{.kind = "item", .text = text.substr(start, index - start)});
      continue;
    }
    const std::size_t start = index;
    int depth = 0;
    while (index < text.size()) {
      const char current = text.at(index);
      if (current == '(')
        ++depth;
      else if (current == ')' && depth > 0)
        --depth;
      else if (current == ',' && depth == 0)
        break;
      ++index;
    }
    tokens.push_back(DisplayToken{.kind = "item", .text = trim(text.substr(start, index - start))});
  }
  return tokens;
}

DisplayItem parse_display_item(const std::string& text, int line) {
  const std::string trimmed = trim(text);
  if (starts_with(trimmed, "\"")) {
    return DisplayItem{
        .kind = "literal",
        .text = parse_quoted_text(trimmed, line, "display"),
        .line = line,
    };
  }
  if (std::regex_match(trimmed, std::regex(R"(^\d+$)"))) {
    return DisplayItem{.kind = "literal", .text = trimmed, .line = line};
  }

  static const std::regex source_regex(R"(^([A-Za-z_][A-Za-z0-9_]*)(?::(0?)(\d+))?$)");
  std::smatch source_match;
  if (std::regex_match(trimmed, source_match, source_regex)) {
    DisplayItem item{
        .kind = "source",
        .name = source_match[1].str(),
        .line = line,
    };
    if (source_match[3].matched) {
      const int width = std::stoi(source_match[3].str());
      if (width <= 0 || width > 8) {
        throw ParseError("Display width must be 1..8, got '" + source_match[3].str() + "'", line);
      }
      item.width = width;
      item.pad = source_match[2].str() == "0" ? "zero" : "space";
    }
    return item;
  }

  if (trimmed.find('"') != std::string::npos) {
    throw ParseError("Display fragments must be separated by commas", line);
  }

  std::string expr_text = trimmed;
  std::optional<std::string> zero;
  std::optional<std::string> width_text;
  const std::size_t colon = trimmed.rfind(':');
  if (colon != std::string::npos) {
    const std::string maybe_width = trimmed.substr(colon + 1);
    if (std::regex_match(maybe_width, std::regex(R"(0?\d+)"))) {
      expr_text = trim(trimmed.substr(0, colon));
      zero = starts_with(maybe_width, "0") ? std::optional<std::string>("0") : std::nullopt;
      width_text = maybe_width;
    }
  }
  DisplayItem item{
      .kind = "source",
      .name = expr_text,
      .expr = parse_expression(expr_text, line),
      .line = line,
  };
  if (width_text.has_value()) {
    const int width = std::stoi(*width_text);
    if (width <= 0 || width > 8) {
      throw ParseError("Display width must be 1..8, got '" + *width_text + "'", line);
    }
    item.width = width;
    item.pad = zero.has_value() ? "zero" : "space";
  }
  return item;
}

std::vector<DisplayItem> parse_display_item_list(const std::string& text, int line) {
  const auto tokens = tokenize_display_items(text, line);
  std::vector<DisplayItem> items;
  bool pending_comma = false;
  bool just_read_item = false;
  for (const auto& token : tokens) {
    if (token.kind == "comma") {
      if (!just_read_item || pending_comma) {
        throw ParseError("Display comma must separate two fragments", line);
      }
      pending_comma = true;
      just_read_item = false;
      continue;
    }
    DisplayItem item = parse_display_item(token.text, line);
    if (pending_comma) {
      pending_comma = false;
    } else if (just_read_item) {
      throw ParseError("Display fragments must be separated by commas", line);
    }
    if (!items.empty() && items.back().kind == "literal" && item.kind == "literal") {
      items.back().text += item.text;
    } else {
      items.push_back(std::move(item));
    }
    just_read_item = true;
  }
  if (pending_comma)
    throw ParseError("Display comma must separate two fragments", line);
  return items;
}

std::vector<std::string> expand_match_case_values(const std::string& left, int line) {
  constexpr int kRangeLimit = 100;
  std::vector<std::string> values;
  for (const auto& part : split_args(left)) {
    static const std::regex range_regex(R"(^(-?\d+)\s*\.\.\s*(-?\d+)$)");
    std::smatch match;
    if (!std::regex_match(part, match, range_regex)) {
      values.push_back(part);
      continue;
    }
    const int min = std::stoi(match[1].str());
    const int max = std::stoi(match[2].str());
    if (min > max)
      throw ParseError("Match range '" + part + "' must be ascending", line);
    if (max - min + 1 > kRangeLimit) {
      throw ParseError("Match range '" + part + "' expands to more than 100 values", line);
    }
    for (int value = min; value <= max; ++value)
      values.push_back(std::to_string(value));
  }
  return values;
}

std::optional<std::size_t> find_assignment_equal(const std::string& text) {
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text.at(index) != '=')
      continue;
    const char prev = index == 0 ? '\0' : text.at(index - 1);
    const char next = index + 1 < text.size() ? text.at(index + 1) : '\0';
    if (prev == '!' || prev == '<' || prev == '>' || prev == '=' || next == '=')
      continue;
    return index;
  }
  return std::nullopt;
}

V2Predicate parse_predicate(const std::string& text) {
  const std::string trimmed = trim(text);
  static const std::regex contains_regex(R"(^(.+?)\s+in\s+([A-Za-z_][A-Za-z0-9_]*)$)");
  std::smatch contains;
  if (std::regex_match(trimmed, contains, contains_regex)) {
    return V2Predicate{
        .kind = "v2_contains",
        .collection = trim(contains[2].str()),
        .item = trim(contains[1].str()),
    };
  }

  static const std::regex compare_regex(R"(^(.+?)\s*(==|!=|<=|>=|<|>)\s*(.+)$)");
  std::smatch compare;
  if (std::regex_match(trimmed, compare, compare_regex)) {
    return V2Predicate{
        .kind = "v2_compare",
        .left = trim(compare[1].str()),
        .op = compare[2].str(),
        .right = trim(compare[3].str()),
    };
  }

  return V2Predicate{
      .kind = "v2_compare",
      .left = trimmed,
      .op = "!=",
      .right = "0",
  };
}

std::string lower_v2_state_field_type(const std::string& type) {
  if (type == "counter")
    return "range";
  if (type == "coord" || type == "cells" || type == "coord_list")
    return "packed";
  return type;
}

StateFieldAst lower_v2_state_field(const V2StateField& field) {
  StateFieldAst lowered;
  lowered.name = field.name;
  lowered.type = lower_v2_state_field_type(field.type);
  lowered.min = field.min;
  lowered.max = field.max;
  lowered.initial_stack = field.initial_stack;
  lowered.implicit = field.implicit;
  lowered.line = field.line;
  if (field.initial.has_value()) {
    try {
      lowered.initial = ExpressionParser(*field.initial, field.line).parse();
    } catch (const ParseError&) {
      // V2 indexed/bulk initializers are lowered through setup-specific paths; the generic
      // state projection must not make otherwise valid V2 programs fail to parse.
    }
  }
  return lowered;
}

std::vector<StateAst> lower_v2_states(const V2Program& program) {
  if (program.state.empty())
    return {};
  StateAst state;
  state.name = program.name;
  state.line = program.line;
  state.fields.reserve(program.state.size());
  for (const V2StateField& field : program.state)
    state.fields.push_back(lower_v2_state_field(field));
  return {std::move(state)};
}

struct V2SpatialRewriteContext {
  const V2Program* program = nullptr;
  std::map<std::string, std::string> state_domains;
  std::map<std::string, std::pair<int, int>> state_ranges;
  std::map<std::string, std::string> world_encodings;
};

std::string lower_ascii_text(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string decimal_ones_expression_text(const std::string& expr) {
  return "(" + expr + ") - 10 * int((" + expr + ") / 10)";
}

bool is_single_decimal_digit_expression(const std::string& expr,
                                        const V2SpatialRewriteContext& context) {
  const auto range = context.state_ranges.find(trim(expr));
  return range != context.state_ranges.end() && range->second.first >= 0 &&
         range->second.second <= 9;
}

std::optional<std::string> single_decimal_player_cells_field(
    const V2Program& program, const std::string& domain,
    const V2SpatialRewriteContext& context) {
  const auto world = context.world_encodings.find(domain);
  if (world == context.world_encodings.end() || world->second != "decimal_player")
    return std::nullopt;
  std::vector<std::string> names;
  for (const V2StateField& field : program.state) {
    if (field.type == "cells" && field.domain.has_value() && *field.domain == domain)
      names.push_back(field.name);
  }
  return names.size() == 1U ? std::optional<std::string>(names.front()) : std::nullopt;
}

std::string cell_map_name_for_domain(const std::string& domain,
                                     const V2SpatialRewriteContext& context) {
  const V2Program& program = *context.program;
  std::optional<std::string> explicit_map;
  std::optional<std::string> domain_specific;
  const std::string lower_domain = lower_ascii_text(domain);
  for (const V2StateField& field : program.state) {
    if (field.type != "packed")
      continue;
    const std::string lower_name = lower_ascii_text(field.name);
    if (!explicit_map.has_value() && (lower_name == "plan" || lower_name == "map"))
      explicit_map = field.name;
    if (!domain_specific.has_value() &&
        (lower_name == lower_domain + "_plan" || lower_name == lower_domain + "_map")) {
      domain_specific = field.name;
    }
  }
  if (domain_specific.has_value())
    return *domain_specific;
  if (explicit_map.has_value())
    return *explicit_map;
  if (const std::optional<std::string> cells =
          single_decimal_player_cells_field(program, domain, context)) {
    return *cells;
  }
  return "__cell_map_" + domain;
}

std::pair<std::optional<std::string>, std::optional<std::string>>
cell_at_domain_and_position(const std::vector<std::string>& args,
                            const V2SpatialRewriteContext& context) {
  if (args.size() == 2U)
    return {trim(args.at(0)), trim(args.at(1))};
  if (args.size() != 1U)
    return {std::nullopt, std::nullopt};
  const std::string pos = trim(args.front());
  const auto domain = context.state_domains.find(pos);
  if (domain == context.state_domains.end())
    return {std::nullopt, std::nullopt};
  return {domain->second, pos};
}

std::string cell_at_index_expression_text(const std::string& pos, const std::string& domain,
                                          const V2SpatialRewriteContext& context) {
  const auto world = context.world_encodings.find(domain);
  const std::string encoding = world == context.world_encodings.end() ? "" : world->second;
  if (encoding == "row_scan" || encoding == "floor_plan" || encoding == "decimal_player" ||
      encoding == "pier_to_ship") {
    if (is_single_decimal_digit_expression(pos, context))
      return pos;
    return decimal_ones_expression_text(pos);
  }
  return pos;
}

std::optional<std::string> cell_at_expression_text(const std::vector<std::string>& args,
                                                   const V2SpatialRewriteContext& context) {
  auto [domain, pos] = cell_at_domain_and_position(args, context);
  if (!domain.has_value() || !pos.has_value())
    return std::nullopt;
  return "digit_at(" + cell_map_name_for_domain(*domain, context) + ", " +
         cell_at_index_expression_text(*pos, *domain, context) + ")";
}

std::optional<std::pair<std::size_t, std::size_t>>
call_span_at(const std::string& text, std::size_t name_pos, const std::string& name) {
  if (name_pos > 0 &&
      is_identifier_continue(static_cast<unsigned char>(text.at(name_pos - 1)))) {
    return std::nullopt;
  }
  std::size_t index = name_pos + name.size();
  if (index < text.size() && is_identifier_continue(static_cast<unsigned char>(text.at(index))))
    return std::nullopt;
  while (index < text.size() && std::isspace(static_cast<unsigned char>(text.at(index))) != 0)
    ++index;
  if (index >= text.size() || text.at(index) != '(')
    return std::nullopt;

  const std::size_t open = index;
  int depth = 0;
  bool quote = false;
  bool escaped = false;
  for (; index < text.size(); ++index) {
    const char ch = text.at(index);
    if (quote) {
      if (escaped)
        escaped = false;
      else if (ch == '\\')
        escaped = true;
      else if (ch == '"')
        quote = false;
      continue;
    }
    if (ch == '"') {
      quote = true;
      continue;
    }
    if (ch == '(') {
      ++depth;
      continue;
    }
    if (ch != ')')
      continue;
    --depth;
    if (depth == 0)
      return std::pair{open, index};
    if (depth < 0)
      return std::nullopt;
  }
  return std::nullopt;
}

std::string rewrite_cell_at_calls(const std::string& text, const V2SpatialRewriteContext& context) {
  static const std::string name = "cell_at";
  std::string result;
  std::size_t search = 0;
  std::size_t copied = 0;
  while (true) {
    const std::size_t pos = text.find(name, search);
    if (pos == std::string::npos)
      break;
    const std::optional<std::pair<std::size_t, std::size_t>> span =
        call_span_at(text, pos, name);
    if (!span.has_value()) {
      search = pos + name.size();
      continue;
    }
    const std::string raw_args = text.substr(span->first + 1, span->second - span->first - 1);
    const std::optional<std::string> replacement =
        cell_at_expression_text(split_args(raw_args), context);
    if (!replacement.has_value()) {
      search = span->second + 1;
      continue;
    }
    result.append(text.substr(copied, pos - copied));
    result.append(*replacement);
    copied = span->second + 1;
    search = copied;
  }
  if (copied == 0)
    return text;
  result.append(text.substr(copied));
  return result;
}

void rewrite_optional_expression_text(std::optional<std::string>& text,
                                      const V2SpatialRewriteContext& context) {
  if (text.has_value())
    *text = rewrite_cell_at_calls(*text, context);
}

void rewrite_display_items(std::vector<DisplayItem>& items,
                           const V2SpatialRewriteContext& context) {
  for (DisplayItem& item : items) {
    if (!item.expr.has_value())
      continue;
    item.name = rewrite_cell_at_calls(item.name, context);
    item.expr = parse_expression(item.name, item.line);
  }
}

void rewrite_statement_spatial_expressions(V2Statement& statement,
                                           const V2SpatialRewriteContext& context) {
  rewrite_optional_expression_text(statement.expr, context);
  if (statement.predicate.has_value()) {
    statement.predicate->left = rewrite_cell_at_calls(statement.predicate->left, context);
    statement.predicate->right = rewrite_cell_at_calls(statement.predicate->right, context);
    statement.predicate->item = rewrite_cell_at_calls(statement.predicate->item, context);
  }
  if (statement.items.has_value())
    rewrite_display_items(*statement.items, context);
  for (std::string& arg : statement.args)
    arg = rewrite_cell_at_calls(arg, context);
  for (V2RawInput& input : statement.inputs)
    input.expr = rewrite_cell_at_calls(input.expr, context);
  for (V2Statement& child : statement.body)
    rewrite_statement_spatial_expressions(child, context);
  for (V2Statement& child : statement.then_body)
    rewrite_statement_spatial_expressions(child, context);
  for (V2Statement& child : statement.else_body)
    rewrite_statement_spatial_expressions(child, context);
  for (V2MatchCase& match_case : statement.cases) {
    if (match_case.action != nullptr)
      rewrite_statement_spatial_expressions(*match_case.action, context);
  }
  if (statement.otherwise != nullptr)
    rewrite_statement_spatial_expressions(*statement.otherwise, context);
}

V2SpatialRewriteContext build_spatial_rewrite_context(const V2Program& program) {
  V2SpatialRewriteContext context;
  context.program = &program;
  for (const V2StateField& field : program.state) {
    if (field.domain.has_value())
      context.state_domains[field.name] = *field.domain;
    if (field.min.has_value() && field.max.has_value())
      context.state_ranges[field.name] = {*field.min, *field.max};
  }
  for (const V2World& world : program.worlds) {
    if (world.position.has_value() && world.position->encoding.has_value())
      context.world_encodings[world.name] = *world.position->encoding;
  }
  return context;
}

void rewrite_v2_spatial_expressions(V2Program& program) {
  const V2SpatialRewriteContext context = build_spatial_rewrite_context(program);
  for (V2Const& constant : program.consts)
    constant.expr = rewrite_cell_at_calls(constant.expr, context);
  for (V2StateField& field : program.state)
    rewrite_optional_expression_text(field.initial, context);
  for (V2Statement& statement : program.body)
    rewrite_statement_spatial_expressions(statement, context);
  for (V2Rule& rule : program.rules) {
    for (V2Statement& statement : rule.body)
      rewrite_statement_spatial_expressions(statement, context);
  }
}

class Parser {
public:
  Parser(std::string source, ParseOptions options)
      : lines_(normalize_source(source)), options_(options) {}

  ProgramAst parse_program() {
    std::optional<std::string> reference;
    std::optional<V2Program> v2;

    while (!done()) {
      const SourceLine line = peek();
      if (line.text == "}")
        throw ParseError("Unexpected closing brace", line.line);
      if (starts_with(line.text, "reference ")) {
        reference = trim(line.text.substr(std::string("reference ").size()));
        ++index_;
        continue;
      }
      if (starts_with(line.text, "program ")) {
        if (v2.has_value())
          throw ParseError("Only one program block is supported", line.line);
        v2 = parse_v2_program();
        continue;
      }
      throw ParseError("Unexpected top-level line '" + line.text + "'", line.line);
    }

    if (!v2.has_value())
      throw ParseError("Program must contain one V2 program block", 1);
    ProgramAst program;
    program.reference = reference;
    program.states = lower_v2_states(*v2);
    program.v2 = std::move(v2);
    return program;
  }

private:
  V2Program parse_v2_program() {
    const SourceLine header = next();
    static const std::regex program_regex(R"(^program\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{$)");
    std::smatch match;
    if (!std::regex_match(header.text, match, program_regex)) {
      throw ParseError("Program must look like 'program Name {'", header.line);
    }
    V2Program program;
    program.name = match[1].str();
    program.line = header.line;

    while (!done()) {
      const SourceLine line = peek();
      if (line.text == "}") {
        ++index_;
        append_implicit_read_state_fields(program);
        collect_inline_screens(program);
        rewrite_v2_spatial_expressions(program);
        return program;
      }
      if (starts_with(line.text, "requires ")) {
        ++index_;
        parse_requirement(line, program);
        continue;
      }
      if (starts_with(line.text, "const ")) {
        ++index_;
        program.consts.push_back(parse_const(line));
        continue;
      }
      if (line.text == "state {") {
        ++index_;
        auto fields = parse_state_block();
        program.state.insert(program.state.end(), fields.begin(), fields.end());
        continue;
      }
      if (auto board = parse_board_declaration(line); board.has_value()) {
        ++index_;
        program.boards.push_back(std::move(*board));
        continue;
      }
      if (auto world = parse_compact_board_declaration(line); world.has_value()) {
        ++index_;
        program.worlds.push_back(std::move(*world));
        continue;
      }
      if (starts_with(line.text, "board ")) {
        throw ParseError("Board must look like 'name: board(0..9, 0..9)'", line.line);
      }
      if (starts_with(line.text, "fn ")) {
        program.rules.push_back(parse_rule(line.text));
        continue;
      }
      const bool known_statement_block =
          std::regex_search(line.text, std::regex(R"(^(match|if|while|loop)\b)")) ||
          line.text == "raw {";
      if (starts_with(line.text, "input ") ||
          (ends_with(line.text, "{") && !known_statement_block)) {
        throw ParseError("Unexpected program line '" + line.text + "'", line.line);
      }
      program.body.push_back(parse_statement());
    }

    throw ParseError("Unclosed program block", header.line);
  }

  void parse_requirement(const SourceLine& line, V2Program& program) {
    static const std::regex angle_regex(R"(^requires\s+angle_mode\(([^)]+)\)$)");
    std::smatch match;
    if (!std::regex_match(line.text, match, angle_regex) || trim(match[1].str()) != "grd") {
      throw ParseError("Program requirement must look like 'requires angle_mode(grd)'", line.line);
    }
    program.angle_mode = V2ProgramRequirement{.mode = "grd", .line = line.line};
  }

  void append_implicit_read_state_fields(V2Program& program) {
    std::set<std::string> declared;
    for (const V2StateField& field : program.state)
      declared.insert(field.name);

    std::vector<V2StateField> implicit_fields;
    auto add = [&](const std::string& name, int line) {
      if (!declared.insert(name).second)
        return;
      implicit_fields.push_back(V2StateField{
          .name = name,
          .type = "packed",
          .implicit = true,
          .line = line,
      });
    };

    const auto visit_action = [&](const V2StatementPtr& action, const auto& self) -> void {
      if (action != nullptr)
        self(std::vector<V2Statement>{*action}, self);
    };
    const auto visit = [&](const std::vector<V2Statement>& statements, const auto& self) -> void {
      for (const V2Statement& statement : statements) {
        if (statement.kind == "v2_read" && statement.target.has_value())
          add(*statement.target, statement.line);
        self(statement.body, self);
        self(statement.then_body, self);
        self(statement.else_body, self);
        for (const V2MatchCase& match_case : statement.cases)
          visit_action(match_case.action, self);
        visit_action(statement.otherwise, self);
      }
    };

    visit(program.body, visit);
    for (const V2Rule& rule : program.rules)
      visit(rule.body, visit);
    program.state.insert(program.state.end(), implicit_fields.begin(), implicit_fields.end());
  }

  static std::optional<std::string> display_literal_text(const std::vector<DisplayItem>& items) {
    std::string text;
    for (const DisplayItem& item : items) {
      if (item.kind != "literal")
        return std::nullopt;
      text += item.text;
    }
    return text;
  }

  static std::string display_item_key(const std::vector<DisplayItem>& items) {
    std::ostringstream out;
    for (const DisplayItem& item : items) {
      if (item.kind == "literal") {
        out << "literal\0" << item.text << '\0';
      } else {
        out << "source\0" << item.name << '\0';
        if (item.width.has_value())
          out << *item.width;
        out << '\0';
        if (item.pad.has_value())
          out << *item.pad;
        out << '\0';
      }
    }
    return out.str();
  }

  static void assign_inline_screen_name(
      V2Statement& statement, const std::vector<DisplayItem>& items,
      std::vector<std::pair<std::string, std::string>>& screens, int& next) {
    const std::string key = display_item_key(items);
    const auto existing =
        std::find_if(screens.begin(), screens.end(),
                     [&](const auto& entry) { return entry.first == key; });
    if (existing != screens.end()) {
      statement.inline_name = existing->second;
      return;
    }
    const std::string name =
        "__inline_show_" + std::to_string(statement.line) + "_" + std::to_string(next++);
    statement.inline_name = name;
    screens.emplace_back(key, name);
  }

  static void collect_inline_screens(V2Program& program) {
    std::vector<std::pair<std::string, std::string>> screens;
    int next = 0;

    const auto visit_action = [&](V2StatementPtr& action, const auto& visit_statement) -> void {
      if (action != nullptr)
        visit_statement(*action, visit_statement);
    };
    const auto visit_statements = [&](std::vector<V2Statement>& statements,
                                      const auto& visit_statement) -> void {
      for (V2Statement& statement : statements)
        visit_statement(statement, visit_statement);
    };
    const auto visit_statement = [&](V2Statement& statement, const auto& self) -> void {
      if (statement.kind == "v2_show" && statement.items.has_value()) {
        assign_inline_screen_name(statement, *statement.items, screens, next);
      }
      if (statement.kind == "v2_stop" && statement.items.has_value() &&
          !display_literal_text(*statement.items).has_value()) {
        assign_inline_screen_name(statement, *statement.items, screens, next);
      }

      visit_statements(statement.body, self);
      visit_statements(statement.then_body, self);
      visit_statements(statement.else_body, self);
      for (V2MatchCase& match_case : statement.cases)
        visit_action(match_case.action, self);
      visit_action(statement.otherwise, self);
    };

    visit_statements(program.body, visit_statement);
    for (V2Rule& rule : program.rules)
      visit_statements(rule.body, visit_statement);
  }

  V2Const parse_const(const SourceLine& line) {
    static const std::regex const_regex(R"(^const\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$)");
    std::smatch match;
    if (!std::regex_match(line.text, match, const_regex)) {
      throw ParseError("Const must look like 'const NAME = expr'", line.line);
    }
    return V2Const{.name = match[1].str(), .expr = trim(match[2].str()), .line = line.line};
  }

  std::vector<V2StateField> parse_state_block() {
    std::vector<V2StateField> fields;
    while (!done()) {
      const SourceLine line = peek();
      if (line.text == "}") {
        ++index_;
        return fields;
      }
      if (std::regex_match(
              line.text,
              std::regex(
                  R"(^[A-Za-z_][A-Za-z0-9_]*\s*:\s*group\s*\(\s*-?\d+\.\.-?\d+\s*\)\s*\{$)"))) {
        auto group_fields = parse_state_group();
        fields.insert(fields.end(), group_fields.begin(), group_fields.end());
        continue;
      }
      ++index_;
      fields.push_back(parse_state_field(line));
    }
    throw ParseError("Unclosed state block", lines_.empty() ? 1 : lines_.back().line);
  }

  std::vector<V2StateField> parse_state_group() {
    const SourceLine header = next();
    static const std::regex group_regex(
        R"(^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*group\s*\(\s*(-?\d+)\.\.(-?\d+)\s*\)\s*\{$)");
    std::smatch match;
    if (!std::regex_match(header.text, match, group_regex)) {
      throw ParseError("State group must look like 'name: group(1..3) {'", header.line);
    }
    const std::string name = match[1].str();
    const int min = std::stoi(match[2].str());
    const int max = std::stoi(match[3].str());
    if (max < min)
      throw ParseError("Invalid state group range '" + match[2].str() + ".." + match[3].str() + "'",
                       header.line);

    std::vector<V2StateField> fields;
    while (!done()) {
      const SourceLine line = peek();
      if (line.text == "}") {
        ++index_;
        return fields;
      }
      if (line.text.find("group") != std::string::npos) {
        throw ParseError("Nested state groups are not supported", line.line);
      }
      ++index_;
      V2StateField field = parse_state_field(line);
      if (field.bank.has_value()) {
        throw ParseError("State group members cannot also be indexed arrays", line.line);
      }
      const std::string member = field.name;
      field.name = name + "_" + member;
      field.bank = V2StateBankField{.name = name, .member = member, .min = min, .max = max};
      fields.push_back(std::move(field));
    }
    throw ParseError("Unclosed state group", header.line);
  }

  V2StateField parse_state_field(const SourceLine& line) {
    static const std::regex field_regex(
        R"(^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_]*)(?:\[\s*(-?\d+)\.\.(-?\d+)\s*\])?(?:\(([^)]*)\))?(.*)$)");
    std::smatch match;
    if (!std::regex_match(line.text, match, field_regex)) {
      throw ParseError("State field must look like 'name: counter 0..9 = 0', 'name: packed[1..3] = "
                       "0', or 'name: cells(domain) = random()'",
                       line.line);
    }
    const std::string type = match[2].str();
    const std::unordered_set<std::string> allowed = {"flag",  "counter",    "coord",
                                                     "cells", "coord_list", "packed"};
    if (!allowed.contains(type))
      throw ParseError("Unknown state type '" + type + "'", line.line);

    V2StateField field{.name = match[1].str(), .type = type, .line = line.line};
    if (match[3].matched || match[4].matched) {
      const int bank_min = std::stoi(match[3].str());
      const int bank_max = std::stoi(match[4].str());
      if (bank_max < bank_min) {
        throw ParseError("Invalid indexed state range '" + match[3].str() + ".." + match[4].str() +
                             "'",
                         line.line);
      }
      if (type == "coord_list")
        throw ParseError("coord_list cannot be declared as an indexed state bank", line.line);
      field.bank = V2StateBankField{.name = field.name, .min = bank_min, .max = bank_max};
    }

    const std::string args = match[5].matched ? trim(match[5].str()) : "";
    const auto arg_list = args.empty() ? std::vector<std::string>{} : split_args(args);
    if (type == "cells" && arg_list.size() != 1) {
      throw ParseError("cells state must look like 'name: cells(domain) = random()'", line.line);
    }
    if (type == "coord" && arg_list.size() != 1) {
      throw ParseError("coord state must look like 'name: coord(domain)'", line.line);
    }
    if (type == "coord_list" && arg_list.size() != 2) {
      throw ParseError("coord_list state must look like 'name: coord_list(domain, count)'",
                       line.line);
    }
    if (type != "cells" && type != "coord" && type != "coord_list" && !args.empty()) {
      throw ParseError("State type '" + type + "' does not take parameters", line.line);
    }
    if (type == "cells" || type == "coord")
      field.domain = arg_list.at(0);
    if (type == "coord_list") {
      field.domain = arg_list.at(0);
      const int count = std::stoi(arg_list.at(1));
      if (count < 1)
        throw ParseError(
            "coord_list count must be a positive integer, got '" + arg_list.at(1) + "'", line.line);
      field.count = count;
    }

    const std::string tail = trim(match[6].str());
    static const std::regex tail_regex(R"(^(?:(-?\d+)\.\.(-?\d+))?(?:\s*=\s*(.+))?$)");
    std::smatch tail_match;
    if (!std::regex_match(tail, tail_match, tail_regex)) {
      throw ParseError(
          "State field must look like 'name: counter 0..9 = 0' or 'name: cells(domain) = random()'",
          line.line);
    }
    if (type == "counter" && !tail_match[1].matched) {
      throw ParseError("counter state must look like 'name: counter 0..9 = 0'", line.line);
    }
    if (type != "counter" && tail_match[1].matched) {
      throw ParseError("State type '" + type + "' does not take a numeric range", line.line);
    }
    if (tail_match[1].matched) {
      field.min = std::stoi(tail_match[1].str());
      field.max = std::stoi(tail_match[2].str());
    }
    if (tail_match[3].matched) {
      const std::string initial = trim(tail_match[3].str());
      if (initial == "stack.X") {
        field.initial_stack = "X";
      } else if (initial == "stack.Y") {
        field.initial_stack = "Y";
      } else if (initial.rfind("stack.", 0) == 0) {
        throw ParseError("Use 'stack.X' or 'stack.Y' for startup stack values", line.line);
      } else {
        field.initial = initial;
      }
    }
    return field;
  }

  std::optional<V2Board> parse_board_declaration(const SourceLine& line) {
    static const std::regex board_regex(
        R"(^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*board\(\s*(-?\d+)\.\.(-?\d+)\s*,\s*(-?\d+)\.\.(-?\d+)\s*\)$)");
    std::smatch match;
    if (!std::regex_match(line.text, match, board_regex))
      return std::nullopt;
    const int x_min = std::stoi(match[2].str());
    const int x_max = std::stoi(match[3].str());
    const int y_min = std::stoi(match[4].str());
    const int y_max = std::stoi(match[5].str());
    if (x_min > x_max || y_min > y_max)
      throw ParseError("Board ranges must be ascending", line.line);
    return V2Board{
        .name = match[1].str(),
        .x_min = x_min,
        .x_max = x_max,
        .y_min = y_min,
        .y_max = y_max,
        .width = x_max - x_min + 1,
        .height = y_max - y_min + 1,
        .line = line.line,
    };
  }

  std::optional<V2World> parse_compact_board_declaration(const SourceLine& line) {
    static const std::regex world_regex(
        R"(^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*board\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)$)");
    std::smatch match;
    if (!std::regex_match(line.text, match, world_regex))
      return std::nullopt;
    static const std::unordered_set<std::string> known = {
        "corridor_plan",           "decimal_player", "floor_plan",
        "packed_decimal_zero_run", "pier_to_ship",   "row_scan",
    };
    const std::string encoding = match[2].str();
    if (!known.contains(encoding))
      throw ParseError("Unknown board encoding '" + encoding + "'.", line.line);
    const std::string name = match[1].str();
    return V2World{
        .name = name,
        .position = V2WorldPosition{.name = name, .encoding = encoding, .line = line.line},
        .line = line.line,
    };
  }

  V2Rule parse_rule(const std::string& text) {
    const SourceLine header = next();
    static const std::regex rule_regex(R"(^fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*\{$)");
    std::smatch match;
    if (!std::regex_match(text, match, rule_regex)) {
      throw ParseError("Function must look like 'fn name(arg, ...) {'", header.line);
    }
    const std::string name = match[1].str();
    if (kReservedRuleNames.contains(name)) {
      throw ParseError("Function name '" + name + "' is reserved", header.line);
    }
    auto params = parse_comma_identifier_list(match[2].str(), header.line);
    for (const auto& param : params) {
      if (!is_identifier_text(param))
        throw ParseError("Invalid function parameter '" + param + "'", header.line);
    }
    return V2Rule{.name = name,
                  .params = std::move(params),
                  .body = parse_statement_block(),
                  .line = header.line};
  }

  std::vector<V2Statement> parse_statement_block() {
    std::vector<V2Statement> statements;
    while (!done()) {
      const SourceLine line = peek();
      if (line.text == "}") {
        ++index_;
        return statements;
      }
      statements.push_back(parse_statement());
    }
    throw ParseError("Unclosed statement block", lines_.empty() ? 1 : lines_.back().line);
  }

  V2Statement parse_statement() {
    const SourceLine line = peek();
    if (starts_with(line.text, "match ") && ends_with(line.text, "{")) {
      ++index_;
      return parse_match(line.text, line.line);
    }
    if (starts_with(line.text, "unless ") && ends_with(line.text, "{")) {
      ++index_;
      SourceLine rewritten{.text = "if " + line.text.substr(std::string("unless ").size()),
                           .line = line.line};
      return parse_if(rewritten, true);
    }
    if (starts_with(line.text, "if ") && ends_with(line.text, "{")) {
      ++index_;
      return parse_if(line);
    }
    if (line.text == "else {" || starts_with(line.text, "else if ")) {
      throw ParseError("'else' without matching 'if'", line.line);
    }
    if (starts_with(line.text, "while ") && ends_with(line.text, "{")) {
      ++index_;
      V2Statement statement;
      statement.kind = "v2_while";
      statement.predicate = parse_predicate(trim(line.text.substr(
          std::string("while ").size(), line.text.size() - std::string("while ").size() - 1)));
      statement.body = parse_statement_block();
      statement.line = line.line;
      return statement;
    }
    if (line.text == "loop {") {
      ++index_;
      V2Statement statement;
      statement.kind = "v2_loop";
      statement.body = parse_statement_block();
      statement.line = line.line;
      return statement;
    }
    if (line.text == "raw {") {
      ++index_;
      return parse_raw(line.line);
    }
    ++index_;
    return parse_inline_statement(line.text, line.line);
  }

  V2Statement parse_raw(int line) {
    V2Statement statement;
    statement.kind = "v2_raw";
    statement.line = line;
    bool saw_code = false;
    bool saw_clobbers = false;
    bool saw_preserves = false;
    while (!done()) {
      const SourceLine body_line = peek();
      if (body_line.text == "}") {
        ++index_;
        if (!saw_code)
          throw ParseError("Raw block must contain code { ... }", line);
        if (!saw_clobbers)
          throw ParseError("Raw block must declare clobbers", line);
        if (!saw_preserves ||
            std::ranges::find(statement.preserves, "state") == statement.preserves.end()) {
          throw ParseError("Raw block must declare preserves state", line);
        }
        if (std::ranges::find(statement.clobbers, "state") != statement.clobbers.end()) {
          throw ParseError(
              "Raw block cannot clobber high-level state; return values through returns X -> name",
              line);
        }
        validate_raw_inputs(statement.inputs, line);
        return statement;
      }
      if (starts_with(body_line.text, "takes ")) {
        ++index_;
        for (auto input : parse_raw_inputs(body_line.text.substr(std::string("takes ").size()),
                                           body_line.line)) {
          if (std::ranges::any_of(statement.inputs, [&input](const auto& existing) {
                return existing.slot == input.slot;
              })) {
            throw ParseError("Raw input for " + input.slot + " is declared more than once",
                             input.line);
          }
          statement.inputs.push_back(std::move(input));
        }
        continue;
      }
      if (starts_with(body_line.text, "returns ")) {
        ++index_;
        if (!statement.outputs.empty()) {
          throw ParseError("Raw block currently supports one return value", body_line.line);
        }
        statement.outputs.push_back(parse_raw_output(
            body_line.text.substr(std::string("returns ").size()), body_line.line));
        continue;
      }
      if (starts_with(body_line.text, "clobbers ")) {
        ++index_;
        if (saw_clobbers)
          throw ParseError("Raw block must declare clobbers only once", body_line.line);
        saw_clobbers = true;
        statement.clobbers = parse_raw_contract_list(
            body_line.text.substr(std::string("clobbers ").size()), body_line.line);
        continue;
      }
      if (starts_with(body_line.text, "preserves ")) {
        ++index_;
        if (saw_preserves)
          throw ParseError("Raw block must declare preserves only once", body_line.line);
        saw_preserves = true;
        statement.preserves = parse_raw_contract_list(
            body_line.text.substr(std::string("preserves ").size()), body_line.line);
        continue;
      }
      if (body_line.text == "code {") {
        ++index_;
        if (saw_code)
          throw ParseError("Raw block must contain only one code block", body_line.line);
        saw_code = true;
        statement.lines = parse_raw_code_block(body_line.line);
        continue;
      }
      throw ParseError("Unexpected raw line '" + body_line.text + "'", body_line.line);
    }
    throw ParseError("Unclosed raw block", line);
  }

  std::vector<RawBlockLine> parse_raw_code_block(int line) {
    std::vector<RawBlockLine> lines;
    while (!done()) {
      const SourceLine raw_line = next();
      if (raw_line.text == "}")
        return lines;
      lines.push_back(RawBlockLine{.text = raw_line.text, .line = raw_line.line});
    }
    throw ParseError("Unclosed raw code block", line);
  }

  V2Statement parse_if(const SourceLine& line, bool negated = false) {
    static const std::regex if_regex(R"(^if\s+(.+)\s*\{$)");
    std::smatch match;
    if (!std::regex_match(line.text, match, if_regex)) {
      throw ParseError("If must look like 'if predicate {'", line.line);
    }
    V2Statement statement;
    statement.kind = "v2_if";
    statement.predicate = parse_predicate(trim(match[1].str()));
    statement.then_body = parse_statement_block();
    statement.negated = negated;
    statement.line = line.line;
    auto else_body = parse_else_body();
    if (else_body.has_value()) {
      statement.has_else_body = true;
      statement.else_body = std::move(*else_body);
    }
    return statement;
  }

  std::optional<std::vector<V2Statement>> parse_else_body() {
    if (done())
      return std::nullopt;
    const SourceLine line = peek();
    if (line.text == "else {") {
      ++index_;
      return parse_statement_block();
    }
    if (starts_with(line.text, "else if ") && ends_with(line.text, "{")) {
      ++index_;
      std::vector<V2Statement> result;
      result.push_back(parse_if(
          SourceLine{.text = line.text.substr(std::string("else ").size()), .line = line.line}));
      return result;
    }
    return std::nullopt;
  }

  V2Statement parse_match(const std::string& text, int line) {
    static const std::regex match_regex(R"(^match\s+(.+?)\s*\{$)");
    std::smatch match;
    if (!std::regex_match(text, match, match_regex)) {
      throw ParseError("Match must look like 'match expr {'", line);
    }
    V2Statement statement;
    statement.kind = "v2_match";
    statement.expr = trim(match[1].str());
    statement.line = line;
    while (!done()) {
      const SourceLine body_line = next();
      if (body_line.text == "}")
        return statement;
      static const std::regex arrow_regex(R"(^(.+?)\s*=>\s*(.+)$)");
      std::smatch arrow;
      if (!std::regex_match(body_line.text, arrow, arrow_regex)) {
        throw ParseError("Match cases must look like 'value => action' or 'value => {'",
                         body_line.line);
      }
      const std::string left = trim(arrow[1].str());
      const std::string right = trim(arrow[2].str());
      V2Statement action = right == "{" ? V2Statement{.kind = "v2_block",
                                                      .body = parse_statement_block(),
                                                      .line = body_line.line}
                                        : parse_inline_statement(right, body_line.line);
      if (left == "otherwise") {
        statement.otherwise = std::make_shared<V2Statement>(std::move(action));
      } else {
        statement.cases.push_back(V2MatchCase{
            .values = expand_match_case_values(left, body_line.line),
            .action = std::make_shared<V2Statement>(std::move(action)),
            .line = body_line.line,
        });
      }
    }
    throw ParseError("Unclosed match block", line);
  }

  V2Statement parse_inline_statement(const std::string& text, int line) {
    if (auto show = parse_named_call(text, "show"); show.has_value())
      return parse_show(show->second, line);
    if (auto preview = parse_named_call(text, "preview"); preview.has_value()) {
      const std::string expr = trim(preview->second);
      if (expr.empty())
        throw ParseError("Preview must look like 'preview(expr)'", line);
      return V2Statement{.kind = "v2_preview", .expr = expr, .line = line};
    }
    if (auto halt = parse_named_call(text, "halt"); halt.has_value())
      return parse_halt(halt->second, line);

    static const std::regex read_assignment(R"(^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*read\s*\(\s*\)$)");
    std::smatch read_match;
    if (std::regex_match(text, read_match, read_assignment)) {
      return V2Statement{.kind = "v2_read", .target = read_match[1].str(), .line = line};
    }
    if (starts_with(text, "show "))
      throw ParseError("Show must look like 'show(...)'", line);
    if (starts_with(text, "preview "))
      throw ParseError("Preview must look like 'preview(expr)'", line);
    if (std::regex_match(text, std::regex(R"(^read\s+[A-Za-z_][A-Za-z0-9_]*$)"))) {
      throw ParseError("Read input with 'name = read()'", line);
    }
    if (starts_with(text, "read "))
      throw ParseError("Read input with 'name = read()'", line);
    if (text == "return" || starts_with(text, "return ")) {
      const std::string expr = trim(text.substr(std::string("return").size()));
      if (expr.empty())
        throw ParseError("'return' must return a value, e.g. 'return x + 1'", line);
      return V2Statement{.kind = "v2_return", .expr = expr, .line = line};
    }
    static const std::regex step_regex(R"(^([A-Za-z_][A-Za-z0-9_]*)\s*(\+\+|--)$)");
    std::smatch step_match;
    if (std::regex_match(text, step_match, step_regex)) {
      return V2Statement{
          .kind = "v2_update",
          .target = step_match[1].str(),
          .expr = "1",
          .op = step_match[2].str() == "++" ? "+=" : "-=",
          .line = line,
      };
    }
    static const std::regex update_regex(R"(^(.+?)\s*(\+=|-=)\s*(.+)$)");
    std::smatch update_match;
    if (std::regex_match(text, update_match, update_regex)) {
      const std::string target = trim(update_match[1].str());
      validate_assignment_target(target, line);
      return V2Statement{
          .kind = "v2_update",
          .target = target,
          .expr = trim(update_match[3].str()),
          .op = update_match[2].str(),
          .line = line,
      };
    }
    if (auto equal = find_assignment_equal(text); equal.has_value()) {
      const std::string target = trim(text.substr(0, *equal));
      validate_assignment_target(target, line);
      return V2Statement{
          .kind = "v2_assign",
          .target = target,
          .expr = trim(text.substr(*equal + 1)),
          .line = line,
      };
    }
    if (auto call = parse_call(text); call.has_value()) {
      return V2Statement{
          .kind = "v2_invoke",
          .name = call->first,
          .args = split_args(call->second),
          .line = line,
      };
    }
    if (std::regex_match(text, std::regex(R"(^[A-Za-z_][A-Za-z0-9_]*(?:\s+.+)?$)"))) {
      throw ParseError("Function calls must look like 'name(...)'", line);
    }
    throw ParseError("Unexpected statement '" + text + "'", line);
  }

  V2Statement parse_show(const std::string& args_text, int line) {
    V2Statement statement;
    statement.kind = "v2_show";
    apply_display_call(statement, args_text, line, false);
    statement.line = line;
    return statement;
  }

  V2Statement parse_halt(const std::string& args_text, int line) {
    const std::string trimmed = trim(args_text);
    V2Statement statement;
    statement.kind = "v2_stop";
    statement.line = line;
    if (trimmed.empty()) {
      statement.target = "0";
      return statement;
    }
    if (split_args(trimmed).size() == 1 && !starts_with(trimmed, "\"")) {
      statement.target = trimmed;
      return statement;
    }
    apply_display_call(statement, args_text, line, true);
    return statement;
  }

  void apply_display_call(V2Statement& statement, const std::string& args_text, int line,
                          bool halt) {
    const std::string trimmed = trim(args_text);
    if (trimmed.empty()) {
      statement.items = std::vector<DisplayItem>{};
      return;
    }
    if (!halt && is_numeric_literal_text(trimmed)) {
      statement.target = trimmed;
      return;
    }
    statement.items = parse_display_item_list(trimmed, line);
  }

  void validate_assignment_target(const std::string& target, int line) {
    if (is_identifier_text(target))
      return;
    Expression expr = parse_expression(normalize_v2_expression_text(target), line);
    if (expr.kind == "indexed")
      return;
    throw ParseError("Invalid assignment target '" + target + "'", line);
  }

  std::vector<V2RawInput> parse_raw_inputs(const std::string& text, int line) {
    std::vector<V2RawInput> inputs;
    static const std::regex input_regex(R"(^\s*(X|Y|Z|T)\s*=\s*(.+)$)", std::regex::icase);
    for (const auto& part : split_args(text)) {
      std::smatch match;
      if (!std::regex_match(part, match, input_regex)) {
        throw ParseError("Raw input must look like 'takes X = expr'", line);
      }
      std::string slot = match[1].str();
      std::ranges::transform(slot, slot.begin(),
                             [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
      inputs.push_back(V2RawInput{.slot = slot, .expr = trim(match[2].str()), .line = line});
    }
    return inputs;
  }

  V2RawOutput parse_raw_output(const std::string& text, int line) {
    const std::string trimmed = trim(text);
    static const std::regex explicit_regex(R"(^X\s*->\s*([A-Za-z_][A-Za-z0-9_]*)$)",
                                           std::regex::icase);
    static const std::regex shorthand_regex(R"(^([A-Za-z_][A-Za-z0-9_]*)$)");
    std::smatch match;
    if (std::regex_match(trimmed, match, explicit_regex) ||
        std::regex_match(trimmed, match, shorthand_regex)) {
      return V2RawOutput{.target = match[1].str(), .line = line};
    }
    throw ParseError("Raw output must look like 'returns X -> name'", line);
  }

  std::vector<std::string> parse_raw_contract_list(const std::string& text, int line) {
    std::vector<std::string> values;
    for (const auto& item : parse_identifier_list(text))
      values.push_back(normalize_raw_contract_item(item, line));
    if (values.empty())
      throw ParseError("Raw contract list must not be empty", line);
    if (std::ranges::find(values, "none") != values.end() && values.size() > 1) {
      throw ParseError("Raw contract item 'none' cannot be combined with other items", line);
    }
    std::vector<std::string> unique;
    for (const auto& value : values) {
      if (std::ranges::find(unique, value) == unique.end())
        unique.push_back(value);
    }
    return unique;
  }

  std::string normalize_raw_contract_item(const std::string& text, int line) {
    std::string value = trim(text);
    std::string lower = value;
    std::ranges::transform(lower, lower.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lower == "none" || lower == "state" || lower == "stack" || lower == "display" ||
        lower == "flags" || lower == "memory") {
      return lower;
    }
    std::string upper = value;
    std::ranges::transform(upper, upper.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    if (upper == "X" || upper == "Y" || upper == "Z" || upper == "T" || upper == "X1") {
      return upper;
    }
    static const std::regex register_regex(R"(^R?([0-9a-e])$)", std::regex::icase);
    std::smatch match;
    if (std::regex_match(value, match, register_regex)) {
      std::string reg = match[1].str();
      std::ranges::transform(reg, reg.begin(),
                             [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      return "R" + reg;
    }
    throw ParseError("Unknown raw contract item '" + text + "'", line);
  }

  void validate_raw_inputs(const std::vector<V2RawInput>& inputs, int line) {
    auto has = [&inputs](std::string_view slot) {
      return std::ranges::any_of(inputs, [slot](const auto& input) { return input.slot == slot; });
    };
    if (has("T") && (!has("Z") || !has("Y") || !has("X"))) {
      throw ParseError("Raw input T requires Z, Y, and X inputs", line);
    }
    if (has("Z") && (!has("Y") || !has("X"))) {
      throw ParseError("Raw input Z requires Y and X inputs", line);
    }
    if (has("Y") && !has("X"))
      throw ParseError("Raw input Y requires an X input", line);
  }

  [[nodiscard]] bool done() const {
    return index_ >= lines_.size();
  }

  SourceLine peek() const {
    if (done()) {
      throw ParseError("Unexpected end of file", lines_.empty() ? 1 : lines_.back().line);
    }
    return lines_.at(index_);
  }

  SourceLine next() {
    const SourceLine line = peek();
    ++index_;
    return line;
  }

  std::vector<SourceLine> lines_;
  ParseOptions options_;
  std::size_t index_ = 0;
};

} // namespace

ParseError::ParseError(const std::string& message, int line)
    : std::runtime_error(message + " at line " + std::to_string(line)), line_(line) {}

int ParseError::line() const noexcept {
  return line_;
}

ProgramAst parse_program(const std::string& source, ParseOptions options) {
  (void)options;
  return Parser(source, options).parse_program();
}

std::string normalize_v2_expression_text(const std::string& text) {
  static const std::regex floor_regex(R"(\b([A-Za-z_][A-Za-z0-9_]*)\.floor\b)");
  return trim(std::regex_replace(text, floor_regex, "int($1 / 100)"));
}

Expression parse_expression(const std::string& text, int line) {
  return ExpressionParser(normalize_v2_expression_text(text), line).parse();
}

} // namespace mkpro
