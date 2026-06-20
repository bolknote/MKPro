#pragma once

#include "mkpro/core/ast.hpp"

#include <stdexcept>
#include <string>

namespace mkpro {

class ParseError : public std::runtime_error {
 public:
  ParseError(const std::string& message, int line);

  [[nodiscard]] int line() const noexcept;

 private:
  int line_;
};

struct ParseOptions {
  bool signed_abs_match_pairs = false;
  bool synthesize_parametric_siblings = false;
  bool segmented_bitplanes = false;
};

ProgramAst parse_program(const std::string& source, ParseOptions options = {});
Expression parse_expression(const std::string& text, int line = 0);
std::string normalize_v2_expression_text(const std::string& text);

}  // namespace mkpro
