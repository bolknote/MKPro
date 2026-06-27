#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>

namespace mkpro::tests {

namespace {

CompileResult compile_budget_999(const std::string& source) {
  CompileOptions options;
  options.budget = 999;
  // This suite pins the direct-call (ПП) function-lowering structure; suppress
  // the default-on aggressive post-layout indirect-flow repacking so the
  // assertions keep targeting the mid-level lowering rather than its later
  // indirect-call packed form.
  options.disable_aggressive_post_layout = true;
  return compile_source(source, options);
}

void require_clean_compile(const CompileResult& result, const std::string& context) {
  require(result.implemented, context + " should compile");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            context + " should not report errors: " + diagnostic.message);
  }
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

bool has_hex(const CompileResult& result, const std::string& hex) {
  return std::any_of(result.steps.begin(), result.steps.end(),
                     [&](const ResolvedStep& step) { return step.hex == hex; });
}

int count_hex(const CompileResult& result, const std::string& hex) {
  return static_cast<int>(std::count_if(result.steps.begin(), result.steps.end(),
                                        [&](const ResolvedStep& step) {
                                          return step.hex == hex;
                                        }));
}

bool has_error_code(const CompileResult& result, const std::string& code) {
  return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [&](const Diagnostic& diagnostic) {
                       return diagnostic.severity == DiagnosticSeverity::Error &&
                              diagnostic.code == code;
                     });
}

void require_compile_error_contains(const std::string& source, const std::string& fragment) {
  const CompileResult result = compile_budget_999(source);
  require(!result.implemented, "program should fail to compile with '" + fragment + "'");
  const bool found = std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                                 [&](const Diagnostic& diagnostic) {
                                   return diagnostic.severity == DiagnosticSeverity::Error &&
                                          diagnostic.message.find(fragment) != std::string::npos;
                                 });
  require(found, "compile diagnostics should contain '" + fragment + "'");
}

void require_compile_error_code_contains(const std::string& source, const std::string& code,
                                         const std::string& fragment) {
  const CompileResult result = compile_budget_999(source);
  require(!result.implemented, "program should fail to compile with '" + fragment + "'");
  require(has_error_code(result, code), "compile diagnostics should include code '" + code + "'");
  const bool found = std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                                 [&](const Diagnostic& diagnostic) {
                                   return diagnostic.severity == DiagnosticSeverity::Error &&
                                          diagnostic.code == code &&
                                          diagnostic.message.find(fragment) != std::string::npos;
                                 });
  require(found, "compile diagnostics should contain '" + fragment + "' for code '" + code + "'");
}

} // namespace

void functions_match_typescript_contract() {
  {
    const CompileResult result = compile_budget_999(R"mkpro(
program FunctionDemo {
  state {
    result: counter 0..99 = 0
  }
  fn double(n) {
    return n + n
  }
  loop {
    x = read()
    result = double(x)
    halt(result)
  }
}
)mkpro");

    require_clean_compile(result, "value-returning function call");
    require(has_hex(result, "53"), "value-returning function call should emit ПП");
    require(has_hex(result, "52"), "value-returning function call should emit В/О");
    require(has_optimization(result, "function-call"),
            "value-returning function call should report function-call");
  }

  {
    const CompileResult result = compile_budget_999(R"mkpro(
program Nested {
  state {
    result: counter 0..99 = 0
  }
  fn inc(n) {
    return n + 1
  }
  fn dbl(n) {
    return n + n
  }
  loop {
    x = read()
    result = dbl(inc(x))
    halt(result)
  }
}
)mkpro");

    require_clean_compile(result, "nested function call");
    require(has_optimization(result, "function-call-lifting"),
            "nested function call should report function-call-lifting");
    require(count_hex(result, "53") == 2,
            "nested function call should emit two ПП calls, got " +
                std::to_string(count_hex(result, "53")));
  }

  {
    const CompileResult result = compile_budget_999(R"mkpro(
program Sign {
  state {
    result: counter 0..99 = 0
  }
  fn sign(n) {
    if n < 0 {
      return 0
    }
    else {
      return 1
    }
  }
  loop {
    x = read()
    result = sign(x)
    halt(result)
  }
}
)mkpro");

    require_clean_compile(result, "early branch returns");
    require(has_hex(result, "52"), "early branch returns should emit В/О");
  }

  {
    const CompileResult result = compile_budget_999(R"mkpro(
program Recursive {
  state {
    result: counter 0..99 = 0
  }
  fn count_down(n, acc) {
    if n <= 0 {
      return acc
    }
    else {
      return count_down(n - 1, acc + 1)
    }
  }
  loop {
    x = read()
    result = count_down(x, 0)
    halt(result)
  }
}
)mkpro");

    require_clean_compile(result, "direct tail recursion");
    require(has_optimization(result, "function-tail-recursion"),
            "direct tail recursion should report function-tail-recursion");
    require(count_hex(result, "53") == 1,
            "direct tail recursion should emit one ПП root call, got " +
                std::to_string(count_hex(result, "53")));
  }

  {
    const CompileResult result = compile_budget_999(R"mkpro(
program Mutual {
  state {
    result: counter 0..99 = 0
  }
  fn even(n) {
    if n <= 0 {
      return 1
    }
    else {
      return odd(n - 1)
    }
  }
  fn odd(n) {
    if n <= 0 {
      return 0
    }
    else {
      return even(n - 1)
    }
  }
  loop {
    x = read()
    result = even(x)
    halt(result)
  }
}
)mkpro");

    require_clean_compile(result, "mutual tail recursion");
    require(has_optimization(result, "function-tail-call"),
            "mutual tail recursion should report function-tail-call");
    require(count_hex(result, "53") == 1,
            "mutual tail recursion should emit one ПП root call, got " +
                std::to_string(count_hex(result, "53")));
  }

  {
    const CompileResult result = compile_budget_999(R"mkpro(
program TailForward {
  state {
    result: counter 0..99 = 0
  }
  fn id(n) {
    return n
  }
  fn forward(n) {
    return id(n + 1)
  }
  loop {
    x = read()
    result = forward(x)
    halt(result)
  }
}
)mkpro");

    require_clean_compile(result, "non-recursive value-function tail call");
    require(has_optimization(result, "function-tail-call"),
            "non-recursive tail call should report function-tail-call");
    require(count_hex(result, "53") == 1,
            "non-recursive tail call should emit one ПП root call, got " +
                std::to_string(count_hex(result, "53")));
  }

  {
    const CompileResult result = compile_budget_999(R"mkpro(
program FiveDeep {
  state {
    result: counter 0..999 = 0
  }
  fn f1(n) {
    return f2(n) + 1
  }
  fn f2(n) {
    return f3(n) + 1
  }
  fn f3(n) {
    return f4(n) + 1
  }
  fn f4(n) {
    return f5(n) + 1
  }
  fn f5(n) {
    return n
  }
  loop {
    x = read()
    result = f1(x)
    halt(result)
  }
}
)mkpro");

    require_clean_compile(result, "five nested return-stack frames");
    require(!has_error_code(result, "RETURN_STACK_DEPTH_EXCEEDED"),
            "five nested return-stack frames should be accepted");
  }

  require_compile_error_code_contains(R"mkpro(
program SixDeep {
  state {
    result: counter 0..999 = 0
  }
  fn f1(n) {
    return f2(n) + 1
  }
  fn f2(n) {
    return f3(n) + 2
  }
  fn f3(n) {
    return f4(n) + 3
  }
  fn f4(n) {
    return f5(n) + 4
  }
  fn f5(n) {
    return f6(n) + 5
  }
  fn f6(n) {
    return random(9)
  }
  loop {
    x = read()
    result = f1(x)
    halt(result)
  }
}
)mkpro",
                                      "RETURN_STACK_DEPTH_EXCEEDED", "depth 6");

  {
    const CompileResult result = compile_budget_999(R"mkpro(
program TailChain {
  state {
    result: counter 0..999 = 0
  }
  fn f1(n) {
    return f2(n)
  }
  fn f2(n) {
    return f3(n)
  }
  fn f3(n) {
    return f4(n)
  }
  fn f4(n) {
    return f5(n)
  }
  fn f5(n) {
    return f6(n)
  }
  fn f6(n) {
    return f7(n)
  }
  fn f7(n) {
    return n
  }
  loop {
    x = read()
    result = f1(x)
    halt(result)
  }
}
)mkpro");

    require_clean_compile(result, "tail-call chain");
    require(has_optimization(result, "function-tail-call"),
            "tail-call chain should report function-tail-call");
    require(!has_error_code(result, "RETURN_STACK_DEPTH_EXCEEDED"),
            "tail-call chain should not count as nested return-stack frames");
  }

  require_compile_error_contains(R"mkpro(
program Recursive {
  state {
    result: counter 0..99 = 0
  }
  fn loopy(n) {
    return loopy(n) + 1
  }
  loop {
    x = read()
    result = loopy(x)
    halt(result)
  }
}
)mkpro",
                                 "non-tail call to 'loopy'");

  require_compile_error_contains(R"mkpro(
program Mutual {
  state {
    result: counter 0..99 = 0
  }
  fn ping(n) {
    return pong(n) + 1
  }
  fn pong(n) {
    return ping(n)
  }
  loop {
    x = read()
    result = ping(x)
    halt(result)
  }
}
)mkpro",
                                 "non-tail call to 'pong'");

  require_compile_error_contains(R"mkpro(
program Hidden {
  state {
    result: counter 0..99 = 0
  }
  fn wrap(n) {
    return finish(wrap(n - 1))
  }
  fn finish(n) {
    return n
  }
  loop {
    x = read()
    result = wrap(x)
    halt(result)
  }
}
)mkpro",
                                 "non-tail call to 'wrap'");

  require_compile_error_contains(R"mkpro(
program BadReturn {
  loop {
    return 1
  }
}
)mkpro",
                                 "'return' is only allowed inside a function");

  require_compile_error_contains(R"mkpro(
program PartialReturn {
  state {
    result: counter 0..99 = 0
  }
  fn maybe(n) {
    if n < 0 {
      return 0
    }
  }
  loop {
    x = read()
    result = maybe(x)
    halt(result)
  }
}
)mkpro",
                                 "must return a value on every path");

  require_compile_error_contains(R"mkpro(
program EmptyReturn {
  state {
    result: counter 0..99 = 0
  }
  fn nothing(n) {
    return
  }
  loop {
    x = read()
    result = nothing(x)
    halt(result)
  }
}
)mkpro",
                                 "'return' must return a value");
}

} // namespace mkpro::tests
