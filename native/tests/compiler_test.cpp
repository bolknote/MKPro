#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace mkpro::tests {

namespace {

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read fixture: " + path.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

bool has_error_diagnostic(const CompileResult& result) {
  return std::any_of(
      result.diagnostics.begin(), result.diagnostics.end(),
      [](const Diagnostic& item) { return item.severity == DiagnosticSeverity::Error; });
}

bool has_optimization_detail(const CompileResult& result, const std::string& name,
                             const std::string& detail) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) {
                       return item.name == name && item.detail.find(detail) != std::string::npos;
                     });
}

std::size_t optimization_count(const CompileResult& result, const std::string& name) {
  return static_cast<std::size_t>(
      std::count_if(result.optimizations.begin(), result.optimizations.end(),
                    [&](const OptimizationReport& item) { return item.name == name; }));
}

} // namespace

void compiler_lowers_initial_v2_subset() {
  const std::filesystem::path root = std::filesystem::current_path();
  const std::string basic_source = read_file(root / "examples" / "basic.mkpro");
  const CompileResult basic = compile_source(basic_source);

  require(basic.implemented, "native compiler should implement the BasicSum subset");
  require(basic.diagnostics.empty(), "basic compile should not report diagnostics");
  require(basic.hex == "00: 50 41 50 61 10 50 51 00",
          "basic compile should match the frozen byte oracle");
  require(basic.steps.size() == 8, "basic compile should produce 8 steps");
  require(basic.steps.at(0).comment == "read x", "first read comment mismatch");
  require(basic.steps.at(3).opcode == 0x61, "basic compile should recall x from R1");
  require(basic.steps.at(3).comment == "recall x", "recall comment mismatch");
  require(basic.steps.at(5).comment == "halt", "halt comment mismatch");
  require(basic.labels.at("__loop_0") == "00", "loop label should resolve to 00");
  require(basic.registers.at("result") == "0", "state field should allocate R0");
  require(basic.registers.at("x") == "1", "first local should allocate R1");
  require(basic.registers.at("y") == "2", "second local should allocate R2");
  require(basic.listing.find("read x") != std::string::npos, "basic listing should include read x");
  require(basic.listing.find("loop back") != std::string::npos,
          "basic listing should include loop back");

  const CompileResult invoked = compile_source(R"mkpro(
program InvokeProcedure {
  state {
    total: counter 0..9 = 0
  }

  fn bump() {
    total += 1
  }

  loop {
    bump()
    halt(total)
  }
}
)mkpro");
  require(invoked.implemented, "native compiler should lower no-arg v2_invoke");
  require(invoked.diagnostics.empty(), "invoke compile should not report diagnostics");
  require(invoked.listing.find("call function bump") == std::string::npos,
          "no-arg statement procedure should inline instead of emitting a call");
  require(invoked.listing.find("implicit return from function bump") == std::string::npos,
          "inlined statement procedure should not emit a dead function body");
  require(invoked.listing.find("increment total") != std::string::npos,
          "inlined procedure body should still lower its update");
  require(has_optimization(invoked, "single-use-rule-inline"),
          "single-use statement procedure inline should report the TS optimization name");

  const CompileResult identity_assignment = compile_source(R"mkpro(
program IdentityAssignment {
  state {
    value: packed = 0
  }

  loop {
    value = value
    halt(value)
  }
}
)mkpro");
  require(identity_assignment.implemented,
          "native compiler should lower identity assignment probe");
  require(identity_assignment.diagnostics.empty(),
          "identity assignment compile should not report diagnostics");
  require(has_optimization(identity_assignment, "identity-assignment-elimination"),
          "identity assignment probe should report identity-assignment-elimination");
  require(identity_assignment.listing.find("assign value") == std::string::npos,
          "identity assignment should be pruned before lowering");

  const CompileResult dead_state = compile_source(R"mkpro(
program DeadState {
  state {
    visible: packed = 0
    scratch: packed = 0
  }

  loop {
    scratch = 5
    visible = 1
    halt(visible)
  }
}
)mkpro");
  require(dead_state.implemented, "native compiler should eliminate unobserved state");
  require(dead_state.diagnostics.empty(), "dead-state compile should not report diagnostics");
  require(has_optimization(dead_state, "dead-state-elimination"),
          "unobserved state should report dead-state-elimination");
  require(dead_state.registers.find("scratch") == dead_state.registers.end(),
          "unobserved state should not allocate a register");
  require(dead_state.listing.find("assign scratch") == std::string::npos,
          "unobserved state assignment should be pruned before lowering");

  const CompileResult tail_copy = compile_source(R"mkpro(
program TailCopy {
  state {
    pos: counter 0..99 = 1
    next: counter 0..99 = 0
    dir: counter -9..9 = 1
  }
  loop {
    try_move()
  }
  fn try_move() {
    next = pos + dir
    enter_next()
  }
  fn enter_next() {
    pos = next
    halt(pos)
  }
}
)mkpro");
  require(tail_copy.implemented, "native compiler should fuse tail copy assignments");
  require(tail_copy.diagnostics.empty(), "tail copy fusion compile should not report diagnostics");
  require(has_optimization(tail_copy, "tail-copy-assignment-fusion"),
          "tail copy assignment should report tail-copy-assignment-fusion");
  require(tail_copy.listing.find("enter_next") == std::string::npos,
          "fused tail copy helper should be removed from emitted listing");
  require(tail_copy.listing.find("assign next") == std::string::npos,
          "tail copy fusion should not materialize the intermediate copy target");
  require(has_optimization(tail_copy, "dead-state-elimination"),
          "tail copy fusion should expose the intermediate copy target as dead state");
  require(tail_copy.registers.find("next") == tail_copy.registers.end(),
          "dead-state elimination should remove the tail-copy intermediate state");

  const CompileResult one_shot = compile_source(R"mkpro(
program OneShotInit {
  state {
    entered: flag = 0
    value: counter 0..9 = 0
  }

  loop {
    if entered == 0 {
      entered = 1
      value++
      show(value)
    }
    else {
      key = read()
      value += key
      show(value)
    }
  }
}
)mkpro");
  require(one_shot.implemented, "native compiler should hoist one-shot loop initializers");
  require(one_shot.diagnostics.empty(), "one-shot init compile should not report diagnostics");
  require(has_optimization(one_shot, "one-shot-loop-init-hoist"),
          "one-shot init probe should report one-shot-loop-init-hoist");
  require(has_optimization(one_shot, "dead-state-elimination"),
          "one-shot init hoist should expose the guard as dead state");
  require(one_shot.registers.find("entered") == one_shot.registers.end(),
          "one-shot guard should not allocate a register after hoisting");

  const CompileResult guarded_call = compile_source(R"mkpro(
program GuardedCall {
  state {
    flag: flag = 0
    score: counter 0..9 = 0
  }
  loop {
    flag = 1
    maybe_score()
    halt(score)
  }
  fn maybe_score() {
    if flag == 1 {
      score++
      flag = 0
    }
  }
}
)mkpro");
  require(guarded_call.implemented, "native compiler should inline constant guarded calls");
  require(guarded_call.diagnostics.empty(),
          "constant guarded call compile should not report diagnostics");
  require(has_optimization(guarded_call, "constant-guarded-call-inline"),
          "guarded call probe should report constant-guarded-call-inline");
  require(has_optimization(guarded_call, "dead-state-elimination"),
          "guarded call inline should expose the guard as dead state");
  require(guarded_call.registers.find("flag") == guarded_call.registers.end(),
          "guarded call flag should not allocate a register after inlining");
  require(guarded_call.listing.find("maybe_score") == std::string::npos,
          "inlined guarded call helper should be removed from emitted listing");

  const CompileResult common_branch_tail = compile_source(R"mkpro(
program CommonBranchTail {
  state {
    selector: counter 0..9 = 0
    value: counter 0..9 = 0
  }

  loop {
    if selector == 0 {
      value = 1
      show(value)
    }
    else {
      value = 2
      show(value)
    }
  }
}
)mkpro");
  require(common_branch_tail.implemented, "native compiler should hoist common branch tails");
  require(common_branch_tail.diagnostics.empty(),
          "common branch tail compile should not report diagnostics");
  require(has_optimization(common_branch_tail, "common-branch-tail-hoisting"),
          "common branch tail probe should report common-branch-tail-hoisting");
  require(std::count_if(common_branch_tail.steps.begin(), common_branch_tail.steps.end(),
                        [](const ResolvedStep& step) { return step.hex == "50"; }) == 1,
          "common branch tail should emit one display stop");

  const CompileResult common_dispatch_tail = compile_source(R"mkpro(
program CommonDispatchTail {
  state {
    selector: counter 0..9 = 0
    value: counter 0..9 = 0
  }

  loop {
    if selector == 1 {
      value = 1
      show(value)
    }
    else {
      if selector == 2 {
        value = 2
        show(value)
      }
      else {
        value = 3
        show(value)
      }
    }
  }
}
)mkpro");
  require(common_dispatch_tail.implemented,
          "native compiler should hoist nested common branch tails");
  require(common_dispatch_tail.diagnostics.empty(),
          "common dispatch tail compile should not report diagnostics");
  require(has_optimization(common_dispatch_tail, "common-branch-tail-hoisting"),
          "common dispatch tail probe should report common-branch-tail-hoisting");
  require(std::count_if(common_dispatch_tail.steps.begin(), common_dispatch_tail.steps.end(),
                        [](const ResolvedStep& step) { return step.hex == "50"; }) == 1,
          "common dispatch tail should emit one display stop");

  const CompileResult single_use_tail = compile_source(R"mkpro(
program SingleUseTailInline {
  state {
    selector: counter 0..9 = 0
    value: counter 0..9 = 0
  }

  loop {
    if selector == 0 {
      positive()
    }
    else {
      negative()
    }
  }

  fn positive() {
    value = 1
    finish()
  }

  fn negative() {
    value = 2
    finish()
  }

  fn finish() {
    show(value)
  }
}
)mkpro");
  require(single_use_tail.implemented,
          "native compiler should expand single-use tail calls for common suffixes");
  require(single_use_tail.diagnostics.empty(),
          "single-use tail inline compile should not report diagnostics");
  require(has_optimization(single_use_tail, "single-use-tail-inline"),
          "single-use tail probe should report single-use-tail-inline");
  require(has_optimization(single_use_tail, "common-branch-tail-hoisting"),
          "single-use tail expansion should expose common branch tail hoisting");
  require(std::count_if(single_use_tail.steps.begin(), single_use_tail.steps.end(),
                        [](const ResolvedStep& step) { return step.hex == "50"; }) == 1,
          "single-use tail inline should expose one display stop");

  const CompileResult compact_dispatch = compile_source(R"mkpro(
program CompactDispatch {
  state {
    selector: counter 0..9 = 0
    value: counter 0..9 = 0
  }

  loop {
    match selector {
      otherwise => show(value)
    }
  }
}
)mkpro");
  require(compact_dispatch.implemented, "native compiler should compact default-only dispatch");
  require(compact_dispatch.diagnostics.empty(),
          "compact dispatch compile should not report diagnostics");
  require(has_optimization(compact_dispatch, "compact-dispatch-simplification"),
          "default-only dispatch should report compact-dispatch-simplification");

  CompileOptions guarded_prologue_options;
  guarded_prologue_options.guarded_prologue_gadgets = true;
  const CompileResult guarded_prologue = compile_source(R"mkpro(
program GuardedPrologueGadget {
  state {
    action: packed = 0
    energy: counter 0..9 = 9
    pos: packed = 0
  }

  fn pay() {
    energy--
  }

  fn drained() {
    halt(-999)
  }

  loop {
    action = read()
    match action {
      1 => left()
      2 => right()
      3 => up()
      otherwise => halt(pos)
    }
  }

  fn left() {
    pay()
    if energy > 0 {
      pos += 1
    }
    else {
      drained()
    }
    halt(pos)
  }

  fn right() {
    pay()
    if energy > 0 {
      pos += 10
    }
    else {
      drained()
    }
    halt(pos)
  }

  fn up() {
    pay()
    if energy > 0 {
      pos += 100
    }
    else {
      drained()
    }
    halt(pos)
  }
}
)mkpro",
                                                        guarded_prologue_options);
  require(guarded_prologue.implemented,
          "native compiler should lower guarded prologue gadget probe");
  require(guarded_prologue.diagnostics.empty(),
          "guarded prologue compile should not report diagnostics");
  require(has_optimization(guarded_prologue, "guarded-prologue-gadget"),
          "guarded prologue probe should report guarded-prologue-gadget");
  require(
      has_optimization_detail(guarded_prologue, "guarded-prologue-gadget", "across 3 call sites"),
      "guarded prologue pass should rewrite all matching call sites");
  require(guarded_prologue.listing.find("__guarded_prologue_0") != std::string::npos,
          "guarded prologue pass should emit a shared helper procedure");

  const CompileResult recursive_invoked = compile_source(R"mkpro(
program RecursiveProcedure {
  state {
    total: counter 0..9 = 0
  }

  fn spin() {
    total += 1
    spin()
  }

  loop {
    spin()
    halt(total)
  }
}
)mkpro");
  require(recursive_invoked.implemented,
          "native compiler should lower recursive no-arg procedures as calls");
  require(recursive_invoked.diagnostics.empty(),
          "recursive no-arg procedure compile should not report diagnostics");
  require(recursive_invoked.listing.find("call function spin") != std::string::npos,
          "recursive no-arg procedure should emit an initial function call");
  require(recursive_invoked.listing.find("tail call function spin") != std::string::npos,
          "recursive no-arg procedure should lower tail recursion to a jump");
  require(recursive_invoked.listing.find("implicit return from function spin") == std::string::npos,
          "recursive no-arg tail recursion should not emit a dead implicit return");
  require(has_optimization(recursive_invoked, "proc-call-lowering"),
          "recursive procedure call should report TS proc-call-lowering");

  const CompileResult invoked_with_arg = compile_source(R"mkpro(
program InvokeWithArg {
  state {
    total: counter 0..9 = 0
  }

  fn add(delta) {
    total += delta
  }

  loop {
    add(2)
    halt(total)
  }
}
)mkpro");
  require(invoked_with_arg.implemented, "native compiler should lower parameterized v2_invoke");
  require(invoked_with_arg.diagnostics.empty(),
          "parameterized invoke compile should not report diagnostics");
  require(invoked_with_arg.registers.find("delta") != invoked_with_arg.registers.end(),
          "parameterized invoke should allocate a register for the rule parameter");
  require(invoked_with_arg.listing.find("arg delta for add") != std::string::npos,
          "parameterized invoke listing should assign argument to parameter");
  require(invoked_with_arg.listing.find("call function add") != std::string::npos,
          "parameterized invoke listing should include function call");
  require(has_optimization(invoked_with_arg, "proc-call-lowering"),
          "parameterized procedure call should report TS proc-call-lowering");

  const CompileResult x_param_invoked = compile_source(R"mkpro(
program XParamInvoke {
  state {
    total: counter 0..9 = 0
  }

  fn set_to(value) {
    total = value
  }

  loop {
    set_to(4)
    halt(total)
  }
}
)mkpro");
  require(x_param_invoked.implemented, "native compiler should lower X-parameter v2_invoke");
  require(x_param_invoked.diagnostics.empty(), "X-parameter invoke should not report diagnostics");
  require(x_param_invoked.registers.find("value") == x_param_invoked.registers.end(),
          "X-parameter transport should not allocate a register for the rule parameter");
  require(x_param_invoked.listing.find("arg value") == std::string::npos,
          "X-parameter call should not store an argument register before the call");
  require(has_optimization(x_param_invoked, "x-param-proc-call"),
          "X-parameter invocation should report the TS x-param call optimization");
  require(has_optimization(x_param_invoked, "x-param-proc-entry"),
          "X-parameter invocation should report the TS x-param entry optimization");

  const CompileResult x_param_rule = compile_source(R"mkpro(
program XParamRule {
  state {
    key: counter 0..9 = 1
    pos: packed = 0
  }

  loop {
    match key {
      1 => go(1)
      2 => go(2)
      3 => go(3)
      otherwise => halt(0)
    }
  }

  fn go(delta) {
    pos = pos + delta
    show(pos)
  }
}
)mkpro");
  require(x_param_rule.implemented, "native compiler should lower shared X-parameter rules");
  require(x_param_rule.diagnostics.empty(), "X-parameter rule should not report diagnostics");
  require(has_optimization(x_param_rule, "x-param-proc-call"),
          "shared X-parameter rule should report x-param-proc-call");
  require(has_optimization(x_param_rule, "x-param-proc-entry"),
          "shared X-parameter rule should report x-param-proc-entry");
  require(x_param_rule.registers.find("delta") == x_param_rule.registers.end(),
          "shared X-parameter rule should not allocate the parameter register");
  require(x_param_rule.listing.find("set delta") == std::string::npos,
          "shared X-parameter rule should not materialize the parameter");

  const CompileResult repeated_literal_x_param = compile_source(R"mkpro(
program RepeatedLiteralXParam {
  state {
    best_score: packed = 0
    mark_score: packed = 0
    out: packed = 0
  }

  loop {
    best_score = -1
    mark_lines_and_check(-1)
    best_score = 1
    mark_lines_and_check(1)
    out = best_score + mark_score
    halt(out)
  }

  fn mark_lines_and_check(mark_value) {
    mark_score = mark_value
    mark_score += 1
    mark_score += 2
    mark_score += 3
  }
}
)mkpro");
  require(repeated_literal_x_param.implemented,
          "native compiler should keep repeated literals compatible with X parameters");
  require(repeated_literal_x_param.diagnostics.empty(),
          "repeated literal X-parameter program should not report diagnostics");
  require(has_optimization(repeated_literal_x_param, "x-param-proc-call"),
          "repeated literal X-parameter program should report x-param-proc-call");
  require(has_optimization(repeated_literal_x_param, "x-param-proc-entry"),
          "repeated literal X-parameter program should report x-param-proc-entry");
  require(!has_optimization(repeated_literal_x_param, "repeated-assignment-value-reuse"),
          "repeated literal reuse should not consume registerless X parameters");
  require(repeated_literal_x_param.registers.find("mark_value") ==
              repeated_literal_x_param.registers.end(),
          "repeated literal X-parameter should not allocate the parameter register");
  require(repeated_literal_x_param.listing.find("set mark_value") == std::string::npos,
          "repeated literal X-parameter should not materialize the parameter");

  const CompileResult x_param_current_x_identifier = compile_source(R"mkpro(
program XParamCurrentXIdentifier {
  state {
    carrier: packed = 0
    copied: packed = 0
    sink: packed = 0
  }

  loop {
    carrier = -1
    consume(carrier)
    consume(1)
    sink = copied + carrier
    halt(sink)
  }

  fn consume(value) {
    copied = value
    copied += 1
    copied += 2
    copied += 3
  }
}
)mkpro");
  require(x_param_current_x_identifier.implemented,
          "native compiler should reuse current X for identifier X-parameters");
  require(x_param_current_x_identifier.diagnostics.empty(),
          "current-X identifier X-parameter program should not report diagnostics");
  require(
      has_optimization_detail(x_param_current_x_identifier, "x-param-proc-call", "already in X"),
      "identifier-shaped X-parameter call should report already-in-X reuse");

  const CompileResult repeated_x_param_decay = compile_source(R"mkpro(
program RepeatedXParamDecay {
  state {
    pos: packed = 7
  }

  fn decay(value) {
    return value - int((value * 1) / 4)
  }

  loop {
    pos = decay(pos)
    pos = decay(pos)
    halt(pos)
  }
}
)mkpro");
  require(repeated_x_param_decay.implemented,
          "native compiler should lower repeated X-param return-decay assignments");
  require(repeated_x_param_decay.diagnostics.empty(),
          "repeated X-param return-decay compile should not report diagnostics");
  require(has_optimization(repeated_x_param_decay, "x-param-return-decay"),
          "return-decay helper body should report x-param-return-decay");
  require(has_optimization(repeated_x_param_decay, "x-param-return-decay-call"),
          "return-decay calls should report x-param-return-decay-call");
  require(has_optimization(repeated_x_param_decay, "repeated-x-param-self-assignment"),
          "adjacent self-assignments should report repeated-x-param-self-assignment");
  require(repeated_x_param_decay.listing.find("repeated decay base pos") != std::string::npos,
          "repeated X-param self-assignment should recall the base once before both calls");
  require(optimization_count(repeated_x_param_decay, "x-param-return-decay-call") == 2,
          "repeated X-param self-assignment should emit two return-decay calls");

  const CompileResult x_param_expression_rule = compile_source(R"mkpro(
program XParamExpressionRule {
  state {
    line: packed = 0
    pos: packed = 8
    other: packed = 7
  }

  loop {
    normalize(pos)
    normalize(other)
    halt(line)
  }

  fn normalize(value) {
    line = frac(int(value) / 4) * 4
    if line <= 0 {
      line += 4
    }
  }
}
)mkpro");
  require(x_param_expression_rule.implemented,
          "native compiler should lower expression-shaped X-parameter entries");
  require(x_param_expression_rule.diagnostics.empty(),
          "expression-shaped X-parameter program should not report diagnostics");
  require(has_optimization(x_param_expression_rule, "x-param-proc-call"),
          "expression-shaped X-parameter program should report x-param-proc-call");
  require(has_optimization(x_param_expression_rule, "x-param-proc-entry"),
          "expression-shaped X-parameter program should report x-param-proc-entry");
  require(x_param_expression_rule.registers.find("value") ==
              x_param_expression_rule.registers.end(),
          "expression-shaped X-parameter should not allocate the parameter register");
  require(x_param_expression_rule.listing.find("set value") == std::string::npos,
          "expression-shaped X-parameter should not materialize the parameter");
  require(x_param_expression_rule.listing.find("set line from X parameter expression") !=
              std::string::npos,
          "expression-shaped X-parameter should derive the first assignment from X");

  const CompileResult x_param_state_elision = compile_source(R"mkpro(
program XParamStateElision {
  state {
    value: packed = 0
    input: packed = 0
  }

  loop {
    capture(7)
    halt(value)
  }

  fn capture(input) {
    value = input
  }
}
)mkpro");
  require(x_param_state_elision.implemented,
          "native compiler should elide register-backed X-param state fields");
  require(x_param_state_elision.diagnostics.empty(),
          "X-param state elision compile should not report diagnostics");
  require(has_optimization(x_param_state_elision, "x-param-state-elision"),
          "X-param state field should report x-param-state-elision");
  require(x_param_state_elision.registers.find("input") == x_param_state_elision.registers.end(),
          "X-param state field should not allocate a register after elision");
  require(x_param_state_elision.listing.find("recall input") == std::string::npos,
          "X-param state elision should not recall the removed parameter state field");

  CompileOptions x_param_value_options;
  x_param_value_options.disable_candidate_search = true;
  x_param_value_options.x_param_value_functions = true;
  const CompileResult x_param_value_scratch = compile_source(R"mkpro(
program XParamValueScratch {
  state {
    wrapped: packed = 0
    value: packed = 0
  }

  fn wrap(value) {
    value = 4 * frac(int(value) / 4)
    if value <= 0 {
      return value + 4
    }
    return value
  }

  loop {
    wrapped = wrap(7)
    halt(wrapped)
  }
}
)mkpro",
                                                             x_param_value_options);
  require(x_param_value_scratch.implemented,
          "native compiler should materialize the X-param value-function scratch");
  require(x_param_value_scratch.diagnostics.empty(),
          "X-param value-function scratch compile should not report diagnostics");
  require(has_optimization(x_param_value_scratch, "x-param-value-function-scratch"),
          "X-param value-function strategy should report x-param-value-function-scratch");
  require(has_optimization(x_param_value_scratch, "x-param-value-function"),
          "X-param value-function body should report x-param-value-function");
  require(has_optimization(x_param_value_scratch, "x-param-value-function-call"),
          "X-param value-function call should report x-param-value-function-call");
  require(has_optimization(x_param_value_scratch, "x-param-value-state-elision"),
          "X-param value-function parameter state should report x-param-value-state-elision");
  require(x_param_value_scratch.registers.find("value") == x_param_value_scratch.registers.end(),
          "X-param value-function parameter state should not allocate a register after elision");
  require(x_param_value_scratch.registers.find("__mkpro_unary_arg_1") !=
              x_param_value_scratch.registers.end(),
          "X-param value-function scratch should allocate the TS repeated-unary scratch field");

  const CompileResult x_param_value_store_elision = compile_source(R"mkpro(
program XParamValueStoreElision {
  state {
    __mkpro_unary_arg_1: packed = 0
    wrapped: packed = 0
    value: packed = 0
  }

  fn wrap(value) {
    value = 4 * frac(int(value) / 4)
    if value <= 0 {
      return value + 4
    }
    return value
  }

  loop {
    __mkpro_unary_arg_1 = wrap(7)
    wrapped = __mkpro_unary_arg_1
    halt(wrapped)
  }
}
)mkpro",
                                                                   x_param_value_options);
  require(x_param_value_store_elision.implemented,
          "native compiler should elide X-param value scratch stores");
  require(x_param_value_store_elision.diagnostics.empty(),
          "X-param value scratch store elision compile should not report diagnostics");
  require(has_optimization(x_param_value_store_elision, "x-param-value-scratch-store-elision"),
          "assignment to the X-param value scratch should report store elision");
  require(has_optimization(x_param_value_store_elision, "x-param-value-function-call"),
          "store elision should reuse the X-param value-function call lowering");

  const CompileResult x_param_value_call_temp_reuse = compile_source(R"mkpro(
program XParamValueCallTempReuse {
  state {
    wrapped: packed = 0
    adjusted: packed = 0
    value: packed = 0
  }

  fn wrap(value) {
    value = 4 * frac(int(value) / 4)
    if value <= 0 {
      return value + 4
    }
    return value
  }

  fn plus_one(input) {
    return input + 1
  }

  loop {
    adjusted = read()
    wrapped = wrap(7) + plus_one(adjusted)
    halt(wrapped)
  }
}
)mkpro",
                                                                 x_param_value_options);
  require(x_param_value_call_temp_reuse.implemented,
          "native compiler should reuse X-param value scratch for nested call temps");
  require(x_param_value_call_temp_reuse.diagnostics.empty(),
          "X-param value call temp reuse compile should not report diagnostics");
  require(has_optimization(x_param_value_call_temp_reuse, "x-param-value-call-temp-reuse"),
          "X-param value call temp reuse should report the TS strategy name");
  require(x_param_value_call_temp_reuse.registers.find("__mkpro_call_1") ==
              x_param_value_call_temp_reuse.registers.end(),
          "X-param value call temp reuse should avoid the generic function-call scratch");
  require(x_param_value_call_temp_reuse.listing.find("recall __mkpro_unary_arg_1") !=
              std::string::npos,
          "X-param value call temp reuse should recall the existing X-param scratch");

  const CompileResult x_param_indexed_rule = compile_source(R"mkpro(
program XParamIndexedRule {
  state {
    slots: packed[1..3] = [1, 2, 3]
    first: counter 1..3 = 1
    second: counter 1..3 = 2
  }

  loop {
    bump(first)
    bump(second)
    halt(slots[2])
  }

  fn bump(pos) {
    slots[pos] = slots[pos] + 1
  }
}
)mkpro");
  require(x_param_indexed_rule.implemented,
          "native compiler should lower indexed X-parameter entries");
  require(x_param_indexed_rule.diagnostics.empty(),
          "indexed X-parameter program should not report diagnostics");
  require(has_optimization(x_param_indexed_rule, "x-param-proc-call"),
          "indexed X-parameter program should report x-param-proc-call");
  require(has_optimization(x_param_indexed_rule, "x-param-indexed-entry"),
          "indexed X-parameter program should report x-param-indexed-entry");
  require(x_param_indexed_rule.registers.find("pos") == x_param_indexed_rule.registers.end(),
          "indexed X-parameter should not allocate the parameter register");
  require(x_param_indexed_rule.listing.find("recall pos") == std::string::npos,
          "indexed X-parameter should not recall the parameter register");
  require(x_param_indexed_rule.listing.find("indexed set slots") != std::string::npos,
          "indexed X-parameter entry should emit the indexed store");

  const CompileResult loop_prompt_x = compile_source(R"mkpro(
program LoopPromptX {
  state {
    screen: packed = 0
    key: counter 0..9 = 0
    pos: packed = 1
  }

  loop {
    show(screen)
    key = read()
    if key == 0 {
      screen = 0
    }
    else {
      pos += key
      screen = pos
    }
  }
}
)mkpro");
  require(loop_prompt_x.implemented,
          "native compiler should carry eligible loop prompt state in X");
  require(loop_prompt_x.diagnostics.empty(),
          "loop-carried prompt compile should not report diagnostics");
  require(has_optimization(loop_prompt_x, "loop-carried-prompt-x"),
          "loop prompt state should report loop-carried-prompt-x");
  require(has_optimization(loop_prompt_x, "loop-carried-prompt-input-branch"),
          "loop prompt branch should report loop-carried-prompt-input-branch");
  require(loop_prompt_x.registers.find("screen") == loop_prompt_x.registers.end(),
          "loop-carried prompt state should not allocate a register");
  require(loop_prompt_x.registers.find("key") == loop_prompt_x.registers.end(),
          "loop-carried prompt branch should not allocate the read input");
  require(std::none_of(loop_prompt_x.steps.begin(), loop_prompt_x.steps.end(),
                       [](const ResolvedStep& step) { return step.comment == "set screen"; }),
          "loop-carried prompt state should not be stored to a register");
  require(std::none_of(loop_prompt_x.steps.begin(), loop_prompt_x.steps.end(),
                       [](const ResolvedStep& step) { return step.comment == "read key"; }),
          "loop-carried prompt branch should not store the input key");

  CompileOptions loop_prompt_decrement_options;
  loop_prompt_decrement_options.indirect_underflow_decrement = true;
  const CompileResult loop_prompt_decrement = compile_source(R"mkpro(
program LoopPromptBranchGuard {
  state {
    screen: packed = 0
    key: counter 0..9 = 0
    energy: counter 0..99 = 5
  }

  loop {
    show(screen)
    key = read()
    if key == 0 {
      screen = 0
    }
    else {
      energy--
      screen = 2
    }
    if energy <= 0 {
      halt(1)
    }
    halt(screen)
  }
}
)mkpro",
                                                             loop_prompt_decrement_options);
  require(loop_prompt_decrement.implemented,
          "native compiler should combine loop prompt branch and delayed decrement");
  require(loop_prompt_decrement.diagnostics.empty(),
          "loop prompt branch decrement compile should not report diagnostics");
  require(has_optimization(loop_prompt_decrement, "loop-carried-prompt-input-branch"),
          "loop prompt decrement should report loop-carried-prompt-input-branch");
  require(has_optimization(loop_prompt_decrement, "delayed-indirect-underflow-decrement"),
          "loop prompt decrement should report delayed-indirect-underflow-decrement");
  require(loop_prompt_decrement.registers.find("screen") == loop_prompt_decrement.registers.end(),
          "loop prompt decrement should not allocate the screen prompt");
  require(loop_prompt_decrement.registers.find("key") == loop_prompt_decrement.registers.end(),
          "loop prompt decrement should not allocate the prompt input");
  require(std::none_of(loop_prompt_decrement.steps.begin(), loop_prompt_decrement.steps.end(),
                       [](const ResolvedStep& step) { return step.comment == "read key"; }),
          "loop prompt decrement should not store the prompt input");

  const CompileResult loop_prompt_dispatch = compile_source(R"mkpro(
program LoopPromptDispatch {
  state {
    screen: packed = 0
    key: counter 0..9 = 0
    pos: packed = 1
  }

  loop {
    show(screen)
    key = read()
    match key {
      1 => screen = pos
      otherwise => screen = 0
    }
  }
}
)mkpro");
  require(loop_prompt_dispatch.implemented,
          "native compiler should dispatch directly on loop prompt input");
  require(loop_prompt_dispatch.diagnostics.empty(),
          "loop prompt dispatch compile should not report diagnostics");
  require(has_optimization(loop_prompt_dispatch, "loop-carried-prompt-input-dispatch"),
          "loop prompt dispatch should report loop-carried-prompt-input-dispatch");
  require(loop_prompt_dispatch.registers.find("screen") == loop_prompt_dispatch.registers.end(),
          "loop prompt dispatch should not allocate the screen prompt");
  require(loop_prompt_dispatch.registers.find("key") == loop_prompt_dispatch.registers.end(),
          "loop prompt dispatch should not allocate the prompt input");
  require(std::none_of(loop_prompt_dispatch.steps.begin(), loop_prompt_dispatch.steps.end(),
                       [](const ResolvedStep& step) { return step.comment == "read key"; }),
          "loop prompt dispatch should not store the prompt input");

  const CompileResult loop_prompt_guard_dispatch = compile_source(R"mkpro(
program LoopPromptGuardDispatch {
  state {
    screen: packed = 0
    key: counter 0..9 = 0
    food: counter 0..9 = 3
    pos: packed = 1
  }

  loop {
    show(screen)
    key = read()
    food--
    if food < 0 {
      loop {
      }
    }
    match key {
      1 => move()
      otherwise => halt(0)
    }
  }

  fn move() {
    pos += 1
    screen = pos
  }
}
)mkpro");
  require(loop_prompt_guard_dispatch.implemented,
          "native compiler should keep loop prompt input across decrement guard");
  require(loop_prompt_guard_dispatch.diagnostics.empty(),
          "loop prompt guard dispatch compile should not report diagnostics");
  require(has_optimization(loop_prompt_guard_dispatch, "loop-carried-prompt-decrement-underflow"),
          "loop prompt guarded dispatch should report loop-carried-prompt-decrement-underflow");
  require(loop_prompt_guard_dispatch.registers.find("screen") ==
              loop_prompt_guard_dispatch.registers.end(),
          "loop prompt guarded dispatch should not allocate the screen prompt");
  require(std::none_of(loop_prompt_guard_dispatch.steps.begin(),
                       loop_prompt_guard_dispatch.steps.end(),
                       [](const ResolvedStep& step) { return step.comment == "read key"; }),
          "loop prompt guarded dispatch should not store the prompt input");

  const CompileResult loop_prompt_indexed_guard_dispatch = compile_source(R"mkpro(
program LoopPromptIndexedGuardDispatch {
  state {
    screen: packed = 0
    key: counter 0..9 = 0
    resources: packed[1..2] = [3, 0]
    pos: packed = 1
  }

  loop {
    show(screen)
    key = read()
    resources[1] -= 1
    if resources[1] < 0 {
      loop {
      }
    }
    match key {
      1 => move()
      otherwise => halt(0)
    }
  }

  fn move() {
    pos += 1
    screen = pos
  }
}
)mkpro");
  require(loop_prompt_indexed_guard_dispatch.implemented,
          "native compiler should keep loop prompt input across indexed decrement guard");
  require(loop_prompt_indexed_guard_dispatch.diagnostics.empty(),
          "loop prompt indexed guard dispatch compile should not report diagnostics");
  require(
      has_optimization(loop_prompt_indexed_guard_dispatch,
                       "loop-carried-prompt-decrement-underflow"),
      "loop prompt indexed guard dispatch should report loop-carried-prompt-decrement-underflow");
  require(loop_prompt_indexed_guard_dispatch.registers.find("screen") ==
              loop_prompt_indexed_guard_dispatch.registers.end(),
          "loop prompt indexed guard dispatch should not allocate the screen prompt");
  require(std::none_of(loop_prompt_indexed_guard_dispatch.steps.begin(),
                       loop_prompt_indexed_guard_dispatch.steps.end(),
                       [](const ResolvedStep& step) { return step.comment == "read key"; }),
          "loop prompt indexed guard dispatch should not store the prompt input");

  const CompileResult loop_prompt_assigned_before_loop = compile_source(R"mkpro(
program LoopPromptAssignedBeforeLoop {
  state {
    screen: packed = 0
    key: counter 0..9 = 0
  }

  init()
  loop {
    show(screen)
    key = read()
    screen = key
  }

  fn init() {
    screen = 5
  }
}
)mkpro");
  require(loop_prompt_assigned_before_loop.implemented,
          "native compiler should compile prompt assigned before loop");
  require(loop_prompt_assigned_before_loop.diagnostics.empty(),
          "prompt assigned before loop should not report diagnostics");
  require(!has_optimization(loop_prompt_assigned_before_loop, "loop-carried-prompt-x"),
          "prompt assigned before loop should not use the loop-carried prompt rewrite");

  const CompileResult loop_prompt_stack_mirror = compile_source(R"mkpro(
program LoopPromptStackMirror {
  state {
    pos: packed = stack.X
    screen: packed = stack.X
    key: counter 0..9 = 0
  }

  loop {
    show(screen)
    key = read()
    if key == 0 {
      screen = 0
    }
    else {
      screen = pos
    }
  }
}
)mkpro");
  require(loop_prompt_stack_mirror.implemented,
          "native compiler should carry stack-mirrored prompt state in X");
  require(loop_prompt_stack_mirror.diagnostics.empty(),
          "stack-mirrored loop prompt should not report diagnostics");
  require(has_optimization(loop_prompt_stack_mirror, "loop-carried-prompt-x"),
          "stack-mirrored loop prompt should report loop-carried-prompt-x");
  require(loop_prompt_stack_mirror.registers.find("screen") ==
              loop_prompt_stack_mirror.registers.end(),
          "stack-mirrored loop prompt state should not allocate a register");

  CompileOptions x_param_y_stack_options;
  x_param_y_stack_options.disable_candidate_search = true;
  const std::string x_param_y_stack_source = R"mkpro(
program XParamYStackProcCall {
  state {
    lines: packed[4..7] = [44447.4, 44444.4, 44444.4, 44444.4]
    line: packed = 0
    delta: packed = 1
    slot: counter 0..7
  }

  loop {
    line = 2
    mark_one(4)
    line = 3
    mark_one(5)
    halt(lines[4])
  }

  fn mark_one(next_slot) {
    slot = next_slot
    lines[slot] = packed_add(lines[slot], line, delta)
    if bit_and(lines[slot], 88888834) != 8 {
      halt(bit_and(lines[slot], 88888834))
    }
  }
}
)mkpro";
  const CompileResult x_param_y_stack =
      compile_source(x_param_y_stack_source, x_param_y_stack_options);
  require(x_param_y_stack.implemented, "native compiler should lower X-param/Y-stack calls");
  require(x_param_y_stack.diagnostics.empty(),
          "X-param/Y-stack program should not report diagnostics");
  require(has_optimization(x_param_y_stack, "x-param-y-stack-proc-call"),
          "X-param/Y-stack program should report x-param-y-stack-proc-call");
  require(has_optimization(x_param_y_stack, "indexed-packed-y-stack-pow10-delta"),
          "X-param/Y-stack entry should reuse the pow10 index carried in Y");

  CompileOptions x_param_y_stack_stored_options = x_param_y_stack_options;
  x_param_y_stack_stored_options.x_param_y_stack_stored_entry = true;
  const CompileResult x_param_y_stack_stored =
      compile_source(x_param_y_stack_source, x_param_y_stack_stored_options);
  require(x_param_y_stack_stored.implemented,
          "native compiler should lower stored X-param/Y-stack entries");
  require(x_param_y_stack_stored.diagnostics.empty(),
          "stored X-param/Y-stack program should not report diagnostics");
  require(has_optimization(x_param_y_stack_stored, "x-param-y-stack-multi-entry"),
          "stored X-param/Y-stack program should report secondary entry lowering");
  require(has_optimization(x_param_y_stack_stored, "x-param-y-stack-stored-entry-call"),
          "stored X-param/Y-stack program should report stored-entry calls");
  require(has_optimization(x_param_y_stack_stored, "indexed-packed-x-stack-pow10-delta"),
          "stored X-param/Y-stack entry should reuse the pow10 index carried in X");
  require(x_param_y_stack_stored.listing.find("proc call mark_one y-stack entry") !=
              std::string::npos,
          "stored X-param/Y-stack call should jump to the secondary entry label");
  require(x_param_y_stack_stored.listing.find("set slot from X parameter before y-stack entry") !=
              std::string::npos,
          "stored X-param/Y-stack call should store the parameter before producing y");

  CompileOptions indexed_packed_report_options;
  indexed_packed_report_options.disable_candidate_search = true;
  const CompileResult indexed_packed_bit_report = compile_source(R"mkpro(
program Packed4BitReportBranch {
  state {
    lines: packed[1..4] = [44447.4, 44444.4, 44444.4, 44444.4]
    slot: counter 0..7 = 4
    line: packed = 1
  }
  loop {
    lines[slot - 3] = packed_add(lines[slot - 3], line, 1)
    if bit_and(lines[slot - 3], 88888834) != 8 {
      halt(bit_and(lines[slot - 3], 88888834))
    }
    halt(0)
  }
}
)mkpro",
                                                                 indexed_packed_report_options);
  require(indexed_packed_bit_report.implemented,
          "native compiler should lower indexed packed bit report branches");
  require(indexed_packed_bit_report.diagnostics.empty(),
          "indexed packed bit report program should not report diagnostics");
  require(has_optimization(indexed_packed_bit_report, "indexed-packed-bit-report-branch"),
          "indexed packed bit report program should report branch fusion");
  require(indexed_packed_bit_report.listing.find("updated packed report mask") != std::string::npos,
          "indexed packed bit report should reuse the updated packed value for the mask");
  require(indexed_packed_bit_report.listing.find("expr bit_and(lines") == std::string::npos,
          "indexed packed bit report should not recompile bit_and(lines...)");

  const CompileResult indexed_packed_fractional_report =
      compile_source(R"mkpro(
program Packed4FractionalBitReportBranch {
  state {
    lines: packed[1..4] = [44438.4, 44444.4, 44444.4, 44444.4]
    slot: counter 0..7 = 4
    line: packed = 1
  }
  loop {
    lines[slot - 3] = packed_add(lines[slot - 3], line, 1)
    if frac(bit_and(lines[slot - 3], 88888834)) != 0 {
      halt(bit_and(lines[slot - 3], 88888834))
    }
    halt(0)
  }
}
)mkpro",
                     indexed_packed_report_options);
  require(indexed_packed_fractional_report.implemented,
          "native compiler should lower indexed packed fractional report branches");
  require(indexed_packed_fractional_report.diagnostics.empty(),
          "indexed packed fractional report program should not report diagnostics");
  require(
      has_optimization(indexed_packed_fractional_report, "indexed-packed-fractional-report-branch"),
      "indexed packed fractional report program should report branch fusion");
  require(indexed_packed_fractional_report.listing.find(
              "updated packed fractional report predicate") != std::string::npos,
          "indexed packed fractional report should reuse the updated value for frac predicate");
  require(indexed_packed_fractional_report.listing.find(
              "updated packed fractional report restore") != std::string::npos,
          "indexed packed fractional report should restore the packed report before halt");
  require(indexed_packed_fractional_report.listing.find("expr bit_and(lines") == std::string::npos,
          "indexed packed fractional report should not recompile bit_and(lines...)");

  const CompileResult indexed_packed_report_temp = compile_source(R"mkpro(
program Packed4FractionalBitReportTemp {
  state {
    lines: packed[1..4] = [44438.4, 44444.4, 44444.4, 44444.4]
    slot: counter 0..7 = 4
    line: packed = 1
    report: packed = 0
  }
  loop {
    lines[slot - 3] = packed_add(lines[slot - 3], line, 1)
    report = bit_and(lines[slot - 3], 88888834)
    if frac(report) != 0 {
      halt(report)
    }
    halt(0)
  }
}
)mkpro",
                                                                  indexed_packed_report_options);
  require(indexed_packed_report_temp.implemented,
          "native compiler should inline indexed packed report temps");
  require(indexed_packed_report_temp.diagnostics.empty(),
          "indexed packed report temp program should not report diagnostics");
  require(has_optimization(indexed_packed_report_temp, "indexed-packed-report-temp-inline"),
          "indexed packed report temp program should report pre-liveness temp inline");
  require(has_optimization(indexed_packed_report_temp, "indexed-packed-fractional-report-branch"),
          "indexed packed report temp program should still use fractional branch fusion");
  require(indexed_packed_report_temp.registers.find("report") ==
              indexed_packed_report_temp.registers.end(),
          "inlined indexed packed report temp should not allocate a report register");
  require(indexed_packed_report_temp.registers.find("line") !=
              indexed_packed_report_temp.registers.end(),
          "inlined indexed packed report temp should leave unrelated state allocated");
  require(indexed_packed_report_temp.listing.find("set report") == std::string::npos,
          "inlined indexed packed report temp should not store report");
  require(indexed_packed_report_temp.listing.find("recall report") == std::string::npos,
          "inlined indexed packed report temp should not recall report");

  const CompileResult x_param_scoped_names = compile_source(R"mkpro(
program XParamScopedNames {
  state {
    left: packed[1..3] = [1, 2, 3]
    right: packed[1..3] = [4, 5, 6]
    first: counter 1..3 = 1
    second: counter 1..3 = 2
  }

  loop {
    bump_left(first)
    bump_left(second)
    bump_right(first)
    bump_right(second)
    halt(left[1] + right[2])
  }

  fn bump_left(pos) {
    left[pos] = left[pos] + 1
  }

  fn bump_right(pos) {
    right[pos] = right[pos] + 1
  }
}
)mkpro");
  require(x_param_scoped_names.implemented,
          "native compiler should scope X-parameter read counts per procedure");
  require(x_param_scoped_names.diagnostics.empty(),
          "scoped X-parameter program should not report diagnostics");
  require(optimization_count(x_param_scoped_names, "x-param-indexed-entry") == 2,
          "scoped X-parameter program should report one indexed entry per matching procedure");
  require(x_param_scoped_names.registers.find("pos") == x_param_scoped_names.registers.end(),
          "scoped indexed X-parameter should not allocate the shared parameter name");
  require(x_param_scoped_names.listing.find("recall pos") == std::string::npos,
          "scoped indexed X-parameter should not recall the shared parameter name");

  const CompileResult x_param_name_collision = compile_source(R"mkpro(
program XParamNameCollision {
  state {
    line: packed = 0
    pos: packed = 8
    other: packed = 7
    out: packed = 0
  }

  loop {
    normalize(pos)
    normalize(other)
    out = normalized(3)
    halt(out + line)
  }

  fn normalize(value) {
    line = frac(int(value) / 4) * 4
    if line <= 0 {
      line += 4
    }
  }

  fn normalized(value) {
    return value + 1
  }
}
)mkpro");
  require(x_param_name_collision.implemented,
          "native compiler should preserve same-named parameters needed by other procedures");
  require(x_param_name_collision.diagnostics.empty(),
          "same-name X-parameter program should not report diagnostics");
  require(has_optimization(x_param_name_collision, "x-param-proc-call"),
          "same-name X-parameter program should still report x-param-proc-call");
  require(has_optimization(x_param_name_collision, "x-param-proc-entry"),
          "same-name X-parameter program should still report x-param-proc-entry");
  require(x_param_name_collision.registers.find("value") != x_param_name_collision.registers.end(),
          "same-named parameter should remain allocated when another procedure needs storage");

  const CompileResult entered_current_x = compile_source(R"mkpro(
program EnteredCurrentX {
  state {
    x: counter 0..9 = 0
    y: counter 0..9 = 0
  }

  loop {
    x = entered()
    y = entered()
    halt(y)
  }
}
)mkpro");
  require(entered_current_x.implemented, "native compiler should lower entered()");
  require(entered_current_x.diagnostics.empty(), "entered() compile should not report diagnostics");
  require(entered_current_x.listing.find("read()") == std::string::npos,
          "entered() should consume current X without emitting a read stop");
  require(has_optimization(entered_current_x, "entered-current-x"),
          "entered() should report the TS strategy name");

  CompileOptions input_branch_options;
  input_branch_options.analysis = true;
  const CompileResult input_branch = compile_source(R"mkpro(
program InputBranch {
  state {
    target: counter -9..9 = 0
  }

  loop {
    target = read()
    if target >= 0 {
      halt(1)
    }
    else {
      halt(-1)
    }
  }
}
)mkpro",
                                                    input_branch_options);
  require(input_branch.implemented, "native compiler should lower ephemeral input branches");
  require(input_branch.diagnostics.empty(),
          "ephemeral input branch compile should not report diagnostics");
  require(input_branch.registers.find("target") == input_branch.registers.end(),
          "ephemeral input branch should not allocate the read target");
  require(has_optimization(input_branch, "ephemeral-input-branch"),
          "ephemeral input branch should report the TS optimization name");
  require(!input_branch.steps.empty() && input_branch.steps.front().hex == "50",
          "ephemeral input branch should start with the calculator stop/read opcode");

  CompileOptions input_dispatch_options;
  input_dispatch_options.analysis = true;
  const CompileResult input_dispatch = compile_source(R"mkpro(
program InputDispatch {
  loop {
    target = read()
    match target {
      1 => halt(1)
      2 => halt(2)
      otherwise => halt(0)
    }
  }
}
)mkpro",
                                                      input_dispatch_options);
  require(input_dispatch.implemented, "native compiler should lower ephemeral input dispatch");
  require(input_dispatch.diagnostics.empty(),
          "ephemeral input dispatch compile should not report diagnostics");
  require(input_dispatch.registers.find("target") == input_dispatch.registers.end(),
          "ephemeral input dispatch should not allocate the read target");
  require(has_optimization(input_dispatch, "ephemeral-input-dispatch"),
          "ephemeral input dispatch should report the TS optimization name");
  require(!input_dispatch.steps.empty() && input_dispatch.steps.front().hex == "50",
          "ephemeral input dispatch should start with the calculator stop/read opcode");

  const CompileResult stored_read = compile_source(R"mkpro(
program StoredRead {
  state {
    key: counter 0..9 = 0
    other: counter 0..9 = 0
  }

  loop {
    key = read()
    other = 1
    halt(key + other)
  }
}
)mkpro");
  require(stored_read.implemented, "native compiler should lower stored read inputs");
  require(stored_read.diagnostics.empty(), "stored read compile should not report diagnostics");
  require(has_optimization(stored_read, "intent-read-lowering"),
          "stored read should report intent-read-lowering");

  const CompileResult packed_line_macros = compile_source(R"mkpro(
program PackedLineMacros {
  state {
    line: packed = 44444.4
    slot: counter 0..7 = 4
    score: packed = 0
  }

  loop {
    line = packed_add(line, slot, 1)
    score = packed_score(line, slot) + packed_digit(line, slot)
    halt(score)
  }
}
)mkpro");
  require(packed_line_macros.implemented, "native compiler should lower packed-line macros");
  require(packed_line_macros.diagnostics.empty(),
          "packed-line macro compile should not report diagnostics");
  require(packed_line_macros.listing.find("packed_score") == std::string::npos,
          "packed_score should expand before final listing");
  require(packed_line_macros.listing.find("frac()") != std::string::npos,
          "packed_score/packed_digit expansion should use frac()");

  const CompileResult packed_score_helper = compile_source(R"mkpro(
program PackedScoreHelper {
  state {
    line: packed = 44444.4
    slot: counter 0..7 = 4
    score: packed = 0
  }

  loop {
    score = packed_score(line, slot)
    score = score + packed_score(line, slot)
    score = score + packed_score(line, slot)
    halt(score)
  }
}
)mkpro");
  require(packed_score_helper.implemented,
          "native compiler should lower repeated packed_score calls");
  require(packed_score_helper.diagnostics.empty(),
          "repeated packed_score compile should not report diagnostics");
  require(packed_score_helper.listing.find("packed_score helper") != std::string::npos,
          "three packed_score calls should use the shared stack helper");

  const CompileResult packed_line_score_proc = compile_source(R"mkpro(
program PackedLineScoreProc {
  state {
    lines: packed[4..7] = [44444.4, 44444.4, 44444.4, 44444.4]
    x: counter 0..5 = 4
    y: counter 0..5 = 4
    line: packed = 0
    score: packed = 0
  }

  fn score_move() {
    score = packed_score(lines[7], x) + packed_score(lines[6], y)
    normalize(x + y)
    score += packed_score(lines[5], line)
    normalize(x - y)
    score += packed_score(lines[4], line)
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }

  loop {
    score_move()
    halt(score)
  }
}
)mkpro");
  require(packed_line_score_proc.implemented,
          "native compiler should lower packed-line score procedure shape");
  require(packed_line_score_proc.diagnostics.empty(),
          "packed-line score procedure compile should not report diagnostics");
  require(packed_line_score_proc.listing.find("packed-line score helper accumulate") !=
              std::string::npos,
          "four-term packed_score procedure should use the packed-line accumulator helper");

  const CompileResult packed_line_stack_score = compile_source(R"mkpro(
program PackedLineStackScore {
  state {
    lines: packed[4..7] = [44444.4, 44444.4, 44444.4, 44444.4]
    x: counter 0..5 = 4
    y: counter 0..5 = 4
    best_y: counter 0..5 = 0
    best_score: packed = 0
    score: packed = 0
    line: packed = 0
    slot: counter 0..7 = 4
  }

  fn score_move() {
    score = packed_score(lines[7], x) + packed_score(lines[6], y)
    normalize(x + y)
    score += packed_score(lines[5], line)
    normalize(x - y)
    score += packed_score(lines[4], line)
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }

  loop {
    best_score = 0
    score_move()
    best_score = max(score, best_score)
    if score == best_score {
      slot = x
      best_y = y
    }
    halt(best_score)
  }
}
)mkpro");
  require(packed_line_stack_score.implemented,
          "native compiler should lower stack-only packed-line score consumers");
  require(packed_line_stack_score.diagnostics.empty(),
          "stack-only packed-line score compile should not report diagnostics");
  require(packed_line_stack_score.registers.find("score") ==
              packed_line_stack_score.registers.end(),
          "stack-only packed-line score should not allocate a score register");
  require(packed_line_stack_score.listing.find("set score") == std::string::npos,
          "stack-only packed-line score should not store score");
  require(packed_line_stack_score.listing.find("recall score") == std::string::npos,
          "stack-only packed-line score should not recall score");
  require(has_optimization(packed_line_stack_score, "stack-only-state-field"),
          "stack-only packed-line score should report stack-only-state-field");
  require(packed_line_stack_score.listing.find("max-assignment equality compare") !=
              std::string::npos,
          "stack-only packed-line score should fuse the following max/equality branch");
  require(packed_line_stack_score.listing.find("zero-accumulator entry") != std::string::npos,
          "stack-only packed-line score should enter after the redundant zero literal");

  const CompileResult packed_line_update_proc = compile_source(R"mkpro(
program PackedLineUpdateProc {
  state {
    lines: packed[4..7] = [44444.4, 44444.4, 44444.4, 44444.4]
    aa: packed = 0
    ab: packed = 0
    x: counter 0..5 = 1
    y: counter 0..5 = 2
    best_score: packed = 0
    line: packed = 0
    slot: counter 0..7 = 4
  }

  fn mark_one(next_slot) {
    slot = next_slot
    lines[slot] = packed_add(lines[slot], line, best_score)
    report = bit_and(lines[slot], 88888834)
    if frac(report) != 0 {
      halt(report)
    }
  }

  fn mark_lines_and_check(mark_sign) {
    best_score = mark_sign
    line = x
    mark_one(7)
    line = y
    mark_one(5)
    line = x + y
    mark_one(6)
    line = x - y
    mark_one(4)
  }

  loop {
    mark_lines_and_check(1)
    halt(best_score)
  }
}
)mkpro");
  require(packed_line_update_proc.implemented,
          "native compiler should lower packed-line update/check procedure shape");
  require(packed_line_update_proc.diagnostics.empty(),
          "packed-line update/check procedure compile should not report diagnostics");
  require(packed_line_update_proc.listing.find("packed-line shared update/check tail") !=
              std::string::npos,
          "four packed-line marker calls should use one shared update/check tail");
  require(packed_line_update_proc.listing.find("call function mark_one") == std::string::npos,
          "consumed packed-line update helper should not be emitted as a separate call body");

  const CompileResult packed_line_mutating_update_proc = compile_source(R"mkpro(
program PackedLineMutatingUpdateProc {
  state {
    lines: packed[4..7] = [44444.4, 44444.4, 44444.4, 44444.4]
    x: counter 0..5 = 1
    y: counter 0..5 = 2
    best_score: packed = 0
    line: packed = 0
    slot: counter 0..7 = 4
  }

  fn mark_one(next_slot) {
    slot = next_slot
    lines[slot] = packed_add(lines[slot], line, best_score)
    report = bit_and(lines[slot], 88888834)
    if frac(report) != 0 {
      halt(report)
    }
  }

  fn mark_lines_and_check(mark_sign) {
    best_score = mark_sign
    line = x
    mark_one(7)
    line = y
    mark_one(6)
    line = x + y
    mark_one(5)
    line = x - y
    mark_one(4)
  }

  loop {
    mark_lines_and_check(1)
    halt(best_score)
  }
}
)mkpro");
  require(packed_line_mutating_update_proc.implemented,
          "native compiler should lower descending packed-line update/check procedure shape");
  require(packed_line_mutating_update_proc.diagnostics.empty(),
          "descending packed-line update/check procedure compile should not report diagnostics");
  require(packed_line_mutating_update_proc.listing.find(
              "packed-line mutating shared update/check tail") != std::string::npos,
          "descending marker calls should use the mutating selector shared tail");
  require(packed_line_mutating_update_proc.listing.find("call function mark_one") ==
              std::string::npos,
          "mutating packed-line update helper should not be emitted as a separate call body");

  const CompileResult bit_or_test_and_set = compile_source(R"mkpro(
program BitOrTestAndSet {
  state {
    occupied: packed = 0
    mask: packed = 0
    result: packed = 0
  }

  fn occupied_cell() {
    result = 9
  }

  fn mark(sign) {
    result = sign
  }

  loop {
    mask = 4
    if bit_and(occupied, mask) != 0 {
      occupied_cell()
    }
    else {
      occupied = bit_or(occupied, mask)
      mark(-1)
    }
    halt(result)
  }
}
)mkpro");
  require(bit_or_test_and_set.implemented,
          "native compiler should lower bit_or test-and-set branches");
  require(bit_or_test_and_set.diagnostics.empty(),
          "bit_or test-and-set branch compile should not report diagnostics");
  require(has_optimization(bit_or_test_and_set, "bit-or-test-and-set-branch"),
          "bit_or test-and-set branch should report the TS strategy name");
  require(has_optimization(bit_or_test_and_set, "bit-or-test-and-set-negative-arg"),
          "bit_or test-and-set negative argument should report the TS strategy name");
  require(bit_or_test_and_set.listing.find("bit_or test-and-set occupied") != std::string::npos,
          "membership/update branch should fuse into bit_or test-and-set lowering");
  require(bit_or_test_and_set.listing.find("bit_or test-and-set sign = -1") != std::string::npos,
          "negative x-parameter argument should be derived from changed value");
  require(bit_or_test_and_set.listing.find("assign mask") == std::string::npos,
          "test-and-set mask temporary should not be spilled before the branch");

  const CompileResult counted_while = compile_source(R"mkpro(
program CountedWhile {
  state {
    ticks: counter 0..3 = 0
    total: counter 0..9 = 0
  }

  loop {
    ticks = 3
    total = 0
    while ticks >= 1 {
      total += 1
      ticks -= 1
    }
    halt(total)
  }
}
)mkpro");
  require(counted_while.implemented,
          "native compiler should lower initialized counted while loops");
  require(counted_while.diagnostics.empty(), "counted while compile should not report diagnostics");
  require(has_optimization(counted_while, "initialized-counted-while-loop"),
          "counted while should report initialized-counted-while-loop");
  require(counted_while.listing.find("counted while ticks") != std::string::npos,
          "initialized decrementing while should use an F Lx counted loop back edge");
  require(counted_while.listing.find("while loop back") == std::string::npos,
          "counted while should not emit the generic while back edge");

  const CompileResult repeated_init_counted = compile_source(R"mkpro(
program RepeatedInitDoesNotHideCounted {
  state {
    score: counter 0..9 = 0
    x: counter 0..4 = 0
    total: counter 0..99 = 0
  }

  loop {
    score = 4
    x = 4
    while x >= 1 {
      total += score
      x--
    }
    halt(total)
  }
}
)mkpro");
  require(repeated_init_counted.implemented,
          "native compiler should reuse repeated counted-loop initializer values");
  require(repeated_init_counted.diagnostics.empty(),
          "repeated counted-loop initializer compile should not report diagnostics");
  require(has_optimization(repeated_init_counted, "initialized-counted-while-loop"),
          "repeated counted-loop initializer should still report initialized-counted-while-loop");
  require(has_optimization(repeated_init_counted, "repeated-assignment-counted-loop-reuse"),
          "repeated counted-loop initializer should report repeated-assignment-counted-loop-reuse");
  require(!has_optimization(repeated_init_counted, "repeated-assignment-value-reuse"),
          "repeated counted-loop initializer should not hide the counted-loop strategy behind "
          "repeated-assignment-value-reuse");
  require(repeated_init_counted.listing.find("counted while x") != std::string::npos,
          "repeated counted-loop initializer should use an F Lx counted loop back edge");

  for (const char* condition : {"ticks > 0", "1 <= ticks", "0 < ticks"}) {
    const CompileResult counted_while_alt = compile_source(std::string(R"mkpro(
program CountedWhileAlt {
  state {
    ticks: counter 0..3 = 0
    total: counter 0..9 = 0
  }

  loop {
    ticks = 3
    total = 0
    while )mkpro") + condition + R"mkpro( {
      total += 1
      ticks -= 1
    }
    halt(total)
  }
}
)mkpro");
    require(counted_while_alt.implemented,
            "native compiler should lower alternate initialized counted while loops");
    require(counted_while_alt.diagnostics.empty(),
            "alternate counted while compile should not report diagnostics");
    require(has_optimization(counted_while_alt, "initialized-counted-while-loop"),
            "alternate counted while should report initialized-counted-while-loop");
    require(counted_while_alt.listing.find("counted while ticks") != std::string::npos,
            "alternate initialized decrementing while should use an F Lx counted loop back edge");
  }

  const CompileResult decrement_test = compile_source(R"mkpro(
program DecrementTest {
  state {
    remaining: counter 0..9 = 3
    result: counter 0..9 = 0
  }

  loop {
    remaining -= 1
    if remaining <= 0 {
      result = 9
    }
    else {
      result = -remaining
    }
    halt(result)
  }
}
)mkpro");
  require(decrement_test.implemented,
          "native compiler should lower decrement/test counter branches");
  require(decrement_test.diagnostics.empty(),
          "decrement/test compile should not report diagnostics");
  require(decrement_test.listing.find("decrement/test remaining") != std::string::npos,
          "counter decrement followed by <= 0 should use an FL decrement/test branch");
  require(decrement_test.listing.find("set remaining") == std::string::npos,
          "fused decrement/test should not emit a separate counter store");

  const CompileResult expression_call = compile_source(R"mkpro(
program ExpressionCall {
  state {
    result: counter 0..9 = 0
  }

  fn inc(value) {
    return value + 1
  }

  loop {
    result = inc(2)
    halt(result)
  }
}
)mkpro");
  require(expression_call.implemented, "native compiler should lower expression function call");
  require(expression_call.diagnostics.empty(),
          "expression function call should not report diagnostics");
  require(expression_call.registers.find("value") != expression_call.registers.end(),
          "expression call should allocate a register for the rule parameter");
  require(expression_call.listing.find("arg value for inc") != std::string::npos,
          "expression call listing should assign argument to parameter");
  require(expression_call.listing.find("call function inc") != std::string::npos,
          "expression call listing should include function call");
  require(expression_call.listing.find("recall value") != std::string::npos,
          "expression call listing should recall the function parameter before using it");
  bool has_ts_function_body_order = false;
  for (std::size_t index = 0; index + 3 < expression_call.steps.size(); ++index) {
    if (expression_call.steps.at(index).opcode == 0x01 &&
        expression_call.steps.at(index + 1).opcode == 0x61 &&
        expression_call.steps.at(index + 2).opcode == 0x10 &&
        expression_call.steps.at(index + 3).opcode == 0x52) {
      has_ts_function_body_order = true;
      break;
    }
  }
  require(has_ts_function_body_order, "expression call should match TS function body byte order");
  require(expression_call.listing.find("return value") != std::string::npos,
          "expression call listing should include function return");

  const CompileResult x_param_stack_stop = compile_source(R"mkpro(
program XParamStackStopRisk {
  state {
    result: packed = 0
  }

  fn risk(stake) {
    show(stake)
    return int(stake * (1 + sin(read())))
  }

  loop {
    result = risk(7)
    halt(result)
  }
}
)mkpro");
  require(x_param_stack_stop.implemented,
          "native compiler should lower X-parameter stack-stop-risk functions");
  require(x_param_stack_stop.diagnostics.empty(),
          "X-parameter stack-stop-risk compile should not report diagnostics");
  require(x_param_stack_stop.registers.find("stake") == x_param_stack_stop.registers.end(),
          "X-parameter stack-stop-risk parameter should not allocate a register");
  require(has_optimization(x_param_stack_stop, "show-read-stack-stop-risk-lowering"),
          "single-use X-parameter stack-stop-risk should report show/read lowering");
  require(has_optimization(x_param_stack_stop, "x-param-stack-stop-risk-inline"),
          "single-use X-parameter stack-stop-risk should inline the helper");
  require(x_param_stack_stop.listing.find("x-param keep displayed value") != std::string::npos,
          "X-parameter stack-stop-risk lowering should park the displayed value in Y");
  require(x_param_stack_stop.listing.find("risk input read()") != std::string::npos,
          "X-parameter stack-stop-risk lowering should consume read() directly from X");
  require(x_param_stack_stop.listing.find("call function risk") == std::string::npos,
          "single-use X-parameter stack-stop-risk should not call a helper");
  require(x_param_stack_stop.listing.find("x-param stack-stop-risk return") == std::string::npos,
          "single-use X-parameter stack-stop-risk should not emit a helper return");

  const CompileResult x_param_stack_stop_multi = compile_source(R"mkpro(
program XParamStackStopRiskMultiUse {
  state {
    first: packed = 0
    second: packed = 0
  }

  fn risk(stake) {
    show(stake)
    return int(stake * (1 + sin(read())))
  }

  loop {
    first = risk(7)
    second = risk(8)
    halt(first + second)
  }
}
)mkpro");
  require(x_param_stack_stop_multi.implemented,
          "native compiler should keep multi-use stack-stop-risk functions callable");
  require(x_param_stack_stop_multi.diagnostics.empty(),
          "multi-use X-parameter stack-stop-risk compile should not report diagnostics");
  require(has_optimization(x_param_stack_stop_multi, "x-param-stack-stop-risk-call"),
          "multi-use X-parameter stack-stop-risk should report the call path");
  require(has_optimization(x_param_stack_stop_multi, "x-param-stack-stop-risk-read"),
          "multi-use X-parameter stack-stop-risk should report the helper read path");
  require(!has_optimization(x_param_stack_stop_multi, "x-param-stack-stop-risk-inline"),
          "multi-use X-parameter stack-stop-risk should not report inline lowering");
  require(x_param_stack_stop_multi.listing.find("call function risk") != std::string::npos,
          "multi-use X-parameter stack-stop-risk should call the helper");
  require(x_param_stack_stop_multi.listing.find("x-param stack-stop-risk return") !=
              std::string::npos,
          "multi-use X-parameter stack-stop-risk should emit a helper return");

  const CompileResult function_local = compile_source(R"mkpro(
program FunctionLocal {
  state {
    result: counter 0..99 = 0
  }

  fn sample(delta) {
    scratch = read()
    return scratch + delta
  }

  loop {
    result = sample(2)
    halt(result)
  }
}
)mkpro");
  require(function_local.implemented,
          "native compiler should allocate locals discovered inside function bodies");
  require(function_local.diagnostics.empty(),
          "function-local compile should not report diagnostics");
  require(function_local.registers.find("scratch") != function_local.registers.end(),
          "function-local scratch variable should allocate a register");
  require(function_local.listing.find("read scratch") != std::string::npos,
          "function-local read should lower inside function body");

  const CompileResult builtin_call = compile_source(R"mkpro(
program BuiltinCalls {
  state {
    result: counter -99..99 = 0
    mask: packed = 0
  }

  loop {
    result = int(abs(random()) * 10)
    mask = bit_xor(bit_or(1, 2), bit_and(7, 3))
    result = max(result, frac(mask))
    result = min(result, 7)
    result = cos(100)
    halt(result)
  }
}
)mkpro");
  require(builtin_call.implemented, "native compiler should lower intrinsic built-in calls");
  require(builtin_call.diagnostics.empty(), "built-in call compile should not report diagnostics");
  require(builtin_call.listing.find("random()") != std::string::npos,
          "built-in call listing should include random()");
  require(builtin_call.listing.find("abs()") != std::string::npos,
          "built-in call listing should include abs()");
  require(builtin_call.listing.find("int()") != std::string::npos,
          "built-in call listing should include int()");
  require(builtin_call.listing.find("bit_xor()") != std::string::npos,
          "built-in call listing should include bit_xor()");
  require(builtin_call.listing.find("max()") != std::string::npos,
          "built-in call listing should include max()");
  require(builtin_call.listing.find("min negated max") != std::string::npos,
          "built-in call listing should include min()");
  require(builtin_call.registers.find("__min_scratch") != builtin_call.registers.end(),
          "min() should allocate a reusable scratch register");
  require(builtin_call.listing.find("cos()") != std::string::npos,
          "built-in call listing should include cos()");

  const CompileResult digit_at_call = compile_source(R"mkpro(
program DigitAtCall {
  state {
    packed: counter 0..99999999 = 12345678
    index: counter 1..8 = 1
    result: counter 0..9 = 0
  }

  loop {
    result = digit_at(packed, index)
    show(result)
  }
}
)mkpro");
  require(digit_at_call.implemented, "native compiler should lower digit_at()");
  require(digit_at_call.diagnostics.empty(), "digit_at compile should not report diagnostics");
  require(digit_at_call.listing.find("digit_at place") != std::string::npos,
          "digit_at should compute a decimal place");
  require(digit_at_call.listing.find("digit_at()") != std::string::npos,
          "digit_at should extract an integer digit");

  const CompileResult small_set_call = compile_source(R"mkpro(
program SmallSetCalls {
  state {
    room: counter 0..20 = 10
    target: counter 0..20 = 11
    hazard: counter 0..20 = 12
    margin: counter -20..20 = 0
    match: counter -999..999 = 0
  }

  loop {
    margin = near_any(room, 1, target, hazard)
    match = eq_any(room, target, hazard)
    show(margin)
    show(match)
  }
}
)mkpro");
  require(small_set_call.implemented,
          "native compiler should lower near_any()/eq_any() small-set macros");
  require(small_set_call.diagnostics.empty(),
          "small-set macro compile should not report diagnostics");
  require(small_set_call.listing.find("abs()") != std::string::npos,
          "near_any should lower through absolute distance");
  require(small_set_call.listing.find("max()") != std::string::npos,
          "near_any should combine multiple candidates through max()");
  require(small_set_call.listing.find("assign match") != std::string::npos,
          "eq_any should produce an assignable expression result");

  const CompileResult bounded_random_call = compile_source(R"mkpro(
program BoundedRandomCalls {
  state {
    result: counter 0..99 = 0
  }

  loop {
    result = random(10)
    result = random(0, 100)
    halt(result)
  }
}
)mkpro");
  require(bounded_random_call.implemented,
          "native compiler should lower zero-based bounded random calls");
  require(bounded_random_call.diagnostics.empty(),
          "bounded random compile should not report diagnostics");
  int random_range_count = 0;
  for (const ResolvedStep& step : bounded_random_call.steps) {
    if (step.comment == "random range")
      ++random_range_count;
  }
  require(random_range_count == 2, "bounded random calls should multiply by their range");

  const CompileResult prior_random_stack_reuse = compile_source(R"mkpro(
program PriorRandomStackReuse {
  state {
    seed: packed = 0.5
    scratch: packed = 0
  }

  fn any_seed_update_name() {
    seed = random()
  }

  loop {
    scratch = seed
    any_seed_update_name()
    scratch = int((1 + scratch + seed) * 20) / 1000
    halt(scratch)
  }
}
)mkpro");
  require(prior_random_stack_reuse.implemented,
          "native compiler should keep prior random seed on stack");
  require(prior_random_stack_reuse.diagnostics.empty(),
          "prior random stack reuse compile should not report diagnostics");
  require(has_optimization(prior_random_stack_reuse, "prior-random-stack-reuse"),
          "prior random stack reuse should report prior-random-stack-reuse");
  require(prior_random_stack_reuse.listing.find("prior random keep seed") != std::string::npos,
          "prior random stack reuse should keep the old seed in Y");
  require(prior_random_stack_reuse.listing.find("prior random additive term") != std::string::npos,
          "prior random stack reuse should consume additive terms without spilling");

  const CompileResult prior_random_branch = compile_source(R"mkpro(
program PriorRandomBranch {
  state {
    random_state: packed = 0.5
    scratch: packed = 0
    score: packed = 0
  }

  scratch = random_state
  random_state = random()
  if scratch - random_state < 0 {
    score = 1
  }
  else {
    score = 2
  }
  halt(score)
}
)mkpro");
  require(prior_random_branch.implemented,
          "native compiler should branch on prior random value without spilling it");
  require(prior_random_branch.diagnostics.empty(),
          "prior random branch compile should not report diagnostics");
  require(has_optimization(prior_random_branch, "prior-random-branch-stack-reuse"),
          "prior random branch should report prior-random-branch-stack-reuse");
  require(prior_random_branch.listing.find("set scratch") == std::string::npos,
          "prior random branch should not spill the previous random value");

  const CompileResult prior_random_branch_proc = compile_source(R"mkpro(
program PriorRandomBranchProc {
  state {
    random_state: packed = 0.5
    scratch: packed = 0
    score: packed = 0
  }

  fn roll() {
    random_state = random()
  }

  scratch = random_state
  roll()
  if scratch - random_state < 0 {
    score = 1
  }
  else {
    score = 2
  }
  halt(score)
}
)mkpro");
  require(prior_random_branch_proc.implemented,
          "native compiler should inline proc random refresh for prior random branch");
  require(prior_random_branch_proc.diagnostics.empty(),
          "prior random branch proc compile should not report diagnostics");
  require(has_optimization(prior_random_branch_proc, "prior-random-branch-stack-reuse"),
          "prior random branch proc should report prior-random-branch-stack-reuse");
  require(std::any_of(prior_random_branch_proc.steps.begin(), prior_random_branch_proc.steps.end(),
                      [](const ResolvedStep& step) { return step.opcode == 0x3b; }),
          "prior random branch proc should inline the random draw");
  require(prior_random_branch_proc.listing.find("set scratch") == std::string::npos,
          "prior random branch proc should not spill the previous random value");

  const CompileResult prior_random_fractional_decrement = compile_source(R"mkpro(
program PriorRandomFractionalDecrement {
  state {
    random_state: packed = 0.5
    scratch: packed = 0
    slot: counter 1..2 = 1
    event_target: counter 1..2 = 2
    cells: packed[1..2] = [5000.999, 25000]
  }

  fn roll() {
    random_state = random()
  }

  fn lost() {
    halt("ЕГГОГ")
  }

  scratch = random_state
  roll()
  scratch = int((scratch + random_state + 1) * 20 * cells[slot] / cells[event_target]) / 1000
  if frac(cells[slot]) - scratch <= 0 {
    lost()
  }
  else {
    cells[slot] -= scratch
  }
  halt(cells[slot])
}
)mkpro");
  require(prior_random_fractional_decrement.implemented,
          "native compiler should lower prior-random fractional decrement");
  require(prior_random_fractional_decrement.diagnostics.empty(),
          "prior-random fractional decrement compile should not report diagnostics");
  require(has_optimization(prior_random_fractional_decrement, "prior-random-fractional-decrement"),
          "prior-random fractional decrement should report the TS strategy name");
  require(prior_random_fractional_decrement.listing.find("fractional decrement amount frac") !=
              std::string::npos,
          "prior-random fractional decrement should keep frac(amount) on the stack");
  require(prior_random_fractional_decrement.listing.find(
              "fractional decrement domain-error guard trap") != std::string::npos,
          "prior-random fractional decrement should use the fused domain-error guard");
  require(prior_random_fractional_decrement.listing.find("set scratch") == std::string::npos,
          "prior-random fractional decrement should not spill the random scratch value");

  const CompileResult single_use_stack_temp = compile_source(R"mkpro(
program SingleUseStackTemp {
  state {
    a: packed = 4
    tmp: packed = 0
    out: packed = 0
  }

  loop {
    tmp = a + 1
    out = tmp * 2
    halt(out)
  }
}
)mkpro");
  require(single_use_stack_temp.implemented,
          "native compiler should keep a single-use temporary in X");
  require(single_use_stack_temp.diagnostics.empty(),
          "single-use stack temp compile should not report diagnostics");
  require(has_optimization(single_use_stack_temp, "stack-current-x-scheduling"),
          "single-use stack temp should report stack-current-x-scheduling");
  require(single_use_stack_temp.listing.find("set tmp") == std::string::npos,
          "single-use stack temp should not spill tmp");

  const CompileResult read_expression_call = compile_source(R"mkpro(
program ReadExpression {
  state {
    result: counter 0..99 = 0
  }

  loop {
    result = int(read())
    if read() >= 0 {
      result += 1
    }
    halt(result)
  }
}
)mkpro");
  require(read_expression_call.implemented, "native compiler should lower read() expressions");
  require(read_expression_call.diagnostics.empty(),
          "read() expression compile should not report diagnostics");
  int read_expression_count = 0;
  for (const ResolvedStep& step : read_expression_call.steps) {
    if (step.comment == "read()")
      ++read_expression_count;
  }
  require(read_expression_count == 2, "read() expressions should emit input stops");
  require(read_expression_call.listing.find("int()") != std::string::npos,
          "read() expression should compose with unary intrinsics");

  const CompileResult floor_sugar = compile_source(R"mkpro(
program FloorSugar {
  state {
    pos: packed = 305
    floor: counter 0..9 = 0
  }

  loop {
    floor = pos.floor
    halt(floor)
  }
}
)mkpro");
  require(floor_sugar.implemented, "native compiler should normalize .floor expressions");
  require(floor_sugar.diagnostics.empty(), ".floor compile should not report diagnostics");
  require(floor_sugar.listing.find("int()") != std::string::npos,
          ".floor should lower through int(pos / 100)");

  const CompileResult constant_indexed_state = compile_source(R"mkpro(
program ConstantIndexedState {
  state {
    slots: packed[1..3] = 0
    x: packed = 0
  }

  loop {
    x = read()
    slots[2] = x
    slots[2] -= 1
    x = 0
    halt(slots[2])
  }
}
)mkpro");
  require(constant_indexed_state.implemented,
          "native compiler should lower constant indexed state access");
  require(constant_indexed_state.diagnostics.empty(),
          "constant indexed state compile should not report diagnostics");
  require(constant_indexed_state.registers.find("slots_2") !=
              constant_indexed_state.registers.end(),
          "constant indexed state should allocate scalar bank elements");
  require(constant_indexed_state.listing.find("indexed set slots[2]") != std::string::npos,
          "constant indexed assignment should target the scalar bank element");
  require(constant_indexed_state.listing.find("indexed recall slots[2]") != std::string::npos,
          "constant indexed recall should target the scalar bank element");
  require(constant_indexed_state.listing.find("expr -") != std::string::npos,
          "constant indexed compound update should lower arithmetic before storing");
  require(has_optimization(constant_indexed_state, "constant-indexed-state-resolution"),
          "constant indexed state should report the TS optimization name");

  CompileOptions comparison_guarded_update_options;
  comparison_guarded_update_options.budget = 999;
  comparison_guarded_update_options.analysis = true;
  comparison_guarded_update_options.comparison_guarded_update_selectors = true;
  const CompileResult comparison_guarded_update = compile_source(R"mkpro(
program SourceComparisonMaskCorrections {
  state {
    tile: counter 0..9 = 7
    strength: counter -99..99 = 40
    score: counter 0..99 = 0
  }

  loop {
    strength += 4 - tile
    score += tile - 2
    if tile == 7 {
      strength--
      score++
    }
    halt(score + strength)
  }
}
)mkpro",
                                                                 comparison_guarded_update_options);
  require(comparison_guarded_update.implemented,
          "comparison guarded update correction should compile");
  require(has_optimization(comparison_guarded_update, "comparison-guarded-update-correction"),
          "comparison guarded update correction should report the TS strategy name");
  require(std::none_of(comparison_guarded_update.steps.begin(),
                       comparison_guarded_update.steps.end(),
                       [](const ResolvedStep& step) {
                         return step.comment.has_value() && *step.comment == "false branch for ==";
                       }),
          "comparison guarded update correction should remove the guarded branch");

  const CompileResult dynamic_indexed_state = compile_source(R"mkpro(
program DynamicIndexedState {
  state {
    slots: packed[1..2] = 0
    index: counter 1..2 = 2
    x: packed = 0
  }

  loop {
    slots[index] = 4
    x = slots[index]
    halt(x)
  }
}
)mkpro");
  require(dynamic_indexed_state.implemented,
          "native compiler should lower dynamic indexed state access");
  require(dynamic_indexed_state.diagnostics.empty(),
          "dynamic indexed state compile should not report diagnostics");
  require(dynamic_indexed_state.registers.find("__indexed_selector") ==
              dynamic_indexed_state.registers.end(),
          "dynamic indexed state should not allocate the obsolete anonymous selector");
  require(dynamic_indexed_state.registers.find("__bank_selector_slots") !=
              dynamic_indexed_state.registers.end(),
          "dynamic indexed state should allocate the named state-bank selector");
  require(dynamic_indexed_state.registers.find("__indexed_value") ==
              dynamic_indexed_state.registers.end(),
          "indirect dynamic indexed state should not allocate a branch-chain value scratch");
  require(dynamic_indexed_state.listing.find("indexed selector slots") != std::string::npos,
          "dynamic indexed state should materialize the indirect-memory selector");
  require(dynamic_indexed_state.listing.find("indexed set slots") != std::string::npos,
          "dynamic indexed assignment should use indirect-memory store");
  require(dynamic_indexed_state.listing.find("indexed recall slots") != std::string::npos,
          "dynamic indexed recall should use indirect-memory recall");

  CompileOptions preincrement_indexed_options;
  preincrement_indexed_options.budget = 999;
  preincrement_indexed_options.analysis = true;
  preincrement_indexed_options.disable_interprocedural_opts = true;
  preincrement_indexed_options.disable_candidate_search = true;
  const CompileResult preincrement_indexed_store = compile_source(R"mkpro(
program IndexedPreincrementStore {
  state {
    slots: packed[2..4] = 0
    pointer: counter 1..4 = 1
    value: packed = 7
  }

  loop {
    slots[pointer + 1] = value
    pointer++
    halt(slots[2])
  }
}
)mkpro",
                                                                  preincrement_indexed_options);
  require(preincrement_indexed_store.implemented,
          "native compiler should lower preincrement indexed store");
  require(has_optimization(preincrement_indexed_store, "preincrement-indexed-store"),
          "preincrement indexed store should report the TS strategy name");
  require(std::any_of(preincrement_indexed_store.steps.begin(),
                      preincrement_indexed_store.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.comment.has_value() &&
                               *step.comment == "preincrement indexed set slots";
                      }),
          "preincrement indexed store should use the fused indirect store comment");
  require(std::none_of(preincrement_indexed_store.steps.begin(),
                       preincrement_indexed_store.steps.end(),
                       [](const ResolvedStep& step) {
                         return step.comment.has_value() && *step.comment == "increment pointer";
                       }),
          "preincrement indexed store should consume the separate pointer increment");

  const CompileResult zero_fallback_store = compile_source(R"mkpro(
program ZeroFallbackStore {
  state {
    target: counter 0..9 = 0
    random_state: packed = 0.5
  }

  target = int(random_state * 13 / 3)
  if target == 0 {
    target = 7
  }
  show(random_state)
  halt(target)
}
)mkpro");
  require(zero_fallback_store.implemented, "native compiler should lower zero fallback store");
  require(has_optimization(zero_fallback_store, "assign-zero-fallback-store"),
          "zero fallback store should report the TS strategy name");
  require(std::count_if(zero_fallback_store.steps.begin(), zero_fallback_store.steps.end(),
                        [](const ResolvedStep& step) {
                          return step.comment.has_value() && *step.comment == "set target";
                        }) == 1,
          "zero fallback store should store target once");

  const CompileResult max_assign_equality = compile_source(R"mkpro(
program MaxAssignEqualityBranch {
  state {
    best: packed = -1
    score: packed = 0
    cell: counter 0..9 = 3
    chosen: counter 0..9 = 0
  }
  loop {
    score = read()
    best = max(score, best)
    if score == best {
      chosen = cell
    }
    halt(chosen + best)
  }
}
)mkpro");
  require(max_assign_equality.implemented,
          "native compiler should lower max assignment equality branch");
  require(has_optimization(max_assign_equality, "max-assign-equality-branch"),
          "max assignment equality branch should report the TS strategy name");
  const auto max_step =
      std::find_if(max_assign_equality.steps.begin(), max_assign_equality.steps.end(),
                   [](const ResolvedStep& step) { return step.opcode == 0x36; });
  require(max_step != max_assign_equality.steps.end(),
          "max assignment equality branch should emit K max");
  const std::size_t max_index =
      static_cast<std::size_t>(std::distance(max_assign_equality.steps.begin(), max_step));
  require(max_index + 2U < max_assign_equality.steps.size(),
          "max assignment equality branch should have store and compare after K max");
  require(max_assign_equality.steps.at(max_index + 1U).comment.has_value() &&
              max_assign_equality.steps.at(max_index + 1U).comment->find("set best") !=
                  std::string::npos,
          "max assignment equality branch should store best after K max");
  require(max_assign_equality.steps.at(max_index + 2U).opcode == 0x11,
          "max assignment equality branch should compare immediately after storing best");

  const CompileResult arithmetic_max = compile_source(R"mkpro(
program ArithmeticIfMax {
  state {
    left: packed = 3
    right: packed = 5
    result: packed = 0
  }
  loop {
    if left > right {
      result = left
    }
    else {
      result = right
    }
    halt(result)
  }
}
)mkpro");
  require(arithmetic_max.implemented, "native compiler should lower arithmetic-if max");
  require(arithmetic_max.diagnostics.empty(), "arithmetic-if max should not report diagnostics");
  require(has_optimization(arithmetic_max, "branch-removal"),
          "arithmetic-if max should report branch-removal");
  require(has_optimization(arithmetic_max, "arithmetic-if-max"),
          "arithmetic-if max should report the TS strategy name");
  require(std::none_of(arithmetic_max.steps.begin(), arithmetic_max.steps.end(),
                       [](const ResolvedStep& step) {
                         return step.comment.has_value() &&
                                step.comment->find("false branch for >") != std::string::npos;
                       }),
          "arithmetic-if max should not emit the original branch");

  const CompileResult arithmetic_min = compile_source(R"mkpro(
program ArithmeticIfMin {
  state {
    left: packed = 3
    right: packed = 5
    result: packed = 0
  }
  loop {
    if left < right {
      result = left
    }
    else {
      result = right
    }
    halt(result)
  }
}
)mkpro");
  require(arithmetic_min.implemented, "native compiler should lower arithmetic-if min");
  require(arithmetic_min.diagnostics.empty(), "arithmetic-if min should not report diagnostics");
  require(has_optimization(arithmetic_min, "branch-removal"),
          "arithmetic-if min should report branch-removal");
  require(has_optimization(arithmetic_min, "arithmetic-if-min"),
          "arithmetic-if min should report the TS strategy name");

  const CompileResult arithmetic_abs = compile_source(R"mkpro(
program ArithmeticIfAbs {
  state {
    value: packed = -3
  }
  loop {
    if value < 0 {
      value = -value
    }
    halt(value)
  }
}
)mkpro");
  require(arithmetic_abs.implemented, "native compiler should lower arithmetic-if abs");
  require(arithmetic_abs.diagnostics.empty(), "arithmetic-if abs should not report diagnostics");
  require(has_optimization(arithmetic_abs, "branch-removal"),
          "arithmetic-if abs should report branch-removal");
  require(has_optimization(arithmetic_abs, "arithmetic-if-abs"),
          "arithmetic-if abs should report the TS strategy name");
  require(std::any_of(arithmetic_abs.steps.begin(), arithmetic_abs.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.mnemonic == "К |x|" && step.comment == "abs()";
                      }),
          "arithmetic-if abs should lower through abs()");

  const CompileResult arithmetic_clamp = compile_source(R"mkpro(
program ArithmeticIfClamp {
  state {
    value: packed = -3
  }
  loop {
    if value < 0 {
      value = 0
    }
    halt(value)
  }
}
)mkpro");
  require(arithmetic_clamp.implemented, "native compiler should lower arithmetic-if clamp");
  require(arithmetic_clamp.diagnostics.empty(),
          "arithmetic-if clamp should not report diagnostics");
  require(has_optimization(arithmetic_clamp, "branch-removal"),
          "arithmetic-if clamp should report branch-removal");
  require(has_optimization(arithmetic_clamp, "arithmetic-if-max"),
          "lower clamp should report arithmetic-if-max");

  const CompileResult arithmetic_double_clamp = compile_source(R"mkpro(
program ArithmeticIfDoubleClamp {
  state {
    value: packed = 12
  }
  loop {
    if value < 0 {
      value = 0
    }
    if value > 9 {
      value = 9
    }
    halt(value)
  }
}
)mkpro");
  require(arithmetic_double_clamp.implemented,
          "native compiler should lower arithmetic-if double clamp");
  require(arithmetic_double_clamp.diagnostics.empty(),
          "arithmetic-if double clamp should not report diagnostics");
  require(has_optimization(arithmetic_double_clamp, "branch-removal"),
          "arithmetic-if double clamp should report branch-removal");
  require(has_optimization(arithmetic_double_clamp, "arithmetic-if-double-clamp"),
          "arithmetic-if double clamp should report the TS strategy name");
  require(std::none_of(arithmetic_double_clamp.steps.begin(),
                       arithmetic_double_clamp.steps.end(), [](const ResolvedStep& step) {
                         return step.comment.has_value() &&
                                step.comment->find("false branch for") != std::string::npos;
                       }),
          "arithmetic-if double clamp should not emit the original branch tests");

  const CompileResult arithmetic_select = compile_source(R"mkpro(
program ArithmeticIfSelect {
  state {
    flag: flag = 0
    result: counter 0..99 = 0
  }
  loop {
    if flag == 1 {
      result = 50
    }
    else {
      result = 10
    }
    halt(result)
  }
}
)mkpro");
  require(arithmetic_select.implemented, "native compiler should lower arithmetic-if select");
  require(arithmetic_select.diagnostics.empty(),
          "arithmetic-if select should not report diagnostics");
  require(has_optimization(arithmetic_select, "branch-removal"),
          "arithmetic-if select should report branch-removal");
  require(has_optimization(arithmetic_select, "arithmetic-if-select"),
          "arithmetic-if select should report the TS strategy name");
  require(std::none_of(arithmetic_select.steps.begin(), arithmetic_select.steps.end(),
                       [](const ResolvedStep& step) {
                         return step.comment.has_value() &&
                                step.comment->find("false branch for ==") != std::string::npos;
                       }),
          "arithmetic-if select should not emit the original branch test");

  const CompileResult arithmetic_comparison_mask = compile_source(R"mkpro(
program ArithmeticIfComparisonMask {
  state {
    tile: counter 0..9 = 7
    matched: flag = 0
  }
  loop {
    if tile == 7 {
      matched = 1
    }
    else {
      matched = 0
    }
    halt(matched)
  }
}
)mkpro");
  require(arithmetic_comparison_mask.implemented,
          "native compiler should lower comparison boolean masks");
  require(arithmetic_comparison_mask.diagnostics.empty(),
          "arithmetic-if comparison mask should not report diagnostics");
  require(has_optimization(arithmetic_comparison_mask, "branch-removal"),
          "arithmetic-if comparison mask should report branch-removal");
  require(has_optimization(arithmetic_comparison_mask, "arithmetic-if-comparison-mask"),
          "arithmetic-if comparison mask should report the TS strategy name");
  require(std::none_of(arithmetic_comparison_mask.steps.begin(),
                       arithmetic_comparison_mask.steps.end(), [](const ResolvedStep& step) {
                         return step.comment.has_value() &&
                                step.comment->find("false branch for ==") != std::string::npos;
                       }),
          "arithmetic-if comparison mask should not emit the original branch test");

  const CompileResult arithmetic_boolean_and = compile_source(R"mkpro(
program ArithmeticIfBooleanAnd {
  state {
    flag: flag = 0
    other: flag = 1
    result: flag = 0
  }
  loop {
    if flag == 1 {
      result = other
    }
    else {
      result = 0
    }
    halt(result)
  }
}
)mkpro");
  require(arithmetic_boolean_and.implemented,
          "native compiler should lower boolean algebra AND");
  require(arithmetic_boolean_and.diagnostics.empty(),
          "arithmetic-if boolean algebra should not report diagnostics");
  require(has_optimization(arithmetic_boolean_and, "branch-removal"),
          "arithmetic-if boolean algebra should report branch-removal");
  require(has_optimization(arithmetic_boolean_and, "arithmetic-if-boolean-algebra"),
          "arithmetic-if boolean algebra should report the TS strategy name");

  const CompileResult arithmetic_boolean_or = compile_source(R"mkpro(
program ArithmeticIfBooleanOr {
  state {
    flag: flag = 0
    other: flag = 1
    result: flag = 0
  }
  loop {
    if flag == 1 {
      result = 1
    }
    else {
      result = other
    }
    halt(result)
  }
}
)mkpro");
  require(arithmetic_boolean_or.implemented,
          "native compiler should lower boolean algebra OR");
  require(arithmetic_boolean_or.diagnostics.empty(),
          "arithmetic-if boolean OR should not report diagnostics");
  require(has_optimization(arithmetic_boolean_or, "arithmetic-if-boolean-algebra"),
          "arithmetic-if boolean OR should report the TS strategy name");

  const CompileResult arithmetic_boolean_xor = compile_source(R"mkpro(
program ArithmeticIfBooleanXor {
  state {
    flag: flag = 0
    other: flag = 1
    result: flag = 0
  }
  loop {
    if flag == 1 {
      result = 1 - other
    }
    else {
      result = other
    }
    halt(result)
  }
}
)mkpro");
  require(arithmetic_boolean_xor.implemented,
          "native compiler should lower boolean algebra XOR");
  require(arithmetic_boolean_xor.diagnostics.empty(),
          "arithmetic-if boolean XOR should not report diagnostics");
  require(has_optimization(arithmetic_boolean_xor, "arithmetic-if-boolean-algebra"),
          "arithmetic-if boolean XOR should report the TS strategy name");

  const CompileResult arithmetic_sign_toggle = compile_source(R"mkpro(
program ArithmeticIfSignToggle {
  state {
    flag: flag = 0
    value: packed = 3
  }
  loop {
    if flag == 1 {
      value = -value
    }
    else {
      value = value
    }
    halt(value)
  }
}
)mkpro");
  require(arithmetic_sign_toggle.implemented,
          "native compiler should lower arithmetic-if sign toggle");
  require(arithmetic_sign_toggle.diagnostics.empty(),
          "arithmetic-if sign toggle should not report diagnostics");
  require(has_optimization(arithmetic_sign_toggle, "branch-removal"),
          "arithmetic-if sign toggle should report branch-removal");
  require(has_optimization(arithmetic_sign_toggle, "arithmetic-if-sign-toggle"),
          "arithmetic-if sign toggle should report the TS strategy name");

  const CompileResult arithmetic_update = compile_source(R"mkpro(
program ArithmeticIfUpdate {
  state {
    flag: flag = 0
    score: counter 0..99 = 0
  }
  loop {
    if flag == 1 {
      score = score + 4
    }
    halt(score)
  }
}
)mkpro");
  require(arithmetic_update.implemented, "native compiler should lower arithmetic-if update");
  require(arithmetic_update.diagnostics.empty(),
          "arithmetic-if update should not report diagnostics");
  require(has_optimization(arithmetic_update, "branch-removal"),
          "arithmetic-if update should report branch-removal");
  require(has_optimization(arithmetic_update, "arithmetic-if-update"),
          "arithmetic-if update should report the TS strategy name");

  const CompileResult arithmetic_conditional_move = compile_source(R"mkpro(
program ArithmeticIfConditionalMove {
  state {
    flag: flag = 0
    score: counter 0..99 = 0
    bonus: counter 0..99 = 4
  }
  loop {
    if flag == 1 {
      score = bonus
    }
    halt(score)
  }
}
)mkpro");
  require(arithmetic_conditional_move.implemented,
          "native compiler should lower arithmetic-if conditional move");
  require(arithmetic_conditional_move.diagnostics.empty(),
          "arithmetic-if conditional move should not report diagnostics");
  require(has_optimization(arithmetic_conditional_move, "branch-removal"),
          "arithmetic-if conditional move should report branch-removal");
  require(has_optimization(arithmetic_conditional_move, "arithmetic-if-conditional-move"),
          "arithmetic-if conditional move should report the TS strategy name");

  CompileOptions comparison_update_options;
  comparison_update_options.comparison_guarded_update_selectors = true;
  const CompileResult arithmetic_comparison_update = compile_source(R"mkpro(
program ArithmeticIfComparisonUpdate {
  state {
    tile: counter 0..9 = 7
    score: counter 0..99 = 0
  }
  loop {
    if tile == 7 {
      score++
    }
    halt(score)
  }
}
)mkpro",
                                                                 comparison_update_options);
  require(arithmetic_comparison_update.implemented,
          "native compiler should lower comparison guarded arithmetic updates");
  require(arithmetic_comparison_update.diagnostics.empty(),
          "arithmetic-if comparison update should not report diagnostics");
  require(has_optimization(arithmetic_comparison_update, "branch-removal"),
          "arithmetic-if comparison update should report branch-removal");
  require(has_optimization(arithmetic_comparison_update, "arithmetic-if-comparison-update"),
          "arithmetic-if comparison update should report the TS strategy name");
  require(std::none_of(arithmetic_comparison_update.steps.begin(),
                       arithmetic_comparison_update.steps.end(), [](const ResolvedStep& step) {
                         return step.comment.has_value() &&
                                step.comment->find("false branch for ==") != std::string::npos;
                       }),
          "arithmetic-if comparison update should not emit the original branch test");

  const CompileResult boundary_normalize = compile_source(R"mkpro(
program BoundaryNormalize {
  state {
    fuel: counter 0..9 = 4
  }
  loop {
    if fuel <= -1 {
      fuel--
      show(0)
    }
    halt(fuel)
  }
}
)mkpro");
  require(boundary_normalize.implemented,
          "native compiler should normalize adjacent integer comparison bounds");
  require(boundary_normalize.diagnostics.empty(),
          "boundary normalization should not report diagnostics");
  require(has_optimization(boundary_normalize, "comparison-boundary-normalization"),
          "boundary normalization should report the TS strategy name");

  const CompileResult difference_zero_normalize = compile_source(R"mkpro(
program DifferenceZeroNormalize {
  state {
    left: packed = 3
    right: packed = 5
    result: packed = 0
  }
  loop {
    if left - right <= 0 {
      result = 1
    }
    else {
      result = 2
    }
    halt(result)
  }
}
)mkpro");
  require(difference_zero_normalize.implemented,
          "native compiler should normalize subtraction-against-zero comparisons");
  require(difference_zero_normalize.diagnostics.empty(),
          "difference zero normalization should not report diagnostics");
  require(has_optimization(difference_zero_normalize, "comparison-boundary-normalization"),
          "difference zero normalization should report the TS strategy name");
  require(has_optimization_detail(difference_zero_normalize,
                                  "comparison-boundary-normalization", "right - left >= 0"),
          "difference zero normalization should preserve the TS normalized predicate shape");

  const CompileResult condition_current_x_reuse = compile_source(R"mkpro(
program ConditionCurrentXReuse {
  state {
    seen: counter 0..9 = 0
    target: counter 0..9 = 5
  }
  loop {
    seen = read()
    if target == seen {
      target++
    }
    halt(target)
  }
}
)mkpro");
  require(condition_current_x_reuse.implemented,
          "native compiler should reuse current X in equality conditions");
  require(condition_current_x_reuse.diagnostics.empty(),
          "condition current-X reuse should not report diagnostics");
  require(has_optimization(condition_current_x_reuse, "condition-current-x-reuse"),
          "condition current-X reuse should report the TS strategy name");
  const auto current_x_compare =
      std::find_if(condition_current_x_reuse.steps.begin(),
                   condition_current_x_reuse.steps.end(), [](const ResolvedStep& step) {
                     return step.comment == "condition compare";
                   });
  require(current_x_compare != condition_current_x_reuse.steps.end(),
          "condition current-X reuse should still emit a comparison");
  require(current_x_compare == condition_current_x_reuse.steps.begin() ||
              std::prev(current_x_compare)->comment != "recall seen",
          "condition current-X reuse should not recall the value already in X");

  const CompileResult current_x_negated_zero = compile_source(R"mkpro(
program CurrentXNegatedZeroTest {
  state {
    line: packed = 0
  }
  loop {
    line = frac(int(read()) / 4) * 4
    if line <= 0 {
      line += 4
    }
    halt(line)
  }
}
)mkpro");
  require(current_x_negated_zero.implemented,
          "native compiler should negate current X for compact zero tests");
  require(current_x_negated_zero.diagnostics.empty(),
          "current-X negated zero test should not report diagnostics");
  require(has_optimization(current_x_negated_zero, "current-x-negated-zero-test"),
          "current-X negated zero test should report the TS strategy name");
  require(std::any_of(current_x_negated_zero.steps.begin(), current_x_negated_zero.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.mnemonic == "/-/" &&
                               step.comment == "negate line for zero test";
                      }),
          "current-X negated zero test should emit the TS negate step");
  require(std::none_of(current_x_negated_zero.steps.begin(), current_x_negated_zero.steps.end(),
                       [](const ResolvedStep& step) {
                         return step.comment == "condition compare";
                       }),
          "current-X negated zero test should avoid materialized-zero comparison");

  const CompileResult negated_zero = compile_source(R"mkpro(
program NegatedZeroComparison {
  state {
    energy: counter -99..99 = 5
    other: counter 0..9 = 0
  }
  loop {
    other++
    if energy <= 0 {
      halt(1)
    }
    halt(0)
  }
}
)mkpro");
  require(negated_zero.implemented,
          "native compiler should negate non-current values for compact zero tests");
  require(negated_zero.diagnostics.empty(), "negated zero test should not report diagnostics");
  require(has_optimization(negated_zero, "negated-zero-test"),
          "negated zero test should report the TS strategy name");
  require(std::any_of(negated_zero.steps.begin(), negated_zero.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.mnemonic == "/-/" &&
                               step.comment == "negate energy for zero test";
                      }),
          "negated zero test should emit the TS negate step");
  require(std::none_of(negated_zero.steps.begin(), negated_zero.steps.end(),
                       [](const ResolvedStep& step) {
                         return step.comment == "condition compare";
                       }),
          "negated zero test should avoid materialized-zero comparison");

  const CompileResult indexed_store_guard = compile_source(R"mkpro(
program IndexedStoreGuard {
  state {
    cells: packed[1..2] = [5, 0]
    index: counter 1..2 = 1
    scratch: packed = 6
  }

  loop {
    cells[index] -= scratch
    if cells[index] < 0 {
      halt("ЕГГОГ")
    } else {
      halt(cells[index])
    }
  }
}
)mkpro");
  require(indexed_store_guard.implemented,
          "native compiler should fuse indexed stores with immediate domain guards");
  require(indexed_store_guard.diagnostics.empty(),
          "indexed store guard compile should not report diagnostics");
  require(std::any_of(indexed_store_guard.optimizations.begin(),
                      indexed_store_guard.optimizations.end(),
                      [](const OptimizationReport& optimization) {
                        return optimization.name == "indexed-assign-zero-domain-guard";
                      }),
          "indexed store guard should report the TS optimization name");
  require(std::any_of(indexed_store_guard.steps.begin(), indexed_store_guard.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x21 &&
                               step.comment == "indexed assign domain-error guard trap";
                      }),
          "indexed < 0 guard should use F sqrt on the stored value");

  const CompileResult indexed_equality_guard = compile_source(R"mkpro(
program IndexedEqualityGuard {
  state {
    cells: packed[1..2] = [5, 0]
    index: counter 1..2 = 1
    scratch: packed = 6
  }

  loop {
    cells[index] -= scratch
    if cells[index] == 0 {
      halt("ЕГГОГ")
    } else {
      halt(cells[index])
    }
  }
}
)mkpro");
  require(indexed_equality_guard.implemented,
          "native compiler should fuse indexed equality domain guards");
  require(indexed_equality_guard.diagnostics.empty(),
          "indexed equality guard compile should not report diagnostics");
  require(std::any_of(indexed_equality_guard.optimizations.begin(),
                      indexed_equality_guard.optimizations.end(),
                      [](const OptimizationReport& optimization) {
                        return optimization.name == "indexed-assign-zero-domain-guard";
                      }),
          "indexed equality guard should report the TS optimization name");
  require(std::any_of(indexed_equality_guard.steps.begin(), indexed_equality_guard.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x23 &&
                               step.comment == "indexed assign domain-error guard trap";
                      }),
          "indexed == 0 guard should use F 1/x on the stored value");

  const CompileResult complex_dynamic_indexed_state = compile_source(R"mkpro(
program ComplexDynamicIndexedState {
  state {
    slots: packed[1..3] = 0
    index: counter 0..2 = 1
  }

  loop {
    slots[index + 1] = 7
    halt(slots[index + 1])
  }
}
)mkpro");
  require(complex_dynamic_indexed_state.implemented,
          "native compiler should lower complex dynamic indexed state access");
  require(complex_dynamic_indexed_state.diagnostics.empty(),
          "complex dynamic indexed state compile should not report diagnostics");
  require(complex_dynamic_indexed_state.registers.find("__bank_selector_slots") !=
              complex_dynamic_indexed_state.registers.end(),
          "complex dynamic indexed state should materialize the TS-compatible bank selector");
  require(complex_dynamic_indexed_state.registers.find("__indexed_selector") ==
              complex_dynamic_indexed_state.registers.end(),
          "complex dynamic indexed state should not allocate the obsolete anonymous selector");
  require(complex_dynamic_indexed_state.listing.find("indexed selector slots") != std::string::npos,
          "complex dynamic indexed state should materialize the selector expression");

  CompileOptions indexed_selector_cache_options;
  indexed_selector_cache_options.budget = 999;
  indexed_selector_cache_options.analysis = true;
  const CompileResult indexed_selector_cache = compile_source(R"mkpro(
program IndexedSelectorSiblingReuse {
  state {
    line: group(1..3) {
      front: packed = 10
      robots: packed = 0
    }
    i: counter 1..2 = 1
    j: counter 0..1 = 1
    order: packed = 3
  }

  loop {
    line[i + j + j].robots += order
    line[i + j + j].front -= 1
    halt(line[1].front)
  }
}
)mkpro",
                                                        indexed_selector_cache_options);
  require(indexed_selector_cache.implemented,
          "native compiler should lower sibling indexed selector cache reuse");
  require(indexed_selector_cache.diagnostics.empty(),
          "sibling indexed selector cache compile should not report diagnostics");
  require(has_optimization(indexed_selector_cache, "indexed-selector-cache"),
          "sibling indexed selector cache should report the TS strategy name");
  require(std::any_of(indexed_selector_cache.steps.begin(), indexed_selector_cache.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.comment == "indexed selector reuse line.front";
                      }),
          "sibling indexed selector cache should recall a cached selector");

  const CompileResult int_dynamic_indexed_state = compile_source(R"mkpro(
program IntDynamicIndexedState {
  state {
    slots: packed[1..3] = 0
    pos: coord(cave) = 1.0000008
  }

  cave: board(packed_decimal_zero_run)

  loop {
    slots[int(pos)] = 7
    halt(slots[int(pos)])
  }
}
)mkpro");
  require(int_dynamic_indexed_state.implemented,
          "native compiler should lower int(identifier) indexed state access");
  require(int_dynamic_indexed_state.diagnostics.empty(),
          "int(identifier) indexed state compile should not report diagnostics");
  require(int_dynamic_indexed_state.registers.find("__bank_selector_slots") !=
              int_dynamic_indexed_state.registers.end(),
          "int(identifier) indexed state should use the named state-bank selector");
  require(int_dynamic_indexed_state.listing.find("indexed selector slots") != std::string::npos,
          "int(identifier) indexed state should materialize integer selector expression");

  const CompileResult current_x_indexed_selector = compile_source(R"mkpro(
program CurrentXIndexedSelector {
  state {
    slots: packed[1..3] = 0
    index: counter 1..3 = 1
    value: packed = 0
  }

  loop {
    index = read()
    value = slots[index]
    halt(value)
  }
}
)mkpro");
  require(current_x_indexed_selector.implemented,
          "native compiler should lower current-X indexed selector reuse");
  require(current_x_indexed_selector.diagnostics.empty(),
          "current-X indexed selector compile should not report diagnostics");
  require(has_optimization(current_x_indexed_selector, "current-x-indexed-selector"),
          "current-X indexed selector reuse should report the TS strategy name");

  const CompileResult affine_indexed_selector_reuse = compile_source(R"mkpro(
program AffineIndexedSelectorReuse {
  state {
    slots: packed[17..19] = 0
    f0: packed = 0
    f1: packed = 0
    f2: packed = 0
    f3: packed = 0
    index: counter 16..18 = 16
    value: packed = 0
  }

  loop {
    f0 = read()
    f1 = read()
    f2 = read()
    f3 = read()
    index = read()
    value = slots[index + 1]
    halt(value + f0 + f1 + f2 + f3)
  }
}
)mkpro");
  require(affine_indexed_selector_reuse.implemented,
          "native compiler should lower affine direct indexed selector reuse");
  require(affine_indexed_selector_reuse.diagnostics.empty(),
          "affine indexed selector compile should not report diagnostics");
  require(has_optimization(affine_indexed_selector_reuse, "affine-indexed-selector-reuse"),
          "affine indexed selector reuse should report the TS strategy name");
  require(has_optimization(affine_indexed_selector_reuse, "indirect-memory-alias-selector"),
          "affine indexed selector reuse should report indirect-memory aliasing");
  require(affine_indexed_selector_reuse.listing.find("indexed selector slots") ==
              std::string::npos,
          "affine direct selector reuse should not materialize the hidden selector");
  require(affine_indexed_selector_reuse.listing.find("indirect-memory-targets=0,1,2") !=
              std::string::npos,
          "affine direct selector listing should keep indirect-memory target metadata");

  const CompileResult fractional_indirect_addressing = compile_source(R"mkpro(
program FractionalIndirectAddressing {
  state {
    slots: packed[0..2] = 0
    f0: packed = 0
    f1: packed = 0
    f2: packed = 0
    f3: packed = 0
    pos: coord(cave) = 1.0000008
    value: packed = 0
  }

  cave: board(packed_decimal_zero_run)

  loop {
    f0 = read()
    f1 = read()
    f2 = read()
    f3 = read()
    pos = read()
    value = slots[int(pos)]
    halt(value + f0 + f1 + f2 + f3)
  }
}
)mkpro");
  require(fractional_indirect_addressing.implemented,
          "native compiler should lower fractional indirect addressing");
  require(fractional_indirect_addressing.diagnostics.empty(),
          "fractional indirect addressing compile should not report diagnostics");
  require(has_optimization(fractional_indirect_addressing, "fractional-indirect-addressing"),
          "fractional indirect addressing should report the TS strategy name");
  require(fractional_indirect_addressing.listing.find("indirect-selector-integer-part=pos") !=
              std::string::npos,
          "fractional indirect addressing listing should record integer-part selector metadata");

  const CompileResult coord_list_spatial = compile_source(R"mkpro(
program CoordListSpatial {
  field: board(0..9, 0..9)
  state {
    cell: coord(field)
    foxes: coord_list(field, 1) = random_unique()
    bearing: counter 0..9 = 0
  }

  loop {
    cell = read()
    if cell in foxes {
      bearing = 9
    }
    bearing = line_count(foxes, cell)
    halt(bearing)
  }
}
)mkpro");
  require(coord_list_spatial.implemented,
          "native compiler should lower coord_list membership and line_count");
  require(coord_list_spatial.diagnostics.empty(),
          "coord_list spatial compile should not report diagnostics");
  require(coord_list_spatial.registers.find("foxes") == coord_list_spatial.registers.end(),
          "logical coord_list name should not allocate a native register");
  require(coord_list_spatial.registers.find("__coord_list_foxes_0") !=
              coord_list_spatial.registers.end(),
          "coord_list should allocate scalar item registers");
  require(coord_list_spatial.listing.find("coord_list contains found item") != std::string::npos,
          "coord_list membership should compare scalar item registers");
  require(coord_list_spatial.listing.find("coord_list same column") != std::string::npos,
          "coord_list line_count should test columns");
  require(coord_list_spatial.listing.find("coord_list same row") != std::string::npos,
          "coord_list line_count should test rows");
  require(coord_list_spatial.listing.find("coord_list same diagonal") != std::string::npos,
          "coord_list line_count should test diagonals");
  require(coord_list_spatial.listing.find("coord_list line_count total") != std::string::npos,
          "coord_list line_count should accumulate visible items");

  CompileOptions spatial_options;
  spatial_options.analysis = true;
  const CompileResult cells_spatial = compile_source(R"mkpro(
program CellsSpatial {
  field: board(0..3, 0..0)
  state {
    cell: coord(field)
    marks: cells(field) = 0
    line: counter 0..4 = 0
    neighbors: counter 0..2 = 0
  }

  loop {
    cell = read()
    marks += cell
    if cell in marks {
      line = 4
    }
    marks -= cell
    line = line_count(marks, cell)
    neighbors = neighbor_count(marks, cell)
    halt(line + neighbors)
  }
}
)mkpro",
                                                     spatial_options);
  require(cells_spatial.implemented,
          "native compiler should lower cells membership and spatial counts");
  require(cells_spatial.diagnostics.empty(), "cells spatial compile should not report diagnostics");
  require(cells_spatial.listing.find("false branch for cells in") != std::string::npos,
          "cells membership should lower through bit_has");
  require(cells_spatial.listing.find("spatial hit marks") != std::string::npos,
          "cells membership should reuse the spatial-hit helper when spatial counts need it");
  require(cells_spatial.listing.find("spatial hit to count") != std::string::npos,
          "spatial-hit helper should return a 0/1 hit value");
  require(cells_spatial.listing.find("cells set marks") != std::string::npos,
          "cells += should lower through bit-set semantics");
  require(cells_spatial.listing.find("cells clear marks") != std::string::npos,
          "cells -= should lower through bit-clear semantics");
  require(cells_spatial.listing.find("line_count result") != std::string::npos,
          "cells line_count should lower to a result");
  require(cells_spatial.listing.find("neighbor_count result") != std::string::npos ||
              cells_spatial.listing.find("shared straight-line helper") != std::string::npos,
          "cells neighbor_count should lower to a result or an optimized helper body");
  require(std::any_of(cells_spatial.optimizations.begin(), cells_spatial.optimizations.end(),
                      [](const OptimizationReport& optimization) {
                        return optimization.name == "spatial-hit-condition-helper";
                      }),
          "cells membership should report the TS spatial-hit condition helper");
  require(std::any_of(cells_spatial.optimizations.begin(), cells_spatial.optimizations.end(),
                      [](const OptimizationReport& optimization) {
                        return optimization.name == "spatial-neighbor-count-unroll";
                      }),
          "neighbor_count should report the TS register-accumulated unroll strategy");
  require(std::any_of(cells_spatial.optimizations.begin(), cells_spatial.optimizations.end(),
                      [](const OptimizationReport& optimization) {
                        return optimization.name == "bit-mask-quotient-reuse";
                      }),
          "spatial-hit helper should reuse the bit-mask quotient scratch");

  const CompileResult error_stop = compile_source(R"mkpro(
program ErrorStop {
  loop {
    halt("ЕГГОГ")
  }
}
)mkpro",
                                                  spatial_options);
  require(error_stop.implemented, "native compiler should lower literal error stops");
  require(error_stop.diagnostics.empty(), "literal error stop should not report diagnostics");
  require(error_stop.listing.find("К ÷") != std::string::npos,
          "literal ЕГГ0Г halt should use the one-cell error opcode");
  require(std::any_of(error_stop.optimizations.begin(), error_stop.optimizations.end(),
                      [](const OptimizationReport& optimization) {
                        return optimization.name == "error-stop";
                      }),
          "literal error halt should report the TS error-stop optimization");

  CompileOptions domain_options;
  domain_options.domain_error_guards = true;
  const CompileResult domain_guards = compile_source(R"mkpro(
program DomainErrorGuards {
  state {
    x: packed = 0
    y: packed = 1
    z: packed = 2
    result: counter 0..9 = 0
  }

  loop {
    if x < 0 { halt("ЕГГОГ") }
    if y <= 0 { halt("ЕГГОГ") }
    if z == 0 { halt("ЕГГОГ") }
    result = 7
    halt(result)
  }
}
)mkpro",
                                                     domain_options);
  require(domain_guards.implemented, "native compiler should lower domain-error guards");
  require(domain_guards.diagnostics.empty(),
          "domain-error guard compile should not report diagnostics");
  require(std::any_of(domain_guards.optimizations.begin(), domain_guards.optimizations.end(),
                      [](const OptimizationReport& optimization) {
                        return optimization.name == "domain-error-guard";
                      }),
          "domain-error guard should report the TS optimization name");
  require(std::any_of(domain_guards.steps.begin(), domain_guards.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x21 && step.comment == "domain-error guard trap";
                      }),
          "x < 0 guard should use F sqrt");
  require(std::any_of(domain_guards.steps.begin(), domain_guards.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x17 && step.comment == "domain-error guard trap";
                      }),
          "x <= 0 guard should use F lg");
  require(std::any_of(domain_guards.steps.begin(), domain_guards.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x23 && step.comment == "domain-error guard trap";
                      }),
          "x == 0 guard should use F 1/x");

  const CompileResult domain_difference = compile_source(R"mkpro(
program DomainErrorDifference {
  state {
    x: packed = 0
    y: packed = 1
    result: counter 0..9 = 0
  }

  loop {
    if x < y { halt("ЕГГОГ") }
    result = 7
    halt(result)
  }
}
)mkpro",
                                                         domain_options);
  require(domain_difference.implemented,
          "native compiler should lower difference domain-error guards");
  require(domain_difference.diagnostics.empty(),
          "difference domain-error guard compile should not report diagnostics");
  require(domain_difference.listing.find("domain guard difference") != std::string::npos,
          "difference domain guard should subtract operands before the trap");
  require(std::any_of(domain_difference.steps.begin(), domain_difference.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x21 && step.comment == "domain-error guard trap";
                      }),
          "x < y guard should use F sqrt after the difference");

  const CompileResult ranged_arc_upper = compile_source(R"mkpro(
program RangedArcDomainGuard {
  state {
    y: counter 0..5 = 0
    result: counter 0..9 = 0
  }

  loop {
    if y > 1 { halt("ЕГГОГ") }
    result = 7
    halt(result)
  }
}
)mkpro",
                                                        domain_options);
  require(ranged_arc_upper.implemented,
          "native compiler should lower ranged upper arc domain guards");
  require(ranged_arc_upper.diagnostics.empty(),
          "ranged upper arc domain guard compile should not report diagnostics");
  require(std::any_of(ranged_arc_upper.optimizations.begin(), ranged_arc_upper.optimizations.end(),
                      [](const OptimizationReport& optimization) {
                        return optimization.name == "domain-error-guard";
                      }),
          "ranged upper arc domain guard should report the TS optimization name");
  require(std::any_of(ranged_arc_upper.steps.begin(), ranged_arc_upper.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x19 && step.comment == "domain-error guard trap";
                      }),
          "y > 1 with y in 0..5 should use F sin^-1");
  require(std::none_of(
              ranged_arc_upper.steps.begin(), ranged_arc_upper.steps.end(),
              [](const ResolvedStep& step) { return step.comment == "domain guard difference"; }),
          "ranged upper arc domain guard should not emit a difference subtraction");

  const CompileResult ranged_arc_integer_upper = compile_source(R"mkpro(
program RangedArcDomainGuard {
  state {
    y: counter 0..5 = 0
    result: counter 0..9 = 0
  }

  loop {
    if y >= 2 { halt("ЕГГОГ") }
    result = 7
    halt(result)
  }
}
)mkpro",
                                                                domain_options);
  require(ranged_arc_integer_upper.implemented,
          "native compiler should lower integer ranged upper arc domain guards");
  require(ranged_arc_integer_upper.diagnostics.empty(),
          "integer ranged upper arc domain guard compile should not report diagnostics");
  require(std::any_of(ranged_arc_integer_upper.steps.begin(), ranged_arc_integer_upper.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x19 && step.comment == "domain-error guard trap";
                      }),
          "y >= 2 with integer y in 0..5 should use F sin^-1");
  require(std::none_of(
              ranged_arc_integer_upper.steps.begin(), ranged_arc_integer_upper.steps.end(),
              [](const ResolvedStep& step) { return step.comment == "domain guard difference"; }),
          "integer ranged upper arc domain guard should not emit a difference subtraction");

  const CompileResult ranged_arc_lower = compile_source(R"mkpro(
program RangedArcDomainGuard {
  state {
    y: counter -5..0 = 0
    result: counter 0..9 = 0
  }

  loop {
    if y < -1 { halt("ЕГГОГ") }
    result = 7
    halt(result)
  }
}
)mkpro",
                                                        domain_options);
  require(ranged_arc_lower.implemented,
          "native compiler should lower ranged lower arc domain guards");
  require(ranged_arc_lower.diagnostics.empty(),
          "ranged lower arc domain guard compile should not report diagnostics");
  require(std::any_of(ranged_arc_lower.steps.begin(), ranged_arc_lower.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x19 && step.comment == "domain-error guard trap";
                      }),
          "y < -1 with y in -5..0 should use F sin^-1");
  require(std::none_of(
              ranged_arc_lower.steps.begin(), ranged_arc_lower.steps.end(),
              [](const ResolvedStep& step) { return step.comment == "domain guard difference"; }),
          "ranged lower arc domain guard should not emit a difference subtraction");

  const CompileResult unranged_arc = compile_source(R"mkpro(
program UnrangedArcDomainGuard {
  state {
    x: packed = 0
    result: counter 0..9 = 0
  }

  loop {
    if x > 1 { halt("ЕГГОГ") }
    result = 7
    halt(result)
  }
}
)mkpro",
                                                    domain_options);
  require(unranged_arc.implemented, "native compiler should still lower unranged domain guards");
  require(unranged_arc.diagnostics.empty(),
          "unranged arc domain guard compile should not report diagnostics");
  require(std::none_of(unranged_arc.steps.begin(), unranged_arc.steps.end(),
                       [](const ResolvedStep& step) {
                         return step.opcode == 0x19 && step.comment == "domain-error guard trap";
                       }),
          "unranged x > 1 should not use F sin^-1");
  require(unranged_arc.listing.find("domain guard difference") != std::string::npos,
          "unranged x > 1 should fall back to a difference domain guard");

  const CompileResult decrement_underflow = compile_source(R"mkpro(
program ResourceUnderflow {
  state {
    food: counter 0..9 = 2
  }

  loop {
    food--
    if food < 0 {
      halt("ЕГГ0Г")
    }
    halt(food)
  }
}
)mkpro");
  require(decrement_underflow.implemented,
          "native compiler should lower decrement underflow domain guards");
  require(decrement_underflow.diagnostics.empty(),
          "decrement underflow domain guard compile should not report diagnostics");
  require(std::any_of(decrement_underflow.optimizations.begin(),
                      decrement_underflow.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "decrement-underflow-domain-guard";
                      }),
          "resource decrement underflow should report the TS optimization name");
  require(std::any_of(decrement_underflow.steps.begin(), decrement_underflow.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x21 &&
                               step.comment == "decrement underflow domain guard trap";
                      }),
          "resource decrement underflow should use F sqrt");

  const CompileResult decrement_zero = compile_source(R"mkpro(
program DecrementZeroDomainGuard {
  state {
    rows: packed[1..9] = 0
    floor: counter 1..9 = 9
  }

  fn lost() {
    halt("ЕГГОГ")
  }

  loop {
    floor--
    if floor == 0 {
      lost()
    }
    else {
      halt(rows[floor])
    }
  }
}
)mkpro");
  require(decrement_zero.implemented, "native compiler should lower decrement zero domain guards");
  require(decrement_zero.diagnostics.empty(),
          "decrement zero domain guard compile should not report diagnostics");
  require(std::any_of(decrement_zero.optimizations.begin(), decrement_zero.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "decrement-zero-domain-guard";
                      }),
          "non-FL zero decrement should report the TS optimization name");
  require(std::any_of(decrement_zero.steps.begin(), decrement_zero.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x23 &&
                               step.comment == "decrement zero domain guard trap";
                      }),
          "non-FL zero decrement should use F 1/x");

  const CompileResult assign_domain_trap = compile_source(R"mkpro(
program AssignDomainTrap {
  state {
    altitude: counter 0..900 = 500
    fuel: packed = 0
  }

  fn dead() {
    halt("ЕГГОГ")
  }

  loop {
    burn = read()
    altitude -= burn
    if altitude <= 0 { dead() }
    fuel = burn - altitude
    if fuel < 0 { dead() }
    halt(altitude)
  }
}
)mkpro");
  require(assign_domain_trap.implemented,
          "native compiler should lower assign-then-domain-trap fusions");
  require(assign_domain_trap.diagnostics.empty(),
          "assign-then-domain-trap compile should not report diagnostics");
  const int assign_traps = static_cast<int>(std::count_if(
      assign_domain_trap.optimizations.begin(), assign_domain_trap.optimizations.end(),
      [](const OptimizationReport& optimization) {
        return optimization.name == "assign-zero-domain-guard";
      }));
  require(assign_traps >= 2,
          "assign-then-domain-trap should report both fused TS optimization sites");
  require(std::any_of(assign_domain_trap.steps.begin(), assign_domain_trap.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x17 && step.comment == "domain-error guard trap";
                      }),
          "altitude <= 0 fusion should use F lg");
  require(std::any_of(assign_domain_trap.steps.begin(), assign_domain_trap.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x21 && step.comment == "domain-error guard trap";
                      }),
          "fuel < 0 fusion should use F sqrt");
  require(std::none_of(assign_domain_trap.steps.begin(), assign_domain_trap.steps.end(),
                       [](const ResolvedStep& step) { return step.opcode == 0x5c; }),
          "assign-then-domain-trap should not emit F x<0 branches for the fused guards");

  CompileOptions preload_options;
  preload_options.general_constant_preloads = true;
  const CompileResult repeated_constant_preload = compile_source(R"mkpro(
program RepeatedConstantPreload {
  state {
    score: counter 0..99999 = 0
  }

  loop {
    score = 12345
    halt(score + 12345)
  }
}
)mkpro",
                                                                 preload_options);
  require(repeated_constant_preload.implemented,
          "native compiler should lower repeated literal preloads");
  require(repeated_constant_preload.diagnostics.empty(),
          "repeated literal preload compile should not report diagnostics");
  require(repeated_constant_preload.setup_program.has_value(),
          "compiler-owned constant preloads should emit a setup program");
  require(std::any_of(repeated_constant_preload.preloads.begin(),
                      repeated_constant_preload.preloads.end(),
                      [](const PreloadReport& preload) { return preload.value == "12345"; }),
          "preload report should include the repeated runtime literal");
  require(optimization_count(repeated_constant_preload, "preloaded-constant") >= 1,
          "runtime literal uses should recall compiler-owned constants");
  require(repeated_constant_preload.listing.find("preload const 12345") != std::string::npos,
          "main listing should annotate preloaded constant recalls");

  CompileOptions fractional_selector_options;
  fractional_selector_options.analysis = true;
  fractional_selector_options.preloaded_constant_registers["7"] = "36.123456";
  fractional_selector_options.fractional_constant_selectors.push_back(
      FractionalConstantSelectorPlan{.value = "0.123456", .target = 36});
  const CompileResult fractional_selector_preload = compile_source(R"mkpro(
program FractionalSelectorPreload {
  loop {
    halt(0.123456)
  }
}
)mkpro",
                                                                   fractional_selector_options);
  require(fractional_selector_preload.implemented,
          "native compiler should lower fractional selector constant preloads");
  const auto fractional_recall = std::find_if(
      fractional_selector_preload.steps.begin(), fractional_selector_preload.steps.end(),
      [](const ResolvedStep& step) {
        return step.comment.has_value() &&
               step.comment->find("fractional selector source 0.123456") != std::string::npos;
      });
  require(fractional_recall != fractional_selector_preload.steps.end(),
          "fractional selector preload should recall the selector carrier");
  require(std::next(fractional_recall) != fractional_selector_preload.steps.end() &&
              std::next(fractional_recall)->comment == "fractional selector const 0.123456",
          "fractional selector preload should recover the original fractional constant");
  require(has_optimization(fractional_selector_preload, "fractional-constant-selector-use"),
          "fractional selector recovery should report the TS optimization name");

  CompileOptions natural_fractional_selector_options;
  natural_fractional_selector_options.analysis = true;
  natural_fractional_selector_options.preloaded_constant_registers["7"] = "2.2600029E-1";
  natural_fractional_selector_options.fractional_constant_selectors.push_back(
      FractionalConstantSelectorPlan{.value = "0.22600029", .target = 29});
  const CompileResult natural_fractional_selector_preload =
      compile_source(R"mkpro(
program NaturalFractionalSelectorPreload {
  loop {
    halt(0.22600029)
  }
}
)mkpro",
                     natural_fractional_selector_options);
  require(natural_fractional_selector_preload.implemented,
          "native compiler should lower natural fractional selector constant preloads");
  const auto natural_recall =
      std::find_if(natural_fractional_selector_preload.steps.begin(),
                   natural_fractional_selector_preload.steps.end(), [](const ResolvedStep& step) {
                     return step.comment.has_value() &&
                            step.comment->find("natural fractional selector source 0.22600029") !=
                                std::string::npos;
                   });
  require(natural_recall != natural_fractional_selector_preload.steps.end(),
          "natural fractional selector preload should recall the normalized carrier");
  require(std::next(natural_recall) == natural_fractional_selector_preload.steps.end() ||
              std::next(natural_recall)->comment != "fractional selector const 0.22600029",
          "natural fractional selector preload should not emit recovery");
  require(has_optimization(natural_fractional_selector_preload,
                           "natural-fractional-constant-selector-use"),
          "natural fractional selector use should report the TS optimization name");

  const CompileResult known_zero_reuse = compile_source(R"mkpro(
program KnownZeroReuse {
  state {
    value: counter 0..9 = 1
  }

  loop {
    value = 0
    halt(0)
  }
}
)mkpro");
  require(known_zero_reuse.implemented, "native compiler should lower known-zero reuse");
  require(known_zero_reuse.diagnostics.empty(),
          "known-zero reuse compile should not report diagnostics");
  require(has_optimization(known_zero_reuse, "known-zero-reuse"),
          "known zero in X should be reused for the following zero literal");
  require(std::count_if(known_zero_reuse.steps.begin(), known_zero_reuse.steps.end(),
                        [](const ResolvedStep& step) { return step.hex == "00"; }) == 1,
          "known-zero reuse should not emit a second zero literal");

  const CompileResult display_scale_preload = compile_source(R"mkpro(
program DisplayScalePreload {
  state {
    a: counter 0..9 = 1
    b: counter 0..9 = 2
    c: counter 0..9 = 3
  }

  loop {
    show(a, b, c)
  }
}
)mkpro",
                                                             preload_options);
  require(display_scale_preload.implemented, "native compiler should lower display scale preloads");
  require(display_scale_preload.diagnostics.empty(),
          "display scale preload compile should not report diagnostics");
  require(std::any_of(display_scale_preload.preloads.begin(), display_scale_preload.preloads.end(),
                      [](const PreloadReport& preload) { return preload.value == "10"; }),
          "preload report should include the implicit packed display scale");
  require(display_scale_preload.listing.find("preload const 10") != std::string::npos,
          "packed display lowering should recall the preloaded scale");

  const CompileResult cells_line_count_preloads = compile_source(R"mkpro(
program CellsLineCountPreloads {
  field: board(0..9, 0..9)
  state {
    cell: coord(field)
    marks: cells(field) = 0
    clue: counter 0..8 = 0
  }

  loop {
    cell = read()
    clue = line_count(marks, cell)
    halt(clue)
  }
}
)mkpro",
                                                                 spatial_options);
  require(cells_line_count_preloads.implemented,
          "native compiler should lower 10x10 cells line_count");
  require(cells_line_count_preloads.diagnostics.empty(),
          "10x10 cells line_count compile should not report diagnostics");
  require(cells_line_count_preloads.listing.find("line_count progression; preloaded") !=
              std::string::npos,
          "repeated line_count helper calls should use preloaded indirect flow");
  require(std::any_of(cells_line_count_preloads.optimizations.begin(),
                      cells_line_count_preloads.optimizations.end(),
                      [](const OptimizationReport& optimization) {
                        return optimization.name == "spatial-sum-hit-stack-restore";
                      }),
          "line_count helper should report TS stack-carried hit-count restoration");
  require(cells_line_count_preloads.listing.find("preload const 0.5") != std::string::npos,
          "shared bit-mask helper should use the planned fractional preload");
  require(has_optimization(cells_line_count_preloads, "preloaded-stack-constant"),
          "shared bit-mask helper should report stack-only constant preloads");
  require(cells_line_count_preloads.setup_program.has_value(),
          "preloaded line_count programs should expose a setup program");
  require(!cells_line_count_preloads.setup_program->steps.empty(),
          "setup program should contain executable steps");
  require(cells_line_count_preloads.setup_program->steps.back().opcode == 0x50,
          "setup program should stop after initializing preloads");
  require(cells_line_count_preloads.listing.find("# Setup Listing") != std::string::npos,
          "combined listing should include setup listing");
  require(cells_line_count_preloads.listing.find("# Main Listing") != std::string::npos,
          "combined listing should include main listing");
  require(cells_line_count_preloads.setup_listing.find("setup complete") != std::string::npos,
          "setup listing should show setup completion");
  bool found_half_preload = false;
  bool found_flow_preload = false;
  for (const PreloadReport& preload : cells_line_count_preloads.preloads) {
    if (preload.value == "0.5")
      found_half_preload = true;
    if (preload.value != "0.5" && preload.value != "10" && preload.value != "19" &&
        preload.value != "11" && preload.value != "-99" && preload.value != "-81")
      found_flow_preload = true;
  }
  require(found_half_preload, "preload report should include the bit-mask rounding bias");
  require(found_flow_preload, "preload report should include the indirect helper target");

  const CompileResult stack_setup = compile_source(R"mkpro(
program StackSetup {
  state {
    x: counter 0..9 = stack.X
    y: counter 0..9 = stack.Y
    z: counter 0..9 = 0
  }

  loop {
    halt(x + y + z)
  }
}
)mkpro");
  require(stack_setup.implemented, "native compiler should lower stack setup state");
  require(stack_setup.diagnostics.empty(), "stack setup compile should not report diagnostics");
  require(stack_setup.setup_program.has_value(), "stack setup state should expose a setup program");
  require(stack_setup.setup_listing.find("setup from stack.Y") != std::string::npos,
          "setup should move stack.Y into X before storing Y-sourced fields");
  require(stack_setup.setup_listing.find("restore stack.X after stack.Y setup") !=
              std::string::npos,
          "setup should restore stack.X after Y-sourced fields");
  bool found_stack_x = false;
  bool found_stack_y = false;
  bool found_zero_state = false;
  for (const PreloadReport& preload : stack_setup.preloads) {
    found_stack_x = found_stack_x || preload.value == "stack.X";
    found_stack_y = found_stack_y || preload.value == "stack.Y";
    found_zero_state = found_zero_state || preload.value == "0";
  }
  require(found_stack_x, "preload report should include stack.X state");
  require(found_stack_y, "preload report should include stack.Y state");
  require(found_zero_state, "preload report should include numeric state initializer");

  const CompileResult bank_stack_setup = compile_source(R"mkpro(
program BankStackSetup {
  state {
    line: group(1..2) {
      front: packed = stack.X
    }
  }

  loop {
    halt(line[1].front + line[2].front)
  }
}
)mkpro");
  require(bank_stack_setup.implemented, "native compiler should lower state-bank stack setup");
  require(bank_stack_setup.diagnostics.empty(),
          "state-bank stack setup compile should not report diagnostics");
  int bank_stack_x_preloads = 0;
  for (const PreloadReport& preload : bank_stack_setup.preloads) {
    if (preload.value == "stack.X")
      ++bank_stack_x_preloads;
  }
  require(bank_stack_x_preloads == 2,
          "state-bank stack setup should initialize every bank element");

  const CompileResult cells_random_setup = compile_source(R"mkpro(
program CellsRandomSetup {
  field: board(0..9, 0..9)
  state {
    cell: coord(field)
    marks: cells(field) = random()
  }

  loop {
    cell = read()
    if cell in marks {
      halt(1)
    }
    halt(0)
  }
}
)mkpro");
  require(cells_random_setup.implemented, "native compiler should lower cells random setup state");
  require(cells_random_setup.diagnostics.empty(),
          "cells random setup compile should not report diagnostics");
  require(cells_random_setup.setup_program.has_value(),
          "cells random setup should expose a setup program");
  bool found_cells_random_preload = false;
  int cells_random_ops = 0;
  for (const PreloadReport& preload : cells_random_setup.preloads) {
    found_cells_random_preload = found_cells_random_preload || preload.value == "random()";
  }
  for (const ResolvedStep& step : cells_random_setup.setup_program->steps) {
    if (step.hex == "3B" && step.comment.has_value() && *step.comment == "random()")
      ++cells_random_ops;
  }
  require(found_cells_random_preload, "preload report should include cells random initializer");
  require(cells_random_ops == 1, "single cells random setup should draw exactly one random value");
  require(cells_random_setup.setup_listing.find("random int floor") != std::string::npos,
          "cells random setup should lower int(random() * 999) without K [x]");

  const CompileResult two_cells_random_setup = compile_source(R"mkpro(
program TwoCellsRandomSetup {
  field: board(0..9, 0..9)
  state {
    cell: coord(field)
    a: cells(field) = random()
    b: cells(field) = random()
  }

  loop {
    cell = read()
    if cell in a {
      halt(1)
    }
    if cell in b {
      halt(2)
    }
    halt(0)
  }
}
)mkpro");
  require(two_cells_random_setup.implemented,
          "native compiler should lower multiple cells random setup states");
  require(two_cells_random_setup.diagnostics.empty(),
          "multiple cells random setup compile should not report diagnostics");
  require(two_cells_random_setup.setup_program.has_value(),
          "multiple cells random setup should expose a setup program");
  int two_cells_random_ops = 0;
  for (const ResolvedStep& step : two_cells_random_setup.setup_program->steps) {
    if (step.hex == "3B" && step.comment.has_value() && *step.comment == "random()")
      ++two_cells_random_ops;
  }
  require(two_cells_random_ops == 2,
          "multiple cells random setup should draw independent random values");

  const CompileResult coord_random_setup = compile_source(R"mkpro(
program CoordRandomSetup {
  field: board(1..9, 0..6)
  state {
    probe: coord(field) = random(field)
  }

  loop {
    halt(probe)
  }
}
)mkpro");
  require(coord_random_setup.implemented, "native compiler should lower coord random setup state");
  require(coord_random_setup.diagnostics.empty(),
          "coord random setup compile should not report diagnostics");
  require(coord_random_setup.setup_program.has_value(),
          "coord random setup should expose a setup program");
  bool found_coord_random_preload = false;
  int coord_random_ops = 0;
  for (const PreloadReport& preload : coord_random_setup.preloads) {
    found_coord_random_preload = found_coord_random_preload || preload.value == "random(field)";
  }
  for (const ResolvedStep& step : coord_random_setup.setup_program->steps) {
    if (step.hex == "3B" && step.comment.has_value() && *step.comment == "random()")
      ++coord_random_ops;
  }
  require(found_coord_random_preload, "preload report should include coord random initializer");
  require(coord_random_ops == 2,
          "two-dimensional coord random setup should draw x and y independently");
  require(coord_random_setup.setup_listing.find("random coord y decade") != std::string::npos,
          "coord random setup should pack y into the decimal-tens position");

  const CompileResult coord_list_random_setup = compile_source(R"mkpro(
program CoordListRandomSetup {
  field: board(0..9, 0..9)
  state {
    cell: coord(field) = 11
    spots: coord_list(field, 3) = random()
    bearing: counter 0..9 = 0
  }

  loop {
    halt(0)
  }
}
)mkpro");
  require(coord_list_random_setup.implemented,
          "native compiler should lower coord_list random setup state");
  require(coord_list_random_setup.diagnostics.empty(),
          "coord_list random setup compile should not report diagnostics");
  require(coord_list_random_setup.setup_program.has_value(),
          "coord_list random setup should expose a setup program");
  int coord_list_random_preloads = 0;
  int coord_list_random_ops = 0;
  for (const PreloadReport& preload : coord_list_random_setup.preloads) {
    if (preload.value == "random(field)")
      ++coord_list_random_preloads;
  }
  for (const ResolvedStep& step : coord_list_random_setup.setup_program->steps) {
    if (step.hex == "3B" && step.comment.has_value() && *step.comment == "random()")
      ++coord_list_random_ops;
  }
  require(coord_list_random_preloads == 3,
          "coord_list random setup should report one random preload per item");
  require(coord_list_random_ops == 6, "coord_list random setup should draw x and y for every item");

  const CompileResult coord_list_unique_setup = compile_source(R"mkpro(
program CoordListUniqueSetup {
  field: board(0..9, 0..9)
  state {
    foxes: coord_list(field, 3) = random_unique()
  }

  loop {
    halt(0)
  }
}
)mkpro");
  require(coord_list_unique_setup.implemented,
          "native compiler should lower coord_list random_unique setup state");
  require(coord_list_unique_setup.diagnostics.empty(),
          "coord_list random_unique setup compile should not report diagnostics");
  require(coord_list_unique_setup.setup_program.has_value(),
          "coord_list random_unique setup should expose a setup program");
  int unique_preloads = 0;
  int unique_seed_draws = 0;
  int unique_collision_branches = 0;
  int unique_previous_loops = 0;
  int unique_outer_loops = 0;
  int unique_stores = 0;
  int unique_indirect_stores = 0;
  for (const PreloadReport& preload : coord_list_unique_setup.preloads) {
    if (preload.value.find("random_unique(field,foxes,") == 0)
      ++unique_preloads;
  }
  for (const ResolvedStep& step : coord_list_unique_setup.setup_program->steps) {
    if (step.hex == "3B" && step.comment.has_value() && *step.comment == "random coord seed")
      ++unique_seed_draws;
    if (step.hex == "57" && step.comment.has_value() &&
        step.comment->find("random coord collision") != std::string::npos)
      ++unique_collision_branches;
    if (step.hex == "5B" && step.comment.has_value() &&
        *step.comment == "random coord previous loop")
      ++unique_previous_loops;
    if (step.hex == "58" && step.comment.has_value() && *step.comment == "random coord outer loop")
      ++unique_outer_loops;
    if (step.comment.has_value() && *step.comment == "random coord store")
      ++unique_stores;
    if (step.hex == "B5" && step.comment.has_value() && *step.comment == "random coord store")
      ++unique_indirect_stores;
  }
  require(unique_preloads == 3,
          "coord_list random_unique setup should report one generated preload per item");
  require(unique_seed_draws == 1, "coord_list random_unique setup should draw one raw random seed");
  require(unique_collision_branches == 1,
          "coord_list random_unique setup should use one compact collision branch");
  require(unique_previous_loops == 1,
          "coord_list random_unique setup should loop across previous items indirectly");
  require(unique_outer_loops == 1,
          "coord_list random_unique setup should loop across generated items indirectly");
  require(unique_stores == 1,
          "coord_list random_unique setup should use one compact indirect store");
  require(unique_indirect_stores == 1,
          "coord_list random_unique setup should store through the pointer register");

  CompileOptions segmented_options = spatial_options;
  segmented_options.segmented_bitplanes = true;
  const CompileResult segmented_cells = compile_source(R"mkpro(
program SegmentedCells {
  field: board(0..9, 0..9)
  state {
    cell: coord(field)
    marks: cells(field) = 0
    seen: counter 0..1 = 0
  }

  loop {
    cell = read()
    marks += cell
    if cell in marks {
      seen = 1
    }
    marks -= cell
    halt(seen)
  }
}
)mkpro",
                                                       segmented_options);
  require(segmented_cells.implemented,
          "native compiler should lower segmented cells membership and updates");
  require(segmented_cells.diagnostics.empty(),
          "segmented cells compile should not report diagnostics");
  require(segmented_cells.registers.find("marks") == segmented_cells.registers.end(),
          "logical segmented cells collection should not allocate one native register");
  require(segmented_cells.registers.find("__seg_bitplane_marks_0") !=
              segmented_cells.registers.end(),
          "segmented cells should allocate plane 0");
  require(segmented_cells.registers.find("__seg_bitplane_marks_3") !=
              segmented_cells.registers.end(),
          "segmented cells should allocate plane 3");
  require(segmented_cells.registers.find("__seg_bitplane_index") != segmented_cells.registers.end(),
          "segmented cells should allocate the local index scratch");
  require(segmented_cells.listing.find("segmented bitplane hit") != std::string::npos,
          "segmented membership should dispatch through bitplane hit lowering");
  require(segmented_cells.listing.find("segmented bitplane set marks") != std::string::npos,
          "segmented cells += should set the selected plane");
  require(segmented_cells.listing.find("segmented bitplane clear marks") != std::string::npos,
          "segmented cells -= should clear the selected plane");

  const CompileResult cell_clear_reuse = compile_source(R"mkpro(
program CellClearReuse {
  field: board(0..3, 0..0)
  state {
    cell: coord(field)
    marks: cells(field) = 0
    seen: counter 0..1 = 0
  }

  fn hit() {
    marks -= cell
    seen = 1
  }

  loop {
    cell = read()
    if cell in marks {
      hit()
    }
    halt(seen)
  }
}
)mkpro",
                                                        spatial_options);
  require(cell_clear_reuse.implemented,
          "native compiler should lower cells membership-clear reuse");
  require(cell_clear_reuse.diagnostics.empty(),
          "cells membership-clear reuse should not report diagnostics");
  require(cell_clear_reuse.listing.find("reuse membership mask for clear") != std::string::npos,
          "cells contains followed by clear should reuse the membership mask");
  require(cell_clear_reuse.listing.find("clear matched cell with reused mask") != std::string::npos,
          "cells contains followed by clear should clear with the reused mask");
  require(has_optimization(cell_clear_reuse, "cell-membership-clear-reuse"),
          "cells membership-clear reuse should report the TS strategy name");
  require(cell_clear_reuse.listing.find("call function hit") == std::string::npos,
          "membership-clear reuse should see through the inlined procedure call");

  CompileOptions membership_options;
  membership_options.budget = 999;
  membership_options.analysis = true;
  const CompileResult mask_membership_clear = compile_source(R"mkpro(
program MaskMembershipClear {
  state {
    pos: packed = 1.0000008
    marks: packed = 0
  }

  loop {
    if bit_and(marks, frac(pos)) != 0 {
      marks = bit_and(marks, bit_not(frac(pos)))
    }
    halt(marks)
  }
}
)mkpro",
                                                         membership_options);
  require(mask_membership_clear.implemented,
          "native compiler should lower mask membership clear through a delta branch");
  require(mask_membership_clear.diagnostics.empty(),
          "mask membership clear compile should not report diagnostics");
  require(has_optimization(mask_membership_clear, "membership-clear-delta-branch"),
          "mask membership clear should report the TS strategy name");
  require(std::none_of(mask_membership_clear.steps.begin(), mask_membership_clear.steps.end(),
                       [](const ResolvedStep& step) {
                         return step.comment.has_value() &&
                                step.comment->find("membership fraction") != std::string::npos;
                       }),
          "mask membership clear delta branch should avoid the generic membership fraction path");

  const CompileResult membership_single_set = compile_source(R"mkpro(
program MembershipSingleSetCollection {
  grid: board(1..4, 1..4)

  state {
    cell: coord(grid)
    occupied: cells(grid)
    player_marks: cells(grid)
  }

  loop {
    cell = read()
    unless cell in occupied {
      player_marks += cell
      halt(player_marks)
    }
    halt(0)
  }
}
)mkpro",
                                                       membership_options);
  require(membership_single_set.implemented,
          "native compiler should lower failed membership set reuse");
  require(membership_single_set.diagnostics.empty(),
          "failed membership set reuse should not report diagnostics");
  require(has_optimization(membership_single_set, "cell-membership-set-reuse"),
          "failed membership set reuse should report the TS strategy name");
  require(has_optimization(membership_single_set, "membership-mask-stack-test-reuse"),
          "failed membership set reuse should reuse the stack-held mask for the membership test");
  require(!has_optimization(membership_single_set, "cell-membership-mask-run-reuse"),
          "single failed membership set should not report the run reuse strategy");
  const auto single_set_index =
      std::find_if(membership_single_set.steps.begin(), membership_single_set.steps.end(),
                   [](const ResolvedStep& step) {
                     return step.comment == "bit_set with reused mask";
                   });
  require(single_set_index != membership_single_set.steps.end(),
          "failed membership set should use the reusable mask for bit_set");
  require(single_set_index - membership_single_set.steps.begin() >= 2,
          "failed membership set should have recall steps before bit_set");
  require((single_set_index - 2)->comment == "recall player_marks",
          "failed membership set should recall the set collection before the mask");

  const CompileResult membership_mask_run = compile_source(R"mkpro(
program MembershipMaskRun {
  grid: board(1..4, 1..4)

  state {
    cell: coord(grid)
    player_marks: cells(grid)
    occupied: cells(grid)
  }

  loop {
    cell = read()
    if cell in occupied {
      halt(0)
    }
    else {
      player_marks += cell
      occupied += cell
      halt(player_marks + occupied)
    }
  }
}
)mkpro",
                                                   membership_options);
  require(membership_mask_run.implemented,
          "native compiler should lower failed membership mask run reuse");
  require(membership_mask_run.diagnostics.empty(),
          "failed membership mask run reuse should not report diagnostics");
  require(has_optimization(membership_mask_run, "cell-membership-mask-run-reuse"),
          "failed membership mask run should report the TS strategy name");
  require(has_optimization(membership_mask_run, "membership-mask-stack-test-reuse"),
          "failed membership mask run should reuse the stack-held mask for the membership test");
  require(std::count_if(membership_mask_run.steps.begin(), membership_mask_run.steps.end(),
                        [](const ResolvedStep& step) {
                          return step.comment == "bit_set with reused mask";
                        }) == 2,
          "failed membership mask run should use the reusable mask for both set updates");

  const CompileResult fractional_membership_x2 = compile_source(R"mkpro(
program FractionalMembershipMaskX2Set {
  state {
    pos: packed = 1.0000008
    marks: packed = 0
  }

  loop {
    if bit_and(marks, frac(pos)) != 0 {
      halt(9)
    }
    else {
      marks = bit_or(marks, frac(pos))
    }
    halt(marks)
  }
}
)mkpro",
                                                        membership_options);
  require(fractional_membership_x2.implemented,
          "native compiler should lower fractional membership X2 restore");
  require(fractional_membership_x2.diagnostics.empty(),
          "fractional membership X2 restore should not report diagnostics");
  require(has_optimization(fractional_membership_x2, "membership-collection-x2-restore"),
          "fractional membership set should report X2 collection restore");
  require(has_optimization(fractional_membership_x2, "fractional-membership-mask-test"),
          "fractional membership set should skip redundant fractional extraction");
  require(!has_optimization(fractional_membership_x2, "membership-mask-stack-test-reuse"),
          "fractional membership X2 restore should not use the scratch-mask path");
  require(std::any_of(fractional_membership_x2.steps.begin(),
                      fractional_membership_x2.steps.end(), [](const ResolvedStep& step) {
                        return step.comment == "guard X2 restore gap";
                      }),
          "fractional membership X2 restore should insert the preserving no-op");
  require(std::any_of(fractional_membership_x2.steps.begin(),
                      fractional_membership_x2.steps.end(), [](const ResolvedStep& step) {
                        return step.comment == "restore membership collection from X2";
                      }),
          "fractional membership X2 restore should restore the collection from X2");

  const CompileResult membership_current_x_scratch = compile_source(R"mkpro(
program MembershipMaskCurrentXScratch {
  state {
    mask: packed = 0
    occupied: packed = 0
    marks: packed = 0
  }

  loop {
    mask = occupied + 0.1
    if bit_and(occupied, mask) != 0 {
      halt(0)
    }
    else {
      marks = bit_or(marks, mask)
      halt(marks)
    }
  }
}
)mkpro",
                                                          membership_options);
  require(membership_current_x_scratch.implemented,
          "native compiler should lower current-X membership mask scratch");
  require(membership_current_x_scratch.diagnostics.empty(),
          "current-X membership mask scratch should not report diagnostics");
  require(has_optimization(membership_current_x_scratch, "membership-mask-current-x-scratch"),
          "current-X membership mask scratch should report the TS strategy name");
  require(has_optimization(membership_current_x_scratch, "membership-mask-stack-test-reuse"),
          "current-X membership mask scratch should reuse the stack-held mask for the test");
  require(!has_optimization(membership_current_x_scratch, "membership-collection-x2-restore"),
          "current-X membership mask scratch should not use X2 collection restore");
  const auto scratch_index =
      std::find_if(membership_current_x_scratch.steps.begin(),
                   membership_current_x_scratch.steps.end(), [](const ResolvedStep& step) {
                     return step.comment == "cell bit mask scratch";
                   });
  const auto test_index =
      std::find_if(membership_current_x_scratch.steps.begin(),
                   membership_current_x_scratch.steps.end(), [](const ResolvedStep& step) {
                     return step.comment == "membership test with reused mask";
                   });
  require(scratch_index != membership_current_x_scratch.steps.end(),
          "current-X membership mask scratch should store the reusable mask");
  require(scratch_index != membership_current_x_scratch.steps.begin(),
          "current-X membership mask scratch should have a preceding step");
  require((scratch_index - 1)->comment != "recall mask",
          "current-X membership mask scratch should not recall mask before storing it");
  require(test_index > scratch_index,
          "current-X membership mask scratch should test after storing the scratch");

  const CompileResult tail_display = compile_source(R"mkpro(
program TailDisplay {
  state {
    probe: counter 0..9 = 0
    bearing: counter 0..9 = 0
  }

  loop {
    probe = read()
    if probe == 0 {
      bearing = 1
    } else {
      bearing = 2
    }
    show(bearing)
  }
}
)mkpro");
  require(tail_display.implemented, "native compiler should lower display-only branch tails");
  require(tail_display.diagnostics.empty(),
          "display-only branch tail compile should not report diagnostics");
  require(tail_display.listing.find("assign bearing") == std::string::npos,
          "display-only branch tail should leave bearing in X instead of storing it");
  require(tail_display.listing.find("recall bearing") == std::string::npos,
          "following show(bearing) should use the branch-tail X value");

  const CompileResult tail_display_later_read = compile_source(R"mkpro(
program TailDisplayLaterRead {
  state {
    probe: counter 0..9 = 0
    bearing: counter 0..9 = 0
    saved: counter 0..9 = 0
  }

  loop {
    probe = read()
    if probe == 0 {
      bearing = 1
    } else {
      bearing = 2
    }
    show(bearing)
    saved = bearing
    halt(saved)
  }
}
)mkpro");
  require(tail_display_later_read.implemented,
          "native compiler should lower tail-display programs with later reads");
  require(tail_display_later_read.diagnostics.empty(),
          "tail-display later-read compile should not report diagnostics");
  require(tail_display_later_read.listing.find("assign bearing") != std::string::npos,
          "bearing must still be stored when later code reads it");

  const CompileResult if_statement = compile_source(R"mkpro(
program ConditionalNoElse {
  state {
    result: counter 0..9 = 0
  }

  loop {
    x = read()
    if x >= 5 {
      result = 9
    }
    halt(result)
  }
}
)mkpro");
  require(if_statement.implemented, "native compiler should lower a generic if");
  require(if_statement.diagnostics.empty(), "generic if compile should not report diagnostics");
  require(if_statement.listing.find("condition compare") != std::string::npos,
          "generic if listing should include condition compare");
  require(if_statement.listing.find("false branch for >=") != std::string::npos,
          "generic if listing should include comparison branch");

  const CompileResult if_else_statement = compile_source(R"mkpro(
program ConditionalElse {
  state {
    result: counter 0..9 = 0
  }

  loop {
    x = read()
    if x == 0 {
      result = 1
    } else {
      result = 2
    }
    halt(result)
  }
}
)mkpro");
  require(if_else_statement.implemented, "native compiler should lower a generic if/else");
  require(if_else_statement.diagnostics.empty(),
          "generic if/else compile should not report diagnostics");
  require(if_else_statement.listing.find("false branch for ==") != std::string::npos,
          "generic if/else listing should include equality branch");
  require(if_else_statement.listing.find("condition compare") == std::string::npos,
          "zero comparison should branch directly without loading zero and subtracting");
  require(if_else_statement.listing.find("if end") != std::string::npos,
          "generic if/else listing should jump over else body");

  const CompileResult negated_if_statement = compile_source(R"mkpro(
program NegatedConditional {
  state {
    result: counter 0..9 = 0
  }

  loop {
    x = read()
    unless x < 3 {
      result = 7
    }
    halt(result)
  }
}
)mkpro");
  require(negated_if_statement.implemented, "native compiler should lower a negated generic if");
  require(negated_if_statement.diagnostics.empty(),
          "negated generic if compile should not report diagnostics");
  require(negated_if_statement.listing.find("false branch for >=") != std::string::npos,
          "negated generic if should invert the comparison");

  const CompileResult while_statement = compile_source(R"mkpro(
program WhileCounter {
  state {
    count: counter 0..9 = 0
  }

  loop {
    while count < 3 {
      count += 1
    }
    halt(count)
  }
}
)mkpro");
  require(while_statement.implemented, "native compiler should lower a generic while");
  require(while_statement.diagnostics.empty(),
          "generic while compile should not report diagnostics");
  require(while_statement.listing.find("false branch for <") != std::string::npos,
          "generic while listing should include comparison branch");
  require(while_statement.listing.find("while loop back") != std::string::npos,
          "generic while listing should include loop back");

  const CompileResult contains_statement = compile_source(R"mkpro(
program ContainsPredicate {
  state {
    occupied: packed = 7
    hits: counter 0..9 = 0
    misses: counter 0..9 = 0
  }

  loop {
    cell = read()
    if cell in occupied {
      hits += 1
    }
    unless cell in occupied {
      misses += 1
    }
    halt(hits)
  }
}
)mkpro");
  require(contains_statement.implemented, "native compiler should lower contains predicates");
  require(contains_statement.diagnostics.empty(),
          "contains predicate compile should not report diagnostics");
  require(contains_statement.listing.find("bit_and()") != std::string::npos,
          "contains predicate should lower through bit_and()");
  require(contains_statement.listing.find("false branch for in") != std::string::npos,
          "contains predicate should branch on missing membership");
  require(contains_statement.listing.find("false branch for not in") != std::string::npos,
          "negated contains predicate should invert membership branch");

  const CompileResult match_statement = compile_source(R"mkpro(
program ScalarMatch {
  state {
    result: counter 0..9 = 0
    hits: counter 0..9 = 0
  }

  loop {
    choice = read()
    match choice {
      1 => result = 4
      2 => {
        result = 5
        hits += 1
      }
      otherwise => result = 9
    }
    halt(result)
  }
}
)mkpro");
  require(match_statement.implemented, "native compiler should lower a generic scalar match");
  require(match_statement.diagnostics.empty(),
          "generic scalar match compile should not report diagnostics");
  bool saw_match_value_register = false;
  for (const auto& [name, _register_name] : match_statement.registers) {
    if (name.rfind("__match_value_", 0) == 0)
      saw_match_value_register = true;
  }
  require(!saw_match_value_register, "numeric scalar match should not reserve a selector scratch");
  require(match_statement.listing.find("match value") == std::string::npos,
          "numeric scalar match should avoid saving the dispatch expression");
  require(match_statement.listing.find("dispatch residual compare") != std::string::npos,
          "numeric scalar match should reuse residual comparisons");
  require(match_statement.listing.find("case mismatch") != std::string::npos,
          "generic scalar match should compare cases");
  require(match_statement.listing.find("match end") != std::string::npos,
          "generic scalar match should jump to match end");
  require(match_statement.listing.find("increment hits") != std::string::npos,
          "generic scalar match should lower block action statements");

  CompileOptions if_chain_options;
  if_chain_options.canonicalize_if_chains = true;
  const CompileResult canonical_if_chain = compile_source(R"mkpro(
program CanonicalIfChain {
  state {
    result: counter 0..9 = 0
  }

  loop {
    choice = read()
    if choice + 1 == 2 {
      result = 4
    } else if choice + 1 == 3 {
      result = 5
    } else {
      result = 9
    }
    halt(result)
  }
}
)mkpro",
                                                          if_chain_options);
  require(canonical_if_chain.implemented,
          "native compiler should lower canonicalized if/else-if chains");
  require(canonical_if_chain.diagnostics.empty(),
          "canonicalized if-chain compile should not report diagnostics");
  require(std::any_of(canonical_if_chain.optimizations.begin(),
                      canonical_if_chain.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "if-chain-dispatch-canonicalization";
                      }),
          "canonicalized if-chain should report the TS optimization name");
  require(canonical_if_chain.listing.find("dispatch residual compare") != std::string::npos,
          "canonicalized if-chain should lower through residual dispatch compares");
  require(canonical_if_chain.listing.find("match value") == std::string::npos,
          "canonicalized numeric if-chain should not reserve a generic match scratch");

  const CompileResult inverted_if_chain = compile_source(R"mkpro(
program InvertedIfChain {
  state {
    result: counter 0..9 = 0
  }

  loop {
    choice = read()
    if choice != 1 {
      if choice == 2 {
        result = 5
      } else {
        result = 9
      }
    } else {
      result = 4
    }
    halt(result)
  }
}
)mkpro",
                                                         if_chain_options);
  require(inverted_if_chain.implemented,
          "native compiler should lower inverted canonicalized if chains");
  require(inverted_if_chain.diagnostics.empty(),
          "inverted canonicalized if-chain compile should not report diagnostics");
  require(std::any_of(inverted_if_chain.optimizations.begin(),
                      inverted_if_chain.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "if-chain-dispatch-canonicalization";
                      }),
          "inverted if-chain should report the TS optimization name");
  require(inverted_if_chain.listing.find("dispatch residual compare") != std::string::npos,
          "inverted if-chain should lower through residual dispatch compares");

  CompileOptions branch_order_options;
  branch_order_options.disable_candidate_search = true;
  branch_order_options.invert_branch_order = true;
  const CompileResult branch_order = compile_source(R"mkpro(
program BranchOrder {
  state {
    choice: counter 0..9 = 0
    result: counter 0..99 = 0
  }

  loop {
    choice = read()
    if choice == 0 {
      result = 1
    } else {
      result = 2
      result += 3
      result += 4
    }
    halt(result)
  }
}
)mkpro",
                                                    branch_order_options);
  require(branch_order.implemented, "native compiler should lower inverted if branch order");
  require(branch_order.diagnostics.empty(),
          "inverted branch-order compile should not report diagnostics");
  require(has_optimization(branch_order, "if-branch-order-inversion"),
          "inverted branch order should report if-branch-order-inversion");

  const CompileResult preview_statement = compile_source(R"mkpro(
program PreviewValue {
  state {
    total: counter 0..9 = 3
  }

  loop {
    preview(total)
    halt(total)
  }
}
)mkpro");
  require(preview_statement.implemented, "native compiler should lower preview(expr)");
  require(preview_statement.diagnostics.empty(), "preview compile should not report diagnostics");
  int stop_count = 0;
  for (const ResolvedStep& step : preview_statement.steps) {
    if (step.opcode == 0x50)
      ++stop_count;
  }
  require(stop_count == 1, "preview should prepare X without emitting a calculator stop");
  require(preview_statement.listing.find("recall total") != std::string::npos,
          "preview should lower its expression");
  require(has_optimization(preview_statement, "running-display-preview"),
          "preview should report running-display-preview");

  const CompileResult numeric_show_statement = compile_source(R"mkpro(
program NumericShow {
  loop {
    show(0)
    halt(1)
  }
}
)mkpro");
  require(numeric_show_statement.implemented, "native compiler should lower numeric show(...)");
  require(numeric_show_statement.diagnostics.empty(),
          "numeric show compile should not report diagnostics");
  int numeric_show_stop_count = 0;
  bool saw_pause = false;
  for (const ResolvedStep& step : numeric_show_statement.steps) {
    if (step.opcode == 0x50) {
      ++numeric_show_stop_count;
      if (step.comment == "pause")
        saw_pause = true;
    }
  }
  require(numeric_show_stop_count == 2, "numeric show should emit a pause and halt");
  require(saw_pause, "numeric show should emit a pause comment");

  const CompileResult duplicate_terminal_display = compile_source(R"mkpro(
program DuplicateTerminalDisplay {
  loop {
    show(0)
    halt(0)
  }
}
)mkpro");
  require(duplicate_terminal_display.implemented,
          "native compiler should fuse duplicate terminal displays");
  require(duplicate_terminal_display.diagnostics.empty(),
          "duplicate terminal display compile should not report diagnostics");
  require(has_optimization(duplicate_terminal_display, "terminal-display-fusion"),
          "duplicate terminal display should report terminal-display-fusion");
  require(std::count_if(duplicate_terminal_display.steps.begin(),
                        duplicate_terminal_display.steps.end(),
                        [](const ResolvedStep& step) { return step.opcode == 0x50; }) == 1,
          "duplicate terminal display should emit one calculator stop");
  require(std::none_of(duplicate_terminal_display.steps.begin(),
                       duplicate_terminal_display.steps.end(),
                       [](const ResolvedStep& step) { return step.comment == "pause"; }),
          "duplicate terminal display should not emit the preceding pause");

  const CompileResult literal_terminal_display = compile_source(R"mkpro(
program LiteralTerminalDisplay {
  loop {
    show("ЕГГ0Г")
    halt()
  }
}
)mkpro");
  require(literal_terminal_display.implemented,
          "native compiler should fold literal terminal display errors");
  require(!has_error_diagnostic(literal_terminal_display),
          "literal terminal display compile should not report errors");
  require(has_optimization(literal_terminal_display, "terminal-display-fusion"),
          "literal terminal display should report terminal-display-fusion");
  require(std::any_of(literal_terminal_display.steps.begin(), literal_terminal_display.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x29 && step.comment == "halt literal ЕГГ0Г";
                      }),
          "literal terminal display should use the one-cell error stop");

  const CompileResult initial_value_terminal_display = compile_source(R"mkpro(
program InitialValueTerminalDisplay {
  state {
    score: counter 0..9 = 1
  }

  loop {
    show(score)
    halt(1)
  }
}
)mkpro");
  require(initial_value_terminal_display.implemented,
          "native compiler should fuse initial-value terminal displays");
  require(initial_value_terminal_display.diagnostics.empty(),
          "initial-value terminal display compile should not report diagnostics");
  require(has_optimization(initial_value_terminal_display, "terminal-display-fusion"),
          "initial-value terminal display should report terminal-display-fusion");
  require(std::count_if(initial_value_terminal_display.steps.begin(),
                        initial_value_terminal_display.steps.end(),
                        [](const ResolvedStep& step) { return step.opcode == 0x50; }) == 1,
          "initial-value terminal display should emit one calculator stop");
  require(std::none_of(initial_value_terminal_display.steps.begin(),
                       initial_value_terminal_display.steps.end(),
                       [](const ResolvedStep& step) { return step.comment == "show"; }),
          "initial-value terminal display should not emit the preceding show");

  const CompileResult show_read_fusion = compile_source(R"mkpro(
program ShowReadFusion {
  state {
    choice: counter 0..9 = 0
  }

  loop {
    show(choice)
    choice = read()
    halt(choice)
  }
}
)mkpro");
  require(show_read_fusion.implemented, "native compiler should fuse show(...) followed by read");
  require(show_read_fusion.diagnostics.empty(),
          "show-read fusion compile should not report diagnostics");
  require(
      std::any_of(show_read_fusion.optimizations.begin(), show_read_fusion.optimizations.end(),
                  [](const OptimizationReport& item) { return item.name == "show-read-fusion"; }),
      "show-read fusion should report the TS optimization name");
  require(std::none_of(show_read_fusion.steps.begin(), show_read_fusion.steps.end(),
                       [](const ResolvedStep& step) {
                         return step.opcode == 0x50 && step.comment == "read choice";
                       }),
          "show-read fusion should not emit a second calculator stop for read choice");

  CompileOptions terminal_loop_screen_options;
  terminal_loop_screen_options.budget = 999;
  terminal_loop_screen_options.analysis = true;
  const CompileResult terminal_loop_screen = compile_source(R"mkpro(
program TerminalLoopScreen {
  state {
    pos: counter 0..9 = 1
    score: counter 0..9 = 0
  }

  loop {
    show(pos)
    key = read()
    match key {
      1 => score_point()
      otherwise => halt(0)
    }
  }

  fn score_point() {
    score = 1
    show(pos)
  }
}
)mkpro",
                                                          terminal_loop_screen_options);
  require(terminal_loop_screen.implemented,
          "native compiler should elide terminal screens repeated by loop headers");
  require(terminal_loop_screen.diagnostics.empty(),
          "terminal loop screen compile should not report diagnostics");
  require(has_optimization(terminal_loop_screen, "terminal-loop-screen-elision"),
          "terminal loop screen elision should report the TS strategy name");
  require(std::count_if(terminal_loop_screen.steps.begin(), terminal_loop_screen.steps.end(),
                        [](const ResolvedStep& step) { return step.comment == "show pos"; }) <= 1,
          "terminal loop screen elision should remove the procedure-tail show(pos)");

  const CompileResult terminal_then_end = compile_source(R"mkpro(
program TerminalThenEnd {
  state {
    flag: flag = 0
    crash_value: packed = -999
  }

  loop {
    if flag == 1 {
      show(crash_value)
      halt(-999)
    }
    else {
      halt(1)
    }
  }
}
)mkpro");
  require(terminal_then_end.implemented,
          "native compiler should lower terminal then branch end elision");
  require(terminal_then_end.diagnostics.empty(),
          "terminal then branch compile should not report diagnostics");
  require(has_optimization(terminal_then_end, "terminal-branch-end-elision"),
          "terminal then branch should report the TS strategy name");

  const CompileResult direct_terminal_branch = compile_source(R"mkpro(
program DirectTerminalBranch {
  state {
    score: counter 0..9 = 0
    crash_value: packed = -999
  }

  loop {
    if score >= 5 {
      fail()
    }
    else {
      other()
    }
  }

  fn fail() {
    show(crash_value)
    halt(-999)
  }

  fn other() {
    if score < 2 {
      fail()
    }
    else {
      halt(2)
    }
  }
}
)mkpro");
  require(direct_terminal_branch.implemented,
          "native compiler should branch directly to reusable terminal rules");
  require(direct_terminal_branch.diagnostics.empty(),
          "direct terminal branch compile should not report diagnostics");
  require(has_optimization(direct_terminal_branch, "terminal-if-direct-branch"),
          "direct terminal branch should report the TS strategy name");

  const CompileResult late_layout_terminal_if = compile_source(R"mkpro(
program LateLayoutIfVariant {
  state {
    strength: counter -9..9 = 0
    value: counter 0..9 = 0
    fail_value: packed = -999
  }

  loop {
    if strength <= 0 {
      exhausted()
    }
    value++
    if value >= 9 {
      exhausted()
    }
    halt(value)
  }

  fn exhausted() {
    show(fail_value)
    halt(-999)
  }
}
)mkpro");
  require(late_layout_terminal_if.implemented,
          "native compiler should select aggressive terminal-if variants when they win");
  require(late_layout_terminal_if.diagnostics.empty(),
          "late-layout terminal-if compile should not report diagnostics");
  require(has_optimization(late_layout_terminal_if, "late-layout-if-variant"),
          "late-layout terminal-if should keep the TS candidate-selection strategy");
  require(has_optimization(late_layout_terminal_if, "terminal-if-direct-branch"),
          "late-layout terminal-if should report the direct branch strategy");

  CompileOptions branch_x_options;
  branch_x_options.budget = 999;
  branch_x_options.analysis = true;
  const CompileResult false_branch_x = compile_source(R"mkpro(
program FalseBranchXReuse {
  state {
    target: packed = 0
    wumpus: packed = 1
    arrows: counter 1..5 = 2
  }

  loop {
    target = read()
    if target >= 0 {
      halt(1)
    }
    else {
      shoot()
    }
  }

  fn shoot() {
    arrows--

    if target + wumpus == 0 {
      halt(2)
    }
    else {
      halt(arrows)
    }
  }
}
)mkpro",
                                                   branch_x_options);
  require(false_branch_x.implemented,
          "native compiler should preserve X facts into false branches");
  require(false_branch_x.diagnostics.empty(),
          "false-branch X reuse compile should not report diagnostics");
  require(has_optimization(false_branch_x, "x-preserving-false-branch"),
          "false-branch X reuse should report the TS strategy name");
  require(has_optimization(false_branch_x, "indirect-incdec-counter"),
          "false-branch X reuse fixture should still lower arrows through indirect inc/dec");
  require(!has_optimization(false_branch_x, "fl-unit-decrement"),
          "false-branch X reuse should not regress to the FL decrement path");

  const CompileResult fallthrough_x = compile_source(R"mkpro(
program FallthroughXReuse {
  state { energy: counter -9..9 = 0 }

  loop {
    energy = energy + 1 - read()
    if energy < 0 {
      halt(energy)
    }
    halt(0)
  }
}
)mkpro",
                                                  branch_x_options);
  require(fallthrough_x.implemented,
          "native compiler should preserve X facts into fallthrough branches");
  require(fallthrough_x.diagnostics.empty(),
          "fallthrough X reuse compile should not report diagnostics");
  require(has_optimization(fallthrough_x, "x-preserving-fallthrough-branch"),
          "fallthrough X reuse should report the TS strategy name");

  const CompileResult false_branch_zero = compile_source(R"mkpro(
program FalseBranchZeroReuse {
  state {
    value: counter 0..9 = 0
    out: counter 0..9 = 1
    sink: counter 0..99 = 0
  }

  loop {
    if value != 0 {
      sink = value + out
    }
    else {
      out = 0
    }
    halt(out + sink)
  }
}
)mkpro",
                                                        branch_x_options);
  require(false_branch_zero.implemented,
          "native compiler should reuse zero on false inequality branches");
  require(false_branch_zero.diagnostics.empty(),
          "false-branch zero reuse compile should not report diagnostics");
  require(has_optimization(false_branch_zero, "inequality-zero-false-branch"),
          "false-branch zero reuse should report the TS strategy name");
  require(has_optimization(false_branch_zero, "known-zero-reuse"),
          "false-branch zero reuse should feed the existing known-zero optimization");

  const CompileResult equality_zero = compile_source(R"mkpro(
program EqualityZeroFallthroughReuse {
  state {
    value: counter 0..9 = 0
    out: counter 0..9 = 1
  }

  loop {
    if value == 0 {
      out = 0
    }
    halt(out)
  }
}
)mkpro",
                                                   branch_x_options);
  require(equality_zero.implemented,
          "native compiler should reuse zero on equality fallthrough branches");
  require(equality_zero.diagnostics.empty(),
          "equality zero fallthrough compile should not report diagnostics");
  require(has_optimization(equality_zero, "equality-zero-fallthrough"),
          "equality zero fallthrough should report the TS strategy name");
  require(has_optimization(equality_zero, "known-zero-reuse"),
          "equality zero fallthrough should feed the existing known-zero optimization");

  const CompileResult residual_guarded_update = compile_source(R"mkpro(
program ResidualGuardedUpdate {
  state {
    room: counter 0..6 = 0
    shown: packed = 0
  }

  loop {
    if room < 6 {
      room++
      shown = room
    }
    else {
      show(room, shown)
    }
    halt(room)
  }
}
)mkpro");
  require(residual_guarded_update.implemented,
          "native compiler should reuse comparison residuals for guarded self-updates");
  require(residual_guarded_update.diagnostics.empty(),
          "residual guarded update compile should not report diagnostics");
  require(has_optimization(residual_guarded_update, "residual-guarded-update"),
          "residual guarded update should report the TS strategy name");
  require(std::any_of(residual_guarded_update.steps.begin(), residual_guarded_update.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.comment == "residual guarded update room";
                      }),
          "residual guarded update should add the correction before storing room");

  const CompileResult nested_residual_guard = compile_source(R"mkpro(
program NestedDelayedResidualGuardedUpdate {
  state {
    dynamite: counter 0..9 = 4
    blocked: packed = 7
    player: packed = 0
  }
  loop {
    if blocked != 0 {
      if dynamite >= 2 {
        player = blocked
        dynamite -= 2
        show(player)
      }
      else {
        halt(0)
      }
    }
    else {
      halt(0)
    }
  }
}
)mkpro");
  require(nested_residual_guard.implemented,
          "native compiler should share nested failure branches with residual updates");
  require(nested_residual_guard.diagnostics.empty(),
          "nested residual guard compile should not report diagnostics");
  require(has_optimization(nested_residual_guard, "nested-guard-shared-failure"),
          "nested residual guard should report the shared-failure strategy name");
  require(has_optimization(nested_residual_guard, "residual-guarded-update"),
          "nested residual guard should reuse the delayed self-update residual");
  require(std::count_if(nested_residual_guard.steps.begin(), nested_residual_guard.steps.end(),
                        [](const ResolvedStep& step) {
                          return step.comment == "set dynamite";
                        }) == 1,
          "nested residual guard should store dynamite exactly once");

  const CompileResult nested_shared_failure = compile_source(R"mkpro(
program NestedGuardSharedFailure {
  state {
    a: counter 0..9 = 1
    b: counter 0..9 = 1
    score: counter 0..9 = 0
  }

  loop {
    if a != 0 {
      if b != 0 {
        score = 1
      }
      else {
        show(0)
      }
    }
    else {
      show(0)
    }
    halt(score)
  }
}
)mkpro",
                                                     branch_x_options);
  require(nested_shared_failure.implemented,
          "native compiler should share identical nested failure branches");
  require(nested_shared_failure.diagnostics.empty(),
          "nested shared failure compile should not report diagnostics");
  require(has_optimization(nested_shared_failure, "nested-guard-shared-failure"),
          "nested shared failure should report the TS strategy name");

  const CompileResult show_read_decrement = compile_source(R"mkpro(
program ReadKeyResourceUnderflow {
  state {
    food: counter 0..9 = 2
    pos: counter 0..9 = 1
  }

  loop {
    show(pos)
    key = read()
    food--
    if food < 0 {
      loop {
      }
    }
    match key {
      1 => halt(1)
      otherwise => halt(0)
    }
  }
}
)mkpro");
  require(show_read_decrement.implemented,
          "native compiler should fuse show/read with decrement underflow before match");
  require(show_read_decrement.diagnostics.empty(),
          "show-read decrement fusion compile should not report diagnostics");
  require(std::any_of(show_read_decrement.optimizations.begin(),
                      show_read_decrement.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "show-read-decrement-underflow-fusion";
                      }),
          "show-read decrement fusion should report the TS optimization name");
  require(std::none_of(show_read_decrement.steps.begin(), show_read_decrement.steps.end(),
                       [](const ResolvedStep& step) {
                         return step.opcode == 0x50 && step.comment == "read key";
                       }),
          "show-read decrement fusion should not emit a separate read key stop");
  require(std::any_of(show_read_decrement.steps.begin(), show_read_decrement.steps.end(),
                      [](const ResolvedStep& step) { return step.comment == "restore read key"; }),
          "show-read decrement fusion should restore the read key from Y");

  CompileOptions stored_decrement_options;
  stored_decrement_options.disable_candidate_search = true;
  const CompileResult stored_show_read_decrement = compile_source(R"mkpro(
program ReadKeyNestedResourceUnderflow {
  state {
    food: counter 0..9 = 2
    pos: counter 0..9 = 1
    result: counter -9..9 = 0
  }

  loop {
    show(pos)
    key = read()
    food--
    if food < 0 {
      loop {
      }
    }
    if abs(key) == 5 {
      result = sign(key)
    }
    else {
      match key {
        1 => halt(1)
        otherwise => halt(0)
      }
    }
    halt(result)
  }
}
)mkpro",
                                                                  stored_decrement_options);
  require(stored_show_read_decrement.implemented,
          "native compiler should fuse stored show/read decrement underflow");
  require(stored_show_read_decrement.diagnostics.empty(),
          "stored show-read decrement fusion compile should not report diagnostics");
  require(std::any_of(stored_show_read_decrement.optimizations.begin(),
                      stored_show_read_decrement.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "show-read-stored-decrement-underflow-fusion";
                      }),
          "stored show-read decrement fusion should report the TS optimization name");
  require(std::any_of(stored_show_read_decrement.optimizations.begin(),
                      stored_show_read_decrement.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "current-x-unary-derivation";
                      }),
          "stored show-read decrement fusion should expose current-X unary derivation");
  require(std::any_of(stored_show_read_decrement.steps.begin(),
                      stored_show_read_decrement.steps.end(),
                      [](const ResolvedStep& step) { return step.comment == "read key"; }),
          "stored show-read decrement fusion should store the read key");
  require(std::any_of(stored_show_read_decrement.steps.begin(),
                      stored_show_read_decrement.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.comment == "restore read key" && step.mnemonic == "X↔Y";
                      }),
          "stored show-read decrement fusion should restore the key from Y");
  require(std::any_of(stored_show_read_decrement.steps.begin(),
                      stored_show_read_decrement.steps.end(),
                      [](const ResolvedStep& step) { return step.comment == "current-X abs"; }),
          "stored show-read decrement fusion should derive abs(key) from current X");

  CompileOptions recall_decrement_options;
  recall_decrement_options.recall_stored_input_after_decrement = true;
  const CompileResult recalled_show_read_decrement = compile_source(R"mkpro(
program ReadKeyNestedResourceRecallSync {
  state {
    food: counter 0..9 = 2
    pos: counter 0..9 = 1
    result: counter -9..9 = 0
  }

  loop {
    show(pos)
    key = read()
    food--
    if food < 0 {
      loop {
      }
    }
    if abs(key) == 5 {
      result = sign(key)
    }
    else {
      match key {
        1 => halt(1)
        otherwise => halt(0)
      }
    }
    halt(result)
  }
}
)mkpro",
                                                                    recall_decrement_options);
  require(recalled_show_read_decrement.implemented,
          "native compiler should restore stored read keys through recall when selected");
  require(recalled_show_read_decrement.diagnostics.empty(),
          "recall show-read decrement fusion compile should not report diagnostics");
  require(std::any_of(recalled_show_read_decrement.optimizations.begin(),
                      recalled_show_read_decrement.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "show-read-stored-decrement-recall-sync";
                      }),
          "recall show-read decrement fusion should report the TS recall-sync optimization");
  require(std::any_of(
              recalled_show_read_decrement.steps.begin(), recalled_show_read_decrement.steps.end(),
              [](const ResolvedStep& step) {
                return step.comment == "restore read key" && step.mnemonic.rfind("П->X ", 0) == 0;
              }),
          "recall show-read decrement fusion should restore the key via recall");
  require(std::none_of(recalled_show_read_decrement.steps.begin(),
                       recalled_show_read_decrement.steps.end(),
                       [](const ResolvedStep& step) {
                         return step.comment == "restore read key" && step.mnemonic == "X↔Y";
                       }),
          "recall show-read decrement fusion should not use X/Y swap for restore");

  CompileOptions indirect_decrement_options;
  indirect_decrement_options.indirect_underflow_decrement = true;
  const CompileResult indirect_show_read_decrement = compile_source(R"mkpro(
program ReadKeyNestedResourceIndirectUnderflow {
  state {
    food: counter 0..9 = 2
    pos: counter 0..9 = 1
    result: counter -9..9 = 0
  }

  loop {
    show(pos)
    key = read()
    food--
    if food < 0 {
      loop {
      }
    }
    if abs(key) == 5 {
      result = sign(key)
    }
    else {
      match key {
        1 => halt(1)
        otherwise => halt(0)
      }
    }
    halt(result)
  }
}
)mkpro",
                                                                    indirect_decrement_options);
  require(indirect_show_read_decrement.implemented,
          "native compiler should use indirect predecrement for stored show/read guards");
  require(indirect_show_read_decrement.diagnostics.empty(),
          "indirect show-read decrement fusion compile should not report diagnostics");
  require(std::any_of(indirect_show_read_decrement.optimizations.begin(),
                      indirect_show_read_decrement.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "show-read-stored-indirect-decrement-underflow";
                      }),
          "indirect show-read decrement fusion should report the TS optimization name");
  require(std::any_of(indirect_show_read_decrement.optimizations.begin(),
                      indirect_show_read_decrement.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "indirect-underflow-decrement";
                      }),
          "indirect show-read decrement fusion should report indirect-underflow-decrement");
  require(std::any_of(indirect_show_read_decrement.steps.begin(),
                      indirect_show_read_decrement.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.comment == "predecrement food" &&
                               step.mnemonic.rfind("К П->X ", 0) == 0;
                      }),
          "indirect show-read decrement fusion should use К П->X predecrement");
  require(std::none_of(indirect_show_read_decrement.steps.begin(),
                       indirect_show_read_decrement.steps.end(),
                       [](const ResolvedStep& step) { return step.comment == "set food"; }),
          "indirect show-read decrement fusion should not store food after predecrement");
  require(std::any_of(
              indirect_show_read_decrement.steps.begin(), indirect_show_read_decrement.steps.end(),
              [](const ResolvedStep& step) {
                return step.comment == "restore read key" && step.mnemonic.rfind("П->X ", 0) == 0;
              }),
          "indirect show-read decrement fusion should restore the key via recall");

  CompileOptions delayed_indirect_decrement_options;
  delayed_indirect_decrement_options.indirect_underflow_decrement = true;
  const CompileResult delayed_indirect_decrement =
      compile_source(R"mkpro(
program TerminalUnderflowIndirectDecrement {
  state {
    a: packed = 0
    b: packed = 0
    energy: counter 0..99 = 5
    selector: counter 0..9 = 0
  }

  loop {
    if selector == 7 {
      energy--
    }
    if energy <= 0 {
      halt(1)
    }
    halt(energy)
  }
}
)mkpro",
                     delayed_indirect_decrement_options);
  require(delayed_indirect_decrement.implemented,
          "native compiler should sink branch-local indirect predecrements");
  require(delayed_indirect_decrement.diagnostics.empty(),
          "delayed indirect decrement compile should not report diagnostics");
  require(std::any_of(delayed_indirect_decrement.optimizations.begin(),
                      delayed_indirect_decrement.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "delayed-indirect-underflow-decrement";
                      }),
          "delayed indirect decrement should report the TS optimization name");
  require(std::any_of(delayed_indirect_decrement.steps.begin(),
                      delayed_indirect_decrement.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.comment == "delayed predecrement energy" && step.hex == "D2";
                      }),
          "delayed indirect decrement should use R2 К П->X predecrement");
  require(std::none_of(delayed_indirect_decrement.steps.begin(),
                       delayed_indirect_decrement.steps.end(),
                       [](const ResolvedStep& step) { return step.comment == "set energy"; }),
          "delayed indirect decrement should avoid an ordinary energy store");

  const CompileResult prompted_delayed_indirect_decrement =
      compile_source(R"mkpro(
program PromptDelayedBranchUnderflow {
  state {
    a: packed = 0
    b: packed = 0
    energy: counter 0..99 = 5
    key: counter -9..9 = 0
    score: counter 0..99 = 0
  }

  loop {
    show(score)
    key = read()
    if key == 0 {
      score++
    }
    else {
      energy--
      score += 2
    }
    if energy <= 0 {
      halt(1)
    }
    halt(score)
  }
}
)mkpro",
                     delayed_indirect_decrement_options);
  require(prompted_delayed_indirect_decrement.implemented,
          "native compiler should sink delayed predecrement after an ephemeral input branch");
  require(prompted_delayed_indirect_decrement.diagnostics.empty(),
          "prompted delayed decrement compile should not report diagnostics");
  require(prompted_delayed_indirect_decrement.registers.find("key") ==
              prompted_delayed_indirect_decrement.registers.end(),
          "prompted delayed decrement should not allocate the ephemeral input key");
  require(std::any_of(
              prompted_delayed_indirect_decrement.optimizations.begin(),
              prompted_delayed_indirect_decrement.optimizations.end(),
              [](const OptimizationReport& item) { return item.name == "ephemeral-input-branch"; }),
          "prompted delayed decrement should report ephemeral-input-branch");
  require(std::any_of(prompted_delayed_indirect_decrement.optimizations.begin(),
                      prompted_delayed_indirect_decrement.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "delayed-indirect-underflow-decrement";
                      }),
          "prompted delayed decrement should report delayed-indirect-underflow-decrement");
  require(std::any_of(prompted_delayed_indirect_decrement.steps.begin(),
                      prompted_delayed_indirect_decrement.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.comment == "delayed predecrement energy" && step.hex == "D2";
                      }),
          "prompted delayed decrement should use R2 К П->X predecrement");

  const CompileResult delayed_indirect_decrement_barrier =
      compile_source(R"mkpro(
program TerminalUnderflowIndirectDecrementReadBarrier {
  state {
    a: packed = 0
    b: packed = 0
    energy: counter 0..99 = 5
    selector: counter 0..9 = 0
    score: counter 0..99 = 0
  }

  loop {
    if selector == 7 {
      energy--
    }
    score = score + energy
    if energy <= 0 {
      halt(1)
    }
    halt(score)
  }
}
)mkpro",
                     delayed_indirect_decrement_options);
  require(delayed_indirect_decrement_barrier.implemented,
          "native compiler should lower delayed decrement read barrier probe");
  require(delayed_indirect_decrement_barrier.diagnostics.empty(),
          "delayed decrement read barrier compile should not report diagnostics");
  require(std::none_of(delayed_indirect_decrement_barrier.optimizations.begin(),
                       delayed_indirect_decrement_barrier.optimizations.end(),
                       [](const OptimizationReport& item) {
                         return item.name == "delayed-indirect-underflow-decrement";
                       }),
          "reading the decremented variable before the guard should block delayed decrement");

  CompileOptions indexed_indirect_decrement_options;
  indexed_indirect_decrement_options.indirect_underflow_decrement = true;
  const CompileResult indexed_indirect_decrement =
      compile_source(R"mkpro(
program ReadKeyIndexedResourceIndirectUnderflow {
  state {
    resources: packed[1..2] = [2, 0]
    key: counter -9..9 = 0
    pos: counter 0..9 = 1
    result: counter -9..9 = 0
  }

  loop {
    show(pos)
    key = read()
    resources[1] -= 1
    if resources[1] < 0 {
      loop {
      }
    }
    if abs(key) == 5 {
      result = sign(key)
    }
    else {
      match key {
        1 => halt(1)
        otherwise => halt(0)
      }
    }
    halt(result)
  }
}
)mkpro",
                     indexed_indirect_decrement_options);
  require(indexed_indirect_decrement.implemented,
          "native compiler should use indirect predecrement for indexed stored show/read guards");
  require(indexed_indirect_decrement.diagnostics.empty(),
          "indexed indirect show-read decrement fusion compile should not report diagnostics");
  require(std::any_of(indexed_indirect_decrement.optimizations.begin(),
                      indexed_indirect_decrement.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "show-read-stored-indirect-decrement-underflow";
                      }),
          "indexed indirect show-read decrement fusion should report the TS optimization name");
  require(std::any_of(indexed_indirect_decrement.steps.begin(),
                      indexed_indirect_decrement.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.comment == "predecrement resources_1" &&
                               step.mnemonic.rfind("К П->X ", 0) == 0;
                      }),
          "indexed indirect show-read decrement fusion should predecrement the concrete bank "
          "element");
  require(std::none_of(indexed_indirect_decrement.steps.begin(),
                       indexed_indirect_decrement.steps.end(),
                       [](const ResolvedStep& step) { return step.comment == "set resources_1"; }),
          "indexed indirect show-read decrement fusion should not store the bank element after "
          "predecrement");

  const CompileResult packed_display_statement = compile_source(R"mkpro(
program PackedDisplay {
  state {
    a: counter 0..9 = 1
    b: counter 0..99 = 23
    c: counter 0..999 = 456
  }

  loop {
    show(a, b:02, c + 1)
    halt(0)
  }
}
)mkpro");
  require(packed_display_statement.implemented,
          "native compiler should lower packed multi-field display");
  require(packed_display_statement.diagnostics.empty(),
          "packed display compile should not report diagnostics");
  require(has_optimization(packed_display_statement, "display-expression-materialization"),
          "packed display expression should report display-expression-materialization");
  require(std::any_of(packed_display_statement.registers.begin(),
                      packed_display_statement.registers.end(),
                      [](const auto& entry) { return entry.first.starts_with("__display_expr_"); }),
          "packed display expression should allocate the TS display-expression scratch");
  int display_shift_count = 0;
  int display_append_count = 0;
  for (const ResolvedStep& step : packed_display_statement.steps) {
    if (step.comment == "packed display field shift")
      ++display_shift_count;
    if (step.comment == "packed display field append")
      ++display_append_count;
  }
  require(display_shift_count == 2, "three-field display should shift for each appended field");
  require(display_append_count == 2, "three-field display should append each trailing field");
  require(packed_display_statement.listing.find("display scale 100") != std::string::npos,
          "fixed-width display field should reserve decimal positions");
  require(packed_display_statement.listing.find("expr +") != std::string::npos,
          "display expression field should use generic expression lowering");

  const CompileResult display_current_x_suffix = compile_source(R"mkpro(
program DisplayCurrentXSuffixReuse {
  state {
    a: counter 0..9 = 1
    b: counter 0..99 = 23
  }

  loop {
    b = b + 1
    show(a, b)
    halt(0)
  }
}
)mkpro");
  require(display_current_x_suffix.implemented,
          "native compiler should lower packed display current-X suffix reuse");
  require(has_optimization(display_current_x_suffix, "display-current-x-suffix-reuse"),
          "packed display should reuse a suffix field already in X");
  require(has_optimization_detail(display_current_x_suffix, "display-strategy-selection",
                                  "Selected decimal-pack"),
          "ordinary packed display should report decimal-pack strategy selection");
  require(has_optimization(display_current_x_suffix, "packed-display-lowering"),
          "generic packed display lowering should report the TS optimization name");
  require(std::any_of(display_current_x_suffix.steps.begin(), display_current_x_suffix.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.comment == "packed display current field append";
                      }),
          "suffix current-X packed display should append the stack-carried current field");

  const CompileResult display_current_x_first = compile_source(R"mkpro(
program DisplayCurrentXFirstReuse {
  state {
    a: counter 0..9 = 1
    b: counter 0..99 = 23
  }

  loop {
    b = b + 1
    show(b, a)
    halt(0)
  }
}
)mkpro");
  require(display_current_x_first.implemented,
          "native compiler should lower packed display current-X first reuse");
  require(has_optimization(display_current_x_first, "display-current-x-reuse"),
          "packed display should reuse the first field already in X");

  const CompileResult display_storage_reuse = compile_source(R"mkpro(
program DisplayStorageReuse {
  state {
    screen: packed = 0
    tail: counter 0..99 = 7
  }

  loop {
    tail = tail + 1
    show(screen, tail:02)
    halt(0)
  }
}
)mkpro");
  require(display_storage_reuse.implemented,
          "native compiler should lower ready-packed display storage reuse");
  require(has_optimization(display_storage_reuse, "packed-display-storage-reuse"),
          "ready-packed display fields should be added without decimal shifting");
  require(has_optimization_detail(display_storage_reuse, "display-strategy-selection",
                                  "Selected packed-storage-reuse"),
          "ready-packed display should report packed-storage-reuse strategy selection");
  require(has_optimization(display_storage_reuse, "display-stack-reuse"),
          "ready-packed display should reorder fields to reuse the value already in X");
  require(std::any_of(display_storage_reuse.steps.begin(), display_storage_reuse.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.comment == "packed display storage append";
                      }),
          "storage reuse display should append fields with direct addition");

  const CompileResult residual_display_elision = compile_source(R"mkpro(
program ResidualGuardedUpdateFalseBranch {
  state {
    dynamite: counter 0..9 = 4
    walls: packed = 9
  }
  loop {
    if dynamite >= 2 {
      walls = walls - 1
      dynamite -= 2
    }
    else {
      show(dynamite - 2)
    }
    halt(dynamite)
  }
}
)mkpro");
  require(residual_display_elision.implemented,
          "native compiler should lower residual guarded display false branch");
  require(residual_display_elision.diagnostics.empty(),
          "residual guarded display compile should not report diagnostics");
  require(has_optimization(residual_display_elision, "branch-residual-x-reuse"),
          "residual guarded display should reuse the branch residual in X");
  require(has_optimization(residual_display_elision, "residual-display-materialization-elision"),
          "residual guarded display should keep the residual show expression inline");
  require(!has_optimization(residual_display_elision, "display-expression-materialization"),
          "residual guarded display should not materialize the inline residual expression");
  require(std::none_of(
              residual_display_elision.registers.begin(), residual_display_elision.registers.end(),
              [](const auto& entry) { return entry.first.starts_with("__display_expr_"); }),
          "residual display elision should not allocate display-expression scratch");

  const CompileResult decimal_point_display_statement = compile_source(R"mkpro(
program DecimalPointDisplay {
  state {
    room: counter 0..99 = 12
    arrows: counter 0..99 = 3
  }

  loop {
    show(room, ".", arrows:02)
    halt(0)
  }
}
)mkpro");
  require(decimal_point_display_statement.implemented,
          "native compiler should lower decimal-point display");
  require(decimal_point_display_statement.diagnostics.empty(),
          "decimal-point display compile should not report diagnostics");
  require(decimal_point_display_statement.listing.find("decimal-point display fraction") !=
              std::string::npos,
          "decimal-point display should divide the fractional fields");
  require(decimal_point_display_statement.listing.find("decimal-point display append") !=
              std::string::npos,
          "decimal-point display should append the integer field");
  require(decimal_point_display_statement.listing.find("show decimal-point display") !=
              std::string::npos,
          "decimal-point display should emit a calculator stop");

  const CompileResult packed_row_display_statement = compile_source(R"mkpro(
program PackedRowDisplay {
  state {
    floor: counter 1..4 = 2
    row: packed = bit_not(5 / 9)
  }

  loop {
    show(floor, ".", row)
    halt(0)
  }
}
)mkpro");
  require(packed_row_display_statement.implemented,
          "native compiler should lower floor packed-row displays");
  require(packed_row_display_statement.diagnostics.empty(),
          "floor packed-row display compile should not report diagnostics");
  require(packed_row_display_statement.listing.find("display packed row floor merge") !=
              std::string::npos,
          "floor packed-row display should merge the one-digit floor");
  require(packed_row_display_statement.listing.find("display packed row preserve") !=
              std::string::npos,
          "floor packed-row display should preserve the packed row body");
  require(packed_row_display_statement.listing.find("show packed row") != std::string::npos,
          "floor packed-row display should emit a calculator stop");

  const CompileResult packed_row_expression_display_statement = compile_source(R"mkpro(
program PackedRowExpressionDisplay {
  state {
    floor: counter 1..4 = 2
  }

  loop {
    show(floor, ".", bit_not(5 / 9))
    halt(0)
  }
}
)mkpro");
  require(packed_row_expression_display_statement.implemented,
          "native compiler should lower floor packed-row expression displays");
  require(packed_row_expression_display_statement.diagnostics.empty(),
          "floor packed-row expression display compile should not report diagnostics");
  require(packed_row_expression_display_statement.listing.find(
              "display packed row expression merge") != std::string::npos,
          "floor packed-row expression display should merge the computed row");
  require(packed_row_expression_display_statement.listing.find(
              "display packed row expression rotate") != std::string::npos,
          "floor packed-row expression display should preserve the computed row");
  require(packed_row_expression_display_statement.listing.find("show packed row") !=
              std::string::npos,
          "floor packed-row expression display should emit a calculator stop");

  const CompileResult mixed_display_mask_statement = compile_source(R"mkpro(
program MixedDisplayMask {
  state {
    cell: counter 0..99 = 58
    bearing: counter 0..9 = 0
  }

  loop {
    show("--", cell:02, "-", bearing)
    halt(0)
  }
}
)mkpro");
  require(mixed_display_mask_statement.implemented,
          "native compiler should lower mixed literal/source display masks");
  require(mixed_display_mask_statement.diagnostics.empty(),
          "mixed display mask compile should not report diagnostics");
  bool saw_display_mask_scratch = false;
  for (const auto& [name, _register_name] : mixed_display_mask_statement.registers) {
    if (name.rfind("__display_mask_body_", 0) == 0)
      saw_display_mask_scratch = true;
  }
  require(saw_display_mask_scratch, "mixed display mask should allocate a body scratch register");
  require(mixed_display_mask_statement.listing.find("display mask literal") != std::string::npos,
          "mixed display mask should build the literal video mask");
  require(mixed_display_mask_statement.listing.find("display mask body merge") != std::string::npos,
          "mixed display mask should merge numeric body and literal cells");
  require(mixed_display_mask_statement.listing.find("display mask leader preserve") !=
              std::string::npos,
          "mixed display mask should splice the leading display cell");
  require(mixed_display_mask_statement.listing.find("show display mask") != std::string::npos,
          "mixed display mask should emit a calculator stop");

  const CompileResult display_string_inline = compile_source(R"mkpro(
program DisplayStringClue {
  state {
    room: counter 1..20 = 1
    clue: counter 0..9 = 0
  }

  loop {
    clue = "-"
    show(room, " ", clue)
    halt(0)
  }
}
)mkpro");
  require(display_string_inline.implemented,
          "native compiler should inline display string assignments into mixed show templates");
  require(display_string_inline.diagnostics.empty(),
          "display string inline compile should not report diagnostics");
  require(has_optimization(display_string_inline, "display-string-inline"),
          "display string inline should report the TS optimization name");
  require(has_optimization(display_string_inline, "display-string-assignment-elimination"),
          "display string assignment elimination should report the TS optimization name");
  require(display_string_inline.registers.find("clue") == display_string_inline.registers.end(),
          "inlined display string state should not allocate a register");
  require(display_string_inline.listing.find("assign clue") == std::string::npos,
          "inlined display string assignment should be removed before lowering");
  require(display_string_inline.listing.find("display mask literal") != std::string::npos,
          "inlined display string should still use mixed display mask lowering");

  const CompileResult display_string_prefix = compile_source(R"mkpro(
program DisplayStringPrefix {
  state {
    prefix: counter 0..9 = 0
    clue: counter 0..9 = 3
  }

  loop {
    prefix = "L-L "
    show(prefix, clue)
    halt(0)
  }
}
)mkpro");
  require(display_string_prefix.implemented,
          "native compiler should inline leading display string prefixes");
  require(display_string_prefix.diagnostics.empty(),
          "display string prefix compile should not report diagnostics");
  require(has_optimization(display_string_prefix, "display-string-inline"),
          "display string prefix should report display-string-inline");
  require(display_string_prefix.registers.find("prefix") == display_string_prefix.registers.end(),
          "inlined display string prefix should not allocate a register");
  require(display_string_prefix.listing.find("assign prefix") == std::string::npos,
          "inlined display string prefix assignment should be removed");

  const CompileResult guarded_display_string = compile_source(R"mkpro(
program GuardedSpacedDisplayStringDefault {
  state {
    room: counter 1..9 = 1
    clue: counter 0..9 = 0
  }

  loop {
    clue = "-"
    if room < 0 {
      clue = 3
    }
    show(room, " ", clue)
    halt(0)
  }
}
)mkpro");
  require(guarded_display_string.implemented,
          "native compiler should move guarded display string defaults into display branches");
  require(guarded_display_string.diagnostics.empty(),
          "guarded display string compile should not report diagnostics");
  require(has_optimization(guarded_display_string, "display-string-guarded-show"),
          "guarded display string should report display-string-guarded-show");
  require(guarded_display_string.listing.find("display mask literal") != std::string::npos,
          "guarded display string should still lower through display mask branches");

  const CompileResult guarded_display_string_helper = compile_source(R"mkpro(
program GuardedDisplayStringHelper {
  state {
    room: counter 1..9 = 1
    clue: counter 0..9 = 0
  }

  loop {
    sense()
    show(room, " ", clue)
    halt(0)
  }

  fn sense() {
    clue = "-"
    if room < 0 {
      clue = 3
    }
  }
}
)mkpro");
  require(guarded_display_string_helper.implemented,
          "native compiler should move helper guarded display strings into display branches");
  require(guarded_display_string_helper.diagnostics.empty(),
          "helper guarded display string compile should not report diagnostics");
  require(has_optimization(guarded_display_string_helper, "display-string-guarded-show"),
          "helper guarded display string should report display-string-guarded-show");

  const CompileResult edge_space_mixed_display = compile_source(R"mkpro(
program EdgeSpaceMixedDisplay {
  state {
    clue: counter 0..9 = 3
  }

  loop {
    show(" L ", clue, " ")
    halt(0)
  }
}
)mkpro");
  require(edge_space_mixed_display.implemented,
          "native compiler should lower edge-space mixed display templates");
  require(edge_space_mixed_display.diagnostics.empty(),
          "edge-space mixed display compile should not report diagnostics");
  require(has_optimization(edge_space_mixed_display, "display-edge-whitespace-trim"),
          "edge-space mixed display should report display-edge-whitespace-trim");
  require(edge_space_mixed_display.listing.find("display mask literal") != std::string::npos,
          "trimmed edge-space mixed display should still use mask lowering");

  const CompileResult mantissa_exponent_display_statement = compile_source(R"mkpro(
program LiteralSeparatedScoreboard {
  state {
    die: counter 1..6 = 1
    score: counter 0..99 = 0
    total: counter 0..999 = 0
    roll: counter 1..99 = 1
  }

  loop {
    show(die, ".-", score:02, "-", total:03, "-", roll:02)
    halt(0)
  }
}
)mkpro");
  require(mantissa_exponent_display_statement.implemented,
          "native compiler should lower mantissa/exponent display templates");
  require(mantissa_exponent_display_statement.diagnostics.empty(),
          "mantissa/exponent display compile should not report diagnostics");
  require(mantissa_exponent_display_statement.listing.find("display template separator mask") !=
              std::string::npos,
          "mantissa/exponent display should build the separator mask");
  require(mantissa_exponent_display_statement.listing.find("display template exponent loop") !=
              std::string::npos,
          "mantissa/exponent display should emit a dynamic exponent loop");
  require(mantissa_exponent_display_statement.listing.find("display template leader preserve") !=
              std::string::npos,
          "mantissa/exponent display should splice the leading digit");
  require(mantissa_exponent_display_statement.listing.find("show display template") !=
              std::string::npos,
          "mantissa/exponent display should emit a calculator stop");

  const CompileResult literal_display_statement = compile_source(R"mkpro(
program LiteralDisplay {
  loop {
    show("8CEC6-L-")
    halt(0)
  }
}
)mkpro");
  require(literal_display_statement.implemented,
          "native compiler should lower direct literal video display");
  require(literal_display_statement.diagnostics.empty(),
          "literal display compile should not report diagnostics");
  bool saw_literal_kinv = false;
  bool saw_literal_stop = false;
  for (const ResolvedStep& step : literal_display_statement.steps) {
    if (step.opcode == 0x3a && step.comment == "display literal video bytes")
      saw_literal_kinv = true;
    if (step.opcode == 0x50 && step.comment == "show literal")
      saw_literal_stop = true;
  }
  require(saw_literal_kinv, "direct literal display should use K INV lowering");
  require(saw_literal_stop, "direct literal display should emit a calculator stop");

  const CompileResult first_splice_literal_display = compile_source(R"mkpro(
program FirstSpliceLiteralDisplay {
  loop {
    show("Е-LСГ90")
    show("---")
    halt(0)
  }
}
)mkpro");
  require(first_splice_literal_display.implemented,
          "native compiler should lower arbitrary display alphabet literals");
  require(first_splice_literal_display.diagnostics.empty(),
          "first-splice literal display compile should not report diagnostics");
  require(first_splice_literal_display.registers.find("__display_first_literal") !=
              first_splice_literal_display.registers.end(),
          "first-splice literal display should allocate a reusable scratch register");
  require(first_splice_literal_display.listing.find("display literal first digit") !=
              std::string::npos,
          "first-splice literal display should build the leading display cell");
  require(first_splice_literal_display.listing.find("display first-cell splice") !=
              std::string::npos,
          "first-splice literal display should splice the leading display cell");
  require(first_splice_literal_display.listing.find("show literal") != std::string::npos,
          "first-splice literal display should emit a calculator stop");

  const CompileResult decimal_literal_display_statement = compile_source(R"mkpro(
program DecimalLiteralDisplay {
  loop {
    show("-20")
    halt(0)
  }
}
)mkpro");
  require(decimal_literal_display_statement.implemented,
          "native compiler should lower decimal string display literal");
  require(decimal_literal_display_statement.diagnostics.empty(),
          "decimal literal display compile should not report diagnostics");
  require(decimal_literal_display_statement.listing.find("negative number") != std::string::npos,
          "negative decimal literal display should preserve the sign");

  const CompileResult const_statement = compile_source(R"mkpro(
program ConstValues {
  const LIMIT = 3
  const DOUBLE = LIMIT * 2

  state {
    total: counter 0..9 = 0
  }

  loop {
    total = DOUBLE
    if total >= LIMIT {
      preview(DOUBLE)
    }
    halt(total)
  }
}
)mkpro");
  require(const_statement.implemented, "native compiler should inline numeric const values");
  require(const_statement.diagnostics.empty(), "const compile should not report diagnostics");
  require(const_statement.registers.find("LIMIT") == const_statement.registers.end(),
          "const LIMIT should not allocate a register");
  require(const_statement.registers.find("DOUBLE") == const_statement.registers.end(),
          "const DOUBLE should not allocate a register");
  require(const_statement.listing.find("const DOUBLE") != std::string::npos,
          "const use should appear as an inlined value");
  require(const_statement.listing.find("false branch for >=") != std::string::npos,
          "const value should lower inside generic conditions");

  CompileOptions suppress_square_options;
  suppress_square_options.budget = 999;
  suppress_square_options.analysis = true;
  suppress_square_options.suppress_constant_preloads.insert("10000");
  const CompileResult suppress_square = compile_source(R"mkpro(
program ConstantSquareSynthesis {
  state {
    x: packed = 0
  }

  loop {
    x = read()
    x = x + 100
    x = x + 10000
    halt(x)
  }
}
)mkpro",
                                                       suppress_square_options);
  require(suppress_square.implemented,
          "native compiler should compile suppressed square constant synthesis");
  require(std::any_of(suppress_square.preloads.begin(), suppress_square.preloads.end(),
                      [](const PreloadReport& preload) { return preload.value == "100"; }),
          "suppressed square synthesis should keep 100 as a preload");
  require(std::any_of(suppress_square.steps.begin(), suppress_square.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x22 && step.comment.has_value() &&
                               *step.comment == "constant 10000";
                      }),
          "suppressed square synthesis should build 10000 with F x^2");
  require(std::any_of(suppress_square.optimizations.begin(), suppress_square.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "constant-synthesis" &&
                               item.detail.find("Built constant 10000") != std::string::npos;
                      }),
          "suppressed square synthesis should report constant-synthesis");

  CompileOptions suppress_pow10_options;
  suppress_pow10_options.budget = 999;
  suppress_pow10_options.analysis = true;
  suppress_pow10_options.suppress_constant_preloads.insert("10000");
  const CompileResult suppress_pow10 = compile_source(R"mkpro(
program ConstantPow10Synthesis {
  state {
    x: packed = 0
  }

  loop {
    x = read()
    x = x * pow10(4)
    halt(x)
  }
}
)mkpro",
                                                      suppress_pow10_options);
  require(suppress_pow10.implemented,
          "native compiler should compile suppressed pow10 constant synthesis");
  require(std::any_of(suppress_pow10.steps.begin(), suppress_pow10.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0x15 && step.comment.has_value() &&
                               *step.comment == "constant 10000";
                      }),
          "suppressed pow10 synthesis should build 10000 with F 10^x");
  require(std::any_of(suppress_pow10.optimizations.begin(), suppress_pow10.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "constant-synthesis" &&
                               item.detail.find("F 10^x") != std::string::npos;
                      }),
          "suppressed pow10 synthesis should report constant-synthesis");

  CompileOptions trig_options;
  trig_options.budget = 999;
  trig_options.analysis = true;
  const CompileResult guarded_trig = compile_source(R"mkpro(
program GrdTrigConstants {
  requires angle_mode(grd)

  loop {
    halt(acos(0) + cos(100))
  }
}
)mkpro",
                                                    trig_options);
  require(guarded_trig.implemented, "GRD guarded trig constant program should compile");
  require(has_optimization(guarded_trig, "grd-angle-mode-assumption"),
          "GRD guarded trig constant program should report grd-angle-mode-assumption");
  require(std::none_of(guarded_trig.steps.begin(), guarded_trig.steps.end(),
                       [](const ResolvedStep& step) {
                         return step.mnemonic.find("cos") != std::string::npos;
                       }),
          "GRD guarded trig constants should fold away cos/acos opcodes");

  const CompileResult plain_trig = compile_source(R"mkpro(
program PlainTrigConstants {
  loop {
    halt(acos(0))
  }
}
)mkpro",
                                                  trig_options);
  require(plain_trig.implemented, "plain trig constant program should compile");
  require(!has_optimization(plain_trig, "grd-angle-mode-assumption"),
          "plain trig constant program must not assume GRD angle mode");
  require(std::any_of(plain_trig.steps.begin(), plain_trig.steps.end(),
                      [](const ResolvedStep& step) { return step.mnemonic == "F cos^-1"; }),
          "plain trig constant program should still emit F cos^-1");

  const CompileResult const_assignment = compile_source(R"mkpro(
program BadConstAssignment {
  const LIMIT = 3

  loop {
    LIMIT = 4
  }
}
)mkpro");
  require(!const_assignment.implemented, "native compiler should reject assignment to const");
  require(!const_assignment.diagnostics.empty(), "const assignment should report a diagnostic");
  require(const_assignment.diagnostics.at(0).message.find("Cannot assign to const") !=
              std::string::npos,
          "const assignment diagnostic should mention const assignment");

  const CompileResult decimal_series = compile_source(R"mkpro(
program E94Digits {
  digits = 94
  n = 65
  e = 1

  while n >= 1 {
    e = 1 + e / n
    n--
  }

  halt(e)
}
)mkpro");
  require(decimal_series.implemented,
          "native compiler should lower the verified decimal recurrence shape");
  require(decimal_series.diagnostics.empty(),
          "verified decimal recurrence should not report diagnostics");
  require(decimal_series.hex.find("00: 52 06 05 23 40 0D B0 60") != std::string::npos,
          "verified decimal recurrence should emit the TS verified listing");
  require(decimal_series.listing.find("decimal recurrence setup") != std::string::npos,
          "verified decimal recurrence should carry lowering comments");

  const CompileResult unverified_decimal_series = compile_source(R"mkpro(
program E64Digits {
  digits = 94
  n = 64
  e = 1

  while n >= 1 {
    e = 1 + e / n
    n--
  }

  halt(e)
}
)mkpro");
  require(!unverified_decimal_series.implemented,
          "native compiler should reject unverified decimal recurrence pairs");
  require(!unverified_decimal_series.diagnostics.empty(),
          "unverified decimal recurrence should report diagnostics");
  require(unverified_decimal_series.diagnostics.at(0).message.find(
              "No verified decimal recurrence listing for 94-digit precision with counter 64") !=
              std::string::npos,
          "unverified decimal recurrence diagnostic should match the TS contract");

  const CompileResult unsupported = compile_source(R"mkpro(
program Unsupported {
  loop {
    show("HELLO")
  }
}
)mkpro");
  require(!unsupported.implemented, "unsupported lowering should fail explicitly");
  require(!unsupported.diagnostics.empty(), "unsupported lowering should report diagnostics");
  require(unsupported.diagnostics.at(0).code == "native-unsupported",
          "unsupported lowering diagnostic code mismatch");
}

} // namespace mkpro::tests
